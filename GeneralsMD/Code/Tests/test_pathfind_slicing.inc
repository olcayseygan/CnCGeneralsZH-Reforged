// The pathfinder's zones, rebuilt over several frames, and the path queue that serves human players'
// units before computer players'.  Both are GameLogic: every machine has to come out with the same
// zones on the same frame and answer the same requests in the same order.

#include "GameLogic/LocomotorSet.h"

/* A map of the pathfinder's cells, laid out the way Pathfinder::newMap lays them: one block, and a
	 column pointer per x.  Terrain is patches of one type with speckle, so the pass has runs to merge,
	 blocks with one zone and blocks with many, a few cells that reach a bridge layer, and a width and
	 height that leave partial blocks at the edges. */
struct ZoneTestMap
{
	enum { W = 67, H = 59 };
	PathfindCell *block;
	PathfindCell *columns[ W ];
	PathfindLayer layers[ LAYER_LAST + 1 ];
	IRegion2D bounds;

	ZoneTestMap( UnsignedInt seed )
	{
		block = new PathfindCell[ W * H ];
		for( Int i = 0; i < W; ++i )
			columns[ i ] = &block[ i * H ];
		bounds.lo.x = 0;
		bounds.lo.y = 0;
		bounds.hi.x = W - 1;
		bounds.hi.y = H - 1;
		static const PathfindCell::CellType types[] = {
			PathfindCell::CELL_CLEAR, PathfindCell::CELL_WATER, PathfindCell::CELL_CLIFF,
			PathfindCell::CELL_RUBBLE, PathfindCell::CELL_OBSTACLE, PathfindCell::CELL_BRIDGE_IMPASSABLE,
			PathfindCell::CELL_IMPASSABLE };
		for( Int i = 0; i < W; ++i )
			for( Int j = 0; j < H; ++j )
			{
				// patches seven cells across, a type each, then one cell in nine flipped
				UnsignedInt h = ( (UnsignedInt)( i / 7 ) * 73856093u ) ^ ( (UnsignedInt)( j / 7 ) * 19349663u ) ^ seed;
				h ^= h >> 13;
				h *= 0x5bd1e995u;
				h ^= h >> 15;
				Int t = (Int)( h % 7 );
				if( t >= 4 && ( h & 0x100 ) )
					t = 0;		// mostly open ground, as a map is
				UnsignedInt s = (UnsignedInt)( i * 31 + j * 17 ) * 2654435761u ^ seed;
				if( ( s >> 7 ) % 9 == 0 )
					t = (Int)( ( s >> 11 ) % 7 );
				columns[ i ][ j ].setType( types[ t ] );
				if( ( s >> 5 ) % 97 == 0 )
					columns[ i ][ j ].setConnectLayer( (PathfindLayerEnum)( 1 + ( s >> 3 ) % 3 ) );
			}
	}
	~ZoneTestMap() { delete [] block; }

	void setType( Int i, Int j, PathfindCell::CellType t ) { columns[ i ][ j ].setType( t ); }
};

/* Everything a query can ask the zone manager about every cell, folded into one number. */
static UnsignedInt zoneAnswersHash( const PathfindZoneManager &zm, ZoneTestMap &m )
{
	static const LocomotorSurfaceTypeMask masks[] = {
		LOCOMOTORSURFACE_GROUND,
		LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_WATER,
		LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_CLIFF,
		LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_RUBBLE,
		LOCOMOTORSURFACE_WATER };
	UnsignedInt h = 2166136261u;
	#define ZONE_HASH( v ) { h ^= (UnsignedInt)( v ); h *= 16777619u; }
	for( Int i = 0; i < ZoneTestMap::W; ++i )
		for( Int j = 0; j < ZoneTestMap::H; ++j )
		{
			const zoneStorageType zone = m.columns[ i ][ j ].getZone();
			ZONE_HASH( zone );
			ZONE_HASH( zm.getEffectiveTerrainZone( zone ) );
			for( Int k = 0; k < (Int)( sizeof( masks ) / sizeof( masks[ 0 ] ) ); ++k )
				for( Int crusher = 0; crusher < 2; ++crusher )
				{
					ZONE_HASH( zm.getEffectiveZone( masks[ k ], crusher != 0, zone ) );
					ZONE_HASH( zm.getBlockZone( masks[ k ], crusher != 0, i, j, m.columns ) );
				}
			if( i % PathfindZoneManager::ZONE_BLOCK_SIZE == 0 && j % PathfindZoneManager::ZONE_BLOCK_SIZE == 0 )
				ZONE_HASH( zm.interactsWithBridge( i, j ) );
		}
	for( Int l = 0; l <= LAYER_LAST; ++l )
		ZONE_HASH( m.layers[ l ].getZone() );
	#undef ZONE_HASH
	return h;
}

