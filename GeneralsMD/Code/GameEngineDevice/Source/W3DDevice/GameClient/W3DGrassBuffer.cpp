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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: W3DGrassBuffer.cpp
// The GroundFoliage option's grass, shrubs, twigs, pebbles and rocks.  See W3DGrassBuffer.h and
// FoliagePlacement.h.

#include "W3DDevice/GameClient/W3DGrassBuffer.h"
#include "W3DDevice/GameClient/FoliagePlacement.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShroud.h"

#include "Common/FileSystem.h"
#include "Common/GameCommon.h"
#include "Common/Geometry.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "Common/TerrainTypes.h"
#include "Common/ThingTemplate.h"
#include "GameClient/GameClient.h"
#include "GameClient/Snow.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TerrainLogic.h"

#include <assetmgr.h>
#include <texture.h>
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx11runtime.h"
#include "WW3D2/shader.h"
#include "wwmath.h"
#include "d3dx9runtime.h"
#include "d3d8shadertranslate.h"
#include "Platform/RenderTypes.h"

#include <algorithm>
#include <string.h>

using namespace FoliagePlacement;

static_assert(ROCK_VARIANTS == 8 && ROCK_CORNERS == 12, "W3DGrassBuffer::m_rockCorners is sized for these");

// Sizes are world units.  Infantry stand about twelve tall; a tuft comes to the knee.
#define GRASS_TEXTURE				"ReforgedFoliage.tga"
#define GRASS_TEXTURE_PATH	"Art/Textures/ReforgedFoliage.tga"
#define GRASS_SHADER_NAME		"shaders\\grass.vso"	///< what the Direct3D 11 backend knows this shader by
#define ATLAS_ROOT_ROW			246.0f	///< of a 256 cell: where a rooted card's roots are painted
#define TUFT_HEIGHT					5.5f		///< at scale one
#define TUFT_HALF_WIDTH			0.52f		///< of the height: an atlas cell is 256 wide and 246 to the root
#define TUFT_SINK						0.6f		///< a card's foot under the ground at each corner, so its cut edge never shows
#define SHRUB_HEIGHT				15.0f		///< at scale one: about two to three and a half tufts, so a bush reads from the default camera
#define SHRUB_HALF_WIDTH		0.6f
#define SHRUB_SINK					0.9f
#define CANOPY_HEIGHT				0.62f		///< of the bush's height: the rim of its canopy
#define CANOPY_DOME					0.1f		///< of the bush's height: how far the canopy's middle rises over its rim
#define CANOPY_SHARE				0.8f		///< of the bush's half width: the canopy's half side
#define BUSH_TINT						0.35f		///< how far a green bush takes the grass's hue under it
#define BUSH_BRIGHT					1.12f		///< and how much brighter it is for it
#define TWIG_HALF						3.5f		///< half the side of the flat card, at scale one
#define PEBBLE_HALF					2.3f
#define FLAT_LIFT						0.5f		///< a flat card over the drawn ground: what bends between its samples stays under it
#define FLAT_GRID						5				///< a flat card's vertices a side: under two units apart, inside the drawn half cells
#define FLAT_FOLD						1.0f		///< how far under its own height the fold at the ring's edge takes a flat card
#define STONE_RADIUS				2.2f		///< at scale one: about two units showing, under a soldier's knee
#define BOULDER_RADIUS			4.0f
#define BOULDER_BURY				0.3f		///< of the radius, further under for the quarter of the boulders half buried
#define ROCK_SINK						0.1f		///< of the radius, under the lowest ground beneath the rock
#define ROCK_CONTACT				0.6f		///< the light a rock keeps where it meets the ground
#define ROCK_DARK						0.45f		///< the light its side away from the sun keeps, of the lit side's
#define GROUND_TINT					0.6f		///< how far rocks and pebbles take the colour of the ground they lie on
#define ROOT_SHADE					0.65f		///< the roots' share of the light
#define NORMAL_TO_UP				0.5f		///< how far a card's normal goes from the ground's toward straight up
#define GRASS_RADIUS				480.0f	///< around the tactical view's focus
#define GRASS_FADE_WIDTH		140.0f	///< the last stretch of it, thinned on the CPU and folded on the GPU
#define GRASS_MIN_SHARE			0.15f		///< of a chunk's cards drawn at the radius itself
#define GUST_WAVE						220.0f	///< length of one gust across the field
#define SWAY_GAIN						2.5f		///< of the breeze's intensity: grass gives more than a tree
#define SWAY_MAX						0.7f		///< radians
#define SWAY_PERIOD_SHARE		0.6f		///< of the breeze's period: grass flutters faster than a tree sways
#define ROAD_MARGIN					5.0f
#define FOOTPRINT_MARGIN		3.0f
#define DEFAULT_ROAD_WIDTH	30.0f
#define GRASS_ALPHAREF			0x80		///< the atlas is straight alpha cut at a half

// The tree pass's shader: opaque, alpha tested, both faces, writes depth.
#define SC_GRASS ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass grassShader(SC_GRASS);

namespace
{
	Kind kindOf(TerrainClass terrainClass)
	{
		switch (terrainClass)
		{
		case TERRAIN_GRASS:
		case TERRAIN_ACCENT_GRASS:
		case TERRAIN_FIELD:
			return KIND_FULL;
		case TERRAIN_SWISS_1:
		case TERRAIN_SWISS_2:
		case TERRAIN_SWISS_3:
		case TERRAIN_EASTERN_EUROPE_1:
		case TERRAIN_EASTERN_EUROPE_2:
		case TERRAIN_EASTERN_EUROPE_3:
			return KIND_REGION;
		case TERRAIN_COBBLESTONE_GRASS:
		case TERRAIN_BEACH_PARK:
			return KIND_SPARSE;
		default:
			return KIND_NONE;
		}
	}

	/// What bare ground a class is; the vague ones are read by name.
	Bare bareOf(TerrainClass terrainClass, const char *name, Bool grassy)
	{
		switch (terrainClass)
		{
		case TERRAIN_DESERT_1:
		case TERRAIN_DESERT_2:
		case TERRAIN_DESERT_3:
		case TERRAIN_LIVE_DESERT:
		case TERRAIN_DRY_DESERT:
		case TERRAIN_SAND:
		case TERRAIN_ACCENT_SAND:
		case TERRAIN_TROPICAL_BEACH:
			return BARE_DESERT;
		case TERRAIN_DIRT:
			return BARE_DIRT;
		case TERRAIN_ROCK:
		case TERRAIN_ACCENT_ROCK:
		case TERRAIN_RUGGED_MOUNTAIN:
		case TERRAIN_CLIFF:
			return BARE_ROCKY;
		case TERRAIN_SNOW_1:
		case TERRAIN_SNOW_2:
		case TERRAIN_SNOW_3:
		case TERRAIN_RUGGED_SNOW:
		case TERRAIN_FLAT_SNOW:
			return BARE_SNOW;
		case TERRAIN_GRASS:
		case TERRAIN_ACCENT_GRASS:
		case TERRAIN_FIELD:
		case TERRAIN_COBBLESTONE_GRASS:
		case TERRAIN_BEACH_PARK:
			return BARE_NONE;
		case TERRAIN_ASPHALT:
		case TERRAIN_CONCRETE:
		case TERRAIN_URBAN:
		case TERRAIN_RESIDENTIAL:
		case TERRAIN_CHINA:
		case TERRAIN_WOOD:
		case TERRAIN_BLEND_EDGES:
			return BARE_PAVED;
		case TERRAIN_TRANSITION:
			return bareByName(name, BARE_DIRT);
		default:
			// the Swiss and Eastern Europe sets, and anything the table does not name
			return grassy ? BARE_NONE : bareByName(name, terrainClass == TERRAIN_NONE ? BARE_NONE : BARE_DIRT);
		}
	}

	/// A footprint as the grid sees it: a turned box, or the bounding circle of anything round.
	Int clearGeometry(Grid &grid, const Coord3D &pos, const GeometryInfo &geom, Real angle, Range *range)
	{
		if (geom.getGeomType() == GEOMETRY_BOX)
			return clearBox(grid, pos.x, pos.y, geom.getMajorRadius() + FOOTPRINT_MARGIN,
				geom.getMinorRadius() + FOOTPRINT_MARGIN, angle, range);
		const Real r = geom.getBoundingCircleRadius() + FOOTPRINT_MARGIN;
		return clearCapsule(grid, pos.x, pos.y, pos.x, pos.y, r, range);
	}

