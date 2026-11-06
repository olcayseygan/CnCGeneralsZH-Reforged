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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// AIPathfind.h
// AI pathfinding system
// Author: Michael S. Booth, October 2001

#pragma once

#ifndef _PATHFIND_H_
#define _PATHFIND_H_												 

#include "Common/GameType.h"
#include "Common/GameMemory.h"
#include "Common/Snapshot.h"
//#include "GameLogic/Locomotor.h"	// no, do not include this, unless you like long recompiles
#include "GameLogic/LocomotorSet.h"
#include "GameLogic/GameLogic.h"

class Bridge;
class Object;
class Weapon;
class PathfindZoneManager;
class PathfindCell;		// PathfindCellInfo names it before its definition; a friend declaration alone does not declare it

// How close is close enough when moving.

#define PATHFIND_CLOSE_ENOUGH 1.0f
#define PATH_MAX_PRIORITY 0x7FFFFFFF

#define INFANTRY_MOVES_THROUGH_INFANTRY


  typedef UnsignedShort zoneStorageType;

//----------------------------------------------------------------------------------------------------------
// Zone equivalency sets.
//
// A zone equivalency array answers "which zone is this zone really the same as".  EA stored it
// flat - every entry already held its set's representative - and merged two sets by relabelling
// *every* entry of the array, so one merge cost O(zones) and a whole-map rezone cost
// O(cells x zones).  On a big map with a few thousand zones that is tens of milliseconds, once
// every ZONE_UPDATE_FREQUENCY frames, all inside one logic frame.
//
// The array is used as the parent forest of a union-find instead: pathfindZoneFind walks to the
// root with path compression, pathfindZoneUnion links the higher root under the lower one, and
// pathfindZoneFlatten collapses the forest back to the flat form every reader outside the merge
// loops still expects.  Linking the higher root under the lower one is exactly EA's "keep the
// lower zone" rule, so a set's representative is still the smallest id in it and the flattened
// array is identical to what the relabelling version produced - only the cost changes.
//
// Every link points from a higher id to a lower one, which is what lets the flatten be a single
// ascending pass and what keeps pathfindZoneFind's walk terminating.
//----------------------------------------------------------------------------------------------------------

inline Int pathfindZoneFind( zoneStorageType *zoneEquivalency, Int zone )
{
	Int root = zone;
	while (zoneEquivalency[root] != root) {
		root = zoneEquivalency[root];
	}
	while (zoneEquivalency[zone] != root) {
		Int next = zoneEquivalency[zone];
		zoneEquivalency[zone] = (zoneStorageType)root;
		zone = next;
	}
	return root;
}

inline void pathfindZoneUnion( zoneStorageType *zoneEquivalency, Int zoneA, Int zoneB )
{
	Int rootA = pathfindZoneFind( zoneEquivalency, zoneA );
	Int rootB = pathfindZoneFind( zoneEquivalency, zoneB );
	if (rootA == rootB) return;
	if (rootB < rootA) {
		zoneEquivalency[rootA] = (zoneStorageType)rootB;	// keep the lower zone
	} else {
		zoneEquivalency[rootB] = (zoneStorageType)rootA;
	}
}

inline void pathfindZoneFlatten( zoneStorageType *zoneEquivalency, Int numZones )
{
	for (Int i = 0; i < numZones; i++) {
		zoneEquivalency[i] = zoneEquivalency[zoneEquivalency[i]];
	}
}



//----------------------------------------------------------------------------------------------------------

/**
 * PathNodes are used to create a final Path to return from the
 * pathfinder.  Note that these are not used during the A* search.
 */
class PathNode : public MemoryPoolObject
{
public:
	PathNode();

	Coord3D *getPosition( void ) { return &m_pos; }			///< return position of this node
	const Coord3D *getPosition( void ) const { return &m_pos; }			///< return position of this node
	void setPosition( const Coord3D *pos ) { m_pos = *pos; }	///< set the position of this path node

	const Coord3D *computeDirectionVector( void );			///< compute direction to next node

	PathNode *getNext( void ) { return m_next; }				///< return next node in the path
	PathNode *getPrevious( void ) { return m_prev; }		///< return previous node in the path
	const PathNode *getNext( void ) const { return m_next; }				///< return next node in the path
	const PathNode *getPrevious( void ) const { return m_prev; }		///< return previous node in the path

	PathfindLayerEnum getLayer( void ) const { return m_layer; }				///< return layer of this node.
	void setLayer( PathfindLayerEnum layer ) { m_layer = layer; }	///< set the layer of this path node

	void setNextOptimized( PathNode *node );

	PathNode *getNextOptimized(Coord2D* dir = NULL, Real* dist = NULL)  	///< return next node in optimized path
	{ 
		if (dir)
			*dir = m_nextOptiDirNorm2D;
		if (dist)
			*dist = m_nextOptiDist2D;
		return m_nextOpti; 
	}

	const PathNode *getNextOptimized(Coord2D* dir = NULL, Real* dist = NULL) const  	///< return next node in optimized path
	{ 
		if (dir)
			*dir = m_nextOptiDirNorm2D;
		if (dist)
			*dist = m_nextOptiDist2D;
		return m_nextOpti; 
	}

	void setCanOptimize(Bool canOpt) { m_canOptimize = canOpt;}
	Bool getCanOptimize( void ) const { return m_canOptimize;}

	/// given a list, prepend this node, return new list
	PathNode *prependToList( PathNode *list );

	/// given a list, append this node, return new list.  slow implementation.
	/// @todo optimize this
	PathNode *appendToList( PathNode *list );
	
	/// given a node, append to this node
	void append( PathNode *list );

	/// Path::xfer puts back the direction and distance to the next optimized node exactly as saved
	void friend_setNextOptimizedCache( const Coord2D &dir, Real dist ) { m_nextOptiDirNorm2D = dir; m_nextOptiDist2D = dist; }

public:
	mutable Int					m_id; // Used in Path::xfer() to save & recreate the path list.

private:
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( PathNode, "PathNodePool"  );		///< @todo Set real numbers for mem alloc

	PathNode*						m_nextOpti;													///< next node in the optimized path
	PathNode*						m_next;															///< next node in the path
	PathNode*						m_prev;															///< previous node in the path
	Coord3D							m_pos;															///< position of node in space
	PathfindLayerEnum		m_layer;														///< Layer for this section.
	Bool								m_canOptimize;											///< True if this cell can be optimized out.

	Real								m_nextOptiDist2D;										///< if nextOpti is nonnull, the dist to it.
	Coord2D							m_nextOptiDirNorm2D;								///< if nextOpti is nonnull, normalized dir vec towards it.

};

// this doesn't actually seem to be a particularly useful win, 
// performance-wise, so I didn't enable it. (srj)
#define NO_CPOP_STARTS_FROM_PREV_SEG

struct ClosestPointOnPathInfo
{
	Real								distAlongPath;
	Coord3D							posOnPath;
	PathfindLayerEnum		layer;
	/// how far along the route the next bend is, and the cosine of the angle it turns through;
	/// 0 and 1 when the route runs straight to its end, which a vehicle brakes for anyway
	Real								bendDist;
	Real								bendCos;
};

/**
 * This class encapsulates a "path" returned by the Pathfinder.
 */
class Path : public MemoryPoolObject, public Snapshot
{
public:
	Path();
	
	PathNode *getFirstNode( void ) { return m_path; }
	PathNode *getLastNode( void ) { return m_pathTail; }

	void updateLastNode( const Coord3D *pos );

	void prependNode( const Coord3D *pos, PathfindLayerEnum layer );				///< Create a new node at the head of the path
	void appendNode( const Coord3D *pos, PathfindLayerEnum layer );				///< Create a new node at the end of the path
	void setBlockedByAlly(Bool blocked) {m_blockedByAlly = blocked;}
	Bool getBlockedByAlly(void) {return m_blockedByAlly;}
	void optimize( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, Bool blocked );			///< Optimize the path to discard redundant nodes

	void optimizeGroundPath( Bool crusher, Int diameter );			///< Optimize the ground path to discard redundant nodes

