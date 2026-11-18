/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Olcay Seygan
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

// FILE: InstallFiles.cpp /////////////////////////////////////////////////////////////////////////
// Desc:   The sha256 of every install file the game reads, hashed on a thread of its own and kept
//         in the user folder against each file's size and write time (InstallFiles.txt).
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/InstallFiles.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "sha256.h"
#include "zhio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctype.h>
#include <map>
#include <thread>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace
{

struct Job
{
	std::string path;		///< as login sends it
	std::string full;		///< what is opened, and the cache line's key
	FileInfo info;
};

struct Cached
{
	FileInfo info;
	std::string sha256;
};
typedef std::map<std::string, Cached> CacheMap;

struct Hasher
{
	std::thread thread;
	std::atomic<bool> started, done, stop;
	std::vector<InstallFile> list;		///< written by the thread before done, read only after it

	Hasher() : started( false ), done( false ), stop( false ) {}
	~Hasher()
	{
		stop = true;		// a quit mid-hash: the thread drops what it has, the next start reads again
		if (thread.joinable())
			thread.join();
	}
};

Hasher &hasher()
{
	static Hasher h;
	return h;
}

const char *CACHE_FILE = "InstallFiles.txt";
const int CACHE_FORMAT = 2;

std::string lower( std::string s )
{
	for (size_t i = 0; i < s.size(); ++i)
		s[i] = (char)tolower( (unsigned char)s[i] );
	return s;
}

std::string slashed( std::string s )
{
	std::replace( s.begin(), s.end(), '\\', '/' );
	if (!s.empty() && s[s.size() - 1] != '/')
		s += '/';
	return s;
}

bool isAbsolute( const std::string &s )
{
	return !s.empty() && (s[0] == '\\' || s[0] == '/' || (s.size() > 1 && s[1] == ':'));
}

/* The name the server knows: relative to the game folder, lower case, '/'.  The base game's archives are
	 zh_generals/ wherever they were found, Steam's folder beside the exe or a retail install elsewhere. */
std::string sentPath( const std::string &name, const std::string &here, const std::string &baseGame )
{
	std::string s = lower( name );
	std::replace( s.begin(), s.end(), '\\', '/' );
	const std::string base = baseGame.empty() ? std::string() : slashed( lower( baseGame ) );
	if (!base.empty() && s.compare( 0, base.size(), base ) == 0)
		return "zh_generals/" + s.substr( base.size() );
	if (s.compare( 0, here.size(), here ) == 0)
		return s.substr( here.size() );
	return s;
}

/* Names the game writes or the desktop drops beside its files; nothing reads them.  Nor a name outside
	 ASCII: every name the game asks for is ASCII, and the line login sends must be UTF-8, which a name in
	 the ANSI code page is not. */
bool notGameData( const std::string &path )
{
	for (size_t i = 0; i < path.size(); ++i)
		if ((unsigned char)path[i] >= 0x80)
			return true;
	const size_t slash = path.find_last_of( '/' );
	const std::string name = slash == std::string::npos ? path : path.substr( slash + 1 );
	return name == "dx11shaders.cache" || name == "thumbs.db" || name == "desktop.ini" || name == ".ds_store";
}

/* A format version and nothing of the build: a file's sha256 is the same under every build, so a new
	 release rereads only the files it changed, not the whole install.  Format 1 carried the fingerprint. */
std::string cacheHeader()
{
	char header[64];
	snprintf( header, sizeof(header), "InstallFiles %d", CACHE_FORMAT );
	return header;
}

bool sameFile( const FileInfo &a, const FileInfo &b )
{
	return a.sizeHigh == b.sizeHigh && a.sizeLow == b.sizeLow && a.timestampHigh == b.timestampHigh && a.timestampLow == b.timestampLow;
}

CacheMap readCache( const std::string &path )
{
	CacheMap cache;
	FILE *f = zh_fopen( path.c_str(), "r" );
	if (!f)
		return cache;
	char line[2048];
	std::string header;
	if (fgets( line, sizeof(line), f ))
	{
		header = line;
		header.erase( header.find_last_not_of( "\r\n" ) + 1 );
	}
	if (header != cacheHeader())
	{
		fclose( f );
		return cache;		// another format: every file is read again
	}
	while (fgets( line, sizeof(line), f ))
	{
		char hex[65];
		Cached entry;
		int keyOffset = 0;
		if (sscanf( line, "%64s %d %d %d %d %n", hex, &entry.info.sizeHigh, &entry.info.sizeLow,
				&entry.info.timestampHigh, &entry.info.timestampLow, &keyOffset ) != 5 || strlen( hex ) != 64)
			continue;		// a torn line reads again from the file
		std::string key( line + keyOffset );
		key.erase( key.find_last_not_of( "\r\n" ) + 1 );
		entry.sha256 = hex;
		cache[key] = entry;
	}
	fclose( f );
	return cache;
}

/* Written beside and moved over the old one, so a second copy starting at the same moment reads one
	 whole file or the other. */
void writeCache( const std::string &path, const CacheMap &cache )
{
	char scratch[32];
#if defined(_WIN32)
	snprintf( scratch, sizeof(scratch), ".%u", (unsigned)GetCurrentProcessId() );
#else
	snprintf( scratch, sizeof(scratch), ".%u", (unsigned)getpid() );
#endif
	const std::string scratchPath = path + scratch;
	FILE *f = zh_fopen( scratchPath.c_str(), "w" );
	if (!f)
		return;		// a read-only user folder costs the next start a full read, nothing else
	fprintf( f, "%s\n", cacheHeader().c_str() );
	for (CacheMap::const_iterator it = cache.begin(); it != cache.end(); ++it)
	{
		const Cached &e = it->second;
		fprintf( f, "%s %d %d %d %d %s\n", e.sha256.c_str(), e.info.sizeHigh, e.info.sizeLow,
			e.info.timestampHigh, e.info.timestampLow, it->first.c_str() );
	}
	fclose( f );
#if defined(_WIN32)
	const bool moved = MoveFileExA( scratchPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING ) != 0;
#else
	const bool moved = zh_rename( scratchPath.c_str(), path.c_str() ) == 0;
#endif
	if (!moved)
		zh_remove( scratchPath.c_str() );		// another copy holds it open; its own write will do
}

/* ponytail: a file is read again only when its size or write time moved, so an edit that keeps the size
	 and puts the old write time back passes on the cached hash.  Reading every byte at every start (about
	 13.7 GB on the owner's machine) is the upgrade, or the server asking for a random file's bytes. */
void hashAll( std::vector<Job> jobs, std::string cachePath )
{
#if defined(_WIN32)
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL );		// the shell and a match come first
#endif
	Hasher &h = hasher();
	const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
	const CacheMap cache = readCache( cachePath );
	CacheMap current;
	std::vector<InstallFile> list;
	std::vector<unsigned char> buffer( 1 << 20 );
	int filesRead = 0;
	unsigned long long bytesRead = 0;

	for (size_t i = 0; i < jobs.size(); ++i)
	{
		const Job &job = jobs[i];
		Cached entry;
		entry.info = job.info;
		CacheMap::const_iterator cached = cache.find( job.full );
		if (cached != cache.end() && sameFile( cached->second.info, job.info ))
			entry.sha256 = cached->second.sha256;
		else
		{
			FILE *f = zh_fopen( job.full.c_str(), "rb" );
			if (!f)
			{
				DEBUG_LOG(( "INSTALL FILES cannot read %s, left out\n", job.full.c_str() ));
				continue;
			}
			Sha256 sha;
			size_t n;
			while ((n = fread( &buffer[0], 1, buffer.size(), f )) > 0)
			{
				if (h.stop)
				{
					fclose( f );
					return;
				}
				sha.Update( &buffer[0], n );
				bytesRead += n;
			}
			fclose( f );
			unsigned char digest[32];
			sha.Final( digest );
			char hex[65];
			Sha256::Hex( digest, hex );
			entry.sha256 = hex;
			++filesRead;
		}
		current[job.full] = entry;
		InstallFile file;
		file.path = job.path;
		file.sha256 = entry.sha256;
		list.push_back( file );
	}

	if (filesRead > 0)
		writeCache( cachePath, current );
	std::sort( list.begin(), list.end(), []( const InstallFile &a, const InstallFile &b ) { return a.path < b.path; } );
	const double seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - began ).count();
	DEBUG_LOG(( "INSTALL FILES %d hashed, %d read this start (%.1f MB) in %.2f s\n", (int)list.size(), filesRead,
		bytesRead / (1024.0 * 1024.0), seconds ));
	h.list.swap( list );
	h.done = true;
}

}	// namespace

