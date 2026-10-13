/*
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
 * DXT3 decoding.  DDSFileClass's Get_Pixel returned plain white for DXT3 and Get_4x4_Block wrote
 * nothing, so a DXT3 texture that had to be converted came out empty: Shockwave's Generals Challenge
 * map, DXT3 where EA's is DXT5, was not drawn at all.  One 4x4 block with known alphas and colours,
 * read through both.
 */
#include "test_harness.h"

#include "PreRTS.h"
#include "ddsfile.h"
#include "ffactory.h"
#include "Platform/RenderTypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

// The names the game's executable supplies, as test_install_textures supplies them.
static char s_noAppPrefix[] = "";
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
char *gAppPrefix = s_noAppPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

namespace {

void put32( unsigned char *p, unsigned v )
{
	p[ 0 ] = (unsigned char)v; p[ 1 ] = (unsigned char)( v >> 8 ); p[ 2 ] = (unsigned char)( v >> 16 ); p[ 3 ] = (unsigned char)( v >> 24 );
}

/* A 4x4, one-level DXT3 file.  Row 0's alphas are 0, 15, 8 and 4 (of 15) and its colours the four
	 entries of the palette, red to blue; rows 1 to 3 are opaque red. */
std::string write_dxt3_probe()
{
	unsigned char file[ 4 + 124 + 16 ] = { 0 };
	memcpy( file, "DDS ", 4 );
	unsigned char *desc = file + 4;
	put32( desc + 0, 124 );								// size
	put32( desc + 4, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000 );	// caps, height, width, pixel format, mips, linear size
	put32( desc + 8, 4 );									// height
	put32( desc + 12, 4 );								// width
	put32( desc + 16, 16 );								// linear size: one block
	put32( desc + 24, 1 );								// mip levels
	put32( desc + 72, 32 );								// pixel format size
	put32( desc + 76, 0x4 );								// FourCC
	memcpy( desc + 80, "DXT3", 4 );
	put32( desc + 104, 0x1000 );							// caps: texture

	unsigned char *block = file + 4 + 124;
	block[ 0 ] = 0xF0;									// row 0: alpha 0 and 15
	block[ 1 ] = 0x48;									//        alpha 8 and 4
	for( int i = 2; i < 8; ++i ) block[ i ] = 0xFF;		// rows 1 to 3 opaque
	block[ 8 ] = 0x00; block[ 9 ] = 0xF8;				// colour 0: RGB565 red
	block[ 10 ] = 0x1F; block[ 11 ] = 0x00;				// colour 1: RGB565 blue
	block[ 12 ] = 0xE4;									// row 0: indices 0, 1, 2, 3
	block[ 13 ] = block[ 14 ] = block[ 15 ] = 0x00;		// rows 1 to 3: index 0

	const char *temp = getenv( "TMPDIR" );
	std::string path = std::string( temp && temp[ 0 ] ? temp : "/tmp" ) + "/test_ww3d2_dxt3.dds";
	FILE *fp = fopen( path.c_str(), "wb" );
	if( fp )
	{
		fwrite( file, 1, sizeof( file ), fp );
		fclose( fp );
	}
	return path;
}

unsigned alpha_of( unsigned argb ) { return argb >> 24; }
unsigned red_of( unsigned argb ) { return ( argb >> 16 ) & 0xFF; }
unsigned blue_of( unsigned argb ) { return argb & 0xFF; }

void check_dxt3_probe_pixels( const unsigned row0[ 4 ], unsigned below )
{
	CHECK_EQ( alpha_of( row0[ 0 ] ), 0x00u );
	CHECK_EQ( alpha_of( row0[ 1 ] ), 0xFFu );
	CHECK_EQ( alpha_of( row0[ 2 ] ), 0x88u );		// 8 of 15
	CHECK_EQ( alpha_of( row0[ 3 ] ), 0x44u );		// 4 of 15
	CHECK( red_of( row0[ 0 ] ) >= 0xF8 && blue_of( row0[ 0 ] ) == 0 );		// colour 0
	CHECK( blue_of( row0[ 1 ] ) >= 0xF8 && red_of( row0[ 1 ] ) == 0 );		// colour 1
	CHECK( red_of( row0[ 2 ] ) > blue_of( row0[ 2 ] ) );		// two thirds red
	CHECK( blue_of( row0[ 3 ] ) > red_of( row0[ 3 ] ) );		// two thirds blue
	CHECK_EQ( alpha_of( below ), 0xFFu );
	CHECK( red_of( below ) >= 0xF8 && blue_of( below ) == 0 );
}

} // namespace

TEST(dds_dxt3_decodes_its_alpha_and_colours)
{
	const std::string path = write_dxt3_probe();
	SimpleFileFactoryClass factory;
	FileFactoryClass *saved = _TheFileFactory;
	_TheFileFactory = &factory;

	{
		DDSFileClass dds( path.c_str(), 0 );
		CHECK( dds.Is_Available() );
		CHECK( dds.Get_Format() == WW3D_FORMAT_DXT3 );
		CHECK( dds.Load() );
		if( dds.Is_Available() && dds.Get_Format() == WW3D_FORMAT_DXT3 )
		{
			// one pixel at a time
			unsigned row0[ 4 ];
			for( unsigned x = 0; x < 4; ++x )
				row0[ x ] = dds.Get_Pixel( 0, x, 0 );
			check_dxt3_probe_pixels( row0, dds.Get_Pixel( 0, 2, 3 ) );

			// the conversion the game makes of a texture it cannot use compressed (Get_4x4_Block)
			unsigned surface[ 16 ];
			memset( surface, 0, sizeof( surface ) );
			dds.Copy_Level_To_Surface( 0, WW3D_FORMAT_A8R8G8B8, 4, 4, (unsigned char *)surface, 4 * 4 );
			check_dxt3_probe_pixels( surface, surface[ 3 * 4 + 2 ] );
		}
	}

	_TheFileFactory = saved;
	remove( path.c_str() );
}
