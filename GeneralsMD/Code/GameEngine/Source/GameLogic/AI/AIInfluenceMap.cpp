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

#include <algorithm>

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
	m_static.assign( m_cols * m_rows * AITHREAT_COUNT, 0.0f );
	m_friend.assign( m_cols * m_rows * AITHREAT_COUNT, 0.0f );
	m_value.assign( m_cols * m_rows, 0.0f );
	m_hazard.assign( m_cols * m_rows, 0.0f );
	m_height.assign( m_cols * m_rows, 0.0f );
	m_built = FALSE;
}

void AIInfluenceMap::clear()
{
	m_enemy.assign( m_enemy.size(), 0.0f );
	m_static.assign( m_static.size(), 0.0f );
	m_friend.assign( m_friend.size(), 0.0f );
	m_value.assign( m_value.size(), 0.0f );
	m_hazard.assign( m_hazard.size(), 0.0f );
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
	* the range out, and each cell inside it is cut back to the reach its own height allows.  A cell nearer
	* than minRange, flat, is not visited; one past edgeShare of the reach there is visited as the edge. */
template <class Visit>
void AIInfluenceMap::visitReach( Real x, Real y, Real z, Real range, Real minRange, Real edgeShare, Visit &visit ) const
{
	const Real farthest = range + Weapon_highGroundRangeBonus( range, FLT_MAX );
	const Int colLo = max<Int>( 0, REAL_TO_INT_FLOOR( (x - farthest - m_originX) / m_cellSize ) );
	const Int colHi = min<Int>( m_cols - 1, REAL_TO_INT_FLOOR( (x + farthest - m_originX) / m_cellSize ) );
	const Int rowLo = max<Int>( 0, REAL_TO_INT_FLOOR( (y - farthest - m_originY) / m_cellSize ) );
	const Int rowHi = min<Int>( m_rows - 1, REAL_TO_INT_FLOOR( (y + farthest - m_originY) / m_cellSize ) );
	const Real rangeSqr = sqr( range ), farthestSqr = sqr( farthest ), minSqr = sqr( minRange );
	const Bool banded = edgeShare < 1.0f;
	for( Int row = rowLo; row <= rowHi; ++row )
	{
		const Real cy = m_originY + (row + 0.5f) * m_cellSize;
		for( Int col = colLo; col <= colHi; ++col )
		{
			const Real cx = m_originX + (col + 0.5f) * m_cellSize;
			const Real distSqr = sqr( cx - x ) + sqr( cy - y );
			// the bonus is never below 0 nor past the cap, so the height only decides the ring between
			if( distSqr > farthestSqr || distSqr < minSqr )
				continue;
			const Int index = row * m_cols + col;
			if( !banded )
			{
				if( distSqr <= rangeSqr || distSqr <= sqr( range + Weapon_highGroundRangeBonus( range, z - m_height[ index ] ) ) )
					visit( index, FALSE );
				continue;
			}
			const Real reach = range + Weapon_highGroundRangeBonus( range, z - m_height[ index ] );
			if( distSqr <= rangeSqr || distSqr <= sqr( reach ) )
				visit( index, distSqr > sqr( edgeShare * reach ) );
		}
	}
}

struct CollectCell
{
	std::vector<Int> *cells;
	void operator()( Int index, Bool ) { cells->push_back( index ); }
};

/** The full cells into one list and the edge into another. */
struct CollectBand
{
	std::vector<Int> *cells;
	std::vector<Int> *edge;
	void operator()( Int index, Bool isEdge ) { (isEdge ? edge : cells)->push_back( index ); }
};

/** Adds to one slot of every cell: stride AITHREAT_COUNT and the slot of a layer by class. */
struct AddPower
{
	Real *layer;
	Int stride;
	Int slot;
	Real power;
	void operator()( Int index, Bool ) { layer[ index * stride + slot ] += power; }
};

/** The vehicle and infantry slots of every cell a ground gun reaches. */
struct AddGroundPower
{
	Real *layer;
	Real vehicle;
	Real infantry;
	void operator()( Int index, Bool )
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
		visitReach( x, y, z, groundRange, 0.0f, 1.0f, add );
	}
	if( airRange > 0.0f && power[ AITHREAT_AIR ] > 0.0f )
	{
		AddPower add = { &m_enemy[ 0 ], AITHREAT_COUNT, AITHREAT_AIR, power[ AITHREAT_AIR ] };
		visitReach( x, y, z, airRange, 0.0f, 1.0f, add );
	}
}