	/// Given a location, return nearest location on path, and along-path dist to end as function result
	void computePointOnPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D& pos, ClosestPointOnPathInfo& out);

	/// Given a location, return nearest location on path, and along-path dist to end as function result
	void peekCachedPointOnPath( Coord3D& pos ) const {pos = m_cpopOut.posOnPath;}

	/** How far along the route was still left, last time anybody asked.  Priority in the crowd model
			is decided on this, and recomputing it for every neighbour of every unit would be the same
			walk over the same node list a hundred times a frame. Only meaningful once
			hasCachedPointOnPath() is true. */
	Real peekCachedDistToGoal( void ) const { return m_cpopOut.distAlongPath; }

	/** True once computePointOnPath has run at least once on this path, which is the only state in
			which the cached point above means anything. Read by the -showlanes overlay. */
	Bool hasCachedPointOnPath( void ) const { return m_cpopValid; }

	/** Throw away the cached steering point.  computePointOnPath reuses its answer for up to
			MAX_CPOP frames while the unit barely moves, which is right for a unit following the same
			line and wrong the frame it decides to take a different one. */
	void invalidateCachedPointOnPath( void ) { m_cpopValid = false; }

	/** The point on the optimized route closest to `pos`, and the unit direction of the segment it
			sits on.  This is the geometry the lane code works in and nothing else: it does no steering,
			no passability check and no caching, so it is safe to call from outside a locomotor pass.
			False if the path is empty or has a single node, in which case there is no direction. */
	Bool closestPointAndDir( const Coord3D& pos, Coord3D *ptOut, Coord2D *dirOut ) const;

	/** Given a flight path, compute the distance to goal (0 if we are past it) & return the goal pos.
			A node counts as passed once pos is less than nodeReachedDist short of it along its leg. */
	Real computeFlightDistToGoal( const Coord3D *pos, Coord3D& goalPos, Real nodeReachedDist = 0.0f );
	/** After computeFlightDistToGoal, the distance still to drive along the route, where that one sums
			the reach to every node and so overstates it; and in bendDist and bendCos how far off the
			node it steers for is and the cosine of the sharpest turn off the present heading in the few
			cells after it, or 0 and 1 when that node is the last. */
	Real computeFlightRouteLeft( const Coord3D *pos, Real *bendDist, Real *bendCos ) const;

	/// Given a location, return closest location on path, and along-path dist to end as function result
	void markOptimized(void) {m_isOptimized = true;}

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:
	enum {MAX_CPOP=20 * LOGICFRAMES_PER_RETAIL_FRAME};			///< Max times we will return the cached cpop (about once a frame, so two thirds of a second).
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( Path, "PathPool" );							///< @todo Set real numbers for mem alloc

	PathNode*		m_path;															///< The list of PathNode objects that define the path
	PathNode*		m_pathTail;
	Bool				m_isOptimized;											///< True if the path has been optimized
	Bool				m_blockedByAlly;										///< An ally needs to move off of this path.
	// caching info for computePointOnPath.
	Bool										m_cpopValid;
	Int											m_cpopCountdown;				///< We only return the same cpop MAX_CPOP times.  It is occasionally possible to get stuck.
	Coord3D									m_cpopIn;
	ClosestPointOnPathInfo	m_cpopOut;
	const PathNode*					m_cpopRecentStart;
};

//----------------------------------------------------------------------------------------------------------

// See GameType.h for
// enum {LAYER_INVALID = 0, LAYER_GROUND = 1, LAYER_TOP=2 };

// Fits in 4 bits for now
enum {MAX_WALL_PIECES = 128};

class PathfindCellInfo
{
	friend class PathfindCell;
public:
	static void allocateCellInfos(void);
	static void releaseCellInfos(void);

	static PathfindCellInfo * getACellInfo(PathfindCell *cell, const ICoord2D &pos);
	static void releaseACellInfo(PathfindCellInfo *theInfo);

protected:
	static PathfindCellInfo *s_infoArray;
	static PathfindCellInfo *s_firstFree;							///< 


	PathfindCellInfo *m_nextOpen, *m_prevOpen;						///< for A* "open" list, shared by closed list

	PathfindCellInfo *m_pathParent;												///< "parent" cell from pathfinder
	PathfindCell *m_cell;															///< Cell this info belongs to currently.

	UnsignedShort m_totalCost, m_costSoFar;	///< cost estimates for A* search

	/* Ground covered to reach this cell, in the same units as m_costSoFar but with none of the
		 penalties in it.  A penalty is money, not distance: a detour taken to dodge somebody must
		 not also convince the search it arrives eight cells later than it really does.  The flow
		 model turns this number into an arrival frame, which is what the space-time claims below
		 are looked up by.  Only the main ground search maintains it; every other search leaves it
		 at zero and charges no crossing cost. */
	UnsignedShort m_travelSoFar;

	/// have to include cell's coordinates, since cells are often accessed via pointer only
	ICoord2D m_pos;
	
	ObjectID m_goalUnitID; ///< The objectID of the ground unit whose goal this is.
	ObjectID m_posUnitID;  ///< The objectID of the ground unit that is occupying this cell.
	ObjectID m_goalAircraftID; ///< The objectID of the aircraft whose goal this is.

	UnsignedInt m_isFree:1;
	/// @todo Do we need both mark values in this cell?  Can't store a single value and compare it?
	UnsignedInt m_open:1;													///< place for marking this cell as on the open list
	UnsignedInt m_closed:1;												///< place for marking this cell as on the closed list
	/* The open list is indexed by cost (see the bucket queue in AIPathfind.cpp).  This is the
		 bucket the cell was filed under, which is not always m_totalCost any more: callers change
		 the cost of a cell that is still linked into the list and only then take it off, so the
		 cell has to remember where it actually went.  Sixteen bits, the width of m_totalCost, and
		 it rides in the spare room of this bitfield word so PathfindCellInfo does not grow. */
	UnsignedInt m_openBucket:16;
};

/**
 * This represents one cell in the pathfinding grid.
 * These cells categorize the world into idealized cellular states,
 * and are also used for efficient A* pathfinding.
 * @todo Optimize memory usage of pathfind grid.
 */
class PathfindCell
{
public:

	enum CellType
	{
		CELL_CLEAR		= 0x00,									///< clear, unobstructed ground
		CELL_WATER		= 0x01,									///< water area
		CELL_CLIFF		= 0x02,									///< steep altitude change
		CELL_RUBBLE		= 0x03,									///< Cell is occupied by rubble.
		CELL_OBSTACLE	= 0x04,									///< Occupied by a structure
		CELL_BRIDGE_IMPASSABLE = 0x05,				///< Piece of a bridge that is impassable.
		CELL_IMPASSABLE = 0x06								///< Just plain impassable except for aircraft.
	};

	enum CellFlags
	{
		NO_UNITS		= 0x00,						///< No units in this cell.
		UNIT_GOAL		= 0x01,						///< A unit is heading to this cell.
		UNIT_PRESENT_MOVING	= 0x02,		///< A unit is moving through this cell.
		UNIT_PRESENT_FIXED	= 0x03,		///< A unit is stationary in this cell.
		UNIT_GOAL_OTHER_MOVING	= 0x05		///< A unit is moving through this cell, and another unit has this as it's goal.
	};

	/// reset the cell
	void reset( );

	PathfindCell(void);
	~PathfindCell(void);

	Bool setTypeAsObstacle( Object *obstacle, Bool isFence, const ICoord2D &pos );				///< flag this cell as an obstacle, from the given one
	Bool removeObstacle( Object *obstacle );				///< unflag this cell as an obstacle, from the given one
	void setType( CellType type );	///< set the cell type
	CellType getType( void ) const { return (CellType)m_type; }				///< get the cell type
	CellFlags getFlags( void ) const { return (CellFlags)m_flags; }				///< get the cell type
	Bool isAircraftGoal( void) const {return m_aircraftGoal != 0;}

	Bool isObstaclePresent( ObjectID objID ) const;					///< return true if the given object ID is registered as an obstacle in this cell

	Bool isObstacleTransparent( ) const{return m_obstacleIsTransparent; }					///< return true if the obstacle in the cell is KINDOF_CAN_SEE_THROUGHT_STRUCTURE

	Bool isObstacleFence( void ) const {return m_obstacleIsFence; }///< return true if the given obstacle in the cell is a fence.

	/// Return estimated cost from given cell to reach goal cell
	UnsignedInt costToGoal( PathfindCell *goal );

	UnsignedInt costToHierGoal( PathfindCell *goal );

	UnsignedInt costSoFar( PathfindCell *parent );

	/// tell the open list its single seed cell, so the tail pointer starts out right
	static void setOpenListSeed( PathfindCell *cell );

	/// put self on "open" list in ascending cost order, return new list
	PathfindCell *putOnSortedOpenList( PathfindCell *list );		

	/// remove self from "open" list
	PathfindCell *removeFromOpenList( PathfindCell *list );		

	/// put self on "closed" list, return new list
	PathfindCell *putOnClosedList( PathfindCell *list );		

	/// remove self from "closed" list
	PathfindCell *removeFromClosedList( PathfindCell *list );	

	/// remove all cells from closed list.
	static Int releaseClosedList( PathfindCell *list );	

	/// remove all cells from closed list.
	static Int releaseOpenList( PathfindCell *list );	

	inline PathfindCell *getNextOpen(void) {return m_info->m_nextOpen?m_info->m_nextOpen->m_cell:NULL;}

	inline UnsignedShort getXIndex(void) const {return m_info->m_pos.x;}
	inline UnsignedShort getYIndex(void) const {return m_info->m_pos.y;}

	inline Bool isBlockedByAlly(void) const {return m_blockedByAlly;}
	inline void setBlockedByAlly(Bool blocked)  {m_blockedByAlly = (blocked!=0);}

	inline Bool getOpen(void) const {return m_info->m_open;}
	inline Bool getClosed(void) const {return m_info->m_closed;}
	inline UnsignedInt getCostSoFar(void) const {return m_info->m_costSoFar;}
	inline UnsignedInt getTotalCost(void) const {return m_info->m_totalCost;}
	inline UnsignedInt getTravelSoFar(void) const {return m_info->m_travelSoFar;}

	inline void setCostSoFar(UnsignedInt cost) { if( m_info ) m_info->m_costSoFar = cost;}
	inline void setTotalCost(UnsignedInt cost) { if( m_info ) m_info->m_totalCost = cost;}
	/* Saturates rather than wrapping.  Sixteen bits is 6553 cells of route, which no map here can
		 hold, but a search that wanders can still get there - and a wrapped distance turns into an
		 arrival time in the wrong half-minute, which prices a crossing that is not there. */
	inline void setTravelSoFar(UnsignedInt travel) { if( m_info ) m_info->m_travelSoFar = (UnsignedShort)(travel > 0xFFFF ? 0xFFFF : travel);}