	/// W3DTreeBuffer::doLighting without the vertex colour: the map's terrain lights on one normal.
	UnsignedInt lightOn(const Vector3 &normal, Real scale)
	{
		const GlobalData::TerrainLighting *lights = TheGlobalData->m_terrainLighting[TheGlobalData->m_timeOfDay];
		Real r = lights[0].ambient.red, g = lights[0].ambient.green, b = lights[0].ambient.blue;
		for (Int i = 0; i < MAX_GLOBAL_LIGHTS; i++) {
			Vector3 dir(lights[i].lightPos.x, lights[i].lightPos.y, lights[i].lightPos.z);
			dir.Normalize();
			Real shade = -Vector3::Dot_Product(dir, normal);
			if (shade > 1.0f) shade = 1.0f;
			if (shade < 0.0f) shade = 0.0f;
			r += shade * lights[i].diffuse.red;
			g += shade * lights[i].diffuse.green;
			b += shade * lights[i].diffuse.blue;
		}
		r = WWMath::Clamp(r * scale, 0.0f, 1.0f);
		g = WWMath::Clamp(g * scale, 0.0f, 1.0f);
		b = WWMath::Clamp(b * scale, 0.0f, 1.0f);
		return 0xFF000000u | (REAL_TO_UNSIGNEDINT(r * 255.0f) << 16) | (REAL_TO_UNSIGNEDINT(g * 255.0f) << 8) | REAL_TO_UNSIGNEDINT(b * 255.0f);
	}

	/// A lit colour times a tint, each channel.
	UnsignedInt tinted(UnsignedInt colour, const Vector3 &tint)
	{
		const Real r = WWMath::Clamp(((colour >> 16) & 0xFF) * tint.X, 0.0f, 255.0f);
		const Real g = WWMath::Clamp(((colour >> 8) & 0xFF) * tint.Y, 0.0f, 255.0f);
		const Real b = WWMath::Clamp((colour & 0xFF) * tint.Z, 0.0f, 255.0f);
		return 0xFF000000u | (REAL_TO_UNSIGNEDINT(r) << 16) | (REAL_TO_UNSIGNEDINT(g) << 8) | REAL_TO_UNSIGNEDINT(b);
	}

	/// The ground's colour where a rock or pebble lies (its tile's average), as a tint: the ground's
	/// hue, some of its darkness, GROUND_TINT of the way from white.  Grey stone on warm sand read as a
	/// stain; tinted it belongs to the map.
	Vector3 groundTint(WorldHeightMap *map, Real x, Real y)
	{
		RGBColor c;
		map->getTerrainColorAt(x, y, &c);
		Real top = c.red > c.green ? c.red : c.green;
		if (c.blue > top) top = c.blue;
		if (top < 0.05f) {
			return Vector3(1.0f, 1.0f, 1.0f);
		}
		const Real level = (0.55f + 0.45f * top) / top;
		const Vector3 hue(c.red * level, c.green * level, c.blue * level);
		return Vector3(1.0f, 1.0f, 1.0f) * (1.0f - GROUND_TINT) + hue * GROUND_TINT;
	}

	/// A green bush's tint: the grass's hue under it at full brightness, BUSH_TINT of the way from
	/// white, a little brighter, so it reads as living green and not as a dead patch.
	Vector3 bushTint(WorldHeightMap *map, Real x, Real y)
	{
		RGBColor c;
		map->getTerrainColorAt(x, y, &c);
		Real top = c.red > c.green ? c.red : c.green;
		if (c.blue > top) top = c.blue;
		if (top < 0.05f) {
			return Vector3(1.0f, 1.0f, 1.0f);
		}
		const Vector3 hue(c.red / top, c.green / top, c.blue / top);
		return (Vector3(1.0f, 1.0f, 1.0f) * (1.0f - BUSH_TINT) + hue * BUSH_TINT) * BUSH_BRIGHT;
	}

	/// A rock face: the ground's own light, falling to ROCK_DARK of it on the side away from the sun,
	/// so a stone has a shadowed side instead of the ambient-flat grey it was; then tint and shade.
	UnsignedInt lightRock(const Vector3 &normal, const Vector3 &tint, Real shade)
	{
		const GlobalData::TerrainLighting *lights = TheGlobalData->m_terrainLighting[TheGlobalData->m_timeOfDay];
		Vector3 sun(lights[0].lightPos.x, lights[0].lightPos.y, lights[0].lightPos.z);
		sun.Normalize();
		const Real facing = WWMath::Clamp(-Vector3::Dot_Product(sun, normal), 0.0f, 1.0f);
		const Real light = (ROCK_DARK + (1.0f - ROCK_DARK) * facing) * shade;
		return tinted(lightOn(Vector3(0.0f, 0.0f, 1.0f), 1.0f), tint * light);
	}

	/// The cell's slope from the drawn map's corners, bent upShare of the way toward straight up.
	Vector3 cellNormal(WorldHeightMap *map, Int x, Int y, Real upShare)
	{
		const Real h00 = map->getHeight(x, y) * MAP_HEIGHT_SCALE, h10 = map->getHeight(x + 1, y) * MAP_HEIGHT_SCALE;
		const Real h01 = map->getHeight(x, y + 1) * MAP_HEIGHT_SCALE, h11 = map->getHeight(x + 1, y + 1) * MAP_HEIGHT_SCALE;
		Vector3 normal(-((h10 - h00) + (h11 - h01)) * 0.5f, -((h01 - h00) + (h11 - h10)) * 0.5f, MAP_XY_FACTOR);
		normal.Normalize();
		normal = normal * (1.0f - upShare) + Vector3(0.0f, 0.0f, upShare);
		normal.Normalize();
		return normal;
	}

	/// An atlas cell's corners in texture space, half a texel in.
	void atlasCorners(Int cell, Real &u0, Real &v0, Real &u1, Real &v1)
	{
		const Real atlasU = 1.0f / (256.0f * ATLAS_COLUMNS), atlasV = 1.0f / (256.0f * ATLAS_ROWS);
		const Int col = cell % ATLAS_COLUMNS, row = cell / ATLAS_COLUMNS;
		u0 = (col * 256 + 0.5f) * atlasU;
		u1 = (col * 256 + 255.5f) * atlasU;
		v0 = (row * 256 + 0.5f) * atlasV;
		v1 = (row * 256 + 255.5f) * atlasV;
	}

	void putVertex(VertexFormatXYZNDUV1 &v, Real x, Real y, Real z, Real sway, Real root, UnsignedInt colour, Real u, Real t)
	{
		v.x = x;
		v.y = y;
		v.z = z;
		v.nx = sway;		// Trees.vso: c[8 + type], c8 being still
		v.ny = 1.0f;		// the colour factor
		v.nz = root;		// (z - root) is how far the sway moves a vertex, and where the fold takes it
		v.diffuse = colour;
		v.u1 = u;
		v.v1 = t;
	}
}

W3DGrassBuffer::W3DGrassBuffer(BaseHeightMapRenderObjClass *terrain) :
	m_terrain(terrain),
	m_classifiedMap(NULL),
	m_anyFoliage(FALSE),
	m_width(0),
	m_height(0),
	m_border(0),
	m_chunksX(0),
	m_chunksY(0),
	m_vertexShader(NULL),
	m_vertexDeclaration(NULL),
	m_deviceFailed(FALSE),
	m_texture(NULL),
	m_textureMissing(FALSE),
	m_drawFrame(0),
	m_contentVersion(-1),
	m_breezeForDraw(),
	m_pauseForDraw(FALSE),
	m_swayDirX(1.0f),
	m_swayDirY(0.0f),
	m_phase(0.0f),
	m_lastPhaseFrame(0)
{
	for (Int i = 0; i < MAX_SLOTS; i++) {
		m_slots[i].vb = NULL;
		m_slots[i].ib = NULL;
		m_slots[i].vbCapacity = 0;
		m_slots[i].ibCapacity = 0;
		m_slots[i].key = -1;
		m_slots[i].fixedPieces = 0;
		m_slots[i].thinPieces = 0;
		m_slots[i].lastUsed = 0;
		m_slots[i].dirty = FALSE;
	}
	for (Int v = 0; v < ROCK_VARIANTS; v++) {
		rockCorners(v, m_rockCorners[v]);
	}
}

W3DGrassBuffer::~W3DGrassBuffer(void)
{
	freeGrassBuffers();
	REF_PTR_RELEASE(m_texture);
}

/** A picture that can draw the grass at all, fixed at startup: not Classic, not -headless, and on
Windows the Direct3D 11 frame.  -d3d9 gets none: there setShroudTex rewrites the stage's coordinate
index, so the shroud lands wrong, Trees.vso has no distance fold, so the ring's edge pops, and the
grass paints over the tree and decal shadows.  SDL3 draws it. */
Bool W3DGrassBuffer::available(void)
{
	if (TheGlobalData == NULL || TheGlobalData->m_headless || TheGlobalData->m_classicGraphics) {
		return FALSE;
	}
#if defined(_WIN32)
	return Direct3D11_Present_Is_Enabled();
#else
	return TRUE;
#endif
}