void AIInfluenceMap::cellsReached( Real x, Real y, Real z, Real range, std::vector<Int> &cells ) const
{
	if( m_cols <= 0 || range <= 0.0f )
		return;
	CollectCell collect = { &cells };
	visitReach( x, y, z, range, 0.0f, 1.0f, collect );
}

void AIInfluenceMap::cellsBanded( Real x, Real y, Real z, Real range, Real minRange, Real edgeShare,
																	std::vector<Int> &cells, std::vector<Int> &scratch, Int *edgeCount ) const
{
	*edgeCount = 0;
	if( m_cols <= 0 || range <= 0.0f )
		return;
	scratch.clear();
	CollectBand collect = { &cells, &scratch };
	visitReach( x, y, z, range, minRange, edgeShare, collect );
	*edgeCount = (Int)scratch.size();
	cells.insert( cells.end(), scratch.begin(), scratch.end() );
}

void AIInfluenceMap::cellsInDisc( Real x, Real y, Real radius, std::vector<Int> &cells ) const
{
	if( m_cols <= 0 || radius <= 0.0f )
		return;
	const Int colLo = max<Int>( 0, REAL_TO_INT_FLOOR( (x - radius - m_originX) / m_cellSize ) );
	const Int colHi = min<Int>( m_cols - 1, REAL_TO_INT_FLOOR( (x + radius - m_originX) / m_cellSize ) );
	const Int rowLo = max<Int>( 0, REAL_TO_INT_FLOOR( (y - radius - m_originY) / m_cellSize ) );
	const Int rowHi = min<Int>( m_rows - 1, REAL_TO_INT_FLOOR( (y + radius - m_originY) / m_cellSize ) );
	const Real radiusSqr = sqr( radius );
	const size_t start = cells.size();
	for( Int row = rowLo; row <= rowHi; ++row )
	{
		const Real cy = m_originY + (row + 0.5f) * m_cellSize;
		for( Int col = colLo; col <= colHi; ++col )
		{
			const Real cx = m_originX + (col + 0.5f) * m_cellSize;
			if( sqr( cx - x ) + sqr( cy - y ) <= radiusSqr )
				cells.push_back( row * m_cols + col );
		}
	}
	// a worth or a hazard smaller than a cell still marks the cell it stands in
	const Int own = cellIndexAt( x, y );
	if( own >= 0 && std::find( cells.begin() + start, cells.end(), own ) == cells.end() )
		cells.push_back( own );
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

/** The vehicle and infantry slots of a layer by class, then its air slot. */
static void addGround( std::vector<Real> &layer, const Int *cells, Int count, Real vehicle, Real infantry )
{
	for( Int i = 0; i < count; ++i )
	{
		Real *cell = &layer[ cells[ i ] * AITHREAT_COUNT ];
		cell[ AITHREAT_VEHICLE ] += vehicle;
		cell[ AITHREAT_INFANTRY ] += infantry;
	}
}

static void addAir( std::vector<Real> &layer, const Int *cells, Int count, Real air )
{
	for( Int i = 0; i < count; ++i )
		layer[ cells[ i ] * AITHREAT_COUNT + AITHREAT_AIR ] += air;
}

static void addFlat( std::vector<Real> &layer, const Int *cells, Int count, Real figure )
{
	for( Int i = 0; i < count; ++i )
		layer[ cells[ i ] ] += figure;
}

void AIInfluenceMap::addStatic( const Int *cells, Int count, Real vehicle, Real infantry ) { addGround( m_static, cells, count, vehicle, infantry ); }
void AIInfluenceMap::addStaticAir( const Int *cells, Int count, Real air ) { addAir( m_static, cells, count, air ); }
void AIInfluenceMap::addFriend( const Int *cells, Int count, Real vehicle, Real infantry ) { addGround( m_friend, cells, count, vehicle, infantry ); }
void AIInfluenceMap::addFriendAir( const Int *cells, Int count, Real air ) { addAir( m_friend, cells, count, air ); }
void AIInfluenceMap::addValue( const Int *cells, Int count, Real value ) { addFlat( m_value, cells, count, value ); }
void AIInfluenceMap::addHazard( const Int *cells, Int count, Real hazard ) { addFlat( m_hazard, cells, count, hazard ); }

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

/** One cell of a layer by class read with mix, the sum enemyAt makes. */
static Real readClasses( const Real *cell, const AIThreatMix &mix )
{
	return mix.w[ AITHREAT_VEHICLE ] * cell[ AITHREAT_VEHICLE ] + mix.w[ AITHREAT_INFANTRY ] * cell[ AITHREAT_INFANTRY ] +
		mix.w[ AITHREAT_AIR ] * cell[ AITHREAT_AIR ];
}

Real AIInfluenceMap::staticAt( Real x, Real y, const AIThreatMix &mix ) const
{
	const Int index = cellIndexAt( x, y );
	return (index < 0) ? 0.0f : readClasses( &m_static[ index * AITHREAT_COUNT ], mix );
}

/** A still gun counts this much again on a route. */
static const Real STATIC_ROUTE_EXTRA = 1.0f;

Real AIInfluenceMap::routeAt( Real x, Real y, const AIThreatMix &mix ) const
{
	const Int index = cellIndexAt( x, y );
	if( index < 0 )
		return 0.0f;
	const Real enemy = readClasses( &m_enemy[ index * AITHREAT_COUNT ], mix );
	const Real still = readClasses( &m_static[ index * AITHREAT_COUNT ], mix );
	// a field burns what walks through it; an aircraft over it is not touched
	const Real walks = mix.w[ AITHREAT_VEHICLE ] + mix.w[ AITHREAT_INFANTRY ];
	return enemy + STATIC_ROUTE_EXTRA * still + walks * m_hazard[ index ];
}

Real AIInfluenceMap::friendAt( Real x, Real y, const AIThreatMix &mix ) const
{
	const Int index = cellIndexAt( x, y );
	return (index < 0) ? 0.0f : readClasses( &m_friend[ index * AITHREAT_COUNT ], mix );
}

Bool AIInfluenceMap::heldAt( Real x, Real y, const AIThreatMix &friendMix, const AIThreatMix &enemyMix ) const
{
	return enemyAt( x, y, enemyMix ) <= friendAt( x, y, friendMix );
}

Real AIInfluenceMap::valueAt( Real x, Real y ) const
{
	const Int index = cellIndexAt( x, y );
	return (index < 0) ? 0.0f : m_value[ index ];
}

Real AIInfluenceMap::hazardAt( Real x, Real y ) const
{
	const Int index = cellIndexAt( x, y );
	return (index < 0) ? 0.0f : m_hazard[ index ];
}

void AIInfluence_projectedCentre( Real x, Real y, Real velX, Real velY, Real leadFrames, Real maxLead, Real *outX, Real *outY )
{
	Real dx = velX * leadFrames;
	Real dy = velY * leadFrames;
	const Real lead = (Real)sqrt( dx * dx + dy * dy );
	if( lead > maxLead && lead > 0.0f )
	{
		dx *= maxLead / lead;
		dy *= maxLead / lead;
	}
	*outX = x + dx;
	*outY = y + dy;
}

Real AIInfluence_memoryShare( UnsignedInt age, UnsignedInt fadeFrames )
{
	if( fadeFrames == 0 || age >= fadeFrames )
		return 0.0f;
	return INT_TO_REAL( (Int)(fadeFrames - age) ) / INT_TO_REAL( (Int)fadeFrames );
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
		m_static.assign( cells * AITHREAT_COUNT, 0.0f );
		m_friend.assign( cells * AITHREAT_COUNT, 0.0f );
		m_value.assign( cells, 0.0f );
		m_hazard.assign( cells, 0.0f );
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
		Real *friends = &m_friend[ i * AITHREAT_COUNT ];
		if( version < 40 )
		{
			// one figure, read as the ground mix reads two slots of it, or as the cost mix reads the first
			xfer->xferReal( &friends[ AITHREAT_VEHICLE ] );
			friends[ AITHREAT_INFANTRY ] = friends[ AITHREAT_VEHICLE ];
		}
		else
		{
			Real *still = &m_static[ i * AITHREAT_COUNT ];
			for( Int c = 0; c < AITHREAT_COUNT; ++c )
				xfer->xferReal( &still[ c ] );
			for( Int c = 0; c < AITHREAT_COUNT; ++c )
				xfer->xferReal( &friends[ c ] );
			xfer->xferReal( &m_value[ i ] );
			xfer->xferReal( &m_hazard[ i ] );
		}
		xfer->xferReal( &m_height[ i ] );
	}
}

