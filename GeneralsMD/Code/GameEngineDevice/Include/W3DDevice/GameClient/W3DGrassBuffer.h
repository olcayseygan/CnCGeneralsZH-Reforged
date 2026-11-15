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

// FILE: W3DGrassBuffer.h
// The GroundFoliage option: grass tufts and shrubs swaying in the breeze, twigs and pebbles lying on
// the ground, stones on bare ground and boulders on cliffs.  The client's alone.  The map's cells are
// classified once at load into a grid this object owns (FoliagePlacement.h); chunks of 16 by 16 cells
// near the tactical view's focus are baked into a small pool of buffers on demand and drawn after the
// trees with Trees.vso's own layout, the shader registered for Direct3D 11 as "shaders\\grass.vso".
// Nothing in GameEngine includes this and nothing here writes logic: logic never knows it is there.

#pragma once

#ifndef __W3DGRASS_BUFFER_H_
#define __W3DGRASS_BUFFER_H_

#include "always.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "GameLogic/ScriptEngine.h"
#include "W3DDevice/GameClient/FoliagePlacement.h"
#include <vector>

class BaseHeightMapRenderObjClass;
class WorldHeightMap;
class CameraClass;
class TextureClass;
class GeometryInfo;
class Matrix3D;
class Vector3;
class AABoxClass;
class W3DTreeBuffer;

class W3DGrassBuffer
{
public:
	W3DGrassBuffer(BaseHeightMapRenderObjClass *terrain);
	~W3DGrassBuffer(void);

	/// A picture that can draw grass, fixed at startup: the Direct3D 11 frame or SDL3, not Classic, not -headless.
	static Bool available(void);
	/// available(), and the option on.
	static Bool wanted(void);
	/// wanted(), on ground this terrain object draws subdivided: getDrawnHeight is the ground drawn
	/// only there.  The lower terrain detail and -noTerrainSubdivide draw none.
	Bool drawable(void) const;
	/// The other terrain object's grid, for a terrain detail change mid-match: the buildings it
	/// cleared stay clear, and nothing has to read logic beside a tick.
	void takeGrid(const W3DGrassBuffer &from);

	/// At the render's sync point, outside the logic window: the grid for the drawn map, built the
	/// first time the option is on and the ground is subdivided (at a load, or when the option is
	/// switched on mid-match), with every structure standing by then cleared.  Nothing otherwise.
	void classifyIfWanted(WorldHeightMap *map, const W3DTreeBuffer *trees);
	/// Classify a map's cells, roads, placed structures and the groves its trees stand in.  Nothing
	/// for the map already classified, so a device reset keeps the grid.
	void classify(WorldHeightMap *map, const W3DTreeBuffer *trees);
	/// Forget the map: the next classify builds the grid again.
	void clear(void);
	/// A building going up: no grass under it, for the rest of the match.
	void clearFootprint(const Coord3D *pos, const GeometryInfo &geom, Real angle);
	/// Every structure logic holds, after a save loaded.
	void clearStructures(void);
	/// The breeze and the pause, at the render's sync point (W3DTreeBuffer::stepTrees does the same).
	void takeBreezeForDraw(void);
	/// Draw what is near the view's focus.  contentVersion is the terrain's: a change rebakes the chunks.
	/// world is the terrain's transform, set only when something is drawn.
	void drawGrass(CameraClass *camera, Int contentVersion, const Matrix3D &world);
	/// Into the sun's map, from renderTreeShadowCasters with the sun's camera applied: every upright
	/// piece of the chunks the screen drew last, as it drew them (same sway, same thinning, the atlas's
	/// alpha cut), the flat cards left out.  Builds nothing.  Returns the pieces cast.
	Int drawGrassCasters(CameraClass *sunCamera, const Matrix3D &world);
	/// Device reset: everything the device holds goes; the next draw makes it again.
	void freeGrassBuffers(void);

private:
	enum {
		CHUNK_CELLS = 16,
		MAX_SLOTS = 96,							///< the far ring's tree chunks need room beside the near ones; buffers are made as used
		MAX_CHUNK_VERTICES = 16384,	///< a chunk stops taking pieces here; a full grass chunk is 6144
		MAX_CHUNK_INDICES = 32768,
		BUFFER_STEP = 2048,					///< a slot's buffers grow in these
		BUILDS_PER_FRAME = 2,
		MAX_DRAWN_THIN = 25000			///< cards (tufts, twigs, pebbles) drawn in a frame at most
	};

