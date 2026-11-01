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


// WorldHeightMap.h
// Class to encapsulate height map.
// Author: John Ahlquist, April 2001

#pragma once

#ifndef WorldHeightMap_H
#define WorldHeightMap_H

#include "Lib/BaseType.h"
#include "WWLib/refcount.h"
#include "WWMath/vector3.h"
#include "W3DDevice/GameClient/TileData.h"
#include "../../GameEngine/Include/Common/MapObject.h"
#include "GameLogic/WorldHeightMapData.h"

#include "Common/STLTypedefs.h"


/** MapObject class 
Not ref counted.  Do not store pointers to this class.  */


#define NUM_SOURCE_TILES 1024
#define NUM_BLEND_TILES 16192
#define NUM_CLIFF_INFO 32384
#define FLAG_VAL 0x7ADA0000

// For backwards compatiblity.
#define TEX_PATH_LEN 256


/// Struct in memory.
typedef struct {
	Int globalTextureClass; 
	Int firstTile;
	Int numTiles;
	Int width;
	Int isBlendEdgeTile; ///< True if the texture contains blend edges.
	AsciiString name;
	ICoord2D positionInTexture;
} TXTextureClass;

typedef enum {POS_X, POS_Y, NEG_X, NEG_Y} TVDirection;
/// Struct in memory.
typedef struct {
	Real	u0, v0;	 // Upper left uv
	Real	u1, v1;	 // Lower left uv
	Real	u2, v2;	 // Lower right uv
	Real	u3, v3;	 // Upper right uv
	Bool  flip;
	Bool	mutant;  // Mutant mapping needed to get this to fit.
	Short tileIndex; // Tile texture.
} TCliffInfo;

#define NUM_TEXTURE_CLASSES 256


class TextureClass;
class ChunkInputStream;
class InputStream;
class OutputStream;
class DataChunkInput;
struct DataChunkInfo;
class AlphaEdgeTextureClass;
class TerrainTextureClass;		// the friends below name these, which declares them for lookup only on MSVC
class AlphaTerrainTextureClass;
class W3DCustomEdging;

#define NUM_ALPHA_TILES 12