AILossMap::AILossMap() :
	m_originX( 0.0f ), m_originY( 0.0f ), m_cellSize( 1.0f ), m_cols( 0 ), m_rows( 0 )
{
}

void AILossMap::reset( Real originX, Real originY, Real width, Real height, Real cellSize )
{
	m_originX = originX;
	m_originY = originY;
	m_cellSize = cellSize;
	m_cols = (width > 0.0f) ? REAL_TO_INT_CEIL( width / cellSize ) : 0;
	m_rows = (height > 0.0f) ? REAL_TO_INT_CEIL( height / cellSize ) : 0;
	m_loss.assign( m_cols * m_rows, 0.0f );
}

void AILossMap::add( Real x, Real y, Real power )
{
	const Int col = REAL_TO_INT_FLOOR( (x - m_originX) / m_cellSize );
	const Int row = REAL_TO_INT_FLOOR( (y - m_originY) / m_cellSize );
	if( col < 0 || row < 0 || col >= m_cols || row >= m_rows || power <= 0.0f )
		return;
	m_loss[ row * m_cols + col ] += power;
}

void AILossMap::cellsWithin( Real x, Real y, Real radius, std::vector<Int> &cells ) const
{
	const Int colLo = max<Int>( 0, REAL_TO_INT_FLOOR( (x - radius - m_originX) / m_cellSize ) );
	const Int colHi = min<Int>( m_cols - 1, REAL_TO_INT_FLOOR( (x + radius - m_originX) / m_cellSize ) );
	const Int rowLo = max<Int>( 0, REAL_TO_INT_FLOOR( (y - radius - m_originY) / m_cellSize ) );
	const Int rowHi = min<Int>( m_rows - 1, REAL_TO_INT_FLOOR( (y + radius - m_originY) / m_cellSize ) );
	const Real radiusSqr = sqr( radius );
	for( Int row = rowLo; row <= rowHi; ++row )
	{
		const Real cy = m_originY + (row + 0.5f) * m_cellSize;
		for( Int col = colLo; col <= colHi; ++col )
		{
			const Real cx = m_originX + (col + 0.5f) * m_cellSize;
			if( sqr( cx - x ) + sqr( cy - y ) <= radiusSqr )
				cells.push_back( row * m_cols + col );
		}
	}
	// the cell it stands in, however small the radius
	const Int col = REAL_TO_INT_FLOOR( (x - m_originX) / m_cellSize );
	const Int row = REAL_TO_INT_FLOOR( (y - m_originY) / m_cellSize );
	if( col >= 0 && row >= 0 && col < m_cols && row < m_rows )
		cells.push_back( row * m_cols + col );
}