/* What calculateZones answered on these maps before the pass could be spread over frames, taken
	 from the build before the change.  The pass has to come out the same, or every map plays
	 differently from today. */
static const UnsignedInt theZoneSeeds[] = { 1u, 77u, 0xC0FFEEu };
static const UnsignedInt theZoneHashes[] = { 0xAC65A635u, 0x91C3103Fu, 0xC80E008Fu };

/* PathfindCell::s_zoneSlot is one for the game, which has one zone manager.  Each of these tests
	 reads a manager's answers before the next manager swaps, since a swap flips the slot every map's
	 cells read. */

TEST(pathfind_zones_answer_as_they_did_before_slicing)
{
	for( Int s = 0; s < 3; ++s )
	{
		ZoneTestMap m( theZoneSeeds[ s ] );
		PathfindZoneManager zm;
		zm.allocateBlocks( m.bounds );
		zm.calculateZones( m.columns, m.layers, m.bounds );
		CHECK_EQ( zoneAnswersHash( zm, m ), theZoneHashes[ s ] );
	}
}

static UnsignedInt slicedZoneHash( UnsignedInt seed, Int budget, Int *calls )
{
	ZoneTestMap m( seed );
	PathfindZoneManager zm;
	zm.allocateBlocks( m.bounds );
	zm.beginRebuild( m.columns, m.layers, m.bounds );
	*calls = 1;
	while( !zm.stepRebuild( budget ) )
		++*calls;
	return zoneAnswersHash( zm, m );
}

/* Cut anywhere, the pass comes out as the one-frame pass did; and a map of this size, four
	 thousand cells, takes one call at the game's budget, which is what keeps small maps as they were. */
TEST(pathfind_zones_rebuilt_in_slices_answer_as_the_one_frame_pass)
{
	for( Int s = 0; s < 3; ++s )
	{
		Int calls = 0;
		CHECK_EQ( slicedZoneHash( theZoneSeeds[ s ], 1, &calls ), theZoneHashes[ s ] );
		CHECK( calls > 20 );		// one block column or cell column a call
		CHECK_EQ( slicedZoneHash( theZoneSeeds[ s ], 500, &calls ), theZoneHashes[ s ] );
		CHECK( calls > 4 );
		CHECK_EQ( slicedZoneHash( theZoneSeeds[ s ], PathfindZoneManager::REBUILD_CELLS_PER_FRAME, &calls ), theZoneHashes[ s ] );
		CHECK_EQ( calls, 1 );
	}
}

static void wallOffColumn( ZoneTestMap &m, Int x )
{
	for( Int j = 0; j < ZoneTestMap::H; ++j )
		m.setType( x, j, PathfindCell::CELL_IMPASSABLE );
}

/* Until the call that swaps, every query gets the answer it got before the pass began, though the
	 map under it changed; after it, the answers are the changed map's. */
