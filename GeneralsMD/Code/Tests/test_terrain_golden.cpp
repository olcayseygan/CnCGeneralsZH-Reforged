/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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
/*
 * T1's terrain golden, against the moved code.
 *
 * Tests/terrain_golden.txt is what the ORIGINAL W3D terrain code printed for nine shipped maps, run
 * three ways (mingw-w64 under Wine, native arm64, x86_64 under Rosetta) by Tests/terrain_oracle.cpp.
 * This test reads the same maps out of the install's archives, decompresses them with the engine's own
 * CompressionManager, parses them with WorldHeightMapData::parseHeightsAndCells through the engine's own
 * DataChunkInput, samples them with TerrainHeightSampling over the same grid (Tests/terrain_grid.h),
 * and requires every line to match.  So it checks the moved parse, the moved maths, and - against the
 * oracle's own chunk walker - the engine's chunk reading, in one comparison.
 *
 * Needs ZH_DATA_DIR (a folder holding zerohour/ and generals/, or GeneralsMD/Run/ with its ZH_Generals/,
 * read only - rule 9: nothing here starts the engine, it only reads archives).  Without it, or without
 * the two map archives in it: "skip", 77, which ctest reports as Skipped.
 *
 * What it does not show: that the simulation asks these functions (that is W3DTerrainLogic on
 * Windows, and the portable terrain logic off it); anything about bridges (T1b) or the water grid.
 */

#include "PreRTS.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "Compression.h"
#include "GameLogic/TerrainHeightSampling.h"
#include "GameLogic/WorldHeightMapData.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "terrain_grid.h"

namespace {

// A decompressed map in memory, as a ChunkInputStream.
class MemoryChunkInputStream : public ChunkInputStream
{
public:
	MemoryChunkInputStream( const std::vector<unsigned char> &data ) : m_data( data ), m_at( 0 ) {}
	virtual Int read( void *pData, Int numBytes )
	{
		Int n = numBytes;
		if (m_at + n > m_data.size())
			n = (Int)(m_data.size() - m_at);
		if (n > 0)
			memcpy( pData, &m_data[ m_at ], n );
		m_at += n;
		return n;
	}
	virtual UnsignedInt tell( void ) { return (UnsignedInt)m_at; }
	virtual Bool absoluteSeek( UnsignedInt pos ) { if (pos > m_data.size()) return FALSE; m_at = pos; return TRUE; }
	virtual Bool eof( void ) { return m_at >= m_data.size(); }
private:
	const std::vector<unsigned char> &m_data;
	size_t m_at;
};

unsigned be32( const unsigned char *p ) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

bool sameName( const std::string &a, const std::string &b )
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
		if (x != y) return false;
	}
	return true;
}

std::vector<unsigned char> readBigEntry( const std::string &path, const std::string &entryName )
{
	std::vector<unsigned char> data;
	FILE *file = fopen( path.c_str(), "rb" );
	if (file == NULL)
		return data;
	unsigned char header[ 16 ];
	if (fread( header, 1, 16, file ) == 16 && memcmp( header, "BIGF", 4 ) == 0)
	{
		const unsigned count = be32( header + 8 );
		for (unsigned i = 0; i < count; ++i)
		{
			unsigned char pair[ 8 ];
			if (fread( pair, 1, 8, file ) != 8)
				break;
			std::string name;
			int c;
			while ((c = fgetc( file )) > 0)
				name += (char)c;
			if (sameName( name, entryName ))
			{
				data.resize( be32( pair + 4 ) );
				if (fseek( file, (long)be32( pair ), SEEK_SET ) != 0 || fread( &data[0], 1, data.size(), file ) != data.size())
					data.clear();
				break;
			}
		}
	}
	fclose( file );
	return data;
}

// The moved code, as a terrain_grid Sampler.  One map plays all three parts - the render object's
// m_map, the clip map and the logical map - as on Windows, where DO_SEISMIC_SIMULATIONS is off.
struct MovedSampler
{
	WorldHeightMapData *map;
	Real maxHeight;
	Real height( Real x, Real y, Coord3D *normal ) { return TerrainHeightSampling::getHeightMapHeight( map, map, x, y, normal ); }
	Bool cliff( Real x, Real y ) { return TerrainHeightSampling::isCliffCell( map, map, x, y ); }
	Real maxcell( Real x, Real y ) { return TerrainHeightSampling::getMaxCellHeight( map, map, x, y ); }
	Bool sight( const Coord3D &a, const Coord3D &b ) { return TerrainHeightSampling::isClearLineOfSight( map, map, maxHeight, a, b ); }
};

}  // namespace