	void setParentCell(PathfindCell* parent);
	void clearParentCell(void);
	void setParentCellHierarchical(PathfindCell* parent);
	inline PathfindCell* getParentCell(void) const {return m_info ? m_info->m_pathParent ? m_info->m_pathParent->m_cell : NULL : NULL;}

	Bool startPathfind( PathfindCell *goalCell ); 
	Bool getPinched(void) const {return m_pinched;}
	void setPinched(Bool pinch) {m_pinched = pinch;	}

	Bool allocateInfo(const ICoord2D &pos);
	void releaseInfo(void);
	Bool hasInfo(void) const {return m_info!=NULL;}
	zoneStorageType getZone(void) const {return m_zone;}
	void setZone(zoneStorageType zone) {m_zone = zone;}
	void setGoalUnit(ObjectID unit, const ICoord2D &pos );
	void setGoalAircraft(ObjectID unit, const ICoord2D &pos );
	void setPosUnit(ObjectID unit, const ICoord2D &pos );
	inline ObjectID getGoalUnit(void) const {ObjectID id = m_info?m_info->m_goalUnitID:INVALID_ID; return id;}
	inline ObjectID getGoalAircraft(void) const {ObjectID id = m_info?m_info->m_goalAircraftID:INVALID_ID; return id;}
	inline ObjectID getPosUnit(void) const {ObjectID id = m_info?m_info->m_posUnitID:INVALID_ID; return id;}	

	inline ObjectID getObstacleID(void) const {return m_obstacleID;}

	void setLayer( PathfindLayerEnum layer ) { m_layer = layer; }	///< set the cell layer
	PathfindLayerEnum getLayer( void ) const { return (PathfindLayerEnum)m_layer; }				///< get the cell layer

	void setConnectLayer( PathfindLayerEnum layer ) { m_connectsToLayer = layer; }	///< set the cell layer	connect id
	PathfindLayerEnum getConnectLayer( void ) const { return (PathfindLayerEnum)m_connectsToLayer; }				///< get the cell layer connect id

private:
	PathfindCellInfo *m_info;
	//
	// A cell that holds an obstacle used to keep a pooled PathfindCellInfo alive for as long as
	// the obstacle stood there, and releaseInfo() refuses to reclaim an obstacle cell - so every
	// wall, fence and building on the map permanently ate records the A* search needs.  The four
	// fields below are what the record was being held open for, so they live here instead and
	// the pool is left to the search.
	//
	ObjectID m_obstacleID;								///< the object ID who overlaps this cell
	UnsignedInt m_blockedByAlly:1;				///< True if this cell is blocked by an allied unit.
	UnsignedInt m_obstacleIsFence:1;			///< True if occupied by a fence.
	UnsignedInt m_obstacleIsTransparent:1;///< True if obstacle is transparent (undefined if obstacleid is invalid)
	zoneStorageType m_zone:14;			///< Zone. Each zone is a set of adjacent terrain type.  If from & to in the same zone, you can successfully pathfind.  If not,
														// you still may be able to if you can cross multiple terrain types.
	UnsignedShort m_aircraftGoal:1; //< This is an aircraft goal cell.
	UnsignedShort m_pinched:1; //< This cell is surrounded by obstacle cells.
	UnsignedByte m_type:4;			///< what type of cell terrain this is.  
	UnsignedByte m_flags:4;			///< what type of units are in or moving through this cell.
	UnsignedByte m_connectsToLayer:4;	///< This cell can pathfind onto this layer, if > LAYER_TOP.
  UnsignedByte m_layer:4;					 ///< Layer of this cell.
};

typedef PathfindCell *PathfindCellP;


// how close a unit has to be in z to interact with the layer.
#define LAYER_Z_CLOSE_ENOUGH_F 10.0f
/**
 * This class represents a bridge in the map. This is effectively
 * a sub-rectangle of the big pathfind map.
 */
class PathfindLayer
{
public:
	PathfindLayer();
	~PathfindLayer();
public:
	void reset(void);
	Bool init(Bridge *theBridge, PathfindLayerEnum layer);
	void allocateCells(const IRegion2D *extent);
	void allocateCellsForWallLayer(const IRegion2D *extent, ObjectID *wallPieces, Int numPieces);
	void classifyCells();
	void classifyWallCells(ObjectID *wallPieces, Int numPieces);
	Bool setDestroyed(Bool destroyed);
	Bool isUnused(void); // True if it doesn't contain a bridge.
	Bool isDestroyed(void) {return m_destroyed;} // True if it has been destroyed.
	PathfindCell *getCell(Int x, Int y);
	Int getZone(void) {return m_zone;}
	void setZone(Int zone) {m_zone = zone;}
	void applyZone(void); // Propagates m_zone to all cells.
	void getStartCellIndex(ICoord2D *start) {*start = m_startCell;}
	void getEndCellIndex(ICoord2D *end) {*end = m_endCell;}

	ObjectID getBridgeID(void);
	Bool connectsZones(PathfindZoneManager *zm, const LocomotorSet& locomotorSet,Int zone1, Int zone2);
	Bool isPointOnWall(ObjectID *wallPieces, Int numPieces, const Coord3D *pt);

#if defined _DEBUG || defined _INTERNAL
	void doDebugIcons(void) ;
#endif
protected:
	void classifyLayerMapCell( Int i, Int j , PathfindCell *cell, Bridge *theBridge);
	void classifyWallMapCell( Int i, Int j, PathfindCell *cell , ObjectID *wallPieces, Int numPieces);

private:
	PathfindCell *m_blockOfMapCells;		///< Pathfinding map - contains iconic representation of the map
	PathfindCell **m_layerCells;		///< Pathfinding map indexes - contains matrix indexing into the map.
	Int m_width;		// Number of cells in x
	Int m_height;		// Number of cells in y
	Int m_xOrigin;	// Index of first cell in x
	Int m_yOrigin;	// Index of first cell in y
	ICoord2D m_startCell; // pathfind cell indexes for center cell on the from side.
	ICoord2D m_endCell; // pathfind cell indexes for center cell on the to side.

	PathfindLayerEnum m_layer;
	Int m_zone;			// Whole bridge is in same zone.
	Bridge *m_bridge; // Corresponding bridge in TerrainLogic.
	Bool m_destroyed;


};


#define PATHFIND_CELL_SIZE		10
#define PATHFIND_CELL_SIZE_F	10.0f

// One slot per object that can ask (queueForPath refuses a second), so this has to outnumber
// everything that paths at once. Retail 512 was under the lobby's 840-unit limit: one order to 841
// units left 329 of them waiting for a path that was never queued.
enum { PATHFIND_QUEUE_LEN=2048};

struct TCheckMovementInfo;

/** 
 * This class is a helper class for zone manager.  It maintains information regarding the 
 * LocomotorSurfaceTypeMask equivalencies within a ZONE_BLOCK_SIZE x ZONE_BLOCK_SIZE area of 
 * cells.  This is used in hierarchical pathfinding to find the best coarse path at the 
 * block level.
 */
class ZoneBlock
{
public: 

	ZoneBlock();
	~ZoneBlock();  // not virtual, please don't override without making virtual.  jba.

	void blockCalculateZones(	PathfindCell **map, PathfindLayer layers[], const IRegion2D &bounds);	///< Does zone calculations.  
	zoneStorageType getEffectiveZone(LocomotorSurfaceTypeMask acceptableSurfaces, Bool crusher, zoneStorageType zone) const;

	void clearMarkedPassable(void) {m_markedPassable = false;}
	Bool isPassable(void) {return m_markedPassable;}
	void setPassable(Bool pass) {m_markedPassable = pass;}

	Bool getInteractsWithBridge(void) const {return m_interactsWithBridge;}
	void setInteractsWithBridge(Bool interacts) {m_interactsWithBridge = interacts;}

protected:
	void allocateZones(void);
	void freeZones(void);

protected:
	ICoord2D		m_cellOrigin;

	zoneStorageType m_firstZone; // First zone in this block.
	UnsignedShort m_numZones;	 // Number of zones in this block.  If == 1, there is only one zone, and 
														 // no zone equivalency arrays will be allocated.


	UnsignedShort m_zonesAllocated;
	zoneStorageType *m_groundCliffZones;
	zoneStorageType *m_groundWaterZones;
	zoneStorageType *m_groundRubbleZones;
	zoneStorageType *m_crusherZones;
	Bool					m_interactsWithBridge;
	Bool					m_markedPassable;
};
typedef ZoneBlock *ZoneBlockP;

/**
 * This class manages the zones in the map.  A zone is an area in the map that
 * is one contiguous type of terrain (clear, cliff, water, building).  If 
 * a unit is in a zone, and wants to move to another location, the destination 
 * zone has to be the same, or it can't get there.
 * There are equivalency tables for meta-zones.  For example, an amphibious craft can 
 * travel through water and clear cells. 
 */
class PathfindZoneManager
{
public:
	enum {INITIAL_ZONES = 256};
	enum {ZONE_BLOCK_SIZE = 10};	// Zones are calculated in blocks of 20x20.  This way, the raw zone numbers can be used to 
	enum {UNINITIALIZED_ZONE = 0};
																// compute hierarchically between the 20x20 blocks of cells. jba.
	PathfindZoneManager();
	~PathfindZoneManager();

	void reset(void);

