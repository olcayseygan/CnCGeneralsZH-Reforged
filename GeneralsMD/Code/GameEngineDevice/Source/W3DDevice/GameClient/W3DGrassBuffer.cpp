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
// The GroundFoliage option's grass.  See W3DGrassBuffer.h and FoliagePlacement.h.

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

using namespace FoliagePlacement;

// Sizes are world units.  Infantry stand about twelve tall; a tuft comes to the knee.
#define GRASS_TEXTURE				"ReforgedFoliage.tga"
#define GRASS_TEXTURE_PATH	"Art/Textures/ReforgedFoliage.tga"
#define GRASS_SHADER_NAME		"shaders\\grass.vso"	///< what the Direct3D 11 backend knows this shader by
#define TUFT_HEIGHT					5.5f		///< at scale one
#define TUFT_HALF_WIDTH			0.52f		///< of the height: an atlas cell is 256 wide and 246 to the root
#define ROOT_SINK						0.35f		///< roots under the ground, so a slope never shows a gap
#define ROOT_SHADE					0.65f		///< the roots' share of the light
#define NORMAL_TO_UP				0.5f		///< how far a tuft's normal goes from the ground's toward straight up
#define GRASS_RADIUS				480.0f	///< around the tactical view's focus
#define GRASS_FADE_WIDTH		140.0f	///< the last stretch of it, thinned on the CPU and shrunk on the GPU
#define GRASS_MIN_SHARE			0.15f		///< of a chunk's tufts drawn at the radius itself
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
	UnsignedInt lightTuft(const Vector3 &normal, Real scale)
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
}

W3DGrassBuffer::W3DGrassBuffer(BaseHeightMapRenderObjClass *terrain) :
	m_terrain(terrain),
	m_classifiedMap(NULL),
	m_anyGrass(FALSE),
	m_width(0),
	m_height(0),
	m_border(0),
	m_chunksX(0),
	m_chunksY(0),
	m_indices(NULL),
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
		m_slots[i].key = -1;
		m_slots[i].tufts = 0;
		m_slots[i].lastUsed = 0;
		m_slots[i].dirty = FALSE;
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

void W3DGrassBuffer::takeGrid(const W3DGrassBuffer &from)
{
	clear();
	m_classifiedMap = from.m_classifiedMap;
	m_anyGrass = from.m_anyGrass;
	m_density = from.m_density;
	m_width = from.m_width;
	m_height = from.m_height;
	m_border = from.m_border;
	m_chunksX = from.m_chunksX;
	m_chunksY = from.m_chunksY;
}

void W3DGrassBuffer::clear(void)
{
	m_classifiedMap = NULL;
	m_anyGrass = FALSE;
	m_density.clear();
	m_width = m_height = m_border = m_chunksX = m_chunksY = 0;
	for (Int i = 0; i < MAX_SLOTS; i++) {
		m_slots[i].key = -1;
		m_slots[i].tufts = 0;
		m_slots[i].dirty = FALSE;
	}
}

