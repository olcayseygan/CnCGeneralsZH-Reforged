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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// RandomMapGenerator.cpp
// Writes a .map image straight into memory: the same CkMp chunk stream
// WorldBuilder saves (see WHeightMapEdit::saveToFile), minus everything a
// skirmish map does not need.
//
// Nothing here is laid out by hand. The ground is warped fractal noise with
// mesas cut into it; the start positions, the money, the water and the scenery
// are all found in that ground by looking for the places that suit them. Two
// seeds are two different maps rather than one map turned round.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <math.h>
#include <float.h>
#include <algorithm>
#include <map>

#include "Common/RandomMapGenerator.h"
#include "Common/RandomMapTemplate.h"
#include "Common/ThingFactory.h"
#include "Lib/Trig.h"
#include "Common/crc.h"
#include "Common/DataChunk.h"
#include "Common/Dict.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "GameLogic/FPUControl.h"
#include "Common/JobSystem.h"

/** The sentinel WorldHeightMap::ParseBlendTileData asserts on after every blend entry. It is
	FLAG_VAL in WorldHeightMap.h, which lives in GameEngineDevice and so cannot be included from
	here; the number is the file format's, not that header's. */
static const Int RMG_BLEND_FLAG_VALUE = 0x7ADA0000;

// Chunk versions the writers of these chunks use. They live in the .cpp files
// that write them (SidesList.cpp, Scripts.cpp), not in MapReaderWriterInfo.h.
static const Int K_SIDES_DATA_VERSION_3 = 3;
static const Int K_SCRIPTS_DATA_VERSION_1 = 1;
static const Int K_SCRIPT_LIST_DATA_VERSION_1 = 1;

//-----------------------------------------------------------------------------
// The map we build
//-----------------------------------------------------------------------------

// Terrain outside the playable area. The camera looks across it at the map
// edges, so it is scenery, not play space.
#define RMG_BORDER_CELLS		20

// A texture class the sheet table (theTextureSheets) does not know is read as the four tiles of a 2x2
// sheet, which every terrain image holds.
#define RMG_TILES_PER_CLASS		4
#define RMG_TILE_SHEET_WIDTH	2

// The cliff mapping (rmgCliffMapping). A cliff class's cell takes one when both its edges down the fall
// rise more than this many height bytes: WorldBuilder's limit (WHeightMapEdit::doCliffAdjustment, half
// its STRETCH_LIMIT over HEIGHT_SCALE), past which a face drawn by its height stretches the tile no
// more than 1.6 times.
#define RMG_CLIFF_UV_RISE		12
// Entries the reader holds, the default at 0 included (NUM_CLIFF_INFO in WorldHeightMap.h); a chunk
// claiming more throws ERROR_CORRUPT_FILE_FORMAT, so the mapping keeps the steepest cells under it.
#define RMG_NUM_CLIFF_INFO		32384
// A cell is 32 texels of a TEXTURE_WIDTH that is 32 tiles of 64, so 1/64 of it; a height byte is a
// sixteenth of a cell's width (MAP_HEIGHT_SCALE over MAP_XY_FACTOR), 1/1024. The mapping counts in
// 2048ths, which keeps every coordinate it writes exact in a float, whatever the rounding mode.
#define RMG_CLIFF_UV_UNITS		2048
#define RMG_CLIFF_UV_CELL		32		///< RMG_CLIFF_UV_UNITS a cell across
#define RMG_CLIFF_UV_BYTE		2		///< and a height byte up

// How big a map is: a floor everybody gets, plus a share for each player. A normal two-player map
// is 248 cells across, which is 2480 world units - about what a shipped duel map measures - and an
// eight-player one is 416. An eight-player huge map is 672, a little under Death Valley's 710 with
// its border, the largest shipped map.
#define RMG_TINY_FLOOR			112
#define RMG_TINY_PER_PLAYER		14
#define RMG_SMALL_FLOOR			144
#define RMG_SMALL_PER_PLAYER	20
#define RMG_NORMAL_FLOOR		192
#define RMG_NORMAL_PER_PLAYER	28
#define RMG_LARGE_FLOOR			240
#define RMG_LARGE_PER_PLAYER	36
#define RMG_HUGE_FLOOR			288
#define RMG_HUGE_PER_PLAYER		48

// Height field, in height map bytes (a byte is MAP_HEIGHT_SCALE world units).
#define RMG_BASE_HEIGHT			60.0f
// Five octaves of Perlin average out to about a third of their nominal range, so this is roughly
// three times the relief it looks like: about 80 height bytes from valley to hilltop, which is
// three or four terraces of the step below.
#define RMG_AMPLITUDE			120.0f
#define RMG_OCTAVES				5
#define RMG_FEATURES_PER_MAP	4.5f
#define RMG_WARP_STRENGTH		0.45f	///< how far the noise drags its own coordinates

/* Rolling ground is the default, the way Twilight Flame is built: hills a unit can drive, not a
	stack of tables. A second field cuts valleys, and only the peaks of a third field still sit
	on a shelf, so some high ground drops off as a cliff and the rest of the map keeps its slope.
	A shelf still uses this step so a peak that does flatten is steep enough at the rim. How deep
	the valleys cut and where the shelves start is the map type's (theMapTypes). */
#define RMG_TERRACE_STEP		24.0f	///< height bytes; a cliff needs a span of more than 15.7
#define RMG_TERRACE_DETAIL		7.0f	///< bytes of roll on the ground, shelves included
#define RMG_DETAIL_FEATURES		16.0f

// Water. Basins flood the lowest pockets the noise left, following the valleys rather than
// stamping a wobbly circle, and a stream is walked downhill from a high trough to join them.
// Written out as water areas the engine reads as impassable to anything that cannot swim.
#define RMG_WATER_DROP			26.0f	///< height bytes: water surface below the base height
#define RMG_LAKE_DEPTH			13.0f	///< and how far the bed sits below that surface
// Wide, because the shore is what the renderer's soft water edge is drawn on: it looks for cells
// whose corners straddle the water plane, and a bank that drops in one step gives it nothing.
#define RMG_LAKE_SHORE			18.0f	///< cells the basin eases out over
/* Dry land has to be dry. A lake is a polygon, not a flood, so ground outside one that sits below
	the water surface is not filled in - it is drawn as land with the lake standing above it like a
	puddle on a table, which is exactly what the noise leaves behind when it puts a lake in the
	lowest ground it can find. */
#define RMG_LAKE_BANK			4.0f	///< height bytes the land outside a lake is kept above it
#define RMG_WAVE_SPACING		16.0f	///< cells of shoreline between two ambient wave emitters
#define RMG_LAKE_MIN_CELLS		96
#define RMG_LAKE_AREA			0.012f	///< fraction of playable*playable each basin may drown
#define RMG_LAKE_FILL_RISE		18.0f	///< height bytes a basin may climb from its seed
#define RMG_LAKE_EDGE_BAND		14.0f	///< cells inside the water margin where a basin climbs a false slope
#define RMG_LAKE_EDGE_RISE		30.0f	///< height bytes that slope adds at the margin itself
#define RMG_LAKE_MIN_FILL		80		///< cells; smaller is a puddle and is thrown back
#define RMG_RIVER_MIN_LENGTH	36		///< downhill steps before a stream counts
#define RMG_FORD_STUB_STEPS		4.0f	///< river steps past a ford's half-width a stretch beside it must run, or it goes dry
#define RMG_POLYGON_MAX			96		///< sides written into the water area
#define RMG_RIVER_POLYGON_MAX	256		///< and into a stretch of river, whose banks bend every few cells
/* Lakes are kept round enough to read as lakes. The flood pays for distance from its seed as well as
	for height, so it fills the pocket round the seed before it runs up a valley; then arms thinner
	than an opening's disc are taken off, bays as thin closed, and what still reaches past a share of
	the lake's own radius from its middle is cut back. Left to the height alone a basin followed every
	gully and came out as a claw or a hook. */
#define RMG_LAKE_ROUNDING		1.4f	///< height bytes the flood pays per cell from its seed
#define RMG_LAKE_OPEN			4		///< cells: water thinner than twice this is not a lake
#define RMG_LAKE_CLOSE			2		///< cells: land thinner than twice this inside a lake is water
#define RMG_LAKE_REACH			1.3f	///< of the radius a lake's area gives, the furthest its water reaches
#define RMG_LAKE_STRETCH		1.5f	///< at most, the square root of how much longer than wide a plain map's lake grows
#define RMG_LAKE_OPEN_SHARE		0.45f	///< of that radius, the disc a big lake is opened with, so its arms scale with it
#define RMG_LAKE_OPEN_MOST		9		///< cells, the widest that disc gets
#define RMG_LAKE_CLOSE_SHARE	0.25f	///< of that radius, the disc each lake is closed with after, filling its bites
#define RMG_LAKE_CLOSE_MOST		5		///< cells, the widest that disc gets
#define RMG_LAKE_FILL_LEAST		0.8f	///< below this share of the ellipse its spread draws (an oval about 1, two blobs on a neck 0.68, a crescent 0.6) a plain map's lake is opened again
#define RMG_LAKE_REOPEN_SHARE	0.45f	///< of the radius of a disc its size, the disc it is opened with then: a 2 to 1 oval keeps its length
#define RMG_LAKE_DRY_BELOW		0.2f	///< and under this on 4 pi area over cell edges squared after that (a disc about 0.6, a 3 to 1 oval 0.44) it goes dry
#define RMG_LAKE_REOPEN_KEEP	0.75f	///< share of its water a reopened lake must keep, or it is a long valley lake and stays as it was
#define RMG_STREAM_OPEN			2		///< the opening every water mask gets once the stream has joined it
// A river map's river runs from map edge to map edge, bends and changes width as it goes.
#define RMG_RIVER_BEND			0.09f	///< of the playable size, the river's long swing off its line
#define RMG_RIVER_MEANDER		0.05f	///< and its shorter meander on top
#define RMG_RIVER_NARROW		0.55f	///< of the river's mean half-width, the narrowest it gets
#define RMG_RIVER_WIDE			1.6f	///< and the widest
#define RMG_FORD_DRY_STEPS		2.0f	///< river steps past the half-width either side of a ford that stay dry
#define RMG_FORD_TAPER_STEPS	14.0f	///< and the steps over which the river narrows into the ford
#define RMG_FORD_TIP			0.4f	///< of its width, what the river is down to where the ford begins

// A start position gets a flat disc to build on, easing back into the terrain.
#define RMG_FLAT_RADIUS			13.0f
#define RMG_BLEND_RADIUS		26.0f
#define RMG_BASE_FLAT_FLOOR		14.2f	///< cells of flat round a start on every bearing, the pathfinder's square inside it
#define RMG_BASE_FLAT_SWING		5.5f	///< cells the flat edge wanders out past that floor
#define RMG_BASE_BLEND_SWING	0.6f	///< share of its width the blend's outer edge wanders out by
#define RMG_BASE_SHORE_RAMP		4.0f	///< cells past the flat's edge over which a lake takes the blend back
#define RMG_START_MARGIN		10.0f	///< cells of playable area kept outside the blend disc
#define RMG_START_EDGE_FRACTION	0.14f	///< and this much of the map besides, so a base has ground behind it
#define RMG_START_STRIDE		3		///< cells between the spots the search looks at
#define RMG_START_ROUGHNESS		2.6f	///< height bytes a base site may vary by, on average
#define RMG_START_STRICT_SITES_EACH	4		///< dry-flat sites per seat under which the looser sites are taken too
#define RMG_START_LOOSE_PENALTY	4.0f	///< roughness added to a site only the looser shore test passes
#define RMG_CIRCLE_PLAYERS		6		///< this many seats and the starts sit on a ring
#define RMG_CIRCLE_RADIUS		0.36f	///< fraction of the playable size, from the centre
#define RMG_START_RING_TURNS	6		///< rotations of the ring tried within one seat's arc
#define RMG_START_RING_SIZES	2		///< ring radii tried, each this much smaller than the last
#define RMG_START_RING_STEP		0.02f
#define RMG_START_RIVER_RING_SIZES	4		///< sizes of the mirrored river ring walked for six and eight seats
#define RMG_START_CLOSE_COST	1.0f	///< score a layout loses for each share its closest pair gives up
#define RMG_START_CLIFF_COST	30		///< flat steps a cliff cell is worth when seats are compared: the ring routes go a long way round one
#define RMG_START_NUDGES		4		///< seat moves tried on the chosen layout
#define RMG_START_NUDGE_CELLS	12.0f	///< and how far one may go

// Money. One dock beside each base, one more out where it has to be fought
// over, and two oil derricks a player somewhere in between.
#define RMG_HOME_SUPPLY_MIN		18.0f	///< cells from the start
#define RMG_HOME_SUPPLY_MAX		30.0f
#define RMG_FAR_SUPPLY_SEPARATION	0.22f	///< fraction of the playable size
#define RMG_DERRICKS_PER_PLAYER	2
#define RMG_SMALL_PILES_PER_PLAYER	2
#define RMG_PILE_CLEARANCE		4.0f	///< the small bale is a cylinder 16 world units across
#define RMG_PILE_PAD			2.0f
#define RMG_PILE_BLEND			5.0f
#define RMG_SITE_CLEARANCE		7.0f	///< cells kept clear around anything placed
#define RMG_MONEY_WALK_STRETCH	1.1f	///< walk cells per straight-line cell a money ring is aimed at
#define RMG_MONEY_LEAD			8		///< cells of walk a player's own money is ahead of every rival
#define RMG_MONEY_NO_RIVAL		1000000	///< the lead of a cell no other start can walk to
#define RMG_CONTESTED_LEAD_MIN	2		///< a contested dock is this many cells nearer its placer
#define RMG_CONTESTED_LEAD_MAX	6		///< and no more, so it is still the neighbour's fight
#define RMG_ROUTE_LOCK_HALF_WIDTH	2	///< cells either side of a start-to-money walk no later pad levels
#define RMG_MONEY_WALK_SLACK	4		///< walk cells the finished map may add to a money walk before it is cut short

/* Anything that stands on the ground gets the ground levelled under it first. A supply dock on a
	terrace edge is a dock with one corner in the air, and the game will not let a player build
	beside it either. The pad is flat to its radius and eases back into the terrain over the blend. */
#define RMG_PAD_RADIUS			5.0f
#define RMG_PAD_WAY_CLEAR		9		///< four-neighbour cells a plain map's money pad keeps off a carved way: its levelled disc and the way's half width, and two to spare
#define RMG_PAD_BLEND			10.0f
#define RMG_PAD_SHORE_KEEP		2.0f	///< cells of beach a pad never touches
#define RMG_PAD_BLEND_SWING		0.7f	///< share of its width a pad's blend wanders out by

/* A town. Nothing here is a fixed number: each town rolls how many streets it has each way, how far
	apart they run, how deep the plots are, and which way the whole grid is turned. Two towns on one
	map are two different towns. */
#define RMG_TOWN_MIN_STREETS	3
#define RMG_TOWN_MAX_STREETS	6
#define RMG_TOWN_BLOCK_MIN		18.0f	///< cells between two street centre lines, at the closest
#define RMG_TOWN_BLOCK_MAX		34.0f	///< and at the widest
#define RMG_TOWN_PLOT_MIN		9.0f	///< cells of frontage a building takes along a street
#define RMG_TOWN_PLOT_MAX		14.0f
#define RMG_TOWN_SET_BACK_MIN	5.0f	///< cells from the street centre to a building front
#define RMG_TOWN_SET_BACK_MAX	8.0f
#define RMG_TOWN_GAP_IN_100		22		///< plots left empty, so a street has yards and corners
#define RMG_TOWN_EDGE_GAP_IN_100	45		///< and this many more at the town's radius, fewer inside it
#define RMG_TOWN_LOT_PAD		3.0f	///< cells levelled under a building
#define RMG_TOWN_LOT_BLEND		7.0f
#define RMG_TOWN_GRADE_MARGIN	4.0f	///< cells of graded ground kept past the outermost frontage
#define RMG_TOWN_GRADE_FADE		14.0f	///< cells the grading eases back into the hills over
#define RMG_TOWN_GRADE_SWING	0.6f	///< share of that the fade's outer edge wanders out by
#define RMG_TOWN_GRADE_BLUR		9		///< cells either side the ground is averaged over
#define RMG_TOWN_RELIEF_KEEP	0.6f	///< share of its eased roll a plain map's town plate keeps
#define RMG_TOWN_TERRACE		5.0f	///< height bytes from one of the plate's terraces to the next
#define RMG_TOWN_TERRACE_RISE	0.85f	///< share of a terrace's height band its step up takes, so a riser leans back instead of standing
#define RMG_TOWN_RIM_ROUND		0.6f	///< of the grid's shorter half, the radius its plate's corners round over
#define RMG_TOWN_RIM_WANDER		7.0f	///< cells the plate's edge wanders in or out by
#define RMG_TOWN_RIM_CONTOUR	0.5f	///< cells it draws in by for each height byte the ground stands off the plate
#define RMG_TOWN_PLATE_FADE		1.8f	///< and how much wider than RMG_TOWN_GRADE_FADE its rim eases out
#define RMG_TOWN_SQUEEZE		0.5f	///< of its radius, the clear ground a plain map's last-chance town asks for
#define RMG_TOWN_JITTER			2.0f	///< cells a building sits off its own frontage line
#define RMG_TOWN_DRY_MARGIN		4.0f	///< cells a building or a street keeps off the water
#define RMG_TOWN_REROLLS		6		///< plans a town tries, and as many small ones after for a map with no town yet
#define RMG_TOWN_SMALL_BLOCK_MAX	22.0f	///< the widest block of a small plan
#define RMG_TOWN_START_CLEAR	( RMG_BLEND_RADIUS + 2.0f )	///< cells round every start no building stands in
// A street is laid a block at a time, and a block that reaches water stops at the bank.
#define RMG_ROAD_STEP			2.0f	///< cells between two wet-or-dry samples along a block
#define RMG_ROAD_MIN_RUN		12.0f	///< cells; a street from a crossing to the bank shorter than this is a stub

// Bunkers go on the ramps, which is the ground worth holding.
#define RMG_BUNKER_CLEARANCE	10.0f

// Scenery. Counted against the ground rather than the players: a map twice the size wants four
// times the trees, or a wood is a hedge with a field around it.
#define RMG_CELLS_PER_TREE		60.0f	///< a ceiling the woods never reach, not the count
#define RMG_TREE_CELLS_EACH		110.0f	///< the most the woods field may plant: one tree to this many cells
#define RMG_CELLS_PER_ROCK		2600.0f
#define RMG_ROCK_EDGE_CELLS		10		///< no rock this close to the playable edge
#define RMG_PROP_STRIDE			2
#define RMG_ROCK_ACCEPT_IN_256	67U		///< rocky cells out of 256 that get a rock, a quarter
#define RMG_TREE_EDGE_RAGGED	1500U	///< depth units (density x 10000) a tree's place in the cap is shuffled by
#define RMG_FOREST_FEATURES		7.0f
#define RMG_LANE_CLEAR			7		///< cells either side of a route between bases that stay open
#define RMG_LANE_FRAME			18		///< and out to here the woods thicken, so they line the route
#define RMG_LANE_FAR			40		///< the lane distance field stops counting here
#define RMG_GROVE_RADIUS		7.0f	///< cells across a player's own stand of trees, from its middle
#define RMG_GROVE_DISTANCE		42.0f	///< cells from the start to that stand
#define RMG_GROVE_TREES			22

// A black forest: rides from each base to the middle and round the ring of bases, the rest trees.
#define RMG_RIDE_HALF_WIDTH		6.0f	///< cells either side of a ride's centre line kept clear
#define RMG_RIDE_WOBBLE			2.5f	///< cells the ride's edge wanders by
#define RMG_WOOD_CLUMP_SCALE	0.03f	///< of the clump field per cell: a thicket every thirty cells or so
#define RMG_WOOD_CLUMP_EDGE		-0.05f	///< of the mixed field; under it, a glade between thickets
#define RMG_WOOD_BUDGET_CELLS	12.0f	///< ground cells per tree the budget allows

// A massif: the middle of the map lifted into a table with a cliff round it.
#define RMG_MASSIF_HEIGHT		44.0f	///< height bytes, almost two terrace steps
#define RMG_MASSIF_RADIUS		0.16f	///< fraction of the playable size, the largest the foot's mean radius is rolled
#define RMG_MASSIF_RADIUS_MIN	0.12f	///< and the smallest
#define RMG_MASSIF_STRETCH		1.8f	///< the most a massif's long axis may be over its short one
#define RMG_MASSIF_STRETCH_MIN	1.25f	///< and the least, squared like it: a rolled 1 left a round table
#define RMG_MASSIF_SHAPE2		0.26f	///< the most the two-lobed swing moves the foot, a share of the radius
#define RMG_MASSIF_SHAPE3		0.16f	///< and the three-lobed one
#define RMG_MASSIF_OFFSET		0.07f	///< of the playable size, how far off the middle a massif may stand
#define RMG_MASSIF_SPLIT_IN_100	35		///< massifs out of a hundred that stand as two with a pass between
#define RMG_MASSIF_RIM			4.0f	///< cells the cliff takes from foot to top
#define RMG_MASSIF_SLOPE_RIM	16.0f	///< cells a walkable stretch of the rim takes instead
#define RMG_MASSIF_CROWN		14.0f	///< height bytes the crown on top rises over the table's edge
#define RMG_MASSIF_CROWN_SPAN	0.8f	///< of the lobe's axes, how far out the rise to the crown begins
#define RMG_MASSIF_WOBBLE		0.10f	///< share of the radius the outline wanders out by
#define RMG_MASSIF_WOBBLE_IN	0.24f	///< and in by: inward costs no start any room
#define RMG_MASSIF_ROAD_TOP		4.0f	///< cells either side of a ramp's centre where it meets the top
#define RMG_MASSIF_ROAD_FOOT	8.0f	///< and where it meets the ground below, so the foot opens out
#define RMG_MASSIF_ROAD_SWING	4		///< steps of eight degrees either side of a base's bearing a ramp may move to a slope
#define RMG_GORGE_WALL			4.0f	///< cells a canyon wall takes at its steepest, which is a cliff
#define RMG_GORGE_WALL_SWING	4.0f	///< and the cells it may take besides, where it eases to a slope
#define RMG_GORGE_ENDS			0.35f	///< of the floor's width, what a gorge narrows to where it runs out
#define RMG_MASSIF_ROAD_BANK	4.0f	///< cells over which a ramp's sides ease back into the ground
#define RMG_MASSIF_ROAD_GRADE	3.0f	///< height bytes a ramp climbs per cell at most
#define RMG_MASSIF_ROAD_BEND	0.3f	///< of its length, how far a ramp's foot swings toward its base
#define RMG_MASSIF_ROAD_APART	0.8f	///< cosine of the least angle between two ramps up one lobe, 37 degrees
#define RMG_MASSIF_ROAD_SIDE	2.0f	///< height bytes per cell a ramp's cut or fill banks out at
#define RMG_MASSIF_ROAD_WIDEST	8.0f	///< cells a bank may take at most, or a small map's rim is all ramp
#define RMG_MASSIF_ROCK_CLEAR	20.0f	///< cells round a ramp's middle that no rock stands in

// What counts as a cliff. WorldHeightMap marks a cell impassable when its four
// corners span more than this many world units (PATHFIND_CLIFF_SLOPE_LIMIT_F).
#define RMG_CLIFF_WORLD_SPAN	9.8f
#define RMG_CLIFF_BYTES			16		///< the fewest height bytes of corner span the engine calls a cliff: 16 x 0.625 > 9.8
#define RMG_STEEP_BYTES			10		///< and the fewest a walkable cell may span before it reads as one: 64% of the limit
#define RMG_SETTLE_PASSES		6		///< sweeps settleSlopes makes over the map
#define RMG_SETTLE_MOST			8		///< height bytes a corner may move in all of them
#define RMG_SETTLE_TEMPLATE_PASSES	12	///< the same on a template map, whose copied walls run wider than a plain map's pads leave theirs
#define RMG_SETTLE_TEMPLATE_MOST	16
#define RMG_SETTLE_SHORE		1.5f	///< cells of shore it leaves alone
#define RMG_SETTLE_OBJECT		3		///< cells round a building or the money it leaves alone
#define RMG_SETTLE_BASE			( RMG_FLAT_RADIUS + 3.0f )	///< and round a start: its level and a rim, not the blend out to the hills
#define RMG_SETTLE_SPIKE		6		///< height bytes a lone corner may stand off its four neighbours
#define RMG_SETTLE_KEEP_SHORE	2		///< settleSlopes' mark for shore, which even a spike keeps
// And the most a carved pass may climb from one cell to the next, in height
// bytes, which has to leave room under that span for the noise either side.
#define RMG_PASS_MAX_STEP		8.0f
#define RMG_PASS_HALF_WIDTH		2		///< cells either side of the route that get cut
#define RMG_PASS_ROAD_SLOPE		3.0f	///< height bytes per cell the roadway may lean across
#define RMG_PASS_BANK_SLOPE		6.0f	///< and its banks, outside that
#define RMG_PASS_BANK_CELLS		6		///< how far out a bank may reach
#define RMG_PASS_ATTEMPTS		12
// A plain map's ways are laid like roads: eight ways out of a cell, a climb past the gentle grade
// priced by its square so the search takes the long way round a hill, and a cell on a crest or a
// ridge priced again so the way keeps to the valley and the saddle.
#define RMG_ROAD_MAX_STEP		5.0f	///< height bytes a way climbs per cell at most: with the roadway's lean across it, a cell spans under RMG_STEEP_BYTES
#define RMG_ROAD_GENTLE			2.0f	///< height bytes per cell a way climbs at no more than its height's price
#define RMG_ROAD_LENGTH			10		///< what a straight step costs; a diagonal one costs RMG_ROAD_DIAGONAL
#define RMG_ROAD_DIAGONAL		14
#define RMG_ROAD_CLIMB			3		///< price of a height byte climbed or dropped
#define RMG_ROAD_STEEP			6		///< price of the square of each byte past the gentle grade
#define RMG_ROAD_CREST_REACH	6		///< cells either side a crest or a ridge is read over
#define RMG_ROAD_CREST_FREE		2		///< height bytes a cell may stand over both sides before it is a crest
#define RMG_ROAD_CREST			24		///< price of each byte past that
#define RMG_ROAD_MEANDER		20		///< most a cell's slow noise adds to a step
#define RMG_ROAD_MEANDER_SCALE	0.025f	///< of the noise per cell: a bend every forty cells or so
#define RMG_ROAD_SMOOTH			4		///< cells either side the way's points are averaged over
#define RMG_ROAD_SMOOTH_PASSES	3
#define RMG_ROAD_SWAY			9.0f	///< cells a way sways to either side of where the search laid it, at most
#define RMG_ROAD_SWAY_SCALE		0.015f	///< of the sway's noise per cell travelled: a swing every sixty cells or so
#define RMG_ROAD_SWAY_RISE		4		///< bytes a sway may climb off the line the search laid
#define RMG_ROAD_SWAY_CREST		3		///< bytes over both sides a sway may stand where the search's line stood lower
#define RMG_ROAD_SWAY_ROUNDS	4		///< times the sway is averaged again after dropping the stretches that climbed
#define RMG_ROAD_BOW_SHARE		0.07f	///< of a way's length it bows out to one side at its middle
#define RMG_ROAD_BOW_MOST		10.0f	///< cells a way bows at most
#define RMG_ROAD_BOW_BLOCKED	512.0f	///< height a cliff, water or off-map cell counts as when picking the side to bow to
#define RMG_ROAD_SWAY_EASE		25		///< cells at each end over which the sway eases in
#define RMG_ROAD_CUT_MOST		20.0f	///< height bytes a cutting takes off a crest the way could not go round
#define RMG_ROAD_CLIFF			600
#define RMG_ROAD_WATER			2500
#define RMG_WAY_APART			3		///< cells off a drawn way another way counts as running beside it
#define RMG_WAY_DRY_MARGIN		2.0f	///< cells a drawn way stops short of the water

//-----------------------------------------------------------------------------
// Chunk writer
//-----------------------------------------------------------------------------

/** Writes the CkMp stream DataChunkOutput writes, but into a buffer instead of
	through a temp file in the user data directory. Chunk names and dictionary
	keys share one table of contents, exactly as the reader expects. */
class MapChunkWriter
{
public:
	void openChunk( const char *name, DataChunkVersionType version );
	void closeChunk( void );

	void writeInt( Int v );
	void writeReal( Real v );
	void writeByte( Byte v );
	void writeBytes( const void *data, Int len );
	void writeAsciiString( const char *s );

	void beginDict( Int pairCount );
	void dictBool( const char *key, Bool v );
	void dictInt( const char *key, Int v );
	void dictAsciiString( const char *key, const char *v );

	/// Table of contents followed by the chunk stream.
	void finish( std::vector<char>& out );

private:
	UnsignedInt idFor( const char *name );
	void writeKeyAndType( const char *key, Dict::DataType type );

	std::vector<char> m_body;
	std::vector<AsciiString> m_names;	///< index i holds the name of id i+1
	std::vector<Int> m_openChunks;		///< offsets of the size fields still to patch
};

UnsignedInt MapChunkWriter::idFor( const char *name )
{
	for( UnsignedInt i = 0; i < m_names.size(); i++ )
	{
		if( m_names[i].compare( name ) == 0 )
			return i + 1;
	}

	m_names.push_back( AsciiString( name ) );
	return m_names.size();
}

void MapChunkWriter::writeBytes( const void *data, Int len )
{
	const char *p = (const char *)data;
	m_body.insert( m_body.end(), p, p + len );
}

void MapChunkWriter::writeInt( Int v )		{ writeBytes( &v, sizeof(Int) ); }
void MapChunkWriter::writeReal( Real v )	{ writeBytes( &v, sizeof(Real) ); }
void MapChunkWriter::writeByte( Byte v )	{ writeBytes( &v, sizeof(Byte) ); }

void MapChunkWriter::writeAsciiString( const char *s )
{
	UnsignedShort len = (UnsignedShort)strlen( s );
	writeBytes( &len, sizeof(UnsignedShort) );
	writeBytes( s, len );
}

void MapChunkWriter::openChunk( const char *name, DataChunkVersionType version )
{
	UnsignedInt id = idFor( name );
	writeBytes( &id, sizeof(UnsignedInt) );
	writeBytes( &version, sizeof(DataChunkVersionType) );

	m_openChunks.push_back( m_body.size() );
	Int placeholder = 0;
	writeBytes( &placeholder, sizeof(Int) );
}

void MapChunkWriter::closeChunk( void )
{
	Int sizeFieldPos = m_openChunks.back();
	m_openChunks.pop_back();

	Int size = m_body.size() - sizeFieldPos - sizeof(Int);
	memcpy( &m_body[sizeFieldPos], &size, sizeof(Int) );
}

void MapChunkWriter::beginDict( Int pairCount )
{
	UnsignedShort len = (UnsignedShort)pairCount;
	writeBytes( &len, sizeof(UnsignedShort) );
}

void MapChunkWriter::writeKeyAndType( const char *key, Dict::DataType type )
{
	Int keyAndType = idFor( key );
	keyAndType <<= 8;
	keyAndType |= (type & 0xff);
	writeInt( keyAndType );
}

void MapChunkWriter::dictBool( const char *key, Bool v )
{
	writeKeyAndType( key, Dict::DICT_BOOL );
	writeByte( v ? 1 : 0 );
}

void MapChunkWriter::dictInt( const char *key, Int v )
{
	writeKeyAndType( key, Dict::DICT_INT );
	writeInt( v );
}

void MapChunkWriter::dictAsciiString( const char *key, const char *v )
{
	writeKeyAndType( key, Dict::DICT_ASCIISTRING );
	writeAsciiString( v );
}

void MapChunkWriter::finish( std::vector<char>& out )
{
	out.clear();

	const char tag[4] = { 'C', 'k', 'M', 'p' };
	out.insert( out.end(), tag, tag + 4 );

	Int listLength = m_names.size();
	const char *p = (const char *)&listLength;
	out.insert( out.end(), p, p + sizeof(Int) );

	for( Int i = 0; i < listLength; i++ )
	{
		unsigned char len = (unsigned char)m_names[i].getLength();
		out.push_back( (char)len );
		out.insert( out.end(), m_names[i].str(), m_names[i].str() + len );

		UnsignedInt id = i + 1;
		p = (const char *)&id;
		out.insert( out.end(), p, p + sizeof(UnsignedInt) );
	}

	out.insert( out.end(), m_body.begin(), m_body.end() );
}

//-----------------------------------------------------------------------------
// Noise
//-----------------------------------------------------------------------------

/** Permutation seeded by an integer generator only - no rand(), no clock - so
	the field is identical on every machine that asks for the same seed. */
static void seedPermutation( Int seed, UnsignedByte perm[512] )
{
	UnsignedInt state = (UnsignedInt)seed * 1664525U + 1013904223U;
	if( state == 0 )
		state = 0x9E3779B9U;

	Int i;
	for( i = 0; i < 256; i++ )
		perm[i] = (UnsignedByte)i;

	for( i = 255; i > 0; i-- )
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		Int j = (Int)(state % (UnsignedInt)(i + 1));

		UnsignedByte tmp = perm[i];
		perm[i] = perm[j];
		perm[j] = tmp;
	}

	for( i = 0; i < 256; i++ )
		perm[256 + i] = perm[i];
}

static Real fadeCurve( Real t )
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static Real lerpReal( Real a, Real b, Real t )
{
	return a + t * (b - a);
}

static Real gradient( Int hash, Real x, Real y )
{
	switch( hash & 7 )
	{
		case 0:		return  x;
		case 1:		return  x + y;
		case 2:		return  y;
		case 3:		return -x + y;
		case 4:		return -x;
		case 5:		return -x - y;
		case 6:		return -y;
		default:	return  x - y;
	}
}

/// Classic 2D Perlin noise, in [-1,1].
static Real perlin2( const UnsignedByte perm[512], Real x, Real y )
{
	Int xi = (Int)floorf( x );
	Int yi = (Int)floorf( y );
	Real xf = x - (Real)xi;
	Real yf = y - (Real)yi;

	Int gx = xi & 255;
	Int gy = yi & 255;

	Real u = fadeCurve( xf );
	Real v = fadeCurve( yf );

	Int aa = perm[perm[gx] + gy];
	Int ab = perm[perm[gx] + gy + 1];
	Int ba = perm[perm[gx + 1] + gy];
	Int bb = perm[perm[gx + 1] + gy + 1];

	Real x1 = lerpReal( gradient( aa, xf, yf ), gradient( ba, xf - 1.0f, yf ), u );
	Real x2 = lerpReal( gradient( ab, xf, yf - 1.0f ), gradient( bb, xf - 1.0f, yf - 1.0f ), u );

	return lerpReal( x1, x2, v );
}

/// Sum of octaves, normalized back into [-1,1].
static Real fractalNoise( const UnsignedByte perm[512], Real x, Real y, Int octaves )
{
	Real total = 0.0f;
	Real amplitude = 1.0f;
	Real frequency = 1.0f;
	Real range = 0.0f;

	for( Int octave = 0; octave < octaves; octave++ )
	{
		total += amplitude * perlin2( perm, x * frequency, y * frequency );
		range += amplitude;
		amplitude *= 0.5f;
		frequency *= 2.0f;
	}

	return total / range;
}

/** Noise on a bearing, 0 to 1. It is read on a small circle round (originX, originY) in the noise
	field, so it is smooth in the bearing and closes on itself all the way round, and two origins
	give two unrelated outlines. A disc stamped with compasses reads as a disc from across the map;
	the base pads, the money pads and the town grading wander their edge by this instead. */
static Real ringNoise( const UnsignedByte perm[512], Real unitX, Real unitY, Real originX, Real originY )
{
	Real t = 0.5f + 1.4f * fractalNoise( perm, originX + unitX * 1.6f, originY + unitY * 1.6f, 2 );
	if( t < 0.0f ) t = 0.0f;
	if( t > 1.0f ) t = 1.0f;
	return t;
}

/** Repeatable per-cell randomness, for scattering that must not depend on order. The seed is
	unsigned so a caller salts it as (UnsignedInt)seed + salt: a lobby seed runs to 0x7fffffff, and
	the same sum in Int is signed overflow, which the compiler may assume never happens. */
static UnsignedInt hashCell( UnsignedInt seed, Int x, Int y )
{
	UnsignedInt h = seed * 374761393U;
	h += (UnsignedInt)x * 668265263U;
	h ^= h >> 13;
	h += (UnsignedInt)y * 2246822519U;
	h ^= h >> 16;
	h *= 2654435761U;
	return h ^ (h >> 15);
}

//-----------------------------------------------------------------------------
// Layout
//-----------------------------------------------------------------------------

/** Ordered by which one bleeds into which. A cell only ever takes a blend from
	a neighbour above it in this list, so two neighbouring cells never both try
	to blend into each other and leave a seam down the middle. */
enum RMGTerrainClass
{
	RMG_TERRAIN_GRASS = 0,
	RMG_TERRAIN_SAND,
	RMG_TERRAIN_DIRT,
	RMG_TERRAIN_ROCK,

	RMG_TERRAIN_COUNT
};

/* What kind of map a seed is. Each one bends the same height field, water and woods a different
	way, so the type reads off the overview before anybody looks at a start: plains are low and
	wooded with one pond and a stream, highlands stand on shelves, lake country spreads two more
	basins than usual and keeps them apart, a river map is cut in two with fords across it, and a
	canyon map has a few long gorges winding through it. A black forest is low ground under one
	wood from edge to edge, with clear rides cut through it from every base to the middle and round
	the ring of bases; trees do not stop a tank in this game, so the rides are the lanes an army
	can be seen in, not walls. A massif map lifts one or two tables of high ground near the middle,
	of a size, length and lie the seed rolls, with a terrace on top and a rim that is cliff in places
	and slope in others, and a ramp up from each base's side. The starts, the money and every
	playability repair run the same on all of them. */
enum RMGMapType
{
	RMG_MAP_PLAINS = 0,
	RMG_MAP_HIGHLANDS,
	RMG_MAP_LAKES,
	RMG_MAP_RIVER,
	RMG_MAP_CANYON,
	RMG_MAP_FOREST,
	RMG_MAP_MASSIF,

	RMG_MAP_TYPE_COUNT
};

struct RMGMapTypeShape
{
	Real m_amplitude;		///< times RMG_AMPLITUDE
	Real m_valleyDrop;		///< height bytes the valley field cuts at its deepest
	Real m_gorgeWidth;		///< above zero: valleys are gorges along the field's zero line, this many cells either side of their floor's middle
	Real m_shelfRidge;		///< ridge noise above this flattens into a shelf
	Int m_extraLakes;		///< basins on top of the usual one per two players, below zero fewer
	Real m_lakeArea;		///< times RMG_LAKE_AREA
	Bool m_river;			///< one river across the whole map, broken at fords, instead of a stream
	Real m_forestThreshold;	///< of the forest field; under it, open ground
	Bool m_woodland;		///< trees everywhere but the rides (RMG_RIDE_*)
	Real m_massif;			///< height bytes the middle of the map is lifted by, 0 for none
};

static const RMGMapTypeShape theMapTypes[RMG_MAP_TYPE_COUNT] =
{
	//	amp		valley	gorge	shelf	lakes	area	river	forest	wood	massif
	{	0.70f,	14.0f,	0.0f,	0.36f,	-9,		0.7f,	FALSE,	0.04f,	FALSE,	0.0f	},	// plains
	{	1.30f,	30.0f,	0.0f,	0.22f,	-1,		0.8f,	FALSE,	0.14f,	FALSE,	0.0f	},	// highlands
	{	0.85f,	30.0f,	0.0f,	0.42f,	2,		1.5f,	FALSE,	0.12f,	FALSE,	0.0f	},	// lake country
	{	0.85f,	24.0f,	0.0f,	0.46f,	-9,		1.0f,	TRUE,	0.10f,	FALSE,	0.0f	},	// river and fords
	{	1.00f,	44.0f,	7.0f,	0.50f,	-1,		0.8f,	FALSE,	0.18f,	FALSE,	0.0f	},	// canyon
	{	0.60f,	12.0f,	0.0f,	0.50f,	-9,		0.6f,	FALSE,	0.0f,	TRUE,	0.0f	},	// black forest
	{	0.70f,	16.0f,	0.0f,	0.46f,	-9,		0.7f,	FALSE,	0.12f,	FALSE,	RMG_MASSIF_HEIGHT	},	// massif
};

/* What the map is made of: the four ground textures in RMGTerrainClass order, the trees and rocks
	that stand on it, the street surface and the light. Every name is one Terrain.ini, Roads.ini or
	the object INI of the shipped game defines, and every texture is one the shipped terrain archives
	hold an image for. */
#define RMG_BIOME_TREES		4
#define RMG_BIOME_ROCKS		4

enum RMGBiomeKind
{
	RMG_BIOME_TEMPERATE = 0,
	RMG_BIOME_DESERT,
	RMG_BIOME_WINTER,
	RMG_BIOME_STEPPE,

	RMG_BIOME_COUNT
};

struct RMGBiome
{
	const char *m_textures[RMG_TERRAIN_COUNT];
	const char *m_trees[RMG_BIOME_TREES];
	const char *m_rocks[RMG_BIOME_ROCKS];
	const char *m_road;
	Real m_treeShare;			///< times the woods' planting chance
	Real m_terrainAmbient[3];
	Real m_terrainDiffuse[3];
	Real m_objectAmbient[3];
	Real m_objectDiffuse[3];
	Real m_sunDirection[3];		///< x and y point the sun across the map, z takes it down
	UnsignedByte m_previewColours[RMG_TERRAIN_COUNT][3];
};

static const RMGBiome theBiomes[RMG_BIOME_COUNT] =
{
	{	// temperate: green fields, brown high ground
		{ "GrassType1", "SandLargeType1", "DirtType1", "RocksType1" },
		{ "TreeOak01", "TreeMaple1", "TreeDogwood1", "TreeFir01B" },
		{ "Rocks1", "Rocks2", "RockClusterMedium01", "RockClusterSmall01" },
		"TwoLane", 1.0f,
		{ 0.35f, 0.35f, 0.36f }, { 0.78f, 0.75f, 0.68f },
		{ 0.45f, 0.45f, 0.47f }, { 0.82f, 0.79f, 0.72f },
		{ -0.58f, 0.42f, -0.70f },
		{ { 86, 122, 62 }, { 190, 176, 130 }, { 132, 112, 74 }, { 118, 118, 112 } }
	},
	{	// desert: open sand, wet sand at the water, pale dirt on top, palms in thin stands
		{ "SandLargeType2", "SandType3wet", "DirtMediumType3", "RocksType3" },
		{ "TreePalm1", "TreePalm2", "TreePalm1short", "TreePalm2short" },
		{ "Rocks1Brown", "Rocks2Brown", "Rocks3Brown", "RockClusterSmall01" },
		"TwoLaneOld", 0.35f,
		{ 0.40f, 0.37f, 0.32f }, { 0.92f, 0.84f, 0.66f },
		{ 0.50f, 0.47f, 0.42f }, { 0.95f, 0.88f, 0.72f },
		{ -0.45f, 0.30f, -0.84f },
		{ { 214, 190, 140 }, { 168, 138, 88 }, { 196, 172, 132 }, { 140, 132, 118 } }
	},
	{	// winter: snow, slush at the water, deep snow on the heights, snowed-in firs
		{ "SnowType1", "DirtSnowType1", "SnowLargeType1", "RocksMediumType1Snow" },
		{ "TreeFir06Snow", "TreeFir07Snow", "TreePine4snow", "TreeSpruceSnow" },
		{ "RockClusterSmallSnow01", "RockClusterMediumSnow01", "RockClusterLargeSnow01",
			"RockClusterSmallSnow01" },
		"TwoLaneOld2Snow", 1.0f,
		{ 0.40f, 0.42f, 0.48f }, { 0.74f, 0.78f, 0.86f },
		{ 0.48f, 0.50f, 0.55f }, { 0.78f, 0.80f, 0.88f },
		{ -0.62f, 0.48f, -0.62f },
		{ { 222, 226, 230 }, { 140, 132, 122 }, { 244, 246, 250 }, { 120, 124, 130 } }
	},
	{	// steppe: dry golden ground, grassy sand at the water, red gravel hills, turning oaks
		{ "DirtMediumType8", "SandMediumType7Grassy", "DirtType2", "RocksType1" },
		{ "TreeOakFall1", "TreeOakFall3", "TreeOakFall5", "TreeBirch02" },
		{ "Rocks1Brown", "Rocks3Brown", "RockClusterMedium01", "Rocks2" },
		"TwoLaneOld3", 0.75f,
		{ 0.37f, 0.33f, 0.29f }, { 0.88f, 0.74f, 0.54f },
		{ 0.46f, 0.42f, 0.38f }, { 0.90f, 0.78f, 0.60f },
		{ -0.70f, 0.40f, -0.59f },
		{ { 186, 150, 92 }, { 196, 176, 120 }, { 150, 96, 60 }, { 118, 118, 112 } }
	},
};

/* A style is a shipped map's character with a new layout every seed: Tools/RandomMapLab/mapstyle.py
	measures the map and prints the entry (--cpp). Every style so far is a bowl: the starts stand on
	a ring of plateaus above a lower middle, each with one ramp down into it, a ridge on the ring
	between two neighbours and its dock on its own plateau, no water. It is compiled in rather than
	read from a file because every machine in a match, and every replay, rebuilds the map from its
	name, and the style is in the name. Distances are the shipped map's cells: a base is a base
	whatever the player count, and the map grows round it, only a map smaller than
	rmgStyleSmallestCells shrinking the base with it (m_styleScale). */
struct RMGStyle
{
	const char *m_name;			///< the token in the map's name and on the command line
	Int m_cells;				///< the shipped map's playable size, which the distances below are measured at
	Real m_startRing;			///< a start's distance from the middle, a share of half the map
	Real m_raise;				///< height bytes a start's plateau stands above the bowl
	Real m_ridge;				///< and the ridge between two starts above the plateau
	Real m_flat;				///< cells of flat pad round a start
	Real m_plateau;				///< cells of plateau round a start, to the rim and along the ring
	Real m_rampWidth;			///< cells across the way down into the bowl where it leaves the plateau
	Real m_homeSupply;			///< cells in a straight line from a start to its dock
	Int m_piles;				///< small supply piles a player, out towards both neighbours
	TimeOfDay m_timeOfDay;
	RMGBiome m_biome;
};

static const RMGStyle theStyles[] =
{
	{	// twilight, from maps\Twilight Flame\Twilight Flame.map
		"twilight", 443, 0.809f, 110.000f, 56.000f, 30.500f, 53.750f, 57.000f, 33.950f, 4, TIME_OF_DAY_AFTERNOON,
		{
			{ "DirtMediumType10", "SandMediumType6", "SandMediumType10", "CliffLargeType10" },
			{ "TreeDogwood8", "TreeDogwood3", "TreeDogwood4", "TreeDogwood1" },
			{ "Rocks2Brown", "Rocks1Brown", "Rocks3Brown", "RockClusterLarge01" },
			"TwoLaneOld", 0.111f,
			{ 0.216f, 0.200f, 0.169f }, { 1.000f, 1.000f, 0.914f },
			{ 0.216f, 0.200f, 0.169f }, { 1.000f, 1.000f, 0.914f },
			{ -0.894f, -0.190f, -0.407f },
			{ { 150, 126, 90 }, { 178, 154, 110 }, { 196, 172, 130 }, { 118, 108, 96 } }
		}
	},
};
static const Int RMG_STYLE_COUNT = sizeof(theStyles) / sizeof(theStyles[0]);

/** The Direct Strike map (RMGLayout::buildLane) is a style past the table: "-randommap <seed> lane", named
	"RMG_v<version>_<seed>_6p_332c_lane". Its ground has no size or player count of its own, so
	clampSettings pins both; the seed picks only its look. */
static const Int RMG_STYLE_LANE = RMG_STYLE_COUNT + 1;

/** The Apocalypse map (RMGLayout::buildApocalypse), a second style past the table: "-randommap <seed>
	<players> [size] apocalypse". Any player count; the seed picks the look and the gates' bearing. */
static const Int RMG_STYLE_APOCALYPSE = RMG_STYLE_COUNT + 2;

/** The Art of Defense map (RMGLayout::buildAod), a third: "-randommap <seed> [players] [size] aod". Two to
	six seats; the seed picks the look, one or two lanes from the west edge to the east and how they wind. */
static const Int RMG_STYLE_AOD = RMG_STYLE_COUNT + 3;

static const char *rmgStyleName( Int style )
{
	if( style == RMG_STYLE_LANE )
		return "lane";
	if( style == RMG_STYLE_APOCALYPSE )
		return "apocalypse";
	if( style == RMG_STYLE_AOD )
		return "aod";
	return style > 0 && style <= RMG_STYLE_COUNT ? theStyles[style - 1].m_name : "";
}

/* How many tiles each ground image above holds and how many across, as every shipped map that uses
	it declares it in its BlendTileData (MapsZH.big and the base game's maps.big agree on every one).
	WorldHeightMap's readTexClass loads a width by width block of the image only when the class asks
	for no more tiles than the image has: declared smaller, a class draws one corner of its art over
	and over (every class here was four tiles two across, so the twilight style's cliff was a quarter
	of CliffLargeType10 repeating every four cells), and declared bigger it draws nothing. */
struct RMGTextureSheet
{
	const char *m_name;
	Int m_tiles, m_width;
};

static const RMGTextureSheet theTextureSheets[] =
{
	{ "GrassType1", 4, 2 },				{ "SandLargeType1", 36, 6 },		{ "DirtType1", 4, 2 },
	{ "RocksType1", 4, 2 },				{ "SandLargeType2", 36, 6 },		{ "SandType3wet", 4, 2 },
	{ "DirtMediumType3", 16, 4 },		{ "RocksType3", 4, 2 },				{ "SnowType1", 4, 2 },
	{ "DirtSnowType1", 16, 4 },			{ "SnowLargeType1", 36, 6 },		{ "RocksMediumType1Snow", 16, 4 },
	{ "DirtMediumType8", 16, 4 },		{ "SandMediumType7Grassy", 16, 4 },	{ "DirtType2", 4, 2 },
	{ "DirtMediumType10", 16, 4 },		{ "SandMediumType6", 16, 4 },		{ "SandMediumType10", 16, 4 },
	{ "CliffLargeType10", 16, 4 },
	// the lane map's own (theLaneGrounds)
	{ "SandLargeType3Light", 36, 6 },	{ "DirtType3", 4, 2 },				{ "DirtMediumType15Snow", 16, 4 },
	{ "GrassLargeType1Snow", 36, 6 },	{ "GrassMediumType21", 16, 4 },		{ "SandMediumType13", 16, 4 },
	{ "SandMediumType9Snow", 16, 4 },	{ "SnowType6", 16, 4 },
};

static void rmgTextureSheet( const char *name, Int& tiles, Int& width )
{
	tiles = RMG_TILES_PER_CLASS;
	width = RMG_TILE_SHEET_WIDTH;
	for( Int i = 0; i < (Int)( sizeof(theTextureSheets) / sizeof(theTextureSheets[0]) ); i++ )
	{
		if( strcmp( theTextureSheets[i].m_name, name ) == 0 )
		{
			tiles = theTextureSheets[i].m_tiles;
			width = theTextureSheets[i].m_width;
		}
	}
}

/// A class's first tile: the classes lie one after another in RMGTerrainClass order.
static Int rmgClassFirstTile( const RMGBiome& biome, Int terrainClass )
{
	Int first = 0;
	for( Int c = 0; c < terrainClass; c++ )
	{
		Int tiles, width;
		rmgTextureSheet( biome.m_textures[c], tiles, width );
		first += tiles;
	}
	return first;
}

static Int rmgClassWidth( const RMGBiome& biome, Int terrainClass )
{
	Int tiles, width;
	rmgTextureSheet( biome.m_textures[terrainClass], tiles, width );
	return width;
}

/// The bowl a style stands on: rolling low ground, no shelves, no water, thin woods.
static const RMGMapTypeShape theBowlType =
	//	amp		valley	gorge	shelf	lakes	area	river	forest	wood	massif
	{	1.10f,	0.0f,	0.0f,	1.0f,	-9,		0.0f,	FALSE,	0.14f,	FALSE,	0.0f	};

#define RMG_BOWL_CLIFF_CELLS	7.0f	///< cells the rim takes from the bowl's floor to the plateau
#define RMG_BOWL_FACE_CELLS		4.0f	///< and a ridge's face, which is lower, so steeper to stay a cliff
#define RMG_BOWL_RAMP_GRADE		2.75f	///< height bytes a ramp down into the bowl falls per cell, on average
#define RMG_BOWL_RAMP_MIN		24.0f	///< cells, the shortest that ramp may be on a small map
#define RMG_BOWL_RIDGE_RISE		22.0f	///< cells the way up a ridge takes to climb
#define RMG_BOWL_CORE_CELLS		50.0f	///< cells of bowl floor between the ramps' feet and the middle

static Real rmgUnit( Real t )
{
	if( t < 0.0f ) return 0.0f;
	if( t > 1.0f ) return 1.0f;
	return t;
}

/** The smallest map that holds a style's bases at full size: behind a start, its plateau in front,
	the ramp down and some bowl floor, either side of the middle. */
static Int rmgStyleSmallestCells( const RMGStyle& style )
{
	return (Int)( 2.0f * ( ( 1.0f - style.m_startRing ) * (Real)style.m_cells * 0.5f + 0.85f * style.m_plateau
		+ style.m_raise / RMG_BOWL_RAMP_GRADE + RMG_BOWL_CORE_CELLS ) + 0.5f );
}

static Int rmgBiomeFor( Int seed )
{
	return (Int)( hashCell( seed, 9001, 1 ) % (UnsignedInt)RMG_BIOME_COUNT );
}

/** Snow falls on winter maps and nowhere else. The ground, the firs and the rocks of a winter map
	are snow already; on green grass or golden steppe it would put white roofs over a summer field. */
static Bool rmgSnowsOn( Int seed )
{
	return rmgBiomeFor( seed ) == RMG_BIOME_WINTER;
}

/** The hour, on its own salt so it rolls apart from the kind of map and the ground. Over the first
	thousand seeds each of the four comes up between 226 and 269 times. */
static TimeOfDay rmgTimeOfDayFor( Int seed )
{
	return (TimeOfDay)( TIME_OF_DAY_FIRST + (Int)( hashCell( seed, 9001, 14 ) % 4U ) );
}

struct RMGPoint
{
	Real m_cellX;
	Real m_cellY;
};

struct RMGObject
{
	AsciiString m_templateName;
	AsciiString m_uniqueID;
	Real m_worldX;
	Real m_worldY;
	Real m_angle;
	Int m_waypointID;		///< 0 for anything that is not a waypoint
	Int m_flags;			///< MapObject's own flags; the road ones are the only ones used here
	AsciiString m_pathLabel;	///< a waypoint on a skirmish approach path: "Center3", "Flank1" ...
	AsciiString m_objectName;	///< the name a script or the Direct Strike director finds it by; empty for most
	AsciiString m_owner;		///< originalOwner, "StartOwner:1,3,5" for a seat's own (GameLogic.cpp); empty: neutral, "team"
};

/** A named trigger area that is not water, in playable cells: the Direct Strike map's build plateaus and
	middle. */
struct RMGArea
{
	AsciiString m_name;
	std::vector<RMGPoint> m_polygon;
};

/** A road is two objects in a row carrying MapObject's road flags, which W3DRoadBuffer pairs up as
	it walks the list. The values are FLAG_ROAD_POINT1 and FLAG_ROAD_POINT2 from MapObject.h, which
	lives in GameEngine and could be included - but the flags are the file format's, and the object
	list here is written, never read back through MapObject. */
#define RMG_FLAG_ROAD_POINT1	0x00000002
#define RMG_FLAG_ROAD_POINT2	0x00000004
#define RMG_FLAG_ROAD_CORNER_TIGHT	0x00000040	///< on POINT1: a corner turns on half a road width, a town corner

/// A spot flat and dry enough for a base, found before any seat is chosen.
struct RMGStartCandidate
{
	Real m_cellX;
	Real m_cellY;
	Real m_roughness;
};

/** The shape one town was rolled to be: how many streets it has each way, where each one runs in
	the town's own coordinates, how deep its plots are and which way the whole grid is turned. It is
	rolled before a site is looked for, because the search needs the radius to keep the streets
	inside the map. */
struct RMGTownPlan
{
	Real m_rotation;		///< radians the grid is turned by, so no two towns face the same way
	Int m_streetsAcross;
	Int m_streetsDown;
	Real m_acrossAt[RMG_TOWN_MAX_STREETS];	///< where each across-street runs, town coordinates
	Real m_downAt[RMG_TOWN_MAX_STREETS];
	Real m_plotLength;
	Real m_setBack;
	Real m_radius;			///< from the middle to the far corner of the outermost frontage
	Real m_thinRadius;		///< what the thinning towards the edge is measured against
};

/// A hash read as a fraction of one, which is how every choice a town makes is made.
static Real hashUnit( UnsignedInt seed, Int a, Int b )
{
	return (Real)(hashCell( seed, a, b ) % 4096U) / 4096.0f;
}

/// Eight compass points. Buildings, docks and the 8-player ring all sit on these.
static Real snapAngle45( Real angle )
{
	const Real step = PI * 0.25f;
	const Real twoPi = PI * 2.0f;
	while( angle < 0.0f )
		angle += twoPi;
	while( angle >= twoPi )
		angle -= twoPi;

	Int slot = (Int)(angle / step + 0.5f);
	if( slot >= 8 )
		slot = 0;
	return (Real)slot * step;
}

/// Where one direction's streets run, spaced unevenly and centred on the town. Returns the span.
static Real rollStreetLines( UnsignedInt seed, Int town, Int streets, Real blockMax, Real *out )
{
	Real at = 0.0f;
	Int line;

	for( line = 0; line < streets; line++ )
	{
		out[line] = at;
		at += lerpReal( RMG_TOWN_BLOCK_MIN, blockMax, hashUnit( seed, town, line ) );
	}

	Real span = out[streets - 1];
	for( line = 0; line < streets; line++ )
		out[line] -= span * 0.5f;

	return span;
}

/** smallest: the fewest streets each way and the closest blocks, the plan a map tries once the
	town it rolled has found no room anywhere. */
static void rollTownPlan( UnsignedInt seed, Int town, Bool smallest, RMGTownPlan *plan )
{
	plan->m_rotation = (Real)(hashCell( seed + 613, town, 0 ) % 8U) * (PI * 0.25f);

	const UnsignedInt streetChoices = smallest ? 1U : (UnsignedInt)(RMG_TOWN_MAX_STREETS - RMG_TOWN_MIN_STREETS + 1);
	plan->m_streetsAcross = RMG_TOWN_MIN_STREETS +
			(Int)(hashCell( seed + 613, town, 1 ) % streetChoices);
	plan->m_streetsDown = RMG_TOWN_MIN_STREETS +
			(Int)(hashCell( seed + 613, town, 2 ) % streetChoices);

	plan->m_plotLength = lerpReal( RMG_TOWN_PLOT_MIN, RMG_TOWN_PLOT_MAX,
																 hashUnit( seed + 613, town, 3 ) );
	plan->m_setBack = lerpReal( RMG_TOWN_SET_BACK_MIN, RMG_TOWN_SET_BACK_MAX,
															hashUnit( seed + 613, town, 4 ) );

	const Real blockMax = smallest ? RMG_TOWN_SMALL_BLOCK_MAX : RMG_TOWN_BLOCK_MAX;
	Real spanAcross = rollStreetLines( seed + 6131, town, plan->m_streetsAcross, blockMax, plan->m_acrossAt );
	Real spanDown = rollStreetLines( seed + 6133, town, plan->m_streetsDown, blockMax, plan->m_downAt );
	Real span = (spanAcross > spanDown) ? spanAcross : spanDown;

	/* Every clearance a town is placed with - the map's edge, the money, the starts - is this
		radius, so it reaches the corner plot: a building stands its set-back and its jitter off the
		outermost street, and the corner of the grid is that far out on both axes at once. Half the
		longer side, as it was, let corner buildings stand on a supply pile or past the map's edge.
		The thinning keeps measuring against the half side, so a town is no denser than it was. */
	Real halfAcross = spanAcross * 0.5f + plan->m_setBack + RMG_TOWN_JITTER;
	Real halfDown = spanDown * 0.5f + plan->m_setBack + RMG_TOWN_JITTER;
	plan->m_radius = sqrtf( halfAcross * halfAcross + halfDown * halfDown ) + 4.0f;
	plan->m_thinRadius = span * 0.5f + plan->m_setBack + 4.0f;
}

/// A point in the town's own coordinates, put where the map has it.
static void townToWorld( Real centreX, Real centreY, const RMGTownPlan& plan, Real across, Real down,
												 Real *outX, Real *outY )
{
	Real cosine = Cos( plan.m_rotation );
	Real sine = Sin( plan.m_rotation );

	*outX = centreX + across * cosine - down * sine;
	*outY = centreY + across * sine + down * cosine;
}

/// How far a plot is from the nearest street crossing it, so junctions are left as junctions.
static Real nearestStreetDistance( const Real *streetAt, Int streets, Real value )
{
	Real nearest = 1.0e9f;

	for( Int line = 0; line < streets; line++ )
	{
		Real distance = fabsf( value - streetAt[line] );
		if( distance < nearest )
			nearest = distance;
	}

	return nearest;
}

/** A water body is the contour of a flooded basin (and any stream that joined it), not a circle
	with a noisy radius. The polygon the engine floods is that contour, so the shore the game
	draws is the same shape the height field was carved to. */
struct RMGLake
{
	Real m_cellX;
	Real m_cellY;
	std::vector<RMGPoint> m_polygon;
};

/** One table of a massif map's high ground: an ellipse turned to an angle, its outline wandering
	with the bearing, its rim a cliff on some bearings and a slope on others. A massif is one of these
	or two with a pass between them. */
struct RMGMassifLobe
{
	Real m_cellX;			///< the middle, playable cells
	Real m_cellY;
	Real m_major;			///< cells from the middle to the foot along the long axis, before the wander
	Real m_minor;			///< and along the short one
	Real m_cos;				///< the long axis' direction
	Real m_sin;
	Real m_height;			///< height bytes the table stands over the ground
	Real m_salt;			///< offsets this lobe's outline noise from the other's
	Real m_crownX;			///< the rise on top: its middle in the lobe's own axes, cells
	Real m_crownY;
	Real m_shapeCos2;		///< the outline's two- and three-lobed swings, amplitude times the cosine
	Real m_shapeSin2;		///< and sine of each one's turn
	Real m_shapeCos3;
	Real m_shapeSin3;
};

/** How far out a lobe's foot reaches on a bearing (ux, uy, a unit vector in the lobe's own ellipse
	axes), over its plain ellipse. A sum of a two-lobed and a three-lobed swing, written out in the
	vector's components so no angle is taken: it bulges and pinches the outline into a shape of its own
	where the noise wander alone left a scalloped disc. */
static Real rmgLobeShape( const RMGMassifLobe& lobe, Real ux, Real uy )
{
	Real cos2 = ux * ux - uy * uy;
	Real sin2 = 2.0f * ux * uy;
	Real cos3 = ux * ( ux * ux - 3.0f * uy * uy );
	Real sin3 = uy * ( 3.0f * ux * ux - uy * uy );
	return 1.0f + lobe.m_shapeCos2 * cos2 + lobe.m_shapeSin2 * sin2 + lobe.m_shapeCos3 * cos3 + lobe.m_shapeSin3 * sin3;
}

/// A place something has been put, and how much room it wants around it.
struct RMGSite
{
	Real m_cellX;
	Real m_cellY;
	Real m_radius;
};

/// A player's own money and the walk it was placed at, so the finished map can be held to it.
struct RMGMoneyWalk
{
	Int m_player;
	Int m_rival;		///< for a contested dock, the neighbour it has to stay further from; else -1
	Int m_object;		///< and the dock's index in m_objects
	Int m_cellX;		///< map cells, border included
	Int m_cellY;
	Int m_walk;
};

/** Everything derived from the settings, in one place, so the map bytes and the
	preview tga are two views of the same layout rather than two guesses at it. */
class RMGLayout
{
public:
	/** skeleton: the ground, the starts, the money and the AI's paths for a template map
		(RandomMapTemplate.cpp) and nothing it would throw away - no towns, bunkers, waves or
		scenery - on plain open ground unless a style asks for its bowl. */
	void build( const RandomMapSettings& settings, Bool skeleton = FALSE );

	/// A template map's ground given a plain map's finish (rmgFinishTemplateGround).
	void finishTemplateGround( Int playableCells, std::vector<Int>& heights, Int width, Int height,
		const std::vector< std::vector<Int> >& roads, const std::vector<char>& keep, const std::vector<char>& walls, Bool settle );

	Int cellIndex( Int x, Int y ) const { return y * m_width + x; }
	UnsignedByte heightAtCell( Int x, Int y ) const { return m_heights[cellIndex( x, y )]; }
	UnsignedByte terrainAtCell( Int x, Int y ) const { return m_terrain[cellIndex( x, y )]; }
	Bool passableAtCell( Int x, Int y ) const { return m_passable[cellIndex( x, y )] != 0; }
	Bool underwaterAtCell( Int x, Int y ) const;
	/** Inside the boundary writeLayout gives the pathfinder, which clamps every unit to it: a map cell
		of the border band is nobody's ground, however flat. */
	Bool insidePlayable( Int x, Int y ) const
	{
		return x >= RMG_BORDER_CELLS && y >= RMG_BORDER_CELLS && x < RMG_BORDER_CELLS + m_settings.m_playableCells
			&& y < RMG_BORDER_CELLS + m_settings.m_playableCells;
	}
	Bool m_playable;					///< every start distinct, dry and reachable from the others, every money reachable
	std::vector< std::vector<Int> > m_roads;	///< the cells along every carved pass, massif ramp and street, in order (roadCells)
	std::vector<Bool> m_roadCarved;		///< per m_roads entry, whether it is a way routeProfile cut (relayRoads lays it again)
	const std::vector<char> *m_carveKeep;	///< while relayRoads runs, the ground cutTowardLevel leaves alone; else NULL

	RandomMapSettings m_settings;
	const RMGStyle *m_style;			///< the shipped map this one is in the style of, or NULL
	const RMGMapTypeShape *m_type;		///< what kind of map the seed rolled
	const RMGBiome *m_biome;			///< and what it is made of
	TimeOfDay m_timeOfDay;				///< the hour the map is lit for
	Bool m_snowy;						///< weather: snow on the roofs and in the air
	Bool m_skeleton;					///< built for a template map to dress (build's skeleton)
	Int m_width;						///< map cells per side, border included
	Int m_height;
	Real m_waterHeight;					///< height bytes; the surface of every lake
	std::vector<RMGPoint> m_starts;
	std::vector<UnsignedByte> m_heights;
	std::vector<UnsignedByte> m_landHeights;	///< the height field eased onto its floor, swapped in once the lakes are grown
	std::vector<UnsignedByte> m_terrain;
	std::vector<Short> m_blendIndex;	///< into m_blends, 0 for a cell with no blend
	std::vector<Short> m_extraBlendIndex;	///< the second layer, drawn over the first; 0 for none
	std::vector<char> m_passable;
	std::vector<RMGLake> m_lakes;
	std::vector<RMGPoint> m_riverLine;	///< a river map's centre line, from map edge to map edge
	std::vector<Real> m_riverHalf;		///< and the river's half-width at each point of it, cells
	Real m_riverAngle;					///< and the way it runs
	std::vector<RMGMassifLobe> m_massifLobes;	///< a massif map's high ground, one or two tables
	std::vector<RMGObject> m_objects;
	std::vector< std::pair<Int, Int> > m_waypointLinks;	///< waypointID to waypointID, for WaypointsList
	std::vector<RMGArea> m_areas;		///< named areas past the lakes and the base perimeters (buildLane)
	Int m_aodFamily;					///< an Art of Defense map's lane family (buildAod), -1 on any other
	Int m_aodSkipped;					///< families it gave way past from the seed's; RMG_AOD_FAMILIES when none fit (the fallback)

	/// The blend table the BlendTileData chunk carries; entry 0 is the "no blend" default.
	struct RMGBlend
	{
		Int m_blendTileIndex;
		UnsignedByte m_horizontal;
		UnsignedByte m_vertical;
		UnsignedByte m_rightDiagonal;
		UnsignedByte m_leftDiagonal;
		UnsignedByte m_inverted;
		UnsignedByte m_longDiagonal;
	};
	std::vector<RMGBlend> m_blends;

private:
	void buildLane( void );
	void buildApocalypse( void );
	void buildAod( void );
	void placeBowlStarts( void );
	Real bowlRimAt( Real unitX, Real unitY ) const;
	Real bowlHeight( Real cellX, Real cellY, Real noise ) const;
	void buildHeights( const UnsignedByte perm[512] );
	void placeLakes( const UnsignedByte perm[512] );
	void growBasin( const UnsignedByte perm[512], Int seedX, Int seedY, Int targetCells, Real fillRise,
									std::vector<Int> *painted );
	void openWater( Int openRadius, Int closeRadius );
	void trimLakeReach( void );
	void openEachLake( void );
	Real lakeRoundness( const std::vector<Int>& cells ) const;
	Real lakeEllipseFill( const std::vector<Int>& cells ) const;
	void openLakeCells( const std::vector<Int>& cells, Int radius );
	void planMassif( void );
	void shapeMassifLobe( RMGMassifLobe& lobe, Int which ) const;
	Real massifRim( const RMGMassifLobe& lobe, Real cellX, Real cellY, Real *rimOut ) const;
	Real massifLift( Real cellX, Real cellY, Real *heightOut ) const;
	Real lakeEdgeRise( const UnsignedByte perm[512], Int mapX, Int mapY ) const;
	void paintRiver( const UnsignedByte perm[512] );
	void paintMainRiver( const UnsignedByte perm[512] );
	void paintRiverStretches( const std::vector<Int>& fords );
	void openFordsAtStarts( const std::vector<UnsignedByte>& uncarved );
	void fordsForStarts( std::vector<Int>& fords ) const;
	void priceRiverByFords( std::vector<Int>& cost ) const;
	void extractLakePolygons( void );
	void buildShoreDistance( std::vector<Real> &dist, Bool straight ) const;
	Bool waterAllowedAt( Int mapX, Int mapY ) const;
	void carveLakeBasins( void );
	void buildLakeMask( void );
	void chooseStarts( void );
	void searchStarts( const std::vector<RMGStartCandidate>& candidates );
	Bool addDryStart( void );
	void sortStartsRoundCentre( void );
	void ringStarts( const std::vector<RMGStartCandidate>& candidates, Real angle, Real ring, Real edgeRing );
	void startWalks( const std::vector<Int>& cost, std::vector<Int>& walks ) const;
	static Real walkSpread( const std::vector<Int>& walks, Int *nearLowOut );
	void nudgeStarts( const std::vector<RMGStartCandidate>& candidates, const std::vector<Int>& cost, Int farthest );
	void flattenBases( void );
	void shapeBases( void );
	void levelStartSquares( const std::vector<Real>& startHeights );
	void buildPassability( void );
	void connectStarts( void );
	Real massifClearance( Real cellX, Real cellY ) const;
	void carveMassifRamps( void );
	Real rideDistance( Real cellX, Real cellY ) const;
	Bool nearRamp( Real cellX, Real cellY, Real radius ) const;
	Bool carvePass( Int fromStart, Int toStart );
	Bool carvePassBetweenCells( Int fromX, Int fromY, Int toX, Int toY, Bool recordRamp,
															Real protectRadius, Real protectX, Real protectY );
	Int crestAt( Int index ) const;
	void smoothRoute( std::vector<Int>& route ) const;
	Bool roadMoveFits( Int laid, Real movedX, Real movedY ) const;
	Int roadStepCost( Int from, Int to, Bool diagonal, std::vector<Int>& cellPrice ) const;
	void applyRouteProfile( const std::vector<Int>& route, Bool recordRamp,
													Real protectRadius, Real protectX, Real protectY );
	Bool carveStraightCorridor( Int fromX, Int fromY, Int toX, Int toY,
															Real protectRadius, Real protectX, Real protectY );
	void cutTowardLevel( Int x, Int y, Real level, Bool skipLake,
											 Real protectRadius, Real protectX, Real protectY );
	Real plateOutside( Real across, Real down, Real halfAcross, Real halfDown, Real playX, Real playY,
										 Real standOff ) const;
	void gradeTown( Real centreX, Real centreY, const RMGTownPlan& plan,
									Real acrossStart, Real acrossEnd, Real downStart, Real downEnd );
	Bool playableAtCell( Int x, Int y ) const;
	Int ringCrossings( Int startIndex ) const;
	void floodPlayableFrom( Int startIndex );
	Bool openSecondExit( Int startIndex );
	void ensurePlayability( void );
	void holdMoneyWalks( void );
	void startPerimeterToward( Int startIndex, Int targetX, Int targetY, Int *outX, Int *outY ) const;
	void buildTerrainClasses( const UnsignedByte perm[512] );
	void buildBlends( void );
	void placeMoney( void );
	void placeDressing( const UnsignedByte perm[512] );
	Bool everyoneReaches( void );
	Bool lockedWithin( Real cellX, Real cellY, Int radius ) const;
	void clearLockedGround( void );
	void addApproachPaths( Int *waypointID );
	void addApproachWaypoint( const char *lane, Int targetStart, Int fromStart, Int step, Real cellX, Real cellY,
														Int *waypointID, Int *previous );
	void placeSupplyAndDerricks( void );
	void placeTowns( void );
	void placeBunkers( void );
	void placeShoreWaves( void );
	void placeScenery( const UnsignedByte perm[512] );
	Bool stepDownFlood( const std::vector<Int>& dist, Int *x, Int *y, Real fromX, Real fromY,
											Real toX, Real toY ) const;
	void buildLaneDistance( void );
	Int laneDistanceAt( Real cellX, Real cellY ) const;
	Bool sceneryMayStand( Real cellX, Real cellY ) const;
	void plantGroves( const char *const *theTreeNames, Int *propID, Int *treeBudget );
	void flattenPad( Real cellX, Real cellY, Real radius, Real blend );
	void addRoad( Real fromX, Real fromY, Real toX, Real toY );
	void drawCarvedWays( void );
	void recordRoad( Real fromX, Real fromY, Real toX, Real toY );
	Int spanBytes( Int x, Int y ) const;
	void markStandingGround( std::vector<char>& keep ) const;
	void settleSlopes( void );
	void settleSlopesKeeping( std::vector<char>& keep, const std::vector<char> *walls, Int passes, Int most );
	void routeProfile( const std::vector<Int>& route, std::vector<Real>& profile ) const;
	void relayRoads( void );
	void addRoadIfDry( Real fromX, Real fromY, Real toX, Real toY );
	Bool dryAt( Real cellX, Real cellY, Real margin ) const;

	Real cellSpanWorld( Int x, Int y ) const;
	Real roughnessAt( Int cellX, Int cellY, Int radius ) const;
	Bool insideLake( Real cellX, Real cellY, Real *distanceOut ) const;
	Real distanceToNearestStart( Real cellX, Real cellY ) const;
	Bool siteIsClear( Real cellX, Real cellY, Real radius ) const;
	void markWayDistance( void );
	Bool findSiteNear( Real centreX, Real centreY, Real minRadius, Real maxRadius, Real clearance,
										 RMGPoint *out ) const;
	Bool findSiteOnRing( Real centreX, Real centreY, Real targetRadius, Real band, Real clearance,
											 RMGPoint *out ) const;
	Bool findSiteOnBearing( Real centreX, Real centreY, Real targetRadius, Real band, Real bearing,
													Real clearance, RMGPoint *out ) const;
	Bool findSiteOnCompass( Real centreX, Real centreY, Real targetRadius, Real band, Real preferred,
													Real clearance, RMGPoint *out ) const;
	Bool spotIsBuildable( Real x, Real y, Real clearance ) const;
	Real outwardBearing( Real cellX, Real cellY ) const;
	Bool findContestedSite( Int startA, Int startB, Real minWalk, Real clearance, RMGPoint *out ) const;
	void floodDistancesFromCell( Int startX, Int startY, std::vector<Int>& dist ) const;
	void floodDistancesFromCell( Int startX, Int startY, std::vector<Int>& dist, std::vector<Int>& queue ) const;
	void floodFromEveryStart( std::vector< std::vector<Int> >& dist );
	static void floodFromStartJob( Int start, void *context );
	void walkFromStarts( std::vector< std::vector<Int> >& walk, std::vector<Int>& owner,
											 std::vector<Int>& lead );
	Bool findSiteByWalk( const std::vector< std::vector<Int> >& walk, const std::vector<Int>& owner,
											 const std::vector<Int>& lead, Int player, Int target, Int band, Int minLead,
											 Int maxLead, Real bearing, Real clearance, RMGPoint *out, Int rival = -1,
											 Int rivalMaxLead = RMG_MONEY_NO_RIVAL ) const;
	Bool placersSide( Int mapX, Int mapY, Int player, Int rival, Int maxLead ) const;
	void lockRoute( const std::vector<Int>& walk, const RMGPoint& site );
	void reserveSite( Real cellX, Real cellY, Real radius );
	void addObject( const char *templateName, const char *uniqueID, Real cellX, Real cellY,
									Real angle );
	Short blendEntryFor( Int blendTileIndex, Int shapeIndex, Bool flipped );
	Int blendLayersAt( Int x, Int y, Int *classes, Int *masks ) const;
	void settleTerrainForBlends( void );

	UnsignedByte m_perm[512];			///< the seed's noise permutation, for the stamps that wobble

	// A style's bowl, worked out with its starts (placeBowlStarts), in cells.
	Real m_styleScale;					///< the generated map's size over the shipped one's, held to 0.6 - 1.2
	Real m_bowlSeatRing;				///< starts' distance from the middle
	Real m_bowlRim;						///< the rim's, before it wanders
	Real m_bowlRampLength;
	Real m_bowlRidgeStart;				///< round the ring from a start, where the ridge begins to climb
	std::vector<RMGPoint> m_seatDirections;	///< each start's bearing from the middle, a unit vector
	std::vector<Real> m_seatRims;		///< and the rim's distance on that bearing

	std::map<Int, Short> m_blendLookup;	///< tile and shape to the table entry that holds them
	std::vector<RMGSite> m_sites;		///< everything placed so far, with its elbow room
	std::vector<char> m_routeLock;		///< cells on a walk to somebody's money or a repair's roadway, which pads and buildings leave alone
	Bool m_lockCarves;					///< cutTowardLevel locks the roadway it cuts: the repairs after the money
	std::vector<UnsignedByte> m_wayDistance;	///< cells from the nearest carved way, capped (markWayDistance)
	Bool m_padsKeepOffWays;				///< siteIsClear also keeps RMG_PAD_WAY_CLEAR cells off every carved way
	std::vector< std::vector<Int> > m_floodQueues;	///< a breadth-first queue a start, kept between floods
	std::vector<char> m_baseLevelled;	///< cells a base blend has touched
	std::vector<UnsignedByte> m_baseUnder;	///< and the ground it blended there
	std::vector<UnsignedByte> m_baseResult;	///< and what it left, so a second levelling can tell
	std::vector<RMGPoint> m_baseShapeStarts;	///< the starts the three below were worked out for
	std::vector<Int> m_baseCells;		///< cells a base levelling touches
	std::vector<Real> m_baseBlendT;		///< and how far into the blend each is, 0 on the flat
	std::vector<UnsignedByte> m_baseOwners;	///< and whose base it is
	std::vector<Real> m_baseLevels;		///< the height each base is levelled to, height bytes
	std::vector<RMGMoneyWalk> m_moneyWalks;	///< every walked placement of a player's own money
	std::vector<RMGPoint> m_ramps;		///< where a carved route changed layer, for the bunkers
	std::vector<char> m_inLake;			///< cells the basins drowned, whatever the ground does
	std::vector<Real> m_shoreDist;		///< signed cells to shore: negative in the water
	std::vector<char> m_visited;		///< scratch for the flood fill
	Int m_startSearchStride;

	/** The ground route from one start to another, as map cell indices from the first, the way the
		"Center" attack path walks it. The woods are laid out against these. */
	struct RMGLane
	{
		Int m_from;
		Int m_to;
		std::vector<Int> m_cells;
	};
	std::vector<RMGLane> m_lanes;
	std::vector<UnsignedByte> m_laneDist;	///< cells to the nearest route, capped at RMG_LANE_FAR
};

//-----------------------------------------------------------------------------
// A style's bowl
//-----------------------------------------------------------------------------

/** A style's starts go down before the ground, because the ground is built round them: one seat a
	turn of the ring, the ring turned by the seed, each seat nudged a little round it and in or out so
	no two maps share an outline. Ordered round the ring, which is the order connectStarts joins
	neighbours in. */
void RMGLayout::placeBowlStarts( void )
{
	const Real playable = (Real)m_settings.m_playableCells;
	const Real half = playable * 0.5f;
	const Real k = m_styleScale;
	const Int players = m_settings.m_numPlayers;

	m_bowlSeatRing = half - ( 1.0f - m_style->m_startRing ) * (Real)m_style->m_cells * 0.5f * k;
	m_bowlRim = m_bowlSeatRing - 0.85f * m_style->m_plateau * k;
	m_bowlRampLength = m_style->m_raise / RMG_BOWL_RAMP_GRADE * k;
	if( m_bowlRampLength < RMG_BOWL_RAMP_MIN )
		m_bowlRampLength = RMG_BOWL_RAMP_MIN;
	m_bowlRidgeStart = 0.8f * m_style->m_plateau * k;

	const Real margin = RMG_FLAT_RADIUS + RMG_START_MARGIN;
	const Real turn = hashUnit( m_settings.m_seed, 9001, 31 ) * 2.0f * PI;
	const Real seatArc = 2.0f * PI / (Real)players;

	m_starts.clear();
	m_seatDirections.clear();
	m_seatRims.clear();
	for( Int i = 0; i < players; i++ )
	{
		Real angle = turn + seatArc * ( (Real)i + 0.16f * ( hashUnit( m_settings.m_seed, i, 9031 ) - 0.5f ) );
		Real ring = m_bowlSeatRing + 0.06f * half * ( hashUnit( m_settings.m_seed, i, 9032 ) - 0.5f );

		RMGPoint start;
		start.m_cellX = half + ring * Cos( angle );
		start.m_cellY = half + ring * Sin( angle );
		if( start.m_cellX < margin ) start.m_cellX = margin;
		if( start.m_cellY < margin ) start.m_cellY = margin;
		if( start.m_cellX > playable - margin ) start.m_cellX = playable - margin;
		if( start.m_cellY > playable - margin ) start.m_cellY = playable - margin;
		m_starts.push_back( start );

		// the spoke through the seat as it ended up, so its ramp leaves from in front of it
		Real dx = start.m_cellX - half;
		Real dy = start.m_cellY - half;
		Real length = sqrtf( dx * dx + dy * dy );
		RMGPoint toward;
		toward.m_cellX = dx / length;
		toward.m_cellY = dy / length;
		m_seatDirections.push_back( toward );
		m_seatRims.push_back( bowlRimAt( toward.m_cellX, toward.m_cellY ) );
	}
}

/// How far the rim is from the middle on this bearing. It wanders, so the bowl is no circle.
Real RMGLayout::bowlRimAt( Real unitX, Real unitY ) const
{
	Real wander = 2.0f * fractalNoise( m_perm, unitX * 2.4f + 71.0f, unitY * 2.4f - 13.0f, 3 );
	if( wander > 1.0f ) wander = 1.0f;
	if( wander < -1.0f ) wander = -1.0f;
	return m_bowlRim * ( 1.0f + 0.06f * wander );
}

/** The ground of a bowl at a cell, from the roll the noise gave it there. Out past the rim it is the
	plateau, the style's raise above the floor with most of the roll taken off; the rim is a cliff all
	round. In front of each start a ramp runs from the rim down into the bowl as a spur with steep
	sides, its top a plateau's depth in front of the seat. Round the ring from a seat the plateau climbs
	to a ridge once it is a plateau's width from the seat, so neighbours are joined over high ground and
	each base reads as its own shelf: one way down, two up. */
Real RMGLayout::bowlHeight( Real cellX, Real cellY, Real noise ) const
{
	const Real half = (Real)m_settings.m_playableCells * 0.5f;
	const Real k = m_styleScale;
	Real dx = cellX - half;
	Real dy = cellY - half;
	Real r = sqrtf( dx * dx + dy * dy );
	Real ux = r > 0.5f ? dx / r : 1.0f;
	Real uy = r > 0.5f ? dy / r : 0.0f;

	// the seat whose spoke is nearest in bearing
	UnsignedInt seat = 0;
	Real nearest = -2.0f;
	for( UnsignedInt i = 0; i < m_seatDirections.size(); i++ )
	{
		Real dot = ux * m_seatDirections[i].m_cellX + uy * m_seatDirections[i].m_cellY;
		if( dot > nearest )
		{
			nearest = dot;
			seat = i;
		}
	}
	const RMGPoint& toward = m_seatDirections[seat];
	Real along = dx * toward.m_cellX + dy * toward.m_cellY;
	Real across = fabsf( dx * toward.m_cellY - dy * toward.m_cellX );

	Real plateau = fadeCurve( rmgUnit( ( r - bowlRimAt( ux, uy ) + RMG_BOWL_CLIFF_CELLS ) / RMG_BOWL_CLIFF_CELLS ) );

	// The ramp is cut no wider than the shipped map's ways down, and its banks take half as much again.
	const Real rampHalf = 0.25f * m_style->m_rampWidth * k;
	const Real bank = 0.5f * rampHalf;
	Real lift = plateau;
	Real ramp = fadeCurve( rmgUnit( ( along - m_seatRims[seat] + m_bowlRampLength ) / m_bowlRampLength ) );
	if( along > 0.0f && ramp > plateau )
	{
		Real weight = 1.0f - fadeCurve( rmgUnit( ( across - rampHalf ) / bank ) );
		lift = plateau + weight * ( ramp - plateau );
	}

	/* The ridge starts a plateau's width round the ring from the seat, measured at the seats'
		distance, so it is a wedge out to the map's edge. Its face is a cliff but for one way up, as
		wide as the way down, where the ring through the seats crosses it. */
	Real gentle = 1.0f - fadeCurve( rmgUnit( ( fabsf( r - m_bowlSeatRing ) - rampHalf ) / bank ) );
	Real rise = RMG_BOWL_FACE_CELLS + gentle * ( RMG_BOWL_RIDGE_RISE * k - RMG_BOWL_FACE_CELLS );
	Real over = ( ATan2( across, along ) - m_bowlRidgeStart / m_bowlSeatRing ) * r;
	Real ridge = fadeCurve( rmgUnit( over / rise ) ) * plateau;

	return RMG_BASE_HEIGHT + noise * ( 1.0f - 0.95f * lift * ( 1.0f - 0.5f * ridge ) )
		+ m_style->m_raise * lift + m_style->m_ridge * ridge;
}

//-----------------------------------------------------------------------------
// A massif map's high ground
//-----------------------------------------------------------------------------

/** The seed rolls the massif's size, how long it is against how wide and which way it lies, how far
	off the middle it stands, and whether it is one table or two with a pass between them. Every
	massif used to be the same scalloped disc at the same size in the middle of the map. */
void RMGLayout::planMassif( void )
{
	m_massifLobes.clear();
	if( m_type->m_massif <= 0.0f || m_style )
		return;

	const UnsignedInt seed = (UnsignedInt)m_settings.m_seed;
	const Real playable = (Real)m_settings.m_playableCells;
	const Real radius = playable * lerpReal( RMG_MASSIF_RADIUS_MIN, RMG_MASSIF_RADIUS, hashUnit( seed, 9101, 1 ) );
	const Real stretch = sqrtf( lerpReal( RMG_MASSIF_STRETCH_MIN, RMG_MASSIF_STRETCH, hashUnit( seed, 9101, 2 ) ) );
	const Real axis = hashUnit( seed, 9101, 3 ) * PI;
	const Real offAngle = hashUnit( seed, 9101, 4 ) * 2.0f * PI;
	const Real off = playable * RMG_MASSIF_OFFSET * hashUnit( seed, 9101, 5 );

	RMGMassifLobe lobe = {};
	lobe.m_cos = Cos( axis );
	lobe.m_sin = Sin( axis );
	lobe.m_cellX = playable * 0.5f + off * Cos( offAngle );
	lobe.m_cellY = playable * 0.5f + off * Sin( offAngle );
	lobe.m_major = radius * stretch;
	lobe.m_minor = radius / stretch;
	lobe.m_height = m_type->m_massif * ( 0.9f + 0.25f * hashUnit( seed, 9101, 7 ) );
	lobe.m_salt = 0.0f;
	shapeMassifLobe( lobe, 0 );

	if( hashCell( seed, 9101, 6 ) % 100U < (UnsignedInt)RMG_MASSIF_SPLIT_IN_100 )
	{
		// Two tables along the long axis, the second a step lower, with a saddle of open ground
		// between their feet.
		const Real centreX = lobe.m_cellX;
		const Real centreY = lobe.m_cellY;
		const Real apart = 0.68f * lobe.m_major;
		RMGMassifLobe second = lobe;
		lobe.m_major *= 0.46f;
		lobe.m_minor *= 0.85f;
		second.m_major = lobe.m_major;
		second.m_minor = lobe.m_minor;
		lobe.m_cellX = centreX + apart * lobe.m_cos;
		lobe.m_cellY = centreY + apart * lobe.m_sin;
		second.m_cellX = centreX - apart * lobe.m_cos;
		second.m_cellY = centreY - apart * lobe.m_sin;
		second.m_height = m_type->m_massif * ( 0.75f + 0.15f * hashUnit( seed, 9101, 8 ) );
		second.m_salt = 17.0f;
		shapeMassifLobe( second, 1 );
		second.m_crownX = ( hashUnit( seed, 9101, 11 ) - 0.5f ) * 0.5f * second.m_major;
		second.m_crownY = ( hashUnit( seed, 9101, 12 ) - 0.5f ) * 0.5f * second.m_minor;
		lobe.m_crownX = ( hashUnit( seed, 9101, 9 ) - 0.5f ) * 0.5f * lobe.m_major;
		lobe.m_crownY = ( hashUnit( seed, 9101, 10 ) - 0.5f ) * 0.5f * lobe.m_minor;
		m_massifLobes.push_back( lobe );
		m_massifLobes.push_back( second );
		return;
	}

	lobe.m_crownX = ( hashUnit( seed, 9101, 9 ) - 0.5f ) * 0.5f * lobe.m_major;
	lobe.m_crownY = ( hashUnit( seed, 9101, 10 ) - 0.5f ) * 0.5f * lobe.m_minor;
	m_massifLobes.push_back( lobe );
}

/** The lobe's own outline: a two-lobed and a three-lobed swing, each at a rolled strength and turn. */
void RMGLayout::shapeMassifLobe( RMGMassifLobe& lobe, Int which ) const
{
	const UnsignedInt seed = (UnsignedInt)m_settings.m_seed;
	const Int salt = 20 + 4 * which;
	Real amount2 = RMG_MASSIF_SHAPE2 * ( 0.55f + 0.45f * hashUnit( seed, 9101, salt ) );
	Real turn2 = hashUnit( seed, 9101, salt + 1 ) * 2.0f * PI;
	Real amount3 = RMG_MASSIF_SHAPE3 * ( 0.55f + 0.45f * hashUnit( seed, 9101, salt + 2 ) );
	Real turn3 = hashUnit( seed, 9101, salt + 3 ) * 2.0f * PI;
	lobe.m_shapeCos2 = amount2 * Cos( turn2 );
	lobe.m_shapeSin2 = amount2 * Sin( turn2 );
	lobe.m_shapeCos3 = amount3 * Cos( turn3 );
	lobe.m_shapeSin3 = amount3 * Sin( turn3 );
}

/** Cells in from a lobe's foot to this point, negative outside it, and the rim's width on its
	bearing. The outline wanders with a noise read off the bearing, which keeps it one closed shape;
	fractal noise only reaches a third or so of its range, so it is doubled and clamped, and it reaches
	further in than out, since the outer bound is what the start search keeps clear of. A second noise
	on the bearing makes the rim a cliff on some stretches and a slope a tank drives on others: those
	are the ways up, and the ramps carved later move onto them. The slope reaches half its extra width
	out past the cliff's foot and half in. */
Real RMGLayout::massifRim( const RMGMassifLobe& lobe, Real cellX, Real cellY, Real *rimOut ) const
{
	Real dx = cellX - lobe.m_cellX;
	Real dy = cellY - lobe.m_cellY;
	Real a = dx * lobe.m_cos + dy * lobe.m_sin;
	Real b = dy * lobe.m_cos - dx * lobe.m_sin;
	Real na = a / lobe.m_major;
	Real nb = b / lobe.m_minor;
	Real q = sqrtf( na * na + nb * nb );

	Real slopeRim = RMG_MASSIF_SLOPE_RIM;
	if( slopeRim > 0.5f * lobe.m_minor )
		slopeRim = 0.5f * lobe.m_minor;
	if( slopeRim < RMG_MASSIF_RIM )
		slopeRim = RMG_MASSIF_RIM;

	if( q < 0.001f )
	{
		*rimOut = RMG_MASSIF_RIM;
		return lobe.m_minor;
	}
	Real ux = na / q;
	Real uy = nb / q;

	Real wander = 2.0f * fractalNoise( m_perm, ux * 1.6f + 41.0f + lobe.m_salt, uy * 1.6f - 37.0f, 2 );
	if( wander > 1.0f ) wander = 1.0f;
	if( wander < -1.0f ) wander = -1.0f;
	Real footQ = ( 1.0f + ( wander > 0.0f ? RMG_MASSIF_WOBBLE : RMG_MASSIF_WOBBLE_IN ) * wander ) * rmgLobeShape( lobe, ux, uy );

	Real gentle = 0.5f + 1.6f * fractalNoise( m_perm, ux * 1.7f + 5.0f + lobe.m_salt, uy * 1.7f + 11.0f, 2 );
	gentle = fadeCurve( rmgUnit( ( gentle - 0.45f ) / 0.2f ) );
	Real rim = RMG_MASSIF_RIM + gentle * ( slopeRim - RMG_MASSIF_RIM );
	*rimOut = rim;

	Real r = sqrtf( a * a + b * b );
	return r * ( footQ / q - 1.0f ) + 0.5f * ( rim - RMG_MASSIF_RIM );
}

/** How far up the massif a point is, 0 on the plain to 1 on the top, and the height in bytes the top
	stands over the plain there. Each lobe's top rises toward an off-centre crown, so it has a high side
	and a low side instead of being one flat sheet. */
Real RMGLayout::massifLift( Real cellX, Real cellY, Real *heightOut ) const
{
	Real best = 0.0f;
	Real bestLift = 0.0f;
	*heightOut = 0.0f;
	for( UnsignedInt l = 0; l < m_massifLobes.size(); l++ )
	{
		const RMGMassifLobe& lobe = m_massifLobes[l];
		Real rim;
		Real inside = massifRim( lobe, cellX, cellY, &rim );
		if( inside <= 0.0f )
			continue;
		Real lift = fadeCurve( rmgUnit( inside / rim ) );

		/* The top rises toward an off-centre crown over most of the lobe's width, with a noise of its
			own warping the rise, so the high ground has a high end, a low end and knolls. A crown with a
			short edge of its own read from above as a ring laid on the table. */
		Real height = lobe.m_height;
		Real dx = cellX - lobe.m_cellX;
		Real dy = cellY - lobe.m_cellY;
		Real a = dx * lobe.m_cos + dy * lobe.m_sin - lobe.m_crownX;
		Real b = dy * lobe.m_cos - dx * lobe.m_sin - lobe.m_crownY;
		Real na = a / ( RMG_MASSIF_CROWN_SPAN * lobe.m_major );
		Real nb = b / ( RMG_MASSIF_CROWN_SPAN * lobe.m_minor );
		Real rise = 1.0f - sqrtf( na * na + nb * nb )
			+ 0.8f * fractalNoise( m_perm, cellX * 0.05f + 3.0f + lobe.m_salt, cellY * 0.05f - 9.0f, 2 );
		height += RMG_MASSIF_CROWN * fadeCurve( rmgUnit( rise ) );

		if( lift * height > best )
		{
			best = lift * height;
			bestLift = lift;
			*heightOut = height;
		}
	}
	return bestLift;
}

//-----------------------------------------------------------------------------
// Height field
//-----------------------------------------------------------------------------

/** Warped fractal noise, left rolling. The warp is what stops the terrain reading as a bowl of
	dents: it drags the noise's own coordinates around with a second field, so ridges bend and
	valleys wander. Most of the slope is kept, so a unit can drive the hills. Valleys are cut
	deeper on a field of their own, and only the peaks of a third field sit on a shelf, which is
	where a drop still reads as a cliff. */
void RMGLayout::buildHeights( const UnsignedByte perm[512] )
{
	Real playable = (Real)m_settings.m_playableCells;
	Real scale = RMG_FEATURES_PER_MAP / playable;
	Real detailScale = RMG_DETAIL_FEATURES / playable;
	Real valleyScale = scale * 1.8f;
	Real ridgeScale = scale * 2.2f;

	m_heights.resize( m_width * m_height );
	m_landHeights.resize( m_width * m_height );

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real px = (Real)(x - RMG_BORDER_CELLS) * scale;
			Real py = (Real)(y - RMG_BORDER_CELLS) * scale;

			Real warpX = px + RMG_WARP_STRENGTH * fractalNoise( perm, px + 5.2f, py + 1.3f, 3 );
			Real warpY = py + RMG_WARP_STRENGTH * fractalNoise( perm, px - 3.7f, py + 8.1f, 3 );

			Real raw = RMG_BASE_HEIGHT
				+ RMG_AMPLITUDE * m_type->m_amplitude * fractalNoise( perm, warpX, warpY, RMG_OCTAVES );

			Real cellX = (Real)(x - RMG_BORDER_CELLS);
			Real cellY = (Real)(y - RMG_BORDER_CELLS);

			if( m_style )
				raw = bowlHeight( cellX, cellY, raw - RMG_BASE_HEIGHT );

			/* The massif goes in before the valleys and is spared by them, so its top is not a table
				with the valley field's ditches through it. Its shape, size, place and rim come from
				planMassif and massifLift. */
			Real massif = 0.0f;
			if( !m_massifLobes.empty() )
			{
				Real lift = 0.0f;
				massif = massifLift( cellX, cellY, &lift );
				// Two fifths of the roll left on the top, so it is high ground with relief of its own;
				// the table is still lifted clear of whatever stood under it.
				Real table = RMG_BASE_HEIGHT + lift + 0.4f * ( raw - RMG_BASE_HEIGHT );
				if( table < raw + 20.0f )
					table = raw + 20.0f;		// a hill under the table still ends up under it
				raw = lerpReal( raw, table, massif );
			}

			if( m_type->m_gorgeWidth > 0.0f )
			{
				/* At a third of the valley field's frequency and two octaves: a few long canyons
					that bend, not a maze. A third octave put a wiggle and a small closed loop on every
					stretch of the zero line, and each one drew two more cliff ribbons. */
				const Real gorgeScale = valleyScale * 0.32f;
				Real valley = fractalNoise( perm, cellX * gorgeScale + 13.0f, cellY * gorgeScale - 21.0f, 2 );
				/* A gorge runs along the field's zero line, which winds and forks across the map the
					way a canyon system does. Its distance from the line is the field over its slope, so
					the floor is as wide where the field turns slowly as where it turns fast; a band of
					the field's own value was a groove one or two cells across wherever the field was
					steep. The floor is flat, the walls drop over a few cells, which is a cliff, and
					ease out where a slower field says so, which is a slope a tank climbs. */
				Real stepX = fractalNoise( perm, ( cellX + 1.0f ) * gorgeScale + 13.0f, cellY * gorgeScale - 21.0f, 2 ) - valley;
				Real stepY = fractalNoise( perm, cellX * gorgeScale + 13.0f, ( cellY + 1.0f ) * gorgeScale - 21.0f, 2 ) - valley;
				Real slope = sqrtf( stepX * stepX + stepY * stepY );
				if( slope < 0.4f * gorgeScale )
					slope = 0.4f * gorgeScale;
				Real off = fabsf( valley ) / slope;
				Real reachMost = m_type->m_gorgeWidth * 1.5f + RMG_GORGE_WALL + RMG_GORGE_WALL_SWING;
				if( off < reachMost )
				{
					/* A slower field decides where the zero line is a canyon at all, so a gorge runs out
						into open ground instead of every branch of the line being cut, and it runs out
						by getting narrower and shallower together over a long stretch, not by stopping. */
					Real reach = fractalNoise( perm, cellX * valleyScale * 0.12f - 41.0f,
																		 cellY * valleyScale * 0.12f + 7.0f, 2 );
					Real depth = fadeCurve( rmgUnit( ( reach + 0.22f ) / 0.2f ) );
					Real form = 0.5f + 1.4f * fractalNoise( perm, cellX * gorgeScale * 3.0f + 5.0f,
																									 cellY * gorgeScale * 3.0f + 71.0f, 2 );
					Real floorHalf = m_type->m_gorgeWidth * ( 0.7f + 0.8f * rmgUnit( form ) )
						* ( RMG_GORGE_ENDS + ( 1.0f - RMG_GORGE_ENDS ) * depth );
					Real wall = RMG_GORGE_WALL + RMG_GORGE_WALL_SWING * fadeCurve( rmgUnit( 1.0f - form ) );
					Real cut = 1.0f;
					if( off > floorHalf )
						cut = 1.0f - fadeCurve( rmgUnit( ( off - floorHalf ) / wall ) );
					raw -= cut * depth * m_type->m_valleyDrop;
				}
			}
			else
			{
				Real valley = fractalNoise( perm, cellX * valleyScale + 13.0f,
																		cellY * valleyScale - 21.0f, 3 );
				if( massif > 0.0f )
					valley = lerpReal( valley, 0.0f, massif );
				if( valley < -0.22f )
				{
					Real cut = ( -0.22f - valley ) / 0.78f;
					raw -= cut * cut * m_type->m_valleyDrop;
				}
			}

			Real ridge = fractalNoise( perm, cellX * ridgeScale + 19.0f,
																 cellY * ridgeScale - 8.0f, 3 );
			Real shelf = 0.0f;
			if( ridge > m_type->m_shelfRidge )
				shelf = ( ridge - m_type->m_shelfRidge ) / ( 1.0f - m_type->m_shelfRidge );
			shelf = shelf * shelf * shelf;

			Real layer = floorf( raw / RMG_TERRACE_STEP ) * RMG_TERRACE_STEP;
			Real h = lerpReal( raw, layer, shelf );

			/* Ground under the lake bank used to be lifted to the bank once the lakes were carved,
				and on a highland map a fifth of the playable area came out as one dead flat sheet at
				that height. The land is eased onto a floor instead, keeping the full slope down to
				softFloor + softRange and a falling share of it below, so a valley bottom still rolls
				and the detail on top of it never reaches under the bank. The lakes are still grown on
				the field without the floor: on the eased ground every low pocket is within a basin's
				climb of every other, and the water spread over whole valleys. */
			Real softFloor = m_waterHeight + RMG_LAKE_BANK + 5.0f;
			Real softRange = 16.0f;
			Real eased = h;
			if( !m_style && eased < softFloor + softRange )		// a style has no water to keep a bank for
				eased = softFloor + softRange / ( 1.0f + ( softFloor + softRange - eased ) / softRange );

			Real detail = RMG_TERRACE_DETAIL * fractalNoise( perm, (Real)x * detailScale + 61.0f,
																											 (Real)y * detailScale - 29.0f, 2 );
			h += detail;
			eased += detail;

			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;
			// A style skips the soft floor, so its pits go below nought; a negative float cast to a
			// byte is undefined, and on x64 it wrapped -3 to 253, a spike field in every deep pit.
			if( eased < 1.0f ) eased = 1.0f;
			if( eased > 254.0f ) eased = 254.0f;

			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
			m_landHeights[cellIndex( x, y )] = (UnsignedByte)(eased + 0.5f);
		}
	}
}

Real RMGLayout::cellSpanWorld( Int x, Int y ) const
{
	if( x < 0 || y < 0 || x + 1 >= m_width || y + 1 >= m_height )
		return 0.0f;

	Int a = m_heights[cellIndex( x, y )];
	Int b = m_heights[cellIndex( x + 1, y )];
	Int c = m_heights[cellIndex( x, y + 1 )];
	Int d = m_heights[cellIndex( x + 1, y + 1 )];

	Int lo = a, hi = a;
	if( b < lo ) lo = b;
	if( b > hi ) hi = b;
	if( c < lo ) lo = c;
	if( c > hi ) hi = c;
	if( d < lo ) lo = d;
	if( d > hi ) hi = d;

	return (Real)(hi - lo) * MAP_HEIGHT_SCALE;
}

/// Mean height difference from the middle of a disc, in height bytes: how flat a site is.
Real RMGLayout::roughnessAt( Int cellX, Int cellY, Int radius ) const
{
	if( cellX - radius < 0 || cellY - radius < 0 ||
			cellX + radius >= m_width || cellY + radius >= m_height )
		return 1000.0f;

	Real centre = (Real)m_heights[cellIndex( cellX, cellY )];
	Real total = 0.0f;
	Int samples = 0;

	for( Int dy = -radius; dy <= radius; dy += 2 )
	{
		for( Int dx = -radius; dx <= radius; dx += 2 )
		{
			if( dx * dx + dy * dy > radius * radius )
				continue;

			total += fabsf( (Real)m_heights[cellIndex( cellX + dx, cellY + dy )] - centre );
			samples++;
		}
	}

	if( samples == 0 )
		return 1000.0f;

	return total / (Real)samples;
}

//-----------------------------------------------------------------------------
// Water
//-----------------------------------------------------------------------------

Bool RMGLayout::insideLake( Real cellX, Real cellY, Real *distanceOut ) const
{
	if( m_shoreDist.empty() )
	{
		if( distanceOut )
			*distanceOut = 1.0e9f;
		return FALSE;
	}

	Real mapX = cellX + (Real)RMG_BORDER_CELLS;
	Real mapY = cellY + (Real)RMG_BORDER_CELLS;

	if( mapX < 0.0f || mapY < 0.0f ||
			mapX >= (Real)(m_width - 1) || mapY >= (Real)(m_height - 1) )
	{
		if( distanceOut )
			*distanceOut = 1.0e9f;
		return FALSE;
	}

	Int x0 = (Int)floorf( mapX );
	Int y0 = (Int)floorf( mapY );
	if( x0 < 0 ) x0 = 0;
	if( y0 < 0 ) y0 = 0;
	if( x0 > m_width - 2 ) x0 = m_width - 2;
	if( y0 > m_height - 2 ) y0 = m_height - 2;

	Real tx = mapX - (Real)x0;
	Real ty = mapY - (Real)y0;
	Real a = m_shoreDist[cellIndex( x0, y0 )];
	Real b = m_shoreDist[cellIndex( x0 + 1, y0 )];
	Real c = m_shoreDist[cellIndex( x0, y0 + 1 )];
	Real d = m_shoreDist[cellIndex( x0 + 1, y0 + 1 )];
	Real dist = lerpReal( lerpReal( a, b, tx ), lerpReal( c, d, tx ), ty );

	if( distanceOut )
		*distanceOut = dist;

	return dist < 0.0f;
}

Bool RMGLayout::waterAllowedAt( Int mapX, Int mapY ) const
{
	Int px = mapX - RMG_BORDER_CELLS;
	Int py = mapY - RMG_BORDER_CELLS;
	Int margin = (Int)RMG_LAKE_SHORE;
	if( px < margin || py < margin ||
			px >= m_settings.m_playableCells - margin ||
			py >= m_settings.m_playableCells - margin )
		return FALSE;

	return TRUE;
}

/** Height bytes a basin pretends the ground rises by near the edge of the water margin. A flood
	that ran into the margin itself stopped on it, and the lake came out cut along a ruled line
	parallel to the map border. Against this slope it stops on a contour of the ground plus the
	slope instead, and the noise on the band's width keeps that contour from running parallel to
	the border on flat ground. */
Real RMGLayout::lakeEdgeRise( const UnsignedByte perm[512], Int mapX, Int mapY ) const
{
	Int px = mapX - RMG_BORDER_CELLS;
	Int py = mapY - RMG_BORDER_CELLS;
	Int last = m_settings.m_playableCells - 1;
	Int edge = px;
	if( py < edge ) edge = py;
	if( last - px < edge ) edge = last - px;
	if( last - py < edge ) edge = last - py;

	Real inside = (Real)edge - RMG_LAKE_SHORE;
	if( inside >= RMG_LAKE_EDGE_BAND + 6.0f )
		return 0.0f;

	inside += 6.0f * fractalNoise( perm, (Real)px * 0.06f - 71.0f, (Real)py * 0.06f + 37.0f, 2 );
	if( inside >= RMG_LAKE_EDGE_BAND )
		return 0.0f;
	if( inside < 0.0f )
		inside = 0.0f;

	Real t = 1.0f - inside / RMG_LAKE_EDGE_BAND;
	return t * t * RMG_LAKE_EDGE_RISE;
}

/** Signed cells to the nearest shore, from the basin mask. Negative is water. Built once so
	every later test is a bilinear sample instead of a walk of the polygons.

	Both measures are a two-pass chamfer. The city-block one is what the starts, the pads and the
	money were tuned on and stays theirs. The straight one, over the 5x5 neighbourhood and within
	two percent of the true distance in every direction, is what the beach is cut from: in city
	blocks the contours of a shore running at forty-five degrees are staircases, and a beach that
	climbs fifty height bytes in ten cells came out as a stack of one-cell terraces, each step a
	rock-textured stripe down the slope. */
void RMGLayout::buildShoreDistance( std::vector<Real> &dist, Bool straight ) const
{
	const Int n = m_width * m_height;
	dist.assign( n, 1.0e9f );

	if( m_inLake.empty() )
		return;

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Int index = cellIndex( x, y );
			Bool water = m_inLake[index] != 0;
			Bool shore = FALSE;
			for( Int i = 0; i < 4; i++ )
			{
				Int nx = x + offsetX[i];
				Int ny = y + offsetY[i];
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
				{
					if( water )
						shore = TRUE;
					continue;
				}
				if( (m_inLake[cellIndex( nx, ny )] != 0) != water )
					shore = TRUE;
			}
			if( !shore )
				continue;

			dist[index] = 0.0f;
		}
	}

	// The half of the 5x5 neighbourhood that comes before a cell in raster order, the two axis
	// steps first; the backward pass reads the same offsets negated.
	static const Int chamferX[8] = { -1, 0, -1, 1, -1, 1, -2, 2 };
	static const Int chamferY[8] = { 0, -1, -1, -1, -2, -2, -1, -1 };
	static const Real chamferCost[8] = { 1.0f, 1.0f, 1.41421356f, 1.41421356f,
																			 2.23606798f, 2.23606798f, 2.23606798f, 2.23606798f };
	Int taps = straight ? 8 : 2;
	for( Int pass = 0; pass < 2; pass++ )
	{
		Int sign = pass == 0 ? 1 : -1;
		for( Int step = 0; step < n; step++ )
		{
			Int index = pass == 0 ? step : n - 1 - step;
			Int x = index % m_width;
			Int y = index / m_width;
			Real best = dist[index];
			for( Int i = 0; i < taps; i++ )
			{
				Int nx = x + sign * chamferX[i];
				Int ny = y + sign * chamferY[i];
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
					continue;
				Real through = dist[cellIndex( nx, ny )] + chamferCost[i];
				if( through < best )
					best = through;
			}
			dist[index] = best;
		}
	}

	for( Int i = 0; i < n; i++ )
	{
		Real d = dist[i];
		if( m_inLake[i] )
			dist[i] = -( d + 0.5f );
		else
			dist[i] = d + 0.5f;
	}
}

void RMGLayout::buildLakeMask( void )
{
	if( m_inLake.empty() )
		m_inLake.assign( m_width * m_height, 0 );

	buildShoreDistance( m_shoreDist, FALSE );
}

Bool RMGLayout::underwaterAtCell( Int x, Int y ) const
{
	return m_inLake[cellIndex( x, y )] != 0 &&
		(Real)m_heights[cellIndex( x, y )] < m_waterHeight;
}

/** Lowest-first flood from a seed. The open list is the basin's rim ordered by height, so the
	water follows the valley and stops at a ridge instead of growing into a circle. */
void RMGLayout::growBasin( const UnsignedByte perm[512], Int seedX, Int seedY, Int targetCells,
													 Real fillRise, std::vector<Int> *painted )
{
	painted->clear();
	if( !waterAllowedAt( seedX, seedY ) )
		return;
	if( m_inLake[cellIndex( seedX, seedY )] )
		return;

	Real seedH = (Real)m_heights[cellIndex( seedX, seedY )];
	Real maxH = seedH + fillRise;

	struct RMGBasinCell
	{
		Int m_cost;
		Int m_index;

		Bool operator<( const RMGBasinCell& other ) const
		{
			if( m_cost != other.m_cost )
				return m_cost > other.m_cost;
			return m_index > other.m_index;
		}
	};

	std::vector<RMGBasinCell> open;
	std::vector<char> queued( m_width * m_height, 0 );

	/* In sixteenths of a height byte, so the price of the distance from the seed adds to the height
		without rounding it away. A skeleton's lakes are a template's to replace and keep their old
		shape. */
	const Real rounding = m_skeleton ? 0.0f : RMG_LAKE_ROUNDING;

	/* The distance is read in an oval of the seed's own turn and length, so a lake grows as an oval
		lying its own way rather than every lake coming out a disc. */
	const UnsignedInt lakeHash = hashCell( (UnsignedInt)m_settings.m_seed + 7717U, seedX, seedY );
	const Real lakeTurn = (Real)( lakeHash % 1024U ) / 1024.0f * PI;
	const Real lakeStretch = 1.0f + ( RMG_LAKE_STRETCH - 1.0f ) * (Real)( ( lakeHash >> 10 ) % 1024U ) / 1024.0f;
	const Real lakeCos = Cos( lakeTurn ) / lakeStretch;
	const Real lakeSin = Sin( lakeTurn ) / lakeStretch;
	const Real lakeCosAcross = Cos( lakeTurn ) * lakeStretch;
	const Real lakeSinAcross = Sin( lakeTurn ) * lakeStretch;

	RMGBasinCell first;
	first.m_cost = 16 * (Int)m_heights[cellIndex( seedX, seedY )];
	first.m_index = cellIndex( seedX, seedY );
	open.push_back( first );
	queued[first.m_index] = 1;

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	/* A cell the edge slope turns away still counts against the basin's size, so a lake that runs
		into the margin ends there smaller rather than spilling the cells it lost back into the
		middle of the map, where they would sit between two starts. */
	Int edgeTurned = 0;
	while( !open.empty() && (Int)painted->size() + edgeTurned < targetCells )
	{
		std::pop_heap( open.begin(), open.end() );
		RMGBasinCell cheapest = open.back();
		open.pop_back();

		Int index = cheapest.m_index;
		if( m_inLake[index] )
			continue;

		Int x = index % m_width;
		Int y = index / m_width;
		const Real here = (Real)m_heights[index];
		if( here > maxH )
			continue;
		if( !painted->empty() && here + lakeEdgeRise( perm, x, y ) > maxH )
		{
			edgeTurned++;
			continue;
		}

		m_inLake[index] = 1;
		painted->push_back( index );

		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( !waterAllowedAt( nx, ny ) )
				continue;

			Int next = cellIndex( nx, ny );
			if( queued[next] || m_inLake[next] )
				continue;

			queued[next] = 1;
			const Real fromSeedX = (Real)( nx - seedX );
			const Real fromSeedY = (Real)( ny - seedY );
			const Real along = fromSeedX * lakeCos + fromSeedY * lakeSin;
			const Real across = fromSeedY * lakeCosAcross - fromSeedX * lakeSinAcross;
			RMGBasinCell reached;
			reached.m_cost = 16 * (Int)m_heights[next]
				+ (Int)( 16.0f * rounding * sqrtf( along * along + across * across ) );
			reached.m_index = next;
			open.push_back( reached );
			std::push_heap( open.begin(), open.end() );
		}
	}
}

/** One morphological step on a water mask: a cell is water after an erosion when every cell within
	the radius was, after a dilation when any was. A dilation never puts water where waterAllowedAt
	keeps it off. */
static void rmgMorphWater( const std::vector<char>& in, std::vector<char>& out, Int width, Int height,
													 Int radius, Bool dilate, const std::vector<char>& allowed )
{
	out.assign( width * height, 0 );
	for( Int y = 0; y < height; y++ )
	{
		for( Int x = 0; x < width; x++ )
		{
			Int index = y * width + x;
			if( dilate ? !allowed[index] : !in[index] )
				continue;
			// a dilation looks for one water cell, an erosion for one dry one
			Bool found = FALSE;
			for( Int dy = -radius; dy <= radius && !found; dy++ )
			{
				for( Int dx = -radius; dx <= radius && !found; dx++ )
				{
					if( dx * dx + dy * dy > radius * radius )
						continue;
					Int nx = x + dx;
					Int ny = y + dy;
					Bool water = nx >= 0 && ny >= 0 && nx < width && ny < height && in[ny * width + nx] != 0;
					found = dilate ? water : !water;
				}
			}
			out[index] = ( dilate ? found : !found ) ? 1 : 0;
		}
	}
}

/** Opens the water mask, which takes off every arm and sliver thinner than the opening's disc, then
	closes it, which fills every bay and spit thinner than the closing's. */
void RMGLayout::openWater( Int openRadius, Int closeRadius )
{
	std::vector<char> allowed( m_width * m_height, 0 );
	for( Int y = 0; y < m_height; y++ )
		for( Int x = 0; x < m_width; x++ )
			allowed[cellIndex( x, y )] = waterAllowedAt( x, y ) ? 1 : 0;

	std::vector<char> scratch;
	if( openRadius > 0 )
	{
		rmgMorphWater( m_inLake, scratch, m_width, m_height, openRadius, FALSE, allowed );
		rmgMorphWater( scratch, m_inLake, m_width, m_height, openRadius, TRUE, allowed );
	}
	if( closeRadius > 0 )
	{
		rmgMorphWater( m_inLake, scratch, m_width, m_height, closeRadius, TRUE, allowed );
		rmgMorphWater( scratch, m_inLake, m_width, m_height, closeRadius, FALSE, allowed );
	}
}

/** Each lake cut back to the cells within RMG_LAKE_REACH of the radius its own area gives, measured
	from its middle. An oval keeps its ends; the hook a basin grew up a side valley does not. */
void RMGLayout::trimLakeReach( void )
{
	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };
	std::vector<char> seen( m_width * m_height, 0 );
	for( Int start = 0; start < m_width * m_height; start++ )
	{
		if( !m_inLake[start] || seen[start] )
			continue;
		std::vector<Int> cells;
		cells.push_back( start );
		seen[start] = 1;
		Real sumX = 0.0f;
		Real sumY = 0.0f;
		for( UnsignedInt head = 0; head < cells.size(); head++ )
		{
			Int x = cells[head] % m_width;
			Int y = cells[head] / m_width;
			sumX += (Real)x;
			sumY += (Real)y;
			for( Int i = 0; i < 4; i++ )
			{
				Int nx = x + offsetX[i];
				Int ny = y + offsetY[i];
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
					continue;
				Int next = cellIndex( nx, ny );
				if( seen[next] || !m_inLake[next] )
					continue;
				seen[next] = 1;
				cells.push_back( next );
			}
		}
		Real count = (Real)cells.size();
		Real middleX = sumX / count;
		Real middleY = sumY / count;

		/* Measured against the lake's own spread rather than a circle, so an oval keeps its length:
			a cell of a solid ellipse lies at most two of its spreads out along any line through the
			middle, a disc's rim at its radius, so RMG_LAKE_REACH of that is the same cut a round lake
			got. */
		Real spreadXX = 0.0f, spreadYY = 0.0f, spreadXY = 0.0f;
		for( UnsignedInt c = 0; c < cells.size(); c++ )
		{
			Real dx = (Real)( cells[c] % m_width ) - middleX;
			Real dy = (Real)( cells[c] / m_width ) - middleY;
			spreadXX += dx * dx;
			spreadYY += dy * dy;
			spreadXY += dx * dy;
		}
		spreadXX = spreadXX / count + 1.0f / 12.0f;	// a cell's own spread, so one row of cells is not flat
		spreadYY = spreadYY / count + 1.0f / 12.0f;
		spreadXY /= count;
		Real determinant = spreadXX * spreadYY - spreadXY * spreadXY;
		Real reach = 2.0f * RMG_LAKE_REACH;
		for( UnsignedInt c = 0; c < cells.size(); c++ )
		{
			Real dx = (Real)( cells[c] % m_width ) - middleX;
			Real dy = (Real)( cells[c] / m_width ) - middleY;
			Real out = ( spreadYY * dx * dx - 2.0f * spreadXY * dx * dy + spreadXX * dy * dy ) / determinant;
			if( out > reach * reach )
				m_inLake[cells[c]] = 0;
		}
	}
}

/** 4 pi area over the length of its cell edges squared for one lake's cells still in m_inLake: a
	digital disc scores about 0.6, a 3 to 1 oval 0.44, a lake drawn as two blobs on a neck about 0.35. */
Real RMGLayout::lakeRoundness( const std::vector<Int>& cells ) const
{
	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };
	Int area = 0, edges = 0;
	for( UnsignedInt c = 0; c < cells.size(); c++ )
	{
		if( !m_inLake[cells[c]] )
			continue;
		area++;
		Int x = cells[c] % m_width;
		Int y = cells[c] / m_width;
		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height || !m_inLake[cellIndex( nx, ny )] )
				edges++;
		}
	}
	if( edges == 0 )
		return 1.0f;
	return 4.0f * PI * (Real)area / ( (Real)edges * (Real)edges );
}

/** One lake's cells still in m_inLake over the area of the ellipse with their spread: an oval scores
	about 1 however ragged its shore, two blobs on a neck about 0.68, a crescent 0.6. */
Real RMGLayout::lakeEllipseFill( const std::vector<Int>& cells ) const
{
	Real area = 0.0f, sumX = 0.0f, sumY = 0.0f;
	for( UnsignedInt c = 0; c < cells.size(); c++ )
	{
		if( !m_inLake[cells[c]] )
			continue;
		area += 1.0f;
		sumX += (Real)( cells[c] % m_width );
		sumY += (Real)( cells[c] / m_width );
	}
	if( area < 1.0f )
		return 1.0f;
	const Real meanX = sumX / area, meanY = sumY / area;
	Real xx = 0.0f, yy = 0.0f, xy = 0.0f;
	for( UnsignedInt c = 0; c < cells.size(); c++ )
	{
		if( !m_inLake[cells[c]] )
			continue;
		const Real dx = (Real)( cells[c] % m_width ) - meanX;
		const Real dy = (Real)( cells[c] / m_width ) - meanY;
		xx += dx * dx;
		yy += dy * dy;
		xy += dx * dy;
	}
	xx /= area;
	yy /= area;
	xy /= area;
	const Real spread = xx * yy - xy * xy;
	if( spread <= 0.0f )
		return 1.0f;
	return area / ( 4.0f * PI * sqrtf( spread ) );
}

/** One lake's cells opened by a disc: what a disc wholly inside the lake can reach stays water. A
	lake no disc fits in is left as it was; the caller decides about it. */
void RMGLayout::openLakeCells( const std::vector<Int>& cells, Int radius )
{
	std::vector<Int> core;
	for( UnsignedInt c = 0; c < cells.size(); c++ )
	{
		Int x = cells[c] % m_width;
		Int y = cells[c] / m_width;
		Bool whole = m_inLake[cells[c]] != 0;
		for( Int dy = -radius; dy <= radius && whole; dy++ )
		{
			for( Int dx = -radius; dx <= radius; dx++ )
			{
				if( dx * dx + dy * dy > radius * radius )
					continue;
				Int nx = x + dx;
				Int ny = y + dy;
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height || !m_inLake[cellIndex( nx, ny )] )
				{
					whole = FALSE;
					break;
				}
			}
		}
		if( whole )
			core.push_back( cells[c] );
	}
	if( core.empty() )
		return;
	std::vector<char> kept( cells.size(), 0 );
	for( UnsignedInt c = 0; c < cells.size(); c++ )
	{
		Int x = cells[c] % m_width;
		Int y = cells[c] / m_width;
		for( UnsignedInt k = 0; k < core.size() && !kept[c]; k++ )
		{
			Int dx = core[k] % m_width - x;
			Int dy = core[k] / m_width - y;
			kept[c] = dx * dx + dy * dy <= radius * radius;
		}
	}
	for( UnsignedInt c = 0; c < cells.size(); c++ )
		if( !kept[c] )
			m_inLake[cells[c]] = 0;
}

/** Each lake opened again with a disc in proportion to its own size. One disc for every lake took
	the slivers off a pond and left a big lake its arms, since an arm of a big lake is wider than any
	disc a pond survives. An opening only takes water away, so each lake is opened on its own cells and
	a lake the disc would erase whole is left as it was. */
void RMGLayout::openEachLake( void )
{
	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };
	std::vector<char> seen( m_width * m_height, 0 );
	std::vector<char> mine( m_width * m_height, 0 );
	std::vector<char> core( m_width * m_height, 0 );
	std::vector<char> grown( m_width * m_height, 0 );
	for( Int start = 0; start < m_width * m_height; start++ )
	{
		if( !m_inLake[start] || seen[start] )
			continue;
		std::vector<Int> cells;
		cells.push_back( start );
		seen[start] = 1;
		for( UnsignedInt head = 0; head < cells.size(); head++ )
		{
			Int x = cells[head] % m_width;
			Int y = cells[head] / m_width;
			for( Int i = 0; i < 4; i++ )
			{
				Int nx = x + offsetX[i];
				Int ny = y + offsetY[i];
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
					continue;
				Int next = cellIndex( nx, ny );
				if( seen[next] || !m_inLake[next] )
					continue;
				seen[next] = 1;
				cells.push_back( next );
			}
		}

		const Real lakeRadius = sqrtf( (Real)cells.size() / PI );
		Int radius = (Int)( RMG_LAKE_OPEN_SHARE * lakeRadius );
		if( radius > RMG_LAKE_OPEN_MOST )
			radius = RMG_LAKE_OPEN_MOST;

		for( UnsignedInt c = 0; c < cells.size(); c++ )
			mine[cells[c]] = 1;

		if( radius <= RMG_LAKE_OPEN )
			radius = 0;

		// the core: cells whose whole disc is this lake's
		Bool anyCore = FALSE;
		for( UnsignedInt c = 0; c < cells.size() && radius > 0; c++ )
		{
			Int x = cells[c] % m_width;
			Int y = cells[c] / m_width;
			Bool whole = TRUE;
			for( Int dy = -radius; dy <= radius && whole; dy++ )
			{
				for( Int dx = -radius; dx <= radius; dx++ )
				{
					if( dx * dx + dy * dy > radius * radius )
						continue;
					Int nx = x + dx;
					Int ny = y + dy;
					if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height || !mine[cellIndex( nx, ny )] )
					{
						whole = FALSE;
						break;
					}
				}
			}
			core[cells[c]] = whole ? 1 : 0;
			anyCore = anyCore || whole;
		}

		// kept: the cells within the disc of the core
		if( anyCore )
		{
			for( UnsignedInt c = 0; c < cells.size(); c++ )
			{
				Int x = cells[c] % m_width;
				Int y = cells[c] / m_width;
				Bool kept = FALSE;
				for( Int dy = -radius; dy <= radius && !kept; dy++ )
				{
					for( Int dx = -radius; dx <= radius; dx++ )
					{
						Int nx = x + dx;
						Int ny = y + dy;
						if( dx * dx + dy * dy > radius * radius || nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
							continue;
						if( core[cellIndex( nx, ny )] )
						{
							kept = TRUE;
							break;
						}
					}
				}
				if( !kept )
					m_inLake[cells[c]] = 0;
			}
		}

		/* Then closed, by a disc a quarter of its radius: the bite a basin's edge left in one side, the
			notch between two bulges, fills in as water. The closing grows only onto ground water may
			cover and not into another lake, and keeps what an erosion of the grown lake leaves. */
		Int close = (Int)( RMG_LAKE_CLOSE_SHARE * lakeRadius );
		if( close > RMG_LAKE_CLOSE_MOST )
			close = RMG_LAKE_CLOSE_MOST;
		std::vector<Int> grownCells;
		for( UnsignedInt c = 0; c < cells.size(); c++ )
			mine[cells[c]] = m_inLake[cells[c]];
		for( UnsignedInt c = 0; c < cells.size() && close >= RMG_LAKE_CLOSE; c++ )
		{
			if( !mine[cells[c]] )
				continue;
			Int x = cells[c] % m_width;
			Int y = cells[c] / m_width;
			for( Int dy = -close; dy <= close; dy++ )
			{
				for( Int dx = -close; dx <= close; dx++ )
				{
					Int nx = x + dx;
					Int ny = y + dy;
					if( dx * dx + dy * dy > close * close || nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
						continue;
					Int next = cellIndex( nx, ny );
					if( mine[next] || grown[next] || m_inLake[next] || !waterAllowedAt( nx, ny ) )
						continue;
					grown[next] = 1;
					grownCells.push_back( next );
				}
			}
		}
		std::vector<Int> filled;
		for( UnsignedInt g = 0; g < grownCells.size(); g++ )
		{
			Int x = grownCells[g] % m_width;
			Int y = grownCells[g] / m_width;
			Bool inside = TRUE;
			for( Int dy = -close; dy <= close && inside; dy++ )
			{
				for( Int dx = -close; dx <= close; dx++ )
				{
					Int nx = x + dx;
					Int ny = y + dy;
					if( dx * dx + dy * dy > close * close )
						continue;
					if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height
						|| !( mine[cellIndex( nx, ny )] || grown[cellIndex( nx, ny )] ) )
					{
						inside = FALSE;
						break;
					}
				}
			}
			if( inside )
				filled.push_back( grownCells[g] );
		}
		for( UnsignedInt g = 0; g < grownCells.size(); g++ )
			grown[grownCells[g]] = 0;
		for( UnsignedInt f = 0; f < filled.size(); f++ )
		{
			m_inLake[filled[f]] = 1;
			seen[filled[f]] = 1;
		}

		/* A plain map's lake can still come out as two blobs on a neck, a crescent or a body with an
			arm, where its own disc was too small to open the arm off. Such a lake is opened again with
			a disc of RMG_LAKE_REOPEN_SHARE of the radius of a disc its size, and one even that
			leaves under RMG_LAKE_DRY_BELOW goes dry rather than draw as a dumbbell. A lake the disc
			would take more than a quarter of is long rather than knotted, a lake down a valley, and
			stays as it was. */
		if( !m_style )
		{
			std::vector<Int> water;
			for( UnsignedInt c = 0; c < cells.size(); c++ )
				if( m_inLake[cells[c]] )
					water.push_back( cells[c] );
			for( UnsignedInt f = 0; f < filled.size(); f++ )
				if( m_inLake[filled[f]] )
					water.push_back( filled[f] );
			if( lakeEllipseFill( water ) < RMG_LAKE_FILL_LEAST )
			{
				Int reopen = (Int)( RMG_LAKE_REOPEN_SHARE * sqrtf( (Real)water.size() / PI ) );
				if( reopen > 2 * RMG_LAKE_OPEN_MOST )
					reopen = 2 * RMG_LAKE_OPEN_MOST;
				openLakeCells( water, reopen );
				Int kept = 0;
				for( UnsignedInt c = 0; c < water.size(); c++ )
					kept += m_inLake[water[c]] ? 1 : 0;
				if( (Real)kept < RMG_LAKE_REOPEN_KEEP * (Real)water.size() )
				{
					for( UnsignedInt c = 0; c < water.size(); c++ )
						m_inLake[water[c]] = 1;
				}
				else if( lakeRoundness( water ) < RMG_LAKE_DRY_BELOW )
				{
					for( UnsignedInt c = 0; c < water.size(); c++ )
						m_inLake[water[c]] = 0;
				}
			}
		}

		for( UnsignedInt c = 0; c < cells.size(); c++ )
		{
			mine[cells[c]] = 0;
			core[cells[c]] = 0;
		}
	}
}

void RMGLayout::paintRiver( const UnsignedByte perm[512] )
{
	Int playable = m_settings.m_playableCells;
	Real riverHalf = 3.0f + (Real)playable * 0.008f;

	std::vector<Int> distWater( m_width * m_height, -1 );
	std::vector<Int> queue;
	static const Int offsetX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const Int offsetY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			if( !m_inLake[cellIndex( x, y )] )
				continue;
			distWater[cellIndex( x, y )] = 0;
			queue.push_back( cellIndex( x, y ) );
		}
	}

	UnsignedInt head = 0;
	while( head < queue.size() )
	{
		Int index = queue[head++];
		Int x = index % m_width;
		Int y = index / m_width;
		Int here = distWater[index];
		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
				continue;
			Int next = cellIndex( nx, ny );
			if( distWater[next] >= 0 )
				continue;
			distWater[next] = here + 1;
			queue.push_back( next );
		}
	}

	Int bestX = -1;
	Int bestY = -1;
	Real bestScore = -1.0e9f;

	for( Int y = RMG_BORDER_CELLS; y < m_height - RMG_BORDER_CELLS; y += 3 )
	{
		for( Int x = RMG_BORDER_CELLS; x < m_width - RMG_BORDER_CELLS; x += 3 )
		{
			if( !waterAllowedAt( x, y ) || m_inLake[cellIndex( x, y )] )
				continue;

			Real here = (Real)m_heights[cellIndex( x, y )];
			Real neighbour = 0.0f;
			Int samples = 0;
			for( Int i = 0; i < 8; i++ )
			{
				Int nx = x + offsetX[i];
				Int ny = y + offsetY[i];
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
					continue;
				neighbour += (Real)m_heights[cellIndex( nx, ny )];
				samples++;
			}
			if( samples == 0 )
				continue;

			Real trough = neighbour / (Real)samples - here;
			if( trough < 1.2f )
				continue;

			Int away = distWater[cellIndex( x, y )];
			if( away < 12 )
				continue;

			Real score = (Real)away + trough * 3.0f + here * 0.08f;
			if( score > bestScore )
			{
				bestScore = score;
				bestX = x;
				bestY = y;
			}
		}
	}

	if( bestX < 0 )
		return;

	std::vector<Int> path;
	std::vector<char> seen( m_width * m_height, 0 );
	Int x = bestX;
	Int y = bestY;
	Bool reachedLake = FALSE;
	Int maxSteps = playable;

	for( Int step = 0; step < maxSteps; step++ )
	{
		Int index = cellIndex( x, y );
		if( seen[index] )
			break;
		seen[index] = 1;
		path.push_back( index );
		if( m_inLake[index] )
		{
			reachedLake = TRUE;
			break;
		}

		Int nextX = -1;
		Int nextY = -1;
		Real best = 1.0e9f;
		for( Int i = 0; i < 8; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 1 || ny < 1 || nx >= m_width - 1 || ny >= m_height - 1 )
				continue;
			Int next = cellIndex( nx, ny );
			if( seen[next] )
				continue;
			if( !waterAllowedAt( nx, ny ) && !m_inLake[next] )
				continue;

			Real score = (Real)m_heights[next];
			score += 2.4f * fractalNoise( perm, (Real)nx * 0.07f + 4.0f, (Real)ny * 0.07f - 2.0f, 2 );
			if( score < best )
			{
				best = score;
				nextX = nx;
				nextY = ny;
			}
		}

		if( nextX < 0 )
			break;
		x = nextX;
		y = nextY;
	}

	Int minLength = reachedLake ? RMG_RIVER_MIN_LENGTH / 2 : RMG_RIVER_MIN_LENGTH;
	if( (Int)path.size() < minLength )
		return;

	Int half = (Int)( riverHalf + 0.5f );
	Int radiusSq = (Int)( riverHalf * riverHalf + 0.5f );
	for( UnsignedInt p = 0; p < path.size(); p++ )
	{
		Int cx = path[p] % m_width;
		Int cy = path[p] / m_width;
		for( Int dy = -half; dy <= half; dy++ )
		{
			for( Int dx = -half; dx <= half; dx++ )
			{
				if( dx * dx + dy * dy > radiusSq )
					continue;
				Int nx = cx + dx;
				Int ny = cy + dy;
				if( !waterAllowedAt( nx, ny ) )
					continue;
				m_inLake[cellIndex( nx, ny )] = 1;
			}
		}
	}
}

/// The mean half-width of a river map's river, cells.
static Real rmgRiverHalf( Int playableCells )
{
	return 4.0f + (Real)playableCells * 0.012f;
}

/** A river map's river: one band of water across the whole map, from map edge to map edge, at one
	of eight angles the seed picks. It swings off its line in a long bend and meanders on top of
	that, and it widens and narrows as it goes, so it reads as a river and not as a ruled canal; its
	ends run on into the border, so the edge of the map is no way across. It goes in unbroken here;
	the fords are cut once the starts are known (openFordsAtStarts). */
void RMGLayout::paintMainRiver( const UnsignedByte perm[512] )
{
	Int playable = m_settings.m_playableCells;
	Real centre = (Real)playable * 0.5f;
	m_riverAngle = (Real)(hashCell( m_settings.m_seed, 9001, 41 ) % 8U) * (PI * 0.125f);
	Real alongX = Cos( m_riverAngle );
	Real alongY = Sin( m_riverAngle );
	Real bend = (Real)playable * RMG_RIVER_BEND;
	Real meander = (Real)playable * RMG_RIVER_MEANDER;
	Real meanHalf = rmgRiverHalf( playable );

	m_riverLine.clear();
	m_riverHalf.clear();
	for( Int step = -playable - RMG_BORDER_CELLS; step <= playable + RMG_BORDER_CELLS; step++ )
	{
		Real t = (Real)step;
		Real longSwing = 2.0f * fractalNoise( perm, t * 0.006f + 7.5f, 3.25f, 2 );
		Real shortSwing = 2.0f * fractalNoise( perm, t * 0.02f - 3.1f, 9.7f, 2 );
		if( longSwing > 1.0f ) longSwing = 1.0f;
		if( longSwing < -1.0f ) longSwing = -1.0f;
		if( shortSwing > 1.0f ) shortSwing = 1.0f;
		if( shortSwing < -1.0f ) shortSwing = -1.0f;
		Real side = bend * longSwing + meander * shortSwing;

		RMGPoint p;
		p.m_cellX = centre + alongX * t - alongY * side;
		p.m_cellY = centre + alongY * t + alongX * side;
		Int mapX = (Int)floorf( p.m_cellX + 0.5f ) + RMG_BORDER_CELLS;
		Int mapY = (Int)floorf( p.m_cellY + 0.5f ) + RMG_BORDER_CELLS;
		if( mapX < 1 || mapY < 1 || mapX >= m_width - 1 || mapY >= m_height - 1 )
			continue;
		m_riverLine.push_back( p );

		Real width = rmgUnit( 0.5f + 1.6f * fractalNoise( perm, t * 0.015f + 2.2f, -5.1f, 2 ) );
		m_riverHalf.push_back( meanHalf * lerpReal( RMG_RIVER_NARROW, RMG_RIVER_WIDE, width ) );
	}

	std::vector<Int> noFords;
	paintRiverStretches( noFords );
}

/** Paint the river along its line, leaving dry the steps either side of each ford's index. The river
	narrows into a ford over RMG_FORD_TAPER_STEPS, so the banks turn in to meet it the way a river
	shallows over a bar, rather than the water stopping in a round end. The basin carve turns each gap
	into a low crossing between two beaches, and every stretch between two fords is a water area of
	its own. */
void RMGLayout::paintRiverStretches( const std::vector<Int>& fords )
{
	const std::vector<RMGPoint>& line = m_riverLine;
	Int n = (Int)line.size();

	std::vector<char> dry( n, 0 );
	std::vector<Real> narrowing( n, 1.0f );
	for( Int i = 0; i < n; i++ )
	{
		for( UnsignedInt ford = 0; ford < fords.size(); ford++ )
		{
			Real dryHalf = m_riverHalf[fords[ford]] + RMG_FORD_DRY_STEPS;
			Real off = fabsf( (Real)( i - fords[ford] ) );
			if( off < dryHalf )
				dry[i] = 1;
			else if( off < dryHalf + RMG_FORD_TAPER_STEPS )
			{
				Real keep = RMG_FORD_TIP + ( 1.0f - RMG_FORD_TIP ) * fadeCurve( ( off - dryHalf ) / RMG_FORD_TAPER_STEPS );
				if( keep < narrowing[i] )
					narrowing[i] = keep;
			}
		}
	}

	/* A stretch cut off between two fords that is too short to read as river is a pond, and serves
		nothing. It goes dry with the fords beside it. The length scales with the ford, so a small map's
		real stretches stay, and the longest stretch always stays, so a river map keeps its river. A
		stretch from a ford to the end of the line stays however short: it runs on off the map, and
		drying it stopped the river short of the edge. */
	if( !fords.empty() )
	{
		const Int minRun = (Int)( rmgRiverHalf( m_settings.m_playableCells ) + RMG_FORD_DRY_STEPS + RMG_FORD_TAPER_STEPS
			+ RMG_FORD_STUB_STEPS );
		Int longestFrom = -1, longest = 0;
		for( Int i = 0; i < n; )
		{
			Int end = i;
			while( end < n && dry[end] == dry[i] )
				end++;
			if( !dry[i] && end - i > longest )
			{
				longest = end - i;
				longestFrom = i;
			}
			i = end;
		}
		for( Int i = 0; i < n; )
		{
			Int end = i;
			while( end < n && dry[end] == dry[i] )
				end++;
			if( !dry[i] && i != longestFrom && end - i < minRun && i > 0 && end < n )
			{
				for( Int k = i; k < end; k++ )
					dry[k] = 1;
			}
			i = end;
		}
	}

	for( Int i = 0; i < n; i++ )
	{
		if( dry[i] )
			continue;

		Real half = m_riverHalf[i] * narrowing[i];
		Int reach = (Int)( half + 0.5f );
		Real radiusSq = half * half + 0.25f;
		Int cx = (Int)floorf( line[i].m_cellX + 0.5f ) + RMG_BORDER_CELLS;
		Int cy = (Int)floorf( line[i].m_cellY + 0.5f ) + RMG_BORDER_CELLS;
		for( Int dy = -reach; dy <= reach; dy++ )
		{
			for( Int dx = -reach; dx <= reach; dx++ )
			{
				if( (Real)( dx * dx + dy * dy ) > radiusSq )
					continue;
				Int x = cx + dx;
				Int y = cy + dy;
				// to the map's edge, a cell short of it so the outline still closes inside the map
				if( x < 1 || y < 1 || x >= m_width - 1 || y >= m_height - 1 )
					continue;
				m_inLake[cellIndex( x, y )] = 1;
			}
		}
	}
}

/** The fords go where the starts are: one at the point of the river nearest each start, two
	that land close together made one. A seat on the ring and its mirror on the far bank share a
	ford, so nobody's way across is further than anybody else's. The river is repainted with the
	gaps and the basin carved again from the ground as it stood before the first carve. */
void RMGLayout::openFordsAtStarts( const std::vector<UnsignedByte>& uncarved )
{
	if( !m_type->m_river || m_riverLine.empty() )
		return;

	std::vector<Int> fords;
	fordsForStarts( fords );
	m_inLake.assign( m_width * m_height, 0 );
	paintRiverStretches( fords );
	extractLakePolygons();
	buildLakeMask();
	m_heights = uncarved;
	carveLakeBasins();
}

/** The river steps the fords go at for the starts as they stand, in order along the river. */
void RMGLayout::fordsForStarts( std::vector<Int>& fords ) const
{
	fords.clear();

	/* Two fords closer than twice this along the river are made one. The water between them would be
		too short a stretch to keep (paintRiverStretches), and drying it left a land bridge three fords
		wide; or it narrowed into both fords and lay between them as a round pond. */
	const Real riverHalf = rmgRiverHalf( m_settings.m_playableCells );
	Real fordHalf = riverHalf + RMG_FORD_DRY_STEPS + RMG_FORD_TAPER_STEPS + 10.0f;
	std::vector<Int> nearest;
	for( UnsignedInt s = 0; s < m_starts.size(); s++ )
	{
		Int best = 0;
		Real bestDistance = 1.0e9f;
		for( UnsignedInt i = 0; i < m_riverLine.size(); i++ )
		{
			Real dx = m_riverLine[i].m_cellX - m_starts[s].m_cellX;
			Real dy = m_riverLine[i].m_cellY - m_starts[s].m_cellY;
			Real distance = dx * dx + dy * dy;
			if( distance < bestDistance )
			{
				bestDistance = distance;
				best = (Int)i;
			}
		}
		nearest.push_back( best );
	}
	std::sort( nearest.begin(), nearest.end() );

	UnsignedInt first = 0;
	for( UnsignedInt i = 1; i <= nearest.size(); i++ )
	{
		if( i < nearest.size() && (Real)( nearest[i] - nearest[first] ) < fordHalf * 2.0f )
			continue;
		fords.push_back( ( nearest[first] + nearest[i - 1] ) / 2 );
		first = i;
	}

	/* The river runs off the map at both ends, so the fords are the only ways across. A duel's two
		seats share one, and a single crossing for a whole river is a gate, not a front: a second goes
		where the river is furthest along from the first, inside the middle half of the playable
		stretch, which is as far from one seat as from its mirror on the far bank. */
	if( fords.size() == 1 )
	{
		const Real playable = (Real)m_settings.m_playableCells;
		const Real alongX = Cos( m_riverAngle );
		const Real alongY = Sin( m_riverAngle );
		Int best = -1;
		Int bestOff = 0;
		for( Int i = 0; i < (Int)m_riverLine.size(); i++ )
		{
			Real along = ( m_riverLine[i].m_cellX - 0.5f * playable ) * alongX
				+ ( m_riverLine[i].m_cellY - 0.5f * playable ) * alongY;
			if( fabsf( along ) > 0.25f * playable )
				continue;
			Int off = i > fords[0] ? i - fords[0] : fords[0] - i;
			if( off > bestOff )
			{
				bestOff = off;
				best = i;
			}
		}
		if( best >= 0 && (Real)bestOff >= fordHalf * 2.0f )
		{
			fords.push_back( best );
			std::sort( fords.begin(), fords.end() );
		}
	}
}

/** A start layout's walks on a river map, with the river as the fords will leave it: closed but for
	the dry gap at each ford those starts would get. Pricing the whole river as a cliff let a seat
	count a crossing straight in front of it that the fords never cut. */
void RMGLayout::priceRiverByFords( std::vector<Int>& cost ) const
{
	std::vector<Int> fords;
	fordsForStarts( fords );
	const Int n = (Int)m_riverLine.size();
	for( UnsignedInt ford = 0; ford < fords.size(); ford++ )
	{
		Int dryHalf = (Int)( m_riverHalf[fords[ford]] + RMG_FORD_DRY_STEPS );
		for( Int i = fords[ford] - dryHalf; i <= fords[ford] + dryHalf; i++ )
		{
			if( i < 0 || i >= n )
				continue;
			Real half = m_riverHalf[i] + 1.0f;
			Int reach = (Int)( half + 0.5f );
			Int cx = (Int)floorf( m_riverLine[i].m_cellX + 0.5f ) + RMG_BORDER_CELLS;
			Int cy = (Int)floorf( m_riverLine[i].m_cellY + 0.5f ) + RMG_BORDER_CELLS;
			for( Int dy = -reach; dy <= reach; dy++ )
			{
				for( Int dx = -reach; dx <= reach; dx++ )
				{
					Int x = cx + dx;
					Int y = cy + dy;
					if( (Real)( dx * dx + dy * dy ) > half * half || x < 0 || y < 0 || x >= m_width - 1 || y >= m_height - 1 )
						continue;
					if( insidePlayable( x, y ) && underwaterAtCell( x, y ) )
						cost[cellIndex( x, y )] = 1;
				}
			}
		}
	}
}

/** A corner of a water outline in playable cells, packed. It is packed counted from the map's own
	corner, since a river runs on into the border where a playable coordinate is below nought. */
static Int rmgVertexKey( Int x, Int y )
{
	return ( ( y + RMG_BORDER_CELLS ) << 16 ) | ( ( x + RMG_BORDER_CELLS ) & 0xFFFF );
}

static Int rmgVertexX( Int key )
{
	return ( key & 0xFFFF ) - RMG_BORDER_CELLS;
}

static Int rmgVertexY( Int key )
{
	return ( key >> 16 ) - RMG_BORDER_CELLS;
}

static void rmgResamplePolygon( std::vector<RMGPoint>& loop, Int maxPoints )
{
	Int n = (Int)loop.size();
	if( n <= maxPoints || n < 3 )
		return;

	std::vector<Real> cum( n + 1, 0.0f );
	for( Int i = 0; i < n; i++ )
	{
		Int j = ( i + 1 ) % n;
		Real dx = loop[j].m_cellX - loop[i].m_cellX;
		Real dy = loop[j].m_cellY - loop[i].m_cellY;
		cum[i + 1] = cum[i] + sqrtf( dx * dx + dy * dy );
	}

	Real total = cum[n];
	if( total < 1.0f )
		return;

	std::vector<RMGPoint> out;
	out.reserve( maxPoints );
	for( Int p = 0; p < maxPoints; p++ )
	{
		Real t = total * (Real)p / (Real)maxPoints;
		Int i = 0;
		while( i < n - 1 && cum[i + 1] < t )
			i++;

		Real span = cum[i + 1] - cum[i];
		Real u = ( span > 0.0f ) ? ( t - cum[i] ) / span : 0.0f;
		Int j = ( i + 1 ) % n;

		RMGPoint q;
		q.m_cellX = lerpReal( loop[i].m_cellX, loop[j].m_cellX, u );
		q.m_cellY = lerpReal( loop[i].m_cellY, loop[j].m_cellY, u );
		out.push_back( q );
	}

	loop.swap( out );
}

static Bool rmgPointInPolygon( Real x, Real y, const std::vector<RMGPoint>& polygon )
{
	Bool inside = FALSE;
	Int n = (Int)polygon.size();
	Int j = n - 1;

	for( Int i = 0; i < n; j = i++ )
	{
		Real yi = polygon[i].m_cellY;
		Real yj = polygon[j].m_cellY;
		if( ( yi > y ) == ( yj > y ) )
			continue;

		Real xi = polygon[i].m_cellX;
		Real xj = polygon[j].m_cellX;
		Real span = yj - yi;
		if( span < 0.0f )
			span = -span;
		if( span < 0.0001f )
			continue;

		if( x < ( xj - xi ) * ( y - yi ) / ( yj - yi ) + xi )
			inside = !inside;
	}

	return inside;
}

static void rmgInflatePolygon( std::vector<RMGPoint>& loop, Real amount )
{
	Int n = (Int)loop.size();
	if( n < 3 )
		return;

	std::vector<RMGPoint> out;
	out.resize( n );

	for( Int i = 0; i < n; i++ )
	{
		const RMGPoint& prev = loop[( i + n - 1 ) % n];
		const RMGPoint& curr = loop[i];
		const RMGPoint& next = loop[( i + 1 ) % n];

		Real dx1 = curr.m_cellX - prev.m_cellX;
		Real dy1 = curr.m_cellY - prev.m_cellY;
		Real dx2 = next.m_cellX - curr.m_cellX;
		Real dy2 = next.m_cellY - curr.m_cellY;
		Real len1 = sqrtf( dx1 * dx1 + dy1 * dy1 );
		Real len2 = sqrtf( dx2 * dx2 + dy2 * dy2 );
		if( len1 < 0.01f ) len1 = 0.01f;
		if( len2 < 0.01f ) len2 = 0.01f;

		// Contour is counter-clockwise with water on the left, so the outward
		// normal is to the right of each edge.
		Real nx = dy1 / len1 + dy2 / len2;
		Real ny = -( dx1 / len1 + dx2 / len2 );
		Real nlen = sqrtf( nx * nx + ny * ny );
		if( nlen < 0.01f )
		{
			out[i] = curr;
			continue;
		}

		out[i].m_cellX = curr.m_cellX + amount * nx / nlen;
		out[i].m_cellY = curr.m_cellY + amount * ny / nlen;
	}

	loop.swap( out );
}

void RMGLayout::extractLakePolygons( void )
{
	m_lakes.clear();
	m_visited.assign( m_width * m_height, 0 );

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Int start = cellIndex( x, y );
			if( !m_inLake[start] || m_visited[start] )
				continue;

			std::vector<Int> cells;
			std::vector<Int> fill;
			fill.push_back( start );
			m_visited[start] = 1;

			UnsignedInt head = 0;
			while( head < fill.size() )
			{
				Int index = fill[head++];
				cells.push_back( index );
				Int cx = index % m_width;
				Int cy = index / m_width;
				for( Int i = 0; i < 4; i++ )
				{
					Int nx = cx + offsetX[i];
					Int ny = cy + offsetY[i];
					if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
						continue;
					Int next = cellIndex( nx, ny );
					if( m_visited[next] || !m_inLake[next] )
						continue;
					m_visited[next] = 1;
					fill.push_back( next );
				}
			}

			if( (Int)cells.size() < RMG_LAKE_MIN_FILL )
			{
				for( UnsignedInt c = 0; c < cells.size(); c++ )
					m_inLake[cells[c]] = 0;
				continue;
			}

			std::map<Int, Int> nextVertex;
			Real sumX = 0.0f;
			Real sumY = 0.0f;
			for( UnsignedInt c = 0; c < cells.size(); c++ )
			{
				Int cx = cells[c] % m_width;
				Int cy = cells[c] / m_width;
				Int px = cx - RMG_BORDER_CELLS;
				Int py = cy - RMG_BORDER_CELLS;
				sumX += (Real)px + 0.5f;
				sumY += (Real)py + 0.5f;

				// Water on the left, so the outer ring walks counter-clockwise.
				if( cy == 0 || !m_inLake[cellIndex( cx, cy - 1 )] )
					nextVertex[rmgVertexKey( px, py )] = rmgVertexKey( px + 1, py );
				if( cx == m_width - 1 || !m_inLake[cellIndex( cx + 1, cy )] )
					nextVertex[rmgVertexKey( px + 1, py )] = rmgVertexKey( px + 1, py + 1 );
				if( cy == m_height - 1 || !m_inLake[cellIndex( cx, cy + 1 )] )
					nextVertex[rmgVertexKey( px + 1, py + 1 )] = rmgVertexKey( px, py + 1 );
				if( cx == 0 || !m_inLake[cellIndex( cx - 1, cy )] )
					nextVertex[rmgVertexKey( px, py + 1 )] = rmgVertexKey( px, py );
			}

			if( nextVertex.size() < 3 )
			{
				for( UnsignedInt c = 0; c < cells.size(); c++ )
					m_inLake[cells[c]] = 0;
				continue;
			}

			Int startKey = nextVertex.begin()->first;
			for( std::map<Int, Int>::const_iterator it = nextVertex.begin();
					 it != nextVertex.end(); ++it )
			{
				Int vx = rmgVertexX( it->first );
				Int vy = rmgVertexY( it->first );
				Int sx = rmgVertexX( startKey );
				Int sy = rmgVertexY( startKey );
				if( vx < sx || ( vx == sx && vy < sy ) )
					startKey = it->first;
			}

			std::vector<RMGPoint> loop;
			Int v = startKey;
			Bool closed = FALSE;
			for( Int guard = 0; guard < (Int)nextVertex.size() + 2; guard++ )
			{
				RMGPoint p;
				p.m_cellX = (Real)rmgVertexX( v );
				p.m_cellY = (Real)rmgVertexY( v );
				loop.push_back( p );

				std::map<Int, Int>::const_iterator found = nextVertex.find( v );
				if( found == nextVertex.end() )
					break;
				v = found->second;
				if( v == startKey )
				{
					closed = TRUE;
					break;
				}
			}

			if( !closed || (Int)loop.size() < 3 )
			{
				for( UnsignedInt c = 0; c < cells.size(); c++ )
					m_inLake[cells[c]] = 0;
				continue;
			}

			if( (Int)loop.size() >= 4 )
			{
				std::vector<RMGPoint> simple;
				Int n = (Int)loop.size();
				for( Int i = 0; i < n; i++ )
				{
					const RMGPoint& prev = loop[( i + n - 1 ) % n];
					const RMGPoint& curr = loop[i];
					const RMGPoint& next = loop[( i + 1 ) % n];
					Real dx1 = curr.m_cellX - prev.m_cellX;
					Real dy1 = curr.m_cellY - prev.m_cellY;
					Real dx2 = next.m_cellX - curr.m_cellX;
					Real dy2 = next.m_cellY - curr.m_cellY;
					if( dx1 * dy2 - dy1 * dx2 == 0.0f && dx1 * dx2 + dy1 * dy2 > 0.0f )
						continue;
					simple.push_back( curr );
				}
				if( (Int)simple.size() >= 3 )
					loop.swap( simple );
			}

			rmgResamplePolygon( loop, m_type->m_river ? RMG_RIVER_POLYGON_MAX : RMG_POLYGON_MAX );
			rmgInflatePolygon( loop, 1.0f );

			RMGLake lake;
			lake.m_cellX = sumX / (Real)cells.size();
			lake.m_cellY = sumY / (Real)cells.size();
			lake.m_polygon.swap( loop );
			m_lakes.push_back( lake );
		}
	}

	// The polygon is what the game floods. Rebuild the mask from it so a chord
	// that cuts a bay does not leave carved bed sitting outside the water area.
	m_inLake.assign( m_width * m_height, 0 );
	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real px = (Real)( x - RMG_BORDER_CELLS ) + 0.5f;
			Real py = (Real)( y - RMG_BORDER_CELLS ) + 0.5f;
			for( UnsignedInt i = 0; i < m_lakes.size(); i++ )
			{
				if( rmgPointInPolygon( px, py, m_lakes[i].m_polygon ) )
				{
					m_inLake[cellIndex( x, y )] = 1;
					break;
				}
			}
		}
	}
}

/** Lakes go where the map is already lowest. Each one floods its pocket lowest-cell-first so the
	water follows the valley the noise cut, and a stream is walked downhill from a high trough to
	join them. The outline is the contour of that mask, not a circle sampled around the seed. */
void RMGLayout::placeLakes( const UnsignedByte perm[512] )
{
	m_lakes.clear();
	m_inLake.assign( m_width * m_height, 0 );
	m_shoreDist.clear();
	m_riverLine.clear();
	m_riverAngle = 0.0f;

	if( m_settings.m_playableCells < RMG_LAKE_MIN_CELLS || m_style )
		return;

	Int playable = m_settings.m_playableCells;
	Int wanted = ( m_settings.m_numPlayers + 2 ) / 2 + m_type->m_extraLakes;
	if( m_type->m_river )
		wanted = 0;
	else if( wanted < 1 )
		wanted = 1;
	Int area = playable * playable;
	Int baseTarget = (Int)( (Real)area * RMG_LAKE_AREA * m_type->m_lakeArea );
	if( baseTarget < RMG_LAKE_MIN_FILL )
		baseTarget = RMG_LAKE_MIN_FILL;

	struct RMGSeed
	{
		Int m_x;
		Int m_y;
		Real m_height;

		Bool operator<( const RMGSeed& other ) const
		{
			if( m_height != other.m_height )
				return m_height < other.m_height;
			if( m_y != other.m_y )
				return m_y < other.m_y;
			return m_x < other.m_x;
		}
	};

	std::vector<RMGSeed> seeds;
	for( Int y = 0; y < m_height; y += 3 )
	{
		for( Int x = 0; x < m_width; x += 3 )
		{
			if( !waterAllowedAt( x, y ) )
				continue;

			Real total = 0.0f;
			Int samples = 0;
			for( Int dy = -2; dy <= 2; dy += 2 )
			{
				for( Int dx = -2; dx <= 2; dx += 2 )
				{
					Int nx = x + dx;
					Int ny = y + dy;
					if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
						continue;
					total += (Real)m_heights[cellIndex( nx, ny )];
					samples++;
				}
			}
			if( samples == 0 )
				continue;

			RMGSeed seed;
			seed.m_x = x;
			seed.m_y = y;
			seed.m_height = total / (Real)samples;
			seeds.push_back( seed );
		}
	}

	std::sort( seeds.begin(), seeds.end() );

	/* Lake country spreads its basins out. The lowest pockets tend to sit in one valley, and a
		basin grown beside another joins it, so without a gap the map gets one big lake where it
		asked for five. */
	Real lakeGap = ( m_type->m_extraLakes > 0 ) ? (Real)playable * 0.2f : 0.0f;
	std::vector<RMGPoint> lakeSeeds;

	Int grown = 0;
	for( UnsignedInt i = 0; i < seeds.size() && grown < wanted; i++ )
	{
		if( m_inLake[cellIndex( seeds[i].m_x, seeds[i].m_y )] )
			continue;

		Bool crowded = FALSE;
		for( UnsignedInt k = 0; k < lakeSeeds.size(); k++ )
		{
			Real dx = (Real)seeds[i].m_x - lakeSeeds[k].m_cellX;
			Real dy = (Real)seeds[i].m_y - lakeSeeds[k].m_cellY;
			if( dx * dx + dy * dy < lakeGap * lakeGap )
				crowded = TRUE;
		}
		if( crowded )
			continue;

		UnsignedInt hash = hashCell( m_settings.m_seed, seeds[i].m_x, seeds[i].m_y );
		Int target = (Int)( (Real)baseTarget * ( 0.75f + 0.5f * (Real)( hash % 1000U ) / 1000.0f ) );
		if( target < RMG_LAKE_MIN_FILL )
			target = RMG_LAKE_MIN_FILL;

		/* A plain map's lake starts its own radius in from the edge of the water margin. Grown from a low
			pocket against the edge, it spread along the margin and came out with one side ruled straight. */
		if( !m_skeleton )
		{
			Int keep = (Int)RMG_LAKE_SHORE + (Int)sqrtf( (Real)target / PI );
			Int px = seeds[i].m_x - RMG_BORDER_CELLS;
			Int py = seeds[i].m_y - RMG_BORDER_CELLS;
			if( px < keep || py < keep || px >= playable - keep || py >= playable - keep )
				continue;
		}

		std::vector<Int> painted;
		growBasin( perm, seeds[i].m_x, seeds[i].m_y, target, RMG_LAKE_FILL_RISE, &painted );
		if( (Int)painted.size() < RMG_LAKE_MIN_FILL )
		{
			for( UnsignedInt p = 0; p < painted.size(); p++ )
				m_inLake[painted[p]] = 0;
			continue;
		}

		RMGPoint grownAt;
		grownAt.m_cellX = (Real)seeds[i].m_x;
		grownAt.m_cellY = (Real)seeds[i].m_y;
		lakeSeeds.push_back( grownAt );
		grown++;
	}

	if( m_type->m_river )
	{
		paintMainRiver( perm );
		extractLakePolygons();
		return;
	}

	if( grown == 0 && !seeds.empty() )
	{
		std::vector<Int> painted;
		growBasin( perm, seeds[0].m_x, seeds[0].m_y, RMG_LAKE_MIN_FILL * 2, RMG_LAKE_FILL_RISE * 2.0f,
							 &painted );
		if( (Int)painted.size() < RMG_LAKE_MIN_FILL )
		{
			for( UnsignedInt p = 0; p < painted.size(); p++ )
				m_inLake[painted[p]] = 0;
		}
	}

	if( !m_skeleton )
	{
		openWater( RMG_LAKE_OPEN, RMG_LAKE_CLOSE );
		trimLakeReach();
		openWater( RMG_LAKE_OPEN, 0 );
		openEachLake();
	}
	/* A plain map paints no stream. Walked into a lake it came out as an arm off it, the finger and the
		hook the lakes are now kept from growing; walked anywhere else it ran down into the water margin
		and lay along the map's edge as a fat sausage of water with one side ruled straight. A template
		map keeps its stream for the template to replace. */
	if( m_skeleton )
		paintRiver( perm );
	if( !m_skeleton )
		openWater( RMG_STREAM_OPEN, 0 );
	extractLakePolygons();
}

/** Cut the basin so that the water is shallow at the edge and deep in the middle, with a beach
	rising out of it on the land side. The depth at the waterline is what the renderer's soft water
	edge is drawn from - it looks for cells whose corners straddle the water plane and fades the
	terrain into it over the first few feet of depth - so a basin dug to its full depth right up to
	the outline gets a hard blue line round it instead of a shore. */
void RMGLayout::carveLakeBasins( void )
{
	if( m_lakes.empty() )
		return;

	Real bed = RMG_BASE_HEIGHT - RMG_WATER_DROP - RMG_LAKE_DEPTH;
	Real atTheWaterline = m_waterHeight - 1.0f;

	std::vector<Real> straightShore;
	buildShoreDistance( straightShore, TRUE );

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real distanceToShore = straightShore[cellIndex( x, y )];
			if( distanceToShore >= RMG_LAKE_SHORE )
				continue;

			Real h;
			if( distanceToShore < 0.0f )
			{
				// Inside the water: just under the surface at the rim, on the bed by the middle.
				Real t = -distanceToShore / RMG_LAKE_SHORE;
				if( t > 1.0f )
					t = 1.0f;

				h = lerpReal( atTheWaterline, bed, fadeCurve( t ) );
			}
			else
			{
				// The beach: out of the water at the rim, back into whatever the land was doing.
				Real t = distanceToShore / RMG_LAKE_SHORE;
				h = lerpReal( atTheWaterline + 2.0f, (Real)m_heights[cellIndex( x, y )],
											fadeCurve( t ) );
			}

			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;

			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
		}
	}

	// And everything the water polygons do not cover comes up out of the water. The engine
	// floods those polygons, not the mask, so this is measured the same way the game is.
	Real bank = m_waterHeight + RMG_LAKE_BANK;

	for( Int landY = 0; landY < m_height; landY++ )
	{
		for( Int landX = 0; landX < m_width; landX++ )
		{
			Real px = (Real)(landX - RMG_BORDER_CELLS);
			Real py = (Real)(landY - RMG_BORDER_CELLS);
			Bool covered = FALSE;
			for( UnsignedInt i = 0; i < m_lakes.size(); i++ )
			{
				if( rmgPointInPolygon( px, py, m_lakes[i].m_polygon ) )
				{
					covered = TRUE;
					break;
				}
			}
			Real distanceToShore;
			insideLake( px, py, &distanceToShore );
			// Deep interior of a polygon stays as the basin. The rim, and anything
			// the polygon does not cover, comes up - the latter is the puddle the
			// test is there to catch, and the former is the one-cell sliver a
			// rounded world vertex can fall into.
			if( covered && distanceToShore < -1.0f )
				continue;

			Int index = cellIndex( landX, landY );
			if( (Real)m_heights[index] < bank )
				m_heights[index] = (UnsignedByte)(bank + 0.5f);
		}
	}
}

//-----------------------------------------------------------------------------
// Start positions
//-----------------------------------------------------------------------------

Real RMGLayout::distanceToNearestStart( Real cellX, Real cellY ) const
{
	Real nearest = 1.0e9f;

	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Real dx = cellX - m_starts[i].m_cellX;
		Real dy = cellY - m_starts[i].m_cellY;
		Real distance = sqrtf( dx * dx + dy * dy );
		if( distance < nearest )
			nearest = distance;
	}

	return nearest;
}

/** The starts are found, not placed: every spot the search looks at is scored
	for how flat and how dry it is, and a seat only ever lands on one of those.
	Only a river map is mirrored; everywhere else the seats stand on ground the
	same noise field made, and fairness is the walk between them, measured. */
void RMGLayout::chooseStarts( void )
{
	m_starts.clear();

	Int playable = m_settings.m_playableCells;

	/* The flat disc has to be inside the playable area, and a good deal more than that: the search
		below takes whichever site is furthest from the ones already picked, which on its own walks
		every base into a corner. A share of the map is kept clear of the edge as well, so there is
		ground behind a base to retreat into and to be attacked through. A map too small to give
		that up keeps the disc's own margin and nothing more. */
	Int margin = (Int)(RMG_FLAT_RADIUS + RMG_START_MARGIN);
	Int inset = (Int)((Real)playable * RMG_START_EDGE_FRACTION);
	if( inset > margin && playable - 2 * inset >= (Int)(4.0f * RMG_FLAT_RADIUS) )
		margin = inset;

	std::vector<RMGStartCandidate> candidates;

	/* The whole levelled flat has to be dry with a beach to spare, not just the inner disc: the
		shore distance is city-block, which reads a diagonal half again as far as it is, and a base
		levelled over the edge of a lake raised the lake bed under the water polygon into dry ground.
		Sampling the flat cell by cell makes the reach a true circle. A map whose lakes leave too few
		such sites to seat everyone with room to choose also takes the inner-disc sites, after the dry
		ones and marked rougher, so a seat still prefers a dry flat and only a lake-crowded map ends
		up on the looser kind. A handful of strict sites alone ran the ring search out of seats. */
	UnsignedInt strictNext = 0;		// both passes walk the same grid in the same order
	for( Int pass = 0; pass < 2
		&& ( pass == 0 || (Int)candidates.size() < m_settings.m_numPlayers * RMG_START_STRICT_SITES_EACH ); pass++ )
	for( Int y = margin; y < playable - margin; y += m_startSearchStride )
	{
		for( Int x = margin; x < playable - margin; x += m_startSearchStride )
		{
			Int mapX = x + RMG_BORDER_CELLS;
			Int mapY = y + RMG_BORDER_CELLS;

			/* A strict site passes the loose test too, and is in the list already. The strict ones
				lead the list in walk order, so one index steps through them; once it reaches the
				loose ones it points behind the walk and never matches again. */
			if( pass == 1 && strictNext < candidates.size() && candidates[strictNext].m_cellX == (Real)x
				&& candidates[strictNext].m_cellY == (Real)y )
			{
				strictNext++;
				continue;
			}

			// The massif is the ground everybody climbs to, not a base: its top is level enough to
			// pass as one, and a seat up there would own every ramp.
			if( !m_massifLobes.empty() && massifClearance( (Real)x, (Real)y ) < RMG_BLEND_RADIUS )
				continue;

			// Far enough that flattening the base does not leave a puddle in the middle of it,
			// and that a river clipping the disc cannot leave a cliff on the pad.
			Real distanceToShore;
			insideLake( (Real)x, (Real)y, &distanceToShore );
			if( distanceToShore < RMG_BLEND_RADIUS )
				continue;

			Bool discDry = TRUE;
			Int disc = pass == 0 ? (Int)( RMG_BASE_FLAT_FLOOR + RMG_BASE_FLAT_SWING ) : (Int)RMG_FLAT_RADIUS;
			Real dryShore = pass == 0 ? RMG_PAD_SHORE_KEEP + 0.5f * RMG_LAKE_SHORE : 4.0f;
			for( Int dy = -disc; dy <= disc && discDry; dy++ )
			{
				for( Int dx = -disc; dx <= disc; dx++ )
				{
					if( dx * dx + dy * dy > disc * disc )
						continue;
					Real shore;
					insideLake( (Real)( x + dx ), (Real)( y + dy ), &shore );
					if( shore < dryShore )
					{
						discDry = FALSE;
						break;
					}
				}
			}
			if( !discDry )
				continue;

			Real roughness = roughnessAt( mapX, mapY, (Int)RMG_FLAT_RADIUS );
			if( roughness > RMG_START_ROUGHNESS * 3.0f )
				continue;

			RMGStartCandidate candidate;
			candidate.m_cellX = (Real)x;
			candidate.m_cellY = (Real)y;
			candidate.m_roughness = pass == 0 ? roughness : roughness + RMG_START_LOOSE_PENALTY;
			candidates.push_back( candidate );
		}
	}

	/* Nowhere flat enough for every seat: the search takes what candidates there are and seats the
		rest on the driest ground left (addDryStart). A plain ring, as it was, put a seat in a lake or on
		the river wherever the ring crossed one. */
	if( (Int)candidates.size() < m_settings.m_numPlayers )
	{
		searchStarts( candidates );
		if( m_settings.m_numPlayers > RandomMapGenerator::DEFAULT_PLAYERS )
			sortStartsRoundCentre();
		return;
	}

	/* Past eight seats nothing below is tuned for it, and a ring leaves the middle of a big map
		empty. The furthest-from-the-rest search spreads them over the whole start box instead, put in
		order round the centre so connectStarts joins each seat to a neighbour, not to one across the
		map. Nor are the layouts walked: twelve layouts of 32 floods each bought nothing a fair duel
		needs. */
	if( m_settings.m_numPlayers > RandomMapGenerator::DEFAULT_PLAYERS )
	{
		searchStarts( candidates );
		sortStartsRoundCentre();
		return;
	}

	/* A river map with an even count sits on a ring, turned half a seat off the river's line,
		so every seat has a mirror image on the far bank and the same walk to it. Left to the search,
		the river's dead band pushed one seat of four out on its own, twice as far from a fight as
		the other three. */
	Int players = m_settings.m_numPlayers;
	Real ringLimit = (Real)playable * 0.5f - (Real)margin;
	const Bool river = m_type->m_river && !m_riverLine.empty();
	std::vector< std::vector<RMGPoint> > layouts;
	if( river && players % 2 == 0 )
	{
		Real ring = (Real)playable * RMG_CIRCLE_RADIUS;
		if( ring > ringLimit )
			ring = ringLimit;
		/* Two or four seats on a river map go out to the edge of the start box instead, as far as the
			search would have put them, and leave the middle to the river and the towns. */
		if( players <= 4 )
		{
			ringStarts( candidates, m_riverAngle + PI / (Real)players, ring, ringLimit );
			if( (Int)m_starts.size() == players )
				return;
		}
		/* Six and eight are too many to trust the mirror alone: the bends push the seats beside the
			river back from it by different amounts, and one seat of eight stood 40% further from its
			nearest enemy than another. The mirrored ring at a few sizes goes in with the other layouts
			and is walked like them. */
		else
		{
			for( Int size = 0; size < RMG_START_RIVER_RING_SIZES; size++ )
			{
				Real sized = ring - (Real)playable * RMG_START_RING_STEP * (Real)size;
				ringStarts( candidates, m_riverAngle + PI / (Real)players, sized, 0.0f );
				if( (Int)m_starts.size() == players )
					layouts.push_back( m_starts );
			}
		}
		// a ring that ran out of sites seats nobody; the layouts below seat everybody
	}

	// A duel is fair by symmetry: each base is the other's nearest enemy, at one walk.
	if( players == 2 )
	{
		searchStarts( candidates );
		return;
	}

	/* Three seats and up, a straight line between two bases says little about the walk: a lake, a
		canyon wall or a cliff band between one pair and not the next had one seat of s42's four at
		153 steps from its nearest enemy and another at 246. So several layouts are tried - the
		search's own, and a ring at a few sizes and turns, each seat snapped onto flat ground - and
		every one is walked. Water blocks the walk and cliffs cost extra, because the passes cut later
		run up a ramp rather than straight over. The layout kept is the one whose nearest and
		second-nearest enemy walks are most nearly the same for every seat, without giving away
		distance. */
	const Int cellCount = m_width * m_height;
	std::vector<Int> cost( cellCount, -1 );
	for( Int y = 0; y < m_height - 1; y++ )
	{
		for( Int x = 0; x < m_width - 1; x++ )
		{
			if( !insidePlayable( x, y ) )
				continue;
			// A river has no fords yet; each layout opens the ones its seats would get (priceRiverByFords).
			if( underwaterAtCell( x, y ) )
				continue;
			cost[cellIndex( x, y )] = cellSpanWorld( x, y ) > RMG_CLIFF_WORLD_SPAN ? RMG_START_CLIFF_COST : 1;
		}
	}

	// Six seats and the furthest-from-the-rest search walks the last ones into the corners.
	if( players < RMG_CIRCLE_PLAYERS )
	{
		searchStarts( candidates );
		layouts.push_back( m_starts );
	}

	Real seedTurn = (Real)(hashCell( m_settings.m_seed, 5, 11 ) % 8U) * (PI * 0.25f);
	Real seatArc = 2.0f * PI / (Real)players;
	for( Int size = 0; size < RMG_START_RING_SIZES; size++ )
	{
		Real ring = (Real)playable * (RMG_CIRCLE_RADIUS - RMG_START_RING_STEP * (Real)size);
		if( ring > ringLimit )
			ring = ringLimit;

		for( Int turn = 0; turn < RMG_START_RING_TURNS; turn++ )
		{
			ringStarts( candidates, seedTurn + seatArc * (Real)turn / (Real)RMG_START_RING_TURNS, ring, 0.0f );
			if( (Int)m_starts.size() == players )
				layouts.push_back( m_starts );
		}
	}

	if( layouts.empty() )
	{
		searchStarts( candidates );
		return;
	}

	std::vector<Real> spreads( layouts.size(), 0.0f );
	std::vector<Int> closest( layouts.size(), 0 );
	Int farthest = 1;
	std::vector<Int> walks;

	std::vector<Int> forded;
	for( UnsignedInt layout = 0; layout < layouts.size(); layout++ )
	{
		m_starts = layouts[layout];
		if( river )
		{
			forded = cost;
			priceRiverByFords( forded );
		}
		startWalks( river ? forded : cost, walks );

		Int nearLow;
		spreads[layout] = walkSpread( walks, &nearLow );
		closest[layout] = nearLow;
		if( nearLow > farthest )
			farthest = nearLow;
	}

	UnsignedInt best = 0;
	Real bestScore = 1.0e9f;
	for( UnsignedInt layout = 0; layout < layouts.size(); layout++ )
	{
		Real score = spreads[layout] + RMG_START_CLOSE_COST * (1.0f - (Real)closest[layout] / (Real)farthest);
		if( score < bestScore )
		{
			bestScore = score;
			best = layout;
		}
	}

	m_starts = layouts[best];

	if( river )
	{
		forded = cost;
		priceRiverByFords( forded );
		nudgeStarts( candidates, forded, farthest );
	}
	else
		nudgeStarts( candidates, cost, farthest );
}

/** How far a layout is from every seat having the same walk to its nearest and second-nearest
	enemy; 1.5 is perfect. The shortest nearest walk comes back as well. */
Real RMGLayout::walkSpread( const std::vector<Int>& walks, Int *nearLowOut )
{
	Int nearLow = 0x7FFFFFFF, nearHigh = 0, secondLow = 0x7FFFFFFF, secondHigh = 0;
	for( UnsignedInt i = 0; i * 2 + 1 < walks.size(); i++ )
	{
		Int nearest = walks[i * 2];
		Int second = walks[i * 2 + 1];
		if( nearest < 1 )
			nearest = 1;
		if( second < 1 )
			second = 1;
		if( nearest < nearLow ) nearLow = nearest;
		if( nearest > nearHigh ) nearHigh = nearest;
		if( second < secondLow ) secondLow = second;
		if( second > secondHigh ) secondHigh = second;
	}

	*nearLowOut = nearLow;
	return (Real)nearHigh / (Real)nearLow + 0.5f * (Real)secondHigh / (Real)secondLow;
}

/** The layouts are rings, and a ring is even only in straight lines: a canyon wall or a lake
	between one pair of seats and not the next leaves one seat walking further to a fight than
	its neighbours. So the seat furthest off the mean walk is moved, a few cells along the
	candidates, by about as much as it is off, and the move is kept only if the walks measure
	more even afterwards. A handful of tries, because every one is a fresh set of walks. */
void RMGLayout::nudgeStarts( const std::vector<RMGStartCandidate>& candidates, const std::vector<Int>& cost, Int farthest )
{
	Int players = (Int)m_starts.size();
	Real hardFloor = 2.0f * RMG_FLAT_RADIUS + 6.0f;

	std::vector<Int> walks;
	startWalks( cost, walks );
	Int nearLow;
	Real bestScore = walkSpread( walks, &nearLow ) + RMG_START_CLOSE_COST * (1.0f - (Real)nearLow / (Real)farthest);

	std::vector<char> tried( players, 0 );
	for( Int attempt = 0; attempt < RMG_START_NUDGES; attempt++ )
	{
		Real mean = 0.0f;
		for( Int i = 0; i < players; i++ )
			mean += (Real)walks[i * 2];
		mean /= (Real)players;

		// The seat furthest off the mean that has not already failed to move.
		Int seat = -1;
		Real worst = 0.0f;
		for( Int i = 0; i < players; i++ )
		{
			Real off = fabsf( (Real)walks[i * 2] - mean );
			if( !tried[i] && off > worst )
			{
				worst = off;
				seat = i;
			}
		}
		if( seat < 0 || worst < 2.0f )
			break;

		// Its nearest enemy in a straight line stands in for the one it walks to.
		Int partner = -1;
		Real partnerDistance = 1.0e9f;
		for( Int j = 0; j < players; j++ )
		{
			if( j == seat )
				continue;
			Real dx = m_starts[seat].m_cellX - m_starts[j].m_cellX;
			Real dy = m_starts[seat].m_cellY - m_starts[j].m_cellY;
			Real distance = sqrtf( dx * dx + dy * dy );
			if( distance < partnerDistance )
			{
				partnerDistance = distance;
				partner = j;
			}
		}

		Real want = mean - (Real)walks[seat * 2];
		if( want > RMG_START_NUDGE_CELLS ) want = RMG_START_NUDGE_CELLS;
		if( want < -RMG_START_NUDGE_CELLS ) want = -RMG_START_NUDGE_CELLS;

		RMGPoint here = m_starts[seat];
		Real bestFit = 1.0e9f;
		Int pick = -1;
		for( UnsignedInt i = 0; i < candidates.size(); i++ )
		{
			Real mx = candidates[i].m_cellX - here.m_cellX;
			Real my = candidates[i].m_cellY - here.m_cellY;
			if( mx * mx + my * my > RMG_START_NUDGE_CELLS * RMG_START_NUDGE_CELLS || (mx == 0.0f && my == 0.0f) )
				continue;

			Bool clear = TRUE;
			for( Int j = 0; j < players && clear; j++ )
			{
				if( j == seat )
					continue;
				Real dx = candidates[i].m_cellX - m_starts[j].m_cellX;
				Real dy = candidates[i].m_cellY - m_starts[j].m_cellY;
				clear = dx * dx + dy * dy >= hardFloor * hardFloor;
			}
			if( !clear )
				continue;

			Real dx = candidates[i].m_cellX - m_starts[partner].m_cellX;
			Real dy = candidates[i].m_cellY - m_starts[partner].m_cellY;
			Real fit = fabsf( sqrtf( dx * dx + dy * dy ) - partnerDistance - want ) + candidates[i].m_roughness * 0.5f;
			if( fit < bestFit )
			{
				bestFit = fit;
				pick = (Int)i;
			}
		}

		tried[seat] = 1;
		if( pick < 0 )
			continue;

		m_starts[seat].m_cellX = candidates[pick].m_cellX;
		m_starts[seat].m_cellY = candidates[pick].m_cellY;
		std::vector<Int> moved;
		startWalks( cost, moved );
		Real score = walkSpread( moved, &nearLow ) + RMG_START_CLOSE_COST * (1.0f - (Real)nearLow / (Real)farthest);
		if( score < bestScore )
		{
			bestScore = score;
			walks = moved;
			for( Int i = 0; i < players; i++ )
				tried[i] = 0;
			tried[seat] = 1;		// the next try goes to another seat
		}
		else
			m_starts[seat] = here;
	}
}

/** The walk from each seat to its nearest and second-nearest enemy: a Dial flood over the cost
	grid, stopped at the second seat it reaches, so it covers the ground between neighbours and not
	the map. The route is the cheapest one, which goes round a cliff band the way the ring routes
	carved later do, and what is kept is its length in steps, which is what a unit walks. A seat
	cut off by water is priced at three times the straight-line steps. */
void RMGLayout::startWalks( const std::vector<Int>& cost, std::vector<Int>& walks ) const
{
	std::vector<Int> stepsTo( m_width * m_height, 0 );
	Int count = (Int)m_starts.size();
	walks.assign( count * 2, 0 );

	std::vector<Int> seatAt( m_width * m_height, -1 );
	std::vector<Int> cells( count, 0 );
	for( Int i = 0; i < count; i++ )
	{
		Int x = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int y = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		cells[i] = cellIndex( x, y );
		seatAt[cells[i]] = i;
	}

	const Int buckets = RMG_START_CLIFF_COST + 1;
	std::vector< std::vector<Int> > open( buckets );
	std::vector<Int> dist( m_width * m_height, -1 );
	std::vector<Int> touched;
	std::vector<Int> reached( count, -1 );

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	for( Int from = 0; from < count; from++ )
	{
		Int found = 0;
		for( Int i = 0; i < count; i++ )
			reached[i] = -1;
		for( Int b = 0; b < buckets; b++ )
			open[b].clear();

		dist[cells[from]] = 0;
		stepsTo[cells[from]] = 0;
		touched.push_back( cells[from] );
		open[0].push_back( cells[from] );
		Int pending = 1;

		for( Int here = 0; pending > 0 && found < 2; here++ )
		{
			std::vector<Int>& bucket = open[here % buckets];
			for( UnsignedInt k = 0; k < bucket.size() && found < 2; k++ )
			{
				Int index = bucket[k];
				pending--;
				if( dist[index] != here )
					continue;

				Int seat = seatAt[index];
				if( seat >= 0 && seat != from )
				{
					reached[seat] = here;
					walks[from * 2 + found] = stepsTo[index];
					found++;
				}

				Int x = index % m_width;
				Int y = index / m_width;
				for( Int i = 0; i < 4; i++ )
				{
					Int nx = x + offsetX[i];
					Int ny = y + offsetY[i];
					if( nx < 0 || ny < 0 || nx >= m_width - 1 || ny >= m_height - 1 )
						continue;

					Int next = ny * m_width + nx;
					if( cost[next] < 0 )
						continue;

					Int step = here + cost[next];
					if( dist[next] >= 0 && dist[next] <= step )
						continue;

					if( dist[next] < 0 )
						touched.push_back( next );
					dist[next] = step;
					stepsTo[next] = stepsTo[index] + 1;
					open[step % buckets].push_back( next );
					pending++;
				}
			}
			bucket.clear();
		}

		for( UnsignedInt t = 0; t < touched.size(); t++ )
			dist[touched[t]] = -1;
		touched.clear();

		if( found < 2 )
		{
			Int nearest = found > 0 ? walks[from * 2] : 0x7FFFFFFF;
			Int second = 0x7FFFFFFF;
			for( Int j = 0; j < count; j++ )
			{
				if( j == from || reached[j] >= 0 )
					continue;

				Real dx = m_starts[from].m_cellX - m_starts[j].m_cellX;
				Real dy = m_starts[from].m_cellY - m_starts[j].m_cellY;
				Int walk = 3 * (Int)(fabsf( dx ) + fabsf( dy ));
				if( walk < nearest )
				{
					second = nearest;
					nearest = walk;
				}
				else if( walk < second )
					second = walk;
			}
			walks[from * 2] = nearest;
			walks[from * 2 + 1] = second;
		}

		// Reached in order of cost, which a cliff can put the longer walk first.
		if( walks[from * 2] > walks[from * 2 + 1] )
			std::swap( walks[from * 2], walks[from * 2 + 1] );
	}
}

/** Seats at even turns round a ring, each snapped to the nearest flat candidate the others leave.
	With an edge ring, each seat goes out to the edge of the start box instead: a quarter turn keeps
	the larger of |cos| and |sin|, so every seat is the same distance out. */
void RMGLayout::ringStarts( const std::vector<RMGStartCandidate>& candidates, Real angle, Real ring, Real edgeRing )
{
	m_starts.clear();

	Real centre = (Real)m_settings.m_playableCells * 0.5f;
	std::vector<char> used( candidates.size(), 0 );
	Real hardFloor = 2.0f * RMG_FLAT_RADIUS + 6.0f;

	for( Int seat = 0; seat < m_settings.m_numPlayers; seat++ )
	{
		Real seatAngle = angle + 2.0f * PI * (Real)seat / (Real)m_settings.m_numPlayers;
		Real seatRing = ring;
		if( edgeRing > 0.0f )
		{
			Real reach = fabsf( Cos( seatAngle ) );
			if( fabsf( Sin( seatAngle ) ) > reach )
				reach = fabsf( Sin( seatAngle ) );
			seatRing = edgeRing / reach;
		}
		Real targetX = centre + seatRing * Cos( seatAngle );
		Real targetY = centre + seatRing * Sin( seatAngle );

		Real bestScore = 1.0e9f;
		UnsignedInt best = 0;
		Bool foundClear = FALSE;
		Bool anyUnused = FALSE;

		for( UnsignedInt i = 0; i < candidates.size(); i++ )
		{
			if( used[i] )
				continue;
			anyUnused = TRUE;

			Real distance = distanceToNearestStart( candidates[i].m_cellX, candidates[i].m_cellY );
			Bool clearOfTheFloor = m_starts.empty() || distance >= hardFloor;
			if( foundClear && !clearOfTheFloor )
				continue;

			Real dx = candidates[i].m_cellX - targetX;
			Real dy = candidates[i].m_cellY - targetY;
			Real score = sqrtf( dx * dx + dy * dy ) + candidates[i].m_roughness * 0.5f;
			if( score < bestScore || (clearOfTheFloor && !foundClear) )
			{
				bestScore = score;
				best = i;
				foundClear = clearOfTheFloor;
			}
		}

		// a ring with no site clear of the floor seats nobody, as one that ran out of sites: ladder seed
		// 312's river ring put its two seats 3 cells apart, and the search after it keeps the floor
		if( !anyUnused || ( !m_starts.empty() && !foundClear ) )
			break;

		used[best] = 1;
		RMGPoint start;
		start.m_cellX = candidates[best].m_cellX;
		start.m_cellY = candidates[best].m_cellY;
		m_starts.push_back( start );
	}
}

/** The flattest candidate first, then each seat after it on whichever good site is furthest from
	the ones already taken. */
void RMGLayout::searchStarts( const std::vector<RMGStartCandidate>& candidates )
{
	m_starts.clear();
	std::vector<char> used( candidates.size(), 0 );

	// The flattest candidate opens the list.
	UnsignedInt flattest = 0;
	for( UnsignedInt i = 1; i < candidates.size(); i++ )
	{
		if( candidates[i].m_roughness < candidates[flattest].m_roughness )
			flattest = i;
	}

	RMGPoint start;
	if( !candidates.empty() )
	{
		start.m_cellX = candidates[flattest].m_cellX;
		start.m_cellY = candidates[flattest].m_cellY;
		m_starts.push_back( start );
		used[flattest] = 1;
	}

	/* Two base discs may not touch, whatever the map looks like. A candidate inside this is not
		scored against the others at all; only a map with nowhere else to go falls back to it. */
	Real hardFloor = 2.0f * RMG_FLAT_RADIUS + 6.0f;

	while( (Int)m_starts.size() < m_settings.m_numPlayers )
	{
		Real bestScore = -1.0e9f;
		UnsignedInt bestCandidate = 0;
		Bool foundClearOne = FALSE;
		Bool foundAny = FALSE;

		for( UnsignedInt i = 0; i < candidates.size(); i++ )
		{
			// a site already taken would stack two seats on one pad
			if( used[i] )
				continue;
			foundAny = TRUE;
			Real distance = distanceToNearestStart( candidates[i].m_cellX, candidates[i].m_cellY );

			Bool clearOfTheFloor = distance >= hardFloor;
			if( foundClearOne && !clearOfTheFloor )
				continue;

			/* Distance first, flatness second, and distance is never traded away: a base that
				gives up ten cells of separation for slightly better ground is a base sharing its
				half of the map with a neighbour, and then both of them are fighting over one
				supply dock while the far side of the map stands empty. */
			Real score = distance - candidates[i].m_roughness * 2.0f;
			if( score > bestScore || (clearOfTheFloor && !foundClearOne) )
			{
				bestScore = score;
				bestCandidate = i;
				foundClearOne = clearOfTheFloor;
			}
		}

		/* Every candidate left inside the floor: the flat sites can all sit in one patch of ground (two
			starts 3 cells apart on ladder seeds 312 and 315), so the seat goes to the dry cell furthest
			from the others, which flattenBases levels, when that one clears the floor. */
		if( foundAny && !foundClearOne && addDryStart() )
		{
			const RMGPoint dry = m_starts.back();
			m_starts.pop_back();
			if( distanceToNearestStart( dry.m_cellX, dry.m_cellY ) >= hardFloor )
			{
				m_starts.push_back( dry );
				continue;
			}
		}

		if( !foundAny )
		{
			if( !addDryStart() )
			{
				// no dry cell left anywhere: a point of a ring, so every seat exists, and the final check
				// flags the map
				Real centre = (Real)m_settings.m_playableCells * 0.5f;
				Real angle = 2.0f * PI * (Real)m_starts.size() / (Real)m_settings.m_numPlayers;
				start.m_cellX = (Real)(Int)( centre + centre * 0.62f * Cos( angle ) );
				start.m_cellY = (Real)(Int)( centre + centre * 0.62f * Sin( angle ) );
				m_starts.push_back( start );
			}
			continue;
		}

		used[bestCandidate] = 1;
		start.m_cellX = candidates[bestCandidate].m_cellX;
		start.m_cellY = candidates[bestCandidate].m_cellY;
		m_starts.push_back( start );
	}
}

/** One more seat when the candidates have run out: the cell of the playable area, the disc's own
	margin in from the edge, that is furthest from every seat so far, preferring one whose base would
	be dry by the blend's width, then by the flat's, then dry at all. Rough ground is fine, since
	flattenBases levels it; water is not, since a base levels to its middle's height. Off the massif's
	top, as a candidate is. The first seat goes to the dry cell furthest from the middle. FALSE when
	no dry cell is left that is not a seat already. */
Bool RMGLayout::addDryStart( void )
{
	const Int playable = m_settings.m_playableCells;
	const Int margin = (Int)RMG_FLAT_RADIUS + 1;
	const Real hardFloor = 2.0f * RMG_FLAT_RADIUS + 6.0f;
	const Real centre = (Real)playable * 0.5f;

	Int bestTier = -1;
	Bool bestClear = FALSE;
	Real bestDistance = 0.0f;
	RMGPoint best;
	best.m_cellX = best.m_cellY = 0.0f;
	for( Int y = margin; y < playable - margin; y += m_startSearchStride )
	{
		for( Int x = margin; x < playable - margin; x += m_startSearchStride )
		{
			if( !m_massifLobes.empty() && massifClearance( (Real)x, (Real)y ) < RMG_BLEND_RADIUS )
				continue;
			Real shore;
			insideLake( (Real)x, (Real)y, &shore );
			if( shore <= 0.0f || underwaterAtCell( x + RMG_BORDER_CELLS, y + RMG_BORDER_CELLS ) )
				continue;
			const Int tier = shore >= RMG_BLEND_RADIUS ? 2 : ( shore >= RMG_FLAT_RADIUS ? 1 : 0 );

			Real distance;
			if( m_starts.empty() )
			{
				const Real dx = (Real)x - centre, dy = (Real)y - centre;
				distance = sqrtf( dx * dx + dy * dy );
			}
			else
				distance = distanceToNearestStart( (Real)x, (Real)y );
			if( distance < 1.0f )
				continue;
			const Bool clear = m_starts.empty() || distance >= hardFloor;

			// clear of the other discs first, then the drier tier, then the further; ties keep walk order
			Bool better = bestTier < 0 || ( clear && !bestClear );
			if( !better && clear == bestClear )
				better = tier > bestTier || ( tier == bestTier && distance > bestDistance );
			if( !better )
				continue;
			bestTier = tier;
			bestClear = clear;
			bestDistance = distance;
			best.m_cellX = (Real)x;
			best.m_cellY = (Real)y;
		}
	}
	if( bestTier < 0 )
		return FALSE;
	m_starts.push_back( best );
	return TRUE;
}

/** A point's bearing from (centre, centre) as a diamond angle, 0 to 4 round the turn: plain
	arithmetic, so every machine orders the same. */
static Real rmgDiamondBearing( const RMGPoint& p, Real centre )
{
	const Real dx = p.m_cellX - centre, dy = p.m_cellY - centre;
	const Real sum = fabsf( dx ) + fabsf( dy );
	if( sum <= 0.0f )
		return 0.0f;
	const Real turn = dy / sum;
	return dx >= 0.0f ? ( turn < 0.0f ? 4.0f + turn : turn ) : 2.0f - turn;
}

/// The starts in order of their bearing from the map's centre; ties keep search order.
void RMGLayout::sortStartsRoundCentre( void )
{
	const Real centre = (Real)m_settings.m_playableCells * 0.5f;
	std::stable_sort( m_starts.begin(), m_starts.end(), [centre]( const RMGPoint& a, const RMGPoint& b )
	{
		return rmgDiamondBearing( a, centre ) < rmgDiamondBearing( b, centre );
	} );
}

/// Flatten a disc under each start so a base can be laid out on it.
void RMGLayout::flattenBases( void )
{
	if( m_baseLevelled.size() != m_heights.size() )
	{
		m_baseLevelled.assign( m_heights.size(), 0 );
		m_baseUnder.assign( m_heights.size(), 0 );
		m_baseResult.assign( m_heights.size(), 0 );
	}

	Bool stale = m_baseShapeStarts.size() != m_starts.size();
	for( UnsignedInt i = 0; i < m_starts.size() && !stale; i++ )
	{
		stale = m_baseShapeStarts[i].m_cellX != m_starts[i].m_cellX ||
						m_baseShapeStarts[i].m_cellY != m_starts[i].m_cellY;
	}
	if( stale )
		shapeBases();

	std::vector<Real> startHeights;
	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Int mapX = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int mapY = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		// the level shapeBases chose, or the start's own cell where it chose none
		startHeights.push_back( m_baseLevels[i] >= 0.0f ? m_baseLevels[i] : (Real)m_heights[cellIndex( mapX, mapY )] );
	}

	for( UnsignedInt e = 0; e < m_baseCells.size(); e++ )
	{
		Int c = m_baseCells[e];
		Real t = m_baseBlendT[e];
		UnsignedInt nearestIndex = m_baseOwners[e];

		/* Blend against the ground as it stood before the last levelling, unless something has
			moved this cell since. Blending the blend again on every call pulled the ring towards
			the base a little more each time while its outer edge stayed put, and after the ring
			route and the money walks had each levelled it, that edge stood as a cliff round the
			base. */
		UnsignedByte under = m_heights[c];
		if( m_baseLevelled[c] && m_heights[c] == m_baseResult[c] )
			under = m_baseUnder[c];

		Real h = lerpReal( startHeights[nearestIndex], (Real)under, fadeCurve( t ) );
		if( h < 1.0f ) h = 1.0f;
		if( h > 254.0f ) h = 254.0f;

		m_heights[c] = (UnsignedByte)(h + 0.5f);
		m_baseLevelled[c] = 1;
		m_baseUnder[c] = under;
		m_baseResult[c] = m_heights[c];
	}

	levelStartSquares( startHeights );
}

/** Which cells round the starts a levelling touches, how far into the blend each one is and which
	start it belongs to. None of it depends on the heights, and flattenBases runs after every route
	and every money walk, so it is worked out once for a set of starts and kept. */
void RMGLayout::shapeBases( void )
{
	m_baseShapeStarts = m_starts;
	m_baseCells.clear();
	m_baseBlendT.clear();
	m_baseOwners.clear();
	m_baseLevels.clear();

	std::vector<Real> flatRadii;
	std::vector<Real> flatWidest;
	std::vector<Real> blendWidths;
	std::vector<Real> blendCaps;
	std::vector<Real> turnCos;
	std::vector<Real> turnSin;

	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		/* Two discs at different heights meeting in the middle is a cliff between two bases, so
			on a map too cramped to hold everybody at the full radius each disc gives way rather
			than overlapping its neighbour. */
		Real nearestOther = 1.0e9f;
		for( UnsignedInt j = 0; j < m_starts.size(); j++ )
		{
			if( j == i )
				continue;

			Real dx = m_starts[i].m_cellX - m_starts[j].m_cellX;
			Real dy = m_starts[i].m_cellY - m_starts[j].m_cellY;
			Real distance = sqrtf( dx * dx + dy * dy );
			if( distance < nearestOther )
				nearestOther = distance;
		}

		Real flat = RMG_BASE_FLAT_FLOOR;
		Real swing = RMG_BASE_FLAT_SWING;
		Real blend = RMG_BLEND_RADIUS;
		if( m_style )
		{
			flat = 0.75f * m_style->m_flat * m_styleScale;
			swing = 0.25f * m_style->m_flat * m_styleScale;
			blend = flat + 12.0f * m_styleScale;
		}
		if( flat > nearestOther * 0.45f )
			flat = nearestOther * 0.45f;
		Real widest = flat + swing;
		if( widest > nearestOther * 0.45f )
			widest = nearestOther * 0.45f;

		/* The level a base is cut to is the median of the dry ground under its flat and the inner half
			of its blend, which moves the least earth there: half of it is cut and half filled. It used to
			be the height of the start's own cell, so a seat on a knoll stood on a raised tray with a rim
			round it and one in a dip sat in a pit. And the more the ground round the flat differs from
			that level, the wider the blend,
			so the ground runs into the base at one slope instead of a ring that is steep where the
			difference is large. A style and a skeleton keep the start cell's height. */
		Real level = -1.0f;
		if( !m_style && !m_skeleton )
		{
			Int histogram[256];
			for( Int k = 0; k < 256; k++ )
				histogram[k] = 0;
			Int counted = 0;
			Real ringDiff = 0.0f;
			Int ringCount = 0;
			const Real ringOut = 0.5f * ( flat + blend );
			const Int box = (Int)ringOut + 1;
			const Int sx = (Int)( m_starts[i].m_cellX + 0.5f ) + RMG_BORDER_CELLS;
			const Int sy = (Int)( m_starts[i].m_cellY + 0.5f ) + RMG_BORDER_CELLS;
			for( Int pass = 0; pass < 2; pass++ )
			{
				for( Int dy = -box; dy <= box; dy++ )
				{
					for( Int dx = -box; dx <= box; dx++ )
					{
						Int x = sx + dx;
						Int y = sy + dy;
						if( x < 0 || y < 0 || x >= m_width || y >= m_height )
							continue;
						Real d = sqrtf( (Real)( dx * dx + dy * dy ) );
						Bool under = d <= ringOut;
						Bool ring = d > flat && d <= ringOut;
						if( pass == 0 ? !under : !ring )
							continue;
						Real shore;
						insideLake( (Real)( x - RMG_BORDER_CELLS ), (Real)( y - RMG_BORDER_CELLS ), &shore );
						if( shore < RMG_PAD_SHORE_KEEP )
							continue;
						Int h = m_heights[cellIndex( x, y )];
						if( pass == 0 )
						{
							histogram[h]++;
							counted++;
						}
						else
						{
							ringDiff += fabsf( (Real)h - level );
							ringCount++;
						}
					}
				}
				if( pass == 0 )
				{
					if( counted == 0 )
						break;
					Int below = 0;
					Int k = 0;
					for( ; k < 255; k++ )
					{
						below += histogram[k];
						if( 2 * below >= counted )
							break;
					}
					level = (Real)k;
				}
			}
			if( ringCount > 0 )
				blend += 2.0f * ringDiff / (Real)ringCount;
		}
		m_baseLevels.push_back( level );

		if( blend > nearestOther * 0.9f )
			blend = nearestOther * 0.9f;
		if( blend < flat + 4.0f )
			blend = flat + 4.0f;

		/* The edge of the flat and the edge of the blend both wander with the bearing, outward only,
			so no base has less flat ground than the floor. The flat edge is one profile turned to a
			different angle for each seat: every base gains the same ground, and no two have the same
			outline to look at. */
		Real turn = hashUnit( m_settings.m_seed, (Int)i, 4242 ) * 2.0f * PI;

		flatRadii.push_back( flat );
		flatWidest.push_back( widest );
		blendWidths.push_back( blend - flat );
		blendCaps.push_back( nearestOther * 0.9f );
		turnCos.push_back( Cos( turn ) );
		turnSin.push_back( Sin( turn ) );
	}

	/* Only the squares round each start can be inside a blend, so only those are walked; a cell
		two squares share is levelled once. Every pass of the ring routes calls this, and walking
		the whole map each time was most of what connecting the starts cost. */
	Real reach = 0.0f;
	for( UnsignedInt i = 0; i < flatRadii.size(); i++ )
	{
		Real widest = flatWidest[i] + blendWidths[i] * ( 1.0f + RMG_BASE_BLEND_SWING );
		if( widest > reach )
			reach = widest;
	}
	Int box = (Int)reach + 2;
	std::vector<char> walked( m_heights.size(), 0 );

	for( UnsignedInt s = 0; s < m_starts.size(); s++ )
	for( Int y = (Int)m_starts[s].m_cellY + RMG_BORDER_CELLS - box; y <= (Int)m_starts[s].m_cellY + RMG_BORDER_CELLS + box; y++ )
	{
		if( y < 0 || y >= m_height )
			continue;
		for( Int x = (Int)m_starts[s].m_cellX + RMG_BORDER_CELLS - box; x <= (Int)m_starts[s].m_cellX + RMG_BORDER_CELLS + box; x++ )
		{
			if( x < 0 || x >= m_width || walked[cellIndex( x, y )] )
				continue;
			walked[cellIndex( x, y )] = 1;

			Real px = (Real)(x - RMG_BORDER_CELLS);
			Real py = (Real)(y - RMG_BORDER_CELLS);

			// The nearest start only. Blending against every start in turn lets two
			// overlapping discs flatten ground that belongs to neither.
			Real nearest = 1.0e9f;
			UnsignedInt nearestIndex = 0;
			for( UnsignedInt i = 0; i < m_starts.size(); i++ )
			{
				Real dx = px - m_starts[i].m_cellX;
				Real dy = py - m_starts[i].m_cellY;
				Real distance = sqrtf( dx * dx + dy * dy );
				if( distance < nearest )
				{
					nearest = distance;
					nearestIndex = i;
				}
			}

			UnsignedInt n = nearestIndex;
			Real flatR = flatRadii[n];
			Real blendR = flatR + blendWidths[n];
			if( nearest > 0.5f )
			{
				Real ux = ( px - m_starts[n].m_cellX ) / nearest;
				Real uy = ( py - m_starts[n].m_cellY ) / nearest;
				Real turnedX = ux * turnCos[n] - uy * turnSin[n];
				Real turnedY = ux * turnSin[n] + uy * turnCos[n];
				flatR += ( flatWidest[n] - flatRadii[n] ) * ringNoise( m_perm, turnedX, turnedY, 11.3f, 47.9f );
				blendR = flatR + blendWidths[n] * ( 1.0f + RMG_BASE_BLEND_SWING * ringNoise( m_perm, turnedX, turnedY, -23.1f, 5.7f ) );
				if( blendR > blendCaps[n] )
					blendR = blendCaps[n];
				if( blendR < flatR + 4.0f )
					blendR = flatR + 4.0f;
			}

			if( nearest >= blendR )
				continue;

			Real t = 0.0f;
			if( nearest > flatR )
				t = (nearest - flatR) / (blendR - flatR);

			/* The ring keeps off the lake the way a pad does: water and its beach are never levelled,
				and nearer the shore the ground keeps more of its own slope down to the water. The pull
				starts at nothing on the flat's edge and comes in over a few cells, so the base's lake
				side eases down instead of dropping off a kerb. Levelled to the start's height, the ring
				used to raise the bed of a lake beside the base into dry ground under the water. */
			Real shore;
			insideLake( px, py, &shore );
			if( shore < RMG_PAD_SHORE_KEEP )
				continue;
			Real shoreT = ( shore - RMG_PAD_SHORE_KEEP ) / RMG_LAKE_SHORE;
			if( shoreT < 1.0f )
			{
				Real ramp = ( nearest - flatR ) / RMG_BASE_SHORE_RAMP;
				if( ramp < 0.0f ) ramp = 0.0f;
				if( ramp > 1.0f ) ramp = 1.0f;
				Real want = ( 1.0f - shoreT ) * ramp;
				if( t < want )
					t = want;
			}

			m_baseCells.push_back( cellIndex( x, y ) );
			m_baseBlendT.push_back( t );
			m_baseOwners.push_back( (UnsignedByte)nearestIndex );
		}
	}
}

void RMGLayout::levelStartSquares( const std::vector<Real>& startHeights )
{
	/* The pathfinder tests a 9-cell square, and the span of a corner cell of that square
		looks one cell further out, into the blend. A river bank sitting there is a cliff
		on the pad. Force that square flat after the disc so the base is walkable. It is forced as
		the disc round the square's corners: a square of one height on rolling ground read in the
		game as a square of one texture with the slope's texture all round it. */
	const Int square = 10;
	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Int sx = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int sy = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		UnsignedByte h = (UnsignedByte)(startHeights[i] + 0.5f);
		if( h < 1 ) h = 1;
		if( h > 254 ) h = 254;

		for( Int dy = -square - 5; dy <= square + 5; dy++ )
		{
			for( Int dx = -square - 5; dx <= square + 5; dx++ )
			{
				if( dx * dx + dy * dy > 2 * square * square )
					continue;
				Int x = sx + dx;
				Int y = sy + dy;
				if( x < 0 || y < 0 || x >= m_width || y >= m_height )
					continue;
				m_heights[cellIndex( x, y )] = h;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Passability, and cutting a pass where there is none
//-----------------------------------------------------------------------------

void RMGLayout::buildPassability( void )
{
	m_passable.assign( m_width * m_height, 0 );

	for( Int y = 0; y < m_height - 1; y++ )
	{
		for( Int x = 0; x < m_width - 1; x++ )
			m_passable[cellIndex( x, y )] = playableAtCell( x, y ) ? 1 : 0;
	}
}

Bool RMGLayout::carvePass( Int fromStart, Int toStart )
{
	Int fromX = (Int)(m_starts[fromStart].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int fromY = (Int)(m_starts[fromStart].m_cellY + 0.5f) + RMG_BORDER_CELLS;
	Int toX = (Int)(m_starts[toStart].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int toY = (Int)(m_starts[toStart].m_cellY + 0.5f) + RMG_BORDER_CELLS;

	return carvePassBetweenCells( fromX, fromY, toX, toY, TRUE, 0.0f, 0.0f, 0.0f );
}

/** A route between two cells, priced so that a terrace is nearly free and the step off one is
	expensive but not forbidden, then the ground along that route is cut into a ramp no steeper than
	the pathfinder will walk. What comes out is a ramp between two layers rather than a trench
	across the map, because the search stayed on the flat wherever it could. */
Bool RMGLayout::carvePassBetweenCells( Int fromX, Int fromY, Int toX, Int toY, Bool recordRamp,
																			 Real protectRadius, Real protectX, Real protectY )
{

	const Int cellCount = m_width * m_height;
	std::vector<Int> cost( cellCount, 0x7FFFFFFF );
	std::vector<Int> cameFrom( cellCount, -1 );

	/* Walking ground costs its climb as well as its length, so a route keeps to the contour where
		one is on offer and leaves less ground for the earthwork to move. */
	const Int flatCost = 4;
	const Int cliffCost = 240;
	const Int waterCost = 1000;
	const Bool plainRoad = !m_style && !m_skeleton;
	std::vector<Int> cellPrice;				// roadStepCost's price of each cell, -1 until read
	if( plainRoad )
		cellPrice.assign( cellCount, -1 );

	/* Dijkstra over a binary heap of (cost, cell). A linear scan of the open list was fine on a
		96-cell map and is not on a 456-cell one: the open list runs to tens of thousands of cells
		and the scan is what the whole generator would then be doing. The cell index breaks ties so
		the route does not depend on how the heap happened to order two equal costs. */
	struct RMGOpenCell
	{
		Int m_cost;
		Int m_index;

		Bool operator<( const RMGOpenCell& other ) const
		{
			if( m_cost != other.m_cost )
				return m_cost > other.m_cost;		// std::push_heap wants the cheapest last
			return m_index > other.m_index;
		}
	};

	std::vector<RMGOpenCell> open;
	RMGOpenCell first;
	first.m_cost = 0;
	first.m_index = fromY * m_width + fromX;
	open.push_back( first );
	cost[first.m_index] = 0;

	while( !open.empty() )
	{
		std::pop_heap( open.begin(), open.end() );
		RMGOpenCell cheapest = open.back();
		open.pop_back();

		Int index = cheapest.m_index;
		if( cheapest.m_cost > cost[index] )
			continue;								// a cheaper way here was found after this was queued

		if( index == toY * m_width + toX )
			break;

		Int x = index % m_width;
		Int y = index / m_width;

		static const Int offsetX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		static const Int offsetY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

		for( Int i = 0; i < (plainRoad ? 8 : 4); i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			// a route through the border band joins nothing a unit can drive
			if( !insidePlayable( nx, ny ) )
				continue;

			Int climb = (Int)m_heights[cellIndex( nx, ny )] - (Int)m_heights[index];
			Int step = flatCost + ((climb >= 0) ? climb : -climb);
			if( plainRoad )
				step = roadStepCost( index, cellIndex( nx, ny ), i >= 4, cellPrice );
			else if( m_passable[cellIndex( nx, ny )] == 0 )
				step = underwaterAtCell( nx, ny ) ? waterCost : cliffCost;

			Int next = ny * m_width + nx;
			if( cost[index] + step >= cost[next] )
				continue;

			cost[next] = cost[index] + step;
			cameFrom[next] = index;

			RMGOpenCell reached;
			reached.m_cost = cost[next];
			reached.m_index = next;
			open.push_back( reached );
			std::push_heap( open.begin(), open.end() );
		}
	}

	if( cameFrom[toY * m_width + toX] < 0 )
		return FALSE;

	// Walk the route back, then cut it as a ramp: each cell of the route may
	// only be so much higher or lower than the one before it.
	std::vector<Int> route;
	for( Int index = toY * m_width + toX; index >= 0; index = cameFrom[index] )
	{
		route.push_back( index );
		if( index == fromY * m_width + fromX )
			break;
	}

	if( plainRoad )
		smoothRoute( route );
	applyRouteProfile( route, recordRamp, protectRadius, protectX, protectY );
	return TRUE;
}

/** A plain map's way as the search found it is a chain of straight and 45 degree runs. It is drawn
	again through a running mean of its own points, RMG_ROAD_SMOOTH cells either side in
	RMG_ROAD_SMOOTH_PASSES passes with its two ends held, which turns each corner into a bend, and
	rasterised back in half-cell steps so the cells stay joined. A point the mean would move onto
	a cliff or into water that the search had kept off stays where the search put it. */
void RMGLayout::smoothRoute( std::vector<Int>& route ) const
{
	const Int count = (Int)route.size();
	if( count < 3 )
		return;
	std::vector<Real> pointX( count ), pointY( count ), nextX( count ), nextY( count );
	for( Int i = 0; i < count; i++ )
	{
		pointX[i] = (Real)( route[i] % m_width );
		pointY[i] = (Real)( route[i] / m_width );
	}
	for( Int pass = 0; pass < RMG_ROAD_SMOOTH_PASSES; pass++ )
	{
		for( Int i = 0; i < count; i++ )
		{
			Int reach = RMG_ROAD_SMOOTH;
			if( reach > i ) reach = i;
			if( reach > count - 1 - i ) reach = count - 1 - i;
			Real sumX = 0.0f, sumY = 0.0f;
			for( Int j = i - reach; j <= i + reach; j++ )
			{
				sumX += pointX[j];
				sumY += pointY[j];
			}
			nextX[i] = sumX / (Real)( 2 * reach + 1 );
			nextY[i] = sumY / (Real)( 2 * reach + 1 );
			if( m_passable[cellIndex( (Int)( nextX[i] + 0.5f ), (Int)( nextY[i] + 0.5f ) )] == 0 && m_passable[route[i]] != 0 )
			{
				nextX[i] = (Real)( route[i] % m_width );
				nextY[i] = (Real)( route[i] / m_width );
			}
		}
		pointX.swap( nextX );
		pointY.swap( nextY );
	}

	/* A way across open ground the search found no reason to bend still runs ruler-straight, often
		at 45 degrees. It sways to either side by up to RMG_ROAD_SWAY cells on a slow noise read
		along its length, eased in over its first and last RMG_ROAD_SWAY_EASE cells so it still
		meets both ends. The offsets are averaged along the way, and wherever the averaged sway would
		climb more than RMG_ROAD_SWAY_RISE bytes off the line the search chose, or onto a crest,
		the sway is dropped over that stretch and the rest averaged again, so a dropped stretch
		bends back to the search's line instead of jumping to it. */
	const Real salt = (Real)( ( route[0] + route[count - 1] ) % 211 );
	std::vector<Real> normalX( count, 0.0f ), normalY( count, 0.0f ), sway( count, 0.0f ), offset( count, 0.0f ), nextOffset( count, 0.0f );
	std::vector<Real> along( count, 0.0f );
	Real travelled = 0.0f;
	for( Int i = 1; i < count - 1; i++ )
	{
		travelled += sqrtf( ( pointX[i] - pointX[i - 1] ) * ( pointX[i] - pointX[i - 1] )
			+ ( pointY[i] - pointY[i - 1] ) * ( pointY[i] - pointY[i - 1] ) );
		Real tangentX = pointX[i + 1] - pointX[i - 1];
		Real tangentY = pointY[i + 1] - pointY[i - 1];
		Real length = sqrtf( tangentX * tangentX + tangentY * tangentY );
		if( length < 0.01f )
			continue;
		normalX[i] = -tangentY / length;
		normalY[i] = tangentX / length;
		Real ease = (Real)( ( i < count - 1 - i ) ? i : count - 1 - i ) / (Real)RMG_ROAD_SWAY_EASE;
		if( ease > 1.0f )
			ease = 1.0f;
		sway[i] = RMG_ROAD_SWAY * fadeCurve( ease ) * 1.4f
			* fractalNoise( m_perm, travelled * RMG_ROAD_SWAY_SCALE + salt, salt * 0.37f, 2 );
		along[i] = travelled;
	}

	/* Noise alone can sit near nought for a whole way, so the way also bows as one arc, to
		whichever side its ground lies lower on. */
	const Real total = along[count - 2] + 1.0f;
	Real bow = RMG_ROAD_BOW_SHARE * total;
	if( bow > RMG_ROAD_BOW_MOST )
		bow = RMG_ROAD_BOW_MOST;
	Real sideGround[2] = { 0.0f, 0.0f };
	for( Int side = 0; side < 2; side++ )
	{
		const Real sign = side == 0 ? 1.0f : -1.0f;
		for( Int i = 1; i < count - 1; i += RMG_ROAD_SMOOTH )
		{
			const Real arc = sign * bow * Sin( PI * along[i] / total );
			const Int arcX = (Int)floorf( pointX[i] + normalX[i] * arc + 0.5f );
			const Int arcY = (Int)floorf( pointY[i] + normalY[i] * arc + 0.5f );
			if( !insidePlayable( arcX, arcY ) )
			{
				sideGround[side] += RMG_ROAD_BOW_BLOCKED;
				continue;
			}
			const Int cell = cellIndex( arcX, arcY );
			sideGround[side] += m_passable[cell] != 0 ? (Real)m_heights[cell] : RMG_ROAD_BOW_BLOCKED;
		}
	}
	const Real bowSign = sideGround[0] < sideGround[1] || ( sideGround[0] == sideGround[1] && ( (Int)salt & 1 ) ) ? 1.0f : -1.0f;
	for( Int i = 1; i < count - 1; i++ )
		sway[i] += bowSign * bow * Sin( PI * along[i] / total );

	const Int window = 2 * RMG_ROAD_SMOOTH;
	for( Int round = 0; round < RMG_ROAD_SWAY_ROUNDS; round++ )
	{
		offset = sway;
		for( Int pass = 0; pass < RMG_ROAD_SMOOTH_PASSES; pass++ )
		{
			for( Int i = 1; i < count - 1; i++ )
			{
				Int reach = window;
				if( reach > i ) reach = i;
				if( reach > count - 1 - i ) reach = count - 1 - i;
				Real sum = 0.0f;
				for( Int j = i - reach; j <= i + reach; j++ )
					sum += offset[j];
				nextOffset[i] = sum / (Real)( 2 * reach + 1 );
			}
			offset.swap( nextOffset );
		}
		Bool dropped = FALSE;
		for( Int i = 1; i < count - 1; i++ )
		{
			const Int laid = cellIndex( (Int)( pointX[i] + 0.5f ), (Int)( pointY[i] + 0.5f ) );
			if( roadMoveFits( laid, pointX[i] + normalX[i] * offset[i], pointY[i] + normalY[i] * offset[i] ) )
				continue;
			for( Int j = max( 0, i - window ); j <= min( count - 1, i + window ); j++ )
				sway[j] = 0.0f;
			dropped = TRUE;
		}
		if( !dropped )
			break;
	}
	for( Int i = 1; i < count - 1; i++ )
	{
		const Int laid = cellIndex( (Int)( pointX[i] + 0.5f ), (Int)( pointY[i] + 0.5f ) );
		Real swayX = pointX[i] + normalX[i] * offset[i];
		Real swayY = pointY[i] + normalY[i] * offset[i];
		if( roadMoveFits( laid, swayX, swayY ) )
		{
			pointX[i] = swayX;
			pointY[i] = swayY;
		}
	}

	std::vector<Int> drawn;
	drawn.push_back( route[0] );
	for( Int i = 1; i < count; i++ )
	{
		Real dx = pointX[i] - pointX[i - 1];
		Real dy = pointY[i] - pointY[i - 1];
		Int steps = (Int)( 2.0f * sqrtf( dx * dx + dy * dy ) ) + 1;
		for( Int k = 1; k <= steps; k++ )
		{
			Real f = (Real)k / (Real)steps;
			Int cell = cellIndex( (Int)( pointX[i - 1] + dx * f + 0.5f ), (Int)( pointY[i - 1] + dy * f + 0.5f ) );
			if( cell != drawn.back() )
				drawn.push_back( cell );
		}
	}
	if( drawn.back() != route[count - 1] )
		drawn.push_back( route[count - 1] );
	route.swap( drawn );
}

/** Whether a way's point may move off the cell the search laid it on: onto open ground, no more
	than RMG_ROAD_SWAY_RISE bytes higher, and onto a crest or ridge no higher than RMG_ROAD_SWAY_CREST
	or than the search's own cell. A sway reaches further than the border band is wide, so a point
	off the playable area does not fit either. */
Bool RMGLayout::roadMoveFits( Int laid, Real movedX, Real movedY ) const
{
	const Int movedCellX = (Int)floorf( movedX + 0.5f );
	const Int movedCellY = (Int)floorf( movedY + 0.5f );
	if( !insidePlayable( movedCellX, movedCellY ) )
		return FALSE;
	const Int moved = cellIndex( movedCellX, movedCellY );
	if( moved == laid )
		return TRUE;
	if( m_passable[moved] == 0 || (Int)m_heights[moved] - (Int)m_heights[laid] > RMG_ROAD_SWAY_RISE )
		return FALSE;
	return crestAt( moved ) <= max( crestAt( laid ), (Int)RMG_ROAD_SWAY_CREST );
}

/** How far a cell stands over the ground RMG_ROAD_CREST_REACH cells off on both sides, along
	whichever of the four lines through it it stands highest on: a hilltop or a ridge reads high
	along every line or across the ridge, a valley floor or a slope reads 0. */
Int RMGLayout::crestAt( Int index ) const
{
	static const Int lineX[4] = { 1, 0, 1, 1 };
	static const Int lineY[4] = { 0, 1, 1, -1 };
	const Int x = index % m_width;
	const Int y = index / m_width;
	const Int here = (Int)m_heights[index];
	Int highest = 0;

	for( Int line = 0; line < 4; line++ )
	{
		Int reach = RMG_ROAD_CREST_REACH;
		if( line >= 2 )
			reach = (RMG_ROAD_CREST_REACH * 5 + 3) / 7;		// the diagonal's cells are 1.4 apart
		Int ax = x + lineX[line] * reach, ay = y + lineY[line] * reach;
		Int bx = x - lineX[line] * reach, by = y - lineY[line] * reach;
		if( ax < 0 || ay < 0 || ax >= m_width || ay >= m_height || bx < 0 || by < 0 || bx >= m_width || by >= m_height )
			continue;
		Int over = min( here - (Int)m_heights[cellIndex( ax, ay )], here - (Int)m_heights[cellIndex( bx, by )] );
		if( over > highest )
			highest = over;
	}
	return highest;
}

/** One step of a plain map's way: its length, its climb, the square of the climb past the gentle
	grade, and the cell's own price, its crest and a slow noise that bends a way over open ground the
	way a road wanders, where eight directions alone rule it into straight runs and 45 degree turns.
	Cliff and water keep their flat prices; the route profile cuts or raises them after. */
Int RMGLayout::roadStepCost( Int from, Int to, Bool diagonal, std::vector<Int>& cellPrice ) const
{
	const Int length = diagonal ? RMG_ROAD_DIAGONAL : RMG_ROAD_LENGTH;
	if( m_passable[to] == 0 )
		return underwaterAtCell( to % m_width, to / m_width ) ? RMG_ROAD_WATER : RMG_ROAD_CLIFF;

	Int climb = (Int)m_heights[to] - (Int)m_heights[from];
	if( climb < 0 )
		climb = -climb;
	Int gentle = (Int)(RMG_ROAD_GENTLE * (Real)length / (Real)RMG_ROAD_LENGTH + 0.5f);
	Int past = climb > gentle ? climb - gentle : 0;

	if( cellPrice[to] < 0 )
	{
		Int over = crestAt( to );
		Int ridge = over > RMG_ROAD_CREST_FREE ? over - RMG_ROAD_CREST_FREE : 0;
		Real playX = (Real)(to % m_width - RMG_BORDER_CELLS);
		Real playY = (Real)(to / m_width - RMG_BORDER_CELLS);
		Real wander = 0.5f + 1.4f * fractalNoise( m_perm, playX * RMG_ROAD_MEANDER_SCALE + 7.0f, playY * RMG_ROAD_MEANDER_SCALE - 29.0f, 2 );
		if( wander < 0.0f ) wander = 0.0f;
		cellPrice[to] = ridge * RMG_ROAD_CREST + (Int)( wander * (Real)RMG_ROAD_MEANDER + 0.5f );
	}

	return length + climb * RMG_ROAD_CLIMB + past * past * RMG_ROAD_STEEP + cellPrice[to];
}

void RMGLayout::applyRouteProfile( const std::vector<Int>& route, Bool recordRamp,
																	 Real protectRadius, Real protectX, Real protectY )
{
	if( route.size() < 2 )
		return;
	m_roads.push_back( route );
	m_roadCarved.push_back( TRUE );

	std::vector<Real> profile;
	routeProfile( route, profile );

	/* Where the cut is deepest is where the route came off one terrace and onto another: that is
		the ramp, and it is the ground worth standing a bunker on. One per route, so a map has as
		many of these as it has carved routes. */
	if( recordRamp )
	{
		Real deepestCut = 6.0f;
		Int rampAt = -1;

		for( UnsignedInt i = 0; i < route.size(); i++ )
		{
			Real cut = fabsf( profile[i] - (Real)m_heights[route[i]] );
			if( cut > deepestCut )
			{
				deepestCut = cut;
				rampAt = (Int)i;
			}
		}

		if( rampAt >= 0 )
		{
			RMGPoint ramp;
			ramp.m_cellX = (Real)(route[rampAt] % m_width - RMG_BORDER_CELLS);
			ramp.m_cellY = (Real)(route[rampAt] / m_width - RMG_BORDER_CELLS);
			m_ramps.push_back( ramp );
		}
	}

	for( UnsignedInt i = 0; i < route.size(); i++ )
		cutTowardLevel( route[i] % m_width, route[i] / m_width, profile[i], FALSE,
										protectRadius, protectX, protectY );
}

/** The level a carved way is cut to along its cells, from the ground under it now: above the water,
	no step steeper than a pass climbs, and on a plain map a road's grade and a cutting through any
	crest it tops. */
void RMGLayout::routeProfile( const std::vector<Int>& route, std::vector<Real>& profile ) const
{
	profile.resize( route.size() );
	for( UnsignedInt i = 0; i < route.size(); i++ )
		profile[i] = (Real)m_heights[route[i]];

	Real waterFloor = m_waterHeight + 2.0f;
	for( UnsignedInt i = 0; i < profile.size(); i++ )
	{
		if( profile[i] < waterFloor )
			profile[i] = waterFloor;
	}

	// a plain map's way climbs a road's grade, and a diagonal step is the longer one
	const Bool plainRoad = !m_style && !m_skeleton;
	std::vector<Real> maxStep( route.size(), RMG_PASS_MAX_STEP );
	for( UnsignedInt i = 1; plainRoad && i < route.size(); i++ )
	{
		Bool diagonal = (route[i] % m_width != route[i - 1] % m_width) && (route[i] / m_width != route[i - 1] / m_width);
		maxStep[i] = diagonal ? RMG_ROAD_MAX_STEP * 1.41421356f : RMG_ROAD_MAX_STEP;
	}

	/* A plain map's road gets its grade by cutting before it fills: a hump is cut down to the grade
		from the low ground either side, and only past RMG_ROAD_CUT_MOST does the pass below raise
		anything. Filled by halves, a dip the way crossed came out as an embankment standing over the
		ground on both sides of it. */
	if( plainRoad )
	{
		std::vector<Real> ground( profile );
		for( Int pass = 0; pass < 2; pass++ )
		{
			for( UnsignedInt i = 1; i < profile.size(); i++ )
			{
				if( profile[i] > profile[i - 1] + maxStep[i] )
					profile[i] = profile[i - 1] + maxStep[i];
			}
			for( Int i = (Int)profile.size() - 2; i >= 0; i-- )
			{
				if( profile[i] > profile[i + 1] + maxStep[i + 1] )
					profile[i] = profile[i + 1] + maxStep[i + 1];
			}
		}
		for( UnsignedInt i = 0; i < profile.size(); i++ )
		{
			if( profile[i] < ground[i] - RMG_ROAD_CUT_MOST )
				profile[i] = ground[i] - RMG_ROAD_CUT_MOST;
			if( profile[i] < waterFloor )
				profile[i] = waterFloor;
		}
	}

	for( Int pass = 0; pass < 2; pass++ )
	{
		for( UnsignedInt i = 1; i < profile.size(); i++ )
		{
			Real limit = profile[i - 1] + maxStep[i];
			if( profile[i] > limit )
				profile[i] = limit;

			limit = profile[i - 1] - maxStep[i];
			if( profile[i] < limit )
				profile[i] = limit;
		}

		for( Int i = (Int)profile.size() - 2; i >= 0; i-- )
		{
			Real limit = profile[i + 1] + maxStep[i + 1];
			if( profile[i] > limit )
				profile[i] = limit;

			limit = profile[i + 1] - maxStep[i + 1];
			if( profile[i] < limit )
				profile[i] = limit;
		}
	}

	/* Where a plain map's way still tops a crest, the saddle it could not go round, it goes through
		a cutting: no cell stands more than the free rise over the higher of the way RMG_ROAD_CREST_REACH
		cells back and on, and none is cut deeper than its banks reach. Both bounds move no faster
		than the profile does, so the cut keeps the grade. A way riding along a ridge, the spur a ramp
		came down or the bank of a pad's cut, is sunk the same way against the ground the same reach
		off to either side of it, and the way either side of the sunk stretch eased down to its grade. */
	if( plainRoad && (Int)profile.size() > 2 * RMG_ROAD_CREST_REACH )
	{
		std::vector<Real> before( profile );
		for( Int i = RMG_ROAD_CREST_REACH; i < (Int)profile.size() - RMG_ROAD_CREST_REACH; i++ )
		{
			Real side = max( before[i - RMG_ROAD_CREST_REACH], before[i + RMG_ROAD_CREST_REACH] );
			const Int x = route[i] % m_width, y = route[i] / m_width;
			Real tangentX = (Real)( route[i + 2] % m_width - route[i - 2] % m_width );
			Real tangentY = (Real)( route[i + 2] / m_width - route[i - 2] / m_width );
			Real length = sqrtf( tangentX * tangentX + tangentY * tangentY );
			if( length > 0.5f )
			{
				Int ax = x + (Int)floorf( -tangentY / length * RMG_ROAD_CREST_REACH + 0.5f );
				Int ay = y + (Int)floorf( tangentX / length * RMG_ROAD_CREST_REACH + 0.5f );
				Int bx = 2 * x - ax, by = 2 * y - ay;
				if( ax >= 0 && ay >= 0 && ax < m_width && ay < m_height && bx >= 0 && by >= 0 && bx < m_width && by < m_height )
					side = min( side, max( (Real)m_heights[cellIndex( ax, ay )], (Real)m_heights[cellIndex( bx, by )] ) );
			}
			Real cap = max( side + (Real)RMG_ROAD_CREST_FREE, before[i] - RMG_ROAD_CUT_MOST );
			if( profile[i] > cap )
				profile[i] = cap;
		}
		for( UnsignedInt i = 1; i < profile.size(); i++ )
		{
			if( profile[i] > profile[i - 1] + maxStep[i] )
				profile[i] = profile[i - 1] + maxStep[i];
		}
		for( Int i = (Int)profile.size() - 2; i >= 0; i-- )
		{
			if( profile[i] > profile[i + 1] + maxStep[i + 1] )
				profile[i] = profile[i + 1] + maxStep[i + 1];
		}
	}

	/* Laid again by relayRoads, a cell under something standing cannot move, so it is held at its
		ground and the way round it graded to it: cut alone, the way kept a step at the edge of a pad
		it was not allowed into. */
	if( m_carveKeep )
	{
		std::vector<char> held( route.size(), 0 );
		for( UnsignedInt i = 0; i < route.size(); i++ )
		{
			held[i] = (*m_carveKeep)[route[i]] != 0;
			if( held[i] )
				profile[i] = (Real)m_heights[route[i]];
		}
		for( Int pass = 0; pass < 2; pass++ )
		{
			for( UnsignedInt i = 1; i < profile.size(); i++ )
			{
				if( held[i] )
					continue;
				if( profile[i] > profile[i - 1] + maxStep[i] )
					profile[i] = profile[i - 1] + maxStep[i];
				if( profile[i] < profile[i - 1] - maxStep[i] )
					profile[i] = profile[i - 1] - maxStep[i];
			}
			for( Int i = (Int)profile.size() - 2; i >= 0; i-- )
			{
				if( held[i] )
					continue;
				if( profile[i] > profile[i + 1] + maxStep[i + 1] )
					profile[i] = profile[i + 1] + maxStep[i + 1];
				if( profile[i] < profile[i + 1] - maxStep[i + 1] )
					profile[i] = profile[i + 1] - maxStep[i + 1];
			}
		}
	}
}

/** A plain map's carved ways laid again over the finished ground. The pads, towns, bases and
	repairs after a way was cut move ground across it, and left a bump or a dip in it that the
	way's own profile never had. Ground under or beside anything standing (the mask settleSlopes
	builds) is not moved, so no building ends up in the air or buried. */
void RMGLayout::relayRoads( void )
{
	std::vector<char> standing;
	markStandingGround( standing );
	m_carveKeep = &standing;
	std::vector<Real> profile;
	for( UnsignedInt r = 0; r < m_roads.size(); r++ )
	{
		if( !m_roadCarved[r] )
			continue;
		routeProfile( m_roads[r], profile );
		for( UnsignedInt i = 0; i < m_roads[r].size(); i++ )
		{
			Int cell = m_roads[r][i];
			if( fabsf( profile[i] - (Real)m_heights[cell] ) >= 1.0f )
				cutTowardLevel( cell % m_width, cell / m_width, profile[i], TRUE, 0.0f, 0.0f, 0.0f );
		}
	}
	m_carveKeep = NULL;
}

/** A template map's finished ground through the plain map's last two passes: every road laid at a
	road's grade with a cutting through a crest it tops (routeProfile and cutTowardLevel, as relayRoads
	lays a plain map's ways), then with settle settleSlopes and the roads laid once more, since easing a
	cutting's banks spills them back over the way. keep is markStandingGround's mask for the template's
	ground, walls the cells its exemplar painted as cliff. Only the members those passes read are set,
	so a layout used this way builds nothing else. */
void RMGLayout::finishTemplateGround( Int playableCells, std::vector<Int>& heights, Int width, Int height,
	const std::vector< std::vector<Int> >& roads, const std::vector<char>& keep, const std::vector<char>& walls, Bool settle )
{
	m_settings = RandomMapSettings();
	m_settings.m_playableCells = playableCells;
	m_style = NULL;
	m_skeleton = FALSE;
	m_lockCarves = FALSE;
	m_width = width;
	m_height = height;
	m_waterHeight = 0.0f;			// the template's water is in keep, every level of it
	m_heights.resize( heights.size() );
	for( size_t i = 0; i < heights.size(); i++ )
		m_heights[i] = (UnsignedByte)( heights[i] < 0 ? 0 : heights[i] > 255 ? 255 : heights[i] );
	m_roads = roads;
	m_roadCarved.assign( roads.size(), TRUE );
	std::vector<Real> profile;
	for( Int pass = 0; pass < ( settle ? 2 : 1 ); pass++ )
	{
		if( pass > 0 )
		{
			std::vector<char> settleKeep( keep );
			settleSlopesKeeping( settleKeep, &walls, RMG_SETTLE_TEMPLATE_PASSES, RMG_SETTLE_TEMPLATE_MOST );
		}
		m_carveKeep = &keep;
		for( UnsignedInt r = 0; r < m_roads.size(); r++ )
		{
			routeProfile( m_roads[r], profile );
			for( UnsignedInt i = 0; i < m_roads[r].size(); i++ )
			{
				Int cell = m_roads[r][i];
				if( fabsf( profile[i] - (Real)m_heights[cell] ) >= 1.0f )
					cutTowardLevel( cell % m_width, cell / m_width, profile[i], FALSE, 0.0f, 0.0f, 0.0f );
			}
		}
		m_carveKeep = NULL;
	}
	for( size_t i = 0; i < heights.size(); i++ )
		heights[i] = m_heights[i];
}

void rmgFinishTemplateGround( Int playableCells, std::vector<Int>& heights, Int width, Int height,
	const std::vector< std::vector<Int> >& roads, const std::vector<char>& keep, const std::vector<char>& walls, Bool settle )
{
	RMGLayout layout;
	layout.finishTemplateGround( playableCells, heights, width, height, roads, keep, walls, settle );
}

/** Earthwork round one cell of a route, and no more of it than the route needs. The ground near the
	route is only moved where it stands further from the route's level than a bank of fixed slope
	allows, so a road over gentle ground leaves it as it was, and a ramp through a terrace step is cut
	as a trough whose sides lean back. Levelling a strip to the route's height did the opposite: on
	every hillside the strip ran across, it left a flat shelf with a rim, and a 4-neighbour route on
	open ground is a dead straight line, so each one drew a ruled seam across the map. */
void RMGLayout::cutTowardLevel( Int x, Int y, Real level, Bool skipLake,
																Real protectRadius, Real protectX, Real protectY )
{
	const Int reach = RMG_PASS_HALF_WIDTH + RMG_PASS_BANK_CELLS;

	for( Int dy = -reach; dy <= reach; dy++ )
	{
		for( Int dx = -reach; dx <= reach; dx++ )
		{
			Int nx = x + dx;
			Int ny = y + dy;
			if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
				continue;
			// a repair's roadway is kept free of whatever is placed after it (build)
			if( m_lockCarves && dx * dx + dy * dy <= RMG_PASS_HALF_WIDTH * RMG_PASS_HALF_WIDTH )
			{
				if( m_routeLock.size() != m_heights.size() )
					m_routeLock.assign( m_heights.size(), 0 );
				m_routeLock[cellIndex( nx, ny )] = 1;
			}
			if( skipLake && m_inLake[cellIndex( nx, ny )] )
				continue;
			if( m_carveKeep && (*m_carveKeep)[cellIndex( nx, ny )] )
				continue;

			if( protectRadius > 0.0f )
			{
				Real px = (Real)nx - protectX;
				Real py = (Real)ny - protectY;
				if( sqrtf( px * px + py * py ) < protectRadius )
					continue;
			}

			Real distance = sqrtf( (Real)(dx * dx + dy * dy) );
			if( distance > (Real)reach )
				continue;

			// Gentle across the roadway itself, steeper on the banks, both under the step a unit climbs.
			Real allowance = distance * RMG_PASS_ROAD_SLOPE;
			if( distance > (Real)RMG_PASS_HALF_WIDTH )
				allowance = (Real)RMG_PASS_HALF_WIDTH * RMG_PASS_ROAD_SLOPE
					+ (distance - (Real)RMG_PASS_HALF_WIDTH) * RMG_PASS_BANK_SLOPE;

			Real ground = (Real)m_heights[cellIndex( nx, ny )];
			Real h = ground;
			if( h > level + allowance )
				h = level + allowance;
			if( h < level - allowance )
				h = level - allowance;
			if( h == ground )
				continue;

			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;
			m_heights[cellIndex( nx, ny )] = (UnsignedByte)(h + 0.5f);
		}
	}
}

/** The ramp network. Terraced ground is a stack of plateaus with cliffs between them, so a map
	that cuts nothing is a map where half the players cannot reach the other half. Every start is
	joined to the next one round the list, which puts a ramp wherever a route has to change layer;
	then the flood fill says who is still cut off, and a pass is cut to each of those in turn.
	The ring is what makes a map rather than a corridor: the shortest way between two players is
	usually not the ramp either of them would use to reach a third. */
void RMGLayout::connectStarts( void )
{
	buildPassability();

	if( m_starts.size() > 2 )
	{
		for( UnsignedInt i = 0; i < m_starts.size(); i++ )
		{
			carvePass( (Int)i, (Int)((i + 1) % m_starts.size()) );
			flattenBases();
			buildPassability();
		}
	}

	for( Int attempt = 0; attempt < RMG_PASS_ATTEMPTS; attempt++ )
	{
		buildPassability();

		m_visited.assign( m_width * m_height, 0 );

		Int startX = (Int)(m_starts[0].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int startY = (Int)(m_starts[0].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		Int startIndex = startY * m_width + startX;

		std::vector<Int> stack;
		m_visited[startIndex] = 1;
		stack.push_back( startIndex );

		while( !stack.empty() )
		{
			Int index = stack.back();
			stack.pop_back();

			Int x = index % m_width;
			Int y = index / m_width;

			static const Int offsetX[4] = { 1, -1, 0, 0 };
			static const Int offsetY[4] = { 0, 0, 1, -1 };

			for( Int i = 0; i < 4; i++ )
			{
				Int nx = x + offsetX[i];
				Int ny = y + offsetY[i];
				if( nx < 0 || ny < 0 || nx >= m_width - 1 || ny >= m_height - 1 )
					continue;

				Int next = ny * m_width + nx;
				if( m_visited[next] || m_passable[next] == 0 )
					continue;

				m_visited[next] = 1;
				stack.push_back( next );
			}
		}

		Int unreachable = -1;
		for( UnsignedInt i = 1; i < m_starts.size(); i++ )
		{
			Int x = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
			Int y = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
			if( !m_visited[y * m_width + x] )
			{
				unreachable = (Int)i;
				break;
			}
		}

		if( unreachable < 0 )
			return;

		if( !carvePass( 0, unreachable ) )
			break;

		// The route runs into the base at either end of it, so the discs are laid flat again
		// rather than left with a ramp cut across the ground somebody has to build on.
		flattenBases();
	}

	// ensurePlayability tries again on the finished ground, and build flags the map if that fails too
	DEBUG_LOG(( "random map: seed %d - connectStarts used its %d passes or found no route, a start may be cut off\n",
		m_settings.m_seed, RMG_PASS_ATTEMPTS ));
	buildPassability();
}

/** Cells from the outermost any lobe's foot can reach on this point's bearing from its middle,
	negative inside it. Measured along the ray, which on an ellipse no longer than RMG_MASSIF_STRETCH
	is near enough the distance that keeps a base's ring off the slope. */
Real RMGLayout::massifClearance( Real cellX, Real cellY ) const
{
	Real nearest = 1.0e9f;
	for( UnsignedInt l = 0; l < m_massifLobes.size(); l++ )
	{
		const RMGMassifLobe& lobe = m_massifLobes[l];
		Real dx = cellX - lobe.m_cellX;
		Real dy = cellY - lobe.m_cellY;
		Real a = dx * lobe.m_cos + dy * lobe.m_sin;
		Real b = dy * lobe.m_cos - dx * lobe.m_sin;
		Real na = a / lobe.m_major;
		Real nb = b / lobe.m_minor;
		Real q = sqrtf( na * na + nb * nb );
		Real slopeOut = 0.5f * ( ( RMG_MASSIF_SLOPE_RIM < 0.5f * lobe.m_minor ? RMG_MASSIF_SLOPE_RIM : 0.5f * lobe.m_minor )
			- RMG_MASSIF_RIM );
		if( slopeOut < 0.0f )
			slopeOut = 0.0f;
		Real clearance = -lobe.m_minor;
		if( q >= 0.001f )
		{
			Real r = sqrtf( a * a + b * b );
			Real reach = ( 1.0f + RMG_MASSIF_WOBBLE ) * rmgLobeShape( lobe, na / q, nb / q );
			clearance = r * ( 1.0f - reach / q ) - slopeOut;
		}
		if( clearance < nearest )
			nearest = clearance;
	}
	return nearest;
}

/** A ramp up the massif for each base, from the lobe nearest it, so every seat has its own way onto
	the high ground and about the same climb as the next one, and one more for a lobe no base is
	nearest. The route search would find a ramp too, but only for a pair it had to join, and it joins
	them round the foot of the massif rather than over it, which leaves the top as a table nobody can
	reach. A ramp leaves on the base's bearing from the lobe's middle or up to RMG_MASSIF_ROAD_SWING
	steps of eight degrees off it, where the rim is widest: on a stretch where the rim is a slope
	already the road is graded into the slope rather than cut through a cliff as a notch. */
/** How far a massif ramp's centre line stands aside from its bearing at t, 0 at the top and 1 at the
	foot: nothing at the rim, then a swing that grows with the square, carried straight on past the
	foot where the road hands back to the ground. */
static Real rmgRampSwing( Real bend, Real t )
{
	if( t <= 0.0f )
		return 0.0f;
	if( t <= 1.0f )
		return bend * t * t;
	return bend * ( 2.0f * t - 1.0f );
}

void RMGLayout::carveMassifRamps( void )
{
	struct RMGMassifRamp
	{
		UnsignedInt m_lobe;
		UnsignedInt m_start;
	};
	std::vector<RMGMassifRamp> planned;
	std::vector<char> served( m_massifLobes.size(), 0 );
	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Real bestInside = -1.0e9f;
		UnsignedInt bestLobe = 0;
		for( UnsignedInt l = 0; l < m_massifLobes.size(); l++ )
		{
			Real rim;
			Real inside = massifRim( m_massifLobes[l], m_starts[i].m_cellX, m_starts[i].m_cellY, &rim );
			if( inside > bestInside )
			{
				bestInside = inside;
				bestLobe = l;
			}
		}
		RMGMassifRamp ramp;
		ramp.m_lobe = bestLobe;
		ramp.m_start = i;
		planned.push_back( ramp );
		served[bestLobe] = 1;
	}
	for( UnsignedInt l = 0; l < m_massifLobes.size(); l++ )
	{
		if( served[l] || m_starts.empty() )
			continue;
		Real bestInside = -1.0e9f;
		UnsignedInt bestStart = 0;
		for( UnsignedInt i = 0; i < m_starts.size(); i++ )
		{
			Real rim;
			Real inside = massifRim( m_massifLobes[l], m_starts[i].m_cellX, m_starts[i].m_cellY, &rim );
			if( inside > bestInside )
			{
				bestInside = inside;
				bestStart = i;
			}
		}
		RMGMassifRamp ramp;
		ramp.m_lobe = l;
		ramp.m_start = bestStart;
		planned.push_back( ramp );
	}

	std::vector<UnsignedInt> carvedLobe;
	std::vector<Real> carvedX, carvedY;
	for( UnsignedInt p = 0; p < planned.size(); p++ )
	{
		const RMGMassifLobe& lobe = m_massifLobes[planned[p].m_lobe];
		const UnsignedInt i = planned[p].m_start;
		const Real centreX = lobe.m_cellX;
		const Real centreY = lobe.m_cellY;
		Real dx = m_starts[i].m_cellX - centreX;
		Real dy = m_starts[i].m_cellY - centreY;
		Real length = sqrtf( dx * dx + dy * dy );
		if( length < 1.0f )
			continue;
		const Real bearing = ATan2( dy, dx );
		const Real farthest = 2.0f * lobe.m_major + 40.0f;

		Real ux = dx / length;
		Real uy = dy / length;
		Real outer = 0.0f;
		Real inner = 0.0f;
		Real bestScore = -1.0e9f;
		for( Int k = -RMG_MASSIF_ROAD_SWING; k <= RMG_MASSIF_ROAD_SWING; k++ )
		{
			const Real angle = bearing + (Real)k * ( PI / 22.5f );
			const Real cx = Cos( angle );
			const Real cy = Sin( angle );
			Real topAt = 0.0f;
			Real footAt = farthest;
			for( Real t = 0.0f; t < farthest; t += 1.0f )
			{
				Real rim;
				Real inside = massifRim( lobe, centreX + cx * t, centreY + cy * t, &rim );
				if( inside >= rim )
					topAt = t;
				if( inside <= 0.0f )
				{
					footAt = t;
					break;
				}
			}
			Real score = footAt - topAt - 1.5f * (Real)( k < 0 ? -k : k );
			if( score > bestScore )
			{
				bestScore = score;
				ux = cx;
				uy = cy;
				outer = footAt;
				inner = topAt;
			}
		}

		/* The road leaves the rim square to it and swings toward the base it serves as it comes down,
			by RMG_MASSIF_ROAD_BEND of its length at the foot, so it reads as a road let down the slope
			and not a spoke ruled out of the table. */
		/* Two bases side by side pick nearly the same bearing up the same lobe, and two ramps laid
			over each other cross in a knot of banks. The second uses the first. */
		Bool shared = FALSE;
		for( UnsignedInt c = 0; c < carvedLobe.size() && !shared; c++ )
			shared = carvedLobe[c] == planned[p].m_lobe && carvedX[c] * ux + carvedY[c] * uy > RMG_MASSIF_ROAD_APART;
		if( shared )
			continue;
		carvedLobe.push_back( planned[p].m_lobe );
		carvedX.push_back( ux );
		carvedY.push_back( uy );

		const Real normalX = -uy;
		const Real normalY = ux;
		Real towardBase = ( m_starts[i].m_cellX - centreX ) * normalX + ( m_starts[i].m_cellY - centreY ) * normalY;
		const Real bendSide = ( towardBase >= 0.0f ) ? RMG_MASSIF_ROAD_BEND : -RMG_MASSIF_ROAD_BEND;

		// Out far enough that the climb is a slope a tank takes, and no nearer any base than its ring.
		Real foot = outer + 12.0f;
		Real top = inner - 6.0f;
		if( top < 2.0f )
			top = 2.0f;
		for( ; foot > top; foot -= 1.0f )
		{
			Bool clear = TRUE;
			for( UnsignedInt s = 0; s < m_starts.size() && clear; s++ )
			{
				Real fx = centreX + ux * foot + normalX * bendSide * ( foot - top ) - m_starts[s].m_cellX;
				Real fy = centreY + uy * foot + normalY * bendSide * ( foot - top ) - m_starts[s].m_cellY;
				clear = fx * fx + fy * fy >= RMG_BLEND_RADIUS * RMG_BLEND_RADIUS;
			}
			if( clear )
				break;
		}
		if( foot <= top )
			continue;
		const Real bend = bendSide * ( foot - top );

		Real footX = centreX + ux * foot + normalX * bend + (Real)RMG_BORDER_CELLS;
		Real footY = centreY + uy * foot + normalY * bend + (Real)RMG_BORDER_CELLS;
		Real topX = centreX + ux * top + (Real)RMG_BORDER_CELLS;
		Real topY = centreY + uy * top + (Real)RMG_BORDER_CELLS;

		/* A graded road rather than a corridor cut to one width: the climb eases in at both ends,
			the road opens out towards the foot the way a spur of spoil would, and its sides blend
			into the ground over several cells. A narrow cut with steep banks read from above as a
			spike in the cliff, half causeway outside and half notch inside. */
		Real span = foot - top;

		/* The road's level along its centre line is the ground's own profile with the cliff graded
			out of it: two thirds of it the highest line under the ground that climbs no faster than
			RMG_MASSIF_ROAD_GRADE, a third the lowest line over it, so most of the climb is a cutting
			in the table and only a short spur stands out on the plain. Where the ground is gentler than the
			grade both lines are the ground, so the road only changes the cliff band. It used to run
			one eased line from a height read on the table to one read at the foot, and that line
			stayed level for a dozen cells past the rim while the ground beside it rolled, which drew
			every ramp as a straight flat strip out of the massif. */
		Real profileFrom = top - 0.25f * span - 1.0f;
		Int profileCount = (Int)( 1.5f * span ) + 4;
		std::vector<Real> ground( profileCount );
		for( Int s = 0; s < profileCount; s++ )
		{
			Real along = profileFrom + (Real)s;
			Real aside = rmgRampSwing( bend, ( along - top ) / span );
			Int cx = (Int)( centreX + ux * along + normalX * aside + (Real)RMG_BORDER_CELLS + 0.5f );
			Int cy = (Int)( centreY + uy * along + normalY * aside + (Real)RMG_BORDER_CELLS + 0.5f );
			if( cx < 0 ) cx = 0;
			if( cy < 0 ) cy = 0;
			if( cx >= m_width ) cx = m_width - 1;
			if( cy >= m_height ) cy = m_height - 1;
			Real g = (Real)m_heights[cellIndex( cx, cy )];
			if( g < m_waterHeight + 2.0f )
				g = m_waterHeight + 2.0f;
			ground[s] = g;
		}
		std::vector<Real> graded( profileCount );
		for( Int s = 0; s < profileCount; s++ )
		{
			Real under = 1.0e9f;
			Real over = -1.0e9f;
			for( Int j = 0; j < profileCount; j++ )
			{
				Real reach = RMG_MASSIF_ROAD_GRADE * (Real)( s > j ? s - j : j - s );
				if( ground[j] + reach < under )
					under = ground[j] + reach;
				if( ground[j] - reach > over )
					over = ground[j] - reach;
			}
			graded[s] = ( 2.0f * under + over ) / 3.0f;
		}
		// A short running mean takes the crease off both ends of the climb without steepening it.
		/* The ground under the centre line gets the same mean before the road is laid as an offset
			over it: read cell by cell along a slanted line, it stepped wherever the line crossed into
			the next row, and every step came out across the road as a stripe of one-cell bumps. */
		std::vector<Real> level( profileCount );
		std::vector<Real> groundMean( profileCount );
		for( Int s = 0; s < profileCount; s++ )
		{
			Real sum = 0.0f;
			Real groundSum = 0.0f;
			Int count = 0;
			for( Int j = s - 2; j <= s + 2; j++ )
			{
				if( j < 0 || j >= profileCount )
					continue;
				sum += graded[j];
				groundSum += ground[j];
				count++;
			}
			level[s] = sum / (Real)count;
			groundMean[s] = groundSum / (Real)count;
		}
		Int reach = (Int)( RMG_MASSIF_ROAD_FOOT + RMG_MASSIF_ROAD_WIDEST + 0.25f * span ) + 1;
		Int loX = (Int)( footX < topX ? footX : topX ) - reach;
		Int hiX = (Int)( footX > topX ? footX : topX ) + reach;
		Int loY = (Int)( footY < topY ? footY : topY ) - reach;
		Int hiY = (Int)( footY > topY ? footY : topY ) + reach;
		for( Int y = loY; y <= hiY; y++ )
		{
			for( Int x = loX; x <= hiX; x++ )
			{
				if( x < 0 || y < 0 || x >= m_width || y >= m_height )
					continue;
				if( m_inLake[cellIndex( x, y )] )
					continue;
				Bool onBase = FALSE;
				for( UnsignedInt s = 0; s < m_starts.size() && !onBase; s++ )
				{
					Real bx = (Real)x - m_starts[s].m_cellX - (Real)RMG_BORDER_CELLS;
					Real by = (Real)y - m_starts[s].m_cellY - (Real)RMG_BORDER_CELLS;
					onBase = bx * bx + by * by < RMG_BLEND_RADIUS * RMG_BLEND_RADIUS;
				}
				if( onBase )
					continue;

				Real px = (Real)x - (Real)RMG_BORDER_CELLS - centreX;
				Real py = (Real)y - (Real)RMG_BORDER_CELLS - centreY;
				Real along = px * ux + py * uy;
				Real t = ( along - top ) / span;		// 0 at the top, 1 at the foot
				if( t < -0.25f || t > 1.25f )
					continue;
				Real across = fabsf( px * normalX + py * normalY - rmgRampSwing( bend, t ) );
				Real tc = t < 0.0f ? 0.0f : ( t > 1.0f ? 1.0f : t );

				Real at = along - profileFrom;
				Int s0 = (Int)at;
				if( s0 < 0 ) s0 = 0;
				if( s0 > profileCount - 2 ) s0 = profileCount - 2;
				Real f = at - (Real)s0;
				if( f < 0.0f ) f = 0.0f;
				if( f > 1.0f ) f = 1.0f;
				Real roadLevel = lerpReal( level[s0], level[s0 + 1], f );
				Real centreGround = lerpReal( groundMean[s0], groundMean[s0 + 1], f );
				Real cut = fabsf( roadLevel - lerpReal( ground[s0], ground[s0 + 1], f ) );

				// The deeper the cut or the higher the fill, the wider its sides, so a bank stands at
				// one slope the whole way along instead of a wall where the road meets the rim.
				Real bank = cut / RMG_MASSIF_ROAD_SIDE;
				if( bank > RMG_MASSIF_ROAD_WIDEST )
					bank = RMG_MASSIF_ROAD_WIDEST;
				if( bank < RMG_MASSIF_ROAD_BANK )
					bank = RMG_MASSIF_ROAD_BANK;
				Real half = lerpReal( RMG_MASSIF_ROAD_TOP, RMG_MASSIF_ROAD_FOOT, tc );
				Real weight = 1.0f;
				if( across > half )
					weight = 1.0f - fadeCurve( ( across - half ) / bank < 1.0f ? ( across - half ) / bank : 1.0f );
				// Past either end the road hands back to the ground over a quarter of its length.
				if( t < 0.0f )
					weight *= 1.0f - fadeCurve( -t * 4.0f );
				else if( t > 1.0f )
					weight *= 1.0f - fadeCurve( ( t - 1.0f ) * 4.0f );
				if( weight <= 0.0f )
					continue;

				/* Where the road only follows the ground it is laid as an offset over the ground
					beside it, so the strip rolls with its sides. Where it cuts or fills the cliff it is
					laid level across, so a rim crossing the road at a slant leaves no step in it. A cell
					of the roadway standing well off the centre line's ground is on such a rim even where
					the centre line is not, and is laid level too: as an offset it kept the rim's lip as a
					step. Banks and the ends past the ramp keep the offset, or every bump on them was
					pulled part way to the road and drew as a pit. */
				Real here = (Real)m_heights[cellIndex( x, y )];
				Real offCentre = ( t >= 0.0f && t <= 1.0f && across <= half ) ? fabsf( here - centreGround ) : 0.0f;
				Real work = max( cut, offCentre ) / 6.0f;
				if( work > 1.0f ) work = 1.0f;
				Real target = lerpReal( here + roadLevel - centreGround, roadLevel, work );
				Real h = lerpReal( here, target, weight );
				if( h < 1.0f ) h = 1.0f;
				if( h > 254.0f ) h = 254.0f;
				m_heights[cellIndex( x, y )] = (UnsignedByte)( h + 0.5f );
			}
		}

		RMGPoint ramp;
		ramp.m_cellX = centreX + ux * ( foot + top ) * 0.5f + normalX * rmgRampSwing( bend, 0.5f );
		ramp.m_cellY = centreY + uy * ( foot + top ) * 0.5f + normalY * rmgRampSwing( bend, 0.5f );
		m_ramps.push_back( ramp );

		std::vector<Int> cells;
		for( Real along = top; along <= foot; along += 0.5f )
		{
			Real aside = rmgRampSwing( bend, ( along - top ) / span );
			Int cx = (Int)( centreX + ux * along + normalX * aside + 0.5f ) + RMG_BORDER_CELLS;
			Int cy = (Int)( centreY + uy * along + normalY * aside + 0.5f ) + RMG_BORDER_CELLS;
			if( cx < 0 || cy < 0 || cx >= m_width || cy >= m_height )
				continue;
			if( cells.empty() || cells.back() != cellIndex( cx, cy ) )
				cells.push_back( cellIndex( cx, cy ) );
		}
		if( cells.size() >= 2 )
		{
			m_roads.push_back( cells );
			m_roadCarved.push_back( FALSE );
		}
	}

	buildPassability();
}

/// Height bytes between a cell's highest and lowest corner, 0 off the height field.
Int RMGLayout::spanBytes( Int x, Int y ) const
{
	if( x < 0 || y < 0 || x + 1 >= m_width || y + 1 >= m_height )
		return 0;
	Int a = m_heights[cellIndex( x, y )];
	Int b = m_heights[cellIndex( x + 1, y )];
	Int c = m_heights[cellIndex( x, y + 1 )];
	Int d = m_heights[cellIndex( x + 1, y + 1 )];
	Int hi = a > b ? a : b;
	if( c > hi ) hi = c;
	if( d > hi ) hi = d;
	Int lo = a < b ? a : b;
	if( c < lo ) lo = c;
	if( d < lo ) lo = d;
	return hi - lo;
}

/** The ground the finishing passes leave alone, as 1, or RMG_SETTLE_KEEP_SHORE for the shore: under
	and round whatever stands on it by now (trees, rocks, waypoints and road points apart), the
	water's edge, and every base's level. */
void RMGLayout::markStandingGround( std::vector<char>& keep ) const
{
	keep.assign( m_width * m_height, 0 );
	for( UnsignedInt o = 0; o < m_objects.size(); o++ )
	{
		const RMGObject& object = m_objects[o];
		if( object.m_templateName.startsWith( "Tree" ) || object.m_templateName.startsWith( "Rock" )
			|| object.m_templateName.startsWith( "*Waypoints" )
			|| ( object.m_flags & ( RMG_FLAG_ROAD_POINT1 | RMG_FLAG_ROAD_POINT2 ) ) )
			continue;
		Int cx = (Int)( object.m_worldX / MAP_XY_FACTOR + 0.5f ) + RMG_BORDER_CELLS;
		Int cy = (Int)( object.m_worldY / MAP_XY_FACTOR + 0.5f ) + RMG_BORDER_CELLS;
		for( Int dy = -RMG_SETTLE_OBJECT; dy <= RMG_SETTLE_OBJECT; dy++ )
		{
			for( Int dx = -RMG_SETTLE_OBJECT; dx <= RMG_SETTLE_OBJECT; dx++ )
			{
				Int x = cx + dx;
				Int y = cy + dy;
				if( x >= 0 && y >= 0 && x < m_width && y < m_height )
					keep[cellIndex( x, y )] = 1;
			}
		}
	}
	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			if( keep[cellIndex( x, y )] )
				continue;
			Real shore;
			insideLake( (Real)( x - RMG_BORDER_CELLS ), (Real)( y - RMG_BORDER_CELLS ), &shore );
			if( shore < RMG_SETTLE_SHORE )
			{
				keep[cellIndex( x, y )] = RMG_SETTLE_KEEP_SHORE;
				continue;
			}
			for( UnsignedInt s = 0; s < m_starts.size(); s++ )
			{
				Real sx = (Real)( x - RMG_BORDER_CELLS ) - m_starts[s].m_cellX;
				Real sy = (Real)( y - RMG_BORDER_CELLS ) - m_starts[s].m_cellY;
				if( sx * sx + sy * sy < RMG_SETTLE_BASE * RMG_SETTLE_BASE )
				{
					keep[cellIndex( x, y )] = 1;
					break;
				}
			}
		}
	}
}

/** No ground a unit walks that reads as a cliff. The engine calls a cell a cliff when its corners
	span more than 9.8 world units (WorldHeightMapData::setCellCliffFlagFromHeights), 16 height bytes;
	a cell of 10 to 15 bytes is walked as open ground and drawn as a wall half the height of one. Each
	such cell is made one or the other: beside a cliff its corners are pushed apart until it is part
	of the cliff, anywhere else pulled together until it is gentle. A carved way, the bases' level,
	whatever stands on the ground and the shore keep their ground, and no corner moves more than
	RMG_SETTLE_MOST bytes in all, so a run of pushes cannot raise a spike. */
void RMGLayout::settleSlopes( void )
{
	std::vector<char> keep;
	markStandingGround( keep );
	settleSlopesKeeping( keep, NULL, RMG_SETTLE_PASSES, RMG_SETTLE_MOST );
}

/** settleSlopes on ground the caller marked: keep as markStandingGround marks it, and walls, when
	given, the cells that steepen into a cliff even with none beside them (a template map's exemplar
	painted them as cliff, and a wall drawn as a slope is the thing to avoid); passes sweeps, and no
	corner moved more than most bytes in all. */
void RMGLayout::settleSlopesKeeping( std::vector<char>& keep, const std::vector<char> *walls, Int passes, Int most )
{
	const Int cellCount = m_width * m_height;
	// the roadway and a cell past it; its banks are ground like any other
	const Int roadReach = RMG_PASS_HALF_WIDTH + 1;
	for( UnsignedInt r = 0; r < m_roads.size(); r++ )
	{
		for( UnsignedInt k = 0; k < m_roads[r].size(); k++ )
		{
			Int cx = m_roads[r][k] % m_width;
			Int cy = m_roads[r][k] / m_width;
			for( Int dy = -roadReach; dy <= roadReach; dy++ )
			{
				for( Int dx = -roadReach; dx <= roadReach; dx++ )
				{
					Int x = cx + dx;
					Int y = cy + dy;
					if( dx * dx + dy * dy <= roadReach * roadReach && x >= 0 && y >= 0 && x < m_width && y < m_height
						&& !keep[cellIndex( x, y )] )
						keep[cellIndex( x, y )] = 1;
				}
			}
		}
	}

	std::vector<Int> moved( cellCount, 0 );
	for( Int pass = 0; pass < passes; pass++ )
	{
		Int changed = 0;
		for( Int y = 0; y < m_height - 1; y++ )
		{
			for( Int x = 0; x < m_width - 1; x++ )
			{
				if( keep[cellIndex( x, y )] || !insidePlayable( x, y ) )
					continue;
				Int span = spanBytes( x, y );
				if( span < RMG_STEEP_BYTES || span >= RMG_CLIFF_BYTES )
					continue;

				Bool cliffBeside = FALSE;
				for( Int dy = -1; dy <= 1 && !cliffBeside; dy++ )
					for( Int dx = -1; dx <= 1 && !cliffBeside; dx++ )
						cliffBeside = spanBytes( x + dx, y + dy ) >= RMG_CLIFF_BYTES;

				Int corner[4] = { cellIndex( x, y ), cellIndex( x + 1, y ), cellIndex( x, y + 1 ), cellIndex( x + 1, y + 1 ) };
				Int hi = 0, lo = 255;
				for( Int k = 0; k < 4; k++ )
				{
					Int h = m_heights[corner[k]];
					if( h > hi ) hi = h;
					if( h < lo ) lo = h;
				}
				// only a cell already against a cliff joins it: one steepened in open ground is a cliff
				// standing alone, which walled a massif's crown off on a small map
				const Bool steepen = cliffBeside || ( walls && (*walls)[cellIndex( x, y )] );
				Int gap = steepen ? RMG_CLIFF_BYTES - span : span - ( RMG_STEEP_BYTES - 1 );
				Int highShare = ( gap + 1 ) / 2;
				Int lowShare = gap - highShare;
				for( Int k = 0; k < 4; k++ )
				{
					Bool high = 2 * (Int)m_heights[corner[k]] >= hi + lo;
					// steepened, the high corners go up and the low ones down; softened, the other way
					Int step = high ? highShare : lowShare;
					Int delta = ( high == steepen ) ? step : -step;
					Int total = moved[corner[k]] + delta;
					if( total > most ) delta = most - moved[corner[k]];
					if( total < -most ) delta = -most - moved[corner[k]];
					Int h = (Int)m_heights[corner[k]] + delta;
					if( h < 1 ) h = 1;
					if( h > 254 ) h = 254;
					moved[corner[k]] += h - (Int)m_heights[corner[k]];
					m_heights[corner[k]] = (UnsignedByte)h;
				}
				changed++;
			}
		}
		if( changed == 0 )
			break;
	}

	/* A corner left standing clear of all four neighbours, up or down, goes back among them, on a
		road or by a building as well: nothing wants a one-corner spike, and a level pad has none. */
	for( Int y = 1; y < m_height - 1; y++ )
	{
		for( Int x = 1; x < m_width - 1; x++ )
		{
			if( keep[cellIndex( x, y )] == RMG_SETTLE_KEEP_SHORE || !insidePlayable( x, y ) )
				continue;
			Int n0 = m_heights[cellIndex( x - 1, y )];
			Int n1 = m_heights[cellIndex( x + 1, y )];
			Int n2 = m_heights[cellIndex( x, y - 1 )];
			Int n3 = m_heights[cellIndex( x, y + 1 )];
			Int top = n0 > n1 ? n0 : n1;
			if( n2 > top ) top = n2;
			if( n3 > top ) top = n3;
			Int bottom = n0 < n1 ? n0 : n1;
			if( n2 < bottom ) bottom = n2;
			if( n3 < bottom ) bottom = n3;
			Int h = m_heights[cellIndex( x, y )];
			if( h > top + RMG_SETTLE_SPIKE )
				m_heights[cellIndex( x, y )] = (UnsignedByte)top;
			else if( h < bottom - RMG_SETTLE_SPIKE )
				m_heights[cellIndex( x, y )] = (UnsignedByte)bottom;
		}
	}
}

/// The map cells along a straight way from one playable point to another, for roadCells.
void RMGLayout::recordRoad( Real fromX, Real fromY, Real toX, Real toY )
{
	Real dx = toX - fromX;
	Real dy = toY - fromY;
	Int steps = (Int)sqrtf( dx * dx + dy * dy ) + 1;
	std::vector<Int> cells;
	for( Int s = 0; s <= steps; s++ )
	{
		Real f = (Real)s / (Real)steps;
		Int x = (Int)floorf( fromX + dx * f + 0.5f ) + RMG_BORDER_CELLS;
		Int y = (Int)floorf( fromY + dy * f + 0.5f ) + RMG_BORDER_CELLS;
		if( x < 0 || y < 0 || x >= m_width || y >= m_height )
			continue;
		Int cell = cellIndex( x, y );
		if( cells.empty() || cells.back() != cell )
			cells.push_back( cell );
	}
	if( cells.size() >= 2 )
	{
		m_roads.push_back( cells );
		m_roadCarved.push_back( FALSE );
	}
}

/** Cells from the nearest ride of a black forest: the spoke from each base to the middle, and the
	ring through the bases at their mean distance from it, which is the ride between neighbours. */
Real RMGLayout::rideDistance( Real cellX, Real cellY ) const
{
	Real centre = (Real)m_settings.m_playableCells * 0.5f;
	Real ring = 0.0f;
	Real nearest = 1.0e9f;

	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Real sx = m_starts[i].m_cellX - centre;
		Real sy = m_starts[i].m_cellY - centre;
		Real length2 = sx * sx + sy * sy;
		ring += sqrtf( length2 );
		if( length2 < 1.0f )
			continue;

		// Distance to the segment from the middle out to the base.
		Real px = cellX - centre;
		Real py = cellY - centre;
		Real t = ( px * sx + py * sy ) / length2;
		if( t < 0.0f ) t = 0.0f;
		if( t > 1.0f ) t = 1.0f;
		Real ex = px - sx * t;
		Real ey = py - sy * t;
		Real d = sqrtf( ex * ex + ey * ey );
		if( d < nearest )
			nearest = d;
	}

	if( !m_starts.empty() )
	{
		ring /= (Real)m_starts.size();
		Real dx = cellX - centre;
		Real dy = cellY - centre;
		Real d = fabsf( sqrtf( dx * dx + dy * dy ) - ring );
		if( d < nearest )
			nearest = d;
	}

	return nearest;
}

/// Whether a cell lies within this many cells of the middle of any ramp a route or the massif cut.
Bool RMGLayout::nearRamp( Real cellX, Real cellY, Real radius ) const
{
	for( UnsignedInt i = 0; i < m_ramps.size(); i++ )
	{
		Real dx = cellX - m_ramps[i].m_cellX;
		Real dy = cellY - m_ramps[i].m_cellY;
		if( dx * dx + dy * dy < radius * radius )
			return TRUE;
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
// Playability after pads: a second way out of each base, and a walk to the money
//-----------------------------------------------------------------------------

/** The one test of walkable ground, which buildPassability stores and every flood reads: inside the
	playable boundary, no cliff, not under the water. A lake cell a route has raised above the surface
	is ground, as the pathfinder has it; counting every basin cell as water here while the carve counted
	it as ground re-cut the same causeway every attempt and never saw it open. */
Bool RMGLayout::playableAtCell( Int x, Int y ) const
{
	if( !insidePlayable( x, y ) )
		return FALSE;
	if( cellSpanWorld( x, y ) > RMG_CLIFF_WORLD_SPAN )
		return FALSE;
	return !underwaterAtCell( x, y );
}

Int RMGLayout::ringCrossings( Int startIndex ) const
{
	const Int samples = 360;
	char walkable[360];
	Int walkableCount = 0;

	Real sx = m_starts[startIndex].m_cellX;
	Real sy = m_starts[startIndex].m_cellY;

	for( Int i = 0; i < samples; i++ )
	{
		Real angle = 2.0f * PI * (Real)i / (Real)samples;
		Real dirX = Cos( angle );
		Real dirY = Sin( angle );
		Bool open = FALSE;
		for( Int radius = 26; radius <= 28; radius++ )
		{
			Int x = (Int)(sx + (Real)radius * dirX + 0.5f) + RMG_BORDER_CELLS;
			Int y = (Int)(sy + (Real)radius * dirY + 0.5f) + RMG_BORDER_CELLS;
			if( playableAtCell( x, y ) )
			{
				open = TRUE;
				break;
			}
		}
		walkable[i] = open ? 1 : 0;
		if( open )
			walkableCount++;
	}

	if( walkableCount == samples )
		return 2;

	Int crossings = 0;
	Int longestOpen = 0;
	Int run = 0;
	for( Int i = 0; i < samples * 2; i++ )
	{
		Int idx = i % samples;
		Int prev = (idx == 0) ? samples - 1 : idx - 1;
		if( i < samples && walkable[idx] && !walkable[prev] )
			crossings++;

		if( walkable[idx] )
		{
			run++;
			if( run > longestOpen )
				longestOpen = run;
		}
		else
		{
			run = 0;
		}
	}

	if( crossings >= 2 )
		return crossings;

	if( longestOpen >= 90 )
		return 2;

	return crossings;
}

void RMGLayout::floodPlayableFrom( Int startIndex )
{
	m_visited.assign( m_width * m_height, 0 );

	Int startX = (Int)(m_starts[startIndex].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int startY = (Int)(m_starts[startIndex].m_cellY + 0.5f) + RMG_BORDER_CELLS;
	if( !playableAtCell( startX, startY ) )
		return;

	std::vector<Int> stack;
	m_visited[startY * m_width + startX] = 1;
	stack.push_back( startY * m_width + startX );

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	while( !stack.empty() )
	{
		Int index = stack.back();
		stack.pop_back();

		Int x = index % m_width;
		Int y = index / m_width;

		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 0 || ny < 0 || nx >= m_width - 1 || ny >= m_height - 1 )
				continue;

			Int next = ny * m_width + nx;
			if( m_visited[next] || !playableAtCell( nx, ny ) )
				continue;

			m_visited[next] = 1;
			stack.push_back( next );
		}
	}
}

void RMGLayout::startPerimeterToward( Int startIndex, Int targetX, Int targetY, Int *outX, Int *outY ) const
{
	Real sx = m_starts[startIndex].m_cellX + (Real)RMG_BORDER_CELLS;
	Real sy = m_starts[startIndex].m_cellY + (Real)RMG_BORDER_CELLS;
	Real dx = (Real)targetX - sx;
	Real dy = (Real)targetY - sy;
	Real length = sqrtf( dx * dx + dy * dy );
	if( length < 1.0f )
	{
		*outX = (Int)(sx + 0.5f);
		*outY = (Int)(sy + 0.5f);
		return;
	}

	Real dist = 16.0f;
	if( dist > length * 0.5f )
		dist = length * 0.5f;

	*outX = (Int)(sx + dx / length * dist + 0.5f);
	*outY = (Int)(sy + dy / length * dist + 0.5f);
}

Bool RMGLayout::carveStraightCorridor( Int fromX, Int fromY, Int toX, Int toY,
																			 Real protectRadius, Real protectX, Real protectY )
{
	Int spanX = toX - fromX;
	Int spanY = toY - fromY;
	Int absX = (spanX >= 0) ? spanX : -spanX;
	Int absY = (spanY >= 0) ? spanY : -spanY;
	Int steps = (absX > absY) ? absX : absY;
	if( steps < 1 )
		return FALSE;

	Real fromHeight = (Real)m_heights[cellIndex( fromX, fromY )];
	Real toHeight = (Real)m_heights[cellIndex( toX, toY )];
	Real waterFloor = m_waterHeight + 2.0f;
	if( fromHeight < waterFloor ) fromHeight = waterFloor;
	if( toHeight < waterFloor ) toHeight = waterFloor;

	for( Int i = 0; i <= steps; i++ )
	{
		Int x = fromX + spanX * i / steps;
		Int y = fromY + spanY * i / steps;
		Real along = (Real)i / (Real)steps;
		cutTowardLevel( x, y, lerpReal( fromHeight, toHeight, along ), TRUE,
										protectRadius, protectX, protectY );
	}
	recordRoad( (Real)( fromX - RMG_BORDER_CELLS ), (Real)( fromY - RMG_BORDER_CELLS ),
							(Real)( toX - RMG_BORDER_CELLS ), (Real)( toY - RMG_BORDER_CELLS ) );

	return TRUE;
}

Bool RMGLayout::openSecondExit( Int startIndex )
{
	const Int samples = 360;
	char openRay[360];
	char lakeRay[360];
	Int openCount = 0;

	Real sx = m_starts[startIndex].m_cellX;
	Real sy = m_starts[startIndex].m_cellY;
	Real mapSX = sx + (Real)RMG_BORDER_CELLS;
	Real mapSY = sy + (Real)RMG_BORDER_CELLS;

	for( Int i = 0; i < samples; i++ )
	{
		Real angle = 2.0f * PI * (Real)i / (Real)samples;
		Real dirX = Cos( angle );
		Real dirY = Sin( angle );
		Bool open = FALSE;
		Bool lake = FALSE;
		for( Int radius = 26; radius <= 28; radius++ )
		{
			Int x = (Int)(sx + (Real)radius * dirX + 0.5f) + RMG_BORDER_CELLS;
			Int y = (Int)(sy + (Real)radius * dirY + 0.5f) + RMG_BORDER_CELLS;
			if( x >= 0 && y >= 0 && x < m_width && y < m_height && m_inLake[cellIndex( x, y )] )
				lake = TRUE;
			if( playableAtCell( x, y ) )
			{
				open = TRUE;
				break;
			}
		}
		openRay[i] = open ? 1 : 0;
		lakeRay[i] = (lake && !open) ? 1 : 0;
		if( open )
			openCount++;
	}

	if( openCount == samples )
		return TRUE;

	Int bestLen = 0;
	Int bestStart = 0;
	Int runLen = 0;
	Int runStart = 0;

	for( Int i = 0; i < samples * 2; i++ )
	{
		Int idx = i % samples;
		if( !openRay[idx] && !lakeRay[idx] )
		{
			if( runLen == 0 )
				runStart = idx;
			runLen++;
			if( runLen > bestLen )
			{
				bestLen = runLen;
				bestStart = runStart;
			}
		}
		else
		{
			runLen = 0;
		}
	}

	if( bestLen < 6 )
	{
		bestLen = 0;
		runLen = 0;
		for( Int i = 0; i < samples * 2; i++ )
		{
			Int idx = i % samples;
			if( !openRay[idx] )
			{
				if( runLen == 0 )
					runStart = idx;
				runLen++;
				if( runLen > bestLen )
				{
					bestLen = runLen;
					bestStart = runStart;
				}
			}
			else
			{
				runLen = 0;
			}
		}
	}

	if( bestLen < 1 )
		return FALSE;

	Int playable = m_settings.m_playableCells;
	Int mid = (bestStart + bestLen / 2) % samples;

	for( Int tryOffset = 0; tryOffset < bestLen; tryOffset++ )
	{
		Int sample = (mid + ((tryOffset % 2) ? tryOffset / 2 : -(tryOffset / 2)) + samples * 4) % samples;
		if( lakeRay[sample] )
			continue;

		Real angle = 2.0f * PI * (Real)sample / (Real)samples;
		Real dirX = Cos( angle );
		Real dirY = Sin( angle );

		Real fromCellX = sx + 16.0f * dirX;
		Real fromCellY = sy + 16.0f * dirY;
		Real toCellX = sx + 34.0f * dirX;
		Real toCellY = sy + 34.0f * dirY;

		if( toCellX < 2.0f ) toCellX = 2.0f;
		if( toCellY < 2.0f ) toCellY = 2.0f;
		if( toCellX > (Real)playable - 3.0f ) toCellX = (Real)playable - 3.0f;
		if( toCellY > (Real)playable - 3.0f ) toCellY = (Real)playable - 3.0f;

		Int toX = (Int)(toCellX + 0.5f) + RMG_BORDER_CELLS;
		Int toY = (Int)(toCellY + 0.5f) + RMG_BORDER_CELLS;
		if( m_inLake[cellIndex( toX, toY )] )
			continue;

		Int fromX = (Int)(fromCellX + 0.5f) + RMG_BORDER_CELLS;
		Int fromY = (Int)(fromCellY + 0.5f) + RMG_BORDER_CELLS;

		if( carveStraightCorridor( fromX, fromY, toX, toY, RMG_FLAT_RADIUS, mapSX, mapSY ) )
			return TRUE;
	}

	return FALSE;
}

void RMGLayout::ensurePlayability( void )
{
	buildPassability();

	for( Int attempt = 0; ; attempt++ )
	{
		floodPlayableFrom( 0 );
		// the attempt count is checked after the flood, so the last carve is judged before giving up
		if( attempt == RMG_PASS_ATTEMPTS )
		{
			for( UnsignedInt i = 1; i < m_starts.size(); i++ )
			{
				Int x = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
				Int y = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
				if( !m_visited[y * m_width + x] )
					DEBUG_LOG(( "random map: seed %d - start %d still cut off from start 1 after %d passes\n",
						m_settings.m_seed, i + 1, RMG_PASS_ATTEMPTS ));
			}
			break;
		}

		Int unreachable = -1;
		for( UnsignedInt i = 1; i < m_starts.size(); i++ )
		{
			Int x = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
			Int y = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
			if( !m_visited[y * m_width + x] )
			{
				unreachable = (Int)i;
				break;
			}
		}

		if( unreachable < 0 )
			break;

		Int fromX, fromY, toX, toY;
		Int targetX = (Int)(m_starts[unreachable].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int targetY = (Int)(m_starts[unreachable].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		startPerimeterToward( 0, targetX, targetY, &fromX, &fromY );
		startPerimeterToward( unreachable,
			(Int)(m_starts[0].m_cellX + 0.5f) + RMG_BORDER_CELLS,
			(Int)(m_starts[0].m_cellY + 0.5f) + RMG_BORDER_CELLS,
			&toX, &toY );

		Real protectX = m_starts[0].m_cellX + (Real)RMG_BORDER_CELLS;
		Real protectY = m_starts[0].m_cellY + (Real)RMG_BORDER_CELLS;
		if( !carvePassBetweenCells( fromX, fromY, toX, toY, FALSE, RMG_FLAT_RADIUS, protectX, protectY ) )
		{
			DEBUG_LOG(( "random map: seed %d - no route to cut from start 1 to start %d\n", m_settings.m_seed, unreachable + 1 ));
			break;
		}

		buildPassability();
	}

	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		for( Int attempt = 0; attempt < 5; attempt++ )
		{
			if( ringCrossings( (Int)i ) >= 2 )
				break;
			if( !openSecondExit( (Int)i ) )
				break;
			buildPassability();
		}
	}

	floodPlayableFrom( 0 );

	static const Int neighbourX[5] = { 0, 1, -1, 0, 0 };
	static const Int neighbourY[5] = { 0, 0, 0, 1, -1 };

	for( UnsignedInt o = 0; o < m_objects.size(); o++ )
	{
		if( m_objects[o].m_templateName.compare( "SupplyDock" ) != 0 &&
				m_objects[o].m_templateName.compare( "TechOilDerrick" ) != 0 &&
				m_objects[o].m_templateName.compare( "SupplyPileSmall" ) != 0 )
			continue;

		Int cellX = (Int)(m_objects[o].m_worldX / MAP_XY_FACTOR + 0.5f) + RMG_BORDER_CELLS;
		Int cellY = (Int)(m_objects[o].m_worldY / MAP_XY_FACTOR + 0.5f) + RMG_BORDER_CELLS;

		Bool reached = FALSE;
		for( Int n = 0; n < 5; n++ )
		{
			Int nx = cellX + neighbourX[n];
			Int ny = cellY + neighbourY[n];
			if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
				continue;
			if( m_visited[ny * m_width + nx] )
			{
				reached = TRUE;
				break;
			}
		}

		if( reached )
			continue;

		Int nearest = 0;
		Real nearestDist = 1.0e9f;
		for( UnsignedInt s = 0; s < m_starts.size(); s++ )
		{
			Real dx = m_starts[s].m_cellX - (m_objects[o].m_worldX / MAP_XY_FACTOR);
			Real dy = m_starts[s].m_cellY - (m_objects[o].m_worldY / MAP_XY_FACTOR);
			Real dist = dx * dx + dy * dy;
			if( dist < nearestDist )
			{
				nearestDist = dist;
				nearest = (Int)s;
			}
		}

		Int fromX, fromY;
		startPerimeterToward( nearest, cellX, cellY, &fromX, &fromY );
		Real protectX = m_starts[nearest].m_cellX + (Real)RMG_BORDER_CELLS;
		Real protectY = m_starts[nearest].m_cellY + (Real)RMG_BORDER_CELLS;
		if( !carvePassBetweenCells( fromX, fromY, cellX, cellY, FALSE, RMG_FLAT_RADIUS, protectX, protectY ) )
			continue;

		buildPassability();
		floodPlayableFrom( 0 );
	}

	buildPassability();
}

/** Hold every player's own money to the walk it was placed at. Whatever came after the placement
	can lengthen one: a second exit cut through a base rim, a dock on a lake bank whose pad and the
	base's last levelling leave a step between them. A walk that grew past the slack gets a ramp cut
	from the base's edge straight to the money, the same repair an unreachable dock gets.
	A contested dock also has to stay on its placer's side of the midline. On open ground a walk is
	as short as it gets and a ramp cannot win it back, so a dock that ended up a cell or two over the
	line is slid across its own pad instead: on one canyon map both docks between two seats had gone
	to one of them, five money to seven. */
void RMGLayout::holdMoneyWalks( void )
{
	buildPassability();

	Int players = (Int)m_starts.size();
	std::vector< std::vector<Int> > dist( players );
	std::vector<char> flooded( players, 0 );
	for( Int player = 0; player < players; player++ )
	{
		for( UnsignedInt m = 0; m < m_moneyWalks.size(); m++ )
		{
			const RMGMoneyWalk& money = m_moneyWalks[m];
			if( money.m_player != player )
				continue;

			for( Int pass = 0; pass < 2; pass++ )
			{
				Int who[2] = { player, money.m_rival };
				for( Int k = 0; k < 2; k++ )
				{
					if( who[k] < 0 || flooded[who[k]] )
						continue;
					floodDistancesFromCell( (Int)(m_starts[who[k]].m_cellX + 0.5f) + RMG_BORDER_CELLS,
																	(Int)(m_starts[who[k]].m_cellY + 0.5f) + RMG_BORDER_CELLS,
																	dist[who[k]] );
					flooded[who[k]] = 1;
				}
				if( pass == 1 )
					break;

				Int now = dist[player][cellIndex( money.m_cellX, money.m_cellY )];
				if( now >= 0 && now <= money.m_walk + RMG_MONEY_WALK_SLACK )
					break;

				Int fromX, fromY;
				startPerimeterToward( player, money.m_cellX, money.m_cellY, &fromX, &fromY );
				Real protectX = m_starts[player].m_cellX + (Real)RMG_BORDER_CELLS;
				Real protectY = m_starts[player].m_cellY + (Real)RMG_BORDER_CELLS;
				if( !carvePassBetweenCells( fromX, fromY, money.m_cellX, money.m_cellY, FALSE,
																		RMG_FLAT_RADIUS, protectX, protectY ) )
					break;

				buildPassability();
				flooded.assign( players, 0 );
			}

			if( money.m_rival < 0 || money.m_object < 0 )
				continue;

			const std::vector<Int>& mine = dist[player];
			const std::vector<Int>& theirs = dist[money.m_rival];
			Int cell = cellIndex( money.m_cellX, money.m_cellY );
			if( mine[cell] >= 0 && ( theirs[cell] < 0 || mine[cell] + RMG_CONTESTED_LEAD_MIN <= theirs[cell] ) )
				continue;

			// The nearest cell on the pad that gives the placer its lead back, if there is one.
			const Int reach = (Int)RMG_PAD_RADIUS - 1;
			Int bestX = 0, bestY = 0, bestOff = -1;
			for( Int dy = -reach; dy <= reach; dy++ )
			{
				for( Int dx = -reach; dx <= reach; dx++ )
				{
					Int off = dx * dx + dy * dy;
					if( off > reach * reach || ( bestOff >= 0 && off >= bestOff ) )
						continue;
					Int c = cellIndex( money.m_cellX + dx, money.m_cellY + dy );
					if( mine[c] < 0 || theirs[c] < 0 )
						continue;
					Int lead = theirs[c] - mine[c];
					if( lead < RMG_CONTESTED_LEAD_MIN || lead > RMG_CONTESTED_LEAD_MAX )
						continue;
					if( !placersSide( money.m_cellX + dx, money.m_cellY + dy, player, money.m_rival, RMG_MONEY_NO_RIVAL ) )
						continue;
					bestX = dx;
					bestY = dy;
					bestOff = off;
				}
			}
			if( bestOff < 0 )
				continue;

			/* The dock moves with everything that was placed by it: its site, so later pads and the
				scenery keep their distance from where it stands now, the walk it is held to, and its pad,
				widened round the new spot so no corner hangs over the old pad's blend. Nothing that
				blocks is placed before this, so only the money sites have to keep clear of it. */
			RMGObject& dock = m_objects[money.m_object];
			Int site = -1;
			for( UnsignedInt s = 0; s < m_sites.size() && site < 0; s++ )
			{
				if( m_sites[s].m_cellX * MAP_XY_FACTOR == dock.m_worldX && m_sites[s].m_cellY * MAP_XY_FACTOR == dock.m_worldY )
					site = (Int)s;
			}
			const Real newX = dock.m_worldX / MAP_XY_FACTOR + (Real)bestX;
			const Real newY = dock.m_worldY / MAP_XY_FACTOR + (Real)bestY;
			RMGSite own;
			own.m_cellX = newX;
			own.m_cellY = newY;
			own.m_radius = RMG_SITE_CLEARANCE;
			if( site >= 0 )
			{
				own = m_sites[site];
				m_sites.erase( m_sites.begin() + site );
				own.m_cellX = newX;
				own.m_cellY = newY;
			}
			/* Clear of the other money only. The sites before it are the starts' discs and the massif,
				and a dock a cell over the midline sits against its placer's disc more often than not: held
				to those too, the slide was refused just where it was wanted. */
			const UnsignedInt firstMoneySite = m_starts.size() + m_massifLobes.size();
			Bool clear = TRUE;
			for( UnsignedInt s = firstMoneySite; s < m_sites.size() && clear; s++ )
			{
				Real sx = newX - m_sites[s].m_cellX;
				Real sy = newY - m_sites[s].m_cellY;
				clear = sqrtf( sx * sx + sy * sy ) >= own.m_radius + m_sites[s].m_radius;
			}
			if( !clear )
			{
				if( site >= 0 )
				{
					own.m_cellX = newX - (Real)bestX;
					own.m_cellY = newY - (Real)bestY;
					m_sites.insert( m_sites.begin() + site, own );
				}
				continue;
			}
			flattenPad( newX, newY, RMG_PAD_RADIUS, RMG_PAD_BLEND );
			if( site >= 0 )
				m_sites.insert( m_sites.begin() + site, own );
			else
				m_sites.push_back( own );

			dock.m_worldX += (Real)bestX * MAP_XY_FACTOR;
			dock.m_worldY += (Real)bestY * MAP_XY_FACTOR;
			RMGMoneyWalk& moved = m_moneyWalks[m];
			moved.m_cellX += bestX;
			moved.m_cellY += bestY;
			moved.m_walk = mine[cellIndex( moved.m_cellX, moved.m_cellY )];
			buildPassability();
			flooded.assign( players, 0 );
		}
	}
}

//-----------------------------------------------------------------------------
// Texture classes and their blends
//-----------------------------------------------------------------------------

/** Height and slope decide the ground cover, with the thresholds pushed about
	by a noise field of their own so the edges wander instead of following a
	contour line.

	The slope is read averaged over the cell and its eight neighbours, and the classes then go
	through two majority votes. Read cell by cell, the slope of rolling ground flickers either side
	of the rock threshold, and version 12 drew that as single rock cells, spurs one cell wide and
	notches in the edge of every patch: on screen a staircase of squares. The average takes the top
	off a steep cell, so rock starts at 0.39 of the cliff span instead of 0.45, and a real cliff
	keeps its rock whatever the vote says. */
void RMGLayout::buildTerrainClasses( const UnsignedByte perm[512] )
{
	m_terrain.resize( m_width * m_height );

	Real scale = RMG_FEATURES_PER_MAP * 2.5f / (Real)m_settings.m_playableCells;

	std::vector<Real> spans( m_width * m_height );
	for( Int y = 0; y < m_height; y++ )
		for( Int x = 0; x < m_width; x++ )
			spans[cellIndex( x, y )] = cellSpanWorld( x, y );

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real height = (Real)m_heights[cellIndex( x, y )];

			Real span = 0.0f;
			Int around = 0;
			for( Int dy = -1; dy <= 1; dy++ )
			{
				for( Int dx = -1; dx <= 1; dx++ )
				{
					if( x + dx < 0 || y + dy < 0 || x + dx >= m_width || y + dy >= m_height )
						continue;
					span += spans[cellIndex( x + dx, y + dy )];
					around++;
				}
			}
			span /= (Real)around;

			Real wobble = fractalNoise( perm, (Real)x * scale + 31.0f, (Real)y * scale - 17.0f, 3 );

			UnsignedByte terrainClass = RMG_TERRAIN_GRASS;

			// Sand runs from the water up the whole beach, which is what makes a lake read as a lake
			// with a shore rather than a hole of water cut in the grass. Only near water, though: a
			// dry hollow on a highland map is low without being a beach.
			Real distanceToShore;
			insideLake( (Real)(x - RMG_BORDER_CELLS), (Real)(y - RMG_BORDER_CELLS), &distanceToShore );

			// The beach's width wanders as well, or it reads as the lake's outline drawn again a
			// fixed distance out.
			if( height < m_waterHeight + 8.0f + wobble * 3.0f &&
					distanceToShore < RMG_LAKE_SHORE + 8.0f + wobble * 8.0f )
				terrainClass = RMG_TERRAIN_SAND;
			else if( span > RMG_CLIFF_WORLD_SPAN * (0.39f + wobble * 0.12f) ||
							 spans[cellIndex( x, y )] > RMG_CLIFF_WORLD_SPAN )
				terrainClass = RMG_TERRAIN_ROCK;
			// Dry high ground. The offset is in height bytes rather than a fraction of the
			// amplitude: five octaves only reach a third of their nominal range, so a fraction of
			// it puts the line above anything the noise ever climbs to.
			// The line scales with the map type's relief, so a plain still has its brown rises and
			// highlands are not brown from the foot of every hill.
			/* A style's dirt is half the plateaus', and with no water its sand is patches on the low
				ground, so the bowl and the shelves each mix two grounds the way the shipped map does. */
			else if( m_style )
			{
				if( height > RMG_BASE_HEIGHT + 0.55f * m_style->m_raise + wobble * 8.0f && wobble > 0.0f )
					terrainClass = RMG_TERRAIN_DIRT;
				else if( wobble > 0.1f )
					terrainClass = RMG_TERRAIN_SAND;
			}
			else if( height > RMG_BASE_HEIGHT + ( 8.0f + wobble * 5.0f ) * m_type->m_amplitude )
				terrainClass = RMG_TERRAIN_DIRT;

			m_terrain[cellIndex( x, y )] = terrainClass;
		}
	}

	// Two rounds of a vote over each cell and its eight neighbours: the class with the most votes
	// wins, a tie keeps what the cell had. Both rounds read the classes as the last one left them.
	std::vector<UnsignedByte> voted( m_terrain.size() );
	for( Int round = 0; round < 2; round++ )
	{
		for( Int y = 0; y < m_height; y++ )
		{
			for( Int x = 0; x < m_width; x++ )
			{
				Int mine = m_terrain[cellIndex( x, y )];
				voted[cellIndex( x, y )] = (UnsignedByte)mine;
				if( spans[cellIndex( x, y )] > RMG_CLIFF_WORLD_SPAN )
					continue;

				Int votes[RMG_TERRAIN_COUNT] = { 0, 0, 0, 0 };
				for( Int dy = -1; dy <= 1; dy++ )
				{
					for( Int dx = -1; dx <= 1; dx++ )
					{
						if( x + dx < 0 || y + dy < 0 || x + dx >= m_width || y + dy >= m_height )
							continue;
						votes[m_terrain[cellIndex( x + dx, y + dy )]]++;
					}
				}

				Int best = mine;
				for( Int k = 0; k < RMG_TERRAIN_COUNT; k++ )
				{
					if( votes[k] > votes[best] )
						best = k;
				}
				voted[cellIndex( x, y )] = (UnsignedByte)best;
			}
		}
		m_terrain.swap( voted );
	}
}

/** The tile index WorldBuilder computes for a cell: four quadrants packed into
	each source tile, the source tile picked by tiling the class across the map. */
static Short tileIndexForCell( Int x, Int y, Int firstTile, Int width )
{
	Int ndx = firstTile + ((x / 2) % width) + width * ((y / 2) % width);
	ndx = (ndx << 2) + 2 * (y & 1) + (x & 1);
	return (Short)ndx;
}

/** The corner alpha the renderer can actually express, and the flags that ask for it, read off
	WorldHeightMap::getAlphaUVData. A horizontal or vertical blend paints one side, a short diagonal
	one corner and a long diagonal three. Every flag in an entry shares the one `inverted` byte, and
	the masks with two opposite corners or all four have no entry at all.
	Corners: bit 0 is (x,y), 1 is (x+1,y), 2 is (x+1,y+1), 3 is (x,y+1), in the order getAlphaUVData
	fills alpha[].

	Version 12 asked for three corners with a side flag and a diagonal flag together. The renderer
	adds those up, and with the shared `inverted` only the pair that point the same way came to three
	corners: the other three entries drew two corners or one, so every concave corner of a patch of
	rock was missing a corner, a notch of the ground underneath it. The long diagonals are what
	WorldBuilder uses and reach all four.

	A diagonal also decides which way the cell's two triangles are cut (m_flip, 1 for the cut from
	corner 1 to corner 3). The renderer cuts the cell the way its first layer says and draws the
	second layer cut its own way, so two layers have to agree; a side blend looks the same cut either
	way (-1) and takes the other layer's cut through FLIPPED_MASK, which is what WorldBuilder does. */
struct RMGBlendShape
{
	Int m_cornerMask;
	UnsignedByte m_horizontal;
	UnsignedByte m_vertical;
	UnsignedByte m_rightDiagonal;
	UnsignedByte m_leftDiagonal;
	UnsignedByte m_inverted;
	UnsignedByte m_longDiagonal;
	Int m_flip;
};

static const RMGBlendShape theBlendShapes[] =
{
	// one corner
	{ 0x1, 0, 0, 0, 1, 1, 0, 1 },		// (x,y)
	{ 0x2, 0, 0, 1, 0, 1, 0, 0 },		// (x+1,y)
	{ 0x4, 0, 0, 1, 0, 0, 0, 1 },		// (x+1,y+1)
	{ 0x8, 0, 0, 0, 1, 0, 0, 0 },		// (x,y+1)
	// one side
	{ 0x6, 1, 0, 0, 0, 0, 0, -1 },		// the +x side
	{ 0x9, 1, 0, 0, 0, 1, 0, -1 },		// the -x side
	{ 0xC, 0, 1, 0, 0, 0, 0, -1 },		// the +y side
	{ 0x3, 0, 1, 0, 0, 1, 0, -1 },		// the -y side
	// three corners
	{ 0x7, 0, 0, 1, 0, 1, 1, 0 },		// everything but (x,y+1)
	{ 0xB, 0, 0, 0, 1, 1, 1, 1 },		// everything but (x+1,y+1)
	{ 0xD, 0, 0, 0, 1, 0, 1, 0 },		// everything but (x+1,y)
	{ 0xE, 0, 0, 1, 0, 0, 1, 1 },		// everything but (x,y)
};
static const Int theNumBlendShapes = sizeof(theBlendShapes) / sizeof(theBlendShapes[0]);
static const UnsignedByte RMG_BLEND_FLIPPED = 0x2;		///< FLIPPED_MASK in TileData.h

/// The shape that paints exactly these corners, or -1 when the renderer has none.
static Int blendShapeFor( Int cornerMask )
{
	for( Int i = 0; i < theNumBlendShapes; i++ )
	{
		if( theBlendShapes[i].m_cornerMask == cornerMask )
			return i;
	}
	return -1;
}

/// Find or add the blend table entry that paints this tile in this shape.
Short RMGLayout::blendEntryFor( Int blendTileIndex, Int shapeIndex, Bool flipped )
{
	/* A tile, a shape and the cut name an entry, so that is the key. A scan of the table instead
		would be a scan per cell of the map, and on a 456-cell map with a few thousand entries in the
		table that is most of the generator's time. */
	Int key = ( blendTileIndex * theNumBlendShapes + shapeIndex ) * 2 + ( flipped ? 1 : 0 );
	std::map<Int, Short>::const_iterator found = m_blendLookup.find( key );
	if( found != m_blendLookup.end() )
		return found->second;

	const RMGBlendShape& shape = theBlendShapes[shapeIndex];

	RMGBlend blend;
	blend.m_blendTileIndex = blendTileIndex;
	blend.m_horizontal = shape.m_horizontal;
	blend.m_vertical = shape.m_vertical;
	blend.m_rightDiagonal = shape.m_rightDiagonal;
	blend.m_leftDiagonal = shape.m_leftDiagonal;
	blend.m_inverted = shape.m_inverted | ( flipped ? RMG_BLEND_FLIPPED : 0 );
	blend.m_longDiagonal = shape.m_longDiagonal;
	m_blends.push_back( blend );

	Short entry = (Short)(m_blends.size() - 1);
	m_blendLookup[key] = entry;
	return entry;
}

/** The layers a cell is drawn with, lowest ground first; returns how many.

	A layer of some ground goes over every corner that touches that ground or anything above it. Put
	that way the alpha of a ground at a corner belongs to the corner and not to whichever of the four
	cells round it is asking, so the four draw it alike, and along an edge the two cells either side
	interpolate the same two values: the picture has no seam anywhere. Version 12 painted only the
	strongest neighbour and only its own corners, so a grass cell between sand and rock cut the sand
	off at a hard line, and a cell the layer above already covers corner for corner added nothing and
	is left out here. The renderer has two layers, the blend and the extra blend over it. */
Int RMGLayout::blendLayersAt( Int x, Int y, Int *classes, Int *masks ) const
{
	Int mine = m_terrain[cellIndex( x, y )];
	Int touch[RMG_TERRAIN_COUNT] = { 0, 0, 0, 0 };

	for( Int dy = -1; dy <= 1; dy++ )
	{
		for( Int dx = -1; dx <= 1; dx++ )
		{
			Int nx = x + dx;
			Int ny = y + dy;
			if( (dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
				continue;

			Int theirs = m_terrain[cellIndex( nx, ny )];
			if( theirs <= mine )
				continue;

			// A neighbour claims the corners it shares with this cell.
			if( dx >= 0 && dy >= 0 ) touch[theirs] |= 0x4;		// (x+1,y+1)
			if( dx >= 0 && dy <= 0 ) touch[theirs] |= 0x2;		// (x+1,y)
			if( dx <= 0 && dy >= 0 ) touch[theirs] |= 0x8;		// (x,y+1)
			if( dx <= 0 && dy <= 0 ) touch[theirs] |= 0x1;		// (x,y)
		}
	}

	Int topDown[RMG_TERRAIN_COUNT];
	Int topDownMasks[RMG_TERRAIN_COUNT];
	Int count = 0;
	Int above = 0;
	for( Int k = RMG_TERRAIN_COUNT - 1; k > mine; k-- )
	{
		Int mask = above | touch[k];
		if( mask != above )
		{
			topDown[count] = k;
			topDownMasks[count] = mask;
			count++;
		}
		above = mask;
	}

	for( Int i = 0; i < count; i++ )
	{
		classes[i] = topDown[count - 1 - i];
		masks[i] = topDownMasks[count - 1 - i];
	}
	return count;
}

/** Raise every cell the renderer cannot draw without a seam to the ground of its lowest layer, until
	none is left. A cell gets raised when it needs a third layer, when a layer covers two opposite
	corners or all four, or when both layers are diagonals cut opposite ways. A cell with ground on
	all four corners is that ground anyway, and a gap one cell wide between two patches of the same
	ground fills in; nothing that is drawn moves by more than the cell. A cell only ever goes up, so
	the passes end. After the first pass only the cells round one that was raised can change, so
	only those are looked at again, in map order. */
void RMGLayout::settleTerrainForBlends( void )
{
	std::vector<Int> look( m_width * m_height );
	for( Int i = 0; i < (Int)look.size(); i++ )
		look[i] = i;
	std::vector<Int> raised;
	std::vector<char> queued( look.size(), 0 );

	while( !look.empty() )
	{
		raised.clear();
		for( UnsignedInt i = 0; i < look.size(); i++ )
		{
			Int x = look[i] % m_width;
			Int y = look[i] / m_width;

			Int classes[RMG_TERRAIN_COUNT];
			Int masks[RMG_TERRAIN_COUNT];
			Int count = blendLayersAt( x, y, classes, masks );
			if( count == 0 )
				continue;

			Bool drawable = count <= 2;
			Int flip = -1;
			for( Int layer = 0; layer < count && drawable; layer++ )
			{
				Int shape = blendShapeFor( masks[layer] );
				if( shape < 0 )
					drawable = FALSE;
				else if( theBlendShapes[shape].m_flip >= 0 )
				{
					if( flip >= 0 && flip != theBlendShapes[shape].m_flip )
						drawable = FALSE;
					flip = theBlendShapes[shape].m_flip;
				}
			}

			if( !drawable )
			{
				m_terrain[look[i]] = (UnsignedByte)classes[0];
				raised.push_back( look[i] );
			}
		}

		look.clear();
		for( UnsignedInt i = 0; i < raised.size(); i++ )
		{
			Int x = raised[i] % m_width;
			Int y = raised[i] / m_width;
			for( Int dy = -1; dy <= 1; dy++ )
			{
				for( Int dx = -1; dx <= 1; dx++ )
				{
					if( x + dx < 0 || y + dy < 0 || x + dx >= m_width || y + dy >= m_height )
						continue;
					Int c = cellIndex( x + dx, y + dy );
					if( !queued[c] )
					{
						queued[c] = 1;
						look.push_back( c );
					}
				}
			}
		}
		std::sort( look.begin(), look.end() );
		for( UnsignedInt i = 0; i < look.size(); i++ )
			queued[look[i]] = 0;
	}
}

void RMGLayout::buildBlends( void )
{
	m_blends.clear();
	m_blendLookup.clear();

	RMGBlend nothing;
	memset( &nothing, 0, sizeof(nothing) );
	m_blends.push_back( nothing );		// entry 0 is "no blend"

	m_blendIndex.assign( m_width * m_height, 0 );
	m_extraBlendIndex.assign( m_width * m_height, 0 );

	settleTerrainForBlends();

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Int classes[RMG_TERRAIN_COUNT];
			Int masks[RMG_TERRAIN_COUNT];
			Int count = blendLayersAt( x, y, classes, masks );
			if( count == 0 )
				continue;

			Int shapes[2];
			Int flip = 0;
			for( Int i = 0; i < count; i++ )
			{
				shapes[i] = blendShapeFor( masks[i] );
				if( theBlendShapes[shapes[i]].m_flip > 0 )
					flip = 1;
			}

			// A side blend takes the cut of the diagonal it shares the cell with.
			for( Int i = 0; i < count; i++ )
			{
				Bool flipped = theBlendShapes[shapes[i]].m_flip < 0 && count == 2 && flip > 0;
				Int tile = tileIndexForCell( x, y, rmgClassFirstTile( *m_biome, classes[i] ), rmgClassWidth( *m_biome, classes[i] ) );
				Short entry = blendEntryFor( tile, shapes[i], flipped );
				if( i == 0 )
					m_blendIndex[cellIndex( x, y )] = entry;
				else
					m_extraBlendIndex[cellIndex( x, y )] = entry;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Everything that stands on the map
//-----------------------------------------------------------------------------

Bool RMGLayout::siteIsClear( Real cellX, Real cellY, Real radius ) const
{
	for( UnsignedInt i = 0; i < m_sites.size(); i++ )
	{
		Real dx = cellX - m_sites[i].m_cellX;
		Real dy = cellY - m_sites[i].m_cellY;
		if( sqrtf( dx * dx + dy * dy ) < radius + m_sites[i].m_radius )
			return FALSE;
	}

	if( m_padsKeepOffWays && !m_wayDistance.empty() )
	{
		Int x = (Int)floorf( cellX + 0.5f ) + RMG_BORDER_CELLS;
		Int y = (Int)floorf( cellY + 0.5f ) + RMG_BORDER_CELLS;
		if( x >= 0 && y >= 0 && x < m_width && y < m_height && m_wayDistance[cellIndex( x, y )] < RMG_PAD_WAY_CLEAR )
			return FALSE;
	}

	return TRUE;
}

/** Cells from each cell to the nearest carved way, in four-neighbour steps, capped at
	RMG_PAD_WAY_CLEAR. A money pad levels a disc and holds the ground round what stands on it, so one
	laid across a way left a 13 to 15 byte step in the way that relayRoads was not allowed to move. */
void RMGLayout::markWayDistance( void )
{
	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };
	m_wayDistance.assign( m_heights.size(), (UnsignedByte)RMG_PAD_WAY_CLEAR );
	std::vector<Int> frontier, next;
	for( UnsignedInt r = 0; r < m_roads.size(); r++ )
	{
		if( !m_roadCarved[r] )
			continue;
		for( UnsignedInt i = 0; i < m_roads[r].size(); i++ )
		{
			if( m_wayDistance[m_roads[r][i]] != 0 )
			{
				m_wayDistance[m_roads[r][i]] = 0;
				frontier.push_back( m_roads[r][i] );
			}
		}
	}
	for( Int step = 1; step < RMG_PAD_WAY_CLEAR && !frontier.empty(); step++ )
	{
		next.clear();
		for( UnsignedInt f = 0; f < frontier.size(); f++ )
		{
			Int x = frontier[f] % m_width;
			Int y = frontier[f] / m_width;
			for( Int k = 0; k < 4; k++ )
			{
				Int nx = x + offsetX[k];
				Int ny = y + offsetY[k];
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
					continue;
				Int cell = cellIndex( nx, ny );
				if( m_wayDistance[cell] > step )
				{
					m_wayDistance[cell] = (UnsignedByte)step;
					next.push_back( cell );
				}
			}
		}
		frontier.swap( next );
	}
}

void RMGLayout::reserveSite( Real cellX, Real cellY, Real radius )
{
	RMGSite site;
	site.m_cellX = cellX;
	site.m_cellY = cellY;
	site.m_radius = radius;
	m_sites.push_back( site );
}

void RMGLayout::addObject( const char *templateName, const char *uniqueID, Real cellX, Real cellY,
													 Real angle )
{
	RMGObject object;
	object.m_templateName = templateName;
	object.m_uniqueID = uniqueID;
	object.m_worldX = cellX * MAP_XY_FACTOR;
	object.m_worldY = cellY * MAP_XY_FACTOR;
	object.m_angle = angle;
	object.m_waypointID = 0;
	object.m_flags = 0;
	m_objects.push_back( object );
}

/** Level the ground under something that is about to stand on it. A supply dock across a terrace
	edge has one corner in the air and nothing can be built beside it, which on a map made of
	terraces is most of the places a search would otherwise call flat enough. */
void RMGLayout::flattenPad( Real cellX, Real cellY, Real radius, Real blend )
{
	Int centreX = (Int)(cellX + 0.5f) + RMG_BORDER_CELLS;
	Int centreY = (Int)(cellY + 0.5f) + RMG_BORDER_CELLS;
	if( centreX < 1 || centreY < 1 || centreX >= m_width - 1 || centreY >= m_height - 1 )
		return;

	Real level = (Real)m_heights[cellIndex( centreX, centreY )];
	Int reach = (Int)( radius + ( blend - radius ) * ( 1.0f + RMG_PAD_BLEND_SWING ) ) + 1;

	for( Int dy = -reach; dy <= reach; dy++ )
	{
		for( Int dx = -reach; dx <= reach; dx++ )
		{
			Int x = centreX + dx;
			Int y = centreY + dy;
			if( x < 0 || y < 0 || x >= m_width || y >= m_height )
				continue;

			// Never fill a lake in to make room for a building.
			if( underwaterAtCell( x, y ) )
				continue;

			if( !m_routeLock.empty() && m_routeLock[cellIndex( x, y )] )
				continue;

			/* The flat stays a disc of the radius asked for, which is what the dock is placed on and
				what every player gets alike; the blend round it wanders out with the bearing. */
			Real distance = sqrtf( (Real)(dx * dx + dy * dy) );
			Real reachHere = blend;
			if( distance > radius )
				reachHere = radius + ( blend - radius ) * ( 1.0f + RMG_PAD_BLEND_SWING *
					ringNoise( m_perm, (Real)dx / distance, (Real)dy / distance, cellX * 0.37f, cellY * 0.37f ) );
			if( distance >= reachHere )
				continue;

			Real t = 0.0f;
			if( distance > radius )
				t = (distance - radius) / (reachHere - radius);

			/* The beach keeps its own profile. A pad that runs to the water's edge levels the shelf
				the soft water edge is drawn on and leaves the bank standing over the lake like a
				kerb, so the pad fades out as it comes up to the shore instead. */
			Real distanceToShore;
			insideLake( (Real)(x - RMG_BORDER_CELLS), (Real)(y - RMG_BORDER_CELLS), &distanceToShore );
			if( distanceToShore < RMG_PAD_SHORE_KEEP )
				continue;

			/* A later pad (a town, a bunker) must not recarve the ground a dock or a start is
				standing on. That recarve is how a supply dock ended up behind a cliff. */
			Real playX = (Real)(x - RMG_BORDER_CELLS);
			Real playY = (Real)(y - RMG_BORDER_CELLS);
			Bool onReserved = FALSE;
			for( UnsignedInt s = 0; s < m_sites.size(); s++ )
			{
				Real sx = playX - m_sites[s].m_cellX;
				Real sy = playY - m_sites[s].m_cellY;
				if( sqrtf( sx * sx + sy * sy ) < m_sites[s].m_radius )
				{
					onReserved = TRUE;
					break;
				}
			}
			if( onReserved )
				continue;

			Real shoreT = (distanceToShore - RMG_PAD_SHORE_KEEP) / RMG_LAKE_SHORE;
			if( shoreT < 1.0f && t < 1.0f - shoreT )
				t = 1.0f - shoreT;

			Real h = lerpReal( level, (Real)m_heights[cellIndex( x, y )], fadeCurve( t ) );
			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;

			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
		}
	}
}

/** A plain map's town plate as terraces: flat for most of each RMG_TOWN_TERRACE band of height, then
	eased up to the next one. The steps fall where the ground's own contours run, so a town on a
	hillside sits on a few broad shelves, and the 1-byte stairs a plate pressed nearly flat drew as
	contour bands are gone. */
static Real terraceLevel( Real level )
{
	Real band = level / RMG_TOWN_TERRACE;
	Real floorBand = (Real)(Int)band;		// heights are never negative, so the cast is the floor
	Real into = band - floorBand;
	Real flat = 1.0f - RMG_TOWN_TERRACE_RISE;
	Real rise = (into <= flat) ? 0.0f : fadeCurve( (into - flat) / RMG_TOWN_TERRACE_RISE );
	return ( floorBand + rise ) * RMG_TOWN_TERRACE;
}

/** How far outside a plain map's town plate a point is, in cells, 0 inside. The grid's rectangle has
	its corners rounded off, its edge wanders by a noise of its own, and it draws in where the ground
	stands off the plate's level, so the plate reaches along the contour and stops short up a slope. */
Real RMGLayout::plateOutside( Real across, Real down, Real halfAcross, Real halfDown, Real playX, Real playY,
															Real standOff ) const
{
	Real corner = RMG_TOWN_RIM_ROUND * ( (halfAcross < halfDown) ? halfAcross : halfDown );
	Real overAcross = fabsf( across ) - halfAcross + corner;
	Real overDown = fabsf( down ) - halfDown + corner;
	Real outAcross = (overAcross > 0.0f) ? overAcross : 0.0f;
	Real outDown = (overDown > 0.0f) ? overDown : 0.0f;
	Real inside = (overAcross > overDown) ? overAcross : overDown;
	if( inside > 0.0f )
		inside = 0.0f;
	Real distance = sqrtf( outAcross * outAcross + outDown * outDown ) + inside - corner;

	distance += RMG_TOWN_RIM_WANDER * 1.4f * fractalNoise( m_perm, playX * 0.07f + 19.0f, playY * 0.07f - 23.0f, 2 );
	distance += RMG_TOWN_RIM_CONTOUR * fabsf( standOff );
	return (distance > 0.0f) ? distance : 0.0f;
}

/** The ground a town stands on, eased rather than levelled. Inside the street grid the relief is
	averaged over a wide window, which takes a terrace step out of a street and leaves the long roll
	of the hill in it; outside the grid that easing fades back into the ground as it was. The shape
	is the grid's own turned rectangle, so a town is a block of streets on a hillside and not a
	round plate pressed into it. Water, the beach and anything already placed keep their ground. */
void RMGLayout::gradeTown( Real centreX, Real centreY, const RMGTownPlan& plan,
													 Real acrossStart, Real acrossEnd, Real downStart, Real downEnd )
{
	Real fromAcross = acrossStart - RMG_TOWN_GRADE_MARGIN;
	Real toAcross = acrossEnd + RMG_TOWN_GRADE_MARGIN;
	Real fromDown = downStart - RMG_TOWN_GRADE_MARGIN;
	Real toDown = downEnd + RMG_TOWN_GRADE_MARGIN;

	// a plain map's town is a plate (below), whose rim has more height to ease away
	const Bool plate = !m_style && !m_skeleton;
	const Real gradeFade = plate ? RMG_TOWN_GRADE_FADE * RMG_TOWN_PLATE_FADE : RMG_TOWN_GRADE_FADE;

	Real farAcross = (fabsf( fromAcross ) > fabsf( toAcross )) ? fabsf( fromAcross ) : fabsf( toAcross );
	Real farDown = (fabsf( fromDown ) > fabsf( toDown )) ? fabsf( fromDown ) : fabsf( toDown );
	Int reach = (Int)(sqrtf( farAcross * farAcross + farDown * farDown )
							+ gradeFade * ( 1.0f + RMG_TOWN_GRADE_SWING )) + 1;

	Int centreCellX = (Int)(centreX + 0.5f) + RMG_BORDER_CELLS;
	Int centreCellY = (Int)(centreY + 0.5f) + RMG_BORDER_CELLS;

	const Int blur = RMG_TOWN_GRADE_BLUR;
	Int left = centreCellX - reach - 2 * blur;
	Int top = centreCellY - reach - 2 * blur;
	Int right = centreCellX + reach + 2 * blur;
	Int bottom = centreCellY + reach + 2 * blur;
	if( left < 0 ) left = 0;
	if( top < 0 ) top = 0;
	if( right > m_width - 1 ) right = m_width - 1;
	if( bottom > m_height - 1 ) bottom = m_height - 1;
	if( right <= left || bottom <= top )
		return;

	Int spanX = right - left + 1;
	Int spanY = bottom - top + 1;
	std::vector<Real> field( spanX * spanY );
	std::vector<Real> scratch( spanX * spanY );

	Int x, y;
	for( y = 0; y < spanY; y++ )
		for( x = 0; x < spanX; x++ )
			field[y * spanX + x] = (Real)m_heights[cellIndex( left + x, top + y )];

	// Two box passes each way, which is a tent: a step comes out as an even slope with no corner.
	for( Int pass = 0; pass < 2; pass++ )
	{
		for( y = 0; y < spanY; y++ )
		{
			for( x = 0; x < spanX; x++ )
			{
				Real total = 0.0f;
				Int count = 0;
				for( Int k = x - blur; k <= x + blur; k++ )
				{
					if( k < 0 || k >= spanX )
						continue;
					total += field[y * spanX + k];
					count++;
				}
				scratch[y * spanX + x] = total / (Real)count;
			}
		}

		for( y = 0; y < spanY; y++ )
		{
			for( x = 0; x < spanX; x++ )
			{
				Real total = 0.0f;
				Int count = 0;
				for( Int k = y - blur; k <= y + blur; k++ )
				{
					if( k < 0 || k >= spanY )
						continue;
					total += scratch[k * spanX + x];
					count++;
				}
				field[y * spanX + x] = total / (Real)count;
			}
		}
	}

	Real cosine = Cos( plan.m_rotation );
	Real sine = Sin( plan.m_rotation );
	Real bankLevel = m_waterHeight + RMG_LAKE_BANK;
	Real siteFade = RMG_TOWN_GRADE_FADE * 0.5f;

	/* A plain map's town stands on a plate: the eased ground inside the grid is pressed toward its
		own mean, keeping RMG_TOWN_RELIEF_KEEP of the roll. Eased alone, a town on a hillside kept the
		hill's whole fall, and every lot levelled flat on it stepped down from the one above like a
		staircase. The walks locked to the money are pressed with the rest; skipped, they stood out of
		the plate as a ruled bank. */
	Real plateLevel = 0.0f;
	if( plate )
	{
		Real total = 0.0f;
		Int count = 0;
		for( y = top; y <= bottom; y++ )
		{
			for( x = left; x <= right; x++ )
			{
				Real dx = (Real)(x - RMG_BORDER_CELLS) - centreX;
				Real dy = (Real)(y - RMG_BORDER_CELLS) - centreY;
				Real across = dx * cosine + dy * sine;
				Real down = dy * cosine - dx * sine;
				if( across < fromAcross || across > toAcross || down < fromDown || down > toDown || underwaterAtCell( x, y ) )
					continue;
				total += field[(y - top) * spanX + (x - left)];
				count++;
			}
		}
		if( count > 0 )
			plateLevel = total / (Real)count;
	}
	const Real middleAcross = ( fromAcross + toAcross ) * 0.5f;
	const Real middleDown = ( fromDown + toDown ) * 0.5f;
	const Real halfAcross = ( toAcross - fromAcross ) * 0.5f;
	const Real halfDown = ( toDown - fromDown ) * 0.5f;

	for( y = centreCellY - reach; y <= centreCellY + reach; y++ )
	{
		for( x = centreCellX - reach; x <= centreCellX + reach; x++ )
		{
			if( x < left || y < top || x > right || y > bottom )
				continue;
			if( underwaterAtCell( x, y ) )
				continue;
			// a locked walk keeps its ground, as it does under a pad
			if( !plate && !m_routeLock.empty() && m_routeLock[cellIndex( x, y )] )
				continue;

			Real playX = (Real)(x - RMG_BORDER_CELLS);
			Real playY = (Real)(y - RMG_BORDER_CELLS);
			Real dx = playX - centreX;
			Real dy = playY - centreY;
			Real across = dx * cosine + dy * sine;
			Real down = dy * cosine - dx * sine;

			Real outAcross = 0.0f;
			if( across < fromAcross ) outAcross = fromAcross - across;
			else if( across > toAcross ) outAcross = across - toAcross;
			Real outDown = 0.0f;
			if( down < fromDown ) outDown = fromDown - down;
			else if( down > toDown ) outDown = down - toDown;

			Real outside = sqrtf( outAcross * outAcross + outDown * outDown );
			if( plate )
				outside = plateOutside( across - middleAcross, down - middleDown, halfAcross, halfDown, playX, playY,
																field[(y - top) * spanX + (x - left)] - plateLevel );
			Real fade = gradeFade;
			Real fromCentre = sqrtf( dx * dx + dy * dy );
			if( outside > 0.0f && fromCentre > 0.5f )
				fade *= 1.0f + RMG_TOWN_GRADE_SWING * ringNoise( m_perm, dx / fromCentre, dy / fromCentre,
																												 centreX * 0.37f, centreY * 0.37f );
			if( outside >= fade )
				continue;

			Real weight = 1.0f - fadeCurve( outside / fade );

			Real distanceToShore;
			insideLake( playX, playY, &distanceToShore );
			if( distanceToShore < RMG_PAD_SHORE_KEEP )
				continue;
			Real shoreT = (distanceToShore - RMG_PAD_SHORE_KEEP) / RMG_LAKE_SHORE;
			if( shoreT < 1.0f )
				weight *= shoreT;

			Bool onReserved = FALSE;
			for( UnsignedInt s = 0; s < m_sites.size(); s++ )
			{
				Real sx = playX - m_sites[s].m_cellX;
				Real sy = playY - m_sites[s].m_cellY;
				Real clear = sqrtf( sx * sx + sy * sy ) - m_sites[s].m_radius;
				if( clear < 0.0f )
				{
					onReserved = TRUE;
					break;
				}
				if( clear < siteFade )
					weight *= clear / siteFade;
			}
			if( onReserved || weight <= 0.0f )
				continue;

			Real ground = (Real)m_heights[cellIndex( x, y )];
			Real graded = field[(y - top) * spanX + (x - left)];
			if( plate )
			{
				// terraced fully on the plate and less so out over its rim, where steps would ring it
				graded = plateLevel + RMG_TOWN_RELIEF_KEEP * ( graded - plateLevel );
				graded = lerpReal( graded, terraceLevel( graded ), weight );
			}

			// Averaging in a lake bed must not sink dry ground under the water line.
			Real lowest = (ground < bankLevel) ? ground : bankLevel;
			if( graded < lowest )
				graded = lowest;

			Real h = lerpReal( ground, graded, weight );
			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;
			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
		}
	}
}

/** One road segment, as the two objects W3DRoadBuffer pairs up. They have to stay next to each
	other in the object list, which is why this writes both and nothing goes between them. */
void RMGLayout::addRoad( Real fromX, Real fromY, Real toX, Real toY )
{
	RMGObject point;
	point.m_templateName = m_biome->m_road;
	point.m_uniqueID = AsciiString::TheEmptyString;
	point.m_angle = 0.0f;
	point.m_waypointID = 0;

	point.m_worldX = fromX * MAP_XY_FACTOR;
	point.m_worldY = fromY * MAP_XY_FACTOR;
	point.m_flags = RMG_FLAG_ROAD_POINT1 | RMG_FLAG_ROAD_CORNER_TIGHT;
	m_objects.push_back( point );

	point.m_worldX = toX * MAP_XY_FACTOR;
	point.m_worldY = toY * MAP_XY_FACTOR;
	point.m_flags = RMG_FLAG_ROAD_POINT2;
	m_objects.push_back( point );
	recordRoad( fromX, fromY, toX, toY );
}

/** A plain map's carved ways drawn as roads, the road objects its town streets are, along the line
	each was cut on (smoothed and laid again by then), so the earthwork a player sees has a road on it.
	A way is cut into segments sharing exact ends, which W3DRoadBuffer joins with a curve, none shorter
	than a street's RMG_ROAD_MIN_RUN. It stops at a base's level, since the base is the player's to build
	on, and at the water's edge; where it comes beside a town's street or a way drawn before it, it stops
	short and is left to that one, so it never crosses a street half way along, which the renderer draws
	as two strips laid over each other, and two ways down one corridor draw one road. Trees and rocks on
	the roadway go. Nothing here moves ground. */
void RMGLayout::drawCarvedWays( void )
{
	std::vector<char> drawn( m_width * m_height, 0 );
	auto markBeside = [&]( const std::vector<Int>& cells )
	{
		for( UnsignedInt c = 0; c < cells.size(); c++ )
		{
			const Int x = cells[c] % m_width, y = cells[c] / m_width;
			for( Int dy = -RMG_WAY_APART; dy <= RMG_WAY_APART; dy++ )
				for( Int dx = -RMG_WAY_APART; dx <= RMG_WAY_APART; dx++ )
					if( x + dx >= 0 && y + dy >= 0 && x + dx < m_width && y + dy < m_height )
						drawn[cellIndex( x + dx, y + dy )] = 1;
		}
	};
	auto cellsApart = [&]( Int a, Int b ) -> Real
	{
		const Real dx = (Real)( a % m_width - b % m_width ), dy = (Real)( a / m_width - b / m_width );
		return sqrtf( dx * dx + dy * dy );
	};
	for( UnsignedInt r = 0; r < m_roads.size(); r++ )
		if( !m_roadCarved[r] )
			markBeside( m_roads[r] );
	std::vector<Int> laid;
	for( UnsignedInt r = 0; r < m_roads.size(); r++ )
	{
		if( !m_roadCarved[r] )
			continue;
		const std::vector<Int>& way = m_roads[r];
		std::vector<Int> run;
		for( UnsignedInt k = 0; k <= way.size(); k++ )
		{
			Bool open = k < way.size();
			Bool beside = FALSE;
			if( open )
			{
				const Int x = way[k] % m_width, y = way[k] / m_width;
				const Real playX = (Real)( x - RMG_BORDER_CELLS ), playY = (Real)( y - RMG_BORDER_CELLS );
				open = insidePlayable( x, y ) && dryAt( playX, playY, RMG_WAY_DRY_MARGIN )
					&& distanceToNearestStart( playX, playY ) > RMG_FLAT_RADIUS;
				beside = open && drawn[way[k]];
				// the way meets the road it runs into: the first cell beside it closes the run
				if( beside && !run.empty() )
					run.push_back( way[k] );
			}
			if( open && !beside )
			{
				// a run that starts beside an earlier road starts on it
				if( run.empty() && k > 0 && drawn[way[k - 1]] )
					run.push_back( way[k - 1] );
				run.push_back( way[k] );
				continue;
			}
			// the run's break points, each a street's shortest run past the one before; a short tail joins the last
			std::vector<UnsignedInt> breaks;
			if( !run.empty() && cellsApart( run.front(), run.back() ) >= RMG_ROAD_MIN_RUN )
			{
				breaks.push_back( 0 );
				for( UnsignedInt c = 1; c < run.size(); c++ )
					if( cellsApart( run[breaks.back()], run[c] ) >= RMG_ROAD_MIN_RUN && cellsApart( run[c], run.back() ) >= RMG_ROAD_MIN_RUN )
						breaks.push_back( c );
				if( breaks.size() > 1 && cellsApart( run[breaks.back()], run.back() ) < RMG_ROAD_MIN_RUN )
					breaks.pop_back();
				breaks.push_back( (UnsignedInt)run.size() - 1 );
			}
			// one segment shares neither end with anything: a scrap of road lying on its own
			if( breaks.size() >= 3 )
			{
				for( UnsignedInt k = 0; k + 1 < breaks.size(); k++ )
				{
					const UnsignedInt c = breaks[k], next = breaks[k + 1];
					RMGObject point;
					point.m_templateName = m_biome->m_road;
					point.m_uniqueID = AsciiString::TheEmptyString;
					point.m_angle = 0.0f;
					point.m_waypointID = 0;
					point.m_worldX = (Real)( run[c] % m_width - RMG_BORDER_CELLS ) * MAP_XY_FACTOR;
					point.m_worldY = (Real)( run[c] / m_width - RMG_BORDER_CELLS ) * MAP_XY_FACTOR;
					point.m_flags = RMG_FLAG_ROAD_POINT1;
					m_objects.push_back( point );
					point.m_worldX = (Real)( run[next] % m_width - RMG_BORDER_CELLS ) * MAP_XY_FACTOR;
					point.m_worldY = (Real)( run[next] / m_width - RMG_BORDER_CELLS ) * MAP_XY_FACTOR;
					point.m_flags = RMG_FLAG_ROAD_POINT2;
					m_objects.push_back( point );
				}
				laid.insert( laid.end(), run.begin(), run.end() );
				markBeside( run );
			}
			run.clear();
		}
	}

	// the roadway clear of trees and rocks, as a repair's is
	std::vector<char> roadway( m_width * m_height, 0 );
	for( UnsignedInt c = 0; c < laid.size(); c++ )
	{
		const Int x = laid[c] % m_width, y = laid[c] / m_width;
		for( Int dy = -RMG_PASS_HALF_WIDTH; dy <= RMG_PASS_HALF_WIDTH; dy++ )
			for( Int dx = -RMG_PASS_HALF_WIDTH; dx <= RMG_PASS_HALF_WIDTH; dx++ )
				if( x + dx >= 0 && y + dy >= 0 && x + dx < m_width && y + dy < m_height )
					roadway[cellIndex( x + dx, y + dy )] = 1;
	}
	UnsignedInt kept = 0;
	for( UnsignedInt o = 0; o < m_objects.size(); o++ )
	{
		const RMGObject& object = m_objects[o];
		const Int x = (Int)( object.m_worldX / MAP_XY_FACTOR + 0.5f ) + RMG_BORDER_CELLS;
		const Int y = (Int)( object.m_worldY / MAP_XY_FACTOR + 0.5f ) + RMG_BORDER_CELLS;
		const Bool scenery = object.m_flags == 0 && object.m_waypointID == 0
			&& ( object.m_templateName.startsWith( "Tree" ) || object.m_templateName.startsWith( "Rock" ) );
		if( scenery && x >= 0 && y >= 0 && x < m_width && y < m_height && roadway[cellIndex( x, y )] )
			continue;
		if( kept != o )
			m_objects[kept] = object;
		kept++;
	}
	m_objects.resize( kept );
}

/// Whether a point is far enough from every lake to build or pave on.
Bool RMGLayout::dryAt( Real cellX, Real cellY, Real margin ) const
{
	Real distanceToShore;
	insideLake( cellX, cellY, &distanceToShore );

	return distanceToShore >= margin;
}

/** One block of street, between two crossings. A road object pair does not care whether the ground
	under it is a lake bed - it paves it, and what the player sees is tarmac going into the water. So
	a block that reaches the water keeps only the dry run from each crossing it starts at, and the run
	stops at the bank. Whatever dry ground lies between two wet stretches is left bare: it touches no
	crossing, and a few cells of street on their own are what the old clip left lying about. */
void RMGLayout::addRoadIfDry( Real fromX, Real fromY, Real toX, Real toY )
{
	Real spanX = toX - fromX;
	Real spanY = toY - fromY;
	Real length = sqrtf( spanX * spanX + spanY * spanY );
	Int steps = (Int)(length / RMG_ROAD_STEP) + 1;
	Int firstWet = -1, lastWet = -1;

	for( Int step = 0; step <= steps; step++ )
	{
		Real along = (Real)step / (Real)steps;
		if( dryAt( fromX + spanX * along, fromY + spanY * along, RMG_TOWN_DRY_MARGIN ) )
			continue;
		if( firstWet < 0 )
			firstWet = step;
		lastWet = step;
	}

	if( firstWet < 0 )
	{
		addRoad( fromX, fromY, toX, toY );
		return;
	}

	// the crossing ends keep their exact numbers, which is what joins them to the other streets
	Real dryFrom = (Real)(firstWet - 1) / (Real)steps;
	if( firstWet > 0 && dryFrom * length >= RMG_ROAD_MIN_RUN )
		addRoad( fromX, fromY, fromX + spanX * dryFrom, fromY + spanY * dryFrom );

	Real dryTo = (Real)(lastWet + 1) / (Real)steps;
	if( lastWet < steps && ( 1.0f - dryTo ) * length >= RMG_ROAD_MIN_RUN )
		addRoad( fromX + spanX * dryTo, fromY + spanY * dryTo, toX, toY );
}

/** The flattest buildable spot in a ring around a point, whatever else is on the map. This is how
	anything a particular player is meant to reach gets placed: search from their ground rather
	than over the whole map, or every flat acre in the middle takes all the money and the players
	on the rough side of the map start with nothing. */
Bool RMGLayout::findSiteNear( Real centreX, Real centreY, Real minRadius, Real maxRadius,
															Real clearance, RMGPoint *out ) const
{
	const Int stepsRound = 96;
	Real bestRoughness = 1.0e9f;
	Bool found = FALSE;

	for( Int step = 0; step < stepsRound; step++ )
	{
		Real angle = 2.0f * PI * (Real)step / (Real)stepsRound;
		Real dirX = Cos( angle );
		Real dirY = Sin( angle );

		for( Real radius = minRadius; radius <= maxRadius; radius += 2.0f )
		{
			Real x = centreX + radius * dirX;
			Real y = centreY + radius * dirY;

			/* Inside the playable area, not merely inside the height field: the border is ground
				the camera looks across, and a supply dock out there is a dock nobody can reach.
				The clearance counts towards the edge as well, so a town whose streets are forty
				cells across is not centred four cells from the corner. */
			Real edge = 4.0f + clearance;
			if( x < edge || y < edge || x > (Real)m_settings.m_playableCells - edge ||
					y > (Real)m_settings.m_playableCells - edge )
				continue;

			Int mapX = (Int)(x + 0.5f) + RMG_BORDER_CELLS;
			Int mapY = (Int)(y + 0.5f) + RMG_BORDER_CELLS;
			if( !passableAtCell( mapX, mapY ) )
				continue;

			/* Off the water by something like what is being placed, capped: a town wants its middle
				well clear of a lake, but demanding the whole grid's radius of dry ground would mean
				no town on any map with water on it. What overhangs the bank is clipped where it is
				laid instead. */
			Real dryMargin = 4.0f + clearance * 0.5f;
			if( dryMargin > 26.0f )
				dryMargin = 26.0f;

			Real distanceToShore;
			insideLake( x, y, &distanceToShore );
			if( distanceToShore < dryMargin )
				continue;

			if( !siteIsClear( x, y, clearance ) )
				continue;

			Real roughness = roughnessAt( mapX, mapY, 5 );
			if( roughness < bestRoughness )
			{
				bestRoughness = roughness;
				out->m_cellX = x;
				out->m_cellY = y;
				found = TRUE;
			}
		}
	}

	return found;
}

/** Same search as findSiteNear, scored to sit on a ring of one radius rather than
	on whichever patch in the annulus is flattest. Every player of one resource
	kind shares that radius, so one seed cannot hand somebody a dock at 18 and
	somebody else the same dock at 30. */
Bool RMGLayout::findSiteOnRing( Real centreX, Real centreY, Real targetRadius, Real band,
																Real clearance, RMGPoint *out ) const
{
	if( targetRadius < 1.0f )
		targetRadius = 1.0f;
	if( band < 2.0f )
		band = 2.0f;

	const Int stepsRound = 96;
	Real bestScore = 1.0e9f;
	Bool found = FALSE;

	Real minRadius = targetRadius - band;
	if( minRadius < 1.0f )
		minRadius = 1.0f;
	Real maxRadius = targetRadius + band;

	for( Int step = 0; step < stepsRound; step++ )
	{
		Real angle = 2.0f * PI * (Real)step / (Real)stepsRound;
		Real dirX = Cos( angle );
		Real dirY = Sin( angle );

		for( Real radius = minRadius; radius <= maxRadius; radius += 2.0f )
		{
			Real x = centreX + radius * dirX;
			Real y = centreY + radius * dirY;

			Real edge = 4.0f + clearance;
			if( x < edge || y < edge || x > (Real)m_settings.m_playableCells - edge ||
					y > (Real)m_settings.m_playableCells - edge )
				continue;

			Int mapX = (Int)(x + 0.5f) + RMG_BORDER_CELLS;
			Int mapY = (Int)(y + 0.5f) + RMG_BORDER_CELLS;
			if( !passableAtCell( mapX, mapY ) )
				continue;

			Real dryMargin = 4.0f + clearance * 0.5f;
			if( dryMargin > 26.0f )
				dryMargin = 26.0f;

			Real distanceToShore;
			insideLake( x, y, &distanceToShore );
			if( distanceToShore < dryMargin )
				continue;

			if( !siteIsClear( x, y, clearance ) )
				continue;

			Real roughness = roughnessAt( mapX, mapY, 5 );
			Real score = fabsf( radius - targetRadius ) + roughness * 0.35f;
			if( score < bestScore )
			{
				bestScore = score;
				out->m_cellX = x;
				out->m_cellY = y;
				found = TRUE;
			}
		}
	}

	return found;
}

Bool RMGLayout::spotIsBuildable( Real x, Real y, Real clearance ) const
{
	Real edge = 4.0f + clearance;
	if( x < edge || y < edge || x > (Real)m_settings.m_playableCells - edge ||
			y > (Real)m_settings.m_playableCells - edge )
		return FALSE;

	Int mapX = (Int)(x + 0.5f) + RMG_BORDER_CELLS;
	Int mapY = (Int)(y + 0.5f) + RMG_BORDER_CELLS;
	if( !passableAtCell( mapX, mapY ) )
		return FALSE;

	Real dryMargin = 4.0f + clearance * 0.5f;
	if( dryMargin > 26.0f )
		dryMargin = 26.0f;

	Real distanceToShore;
	insideLake( x, y, &distanceToShore );
	if( distanceToShore < dryMargin )
		return FALSE;

	return siteIsClear( x, y, clearance );
}

Real RMGLayout::outwardBearing( Real cellX, Real cellY ) const
{
	Real centre = (Real)m_settings.m_playableCells * 0.5f;
	return snapAngle45( ATan2( cellY - centre, cellX - centre ) );
}

Bool RMGLayout::findSiteOnBearing( Real centreX, Real centreY, Real targetRadius, Real band,
																	 Real bearing, Real clearance, RMGPoint *out ) const
{
	bearing = snapAngle45( bearing );
	if( targetRadius < 1.0f )
		targetRadius = 1.0f;
	if( band < 2.0f )
		band = 2.0f;

	Real dirX = Cos( bearing );
	Real dirY = Sin( bearing );
	Real minRadius = targetRadius - band;
	if( minRadius < 1.0f )
		minRadius = 1.0f;

	Real bestScore = 1.0e9f;
	Bool found = FALSE;

	for( Real radius = minRadius; radius <= targetRadius + band; radius += 1.0f )
	{
		Real x = centreX + radius * dirX;
		Real y = centreY + radius * dirY;
		if( !spotIsBuildable( x, y, clearance ) )
			continue;

		Int mapX = (Int)(x + 0.5f) + RMG_BORDER_CELLS;
		Int mapY = (Int)(y + 0.5f) + RMG_BORDER_CELLS;
		Real score = fabsf( radius - targetRadius ) + roughnessAt( mapX, mapY, 5 ) * 0.2f;
		if( score < bestScore )
		{
			bestScore = score;
			out->m_cellX = x;
			out->m_cellY = y;
			found = TRUE;
		}
	}

	return found;
}

Bool RMGLayout::findSiteOnCompass( Real centreX, Real centreY, Real targetRadius, Real band,
																	 Real preferred, Real clearance, RMGPoint *out ) const
{
	preferred = snapAngle45( preferred );
	static const Int turn[8] = { 0, 1, -1, 2, -2, 3, -3, 4 };
	for( Int k = 0; k < 8; k++ )
	{
		Real bearing = preferred + (Real)turn[k] * (PI * 0.25f);
		if( findSiteOnBearing( centreX, centreY, targetRadius, band, bearing, clearance, out ) )
			return TRUE;
	}
	return FALSE;
}

void RMGLayout::floodDistancesFromCell( Int startX, Int startY, std::vector<Int>& dist ) const
{
	std::vector<Int> queue( m_width * m_height );
	floodDistancesFromCell( startX, startY, dist, queue );
}

/** The same with the queue in hand, a cell per map cell, so a pool job that floods allocates nothing
	once dist is that size too. */
void RMGLayout::floodDistancesFromCell( Int startX, Int startY, std::vector<Int>& dist, std::vector<Int>& queue ) const
{
	Int width = m_width;
	dist.assign( width * m_height, -1 );

	if( startX < 0 || startY < 0 || startX >= width - 1 || startY >= m_height - 1 )
		return;
	if( !passableAtCell( startX, startY ) )
		return;

	dist[startY * width + startX] = 0;
	queue[0] = startY * width + startX;
	Int tail = 1;

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	for( Int head = 0; head < tail; )
	{
		Int index = queue[head++];
		Int x = index % width;
		Int y = index / width;
		Int here = dist[index];

		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 0 || ny < 0 || nx >= width - 1 || ny >= m_height - 1 )
				continue;

			Int next = ny * width + nx;
			if( dist[next] >= 0 || !passableAtCell( nx, ny ) )
				continue;

			dist[next] = here + 1;
			queue[tail++] = next;
		}
	}
}

struct RMGStartFloods
{
	const RMGLayout *layout;
	std::vector< std::vector<Int> > *dist;
	std::vector< std::vector<Int> > *queues;
};

void RMGLayout::floodFromStartJob( Int start, void *context )
{
	RMGStartFloods *floods = (RMGStartFloods *)context;
	const RMGLayout *layout = floods->layout;
	Int x = (Int)(layout->m_starts[start].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int y = (Int)(layout->m_starts[start].m_cellY + 0.5f) + RMG_BORDER_CELLS;
	layout->floodDistancesFromCell( x, y, (*floods->dist)[start], (*floods->queues)[start] );
}

/** A flood from every start, on the pool: each writes only its own start's arrays, sized here first. */
void RMGLayout::floodFromEveryStart( std::vector< std::vector<Int> >& dist )
{
	const Int players = (Int)m_starts.size();
	const Int cells = m_width * m_height;
	dist.resize( players );
	m_floodQueues.resize( players );
	for( Int i = 0; i < players; i++ )
	{
		dist[i].resize( cells );
		m_floodQueues[i].resize( cells );
	}
	RMGStartFloods floods = { this, &dist, &m_floodQueues };
	JobSystem::parallel_for( players, 1, floodFromStartJob, &floods );
}

/** The dock worth fighting over: a cell both players can walk to, as even a walk
	from each as the ground allows. Euclidean midpoints miss a cliff between the
	two starts; the two floods do not. */
Bool RMGLayout::findContestedSite( Int startA, Int startB, Real minWalk, Real clearance,
																	 RMGPoint *out ) const
{
	Int ax = (Int)(m_starts[startA].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int ay = (Int)(m_starts[startA].m_cellY + 0.5f) + RMG_BORDER_CELLS;
	Int bx = (Int)(m_starts[startB].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int by = (Int)(m_starts[startB].m_cellY + 0.5f) + RMG_BORDER_CELLS;

	std::vector<Int> distA;
	std::vector<Int> distB;
	floodDistancesFromCell( ax, ay, distA );
	floodDistancesFromCell( bx, by, distB );

	Int minWalkCells = (Int)minWalk;
	Real bestScore = 1.0e9f;
	Bool found = FALSE;

	for( Int y = RMG_BORDER_CELLS + 4; y < m_height - RMG_BORDER_CELLS - 4; y++ )
	{
		for( Int x = RMG_BORDER_CELLS + 4; x < m_width - RMG_BORDER_CELLS - 4; x++ )
		{
			Int index = y * m_width + x;
			if( distA[index] < minWalkCells || distB[index] < minWalkCells )
				continue;
			if( !passableAtCell( x, y ) )
				continue;

			Real cellX = (Real)(x - RMG_BORDER_CELLS);
			Real cellY = (Real)(y - RMG_BORDER_CELLS);

			Real dryMargin = 4.0f + clearance * 0.5f;
			if( dryMargin > 26.0f )
				dryMargin = 26.0f;
			Real distanceToShore;
			insideLake( cellX, cellY, &distanceToShore );
			if( distanceToShore < dryMargin )
				continue;

			if( !siteIsClear( cellX, cellY, clearance ) )
				continue;

			Int balance = distA[index] - distB[index];
			if( balance < 0 )
				balance = -balance;

			Real score = (Real)balance * 8.0f + (Real)(distA[index] + distB[index]) * 0.15f;
			if( score < bestScore )
			{
				bestScore = score;
				out->m_cellX = cellX;
				out->m_cellY = cellY;
				found = TRUE;
			}
		}
	}

	return found;
}

/** Walk distances from every start over the ground as it stands, and for each cell the start
	that walks there first and by how many cells it beats the next one. A tie goes to the lower
	seat, the way anything that counts ownership by walk breaks it too. */
void RMGLayout::walkFromStarts( std::vector< std::vector<Int> >& walk, std::vector<Int>& owner,
															 std::vector<Int>& lead )
{
	/* The build levels the bases once more after everything is placed, and a pad that crept into a
		base's blend ring comes out of that with a step in it. Walk the ground as it will end up. */
	flattenBases();
	buildPassability();

	Int players = (Int)m_starts.size();
	floodFromEveryStart( walk );

	Int cells = m_width * m_height;
	owner.assign( cells, -1 );
	lead.assign( cells, 0 );
	for( Int c = 0; c < cells; c++ )
	{
		Int best = -1;
		Int second = -1;
		for( Int i = 0; i < players; i++ )
		{
			Int d = walk[i][c];
			if( d < 0 )
				continue;
			if( best < 0 || d < walk[best][c] )
			{
				second = best;
				best = i;
			}
			else if( second < 0 || d < walk[second][c] )
				second = i;
		}

		owner[c] = best;
		if( best >= 0 )
			lead[c] = ( second < 0 ) ? RMG_MONEY_NO_RIVAL : walk[second][c] - walk[best][c];
	}
}

/** A spot this player walks to in target cells, give or take the band, and reaches before anybody
	else by minLead to maxLead cells. Ownership is the walk, not the ring: a dock eighteen cells
	out in a straight line is forty on foot when a cliff is in the way, and a derrick put on the
	flank of an eight-seat ring is closer to the neighbour than to the player it was meant for. */
Bool RMGLayout::findSiteByWalk( const std::vector< std::vector<Int> >& walk,
																const std::vector<Int>& owner, const std::vector<Int>& lead,
																Int player, Int target, Int band, Int minLead, Int maxLead,
																Real bearing, Real clearance, RMGPoint *out, Int rival,
																Int rivalMaxLead ) const
{
	const std::vector<Int>& mine = walk[player];
	Real startX = m_starts[player].m_cellX;
	Real startY = m_starts[player].m_cellY;
	Real playable = (Real)m_settings.m_playableCells;
	Real ring = (Real)target / RMG_MONEY_WALK_STRETCH;
	Real edge = 4.0f + clearance;
	Real dryMargin = 4.0f + clearance * 0.5f;
	if( dryMargin > 26.0f )
		dryMargin = 26.0f;

	Real bestScore = 1.0e9f;
	Bool found = FALSE;

	for( Int y = RMG_BORDER_CELLS; y < m_height - RMG_BORDER_CELLS; y++ )
	{
		for( Int x = RMG_BORDER_CELLS; x < m_width - RMG_BORDER_CELLS; x++ )
		{
			Int index = cellIndex( x, y );
			if( owner[index] != player )
				continue;

			Int off = mine[index] - target;
			if( off < 0 )
				off = -off;
			/* A contested dock leads against its rival, not against whoever is second nearest: the
				lead counted against the next seat round the other way put a dock meant for one gap in
				the gap beside it, which then had two. */
			Int cellLead = lead[index];
			if( rival >= 0 )
				cellLead = walk[rival][index] < 0 ? RMG_MONEY_NO_RIVAL : walk[rival][index] - mine[index];
			if( off > band || cellLead < minLead || cellLead > maxLead )
				continue;
			if( rival >= 0 && !placersSide( x, y, player, rival, rivalMaxLead ) )
				continue;

			Real cellX = (Real)(x - RMG_BORDER_CELLS);
			Real cellY = (Real)(y - RMG_BORDER_CELLS);
			if( cellX < edge || cellY < edge || cellX > playable - edge || cellY > playable - edge )
				continue;

			Real turn = ATan2( cellY - startY, cellX - startX ) - bearing;
			while( turn > PI )
				turn -= 2.0f * PI;
			while( turn < -PI )
				turn += 2.0f * PI;

			/* Among spots of equal walk, the one that is also on the straight-line ring, so a seat
				does not get its money as far on foot but a lot further as the crow flies. */
			Real dx = cellX - startX;
			Real dy = cellY - startY;
			Real straight = sqrtf( dx * dx + dy * dy ) - ring;

			// roughness only ever adds, so a spot already worse than the best skips the costly checks
			Real score = (Real)off + fabsf( turn ) * 4.0f + fabsf( straight ) * 0.5f;
			if( score >= bestScore )
				continue;

			Real distanceToShore;
			insideLake( cellX, cellY, &distanceToShore );
			if( distanceToShore < dryMargin )
				continue;

			if( !siteIsClear( cellX, cellY, clearance ) )
				continue;

			score += roughnessAt( x, y, 5 ) * 0.35f;
			if( score < bestScore )
			{
				bestScore = score;
				out->m_cellX = cellX;
				out->m_cellY = cellY;
				found = TRUE;
			}
		}
	}

	return found;
}

/** The walk the flood counts is four-way, so on a ring turned off the axes a contested dock could
	lead its placer by three steps and still stand twenty cells nearer the rival in a straight line
	and on the game's own eight-way paths. The dock has to be on the placer's side as the crow flies
	as well, by RMG_CONTESTED_LEAD_MIN to maxLead cells: octile distance in tenths (10 straight, 14
	diagonal, as the pathfinder counts), integer, from the cell to each start's own cell. */
Bool RMGLayout::placersSide( Int mapX, Int mapY, Int player, Int rival, Int maxLead ) const
{
	Int cellX = mapX - RMG_BORDER_CELLS;
	Int cellY = mapY - RMG_BORDER_CELLS;
	Int octile[2];
	Int who[2] = { player, rival };
	for( Int k = 0; k < 2; k++ )
	{
		Int dx = abs( cellX - (Int)( m_starts[who[k]].m_cellX + 0.5f ) );
		Int dy = abs( cellY - (Int)( m_starts[who[k]].m_cellY + 0.5f ) );
		octile[k] = dx > dy ? dx * 10 + dy * 4 : dy * 10 + dx * 4;
	}
	Int lead = octile[1] - octile[0];
	return lead >= RMG_CONTESTED_LEAD_MIN * 10 && lead <= maxLead * 10;
}

/** Mark the walk from a start to its money so that no later pad - a town, a bunker on a ramp -
	levels the ground it runs over. A bunker pad beside a base on a hill used to leave a step
	across the way to the dock, and the dock twenty cells from the yard became forty-five. The
	route is the flood walked backwards from the site, downhill in walk count, a band either side. */
void RMGLayout::lockRoute( const std::vector<Int>& walk, const RMGPoint& site )
{
	if( m_routeLock.size() != m_heights.size() )
		m_routeLock.assign( m_heights.size(), 0 );

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	Int x = (Int)(site.m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int y = (Int)(site.m_cellY + 0.5f) + RMG_BORDER_CELLS;
	Int here = walk[cellIndex( x, y )];

	while( here > 0 )
	{
		for( Int dy = -RMG_ROUTE_LOCK_HALF_WIDTH; dy <= RMG_ROUTE_LOCK_HALF_WIDTH; dy++ )
		{
			for( Int dx = -RMG_ROUTE_LOCK_HALF_WIDTH; dx <= RMG_ROUTE_LOCK_HALF_WIDTH; dx++ )
			{
				Int lx = x + dx;
				Int ly = y + dy;
				if( lx >= 0 && ly >= 0 && lx < m_width && ly < m_height )
					m_routeLock[cellIndex( lx, ly )] = 1;
			}
		}

		Bool stepped = FALSE;
		for( Int k = 0; k < 4 && !stepped; k++ )
		{
			Int nx = x + offsetX[k];
			Int ny = y + offsetY[k];
			if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
				continue;
			if( walk[cellIndex( nx, ny )] == here - 1 )
			{
				x = nx;
				y = ny;
				here--;
				stepped = TRUE;
			}
		}
		if( !stepped )
			break;
	}
}

/** Every player's money, placed by walking rather than by ring: a home dock, two derricks and two
	small piles that each player reaches first, by a clear lead, at the same walk give or take a
	couple of cells for every seat. Then one contested dock a player, on the walk midline with the
	neighbour who shares it, a few cells to the placing player's side so each seat owns exactly
	one. The ground is re-walked between kinds because every pad moves a slope. All of it is laid
	before towns and bunkers, whose pads are kept off the reserved sites, so nothing walls it. */
void RMGLayout::placeSupplyAndDerricks( void )
{
	Int supplyID = 1;
	Int derrickID = 1;
	Int pileID = 1;
	Real playable = (Real)m_settings.m_playableCells;
	Int players = (Int)m_starts.size();

	std::vector< std::vector<Int> > walk;
	std::vector<Int> owner;
	std::vector<Int> lead;

	Real homeRing = RMG_HOME_SUPPLY_MIN + 4.0f + hashUnit( m_settings.m_seed, 19, 1 ) * 8.0f;
	Real derrickRing = RMG_BLEND_RADIUS + 8.0f + hashUnit( m_settings.m_seed, 23, 2 ) * 8.0f;
	Real pileRing = derrickRing + 24.0f + hashUnit( m_settings.m_seed, 29, 3 ) * 6.0f;

	struct RMGMoney
	{
		const char *m_template;
		const char *m_name;
		Real m_ring;		///< cells in a straight line; the walk target is a little longer
		Real m_turn;		///< preferred bearing off the outward one
		Real m_clearance;
		Real m_pad;
		Real m_blend;
		Int m_maxLead;		///< the most a rival may trail by: low keeps it between two seats
	};
	const RMGMoney theMoney[] =
	{
		{ "SupplyDock", "SupplyDock", homeRing, 0.0f, RMG_SITE_CLEARANCE, RMG_PAD_RADIUS, RMG_PAD_BLEND, RMG_MONEY_NO_RIVAL },
		{ "TechOilDerrick", "Derrick", derrickRing, PI * 0.5f, RMG_SITE_CLEARANCE, RMG_PAD_RADIUS, RMG_PAD_BLEND, RMG_MONEY_NO_RIVAL },
		{ "TechOilDerrick", "Derrick", derrickRing + 12.0f, -PI * 0.5f, RMG_SITE_CLEARANCE, RMG_PAD_RADIUS, RMG_PAD_BLEND, RMG_MONEY_NO_RIVAL },
		{ "SupplyPileSmall", "SupplyPile", pileRing, PI * 0.25f, RMG_PILE_CLEARANCE, RMG_PILE_PAD, RMG_PILE_BLEND, RMG_MONEY_NO_RIVAL },
		{ "SupplyPileSmall", "SupplyPile", pileRing + 10.0f, -PI * 0.25f, RMG_PILE_CLEARANCE, RMG_PILE_PAD, RMG_PILE_BLEND, RMG_MONEY_NO_RIVAL },
	};
	std::vector<RMGMoney> moneyList( theMoney, theMoney + sizeof(theMoney) / sizeof(theMoney[0]) );

	/* A style's money is the shipped map's: the dock on the player's own plateau, out behind the base,
		one derrick down in the bowl in front of it, and the small piles out on the ridges towards both
		neighbours, near enough the midline to be fought over. No contested dock. */
	if( m_style )
	{
		const Real k = m_styleScale;
		const Real derrick = m_bowlSeatRing - 0.6f * ( m_bowlRim - m_bowlRampLength );
		const Real neighbour = 2.0f * m_bowlSeatRing * Sin( PI / (Real)players );
		const Int pileLead = (Int)( 0.15f * neighbour );
		const RMGMoney dock = { "SupplyDock", "SupplyDock", m_style->m_homeSupply * k, 0.0f,
			RMG_SITE_CLEARANCE, RMG_PAD_RADIUS, RMG_PAD_BLEND, RMG_MONEY_NO_RIVAL };
		const RMGMoney oil = { "TechOilDerrick", "Derrick", derrick, PI,
			RMG_SITE_CLEARANCE, RMG_PAD_RADIUS, RMG_PAD_BLEND, RMG_MONEY_NO_RIVAL };
		moneyList.clear();
		moneyList.push_back( dock );
		moneyList.push_back( oil );
		for( Int p = 0; p < m_style->m_piles; p++ )
		{
			const RMGMoney pile = { "SupplyPileSmall", "SupplyPile", 0.42f * neighbour + 8.0f * (Real)( p / 2 ),
				( p % 2 ) ? -PI * 0.5f : PI * 0.5f, RMG_PILE_CLEARANCE, RMG_PILE_PAD, RMG_PILE_BLEND,
				pileLead > RMG_MONEY_LEAD ? pileLead : RMG_MONEY_LEAD };
			moneyList.push_back( pile );
		}
	}

	const Int numMoney = (Int)moneyList.size();
	const Bool plainWays = !m_style && !m_skeleton;
	if( plainWays )
		markWayDistance();
	static const Int theBands[] = { 1, 3, 6, 10 };
	const Int numBands = sizeof(theBands) / sizeof(theBands[0]);

	for( Int m = 0; m < numMoney; m++ )
	{
		const RMGMoney& money = moneyList[m];
		walkFromStarts( walk, owner, lead );
		Int target = (Int)(money.m_ring * RMG_MONEY_WALK_STRETCH + 0.5f);

		for( Int i = 0; i < players; i++ )
		{
			Real sx = m_starts[i].m_cellX;
			Real sy = m_starts[i].m_cellY;
			Real bearing = outwardBearing( sx, sy ) + money.m_turn;

			/* A plain map's pad first looks along the walk for a site off the carved ways
				(markWayDistance), and searches as before, ways and all, only when that finds none. */
			RMGPoint site;
			Bool found = FALSE;
			Bool walked = TRUE;
			for( Int pass = plainWays ? 0 : 1; pass < 2 && !found; pass++ )
			{
				m_padsKeepOffWays = pass == 0;
				walked = TRUE;
				for( Int b = 0; b < numBands && !found; b++ )
					found = findSiteByWalk( walk, owner, lead, i, target, theBands[b], RMG_MONEY_LEAD,
																	money.m_maxLead, bearing, money.m_clearance, &site );
				if( !found )
					found = findSiteByWalk( walk, owner, lead, i, target, 16, 1, RMG_MONEY_NO_RIVAL,
																	bearing, money.m_clearance * 0.5f, &site );
				if( !found && !m_padsKeepOffWays )
				{
					walked = FALSE;
					found = findSiteOnCompass( sx, sy, money.m_ring, 12.0f, bearing, 3.0f, &site ) ||
									findSiteNear( sx, sy, money.m_ring - 8.0f, money.m_ring + 20.0f, 2.0f, &site );
				}
			}
			m_padsKeepOffWays = FALSE;
			if( !found )
				continue;

			// Lock the walk first, so the pad's own blend does not lengthen the walk it was chosen by.
			if( walked )
			{
				RMGMoneyWalk planned;
				planned.m_player = i;
				planned.m_rival = -1;
				planned.m_object = -1;
				planned.m_cellX = (Int)(site.m_cellX + 0.5f) + RMG_BORDER_CELLS;
				planned.m_cellY = (Int)(site.m_cellY + 0.5f) + RMG_BORDER_CELLS;
				planned.m_walk = walk[i][cellIndex( planned.m_cellX, planned.m_cellY )];
				m_moneyWalks.push_back( planned );
			}
			if( walked )
				lockRoute( walk[i], site );
			flattenPad( site.m_cellX, site.m_cellY, money.m_pad, money.m_blend );

			Real angle = snapAngle45( ATan2( sy - site.m_cellY, sx - site.m_cellX ) );

			Int *counter = &pileID;
			if( strcmp( money.m_template, "SupplyDock" ) == 0 )
				counter = &supplyID;
			else if( strcmp( money.m_template, "TechOilDerrick" ) == 0 )
				counter = &derrickID;

			AsciiString uniqueID;
			uniqueID.format( "%s %d", money.m_name, (*counter)++ );
			addObject( money.m_template, uniqueID.str(), site.m_cellX, site.m_cellY, angle );
			reserveSite( site.m_cellX, site.m_cellY, money.m_clearance );
		}
	}

	if( m_style )
		return;

	/* The contested docks: one walk target for every seat, half the shortest walk any player has
		to its rival, so no seat's fight is closer to home than another's. With three seats or more
		the rival is the next seat round the middle of the map, so every gap between two neighbours
		gets one dock and every seat places one. The nearest seat by walk, as it was, ties both ring
		neighbours on a square ring and gave the lowest index both: two docks in one gap, none in the
		gap across from it. A next seat that cannot be walked to gives way to the nearest one. */
	walkFromStarts( walk, owner, lead );
	std::vector<Int> rival( players, -1 );
	if( players >= 3 )
	{
		Real middle = playable * 0.5f;
		std::vector<Real> bearing( players );
		std::vector<Int> seatsRound;
		for( Int i = 0; i < players; i++ )
		{
			bearing[i] = ATan2( m_starts[i].m_cellY - middle, m_starts[i].m_cellX - middle );
			// by bearing, ties by index: insertion keeps it stable
			Int at = (Int)seatsRound.size();
			while( at > 0 && bearing[seatsRound[at - 1]] > bearing[i] )
				at--;
			seatsRound.insert( seatsRound.begin() + at, i );
		}
		for( Int k = 0; k < players; k++ )
			rival[seatsRound[k]] = seatsRound[( k + 1 ) % players];
	}
	Int contestedWalk = -1;
	for( Int i = 0; i < players; i++ )
	{
		Int rivalWalk = -1;
		if( rival[i] >= 0 )
		{
			Int x = (Int)(m_starts[rival[i]].m_cellX + 0.5f) + RMG_BORDER_CELLS;
			Int y = (Int)(m_starts[rival[i]].m_cellY + 0.5f) + RMG_BORDER_CELLS;
			rivalWalk = walk[i][cellIndex( x, y )];
		}
		if( rivalWalk < 0 )
		{
			rival[i] = -1;
			for( Int j = 0; j < players; j++ )
			{
				if( j == i )
					continue;
				Int x = (Int)(m_starts[j].m_cellX + 0.5f) + RMG_BORDER_CELLS;
				Int y = (Int)(m_starts[j].m_cellY + 0.5f) + RMG_BORDER_CELLS;
				Int d = walk[i][cellIndex( x, y )];
				if( d >= 0 && ( rivalWalk < 0 || d < rivalWalk ) )
				{
					rivalWalk = d;
					rival[i] = j;
				}
			}
		}
		if( rivalWalk >= 0 && ( contestedWalk < 0 || rivalWalk / 2 < contestedWalk ) )
			contestedWalk = rivalWalk / 2;
	}

	static const Int theContestedBands[] = { 3, 8, 16, 1000 };
	const Int numContestedBands = sizeof(theContestedBands) / sizeof(theContestedBands[0]);
	for( Int i = 0; i < players; i++ )
	{
		Real sx = m_starts[i].m_cellX;
		Real sy = m_starts[i].m_cellY;
		RMGPoint site;
		Bool found = FALSE;

		if( rival[i] >= 0 && contestedWalk > 0 )
		{
			Real toward = ATan2( m_starts[rival[i]].m_cellY - sy, m_starts[rival[i]].m_cellX - sx );
			for( Int b = 0; b < numContestedBands && !found; b++ )
			{
				found = findSiteByWalk( walk, owner, lead, i, contestedWalk, theContestedBands[b],
																RMG_CONTESTED_LEAD_MIN, RMG_CONTESTED_LEAD_MAX, toward,
																RMG_SITE_CLEARANCE, &site, rival[i] );
				/* Where the four-way walk favours the placer, every spot it gives a small lead is
					nearer the rival in a straight line, so the lead window moves to the straight line
					instead and the walk only has to agree on who gets there first. */
				if( !found )
					found = findSiteByWalk( walk, owner, lead, i, contestedWalk, theContestedBands[b],
																	RMG_CONTESTED_LEAD_MIN, RMG_MONEY_NO_RIVAL, toward,
																	RMG_SITE_CLEARANCE, &site, rival[i], RMG_CONTESTED_LEAD_MAX );
			}
			if( !found )
				found = findSiteByWalk( walk, owner, lead, i, contestedWalk, 1000, 1,
																RMG_CONTESTED_LEAD_MAX * 2, toward, 3.0f, &site, rival[i] );
			if( found )
			{
				RMGMoneyWalk planned;
				planned.m_player = i;
				planned.m_rival = rival[i];
				planned.m_object = (Int)m_objects.size();		// added just below
				planned.m_cellX = (Int)(site.m_cellX + 0.5f) + RMG_BORDER_CELLS;
				planned.m_cellY = (Int)(site.m_cellY + 0.5f) + RMG_BORDER_CELLS;
				planned.m_walk = walk[i][cellIndex( planned.m_cellX, planned.m_cellY )];
				m_moneyWalks.push_back( planned );

				lockRoute( walk[i], site );
				lockRoute( walk[rival[i]], site );
				flattenPad( site.m_cellX, site.m_cellY, RMG_PAD_RADIUS, RMG_PAD_BLEND );
			}
		}
		Bool padded = found;

		if( !found )
		{
			// old way: on this player's side of the straight line to the nearest neighbour
			Real nearest = 1.0e9f;
			Int nearestIndex = i;
			for( Int j = 0; j < players; j++ )
			{
				if( j == i )
					continue;
				Real dx = sx - m_starts[j].m_cellX;
				Real dy = sy - m_starts[j].m_cellY;
				Real distance = sqrtf( dx * dx + dy * dy );
				if( distance < nearest )
				{
					nearest = distance;
					nearestIndex = j;
				}
			}

			Real toward = snapAngle45( ATan2( m_starts[nearestIndex].m_cellY - sy,
																				m_starts[nearestIndex].m_cellX - sx ) );
			Real radius = nearest * 0.42f;
			if( radius < RMG_HOME_SUPPLY_MAX + 4.0f )
				radius = RMG_HOME_SUPPLY_MAX + 4.0f;
			if( radius > playable * 0.38f )
				radius = playable * 0.38f;

			found = findSiteOnCompass( sx, sy, radius, 12.0f, toward, 3.0f, &site ) ||
							findContestedSite( i, nearestIndex, RMG_BLEND_RADIUS, 3.0f, &site );
		}

		if( !found )
			continue;

		Real angle = snapAngle45( ATan2( sy - site.m_cellY, sx - site.m_cellX ) );

		AsciiString uniqueID;
		uniqueID.format( "SupplyDock %d", supplyID++ );
		addObject( "SupplyDock", uniqueID.str(), site.m_cellX, site.m_cellY, angle );
		if( !padded )
			flattenPad( site.m_cellX, site.m_cellY, RMG_PAD_RADIUS, RMG_PAD_BLEND );
		reserveSite( site.m_cellX, site.m_cellY, RMG_SITE_CLEARANCE );
	}
}

/** A town is generated, not stamped. Each one rolls its own grid - how many streets it has each
	way, how far apart they run, how deep its plots are, which way the whole thing faces - and the
	buildings come off a list walked in order down each street with gaps left in it. Anything that
	would land in a lake is dropped where it is laid rather than kept out by making the town smaller,
	so a town on a bank keeps its shape and loses the street that ran into the water. */
/* Two rows of frontage, because a street has two sides and a town where every building is the
	same one is a warehouse estate. The list is walked in order along a street so neighbours
	differ, and each town starts at a different place in it. The lane map's ruins take from it too. */
static const char *theStreetNames[] =
{
	"StanApartment01", "StanSmallRetail01", "StanHotel01", "StanConvenienceStore01",
	"StanApartment02", "StanSmallRetail02", "StanRestaurant01", "StanSmallRetail03",
	"AsianRetailStore01", "AsianOffice01", "AsianHotel01", "AsianBank",
	"AsianRetailStore02", "CivilianHighrise01", "AsianArcade", "CivilianHighrise02"
};
static const Int numStreetNames = sizeof(theStreetNames) / sizeof(theStreetNames[0]);

void RMGLayout::placeTowns( void )
{
	Int buildingID = 1;
	// Two at the least: one town on a map is a landmark, two are a place the fight moves between.
	Int towns = m_settings.m_numPlayers / 2;
	if( towns < 2 )
		towns = 2;

	Int townsPlaced = 0;
	for( Int town = 0; town < towns; town++ )
	{
		/* The next start along when this one's surroundings are full: a river or the money can take
			all the room near one base and leave plenty near the next. Then the middle of the map,
			which is between everybody by construction. A plan that fits nowhere is rolled again, and a
			map about to end with no town at all then tries as many of the smallest plan, three streets
			each way on the closest blocks: with one roll a town, a third of the four-seat maps ended
			with no town, though a small one had room. */
		Int rolls = (town == towns - 1 && townsPlaced == 0) ? 2 * RMG_TOWN_REROLLS : RMG_TOWN_REROLLS;
		RMGTownPlan plan;
		RMGPoint site;
		Bool found = FALSE;
		for( Int roll = 0; roll < rolls && !found; roll++ )
		{
			rollTownPlan( m_settings.m_seed, town + 16 * roll, roll >= RMG_TOWN_REROLLS, &plan );

			// Out of everybody's base, in the ground between them, which is where a town is worth
			// fighting through rather than one more thing in somebody's back garden.
			Real inner = RMG_BLEND_RADIUS + plan.m_radius + 6.0f;
			Real outer = inner + (Real)m_settings.m_playableCells * 0.22f;
			UnsignedInt which = (UnsignedInt)town % (UnsignedInt)m_starts.size();

			for( UnsignedInt tried = 0; tried < m_starts.size() && !found; tried++ )
			{
				UnsignedInt around = ( which + tried ) % (UnsignedInt)m_starts.size();
				found = findSiteNear( m_starts[around].m_cellX, m_starts[around].m_cellY, inner, outer,
															plan.m_radius, &site );
			}
			if( !found )
			{
				Real middle = (Real)m_settings.m_playableCells * 0.5f;
				found = findSiteNear( middle, middle, 0.0f, middle, plan.m_radius, &site );
			}
		}
		/* A plain map still without a town at its last one takes the smallest plan wherever half its
			radius is clear, and keeps each building off the bases and the money on its own instead. A
			crowded six-seat map had no ground the whole grid's radius clear of every pad. */
		Bool squeezed = FALSE;
		if( !found && town == towns - 1 && townsPlaced == 0 && !m_style && !m_skeleton )
		{
			Real middle = (Real)m_settings.m_playableCells * 0.5f;
			found = findSiteNear( middle, middle, 0.0f, middle, plan.m_radius * RMG_TOWN_SQUEEZE, &site );
			squeezed = found;
		}
		if( !found )
			continue;
		townsPlaced++;

		UnsignedInt hash = hashCell( (UnsignedInt)m_settings.m_seed + 613U, (Int)site.m_cellX, (Int)site.m_cellY );
		Int nameOffset = (Int)(hash % (UnsignedInt)numStreetNames);

		Real acrossEnd = plan.m_acrossAt[plan.m_streetsAcross - 1] + plan.m_setBack;
		Real acrossStart = plan.m_acrossAt[0] - plan.m_setBack;
		Real downEnd = plan.m_downAt[plan.m_streetsDown - 1] + plan.m_setBack;
		Real downStart = plan.m_downAt[0] - plan.m_setBack;

		gradeTown( site.m_cellX, site.m_cellY, plan, acrossStart, acrossEnd, downStart, downEnd );

		/* The streets are a lattice of blocks between crossings, and every crossing is one point worked
			out once. W3DRoadBuffer joins segments only where their ends are the same numbers: four ends
			on a point make a crossroads, three a tee, two at a right angle a corner it rounds off. A
			street laid end to end across the others, as they used to be, is drawn as strips of tarmac
			laid over each other with no junction at all. The outermost streets end at the corners, so
			the town is ringed by a street with its four corners turned. */
		Int line;
		Int across, down;
		Real crossingX[RMG_TOWN_MAX_STREETS][RMG_TOWN_MAX_STREETS];
		Real crossingY[RMG_TOWN_MAX_STREETS][RMG_TOWN_MAX_STREETS];
		for( across = 0; across < plan.m_streetsAcross; across++ )
		{
			for( down = 0; down < plan.m_streetsDown; down++ )
				townToWorld( site.m_cellX, site.m_cellY, plan, plan.m_acrossAt[across], plan.m_downAt[down],
										 &crossingX[across][down], &crossingY[across][down] );
		}

		for( across = 0; across < plan.m_streetsAcross; across++ )
		{
			for( down = 0; down + 1 < plan.m_streetsDown; down++ )
				addRoadIfDry( crossingX[across][down], crossingY[across][down],
											crossingX[across][down + 1], crossingY[across][down + 1] );
		}

		for( down = 0; down < plan.m_streetsDown; down++ )
		{
			for( across = 0; across + 1 < plan.m_streetsAcross; across++ )
				addRoadIfDry( crossingX[across][down], crossingY[across][down],
											crossingX[across + 1][down], crossingY[across + 1][down] );
		}

		/* Buildings stand back from a street on both sides, facing it, down its whole length. The
			plots either side of a crossing are left empty so the junction stays a junction, and a
			fifth of the rest are left empty too, which is what stops a street reading as a wall of
			frontage with no yards, corners or car parks in it. */
		for( Int direction = 0; direction < 2; direction++ )
		{
			const Real *streetAt = (direction == 0) ? plan.m_acrossAt : plan.m_downAt;
			const Real *crossingAt = (direction == 0) ? plan.m_downAt : plan.m_acrossAt;
			Int streets = (direction == 0) ? plan.m_streetsAcross : plan.m_streetsDown;
			Int crossings = (direction == 0) ? plan.m_streetsDown : plan.m_streetsAcross;
			Real alongStart = (direction == 0) ? downStart : acrossStart;
			Real alongEnd = (direction == 0) ? downEnd : acrossEnd;

			for( line = 0; line < streets; line++ )
			{
				Int plot = 0;

				for( Real along = alongStart; along <= alongEnd + 0.5f; along += plan.m_plotLength )
				{
					plot++;

					if( nearestStreetDistance( crossingAt, crossings, along ) < plan.m_plotLength * 0.5f )
						continue;

					for( Int side = 0; side < 2; side++ )
					{
						Real jitter = (hashUnit( (UnsignedInt)m_settings.m_seed + 6141U, town * 64 + line * 4 + side,
																		 plot ) - 0.5f) * 2.0f * RMG_TOWN_JITTER;
						Real back = (side == 0) ? -plan.m_setBack - jitter : plan.m_setBack + jitter;
						Real facing = (side == 0) ? 0.0f : PI;

						Real buildingX, buildingY;
						if( direction == 0 )
						{
							townToWorld( site.m_cellX, site.m_cellY, plan, streetAt[line] + back, along,
													 &buildingX, &buildingY );
						}
						else
						{
							townToWorld( site.m_cellX, site.m_cellY, plan, along, streetAt[line] + back,
													 &buildingX, &buildingY );
							facing += PI * 0.5f;
						}

						/* Built up in the middle and thinning out towards the edge, where a town turns
							into yards and then fields, rather than the same density out to a hard line. */
						Real outX = (buildingX - site.m_cellX) / plan.m_thinRadius;
						Real outY = (buildingY - site.m_cellY) / plan.m_thinRadius;
						UnsignedInt gap = RMG_TOWN_GAP_IN_100
							+ (UnsignedInt)(RMG_TOWN_EDGE_GAP_IN_100 * (outX * outX + outY * outY));
						UnsignedInt plotHash = hashCell( (UnsignedInt)m_settings.m_seed + 6139U,
																						 town * 64 + line * 4 + side, plot );
						if( plotHash % 100U < gap )
							continue;

						if( !dryAt( buildingX, buildingY, RMG_TOWN_DRY_MARGIN ) )
							continue;
						// nor across a walk to somebody's money or a repaired way out
						if( lockedWithin( buildingX, buildingY, (Int)RMG_TOWN_LOT_PAD ) )
							continue;
						if( squeezed && !siteIsClear( buildingX, buildingY, RMG_TOWN_LOT_BLEND ) )
							continue;

						// Each building on its own small level, so a street down a slope steps.
						flattenPad( buildingX, buildingY, RMG_TOWN_LOT_PAD, RMG_TOWN_LOT_BLEND );

						AsciiString uniqueID;
						uniqueID.format( "Civilian %d", buildingID++ );
						const char *name = theStreetNames[(nameOffset + buildingID) % numStreetNames];
						// The one shop on the list with no snowed or night model: it would stand bare
						// and dark among lit, snowed neighbours. StanSmallRetail01 has its footprint
						// and its ten garrison seats.
						if( ( m_snowy || m_timeOfDay == TIME_OF_DAY_NIGHT ) && strcmp( name, "StanSmallRetail03" ) == 0 )
							name = "StanSmallRetail01";
						addObject( name, uniqueID.str(), buildingX, buildingY,
											 snapAngle45( plan.m_rotation + facing ) );
					}
				}
			}
		}

		reserveSite( site.m_cellX, site.m_cellY, plan.m_radius + 4.0f );
	}
}

/** Bunkers go where the carved routes changed layer. A ramp is the one place on a terraced map
	that has to be walked through rather than round, so a garrisoned bunker looking down one is
	worth taking, and nothing else on the map is worth putting there. */
void RMGLayout::placeBunkers( void )
{
	Int bunkerID = 1;

	for( UnsignedInt i = 0; i < m_ramps.size(); i++ )
	{
		/* A ramp is a gap in a cliff, so the ground beside one is the ground the search likes least.
			Rather than leave the ramp unwatched, the ask drops to less elbow room and then to a
			wider ring before giving up on it. */
		RMGPoint site;
		if( !findSiteNear( m_ramps[i].m_cellX, m_ramps[i].m_cellY, 6.0f, 18.0f,
											 RMG_BUNKER_CLEARANCE, &site ) &&
				!findSiteNear( m_ramps[i].m_cellX, m_ramps[i].m_cellY, 6.0f, 18.0f, 5.0f, &site ) &&
				!findSiteNear( m_ramps[i].m_cellX, m_ramps[i].m_cellY, 6.0f, 30.0f, 5.0f, &site ) )
			continue;

		if( distanceToNearestStart( site.m_cellX, site.m_cellY ) < RMG_BLEND_RADIUS + 8.0f )
			continue;
		if( lockedWithin( site.m_cellX, site.m_cellY, (Int)RMG_TOWN_LOT_PAD ) )
			continue;

		UnsignedInt hash = hashCell( (UnsignedInt)m_settings.m_seed + 5309U, (Int)site.m_cellX, (Int)site.m_cellY );
		Real angle = snapAngle45( (Real)(hash % 8U) * (PI * 0.25f) );

		AsciiString uniqueID;
		uniqueID.format( "Bunker %d", bunkerID++ );
		addObject( "CivilianBunker01", uniqueID.str(), site.m_cellX, site.m_cellY, angle );
		flattenPad( site.m_cellX, site.m_cellY, 3.0f, 7.0f );
		reserveSite( site.m_cellX, site.m_cellY, RMG_BUNKER_CLEARANCE );
	}
}

/** The sound of water on a shore, spaced round every lake. These are ambient emitters rather than
	anything drawn - what is drawn at the water's edge is the renderer's own soft edge, which is why
	the basin eases out over ten cells instead of dropping like a kerb. */
void RMGLayout::placeShoreWaves( void )
{
	Int waveID = 1;

	for( UnsignedInt i = 0; i < m_lakes.size(); i++ )
	{
		const std::vector<RMGPoint>& polygon = m_lakes[i].m_polygon;
		Int n = (Int)polygon.size();
		if( n < 3 )
			continue;

		Real walked = RMG_WAVE_SPACING;		// so the first point of the outline gets one

		for( Int point = 0; point < n; point++ )
		{
			const RMGPoint& a = polygon[point];
			const RMGPoint& b = polygon[( point + 1 ) % n];
			Real dx = b.m_cellX - a.m_cellX;
			Real dy = b.m_cellY - a.m_cellY;
			Real length = sqrtf( dx * dx + dy * dy );
			if( length < 0.01f )
				continue;

			walked += length;
			if( walked < RMG_WAVE_SPACING )
				continue;

			walked = 0.0f;

			// Inward normal: the contour is counter-clockwise, water on the left.
			Real inv = 1.0f / length;
			Real nx = -dy * inv;
			Real ny = dx * inv;

			Real x = a.m_cellX + dx * 0.5f + nx * 1.5f;
			Real y = a.m_cellY + dy * 0.5f + ny * 1.5f;

			Int mapX = (Int)(x + 0.5f) + RMG_BORDER_CELLS;
			Int mapY = (Int)(y + 0.5f) + RMG_BORDER_CELLS;
			// a river's outline runs on into the border, where nobody hears it
			if( !insidePlayable( mapX, mapY ) )
				continue;

			AsciiString uniqueID;
			uniqueID.format( "Waves %d", waveID++ );
			addObject( "AmbientWavesLake", uniqueID.str(), x, y, snapAngle45( ATan2( ny, nx ) ) );
		}
	}
}

/** How far every cell is from the routes the bases are joined by, each start's two nearest
	neighbours' routes. Counted in cells regardless of the ground, since it is only asked how open
	a stretch of map should be kept. */
void RMGLayout::buildLaneDistance( void )
{
	m_laneDist.assign( m_width * m_height, (UnsignedByte)RMG_LANE_FAR );
	std::vector<Int> queue;

	// A route counts when it is one of the two shortest out of either of its starts.
	for( UnsignedInt lane = 0; lane < m_lanes.size(); lane++ )
	{
		Int ends[2] = { m_lanes[lane].m_from, m_lanes[lane].m_to };
		Bool neighbours = FALSE;
		for( Int e = 0; e < 2 && !neighbours; e++ )
		{
			Int shorter = 0;
			for( UnsignedInt other = 0; other < m_lanes.size(); other++ )
			{
				if( other == lane )
					continue;
				if( m_lanes[other].m_from != ends[e] && m_lanes[other].m_to != ends[e] )
					continue;
				if( m_lanes[other].m_cells.size() < m_lanes[lane].m_cells.size() ||
						( m_lanes[other].m_cells.size() == m_lanes[lane].m_cells.size() && other < lane ) )
					shorter++;
			}
			neighbours = shorter < 2;
		}
		if( !neighbours )
			continue;

		for( UnsignedInt c = 0; c < m_lanes[lane].m_cells.size(); c++ )
		{
			Int index = m_lanes[lane].m_cells[c];
			if( m_laneDist[index] != 0 )
			{
				m_laneDist[index] = 0;
				queue.push_back( index );
			}
		}
	}

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	UnsignedInt head = 0;
	while( head < queue.size() )
	{
		Int index = queue[head++];
		Int here = m_laneDist[index];
		if( here + 1 >= RMG_LANE_FAR )
			continue;
		Int x = index % m_width;
		Int y = index / m_width;
		for( Int k = 0; k < 4; k++ )
		{
			Int nx = x + offsetX[k];
			Int ny = y + offsetY[k];
			if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
				continue;
			Int next = cellIndex( nx, ny );
			if( m_laneDist[next] <= here + 1 )
				continue;
			m_laneDist[next] = (UnsignedByte)(here + 1);
			queue.push_back( next );
		}
	}
}

Int RMGLayout::laneDistanceAt( Real cellX, Real cellY ) const
{
	if( m_laneDist.empty() )
		return RMG_LANE_FAR;
	Int x = (Int)(cellX + 0.5f) + RMG_BORDER_CELLS;
	Int y = (Int)(cellY + 0.5f) + RMG_BORDER_CELLS;
	if( x < 0 || y < 0 || x >= m_width || y >= m_height )
		return RMG_LANE_FAR;
	return m_laneDist[cellIndex( x, y )];
}

/** Whether a tree may stand here: on dry walkable ground, out of the bases, off every placed site,
	off every walk to somebody's money and out of the open strip along a route. */
Bool RMGLayout::sceneryMayStand( Real cellX, Real cellY ) const
{
	Int mapX = (Int)(cellX + 0.5f) + RMG_BORDER_CELLS;
	Int mapY = (Int)(cellY + 0.5f) + RMG_BORDER_CELLS;
	if( mapX < 1 || mapY < 1 || mapX >= m_width - 1 || mapY >= m_height - 1 )
		return FALSE;
	if( !passableAtCell( mapX, mapY ) )
		return FALSE;

	Real distanceToShore;
	insideLake( cellX, cellY, &distanceToShore );
	if( distanceToShore < 3.0f )
		return FALSE;

	if( distanceToNearestStart( cellX, cellY ) < RMG_BLEND_RADIUS )
		return FALSE;
	if( !siteIsClear( cellX, cellY, 3.0f ) )
		return FALSE;
	if( !m_routeLock.empty() && m_routeLock[cellIndex( mapX, mapY )] )
		return FALSE;

	return laneDistanceAt( cellX, cellY ) >= RMG_LANE_CLEAR;
}

/** Every player's own stand of trees, AoE's wood beside the base: the same distance out for every
	seat, a quarter turn off the way the base faces outward, and off the money and the routes. */
void RMGLayout::plantGroves( const char *const *theTreeNames, Int *propID, Int *treeBudget )
{
	static const Int turn[8] = { 0, 1, -1, 2, -2, 3, -3, 4 };
	std::vector<RMGPoint> groves;

	for( UnsignedInt start = 0; start < m_starts.size(); start++ )
	{
		const RMGPoint& base = m_starts[start];
		Real preferred = outwardBearing( base.m_cellX, base.m_cellY ) + PI * 0.5f;

		RMGPoint grove;
		Bool found = FALSE;
		for( Int k = 0; k < 8 && !found; k++ )
		{
			Real bearing = preferred + (Real)turn[k] * (PI * 0.25f);
			if( !findSiteOnBearing( base.m_cellX, base.m_cellY, RMG_GROVE_DISTANCE, 6.0f, bearing,
															RMG_GROVE_RADIUS, &grove ) )
				continue;
			if( laneDistanceAt( grove.m_cellX, grove.m_cellY ) < RMG_LANE_CLEAR + (Int)RMG_GROVE_RADIUS )
				continue;
			found = TRUE;
		}
		if( !found )
			continue;
		groves.push_back( grove );

		UnsignedInt woodHash = hashCell( (UnsignedInt)m_settings.m_seed + 40011U, (Int)start, 0 );
		for( Int tree = 0; tree < RMG_GROVE_TREES && *treeBudget > 0; tree++ )
		{
			// Square root of the spread, so the stand is as thick at its middle as at its rim.
			Real spread = sqrtf( hashUnit( (UnsignedInt)m_settings.m_seed + 40013U, (Int)start, tree ) );
			Real angle = hashUnit( (UnsignedInt)m_settings.m_seed + 40017U, (Int)start, tree ) * 2.0f * PI;
			Real x = grove.m_cellX + Cos( angle ) * spread * RMG_GROVE_RADIUS;
			Real y = grove.m_cellY + Sin( angle ) * spread * RMG_GROVE_RADIUS;
			if( !sceneryMayStand( x, y ) )
				continue;

			AsciiString uniqueID;
			uniqueID.format( "Prop %d", (*propID)++ );
			addObject( theTreeNames[( woodHash + (UnsignedInt)(tree / 8) ) % (UnsignedInt)RMG_BIOME_TREES],
								 uniqueID.str(), x, y, snapAngle45( angle ) );
			(*treeBudget)--;
		}
	}

	// Reserved after planting, so the woods field does not fill a stand back in tree by tree.
	for( UnsignedInt g = 0; g < groves.size(); g++ )
		reserveSite( groves[g].m_cellX, groves[g].m_cellY, RMG_GROVE_RADIUS );
}

/// A spot the woods field would plant, and how deep into its wood it lies.
struct RMGTreeSpot
{
	Real m_x;
	Real m_y;
	Int m_depth;		///< the field's value times 10000, compared as an integer
	Real m_angle;
	UnsignedInt m_wood;
};

static bool deeperInTheWood( const RMGTreeSpot& a, const RMGTreeSpot& b )
{
	return a.m_depth > b.m_depth;
}

/** Trees stand in woods rather than in a sprinkle: a slow noise field decides where a wood is, a
	per-cell hash decides which cells of it are used, and the species is chosen per wood so one
	stand does not hold four kinds of tree. The woods thicken beside a route, so they line the way
	between two bases and leave the route itself open. Rocks go where the ground is already rock. */
void RMGLayout::placeScenery( const UnsignedByte perm[512] )
{
	const char *const *theTreeNames = m_biome->m_trees;
	const char *const *theRockNames = m_biome->m_rocks;
	const Int numTreeNames = RMG_BIOME_TREES;
	const Int numRockNames = RMG_BIOME_ROCKS;
	const Real forestThreshold = m_type->m_forestThreshold;

	Real playable = (Real)m_settings.m_playableCells;
	Real forestScale = RMG_FOREST_FEATURES / playable;

	/* Budgets are a safety net, not the count: the forest field decides how many trees there are,
		and it scales with the ground because it is sampled per cell. A budget spent in scan order
		would put every tree in the top of the map, so it sits high enough that an ordinary map
		never reaches it. */
	Real area = playable * playable;
	const Bool woodland = m_type->m_woodland;
	Int treeBudget = (Int)(area / ( woodland ? RMG_WOOD_BUDGET_CELLS : RMG_CELLS_PER_TREE ));
	// A base in a black forest sits in a clearing a good deal wider than its levelled ground.
	const Real baseClearing = woodland ? RMG_BLEND_RADIUS + 10.0f : RMG_BLEND_RADIUS;
	Int rockBudget = (Int)(area / RMG_CELLS_PER_ROCK);
	Int propID = 1;

	buildLaneDistance();
	plantGroves( theTreeNames, &propID, &treeBudget );

	/* Rows are taken in four interleaved passes, so a rock budget that does run out leaves the
		whole map evenly thinner rather than its top bare. */
	std::vector<RMGTreeSpot> spots;

	const Int passes = 4;
	const Int rowsPerPass = ( m_settings.m_playableCells - 4 ) / ( passes * RMG_PROP_STRIDE ) + 1;
	for( Int row = 0; row < passes * rowsPerPass; row++ )
	{
		Int y = 2 + ( row / rowsPerPass ) * RMG_PROP_STRIDE + ( row % rowsPerPass ) * passes * RMG_PROP_STRIDE;
		if( y >= m_settings.m_playableCells - 2 )
			continue;

		for( Int x = 2; x < m_settings.m_playableCells - 2; x += RMG_PROP_STRIDE )
		{
			if( treeBudget <= 0 && rockBudget <= 0 )
				break;

			Int mapX = x + RMG_BORDER_CELLS;
			Int mapY = y + RMG_BORDER_CELLS;

			// Bases, routes, money walks and everything already placed keep their elbow room.
			if( !sceneryMayStand( (Real)x, (Real)y ) )
				continue;
			if( woodland && distanceToNearestStart( (Real)x, (Real)y ) < baseClearing )
				continue;

			UnsignedInt hash = hashCell( (UnsignedInt)m_settings.m_seed + 7717U, x, y );
			Real jitterX = (Real)(hash % 100U) / 100.0f - 0.5f;
			Real jitterY = (Real)((hash >> 7) % 100U) / 100.0f - 0.5f;
			Real angle = snapAngle45( (Real)((hash >> 14) % 8U) * (PI * 0.25f) );

			/* Steep ground, by the same rule that paints it as rock. Asking the texture classes
				would be reading a field that is not built yet: they are worked out after the
				objects, because every pad an object levels moves the slopes they come from. */
			Bool rocky = cellSpanWorld( mapX, mapY ) > RMG_CLIFF_WORLD_SPAN * 0.45f;
			if( rocky )
			{
				if( rockBudget <= 0 )
					continue;
				// a quarter of the rocky cells: the top eight bits are 0 to 255, never a hundred and more
				if( (hash >> 24) >= RMG_ROCK_ACCEPT_IN_256 )
					continue;
				// The ground steepens where the playable area meets the border, and rocks set along
				// that line stand in a ruled row round the map.
				if( x < RMG_ROCK_EDGE_CELLS || y < RMG_ROCK_EDGE_CELLS ||
						x >= m_settings.m_playableCells - RMG_ROCK_EDGE_CELLS ||
						y >= m_settings.m_playableCells - RMG_ROCK_EDGE_CELLS )
					continue;
				// A ramp's sides are steep too, and a rock there blocks the one way up.
				if( nearRamp( (Real)x, (Real)y, RMG_MASSIF_ROCK_CLEAR ) )
					continue;
				// Nor round the massif's cliff, where they would stand in a ring drawn with compasses.
				Bool onRim = FALSE;
				for( UnsignedInt l = 0; l < m_massifLobes.size() && !onRim; l++ )
				{
					Real rim;
					Real inside = massifRim( m_massifLobes[l], (Real)x, (Real)y, &rim );
					onRim = inside > -3.0f && inside < rim + 3.0f;
				}
				if( onRim )
					continue;

				AsciiString uniqueID;
				uniqueID.format( "Prop %d", propID++ );
				addObject( theRockNames[(hash >> 5) % (UnsignedInt)numRockNames], uniqueID.str(),
									 (Real)x + jitterX, (Real)y + jitterY, angle );
				rockBudget--;
				continue;
			}

			if( treeBudget <= 0 )
				continue;

			/* Where a wood is, and how deep into it this cell is, both come out of the same field,
				warped by a second one so the edge of a wood is ragged rather than a contour line.
				The chance climbs towards the middle of a wood, which is what makes a stand thick in
				the centre and thin at the edges instead of an even sprinkle with a hard border. */
			Real forestX = (Real)x * forestScale + 100.0f;
			Real forestY = (Real)y * forestScale - 100.0f;
			Real warpX = forestX + 0.6f * fractalNoise( perm, forestX * 2.3f, forestY * 2.3f, 2 );
			Real warpY = forestY + 0.6f * fractalNoise( perm, forestX * 2.3f + 9.0f,
																									forestY * 2.3f - 4.0f, 2 );

			Real density = fractalNoise( perm, warpX, warpY, 4 );
			Real chance;
			if( woodland )
			{
				/* One wood over the whole map and nothing on the rides. The ride's edge wanders on a
					noise of its own so it reads as a cut through trees, not a ruled band. Inside the wood
					a faster field makes thickets with glades between them, and the slow field makes whole
					quarters of the map thicker or thinner, so the canopy is not one even speckle. A
					thin-treed biome still gets a wood, only a lighter one. */
				Real wobble = RMG_RIDE_WOBBLE * fractalNoise( perm, (Real)x * 0.07f + 3.0f,
																										 (Real)y * 0.07f + 77.0f, 2 );
				if( rideDistance( (Real)x, (Real)y ) < RMG_RIDE_HALF_WIDTH + wobble )
					continue;
				Real clump = fractalNoise( perm, (Real)x * RMG_WOOD_CLUMP_SCALE + 31.0f,
																	 (Real)y * RMG_WOOD_CLUMP_SCALE - 57.0f, 3 );
				density = 0.65f * clump + 0.35f * density;
				/* A glade keeps the odd tree, so the wood is still a wood between its thickets. Those
					are counted as deep as a thicket's heart, so a capped map thins the thickets' fringes
					and not the glades down to bare ground. */
				if( density >= RMG_WOOD_CLUMP_EDGE )
				{
					chance = 0.05f + ( density - RMG_WOOD_CLUMP_EDGE ) * 0.8f;
				}
				else
				{
					// In tufts of a few trees off a fast field, not one tree here and one there.
					Real tuft = fractalNoise( perm, (Real)x * 0.16f + 5.0f, (Real)y * 0.16f + 11.0f, 1 );
					chance = tuft > 0.25f ? 0.12f : 0.0f;
					density = 1.0f;
				}
				if( chance > 0.25f )
					chance = 0.25f;
				chance *= 0.5f + 0.5f * m_biome->m_treeShare;
				/* The wood's chances were tuned against the old eight-bit per-mille test, which planted
					( floor( 1000 c ) + 1 ) / 256 of the cells: nearly all of them at the cap, a third in a
					tuft. Read straight as a fraction under the exact test below, the same numbers left a
					black forest a third to two thirds short of its tree count. The old rate is kept here;
					the cap that trims the pool to the target is what thins the wood, not this. */
				if( chance > 0.0f )
				{
					chance = (Real)( (Int)( chance * 1000.0f ) + 1 ) / 256.0f;
					if( chance > 1.0f )
						chance = 1.0f;
				}
			}
			else
			{
				/* Beside a route a wood that is nearly there comes in up to the open strip, so the route
					reads as a way through the trees. Out in open country it adds nothing: a route across
					a field stays a route across a field rather than an avenue. */
				Int lane = laneDistanceAt( (Real)x, (Real)y );
				if( lane < RMG_LANE_FRAME && density > forestThreshold - 0.14f )
					density += 0.12f * (Real)( RMG_LANE_FRAME - lane ) / (Real)( RMG_LANE_FRAME - RMG_LANE_CLEAR );

				/* A wood starts a little further into the field than before and fills in faster, so
					what stands is a few thick clumps with open ground between them, not a thin dusting. */
				const Real clumpEdge = forestThreshold + 0.05f;
				if( density < clumpEdge )
					continue;

				/* Not scaled by the biome's tree share: the share already sets how many trees the map
					holds (target, below), and taking it off the chance as well left a desert or a
					Twilight wood a pool a quarter the size of the count it was calibrated to. */
				chance = 0.08f + (density - clumpEdge) * 3.0f;
				if( chance > 0.7f )
					chance = 0.7f;
			}
			/* Ten bits of the hash read as a fraction of one. The eight bits this used to read as a
				per-mille only ever came to a quarter, so every chance over that planted every cell of
				the stride and the woods stood as a solid grid. */
			if( (hash >> 22) >= (UnsignedInt)( chance * 1024.0f ) )
				continue;

			RMGTreeSpot spot;
			spot.m_x = (Real)x + jitterX;
			spot.m_y = (Real)y + jitterY;
			// the depth the cap trims by, ragged by a slice of the hash so the kept edge of a wood is a
			// band rather than one contour line
			spot.m_depth = (Int)( density * 10000.0f ) + (Int)( hashCell( (UnsignedInt)m_settings.m_seed + 7723U, x, y ) % RMG_TREE_EDGE_RAGGED );
			spot.m_angle = angle;
			spot.m_wood = hashCell( (UnsignedInt)m_settings.m_seed + 40009U, x / 16, y / 16 );		// the wood, not the tree, picks the species
			spots.push_back( spot );
		}
	}

	/* The field says where the woods are and how deep into one each spot is; the ground says how
		many trees the map holds. When the field asks for more than that, the trees deepest in a
		wood are kept and the thin fringes go, so a well wooded map has fewer, thick woods rather
		than the same woods thinned out. How many that is comes from the map type, the biome and
		the seed: plains are the most wooded and canyons the least, a desert has a third of a
		forest's trees, and two seeds of one kind still differ by up to half again. A black forest
		takes the most of all, but still a number: an uncapped canopy came to six thousand trees on
		an eight-player map, and every one of them is drawn every frame. */
	Real share = woodland ? 0.5f + 0.5f * m_biome->m_treeShare : m_biome->m_treeShare;
	Real wooded = ( woodland ? 1.8f : 1.3f - 4.0f * forestThreshold ) * share
		* ( 0.75f + 0.5f * hashUnit( m_settings.m_seed, 9001, 17 ) );
	UnsignedInt target = (UnsignedInt)( area / RMG_TREE_CELLS_EACH * wooded );
	if( spots.size() > target )
	{
		std::stable_sort( spots.begin(), spots.end(), deeperInTheWood );
		spots.resize( target );
	}

	for( UnsignedInt s = 0; s < spots.size() && treeBudget > 0; s++ )
	{
		AsciiString uniqueID;
		uniqueID.format( "Prop %d", propID++ );
		addObject( theTreeNames[spots[s].m_wood % (UnsignedInt)numTreeNames], uniqueID.str(),
							 spots[s].m_x, spots[s].m_y, spots[s].m_angle );
		treeBudget--;
	}
}

/** One waypoint every this many cells along an approach path. */
static const Int RMG_APPROACH_SPACING_CELLS = 12;
/** How far a flank or back-door path bows out from the straight line, as a share of its length. */
static const Real RMG_APPROACH_BULGE = 0.3f;
/** How far a bowed point may be moved to find ground a unit can reach. */
static const Int RMG_APPROACH_SNAP_CELLS = 10;

void RMGLayout::addApproachWaypoint( const char *lane, Int targetStart, Int fromStart, Int step, Real cellX, Real cellY,
																		 Int *waypointID, Int *previous )
{
	RMGObject waypoint;
	waypoint.m_templateName = "*Waypoints/Waypoint";
	waypoint.m_uniqueID.format( "%s%d_from%d_%d", lane, targetStart + 1, fromStart + 1, step );
	waypoint.m_pathLabel.format( "%s%d", lane, targetStart + 1 );
	waypoint.m_worldX = cellX * MAP_XY_FACTOR;
	waypoint.m_worldY = cellY * MAP_XY_FACTOR;
	waypoint.m_angle = 0.0f;
	waypoint.m_waypointID = (*waypointID)++;
	waypoint.m_flags = 0;
	m_objects.push_back( waypoint );
	if( *previous > 0 )
		m_waypointLinks.push_back( std::make_pair( *previous, waypoint.m_waypointID ) );
	*previous = waypoint.m_waypointID;
}

/** The skirmish scripts send a wave down "Center", "Flank" or "Backdoor" followed by the target's start
	number, starting from the waypoint of that path nearest the wave. A generated map carried none of
	them, nor the base areas the launch waits on (writePolygonTriggers), so Easy and Medium, whose
	attacks are nothing but those paths, built their waves and never sent one: 128 Medium-against-Medium
	matches lost 159 units between them. Every start gets all three
	paths from every other start. The centre one walks the ground route the flood fill finds; the other
	two bow out to either side of the straight line, each point moved onto the nearest ground a unit
	can reach from the target. */
/// Is start from among the count starts nearest target in a straight line? Ties go to the lower index.
static Bool rmgAmongNearestStarts( const std::vector<RMGPoint>& starts, UnsignedInt target, UnsignedInt from, Int count )
{
	const Real fx = starts[from].m_cellX - starts[target].m_cellX;
	const Real fy = starts[from].m_cellY - starts[target].m_cellY;
	const Real reach = fx * fx + fy * fy;
	Int nearer = 0;
	for( UnsignedInt i = 0; i < starts.size(); i++ )
	{
		if( i == target || i == from )
			continue;
		const Real dx = starts[i].m_cellX - starts[target].m_cellX;
		const Real dy = starts[i].m_cellY - starts[target].m_cellY;
		const Real d = dx * dx + dy * dy;
		if( d < reach || ( d == reach && i < from ) )
			nearer++;
	}
	return nearer < count;
}

void RMGLayout::addApproachPaths( Int *waypointID )
{
	m_waypointLinks.clear();
	static const char *LANES[3] = { "Center", "Flank", "Backdoor" };
	static const Real BULGE[3] = { 0.0f, RMG_APPROACH_BULGE, -RMG_APPROACH_BULGE };

	m_lanes.clear();

	/* The starts are playable-area cells and the flood runs on map cells, border included. These
		paths used to flood from the start's playable cell read as a map cell, twenty cells short on
		both axes, so every waypoint but the last stood on ground offset from the real route. */
	std::vector< std::vector<Int> > floods;
	floodFromEveryStart( floods );
	for( UnsignedInt target = 0; target < m_starts.size(); target++ )
	{
		const Int targetX = (Int)(m_starts[target].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		const Int targetY = (Int)(m_starts[target].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		const std::vector<Int>& dist = floods[target];

		for( UnsignedInt from = 0; from < m_starts.size(); from++ )
		{
			if( from == target )
				continue;
			/* Every pair of 32 seats is 2976 paths and some 80k waypoints, and
				TerrainLogic::addWaypointLink walks every waypoint to find a link's two ends. Past eight
				seats a target takes its paths from its seven nearest, everybody on an eight-seat map;
				a wave from further off joins the path at its nearest waypoint as it always did. */
			if( m_starts.size() > RandomMapGenerator::DEFAULT_PLAYERS
				&& !rmgAmongNearestStarts( m_starts, target, from, RandomMapGenerator::DEFAULT_PLAYERS - 1 ) )
				continue;
			const Real fromX = m_starts[from].m_cellX;
			const Real fromY = m_starts[from].m_cellY;
			const Real dx = m_starts[target].m_cellX - fromX;
			const Real dy = m_starts[target].m_cellY - fromY;
			const Real length = sqrtf( dx * dx + dy * dy );
			if( length < 1.0f )
				continue;

			for( Int lane = 0; lane < 3; lane++ )
			{
				Int previous = 0;
				Int step = 0;
				Int x = (Int)(fromX + 0.5f) + RMG_BORDER_CELLS;
				Int y = (Int)(fromY + 0.5f) + RMG_BORDER_CELLS;
				if( lane == 0 && dist[cellIndex( x, y )] > 0 )
				{
					// One route per pair of starts is kept for the woods to be laid out against.
					RMGLane *route = NULL;
					if( from < target )
					{
						m_lanes.push_back( RMGLane() );
						route = &m_lanes.back();
						route->m_from = (Int)from;
						route->m_to = (Int)target;
						route->m_cells.push_back( cellIndex( x, y ) );
					}

					// downhill on the flood from the target: the route a unit would drive
					const Real lineFromX = (Real)x;
					const Real lineFromY = (Real)y;
					Int walked = 0;
					Bool due = FALSE;
					while( dist[cellIndex( x, y )] > 0 )
					{
						if( !stepDownFlood( dist, &x, &y, lineFromX, lineFromY, (Real)targetX, (Real)targetY ) )
							break;		// a breadth-first flood always leaves one; this only guards the loop
						if( route )
							route->m_cells.push_back( cellIndex( x, y ) );
						if( ++walked % RMG_APPROACH_SPACING_CELLS == 0 )
							due = TRUE;
						if( !due )
							continue;
						/* The flood walks a beach the water polygon still covers, where the ground stands
							just above the surface; a waypoint there is in the water to every unit, so it
							waits for the first dry cell further on. */
						Real shore;
						insideLake( (Real)(x - RMG_BORDER_CELLS), (Real)(y - RMG_BORDER_CELLS), &shore );
						if( shore < 2.0f )
							continue;
						addApproachWaypoint( LANES[lane], (Int)target, (Int)from, step++,
																 (Real)(x - RMG_BORDER_CELLS), (Real)(y - RMG_BORDER_CELLS),
																 waypointID, &previous );
						due = FALSE;
					}
				}
				else
				{
					const Int points = max( 2, (Int)(length / RMG_APPROACH_SPACING_CELLS) );
					for( Int k = 1; k < points; k++ )
					{
						const Real t = (Real)k / (Real)points;
						const Real bow = BULGE[lane] * length * 4.0f * t * (1.0f - t);
						const Real px = fromX + dx * t - dy / length * bow;
						const Real py = fromY + dy * t + dx / length * bow;
						// the nearest cell a unit can reach, ring by ring
						Bool found = FALSE;
						for( Int ring = 0; ring <= RMG_APPROACH_SNAP_CELLS && !found; ring++ )
						{
							for( Int oy = -ring; oy <= ring && !found; oy++ )
							{
								for( Int ox = -ring; ox <= ring && !found; ox++ )
								{
									if( abs( ox ) != ring && abs( oy ) != ring )
										continue;
									const Int cx = (Int)(px + 0.5f) + ox + RMG_BORDER_CELLS;
									const Int cy = (Int)(py + 0.5f) + oy + RMG_BORDER_CELLS;
									if( cx < 0 || cy < 0 || cx >= m_width - 1 || cy >= m_height - 1 || dist[cellIndex( cx, cy )] < 0 )
										continue;
									/* A lake bed is flat ground to the flood, but the water polygon over it
										closes it to every unit, and it reaches a little past the shore line. */
									Real shore;
									insideLake( (Real)(cx - RMG_BORDER_CELLS), (Real)(cy - RMG_BORDER_CELLS), &shore );
									if( shore < 2.0f )
										continue;
									addApproachWaypoint( LANES[lane], (Int)target, (Int)from, step++,
																			 (Real)(cx - RMG_BORDER_CELLS), (Real)(cy - RMG_BORDER_CELLS),
																			 waypointID, &previous );
									found = TRUE;
								}
							}
						}
					}
				}
				addApproachWaypoint( LANES[lane], (Int)target, (Int)from, step++, m_starts[target].m_cellX, m_starts[target].m_cellY,
														 waypointID, &previous );
			}
		}
	}
}

/** One step down a flood, in map cells. Of the neighbours one step nearer, the one that keeps
	closest to the straight line between the two ends, so a walk over open ground runs along that
	line in a fine staircase instead of all the way along one axis and then all the way along the
	other. FALSE at the bottom of the flood. */
Bool RMGLayout::stepDownFlood( const std::vector<Int>& dist, Int *x, Int *y, Real fromX, Real fromY,
															 Real toX, Real toY ) const
{
	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	const Int here = dist[cellIndex( *x, *y )];
	if( here <= 0 )
		return FALSE;

	Real lineX = toX - fromX;
	Real lineY = toY - fromY;
	Real length = sqrtf( lineX * lineX + lineY * lineY );
	if( length < 1.0f )
		length = 1.0f;

	Int bestX = -1, bestY = -1;
	Real best = 1.0e9f;
	for( Int n = 0; n < 4; n++ )
	{
		const Int nx = *x + offsetX[n];
		const Int ny = *y + offsetY[n];
		if( nx < 0 || ny < 0 || nx >= m_width - 1 || ny >= m_height - 1 )
			continue;
		if( dist[cellIndex( nx, ny )] != here - 1 )
			continue;

		Real off = fabsf( ( (Real)nx - fromX ) * lineY - ( (Real)ny - fromY ) * lineX ) / length;
		if( off < best )
		{
			best = off;
			bestX = nx;
			bestY = ny;
		}
	}

	if( bestX < 0 )
		return FALSE;
	*x = bestX;
	*y = bestY;
	return TRUE;
}

/** The starts' waypoints and everybody's money: what the repairs in build have to keep reachable. */
void RMGLayout::placeMoney( void )
{
	m_objects.clear();
	m_sites.clear();
	m_routeLock.clear();
	m_moneyWalks.clear();
	m_laneDist.clear();
	m_lanes.clear();
	m_waypointLinks.clear();

	Int waypointID = 1;
	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		RMGObject waypoint;
		waypoint.m_templateName = "*Waypoints/Waypoint";
		waypoint.m_uniqueID.format( "Player_%d_Start", i + 1 );
		waypoint.m_worldX = m_starts[i].m_cellX * MAP_XY_FACTOR;
		waypoint.m_worldY = m_starts[i].m_cellY * MAP_XY_FACTOR;
		waypoint.m_angle = 0.0f;
		waypoint.m_waypointID = waypointID++;
		waypoint.m_flags = 0;
		m_objects.push_back( waypoint );

		reserveSite( m_starts[i].m_cellX, m_starts[i].m_cellY, RMG_FLAT_RADIUS );
	}

	/* The money and the towns level and grade the ground they stand on, and on the massif's edge
		that turns the cliff into a slope. They keep off it, and off the top as well, which is ground
		to fight for rather than to farm; the bunkers and the trees placed after may still go there.
		They are the sites after the starts' own, one a lobe, which placeDressing drops once the towns
		are down. A lobe is reserved as the circle round its long axis. */
	for( UnsignedInt l = 0; l < m_massifLobes.size(); l++ )
	{
		const RMGMassifLobe& lobe = m_massifLobes[l];
		Real shapeMost = 1.0f + sqrtf( lobe.m_shapeCos2 * lobe.m_shapeCos2 + lobe.m_shapeSin2 * lobe.m_shapeSin2 )
			+ sqrtf( lobe.m_shapeCos3 * lobe.m_shapeCos3 + lobe.m_shapeSin3 * lobe.m_shapeSin3 );
		reserveSite( lobe.m_cellX, lobe.m_cellY, lobe.m_major * ( 1.0f + RMG_MASSIF_WOBBLE ) * shapeMost
								 + 0.5f * ( RMG_MASSIF_SLOPE_RIM - RMG_MASSIF_RIM ) + RMG_MASSIF_RIM );
	}
	placeSupplyAndDerricks();
}

/** Everything placed on the repaired ground: the AI's paths walked over it as it will be played, then
	towns, bunkers, waves and scenery, none of which stands on a locked cell. */
void RMGLayout::placeDressing( const UnsignedByte perm[512] )
{
	Int waypointID = (Int)m_starts.size() + 1;
	buildPassability();
	addApproachPaths( &waypointID );

	const UnsignedInt massifSite = m_starts.size();
	// the shipped map a style comes from has neither a town nor a bunker on it, and a skeleton gets
	// its towns and its dressing from the template
	if( !m_style && !m_skeleton )
	{
		/* A town keeps out of every base's levelled ring, not only the one it is placed beside: a
			start reserves its flat disc alone, and a building in the ring sat on a pad the base's
			levelling then cut through. The rings are the towns' alone, so they go again after. */
		const UnsignedInt startRings = m_sites.size();
		for( UnsignedInt i = 0; i < m_starts.size(); i++ )
			reserveSite( m_starts[i].m_cellX, m_starts[i].m_cellY, RMG_TOWN_START_CLEAR );
		placeTowns();
		m_sites.erase( m_sites.begin() + startRings, m_sites.begin() + startRings + m_starts.size() );
	}
	m_sites.erase( m_sites.begin() + massifSite, m_sites.begin() + massifSite + m_massifLobes.size() );
	if( m_skeleton )
		return;
	if( !m_style )
		placeBunkers();
	placeShoreWaves();
	placeScenery( perm );
}

/// What a player has to reach: their money and the neutral money between them.
static Bool rmgIsMoney( const AsciiString& templateName )
{
	return templateName.compare( "SupplyDock" ) == 0 || templateName.compare( "TechOilDerrick" ) == 0
		|| templateName.compare( "SupplyPileSmall" ) == 0;
}

/** The finished ground walked once more: every start on dry ground of its own and reached from the
	first, every piece of money reached from it (its cell or one beside it, as ensurePlayability asks). */
Bool RMGLayout::everyoneReaches( void )
{
	buildPassability();
	floodPlayableFrom( 0 );

	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Int x = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int y = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		if( !m_visited[cellIndex( x, y )] || underwaterAtCell( x, y ) )
			return FALSE;
		for( UnsignedInt j = 0; j < i; j++ )
		{
			if( m_starts[j].m_cellX == m_starts[i].m_cellX && m_starts[j].m_cellY == m_starts[i].m_cellY )
				return FALSE;
		}
	}

	static const Int neighbourX[5] = { 0, 1, -1, 0, 0 };
	static const Int neighbourY[5] = { 0, 0, 0, 1, -1 };
	for( UnsignedInt o = 0; o < m_objects.size(); o++ )
	{
		if( !rmgIsMoney( m_objects[o].m_templateName ) )
			continue;
		Int cellX = (Int)(m_objects[o].m_worldX / MAP_XY_FACTOR + 0.5f) + RMG_BORDER_CELLS;
		Int cellY = (Int)(m_objects[o].m_worldY / MAP_XY_FACTOR + 0.5f) + RMG_BORDER_CELLS;
		Bool reached = FALSE;
		for( Int n = 0; n < 5 && !reached; n++ )
		{
			Int nx = cellX + neighbourX[n];
			Int ny = cellY + neighbourY[n];
			reached = nx >= 0 && ny >= 0 && nx < m_width && ny < m_height && m_visited[cellIndex( nx, ny )];
		}
		if( !reached )
			return FALSE;
	}
	return TRUE;
}

/// Whether any locked cell lies within radius cells of this playable-area point.
Bool RMGLayout::lockedWithin( Real cellX, Real cellY, Int radius ) const
{
	if( m_routeLock.empty() )
		return FALSE;
	Int centreX = (Int)(cellX + 0.5f) + RMG_BORDER_CELLS;
	Int centreY = (Int)(cellY + 0.5f) + RMG_BORDER_CELLS;
	for( Int dy = -radius; dy <= radius; dy++ )
	{
		for( Int dx = -radius; dx <= radius; dx++ )
		{
			Int x = centreX + dx;
			Int y = centreY + dy;
			if( dx * dx + dy * dy > radius * radius || x < 0 || y < 0 || x >= m_width || y >= m_height )
				continue;
			if( m_routeLock[cellIndex( x, y )] )
				return TRUE;
		}
	}
	return FALSE;
}

/** A repair run after the dressing is down cuts under whatever stands there: every building, bunker,
	tree or rock on its roadway goes. Waypoints, roads, money and waves stay; the money was all placed
	before any of these, so its indices in m_objects (RMGMoneyWalk::m_object) do not move. */
void RMGLayout::clearLockedGround( void )
{
	UnsignedInt kept = 0;
	for( UnsignedInt o = 0; o < m_objects.size(); o++ )
	{
		const RMGObject& object = m_objects[o];
		Bool blocks = object.m_waypointID == 0 && object.m_flags == 0 && !rmgIsMoney( object.m_templateName )
			&& object.m_templateName.compare( "AmbientWavesLake" ) != 0;
		if( blocks )
		{
			Int radius = object.m_uniqueID.startsWith( "Civilian " ) || object.m_uniqueID.startsWith( "Bunker " )
				? (Int)RMG_TOWN_LOT_PAD : 1;
			blocks = lockedWithin( object.m_worldX / MAP_XY_FACTOR, object.m_worldY / MAP_XY_FACTOR, radius );
		}
		if( blocks )
			continue;
		if( kept != o )
			m_objects[kept] = object;
		kept++;
	}
	m_objects.resize( kept );
}

//-----------------------------------------------------------------------------
// The Direct Strike map
//-----------------------------------------------------------------------------

/* Direct Strike (docs/plans/DIRECT-STRIKE.md in the wrapper) plays on this map, built here rather than in
	WorldBuilder so that a change to it is a change to code. Its layout follows the Warcraft III
	Direct Strike map and is the same for every seed; the seed picks only the look round it. Below, x
	runs along the lane and y across it, in playable cells of 10 world units, and every figure comes
	from the constants after this comment (rmgLaneShape works out the rest), so the layout can be
	reshaped there without touching the code that lays it.

	One straight lane, RMG_LANE_LENGTH by RMG_LANE_WIDTH, mirror symmetric about its middle. Team A
	holds the west end and team B the east. Each team's fortress stands RMG_LANE_FORTRESS_AT of the
	half-length from the middle and its tower RMG_LANE_TOWER_AT, both on the middle line
	(RMG_LANE_TOWERS 2 puts two towers at the lane's edges instead). A wave copy is a plot's
	RMG_LANE_PLOT_ACROSS cells wide, centred on the line, and its spawn anchor stands behind its own
	fortress (the owner's rule of 2026-11-21), the copy's front RMG_LANE_PAD_GAP cells clear of the
	fortress's back, so the fortress is between the pad and the lane and the wave walks past it and
	its tower on the way out: the fortress is 14 cells wide on a 45-cell lane, which leaves 15 a side.
	Anything that reaches the pad has got past both. Past each lane end, RMG_LANE_PLOT_GAP cells of green away, stands a column
	of three build plots, one a seat, each RMG_LANE_PLOT_ALONG along the lane by RMG_LANE_PLOT_ACROSS
	across it, upright like the plots at the ends of the Direct Strike map: seat 1's on the lane's axis,
	seat 2's below it and seat 3's above, RMG_LANE_PLOT_PITCH cells apart. A plot's edge towards the
	lane is its front, and the director lays the wave copy out with that edge forward. Every unit
	stands on it, jets included; the air zones that once lay behind each plot are gone (the owner's
	rule of 2026-11-21), and the plot is 192 slots for it. On 2026-11-21 it grew along the lane, 12
	slots across by 16 back; the owner turned it upright on 2026-11-22, 16 across by 12 back, 24 cells
	along the lane by 32 across. The wave copy is 32 cells wide now on the 45-cell lane, so its middle
	seven or eight columns meet the 14-cell fortress head on and go round it through the 15.5 cells
	either side, where the outer columns already walk. Behind each column a strip RMG_LANE_DERRICK_CLEAR
	cells deep stays clear of props: the client stands a player's income derricks there (DirectStrikeHud).

	The lane and the plots are flat at RMG_LANE_GROUND, and nothing walls them in: the director holds
	a plot's units where they were bought and sends a wave straight down its lines, so the terrain
	keeps nobody anywhere. Round them lies RMG_LANE_FLAT_RING cells of level ground, then the bank
	rolls up into low hills over RMG_LANE_HILL_REACH cells. The field is an island in open sea, after
	the Direct Strike minimap: a rounded blob round the lane and both plot columns whose coast wanders
	on two noise fields per seed, a slow one for its shape and a quicker one for its bays and capes. A
	light beach RMG_LANE_SAND cells wide and more runs round it, the ground falls from it into the sea
	over RMG_LANE_BEACH cells, and a shallow shelf RMG_LANE_SHELF cells wide lies off it before the
	bed drops. There are no lakes. The map is 16:9, with RMG_LANE_SEA_END cells behind each plot column
	so the camera sees the whole island in open water. No cell spans a cliff anywhere on dry ground.

	The lane and the plots wear the theme's own ground like the rest of the island; only the beach and
	the worn ground round each fortress and tower are painted apart (theLaneGrounds); a plot is marked by nothing but the slot grid the client draws
	on it in the theme's own line colour (LaneGridColours). The seed picks the theme (one of theBiomes as a plain map's
	seed picks it, with its hour and snow, or one time in five the twilight style's palette at dusk),
	the coast and how wooded and stony the bank is; the trees gather into patches, more of them along
	the long sides. Trees and stones are all that stand on the bank, never on the beach; nothing stands
	on the lane or a plot but what is named below.

	The start positions alternate between the ends, so a 1v1 on starts 1 and 2 faces itself down the
	lane: start 1 is A's seat 1, start 2 is B's seat 1, start 3 A's seat 2 and so on. The names the
	director looks up:

	LaneWar_BuildA1 to A3, B1 to B3      trigger areas, each the outline of one plot; Player_N_Start
	                                     stands in the middle of its own (1 in A1, 2 in B1, 3 in A2,
	                                     4 in B2, 5 in A3, 6 in B3)
	LaneWar_MiddleArea                   trigger area, RMG_LANE_MIDDLE_RADIUS round the lane's middle
	LaneWar_SpawnA, LaneWar_SpawnB       waypoints, each team's spawn anchor
	LaneWar_LaneA, LaneWar_LaneB         waypoint path labels: A's runs from just ahead of its fortress
	                                     to B's fortress, B's back the other way, a waypoint every
	                                     RMG_LANE_PATH_STEP cells (LaneWar_LaneA_01 on), each linked to
	                                     the next
	LaneWar_FortressA, LaneWar_FortressB objects owned "StartOwner:1,3,5" and "StartOwner:2,4,6"
	LaneWar_TowerA1, LaneWar_TowerB1     objects owned the same way (A2 and B2 with two towers, 1 at the
	                                     lane's low y edge)
	LaneWar_MiddleStructure              object, neutral, only when RMG_LANE_MIDDLE_TEMPLATE names one

	An owner string of that shape gives the object to whoever holds the first of those starts that is
	taken, or to the neutral player when none is (teamForStartOwner, GameLogic.cpp). The fortress is
	LaneWarFortress and a tower LaneWarWatchTower (FixesReforged.ini). */
#define RMG_LANE_SEATS			6
#define RMG_LANE_LENGTH			360		///< cells from one end of the lane to the other
#define RMG_LANE_WIDTH			45		///< and across it
#define RMG_LANE_FORTRESS_AT	0.70f	///< a fortress's distance from the middle, a share of half the lane, on the middle line
#define RMG_LANE_FORTRESS_HALF	7.0f	///< cells from its middle to its front, LaneWarFortress's 140 by 140 footprint
#define RMG_LANE_TOWER_AT		0.37f	///< a tower's distance from the middle, the same way
#define RMG_LANE_TOWER_HALF		2.0f	///< cells from its middle to its front, LaneWarWatchTower's 32 by 32 and a little
#define RMG_LANE_PAD_GAP		3.0f	///< cells between the back of a team's fortress and the front of its wave copy
#define RMG_LANE_TOWERS			1		///< a team's towers: 1 on the middle line, 2 at the lane's edges
#define RMG_LANE_TOWER_INSET	3.0f	///< cells a tower at an edge stands in from it
static const char *const RMG_LANE_MIDDLE_TEMPLATE = NULL;	///< a structure in the middle (TechOilRefinery, say), or none
#define RMG_LANE_MIDDLE_RADIUS	20.0f	///< cells, the middle area
#define RMG_LANE_MIDDLE_SIDES	16
#define RMG_LANE_PATH_STEP		15.0f	///< cells between two waypoints of a lane path
#define RMG_LANE_PLOT_GAP		14		///< cells of green between a lane's end and the front of its team's plots
#define RMG_LANE_PLOT_ALONG		24		///< a plot's cells along the lane, front to back: 12 slots of DIRECTSTRIKE_SLOT_SIZE (32, 16 slots, on 2026-11-21)
#define RMG_LANE_PLOT_ACROSS	32		///< and across it, the width of its front: 16 slots (24, 12 slots, on 2026-11-21)
#define RMG_LANE_PLOT_PITCH		42		///< cells between the middles of two neighbouring plots of a column: 10 of green between them, as at 24 across and 34
#define RMG_LANE_DERRICK_CLEAR	7		///< cells behind a plot column's back edge no prop stands in, room for the client's income derricks (a 46 by 42 TechOilDerrick 1 cell off the edge reaches 5.6)
#define RMG_LANE_WEAR_FORTRESS	8.0f	///< cells past a fortress's footprint the ground round it is worn, at most
#define RMG_LANE_WEAR_TOWER		5.0f	///< and past a tower's
#define RMG_LANE_WEAR_FRINGE	0.35f	///< the share of that past which the worn ground only freckles the bank
#define RMG_LANE_SEA_END		103		///< cells from the back of a plot column to the playable edge: green, beach and sea (an odd height keeps the lane's 45 in the middle); 111 before the plot grew 16 cells deeper, 95 until it turned upright 8 cells shallower, so the map stays 642 by 361
#define RMG_LANE_GROUND			64		///< height bytes of the lane, the plots and the level ring round them
#define RMG_LANE_FLAT_RING		3		///< cells of level ground round all of those
#define RMG_LANE_RING_WANDER	4.0f	///< and up to this many more on a slow noise field, so the hills' foot is no ruled line
#define RMG_LANE_HILLS			22.0f	///< bytes the bank's hills rise at most
#define RMG_LANE_HILL_REACH		14.0f	///< cells over which they come up off the beach
#define RMG_LANE_HILL_FOOT		24.0f	///< and off the level ring round play, gentler, so a low sun draws no edge along the lane
#define RMG_LANE_WATER			50		///< of every water surface, under the ground so no sea can flood it
#define RMG_LANE_WATER_BED		38		///< of the sea's bed
#define RMG_LANE_CORE_SIDE		9.0f	///< cells the island's core stands off the plots' outer edges, across the lane
#define RMG_LANE_CORE_ROUND		50.0f	///< cells, the core's rounded ends across the island
#define RMG_LANE_END_ROUND		55.0f	///< and along it, so an end is a half ellipse round its plot column
#define RMG_LANE_TIP_REACH		20.0f	///< the reach straight off an end at most, which keeps 50 cells of sea past the tip (26 while the plot column stood 10 cells further in; the upright plot's column ends 8 cells further in again, about 59 of sea)
#define RMG_LANE_REACH_MEAN		14.0f	///< cells from the core to the coast on the seed's thinnest island, on average
#define RMG_LANE_REACH_SEED		14.0f	///< and how much more a seed may add to that
#define RMG_LANE_REACH_SWING	34.0f	///< cells the coast wanders either side on the slow field, the island's shape
#define RMG_LANE_BAY_SWING		14.0f	///< and on the quick one, its bays and capes
#define RMG_LANE_REACH_LEAST	16.0f	///< never nearer the plots than this: the level ring and a whole beach
#define RMG_LANE_REACH_MOST		44.0f	///< never further out than this, so the sea round it stays wide
#define RMG_LANE_SEA_LEAST		14.0f	///< cells of sea inside every playable edge at least
#define RMG_LANE_BEACH			8.0f	///< cells over which the ground rises from the waterline to the bank
#define RMG_LANE_SAND			6.0f	///< cells of beach in from the waterline at least, up to 3 more on a noise field
#define RMG_LANE_SHELF			10.0f	///< cells of shallow water off the waterline, a few bytes deep, before the bed drops
#define RMG_LANE_DROP			6.0f	///< and the cells over which it drops to the bed
#define RMG_LANE_PROP_CLEAR		3		///< cells a tree or stone keeps off play

/// Everything the constants above put somewhere, in playable cells; vertices at the integers.
struct RMGLaneShape
{
	Int m_playX, m_playY;				///< playable cells, 16:9
	Int m_laneX0, m_laneX1, m_laneY0, m_laneY1;	///< the lane's corner vertices
	Int m_plotX0[2];					///< the west vertex of team A's plots and of team B's
	Int m_plotY0[3];					///< the low y vertex of seat 1's, 2's and 3's
	Real m_middleX, m_middleY;			///< the lane's middle
	Real m_half;						///< half its length
	Real m_fortressX[2];				///< team A's fortress and team B's, on the middle line
	Real m_towerX[2];					///< and their towers
	Real m_spawnX[2];					///< each team's spawn anchor, on the middle line behind its fortress
};

static RMGLaneShape rmgLaneShape( void )
{
	RMGLaneShape s;
	s.m_half = 0.5f * (Real)RMG_LANE_LENGTH;
	s.m_playX = 2 * ( RMG_LANE_SEA_END + RMG_LANE_PLOT_ALONG + RMG_LANE_PLOT_GAP ) + RMG_LANE_LENGTH;
	s.m_playY = s.m_playX * 9 / 16;
	s.m_laneX0 = RMG_LANE_SEA_END + RMG_LANE_PLOT_ALONG + RMG_LANE_PLOT_GAP;
	s.m_laneX1 = s.m_laneX0 + RMG_LANE_LENGTH;
	s.m_laneY0 = ( s.m_playY - RMG_LANE_WIDTH ) / 2;
	s.m_laneY1 = s.m_laneY0 + RMG_LANE_WIDTH;
	s.m_plotX0[0] = RMG_LANE_SEA_END;
	s.m_plotX0[1] = s.m_laneX1 + RMG_LANE_PLOT_GAP;
	s.m_plotY0[0] = s.m_laneY0 + ( RMG_LANE_WIDTH - RMG_LANE_PLOT_ACROSS ) / 2;
	s.m_plotY0[1] = s.m_plotY0[0] - RMG_LANE_PLOT_PITCH;
	s.m_plotY0[2] = s.m_plotY0[0] + RMG_LANE_PLOT_PITCH;
	s.m_middleX = 0.5f * (Real)( s.m_laneX0 + s.m_laneX1 );
	s.m_middleY = 0.5f * (Real)( s.m_laneY0 + s.m_laneY1 );
	for( Int team = 0; team < 2; team++ )
	{
		const Real ahead = team == 0 ? 1.0f : -1.0f;		// towards the other end
		s.m_fortressX[team] = s.m_middleX - ahead * RMG_LANE_FORTRESS_AT * s.m_half;
		s.m_towerX[team] = s.m_middleX - ahead * RMG_LANE_TOWER_AT * s.m_half;
		s.m_spawnX[team] = s.m_fortressX[team] - ahead * ( RMG_LANE_FORTRESS_HALF + RMG_LANE_PAD_GAP + 0.5f * (Real)RMG_LANE_PLOT_ALONG );
	}
	return s;
}

/// The larger of a and b, rounded over the last k either side of where they cross, so a coast held off play by it bends instead of kinking.
static Real rmgSoftMax( Real a, Real b, Real k )
{
	const Real d = a - b;
	if( d >= k ) return a;
	if( d <= -k ) return b;
	return b + ( d + k ) * ( d + k ) / ( 4.0f * k );
}

/// One lane seed in five wears the twilight style's palette instead of its biome.
static Bool rmgLaneTwilight( Int seed )
{
	return hashCell( (UnsignedInt)seed + 51007U, 0, 0 ) % 5U == 0;
}

/* The lane map's ground, a palette per theme in place of the biome's own four. The bank is the whole
	island, the lane and the plots included (the owner's rule of 2026-11-20); the shore is a light sand
	in every theme but winter's so a beach rings the island (on the desert, the steppe and at twilight
	the old shore was the bank's own colour); the third class is the worn ground round the fortresses
	and towers, dirt and gravel, packed sand or slush, the lane's own ground before that rule (winter's
	was frozen golden sand, slush now). The fourth, the plots' ground before it, is painted nowhere,
	except in winter (m_bankMix), where it is a second snow laid in slow patches over the first.
	Winter's bank was SnowType1, a sheet of four tiles whose green-flecked mottle repeated every two
	cells and read as a woven grid from the plan camera, and its slush hardly showed on that mottle;
	the bank is the six by six SnowLargeType1 now, broken up by the specked SnowType6, and the grey
	slush stands out on the plain white. The rest of the biome (stones, light) stays its own, and so
	do the trees unless a row names its own. Every texture is in theTextureSheets with the tile count
	the shipped maps declare.

	The temperate woods drop TreeDogwood1, whose white blossom read as snow from above, and
	TreeFir01B, whose PTFir01 is a snowed-on fir; TreeMaple1's dull grey crown gave way to TreeOak05
	with them.

	A plot's slot grid takes its line colour from the theme (LaneGridColours): pale on the dark grass
	and on the dusk ground, a deep shade of the ground on the light sand, dirt and snow, the plot's
	outline stronger than the lines between its slots. Black lines on every theme were lost on the
	darker grounds. */
struct RMGLaneGround
{
	const char *m_textures[RMG_TERRAIN_COUNT];
	UnsignedByte m_previewColours[RMG_TERRAIN_COUNT][3];
	const char *m_trees[RMG_BIOME_TREES];		///< NULL first: the biome's own
	UnsignedByte m_gridColour[3];				///< the slot grid's lines
	UnsignedByte m_gridAlpha[2];				///< between the slots and round the plot
	Bool m_bankMix;								///< the fourth class is a second bank ground, laid in patches over the first
};

static const RMGLaneGround theLaneGrounds[RMG_BIOME_COUNT + 1] =
{
	{	// temperate: dark grass, cream beach, brown gravel round the walls, green oaks, straw lines
		{ "GrassType1", "SandMediumType13", "DirtType1", "SandLargeType3Light" },
		{ { 86, 122, 62 }, { 247, 231, 195 }, { 125, 107, 88 }, { 238, 219, 158 } },
		{ "TreeOak01", "TreeOak05", "TreeOak02", "TreeOak06" },
		{ 234, 230, 172 }, { 0x60, 0x98 }, FALSE
	},
	{	// desert: yellow sand, cream beach, packed pebbled dirt round the walls, umber lines
		{ "SandLargeType2", "SandMediumType13", "DirtMediumType10", "DirtType3" },
		{ { 237, 203, 129 }, { 247, 231, 195 }, { 191, 161, 107 }, { 116, 86, 60 } },
		{ NULL, NULL, NULL, NULL },
		{ 92, 60, 28 }, { 0x68, 0xA0 }, FALSE
	},
	{	// winter: two snows in patches, frozen mud at the water, slush round the walls, slate lines
		{ "SnowLargeType1", "DirtMediumType15Snow", "DirtSnowType1", "SnowType6" },
		{ { 236, 238, 240 }, { 236, 236, 235 }, { 140, 132, 122 }, { 226, 228, 230 } },
		{ NULL, NULL, NULL, NULL },
		{ 52, 72, 104 }, { 0x60, 0x98 }, TRUE
	},
	{	// steppe: golden ground, pale beach, red-brown gravel round the walls, dark earth lines
		{ "DirtMediumType8", "SandLargeType3Light", "DirtType2", "GrassMediumType21" },
		{ { 206, 174, 120 }, { 238, 219, 158 }, { 122, 88, 59 }, { 126, 123, 69 } },
		{ NULL, NULL, NULL, NULL },
		{ 80, 52, 30 }, { 0x68, 0xA0 }, FALSE
	},
	{	// twilight: its tan dirt, pale beach, red gravel round the walls, lavender lines that read at dusk
		{ "DirtMediumType10", "SandLargeType3Light", "DirtType3", "SandMediumType13" },
		{ { 191, 161, 107 }, { 238, 219, 158 }, { 116, 86, 60 }, { 247, 231, 195 } },
		{ NULL, NULL, NULL, NULL },
		{ 214, 206, 238 }, { 0x58, 0x90 }, FALSE
	},
};

/// The biome a lane map of this theme wears: theBiomes[theme], or the twilight style's for the last, on theLaneGrounds.
static const RMGBiome *rmgLaneBiome( Int theme )
{
	struct Table
	{
		RMGBiome m_biomes[RMG_BIOME_COUNT + 1];
		Table()
		{
			for( Int i = 0; i <= RMG_BIOME_COUNT; i++ )
			{
				m_biomes[i] = i < RMG_BIOME_COUNT ? theBiomes[i] : theStyles[0].m_biome;
				for( Int k = 0; k < RMG_TERRAIN_COUNT; k++ )
				{
					m_biomes[i].m_textures[k] = theLaneGrounds[i].m_textures[k];
					for( Int c = 0; c < 3; c++ )
						m_biomes[i].m_previewColours[k][c] = theLaneGrounds[i].m_previewColours[k][c];
				}
				if( theLaneGrounds[i].m_trees[0] != NULL )
					for( Int k = 0; k < RMG_BIOME_TREES; k++ )
						m_biomes[i].m_trees[k] = theLaneGrounds[i].m_trees[k];
			}
		}
	};
	static const Table table;	// built once, thread-safe as a function's static
	return &table.m_biomes[theme];
}

void RMGLayout::buildLane( void )
{
	m_style = NULL;
	m_styleScale = 1.0f;
	m_skeleton = FALSE;
	m_type = &theMapTypes[RMG_MAP_PLAINS];
	/* The theme: one of the four biomes as a plain map's seed picks them (rmgBiomeFor, and its hour and
		snow with it), or one time in five the twilight style's palette at dusk. The style itself is lit
		for the afternoon, under which the twilight lane read as a plain sunny day. */
	const UnsignedInt seed = (UnsignedInt)m_settings.m_seed;
	Int theme = rmgBiomeFor( m_settings.m_seed );
	m_timeOfDay = rmgTimeOfDayFor( m_settings.m_seed );
	m_snowy = rmgSnowsOn( m_settings.m_seed );
	if( rmgLaneTwilight( m_settings.m_seed ) )
	{
		theme = RMG_BIOME_COUNT;
		m_timeOfDay = TIME_OF_DAY_EVENING;
		m_snowy = FALSE;
	}
	m_biome = rmgLaneBiome( theme );
	const RMGLaneGround& ground = theLaneGrounds[theme];
	seedPermutation( m_settings.m_seed, m_perm );
	m_sites.clear();
	const RMGLaneShape shape = rmgLaneShape();
	m_width = shape.m_playX + 2 * RMG_BORDER_CELLS;
	m_height = shape.m_playY + 2 * RMG_BORDER_CELLS;
	m_waterHeight = (Real)RMG_LANE_WATER;
	m_playable = TRUE;
	m_starts.clear();
	m_objects.clear();
	m_waypointLinks.clear();
	m_lakes.clear();
	m_areas.clear();
	const Int cells = m_width * m_height;
	// the sea is one water area over the whole map, so every cell is in it and is water where it is low
	m_inLake.assign( cells, 1 );

	/* A vertex is play (the lane or a plot) or bank, and a bank vertex knows how many cells it stands
		off the nearest vertex in play (Chebyshev, capped). Area 0 is the lane, 1 to 6 the plots of
		starts 1 to 6. */
	enum { LANE_BANK = 0, LANE_FLOOR, LANE_PLOT };
	const Int areas = RMG_LANE_SEATS + 1, offCap = 64;
	Int areaX0[RMG_LANE_SEATS + 1], areaY0[RMG_LANE_SEATS + 1], areaX1[RMG_LANE_SEATS + 1], areaY1[RMG_LANE_SEATS + 1];
	for( Int area = 0; area < areas; area++ )
	{
		if( area == 0 )
		{
			areaX0[area] = shape.m_laneX0;
			areaY0[area] = shape.m_laneY0;
			areaX1[area] = shape.m_laneX1;
			areaY1[area] = shape.m_laneY1;
		}
		else
		{
			areaX0[area] = shape.m_plotX0[( area - 1 ) & 1];
			areaY0[area] = shape.m_plotY0[( area - 1 ) >> 1];
			areaX1[area] = areaX0[area] + RMG_LANE_PLOT_ALONG;
			areaY1[area] = areaY0[area] + RMG_LANE_PLOT_ACROSS;
		}
	}
	std::vector<char> kind( cells, LANE_BANK );
	std::vector<Int> offPlay( cells, offCap );
	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			const Int px = x - RMG_BORDER_CELLS, py = y - RMG_BORDER_CELLS, i = cellIndex( x, y );
			for( Int area = 0; area < areas; area++ )
			{
				const Int dx = px < areaX0[area] ? areaX0[area] - px : px > areaX1[area] ? px - areaX1[area] : 0;
				const Int dy = py < areaY0[area] ? areaY0[area] - py : py > areaY1[area] ? py - areaY1[area] : 0;
				const Int off = dx > dy ? dx : dy;
				if( off == 0 && kind[i] != LANE_PLOT )
					kind[i] = (char)( area >= 1 ? LANE_PLOT : LANE_FLOOR );
				if( off < offPlay[i] )
					offPlay[i] = off;
			}
		}
	}

	/* The island, a blob round play after the Direct Strike minimap. Its core is a rectangle round the
		lane and both plot columns, RMG_LANE_CORE_SIDE off the plots' outer edges, whose ends are half
		ellipses RMG_LANE_END_ROUND along the lane by RMG_LANE_CORE_ROUND across, set so that each passes
		through the outer corner of its column's last plot. The ends were the corners of a rectangle
		rounded by 30 once, and a coast held off the plots there ran straight down the whole column; an
		ellipse bulges past the column's middle and comes in at its corners, so the end is round. The
		coast lies a reach off the core's edge, measured straight out from it, and the reach wanders on
		two noise fields per seed, a slow one for the island's shape and a quicker one for its bays and
		capes, both read on a curve outside the core so they move the coast along the long sides as much
		as round the ends. Each ray out of the core crosses the coast once, so the land is one island
		with no pond in it: inside an elliptical end a ray leaves the ellipse's middle, keeps its way out
		and gains core distance evenly along it.

		The reach keeps RMG_LANE_REACH_LEAST off the plots and the lane's ends, a little more on the
		quick field so a held coast still wanders, and may cut into the core beside the lane, where the
		core stands wide of it, down to that much off the lane's edge. It never passes
		RMG_LANE_REACH_MOST, less towards RMG_LANE_TIP_REACH as the way out turns along the lane, nor
		RMG_LANE_SEA_LEAST off the playable edge. The coast is a distance in cells, positive inland,
		continuous everywhere a beach or a hill reads it, so the waterline is a smooth curve with no
		staircase on the diagonals. Every noise is read off the lattice, whose zero lines once left the
		first map's hills flat along its low x and low y edges. */
	const Real bendX = RMG_LANE_END_ROUND, bendY = RMG_LANE_CORE_ROUND;
	const Real columnX = shape.m_middleX - (Real)shape.m_plotX0[0];
	const Real columnY = std::max( shape.m_middleY - (Real)shape.m_plotY0[1],
		(Real)( shape.m_plotY0[2] + RMG_LANE_PLOT_ACROSS ) - shape.m_middleY );
	const Real coreY = columnY + RMG_LANE_CORE_SIDE, innerY = coreY - bendY;
	const Real cornerV = ( columnY - innerY ) / bendY;
	const Real innerX = columnX - bendX * sqrtf( 1.0f - cornerV * cornerV );	// the column's corner on the ellipse
	const Real besideLaneCut = coreY - 0.5f * (Real)RMG_LANE_WIDTH;		// the core's edge to the lane's, beside it
	// each long side's mean reach is the seed's own, so one island is fat below and thin above and the next the other way
	const Real lowReach = RMG_LANE_REACH_MEAN + RMG_LANE_REACH_SEED * hashUnit( seed + 51011U, 0, 0 );
	const Real highReach = RMG_LANE_REACH_MEAN + RMG_LANE_REACH_SEED * hashUnit( seed + 51011U, 0, 1 );
	const Real probe = 20.0f;		// cells out from the core's edge the noise is read at
	std::vector<Real> coast( cells );
	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			const Int px = x - RMG_BORDER_CELLS, py = y - RMG_BORDER_CELLS;
			const Real rx = (Real)px - shape.m_middleX, ry = (Real)py - shape.m_middleY;
			const Real sx = rx < 0.0f ? -1.0f : 1.0f, sy = ry < 0.0f ? -1.0f : 1.0f;
			const Real qx = fabsf( rx ) - innerX, qy = fabsf( ry ) - innerY;

			/* the signed distance to the core, the way straight out of it and the core's edge where that
				way crosses it. Past the inner rectangle the distance is the ellipse's level over its
				gradient, exact on both long and short sides and close round the end. */
			Real core, nx, ny, edgeX, edgeY;
			if( qx > 0.0f || qy > 0.0f )
			{
				const Real ox = std::max( qx, 0.0f ), oy = std::max( qy, 0.0f );
				const Real u = ox / bendX, v = oy / bendY;
				const Real level = sqrtf( u * u + v * v );
				const Real gx = u / bendX, gy = v / bendY, slope = sqrtf( gx * gx + gy * gy );
				core = ( level - 1.0f ) * level / slope;
				nx = sx * gx / slope;
				ny = sy * gy / slope;
				edgeX = sx * ( std::min( fabsf( rx ), innerX ) + ox / level );
				edgeY = sy * ( std::min( fabsf( ry ), innerY ) + oy / level );
			}
			else if( qx - bendX >= qy - bendY )
			{
				core = qx - bendX;
				nx = sx;
				ny = 0.0f;
				edgeX = sx * ( innerX + bendX );
				edgeY = ry;
			}
			else
			{
				core = qy - bendY;
				nx = 0.0f;
				ny = sy;
				edgeX = rx;
				edgeY = sy * ( innerY + bendY );
			}

			const Real noiseX = shape.m_middleX + edgeX + probe * nx, noiseY = shape.m_middleY + edgeY + probe * ny;
			const Real shapeNoise = fractalNoise( m_perm, noiseX * 0.011f + 41.3f, noiseY * 0.011f + 27.9f, 3 );
			const Real bayNoise = fractalNoise( m_perm, noiseX * 0.035f + 13.7f, noiseY * 0.035f + 52.1f, 2 );
			// beside the lane a bay may come in to the lane's own least; the cut eases in over 40 cells,
			// as a quicker one squeezed the beach into one steep cell where it held the coast, and is
			// gone by the start of an end
			const Real beside = ny == 0.0f ? 0.0f : rmgUnit( ( std::min( shape.m_half + 8.0f, innerX ) - std::min( fabsf( rx ), innerX ) ) / 40.0f );
			const Real least = RMG_LANE_REACH_LEAST - beside * besideLaneCut + 4.0f + 5.0f * bayNoise;
			const Real meanReach = 0.5f * ( lowReach + highReach ) + 0.5f * ( highReach - lowReach ) * ny;
			const Real most = RMG_LANE_REACH_MOST - ( RMG_LANE_REACH_MOST - RMG_LANE_TIP_REACH ) * nx * nx;
			Real reach = meanReach + RMG_LANE_REACH_SWING * shapeNoise + RMG_LANE_BAY_SWING * bayNoise;
			reach = std::min( rmgSoftMax( reach, least, 6.0f ), most );

			const Real edge = (Real)std::min( std::min( px, py ), std::min( shape.m_playX - px, shape.m_playY - py ) );
			coast[cellIndex( x, y )] = std::min( reach - core, edge - RMG_LANE_SEA_LEAST );
		}
	}

	/* Play and the ring round it are level at RMG_LANE_GROUND. Past the ring the bank rolls up into low
		hills, which settle again towards the coast. The beach rises from just over the water at the
		coast to the bank over RMG_LANE_BEACH cells; off it a shelf RMG_LANE_SHELF cells wide lies one
		to five bytes under the water, through which the sand shows, and the bed drops past it to
		RMG_LANE_WATER_BED over RMG_LANE_DROP cells.

		The ring is RMG_LANE_FLAT_RING wide plus up to RMG_LANE_RING_WANDER more on a slow noise field,
		and the hills come up off it over RMG_LANE_HILL_FOOT cells, not the beach's 14. As wide
		everywhere and that quick, the rise began at one distance all along the lane and reached 10
		degrees seven cells out, and under the twilight's low sun the lit lane met the shaded bank in
		a ruled crease down each of its edges; over 24 cells the bank tilts 6 degrees at most. It stays
		measured in cell steps: straight distance reaches further into the rise across a plot's corner
		and steepened the ground there past what the slope test allows near play. */
	m_heights.assign( cells, (UnsignedByte)RMG_LANE_GROUND );
	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			const Int i = cellIndex( x, y );
			Real height = (Real)RMG_LANE_GROUND;
			if( kind[i] == LANE_BANK )
			{
				const Real hill = 0.5f + 0.5f * fractalNoise( m_perm, (Real)x * 0.035f + 31.7f, (Real)y * 0.035f + 17.3f, 3 );
				const Real ring = (Real)RMG_LANE_FLAT_RING + RMG_LANE_RING_WANDER
					* rmgUnit( 0.5f + 0.9f * fractalNoise( m_perm, (Real)x * 0.06f + 63.1f, (Real)y * 0.06f + 9.7f, 2 ) );
				const Real offRing = fadeCurve( rmgUnit( ( (Real)offPlay[i] - ring ) / RMG_LANE_HILL_FOOT ) );
				const Real inland = fadeCurve( rmgUnit( ( coast[i] - RMG_LANE_BEACH ) / RMG_LANE_HILL_REACH ) );
				height += RMG_LANE_HILLS * hill * offRing * inland;
				const Real shore = (Real)RMG_LANE_WATER + 0.5f;
				if( coast[i] >= 0.0f )
					height = shore + ( height - shore ) * fadeCurve( rmgUnit( coast[i] / RMG_LANE_BEACH ) );
				else
				{
					const Real shelf = (Real)RMG_LANE_WATER - 1.0f, foot = (Real)RMG_LANE_WATER - 5.0f;
					height = shelf - ( shelf - foot ) * fadeCurve( rmgUnit( -coast[i] / RMG_LANE_SHELF ) )
						- ( foot - (Real)RMG_LANE_WATER_BED ) * fadeCurve( rmgUnit( ( -coast[i] - RMG_LANE_SHELF ) / RMG_LANE_DROP ) );
				}
			}
			m_heights[i] = (UnsignedByte)(Int)( height + 0.5f );
		}
	}
	RMGLake sea;
	sea.m_cellX = shape.m_middleX;
	sea.m_cellY = shape.m_middleY;
	const Real reach = (Real)RMG_BORDER_CELLS;
	const RMGPoint corners[4] = { { -reach, -reach }, { shape.m_playX + reach, -reach },
		{ shape.m_playX + reach, shape.m_playY + reach }, { -reach, shape.m_playY + reach } };
	sea.m_polygon.assign( corners, corners + 4 );
	m_lakes.push_back( sea );

	/* The theme's own ground everywhere, the lane and the plots included (the owner's rule of
		2026-11-20: no ground of their own, no centre band; a plot shows only the slot grid the client
		draws over it, LaneGridColours), and a beach of the shore's light sand from the waterline in
		RMG_LANE_SAND cells and up to three more on a noise field so the coast wears no drawn ring. The
		blends soften its edge.

		Round each fortress and tower the ground is worn, as under a building that has stood there a
		long time: the theme's trampled class (theLaneGrounds' third, dirt and gravel, packed sand,
		slush) under the footprint and out to RMG_LANE_WEAR_FORTRESS or RMG_LANE_WEAR_TOWER past it,
		that reach wandering a third either way on a slow noise field so the patch is no rounded
		square. Inside the last RMG_LANE_WEAR_FRINGE of it the ground is worn solid, a little ragged on
		a quick field; past that it only freckles the bank, a lone cell on a hash, thinner outwards and
		out to a sixth past the reach. The renderer blends two grounds across one cell, so a solid patch
		had an edge of whole cells, straight and diagonal steps that read as blocks from close; a lone
		worn cell draws as a soft spot, and a fringe of them fades the edge out over several. Freckles
		sit only on every third cell each way: two closer than that put opposite corners on the cell
		between, which settleTerrainForBlends fills, and a dense fringe filled into a bigger solid patch
		with the same blocky edge.

		In winter (m_bankMix) a second snow lies in slow patches over the bank, kept off the worn ground
		and its fringe, where it would take the slush's edge. */
	Int propID = 1;
	AsciiString uniqueID;
	m_terrain.assign( cells, (UnsignedByte)RMG_TERRAIN_GRASS );
	m_passable.assign( cells, 0 );
	const Real wornX[4] = { shape.m_fortressX[0], shape.m_fortressX[1], shape.m_towerX[0], shape.m_towerX[1] };
	const Real wornHalf[4] = { RMG_LANE_FORTRESS_HALF, RMG_LANE_FORTRESS_HALF, RMG_LANE_TOWER_HALF, RMG_LANE_TOWER_HALF };
	const Real wornReach[4] = { RMG_LANE_WEAR_FORTRESS, RMG_LANE_WEAR_FORTRESS, RMG_LANE_WEAR_TOWER, RMG_LANE_WEAR_TOWER };
	const Real fringeOut = 1.0f / 6.0f;		// the freckles reach this share of the reach past it
	for( Int y = 0; y + 1 < m_height; y++ )
	{
		for( Int x = 0; x + 1 < m_width; x++ )
		{
			const Int i = cellIndex( x, y );
			if( ground.m_bankMix && fractalNoise( m_perm, (Real)x * 0.045f + 87.3f, (Real)y * 0.045f + 45.6f, 3 ) > 0.08f )
				m_terrain[i] = (UnsignedByte)RMG_TERRAIN_ROCK;
			const Real shoreWander = 0.5f + 0.5f * fractalNoise( m_perm, (Real)x * 0.07f + 21.4f, (Real)y * 0.07f + 15.9f, 2 );
			const Real cellCoast = 0.25f * ( coast[i] + coast[cellIndex( x + 1, y )] + coast[cellIndex( x, y + 1 )] + coast[cellIndex( x + 1, y + 1 )] );
			if( cellCoast < RMG_LANE_SAND + 3.0f * shoreWander )
				m_terrain[i] = (UnsignedByte)RMG_TERRAIN_SAND;

			const Real cx = (Real)( x - RMG_BORDER_CELLS ) + 0.5f, cy = (Real)( y - RMG_BORDER_CELLS ) + 0.5f;
			const Real outline = 1.0f + 0.33f * fractalNoise( m_perm, (Real)x * 0.09f + 5.3f, (Real)y * 0.09f + 61.7f, 2 );
			const Real patch = fractalNoise( m_perm, (Real)x * 0.23f + 71.9f, (Real)y * 0.23f + 33.1f, 2 );
			// a freckle only on every third cell each way, so no two share a neighbour and get filled in between
			const Real speck = x % 3 == 0 && y % 3 == 0 ? (Real)( hashCell( seed + 51013U, x, y ) % 1024U ) / 1024.0f : 1.0f;
			for( Int s = 0; s < 4; s++ )
			{
				const Real dx = std::max( fabsf( cx - wornX[s] ) - wornHalf[s], 0.0f );
				const Real dy = std::max( fabsf( cy - shape.m_middleY ) - wornHalf[s], 0.0f );
				const Real wear = 1.0f - sqrtf( dx * dx + dy * dy ) / ( wornReach[s] * outline );
				if( wear <= -fringeOut - 0.25f )
					continue;
				if( m_terrain[i] == RMG_TERRAIN_ROCK )
					m_terrain[i] = (UnsignedByte)RMG_TERRAIN_GRASS;
				const Real fringe = rmgUnit( ( wear + fringeOut ) / ( RMG_LANE_WEAR_FRINGE + fringeOut ) );
				if( wear + 0.15f * patch > RMG_LANE_WEAR_FRINGE || speck < 0.8f * fringe )
					m_terrain[i] = (UnsignedByte)RMG_TERRAIN_DIRT;
			}

			const Bool inside = x >= RMG_BORDER_CELLS && y >= RMG_BORDER_CELLS && x < RMG_BORDER_CELLS + shape.m_playX
				&& y < RMG_BORDER_CELLS + shape.m_playY;
			m_passable[i] = inside && cellSpanWorld( x, y ) <= RMG_CLIFF_WORLD_SPAN && !underwaterAtCell( x, y ) ? 1 : 0;
		}
	}

	/* Trees and stones on the dry bank inside the playable edge, never within RMG_LANE_PROP_CLEAR of
		play and never on the beach, where a stone stood half in the water. How wooded and how stony the
		seed rolls; a slow noise field gathers the trees into patches of forest with a few strays
		between, more patches along the long sides than round the ends. */
	const Real woods = ( 0.10f + 0.15f * hashUnit( seed + 51001U, 0, 0 ) ) * m_biome->m_treeShare;
	const Real strays = 0.004f * m_biome->m_treeShare;
	const Real stones = 0.003f + 0.010f * hashUnit( seed + 51001U, 0, 1 );
	for( Int y = RMG_BORDER_CELLS; y < RMG_BORDER_CELLS + shape.m_playY; y++ )
	{
		for( Int x = RMG_BORDER_CELLS; x < RMG_BORDER_CELLS + shape.m_playX; x++ )
		{
			if( offPlay[cellIndex( x, y )] <= RMG_LANE_PROP_CLEAR || offPlay[cellIndex( x + 1, y + 1 )] <= RMG_LANE_PROP_CLEAR )
				continue;
			const Real dry = std::min( coast[cellIndex( x, y )], coast[cellIndex( x + 1, y + 1 )] ) - RMG_LANE_BEACH;
			if( dry < 1.0f )
				continue;
			const UnsignedInt hash = hashCell( seed + 51003U, x, y );
			const Real roll = (Real)( hash % 1024U ) / 1024.0f;
			const Real cellX = (Real)( x - RMG_BORDER_CELLS ) + 0.5f, cellY = (Real)( y - RMG_BORDER_CELLS ) + 0.5f;
			// the strip behind each plot column where the client stands the income derricks
			const Real columnBackA = (Real)shape.m_plotX0[0], columnBackB = (Real)( shape.m_plotX0[1] + RMG_LANE_PLOT_ALONG );
			if( cellY > (Real)shape.m_plotY0[1] && cellY < (Real)( shape.m_plotY0[2] + RMG_LANE_PLOT_ACROSS )
					&& ( ( cellX < columnBackA && cellX > columnBackA - RMG_LANE_DERRICK_CLEAR )
						|| ( cellX > columnBackB && cellX < columnBackB + RMG_LANE_DERRICK_CLEAR ) ) )
				continue;
			const Bool longSide = cellX > (Real)shape.m_laneX0 && cellX < (Real)shape.m_laneX1;
			const Real patch = fractalNoise( m_perm, (Real)x * 0.05f + 7.1f, (Real)y * 0.05f + 3.9f, 3 ) + ( longSide ? 0.1f : -0.15f );
			const char *templateName = NULL;
			if( roll < ( patch > 0.12f ? woods : strays ) )
				templateName = m_biome->m_trees[( hash >> 10 ) % (UnsignedInt)RMG_BIOME_TREES];
			else if( roll > 1.0f - stones && dry >= 3.0f )
				templateName = m_biome->m_rocks[( hash >> 10 ) % (UnsignedInt)RMG_BIOME_ROCKS];
			if( templateName == NULL || !siteIsClear( cellX, cellY, 0.5f ) )
				continue;
			uniqueID.format( "Prop %d", propID++ );
			addObject( templateName, uniqueID.str(), cellX, cellY, (Real)( ( hash >> 14 ) % 8U ) * PI * 0.25f );
		}
	}

	Int waypointID = 1;
	auto addWaypoint = [&]( const AsciiString& name, const AsciiString& label, Real cellX, Real cellY ) -> Int
	{
		RMGObject waypoint;
		waypoint.m_templateName = "*Waypoints/Waypoint";
		waypoint.m_uniqueID = name;
		waypoint.m_worldX = cellX * MAP_XY_FACTOR;
		waypoint.m_worldY = cellY * MAP_XY_FACTOR;
		waypoint.m_angle = 0.0f;
		waypoint.m_waypointID = waypointID++;
		waypoint.m_flags = 0;
		waypoint.m_pathLabel = label;
		m_objects.push_back( waypoint );
		return waypoint.m_waypointID;
	};
	auto addStructure = [&]( const char *templateName, const AsciiString& name, const AsciiString& owner, Real cellX,
		Real cellY, Real angle )
	{
		addObject( templateName, name.str(), cellX, cellY, angle );
		m_objects.back().m_objectName = name;
		m_objects.back().m_owner = owner;
	};

	AsciiString name, label;
	for( Int start = 0; start < RMG_LANE_SEATS; start++ )
	{
		const Int team = start & 1, seat = start >> 1;
		const Real x0 = (Real)shape.m_plotX0[team], y0 = (Real)shape.m_plotY0[seat];
		const Real x1 = x0 + RMG_LANE_PLOT_ALONG, y1 = y0 + RMG_LANE_PLOT_ACROSS;
		RMGPoint centre = { 0.5f * ( x0 + x1 ), 0.5f * ( y0 + y1 ) };
		m_starts.push_back( centre );
		name.format( "Player_%d_Start", start + 1 );
		addWaypoint( name, AsciiString::TheEmptyString, centre.m_cellX, centre.m_cellY );

		RMGArea area;
		area.m_name.format( "LaneWar_Build%c%d", 'A' + team, seat + 1 );
		const RMGPoint corners[4] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
		area.m_polygon.assign( corners, corners + 4 );
		m_areas.push_back( area );
	}

	const Real middleX = shape.m_middleX, middleY = shape.m_middleY;
	for( Int team = 0; team < 2; team++ )
	{
		const char side = (char)( 'A' + team );
		const Real ahead = team == 0 ? 1.0f : -1.0f;		// towards the other end
		const Real fortressX = shape.m_fortressX[team];
		const Real enemyFortressX = shape.m_fortressX[1 - team];
		AsciiString owner;
		owner.format( "StartOwner:%d,%d,%d", team + 1, team + 3, team + 5 );

		name.format( "LaneWar_Spawn%c", side );
		addWaypoint( name, AsciiString::TheEmptyString, shape.m_spawnX[team], middleY );

		// along the middle line from just ahead of the team's own fortress to the other one, the last point
		label.format( "LaneWar_Lane%c", side );
		const Real firstX = fortressX + ahead * ( RMG_LANE_FORTRESS_HALF + 3.0f );
		Int previous = 0, point = 0;
		for( Real along = 0.0f; ; along += RMG_LANE_PATH_STEP )
		{
			const Bool last = along > fabsf( enemyFortressX - firstX ) - 0.5f * RMG_LANE_PATH_STEP;
			name.format( "%s_%02d", label.str(), ++point );
			const Int id = addWaypoint( name, label, last ? enemyFortressX : firstX + ahead * along, middleY );
			if( previous != 0 )
				m_waypointLinks.push_back( std::make_pair( previous, id ) );
			previous = id;
			if( last )
				break;
		}

		const Real facing = team == 0 ? 0.0f : PI;
		name.format( "LaneWar_Fortress%c", side );
		addStructure( "LaneWarFortress", name, owner, fortressX, middleY, facing );
		for( Int tower = 0; tower < RMG_LANE_TOWERS; tower++ )
		{
			name.format( "LaneWar_Tower%c%d", side, tower + 1 );
			const Real towerY = RMG_LANE_TOWERS == 1 ? middleY
				: tower == 0 ? (Real)shape.m_laneY0 + RMG_LANE_TOWER_INSET : (Real)shape.m_laneY1 - RMG_LANE_TOWER_INSET;
			addStructure( "LaneWarWatchTower", name, owner, shape.m_towerX[team], towerY, facing );
		}
	}

	if( RMG_LANE_MIDDLE_TEMPLATE != NULL )
		addStructure( RMG_LANE_MIDDLE_TEMPLATE, AsciiString( "LaneWar_MiddleStructure" ), AsciiString::TheEmptyString, middleX, middleY, 0.0f );

	RMGArea middle;
	middle.m_name = "LaneWar_MiddleArea";
	for( Int point = 0; point < RMG_LANE_MIDDLE_SIDES; point++ )
	{
		const Real angle = 2.0f * PI * point / RMG_LANE_MIDDLE_SIDES;
		const RMGPoint at = { middleX + RMG_LANE_MIDDLE_RADIUS * Cos( angle ), middleY + RMG_LANE_MIDDLE_RADIUS * Sin( angle ) };
		middle.m_polygon.push_back( at );
	}
	m_areas.push_back( middle );

	buildBlends();
}

//-----------------------------------------------------------------------------
// The Apocalypse map
//-----------------------------------------------------------------------------

/* Apocalypse plays on this map: two to four seats start together in a sunk basin in the middle and
	the dead walk in out of the fog without end (ApocalypseDirector.h). The map is always the
	generator's thirty-two seat normal size. The basin is flat at RMG_APOC_GROUND, one lobe a seat round
	a shared heart, its rim an escarpment of uneven height and width. One pass a seat and one or two on
	the seams cut it, each a canyon, a valley or a wide valley bending as it goes out, so whatever comes
	in comes through a pass the players can hold. Past the rim lies RMG_APOC_WILDERNESS cells of
	wilderness: hills, a dry riverbed, rocky ground, woods in clumps, and a worn trail with no tree or
	stone on it winding down every pass's line into the fog, so the dead never get stuck in a wood on
	their way in. The fog takes the rest of the map, and the dead rise in its inner edge.

	Each start stands on its lobe's axis with a supply dock out along it; the heart holds a dock and
	a derrick on every seam, and a small supply pile lies outside each pass beside its trail, worth a
	sortie. The names the director looks up:

	Apocalypse_Centre               waypoint, the middle, where every new zombie walks
	Apocalypse_Spawn_1 to _N        waypoints, one in the fog on each pass's line, RMG_APOC_SPAWN_PAST
	                                past the clear area: where its wave rises
	Apocalypse_Clear                trigger area, the outline the fog leaves: everything between it and
	                                the playable edge is shrouded for good and deadly to anything but
	                                the dead */
#define RMG_APOC_GROUND			60		///< height bytes of the clearing
/* The basin's size, set so a seat's lobe holds a whole late-game base with room over: America's,
	the largest, is about 1900 cells of footprint (Command Center, six power plants, two each of
	barracks, war factory and airfield, two supply centers, the strategy center, the uplink, a
	supply drop zone and a few defences, Geometry boxes in FactionBuilding.ini), 2900 with half again
	for lanes, against about 4000 flat cells a seat at four seats here (the log's "flat ground" line). */
#define RMG_APOC_START_PER_SEAT	7.0f	///< cells of start ring radius a seat, so a lobe stays as wide as a base
#define RMG_APOC_START_LEAST	80.0f	///< and never nearer the middle than this: the heart lies between the starts
#define RMG_APOC_DOCK_OUT		20.0f	///< cells from a start to its dock, straight out along its lobe
#define RMG_APOC_WALL_OUT		48.0f	///< cells from a start to the rim at its lobe's tip
#define RMG_APOC_HEART			40.0f	///< cells of shared heart round the middle, which every lobe opens onto
#define RMG_APOC_SEAM_PAD		16.0f	///< cells the rim keeps off the heart where two lobes meet
#define RMG_APOC_WALL_FACE		2.0f	///< cells each face of the wall takes
#define RMG_APOC_WALL_TOP		3.0f	///< cells across its top
#define RMG_APOC_FACE_MOST		6.0f	///< cells the face spreads over where it runs broad; the thickness above still holds it
#define RMG_APOC_FACE_STEEP		19.0f	///< bytes a cell the face rises at the least: 11.9 world units, past the pathfinder's 9.8
#define RMG_APOC_FACE_SMOOTHED	3.0f	///< cells the face rises over at the least once the smoothing has spread a narrow crag
#define RMG_APOC_SMOOTH_PAST	3.0f	///< cells past the rim's thickness the smoothing reaches
#define RMG_APOC_PASS_CLIMB		7.0f	///< bytes a cell a pass ramp climbs on average, so its steepest stretch stays under a cliff
#define RMG_APOC_WALL_HEIGHT	40.0f	///< bytes it stands over the clearing, 25 world units: a cliff on both faces
#define RMG_APOC_WALL_WOBBLE	3.0f	///< cells its line wanders in and out
#define RMG_APOC_GATE_HALF		7.0f	///< cells from a gate's middle to each of its posts
#define RMG_APOC_WILDERNESS		64.0f	///< cells of open land past the rim's outer foot before the fog: about 70 game seconds of a walker's march
#define RMG_APOC_RING_LEAST		24.0f	///< cells of fog the map keeps at the least past the clear area
#define RMG_APOC_CLEAR_POINTS	96		///< bearings the clear area's outline is drawn on
#define RMG_APOC_CLEAR_SPREAD	2		///< bearings either side whose rim a bearing keeps the wilderness from as well
#define RMG_APOC_CLEAR_EXTRA	110.0f	///< cells the outline runs out past the wilderness at the most, over high ground
#define RMG_APOC_SPAWN_PAST		14.0f	///< cells into the fog a pass's spawn stands
#define RMG_APOC_HILLS			22.0f	///< bytes the belt's hills rise at most
#define RMG_APOC_SWELL_FREQ		0.008f	///< the broad swells' field, cells: a hill or a hollow every sixty cells or so
#define RMG_APOC_HILL_REACH		24.0f	///< cells over which they come up off the wall
#define RMG_APOC_RIVERBED		12.0f	///< bytes the dry riverbed lies under the land round it
#define RMG_APOC_RIVER_WIDTH	0.025f	///< how near nought its noise line runs inside the bed
#define RMG_APOC_FLAT_SPAN		1.0f	///< world units a cell's corners span at most to count as flat ground to build on in the log
#define RMG_APOC_FOG_FRINGE		16.0f	///< cells the woods run on into the fog at the most
#define RMG_APOC_ROCKY_SPAN		7.0f	///< world units a cell's corners span from which a slope shows rock
#define RMG_APOC_BARE_TOPS		150.0f	///< bytes over the basin from which the high ground's tops lie bare
#define RMG_APOC_PILE_OUT		14.0f	///< cells from the wall's outer foot to a gate's pile
#define RMG_APOC_PILE_ASIDE		10.0f	///< and off its trail
#define RMG_APOC_SEATS_MOST		4		///< co-op seats at most (clampSettings): a lobe each
#define RMG_APOC_GATES_MOST		( RMG_APOC_SEATS_MOST + 2 )
#define RMG_APOC_PASS_RAMP		14.0f	///< cells a pass climbs over out of the basin, about 3 bytes a cell at the rim's height
#define RMG_APOC_RIM_SWELL		0.10f	///< the rim's radius swells and draws in by this share on a slow noise field, a lobe fatter or leaner
#define RMG_APOC_RIM_BAYS		0.07f	///< and by this share again on a quicker one: bays and peninsulas along a lobe's side
#define RMG_APOC_RIM_CRAGS		1.5f	///< cells it frets in and out on a quicker field, its turns still a few cells long
#define RMG_APOC_KNOB_FREQ		0.11f	///< the field that throws rock knobs off the rim's inner foot
#define RMG_APOC_KNOB_LEAST		0.45f	///< how high it has to be for a knob
#define RMG_APOC_KNOB_REACH		7.0f	///< cells into the basin a knob reaches at the most
#define RMG_APOC_SCREE			10.0f	///< bytes of scree slope at the foot of the rim at the most, all a byte of it walkable
#define RMG_APOC_SCREE_FOOT		6.0f	///< cells into the basin it runs at the most
#define RMG_APOC_LOBE_TURN		0.16f	///< share of the even spacing a lobe's bearing is turned off it either way
#define RMG_APOC_LOBE_LEAST		0.88f	///< a lobe's reach, a share of wallRing, at the least
#define RMG_APOC_LOBE_MOST		1.15f	///< and at the most
#define RMG_APOC_PROPS_MOST		3200	///< trees and stones on the whole map at most, inside the tree buffer's 4000 with room to spare
#define RMG_APOC_VALLEY			3.0f	///< pass widths either side of a pass's line over which the land outside sinks into its valley
#define RMG_APOC_PASS_BEND		0.22f	///< radians a pass's line bends either way as it goes out

static Real rmgApocalypseStartRing( Int players )
{
	return std::max( RMG_APOC_START_LEAST, RMG_APOC_START_PER_SEAT * (Real)players );
}

/** Passes through the rim, one spawn each: one at every seat's lobe, its front, and one or two more from
	the seed on the seams between two lobes, which two neighbours have to hold together. */
static Int rmgApocalypseGates( Int players, UnsignedInt seed )
{
	return players + 1 + (Int)( hashCell( seed + 61007U, 0, 0 ) % 2U );
}

static Real rmgApocalypseWallThickness( void )
{
	return 2.0f * RMG_APOC_WALL_FACE + RMG_APOC_WALL_TOP;
}

void RMGLayout::buildApocalypse( void )
{
	m_style = NULL;
	m_styleScale = 1.0f;
	m_skeleton = FALSE;
	m_type = &theMapTypes[RMG_MAP_PLAINS];
	m_biome = &theBiomes[rmgBiomeFor( m_settings.m_seed )];
	// the director runs its own night over the map's hour, so the hour is never the map's own night
	m_timeOfDay = rmgTimeOfDayFor( m_settings.m_seed );
	if( m_timeOfDay == TIME_OF_DAY_NIGHT )
		m_timeOfDay = TIME_OF_DAY_AFTERNOON;
	m_snowy = rmgSnowsOn( m_settings.m_seed );
	const UnsignedInt seed = (UnsignedInt)m_settings.m_seed;
	seedPermutation( m_settings.m_seed, m_perm );

	const Int players = m_settings.m_numPlayers, gates = rmgApocalypseGates( players, seed );
	const Real half = 0.5f * (Real)m_settings.m_playableCells;
	m_width = m_settings.m_playableCells + 2 * RMG_BORDER_CELLS;
	m_height = m_width;
	m_waterHeight = 0.0f;		// no water anywhere
	m_playable = TRUE;
	m_starts.clear();
	m_objects.clear();
	m_waypointLinks.clear();
	m_lakes.clear();
	m_areas.clear();
	m_sites.clear();
	m_routeLock.clear();
	const Int cells = m_width * m_height;
	m_inLake.assign( cells, 0 );

	const Real startRing = rmgApocalypseStartRing( players );
	const Real wallRing = startRing + RMG_APOC_WALL_OUT, thick = rmgApocalypseWallThickness();
	// the farthest the rim's inner foot ever runs from the middle
	const Real rimMost = wallRing * RMG_APOC_LOBE_MOST * ( 1.0f + RMG_APOC_RIM_SWELL + RMG_APOC_RIM_BAYS ) + RMG_APOC_WALL_WOBBLE + RMG_APOC_RIM_CRAGS;
	const Real turn = 2.0f * PI * hashUnit( seed + 61001U, 0, 0 );
	/* Co-op: the basin is a heart in the middle with one lobe a seat.  The lobes are not one shape turned
		round: each is turned off its even bearing, and runs longer or shorter, a long lobe narrower and a
		short one wider so each holds about the same ground (a sector's area goes as its reach squared
		times its angle).  The rim runs out to a lobe's reach at its tip and draws in to the heart's edge
		where two lobes meet, at their seam.

		The passes: one at every lobe's tip, that seat's front, and the rest (rmgApocalypseGates) on seams
		the seed picks, which open straight onto the heart between two seats.  Each is a canyon, a valley
		or a wide one and bends as it goes out on a slow noise field, so none is a ruled spoke. */
	Real lobeAngle[RMG_APOC_SEATS_MOST], lobeX[RMG_APOC_SEATS_MOST], lobeY[RMG_APOC_SEATS_MOST];
	Real lobeReach[RMG_APOC_SEATS_MOST], lobeSpan[RMG_APOC_SEATS_MOST];
	const Real spacing = 2.0f * PI / (Real)players;
	for( Int i = 0; i < players; i++ )
	{
		lobeAngle[i] = turn + spacing * ( (Real)i + RMG_APOC_LOBE_TURN * ( 2.0f * hashUnit( seed + 61017U, i, 0 ) - 1.0f ) );
		lobeX[i] = Cos( lobeAngle[i] );
		lobeY[i] = Sin( lobeAngle[i] );
		const Real stretch = RMG_APOC_LOBE_LEAST + ( RMG_APOC_LOBE_MOST - RMG_APOC_LOBE_LEAST ) * hashUnit( seed + 61019U, i, 0 );
		lobeReach[i] = wallRing * stretch;
		lobeSpan[i] = Cos( 0.46f * spacing / ( stretch * stretch ) );		// a lobe's edge, as the cosine off its axis
	}
	// the bearing half way round from lobe i to the next, where two seats' ground meets
	auto seamAngle = [&]( Int i ) -> Real
	{
		const Real next = i + 1 < players ? lobeAngle[i + 1] : lobeAngle[0] + 2.0f * PI;
		return 0.5f * ( lobeAngle[i] + next );
	};
	const Real seamRim = RMG_APOC_HEART + RMG_APOC_SEAM_PAD;
	Real passAngle[RMG_APOC_GATES_MOST], passHalf[RMG_APOC_GATES_MOST];
	static const Real PASS_HALVES[3] = { 4.5f, 7.0f, 11.0f };	// cells: a canyon, a valley, a wide valley
	{
		Bool seamTaken[RMG_APOC_SEATS_MOST] = { FALSE };
		for( Int g = 0; g < gates; g++ )
		{
			if( g < players )
			{
				// a front comes in where the land outside its lobe lies lowest, within a fifth of the
				// lobe's spacing of its axis, so a pass follows a valley and not a ruled bearing
				const Real axis = lobeAngle[g];
				Real best = axis, lowest = 1.0e9f;
				for( Int k = -4; k <= 4; k++ )
				{
					const Real a = axis + 0.2f * spacing * (Real)k / 4.0f;
					const Real probeX = half + ( lobeReach[g] + 20.0f ) * Cos( a ) + RMG_BORDER_CELLS;
					const Real probeY = half + ( lobeReach[g] + 20.0f ) * Sin( a ) + RMG_BORDER_CELLS;
					const Real land = fractalNoise( m_perm, probeX * RMG_APOC_SWELL_FREQ + 31.7f, probeY * RMG_APOC_SWELL_FREQ + 17.3f, 2 );
					if( land < lowest )
					{
						lowest = land;
						best = a;
					}
				}
				passAngle[g] = best;
			}
			else
			{
				Int seam = (Int)( hashCell( seed + 61013U, g, 0 ) % (UnsignedInt)players );
				while( seamTaken[seam] )
					seam = ( seam + 1 ) % players;
				seamTaken[seam] = TRUE;
				passAngle[g] = seamAngle( seam );
			}
			passHalf[g] = PASS_HALVES[ hashCell( seed + 61011U, g, 0 ) % 3U ];
		}
	}
	auto passBearing = [&]( Int g, Real r ) -> Real
	{
		return passAngle[g] + RMG_APOC_PASS_BEND * fractalNoise( m_perm, r * 0.018f + 7.3f * (Real)g + 3.1f, 41.9f + 2.3f * (Real)g, 2 );
	};

	// cells past the rim's inner foot, from the middle at (dx, dy): out to the lobe's tip on a lobe's
	// axis, in to the heart's edge at a seam, swelling a little on a slow field and fretting on a quick one
	auto pastWall = [&]( Real dx, Real dy ) -> Real
	{
		const Real r = sqrtf( dx * dx + dy * dy );
		const Real ux = r > 0.0f ? dx / r : 1.0f, uy = r > 0.0f ? dy / r : 0.0f;
		// never drawn in over a seat's dock and the knobs' reach past it, however lean the noise makes the lobe
		const Real dockClear = startRing + RMG_APOC_DOCK_OUT + RMG_APOC_KNOB_REACH + 12.0f;
		Real reach = seamRim, floor = seamRim;
		for( Int i = 0; i < players; i++ )
		{
			const Real lobe = fadeCurve( rmgUnit( ( ux * lobeX[i] + uy * lobeY[i] - lobeSpan[i] ) / ( 1.0f - lobeSpan[i] ) ) );
			reach = std::max( reach, seamRim + ( lobeReach[i] - seamRim ) * lobe );
			floor = std::max( floor, seamRim + ( dockClear - seamRim ) * lobe );
		}
		const Real rim = std::max( reach
			* ( 1.0f + RMG_APOC_RIM_SWELL * fractalNoise( m_perm, ux * 1.6f + 17.1f, uy * 1.6f + 5.3f, 3 )
				+ RMG_APOC_RIM_BAYS * fractalNoise( m_perm, ux * 4.3f + 71.9f, uy * 4.3f + 33.1f, 2 ) ), floor );
		return r - rim - RMG_APOC_WALL_WOBBLE * fractalNoise( m_perm, ux * 9.1f + 23.9f, uy * 9.1f + 8.7f, 2 )
			- RMG_APOC_RIM_CRAGS * fractalNoise( m_perm, dx * 0.13f + 3.7f, dy * 0.13f + 91.3f, 1 );
	};
	// the pass whose bent line out of the middle (dx, dy) lies nearest, and how far across from it; -1 for none ahead
	auto gateOf = [&]( Real dx, Real dy, Real *across ) -> Int
	{
		const Real r = sqrtf( dx * dx + dy * dy );
		Int best = -1;
		Real least = 1.0e9f;
		for( Int g = 0; g < gates; g++ )
		{
			const Real bearing = passBearing( g, r );
			const Real gx = Cos( bearing ), gy = Sin( bearing );
			if( dx * gx + dy * gy <= 0.0f )
				continue;
			const Real off = fabsf( dx * gy - dy * gx );
			if( off < least )
			{
				least = off;
				best = g;
			}
		}
		*across = least;
		return best;
	};
	// the rim stands everywhere but in a pass, which widens as it goes out like a valley mouth
	auto inWall = [&]( Real d, Int gate, Real across ) -> Bool
	{
		return d > 0.0f && d < thick && !( gate >= 0 && across < passHalf[gate] * ( 0.8f + 0.6f * d / thick ) );
	};
	// one for the dry riverbed's floor at cell (x, y), falling to nought on its banks: the line where a slow field crosses nought
	auto riverbedAt = [&]( Int x, Int y ) -> Real
	{
		const Real line = fractalNoise( m_perm, (Real)x * 0.0045f + 3.3f, (Real)y * 0.0045f + 71.1f, 1 );
		return fadeCurve( 1.0f - rmgUnit( fabsf( line ) / RMG_APOC_RIVER_WIDTH ) );
	};

	// the clearing flat, the wall up out of it, the belt's hills coming up past the wall
	m_heights.assign( cells, (UnsignedByte)RMG_APOC_GROUND );
	std::vector<Real> heights( cells, (Real)RMG_APOC_GROUND );
	std::vector<Real> rimDistance( cells, -1.0e9f );
	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			const Real dx = (Real)( x - RMG_BORDER_CELLS ) - half, dy = (Real)( y - RMG_BORDER_CELLS ) - half;
			const Real d = pastWall( dx, dy );
			rimDistance[cellIndex( x, y )] = d;
			if( d <= -std::max( RMG_APOC_KNOB_REACH, RMG_APOC_SCREE_FOOT ) )
				continue;
			Real across;
			const Int gate = gateOf( dx, dy, &across );
			Real height = (Real)RMG_APOC_GROUND;
			/* The basin is sunk below the land round it: the rim is the high ground's edge, one escarpment
				and no wall standing on flat ground, its height coming and going along it.  A pass is a ramp
				up out of the basin, its sides the same escarpment, so it reads as a canyon or a valley cut
				into the high ground. */
			const Real ridge = 1.1f + 0.6f * ( 0.5f + 0.5f * fractalNoise( m_perm, (Real)x * 0.06f + 51.3f, (Real)y * 0.06f + 29.1f, 2 ) );
			const Real scree = RMG_APOC_SCREE * rmgUnit( 0.5f + fractalNoise( m_perm, (Real)x * 0.11f + 13.7f, (Real)y * 0.11f + 77.1f, 2 ) );
			/* The face is an escarpment of its own width: a crag a cell and a half across in places, a
				broad steep slope six across elsewhere.  It rises RMG_APOC_FACE_STEEP bytes a cell over at
				least RMG_APOC_FACE_SMOOTHED cells, so the smoothing below, which spreads a narrow crag by a
				cell each side, still leaves no stretch of it walkable. */
			const Real face = RMG_APOC_WALL_FACE + ( RMG_APOC_FACE_MOST - RMG_APOC_WALL_FACE )
				* fadeCurve( rmgUnit( 0.5f + 1.2f * fractalNoise( m_perm, (Real)x * 0.045f + 19.9f, (Real)y * 0.045f + 58.3f, 2 ) ) );
			const Real rise = std::max( RMG_APOC_WALL_HEIGHT * ridge, RMG_APOC_FACE_STEEP * std::max( face, RMG_APOC_FACE_SMOOTHED ) + scree );
			const Real ramp = std::max( RMG_APOC_PASS_RAMP, rise / RMG_APOC_PASS_CLIMB );
			const Bool passMouth = gate >= 0 && across < passHalf[gate] * 1.6f;
			if( d <= 0.0f )
			{
				/* At the rim's inner foot: rounded rock knobs thrown off it on a slow field, else a scree
					slope up to the face, so the face stands on broken ground and not on a ruled line; never
					in a pass's mouth, which stays open. */
				if( passMouth )
					continue;
				const Real knob = fractalNoise( m_perm, (Real)x * RMG_APOC_KNOB_FREQ + 9.1f, (Real)y * RMG_APOC_KNOB_FREQ + 47.3f, 2 );
				const Real knobReach = RMG_APOC_KNOB_REACH * rmgUnit( ( knob - RMG_APOC_KNOB_LEAST ) / 0.25f );
				const Real foot = RMG_APOC_SCREE_FOOT * ( 0.5f + 0.5f * rmgUnit( 0.5f + knob ) );
				height += std::max( 0.8f * rise * fadeCurve( rmgUnit( ( d + knobReach ) / 2.0f ) ),
					scree * fadeCurve( rmgUnit( ( d + foot ) / foot ) ) );
				heights[cellIndex( x, y )] = height;
				continue;
			}
			const Bool inPass = gate >= 0 && across < passHalf[gate] * ( 0.8f + 0.6f * rmgUnit( d / RMG_APOC_PASS_RAMP ) );
			if( inPass )
				height += rise * fadeCurve( rmgUnit( d / ramp ) );
			else
			{
				const Real foot = passMouth ? 0.0f : scree;
				height += foot + ( rise - foot ) * rmgUnit( d / face );
			}
			/* Past the rim the high ground rolls on: broad swells on the same field the fronts' passes were
				taken from the low ground of, a few long ridges on a slower one, every pass's line sunk into a
				valley that the trail runs down, and a dry riverbed winding across the wilderness. */
			const Real swell = 0.5f + 0.5f * fractalNoise( m_perm, (Real)x * RMG_APOC_SWELL_FREQ + 31.7f, (Real)y * RMG_APOC_SWELL_FREQ + 17.3f, 2 );
			const Real ridges = 1.0f - fabsf( fractalNoise( m_perm, (Real)x * 0.016f + 66.1f, (Real)y * 0.016f + 12.7f, 1 ) );
			const Real valley = gate >= 0 ? 0.2f + 0.8f * fadeCurve( rmgUnit( across / ( passHalf[gate] * RMG_APOC_VALLEY ) ) ) : 1.0f;
			const Real hill = ( 0.9f * swell + 0.1f * ridges ) * valley;
			const Real past = fadeCurve( rmgUnit( ( d - thick ) / RMG_APOC_HILL_REACH ) );
			height += RMG_APOC_HILLS * hill * past;
			height -= RMG_APOC_RIVERBED * riverbedAt( x, y ) * past;
			heights[cellIndex( x, y )] = height;
		}
	}

	/* Smooth the rim: a pass of a three by three mean over every cell near it, so the escarpment's foot
		and brow and a pass's sides run as curves and not as steps cell by cell. */
	{
		const std::vector<Real> sharp( heights );
		for( Int y = 1; y + 1 < m_height; y++ )
		{
			for( Int x = 1; x + 1 < m_width; x++ )
			{
				const Real d = rimDistance[cellIndex( x, y )];
				if( d < -( RMG_APOC_KNOB_REACH + 3.0f ) || d > thick + RMG_APOC_SMOOTH_PAST )
					continue;
				Real sum = 0.0f;
				for( Int oy = -1; oy <= 1; oy++ )
					for( Int ox = -1; ox <= 1; ox++ )
						sum += sharp[cellIndex( x + ox, y + oy )];
				heights[cellIndex( x, y )] = sum / 9.0f;
			}
		}
	}
	for( Int i = 0; i < cells; i++ )
		m_heights[i] = (UnsignedByte)(Int)( std::max( 0.0f, std::min( 255.0f, heights[i] ) ) + 0.5f );

	Int waypointID = 1;
	auto addWaypoint = [&]( const AsciiString& name, Real cellX, Real cellY )
	{
		RMGObject waypoint;
		waypoint.m_templateName = "*Waypoints/Waypoint";
		waypoint.m_uniqueID = name;
		waypoint.m_worldX = cellX * MAP_XY_FACTOR;
		waypoint.m_worldY = cellY * MAP_XY_FACTOR;
		waypoint.m_angle = 0.0f;
		waypoint.m_waypointID = waypointID++;
		waypoint.m_flags = 0;
		m_objects.push_back( waypoint );
	};

	AsciiString name;
	for( Int i = 0; i < players; i++ )
	{
		// on its own lobe's axis, the dock behind it towards the lobe's tip
		const Real angle = lobeAngle[i];
		const Real ux = lobeX[i], uy = lobeY[i];
		const RMGPoint start = { half + startRing * ux, half + startRing * uy };
		m_starts.push_back( start );
		name.format( "Player_%d_Start", i + 1 );
		addWaypoint( name, start.m_cellX, start.m_cellY );
		name.format( "SupplyDock %d", i + 1 );
		addObject( "SupplyDock", name.str(), half + ( startRing + RMG_APOC_DOCK_OUT ) * ux,
			half + ( startRing + RMG_APOC_DOCK_OUT ) * uy, angle + PI );
	}
	// the shared heart: a dock in the middle for whoever holds it and a derrick at every seam's mouth
	addObject( "SupplyDock", "SupplyDock Heart", half, half, 0.0f );
	for( Int i = 0; i < players; i++ )
	{
		const Real angle = seamAngle( i );
		name.format( "Derrick %d", i + 1 );
		addObject( "TechOilDerrick", name.str(), half + 0.6f * RMG_APOC_HEART * Cos( angle ), half + 0.6f * RMG_APOC_HEART * Sin( angle ), angle );
	}

	/* What the fog leaves clear: no square but a closed outline round the basin, its radius on each of
		RMG_APOC_CLEAR_POINTS bearings.  On every bearing it stands at least RMG_APOC_WILDERNESS past the
		farthest the basin and its rim reach there or on the bearings beside it, so every lobe and pass
		keeps its march; past that it runs out over high ground and draws in over low, so the fog
		reaches in as tongues down the valleys and holds back off the ridges. */
	Real clearRadius[RMG_APOC_CLEAR_POINTS];
	{
		Real least[RMG_APOC_CLEAR_POINTS];
		for( Int k = 0; k < RMG_APOC_CLEAR_POINTS; k++ )
		{
			const Real bearing = 2.0f * PI * (Real)k / (Real)RMG_APOC_CLEAR_POINTS;
			const Real ux = Cos( bearing ), uy = Sin( bearing );
			Real rimOut = 0.0f;
			for( Real r = 0.0f; r < half; r += 2.0f )
				if( pastWall( r * ux, r * uy ) <= thick )
					rimOut = r;
			least[k] = rimOut + RMG_APOC_WILDERNESS;
		}
		for( Int k = 0; k < RMG_APOC_CLEAR_POINTS; k++ )
		{
			Real lo = 0.0f;
			for( Int o = -RMG_APOC_CLEAR_SPREAD; o <= RMG_APOC_CLEAR_SPREAD; o++ )
				lo = std::max( lo, least[( k + o + RMG_APOC_CLEAR_POINTS ) % RMG_APOC_CLEAR_POINTS] );
			const Real bearing = 2.0f * PI * (Real)k / (Real)RMG_APOC_CLEAR_POINTS;
			const Real ux = Cos( bearing ), uy = Sin( bearing );
			const Real px = half + lo * ux + RMG_BORDER_CELLS, py = half + lo * uy + RMG_BORDER_CELLS;
			const Real land = 0.5f + 0.5f * fractalNoise( m_perm, px * RMG_APOC_SWELL_FREQ + 31.7f, py * RMG_APOC_SWELL_FREQ + 17.3f, 3 );
			const Real lobe = 0.5f + 0.5f * fractalNoise( m_perm, ux * 2.2f + 13.3f, uy * 2.2f + 87.9f, 2 );
			const Real out = RMG_APOC_CLEAR_EXTRA * fadeCurve( rmgUnit( 0.6f * land + 0.6f * lobe - 0.1f ) );
			clearRadius[k] = std::min( lo + out, half - RMG_APOC_RING_LEAST );
		}
	}
	// the outline's radius on any bearing, between its two nearest points
	auto clearAt = [&]( Real bearing ) -> Real
	{
		const Real turns = bearing / ( 2.0f * PI );
		const Real at = ( turns - floorf( turns ) ) * (Real)RMG_APOC_CLEAR_POINTS;
		const Int k = (Int)at % RMG_APOC_CLEAR_POINTS;
		const Real t = at - floorf( at );
		return clearRadius[k] + ( clearRadius[( k + 1 ) % RMG_APOC_CLEAR_POINTS] - clearRadius[k] ) * t;
	};
	{
		RMGArea clear;
		clear.m_name = "Apocalypse_Clear";
		for( Int k = 0; k < RMG_APOC_CLEAR_POINTS; k++ )
		{
			const Real bearing = 2.0f * PI * (Real)k / (Real)RMG_APOC_CLEAR_POINTS;
			const RMGPoint point = { half + clearRadius[k] * Cos( bearing ), half + clearRadius[k] * Sin( bearing ) };
			clear.m_polygon.push_back( point );
		}
		m_areas.push_back( clear );
	}
	addWaypoint( AsciiString( "Apocalypse_Centre" ), half, half );
	for( Int g = 0; g < gates; g++ )
	{
		// down the pass's bent line to RMG_APOC_SPAWN_PAST into the fog
		Real spawnX = half, spawnY = half;
		for( Real r = wallRing; r < 2.0f * half; r += 2.0f )
		{
			const Real bearing = passBearing( g, r );
			spawnX = half + r * Cos( bearing );
			spawnY = half + r * Sin( bearing );
			if( r >= clearAt( bearing ) + RMG_APOC_SPAWN_PAST
				|| spawnX < RMG_APOC_SPAWN_PAST || spawnY < RMG_APOC_SPAWN_PAST
				|| spawnX > 2.0f * half - RMG_APOC_SPAWN_PAST || spawnY > 2.0f * half - RMG_APOC_SPAWN_PAST )
				break;
		}
		name.format( "Apocalypse_Spawn_%d", g + 1 );
		addWaypoint( name, spawnX, spawnY );

		const Real out = rimMost + thick + RMG_APOC_PILE_OUT;
		const Real bearing = passBearing( g, out );
		const Real ux = Cos( bearing ), uy = Sin( bearing );
		const Real aside = ( g & 1 ) ? -( RMG_APOC_PILE_ASIDE + passHalf[g] ) : RMG_APOC_PILE_ASIDE + passHalf[g];
		const Real pileX = half + out * ux - aside * uy, pileY = half + out * uy + aside * ux;
		flattenPad( pileX, pileY, RMG_PILE_PAD, RMG_PILE_BLEND );
		reserveSite( pileX, pileY, RMG_PILE_PAD + 2.0f );
		name.format( "SupplyPile %d", g + 1 );
		addObject( "SupplyPileSmall", name.str(), pileX, pileY, 0.0f );
	}

	/* The ground by place, height and slope: the basin's own ground with a worn patch in the heart and
		scree at the rim's foot, the escarpment and every steep slope in rock, rocky patches on the
		steeper hillsides, the riverbed's dry sand, bare dirt on the high tops and down each pass's trail,
		and the wilderness's grass broken by patches of the second class. */
	m_terrain.assign( cells, (UnsignedByte)RMG_TERRAIN_GRASS );
	m_passable.assign( cells, 0 );
	for( Int y = 0; y + 1 < m_height; y++ )
	{
		for( Int x = 0; x + 1 < m_width; x++ )
		{
			const Int i = cellIndex( x, y );
			const Real dx = (Real)( x - RMG_BORDER_CELLS ) + 0.5f - half, dy = (Real)( y - RMG_BORDER_CELLS ) + 0.5f - half;
			const Real d = rimDistance[i];
			Real across;
			const Int gate = gateOf( dx, dy, &across );
			const Real wander = fractalNoise( m_perm, (Real)x * 0.09f + 5.3f, (Real)y * 0.09f + 61.7f, 2 );
			const Real span = cellSpanWorld( x, y );
			const Real rise = heights[i] - (Real)RMG_APOC_GROUND;
			UnsignedByte terrainClass = RMG_TERRAIN_GRASS;
			if( d <= 0.0f )
			{
				if( sqrtf( dx * dx + dy * dy ) < 12.0f + 4.0f * wander )
					terrainClass = RMG_TERRAIN_DIRT;
				else if( rise > 3.0f + 2.0f * wander )
					terrainClass = span > RMG_APOC_ROCKY_SPAN ? RMG_TERRAIN_ROCK : RMG_TERRAIN_DIRT;
			}
			else if( gate >= 0 && across < 2.5f + 1.5f * wander )
				terrainClass = RMG_TERRAIN_DIRT;
			else if( riverbedAt( x, y ) > 0.25f + 0.2f * wander )
				terrainClass = RMG_TERRAIN_SAND;
			else if( span > RMG_APOC_ROCKY_SPAN + 2.0f * wander
				|| ( span > 0.6f * RMG_APOC_ROCKY_SPAN && fractalNoise( m_perm, (Real)x * 0.05f + 23.3f, (Real)y * 0.05f + 9.7f, 2 ) > 0.45f ) )
				terrainClass = RMG_TERRAIN_ROCK;
			else if( rise > RMG_APOC_BARE_TOPS + 10.0f * wander )
				terrainClass = RMG_TERRAIN_DIRT;
			else if( fractalNoise( m_perm, (Real)x * 0.05f + 87.3f, (Real)y * 0.05f + 45.6f, 3 ) > 0.3f )
				terrainClass = RMG_TERRAIN_SAND;
			const Bool cliff = span > RMG_CLIFF_WORLD_SPAN;
			if( cliff || inWall( d, gate, across ) )
				terrainClass = RMG_TERRAIN_ROCK;
			m_terrain[i] = terrainClass;
			m_passable[i] = insidePlayable( x, y ) && !cliff ? 1 : 0;
		}
	}

	// how much flat ground each seat's lobe and the heart give to build on, for the log
	{
		Int heartFlat = 0, seatFlat[RMG_APOC_SEATS_MOST] = { 0 };
		for( Int y = 0; y + 1 < m_height; y++ )
		{
			for( Int x = 0; x + 1 < m_width; x++ )
			{
				const Int i = cellIndex( x, y );
				if( rimDistance[i] > -2.0f || !m_passable[i] || cellSpanWorld( x, y ) > RMG_APOC_FLAT_SPAN )
					continue;
				const Real dx = (Real)( x - RMG_BORDER_CELLS ) - half, dy = (Real)( y - RMG_BORDER_CELLS ) - half;
				if( dx * dx + dy * dy < RMG_APOC_HEART * RMG_APOC_HEART )
				{
					++heartFlat;
					continue;
				}
				Int seat = 0;
				for( Int s = 1; s < players; s++ )
					if( dx * lobeX[s] + dy * lobeY[s] > dx * lobeX[seat] + dy * lobeY[seat] )
						seat = s;
				++seatFlat[seat];
			}
		}
		DEBUG_LOG(( "random map: apocalypse flat ground in cells, heart %d, seats %d %d %d %d\n",
			heartFlat, seatFlat[0], seatFlat[1], seatFlat[2], seatFlat[3] ));
	}

	// trees and stones in the belt only, never on a trail, a cliff or a pile's pad
	const Real woods = ( 0.12f + 0.12f * hashUnit( seed + 61003U, 0, 0 ) ) * m_biome->m_treeShare;
	const Real strays = 0.006f * m_biome->m_treeShare;
	const Real stones = 0.004f + 0.006f * hashUnit( seed + 61003U, 0, 1 );
	Int propID = 1;
	AsciiString uniqueID;
	/* Two walks over the same cells: the first counts the trees and stones the belt would hold, the second
		plants them thinned evenly to RMG_APOC_PROPS_MOST.  Planted row by row past the tree buffer's room, the
		last rows' trees drew as white blobs, a band across the south of the map. */
	enum { PROP_NONE, PROP_TREE, PROP_ROCK };
	// what the woods put on a cell of the belt, the same on both walks
	auto propAt = [&]( Int x, Int y ) -> Int
	{
		if( !m_passable[cellIndex( x, y )] )
			return PROP_NONE;
		const Real dx = (Real)( x - RMG_BORDER_CELLS ) + 0.5f - half, dy = (Real)( y - RMG_BORDER_CELLS ) + 0.5f - half;
		// the woods run on a little way into the fog, ending on a ragged line and not on its square edge
		const Real fringe = RMG_APOC_FOG_FRINGE * ( 0.5f + 0.5f * fractalNoise( m_perm, (Real)x * 0.04f + 77.7f, (Real)y * 0.04f + 1.9f, 2 ) );
		const Real reach = clearAt( ATan2( dy, dx ) ) + fringe;
		if( dx * dx + dy * dy > reach * reach )
			return PROP_NONE;
		const Real outside = rimDistance[cellIndex( x, y )] - thick;
		if( outside < 3.0f )
			return PROP_NONE;
		Real across;
		const Int pass = gateOf( dx, dy, &across );
		if( pass >= 0 && across < passHalf[pass] + 3.0f )
			return PROP_NONE;
		const Real roll = (Real)( hashCell( seed + 61005U, x, y ) % 1024U ) / 1024.0f;
		/* Woods stand in clumps: thick under the rim's outer face in uneven bands, on the banks of the
			dry riverbed, along each pass valley's sides and wherever a slow field gathers them, with a
			stray tree or two between.  Stones lie in heaps under the rim and on the rocky ground. */
		const Real clump = fractalNoise( m_perm, (Real)x * 0.05f + 7.1f, (Real)y * 0.05f + 3.9f, 3 );
		const Real underRim = 1.0f - rmgUnit( ( outside - 3.0f ) / 22.0f );
		const Real band = fractalNoise( m_perm, (Real)x * 0.09f + 61.3f, (Real)y * 0.09f + 14.9f, 2 );
		const Real bed = riverbedAt( x, y );
		const Bool bank = bed > 0.0f && bed < 0.25f;
		const Bool valleySide = pass >= 0 && across < passHalf[pass] * RMG_APOC_VALLEY * 1.3f && band > 0.0f;
		const Bool wooded = clump > 0.25f || ( underRim > 0.0f && band > -0.15f ) || bank || valleySide;
		const Real treeChance = ( wooded ? woods : strays ) * ( 1.0f + 2.0f * underRim + ( clump > 0.4f ? 1.0f : 0.0f ) );
		if( bed < 0.25f && roll < treeChance )
			return PROP_TREE;
		const Bool rocky = m_terrain[cellIndex( x, y )] == RMG_TERRAIN_ROCK;
		return roll > 1.0f - stones * ( 1.0f + 4.0f * underRim + ( rocky ? 6.0f : 0.0f ) ) ? PROP_ROCK : PROP_NONE;
	};
	const Int lo = RMG_BORDER_CELLS, hi = RMG_BORDER_CELLS + m_settings.m_playableCells;
	Int propsWanted = 0;
	for( Int y = lo; y < hi; y++ )
		for( Int x = lo; x < hi; x++ )
			propsWanted += propAt( x, y ) != PROP_NONE ? 1 : 0;
	const UnsignedInt keep = propsWanted <= RMG_APOC_PROPS_MOST ? 4096U
		: (UnsignedInt)( 4096.0f * (Real)RMG_APOC_PROPS_MOST / (Real)propsWanted );
	for( Int y = lo; y < hi; y++ )
	{
		for( Int x = lo; x < hi; x++ )
		{
			const Int prop = propAt( x, y );
			if( prop == PROP_NONE || hashCell( seed + 61015U, x, y ) % 4096U >= keep )
				continue;
			const UnsignedInt hash = hashCell( seed + 61005U, x, y );
			const Real cellX = (Real)( x - RMG_BORDER_CELLS ) + 0.5f, cellY = (Real)( y - RMG_BORDER_CELLS ) + 0.5f;
			if( !siteIsClear( cellX, cellY, 0.5f ) )
				continue;
			const char *templateName = prop == PROP_TREE ? m_biome->m_trees[( hash >> 10 ) % (UnsignedInt)RMG_BIOME_TREES]
				: m_biome->m_rocks[( hash >> 10 ) % (UnsignedInt)RMG_BIOME_ROCKS];
			// no tree in blossom at the end of the world: the dogwood's white flowers read as snow from above
			if( strcmp( templateName, "TreeDogwood1" ) == 0 )
				templateName = m_biome->m_trees[0];
			uniqueID.format( "Prop %d", propID++ );
			addObject( templateName, uniqueID.str(), cellX, cellY, (Real)( ( hash >> 14 ) % 8U ) * PI * 0.25f );
		}
	}

	buildBlends();
}

//-----------------------------------------------------------------------------
// The Art of Defense map
//-----------------------------------------------------------------------------

/* Art of Defense plays on this map (AodDirector.h). A lane is a trench cut into a plateau; its walls are
	cliffs, so no wave climbs out of it and no seat's vehicle gets down into it: the waves walk the whole lane
	and the seats shoot into it from the high ground beside it. Every unit that reaches the lane's end
	vanishes there and costs the seats a life.

	One seat: one lane in at the west edge and out at the east edge, winding north and south through the
	middle band of the map, long runs joined by narrow runs east; the seat builds on the strip south of it.
	Two to six: one lane a seat, the map turned on itself so every seat has the same lane and the same
	ground (the owner, 2026-11-28: "kaç oyuncu varsa harita o şekilde mirror olmalı"). A seat's sector is a
	slice of a round plateau. Its lane comes in from the mountain round the plateau's rim and winds in
	towards the middle in arcs at falling radii joined by narrow radial steps, then runs into the pit in the
	middle where every lane ends. The seat builds on the rim of its sector and between the lane's arcs.

	AOD_Entry_1 to _N       waypoints, where each lane's waves come in
	AOD_Path_N_1 to _K      waypoints down lane N, AOD_Entry_N linked to the first, the last in the exit
	AOD_Exit                trigger area, the strip along the east edge (one seat) or the pit in the middle:
	                        a wave unit in it has got out */
#define RMG_AOD_FLOOR			24.0f	///< height bytes of a lane's floor
#define RMG_AOD_HIGH			62.0f	///< and of the plateau: 38 bytes, 24 world units, over the floor
/* Cells a trench wall rises over: 30 bytes a cell, 19 world units, a cliff twice the pathfinder's 9.8. At 1.6
	cells, 24 bytes, a wall turned to some angles of a five- or six-way mirror left a cell or two walkable. */
#define RMG_AOD_FACE			1.25f
#define RMG_AOD_BRIDGE			"Concrete"	///< the bridge every lane gets (Roads.ini), stretched end to end
#define RMG_AOD_BRIDGE_HALF		10.0f	///< cells from a lane's middle to each end of a bridge over it
#define RMG_AOD_POCKET			60		///< cells of walkable plateau a region needs before it must have a bridge to it
#define RMG_AOD_APEX			24.0f	///< cells of flat at every turn of a family whose legs would meet in a point
#define RMG_AOD_SLIVER			2.0f	///< cells: plateau less than twice this wide between two trenches goes down to the floor
#define RMG_AOD_RUN_HALF		5.5f	///< cells from a lane's middle to its wall along a run north or south
#define RMG_AOD_STEP_HALF		3.5f	///< and along a run east between two of them: the narrow places
#define RMG_AOD_BAND_MARGIN		14.0f	///< cells a run turns short of its band's edge
#define RMG_AOD_ENDS			40.0f	///< cells from each edge to the first and the last run
#define RMG_AOD_EXIT			14.0f	///< cells of the east edge the exit area takes
#define RMG_AOD_ENTRY_IN		6.0f	///< cells in from the west edge an entry stands
#define RMG_AOD_WAYPOINT_EVERY	22.0f	///< cells between two waypoints of a lane, at the most
#define RMG_AOD_WOBBLE			3.5f	///< cells a run wanders east and west
#define RMG_AOD_START_FLAT		26.0f	///< cells of level ground round a start
#define RMG_AOD_SEATS_MOST		6
#define RMG_AOD_PROPS_MOST		1500	///< trees and stones on the whole map at most
#define RMG_AOD_ARCS			4		///< arcs a ring lane winds through
#define RMG_AOD_ROW				34.0f	///< cells between two arcs, middle to middle
/* Cells a ring lane runs, arcs as long as its sector lets them be up to this: a Technical, wave 1, walks it
	in about 70 game seconds (95 world units a game second, measured on the first build), a Rebel in five
	minutes. */
#define RMG_AOD_LANE_LENGTH		650.0f
#define RMG_AOD_RIM_BASE		70.0f	///< cells of plateau between the outermost arc and the rim, where the starts stand
#define RMG_AOD_MOUNTAIN		48.0f	///< height bytes the mountain past the rim stands over the plateau
#define RMG_AOD_PIT				12.0f	///< cells of exit round the middle of a ring map

/** A lane map's plan in playable cells, y north. One seat: a band of m_band cells across the middle between
	two strips of m_strip. More: a round plateau of radius m_rim, a lane a seat whose arcs sweep m_span radians
	from m_inner out by RMG_AOD_ROW, and a mountain round it to the square's edge. */
struct RMGAodShape
{
	Int m_lanes;			///< one a seat
	Int m_runs;				///< runs north and south the one-seat lane winds through, three or four
	Real m_strip;			///< cells of plateau each strip is deep (one seat)
	Real m_band;			///< and the lane's band
	Real m_inner;			///< the innermost arc's radius (ring)
	Real m_span;			///< every arc's sweep, radians
	Real m_rim;				///< and the plateau's edge
	Int m_cells;			///< the playable square's side
};

/* The lane's shape, one of RMG_AOD_FAMILIES the seed picks (the owner, 2026-11-28: "çeşitli yol varyasyonları
	üret"). A family draws the lane in its own frame, u from 0 at the entry to 1 at the end of the winding and v
	across the lane's room from -1 to 1, with a repeat count k and a reach s across; buildAod lays that frame over
	the band (one seat) or the sector (a ring), tries each k and s, and keeps the one whose lane comes nearest
	RMG_AOD_LANE_LENGTH with every stretch of trench clear of every other. A family no k or s fits gives way to
	the next. side holds trench off the waves' way: a fork's other branch, a comb's bays.
	The owner, 2026-11-28 later: "yollar oldukları yerde daire de çizebilirler", so "circles" winds a switchback
	whose every run turns a near full circle where it is, round an island of plateau a bridge reaches. */
enum { RMG_AOD_FAMILIES = 12, RMG_AOD_MIXED = 11 };
/* "mixed" is a lane of two or three stretches of other families in a row (the owner, 2026-11-28: "karışımları da
	olabilir hem kare hem daire vs"); a third of the seeds draw it (buildAod), the rest one family alone. */
static const char *const theAodFamilies[RMG_AOD_FAMILIES] =
	{ "switchback", "zigzag", "wide S", "wiggle", "hairpins", "terraces", "comb", "fork", "sawtooth", "loops", "circles", "mixed" };
/// cells of lane walked between two bridges over it, each family's: fewer where the lane winds tight
static const Real theAodBridgeEvery[RMG_AOD_FAMILIES] = { 150.0f, 150.0f, 170.0f, 170.0f, 190.0f, 150.0f, 150.0f, 130.0f, 150.0f, 190.0f, 110.0f, 140.0f };
static Int theAodFamilyForTests = -1;		///< RandomMapGenerator::setAodFamilyForTests; -1, the seed's

struct RMGAodPlan
{
	std::vector<RMGPoint> path;							///< m_cellX is u, m_cellY is v
	std::vector< std::vector<RMGPoint> > side;
};

/** the repeat counts a family tries; its reach across is bisected to the length. Circles stops at 4: past it a ring's
	runs stand too close for a loop of 14 cells (0.3 of the step), and the lane would be a plain switchback. */
static void rmgAodRepeats( Int family, std::vector<Int>& repeats )
{
	static const Int theRepeats[RMG_AOD_FAMILIES][8] =
	{
		{ 3, 4, 5, 6, 7, 8, 0, 0 }, { 3, 4, 5, 6, 7, 9, 11, 14 }, { 2, 3, 4, 0, 0, 0, 0, 0 }, { 5, 6, 7, 8, 10, 12, 14, 0 },
		{ 3, 5, 7, 0, 0, 0, 0, 0 }, { 1, 2, 3, 4, 0, 0, 0, 0 }, { 3, 4, 5, 6, 7, 8, 0, 0 }, { 2, 3, 4, 5, 6, 0, 0, 0 },
		{ 2, 3, 4, 5, 6, 7, 0, 0 }, { 3, 5, 7, 0, 0, 0, 0, 0 }, { 4, 3, 2, 0, 0, 0, 0, 0 }, { 3, 2, 0, 0, 0, 0, 0, 0 },
	};
	repeats.clear();
	for( Int i = 0; i < 8 && theRepeats[family][i] > 0; i++ )
		repeats.push_back( theRepeats[family][i] );
}

/* uCells and vCells: the cells one unit of u and of v spans (v at the room's middle on a ring). Two straight legs
	meeting at a sharp point leave a wedge of plateau between them too thin to build on, its tip a sliver that
	buildAod cuts away; so a family whose legs meet at a point (zigzag, sawtooth, the fork's branches) runs a flat
	stretch RMG_AOD_APEX cells long at every turn instead. */
/* variety: the seed's own draws (the owner, 2026-11-28: "haritalar her zaman rasgele değil mi? köşeli kısımlar da
	yapabilirsin 45 açılar da yapabilirsin"). A family is a preset; within it the seed moves every run along the room,
	gives each its own reach across, and turns each corner square, cut at 45 degrees or round, so two seeds of one
	family wind apart. The draws are hashed from the seed and the run's number, never from k or s, so the reach the
	caller bisects for still grows the lane smoothly. */
static void rmgAodPlan( Int family, Int k, Real s, Real uCells, Real vCells, UnsignedInt variety, RMGAodPlan& plan )
{
	plan.path.clear();
	plan.side.clear();
	std::vector<RMGPoint>& p = plan.path;
	auto add = [&]( std::vector<RMGPoint>& line, Real u, Real v ) { const RMGPoint q = { u, v }; line.push_back( q ); };
	// hashed twice: once, neighbouring seeds drew alike for the first runs
	auto rnd = [&]( Int i, Int salt ) -> Real { return (Real)( hashCell( hashCell( variety, i, salt ), salt, i ) % 4096U ) / 4096.0f; };
	const Real a = 0.06f, b = 0.94f;
	const Real flat = RMG_AOD_APEX / uCells;
	const Real half = 0.5f * std::min( flat, 0.5f * ( b - a ) / (Real)std::max( 1, k ) );		///< half an apex flat, in u
	// a run's place along the room, moved up to a fifth of a step either way (not the first or the last), and its reach
	auto runU = [&]( Int i, Int count ) -> Real
	{
		const Real step = ( b - a ) / (Real)std::max( 1, count - 1 );
		const Real jitter = i > 0 && i + 1 < count ? ( rnd( i, 1 ) - 0.5f ) * 0.4f * step : 0.0f;
		return a + step * (Real)i + jitter;
	};
	// the waves draw less (a six-seat ring's wide S has no length to spare)
	const Real least = family == 2 ? 0.85f : family == 3 ? 0.6f : 0.65f;
	auto reachOf = [&]( Int i ) -> Real { return s * ( least + ( 1.0f - least ) * rnd( i, 2 ) ); };
	switch( family )
	{
		default:
		case 0:		// switchback: runs across the room at even steps along it, the steps the narrow places
		case 6:		// comb: the same, with a dead-end bay off the middle of every run
		{
			Real v = -reachOf( 0 );
			add( p, 0.0f, v );
			for( Int i = 0; i < k; i++ )
			{
				const Real u = runU( i, k );
				add( p, u, v );
				v = v < 0.0f ? reachOf( i + 1 ) : -reachOf( i + 1 );
				add( p, u, v );
				if( family == 6 )
				{
					// a bay a third of the way to the next run, off whichever side has room
					const Real gap = ( b - a ) / (Real)( k - 1 ), out = ( i + 1 < k ? 0.33f : -0.33f ) * gap;
					std::vector<RMGPoint> bay;
					add( bay, u, 0.0f );
					add( bay, u + out, 0.0f );
					plan.side.push_back( bay );
				}
			}
			add( p, 1.0f, v );
			break;
		}
		case 1:		// zigzag: straight legs from one side of the room to the other, a short flat at every turn
		{
			Real v = -reachOf( 0 );
			add( p, 0.0f, v );
			add( p, a + half, v );
			for( Int i = 1; i <= k; i++ )
			{
				v = v < 0.0f ? reachOf( i ) : -reachOf( i );
				const Real u = runU( i, k + 1 );
				add( p, u - half, v );
				if( i < k )
					add( p, u + half, v );
			}
			add( p, 1.0f, v );
			break;
		}
		case 2:		// wide S, and 3, wiggle: a smooth wave across the room, k half waves
		case 3:
		{
			/* The wiggle's crests broadened (the square root of the wave): its half waves are narrow, and a plain
				wave's turns there are as sharp as a zigzag's. */
			/* The seed shifts the wave's phase up to a third of a half wave and warps its spacing (t + w sin(2 pi m t) /
				2 pi m, |w| under 0.6, so it still runs one way), so the crests stand unevenly and in other places. */
			const Int points = 24 * k;
			// the wide S warps less: on a six-seat ring it has no length to spare
			const Real phase = ( rnd( 0, 10 ) - 0.5f ) * 0.66f * PI, warp = ( rnd( 0, 11 ) - 0.5f ) * ( family == 2 ? 0.6f : 1.2f );
			const Real warpWaves = rnd( 0, 12 ) < 0.5f ? 1.0f : 2.0f;
			for( Int i = 0; i <= points; i++ )
			{
				const Real t0 = (Real)i / (Real)points;
				const Real t = t0 + warp * Sin( 2.0f * PI * warpWaves * t0 ) / ( 2.0f * PI * warpWaves );
				Real c = Cos( PI * (Real)k * t + phase );
				if( family == 3 )
					c = c < 0.0f ? -sqrtf( -c ) : sqrtf( c );
				// each crest its own reach, eased from one crest to the next across the crossing between them
				const Real at = (Real)k * t;
				const Int wave = (Int)floorf( at );
				const Real f = at - (Real)wave, ease = f * f * ( 3.0f - 2.0f * f );
				const Real reach = reachOf( wave ) + ( reachOf( wave + 1 ) - reachOf( wave ) ) * ease;
				if( i == 0 )
					add( p, 0.0f, -reach * c );
				add( p, a + ( b - a ) * t0, -reach * c );
			}
			add( p, 1.0f, p.back().m_cellY );
			break;
		}
		case 4:		// hairpins: k long runs up and down the room's length, side by side, turned square
		case 9:		// loops: the same turned round
		{
			// here the reach is how far along the room the runs go; they stand side by side right across it
			const Real lo0 = 0.5f - ( family == 9 ? 0.4f : 0.44f ) * s, hi0 = 0.5f + ( family == 9 ? 0.4f : 0.44f ) * s;
			// the outermost runs a tenth in from the room's sides, clear of the starts on a six-seat ring
			const Real edge0 = 0.9f;
			// each turn's place along the room drawn: up to an eighth of the runs' length in from the end
			auto turnAt = [&]( Int j, Bool atHi ) -> Real { const Real in = 0.12f * rnd( j, 3 ) * ( hi0 - lo0 ); return atHi ? hi0 - in : lo0 + in; };
			/* The seed spaces the runs unevenly (an inner run moved up to 0.15 of the spacing either way) and picks each
				turn's kind: a half circle, bulging 0.03 to 0.07 of the room, on three of four of the loops' turns and two
				of five of the hairpins', square on the rest. */
			auto runV = [&]( Int j ) -> Real
			{
				const Real spacing = 2.0f * edge0 / (Real)( k - 1 );
				return -edge0 + spacing * (Real)j + ( j > 0 && j + 1 < k ? ( rnd( j, 13 ) - 0.5f ) * 0.3f * spacing : 0.0f );
			};
			Real from = lo0;
			add( p, 0.0f, -edge0 );
			for( Int j = 0; j < k; j++ )
			{
				const Real v = runV( j );
				const Bool down = ( j & 1 ) == 0;
				const Real to = j + 1 < k ? turnAt( j, down ) : ( down ? hi0 : lo0 );
				add( p, from, v );
				add( p, to, v );
				if( j + 1 < k )
				{
					const Real next = runV( j + 1 );
					if( rnd( j, 14 ) < ( family == 9 ? 0.75f : 0.4f ) )
					{
						// a half circle round to the next run
						const Real mid = 0.5f * ( v + next ), rv = 0.5f * ( next - v ), ru = 0.03f + 0.04f * rnd( j, 15 );
						for( Int q = 1; q < 12; q++ )
						{
							const Real th = PI * (Real)q / 12.0f;
							add( p, to + ( down ? ru : -ru ) * Sin( th ), mid - rv * Cos( th ) );
						}
					}
					add( p, to, next );
					from = to;
				}
			}
			add( p, 1.0f, p.back().m_cellY );
			break;
		}
		case 5:		// terraces: a staircase across the room and back, k times, every step a short run and a turn
		{
			/* Four steps up, a flat across the top, four down, a flat across the bottom: the top and bottom treads a
				whole apex flat long, so the last riser up and the first down stand that far apart. */
			Real u = a, v = -s;
			add( p, 0.0f, v );
			add( p, u, v );
			const Real apex = 2.0f * half;
			const Real du = std::max( 0.0f, ( b - a ) - (Real)( 2 * k - 1 ) * apex ) / (Real)( 6 * k + 1 ), dv = 0.5f * s;
			// each flight's four risers of drawn heights that still add up to the whole climb
			auto riser = [&]( Int i ) -> Real
			{
				Real sum = 0.0f;
				for( Int m = 0; m < 4; m++ )
					sum += 0.6f + 0.8f * rnd( ( i / 4 ) * 4 + m, 4 );
				return 4.0f * dv * ( 0.6f + 0.8f * rnd( i, 4 ) ) / sum;
			};
			for( Int i = 0; i < 8 * k; i++ )
			{
				const Bool top = ( i % 8 ) == 4, bottom = i > 0 && ( i % 8 ) == 0;
				u += top || bottom ? apex : du;
				add( p, u, v );
				v += ( ( i / 4 ) & 1 ) ? -riser( i ) : riser( i );
				add( p, u, v );
			}
			add( p, 1.0f, v );
			break;
		}
		case 7:		// fork: two branches round an island, each in battlements, the waves sent down one
		{
			/* Square battlements and not a zigzag: a branch has half the room to wind in, and a zigzag that dense
				turns in points (the first build gave way on every ring). */
			const Real split = 0.1f, join = 0.9f, span = join - split - 0.06f, step = 0.5f * span / (Real)k;
			const Real low = 0.12f, high = 0.25f + 0.75f * s;
			add( p, 0.0f, 0.0f );
			add( p, split, 0.0f );
			std::vector<RMGPoint> other;
			add( other, split, 0.0f );
			for( Int i = 0; i < k; i++ )
			{
				const Real u = split + 0.03f + 2.0f * step * (Real)i;
				const Real tall = low + ( high - low ) * ( 0.7f + 0.3f * rnd( i, 5 ) );		// each merlon its own height
				const Real us[4] = { u, u + step, u + step, u + 2.0f * step }, vs[4] = { tall, tall, low, low };
				for( Int c = 0; c < 4; c++ )
				{
					add( p, us[c], vs[c] );
					add( other, us[c], -vs[c] );
				}
			}
			add( p, join, 0.0f );
			add( other, join, 0.0f );
			add( p, 1.0f, 0.0f );
			plan.side.push_back( other );
			break;
		}
		case 8:		// sawtooth: a long slant across, a short run straight back, again
		{
			// a flat before the run back and one after it, so neither of the slant's ends meets it in a point
			add( p, 0.0f, -s );
			add( p, a, -s );
			for( Int i = 1; i <= k; i++ )
			{
				const Real u = runU( i, k + 1 ), tooth = reachOf( i );
				add( p, u - 2.0f * half, tooth );
				add( p, u, tooth );
				add( p, u, -s );
				if( i < k )
					add( p, u + 2.0f * half, -s );
			}
			add( p, 1.0f, -s );
			break;
		}
		case 10:	// circles: a switchback whose every run turns a near full circle at its middle
		{
			/* A loop of 280 degrees, the 80 left open at the run, so the trench coming in and the trench going out
				stand 1.29 radii apart there: from 14 cells of radius that is past the 17.5 the lanes keep. Every loop
				but the last run's bulges towards the next run (one bulging back met the loop before it), at most 0.3
				of the step between runs and 20 cells short of the run's end. Drawn in cells and turned into u and v. */
			/* And every step from one run to the next turns a loop at its middle out towards the room's side, as
				large as the room left past the run's end lets it (1.77 radii of bulge, 8 cells spare): on the
				one-seat band the runs are short and those are the big loops. */
			const Real step = ( b - a ) / (Real)std::max( 1, k - 1 );
			const Real radius = std::min( 26.0f, std::min( 0.3f * step * uCells, s * vCells - 20.0f ) );
			const Real outRadius = std::min( 26.0f, std::min( 0.3f * step * uCells, ( ( 1.0f - s ) * vCells - 8.0f ) / 1.77f ) );
			const Real gap = 40.0f * PI / 180.0f;
			/* Runs stay even here (a moved run left too little room for its loop). The seed draws which runs and steps
				get a loop (three of four, at least one), where it sits along its run or step, and its size. */
			Bool runLoop[16], stepLoop[16], any = FALSE;
			for( Int i = 0; i < 16; i++ )
			{
				runLoop[i] = i + 1 < k && radius >= 14.0f && rnd( i, 16 ) < 0.75f;
				stepLoop[i] = i > 0 && i < k && outRadius >= 14.0f && rnd( i, 17 ) < 0.75f;
				any = any || runLoop[i] || stepLoop[i];
			}
			if( !any )
			{
				if( outRadius >= 14.0f && k > 1 )
					stepLoop[1] = TRUE;
				else if( radius >= 14.0f && k > 1 )
					runLoop[0] = TRUE;
			}
			Real v = -s;
			add( p, 0.0f, v );
			for( Int i = 0; i < k; i++ )
			{
				const Real u = a + step * (Real)i;
				const Real outR = outRadius >= 14.0f ? std::max( 14.0f, outRadius * ( 0.75f + 0.25f * rnd( i, 6 ) ) ) : outRadius;
				const Real runR = radius >= 14.0f ? std::max( 14.0f, radius * ( 0.75f + 0.25f * rnd( i, 7 ) ) ) : radius;
				if( i < 16 && stepLoop[i] )
				{
					// the step runs along u at v; the loop's side is outwards, the sign of v; anywhere from 0.35 to 0.65 of it
					const Real middle = u - ( 0.65f - 0.3f * rnd( i, 19 ) ) * step, out = v < 0.0f ? -1.0f : 1.0f;
					for( Int q = 0; q <= 28; q++ )
					{
						const Real t = -0.5f * PI - gap - ( 2.0f * PI - 2.0f * gap ) * (Real)q / 28.0f;
						add( p, middle + outR * Cos( t ) / uCells, v + out * ( outR * Cos( gap ) + outR * Sin( t ) ) / vCells );
					}
				}
				add( p, u, v );
				const Real dir = v < 0.0f ? 1.0f : -1.0f, side = 1.0f;
				if( i < 16 && runLoop[i] )
				{
					// the run's direction d is along v (dir), the loop's side n along u (side); the centre stands off the run,
					// moved along it as far as leaves 6 cells to either end
					const Real room = std::max( 0.0f, s * vCells - 6.0f - runR ) / vCells;
					const Real along = ( rnd( i, 18 ) - 0.5f ) * 1.6f * room;
					const Real centreU = u + side * runR * Cos( gap ) / uCells;
					for( Int q = 0; q <= 28; q++ )
					{
						const Real t = -0.5f * PI - gap - ( 2.0f * PI - 2.0f * gap ) * (Real)q / 28.0f;
						add( p, centreU + side * runR * Sin( t ) / uCells, along + dir * runR * Cos( t ) / vCells );
					}
				}
				v = -v;
				add( p, u, v );
			}
			add( p, 1.0f, v );
			break;
		}
		case RMG_AOD_MIXED:	// mixed: k stretches (two or three) of other families one after the other along the room
		{
			/* Each stretch a family the seed picks (never the one before it), drawn with its own draws in its share of
				the room and its own cells, so its turns and loops keep their size. A run straight across the room joins
				two stretches, 20 cells clear of each. The fork, the comb, the hairpins and the loops stay out: a
				fork's branch or a comb's bay at a joint, and runs the room's whole width, would meet the next stretch. */
			static const Int theMix[7] = { 0, 1, 2, 3, 5, 8, 10 };
			static const Int theMixK[7] = { 3, 3, 2, 4, 1, 3, 2 };
			const Int parts = std::max( 2, std::min( 3, k ) );
			const Real margin = 20.0f / uCells;
			Int last = -1;
			RMGAodPlan sub;
			for( Int j = 0; j < parts; j++ )
			{
				Int pick = (Int)( hashCell( variety, j, 40 ) % 7U );
				if( theMix[pick] == last )
					pick = ( pick + 1 ) % 7;
				last = theMix[pick];
				const Real u0 = (Real)j / (Real)parts, u1 = (Real)( j + 1 ) / (Real)parts;
				const Real lo = u0 + ( j > 0 ? margin : 0.0f ), hi = u1 - ( j + 1 < parts ? margin : 0.0f );
				rmgAodPlan( theMix[pick], theMixK[pick], s, uCells * ( hi - lo ), vCells, hashCell( variety, j, 41 ), sub );
				if( sub.path.empty() )
					continue;
				if( !p.empty() )
				{
					add( p, u0, p.back().m_cellY );
					add( p, u0, sub.path[0].m_cellY );
				}
				for( UnsignedInt q = 0; q < sub.path.size(); q++ )
					add( p, lo + sub.path[q].m_cellX * ( hi - lo ), sub.path[q].m_cellY );
			}
			break;
		}
	}
	/* Every corner of a turn sharper than 60 degrees: kept square, cut at 45 degrees (a 90-degree turn becomes two of 45
		with a diagonal between), or rounded, by the seed, cut 4 to 10 cells along each leg and never more than a third
		of either. The smooth waves and the loops' half circles have no such corners, and the hairpins keep theirs
		square: cut, their runs side by side on a four- or six-seat ring came too near at every turn. Worked in cells;
		a draw keyed on the corner's place along the room, so a fork's two branches turn alike. */
	if( family != 2 && family != 3 && family != 4 && family != 9 && family != RMG_AOD_MIXED )
	{
		auto cornerize = [&]( std::vector<RMGPoint>& line )
		{
			if( line.size() < 3 )
				return;
			std::vector<RMGPoint> out;
			out.push_back( line[0] );
			for( UnsignedInt i = 1; i + 1 < line.size(); i++ )
			{
				const Real ax = line[i - 1].m_cellX * uCells, ay = line[i - 1].m_cellY * vCells;
				const Real px = line[i].m_cellX * uCells, py = line[i].m_cellY * vCells;
				const Real bx = line[i + 1].m_cellX * uCells, by = line[i + 1].m_cellY * vCells;
				const Real la = sqrtf( ( px - ax ) * ( px - ax ) + ( py - ay ) * ( py - ay ) ), lb = sqrtf( ( bx - px ) * ( bx - px ) + ( by - py ) * ( by - py ) );
				if( la < 0.01f || lb < 0.01f )
					continue;
				const Real dax = ( px - ax ) / la, day = ( py - ay ) / la, dbx = ( bx - px ) / lb, dby = ( by - py ) / lb;
				const Int key = (Int)floorf( line[i].m_cellX * 997.0f + 0.5f );
				const Real roll = rnd( key, 8 );
				const Real cut = std::min( 4.0f + 6.0f * rnd( key, 9 ), 0.33f * std::min( la, lb ) );
				if( dax * dbx + day * dby > 0.5f || roll < 0.35f || cut < 3.0f )
				{
					out.push_back( line[i] );
					continue;
				}
				const Real qax = px - dax * cut, qay = py - day * cut, qbx = px + dbx * cut, qby = py + dby * cut;
				const RMGPoint first = { qax / uCells, qay / vCells }, last = { qbx / uCells, qby / vCells };
				out.push_back( first );
				if( roll >= 0.7f )
				{
					// round: a quadratic curve through the corner's cut points
					for( Int q = 1; q < 6; q++ )
					{
						const Real t = (Real)q / 6.0f, w0 = ( 1.0f - t ) * ( 1.0f - t ), w1 = 2.0f * t * ( 1.0f - t ), w2 = t * t;
						const RMGPoint c = { ( w0 * qax + w1 * px + w2 * qbx ) / uCells, ( w0 * qay + w1 * py + w2 * qby ) / vCells };
						out.push_back( c );
					}
				}
				out.push_back( last );
			}
			out.push_back( line.back() );
			line.swap( out );
		};
		cornerize( p );
		for( UnsignedInt n = 0; n < plan.side.size(); n++ )
			cornerize( plan.side[n] );
	}
	// and on half the seeds the whole drawing turned over across the room, so it winds the other way first
	if( rnd( 0, 21 ) < 0.5f )
	{
		for( UnsignedInt i = 0; i < p.size(); i++ )
			p[i].m_cellY = -p[i].m_cellY;
		for( UnsignedInt n = 0; n < plan.side.size(); n++ )
			for( UnsignedInt i = 0; i < plan.side[n].size(); i++ )
				plan.side[n][i].m_cellY = -plan.side[n][i].m_cellY;
	}
}

static RMGAodShape rmgAodShape( Int seed, Int players )
{
	RMGAodShape shape;
	shape.m_lanes = std::max( 1, std::min( players, (Int)RMG_AOD_SEATS_MOST ) );
	shape.m_runs = 3 + (Int)( hashCell( (UnsignedInt)seed + 71003U, 0, 0 ) % 2U );
	shape.m_strip = 72.0f;
	shape.m_inner = shape.m_span = shape.m_rim = 0.0f;
	if( shape.m_lanes == 1 )
	{
		// a band 196 cells deep on 340, room for the lane families to wind as far as on a ring map
		shape.m_runs = 4;
		shape.m_cells = 340;
		shape.m_band = (Real)shape.m_cells - 2.0f * shape.m_strip;
		return shape;
	}
	shape.m_band = 0.0f;
	// the inner arc far enough out that a sector's arc leaves plateau between it and its neighbours'
	shape.m_inner = 30.0f + 6.0f * (Real)shape.m_lanes;
	const Real outer = shape.m_inner + RMG_AOD_ROW * (Real)( RMG_AOD_ARCS - 1 );
	shape.m_rim = outer + RMG_AOD_RIM_BASE;
	Real arcs = 0.0f;
	for( Int i = 0; i < RMG_AOD_ARCS; i++ )
		arcs += shape.m_inner + RMG_AOD_ROW * (Real)i;
	const Real radial = ( shape.m_rim - 8.0f - outer ) + RMG_AOD_ROW * (Real)( RMG_AOD_ARCS - 1 ) + shape.m_inner;
	const Real sector = 2.0f * PI / (Real)shape.m_lanes;
	shape.m_span = std::max( 0.35f, std::min( ( RMG_AOD_LANE_LENGTH - radial ) / arcs, 0.7f * sector ) );
	shape.m_cells = 2 * (Int)( shape.m_rim + 14.0f + 0.5f );
	return shape;
}

void RMGLayout::buildAod( void )
{
	m_style = NULL;
	m_styleScale = 1.0f;
	m_skeleton = FALSE;
	m_type = &theMapTypes[RMG_MAP_PLAINS];
	m_biome = &theBiomes[rmgBiomeFor( m_settings.m_seed )];
	m_timeOfDay = rmgTimeOfDayFor( m_settings.m_seed );
	m_snowy = rmgSnowsOn( m_settings.m_seed );
	const UnsignedInt seed = (UnsignedInt)m_settings.m_seed;
	seedPermutation( m_settings.m_seed, m_perm );

	const RMGAodShape shape = rmgAodShape( m_settings.m_seed, m_settings.m_numPlayers );
	const Int players = m_settings.m_numPlayers;
	const Real side = (Real)m_settings.m_playableCells, half = 0.5f * side;
	const Bool ring = shape.m_lanes > 1;
	const Real sector = 2.0f * PI / (Real)shape.m_lanes;
	const Real turn = ring ? 2.0f * PI * hashUnit( seed + 71005U, 0, 1 ) : 0.0f;
	m_width = m_settings.m_playableCells + 2 * RMG_BORDER_CELLS;
	m_height = m_width;
	m_waterHeight = 0.0f;		// no water anywhere
	m_playable = TRUE;
	m_starts.clear();
	m_objects.clear();
	m_waypointLinks.clear();
	m_lakes.clear();
	m_areas.clear();
	m_sites.clear();
	m_routeLock.clear();
	const Int cells = m_width * m_height;
	m_inLake.assign( cells, 0 );

	// a point of sector 0 turned into sector n, and any point turned back into sector 0: the mirror
	auto turned = [&]( const RMGPoint& p, Real angle ) -> RMGPoint
	{
		const Real c = Cos( angle ), s = Sin( angle ), dx = p.m_cellX - half, dy = p.m_cellY - half;
		const RMGPoint q = { half + dx * c - dy * s, half + dx * s + dy * c };
		return q;
	};
	auto mirrored = [&]( Real cellX, Real cellY ) -> RMGPoint
	{
		const RMGPoint p = { cellX, cellY };
		if( !ring )
			return p;
		const Real a = ATan2( cellY - half, cellX - half ) - turn;
		const Real n = floorf( a / sector + 0.5f );
		return turned( p, -n * sector );
	};
	auto polar = [&]( Real r, Real a ) -> RMGPoint
	{
		const RMGPoint p = { half + r * Cos( turn + a ), half + r * Sin( turn + a ) };
		return p;
	};

	/* The seats: one, on the strip south of the band; more, one on the rim where its sector meets the next,
		between two lanes' rooms, the dock in from it. */
	std::vector<RMGPoint> docks;
	for( Int i = 0; i < players; i++ )
	{
		if( !ring )
		{
			const RMGPoint start = { half, 0.5f * shape.m_strip }, dock = { half + 18.0f, 0.5f * shape.m_strip };
			m_starts.push_back( start );
			docks.push_back( dock );
			continue;
		}
		const Real a = 0.5f * sector;
		const Real r = shape.m_rim - 28.0f;
		m_starts.push_back( turned( polar( r, a ), sector * (Real)i ) );
		docks.push_back( turned( polar( r - 16.0f, a ), sector * (Real)i ) );
	}
	/* The map list takes a map with one start for a mission's and leaves it out of every room, so the
		one-seat map has a spare start on the strip north of the band; nobody's lane is beside it. */
	if( !ring )
	{
		const RMGPoint spare = { half, side - 0.5f * shape.m_strip }, dock = { half + 18.0f, side - 0.5f * shape.m_strip };
		m_starts.push_back( spare );
		docks.push_back( dock );
	}
	auto nearStart = [&]( Real cellX, Real cellY ) -> Real
	{
		Real nearest = 1.0e9f;
		for( UnsignedInt s = 0; s < m_starts.size(); s++ )
		{
			const Real dx = cellX - m_starts[s].m_cellX, dy = cellY - m_starts[s].m_cellY;
			nearest = std::min( nearest, sqrtf( dx * dx + dy * dy ) );
		}
		return nearest;
	};

	/* The plateau: level round each start, rolling a little everywhere else, the roll read in sector 0 so
		every sector rolls alike; on a ring map a mountain past the rim, its foot a cliff. */
	std::vector<Real> high( cells );
	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			const Real cellX = (Real)( x - RMG_BORDER_CELLS ), cellY = (Real)( y - RMG_BORDER_CELLS );
			const RMGPoint m = mirrored( cellX, cellY );
			const Real roll = rmgUnit( ( nearStart( cellX, cellY ) - RMG_AOD_START_FLAT ) / 20.0f );
			Real h = RMG_AOD_HIGH + 6.0f * roll * fractalNoise( m_perm, m.m_cellX * 0.03f + 11.3f, m.m_cellY * 0.03f + 47.9f, 3 );
			if( ring && ( cellX - half ) * ( cellX - half ) + ( cellY - half ) * ( cellY - half ) > shape.m_rim * shape.m_rim )
				h += RMG_AOD_MOUNTAIN + 10.0f * fractalNoise( m_perm, m.m_cellX * 0.05f + 3.7f, m.m_cellY * 0.05f + 71.3f, 2 );
			high[cellIndex( x, y )] = h;
		}
	}

	/* Lane 0, as points half a cell apart with the half-width there, and the spots of its waypoints; the other
		lanes are it turned into their sectors. The family's frame is laid over the band, u running east from the
		west edge's entry to the exit strip and v north and south across the band; or over the sector, u running
		in from the rim to the innermost radius and v round the sector's middle, so a stretch across the room is
		an arc. A straight stretch of 15 to 40 cells is a narrow place. One seat's lane runs on out of the east
		edge; a ring's runs from its end straight into the pit. */
	struct AodSample { Real x, y, half, at; };		///< at: cells along the waves' way (a side: where it hangs off it)
	const Real rOut = shape.m_rim - 8.0f, rIn = shape.m_inner, roomAngle = 0.35f * sector;
	const Real bandMid = shape.m_strip + 0.5f * shape.m_band, bandReach = 0.5f * shape.m_band - RMG_AOD_BAND_MARGIN;
	const Real eastEnd = side - 0.5f * RMG_AOD_EXIT;
	// the cells a unit of u spans, and a unit of v (at the room's middle on a ring): rmgAodPlan draws turns and circles in cells
	const Real uCells = ring ? rOut - rIn : eastEnd - RMG_AOD_ENTRY_IN;
	const Real vCells = ring ? roomAngle * 0.5f * ( rOut + rIn ) : bandReach;
	const UnsignedInt variety = hashCell( seed, 71031, 7 );		///< rmgAodPlan's draws within a family, the seed's own
	auto frame =[&]( Real u, Real v ) -> RMGPoint
	{
		if( !ring )
		{
			const RMGPoint q = { RMG_AOD_ENTRY_IN + u * ( eastEnd - RMG_AOD_ENTRY_IN ), bandMid + v * bandReach };
			return q;
		}
		return polar( rOut - u * ( rOut - rIn ), v * roomAngle );
	};
	// lane 0 laid from a plan: its samples, its stops if asked for, and the length of the waves' way, cells
	auto layLane = [&]( const RMGAodPlan& plan, std::vector<AodSample>& out, std::vector<RMGPoint> *spotsOut ) -> Real
	{
		out.clear();
		if( spotsOut != NULL )
			spotsOut->clear();
		Real along = 0.0f, sinceSpot = 0.0f;
		auto spot = [&]( const RMGPoint& q, Bool last )
		{
			// no stop in the one seat's exit strip but the last
			if( spotsOut != NULL && ( last || ring || q.m_cellX < side - RMG_AOD_EXIT - 2.0f ) )
				spotsOut->push_back( q );
		};
		auto add = [&]( const RMGPoint& q, Real halfWidth, Real at )
		{
			const AodSample sample = { q.m_cellX, q.m_cellY, halfWidth, at };
			out.push_back( sample );
		};
		// a line of the frame; a side's samples carry where along the way it hangs, from at0 to at1
		auto walk = [&]( const std::vector<RMGPoint>& line, Bool onWay, Real at0, Real at1 )
		{
			std::vector<Real> lengths;
			Real lineLength = 0.0f;
			for( UnsignedInt i = 1; i < line.size(); i++ )
			{
				// over eight chords, so an arc counts its curve
				Real l = 0.0f;
				RMGPoint prev = frame( line[i - 1].m_cellX, line[i - 1].m_cellY );
				for( Int c = 1; c <= 8; c++ )
				{
					const Real t = (Real)c / 8.0f;
					const RMGPoint q = frame( line[i - 1].m_cellX + ( line[i].m_cellX - line[i - 1].m_cellX ) * t,
						line[i - 1].m_cellY + ( line[i].m_cellY - line[i - 1].m_cellY ) * t );
					l += sqrtf( ( q.m_cellX - prev.m_cellX ) * ( q.m_cellX - prev.m_cellX ) + ( q.m_cellY - prev.m_cellY ) * ( q.m_cellY - prev.m_cellY ) );
					prev = q;
				}
				lengths.push_back( l );
				lineLength += l;
			}
			Real done = 0.0f;
			for( UnsignedInt i = 1; i < line.size(); i++ )
			{
				const Real l = lengths[i - 1];
				const Int steps = std::max( 1, (Int)( l / 0.5f ) );
				const Real halfWidth = l >= 15.0f && l < 40.0f ? RMG_AOD_STEP_HALF : RMG_AOD_RUN_HALF;
				for( Int s = 1; s <= steps; s++ )
				{
					const Real t = (Real)s / (Real)steps;
					const RMGPoint q = frame( line[i - 1].m_cellX + ( line[i].m_cellX - line[i - 1].m_cellX ) * t,
						line[i - 1].m_cellY + ( line[i].m_cellY - line[i - 1].m_cellY ) * t );
					const Real here = done + l * t;
					add( q, halfWidth, onWay ? along + here : at0 + ( at1 - at0 ) * ( lineLength > 0.0f ? here / lineLength : 0.0f ) );
					if( !onWay )
						continue;
					sinceSpot += l / (Real)steps;
					const Bool last = s == steps && i + 1 == line.size();
					if( sinceSpot >= RMG_AOD_WAYPOINT_EVERY || last )
					{
						spot( q, last && !ring );
						sinceSpot = 0.0f;
					}
				}
				done += l;
			}
			if( onWay )
				along += lineLength;
		};
		const RMGPoint entry = frame( plan.path[0].m_cellX, plan.path[0].m_cellY );
		spot( entry, FALSE );
		// the trench on out of the west edge, or into the mountain's foot: a dead end the waves come out of
		for( Real o = 0.5f; o <= 12.0f; o += 0.5f )
			add( ring ? polar( rOut + o, plan.path[0].m_cellY * roomAngle ) : RMGPoint{ entry.m_cellX - o, entry.m_cellY }, RMG_AOD_RUN_HALF, 0.0f );
		walk( plan.path, TRUE, 0.0f, 0.0f );
		const UnsignedInt wayEnd = out.size();
		// the sides, each hung off the way where it meets it
		for( UnsignedInt n = 0; n < plan.side.size(); n++ )
		{
			const RMGPoint from = frame( plan.side[n].front().m_cellX, plan.side[n].front().m_cellY );
			const RMGPoint to = frame( plan.side[n].back().m_cellX, plan.side[n].back().m_cellY );
			Real at0 = 0.0f, at1 = 0.0f, d0 = 1.0e9f, d1 = 1.0e9f;
			for( UnsignedInt i = 0; i < wayEnd; i++ )
			{
				const Real e0 = ( out[i].x - from.m_cellX ) * ( out[i].x - from.m_cellX ) + ( out[i].y - from.m_cellY ) * ( out[i].y - from.m_cellY );
				const Real e1 = ( out[i].x - to.m_cellX ) * ( out[i].x - to.m_cellX ) + ( out[i].y - to.m_cellY ) * ( out[i].y - to.m_cellY );
				if( e0 < d0 ) { d0 = e0; at0 = out[i].at; }
				if( e1 < d1 ) { d1 = e1; at1 = out[i].at; }
			}
			// a bay (its far end on no way) hangs from its near end alone
			if( d1 > 4.0f )
				at1 = at0;
			walk( plan.side[n], FALSE, at0, at1 );
		}
		const RMGPoint last = frame( plan.path.back().m_cellX, plan.path.back().m_cellY );
		if( !ring )
		{
			for( Real o = 0.5f; o <= 2.0f * RMG_AOD_EXIT; o += 0.5f )
				add( RMGPoint{ last.m_cellX + o, last.m_cellY }, RMG_AOD_RUN_HALF, along + o );	// and out of the east edge
			return along;
		}
		// into the pit, a stop half way and one in it, so the one before the last stands well out of it
		const Real a = plan.path.back().m_cellY * roomAngle;
		for( Real r = rIn - 0.5f; r >= 3.0f; r -= 0.5f )
			add( polar( r, a ), RMG_AOD_RUN_HALF, along + rIn - r );
		along += rIn - 3.0f;
		if( spotsOut != NULL )
		{
			spotsOut->push_back( polar( std::max( RMG_AOD_PIT + 10.0f, 0.5f * rIn ), a ) );
			spotsOut->push_back( polar( 3.0f, a ) );
		}
		return along;
	};
	/* Whether a laid lane 0 will do: about as long as RMG_AOD_LANE_LENGTH, every two stretches of trench that
		are not one after the other with a strip of plateau between them, every start clear of it, and on a ring
		every neighbour's lane (lane 0 turned) twenty cells off it but in the pit. Every fourth sample. */
	// 0 when it fits, else why not: 1 too short, 2 too long, 3 two stretches too near, 4 a start, 5 a neighbour
	auto fits = [&]( const std::vector<AodSample>& s, Real length ) -> Int
	{
		if( length < 520.0f )
			return 1;
		if( length > 820.0f )
			return 2;
		const Real clear = 2.0f * ( RMG_AOD_RUN_HALF + RMG_AOD_FACE ) + 4.0f;
		for( UnsignedInt i = 0; i < s.size(); i += 4 )
		{
			for( UnsignedInt j = i + 4; j < s.size(); j += 4 )
			{
				const Real dx = s[i].x - s[j].x, dy = s[i].y - s[j].y;
				if( fabsf( s[i].at - s[j].at ) > 3.0f * clear && dx * dx + dy * dy < clear * clear )
					return 3;
			}
			for( UnsignedInt k = 0; k < m_starts.size(); k++ )
			{
				const Real dx = s[i].x - m_starts[k].m_cellX, dy = s[i].y - m_starts[k].m_cellY;
				if( dx * dx + dy * dy < ( RMG_AOD_START_FLAT + 8.0f ) * ( RMG_AOD_START_FLAT + 8.0f ) )
					return 4;
			}
			if( !ring )
				continue;
			const Real ri = sqrtf( ( s[i].x - half ) * ( s[i].x - half ) + ( s[i].y - half ) * ( s[i].y - half ) );
			if( ri < rIn - 2.0f )
				continue;
			for( Int turnBy = -1; turnBy <= 1; turnBy += 2 )
			{
				for( UnsignedInt j = 0; j < s.size(); j += 4 )
				{
					const RMGPoint p = { s[j].x, s[j].y };
					const RMGPoint q = turned( p, sector * (Real)turnBy );
					const Real dx = s[i].x - q.m_cellX, dy = s[i].y - q.m_cellY;
					const Real rq = sqrtf( ( q.m_cellX - half ) * ( q.m_cellX - half ) + ( q.m_cellY - half ) * ( q.m_cellY - half ) );
					if( rq >= rIn - 2.0f && dx * dx + dy * dy < 20.0f * 20.0f )
						return 5;
				}
			}
		}
		return 0;
	};
	// the seed's family, or the next one that fits, with the repeat and reach that come nearest the length
	// a third of the seeds a mixed lane, the rest one of the eleven families alone
	const Int seeded = theAodFamilyForTests >= 0 ? theAodFamilyForTests % RMG_AOD_FAMILIES
		: hashCell( seed + 71021U, 1, 0 ) % 3U == 0U ? (Int)RMG_AOD_MIXED
		: (Int)( hashCell( seed + 71021U, 0, 0 ) % (UnsignedInt)( RMG_AOD_FAMILIES - 1 ) );
	Int family = -1, chosenK = 4;
	Real chosenS = 1.0f;
	{
		RMGAodPlan plan;
		std::vector<AodSample> trial;
		std::vector<Int> repeats;
		for( Int f = 0; f < RMG_AOD_FAMILIES && family < 0; f++ )
		{
			const Int candidate = ( seeded + f ) % RMG_AOD_FAMILIES;
			rmgAodRepeats( candidate, repeats );
			Real best = 1.0e9f;
			Int why[6] = { 0, 0, 0, 0, 0, 0 };
			/* Circles takes the most loops that fit, not the length nearest the mark: its counts come most first, and
				the first that fits is kept. Taken by length, the one-seat band chose two runs and a single loop. */
			for( UnsignedInt r = 0; r < repeats.size() && !( candidate == 10 && family == candidate ); r++ )
			{
				/* The reach that brings the lane to the length: it grows with the reach, so halve the gap eight
					times between 0.15 and 1, then try that reach and two a little either side of it. */
				Real lo = 0.15f, hi = 1.0f;
				for( Int step = 0; step < 8; step++ )
				{
					const Real mid = 0.5f * ( lo + hi );
					rmgAodPlan( candidate, repeats[r], mid, uCells, vCells, variety, plan );
					if( layLane( plan, trial, NULL ) < RMG_AOD_LANE_LENGTH )
						lo = mid;
					else
						hi = mid;
				}
				const Real reaches[3] = { 0.5f * ( lo + hi ), std::max( 0.15f, 0.5f * ( lo + hi ) - 0.08f ), std::min( 1.0f, 0.5f * ( lo + hi ) + 0.08f ) };
				for( Int t = 0; t < 3; t++ )
				{
					rmgAodPlan( candidate, repeats[r], reaches[t], uCells, vCells, variety, plan );
					const Real length = layLane( plan, trial, NULL );
					if( fabsf( length - RMG_AOD_LANE_LENGTH ) >= best )
						continue;
					const Int reason = fits( trial, length );
					++why[reason];
					if( reason == 0 )
					{
						best = fabsf( length - RMG_AOD_LANE_LENGTH );
						family = candidate;
						chosenK = repeats[r];
						chosenS = reaches[t];
					}
				}
			}
			if( family < 0 )
				DEBUG_LOG(( "random map: aod %s lanes do not fit %d seats: %d too short, %d too long, %d crowd themselves, %d a start, %d a neighbour\n",
					theAodFamilies[candidate], players, why[1], why[2], why[3], why[4], why[5] ));
		}
		m_aodSkipped = family < 0 ? RMG_AOD_FAMILIES : ( family - seeded + RMG_AOD_FAMILIES ) % RMG_AOD_FAMILIES;
		if( family < 0 )
		{
			DEBUG_LOG(( "random map: aod seed %d found no lane that fits; the switchback as it comes\n", m_settings.m_seed ));
			family = 0;
		}
	}
	m_aodFamily = family;
	std::vector<AodSample> lane;
	std::vector<RMGPoint> laneSpots;
	Real way = 0.0f;
	{
		RMGAodPlan plan;
		rmgAodPlan( family, chosenK, chosenS, uCells, vCells, variety, plan );
		way = layLane( plan, lane, &laneSpots );
	}
	// every lane: lane 0 turned into its sector
	std::vector<AodSample> samples;
	std::vector< std::vector<RMGPoint> > spots( shape.m_lanes );
	std::vector<Real> lengths( shape.m_lanes, way );
	for( Int n = 0; n < shape.m_lanes; n++ )
	{
		const Real angle = sector * (Real)n;
		for( UnsignedInt s = 0; s < lane.size(); s++ )
		{
			const RMGPoint p = { lane[s].x, lane[s].y };
			const RMGPoint q = n == 0 ? p : turned( p, angle );
			const AodSample sample = { q.m_cellX, q.m_cellY, lane[s].half, lane[s].at };
			samples.push_back( sample );
		}
		for( UnsignedInt s = 0; s < laneSpots.size(); s++ )
			spots[n].push_back( n == 0 ? laneSpots[s] : turned( laneSpots[s], angle ) );
	}

	// the trenches cut into the plateau, the union of every sample's: a flat floor, cliff walls
	std::vector<Real> heights( high );
	std::vector<Real> nearPath( cells, 1.0e9f );		///< cells from the nearest path's wall foot, negative on a floor
	for( UnsignedInt s = 0; s < samples.size(); s++ )
	{
		const AodSample& sample = samples[s];
		const Real outer = sample.half + RMG_AOD_FACE + 8.0f;
		const Int x0 = std::max( 0, (Int)( sample.x - outer ) + RMG_BORDER_CELLS ), x1 = std::min( m_width - 1, (Int)( sample.x + outer ) + 1 + RMG_BORDER_CELLS );
		const Int y0 = std::max( 0, (Int)( sample.y - outer ) + RMG_BORDER_CELLS ), y1 = std::min( m_height - 1, (Int)( sample.y + outer ) + 1 + RMG_BORDER_CELLS );
		for( Int y = y0; y <= y1; y++ )
		{
			for( Int x = x0; x <= x1; x++ )
			{
				const Int i = cellIndex( x, y );
				const Real dx = (Real)( x - RMG_BORDER_CELLS ) - sample.x, dy = (Real)( y - RMG_BORDER_CELLS ) - sample.y;
				const Real d = sqrtf( dx * dx + dy * dy ) - sample.half;
				if( d < nearPath[i] )
					nearPath[i] = d;
				const Real wall = RMG_AOD_FLOOR + ( high[i] - RMG_AOD_FLOOR ) * rmgUnit( d / RMG_AOD_FACE );
				if( wall < heights[i] )
					heights[i] = wall;
			}
		}
	}
	/* A sliver of plateau between two stretches of trench (the tip of the wedge inside a sharp turn) has a crest
		that climbs gently along it from the tip, a ramp out of the lane on every map whose turns are acute. Every
		strip of plateau narrower than RMG_AOD_SLIVER cells goes down to the floor: the wedge ends in a blunt face
		where it is already full height, a cliff. An opening of the plateau by a disc, read on height vertices. */
	{
		std::vector<UnsignedByte> plateau( cells ), core( cells, 0 );
		for( Int i = 0; i < cells; i++ )
			plateau[i] = nearPath[i] > 0.0f ? 1 : 0;
		const Int reach = (Int)RMG_AOD_SLIVER;
		const Real reach2 = RMG_AOD_SLIVER * RMG_AOD_SLIVER;
		for( Int y = reach; y < m_height - reach; y++ )
			for( Int x = reach; x < m_width - reach; x++ )
			{
				Bool all = plateau[cellIndex( x, y )] != 0;
				for( Int oy = -reach; oy <= reach && all; oy++ )
					for( Int ox = -reach; ox <= reach && all; ox++ )
						if( (Real)( ox * ox + oy * oy ) <= reach2 && !plateau[cellIndex( x + ox, y + oy )] )
							all = FALSE;
				core[cellIndex( x, y )] = all ? 1 : 0;
			}
		Int slivers = 0;
		for( Int y = reach; y < m_height - reach; y++ )
			for( Int x = reach; x < m_width - reach; x++ )
			{
				const Int i = cellIndex( x, y );
				if( !plateau[i] )
					continue;
				Bool kept = FALSE;
				for( Int oy = -reach; oy <= reach && !kept; oy++ )
					for( Int ox = -reach; ox <= reach && !kept; ox++ )
						if( (Real)( ox * ox + oy * oy ) <= reach2 && core[cellIndex( x + ox, y + oy )] )
							kept = TRUE;
				if( kept )
					continue;
				heights[i] = RMG_AOD_FLOOR;
				nearPath[i] = -0.5f;
				++slivers;
			}
		DEBUG_LOG(( "random map: aod %d height points of plateau slivers brought down to the floor\n", slivers ));
	}
	m_heights.assign( cells, (UnsignedByte)RMG_AOD_HIGH );
	for( Int i = 0; i < cells; i++ )
		m_heights[i] = (UnsignedByte)(Int)( std::max( 0.0f, std::min( 255.0f, heights[i] ) ) + 0.5f );

	Int waypointID = 1;
	auto addWaypoint = [&]( const AsciiString& name, Real cellX, Real cellY ) -> Int
	{
		RMGObject waypoint;
		waypoint.m_templateName = "*Waypoints/Waypoint";
		waypoint.m_uniqueID = name;
		waypoint.m_worldX = cellX * MAP_XY_FACTOR;
		waypoint.m_worldY = cellY * MAP_XY_FACTOR;
		waypoint.m_angle = 0.0f;
		waypoint.m_waypointID = waypointID++;
		waypoint.m_flags = 0;
		m_objects.push_back( waypoint );
		return waypoint.m_waypointID;
	};

	AsciiString name;
	for( Int i = 0; i < (Int)m_starts.size(); i++ )
	{
		name.format( "Player_%d_Start", i + 1 );
		addWaypoint( name, m_starts[i].m_cellX, m_starts[i].m_cellY );
		reserveSite( m_starts[i].m_cellX, m_starts[i].m_cellY, 20.0f );
		// no supply dock: a seat lives on what its kills and the waves pay (the owner, 2026-11-28; AodDirector.h)
	}
	{
		RMGArea exit;
		exit.m_name = "AOD_Exit";
		if( ring )
		{
			for( Int k = 0; k < 24; k++ )
				exit.m_polygon.push_back( polar( RMG_AOD_PIT, 2.0f * PI * (Real)k / 24.0f ) );
		}
		else
		{
			const RMGPoint corners[4] = { { side - RMG_AOD_EXIT, 0.0f }, { side, 0.0f }, { side, side }, { side - RMG_AOD_EXIT, side } };
			exit.m_polygon.assign( corners, corners + 4 );
		}
		m_areas.push_back( exit );
	}
	for( Int n = 0; n < shape.m_lanes; n++ )
	{
		name.format( "AOD_Entry_%d", n + 1 );
		Int previous = addWaypoint( name, spots[n][0].m_cellX, spots[n][0].m_cellY );
		for( UnsignedInt k = 1; k < spots[n].size(); k++ )
		{
			name.format( "AOD_Path_%d_%d", n + 1, (Int)k );
			const Int id = addWaypoint( name, spots[n][k].m_cellX, spots[n][k].m_cellY );
			m_waypointLinks.push_back( std::make_pair( previous, id ) );
			previous = id;
		}
	}

	/* Bridges over the lanes (the owner, 2026-11-28: "köprü at aralara ki diğer tarafa geçebilsin workerlar
		birimler"), so a seat's dozers and units reach the plateau across a lane: the far side of a one-seat map,
		a neighbour's slice, the island inside a circle. A bridge is the map's own kind, a pair of objects with
		the bridge flags that W3DBridgeBuffer::loadBridges turns into a logic bridge on its own pathfinding layer.
		Its ends stand on the plateau RMG_AOD_BRIDGE_HALF cells either side of the lane's middle, so the deck
		runs 24 units over the floor: the waves walk on under it (PathfindLayer::classifyLayerMapCell blocks
		the ground below a deck only within 10 units of it) and cannot reach its ends up the cliffs, and nobody
		on it can get down into the lane. Lane 0's bridges are found on straight stretches clear of every stop
		(a stop under a deck could send a wave to the deck's layer), every theAodBridgeEvery cells of the way,
		and kept only if every lane's turned copy also lands on plateau at both ends and crosses its own lane
		alone. */
	Int bridges = 0;
	std::vector<RMGPoint> bridgeEnds;		///< every end, kept clear of trees
	{
		const Int bridgePoint1 = 0x00000010, bridgePoint2 = 0x00000020;		// FLAG_BRIDGE_POINT1 and 2, MapObject.h
		auto okEnd = [&]( const RMGPoint& e ) -> Bool
		{
			const Int x = (Int)floorf( e.m_cellX + 0.5f ) + RMG_BORDER_CELLS, y = (Int)floorf( e.m_cellY + 0.5f ) + RMG_BORDER_CELLS;
			if( x < RMG_BORDER_CELLS + 4 || y < RMG_BORDER_CELLS + 4 || x >= RMG_BORDER_CELLS + m_settings.m_playableCells - 4
				|| y >= RMG_BORDER_CELLS + m_settings.m_playableCells - 4 )
				return FALSE;
			if( !ring && ( e.m_cellX < RMG_AOD_ENTRY_IN + 4.0f || e.m_cellX > side - RMG_AOD_EXIT - 4.0f ) )
				return FALSE;
			if( ring && ( e.m_cellX - half ) * ( e.m_cellX - half ) + ( e.m_cellY - half ) * ( e.m_cellY - half ) > ( shape.m_rim - 6.0f ) * ( shape.m_rim - 6.0f ) )
				return FALSE;
			const Int i = cellIndex( x, y );
			return nearPath[i] > 2.5f && heights[i] >= RMG_AOD_HIGH - 8.0f;
		};
		// the deck from end to end crosses floor only within the lane it is for
		auto crossesOne = [&]( const RMGPoint& e1, const RMGPoint& e2, const RMGPoint& mid ) -> Bool
		{
			for( Int k = 0; k <= 40; k++ )
			{
				const Real t = (Real)k / 40.0f;
				const Real cx = e1.m_cellX + ( e2.m_cellX - e1.m_cellX ) * t, cy = e1.m_cellY + ( e2.m_cellY - e1.m_cellY ) * t;
				const Int x = (Int)floorf( cx + 0.5f ) + RMG_BORDER_CELLS, y = (Int)floorf( cy + 0.5f ) + RMG_BORDER_CELLS;
				if( nearPath[cellIndex( x, y )] <= 0.0f && ( cx - mid.m_cellX ) * ( cx - mid.m_cellX ) + ( cy - mid.m_cellY ) * ( cy - mid.m_cellY ) > 8.0f * 8.0f )
					return FALSE;
			}
			return TRUE;
		};
		// a bridge over lane 0's sample i, if one may stand there: its two ends
		auto candidateAt = [&]( Int i, RMGPoint& e1, RMGPoint& e2 ) -> Bool
		{
			const AodSample& here = lane[i];
			if( here.at < 40.0f || here.at > way - 40.0f )
				return FALSE;
			// a straight stretch of the way: twelve half-cell steps either side, six cells apart
			const AodSample& back = lane[i - 6];
			const AodSample& ahead = lane[i + 6];
			if( fabsf( ahead.at - back.at - 6.0f ) > 0.6f )
				return FALSE;
			const Real dx = ahead.x - back.x, dy = ahead.y - back.y, length = sqrtf( dx * dx + dy * dy );
			if( length < 5.5f )
				return FALSE;
			for( UnsignedInt s = 0; s < laneSpots.size(); s++ )
				if( ( laneSpots[s].m_cellX - here.x ) * ( laneSpots[s].m_cellX - here.x )
					+ ( laneSpots[s].m_cellY - here.y ) * ( laneSpots[s].m_cellY - here.y ) < 6.0f * 6.0f )
					return FALSE;
			const RMGPoint mid = { here.x, here.y };
			e1.m_cellX = here.x + dy / length * RMG_AOD_BRIDGE_HALF;
			e1.m_cellY = here.y - dx / length * RMG_AOD_BRIDGE_HALF;
			e2.m_cellX = here.x - dy / length * RMG_AOD_BRIDGE_HALF;
			e2.m_cellY = here.y + dx / length * RMG_AOD_BRIDGE_HALF;
			for( Int n = 0; n < shape.m_lanes; n++ )
			{
				const Real angle = sector * (Real)n;
				const RMGPoint a1 = n == 0 ? e1 : turned( e1, angle ), a2 = n == 0 ? e2 : turned( e2, angle ), am = n == 0 ? mid : turned( mid, angle );
				if( !okEnd( a1 ) || !okEnd( a2 ) || !crossesOne( a1, a2, am ) )
					return FALSE;
			}
			return TRUE;
		};
		const Real every = theAodBridgeEvery[family];
		Real lastAt = -0.5f * every;
		std::vector< std::pair<RMGPoint, RMGPoint> > picked;
		for( Int i = 6; i + 6 < (Int)lane.size(); i++ )
		{
			RMGPoint e1, e2;
			if( lane[i].at < lastAt + every || !candidateAt( i, e1, e2 ) )
				continue;
			picked.push_back( std::make_pair( e1, e2 ) );
			lastAt = lane[i].at;
		}
		/* Then every pocket of plateau a lane cuts off (a circle's island, the land between a comb's bays, the far
			side of a one-seat map) gets a bridge from land a seat already reaches: the plateau's walkable regions
			are labelled once, the bridges join them, and while a region of RMG_AOD_POCKET cells or more stays out of
			every seat's reach, the first place along lane 0 whose bridge (in some lane's copy) joins it to a reached
			one gets a bridge too. */
		{
			std::vector<Int> region( cells, -1 );
			std::vector<Int> regionSize;
			for( Int y = 0; y + 1 < m_height; y++ )
				for( Int x = 0; x + 1 < m_width; x++ )
				{
					auto plateauCell = [&]( Int cx, Int cy ) -> Bool
					{
						if( cx < 0 || cy < 0 || cx + 1 >= m_width || cy + 1 >= m_height || !insidePlayable( cx, cy ) )
							return FALSE;
						const Real px = (Real)( cx - RMG_BORDER_CELLS ) - half, py = (Real)( cy - RMG_BORDER_CELLS ) - half;
						if( ring && px * px + py * py > shape.m_rim * shape.m_rim )
							return FALSE;
						return m_heights[cellIndex( cx, cy )] >= 50 && cellSpanWorld( cx, cy ) <= RMG_CLIFF_WORLD_SPAN;
					};
					if( region[cellIndex( x, y )] >= 0 || !plateauCell( x, y ) )
						continue;
					const Int label = (Int)regionSize.size();
					Int size = 0;
					std::vector<Int> queue( 1, cellIndex( x, y ) );
					region[queue[0]] = label;
					for( UnsignedInt head = 0; head < queue.size(); head++ )
					{
						++size;
						const Int qx = queue[head] % m_width, qy = queue[head] / m_width;
						const Int nx[4] = { qx - 1, qx + 1, qx, qx }, ny[4] = { qy, qy, qy - 1, qy + 1 };
						for( Int k = 0; k < 4; k++ )
							if( plateauCell( nx[k], ny[k] ) && region[cellIndex( nx[k], ny[k] )] < 0 )
							{
								region[cellIndex( nx[k], ny[k] )] = label;
								queue.push_back( cellIndex( nx[k], ny[k] ) );
							}
					}
					regionSize.push_back( size );
				}
			auto regionAt = [&]( const RMGPoint& e ) -> Int
			{
				return region[cellIndex( (Int)floorf( e.m_cellX + 0.5f ) + RMG_BORDER_CELLS, (Int)floorf( e.m_cellY + 0.5f ) + RMG_BORDER_CELLS )];
			};
			// the regions the seats reach over ground and the bridges so far
			std::vector<char> reached;
			auto reach = [&]()
			{
				reached.assign( regionSize.size(), 0 );
				for( Int s = 0; s < players; s++ )
				{
					const Int r = regionAt( m_starts[s] );
					if( r >= 0 )
						reached[r] = 1;
				}
				for( Bool grew = TRUE; grew; )
				{
					grew = FALSE;
					for( UnsignedInt k = 0; k < picked.size(); k++ )
						for( Int n = 0; n < shape.m_lanes; n++ )
						{
							const Real angle = sector * (Real)n;
							const Int r1 = regionAt( n == 0 ? picked[k].first : turned( picked[k].first, angle ) );
							const Int r2 = regionAt( n == 0 ? picked[k].second : turned( picked[k].second, angle ) );
							if( r1 >= 0 && r2 >= 0 && reached[r1] != reached[r2] )
							{
								reached[r1] = reached[r2] = 1;
								grew = TRUE;
							}
						}
				}
			};
			for( Int round = 0; round < 40; round++ )
			{
				reach();
				Bool missing = FALSE;
				for( UnsignedInt r = 0; r < regionSize.size() && !missing; r++ )
					missing = !reached[r] && regionSize[r] >= RMG_AOD_POCKET;
				if( !missing )
					break;
				Bool added = FALSE;
				for( Int i = 6; i + 6 < (Int)lane.size() && !added; i++ )
				{
					RMGPoint e1, e2;
					if( !candidateAt( i, e1, e2 ) )
						continue;
					Bool crowded = FALSE;		// not beside a bridge already there
					for( UnsignedInt k = 0; k < picked.size() && !crowded; k++ )
						crowded = ( picked[k].first.m_cellX - e1.m_cellX ) * ( picked[k].first.m_cellX - e1.m_cellX )
							+ ( picked[k].first.m_cellY - e1.m_cellY ) * ( picked[k].first.m_cellY - e1.m_cellY ) < 12.0f * 12.0f;
					for( Int n = 0; n < shape.m_lanes && !added && !crowded; n++ )
					{
						const Real angle = sector * (Real)n;
						const Int r1 = regionAt( n == 0 ? e1 : turned( e1, angle ) ), r2 = regionAt( n == 0 ? e2 : turned( e2, angle ) );
						if( r1 < 0 || r2 < 0 || reached[r1] == reached[r2] )
							continue;
						const Int out = reached[r1] ? r2 : r1;
						if( regionSize[out] >= RMG_AOD_POCKET )
						{
							picked.push_back( std::make_pair( e1, e2 ) );
							added = TRUE;
						}
					}
				}
				if( !added )
				{
					DEBUG_LOG(( "random map: aod seed %d has a pocket of plateau no bridge reaches\n", m_settings.m_seed ));
					break;
				}
			}
		}
		for( UnsignedInt k = 0; k < picked.size(); k++ )
			DEBUG_LOG(( "random map: aod bridge %d over lane 1, world (%.0f,%.0f) to (%.0f,%.0f)\n", (Int)k + 1,
				picked[k].first.m_cellX * MAP_XY_FACTOR, picked[k].first.m_cellY * MAP_XY_FACTOR, picked[k].second.m_cellX * MAP_XY_FACTOR, picked[k].second.m_cellY * MAP_XY_FACTOR ));
		AsciiString bridgeID;
		for( Int n = 0; n < shape.m_lanes; n++ )
		{
			const Real angle = sector * (Real)n;
			for( UnsignedInt k = 0; k < picked.size(); k++ )
			{
				const RMGPoint ends[2] = { n == 0 ? picked[k].first : turned( picked[k].first, angle ), n == 0 ? picked[k].second : turned( picked[k].second, angle ) };
				for( Int e = 0; e < 2; e++ )
				{
					RMGObject point;
					point.m_templateName = RMG_AOD_BRIDGE;
					bridgeID.format( "AOD Bridge %d %d %c", n + 1, (Int)k + 1, e == 0 ? 'a' : 'b' );
					point.m_uniqueID = bridgeID;
					point.m_worldX = ends[e].m_cellX * MAP_XY_FACTOR;
					point.m_worldY = ends[e].m_cellY * MAP_XY_FACTOR;
					point.m_angle = 0.0f;
					point.m_waypointID = 0;
					point.m_flags = e == 0 ? bridgePoint1 : bridgePoint2;
					m_objects.push_back( point );
					bridgeEnds.push_back( ends[e] );
				}
				++bridges;
			}
		}
	}

	/* The ground: the floors bare dirt, the walls and the rim along them rock, the ground round a start
		worn, the plateau grass broken by patches of the second class. */
	m_terrain.assign( cells, (UnsignedByte)RMG_TERRAIN_GRASS );
	m_passable.assign( cells, 0 );
	for( Int y = 0; y + 1 < m_height; y++ )
	{
		for( Int x = 0; x + 1 < m_width; x++ )
		{
			const Int i = cellIndex( x, y );
			// the ground's patches read in sector 0, so every sector is dressed alike
			const RMGPoint m = mirrored( (Real)( x - RMG_BORDER_CELLS ) + 0.5f, (Real)( y - RMG_BORDER_CELLS ) + 0.5f );
			const Real mx = m.m_cellX + (Real)RMG_BORDER_CELLS, my = m.m_cellY + (Real)RMG_BORDER_CELLS;
			const Real wander = fractalNoise( m_perm, mx * 0.09f + 5.3f, my * 0.09f + 61.7f, 2 );
			const Bool cliff = cellSpanWorld( x, y ) > RMG_CLIFF_WORLD_SPAN;
			UnsignedByte terrainClass = RMG_TERRAIN_GRASS;
			if( cliff || ( nearPath[i] > 0.0f && nearPath[i] < 2.5f + wander ) )
				terrainClass = RMG_TERRAIN_ROCK;
			else if( nearPath[i] <= 0.0f )
				terrainClass = RMG_TERRAIN_DIRT;
			else if( nearStart( (Real)( x - RMG_BORDER_CELLS ) + 0.5f, (Real)( y - RMG_BORDER_CELLS ) + 0.5f ) < RMG_AOD_START_FLAT )
				terrainClass = fractalNoise( m_perm, mx * 0.06f + 3.1f, my * 0.06f + 9.4f, 2 ) > 0.0f ? RMG_TERRAIN_DIRT : RMG_TERRAIN_GRASS;
			else if( fractalNoise( m_perm, mx * 0.05f + 87.3f, my * 0.05f + 45.6f, 3 ) > 0.3f )
				terrainClass = RMG_TERRAIN_SAND;
			m_terrain[i] = terrainClass;
			m_passable[i] = insidePlayable( x, y ) && !cliff ? 1 : 0;
		}
	}

	// trees and stones on the plateau in clumps, off the rims and away from the starts, thinned to RMG_AOD_PROPS_MOST
	enum { PROP_NONE, PROP_TREE, PROP_ROCK };
	auto propAt = [&]( Int x, Int y ) -> Int
	{
		const Int i = cellIndex( x, y );
		if( !m_passable[i] || nearPath[i] < 5.0f
			|| nearStart( (Real)( x - RMG_BORDER_CELLS ) + 0.5f, (Real)( y - RMG_BORDER_CELLS ) + 0.5f ) < RMG_AOD_START_FLAT + 12.0f )
			return PROP_NONE;
		for( UnsignedInt e = 0; e < bridgeEnds.size(); e++ )
		{
			const Real ex = (Real)( x - RMG_BORDER_CELLS ) + 0.5f - bridgeEnds[e].m_cellX, ey = (Real)( y - RMG_BORDER_CELLS ) + 0.5f - bridgeEnds[e].m_cellY;
			if( ex * ex + ey * ey < 8.0f * 8.0f )
				return PROP_NONE;
		}
		// read in sector 0: every sector's woods stand alike, within a cell
		const RMGPoint m = mirrored( (Real)( x - RMG_BORDER_CELLS ) + 0.5f, (Real)( y - RMG_BORDER_CELLS ) + 0.5f );
		const Int mx = (Int)floorf( m.m_cellX ) + RMG_BORDER_CELLS, my = (Int)floorf( m.m_cellY ) + RMG_BORDER_CELLS;
		const Real roll = (Real)( hashCell( seed + 71011U, mx, my ) % 1024U ) / 1024.0f;
		const Real clump = fractalNoise( m_perm, (Real)mx * 0.05f + 7.1f, (Real)my * 0.05f + 3.9f, 3 );
		if( roll < ( clump > 0.3f ? 0.08f : 0.004f ) * m_biome->m_treeShare )
			return PROP_TREE;
		return roll > 0.997f ? PROP_ROCK : PROP_NONE;
	};
	const Int lo = RMG_BORDER_CELLS, hi = RMG_BORDER_CELLS + m_settings.m_playableCells;
	Int propsWanted = 0;
	for( Int y = lo; y < hi; y++ )
		for( Int x = lo; x < hi; x++ )
			propsWanted += propAt( x, y ) != PROP_NONE ? 1 : 0;
	const UnsignedInt keep = propsWanted <= RMG_AOD_PROPS_MOST ? 4096U
		: (UnsignedInt)( 4096.0f * (Real)RMG_AOD_PROPS_MOST / (Real)propsWanted );
	Int propID = 1;
	AsciiString uniqueID;
	for( Int y = lo; y < hi; y++ )
	{
		for( Int x = lo; x < hi; x++ )
		{
			const Int prop = propAt( x, y );
			if( prop == PROP_NONE || hashCell( seed + 71013U, x, y ) % 4096U >= keep )
				continue;
			const UnsignedInt hash = hashCell( seed + 71011U, x, y );
			const char *templateName = prop == PROP_TREE ? m_biome->m_trees[( hash >> 10 ) % (UnsignedInt)RMG_BIOME_TREES]
				: m_biome->m_rocks[( hash >> 10 ) % (UnsignedInt)RMG_BIOME_ROCKS];
			uniqueID.format( "Prop %d", propID++ );
			addObject( templateName, uniqueID.str(), (Real)( x - RMG_BORDER_CELLS ) + 0.5f, (Real)( y - RMG_BORDER_CELLS ) + 0.5f,
				(Real)( ( hash >> 14 ) % 8U ) * PI * 0.25f );
		}
	}

	DEBUG_LOG(( "random map: aod %d lanes, %s, %d cells, %s lanes (%d, %.1f) %.0f cells long, %d bridges, theme %s, %d props\n",
		shape.m_lanes, ring ? "a ring into the middle" : "west edge to east", m_settings.m_playableCells, theAodFamilies[family],
		chosenK, chosenS, lengths[0], bridges, RandomMapGenerator::aodThemeName( m_settings.m_seed ).str(), propID - 1 ));

	buildBlends();
}

//-----------------------------------------------------------------------------
// The whole layout
//-----------------------------------------------------------------------------

void RMGLayout::build( const RandomMapSettings& settings, Bool skeleton )
{
	/* The height field is x87 arithmetic, so it comes out differently at 53-bit
		precision than at the 24 bits the simulation runs in - the same trap
		GameLogic::update re-asserts the control word for at the top of every logic
		frame. A map generated in whatever mode the last DLL left behind is a map
		the other machine does not have, so put the FPU where the simulation keeps
		it and give the caller back what it had. */
	UnsignedInt callersFPMode = getFPMode();
	setFPMode();

	m_settings = settings;
	RandomMapGenerator::clampSettings( m_settings );
	m_aodFamily = -1;
	m_aodSkipped = 0;
	m_lockCarves = FALSE;
	m_padsKeepOffWays = FALSE;
	m_wayDistance.clear();
	m_playable = FALSE;
	m_roads.clear();
	m_roadCarved.clear();
	m_carveKeep = NULL;

	if( m_settings.m_style == RMG_STYLE_LANE )
	{
		buildLane();
		restoreFPMode( callersFPMode );
		return;
	}
	if( m_settings.m_style == RMG_STYLE_APOCALYPSE )
	{
		buildApocalypse();
		restoreFPMode( callersFPMode );
		return;
	}
	if( m_settings.m_style == RMG_STYLE_AOD )
	{
		buildAod();
		restoreFPMode( callersFPMode );
		return;
	}

	/* The seed picks what kind of map this is and what it is made of, apart from each other, so a
		river can run through snow as well as through grass. Neither depends on the player count:
		the same seed is the same kind of map for two players and for eight. Over the first thousand
		seeds each of the seven kinds comes up between 131 and 159 times. */
	m_type = &theMapTypes[hashCell( m_settings.m_seed, 9001, 8 ) % (UnsignedInt)RMG_MAP_TYPE_COUNT];
	m_biome = &theBiomes[rmgBiomeFor( m_settings.m_seed )];
	m_timeOfDay = rmgTimeOfDayFor( m_settings.m_seed );
	m_snowy = rmgSnowsOn( m_settings.m_seed );

	// A style fixes what the seed would have picked; the seed still lays out the ground.
	m_style = NULL;
	m_styleScale = 1.0f;
	if( m_settings.m_style > 0 )
	{
		m_style = &theStyles[m_settings.m_style - 1];
		m_type = &theBowlType;
		m_biome = &m_style->m_biome;
		m_timeOfDay = m_style->m_timeOfDay;
		m_snowy = FALSE;
		m_styleScale = (Real)m_settings.m_playableCells / (Real)rmgStyleSmallestCells( *m_style );
		if( m_styleScale > 1.0f ) m_styleScale = 1.0f;
		if( m_styleScale < 0.6f ) m_styleScale = 0.6f;
	}

	// a template's own rivers, lakes, gorges and woods replace whatever the seed would have rolled
	m_skeleton = skeleton;
	if( m_skeleton && !m_style )
		m_type = &theMapTypes[RMG_MAP_PLAINS];
	// night hides a massif's relief and a template's copied ground; both play by afternoon instead
	if( m_timeOfDay == TIME_OF_DAY_NIGHT && ( m_skeleton || m_type == &theMapTypes[RMG_MAP_MASSIF] ) )
		m_timeOfDay = TIME_OF_DAY_AFTERNOON;

	m_width = m_settings.m_playableCells + 2 * RMG_BORDER_CELLS;
	m_height = m_width;
	m_waterHeight = RMG_BASE_HEIGHT - RMG_WATER_DROP;
	m_startSearchStride = RMG_START_STRIDE;

	seedPermutation( m_settings.m_seed, m_perm );
	const UnsignedByte *perm = m_perm;

	if( m_style )
		placeBowlStarts();
	planMassif();
	buildHeights( perm );
	placeLakes( perm );
	m_heights.swap( m_landHeights );
	m_landHeights.clear();
	buildLakeMask();
	std::vector<UnsignedByte> uncarved;
	if( m_type->m_river )
		uncarved = m_heights;
	carveLakeBasins();

	if( !m_style )
		chooseStarts();
	m_baseLevelled.clear();
	openFordsAtStarts( uncarved );
	flattenBases();

	m_ramps.clear();
	connectStarts();
	if( !m_massifLobes.empty() )
		carveMassifRamps();
	if( !m_style && !m_skeleton )
		settleSlopes();
	// the bowl's ramps, so no rock is set on one
	for( UnsignedInt i = 0; m_style && i < m_starts.size(); i++ )
	{
		RMGPoint ramp;
		Real out = m_seatRims[i] - 0.5f * m_bowlRampLength;
		ramp.m_cellX = (Real)m_settings.m_playableCells * 0.5f + m_seatDirections[i].m_cellX * out;
		ramp.m_cellY = (Real)m_settings.m_playableCells * 0.5f + m_seatDirections[i].m_cellY * out;
		m_ramps.push_back( ramp );
	}

	/* Objects before textures, because placing them changes the ground: every dock, derrick,
		bunker and town levels a pad under itself, and a pad moves the cliffs and the shore lines
		that the passability and the texture classes are read from. Pads can also wall a dock or
		close the second way out of a base, so playability is repaired on the finished height
		field rather than by picking a different seed.

		The money goes down first and the repairs right after it, before anything that blocks a
		unit: a repair cut under a town or a wood used to leave a building or a rock standing in the
		corridor it had just opened. Each repair locks its roadway, which the towns, bunkers and
		scenery after keep off. Their pads keep off it too, but can still close something else, so the
		ground is walked once more at the end; a repair that has to run then clears whatever stands
		on its roadway. */
	placeMoney();
	m_lockCarves = TRUE;
	ensurePlayability();
	flattenBases();
	// After the last levelling: levelling again would ease a cut ramp back into the base's ring.
	holdMoneyWalks();
	placeDressing( perm );
	if( !everyoneReaches() )
	{
		// a cut can run into a base's ring, which is levelled again as after the first repair; the
		// check below says whether the levelling closed what the cut opened
		ensurePlayability();
		flattenBases();
		// the levelling can ease a money walk's ramp or a contested dock's lead away again
		holdMoneyWalks();
		clearLockedGround();
	}
	/* The pads, the bases' last levelling and the repairs leave steep rings of their own, so a plain
		map is settled once more on the finished ground; a cell it made a cliff can close a way, which
		the same repairs as above open again. */
	if( !m_style && !m_skeleton )
	{
		relayRoads();
		settleSlopes();
		if( !everyoneReaches() )
		{
			ensurePlayability();
			holdMoneyWalks();
			clearLockedGround();
		}
		drawCarvedWays();
	}
	m_lockCarves = FALSE;
	// a seat, a start or any money still cut off: built anyway, flagged and logged
	m_playable = everyoneReaches();
	if( !m_playable )
		DEBUG_LOG(( "random map: seed %d, %d players, %d cells, style %d - a start or its money is cut off, or a seat is wet\n",
			m_settings.m_seed, m_settings.m_numPlayers, m_settings.m_playableCells, m_settings.m_style ));

	buildTerrainClasses( perm );
	buildBlends();

	restoreFPMode( callersFPMode );
}

//-----------------------------------------------------------------------------
// Map pieces
//-----------------------------------------------------------------------------

/// One waypoint object. The reader calls anything with a waypointID a waypoint.
static void writeWaypoint( MapChunkWriter& w, const RMGObject& object )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( object.m_worldX );
		w.writeReal( object.m_worldY );
		w.writeReal( 0.0f );
		w.writeReal( 0.0f );
		w.writeInt( 0 );
		w.writeAsciiString( object.m_templateName.str() );

		const Bool onPath = !object.m_pathLabel.isEmpty();
		w.beginDict( onPath ? 13 : 12 );
		w.dictInt( "objectInitialHealth", 100 );
		w.dictBool( "objectEnabled", TRUE );
		w.dictBool( "objectPowered", TRUE );
		w.dictBool( "objectRecruitableAI", TRUE );
		w.dictAsciiString( "originalOwner", "team" );
		w.dictAsciiString( "uniqueID", object.m_uniqueID.str() );
		w.dictAsciiString( "objectLayer", "Start Points" );
		w.dictInt( "waypointID", object.m_waypointID );
		w.dictBool( "objectDestructible", TRUE );
		w.dictBool( "objectSellable", TRUE );
		w.dictBool( "objectRepairable", TRUE );
		w.dictAsciiString( "waypointName", object.m_uniqueID.str() );
		if( onPath )
			w.dictAsciiString( "waypointPathLabel1", object.m_pathLabel.str() );
	w.closeChunk();
}

/** A road point. It is not an object the game builds: the renderer walks the map object list, and
	a pair of these with the road flags on them becomes a road segment of whatever type the name
	says. The dictionary is what every map object carries, minus everything that only means
	something to a thing that exists in the world. */
static void writeRoadPoint( MapChunkWriter& w, const RMGObject& object )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( object.m_worldX );
		w.writeReal( object.m_worldY );
		w.writeReal( 0.0f );
		w.writeReal( 0.0f );
		w.writeInt( object.m_flags );
		w.writeAsciiString( object.m_templateName.str() );

		w.beginDict( 1 );
		w.dictAsciiString( "objectLayer", "" );
	w.closeChunk();
}

static void writeNeutralObject( MapChunkWriter& w, const RMGObject& object )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( object.m_worldX );
		w.writeReal( object.m_worldY );
		w.writeReal( 0.0f );
		w.writeReal( object.m_angle );
		w.writeInt( object.m_flags );
		w.writeAsciiString( object.m_templateName.str() );

		w.beginDict( 11 );
		w.dictInt( "objectInitialHealth", 100 );
		w.dictBool( "objectEnabled", TRUE );
		w.dictBool( "objectIndestructible", FALSE );
		w.dictBool( "objectUnsellable", !object.m_owner.isEmpty() );	// a seat's fortress is not its to sell
		w.dictBool( "objectPowered", TRUE );
		w.dictBool( "objectRecruitableAI", TRUE );
		w.dictAsciiString( "objectName", object.m_objectName.str() );
		w.dictAsciiString( "originalOwner", object.m_owner.isEmpty() ? "team" : object.m_owner.str() );
		w.dictAsciiString( "uniqueID", object.m_uniqueID.str() );
		w.dictAsciiString( "objectLayer", "" );
		w.dictBool( "objectSelectable", TRUE );
	w.closeChunk();
}

/** The sides every skirmish map carries: the civilians that own map scenery,
	and one side per playable faction for the skirmish scripts to attach to. */
static const char *theSkirmishSides[][2] =
{
	{ "PlyrCivilian",						"FactionCivilian" },
	{ "SkirmishAmerica",					"FactionAmerica" },
	{ "SkirmishChina",						"FactionChina" },
	{ "SkirmishGLA",						"FactionGLA" },
	{ "SkirmishAmericaAirForceGeneral",		"FactionAmericaAirForceGeneral" },
	{ "SkirmishAmericaLaserGeneral",		"FactionAmericaLaserGeneral" },
	{ "SkirmishAmericaSuperWeaponGeneral",	"FactionAmericaSuperWeaponGeneral" },
	{ "SkirmishChinaTankGeneral",			"FactionChinaTankGeneral" },
	{ "SkirmishChinaNukeGeneral",			"FactionChinaNukeGeneral" },
	{ "SkirmishChinaInfantryGeneral",		"FactionChinaInfantryGeneral" },
	{ "SkirmishGLADemolitionGeneral",		"FactionGLADemolitionGeneral" },
	{ "SkirmishGLAToxinGeneral",			"FactionGLAToxinGeneral" },
	{ "SkirmishGLAStealthGeneral",			"FactionGLAStealthGeneral" },
};
static const Int theNumSkirmishSides = sizeof(theSkirmishSides) / sizeof(theSkirmishSides[0]);

static void writeSides( MapChunkWriter& w )
{
	Int numSides = theNumSkirmishSides + 1;		// plus neutral
	Int i;

	w.openChunk( "SidesList", K_SIDES_DATA_VERSION_3 );
		w.writeInt( numSides );

		// Neutral, the side that owns the map itself.
		w.beginDict( 6 );
		w.dictAsciiString( "playerName", "" );
		w.dictBool( "playerIsHuman", FALSE );
		w.dictAsciiString( "playerDisplayName", "Neutral" );
		w.dictAsciiString( "playerFaction", "" );
		w.dictAsciiString( "playerAllies", "" );
		w.dictAsciiString( "playerEnemies", "" );
		w.writeInt( 0 );	// empty build list

		for( i = 0; i < theNumSkirmishSides; i++ )
		{
			w.beginDict( 6 );
			w.dictAsciiString( "playerName", theSkirmishSides[i][0] );
			w.dictBool( "playerIsHuman", FALSE );
			w.dictAsciiString( "playerDisplayName", theSkirmishSides[i][0] );
			w.dictAsciiString( "playerFaction", theSkirmishSides[i][1] );
			w.dictAsciiString( "playerAllies", "" );
			w.dictAsciiString( "playerEnemies", "" );
			w.writeInt( 0 );	// empty build list
		}

		// One default team per side.
		w.writeInt( numSides );

		w.beginDict( 3 );
		w.dictAsciiString( "teamName", "team" );
		w.dictAsciiString( "teamOwner", "" );
		w.dictBool( "teamIsSingleton", TRUE );

		for( i = 0; i < theNumSkirmishSides; i++ )
		{
			AsciiString teamName;
			teamName.format( "team%s", theSkirmishSides[i][0] );

			w.beginDict( 3 );
			w.dictAsciiString( "teamName", teamName.str() );
			w.dictAsciiString( "teamOwner", theSkirmishSides[i][0] );
			w.dictBool( "teamIsSingleton", TRUE );
		}

		// No scripts: skirmish and multiplayer games fall back to the shipped
		// SkirmishScripts.scb / MultiplayerScripts.scb.
		w.openChunk( "PlayerScriptsList", K_SCRIPTS_DATA_VERSION_1 );
			for( i = 0; i < numSides; i++ )
			{
				w.openChunk( "ScriptList", K_SCRIPT_LIST_DATA_VERSION_1 );
				w.closeChunk();
			}
		w.closeChunk();
	w.closeChunk();
}

/** The skirmish scripts' base areas, in cells: InnerPerimeterN round start N is "my base", and a
	wave only leaves while it stands inside OuterPerimeterN. Neither is ever bigger than a share of the
	way to the nearest other start, so two bases' areas never overlap. */
static const Real RMG_INNER_PERIMETER_CELLS = 30.0f;
static const Real RMG_OUTER_PERIMETER_CELLS = 55.0f;
static const Real RMG_OUTER_PERIMETER_SHARE = 0.45f;
static const Real RMG_INNER_OF_OUTER = 0.6f;
static const Int RMG_PERIMETER_SIDES = 16;

/** Every trigger area the map has: one water area per basin, and the two base areas round every
	start. Point zero of a water area carries the water height for the whole area, which is what
	isUnderwater compares the ground against. The base areas are what an Easy or Medium attack
	waits on: "[Skirmish]MyOuterPerimeter" resolves to OuterPerimeterN, a generated map had none,
	and the scripts' launch condition was never true. */
static void writePolygonTriggers( MapChunkWriter& w, const RMGLayout& layout )
{
	// The polygon is the contour of the basin that was carved, so the engine floods the same
	// shape the height field holds.
	Int written = 0;
	for( UnsignedInt i = 0; i < layout.m_lakes.size(); i++ )
	{
		if( (Int)layout.m_lakes[i].m_polygon.size() >= 3 )
			written++;
	}
	written += 2 * (Int)layout.m_starts.size() + (Int)layout.m_areas.size();

	w.openChunk( "PolygonTriggers", K_TRIGGERS_VERSION_4 );
		w.writeInt( written );

		Int id = 1;
		for( UnsignedInt i = 0; i < layout.m_lakes.size(); i++ )
		{
			const std::vector<RMGPoint>& polygon = layout.m_lakes[i].m_polygon;
			Int numSides = (Int)polygon.size();
			if( numSides < 3 )
				continue;

			AsciiString name;
			name.format( "Lake%d", id );

			w.writeAsciiString( name.str() );
			w.writeAsciiString( "" );			// layer
			w.writeInt( id );					// trigger id
			w.writeByte( 1 );					// is a water area
			w.writeByte( 0 );					// not a river
			w.writeInt( 0 );					// river start
			w.writeInt( numSides );

			Int waterZ = (Int)(layout.m_waterHeight * MAP_HEIGHT_SCALE + 0.5f);

			for( Int point = 0; point < numSides; point++ )
			{
				w.writeInt( (Int)(polygon[point].m_cellX * MAP_XY_FACTOR + 0.5f) );
				w.writeInt( (Int)(polygon[point].m_cellY * MAP_XY_FACTOR + 0.5f) );
				w.writeInt( waterZ );
			}

			id++;
		}

		for( UnsignedInt start = 0; start < layout.m_starts.size(); start++ )
		{
			const RMGPoint& centre = layout.m_starts[start];
			Real nearest = FLT_MAX;
			for( UnsignedInt other = 0; other < layout.m_starts.size(); other++ )
			{
				if( other == start )
					continue;
				const Real dx = layout.m_starts[other].m_cellX - centre.m_cellX;
				const Real dy = layout.m_starts[other].m_cellY - centre.m_cellY;
				nearest = min( nearest, (Real)sqrt( dx * dx + dy * dy ) );
			}
			const Real outer = min( RMG_OUTER_PERIMETER_CELLS, nearest * RMG_OUTER_PERIMETER_SHARE );
			const Real inner = min( RMG_INNER_PERIMETER_CELLS, outer * RMG_INNER_OF_OUTER );
			for( Int ring = 0; ring < 2; ring++ )
			{
				AsciiString name;
				name.format( "%s%d", ring == 0 ? "InnerPerimeter" : "OuterPerimeter", start + 1 );
				const Real radius = (ring == 0) ? inner : outer;

				w.writeAsciiString( name.str() );
				w.writeAsciiString( "" );			// layer
				w.writeInt( id++ );					// trigger id
				w.writeByte( 0 );					// not water
				w.writeByte( 0 );					// not a river
				w.writeInt( 0 );					// river start
				w.writeInt( RMG_PERIMETER_SIDES );
				for( Int point = 0; point < RMG_PERIMETER_SIDES; point++ )
				{
					const Real angle = 2.0f * PI * point / RMG_PERIMETER_SIDES;
					w.writeInt( (Int)((centre.m_cellX + radius * Cos( angle )) * MAP_XY_FACTOR + 0.5f) );
					w.writeInt( (Int)((centre.m_cellY + radius * Sin( angle )) * MAP_XY_FACTOR + 0.5f) );
					w.writeInt( 0 );
				}
			}
		}

		for( UnsignedInt i = 0; i < layout.m_areas.size(); i++ )
		{
			const RMGArea& area = layout.m_areas[i];
			w.writeAsciiString( area.m_name.str() );
			w.writeAsciiString( "" );			// layer
			w.writeInt( id++ );					// trigger id
			w.writeByte( 0 );					// not water
			w.writeByte( 0 );					// not a river
			w.writeInt( 0 );					// river start
			w.writeInt( (Int)area.m_polygon.size() );
			for( UnsignedInt point = 0; point < area.m_polygon.size(); point++ )
			{
				w.writeInt( (Int)(area.m_polygon[point].m_cellX * MAP_XY_FACTOR + 0.5f) );
				w.writeInt( (Int)(area.m_polygon[point].m_cellY * MAP_XY_FACTOR + 0.5f) );
				w.writeInt( 0 );
			}
		}
	w.closeChunk();
}

/** How each hour is lit, as a tint over the biome's afternoon so a desert dusk stays a desert's and
	a winter night keeps the snow's blue. Afternoon is the biome as it is. Morning and evening put
	the sun low on opposite sides, evening warmer and dimmer; night is the moon, high and blue, at
	about the level of the shipped night maps (Dark Night's ambient is 0.09 0.16 0.50, its diffuse
	0.20 0.24 0.37), so the ground and the units still read. */
struct RMGHourLight
{
	Real m_ambient[3];		///< times the biome's ambient
	Real m_diffuse[3];		///< times the biome's diffuse
	Real m_sun[3];			///< the light's direction; afternoon keeps the biome's
	Bool m_biomeSun;
};

static const RMGHourLight theHourLights[4] =
{
	{ { 1.10f, 1.00f, 0.88f }, { 1.05f, 0.90f, 0.78f }, { -0.90f, 0.20f, -0.38f }, FALSE },	// morning
	{ { 1.00f, 1.00f, 1.00f }, { 1.00f, 1.00f, 1.00f }, { 0.0f, 0.0f, -1.0f }, TRUE },		// afternoon
	{ { 0.85f, 0.68f, 0.58f }, { 1.05f, 0.72f, 0.48f }, { 0.86f, -0.30f, -0.42f }, FALSE },	// evening
	{ { 0.34f, 0.42f, 0.80f }, { 0.34f, 0.40f, 0.58f }, { -0.43f, 0.43f, -0.80f }, FALSE },	// night
};

/** The twilight lane's evening, which is dusk rather than the golden hour above: the sun just down,
	a dim violet light from low in the west and a blue sky filling the shadows, so the style's warm
	sand reads as evening and not as late afternoon. */
static const RMGHourLight theLaneDusk =
	{ { 0.80f, 0.92f, 1.70f }, { 0.60f, 0.56f, 0.78f }, { 0.86f, -0.30f, -0.42f }, FALSE };

static void writeTintedLight( MapChunkWriter& w, const Real *ambient, const Real *diffuse,
															const RMGHourLight& hour, const Real *biomeSun )
{
	Int channel;
	for( channel = 0; channel < 3; channel++ )
		w.writeReal( ambient[channel] * hour.m_ambient[channel] );
	for( channel = 0; channel < 3; channel++ )
		w.writeReal( diffuse[channel] * hour.m_diffuse[channel] );		// the brightest biome stays under 1
	for( channel = 0; channel < 3; channel++ )
		w.writeReal( hour.m_biomeSun ? biomeSun[channel] : hour.m_sun[channel] );
}

/** The light. A map with no lighting chunk keeps whatever GameData.ini left in GlobalData, which is
	the dusk the shipped maps all override, so a generated map looked like somebody had turned the
	sun off. All four hours are written and the seed's is the current one; the reader takes the
	current hour's set, and the buildings pick their night or snow models from the hour and the
	WorldInfo weather. */
static void writeGlobalLighting( MapChunkWriter& w, const RMGBiome& biome, TimeOfDay current )
{
	// the twilight lane's own palette (rmgLaneBiome) is lit at dusk
	const Bool dusk = &biome == rmgLaneBiome( RMG_BIOME_COUNT );
	w.openChunk( "GlobalLighting", K_LIGHTING_VERSION_3 );
		w.writeInt( current );

		Int timeOfDay, light, channel;
		for( timeOfDay = 0; timeOfDay < 4; timeOfDay++ )
		{
			const RMGHourLight& hour = dusk && timeOfDay == TIME_OF_DAY_EVENING - TIME_OF_DAY_FIRST ? theLaneDusk : theHourLights[timeOfDay];
			writeTintedLight( w, biome.m_terrainAmbient, biome.m_terrainDiffuse, hour, biome.m_sunDirection );
			writeTintedLight( w, biome.m_objectAmbient, biome.m_objectDiffuse, hour, biome.m_sunDirection );

			// The two extra lights of version 3, dark but pointing somewhere valid: one sun is
			// what this map wants, and a light with no direction at all upsets the shaders.
			for( light = 1; light < MAX_GLOBAL_LIGHTS; light++ )
			{
				for( channel = 0; channel < 6; channel++ )
					w.writeReal( 0.0f );
				w.writeReal( 0.0f );
				w.writeReal( 0.0f );
				w.writeReal( -1.0f );
			}
			for( light = 1; light < MAX_GLOBAL_LIGHTS; light++ )
			{
				for( channel = 0; channel < 6; channel++ )
					w.writeReal( 0.0f );
				w.writeReal( 0.0f );
				w.writeReal( 0.0f );
				w.writeReal( -1.0f );
			}
		}
	w.closeChunk();
}

//-----------------------------------------------------------------------------
// RandomMapGenerator
//-----------------------------------------------------------------------------

Int RandomMapGenerator::cellsFor( RandomMapSize size, Int numPlayers )
{
	if( numPlayers < MIN_PLAYERS ) numPlayers = MIN_PLAYERS;
	if( numPlayers > MAX_PLAYERS ) numPlayers = MAX_PLAYERS;

	// the table stops at eight; past it the area grows as players / 8, so every player keeps the
	// ground a seat has on an eight-player map of this size
	const Int tablePlayers = numPlayers < DEFAULT_PLAYERS ? numPlayers : DEFAULT_PLAYERS;
	Int cells;
	switch( size )
	{
		case RANDOM_MAP_SIZE_TINY:
			cells = RMG_TINY_FLOOR + RMG_TINY_PER_PLAYER * tablePlayers;
			break;
		case RANDOM_MAP_SIZE_SMALL:
			cells = RMG_SMALL_FLOOR + RMG_SMALL_PER_PLAYER * tablePlayers;
			break;
		case RANDOM_MAP_SIZE_LARGE:
			cells = RMG_LARGE_FLOOR + RMG_LARGE_PER_PLAYER * tablePlayers;
			break;
		case RANDOM_MAP_SIZE_HUGE:
			cells = RMG_HUGE_FLOOR + RMG_HUGE_PER_PLAYER * tablePlayers;
			break;
		default:
			cells = RMG_NORMAL_FLOOR + RMG_NORMAL_PER_PLAYER * tablePlayers;
			break;
	}
	if( numPlayers > DEFAULT_PLAYERS )
		cells = (Int)sqrt( (double)cells * cells * numPlayers / DEFAULT_PLAYERS );

	if( cells < MIN_CELLS ) cells = MIN_CELLS;
	if( cells > maxCellsFor( numPlayers ) ) cells = maxCellsFor( numPlayers );
	return cells;
}

RandomMapSettings RandomMapGenerator::quickMatchSettings( Int matchSeed, Int numPlayers )
{
	RandomMapSettings settings;
	// non-negative, as the lobby's random tab makes them, so the name has the shape every other has
	settings.m_seed = matchSeed & 0x7fffffff;
	settings.m_numPlayers = numPlayers;
	settings.m_playableCells = cellsFor( RANDOM_MAP_SIZE_NORMAL, numPlayers );
	return settings;
}

Int RandomMapGenerator::cellsForTemplate( RandomMapSize size, Int numPlayers, Int cellsX, Int cellsY, Int seats )
{
	// area against Medium, in percent: Extra Large for eight on Twilight Flame is 708 across
	static const Int AREA_PERCENT[RANDOM_MAP_SIZE_COUNT] = { 45, 70, 100, 140, 220 };
	const Int PERCENT = 100;

	if( numPlayers < MIN_PLAYERS ) numPlayers = MIN_PLAYERS;
	if( numPlayers > MAX_PLAYERS ) numPlayers = MAX_PLAYERS;
	// Medium is the template's own ground for as many players as it seats, the room they have on it;
	// only players past its seats add a seat's share each
	const Int seatsFilled = std::max( numPlayers, seats );
	double area = (double)cellsX * cellsY * seatsFilled * AREA_PERCENT[size] / ( (double)seats * PERCENT );
	Int cells = (Int)sqrt( area );
	if( cells < MIN_CELLS ) cells = MIN_CELLS;
	if( cells > maxCellsFor( numPlayers ) ) cells = maxCellsFor( numPlayers );
	return cells;
}

Int RandomMapGenerator::cellsFor( RandomMapSize size, Int numPlayers, const AsciiString& templatePath )
{
	Int cellsX, cellsY, seats;
	if( templatePath.isEmpty() || !rmgTemplateShape( templatePath, &cellsX, &cellsY, &seats ) )
		return cellsFor( size, numPlayers );
	return cellsForTemplate( size, numPlayers, cellsX, cellsY, seats );
}

static Bool rmgTemplateFullCRC( const AsciiString& templatePath, UnsignedInt *crcOut );

/** A template map from settings whose CRC is the full one (rmgTemplateFullCRC): checked against this
	machine's template and rules, then handed to the synthesis with the CRC of the .map alone, which is
	the one rmgGenerateFromTemplate checks the bytes it reads against. Nothing on a mismatch. */
static void rmgGenerateFromTemplateChecked( const RandomMapSettings& clamped, std::vector<char>& mapBytes,
	std::vector<char> *previewBytes )
{
	RandomMapSettings bare = clamped;
	UnsignedInt full = 0;
	if( !rmgTemplateFullCRC( clamped.m_template, &full ) || full != clamped.m_templateCRC
		|| !rmgTemplateCRC( clamped.m_template, &bare.m_templateCRC ) )
	{
		DEBUG_LOG(( "random map: template '%s' with rules CRC %08X is not installed here\n", clamped.m_template.str(),
			clamped.m_templateCRC ));
		mapBytes.clear();
		if( previewBytes )
			previewBytes->clear();
		return;
	}
	rmgGenerateFromTemplate( bare, mapBytes, previewBytes );
}

void RandomMapGenerator::clampSettings( RandomMapSettings& settings )
{
	const Int askedPlayers = settings.m_numPlayers;	// an Art of Defense map takes one seat, every other at least two
	if( settings.m_numPlayers < MIN_PLAYERS ) settings.m_numPlayers = MIN_PLAYERS;
	if( settings.m_numPlayers > MAX_PLAYERS ) settings.m_numPlayers = MAX_PLAYERS;

	if( settings.m_style < 0 || settings.m_style > RMG_STYLE_AOD )
		settings.m_style = 0;

	// one ground for the seed and the seats: the size asked for is not taken
	if( settings.m_style == RMG_STYLE_AOD )
	{
		settings.m_numPlayers = std::max( 1, std::min( askedPlayers, (Int)RMG_AOD_SEATS_MOST ) );
		settings.m_template.clear();
		settings.m_templateCRC = 0;
		settings.m_playableCells = rmgAodShape( settings.m_seed, settings.m_numPlayers ).m_cells;
		return;
	}

	// the ground grows with the seats: never smaller than the walled clearing and a belt round it
	if( settings.m_style == RMG_STYLE_APOCALYPSE )
	{
		if( settings.m_numPlayers > RMG_APOC_SEATS_MOST )
			settings.m_numPlayers = RMG_APOC_SEATS_MOST;
		settings.m_template.clear();
		settings.m_templateCRC = 0;
		// always the generator's biggest normal map, a thirty-two seat one, whatever the seats: the room
		// past the basin is wilderness between the heart and the fog. A size asked for is not taken.
		settings.m_playableCells = cellsFor( RANDOM_MAP_SIZE_NORMAL, MAX_PLAYERS );
		return;
	}

	// one ground whatever the size or seat count asked for; the seed picks its look
	if( settings.m_style == RMG_STYLE_LANE )
	{
		settings.m_numPlayers = RMG_LANE_SEATS;
		settings.m_playableCells = rmgLaneShape().m_playX;
		settings.m_template.clear();
		settings.m_templateCRC = 0;
		return;
	}

	/* A template replaces the style. It has to be a shipped map's "maps\<name>\<name>.map", the
		shape the map's name can carry, and installed here; the CRC comes off the installed file
		unless the settings already carry one, which a name read back does, so a machine holding
		other bytes under that path keeps the name's CRC and builds nothing rather than another map. */
	if( !settings.m_template.isEmpty() )
	{
		settings.m_template.toLower();
		AsciiString token;
		UnsignedInt installed = 0;
		if( !rmgTemplateToken( settings.m_template, token ) || !rmgTemplateFullCRC( settings.m_template, &installed ) )
		{
			settings.m_template.clear();
			settings.m_templateCRC = 0;
		}
		else
		{
			settings.m_style = 0;
			if( settings.m_templateCRC == 0 )
				settings.m_templateCRC = installed;
		}
	}
	else
		settings.m_templateCRC = 0;

	/* Nought means "whatever this many players need". A style's eight-player map is the shipped
		map's own size and the other counts scale off it the way the normal size does. */
	if( settings.m_playableCells <= 0 )
	{
		settings.m_playableCells = cellsFor( RANDOM_MAP_SIZE_NORMAL, settings.m_numPlayers, settings.m_template );
		if( settings.m_style > 0 )
		{
			const RMGStyle& style = theStyles[settings.m_style - 1];
			settings.m_playableCells = settings.m_playableCells * style.m_cells / cellsFor( RANDOM_MAP_SIZE_NORMAL, DEFAULT_PLAYERS );
			if( settings.m_playableCells < rmgStyleSmallestCells( style ) )
				settings.m_playableCells = rmgStyleSmallestCells( style );
		}
	}

	/* A typed cell count or a name read back is held to the least the bowl can shrink to, 0.6 of its
		full size (build's m_styleScale floor): under it the ring, the rim and the ramp still came out at
		0.6, so "-randommap 1 8 64 twilight" was one plateau over the whole map, starts five cells apart. */
	if( settings.m_style > 0 )
	{
		const Int least = ( rmgStyleSmallestCells( theStyles[settings.m_style - 1] ) * 3 + 4 ) / 5;
		if( settings.m_playableCells < least )
			settings.m_playableCells = least;
	}

	/* A template's smallest size is the least ground its seats can be spread over. The size buttons
		never go under it, but a typed cell count or a name read back can, and eight seats on a 64-cell
		field came out as eight pads in a heap with a third of the objects outside play. */
	if( !settings.m_template.isEmpty() )
	{
		Int tiny = cellsFor( RANDOM_MAP_SIZE_TINY, settings.m_numPlayers, settings.m_template );
		if( settings.m_playableCells < tiny )
			settings.m_playableCells = tiny;
	}

	if( settings.m_playableCells < MIN_CELLS ) settings.m_playableCells = MIN_CELLS;
	if( settings.m_playableCells > maxCellsFor( settings.m_numPlayers ) ) settings.m_playableCells = maxCellsFor( settings.m_numPlayers );
}

Int RandomMapGenerator::styleNamed( const char *name )
{
	for( Int style = 1; style <= RMG_STYLE_AOD; style++ )
	{
		if( AsciiString( name ).compareNoCase( rmgStyleName( style ) ) == 0 )
			return style;
	}
	return 0;
}

/* The planar tiling lays a class's sheet flat over the ground, two cells a tile, so a face that rises
	g bytes a cell shows the same 32 texels over sqrt(100 + (0.625 g)^2) world units: the twilight
	style's rim, 29 bytes a cell at the median, drew its rock half as sharp along the fall as across
	it, and the template's pads and canyons are steeper still. WorldHeightMap::getUVForTileIndex takes
	a cell's coordinates out of its cliff entry instead, when it has one of its own class, so the faces
	get one here the way WorldBuilder's doCliffAdjustment bakes them.

	V is the height, as WorldBuilder has it: a byte is 1/1024 of TEXTURE_WIDTH, which is the true
	length of a steep face and does not depend on which way it falls. Every corner a face's cells share
	takes the same V, so a face is one piece of texture with no seam inside it. A cell's corners have
	to lie inside its class's block of the terrain texture, 128 bytes tall for a sheet four tiles
	across, and the sheet tiles, so V starts the block again every block's height. WorldBuilder centred
	a face in one block and squeezed a taller one; the rim and ridge of a twilight bowl climb 166 bytes,
	and squeezed to 128 their gentler cells came out worse than the plane. A cell that a restart height
	runs through cannot hold both sides of it, so each face (an 8-connected patch of mapped cells of
	one class) restarts at the heights where fewest of its cells cross, which is mostly the flat of a
	plateau where it has none, and a cell that still crosses keeps the plane.

	U runs across the fall: along y on a cell that falls more along x, along x otherwise, a whole sheet
	period behind the cell so the four corners stay inside the block. That is the planar U itself on a
	cell falling along y, so its seam with the flat ground beside it is in V alone.

	Height alone gives an edge that does not rise no texture at all, so a cell takes the mapping only
	when both its edges down the fall rise more than WorldBuilder's limit: then no edge down the fall
	is drawn more than 1.6 times stretched, where the plane drew the twilight rim at 2 and more, and
	none across it worse than the plane. WorldBuilder maps on any one steep edge and nudges flat
	corners up afterwards (minHeightDelta), which seams a face; a rim's top and foot cells keep the
	plane here instead. A cell whose class is no cliff keeps the planar tiling too, and so do the
	blends of other classes drawn over a mapped cell, which the renderer matches to the entry's class. */
void rmgCliffMapping( const std::vector<Int>& heights, Int W, Int H, const std::vector<Int>& sheetFirst,
	const std::vector<Int>& sheetWidth, std::vector<Short>& indexOut, std::vector<RMGCliffInfo>& infoOut )
{
	indexOut.assign( W * H, 0 );
	infoOut.assign( 1, RMGCliffInfo() );

	// the cells that rise down both edges along their fall, and their lowest and highest corner
	std::vector<Int> lowest( W * H, -1 ), highest( W * H, -1 );
	for( Int y = 0; y + 1 < H; y++ )
	{
		for( Int x = 0; x + 1 < W; x++ )
		{
			const Int i = y * W + x;
			if( sheetFirst[i] < 0 || sheetWidth[i] < 1 )
				continue;
			const Int h[4] = { heights[i], heights[i + 1], heights[i + W + 1], heights[i + W] };
			const Int alongX = std::min( abs( h[0] - h[1] ), abs( h[3] - h[2] ) );
			const Int alongY = std::min( abs( h[0] - h[3] ), abs( h[1] - h[2] ) );
			const Bool fallsAlongX = std::max( abs( h[0] - h[1] ), abs( h[3] - h[2] ) ) >= std::max( abs( h[0] - h[3] ), abs( h[1] - h[2] ) );
			if( ( fallsAlongX ? alongX : alongY ) <= RMG_CLIFF_UV_RISE )
				continue;
			lowest[i] = std::min( std::min( h[0], h[1] ), std::min( h[2], h[3] ) );
			highest[i] = std::max( std::max( h[0], h[1] ), std::max( h[2], h[3] ) );
		}
	}

	// the faces, flooded in raster order, and the cells of each
	std::vector<Int> face( W * H, -1 ), faceStart, faceCells, stack;
	for( Int seed = 0; seed < W * H; seed++ )
	{
		if( lowest[seed] < 0 || face[seed] >= 0 )
			continue;
		const Int f = (Int)faceStart.size();
		faceStart.push_back( (Int)faceCells.size() );
		face[seed] = f;
		stack.push_back( seed );
		while( !stack.empty() )
		{
			const Int i = stack.back(), x = i % W, y = i / W;
			stack.pop_back();
			faceCells.push_back( i );
			for( Int dy = -1; dy <= 1; dy++ )
			{
				for( Int dx = -1; dx <= 1; dx++ )
				{
					const Int nx = x + dx, ny = y + dy, n = ny * W + nx;
					if( nx < 0 || ny < 0 || nx >= W || ny >= H || lowest[n] < 0 || face[n] >= 0 || sheetFirst[n] != sheetFirst[seed] )
						continue;
					face[n] = f;
					stack.push_back( n );
				}
			}
		}
	}
	faceStart.push_back( (Int)faceCells.size() );

	// each face's restart: of the heights a block apart, the lowest phase the fewest of its cells
	// cross; a cell crosses one strictly between its lowest and highest corner
	std::vector<Int> phase( faceStart.size() - 1, 0 ), crossing;
	for( Int f = 0; f + 1 < (Int)faceStart.size(); f++ )
	{
		const Int sheetBytes = sheetWidth[faceCells[faceStart[f]]] * RMG_CLIFF_UV_CELL * 2 / RMG_CLIFF_UV_BYTE;
		crossing.assign( sheetBytes, 0 );
		for( Int c = faceStart[f]; c < faceStart[f + 1]; c++ )
			for( Int t = lowest[faceCells[c]] + 1; t < highest[faceCells[c]]; t++ )
				crossing[t % sheetBytes]++;
		for( Int p = 1; p < sheetBytes; p++ )
			if( crossing[p] < crossing[phase[f]] )
				phase[f] = p;
	}

	// a cell its face's restart still runs through keeps the plane, and more cells than the reader
	// takes keep the steepest
	std::vector< std::pair<Int, Int> > cells;
	std::vector<Int> restart( W * H, 0 );
	for( Int i = 0; i < W * H; i++ )
	{
		if( lowest[i] < 0 )
			continue;
		const Int sheetBytes = sheetWidth[i] * RMG_CLIFF_UV_CELL * 2 / RMG_CLIFF_UV_BYTE, p = phase[face[i]];
		const Int below = lowest[i] - p >= 0 ? ( lowest[i] - p ) / sheetBytes : -( ( p - lowest[i] + sheetBytes - 1 ) / sheetBytes );
		restart[i] = p + below * sheetBytes;
		if( highest[i] - restart[i] > sheetBytes )
			continue;
		cells.push_back( std::make_pair( lowest[i] - highest[i], i ) );
	}
	if( (Int)cells.size() > RMG_NUM_CLIFF_INFO - 1 )
	{
		std::sort( cells.begin(), cells.end() );
		cells.resize( RMG_NUM_CLIFF_INFO - 1 );
		std::sort( cells.begin(), cells.end(), []( const std::pair<Int, Int>& a, const std::pair<Int, Int>& b ) { return a.second < b.second; } );
	}

	for( size_t c = 0; c < cells.size(); c++ )
	{
		const Int i = cells[c].second, x = i % W, y = i / W, w = sheetWidth[i];
		const Int h[4] = { heights[i], heights[i + 1], heights[i + W + 1], heights[i + W] };
		const Bool fallsAlongX = std::max( abs( h[0] - h[1] ), abs( h[3] - h[2] ) ) >= std::max( abs( h[0] - h[3] ), abs( h[1] - h[2] ) );
		const Int along = fallsAlongX ? y : x, period = 2 * w, behind = along - along % period;
		const Int corner[4][2] = { { x, y }, { x + 1, y }, { x + 1, y + 1 }, { x, y + 1 } };
		RMGCliffInfo info;
		info.m_tileIndex = sheetFirst[i] << 2;
		for( Int k = 0; k < 4; k++ )
		{
			const Int u = ( corner[k][fallsAlongX ? 1 : 0] - behind ) * RMG_CLIFF_UV_CELL;
			const Int v = -( h[k] - restart[i] ) * RMG_CLIFF_UV_BYTE;
			info.m_u[k] = (Real)u / (Real)RMG_CLIFF_UV_UNITS;
			info.m_v[k] = (Real)v / (Real)RMG_CLIFF_UV_UNITS;
		}
		info.m_flip = abs( h[2] - h[0] ) >= abs( h[3] - h[1] );
		indexOut[i] = (Short)infoOut.size();
		infoOut.push_back( info );
	}
}

/// The CkMp stream for a built layout.
static void writeLayout( const RMGLayout& layout, std::vector<char>& mapBytes )
{
	Int width = layout.m_width;
	Int height = layout.m_height;
	Int dataSize = width * height;

	MapChunkWriter w;

	/***************HEIGHT MAP DATA ***************/
	w.openChunk( "HeightMapData", K_HEIGHT_MAP_VERSION_4 );
		w.writeInt( width );
		w.writeInt( height );
		w.writeInt( RMG_BORDER_CELLS );
		w.writeInt( 1 );					// one boundary
		w.writeInt( width - 2 * RMG_BORDER_CELLS );	// the playable cells; the lane map's are not square
		w.writeInt( height - 2 * RMG_BORDER_CELLS );
		w.writeInt( dataSize );
		w.writeBytes( &layout.m_heights[0], dataSize );
	w.closeChunk();

	/***************BLEND TILE DATA ***************/
	// Version 6 on purpose: from version 7 on the reader expects the cliff and
	// passability bits in the file, below it works them out from the heights.
	{
		std::vector<Short> tiles( dataSize );
		std::vector<Int> heights( layout.m_heights.begin(), layout.m_heights.end() );
		std::vector<Int> sheetFirst( dataSize, -1 ), sheetWidth( dataSize, 0 );
		const RMGBiome& biome = *layout.m_biome;

		for( Int y = 0; y < height; y++ )
		{
			for( Int x = 0; x < width; x++ )
			{
				const Int i = layout.cellIndex( x, y ), terrainClass = layout.m_terrain[i];
				const Int first = rmgClassFirstTile( biome, terrainClass ), sheet = rmgClassWidth( biome, terrainClass );
				tiles[i] = tileIndexForCell( x, y, first, sheet );
				// the rock is what the generator paints its faces with
				if( terrainClass == RMG_TERRAIN_ROCK )
				{
					sheetFirst[i] = first;
					sheetWidth[i] = sheet;
				}
			}
		}
		std::vector<Short> cliffIndex;
		std::vector<RMGCliffInfo> cliffInfo;
		rmgCliffMapping( heights, width, height, sheetFirst, sheetWidth, cliffIndex, cliffInfo );

		w.openChunk( "BlendTileData", K_BLEND_TILE_VERSION_6 );
			w.writeInt( dataSize );
			w.writeBytes( &tiles[0], dataSize * sizeof(Short) );
			w.writeBytes( &layout.m_blendIndex[0], dataSize * sizeof(Short) );
			w.writeBytes( &layout.m_extraBlendIndex[0], dataSize * sizeof(Short) );
			w.writeBytes( &cliffIndex[0], dataSize * sizeof(Short) );

			w.writeInt( rmgClassFirstTile( biome, RMG_TERRAIN_COUNT ) );	// bitmap tiles
			w.writeInt( (Int)layout.m_blends.size() );
			w.writeInt( (Int)cliffInfo.size() );	// entry 0 is the default and is never written

			w.writeInt( RMG_TERRAIN_COUNT );
			for( Int terrainClass = 0; terrainClass < RMG_TERRAIN_COUNT; terrainClass++ )
			{
				Int tilesInClass, sheet;
				rmgTextureSheet( biome.m_textures[terrainClass], tilesInClass, sheet );
				w.writeInt( rmgClassFirstTile( biome, terrainClass ) );
				w.writeInt( tilesInClass );
				w.writeInt( sheet );
				w.writeInt( 0 );				// legacy field
				w.writeAsciiString( biome.m_textures[terrainClass] );
			}

			w.writeInt( 0 );					// no edge tiles
			w.writeInt( 0 );					// no edge texture classes

			// The blend table, entry 0 excepted: it is the "no blend" default and
			// is never written.
			for( UnsignedInt i = 1; i < layout.m_blends.size(); i++ )
			{
				w.writeInt( layout.m_blends[i].m_blendTileIndex );
				w.writeByte( layout.m_blends[i].m_horizontal );
				w.writeByte( layout.m_blends[i].m_vertical );
				w.writeByte( layout.m_blends[i].m_rightDiagonal );
				w.writeByte( layout.m_blends[i].m_leftDiagonal );
				w.writeByte( layout.m_blends[i].m_inverted );
				w.writeByte( layout.m_blends[i].m_longDiagonal );
				w.writeInt( -1 );				// no custom blend edge class: use the alpha
				w.writeInt( RMG_BLEND_FLAG_VALUE );
			}

			// the cliff mappings, in the order the reader takes them from version 5 on
			for( UnsignedInt i = 1; i < cliffInfo.size(); i++ )
			{
				w.writeInt( cliffInfo[i].m_tileIndex );
				for( Int k = 0; k < 4; k++ )
				{
					w.writeReal( cliffInfo[i].m_u[k] );
					w.writeReal( cliffInfo[i].m_v[k] );
				}
				w.writeByte( cliffInfo[i].m_flip ? 1 : 0 );
				w.writeByte( 0 );				// not a mutant: WorldBuilder's mark for a cell it gave up on
			}
		w.closeChunk();
	}

	/***************WORLD DATA ***************/
	// Must come before the sides chunk.
	w.openChunk( "WorldInfo", K_WORLDDICT_VERSION_1 );
		w.beginDict( 2 );
		// WorldHeightMapData copies it into GlobalData, where the buildings and the props read it
		// to pick their snow models; the falling snow is the map.ini's, see generatedMapBytes
		w.dictInt( "weather", layout.m_snowy ? WEATHER_SNOWY : WEATHER_NORMAL );
		w.dictInt( "compression", 0 );
	w.closeChunk();

	/***************PLAYER DATA ***************/
	// Must come before the object list.
	writeSides( w );

	/***************OBJECTS DATA ***************/
	w.openChunk( "ObjectsList", K_OBJECTS_VERSION_3 );
		for( UnsignedInt i = 0; i < layout.m_objects.size(); i++ )
		{
			if( layout.m_objects[i].m_waypointID > 0 )
				writeWaypoint( w, layout.m_objects[i] );
			else if( layout.m_objects[i].m_flags != 0 )
				writeRoadPoint( w, layout.m_objects[i] );
			else
				writeNeutralObject( w, layout.m_objects[i] );
		}
	w.closeChunk();

	/***************WATER AND BASE AREAS ***************/
	writePolygonTriggers( w, layout );

	/***************GLOBAL LIGHTING DATA ***************/
	writeGlobalLighting( w, *layout.m_biome, layout.m_timeOfDay );

	/***************WAYPOINT LINKS ***************/
	// read after every waypoint exists (TerrainLogic::loadMap), so its place in the file is free
	w.openChunk( "WaypointsList", K_WAYPOINTS_VERSION_1 );
		w.writeInt( (Int)layout.m_waypointLinks.size() );
		for( UnsignedInt i = 0; i < layout.m_waypointLinks.size(); i++ )
		{
			w.writeInt( layout.m_waypointLinks[i].first );
			w.writeInt( layout.m_waypointLinks[i].second );
		}
	w.closeChunk();

	w.finish( mapBytes );
}

void RandomMapGenerator::generate( const RandomMapSettings& settings, std::vector<char>& mapBytes )
{
	RandomMapSettings clamped = settings;
	clampSettings( clamped );
	if( !clamped.m_template.isEmpty() )
	{
		rmgGenerateFromTemplateChecked( clamped, mapBytes, NULL );
		return;
	}

	// The light is multiplied out while the bytes are written, after build() has handed the FPU
	// back, so the writing runs in the simulation's rounding mode as well.
	UnsignedInt callersFPMode = getFPMode();
	setFPMode();

	RMGLayout layout;
	layout.build( settings );
	writeLayout( layout, mapBytes );

	restoreFPMode( callersFPMode );
}

void rmgBuildSkeleton( const RandomMapSettings& settings, std::vector<char>& mapBytes )
{
	RandomMapSettings skeleton = settings;
	skeleton.m_template.clear();
	skeleton.m_templateCRC = 0;
	skeleton.m_style = 0;
	RandomMapGenerator::clampSettings( skeleton );

	UnsignedInt callersFPMode = getFPMode();
	setFPMode();

	RMGLayout layout;
	layout.build( skeleton, TRUE );
	writeLayout( layout, mapBytes );

	restoreFPMode( callersFPMode );
}

UnsignedInt RandomMapGenerator::fingerprint( const RandomMapSettings& settings )
{
	std::vector<char> mapBytes;
	generate( settings, mapBytes );

	CRC crc;
	Int version = RANDOM_MAP_GENERATOR_VERSION;
	crc.computeCRC( &version, sizeof(version) );
	crc.computeCRC( &mapBytes[0], (Int)mapBytes.size() );

	return crc.get();
}

Int RandomMapGenerator::aodFamilyCount( void ) { return RMG_AOD_FAMILIES; }

const char *RandomMapGenerator::aodFamilyName( Int family )
{
	return family >= 0 && family < RMG_AOD_FAMILIES ? theAodFamilies[family] : "";
}

Int RandomMapGenerator::aodFamilyOf( const RandomMapSettings& settings )
{
	RMGLayout layout;
	layout.build( settings );
	return layout.m_aodFamily;
}

Int RandomMapGenerator::aodFamiliesSkipped( const RandomMapSettings& settings )
{
	RMGLayout layout;
	layout.build( settings );
	return layout.m_aodSkipped;
}

AsciiString RandomMapGenerator::aodThemeName( Int seed )
{
	static const char *const theGrounds[RMG_BIOME_COUNT] = { "temperate", "desert", "winter", "steppe" };
	static const char *const theHours[4] = { "morning", "afternoon", "evening", "night" };
	AsciiString name;
	name.format( "%s, %s", theGrounds[rmgBiomeFor( seed )], theHours[rmgTimeOfDayFor( seed ) - TIME_OF_DAY_FIRST] );
	return name;
}

void RandomMapGenerator::setAodFamilyForTests( Int family )
{
	theAodFamilyForTests = family;
}

Bool RandomMapGenerator::isPlayable( const RandomMapSettings& settings )
{
	RMGLayout layout;
	layout.build( settings );
	return layout.m_playable;
}

void RandomMapGenerator::roadCells( const RandomMapSettings& settings, std::vector< std::vector<Int> >& roads, Int *mapWidth )
{
	RMGLayout layout;
	layout.build( settings );
	roads = layout.m_roads;
	*mapWidth = layout.m_width;
}

//-----------------------------------------------------------------------------
// Preview
//-----------------------------------------------------------------------------

/// Uncompressed 24-bit bottom-up tga, the shape every loader in the tree reads.
static void writeTgaHeader( std::vector<char>& tga, Int pixels )
{
	const Int headerBytes = 18;
	char header[headerBytes];
	memset( header, 0, headerBytes );

	header[2] = 2;								// uncompressed true colour
	header[12] = (char)(pixels & 0xff);
	header[13] = (char)((pixels >> 8) & 0xff);
	header[14] = (char)(pixels & 0xff);
	header[15] = (char)((pixels >> 8) & 0xff);
	header[16] = 24;							// bits per pixel

	tga.insert( tga.end(), header, header + headerBytes );
}

void RandomMapGenerator::generatePreview( const RandomMapSettings& settings,
																					std::vector<char>& tgaBytes )
{
	RandomMapSettings clamped = settings;
	clampSettings( clamped );
	if( !clamped.m_template.isEmpty() )
	{
		std::vector<char> mapBytes;
		tgaBytes.clear();
		rmgGenerateFromTemplateChecked( clamped, mapBytes, &tgaBytes );
		return;
	}

	RMGLayout layout;
	layout.build( settings );

	const Int pixels = PREVIEW_PIXELS;

	tgaBytes.clear();
	writeTgaHeader( tgaBytes, pixels );

	// Ground colours are the biome's, in the RMGTerrainClass order; water and the marks are fixed.
	const UnsignedByte (*theGroundColours)[3] = layout.m_biome->m_previewColours;
	static const UnsignedByte theWaterColour[3] = { 48, 86, 130 };
	static const UnsignedByte theStartColour[3] = { 240, 240, 240 };
	static const UnsignedByte theCliffColour[3] = { 58, 54, 50 };

	// the longer side across the picture; the lane map's short one leaves black below it
	const Int playableX = layout.m_width - 2 * RMG_BORDER_CELLS, playableY = layout.m_height - 2 * RMG_BORDER_CELLS;
	Real playable = (Real)( playableX > playableY ? playableX : playableY );
	Real cellsPerPixel = playable / (Real)pixels;

	for( Int row = 0; row < pixels; row++ )
	{
		// Bottom-up file order, the first row the map's lowest y, as WorldBuilder wrote every shipped
		// preview: the menus put a map's highest y at the top of the picture and its start marks there.
		for( Int column = 0; column < pixels; column++ )
		{
			Int cellX = (Int)((Real)column * cellsPerPixel) + RMG_BORDER_CELLS;
			Int cellY = (Int)((Real)row * cellsPerPixel) + RMG_BORDER_CELLS;

			if( cellX >= RMG_BORDER_CELLS + playableX || cellY >= RMG_BORDER_CELLS + playableY )
			{
				tgaBytes.push_back( 0 );
				tgaBytes.push_back( 0 );
				tgaBytes.push_back( 0 );
				continue;
			}
			if( cellX >= layout.m_width ) cellX = layout.m_width - 1;
			if( cellY >= layout.m_height ) cellY = layout.m_height - 1;

			Real height = (Real)layout.heightAtCell( cellX, cellY );
			UnsignedByte terrainClass = layout.terrainAtCell( cellX, cellY );

			const UnsignedByte *colour = theGroundColours[terrainClass];

			if( layout.underwaterAtCell( cellX, cellY ) )
				colour = theWaterColour;
			else if( !layout.passableAtCell( cellX, cellY ) )
				colour = theCliffColour;

			// Cheap relief: the higher the ground, the lighter the pixel.
			Real shade = 0.70f + 0.60f * (height - RMG_BASE_HEIGHT) / (RMG_AMPLITUDE * 2.0f);
			if( shade < 0.45f ) shade = 0.45f;
			if( shade > 1.35f ) shade = 1.35f;

			Real blue = (Real)colour[2] * shade;
			Real green = (Real)colour[1] * shade;
			Real red = (Real)colour[0] * shade;

			// The start positions, so the list entry says how many players it is for.
			Real px = (Real)(cellX - RMG_BORDER_CELLS);
			Real py = (Real)(cellY - RMG_BORDER_CELLS);
			for( UnsignedInt i = 0; i < layout.m_starts.size(); i++ )
			{
				Real dx = px - layout.m_starts[i].m_cellX;
				Real dy = py - layout.m_starts[i].m_cellY;
				if( dx * dx + dy * dy < 9.0f * cellsPerPixel * cellsPerPixel )
				{
					red = (Real)theStartColour[0];
					green = (Real)theStartColour[1];
					blue = (Real)theStartColour[2];
				}
			}

			if( red > 255.0f ) red = 255.0f;
			if( green > 255.0f ) green = 255.0f;
			if( blue > 255.0f ) blue = 255.0f;

			tgaBytes.push_back( (char)(UnsignedByte)blue );
			tgaBytes.push_back( (char)(UnsignedByte)green );
			tgaBytes.push_back( (char)(UnsignedByte)red );
		}
	}
}

//-----------------------------------------------------------------------------
// Generated maps, kept in memory where the map cache will find them
//-----------------------------------------------------------------------------

/** A generated map lives here and nowhere else. The seed, the player count and the size are in the
	path, so anything that names the map - a replay, a save, a lobby telling the other machines what
	is being played - names everything the generator needs to build the same bytes again. That is
	what lets the map stay out of the file system entirely: a machine that has never seen this seed
	rebuilds it from the name the moment something opens it.

	Three maps are kept. Rerolling in the menu walks through them, and a map that falls off the end
	is not lost, only forgotten: the next open of that path builds it again. */
enum { RMG_MAPS_KEPT = 3 };

struct RMGStagedMap
{
	RandomMapSettings m_settings;		///< what it was built from, which is what a slot is looked up by
	AsciiString m_mapPath;					///< lowercase, the path the rest of the game names it by
	std::vector<char> m_mapBytes;
	std::vector<char> m_previewBytes;
	UnsignedInt m_stagedAt;					///< which staging this was, so the oldest can go first
};

static RMGStagedMap theStagedMaps[ RMG_MAPS_KEPT ];
static UnsignedInt theStagingCount = 0;

/** Where a generated map's bytes would live, given its settings. The map cache expects
	"<user maps>\<name>\<name>.map" - the directory carries the name - and getMapPreviewImage wants
	"<name>.tga" beside it. */
static void generatedMapPathsFor( const RandomMapSettings& clamped, AsciiString& mapPath,
																	AsciiString& previewPath )
{
	AsciiString name;
	name.format( "RMG_v%d_%d_%dp_%dc%s%s", RANDOM_MAP_GENERATOR_VERSION, clamped.m_seed,
							 clamped.m_numPlayers, clamped.m_playableCells, clamped.m_style > 0 ? "_" : "",
							 rmgStyleName( clamped.m_style ) );

	// a template map: "_t<the template's CRC>_<its name>", the name last because it may hold anything
	AsciiString token;
	if( rmgTemplateToken( clamped.m_template, token ) )
	{
		AsciiString tail;
		tail.format( "_t%08x_%s", clamped.m_templateCRC, token.str() );
		name.concat( tail );
	}

	AsciiString dir;
	dir.format( "%sMaps\\%s", TheGlobalData->getPath_UserData().str(), name.str() );
	mapPath.format( "%s\\%s.map", dir.str(), name.str() );
	previewPath.format( "%s\\%s.tga", dir.str(), name.str() );

	mapPath.toLower();
	previewPath.toLower();
}

/// "_t<8 hex>_<name>", what follows the size in a template map's name.
static Bool isTemplateTail( const char *rest )
{
	Bool isTemplate = rest[0] == '_' && rest[1] == 't' && strlen( rest ) > 11 && rest[10] == '_';
	for( Int i = 2; isTemplate && i < 10; i++ )
		isTemplate = isxdigit( (UnsignedByte)rest[i] ) != 0;
	return isTemplate;
}

/** Read the settings back out of a generated map's file name. FALSE for anything else, including a
	name written by another generator version - those bytes cannot be rebuilt here. */
static Bool settingsFromGeneratedPath( const AsciiString& path, RandomMapSettings& settingsOut )
{
	const char *leafStart = path.reverseFind( '\\' );
	AsciiString leaf = leafStart ? leafStart + 1 : path.str();

	// every path the game hands around has been through toLower somewhere, and a name is a name
	// whichever case it arrives in
	leaf.toLower();

	Int version = 0, seed = 0, players = 0, cells = 0, read = 0;
	if( sscanf( leaf.str(), "rmg_v%d_%d_%dp_%dc%n", &version, &seed, &players, &cells, &read ) != 4 )
		return FALSE;

	if( version != RANDOM_MAP_GENERATOR_VERSION )
		return FALSE;

	// "_twilight" after the size is a style; a name for one this build does not have is not ours.
	// "_t<8 hex>_<name>" is a template, ours only when this machine's copy of it has that CRC.
	Int style = 0;
	AsciiString templatePath;
	UnsignedInt templateCRC = 0;
	const char *rest = leaf.str() + read;
	if( isTemplateTail( rest ) )
	{
		templateCRC = (UnsignedInt)strtoul( rest + 2, NULL, 16 );
		AsciiString token = rest + 11;
		// the leaf is the folder, "<name>.map" or "<name>.tga"
		const char *dot = token.reverseFind( '.' );
		if( dot && strlen( dot ) == 4 )
		{
			for( Int i = 0; i < 4; i++ )
				token.removeLastChar();
		}
		rmgTemplateFromToken( token, templatePath );
		UnsignedInt installed = 0;
		if( !rmgTemplateFullCRC( templatePath, &installed ) || installed != templateCRC )
			return FALSE;
	}
	else if( *rest == '_' )
	{
		const char *dot = strchr( rest, '.' );
		Int length = ( dot ? (Int)( dot - rest ) : (Int)strlen( rest ) ) - 1;
		char token[32];
		if( length >= (Int)sizeof(token) )
			return FALSE;
		memcpy( token, rest + 1, length );
		token[length] = 0;
		style = RandomMapGenerator::styleNamed( token );
		if( style == 0 )
			return FALSE;
	}

	settingsOut.m_seed = seed;
	settingsOut.m_numPlayers = players;
	settingsOut.m_playableCells = cells;
	settingsOut.m_style = style;
	settingsOut.m_template = templatePath;
	settingsOut.m_templateCRC = templateCRC;

	// a name carrying settings the generator would clamp names bytes it never produced
	RandomMapSettings clamped = settingsOut;
	RandomMapGenerator::clampSettings( clamped );
	return clamped.m_seed == settingsOut.m_seed
			&& clamped.m_numPlayers == settingsOut.m_numPlayers
			&& clamped.m_playableCells == settingsOut.m_playableCells
			&& clamped.m_style == settingsOut.m_style
			&& clamped.m_template == settingsOut.m_template
			&& clamped.m_templateCRC == settingsOut.m_templateCRC;
}

Bool LaneGridColours( const AsciiString& mapPath, UnsignedInt *lineARGB, UnsignedInt *edgeARGB )
{
	RandomMapSettings settings;
	if( !settingsFromGeneratedPath( mapPath, settings ) || settings.m_style != RMG_STYLE_LANE )
		return FALSE;
	const RMGLaneGround& ground = theLaneGrounds[rmgLaneTwilight( settings.m_seed ) ? RMG_BIOME_COUNT : rmgBiomeFor( settings.m_seed )];
	const UnsignedInt rgb = ( (UnsignedInt)ground.m_gridColour[0] << 16 ) | ( (UnsignedInt)ground.m_gridColour[1] << 8 )
		| (UnsignedInt)ground.m_gridColour[2];
	*lineARGB = rgb | ( (UnsignedInt)ground.m_gridAlpha[0] << 24 );
	*edgeARGB = rgb | ( (UnsignedInt)ground.m_gridAlpha[1] << 24 );
	return TRUE;
}

Bool generatedMapTemplate( const AsciiString& path, AsciiString& templatePathOut )
{
	RandomMapSettings settings;
	if( !settingsFromGeneratedPath( path, settings ) || settings.m_template.isEmpty() )
		return FALSE;
	templatePathOut = settings.m_template;
	return TRUE;
}

/* On the wire "rmg_v19_2147483647_8p_672c_tffffffff_bombardment beach" is
	"~19_zik0zj_8_io~143ez3": the numbers in base 36, then four base-36 letters of the template folder's
	name and two of its CRC. A plain or styled map keeps its tail, "~19_zik0zj_8_io_twilight". The CRC's
	own eight hex digits would cost every seat's name in a full LAN room six letters more than a shipped
	map's field does; with two, the field is as long as "maps/bombardment beach". The name's letters pick
	the folder without opening a file, and only that folder's template is read for the CRC's: a machine
	whose template or rules differ fails those two letters 1295 times in 1296, so the name does not read
	back and the room sees a map it does not have. The map's own CRC in the options catches the rest. */
static const char WIRE_MARK = '~';
enum { WIRE_NAME_LETTERS = 4, WIRE_CRC_LETTERS = 2, WIRE_NUMBER_LETTERS = 15 };

static AsciiString base36( UnsignedInt value, Int width )
{
	static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	char letters[WIRE_NUMBER_LETTERS + 1];
	Int at = WIRE_NUMBER_LETTERS;
	letters[at] = 0;
	do
	{
		letters[--at] = digits[value % 36];
		value /= 36;
	}
	while( value != 0 || WIRE_NUMBER_LETTERS - at < width );
	return AsciiString( letters + at );
}

/// Four base-36 letters of a template folder's name: 1.7 million values for the 150 shipped maps.
static AsciiString wireNameLetters( const AsciiString& token )
{
	CRC crc;
	crc.computeCRC( token.str(), token.getLength() );
	return base36( crc.get() % ( 36u * 36u * 36u * 36u ), WIRE_NAME_LETTERS );
}

/// The name's letters and two of the template's CRC.
static AsciiString wireTemplateLetters( const AsciiString& token, UnsignedInt templateCRC )
{
	AsciiString letters = wireNameLetters( token );
	letters.concat( base36( templateCRC % ( 36u * 36u ), WIRE_CRC_LETTERS ) );
	return letters;
}

Bool generatedMapWireName( const AsciiString& folder, AsciiString& wireOut )
{
	Int version = 0, seed = 0, players = 0, cells = 0, read = 0;
	if( sscanf( folder.str(), "rmg_v%d_%d_%dp_%dc%n", &version, &seed, &players, &cells, &read ) != 4 )
		return FALSE;
	const char *rest = folder.str() + read;
	AsciiString tail = rest;
	if( isTemplateTail( rest ) )
		tail.format( "%c%s", WIRE_MARK,
			wireTemplateLetters( AsciiString( rest + 11 ), (UnsignedInt)strtoul( rest + 2, NULL, 16 ) ).str() );
	wireOut.format( "%c%d_%s_%d_%s%s", WIRE_MARK, version, base36( (UnsignedInt)seed, 1 ).str(), players,
		base36( (UnsignedInt)cells, 1 ).str(), tail.str() );
	return TRUE;
}

Bool generatedMapFromWireName( const AsciiString& wire, AsciiString& folderOut )
{
	Int version = 0, players = 0, read = 0;
	char seedText[WIRE_NUMBER_LETTERS + 1], cellText[WIRE_NUMBER_LETTERS + 1];
	if( wire.getCharAt( 0 ) != WIRE_MARK
		|| sscanf( wire.str() + 1, "%d_%15[0-9a-z]_%d_%15[0-9a-z]%n", &version, seedText, &players, cellText, &read ) != 4 )
		return FALSE;
	// the seed went out as its 32 bits unsigned and comes back signed, so "-5" reads back as "-5"
	AsciiString head;
	head.format( "rmg_v%d_%d_%dp_%uc", version, (Int)(UnsignedInt)strtoul( seedText, NULL, 36 ), players,
		(UnsignedInt)strtoul( cellText, NULL, 36 ) );
	const char *rest = wire.str() + 1 + read;
	if( *rest != WIRE_MARK )
	{
		folderOut.format( "%s%s", head.str(), rest );
		return TRUE;
	}

	/* ponytail: the first shipped map whose name letters and CRC letters both match wins, so two
		folders sharing four name letters leave the later one out of LAN games unless their CRCs differ in
		those two letters; a fifth name letter when that ever happens. The name letters do not depend on
		any file, so every_shipped_map_has_wire_letters_of_its_own checks them over every shipped folder
		with no game data. The CRC is read only for a folder whose name letters match, so a parse opens
		one template, not all of them. */
	const AsciiString wanted( rest + 1 );
	if( wanted.getLength() != WIRE_NAME_LETTERS + WIRE_CRC_LETTERS )
		return FALSE;
	AsciiString wantedName = wanted;
	for( Int i = 0; i < WIRE_CRC_LETTERS; i++ )
		wantedName.removeLastChar();
	FilenameList maps;
	TheFileSystem->getFileListInDirectory( AsciiString( "Maps\\" ), AsciiString( "*.map" ), maps, TRUE );
	AsciiString token;
	for( FilenameListIter it = maps.begin(); it != maps.end(); ++it )
	{
		AsciiString path = *it;
		path.toLower();
		UnsignedInt crc = 0;
		if( rmgTemplateToken( path, token ) && wireNameLetters( token ) == wantedName
			&& rmgTemplateFullCRC( path, &crc ) && wireTemplateLetters( token, crc ) == wanted )
		{
			folderOut.format( "%s_t%08x_%s", head.str(), crc, token.str() );
			return TRUE;
		}
	}
	return FALSE;
}

/** The falling snow is not map data: SnowManager draws it when the Weather block says
	SnowEnabled, and a shipped winter map turns that on from the map.ini in its folder. A generated
	map that snows gets one too, served from here the way its bytes are; the values are Bitter
	Winter's. GameLogic loads it as an override and drops it at the next reset, so a map that does
	not snow has no map.ini and keeps the stock Weather.ini. */
static const char theSnowRules[] =
	"Weather\r\n"
	"  SnowEnabled = Yes\r\n"
	"  SnowTexture = ExSnowFlake1.tga\r\n"
	"  SnowBoxDimensions = 100\r\n"
	"  SnowBoxDensity = 1\r\n"
	"  SnowFrequencyScaleX = 0.0533\r\n"
	"  SnowFrequencyScaleY = 0.0275\r\n"
	"  SnowAmplitude = 4.0\r\n"
	"  SnowVelocity = 3.0\r\n"
	"  SnowPointSize = 0.16\r\n"
	"  SnowMaxPointSize = 10.0\r\n"
	"  SnowMinPointSize = 0.0\r\n"
	"  SnowPointSprites = Yes\r\n"
	"  SnowQuadSize = 0.5\r\n"
	"End\r\n";

/** A template map's rules are its template's own map.ini, the snow or rain, the water and the
	overrides of the objects it copies; read once a run, an empty one when the template has none. */
static const std::vector<char>& templateRules( const AsciiString& templatePath )
{
	static std::map<AsciiString, std::vector<char> > theRules;
	std::map<AsciiString, std::vector<char> >::iterator it = theRules.find( templatePath );
	if( it != theRules.end() )
		return it->second;
	std::vector<char>& rules = theRules[templatePath];
	AsciiString rulesPath = templatePath;
	while( !rulesPath.isEmpty() && !rulesPath.endsWith( "\\" ) )
		rulesPath.removeLastChar();
	rulesPath.concat( "map.ini" );
	File *file = TheFileSystem->openFile( rulesPath.str(), File::READ | File::BINARY );
	if( file != NULL )
	{
		Int size = file->size();
		char *contents = file->readEntireAndClose();
		rules.assign( contents, contents + size );
		delete [] contents;
	}
	return rules;
}

/** A template's CRC as a generated map's name carries it: the .map's (rmgTemplateCRC) with the bytes of
	the map.ini beside it folded in, since those rules override the logic of the objects the map copies
	and two machines with other rules would play other games on the same name. A template with no
	map.ini keeps the .map's own CRC. Never 0, which is "not installed". */
static Bool rmgTemplateFullCRC( const AsciiString& templatePath, UnsignedInt *crcOut )
{
	UnsignedInt mapCRC = 0;
	*crcOut = 0;
	if( !rmgTemplateCRC( templatePath, &mapCRC ) )
		return FALSE;
	const std::vector<char>& rules = templateRules( templatePath );
	if( rules.empty() )
	{
		*crcOut = mapCRC;
		return TRUE;
	}
	CRC crc;
	crc.computeCRC( &mapCRC, sizeof(mapCRC) );
	crc.computeCRC( &rules[0], (Int)rules.size() );
	*crcOut = crc.get() == 0 ? 1 : crc.get();
	return TRUE;
}

/// "<generated map folder>\map.ini", for a seed that snows or a template that has one; its bytes or NULL.
static const char *rulesFor( const AsciiString& path, Int *sizeOut )
{
	AsciiString folder = path;
	folder.toLower();
	if( !folder.endsWith( "\\map.ini" ) )
		return NULL;
	for( Int i = 0; i < 8; i++ )
		folder.removeLastChar();

	RandomMapSettings settings;
	if( !settingsFromGeneratedPath( folder, settings )
			|| ( settings.m_style != 0 && settings.m_style != RMG_STYLE_LANE && settings.m_style != RMG_STYLE_APOCALYPSE
				&& settings.m_style != RMG_STYLE_AOD ) )
		return NULL;
	if( settings.m_style == RMG_STYLE_LANE && rmgLaneTwilight( settings.m_seed ) )
		return NULL;
	if( !settings.m_template.isEmpty() )
	{
		const std::vector<char>& rules = templateRules( settings.m_template );
		*sizeOut = (Int)rules.size();
		return rules.empty() ? NULL : &rules[0];
	}
	*sizeOut = (Int)sizeof(theSnowRules) - 1;
	return rmgSnowsOn( settings.m_seed ) ? theSnowRules : NULL;
}

static Bool isSnowRulesPath( const AsciiString& path )
{
	Int size = 0;
	return rulesFor( path, &size ) != NULL;
}

Bool isGeneratedMapPath( const AsciiString& path )
{
	RandomMapSettings settings;
	return settingsFromGeneratedPath( path, settings ) || isSnowRulesPath( path );
}

/** The slot holding this map, or NULL.  Slots are looked up by what they were built from rather
	than by the path that asked for them: the same map is named several ways over a run - the switch
	that made it, the lobby, the loader, the preview - and two spellings of one map would otherwise
	each build their own copy. */
static RMGStagedMap *findStagedMap( const RandomMapSettings& settings )
{
	for( Int i = 0; i < RMG_MAPS_KEPT; i++ )
	{
		// a slot that came out empty stays, so a template that failed is not built again on every options packet
		if( theStagedMaps[i].m_stagedAt == 0 )
			continue;

		if( theStagedMaps[i].m_settings.m_seed == settings.m_seed
				&& theStagedMaps[i].m_settings.m_numPlayers == settings.m_numPlayers
				&& theStagedMaps[i].m_settings.m_playableCells == settings.m_playableCells
				&& theStagedMaps[i].m_settings.m_style == settings.m_style
				&& theStagedMaps[i].m_settings.m_template == settings.m_template
				&& theStagedMaps[i].m_settings.m_templateCRC == settings.m_templateCRC )
			return &theStagedMaps[i];
	}

	return NULL;
}

/// Build a map into the slot that has been unused longest.
static RMGStagedMap *stageMap( const RandomMapSettings& clamped )
{
	RMGStagedMap *slot = &theStagedMaps[0];
	for( Int i = 1; i < RMG_MAPS_KEPT; i++ )
	{
		if( theStagedMaps[i].m_stagedAt < slot->m_stagedAt )
			slot = &theStagedMaps[i];
	}

	AsciiString previewPath;
	generatedMapPathsFor( clamped, slot->m_mapPath, previewPath );
	slot->m_settings = clamped;
	slot->m_mapBytes.clear();
	slot->m_previewBytes.clear();
	// a template map is a synthesis of seconds, so the map and its preview come out of one pass
	if( !clamped.m_template.isEmpty() )
		rmgGenerateFromTemplateChecked( clamped, slot->m_mapBytes, &slot->m_previewBytes );
	else
	{
		RandomMapGenerator::generate( clamped, slot->m_mapBytes );
		RandomMapGenerator::generatePreview( clamped, slot->m_previewBytes );
	}
	slot->m_stagedAt = ++theStagingCount;

	if( !slot->m_mapBytes.empty() )
	{
		CRC crc;
		Int version = RANDOM_MAP_GENERATOR_VERSION;
		crc.computeCRC( &version, sizeof(version) );
		crc.computeCRC( &slot->m_mapBytes[0], (Int)slot->m_mapBytes.size() );
		DEBUG_LOG(("random map: built '%s' - %d players, %d cells, %d bytes, fingerprint %X\n",
			slot->m_mapPath.str(), clamped.m_numPlayers, clamped.m_playableCells, slot->m_mapBytes.size(),
			crc.get()));
	}

	return slot;
}

Bool stageRandomMap( const RandomMapSettings& settings, AsciiString& mapPathOut )
{
	RandomMapSettings clamped = settings;
	RandomMapGenerator::clampSettings( clamped );

	AsciiString mapPath, previewPath;
	generatedMapPathsFor( clamped, mapPath, previewPath );

	/* A template map reads its towns' buildings off the object INI, which the command line is
		parsed before: from -randommap it is only named here, and built when the map cache or the
		loader first opens it, which generatedMapBytes does for any name it has not staged. */
	Bool buildNow = clamped.m_template.isEmpty() || TheThingFactory != NULL;
	if( buildNow && findStagedMap( clamped ) == NULL )
		stageMap( clamped );

	mapPathOut = mapPath;
	return TRUE;
}

Bool restageRandomMap( const AsciiString& path, Int numPlayers, AsciiString& mapPathOut )
{
	RandomMapSettings settings;
	if( !settingsFromGeneratedPath( path, settings ) )
		return FALSE;

	// the size is the one that gives the name's cells at the name's count; a style's map, or cells
	// no size gives, take whatever the new count needs
	const Int cells = settings.m_playableCells;
	settings.m_playableCells = 0;
	for( Int size = 0; size < RANDOM_MAP_SIZE_COUNT && settings.m_style == 0; size++ )
	{
		if( RandomMapGenerator::cellsFor( (RandomMapSize)size, settings.m_numPlayers, settings.m_template ) == cells )
		{
			settings.m_playableCells = RandomMapGenerator::cellsFor( (RandomMapSize)size, numPlayers, settings.m_template );
			break;
		}
	}
	settings.m_numPlayers = numPlayers;
	return stageRandomMap( settings, mapPathOut );
}

Bool generatedMapBytes( const AsciiString& path, const char **bytesOut, Int *sizeOut )
{
	Int rulesSize = 0;
	const char *rules = rulesFor( path, &rulesSize );
	if( rules != NULL )
	{
		if( bytesOut )
			*bytesOut = rules;
		if( sizeOut )
			*sizeOut = rulesSize;
		return TRUE;
	}

	RandomMapSettings settings;
	if( !settingsFromGeneratedPath( path, settings ) )
		return FALSE;

	RandomMapSettings clamped = settings;
	RandomMapGenerator::clampSettings( clamped );

	// a template's towns need the object INI; asked before it is loaded, there is nothing yet
	if( !clamped.m_template.isEmpty() && TheThingFactory == NULL )
		return FALSE;

	RMGStagedMap *slot = findStagedMap( clamped );
	if( slot == NULL )
	{
		// nothing has this seed in hand - a replay, a save or a joined game naming a map this
		// machine has never built.  The name says how to build it, so build it
		slot = stageMap( clamped );
	}

	AsciiString lower = path;
	lower.toLower();

	// the map and its preview, nothing else: the terrain loader asks for "<map>.wak" beside every map,
	// and handing it the map read the map's last four bytes as a count of shore waves. It came out
	// harmless while the map ended in the lighting chunk and crashed every load once it ended in the
	// waypoint links.
	const Bool preview = lower.endsWith( ".tga" );
	if( !preview && !lower.endsWith( ".map" ) )
		return FALSE;
	const std::vector<char>& bytes = preview ? slot->m_previewBytes : slot->m_mapBytes;
	if( bytes.empty() )
		return FALSE;

	if( bytesOut )
		*bytesOut = &bytes[0];
	if( sizeOut )
		*sizeOut = (Int)bytes.size();

	return TRUE;
}

void generatedMapPaths( std::vector<AsciiString>& pathsOut )
{
	for( Int i = 0; i < RMG_MAPS_KEPT; i++ )
	{
		if( !theStagedMaps[i].m_mapBytes.empty() )
			pathsOut.push_back( theStagedMaps[i].m_mapPath );
	}
}

//-----------------------------------------------------------------------------
// MemoryChunkInputStream
//-----------------------------------------------------------------------------

MemoryChunkInputStream::MemoryChunkInputStream( const char *data, Int size ) :
	m_data(data), m_size(size), m_pos(0)
{
}

Int MemoryChunkInputStream::read( void *pData, Int numBytes )
{
	if( numBytes > m_size - m_pos )
		numBytes = m_size - m_pos;

	if( pData )
		memcpy( pData, m_data + m_pos, numBytes );

	m_pos += numBytes;
	return numBytes;
}

UnsignedInt MemoryChunkInputStream::tell( void )
{
	return m_pos;
}

Bool MemoryChunkInputStream::absoluteSeek( UnsignedInt pos )
{
	if( (Int)pos > m_size )
		pos = m_size;

	m_pos = pos;
	return TRUE;
}

Bool MemoryChunkInputStream::eof( void )
{
	return m_pos >= m_size;
}