	Bool needToCalculateZones(void) const {return m_nextFrameToCalculateZones <= TheGameLogic->getFrame() ;} ///< Returns true if the zones need to be recalculated.
 	void markZonesDirty( Bool insert ) ; ///< Called when the zones need to be recalculated.
 	void updateZonesForModify( PathfindCell **map,  PathfindLayer layers[], const IRegion2D &structureBounds, const IRegion2D &globalBounds ) ; ///< Called to recalculate an area when a structure has been removed.
	void calculateZones(	PathfindCell **map, PathfindLayer layers[], const IRegion2D &bounds);	///< Does zone calculations.  
	zoneStorageType getEffectiveZone(LocomotorSurfaceTypeMask acceptableSurfaces, Bool crusher, zoneStorageType zone) const;
	zoneStorageType getEffectiveTerrainZone(zoneStorageType zone) const;

	zoneStorageType getNextZone(void);

	void getExtent(ICoord2D &extent) const {extent = m_zoneBlockExtent;}

	/// return zone relative the the block zone that this cell resides in.
	zoneStorageType getBlockZone(LocomotorSurfaceTypeMask acceptableSurfaces, Bool crusher, Int cellX, Int cellY, PathfindCell **map) const;
	void allocateBlocks(const IRegion2D &globalBounds);

	void clearPassableFlags(void);
	Bool isPassable(Int cellX, Int cellY) const;
	Bool clipIsPassable(Int cellX, Int cellY) const;
	void setPassable(Int cellX, Int cellY, Bool passable);
	/// mark this cell's block passable together with the eight blocks around it, clipped to the map
	void setPassableWithNeighbors(Int cellX, Int cellY);

	void setAllPassable(void);

	void setBridge(Int cellX, Int cellY, Bool bridge);
	Bool interactsWithBridge(Int cellX, Int cellY) const; 

private:
	void allocateZones(void);
	void freeZones(void);
	void freeBlocks(void);

private:
	ZoneBlock			*m_blockOfZoneBlocks;			///< Zone blocks - Info for hierarchical pathfinding at a "blocky" level.
	ZoneBlock			**m_zoneBlocks;						///< Zone blocks as a matrix - contains matrix indexing into the map.
	ICoord2D			m_zoneBlockExtent;				///< Zone block extents. Not the same scale as the pathfind extents.

	UnsignedShort m_maxZone;								///< Max zone used.
	UnsignedInt		m_nextFrameToCalculateZones;		///< WHen should I recalculate, next?.
	UnsignedShort m_zonesAllocated;
	zoneStorageType *m_groundCliffZones;
	zoneStorageType *m_groundWaterZones;
	zoneStorageType *m_groundRubbleZones;
	zoneStorageType *m_terrainZones;
	zoneStorageType *m_crusherZones;
	zoneStorageType *m_hierarchicalZones;
};

//----------------------------------------------------------------------------------------------------------
// The flow model.
//
// Retail's A* prices terrain and nothing else, so every unit ordered across the same ground is
// handed the same line: they stack on it, the ones behind stop, and the search that eventually
// runs for a stuck unit hands it the line it is stuck on.  Three maps are laid over the terrain
// cost to fix that, ported from the browser sandbox (astar-units.html) that they were tuned in:
//
//   Clearance - chamfer distance from each cell to the nearest cell the unit could not stand in.
//     A route is charged for hugging terrain, quadratically, over a band about two and a half
//     cells wide.  That is what makes a tank take a corner wide instead of scraping it, and it
//     is the one map that costs nothing to keep: the terrain it measures only changes when a
//     building goes up.
//
//   Traffic - where units are actually stopped, decayed over about a second.  A queue therefore
//     looks expensive to the next search, and a unit repathing out of one is given a way round
//     it rather than back into it.
//
//   Claims - where each live path expects its owner to be, and *when*, in half-second buckets.
//     Two units following one road are in the same cells at different moments and pay nothing;
//     two columns crossing want the same cell at the same moment and are charged for it.  Time
//     is the whole point: without it a shared road and a crossing look identical, which is what
//     the fork's earlier congestion map got wrong.
//
// Every one of them is integer arithmetic over deterministic inputs, so two machines expand the
// same cells in the same order.  All three are off under -noflowpath.
//----------------------------------------------------------------------------------------------------------

enum
{
	// clearance is a chamfer distance: 5 per orthogonal step, 7 per diagonal, so one cell is 5.
	PF_CLEARANCE_ORTHO				= 5,
	PF_CLEARANCE_DIAG					= 7,
	/* Twelve cells of open ground.  Nothing above that is ever read: the widest body the game
		 has needs about three cells and the band charged for is two and a half more, so a higher
		 ceiling would only cost the rebuild time to compute. */
	PF_CLEARANCE_MAX					= 60,
	PF_CLEARANCE_SOFT					= 13,			///< the band charged for, ~2.5 cells
	PF_WALLHUG_NUM						= 8,			///< peak wall-hug penalty is NUM/DEN of a step
	PF_WALLHUG_DEN						= 5,

	PF_TRAFFIC_MAX						= 255,
	PF_TRAFFIC_DEPOSIT				= 24 / LOGICFRAMES_PER_RETAIL_FRAME,	///< one blocked unit-frame, at the centre cell; 24 a retail frame, so the same per second
	PF_TRAFFIC_DECAY_NUM			= 54,			///< x54/64 every decay tick ~ the sandbox's x0.85 per 0.1s
	PF_TRAFFIC_DECAY_DEN			= 64,
	PF_TRAFFIC_DECAY_FRAMES		= 3 * LOGICFRAMES_PER_RETAIL_FRAME,	///< a tenth of a second
	PF_TRAFFIC_FULL						= 192,		///< traffic at or above this is as expensive as it gets
	PF_TRAFFIC_NUM						= 4,			///< peak traffic penalty is NUM/DEN of a step - the sandbox's traffic weight
	PF_TRAFFIC_DEN						= 1,
	PF_TRAFFIC_CELLS					= 4096,		///< live traffic cells tracked; past this, deposits are dropped

	PF_CLAIM_BUCKET_FRAMES		= LOGICFRAMES_PER_SECOND / 2,			///< half a second
	PF_CLAIM_BUCKETS					= 24,			///< twelve seconds of horizon
	PF_CLAIM_REFRESH_FRAMES		= LOGICFRAMES_PER_SECOND,			///< a unit re-stamps its plan once a second
	PF_CLAIM_TABLE_SIZE				= 32768,	///< power of two: the hash is masked, not divided
	PF_CLAIM_PROBES						= 8,			///< linear probe depth; past it a claim is simply dropped
	PF_CLAIM_UNITS_MAX				= 4,			///< claims beyond this on one cell-moment cost nothing more
	PF_CLAIM_SAMPLES_MAX			= 32,			///< cells of its own plan one unit may stamp per refresh; 64 was 5% of a 530-unit battle
	PF_CLEARANCE_REBUILD_FRAMES	= LOGICFRAMES_PER_SECOND,		///< how often a dirty clearance field is actually rebuilt
	PF_CROSSING_NUM						= 1,			///< peak crossing penalty is NUM/DEN of a step
	PF_CROSSING_DEN						= 2,

	/* What the A* estimate is scaled by while the flow charge is on, against retail's 3/2.  The
		 charge is invisible to the estimate, so leaving the estimate alone leaves the search with
		 no reason to walk towards the goal rather than round it. */
	PF_HEURISTIC_FLOW_NUM			= 9,
	PF_HEURISTIC_FLOW_DEN			= 4,

	/* Momentum.  Retail charges a turn 4, 8 or 16 depending on how sharp it is, the same for a
		 motorcycle and a battleship, which is how a route comes to ask a tank for a turn it has to
		 stop and grind out.  What a turn actually costs is the time to swing the hull, and in these
		 units that is exactly speed-per-frame over turn-rate-per-frame for every radian of it: a
		 unit that covers two cells while turning one radian pays two cells for one radian.

		 Two caps, and they are different on purpose.  Mid-route the charge is held to STEP so the
		 estimate, which cannot see any of this, still has something to steer by - an A* whose real
		 costs run far ahead of its estimate stops walking towards the goal and expands the whole
		 corridor instead.  The first step out of the start cell is capped at START instead, higher,
		 because that is the one turn the unit is definitely making right now and the one route
		 nobody wants: the one that begins by reversing out from under its own tracks. */
	PF_TURN_CAP_STEP					= 28,			///< 2 x COST_DIAGONAL
	PF_TURN_CAP_START					= 60,

	/* Lanes.  The three maps above decide where a route goes; these decide where across it a unit
		 drives.  PROBE is how far sideways the ground is measured before the band is called wide
		 enough, in cells; TAPER is how close to the goal the band closes back onto the centre so
		 every unit can still reach the cell reserved for it; HOLD is how long a lane taken to get
		 round somebody is kept before the unit drifts back toward the middle.

		 PROBE was 8 and that was too small to see: measured over a four-player match the band came
		 out 77 world units wide, and a tank is 24 of those, so the whole band was three tanks and a
		 selection of twenty-five had nowhere to go. 16 cells is 160 a side before the body's own
		 radius comes off it, which is about ten tanks abreast where the ground allows it and exactly
		 what it was before where it does not. TAPER moved with it: the band now opens wider, so it
		 needs longer to close again before the goal. */
	PF_LANE_PROBE_CELLS				= 16,
	PF_LANE_TAPER_CELLS				= 12,
	/* The band is shut, not merely narrowing, over the last few cells.  A taper that only reaches
		 zero at the goal itself leaves a unit seventy feet off its own route still twenty feet off
		 it when it arrives, and a group ordered onto one point spreads out instead of gathering on
		 it - which is what the ramp looked like in game. */
	PF_LANE_CLOSE_CELLS				= 3,
	PF_LANE_HOLD_FRAMES				= 3 * LOGICFRAMES_PER_SECOND / 2		///< a second and a half
};