// The heights, cells and cliff bits, and their parsing, are the WorldHeightMapData base's: the
// simulation samples them, so they live in gameengine (T1).  This class adds the terrain's drawing.
class WorldHeightMap : public RefCountClass,
                       public WorldHeightMapInterfaceClass,
                       public WorldHeightMapData
{
	friend class TerrainTextureClass;
	friend class AlphaTerrainTextureClass;
	friend class W3DCustomEdging;
	friend class AlphaEdgeTextureClass;

#define NO_EVAL_TILING_MODES

public:
#ifdef EVAL_TILING_MODES
	enum {TILE_4x4, TILE_6x6, TILE_8x8} m_tileMode;
#endif
	enum {
		NORMAL_DRAW_WIDTH = 129,
		NORMAL_DRAW_HEIGHT = 129,
		STRETCH_DRAW_WIDTH = 65,
		STRETCH_DRAW_HEIGHT = 65
	};

protected:

	
	Int m_numBitmapTiles;	// Number of tiles initialized from bitmaps in m_SourceTiles.
	Int m_numEdgeTiles;	// Number of tiles initialized from bitmaps in m_SourceTiles.
	Int m_numBlendedTiles;	// Number of blended tiles created from bitmap tiles.

	TileData			*m_sourceTiles[NUM_SOURCE_TILES];	///< Tiles for m_textureClasses
	TileData			*m_edgeTiles[NUM_SOURCE_TILES];	///< Tiles for m_textureClasses

	TBlendTileInfo	m_blendedTiles[NUM_BLEND_TILES];
	TBlendTileInfo	m_extraBlendedTiles[NUM_BLEND_TILES];

	TCliffInfo	m_cliffInfo[NUM_CLIFF_INFO];
	Int m_numCliffInfo; ///< Number of cliffInfo's used in m_cliffInfo.

	// Texture classes.  There is one texture class for each bitmap read in.
	// A class may have more than one tile.  For example, if the grass bitmap is 
	// 128x128, it creates 4 64x64 tiles, so the grass texture class will have 4 tiles.
	int m_numTextureClasses;
	TXTextureClass m_textureClasses[NUM_TEXTURE_CLASSES];

	// Edge Texture classes.  There is one texture class for each bitmap read in.
	// An edge class will normally have 4 tiles.
	int m_numEdgeTextureClasses;
	TXTextureClass m_edgeTextureClasses[NUM_TEXTURE_CLASSES];

	/** The actual texture used to render the 3d mesh.  Note that it is 
	 basically m_SourceTiles laid out in rows, so by itself it is not useful.
	 Use GetUVData to get the mapping info for height cells to map into the 
	 texture. */
	TerrainTextureClass *m_terrainTex;
	Int	m_terrainTexHeight; /// Height of m_terrainTex allocated.
	/** The texture that contains the alpha edge tiles that get blended on 
			top of the base texture. getAlphaUVData does the mapping. */
	AlphaTerrainTextureClass *m_alphaTerrainTex;
	Int	m_alphaTexHeight; /// Height of m_alphaTerrainTex allocated.

	/** The texture that contains custom blend edge tiles. */
	AlphaEdgeTextureClass *m_alphaEdgeTex;
	Int	m_alphaEdgeHeight; /// Height of m_alphaEdgeTex allocated.

	/// Drawing info - re the part of the map that is being drawn.
	Int m_drawOriginX;
	Int m_drawOriginY;
	Int m_drawWidthX;
	Int m_drawHeightY;

	/// Tiles that hold the alpha channel info.
	static TileData *m_alphaTiles[NUM_ALPHA_TILES];

	/** getExtraAlphaUVData's answer per blend tile, because the terrain asks again for every visible
		3-way tile on every render pass.  The answer depends on the blend tile, the cell's cliff info
		and where the tiles sit in the terrain texture; the first two are the key, and the last is
		m_extraBlendUVGeneration, bumped whenever the texture is laid out again. */
	struct ExtraBlendUV
	{
		Real U[4];
		Real V[4];
		UnsignedByte alpha[4];
		Bool flip;
		Bool cliff;
		Short cliffInfoNdx;
		Bool adjustCliff;
		Int generation;		///< 0 is never valid
	};
	std::vector<ExtraBlendUV> m_extraBlendUV;	///< indexed by blend tile, allocated on first use
	Int m_extraBlendUVGeneration;


protected:
	TileData *getSourceTile(UnsignedInt ndx) { if (ndx<NUM_SOURCE_TILES) return(m_sourceTiles[ndx]); return(NULL); };
	TileData *getEdgeTile(UnsignedInt ndx) { if (ndx<NUM_SOURCE_TILES) return(m_edgeTiles[ndx]); return(NULL); };
	/// UV mapping data for a cell to map into the terrain texture.
	void getUVForNdx(Int ndx, float *minU, float *minV, float *maxU, float*maxV, Bool fullTile);
	Bool getUVForTileIndex(Int ndx, Short tileNdx, float U[4], float V[4], Bool fullTile);
	Int getTextureClassFromNdx(Int tileNdx);
	void readTexClass(TXTextureClass *texClass, TileData **tileData); 
	Int updateTileTexturePositions(Int *edgeHeight); ///< Places each tile in the texture.

protected:	 // file reader callbacks.
	static Bool ParseHeightMapDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseSizeOnlyInChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseBlendTileDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	Bool ParseBlendTileData(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseObjectsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseObjectDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	static Bool ParseLightingDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);

protected:
	WorldHeightMap(void);			///< Simple constructor for WorldHeightMapEdit class.

public: // constructors/destructors
	WorldHeightMap(ChunkInputStream *pFile, Bool bHMapOnly=false);	// read from file.
	~WorldHeightMap(void);			// destroy.

public:  // Boundary info

public:  // height map info.


	

	inline Int getDrawOrgX(void) {return m_drawOriginX;}
	inline Int getDrawOrgY(void) {return m_drawOriginY;}

	inline Int getDrawWidth(void) {return m_drawWidthX;}
	inline Int getDrawHeight(void) {return m_drawHeightY;}
	inline void setDrawWidth(Int width) {m_drawWidthX = width; if (m_drawWidthX>m_width) m_drawWidthX = m_width;}
	inline void setDrawHeight(Int height) {m_drawHeightY = height; if (m_drawHeightY>m_height) m_drawHeightY = m_height;}
	virtual Int getBorderSize(void) {return m_borderSize;}
	/// Get height with the offset that HeightMapRenderObjClass uses built in.
	inline UnsignedByte getDisplayHeight(Int x, Int y) { return m_data[x+m_drawOriginX+m_width*(y+m_drawOriginY)];}


	void getUVForBlend(Int edgeClass, Region2D *range);

	Bool setDrawOrg(Int xOrg, Int yOrg);


	Int getTextureClassNoBlend(Int xIndex, Int yIndex, Bool baseClass=false);
	Int getTextureClass(Int xIndex, Int yIndex, Bool baseClass=false);
	void countSandCells(Int *sandCells, Int *cells);
	TXTextureClass getTextureFromIndex( Int textureIndex );

public:  // tile and texture info.	
	void setTextureLOD(Int lod);	///< set maximum lod level sent to the hardware.
	TextureClass *getTerrainTexture(void);  //< generates if needed and returns the terrain texture
	TextureClass *getAlphaTerrainTexture(void); //< generates if needed and returns alpha terrain texture
	TextureClass *getEdgeTerrainTexture(void); //< generates if needed and returns blend edge texture
	/// UV mapping data for a cell to map into the terrain texture.  Returns true if the textures had to be stretched for cliffs.
	Bool getUVData(Int xIndex, Int yIndex, float U[4], float V[4], Bool fullTile);

	Bool getExtraAlphaUVData(Int xIndex, Int yIndex, float U[4], float V[4], UnsignedByte alpha[4], Bool *flip, Bool *cliff);
	Int getExtraBlendUVGeneration(void) const { return m_extraBlendUVGeneration; }	///< changes when getExtraAlphaUVData's answers can
	/// UV mapping data for a cell to map into the alpha terrain texture.
	void getAlphaUVData(Int xIndex, Int yIndex, float U[4], float V[4], UnsignedByte alpha[4], Bool *flip, Bool fullTile);
	void getTerrainColorAt(Real x, Real y, RGBColor *pColor);
	AsciiString getTerrainNameAt(Real x, Real y);
	Bool isCliffMappedTexture(Int xIndex, Int yIndex);


  virtual Real getSeismicZVelocity(Int xIndex, Int yIndex) const;
  virtual void setSeismicZVelocity(Int xIndex, Int yIndex, Real value); 
  virtual Real getBilinearSampleSeismicZVelocity( Int x, Int y);



public:  // Flat tile texture info.	
	TerrainTextureClass *getFlatTexture(Int xCell, Int yCell, Int cellWidth, Int pixelsPerCell);  //< generates and returns the terrain texture

	static void setupAlphaTiles(void);
	UnsignedByte *getPointerToTileData(Int xIndex, Int yIndex, Int width);
	Bool getRawTileData(Short tileNdx, Int width, UnsignedByte *buffer, Int bufLen);
	UnsignedByte *getRGBAlphaDataForWidth(Int width, TBlendTileInfo *pBlend);

public:  // modify height value
public: // Read tile utilities. jba [7/9/2003]
	// sourceExtent is the tile side the image was drawn in: 64 for everything the game ships, 128
	// for an image from Art/TerrainHD.  The tiles come out at TILE_PIXEL_EXTENT either way.
	static Bool readTiles(InputStream *pStrm, TileData **tiles, Int numRows,
		Int sourceExtent=SOURCE_TILE_PIXEL_EXTENT);
	static Int countTiles(InputStream *pStrm, Bool *halfTile=NULL,
		Int sourceExtent=SOURCE_TILE_PIXEL_EXTENT);

protected:

};

#endif