TEST(pathfind_zone_queries_read_the_old_zones_until_the_swap)
{
	ZoneTestMap m( 77u );
	PathfindZoneManager zm;
	zm.allocateBlocks( m.bounds );
	zm.calculateZones( m.columns, m.layers, m.bounds );
	const UnsignedInt before = zoneAnswersHash( zm, m );
	wallOffColumn( m, 30 );

	zm.beginRebuild( m.columns, m.layers, m.bounds );
	Int calls = 0;
	Int changedEarly = 0;
	while( !zm.stepRebuild( 300 ) )
	{
		++calls;
		if( zoneAnswersHash( zm, m ) != before )
			++changedEarly;
	}
	CHECK( calls > 5 );
	CHECK_EQ( changedEarly, 0 );
	CHECK( zm.isRebuilding() == FALSE );
	const UnsignedInt after = zoneAnswersHash( zm, m );
	CHECK( after != before );

	ZoneTestMap walled( 77u );
	wallOffColumn( walled, 30 );
	PathfindZoneManager zw;
	zw.allocateBlocks( walled.bounds );
	zw.calculateZones( walled.columns, walled.layers, walled.bounds );
	CHECK_EQ( zoneAnswersHash( zw, walled ), after );
}

/* A structure put down while the pass runs, across ground the pass has read and ground it has not:
	 its cells get what a structure gets between passes (updateZonesForModify) when the pass swaps,
	 every other cell keeps a zone, and the pass the change asked for sees the map as it is. */
TEST(pathfind_zones_changed_during_a_rebuild_are_kept_for_the_swap)
{
	ZoneTestMap m( 1u );
	PathfindZoneManager zm;
	zm.allocateBlocks( m.bounds );
	zm.calculateZones( m.columns, m.layers, m.bounds );

	zm.beginRebuild( m.columns, m.layers, m.bounds );
	for( Int k = 0; k < 3; ++k )
		CHECK( !zm.stepRebuild( 1 ) );		// three block columns read: x 0 to 29

	IRegion2D box;
	box.lo.x = 24;
	box.lo.y = 20;
	box.hi.x = 36;
	box.hi.y = 27;
	for( Int i = box.lo.x; i <= box.hi.x; ++i )
		for( Int j = box.lo.y; j <= box.hi.y; ++j )
		{
			// what classifyObjectFootprint does to a cell a structure takes
			m.setType( i, j, PathfindCell::CELL_OBSTACLE );
			m.columns[ i ][ j ].clearZone();
		}
	zm.updateZonesForModify( m.columns, m.layers, box, m.bounds );

	while( !zm.stepRebuild( 1 ) )
		;
	Int unset = 0;
	for( Int i = 0; i < ZoneTestMap::W; ++i )
		for( Int j = 0; j < ZoneTestMap::H; ++j )
		{
			const Bool inBox = i >= box.lo.x && i <= box.hi.x && j >= box.lo.y && j <= box.hi.y;
			if( !inBox && m.columns[ i ][ j ].getZone() == PathfindZoneManager::UNINITIALIZED_ZONE )
				++unset;
		}
	CHECK_EQ( unset, 0 );

	zm.beginRebuild( m.columns, m.layers, m.bounds );
	while( !zm.stepRebuild( 1 ) )
		;
	const UnsignedInt next = zoneAnswersHash( zm, m );

	ZoneTestMap built( 1u );
	for( Int i = box.lo.x; i <= box.hi.x; ++i )
		for( Int j = box.lo.y; j <= box.hi.y; ++j )
			built.setType( i, j, PathfindCell::CELL_OBSTACLE );
	PathfindZoneManager zb;
	zb.allocateBlocks( built.bounds );
	zb.calculateZones( built.columns, built.layers, built.bounds );
	CHECK_EQ( zoneAnswersHash( zb, built ), next );
}

// ------------------------------------------------------------------------------------------------
// The path queue's two lines

/* A stand-in for the pathfinder's side: records who was answered, has cells for `budget` answers,
	 and stops the search of `stopsOn` once, which spends the frame. */