/** How much extra a step into a cell with this clearance costs a body needing `needClearance`.
	  Zero out in the open, rising quadratically as the body's own margin is eaten into. */
extern Int Pathfinder_wallHugCost( Int baseCost, Int clearance, Int needClearance );

/// How much extra a step into a cell holding this much live traffic costs.
extern Int Pathfinder_trafficCost( Int baseCost, Int traffic );

/// How much extra a step into a cell somebody else wants at the same moment costs.
extern Int Pathfinder_crossingCost( Int baseCost, Int claimHalves );

/** What swinging the hull through `radians` costs this chassis, capped at `cap`.

		`chassis` is the unit's own speed per frame divided by its turn rate per frame, which in the
		pathfinder's units is the cost of one radian directly: a hull that covers 40 world units in
		the time it turns one radian pays 40 for that radian, the same as walking four cells.  It is
		zero for anything that turns on the spot, and the caller passes zero to switch the whole
		charge off (`-nomomentum`, or a search with no object behind it). */
extern Int Pathfinder_turnCost( Real radians, Int chassis, Int cap );

/** Where across a band a body sitting `lateral` world units left of the centre line rides.
	  0 is hard against the right edge, 1 hard against the left, 0.5 the centre; clamped just
	  inside both edges so a lane is never the wall itself.  Half-widths of zero give 0.5. */
extern Real Pathfinder_laneFraction( Real lateral, Real leftExtent, Real rightExtent );

/// The sideways offset, in world units left of the centre line, that lane fraction `u` means.
extern Real Pathfinder_laneOffset( Real u, Real leftExtent, Real rightExtent );
/** Where across the band a group member riding `lateral` world units left of its route's centre
		line belongs, against the widest band the probe can ever find rather than against the width
		this particular piece of ground turns out to have.

		Measuring it against the group's own width was the previous answer and it was wrong twice
		over: a tightly packed selection has almost no width, so every member came out near the
		centre, and a wide one has more width than any road, so every member came out against an
		edge.  Neither has anything to do with how much room a tank needs.  Against a fixed
		reference the offset means feet, the caller can space members a body apart, and a narrow
		road still squeezes them - laneOffset scales the fraction by the room actually found, so
		half the band is half the offset. */
extern Real Pathfinder_groupLane( Real lateral );

/// The widest half-band the probe can report, which is what Pathfinder_groupLane measures against.
extern Real Pathfinder_laneReference( void );

/** How much of a lane is left this many world units short of the end of the route: all of it
		beyond PF_LANE_TAPER_CELLS, none of it inside PF_LANE_CLOSE_CELLS, straight line between. */
extern Real Pathfinder_laneTaper( Real remaining );

/* There is no lane-bend factor here on purpose.  Pinching the band shut through a turn - scaling
	 the offset by the cosine between the two headings - sounds right and measures terrible: twelve
	 four-player matches went from 32 blocked unit-frames per 1000 to 102, because a route bends
	 constantly and closing the band at every bend puts the whole group back into single file for
	 the length of each one.  The corner problem is that the offset was being measured against the
	 wrong heading, and computePointOnPath fixes that by taking the heading at the steering point
	 and refusing a lane point that lands behind the unit. */

/// The A* estimate's scale factor over plain octile distance, set per search by beginFlowSearch.
extern Int thePathHeuristicNum;
extern Int thePathHeuristicDen;

/**
 * One claim on one cell at one moment: which cell, which absolute half-second bucket, and how
 * much of a unit is expected to be there.  Weight is in halves, because a claim spills into the
 * buckets either side of the one it lands in at half strength - a unit's arrival time is an
 * estimate, and an estimate that is only ever right to the bucket prices nothing.
 */
struct PathClaim
{
	Int						m_cell;			///< cell index, -1 when the slot has never been used
	UnsignedInt		m_bucket;		///< absolute bucket (logic frame / PF_CLAIM_BUCKET_FRAMES)
	UnsignedShort	m_weight;		///< in halves
};

/**
 * The pathfinding services interface provides access to the 3 expensive path find calls:
 * findPath, findClosestPath, and findAttackPath.
 * It is only available to units when their ai interface doPathfind method is called.
 * This allows the pathfinder to spread out the pathfinding over a number of frames 
 * when a lot of units are trying to pathfind all at the same time.
 */
class PathfindServicesInterface {
public:
	virtual Path *findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, 
		const Coord3D *to )=0;	///< Find a short, valid path between given locations
	/** Find a short, valid path to a location NEAR the to location.
		This succeds when the destination is unreachable (like inside a building).
		If the destination is unreachable, it will adjust the to point.  */
	virtual Path *findClosestPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		Coord3D *to, Bool blocked, Real pathCostMultiplier, Bool moveAllies )=0;	

	/** Find a short, valid path to a location that obj can attack victim from.  */
	virtual Path *findAttackPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Object *victim, const Coord3D* victimPos, const Weapon *weapon )=0;	

	/** Patch to the exiting path from the current position, either because we became blocked, 
  or because we had to move off the path to avoid other units. */
	virtual Path *patchPath( const Object *obj, const LocomotorSet& locomotorSet, 
		Path *originalPath, Bool blocked ) = 0;

	/** Find a short, valid path to a location that is away from the repulsors.  */
	virtual Path *findSafePath( const Object *obj, const LocomotorSet& locomotorSet, 
		const Coord3D *from, const Coord3D* repulsorPos1, const Coord3D* repulsorPos2, Real repulsorRadius ) = 0;	

};

/**
 * The Pathfinding engine itself.
 */
class Pathfinder : PathfindServicesInterface, public Snapshot
{
// The following routines are private, but available through the doPathfind callback to aiInterface. jba.
private:
	virtual Path *findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to);	///< Find a short, valid path between given locations
	/** Find a short, valid path to a location NEAR the to location.
		This succeds when the destination is unreachable (like inside a building).
		If the destination is unreachable, it will adjust the to point.  */
	virtual Path *findClosestPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		Coord3D *to, Bool blocked, Real pathCostMultiplier, Bool moveAllies );	

	/** Find a short, valid path to a location that obj can attack victim from.  */
	virtual Path *findAttackPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Object *victim, const Coord3D* victimPos, const Weapon *weapon );	

	/** Find a short, valid path to a location that is away from the repulsors.  */
	virtual Path *findSafePath( const Object *obj, const LocomotorSet& locomotorSet, 
		const Coord3D *from, const Coord3D* repulsorPos1, const Coord3D* repulsorPos2, Real repulsorRadius );	

	/** Patch to the exiting path from the current position, either because we became blocked, 
  or because we had to move off the path to avoid other units. */
	virtual Path *patchPath( const Object *obj, const LocomotorSet& locomotorSet, 
		Path *originalPath, Bool blocked );