void W3DGrassBuffer::freeGrassBuffers(void)
{
	for (Int i = 0; i < MAX_SLOTS; i++) {
		REF_PTR_RELEASE(m_slots[i].vb);
		m_slots[i].key = -1;
		m_slots[i].tufts = 0;
	}
	REF_PTR_RELEASE(m_indices);
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
/** The density grid.  Once a map, at its load, so TheTerrainLogic's water is read while no tick runs. */
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
	m_density.assign((size_t)m_width * m_height, 0);

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

	// tile -> density, through the tile's Terrain.ini class
	UnsignedByte tileDensity[NUM_SOURCE_TILES];
	memset(tileDensity, 0, sizeof(tileDensity));
	const Int numClasses = min(map->m_numTextureClasses, (Int)NUM_TEXTURE_CLASSES);
	const TXTextureClass *classes = map->m_textureClasses;
	for (Int i = 0; i < numClasses; i++) {
		if (classes[i].firstTile < 0) {
			continue;
		}
		TerrainType *terrain = TheTerrainTypes ? TheTerrainTypes->findTerrain(classes[i].name) : NULL;	// a map may name one its data lacks
		const Bool named = nameHasGrass(classes[i].name.str());
		const UnsignedByte density = terrain != NULL ? kindDensity(kindOf(terrain->getClass()), named) : (named ? 255 : 0);
		for (Int t = classes[i].firstTile; t < classes[i].firstTile + classes[i].numTiles && t < NUM_SOURCE_TILES; t++) {
			tileDensity[t] = density;
		}
	}

	for (Int y = 0; y < m_height - 1; y++) {
		for (Int x = 0; x < m_width - 1; x++) {
			const Int ndx = y * m_width + x;
			const Int baseTile = tiles[ndx] >> 2;
			const Int base = (baseTile >= 0 && baseTile < NUM_SOURCE_TILES) ? tileDensity[baseTile] : 0;
			Int overlay = 0, extra = 0;
			const Short b = blends ? blends[ndx] : 0;
			const Bool hasOverlay = b > 0 && b < NUM_BLEND_TILES;
			if (hasOverlay) {
				const Int t = blendTiles[b].blendNdx >> 2;
				overlay = (t >= 0 && t < NUM_SOURCE_TILES) ? tileDensity[t] : 0;
			}
			const Short e = extraBlends ? extraBlends[ndx] : 0;
			const Bool hasExtra = e > 0 && e < NUM_BLEND_TILES;
			if (hasExtra) {
				const Int t = extraBlendTiles[e].blendNdx >> 2;
				extra = (t >= 0 && t < NUM_SOURCE_TILES) ? tileDensity[t] : 0;
			}
			const UnsignedByte blended = blendMean(base, hasOverlay, overlay, hasExtra, extra);
			if (blended == 0) {
				continue;
			}
			Bool blocked = (cliffInfo && cliffInfo[ndx] != 0) || map->getCliffState(x, y)
				|| tooSteep(map->getHeight(x, y) * MAP_HEIGHT_SCALE, map->getHeight(x + 1, y) * MAP_HEIGHT_SCALE,
					map->getHeight(x, y + 1) * MAP_HEIGHT_SCALE, map->getHeight(x + 1, y + 1) * MAP_HEIGHT_SCALE, MAP_XY_FACTOR);
			if (!blocked && TheTerrainLogic) {
				blocked = TheTerrainLogic->isUnderwater((x - m_border + 0.5f) * MAP_XY_FACTOR, (y - m_border + 0.5f) * MAP_XY_FACTOR);
			}
			m_density[ndx] = cellDensity(blended, blocked);
		}
	}
	clearRoadsAndMapStructures();

	Int grassCells = 0;
	for (size_t i = 0; i < m_density.size(); i++) {
		if (m_density[i] != 0) {
			++grassCells;
		}
	}
	m_anyGrass = grassCells > 0;
	DEBUG_LOG(("GROUNDFOLIAGE %d of %d cells grow grass\n", grassCells, (Int)m_density.size()));
}