struct FakeQueueServer
{
	std::vector<ObjectID> served;
	Int budget;
	ObjectID stopsOn;
	FakeQueueServer( Int b, ObjectID stop ) : budget( b ), stopsOn( stop ) {}
	Bool hasBudget( void ) const { return budget > 0; }
	Bool serve( PathRequestRing &ring )
	{
		const ObjectID id = ring.front();
		served.push_back( id );
		--budget;
		if( id == stopsOn )
		{
			stopsOn = INVALID_ID;
			budget = 0;
			return FALSE;
		}
		return TRUE;
	}
	Bool servedInOrder( const ObjectID *ids, Int n ) const
	{
		if( (Int)served.size() != n )
			return FALSE;
		for( Int i = 0; i < n; ++i )
			if( served[ i ] != ids[ i ] )
				return FALSE;
		return TRUE;
	}
};

static PathRequestRing theHumanLine, theComputerLine;

static void fillLine( PathRequestRing &ring, const ObjectID *ids, Int n )
{
	ring.clear();
	for( Int i = 0; i < n; ++i )
		ring.push( ids[ i ], 0 );
}

/* A person's order is answered before the computer's requests already waiting, and one given
	 later still goes ahead of the computer's that are left. */
TEST(pathfind_queue_answers_human_players_before_computer_players)
{
	const ObjectID computer[] = { (ObjectID)1, (ObjectID)2, (ObjectID)3, (ObjectID)4 };
	const ObjectID human[] = { (ObjectID)10, (ObjectID)11 };
	fillLine( theComputerLine, computer, 4 );
	fillLine( theHumanLine, human, 2 );

	FakeQueueServer first( 100, INVALID_ID );
	CHECK_EQ( Pathfinder_servePathQueues( theHumanLine, theComputerLine, INVALID_ID, 3, first ), 3 );
	const ObjectID firstOrder[] = { (ObjectID)10, (ObjectID)11, (ObjectID)1 };
	CHECK( first.servedInOrder( firstOrder, 3 ) );
	CHECK( theHumanLine.isEmpty() );
	CHECK_EQ( theComputerLine.depth(), 3 );

	theHumanLine.push( (ObjectID)12, 5 );
	FakeQueueServer second( 100, INVALID_ID );
	Pathfinder_servePathQueues( theHumanLine, theComputerLine, INVALID_ID, 3, second );
	const ObjectID secondOrder[] = { (ObjectID)12, (ObjectID)2, (ObjectID)3 };
	CHECK( second.servedInOrder( secondOrder, 3 ) );

	// with nobody human waiting it is one line, in the order it came
	FakeQueueServer third( 100, INVALID_ID );
	Pathfinder_servePathQueues( theHumanLine, theComputerLine, INVALID_ID, 3, third );
	const ObjectID thirdOrder[] = { (ObjectID)4 };
	CHECK( third.servedInOrder( thirdOrder, 1 ) );
	CHECK( theComputerLine.isEmpty() );
}

/* A computer search the cells ran out on holds the open lists, so it finishes first; if it stops
	 again that is the frame, and the person's request waits one frame rather than throwing the
	 search away, which under a stream of human requests would never finish. */
TEST(pathfind_queue_lets_a_stopped_search_finish_first)
{
	const ObjectID computer[] = { (ObjectID)5, (ObjectID)6 };
	const ObjectID human[] = { (ObjectID)20 };
	fillLine( theComputerLine, computer, 2 );
	fillLine( theHumanLine, human, 1 );
	FakeQueueServer finishes( 100, INVALID_ID );
	CHECK_EQ( Pathfinder_servePathQueues( theHumanLine, theComputerLine, (ObjectID)5, 3, finishes ), 3 );
	const ObjectID finishOrder[] = { (ObjectID)5, (ObjectID)20, (ObjectID)6 };
	CHECK( finishes.servedInOrder( finishOrder, 3 ) );

	fillLine( theComputerLine, computer, 2 );
	fillLine( theHumanLine, human, 1 );
	FakeQueueServer stopsAgain( 100, (ObjectID)5 );
	CHECK_EQ( Pathfinder_servePathQueues( theHumanLine, theComputerLine, (ObjectID)5, 3, stopsAgain ), 1 );
	CHECK( theComputerLine.front() == (ObjectID)5 );
	CHECK( theHumanLine.front() == (ObjectID)20 );

	// a human search that stops keeps the head of its own line, and goes first next frame anyway
	const ObjectID people[] = { (ObjectID)20, (ObjectID)21 };
	fillLine( theHumanLine, people, 2 );
	FakeQueueServer humanStops( 100, (ObjectID)20 );
	CHECK_EQ( Pathfinder_servePathQueues( theHumanLine, theComputerLine, INVALID_ID, 3, humanStops ), 1 );
	CHECK( theHumanLine.front() == (ObjectID)20 );
	CHECK_EQ( theHumanLine.depth(), 2 );
}

