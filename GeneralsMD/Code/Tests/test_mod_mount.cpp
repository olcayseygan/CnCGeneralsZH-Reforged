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
 * -mod mounts before the rest of the command line.  The mod's archives used to mount at the end of
 * parseCommandLine, after GameData.ini had been read, so a mod's GameData.ini never counted: Shockwave's
 * shell map, camera and the rest stayed EA's.  mountModFromCommandLine reads -mod alone and mounts it,
 * so GameEngine::init can read the mod's GameData.ini before parseCommandLine applies every other switch.
 * A mod folder holding one archive with a GameData.ini, made here; no game data is needed.
 */
#include "test_harness.h"

#include "PreRTS.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/CommandLine.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/NameKeyGenerator.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Platform/RenderTypes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>

// The names the game's executable supplies, as test_install_textures supplies them.
static char s_noAppPrefix[] = "";
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
char *gAppPrefix = s_noAppPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

namespace {

void put_be32( std::string &out, unsigned v )
{
	out += (char)( v >> 24 ); out += (char)( v >> 16 ); out += (char)( v >> 8 ); out += (char)v;
}

// One file in a BIGF archive: "BIGF", the archive's size (little-endian), the count and the header's
// size (big-endian), then each file's offset, size and name, then the data.
void write_big( const std::string &path, const std::string &name, const std::string &data )
{
	const unsigned header = 16 + 8 + (unsigned)name.size() + 1;
	const unsigned total = header + (unsigned)data.size();
	std::string out = "BIGF";
	out += (char)total; out += (char)( total >> 8 ); out += (char)( total >> 16 ); out += (char)( total >> 24 );
	put_be32( out, 1 );
	put_be32( out, header );
	put_be32( out, header );
	put_be32( out, (unsigned)data.size() );
	out += name;
	out += '\0';
	out += data;
	FILE *fp = fopen( path.c_str(), "wb" );
	if( fp )
	{
		fwrite( out.data(), 1, out.size(), fp );
		fclose( fp );
	}
}

} // namespace

TEST(mod_archives_mount_before_the_rest_of_the_command_line)
{
	const char *temp = getenv( "TMPDIR" );
	const std::string mod = std::string( temp && temp[ 0 ] ? temp : "/tmp" ) + "/test_mod_mount/";
	mkdir( mod.c_str(), 0777 );
	write_big( mod + "01_probe.big", "Data\\INI\\GameData.ini",
		"GameData\r\n  ShellMapName = Maps\\ModProbe\\ModProbe.map\r\nEnd\r\n" );

	initMemoryManager();
	TheNameKeyGenerator = NEW NameKeyGenerator;		// FileSystem's existence cache keys on it
	TheNameKeyGenerator->init();
	TheLocalFileSystem = NEW PosixLocalFileSystem;
	TheArchiveFileSystem = NEW Win32BIGFileSystem;		// not init(): no install to mount, only the mod
	TheWritableGlobalData = NEW GlobalData;
	CHECK( TheGlobalData->m_shellMapOn );

	char arg0[] = "generals", noShellMap[] = "-noshellmap", modSwitch[] = "-mod";
	char *modPath = strdup( mod.c_str() );
	char *argv[] = { arg0, noShellMap, modSwitch, modPath };
	mountModFromCommandLine( 4, argv );

	// -mod is read and its archives are mounted, so the mod's GameData.ini can be read straight away
	CHECK( TheGlobalData->m_modDir == AsciiString( mod.c_str() ) );
	CHECK( TheArchiveFileSystem->doesFileExist( "Data\\INI\\GameData.ini" ) );

	// and nothing else on the line is: -noshellmap waits for parseCommandLine, after the mod's file
	CHECK( TheGlobalData->m_shellMapOn );

	free( modPath );
	remove( ( mod + "01_probe.big" ).c_str() );
	rmdir( mod.c_str() );
}
