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
	m_enemy.assign( m_cols * m_rows, 0.0f );
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
void AIInfluenceMap::stamp( std::vector<Real> &layer, Real x, Real y, Real z, Real range, Real power )
{
	if( m_cols <= 0 || power <= 0.0f || range <= 0.0f )
		return;

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
				layer[ index ] += power;
		}
	}
}

void AIInfluenceMap::stampEnemy( Real x, Real y, Real z, Real range, Real power )
{
	stamp( m_enemy, x, y, z, range, power );
}

void AIInfluenceMap::stampFriend( Real x, Real y, Real z, Real range, Real power )
{
	stamp( m_friend, x, y, z, range, power );
}

Real AIInfluenceMap::enemyAt( Real x, Real y ) const
{
	const Int index = cellIndexAt( x, y );
	return (index < 0) ? 0.0f : m_enemy[ index ];
}

Real AIInfluenceMap::friendAt( Real x, Real y ) const
{
	const Int index = cellIndexAt( x, y );
	return (index < 0) ? 0.0f : m_friend[ index ];
}

void AIInfluenceMap::xfer( Xfer *xfer )
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
		m_enemy.assign( cells, 0.0f );
		m_friend.assign( cells, 0.0f );
		m_height.assign( cells, 0.0f );
	}
	for( size_t i = 0; i < cells; ++i )
	{
		xfer->xferReal( &m_enemy[ i ] );
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