//-------------------------------------------------------------------------------------------------
/** The map's own roads, by the pairs of points W3DRoadBuffer::addMapObjects reads, and the
	structures it places. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::clearRoadsAndMapStructures(void)
{
	Grid grid = { &m_density[0], m_width, m_height, MAP_XY_FACTOR, -m_border * MAP_XY_FACTOR, -m_border * MAP_XY_FACTOR };
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
	if (m_density.empty() || pos == NULL) {
		return;
	}
	Grid grid = { &m_density[0], m_width, m_height, MAP_XY_FACTOR, -m_border * MAP_XY_FACTOR, -m_border * MAP_XY_FACTOR };
	Range range;
	if (clearGeometry(grid, *pos, geom, angle, &range) > 0) {
		dirtyCells(range.loX, range.loY, range.hiX, range.hiY);
	}
}

void W3DGrassBuffer::clearStructures(void)
{
	if (m_density.empty() || TheGameLogic == NULL) {
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
	const Int cx0 = loX / CHUNK_CELLS, cx1 = hiX / CHUNK_CELLS;
	const Int cy0 = loY / CHUNK_CELLS, cy1 = hiY / CHUNK_CELLS;
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
/** The shader (Trees.vso, under its own name for Direct3D 11), the shared index buffer and the atlas. */
//-------------------------------------------------------------------------------------------------
Bool W3DGrassBuffer::makeDeviceObjects(void)
{
	if (m_vertexShader && m_indices && m_texture) {
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
		// Direct3D 11 draws it with the grass program (distance shrink, shadow receiving) and the
		// trees keep theirs.
		Direct3D11_Register_Engine_Shader(m_vertexShader, GRASS_SHADER_NAME);
	}
	if (m_indices == NULL) {
		m_indices = NEW_REF(DX8IndexBufferClass, (MAX_CHUNK_INDICES, DX8IndexBufferClass::USAGE_DEFAULT));
		Bool filled = FALSE;
		{
			DX8IndexBufferClass::WriteLockClass lock(m_indices, 0);
			UnsignedShort *ib = lock.Get_Index_Array();
			if (ib != NULL) {
				// tuft t is vertices 8t..8t+7, two quads of four
				for (Int q = 0; q < MAX_CHUNK_TUFTS * 2; q++) {
					const UnsignedShort v = (UnsignedShort)(q * 4);
					*ib++ = v; *ib++ = v + 1; *ib++ = v + 2;
					*ib++ = v; *ib++ = v + 2; *ib++ = v + 3;
				}
				filled = TRUE;
			}
		}
		if (!filled) {
			REF_PTR_RELEASE(m_indices);
			return FALSE;
		}
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Bake one chunk: its tufts in rank order, so drawing the first n is an even thinning. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::buildChunk(Slot &slot, Int key)
{
	slot.key = key;
	slot.dirty = FALSE;
	slot.tufts = 0;

	const Int cx = key % m_chunksX, cy = key / m_chunksX;
	const Int x0 = cx * CHUNK_CELLS, y0 = cy * CHUNK_CELLS;
	const Int x1 = min(x0 + CHUNK_CELLS, m_width - 1), y1 = min(y0 + CHUNK_CELLS, m_height - 1);
	m_scratch.clear();
	for (Int y = y0; y < y1; y++) {
		for (Int x = x0; x < x1; x++) {
			const UnsignedByte d = m_density[y * m_width + x];
			if (d == 0) {
				continue;
			}
			for (Int k = 0; k < MAX_TUFTS_PER_CELL; k++) {
				if (tuftPresent(x, y, k, d)) {
					ChunkTuft t;
					t.rank = placeTuft(x, y, k).rank;
					t.x = (Short)x;
					t.y = (Short)y;
					t.k = (Short)k;
					m_scratch.push_back(t);
				}
			}
		}
	}
	if (m_scratch.empty()) {
		return;
	}
	std::sort(m_scratch.begin(), m_scratch.end(), [](const ChunkTuft &a, const ChunkTuft &b) {
		if (a.rank != b.rank) return a.rank < b.rank;
		if (a.y != b.y) return a.y < b.y;
		if (a.x != b.x) return a.x < b.x;
		return a.k < b.k;
	});

	if (slot.vb == NULL) {
		slot.vb = NEW_REF(DX8VertexBufferClass, (DX8_FVF_XYZNDUV1, MAX_CHUNK_VERTICES, DX8VertexBufferClass::USAGE_DEFAULT));
	}
	DX8VertexBufferClass::WriteLockClass lock(slot.vb, 0);
	VertexFormatXYZNDUV1 *vb = (VertexFormatXYZNDUV1 *)lock.Get_Vertex_Array();
	if (vb == NULL) {
		slot.key = -1;	// a device that has gone away: free the slot, so the chunk is baked again
		return;
	}

	WorldHeightMap *map = m_terrain->getMap();
	const Real atlasU = 1.0f / (256.0f * ATLAS_COLUMNS), atlasV = 1.0f / (256.0f * ATLAS_ROWS);
	const Int count = min((Int)m_scratch.size(), (Int)MAX_CHUNK_TUFTS);
	for (Int n = 0; n < count; n++) {
		const ChunkTuft &ct = m_scratch[n];
		const Tuft t = placeTuft(ct.x, ct.y, ct.k);
		const Real wx = (ct.x - m_border + t.fx) * MAP_XY_FACTOR;
		const Real wy = (ct.y - m_border + t.fy) * MAP_XY_FACTOR;
		const Real z = m_terrain->getDrawnHeight(wx, wy) - ROOT_SINK;
		const Real h = TUFT_HEIGHT * t.scale;
		const Real hw = h * TUFT_HALF_WIDTH;

		// the cell's slope, bent toward straight up: a tuft is lit like the ground it stands on, only softer
		const Real h00 = map->getHeight(ct.x, ct.y) * MAP_HEIGHT_SCALE, h10 = map->getHeight(ct.x + 1, ct.y) * MAP_HEIGHT_SCALE;
		const Real h01 = map->getHeight(ct.x, ct.y + 1) * MAP_HEIGHT_SCALE, h11 = map->getHeight(ct.x + 1, ct.y + 1) * MAP_HEIGHT_SCALE;
		Vector3 normal(-((h10 - h00) + (h11 - h01)) * 0.5f, -((h01 - h00) + (h11 - h10)) * 0.5f, MAP_XY_FACTOR);
		normal.Normalize();
		normal = normal * (1.0f - NORMAL_TO_UP) + Vector3(0.0f, 0.0f, NORMAL_TO_UP);
		normal.Normalize();
		const UnsignedInt top = lightTuft(normal, 1.0f);
		const UnsignedInt root = lightTuft(normal, ROOT_SHADE);

		const Int col = t.atlasCell % ATLAS_COLUMNS, row = t.atlasCell / ATLAS_COLUMNS;
		Real u0 = (col * 256 + 0.5f) * atlasU, u1 = (col * 256 + 255.5f) * atlasU;
		if (t.mirror) {
			const Real swap = u0; u0 = u1; u1 = swap;
		}
		const Real vTop = (row * 256 + 0.5f) * atlasV, vRoot = (row * 256 + 246.0f) * atlasV;
		const Real sway = (Real)swayType(wx, wy, m_swayDirX, m_swayDirY, GUST_WAVE);

		for (Int q = 0; q < 2; q++) {
			const Real yaw = t.yaw + q * (PI * 0.5f);
			const Real ax = WWMath::Cos(yaw) * hw, ay = WWMath::Sin(yaw) * hw;
			const Real px[4] = { wx - ax, wx + ax, wx + ax, wx - ax };
			const Real py[4] = { wy - ay, wy + ay, wy + ay, wy - ay };
			const Real pz[4] = { z, z, z + h, z + h };
			const Real pu[4] = { u0, u1, u1, u0 };
			const Real pv[4] = { vRoot, vRoot, vTop, vTop };
			for (Int c = 0; c < 4; c++) {
				vb->x = px[c];
				vb->y = py[c];
				vb->z = pz[c];
				vb->nx = sway;		// Trees.vso: c[8 + type]
				vb->ny = 1.0f;		// the colour factor
				vb->nz = z;			// the root: (z - root) is how far the sway moves a vertex
				vb->diffuse = c < 2 ? root : top;
				vb->u1 = pu[c];
				vb->v1 = pv[c];
				++vb;
			}
		}
	}
	slot.tufts = count;
}

//-------------------------------------------------------------------------------------------------
/** Draw the grass near the tactical view's focus.  From renderTrees, after the trees: never the sun
	pass (grass casts nothing) and never the water's mirror. */
//-------------------------------------------------------------------------------------------------
void W3DGrassBuffer::drawGrass(CameraClass *camera, Int contentVersion, const Matrix3D &world)
{
	if (!wanted() || !m_anyGrass || camera == NULL || m_terrain == NULL || m_terrain->getMap() == NULL) {
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
	const Real minZ = m_terrain->getMinHeight(), maxZ = m_terrain->getMaxHeight() + TUFT_HEIGHT * 1.5f;
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
			AABoxClass box(Vector3(x0 + chunkWorld * 0.5f, y0 + chunkWorld * 0.5f, (minZ + maxZ) * 0.5f),
				Vector3(chunkWorld * 0.5f, chunkWorld * 0.5f, (maxZ - minZ) * 0.5f + 1.0f));
			if (camera->Cull_Box(box)) {
				continue;
			}
			nearby[numNear].key = cy * m_chunksX + cx;
			nearby[numNear].dist = dist;
			numNear++;
		}
	}
	std::sort(nearby, nearby + numNear,[](const Near &a, const Near &b) {
		return a.dist != b.dist ? a.dist < b.dist : a.key < b.key;
	});

	// a slot each, at most BUILDS_PER_FRAME baked this frame; a dirty one draws its old tufts meanwhile
	Slot *draw[MAX_NEAR];
	Real share[MAX_NEAR];
	Int numDraw = 0;
	Int builds = 0;
	Int wantedTufts = 0;
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
			}
		}
		if (slot == NULL) {
			continue;
		}
		slot->lastUsed = m_drawFrame;
		if (slot->tufts == 0) {
			continue;
		}
		Real s = (GRASS_RADIUS - nearby[n].dist) / GRASS_FADE_WIDTH;
		s = WWMath::Clamp(s, GRASS_MIN_SHARE, 1.0f);
		draw[numDraw] = slot;
		share[numDraw] = s;
		wantedTufts += (Int)(slot->tufts * s + 0.5f);
		numDraw++;
	}
	if (numDraw == 0) {
		return;
	}
	const Real budget = wantedTufts > MAX_DRAWN_TUFTS ? (Real)MAX_DRAWN_TUFTS / wantedTufts : 1.0f;

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

	// c4 world-view-projection, c8 still, c9..c18 the sway, c19/c20 the fade (Direct3D 11's
	// program only), c32/c33 the shroud
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
	DX8Wrapper::Set_Index_Buffer(m_indices, 0);
	for (Int n = 0; n < numDraw; n++) {
		const Int tufts = min(draw[n]->tufts, (Int)(draw[n]->tufts * share[n] * budget + 0.5f));
		if (tufts <= 0) {
			continue;
		}
		DX8Wrapper::Set_Vertex_Buffer(draw[n]->vb);
		DX8Wrapper::Apply_Render_State_Changes();
		// the declaration beside the shader, every draw: the FVF the buffer set has no slot the
		// sway rides in (the w3d-renderer rule's black-trees trap)
		if (dev) {
			dev->SetVertexDeclaration(m_vertexDeclaration);
		}
		DX8Wrapper::Set_Vertex_Shader(m_vertexShader);
		DX8Wrapper::Draw_Triangles(0, tufts * 4, 0, tufts * 8);
	}

	DX8Wrapper::Set_Vertex_Format(DX8_FVF_XYZNDUV1);
	DX8Wrapper::Set_Pixel_Shader(NULL);
	DX8Wrapper::Invalidate_Cached_Render_States();	// the alpha reference and the stages went in behind the wrapper
}
