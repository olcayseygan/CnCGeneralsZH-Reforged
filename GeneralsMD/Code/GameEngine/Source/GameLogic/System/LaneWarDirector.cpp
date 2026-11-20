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

// FILE: LaneWarDirector.cpp //////////////////////////////////////////////////////////////////////
//
// The Lane War mode.  See LaneWarDirector.h for what it does, and docs/plans/LANE-MODE.md in the
// wrapper for why.  Everything here is logic state: time is the logic frame, a wave is taken from
// the plot in ObjectID order, purchases arrive only through TheCommandList (or the scenario harness,
// which calls the same entry point inside the logic frame), and nothing reads the client.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LaneWarDirector.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

static LaneWarDirector theLaneWarDirector;
LaneWarDirector *TheLaneWarDirector = &theLaneWarDirector;

static const char *const TEAM_NAME[ LANEWAR_TEAMS ] = { "A", "B" };

//-------------------------------------------------------------------------------------------------
Int LaneWar_seatForTick( const Bool occupied[ LANEWAR_SEATS ], UnsignedInt tick )
{
	Int count = 0;
	for( Int s = 0; s < LANEWAR_SEATS; ++s )
		if( occupied[ s ] )
			++count;
	if( count == 0 )
		return -1;

	Int turn = (Int)( tick % (UnsignedInt)count );
	for( Int s = 0; s < LANEWAR_SEATS; ++s )
	{
		if( !occupied[ s ] )
			continue;
		if( turn == 0 )
			return s;
		--turn;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
/* The holding rule (the doc's open item): on each spawn tick, count every live wave copy whose
	 position lies past the centre line on the enemy's side of it, and the team with more holds the
	 middle until the next tick.  A tie holds nothing.  No time in place is asked for: the tick is
	 30 wall seconds apart already. */
Int LaneWar_middleHolder( Int pastLineA, Int pastLineB )
{
	if( pastLineA > pastLineB )
		return 0;
	if( pastLineB > pastLineA )
		return 1;
	return -1;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt LaneWar_income( Bool holdsMiddle )
{
	if( !holdsMiddle )
		return LANEWAR_INCOME;
	return LANEWAR_INCOME + LANEWAR_INCOME * LANEWAR_MIDDLE_BONUS_PERCENT / 100;
}

//-------------------------------------------------------------------------------------------------
/// the footprint's widest extent, rounded up to whole slots; a Ranger takes one, a Crusader two
Int LaneWar_slotsFor( Real radius )
{
	const Int slots = (Int)ceil( 2.0f * radius / (Real)LANEWAR_SLOT_SIZE );
	return slots < 1 ? 1 : slots;
}

//-------------------------------------------------------------------------------------------------
LaneWarDirector::LaneWarDirector( void )
{
	reset();
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::reset( void )
{
	m_active = FALSE;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			m_plotLo[ t ][ s ].x = m_plotLo[ t ][ s ].y = 0.0f;
			m_plotHi[ t ][ s ] = m_plotLo[ t ][ s ];
			m_gridCols[ t ][ s ] = m_gridRows[ t ][ s ] = 0;
			m_plotAnchor[ t ][ s ].zero();
			m_seatPlayer[ t ][ s ] = -1;
		}
		m_spawn[ t ].zero();
		m_fortress[ t ] = INVALID_ID;
		m_tower[ t ][ 0 ] = m_tower[ t ][ 1 ] = INVALID_ID;
		m_fortressDown[ t ] = FALSE;
	}
	m_middle.zero();
	m_nextTickFrame = 0;
	m_tickCount = 0;
	m_copies.clear();
	m_plotUnits.clear();
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::newMap( Bool loadingSaveGame )
{
	reset();

	// the map's own names; a map without every one of them is not a lane map
	const PolygonTrigger *middle = TheTerrainLogic->getTriggerAreaByName( "LaneWar_MiddleArea" );
	if( middle == NULL )
		return;

	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		AsciiString name;
		name.format( "LaneWar_Spawn%s", TEAM_NAME[ t ] );
		const Waypoint *spawn = TheTerrainLogic->getWaypointByName( name );
		if( spawn == NULL )
		{
			DEBUG_LOG(( "LANEWAR: the map has LaneWar_MiddleArea but no %s, so the mode stays off\n", name.str() ));
			return;
		}
		m_spawn[ t ] = *spawn->getLocation();

		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			name.format( "LaneWar_Build%s%d", TEAM_NAME[ t ], s + 1 );
			const PolygonTrigger *plot = TheTerrainLogic->getTriggerAreaByName( name );
			if( plot == NULL || plot->getNumPoints() < 3 )
			{
				DEBUG_LOG(( "LANEWAR: the map has LaneWar_MiddleArea but no %s, so the mode stays off\n", name.str() ));
				reset();
				return;
			}
			ICoord3D lo = *plot->getPoint( 0 ), hi = lo;
			for( Int p = 1; p < plot->getNumPoints(); ++p )
			{
				const ICoord3D *pt = plot->getPoint( p );
				lo.x = min( lo.x, pt->x );
				lo.y = min( lo.y, pt->y );
				hi.x = max( hi.x, pt->x );
				hi.y = max( hi.y, pt->y );
			}
			m_plotLo[ t ][ s ].x = (Real)lo.x;
			m_plotLo[ t ][ s ].y = (Real)lo.y;
			m_plotHi[ t ][ s ].x = (Real)hi.x;
			m_plotHi[ t ][ s ].y = (Real)hi.y;
		}
	}

	/* The plot's grid, whole slots from its front left corner: a plot stands behind its end of the
		 lane, so its edge towards the lane, its front, faces the enemy end, and the front runs across y.
		 The anchor a wave copy is laid out round is the grid's middle. */
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			const Coord2D &lo = m_plotLo[ t ][ s ], &hi = m_plotHi[ t ][ s ];
			m_gridCols[ t ][ s ] = (Int)( hi.y - lo.y ) / LANEWAR_SLOT_SIZE;
			m_gridRows[ t ][ s ] = (Int)( hi.x - lo.x ) / LANEWAR_SLOT_SIZE;
			const Real front = towardEnemy( t );
			const Coord2D first = LaneWar_slotPlace( lo.x, lo.y, hi.x, hi.y, front, 0, 0, 1 );
			const Coord2D last = LaneWar_slotPlace( lo.x, lo.y, hi.x, hi.y, front, m_gridCols[ t ][ s ] - 1, m_gridRows[ t ][ s ] - 1, 1 );
			m_plotAnchor[ t ][ s ].x = 0.5f * ( first.x + last.x );
			m_plotAnchor[ t ][ s ].y = 0.5f * ( first.y + last.y );
			m_plotAnchor[ t ][ s ].z = TheTerrainLogic->getGroundHeight( m_plotAnchor[ t ][ s ].x, m_plotAnchor[ t ][ s ].y );
		}
	middle->getCenterPoint( &m_middle );
	m_active = TRUE;

	// seats by start position: start 1 is team A's seat 1, start 2 team B's seat 1, start 3 A's seat 2
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		const Int start = player != NULL && player->isPlayableSide() ? player->getMpStartIndex() : -1;
		if( start >= 0 && start < LANEWAR_TEAMS * LANEWAR_SEATS )
			m_seatPlayer[ LaneWar_teamOfStart( start ) ][ LaneWar_seatOfStart( start ) ] = i;
	}

	m_nextTickFrame = TheGameLogic->getFrame() + LANEWAR_TICK_FRAMES;

	if( !loadingSaveGame )
	{
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			for( Int t = 0; t < LANEWAR_TEAMS; ++t )
			{
				AsciiString name;
				name.format( "LaneWar_Fortress%s", TEAM_NAME[ t ] );
				if( obj->getName() == name )
					m_fortress[ t ] = obj->getID();
				for( Int k = 0; k < 2; ++k )
				{
					name.format( "LaneWar_Tower%s%d", TEAM_NAME[ t ], k + 1 );
					if( obj->getName() == name )
						m_tower[ t ][ k ] = obj->getID();
				}
			}

		// the map, not the lobby, makes the teams: both ends ally inside and fight across
		for( Int t = 0; t < LANEWAR_TEAMS; ++t )
			for( Int s = 0; s < LANEWAR_SEATS; ++s )
			{
				if( m_seatPlayer[ t ][ s ] < 0 )
					continue;
				Player *player = ThePlayerList->getNthPlayer( m_seatPlayer[ t ][ s ] );
				player->getMoney()->init();
				player->getMoney()->deposit( LANEWAR_START_MONEY, FALSE );

				for( Int t2 = 0; t2 < LANEWAR_TEAMS; ++t2 )
					for( Int s2 = 0; s2 < LANEWAR_SEATS; ++s2 )
					{
						if( m_seatPlayer[ t2 ][ s2 ] < 0 || ( t2 == t && s2 == s ) )
							continue;
						player->setPlayerRelationship( ThePlayerList->getNthPlayer( m_seatPlayer[ t2 ][ s2 ] ),
																					 t2 == t ? ALLIES : ENEMIES );
					}
			}
	}

	DEBUG_LOG(( "LANEWAR: on; seats A [%d %d %d] B [%d %d %d], fortress A %d B %d, spawn A (%.0f,%.0f) B (%.0f,%.0f), middle x %.0f, plot A1 %d across by %d deep from (%.0f,%.0f) to (%.0f,%.0f), first tick at frame %u\n",
							m_seatPlayer[0][0], m_seatPlayer[0][1], m_seatPlayer[0][2], m_seatPlayer[1][0], m_seatPlayer[1][1], m_seatPlayer[1][2],
							(Int)m_fortress[0], (Int)m_fortress[1], m_spawn[0].x, m_spawn[0].y, m_spawn[1].x, m_spawn[1].y, m_middle.x,
							m_gridCols[0][0], m_gridRows[0][0], m_plotLo[0][0].x, m_plotLo[0][0].y, m_plotHi[0][0].x, m_plotHi[0][0].y, m_nextTickFrame ));
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::seatOf( const Player *player, Int *team, Int *seat ) const
{
	if( player == NULL )
		return FALSE;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			if( m_seatPlayer[ t ][ s ] == player->getPlayerIndex() )
			{
				*team = t;
				*seat = s;
				return TRUE;
			}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/// +1 when the enemy end lies towards +x, -1 when it lies towards -x; the lane runs along x
Real LaneWarDirector::towardEnemy( Int team ) const
{
	return m_spawn[ 1 - team ].x >= m_spawn[ team ].x ? 1.0f : -1.0f;
}

//-------------------------------------------------------------------------------------------------
/// the middle of a size by size block of slots whose front left slot is (col, row)
Coord3D LaneWarDirector::slotCentre( Int team, Int seat, Int col, Int row, Int size ) const
{
	const Coord2D &lo = m_plotLo[ team ][ seat ], &hi = m_plotHi[ team ][ seat ];
	const Coord2D at = LaneWar_slotPlace( lo.x, lo.y, hi.x, hi.y, towardEnemy( team ), col, row, size );
	Coord3D pos;
	pos.x = at.x;
	pos.y = at.y;
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	return pos;
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::getPlotBounds( Int team, Int seat, Coord2D *lo, Coord2D *hi ) const
{
	if( !m_active || team < 0 || team >= LANEWAR_TEAMS || seat < 0 || seat >= LANEWAR_SEATS )
		return FALSE;
	*lo = m_plotLo[ team ][ seat ];
	*hi = m_plotHi[ team ][ seat ];
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::hasLostFortress( const Player *player ) const
{
	Int team, seat;
	if( !m_active || !seatOf( player, &team, &seat ) )
		return FALSE;
	const Object *fortress = TheGameLogic->findObjectByID( m_fortress[ team ] );
	return fortress == NULL || fortress->isEffectivelyDead();
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::onOrder( Player *player, Int kind, const Int *args, Int argCount )
{
	if( !m_active || player == NULL )
		return;

	if( kind == LANEWAR_ORDER_PLACE && argCount >= 3 && args[ 0 ] >= 0 && args[ 0 ] <= 0xFFFF )
	{
		placeUnit( player, TheThingFactory->findByTemplateID( (UnsignedShort)args[ 0 ] ), args[ 1 ], args[ 2 ] );
		return;
	}

	// LANEWAR_ORDER_TIER and the rest come with the buy panel step
	DEBUG_LOG(( "LANEWAR ORDER: frame %u player %d kind %d with %d arguments, not for sale yet\n",
							TheGameLogic->getFrame(), player->getPlayerIndex(), kind, argCount ));
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::placeUnit( Player *player, const ThingTemplate *tmpl, Int col, Int row )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Int team, seat;
	const char *refused = NULL;
	Int size = 0;
	Int cost = 0;
	if( !m_active || !seatOf( player, &team, &seat ) )
		refused = "not seated in a lane game";
	else if( tmpl == NULL || tmpl->isKindOf( KINDOF_STRUCTURE ) || tmpl->isKindOf( KINDOF_PROJECTILE ) )
		refused = "not a unit";
	else
	{
		const GeometryInfo &geom = tmpl->getTemplateGeometryInfo();
		size = LaneWar_slotsFor( max( geom.getMajorRadius(), geom.getMinorRadius() ) );
		cost = tmpl->calcCostToBuild( player );
		if( !LaneWar_blockFits( col, row, size, m_gridCols[ team ][ seat ], m_gridRows[ team ][ seat ] ) )
			refused = "off the plot";
		else
		{
			for( size_t i = 0; i < m_plotUnits.size() && refused == NULL; ++i )
			{
				const PlotUnit &u = m_plotUnits[ i ];
				if( u.team == team && u.seat == seat && TheGameLogic->findObjectByID( u.id ) != NULL
						&& LaneWar_blocksOverlap( col, row, size, u.col, u.row, u.size ) )
					refused = "slot taken";
			}
			if( refused == NULL && (Int)player->getMoney()->countMoney() < cost )
				refused = "too dear";
		}
	}

	if( refused != NULL )
	{
		DEBUG_LOG(( "LANEWAR PLACE: frame %u player %d '%s' at column %d row %d refused: %s\n", now,
								player ? (Int)player->getPlayerIndex() : -1, tmpl ? tmpl->getName().str() : "?", col, row, refused ));
		return FALSE;
	}

	Team *owner = player->getDefaultTeam();
	Object *obj = TheThingFactory->newObject( tmpl, owner );
	if( obj == NULL )
		return FALSE;
	player->getMoney()->withdraw( (UnsignedInt)cost, FALSE );

	const Coord3D pos = slotCentre( team, seat, col, row, size );
	obj->setOrientation( towardEnemy( team ) > 0.0f ? 0.0f : PI );
	obj->setPosition( &pos );
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if( create )
			create->onBuildComplete();
	}
	owner->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );

	// held where it was put, unable to fire, unhurt and unpicked, whatever the ground round it is
	obj->setDisabled( DISABLED_HELD );
	obj->setStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_NO_ATTACK, OBJECT_STATUS_UNATTACKABLE ) );
	if( obj->getBodyModule() )
		obj->getBodyModule()->setIndestructible( TRUE );

	PlotUnit entry;
	entry.id = obj->getID();
	entry.team = team;
	entry.seat = seat;
	entry.col = col;
	entry.row = row;
	entry.size = size;
	m_plotUnits.push_back( entry );

	DEBUG_LOG(( "LANEWAR PLACE: frame %u player %d '%s' at column %d row %d size %d on plot %s%d at (%.0f,%.0f) for %d, %u left\n", now,
							(Int)player->getPlayerIndex(), tmpl->getName().str(), col, row, size, TEAM_NAME[ team ], seat + 1,
							pos.x, pos.y, cost, player->getMoney()->countMoney() ));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::update( void )
{
	if( !m_active )
		return;

	const UnsignedInt now = TheGameLogic->getFrame();

	expireCopies( now );

	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		const Object *fortress = TheGameLogic->findObjectByID( m_fortress[ t ] );
		if( !m_fortressDown[ t ] && ( fortress == NULL || fortress->isEffectivelyDead() ) )
		{
			m_fortressDown[ t ] = TRUE;
			DEBUG_LOG(( "HEADLESS LANEWAR: frame %u fortress %s down, team %s loses\n", now, TEAM_NAME[ t ], TEAM_NAME[ t ] ));
		}
	}
	if( m_fortressDown[ 0 ] || m_fortressDown[ 1 ] )
		return;

	if( now >= m_nextTickFrame )
	{
		tick( now );
		m_nextTickFrame += LANEWAR_TICK_FRAMES;
	}
}