public:
	Pathfinder( void );
	~Pathfinder() ;

	void reset( void );														///< Reset system in preparation for new map

	// --------------- inherited from Snapshot interface --------------
	void crc( Xfer *xfer );
	void xfer( Xfer *xfer );
	void loadPostProcess( void );
	void xferFlowState( Xfer *xfer );							///< the request queue and the flow maps, which GameLogic saves after the objects

	Bool clientSafeQuickDoesPathExist( const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to );  ///< Can we build any path at all between the locations	(terrain & buildings check - fast)
	Bool clientSafeQuickDoesPathExistForUI( const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to );  ///< Can we build any path at all between the locations	(terrain onlyk - fast)
	Bool slowDoesPathExist( Object *obj, const Coord3D *from, 
		const Coord3D *to, ObjectID ignoreObject=INVALID_ID );  ///< Can we build any path at all between the locations	(terrain, buildings & units check - slower)

	Bool queueForPath(ObjectID id);	 ///< The object wants to request a pathfind, so put it on the list to process.

	//
	// Per-frame accounting of where pathfinder time actually goes. The slow-frame log used to
	// charge all of it to `processPathfindQueue`, and most slow frames turned out to run no
	// search large enough to log itself - so the cost is spread over the other entry points
	// (patching, attack paths, goal and position updates, footprint reclassification) and there
	// was no way to tell which. Reset at the top of each logic frame, printed when the frame runs
	// over budget. Timing only, nothing the simulation reads.
	//
	static void resetProfile( void );							///< start a new frame's tally
	static const char *getProfileReport( void );	///< "patch 12x/3.1 attack 4x/8.0", empty when idle

	//
	// The same tallies summed over a whole match, plus the counts that say whether a pathing
	// change made things worse rather than merely different: searches that ran the cell pool dry,
	// requests that came back with no path at all, and unit-frames lost to standing behind
	// another unit. A headless batch prints these at the end of the run, so a change to path cost
	// can be argued with numbers instead of with how the paths looked.
	//
	static void resetMatchProfile( void );					///< zero the match totals (start of a headless run)
	static const char *getMatchProfileReport( void );
	static void bumpNoPath( void );									///< a path request came back empty
	static void bumpBlockedFrame( Bool stuck );			///< one unit-frame spent blocked by another unit
	static void bumpWedgeFrame( Int consecutive );	///< one unit-frame stopped with no collision to blame
	static void bumpEscape( void );									///< a unit started backing out of somewhere
	static void bumpCrowdRepath( void );						///< a unit gave up on its route rather than steer round
	static void bumpDither( void );									///< a unit was caught driving a long way and gaining nothing
	void processPathfindQueue(void); ///< Process some or all of the queued pathfinds.
	/// this unit's search ran out of the frame's cells and carries on at its next turn in the queue
	Bool isSearchSuspended( ObjectID id ) const { return id != INVALID_ID && m_search.owner == id && !m_search.done; }
	void forceMapRecalculation( );	///< Force pathfind map recomputation. If region is given, only that area is recomputed

	/** Returns an aircraft path to the goal.  */
	Path *getAircraftPath( const Object *obj, const Coord3D *to); 
	Path *findGroundPath( const Coord3D *from, const Coord3D *to, Int pathRadius,
		Bool crusher);	///< Find a short, valid path of the desired width on the ground.

	void addObjectToPathfindMap( class Object *obj );				///< Classify the given object's cells in the map
	void removeObjectFromPathfindMap( class Object *obj );	///< De-classify the given object's cells in the map

	void removeUnitFromPathfindMap( Object *obj );	///< De-classify the given mobile unit's cells in the map
	void updateGoal( Object *obj, const Coord3D *newGoalPos, PathfindLayerEnum layer);		///< Update the given mobile unit's cells in the map
	void updateAircraftGoal( Object *obj, const Coord3D *newGoalPos);		///< Update the given aircraft unit's cells in the map
	void removeGoal( Object *obj);		///< Removes the given mobile unit's goal cells in the map
	void updatePos( Object *obj, const Coord3D *newPos);		///< Update the given mobile unit's cells in the map
	void removePos( Object *obj);		///< Removes the unit's position cells from the map

	Bool moveAllies(Object *obj, Path *path);

	// NOTE - The object MUST NOT MOVE between the call to createAWall... and removeWall...
	// or BAD THINGS will happen.  jba.
	void createAWallFromMyFootprint( Object *obj ) {internal_classifyObjectFootprint(obj, true);}  // Temporarily treat this object as an obstacle.
	void removeWallFromMyFootprint( Object *obj ){internal_classifyObjectFootprint(obj, false);}   // Undo createAWallFromMyFootprint.

	Path *getMoveAwayFromPath(Object *obj, Object *otherObj, Path *pathToAvoid, Object *otherObj2, Path *pathToAvoid2);

	void changeBridgeState( PathfindLayerEnum layer, Bool repaired );

	Bool findBrokenBridge(const LocomotorSet &locomotorSet, const Coord3D *from, const Coord3D *to, ObjectID *bridgeID);

	void newMap(void);

	PathfindCell *getCell( PathfindLayerEnum layer, Int x, Int y );							///< Return the cell at grid coords (x,y)
	PathfindCell *getCell( PathfindLayerEnum layer, const Coord3D *pos );				///< Given a position, return associated grid cell
	PathfindCell *getClippedCell( PathfindLayerEnum layer, const Coord3D *pos );				///< Given a position, return associated grid cell
	void clip(Coord3D *from, Coord3D *to);
	Bool worldToCell( const Coord3D *pos, ICoord2D *cell );	///< Given a world position, return grid cell coordinate

	const ICoord2D *getExtent(void) const {return &m_extent.hi;}

	// ---- the flow model; see the block comment above PathfindServicesInterface ----

	/// Chamfer distance from this cell to the nearest cell nothing can stand in.  5 units per cell.
	Int getClearance( Int x, Int y ) const;
	/// Rebuild the whole clearance field.  Two linear scans over the map, no queue.
	void buildClearance( void );
	/* A structure went up or came down, so the field is stale.  It is not rebuilt on the spot:
		 during base building that would be several rebuilds a second for a field nothing reads
		 until the next search, so the flag is collected and cashed in once, from the pathfind
		 queue, at most every PF_CLEARANCE_REBUILD_FRAMES. */
	void markClearanceDirty( void ) { m_clearanceDirty = true; }
	void updateClearanceIfDirty( void );

	/// One blocked unit-frame, stamped where it happened and spread over the body's footprint.
	void noteTraffic( const Coord3D *pos, Int radiusInCells );
	void decayTraffic( void );							///< age the whole live set; call once per logic frame
	Int getTraffic( Int x, Int y ) const;

	/// Stamp where this unit expects to be over the next few seconds, from where it actually is.
	void claimPathTiming( const Object *obj, class Path *path );
	/// Weight (in halves) claimed on this cell for the bucket holding `frame`.
	Int getClaims( Int x, Int y, UnsignedInt frame ) const;
	void resetClaims( void );								///< the ground under every claim is gone

	void setIgnoreObstacleID( ObjectID objID );				///< if non-zero, the pathfinder will ignore the given obstacle
	void setIgnoreUnderConstruction( Bool ignore );			///< if true, half-built structures are not obstacles to this search
	Bool builderMayCross( const PathfindCell *cell ) const;	///< is this obstacle cell a structure the builder now searching may walk over?
	Bool builderMayCrossPinch( Int cellX, Int cellY, PathfindLayerEnum layer );	///< is this cell pinched only by a site that builder may walk over?

	Bool validMovementPosition( Bool isCrusher, LocomotorSurfaceTypeMask acceptableSurfaces, PathfindCell *toCell, PathfindCell *fromCell = NULL );		///< Return true if given position is a valid movement location
	Bool validMovementPosition( Bool isCrusher, PathfindLayerEnum layer, const LocomotorSet& locomotorSet, Int x, Int y );					///< Return true if given position is a valid movement location
	Bool validMovementPosition( Bool isCrusher, PathfindLayerEnum layer, const LocomotorSet& locomotorSet, const Coord3D *pos );		///< Return true if given position is a valid movement location
	Bool validMovementTerrain( PathfindLayerEnum layer, const Locomotor* locomotor, const Coord3D *pos );		///< Return true if given position is a valid movement location

	/** How far sideways, in world units, a body could slide from `from` along `dir` before the
			ground stops taking it.  Half-cell steps out to PF_LANE_PROBE_CELLS, then the body's own
			radius taken off the answer so a wide tank is not offered the last passable cell as a lane.
			This is the width of the band the route runs down, measured where the unit is now rather
			than stored with the path: the terrain under a route changes while units are on it. */
	Real laneExtent( const Object *obj, const LocomotorSet& locomotorSet, PathfindLayerEnum layer,
		const Coord3D *from, const Coord2D *dir );

	Locomotor* chooseBestLocomotorForPosition(PathfindLayerEnum layer,  LocomotorSet* locomotorSet, const Coord3D* pos );

	Bool isViewBlockedByObstacle(const Object* obj, const Object* objOther);	///< Return true if the straight line between the given points contains any obstacle, and thus blocks vision

	Bool isAttackViewBlockedByObstacle(const Object* obj, const Coord3D& attackerPos,  const Object* victim, const Coord3D& victimPos);	///< Return true if the straight line between the given points contains any obstacle, and thus blocks vision
	Bool isGroundViewBlockedByObstacle(const Coord3D& attackerPos, const Coord3D& victimPos, ObjectID standingIn = INVALID_ID);	///< the same walk for a ground shooter and target that are no Objects (the placement cursor's reach overlay)

	Bool isLinePassable( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, 
		PathfindLayerEnum layer, const Coord3D& startWorld, const Coord3D& endWorld, 
		Bool blocked, Bool allowPinched );	///< Return true if the straight line between the given points is passable

	void moveAlliesAwayFromDestination( Object *obj,const Coord3D& destination);	

	Bool isGroundPathPassable( Bool isCrusher, const Coord3D& startWorld, PathfindLayerEnum startLayer, 
		const Coord3D& endWorld, Int pathDiameter);	///< Return true if the straight line between the given points is passable

	// for debugging
	const Coord3D *getDebugPathPosition( void );
	void setDebugPathPosition( const Coord3D *pos );
	Path *getDebugPath( void );
	void setDebugPath( Path *debugpath );

	void cleanOpenAndClosedLists(void);

	// Adjusts the destination to a spot near dest that is not occupied by other units.
	Bool adjustDestination(Object *obj, const LocomotorSet& locomotorSet,
		Coord3D *dest, const Coord3D *groupDest=NULL);

	// Gives each ground member its own reserved goal, spreading outward from dest with the soonest
	// arrivals nearest it. goals[k] belongs to members[k]; a member never placed keeps dest itself.
	void floodGroupGoals(const Coord3D *dest, const std::vector<Object *>& members, std::vector<Coord3D>& goals);

	// Adjusts the destination to a spot near dest for landing that is not occupied by other units.
	Bool adjustToLandingDestination(Object *obj, Coord3D *dest);

	// Adjusts the destination to a spot that can attack target that is not occupied by other units.
	Bool adjustTargetDestination(const Object *obj, const Object *target, const Coord3D *targetPos, 
		const Weapon *weapon, Coord3D *dest);

	// Adjusts destination to a spot near dest that is possible to path to.
	Bool adjustToPossibleDestination(Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest);

	void snapPosition(Object *obj, Coord3D *pos); // Snaps the current position to it's grid location.
	void snapClosestGoalPosition(Object *obj, Coord3D *pos); // Snaps the current position to a good goal position.
	Bool goalPosition(Object *obj, Coord3D *pos); // Returns the goal position on the grid.

	PathfindLayerEnum addBridge(Bridge *theBridge); // Adds a bridge layer, and returns the layer id.

	void addWallPiece(Object *wallPiece); // Adds a wall piece.
	void removeWallPiece(Object *wallPiece);  // Removes a wall piece.
	Real getWallHeight(void) {return m_wallHeight;}
	Bool isPointOnWall(const Coord3D *pos);

	void updateLayer(Object *obj, PathfindLayerEnum layer); ///< Updates object's layer.

	static void classifyMapCell( Int x, Int y, PathfindCell *cell);					///< Classify the given map cell
	Int clearCellForDiameter( Bool crusher, Int cellX, Int cellY, PathfindLayerEnum layer, Int pathDiameter );		///< Return true if given position is a valid movement location
	void getRadiusAndCenter(const Object *obj, Int &iRadius, Bool &center);