int main( void )
{
	const char *dir = getenv( "ZH_DATA_DIR" );
	if (dir == NULL || *dir == 0)
	{
		printf( "skip: no game data (ZH_DATA_DIR)\n" );
		return 77;
	}
	/* Two layouts: an install's zerohour/ and generals/, or GeneralsMD/Run/, where the Zero Hour bigs sit
		 loose and the base game's in ZH_Generals/ (what ctest points at when ZH_GAME_DATA is not set). */
	std::string zhPrefix = "zerohour/", genPrefix = "generals/";
	{
		const std::string root( dir );
		FILE *probe = fopen( (root + "/zerohour/MapsZH.big").c_str(), "rb" );
		if (probe == NULL)
		{
			zhPrefix = "";
			genPrefix = "ZH_Generals/";
			probe = fopen( (root + "/MapsZH.big").c_str(), "rb" );
			FILE *base = fopen( (root + "/ZH_Generals/maps.big").c_str(), "rb" );
			if (base != NULL)
				fclose( base );
			else if (probe != NULL)
			{
				fclose( probe );
				probe = NULL;
			}
		}
		if (probe == NULL)
		{
			printf( "skip: no MapsZH.big and maps.big under %s\n", dir );
			return 77;
		}
		fclose( probe );
	}
	initMemoryManager();
	/* The moved BlendTileData parser reads TheGlobalData->m_use3WayTerrainBlends (it only clears the extra
		 blend indexes, which nothing here samples); in the game a GlobalData always exists, so the test
		 makes one, with its defaults - the flag's is 1, which is what the oracle's stand-in holds. */
	TheNameKeyGenerator = NEW NameKeyGenerator;
	TheNameKeyGenerator->init();
	TheWritableGlobalData = NEW GlobalData;

	// The golden: "file <archive>|<entry>" then the oracle's lines, until the next "file".
	FILE *golden = fopen( TERRAIN_GOLDEN_FILE, "r" );
	if (golden == NULL)
	{
		printf( "FAIL: cannot open %s\n", TERRAIN_GOLDEN_FILE );
		return 1;
	}
	std::vector<std::pair<std::string, std::string> > expected;
	char line[ 512 ];
	while (fgets( line, sizeof( line ), golden ) != NULL)
	{
		if (line[0] == '#')
			continue;
		if (strncmp( line, "file ", 5 ) == 0)
		{
			std::string key( line + 5 );
			while (!key.empty() && (key.back() == '\n' || key.back() == '\r'))
				key.pop_back();
			expected.push_back( std::make_pair( key, std::string() ) );
		}
		else if (!expected.empty())
			expected.back().second += line;
	}
	fclose( golden );

	int failures = 0;
	for (size_t m = 0; m < expected.size(); ++m)
	{
		const std::string &key = expected[m].first;
		const size_t bar = key.find( '|' );
		std::string archive = key.substr( 0, bar );
		if (archive.compare( 0, 9, "zerohour/" ) == 0)
			archive = zhPrefix + archive.substr( 9 );
		else if (archive.compare( 0, 9, "generals/" ) == 0)
			archive = genPrefix + archive.substr( 9 );
		archive = std::string( dir ) + "/" + archive;
		const std::vector<unsigned char> packed = readBigEntry( archive, key.substr( bar + 1 ) );
		if (packed.empty())
		{
			printf( "FAIL %s: not found in the install\n", key.c_str() );
			++failures;
			continue;
		}
		std::vector<unsigned char> bytes;
		if (CompressionManager::isDataCompressed( &packed[0], (Int)packed.size() ))
		{
			bytes.resize( CompressionManager::getUncompressedSize( &packed[0], (Int)packed.size() ) );
			if (CompressionManager::decompressData( (void *)&packed[0], (Int)packed.size(), &bytes[0], (Int)bytes.size() ) != (Int)bytes.size())
			{
				printf( "FAIL %s: did not decompress\n", key.c_str() );
				++failures;
				continue;
			}
		}
		else
			bytes = packed;

		WorldHeightMapData map;
		MemoryChunkInputStream stream( bytes );
		if (!map.parseHeightsAndCells( &stream ))
		{
			printf( "FAIL %s: parseHeightsAndCells refused it\n", key.c_str() );
			++failures;
			continue;
		}
		Real minHeight = 0, maxHeight = 0;
		TerrainHeightSampling::findMinMaxHeights( &map, minHeight, maxHeight );
		MovedSampler sampler = { &map, maxHeight };
		const std::string got = terrain_grid::run( sampler, map.getXExtent(), map.getYExtent(), map.getBorderSizeInline(),
			(int)map.getAllBoundaries().size(), maxHeight, MAP_XY_FACTOR, false );

		if (got == expected[m].second)
			printf( "same %s\n", key.c_str() );
		else
		{
			printf( "FAIL %s\n  want:\n%s  got:\n%s", key.c_str(), expected[m].second.c_str(), got.c_str() );
			++failures;
		}
	}

	if (expected.size() != 9)
	{
		printf( "FAIL: the golden holds %d maps, want 9\n", (int)expected.size() );
		++failures;
	}
	if (failures != 0)
	{
		printf( "terrain_golden: %d failure(s)\n", failures );
		return 1;
	}
	printf( "terrain_golden: the moved terrain code samples all %d maps as the original did\n", (int)expected.size() );
	return 0;
}
