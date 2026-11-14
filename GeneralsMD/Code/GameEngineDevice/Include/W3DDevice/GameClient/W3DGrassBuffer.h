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
// The GroundFoliage option: grass tufts on grass ground, swaying in the breeze.  The client's alone.
// The map's cells are classified once at load into a density grid this object owns
// (FoliagePlacement.h); chunks of 16 by 16 cells near the tactical view's focus are baked into a
// small pool of vertex buffers on demand and drawn after the trees with Trees.vso's own layout, the
// shader registered for Direct3D 11 as "shaders\\grass.vso".  Nothing in GameEngine includes this and
// nothing here writes logic: logic never knows the grass is there.

#pragma once

#ifndef __W3DGRASS_BUFFER_H_
#define __W3DGRASS_BUFFER_H_

#include "always.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "GameLogic/ScriptEngine.h"
#include <vector>

class BaseHeightMapRenderObjClass;
class WorldHeightMap;
class CameraClass;
class TextureClass;
class GeometryInfo;
class Matrix3D;

class W3DGrassBuffer
{
public:
	W3DGrassBuffer(BaseHeightMapRenderObjClass *terrain);
	~W3DGrassBuffer(void);

	/// A picture that can draw grass, fixed at startup: the Direct3D 11 frame or SDL3, not Classic, not -headless.
	static Bool available(void);
	/// available(), and the option on.
	static Bool wanted(void);
	/// The other terrain object's grid, for a terrain detail change mid-match: the buildings it
	/// cleared stay clear, and nothing has to read logic beside a tick.
	void takeGrid(const W3DGrassBuffer &from);

	/// Classify a map's cells, roads and placed structures.  Nothing for the map already classified,
	/// so a device reset re-entering initHeightData keeps the grid.
	void classify(WorldHeightMap *map);
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
	/// Device reset: everything the device holds goes; the next draw makes it again.
	void freeGrassBuffers(void);

private:
	enum {
		CHUNK_CELLS = 16,
		MAX_SLOTS = 48,
		MAX_CHUNK_TUFTS = CHUNK_CELLS * CHUNK_CELLS * 3,	///< FoliagePlacement::MAX_TUFTS_PER_CELL
		MAX_CHUNK_VERTICES = MAX_CHUNK_TUFTS * 8,
		MAX_CHUNK_INDICES = MAX_CHUNK_TUFTS * 12,
		BUILDS_PER_FRAME = 2,
		MAX_DRAWN_TUFTS = 25000
	};

	struct Slot
	{
		DX8VertexBufferClass *vb;
		Int key;							///< chunk index, -1 while free
		Int tufts;
		UnsignedInt lastUsed;	///< m_drawFrame it was last drawn on
		Bool dirty;
	};

	struct ChunkTuft
	{
		UnsignedInt rank;
		Short x, y;
		Short k;
	};

	BaseHeightMapRenderObjClass *m_terrain;
	const WorldHeightMap *m_classifiedMap;	///< compared, never dereferenced
	Bool m_anyGrass;												///< a desert map draws nothing and builds nothing
	std::vector<UnsignedByte> m_density;		///< a byte a cell, whole map, border included
	Int m_width;
	Int m_height;
	Int m_border;
	Int m_chunksX;
	Int m_chunksY;

	Slot m_slots[MAX_SLOTS];
	std::vector<ChunkTuft> m_scratch;
	DX8IndexBufferClass *m_indices;
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
	void dirtyCells(Int loX, Int loY, Int hiX, Int hiY);
	void dirtyAll(void);
	void buildChunk(Slot &slot, Int key);
	void clearRoadsAndMapStructures(void);
};

#endif  // end __W3DGRASS_BUFFER_H_