protected:
	virtual Path *internalFindPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to);	///< Find a short, valid path between given locations
	Path *continueFindPath( Object *obj, const LocomotorSet& locomotorSet, Bool mayStop );	///< internalFindPath's cell search, from wherever it last stopped
	void settleSuspendedSearch( void );	///< finish a stopped search before another search takes the lists
	void dropSuspendedSearch( void );		///< throw a stopped search away
	void closeSearch( void );						///< hand back the lists of the main search that just ended
	Path *findHierarchicalPath( Bool isHuman, const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to, Bool crusher);	
	Path *findClosestHierarchicalPath( Bool isHuman, const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to, Bool crusher);	
	Path *internal_findHierarchicalPath( Bool isHuman, const LocomotorSurfaceTypeMask locomotorSurface, const Coord3D *from, const Coord3D *to, Bool crusher, Bool closestOK, Int cellCap = 0);	///< cellCap 0 searches every block
	void processHierarchicalCell( const ICoord2D &scanCell, const ICoord2D &deltaPathfindCell,
																PathfindCell *parentCell, 
																PathfindCell *goalCell, zoneStorageType parentZone, 
																zoneStorageType *examinedZones, Int &numExZones,
																Bool crusher, Int &cellCount);
	Bool checkForAdjust(Object *, const LocomotorSet& locomotorSet, Bool isHuman, Int cellX, Int cellY, 
		PathfindLayerEnum layer, Int iRadius, Bool center,Coord3D *dest, const Coord3D *groupDest) ;
	Bool checkForLanding(Int cellX, Int cellY, 
		PathfindLayerEnum layer, Int iRadius, Bool center,Coord3D *dest) ;
	Bool checkForTarget(const Object *obj, 	Int cellX, Int cellY, const Weapon *weapon,
																const Object *victim, const Coord3D *victimPos,
																Int iRadius, Bool center,Coord3D *dest) ;
	Bool checkForPossible(Bool isCrusher, Int fromZone,  Bool center, const LocomotorSet& locomotorSet,
		Int cellX, Int cellY, PathfindLayerEnum layer, Coord3D *dest, Bool startingInObstacle) ;
	void adjustCoordToCell(Int cellX, Int cellY, Bool centerInCell, Coord3D &pos, PathfindLayerEnum layer);
	Bool checkDestination(const Object *obj, Int cellX, Int cellY, PathfindLayerEnum layer, Int iRadius, Bool centerInCell);
	Bool checkForMovement(const Object *obj, TCheckMovementInfo &info);
	Bool segmentIntersectsTallBuilding(const PathNode *curNode, PathNode *nextNode,  
		ObjectID ignoreBuilding, Coord3D *insertPos1, Coord3D *insertPos2, Coord3D *insertPos3);	///< Return true if the straight line between the given points intersects a tall building.
	Bool circleClipsTallBuilding(const Coord3D *from, const Coord3D *to, Real radius, ObjectID ignoreBuilding, Coord3D *adjustTo);	///< Return true if the circle at the end of the line between the given points intersects a tall building.

	enum {NO_ATTACK=0};
	Int examineNeighboringCells(PathfindCell *parentCell, PathfindCell *goalCell,
										const LocomotorSet& locomotorSet, Bool isHumanPlayer, 
										Bool centerInCell, Int radius, const ICoord2D &startCellNdx,
										const Object *obj, Int attackDistance);

 	Bool pathDestination( Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest, 
		PathfindLayerEnum layer, const Coord3D *groupDest);	///< Checks cost between given locations

	Int checkPathCost(Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, 
		const Coord3D *to);

	void tightenPath(Object *obj, const LocomotorSet& locomotorSet, Coord3D *from, 
		const Coord3D *to);

	/**
		return 0 to continue iterating the line, nonzero to terminate the iteration. 
		the nonzero result will be returned as the result of iterateCellsAlongLine().
		iterateCellsAlongLine will return zero if it completes.
	*/
	typedef Int (*CellAlongLineProc)(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	Int iterateCellsAlongLine(const Coord3D& startWorld, const Coord3D& endWorld, 
		PathfindLayerEnum layer, CellAlongLineProc proc, void* userData);

	Int iterateCellsAlongLine(const ICoord2D &start, const ICoord2D &end, 
		PathfindLayerEnum layer, CellAlongLineProc proc, void* userData);

	static Int linePassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	static Int groundPathPassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	static Int lineBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	static Int tightenPathCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	static Int attackBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
 	static Int examineCellsCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
 	static Int groundCellsCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
 	static Int moveAlliesDestinationCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);

	static Int segmentIntersectsBuildingCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);

	void classifyMap( void );					///< Classify all cells in grid as obstacles, etc
	void classifyObjectFootprint( Object *obj, Bool insert );	/** Classify the cells under the given object
																																If 'insert' is true, object is being added 
																																If 'insert' is false, object is being removed */
	void internal_classifyObjectFootprint( Object *obj, Bool insert );	/** Classify the cells under the given object
																																If 'insert' is true, object is being added 
																																If 'insert' is false, object is being removed */
	void classifyFence( Object *obj, Bool insert );	/** Classify the cells under the given fence object. */
	void classifyUnitFootprint( Object *obj, Bool insert, Bool remove, Bool update );	/** Classify the cells under the given object If 'insert' is true, object is being added */
	/// Convert world coordinate to array index
	void worldToGrid( const Coord3D *pos, ICoord2D *cellIndex );

	Bool evaluateCell(PathfindCell* newCell, PathfindCell *parentCell,
									const LocomotorSet& locomotorSet, 
									 Bool centerInCell, Int radius, 
									 const Object *obj, Int attackDistance);

	Path *buildActualPath( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, 
		const Coord3D *fromPos, PathfindCell *goalCell, Bool center, Bool blocked );	///< Work backwards from goal cell to construct final path
	Path *buildGroundPath( Bool isCrusher,const Coord3D *fromPos, PathfindCell *goalCell, 
		Bool center, Int pathDiameter );	///< Work backwards from goal cell to construct final path
	Path *buildHierachicalPath( const Coord3D *fromPos, PathfindCell *goalCell);	///< Work backwards from goal cell to construct final path

	void  prependCells( Path *path, const Coord3D *fromPos, 
																	PathfindCell *goalCell, Bool center ); ///< Add pathfind cells to a path.

	void debugShowSearch( Bool pathFound );				///< Show all cells touched in the last search
	static LocomotorSurfaceTypeMask validLocomotorSurfacesForCellType(PathfindCell::CellType t);

	void checkChangeLayers(PathfindCell *parentCell);

#if defined _DEBUG || defined _INTERNAL
	void doDebugIcons(void) ;
#endif

private:
	/// This uses WAY too much memory.  Should at least be array of pointers to cells w/ many fewer cells
	PathfindCell *m_blockOfMapCells;		///< Pathfinding map - contains iconic representation of the map
	PathfindCell **m_map;		///< Pathfinding map indexes - contains matrix indexing into the map.

	//
	// The three flow maps.  All of them are (hi.x+1)*(hi.y+1) and indexed [x*(hi.y+1)+y], the same
	// shape as m_map, so a cell index is a cell index everywhere below.
	//
	UnsignedByte *m_clearance;								///< chamfer distance to the nearest impassable cell
	Bool m_clearanceDirty;										///< a structure changed; rebuild when the throttle allows
	UnsignedInt m_clearanceFrame;							///< logic frame the last rebuild ran on
	UnsignedByte *m_traffic;									///< where units are stopped, decayed

	/* The traffic map is nearly all zeroes nearly all of the time, and decaying it by walking the
		 whole map every third frame costs more than the map buys.  Only the cells that actually
		 hold something are visited, so an unjammed match pays nothing at all. */
	Int *m_trafficCells;											///< PF_TRAFFIC_CELLS cell indices, the non-zero set
	Int m_numTrafficCells;
	UnsignedInt m_trafficDecayFrame;					///< logic frame the last decay ran on

	/* A dense cell-by-bucket array would be twenty-four whole-map allocations to hold a few
		 thousand live claims, so they live in one open-addressed table hashed on (cell, bucket).
		 An entry whose bucket is already in the past is dead and its slot is reused; nothing ever
		 has to be released, which is what lets a unit simply re-stamp its plan once a second from
		 where it really is instead of maintaining a claim list per path. */
	PathClaim *m_claims;

	/* The last bucket any claim was written for, per cell.  Without it every neighbour of every
		 expansion pays an eight-slot hash probe to be told the cell is empty, which it nearly
		 always is; with it that is one array read and a compare, and the hash is only walked on
		 ground somebody has actually spoken for. */
	UnsignedShort *m_claimTouch;

	void addClaim( Int cell, UnsignedInt bucket, Int weight );
	void stampClaimAt( const Coord3D *pos, UnsignedInt frame, Int dilation );
	void allocateFlowMaps( void );
	void freeFlowMaps( void );
	Int flowSpeedOf( const Object *obj );