/* The cells and the request cap bound both lines together: the computer gets what is left. */
TEST(pathfind_queue_shares_one_budget_between_the_lines)
{
	const ObjectID computer[] = { (ObjectID)1, (ObjectID)2 };
	const ObjectID human[] = { (ObjectID)10, (ObjectID)11, (ObjectID)12 };
	fillLine( theComputerLine, computer, 2 );
	fillLine( theHumanLine, human, 3 );
	FakeQueueServer twoCells( 2, INVALID_ID );
	CHECK_EQ( Pathfinder_servePathQueues( theHumanLine, theComputerLine, INVALID_ID, 6, twoCells ), 2 );
	CHECK_EQ( theComputerLine.depth(), 2 );

	FakeQueueServer capTwo( 100, INVALID_ID );
	CHECK_EQ( Pathfinder_servePathQueues( theHumanLine, theComputerLine, INVALID_ID, 2, capTwo ), 2 );
	const ObjectID capOrder[] = { (ObjectID)12, (ObjectID)1 };
	CHECK( capTwo.servedInOrder( capOrder, 2 ) );
}

/* A deep queue gets more cells a frame in steps, to four times; a person waiting on a computer's
	 stopped search gets the four times at once. */
TEST(pathfind_queue_cells_grow_with_the_backlog)
{
	CHECK_EQ( Pathfinder_queueCellBudget( 2500, 0, FALSE ), 2500 );
	CHECK_EQ( Pathfinder_queueCellBudget( 2500, PATHFIND_DEEP_QUEUE, FALSE ), 2500 );
	CHECK_EQ( Pathfinder_queueCellBudget( 2500, PATHFIND_DEEP_QUEUE + PATHFIND_DEEP_STEP - 1, FALSE ), 2500 );
	CHECK_EQ( Pathfinder_queueCellBudget( 2500, PATHFIND_DEEP_QUEUE + PATHFIND_DEEP_STEP, FALSE ), 5000 );
	CHECK_EQ( Pathfinder_queueCellBudget( 2500, 300, FALSE ), 7500 );
	CHECK_EQ( Pathfinder_queueCellBudget( 2500, PATHFIND_QUEUE_LEN, FALSE ), 10000 );
	CHECK_EQ( Pathfinder_queueCellBudget( 2500, 1, TRUE ), 10000 );
}

TEST(pathfind_queue_line_holds_one_less_than_its_length)
{
	PathRequestRing &r = theComputerLine;
	r.clear();
	for( Int i = 0; i < PATHFIND_QUEUE_LEN - 1; ++i )
		CHECK( r.push( (ObjectID)( i + 1 ), 0 ) );
	CHECK( !r.push( (ObjectID)9999, 0 ) );
	CHECK_EQ( r.depth(), PATHFIND_QUEUE_LEN - 1 );
	r.pop();
	CHECK( r.push( (ObjectID)9999, 0 ) );		// round the end of the array
	CHECK( r.front() == (ObjectID)2 );
	CHECK_EQ( r.depth(), PATHFIND_QUEUE_LEN - 1 );
}