Bool W3DGrassBuffer::wanted(void)
{
	return available() && TheGlobalData->m_groundFoliage;
}

Bool W3DGrassBuffer::drawable(void) const
{
	return wanted() && m_terrain != NULL && m_terrain->terrainSubdivided();
}

void W3DGrassBuffer::classifyIfWanted(WorldHeightMap *map)
{
	if (map == NULL || map == m_classifiedMap || !drawable() || TheGlobalData->m_loadScreenRender) {
		return;	// not while the load screen draws: the map's water and objects may be half in
	}
	classify(map);
	clearStructures();	// switched on mid-match: what was built since the load
}

void W3DGrassBuffer::takeGrid(const W3DGrassBuffer &from)
{
	clear();
	m_classifiedMap = from.m_classifiedMap;
	m_anyFoliage = from.m_anyFoliage;
	m_cells = from.m_cells;
	m_width = from.m_width;
	m_height = from.m_height;
	m_border = from.m_border;
	m_chunksX = from.m_chunksX;
	m_chunksY = from.m_chunksY;
}

void W3DGrassBuffer::clear(void)
{
	m_classifiedMap = NULL;
	m_anyFoliage = FALSE;
	m_cells.clear();
	m_width = m_height = m_border = m_chunksX = m_chunksY = 0;
	for (Int i = 0; i < MAX_SLOTS; i++) {
		m_slots[i].key = -1;
		m_slots[i].fixedPieces = 0;
		m_slots[i].thinPieces = 0;
		m_slots[i].ends.clear();
		m_slots[i].dirty = FALSE;
	}
}

void W3DGrassBuffer::releaseSlot(Slot &slot)
{
	REF_PTR_RELEASE(slot.vb);
	REF_PTR_RELEASE(slot.ib);
	slot.vbCapacity = 0;
	slot.ibCapacity = 0;
	slot.key = -1;
	slot.fixedPieces = 0;
	slot.thinPieces = 0;
	slot.ends.clear();
}