	/** One chunk's pieces, in its own buffers, by FoliagePlacement::PieceGroup: the big trees, the rest
		that never thin, their flat ones, then the upright and the flat cards in rank order.  ends[i] is
		where piece i stops, indices in the low half, vertices in the high. */
	struct Slot
	{
		DX8VertexBufferClass *vb;
		DX8IndexBufferClass *ib;
		Int vbCapacity;
		Int ibCapacity;
		Int key;							///< chunk index, -1 while free
		Int groups[FoliagePlacement::PIECE_GROUPS];	///< pieces in each group
		Int drawnThin;				///< upright cards the screen's last draw of it took, which the sun's map takes too
		Bool drawnFarOnly;		///< and whether that draw was the far ring's, big trees alone
		std::vector<UnsignedInt> ends;
		UnsignedInt lastUsed;	///< m_drawFrame it was last drawn on
		Bool dirty;
	};

	struct ChunkPiece
	{
		UnsignedInt rank;
		Short x, y;
		UnsignedByte piece;		///< FoliagePlacement::Piece
		UnsignedByte k;				///< a tuft's slot
		UnsignedByte group;		///< FoliagePlacement::PieceGroup
	};

	BaseHeightMapRenderObjClass *m_terrain;
	const WorldHeightMap *m_classifiedMap;	///< compared, never dereferenced
	Bool m_anyFoliage;											///< a map with nothing to draw builds nothing
	std::vector<UnsignedByte> m_cells;			///< FoliagePlacement::CELL_BYTES a cell, whole map, border included
	std::vector<UnsignedByte> m_chunkFar;		///< a chunk a byte: it holds a big tree, so the far ring wants it
	Int m_width;
	Int m_height;
	Int m_border;
	Int m_chunksX;
	Int m_chunksY;

	Slot m_slots[MAX_SLOTS];
	std::vector<ChunkPiece> m_scratch;
	std::vector<VertexFormatXYZNDUV1> m_vertices;	///< a chunk baked here, then copied into its slot
	std::vector<UnsignedShort> m_indices;
	Real m_rockCorners[8][12][3];						///< FoliagePlacement::rockCorners, ROCK_VARIANTS of them
	Real m_bushCorners[6][37][3];						///< FoliagePlacement::bushCorners, BUSH_VARIANTS of them
	FoliagePlacement::TreeMesh m_deadTrees[FoliagePlacement::DEAD_TREE_VARIANTS];
	FoliagePlacement::TreeMesh m_firs[FoliagePlacement::FIR_VARIANTS];
	IDirect3DVertexShader9 *m_vertexShader;
	IDirect3DVertexDeclaration9 *m_vertexDeclaration;
	Bool m_deviceFailed;		///< the shader would not load; tried again after a device reset
	TextureClass *m_texture;
	Bool m_textureMissing;

	UnsignedInt m_drawFrame;
	Int m_contentVersion;
	BreezeInfo m_breezeForDraw;
	Bool m_pauseForDraw;
	Real m_swayDirX;			///< the wind the chunks were baked for: the sway types follow it
	Real m_swayDirY;
	Real m_phase;
	UnsignedInt m_lastPhaseFrame;

	Bool makeDeviceObjects(void);
	Coord3D drawFocus(CameraClass *camera) const;
	AABoxClass chunkBox(Int key) const;
	/// The tree pass's states, the shader and its constants, for the screen or the sun's map.
	void beginDraw(const Matrix3D &world, const Coord3D &focus, Bool sunPass);
	void endDraw(void);
	/// pieces first .. first + count - 1 of a slot, one draw
	void drawPieces(const Slot &slot, Int first, Int count);
	void dirtyCells(Int loX, Int loY, Int hiX, Int hiY);
	void dirtyAll(void);
	void buildChunk(Slot &slot, Int key);
	Bool uploadChunk(Slot &slot);
	void clearRoadsAndMapStructures(void);
	void flagNeighbours(WorldHeightMap *map);
	void addGroves(const W3DTreeBuffer *trees);
	void logCounts(void);
	// one piece into m_vertices / m_indices; FALSE when the chunk is full
	Bool addCards(Real x, Real y, Real yaw, Int planes, Real height, Real halfWidth, Real sink,
		Int atlasCell, Bool mirror, Real sway, UnsignedInt top, UnsignedInt root);
	Bool addFlat(Real x, Real y, Real yaw, Real half, Int atlasCell, UnsignedInt colour);
	Bool addBush(Real x, Real y, Real yaw, Real radius, Real height, Int variant, Int atlasCell, Real sway,
		const Vector3 &tint);
	Bool addTreeMesh(const FoliagePlacement::TreeMesh &mesh, Real x, Real y, Real yaw, Real height,
		const Vector3 &tint, const Vector3 &woodTint, Real dark);
	void releaseSlot(Slot &slot);	///< its buffers gone, the slot free
	Bool addRock(Real x, Real y, Real yaw, Real radius, Real stretch, Int variant, Int atlasCell,
		Bool detailed, Real sink, const Vector3 &tint);
};

#endif  // end __W3DGRASS_BUFFER_H_