void InstallFiles_start()
{
	Hasher &h = hasher();
	if (h.started.exchange( true ))
		return;

	const std::string cwd = TheLocalFileSystem->getCurrentDirectory().str();
	const std::string here = slashed( lower( cwd ) );
	const std::string baseGame = TheArchiveFileSystem->m_baseGameDirectory.str();
#if defined(_WIN32)
	const char separator = '\\';
#else
	const char separator = '/';
#endif

	std::vector<AsciiString> names;
	const ArchiveFileMap &archives = TheArchiveFileSystem->getArchiveFileMap();
	for (ArchiveFileMap::const_iterator it = archives.begin(); it != archives.end(); ++it)
		names.push_back( it->first );
	FilenameList loose;
	static const char *const LOOSE_FOLDERS[] = { "Art\\", "Data\\", "Window\\" };
	for (size_t i = 0; i < sizeof(LOOSE_FOLDERS) / sizeof(LOOSE_FOLDERS[0]); ++i)
		TheLocalFileSystem->getFileListInDirectory( AsciiString( LOOSE_FOLDERS[i] ), AsciiString::TheEmptyString, AsciiString( "*" ), loose, TRUE );
	// a .big down there is mounted by nothing (only the exe's own folder and the base game's are), so it
	// is never read, and the server would refuse it as an archive the release does not ship
	for (FilenameList::const_iterator it = loose.begin(); it != loose.end(); ++it)
		if (!it->endsWithNoCase( ".big" ))
			names.push_back( *it );

	std::vector<Job> jobs;
	for (size_t i = 0; i < names.size(); ++i)
	{
		Job job;
		const std::string name = names[i].str();
		job.path = sentPath( name, here, baseGame );
		if (notGameData( job.path ))
			continue;
		job.full = isAbsolute( name ) ? name : cwd + separator + name;
		if (!TheLocalFileSystem->getFileInfo( names[i], &job.info ))
			continue;
		jobs.push_back( job );
	}

	std::string cachePath = TheGlobalData->getPath_UserData().str();
	cachePath += CACHE_FILE;
	DEBUG_LOG(( "INSTALL FILES hashing %d files (%d archives) on a thread\n", (int)jobs.size(), (int)archives.size() ));
	h.thread = std::thread( hashAll, jobs, cachePath );
}

bool InstallFiles_ready()
{
	const Hasher &h = hasher();
	return !h.started || h.done;
}

const std::vector<InstallFile> &InstallFiles_list()
{
	static const std::vector<InstallFile> none;
	const Hasher &h = hasher();
	return h.done ? h.list : none;
}

void InstallFiles_setList( const std::vector<InstallFile> &list )
{
	Hasher &h = hasher();
	h.list = list;
	h.started = true;
	h.done = true;
}