//-------------------------------------------------------------------------------------------------
/// a copy dies LANEWAR_LIFESPAN_FRAMES after it spawned; the list drops the dead as it goes
void LaneWarDirector::expireCopies( UnsignedInt now )
{
	Int expired = 0;
	size_t kept = 0;
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		if( obj == NULL || obj->isEffectivelyDead() )
			continue;
		if( now >= m_copies[ i ].dieFrame )
		{
			obj->kill();
			++expired;
			continue;
		}
		m_copies[ kept++ ] = m_copies[ i ];
	}
	m_copies.resize( kept );

	if( expired > 0 )
		DEBUG_LOG(( "HEADLESS LANEWAR: frame %u lifespan ended %d copies, %d still out\n", now, expired, (Int)kept ));
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::tick( UnsignedInt now )
{
	// the middle first, over the copies already out, then the pay, then the new waves
	Int pastLine[ LANEWAR_TEAMS ] = { 0, 0 };
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		Int team, seat;
		if( obj == NULL || obj->isEffectivelyDead() || !seatOf( obj->getControllingPlayer(), &team, &seat ) )
			continue;
		if( ( obj->getPosition()->x - m_middle.x ) * towardEnemy( team ) > 0.0f )
			++pastLine[ team ];
	}
	const Int holder = LaneWar_middleHolder( pastLine[ 0 ], pastLine[ 1 ] );

	UnsignedInt paid[ LANEWAR_TEAMS ] = { 0, 0 };
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			if( m_seatPlayer[ t ][ s ] < 0 )
				continue;
			paid[ t ] = LaneWar_income( holder == t );
			ThePlayerList->getNthPlayer( m_seatPlayer[ t ][ s ] )->getMoney()->deposit( paid[ t ], FALSE );
		}

	// a plot's units never leave their slots: the furthest any stands from its own, for the log alone
	Real drift = 0.0f;
	for( size_t i = 0; i < m_plotUnits.size(); ++i )
	{
		const PlotUnit &u = m_plotUnits[ i ];
		const Object *obj = TheGameLogic->findObjectByID( u.id );
		if( obj == NULL )
			continue;
		const Coord3D at = slotCentre( u.team, u.seat, u.col, u.row, u.size );
		const Real dx = obj->getPosition()->x - at.x, dy = obj->getPosition()->y - at.y;
		drift = max( drift, (Real)sqrt( dx * dx + dy * dy ) );
	}

	DEBUG_LOG(( "HEADLESS LANEWAR: frame %u tick %u: past the line A %d B %d, middle %s, paid A %u B %u a player, %d plot units, furthest %.1f off its slot\n",
							now, m_tickCount, pastLine[ 0 ], pastLine[ 1 ], holder < 0 ? "nobody" : TEAM_NAME[ holder ], paid[ 0 ], paid[ 1 ],
							(Int)m_plotUnits.size(), drift ));

	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		Bool occupied[ LANEWAR_SEATS ];
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			occupied[ s ] = m_seatPlayer[ t ][ s ] >= 0;
		const Int seat = LaneWar_seatForTick( occupied, m_tickCount );
		if( seat >= 0 )
			spawnWave( t, seat, now );
	}
	++m_tickCount;
}