void W3DGrassBuffer::freeGrassBuffers(void)
{
	for (Int i = 0; i < MAX_SLOTS; i++) {
		releaseSlot(m_slots[i]);
	}
	if (m_vertexShader) {
		m_vertexShader->Release();
		m_vertexShader = NULL;
	}
	if (m_vertexDeclaration) {
		m_vertexDeclaration->Release();
		m_vertexDeclaration = NULL;
	}
	m_deviceFailed = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** The grid.  Once a map, at its load, so TheTerrainLogic's water is read while no tick runs. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::classify(WorldHeightMap *map)
{
	if (map == NULL || map == m_classifiedMap || !available()) {
		return;	// a picture that never draws grass (-headless, Classic, -d3d9) never pays for the grid
	}
	clear();
	m_classifiedMap = map;
	m_width = map->getXExtent();
	m_height = map->getYExtent();
	m_border = map->getBorderSizeInline();
	m_chunksX = (m_width + CHUNK_CELLS - 1) / CHUNK_CELLS;
	m_chunksY = (m_height + CHUNK_CELLS - 1) / CHUNK_CELLS;
	m_cells.assign((size_t)m_width * m_height * CELL_BYTES, 0);

	// WorldHeightMap names this class a friend for these
	const Short *tiles = map->m_tileNdxes;
	const Short *blends = map->m_blendTileNdxes;
	const Short *extraBlends = map->m_extraBlendTileNdxes;
	const Short *cliffInfo = map->m_cliffInfoNdxes;
	const TBlendTileInfo *blendTiles = map->m_blendedTiles;
	const TBlendTileInfo *extraBlendTiles = map->m_extraBlendedTiles;
	if (tiles == NULL || m_width < 2 || m_height < 2) {
		return;
	}

	// tile -> what grows on it, through the tile's Terrain.ini class
	struct TileInfo { UnsignedByte grass, stones, twigs, rock, known; };
	TileInfo tileInfo[NUM_SOURCE_TILES];
	memset(tileInfo, 0, sizeof(tileInfo));
	const Int numClasses = min(map->m_numTextureClasses, (Int)NUM_TEXTURE_CLASSES);
	const TXTextureClass *classes = map->m_textureClasses;
	for (Int i = 0; i < numClasses; i++) {
		if (classes[i].firstTile < 0) {
			continue;
		}
		TerrainType *terrain = TheTerrainTypes ? TheTerrainTypes->findTerrain(classes[i].name) : NULL;	// a map may name one its data lacks
		const char *name = classes[i].name.str();
		const Bool named = nameHasGrass(name);
		const TerrainClass terrainClass = terrain != NULL ? terrain->getClass() : TERRAIN_NONE;
		TileInfo info;
		info.grass = terrain != NULL ? kindDensity(kindOf(terrainClass), named) : (named ? 255 : 0);
		const Bare bare = bareOf(terrainClass, name, info.grass == 255);
		info.stones = stoneDensity(bare);
		info.twigs = twigDensity(info.grass, bare);
		info.rock = (UnsignedByte)rockOf(bare, name);
		info.known = 1;
		for (Int t = classes[i].firstTile; t < classes[i].firstTile + classes[i].numTiles && t < NUM_SOURCE_TILES; t++) {
			tileInfo[t] = info;
		}
	}
	TileInfo none;
	memset(&none, 0, sizeof(none));

	for (Int y = 0; y < m_height - 1; y++) {
		for (Int x = 0; x < m_width - 1; x++) {
			const Int ndx = y * m_width + x;
			const Int baseTile = tiles[ndx] >> 2;
			const TileInfo &base = (baseTile >= 0 && baseTile < NUM_SOURCE_TILES) ? tileInfo[baseTile] : none;
			const Short b = blends ? blends[ndx] : 0;
			const Bool hasOverlay = b > 0 && b < NUM_BLEND_TILES;
			const Int overlayTile = hasOverlay ? blendTiles[b].blendNdx >> 2 : -1;
			const TileInfo &overlay = (overlayTile >= 0 && overlayTile < NUM_SOURCE_TILES) ? tileInfo[overlayTile] : none;
			const Short e = extraBlends ? extraBlends[ndx] : 0;
			const Bool hasExtra = e > 0 && e < NUM_BLEND_TILES;
			const Int extraTile = hasExtra ? extraBlendTiles[e].blendNdx >> 2 : -1;
			const TileInfo &extra = (extraTile >= 0 && extraTile < NUM_SOURCE_TILES) ? tileInfo[extraTile] : none;

			UnsignedByte *cell = &m_cells[(size_t)ndx * CELL_BYTES];
			const UnsignedByte grass = blendMean(base.grass, hasOverlay, overlay.grass, hasExtra, extra.grass);
			const UnsignedByte stones = blendMean(base.stones, hasOverlay, overlay.stones, hasExtra, extra.stones);
			const UnsignedByte twigs = blendMean(base.twigs, hasOverlay, overlay.twigs, hasExtra, extra.twigs);
			const UnsignedByte rock = base.known ? base.rock : overlay.rock;
			const Bool cliff = map->getCliffState(x, y);
			const Real h00 = map->getHeight(x, y) * MAP_HEIGHT_SCALE, h10 = map->getHeight(x + 1, y) * MAP_HEIGHT_SCALE;
			const Real h01 = map->getHeight(x, y + 1) * MAP_HEIGHT_SCALE, h11 = map->getHeight(x + 1, y + 1) * MAP_HEIGHT_SCALE;
			// a face the pathfinder still lets units onto (a cliff texture, or a slope under its 9.8
			// units a cell): nothing on it, and its foot is where the open ground starts below it
			const Bool steep = !cliff && ((cliffInfo && cliffInfo[ndx] != 0) || tooSteep(h00, h10, h01, h11, MAP_XY_FACTOR));
			if (grass == 0 && stones == 0 && twigs == 0 && !cliff && !steep) {
				cell[BYTE_FLAGS] = rock;	// a cliff's foot may still gather stones of it
				continue;
			}
			if (TheTerrainLogic
				&& TheTerrainLogic->isUnderwater((x - m_border + 0.5f) * MAP_XY_FACTOR, (y - m_border + 0.5f) * MAP_XY_FACTOR)) {
				continue;
			}
			cell[BYTE_FLAGS] = rock;
			if (cliff) {
				// impassable: nothing grows, and the boulders gather at its foot; a gentle ledge
				// holds the odd small stone
				cell[BYTE_FLAGS] |= CF_CLIFF;
				if (!tooSteep(h00, h10, h01, h11, MAP_XY_FACTOR * LEDGE_SPAN / MAX_SLOPE)) {
					cell[BYTE_FLAGS] |= CF_LEDGE;
				}
				continue;
			}
			if (steep) {
				cell[BYTE_FLAGS] |= CF_STEEP;
				continue;
			}
			cell[BYTE_GRASS] = grass;
			cell[BYTE_STONES] = stones;
			cell[BYTE_TWIGS] = twigs;
		}
	}
	flagNeighbours();
	clearRoadsAndMapStructures();
	logCounts();
}

//-------------------------------------------------------------------------------------------------
/** The flags a cell takes from its neighbours: the foot of a cliff, and where grass meets bare ground.
	A foot is open ground beside a cliff or a steep cell, never a steep cell itself: a scree band
	under a cliff pushes the foot out to where the ground levels, and nothing sits on the face. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::flagNeighbours(void)
{
	for (Int y = 0; y < m_height - 1; y++) {
		for (Int x = 0; x < m_width - 1; x++) {
			UnsignedByte *cell = &m_cells[((size_t)y * m_width + x) * CELL_BYTES];
			if (cell[BYTE_FLAGS] & (CF_CLIFF | CF_STEEP)) {
				continue;
			}
			const Bool grassy = cell[BYTE_GRASS] != 0;
			Bool foot = FALSE, edge = FALSE;
			for (Int dy = -1; dy <= 1; dy++) {
				for (Int dx = -1; dx <= 1; dx++) {
					const Int nx = x + dx, ny = y + dy;
					if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= m_width - 1 || ny >= m_height - 1) {
						continue;
					}
					const UnsignedByte *other = &m_cells[((size_t)ny * m_width + nx) * CELL_BYTES];
					if (other[BYTE_FLAGS] & (CF_CLIFF | CF_STEEP)) {
						foot = TRUE;
					} else if ((dx == 0 || dy == 0) && (other[BYTE_GRASS] != 0) != grassy) {
						edge = TRUE;
					}
				}
			}
			const Bool empty = cell[BYTE_GRASS] == 0 && cell[BYTE_STONES] == 0 && cell[BYTE_TWIGS] == 0;
			if (foot && empty && TheTerrainLogic
				&& TheTerrainLogic->isUnderwater((x - m_border + 0.5f) * MAP_XY_FACTOR, (y - m_border + 0.5f) * MAP_XY_FACTOR)) {
				foot = FALSE;	// a cliff standing in the water keeps its stones out of it
			}
			if (foot) {
				cell[BYTE_FLAGS] |= CF_CLIFF_FOOT;
			}
			if (edge) {
				cell[BYTE_FLAGS] |= CF_EDGE;
			}
		}
	}
	// what nothing will ever stand on keeps no rock either, so a clear's count means something
	for (size_t i = 0; i < m_cells.size(); i += CELL_BYTES) {
		if ((m_cells[i + BYTE_FLAGS] & (CF_CLIFF | CF_CLIFF_FOOT)) == 0 && m_cells[i + BYTE_GRASS] == 0
			&& m_cells[i + BYTE_STONES] == 0 && m_cells[i + BYTE_TWIGS] == 0) {
			m_cells[i + BYTE_FLAGS] = 0;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** What the map grows, per kind, for the log, and whether there is anything to draw at all. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::logCounts(void)
{
	Int counts[PIECE_KINDS];
	memset(counts, 0, sizeof(counts));
	Int cells = 0;
	for (Int y = 0; y < m_height - 1; y++) {
		for (Int x = 0; x < m_width - 1; x++) {
			const UnsignedByte *cell = &m_cells[((size_t)y * m_width + x) * CELL_BYTES];
			if (cell[BYTE_GRASS] == 0 && cell[BYTE_STONES] == 0 && cell[BYTE_TWIGS] == 0 && cell[BYTE_FLAGS] == 0) {
				continue;
			}
			++cells;
			for (Int k = 0; k < MAX_TUFTS_PER_CELL; k++) {
				counts[PIECE_TUFT] += tuftPresent(x, y, k, cell[BYTE_GRASS]) ? 1 : 0;
			}
			for (Int p = PIECE_SHRUB; p < PIECE_KINDS; p++) {
				counts[p] += piecePresent(x, y, (Piece)p, cell) ? 1 : 0;
			}
		}
	}
	Int total = 0;
	for (Int p = 0; p < PIECE_KINDS; p++) {
		total += counts[p];
	}
	m_anyFoliage = total > 0;
	DEBUG_LOG(("GROUNDFOLIAGE %d of %d cells: %d tufts, %d shrubs, %d twigs, %d pebbles, %d stones, %d boulders\n",
		cells, m_width * m_height, counts[PIECE_TUFT], counts[PIECE_SHRUB], counts[PIECE_TWIGS],
		counts[PIECE_PEBBLES], counts[PIECE_STONE], counts[PIECE_BOULDER]));
}

//-------------------------------------------------------------------------------------------------
/** The map's own roads, by the pairs of points W3DRoadBuffer::addMapObjects reads, and the
	structures it places. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::clearRoadsAndMapStructures(void)
{
	Grid grid = { &m_cells[0], m_width, m_height, MAP_XY_FACTOR, -m_border * MAP_XY_FACTOR, -m_border * MAP_XY_FACTOR, CELL_BYTES };
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (obj->getFlag(FLAG_ROAD_POINT1)) {
			MapObject *obj2 = obj->getNext();
			if (obj2 == NULL) {
				break;
			}
			if (!obj2->getFlag(FLAG_ROAD_POINT2)) {
				continue;
			}
			Real width = DEFAULT_ROAD_WIDTH;
			TerrainRoadType *road = TheTerrainRoads ? TheTerrainRoads->findRoad(obj->getName()) : NULL;
			if (road) {
				width = road->getRoadWidth();
			}
			clearCapsule(grid, obj->getLocation()->x, obj->getLocation()->y, obj2->getLocation()->x, obj2->getLocation()->y,
				width * 0.5f + ROAD_MARGIN);
			continue;
		}
		const ThingTemplate *tmpl = obj->getThingTemplate();
		if (tmpl && tmpl->isKindOf(KINDOF_STRUCTURE)) {
			clearGeometry(grid, *obj->getLocation(), tmpl->getTemplateGeometryInfo(), obj->getAngle(), NULL);
		}
	}
}

void W3DGrassBuffer::clearFootprint(const Coord3D *pos, const GeometryInfo &geom, Real angle)
{
	if (m_cells.empty() || pos == NULL) {
		return;
	}
	Grid grid = { &m_cells[0], m_width, m_height, MAP_XY_FACTOR, -m_border * MAP_XY_FACTOR, -m_border * MAP_XY_FACTOR, CELL_BYTES };
	Range range;
	if (clearGeometry(grid, *pos, geom, angle, &range) > 0) {
		dirtyCells(range.loX, range.loY, range.hiX, range.hiY);
	}
}

void W3DGrassBuffer::clearStructures(void)
{
	if (m_cells.empty() || TheGameLogic == NULL) {
		return;
	}
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject()) {
		if (obj->isKindOf(KINDOF_STRUCTURE)) {
			clearFootprint(obj->getPosition(), obj->getGeometryInfo(), obj->getOrientation());
		}
	}
}

void W3DGrassBuffer::dirtyCells(Int loX, Int loY, Int hiX, Int hiY)
{
	if (loX > hiX || loY > hiY) {
		return;
	}
	// a rock baked into the next chunk can reach a cell or so across the line
	const Int cx0 = max(0, loX - 1) / CHUNK_CELLS, cx1 = (hiX + 1) / CHUNK_CELLS;
	const Int cy0 = max(0, loY - 1) / CHUNK_CELLS, cy1 = (hiY + 1) / CHUNK_CELLS;
	for (Int i = 0; i < MAX_SLOTS; i++) {
		const Int key = m_slots[i].key;
		if (key < 0) {
			continue;
		}
		const Int cx = key % m_chunksX, cy = key / m_chunksX;
		if (cx >= cx0 && cx <= cx1 && cy >= cy0 && cy <= cy1) {
			m_slots[i].dirty = TRUE;
		}
	}
}

void W3DGrassBuffer::dirtyAll(void)
{
	for (Int i = 0; i < MAX_SLOTS; i++) {
		if (m_slots[i].key >= 0) {
			m_slots[i].dirty = TRUE;
		}
	}
}

void W3DGrassBuffer::takeBreezeForDraw(void)
{
	Bool pause = TheScriptEngine && (TheScriptEngine->isTimeFrozenScript() || TheScriptEngine->isTimeFrozenDebug());
	if (TheGameLogic && TheGameLogic->isGamePaused()) {
		pause = TRUE;
	}
	if (TheScriptEngine) {
		m_breezeForDraw = weatherBreezeInfo();
	}
	m_pauseForDraw = pause;
}

//-------------------------------------------------------------------------------------------------
/** The shader (Trees.vso, under its own name for Direct3D 11) and the atlas. */
//-------------------------------------------------------------------------------------------------
Bool W3DGrassBuffer::makeDeviceObjects(void)
{
	if (m_vertexShader && m_texture) {
		return TRUE;
	}
	if (m_deviceFailed || m_textureMissing) {
		return FALSE;
	}
	if (m_texture == NULL) {
		if (TheFileSystem == NULL || !TheFileSystem->doesFileExist(GRASS_TEXTURE_PATH)) {
			DEBUG_LOG(("GROUNDFOLIAGE %s is missing, no grass\n", GRASS_TEXTURE_PATH));
			m_textureMissing = TRUE;
			return FALSE;
		}
		m_texture = WW3DAssetManager::Get_Instance()->Get_Texture(GRASS_TEXTURE);
		if (m_texture == NULL) {
			m_textureMissing = TRUE;
			return FALSE;
		}
	}
	if (m_vertexShader == NULL) {
		// DX8_FVF_XYZNDUV1, the tree buffer's layout: the normal carries the sway type, the colour
		// factor and the root's height (W3DTreeBuffer::allocateTreeBuffers)
		RenderUInt32 declaration[] =
		{
			D3DVSD_STREAM( 0 ),
			D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),  // Position
			D3DVSD_REG( 1, D3DVSDT_FLOAT3 ),  // Normal
			D3DVSD_REG( 2, D3DVSDT_D3DCOLOR), // Diffuse color
			D3DVSD_REG( 7, D3DVSDT_FLOAT2 ),  // Tex coord
			D3DVSD_END()
		};
		RenderResult hr = W3DShaderManager::LoadAndCreateD3DVertexShader("shaders\\Trees.vso", &declaration[0],
			&m_vertexShader, &m_vertexDeclaration);
		if (Render_Failed(hr) || m_vertexShader == NULL) {
			DEBUG_LOG(("GROUNDFOLIAGE no vertex shader, no grass\n"));
			m_deviceFailed = TRUE;
			return FALSE;
		}
		// The loader registered this object as Trees.vso; a second registration overwrites it, so
		// Direct3D 11 draws it with the grass program (distance fold, shadow receiving) and the
		// trees keep theirs.
		Direct3D11_Register_Engine_Shader(m_vertexShader, GRASS_SHADER_NAME);
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Rooted cards: planes crossed about (x, y), each foot sunk under the drawn ground at its own corner
	and still, the top swaying.  A tuft is two planes, a shrub three. */
//-------------------------------------------------------------------------------------------------
Bool W3DGrassBuffer::addCards(Real x, Real y, Real yaw, Int planes, Real height, Real halfWidth, Real sink,
	Int atlasCell, Bool mirror, Real sway, UnsignedInt top, UnsignedInt root, Real canopyHalf)
{
	const size_t base = m_vertices.size();
	const Int extraVertices = canopyHalf > 0.0f ? 5 : 0, extraIndices = canopyHalf > 0.0f ? 12 : 0;
	if (base + planes * 4 + extraVertices > MAX_CHUNK_VERTICES || m_indices.size() + planes * 6 + extraIndices > MAX_CHUNK_INDICES) {
		return FALSE;
	}
	Real u0, v0, u1, v1;
	atlasCorners(atlasCell, u0, v0, u1, v1);
	v1 = v0 + (v1 - v0) * (ATLAS_ROOT_ROW / 256.0f);	// the painted roots, not the cell's bottom
	if (mirror) {
		const Real swap = u0; u0 = u1; u1 = swap;
	}
	const Real rootZ = m_terrain->getDrawnHeight(x, y) - sink;
	for (Int p = 0; p < planes; p++) {
		const Real angle = yaw + p * (PI / planes);
		const Real ax = WWMath::Cos(angle) * halfWidth, ay = WWMath::Sin(angle) * halfWidth;
		const Real z0 = m_terrain->getDrawnHeight(x - ax, y - ay) - sink;
		const Real z1 = m_terrain->getDrawnHeight(x + ax, y + ay) - sink;
		VertexFormatXYZNDUV1 v[4];
		putVertex(v[0], x - ax, y - ay, z0, sway, z0, root, u0, v1);
		putVertex(v[1], x + ax, y + ay, z1, sway, z1, root, u1, v1);
		putVertex(v[2], x + ax, y + ay, rootZ + height, sway, rootZ, top, u1, v0);
		putVertex(v[3], x - ax, y - ay, rootZ + height, sway, rootZ, top, u0, v0);
		const UnsignedShort first = (UnsignedShort)m_vertices.size();
		m_vertices.insert(m_vertices.end(), v, v + 4);
		const UnsignedShort quad[6] = { first, (UnsignedShort)(first + 1), (UnsignedShort)(first + 2),
			first, (UnsignedShort)(first + 2), (UnsignedShort)(first + 3) };
		m_indices.insert(m_indices.end(), quad, quad + 6);
	}
	if (canopyHalf > 0.0f) {
		// The canopy a bush shows the camera from above, where its upright planes are a thin star:
		// a low dome over CANOPY_HEIGHT of it, four triangles from a raised middle, the middle of
		// the bush's painting on it, turned half a plane's step off them, swaying with the top.
		const Real cu0 = u0 + (u1 - u0) * 0.2f, cu1 = u0 + (u1 - u0) * 0.8f;
		const Real cv0 = v0 + (v1 - v0) * 0.15f, cv1 = v0 + (v1 - v0) * 0.65f;
		const Real rimZ = rootZ + height * CANOPY_HEIGHT, middleZ = rimZ + height * CANOPY_DOME;
		const Real angle = yaw + PI / (planes * 2);
		const Real c = WWMath::Cos(angle) * canopyHalf, s = WWMath::Sin(angle) * canopyHalf;
		const Real cornerX[4] = { -1.0f, 1.0f, 1.0f, -1.0f }, cornerY[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
		const Real cornerU[4] = { cu0, cu1, cu1, cu0 }, cornerV[4] = { cv0, cv0, cv1, cv1 };
		const UnsignedShort middle = (UnsignedShort)m_vertices.size();
		VertexFormatXYZNDUV1 v;
		putVertex(v, x, y, middleZ, sway, rootZ, top, (cu0 + cu1) * 0.5f, (cv0 + cv1) * 0.5f);
		m_vertices.push_back(v);
		for (Int k = 0; k < 4; k++) {
			putVertex(v, x + cornerX[k] * c - cornerY[k] * s, y + cornerX[k] * s + cornerY[k] * c, rimZ, sway, rootZ, top,
				cornerU[k], cornerV[k]);
			m_vertices.push_back(v);
		}
		for (Int k = 0; k < 4; k++) {
			const UnsignedShort tri[3] = { middle, (UnsignedShort)(middle + 1 + k), (UnsignedShort)(middle + 1 + (k + 1) % 4) };
			m_indices.insert(m_indices.end(), tri, tri + 3);
		}
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A card lying on the ground, FLAT_GRID vertices a side so it bends with the subdivided ground's
	five unit triangles, FLAT_LIFT above it.  No sway; the fold at the ring's edge sinks it. */
//-------------------------------------------------------------------------------------------------
Bool W3DGrassBuffer::addFlat(Real x, Real y, Real yaw, Real half, Int atlasCell, UnsignedInt colour)
{
	const Int n = FLAT_GRID, cells = FLAT_GRID - 1;
	if (m_vertices.size() + n * n > MAX_CHUNK_VERTICES || m_indices.size() + cells * cells * 6 > MAX_CHUNK_INDICES) {
		return FALSE;
	}
	Real u0, v0, u1, v1;
	atlasCorners(atlasCell, u0, v0, u1, v1);
	const Real c = WWMath::Cos(yaw) * half, s = WWMath::Sin(yaw) * half;
	const UnsignedShort first = (UnsignedShort)m_vertices.size();
	for (Int j = 0; j < n; j++) {
		for (Int i = 0; i < n; i++) {
			const Real fi = (Real)i / cells, fj = (Real)j / cells;
			const Real a = fi * 2.0f - 1.0f, b = fj * 2.0f - 1.0f;
			const Real px = x + a * c - b * s, py = y + a * s + b * c;
			const Real pz = m_terrain->getDrawnHeight(px, py) + FLAT_LIFT;
			VertexFormatXYZNDUV1 v;
			putVertex(v, px, py, pz, 0.0f, pz - FLAT_FOLD, colour, u0 + (u1 - u0) * fi, v0 + (v1 - v0) * fj);
			m_vertices.push_back(v);
		}
	}
	for (Int j = 0; j < cells; j++) {
		for (Int i = 0; i < cells; i++) {
			const UnsignedShort a = (UnsignedShort)(first + j * n + i);
			const UnsignedShort quad[6] = { a, (UnsignedShort)(a + 1), (UnsignedShort)(a + n + 1),
				a, (UnsignedShort)(a + n + 1), (UnsignedShort)(a + n) };
			m_indices.insert(m_indices.end(), quad, quad + 6);
		}
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A rock: one of the icosahedron variants, faceted (a boulder's faces split in four, so up close it
	is no crude prism), its texture projected along each face's main axis.  Lit from its own normals
	with a dark side, darker still where it meets the ground, and tinted the ground's colour.  It sits
	sink radii under the lowest ground beneath it, so a slope never shows its underside, and the fold
	at the ring's edge sinks it into the ground. */
//-------------------------------------------------------------------------------------------------
Bool W3DGrassBuffer::addRock(Real x, Real y, Real yaw, Real radius, Real stretch, Int variant, Int atlasCell,
	Bool detailed, Real sink, const Vector3 &tint)
{
	const Int triangles = detailed ? ROCK_FACES * 4 : ROCK_FACES;
	if (m_vertices.size() + triangles * 3 > MAX_CHUNK_VERTICES || m_indices.size() + triangles * 3 > MAX_CHUNK_INDICES) {
		return FALSE;
	}
	Real u0, v0, u1, v1;
	atlasCorners(atlasCell, u0, v0, u1, v1);
	const Real c = WWMath::Cos(yaw), s = WWMath::Sin(yaw);
	// the lowest drawn ground under the whole footprint, out to the stretched side's reach in eight
	// directions, so the downhill side of a rock on a slope is buried rather than hanging in the air
	Real ground = m_terrain->getDrawnHeight(x, y);
	const Real reach = radius * stretch * ROCK_MAX_RADIUS * 1.05f;	// the widest corner, or a split edge pushed out
	for (Int k = 0; k < 8; k++) {
		const Real a = k * (PI * 0.25f);
		const Real below = m_terrain->getDrawnHeight(x + WWMath::Cos(a) * reach, y + WWMath::Sin(a) * reach);
		if (below < ground) {
			ground = below;
		}
	}
	const Real centreZ = ground - sink * radius;
	const Real foldZ = ground - radius;	// where the fold at the ring's edge takes it: under all of that ground
	const Int v = variant % ROCK_VARIANTS;
	const Real (*corners)[3] = m_rockCorners[v];
	const int (*faces)[3] = rockFaces();
	for (Int f = 0; f < ROCK_FACES; f++) {
		// the face, or its four quarters split at the edges' pushed-out middles
		Real tri[4][3][3];
		Int count = 1;
		const Int ia = faces[f][0], ib = faces[f][1], ic = faces[f][2];
		if (!detailed) {
			memcpy(tri[0][0], corners[ia], sizeof(tri[0][0]));
			memcpy(tri[0][1], corners[ib], sizeof(tri[0][1]));
			memcpy(tri[0][2], corners[ic], sizeof(tri[0][2]));
		} else {
			Real ab[3], bc[3], ca[3];
			rockMidpoint(v, ia, ib, corners, ab);
			rockMidpoint(v, ib, ic, corners, bc);
			rockMidpoint(v, ic, ia, corners, ca);
			const Real *quarters[4][3] = {
				{ corners[ia], ab, ca }, { ab, corners[ib], bc }, { ca, bc, corners[ic] }, { ab, bc, ca } };
			for (Int q = 0; q < 4; q++) {
				for (Int e = 0; e < 3; e++) {
					memcpy(tri[q][e], quarters[q][e], sizeof(tri[q][e]));
				}
			}
			count = 4;
		}
		for (Int q = 0; q < count; q++) {
			Vector3 local[3], world[3];
			for (Int e = 0; e < 3; e++) {
				local[e].Set(tri[q][e][0] * stretch, tri[q][e][1], tri[q][e][2]);
				world[e].Set(x + (local[e].X * c - local[e].Y * s) * radius, y + (local[e].X * s + local[e].Y * c) * radius,
					centreZ + local[e].Z * radius);
			}
			Vector3 normal;
			Vector3::Cross_Product(world[1] - world[0], world[2] - world[0], &normal);
			normal.Normalize();
			const Vector3 outward = (world[0] + world[1] + world[2]) * (1.0f / 3.0f) - Vector3(x, y, centreZ);
			if (Vector3::Dot_Product(normal, outward) < 0.0f) {
				normal = normal * -1.0f;
			}
			Vector3 localNormal;
			Vector3::Cross_Product(local[1] - local[0], local[2] - local[0], &localNormal);
			const Real nx = fabsf(localNormal.X), ny = fabsf(localNormal.Y), nz = fabsf(localNormal.Z);
			const UnsignedInt lit = lightRock(normal, tint, 1.0f);
			const UnsignedInt contact = lightRock(normal, tint, ROCK_CONTACT);
			for (Int e = 0; e < 3; e++) {
				Real a, b;
				if (nz >= nx && nz >= ny) {
					a = local[e].X; b = local[e].Y;
				} else if (nx >= ny) {
					a = local[e].Y; b = local[e].Z;
				} else {
					a = local[e].X; b = local[e].Z;
				}
				const Real tu = WWMath::Clamp(0.5f + a / 3.6f, 0.0f, 1.0f), tv = WWMath::Clamp(0.5f + b / 3.6f, 0.0f, 1.0f);
				VertexFormatXYZNDUV1 vertex;
				// the ring where it meets the ground darker, the contact shadow a rock casts on itself
				const Bool atGround = world[e].Z < ground + 0.15f * radius;
				putVertex(vertex, world[e].X, world[e].Y, world[e].Z, 0.0f, foldZ, atGround ? contact : lit,
					u0 + (u1 - u0) * tu, v0 + (v1 - v0) * tv);
				m_indices.push_back((UnsignedShort)m_vertices.size());
				m_vertices.push_back(vertex);
			}
		}
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Bake one chunk: the pieces that never thin first, then the cards in rank order, so drawing the
	first n of those is an even thinning. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::buildChunk(Slot &slot, Int key)
{
	slot.key = key;
	slot.dirty = FALSE;
	slot.fixedPieces = 0;
	slot.thinPieces = 0;
	slot.ends.clear();

	const Int cx = key % m_chunksX, cy = key / m_chunksX;
	const Int x0 = cx * CHUNK_CELLS, y0 = cy * CHUNK_CELLS;
	const Int x1 = min(x0 + CHUNK_CELLS, m_width - 1), y1 = min(y0 + CHUNK_CELLS, m_height - 1);
	m_scratch.clear();
	for (Int y = y0; y < y1; y++) {
		for (Int x = x0; x < x1; x++) {
			const UnsignedByte *cell = &m_cells[((size_t)y * m_width + x) * CELL_BYTES];
			ChunkPiece cp;
			cp.x = (Short)x;
			cp.y = (Short)y;
			for (Int k = 0; k < MAX_TUFTS_PER_CELL; k++) {
				if (tuftPresent(x, y, k, cell[BYTE_GRASS])) {
					cp.rank = placeTuft(x, y, k).rank;
					cp.piece = PIECE_TUFT;
					cp.k = (UnsignedByte)k;
					cp.thins = TRUE;
					m_scratch.push_back(cp);
				}
			}
			for (Int p = PIECE_SHRUB; p < PIECE_KINDS; p++) {
				if (piecePresent(x, y, (Piece)p, cell)) {
					cp.rank = placeTuft(x, y, pieceSlot((Piece)p)).rank;
					cp.piece = (UnsignedByte)p;
					cp.k = 0;
					cp.thins = pieceThins((Piece)p);
					m_scratch.push_back(cp);
				}
			}
		}
	}
	if (m_scratch.empty()) {
		return;
	}
	std::sort(m_scratch.begin(), m_scratch.end(), [](const ChunkPiece &a, const ChunkPiece &b) {
		if (a.thins != b.thins) return !a.thins;
		if (a.rank != b.rank) return a.rank < b.rank;
		if (a.y != b.y) return a.y < b.y;
		if (a.x != b.x) return a.x < b.x;
		if (a.piece != b.piece) return a.piece < b.piece;
		return a.k < b.k;
	});

	WorldHeightMap *map = m_terrain->getMap();
	m_vertices.clear();
	m_indices.clear();
	for (size_t n = 0; n < m_scratch.size(); n++) {
		const ChunkPiece &cp = m_scratch[n];
		const UnsignedByte *cell = &m_cells[((size_t)cp.y * m_width + cp.x) * CELL_BYTES];
		const Piece piece = (Piece)cp.piece;
		const Tuft t = placeTuft(cp.x, cp.y, piece == PIECE_TUFT ? cp.k : pieceSlot(piece));
		const Real wx = (cp.x - m_border + t.fx) * MAP_XY_FACTOR;
		const Real wy = (cp.y - m_border + t.fy) * MAP_XY_FACTOR;
		const Int rockAtlas = rockCell((Rock)(cell[BYTE_FLAGS] & CF_ROCK_MASK));
		const Int variant = (Int)(t.rank >> 24);
		Bool added = FALSE;
		switch (piece) {
		case PIECE_TUFT:
		case PIECE_SHRUB: {
			const Vector3 normal = cellNormal(map, cp.x, cp.y, NORMAL_TO_UP);
			const UnsignedInt top = lightOn(normal, 1.0f);
			const UnsignedInt root = lightOn(normal, ROOT_SHADE);
			const Real sway = (Real)swayType(wx, wy, m_swayDirX, m_swayDirY, GUST_WAVE);
			if (piece == PIECE_TUFT) {
				const Real h = TUFT_HEIGHT * t.scale;
				added = addCards(wx, wy, t.yaw, 2, h, h * TUFT_HALF_WIDTH, TUFT_SINK, t.atlasCell, t.mirror, sway, top, root);
			} else {
				const Real h = SHRUB_HEIGHT * t.scale;
				const Int bush = shrubCell(cp.x, cp.y, cell);
				UnsignedInt bushTop = top, bushRoot = root;
				if (bush != ATLAS_SHRUB_SCRUB) {
					const Vector3 tint = bushTint(map, wx, wy);
					bushTop = tinted(top, tint);
					bushRoot = tinted(root, tint);
				}
				added = addCards(wx, wy, t.yaw, 3, h, h * SHRUB_HALF_WIDTH, SHRUB_SINK, bush,
					t.mirror, sway, bushTop, bushRoot, h * SHRUB_HALF_WIDTH * CANOPY_SHARE);
			}
			break;
		}
		case PIECE_TWIGS:
			added = addFlat(wx, wy, t.yaw * 4.0f, TWIG_HALF * t.scale, ATLAS_TWIGS,
				lightOn(cellNormal(map, cp.x, cp.y, 0.0f), 1.0f));
			break;
		case PIECE_PEBBLES:
			// the ground's own colour, or grey gravel on warm sand reads as a stain
			added = addFlat(wx, wy, t.yaw * 4.0f, PEBBLE_HALF * t.scale, ATLAS_PEBBLES,
				tinted(lightOn(cellNormal(map, cp.x, cp.y, 0.0f), 1.0f), groundTint(map, wx, wy)));
			break;
		case PIECE_STONE:
			added = addRock(wx, wy, t.yaw * 4.0f, STONE_RADIUS * t.scale, 1.0f + 0.4f * t.fx, variant, rockAtlas,
				FALSE, ROCK_SINK, groundTint(map, wx, wy));
			break;
		case PIECE_BOULDER:
			// resting on the ground at the cliff's foot, a quarter of them half buried
			added = addRock(wx, wy, t.yaw * 4.0f, BOULDER_RADIUS * t.scale, 1.0f + 0.35f * t.fy, variant, rockAtlas,
				TRUE, ROCK_SINK + ((t.rank & 3) == 0 ? BOULDER_BURY : 0.0f), groundTint(map, wx, wy));
			break;
		default:
			break;
		}
		if (!added) {
			break;	// the chunk is full: what is left is the last of the cards by rank
		}
		slot.ends.push_back((UnsignedInt)m_indices.size() | ((UnsignedInt)m_vertices.size() << 16));
		if (cp.thins) {
			slot.thinPieces++;
		} else {
			slot.fixedPieces++;
		}
	}
	if (!uploadChunk(slot)) {
		releaseSlot(slot);	// a device that has gone away: the next bake makes new buffers
	}
}

//-------------------------------------------------------------------------------------------------
/** The baked chunk into its slot's buffers, grown in BUFFER_STEP when it does not fit. */
//-------------------------------------------------------------------------------------------------
Bool W3DGrassBuffer::uploadChunk(Slot &slot)
{
	const Int vertices = (Int)m_vertices.size(), indices = (Int)m_indices.size();
	if (vertices == 0) {
		return TRUE;
	}
	if (vertices > slot.vbCapacity) {
		REF_PTR_RELEASE(slot.vb);
		slot.vbCapacity = (vertices + BUFFER_STEP - 1) / BUFFER_STEP * BUFFER_STEP;
		slot.vb = NEW_REF(DX8VertexBufferClass, (DX8_FVF_XYZNDUV1, slot.vbCapacity, DX8VertexBufferClass::USAGE_DEFAULT));
	}
	if (indices > slot.ibCapacity) {
		REF_PTR_RELEASE(slot.ib);
		slot.ibCapacity = (indices + BUFFER_STEP - 1) / BUFFER_STEP * BUFFER_STEP;
		slot.ib = NEW_REF(DX8IndexBufferClass, (slot.ibCapacity, DX8IndexBufferClass::USAGE_DEFAULT));
	}
	{
		DX8VertexBufferClass::WriteLockClass lock(slot.vb, 0);
		VertexFormatXYZNDUV1 *vb = (VertexFormatXYZNDUV1 *)lock.Get_Vertex_Array();
		if (vb == NULL) {
			return FALSE;
		}
		memcpy(vb, &m_vertices[0], vertices * sizeof(VertexFormatXYZNDUV1));
	}
	{
		DX8IndexBufferClass::WriteLockClass lock(slot.ib, 0);
		UnsignedShort *ib = lock.Get_Index_Array();
		if (ib == NULL) {
			return FALSE;
		}
		memcpy(ib, &m_indices[0], indices * sizeof(UnsignedShort));
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Draw what lies near the tactical view's focus.  From renderTrees, after the trees: never the sun
	pass (none of it casts) and never the water's mirror. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::drawGrass(CameraClass *camera, Int contentVersion, const Matrix3D &world)
{
	if (!drawable() || !m_anyFoliage || camera == NULL || m_terrain->getMap() == NULL) {
		return;
	}
	if (ShaderClass::Is_Backface_Culling_Inverted()) {
		return;
	}
	if (!makeDeviceObjects()) {
		return;
	}
	++m_drawFrame;

	if (contentVersion != m_contentVersion) {
		m_contentVersion = contentVersion;	// heights, lighting, time of day
		dirtyAll();
	}

	// The sway types are baked against the wind's direction; the phase walks once a client frame,
	// like the trees' (W3DTreeBuffer::drawTrees).
	const BreezeInfo &info = m_breezeForDraw;
	Real dirX = info.m_directionVec.x, dirY = info.m_directionVec.y;
	if (dirX == 0.0f && dirY == 0.0f) {
		dirX = 1.0f;
	}
	if (dirX != m_swayDirX || dirY != m_swayDirY) {
		m_swayDirX = dirX;
		m_swayDirY = dirY;
		dirtyAll();
	}
	const UnsignedInt now = TheGameClient ? TheGameClient->getFrame() : 0;
	if (!m_pauseForDraw && now != m_lastPhaseFrame) {
		m_lastPhaseFrame = now;
		const Real period = info.m_breezePeriod > 0 ? info.m_breezePeriod * SWAY_PERIOD_SHARE : (Real)LOGICFRAMES_PER_SECOND;
		m_phase += 2.0f * PI / period;
		if (m_phase > 2.0f * PI) {
			m_phase -= 2.0f * PI;
		}
	}

	Coord3D focus;
	if (TheTacticalView) {
		TheTacticalView->getPosition(&focus);
	} else {
		const Vector3 eye = camera->Get_Position();
		focus.set(eye.X, eye.Y, 0.0f);
	}

	// the chunks within the radius and in the frustum, nearest first
	struct Near { Int key; Real dist; };
	enum { MAX_NEAR = 128 };
	Near nearby[MAX_NEAR];	// not "near": windef.h defines it
	Int numNear = 0;
	const Real chunkWorld = CHUNK_CELLS * MAP_XY_FACTOR;
	const Real origin = -m_border * MAP_XY_FACTOR;
	const Real minZ = m_terrain->getMinHeight(), maxZ = m_terrain->getMaxHeight() + SHRUB_HEIGHT * 1.5f;
	const Int cx0 = max(0, (Int)floorf((focus.x - GRASS_RADIUS - origin) / chunkWorld));
	const Int cy0 = max(0, (Int)floorf((focus.y - GRASS_RADIUS - origin) / chunkWorld));
	const Int cx1 = min(m_chunksX - 1, (Int)floorf((focus.x + GRASS_RADIUS - origin) / chunkWorld));
	const Int cy1 = min(m_chunksY - 1, (Int)floorf((focus.y + GRASS_RADIUS - origin) / chunkWorld));
	for (Int cy = cy0; cy <= cy1; cy++) {
		for (Int cx = cx0; cx <= cx1 && numNear < MAX_NEAR; cx++) {
			const Real x0 = origin + cx * chunkWorld, y0 = origin + cy * chunkWorld;
			const Real dx = max(0.0f, max(x0 - focus.x, focus.x - (x0 + chunkWorld)));
			const Real dy = max(0.0f, max(y0 - focus.y, focus.y - (y0 + chunkWorld)));
			const Real dist = sqrtf(dx * dx + dy * dy);
			if (dist > GRASS_RADIUS) {
				continue;
			}
			// a cell past the chunk's edge for the shrubs and boulders that lean over it
			AABoxClass box(Vector3(x0 + chunkWorld * 0.5f, y0 + chunkWorld * 0.5f, (minZ + maxZ) * 0.5f),
				Vector3(chunkWorld * 0.5f + MAP_XY_FACTOR, chunkWorld * 0.5f + MAP_XY_FACTOR, (maxZ - minZ) * 0.5f + 1.0f));
			if (camera->Cull_Box(box)) {
				continue;
			}
			nearby[numNear].key = cy * m_chunksX + cx;
			nearby[numNear].dist = dist;
			numNear++;
		}
	}
	std::sort(nearby, nearby + numNear, [](const Near &a, const Near &b) {
		return a.dist != b.dist ? a.dist < b.dist : a.key < b.key;
	});

	// a slot each, at most BUILDS_PER_FRAME baked this frame; a dirty one draws its old pieces meanwhile
	Slot *draw[MAX_NEAR];
	Real share[MAX_NEAR];
	Int numDraw = 0;
	Int builds = 0;
	Int wantedThin = 0;
	for (Int n = 0; n < numNear; n++) {
		const Int key = nearby[n].key;
		Slot *slot = NULL;
		for (Int i = 0; i < MAX_SLOTS; i++) {
			if (m_slots[i].key == key) {
				slot = &m_slots[i];
				break;
			}
		}
		if (slot && slot->dirty && builds < BUILDS_PER_FRAME) {
			buildChunk(*slot, key);
			builds++;
			if (slot->key != key) {
				continue;	// the upload failed
			}
		}
		if (slot == NULL && builds < BUILDS_PER_FRAME) {
			// a free slot, or the one drawn longest ago that this frame has not claimed
			for (Int i = 0; i < MAX_SLOTS; i++) {
				if (m_slots[i].key < 0) {
					slot = &m_slots[i];
					break;
				}
				if (m_slots[i].lastUsed != m_drawFrame && (slot == NULL || m_slots[i].lastUsed < slot->lastUsed)) {
					slot = &m_slots[i];
				}
			}
			if (slot) {
				buildChunk(*slot, key);
				builds++;
				if (slot->key != key) {
					continue;
				}
			}
		}
		if (slot == NULL) {
			continue;
		}
		slot->lastUsed = m_drawFrame;
		if (slot->fixedPieces + slot->thinPieces == 0) {
			continue;
		}
		Real s = (GRASS_RADIUS - nearby[n].dist) / GRASS_FADE_WIDTH;
		s = WWMath::Clamp(s, GRASS_MIN_SHARE, 1.0f);
		draw[numDraw] = slot;
		share[numDraw] = s;
		wantedThin += (Int)(slot->thinPieces * s + 0.5f);
		numDraw++;
	}
	if (numDraw == 0) {
		return;
	}
	const Real budget = wantedThin > MAX_DRAWN_THIN ? (Real)MAX_DRAWN_THIN / wantedThin : 1.0f;

	// The tree pass's states, constants and shroud (W3DTreeBuffer::drawTrees).  The world matrix is
	// the terrain's, set only here, past every way out, so a frame without grass sets nothing: apply
	// before reading the transforms back, or the device hands over whatever was drawn last.
	DX8Wrapper::Set_Transform(D3DTS_WORLD, world);
	DX8Wrapper::Apply_Render_State_Changes();
	D3DXMATRIX mat, matProj, matView, matWorld;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_WORLD, *(Matrix4x4*)&matWorld);
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, *(Matrix4x4*)&matView);
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, *(Matrix4x4*)&matProj);
	D3DXMatrixMultiply(&mat, &matView, &matProj);
	D3DXMatrixMultiply(&mat, &matWorld, &mat);
	D3DXMatrixTranspose(&mat, &mat);

	DX8Wrapper::Set_Shader(grassShader);
	DX8Wrapper::Set_Texture(0, m_texture);
	DX8Wrapper::Set_Texture(1, NULL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, 1);
	DX8Wrapper::Apply_Render_State_Changes();
	W3DShaderManager::setShroudTex(1);
	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF, GRASS_ALPHAREF);

	// c4 world-view-projection, c8 still (rocks and flat cards), c9..c18 the sway, c19/c20 the fold
	// (Direct3D 11's program only), c32/c33 the shroud
	DX8Wrapper::Set_Vertex_Shader_Constant(4, &mat, 4);
	Vector4 still(0, 0, 0, 0);
	DX8Wrapper::Set_Vertex_Shader_Constant(8, &still, 1);
	for (Int k = 0; k < SWAY_TYPES; k++) {
		Real bend = info.m_lean + SWAY_GAIN * info.m_intensity * WWMath::Sin(m_phase - 2.0f * PI * k / SWAY_TYPES);
		bend = WWMath::Clamp(bend, -SWAY_MAX, SWAY_MAX);
		const Real s = WWMath::Sin(bend);
		Vector4 sway(m_swayDirX * s, m_swayDirY * s, WWMath::Cos(bend) - 1.0f, 0);
		DX8Wrapper::Set_Vertex_Shader_Constant(9 + k, &sway, 1);
	}
	Vector4 fade(focus.x, focus.y, 0.0f, GRASS_RADIUS);
	DX8Wrapper::Set_Vertex_Shader_Constant(19, &fade, 1);
	Vector4 fadeWidth(1.0f / GRASS_FADE_WIDTH, 0.0f, 0.0f, 0.0f);
	DX8Wrapper::Set_Vertex_Shader_Constant(20, &fadeWidth, 1);
	W3DShroud *shroud = m_terrain->getShroud();
	if (shroud) {
		Real width = shroud->getCellWidth();
		Real height = shroud->getCellHeight();
		Vector4 offset(-(float)shroud->getDrawOriginX() + width, -(float)shroud->getDrawOriginY() + height, 0, 0);
		DX8Wrapper::Set_Vertex_Shader_Constant(32, &offset, 1);
		offset.Set(1.0f / (width * shroud->getTextureWidth()), 1.0f / (height * shroud->getTextureHeight()), 1, 1);
		DX8Wrapper::Set_Vertex_Shader_Constant(33, &offset, 1);
	} else {
		Vector4 offset(0, 0, 0, 0);
		DX8Wrapper::Set_Vertex_Shader_Constant(32, &offset, 1);
		DX8Wrapper::Set_Vertex_Shader_Constant(33, &offset, 1);
	}

	LPDIRECT3DDEVICE9 dev = DX8Wrapper::_Get_D3D_Device();
	for (Int n = 0; n < numDraw; n++) {
		const Slot &slot = *draw[n];
		const Int thin = min(slot.thinPieces, (Int)(slot.thinPieces * share[n] * budget + 0.5f));
		const Int pieces = slot.fixedPieces + thin;
		if (pieces <= 0 || pieces > (Int)slot.ends.size()) {
			continue;
		}
		const UnsignedInt end = slot.ends[pieces - 1];
		const Int indices = (Int)(end & 0xFFFF), vertices = (Int)(end >> 16);
		DX8Wrapper::Set_Index_Buffer(slot.ib, 0);
		DX8Wrapper::Set_Vertex_Buffer(slot.vb);
		DX8Wrapper::Apply_Render_State_Changes();
		// the declaration beside the shader, every draw: the FVF the buffer set has no slot the
		// sway rides in (the w3d-renderer rule's black-trees trap)
		if (dev) {
			dev->SetVertexDeclaration(m_vertexDeclaration);
		}
		DX8Wrapper::Set_Vertex_Shader(m_vertexShader);
		DX8Wrapper::Draw_Triangles(0, indices / 3, 0, vertices);
	}

	DX8Wrapper::Set_Vertex_Format(DX8_FVF_XYZNDUV1);
	DX8Wrapper::Set_Pixel_Shader(NULL);
	DX8Wrapper::Invalidate_Cached_Render_States();	// the alpha reference and the stages went in behind the wrapper
}