public:
	/* Arm (or disarm) the flow charge for the search about to run.  Every top-level search calls
		 it: the two that units actually move on pass their object, the hierarchical, attack, safe
		 and group-corridor searches pass NULL, because those answer "can this be reached at all"
		 and pricing traffic into that answer only makes them fail. */
	void beginFlowSearch( const Object *obj );
	/// The whole flow charge for one step into (cellX, cellY), given the ground covered to get there.
	Int flowStepCost( Int baseCost, Int cellX, Int cellY, Int radius, UnsignedInt travelSoFar );
	Bool flowCostsOn( void ) const { return m_flowCosts; }

private:

	Bool m_flowCosts;													///< is the search now running allowed to price the flow maps?
	Int m_flowSpeed;													///< cost units of travel the searching unit covers per logic frame, x256
	UnsignedInt m_flowNow;										///< logic frame the search started on, so an ETA is absolute
	UnsignedInt m_flowNowBucket;							///< and the claim bucket it falls in, so the screen costs no division

	IRegion2D m_extent;														///< Grid extent limits
	IRegion2D m_logicalExtent;										///< Logical grid extent limits

	PathfindCell *m_openList;											///< Cells ready to be explored
	PathfindCell *m_closedList;										///< Cells already explored
	Int m_probeBestDistSqr;											///< closest a straight-line-to-goal probe has been fired from, this search

	Bool m_isMapReady;														///< True if all cells of map have been classified
	Bool m_isTunneling;														///< True if path started in an obstacle

	Int m_frameToShowObstacles;										///< Time to redraw obstacles.  For debug output.

	Coord3D debugPathPos;													///< Used for visual debugging
	Path *debugPath;															///< Used for visual debugging

	ObjectID m_ignoreObstacleID;									///< Ignore the given obstacle
	Bool m_ignoreUnderConstruction;								///< Ignore every structure that is still going up (builders only)

	PathfindZoneManager m_zoneManager;						///< Handles the pathfind zones.

	PathfindLayer m_layers[LAYER_LAST+1];

	ObjectID			m_wallPieces[MAX_WALL_PIECES];
	Int						m_numWallPieces;
	Real					m_wallHeight;

	Int						m_moveAlliesDepth;


	// Pathfind queue
	ObjectID			m_queuedPathfindRequests[PATHFIND_QUEUE_LEN];
	Int						m_queuePRHead;
	Int						m_queuePRTail;
	/* Which ids sit between head and tail, by ObjectID, so queueForPath answers "already queued"
		 without walking up to 2048 slots on every request. Derived from the ring, never saved. */
	std::vector<bool>	m_isQueuedForPath;
	Int						m_cumulativeCellsAllocated;

	/* The main cell search's state, kept here rather than on the stack so a search that runs out of
		 the queue's cells for the frame can stop and carry on from the same cell next frame.  At most
		 one is ever stopped: it belongs to the unit at the head of the queue, which keeps that place
		 until the search ends, so requests are still answered in the order they came.  Its cells stay
		 on m_openList and m_closedList, so any other search first finishes it (settleSuspendedSearch)
		 and keeps the answer for the unit's next turn.  Never saved: a loaded game starts that unit's
		 search over. */
	struct ResumableSearch
	{
		ObjectID owner;							///< the unit a stopped search belongs to, INVALID_ID when none
		Bool done;									///< another search finished it, and result is the answer
		Bool claimed;								///< the owner's turn in the queue carried it on or took the answer
		Path *result;
		Coord3D from, rawTo, to;
		PathfindCell *goalCell, *parentCell, *closestCell;
		Real closestDistanceSqr;
		ICoord2D startCellNdx, goalCellNdx;
		Int radius;
		Bool centerInCell, isHuman;
		Int cellCount;							///< cells taken so far, all frames together
		Int credited;								///< of those, already charged to earlier frames' budgets
		Bool isTunneling, ignoreUnderConstruction, restricted;
		ObjectID ignoreObstacleID;
		UnsignedInt flowNow, flowNowBucket;	///< the flow clock starts when the search did, not when it resumed
		Real startDirX, startDirY;
		Bool startDirValid;
	};
	ResumableSearch m_search;
	ObjectID			m_queueOwnerID;		///< the unit whose turn in the queue is running, INVALID_ID outside it

	/* diagnostics only - never read by the simulation, so it stays out of xfer().  Records
		 whether the cell search now running was fenced into a hierarchical corridor or was let
		 loose on the whole map, so the slow-search log can say which of the two is costing the
		 frame. */
	Bool					m_lastSearchRestricted;
};


inline void Pathfinder::setIgnoreObstacleID( ObjectID objID )
{
	m_ignoreObstacleID = objID;
}

inline void Pathfinder::setIgnoreUnderConstruction( Bool ignore )
{
	m_ignoreUnderConstruction = ignore;
}

inline void Pathfinder::worldToGrid( const Coord3D *pos, ICoord2D *cellIndex ) 
{ 
	cellIndex->x = REAL_TO_INT(pos->x/PATHFIND_CELL_SIZE); 
	cellIndex->y = REAL_TO_INT(pos->y/PATHFIND_CELL_SIZE); 
}

inline Bool Pathfinder::validMovementPosition( Bool isCrusher, PathfindLayerEnum layer, const LocomotorSet& locomotorSet, Int x, Int y )
{
	return validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), getCell( layer, x, y ) );
}

inline Bool Pathfinder::validMovementPosition( Bool isCrusher, PathfindLayerEnum layer, const LocomotorSet& locomotorSet, const Coord3D *pos )
{

	Int x = REAL_TO_INT(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT(pos->y/PATHFIND_CELL_SIZE);

	return validMovementPosition( isCrusher, layer, locomotorSet, x, y );
}

inline const Coord3D *Pathfinder::getDebugPathPosition( void ) 
{ 
	return &debugPathPos; 
}

inline void Pathfinder::setDebugPathPosition( const Coord3D *pos ) 
{ 
	debugPathPos = *pos; 
}

inline Path *Pathfinder::getDebugPath( void ) 
{ 
	return debugPath; 
}

inline void Pathfinder::addObjectToPathfindMap( class Object *obj ) 
{ 
	classifyObjectFootprint( obj, true ); 
}

inline void Pathfinder::removeObjectFromPathfindMap( class Object *obj ) 
{ 
	classifyObjectFootprint( obj, false ); 
}

inline PathfindCell *Pathfinder::getCell( PathfindLayerEnum layer, Int x, Int y ) 
{ 
	if (x >= m_extent.lo.x && x <= m_extent.hi.x &&
		y >= m_extent.lo.y && y <= m_extent.hi.y)	
	{
		PathfindCell *cell = NULL;
		if (layer > LAYER_GROUND && layer <= LAYER_LAST) 
		{
			cell = m_layers[layer].getCell(x, y);
			if (cell) 
				return cell;
		}
		return &m_map[x][y]; 
	}
	else
	{
		return NULL;
	}
}

inline PathfindCell *Pathfinder::getCell( PathfindLayerEnum layer, const Coord3D *pos ) 
{
	ICoord2D cell;
	Bool overflow = worldToCell( pos, &cell );
	if (overflow) return NULL;
	return getCell( layer, cell.x, cell.y );
}

inline PathfindCell *Pathfinder::getClippedCell( PathfindLayerEnum layer, const Coord3D *pos)
{
	ICoord2D cell;
	worldToCell( pos, &cell );
	return getCell( layer, cell.x, cell.y );
}

inline Bool Pathfinder::worldToCell( const Coord3D *pos, ICoord2D *cell )
{
	cell->x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	cell->y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);
	Bool overflow = false;
	if (cell->x < m_extent.lo.x) {overflow = true; cell->x = m_extent.lo.x;}
	if (cell->y < m_extent.lo.y) {overflow = true; cell->y = m_extent.lo.y;}
	if (cell->x > m_extent.hi.x) {overflow = true; cell->x = m_extent.hi.x;}
	if (cell->y > m_extent.hi.y) {overflow = true; cell->y = m_extent.hi.y;}
	return overflow;
}

/* Both of these are read once per neighbour per expansion - a few million times in a busy frame -
	 so they are a bounds check and an array index and nothing else.  Off the map reads as "no room
	 at all" for clearance and "empty" for traffic, which is what the search wants at the edge. */
inline Int Pathfinder::getClearance( Int x, Int y ) const
{
	if (m_clearance == NULL) return PF_CLEARANCE_MAX;
	if (x < m_extent.lo.x || y < m_extent.lo.y || x > m_extent.hi.x || y > m_extent.hi.y) return 0;
	return m_clearance[x*(m_extent.hi.y+1) + y];
}

inline Int Pathfinder::getTraffic( Int x, Int y ) const
{
	if (m_traffic == NULL) return 0;
	if (x < m_extent.lo.x || y < m_extent.lo.y || x > m_extent.hi.x || y > m_extent.hi.y) return 0;
	return m_traffic[x*(m_extent.hi.y+1) + y];
}

/**
 * Return true if the given object ID is registered as an obstacle in this cell
 */
inline Bool PathfindCell::isObstaclePresent( ObjectID objID ) const
{
	if (objID != INVALID_ID && (getType() == PathfindCell::CELL_OBSTACLE))
	{
		return (m_obstacleID == objID);
	}

	return false;
}


#endif // _PATHFIND_H_