//-------------------------------------------------------------------------------------------------
/** The seat's plot, copied onto the team's pad: each plot unit's template at its slot's place
	  relative to the grid's centre, put down relative to the spawn waypoint.  A plot's front, the edge
	  towards the lane, faces the enemy end as the wave does, so the offset carries over unturned: the
	  front row leads the wave and column 0 is its left flank.  Each copy faces the enemy end.  A copy
	  that lands on ground its locomotor cannot use is moved to the nearest that it can, as a starting
	  unit is. */
void LaneWarDirector::spawnWave( Int team, Int seat, UnsignedInt now )
{
	Player *player = ThePlayerList->getNthPlayer( m_seatPlayer[ team ][ seat ] );
	const Real facing = towardEnemy( team ) > 0.0f ? 0.0f : PI;
	Team *owner = player->getDefaultTeam();
	AIGroup *group = TheAI->createGroup();

	// m_plotUnits is in purchase order, which is ObjectID order
	for( size_t i = 0; i < m_plotUnits.size(); ++i )
	{
		const PlotUnit &u = m_plotUnits[ i ];
		const Object *original = u.team == team && u.seat == seat ? TheGameLogic->findObjectByID( u.id ) : NULL;
		if( original == NULL || original->getControllingPlayer() != player )
			continue;

		const Coord3D at = slotCentre( team, seat, u.col, u.row, u.size );
		Coord3D pos = m_spawn[ team ];
		pos.x += at.x - m_plotAnchor[ team ][ seat ].x;
		pos.y += at.y - m_plotAnchor[ team ][ seat ].y;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

		Object *copy = TheThingFactory->newObject( original->getTemplate(), owner );
		if( copy == NULL )
			continue;
		copy->setOrientation( facing );
		copy->setPosition( &pos );
		for( BehaviorModule **m = copy->getBehaviorModules(); *m; ++m )
		{
			CreateModuleInterface *create = (*m)->getCreate();
			if( create )
				create->onBuildComplete();
		}
		copy->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
		owner->setActive();
		TheAI->pathfinder()->addObjectToPathfindMap( copy );

		AIUpdateInterface *ai = copy->getAI();
		if( ai && !copy->isKindOf( KINDOF_AIRCRAFT )
				&& TheAI->pathfinder()->adjustDestination( copy, ai->getLocomotorSet(), &pos ) )
		{
			TheAI->pathfinder()->updateGoal( copy, &pos, LAYER_GROUND );
			copy->setPosition( &pos );
		}

		group->add( copy );
		Copy entry;
		entry.id = copy->getID();
		entry.dieFrame = now + LANEWAR_LIFESPAN_FRAMES;
		m_copies.push_back( entry );
	}

	/* As far as the enemy fortress: the wave's middle walks onto its line, so the rows in front of the
		 fortress stop at it and fight it and the flanks come up level with it.  The pads stand in front of
		 the towers, so the distance between them would stop a wave short of the enemy's tower. */
	Real dx = m_spawn[ 1 - team ].x - m_spawn[ team ].x;
	const Object *enemyFortress = TheGameLogic->findObjectByID( m_fortress[ 1 - team ] );
	if( enemyFortress != NULL )
		dx = enemyFortress->getPosition()->x - m_spawn[ team ].x;
	const VecObjectID &ids = group->getAllIDs();
	DEBUG_LOG(( "HEADLESS LANEWAR: frame %u tick %u team %s seat %d (player %d) sends %d\n",
							now, m_tickCount, TEAM_NAME[ team ], seat + 1, (Int)player->getPlayerIndex(), (Int)ids.size() ));

	if( group->isEmpty() )
	{
		TheAI->destroyGroup( group );
		return;
	}
	group->groupLaneMarch( dx, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );

	AsciiString label;
	label.format( "LaneWar %s%d", TEAM_NAME[ team ], seat + 1 );
	ScenarioDrill_watchLaneMarch( (Int)player->getPlayerIndex(), label, group, dx );
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::crc( Xfer *xfer )
{
	LaneWarDirector::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Version 1: the seats, the fortresses and towers, the tick clock, the copies with their death frames and the
	  plot units with their slots.  The grids, pads and middle come from the map on every load. */
void LaneWarDirector::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			xfer->xferInt( &m_seatPlayer[ t ][ s ] );
		xfer->xferObjectID( &m_fortress[ t ] );
		xfer->xferObjectID( &m_tower[ t ][ 0 ] );
		xfer->xferObjectID( &m_tower[ t ][ 1 ] );
		xfer->xferBool( &m_fortressDown[ t ] );
	}
	xfer->xferUnsignedInt( &m_nextTickFrame );
	xfer->xferUnsignedInt( &m_tickCount );

	UnsignedInt count = (UnsignedInt)m_copies.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_copies.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		xfer->xferObjectID( &m_copies[ i ].id );
		xfer->xferUnsignedInt( &m_copies[ i ].dieFrame );
	}

	count = (UnsignedInt)m_plotUnits.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_plotUnits.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		PlotUnit &u = m_plotUnits[ i ];
		xfer->xferObjectID( &u.id );
		xfer->xferInt( &u.team );
		xfer->xferInt( &u.seat );
		xfer->xferInt( &u.col );
		xfer->xferInt( &u.row );
		xfer->xferInt( &u.size );
	}
}