Real AILossMap::lossAround( const Real *xs, const Real *ys, Int count, Real radius, std::vector<Int> &scratch ) const
{
	if( m_cols <= 0 || count <= 0 )
		return 0.0f;
	scratch.clear();
	for( Int i = 0; i < count; ++i )
		cellsWithin( xs[ i ], ys[ i ], radius, scratch );
	std::sort( scratch.begin(), scratch.end() );
	scratch.erase( std::unique( scratch.begin(), scratch.end() ), scratch.end() );
	Real sum = 0.0f;
	for( size_t i = 0; i < scratch.size(); ++i )
		sum += m_loss[ scratch[ i ] ];
	return sum;
}

Real AILossMap::lossAround( Real x, Real y, Real radius ) const
{
	std::vector<Int> scratch;
	return lossAround( &x, &y, 1, radius, scratch );
}

void AILossMap::fade( Real kept, Real least )
{
	for( size_t i = 0; i < m_loss.size(); ++i )
	{
		if( m_loss[ i ] <= 0.0f )
			continue;
		m_loss[ i ] *= kept;
		if( m_loss[ i ] < least )
			m_loss[ i ] = 0.0f;
	}
}

void AILossMap::xfer( Xfer *xfer )
{
	xfer->xferReal( &m_originX );
	xfer->xferReal( &m_originY );
	xfer->xferReal( &m_cellSize );
	xfer->xferInt( &m_cols );
	xfer->xferInt( &m_rows );
	const size_t cells = (size_t)m_cols * (size_t)m_rows;
	if( xfer->getXferMode() == XFER_LOAD )
		m_loss.assign( cells, 0.0f );
	for( size_t i = 0; i < cells; ++i )
		xfer->xferReal( &m_loss[ i ] );
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
