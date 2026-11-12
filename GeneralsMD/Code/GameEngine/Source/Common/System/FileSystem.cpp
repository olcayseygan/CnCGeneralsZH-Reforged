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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   WSYS Library
//
// Module:    IO
//
// File name: IO_FileSystem.cpp
//
// Created:   4/23/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"
#include "Common/file.h"
#include "Common/FileSystem.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/CDManager.h"
#include "Common/GameAudio.h"
#include "Common/LocalFileSystem.h"
#include "Common/PerfTimer.h"
#include "Common/RAMFile.h"
#include "Common/RandomMapGenerator.h"
#include "Common/GlobalData.h"

#include <algorithm>
#include <set>


DECLARE_PERF_TIMER(FileSystem)

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------

//===============================
// TheFileSystem 
//===============================
/**
  *	This is the FileSystem's singleton class. All file access 
	* should be through TheFileSystem, unless code needs to use an explicit
	* File or FileSystem derivative.
	*
	* Using TheFileSystem->open and File exclusively for file access, particularly
	* in library or modular code, allows applications to transparently implement 
	* file access as they see fit. This is particularly important for code that
	* needs to be shared between applications, such as games and tools.
	*/
//===============================

FileSystem	*TheFileSystem = NULL;

//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------


//============================================================================
// FileSystem::FileSystem
//============================================================================

FileSystem::FileSystem()
{

}

//============================================================================
// FileSystem::~FileSystem
//============================================================================

FileSystem::~FileSystem()
{

}

//============================================================================
// FileSystem::init
//============================================================================

void		FileSystem::init( void )
{
	{
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		m_fileExist.clear();		// see reset(): the cache outlives whatever it was an answer about
		m_archivedModel.clear();
		m_plainTrees.clear();
		m_wheelTrees.clear();
	}
	TheLocalFileSystem->init();
	TheArchiveFileSystem->init();
}

//============================================================================
// FileSystem::update
//============================================================================

void		FileSystem::update( void )
{
	USE_PERF_TIMER(FileSystem)
	TheLocalFileSystem->update();
	TheArchiveFileSystem->update();
}

//============================================================================
// FileSystem::reset
//============================================================================

void		FileSystem::reset( void )
{
	USE_PERF_TIMER(FileSystem)

	// doesFileExist remembers its answer for the life of the process and this is the only place
	// that ever gets told the world underneath it changed. A "no" cached before
	// Win32BIGFileSystem::init re-loaded the Patch*.big over the plain pass made
	// that file invisible for the rest of the run - and the cache is keyed by name, so nothing else
	// would ever ask again.
	{
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		m_fileExist.clear();
		m_archivedModel.clear();
		m_plainTrees.clear();
		m_wheelTrees.clear();
	}

	TheLocalFileSystem->reset();
	TheArchiveFileSystem->reset();
}

//============================================================================
// FileSystem::open
//============================================================================

File*		FileSystem::openFile( const Char *filename, Int access )
{
	USE_PERF_TIMER(FileSystem)
	File *file = NULL;

	//
	// A generated map is not on any disk and never will be: its seed, player count and size are in
	// its name, so the bytes are built from the name on the first read and served out of memory
	// after that.  It goes in front of both file systems because the map cache, the loader, the
	// CRC and the preview all reach a map through here and none of them has to know the difference.
	//
	const Char *generated = NULL;
	Int generatedSize = 0;
	if ( generatedMapBytes( AsciiString( filename ), &generated, &generatedSize ) )
	{
		RAMFile *ramFile = newInstance( RAMFile );
		if ( ramFile->openFromMemory( generated, generatedSize, AsciiString( filename ) ) )
			return ramFile;

		ramFile->close();
		ramFile->deleteInstance();
		return NULL;
	}

	// A loose model beats every archive, except a Reforged batch's copy of it that moves nothing the
	// logic reads (takesArchivedModel): the
	// loose master stays the fallback and the INI CRC's source, the batch adds only its LODs.
	if ( (access & (File::WRITE | File::CREATE)) == 0 && takesArchivedModel( filename ) )
	{
		file = TheArchiveFileSystem->openFile( filename );
		if ( file != NULL )
			return file;
	}

	if ( TheLocalFileSystem != NULL )
	{
		file = TheLocalFileSystem->openFile( filename, access );
	}

	/* A file made here exists from now on, and doesFileExist may already have said it did not: a
		 map transfer asks before it writes, and the map.ini it then writes was skipped by the loader
		 on the receiving machine, which played with the stock data while the host played with the
		 map's (upstream #991). */
	if ( file != NULL && (access & File::CREATE) )
	{
		const unsigned key = TheNameKeyGenerator->nameToLowercaseKey( filename );
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		m_fileExist[ key ] = true;
	}

	if ( (TheArchiveFileSystem != NULL) && (file == NULL) )
	{
		file = TheArchiveFileSystem->openFile( filename );
	}

	return file;
}

//============================================================================
// FileSystem::takesArchivedModel
//============================================================================

namespace {
	struct W3DChunk
	{
		UnsignedInt type;
		UnsignedInt length;		// of the data, without the 8 byte header
		const char *whole;		// header first
	};

	struct SubObject
	{
		Int level;					///< LOD array index, coarsest 0; -1 for an aggregate
		std::string name;		///< upper case
		UnsignedInt bone;
	};

	/// What the logic can read of one HLOD: muzzle, fire FX and launch names the htree lacks are found
	/// among its sub-objects (W3DModelDraw findSingleSubObj), in Get_Sub_Object_By_Name's order.
	struct HLodView
	{
		std::string name, tree;				///< the header's, upper case, up to the first NUL
		std::string extras;						///< the aggregate and proxy arrays whole, in file order
		std::vector<SubObject> subs;		///< the LOD arrays coarsest first, then the aggregates
	};
}

static Bool readWhole( File *file, std::string &bytes )
{
	if ( file == NULL )
		return FALSE;
	const Int size = file->size();
	char *data = file->readEntireAndClose();
	bytes.assign( data, size );
	delete [] data;
	return TRUE;
}

/** The chunks of one level of a W3D file; FALSE when they do not add up to its size. */
static Bool walkChunks( const char *bytes, UnsignedInt size, std::vector<W3DChunk> &chunks )
{
	UnsignedInt at = 0;
	while ( at < size )
	{
		if ( size - at < 8 )
			return FALSE;
		W3DChunk chunk;
		memcpy( &chunk.type, bytes + at, 4 );
		memcpy( &chunk.length, bytes + at + 4, 4 );
		chunk.length &= 0x7FFFFFFF;		// the top bit says "has sub-chunks"
		if ( chunk.length > size - at - 8 )
			return FALSE;
		chunk.whole = bytes + at;
		chunks.push_back( chunk );
		at += 8 + chunk.length;
	}
	return TRUE;
}

static std::string chunkName( const char *name, size_t length )
{
	std::string s( name, strnlen( name, length ) );
	for ( size_t i = 0; i < s.size(); ++i )
		s[i] = (char)toupper( (unsigned char)s[i] );
	return s;
}

static Bool readHLod( const W3DChunk &hlod, HLodView &view )
{
	std::vector<W3DChunk> parts;
	if ( !walkChunks( hlod.whole + 8, hlod.length, parts ) )
		return FALSE;
	std::vector<SubObject> aggregates;
	Int headers = 0, levels = 0;
	for ( size_t i = 0; i < parts.size(); ++i )
	{
		const W3DChunk &part = parts[i];
		const char *data = part.whole + 8;
		if ( part.type == 0x701 )				// HLOD_HEADER: version, level count, name, hierarchy name
		{
			if ( part.length < 40 )
				return FALSE;
			view.name = chunkName( data + 8, 16 );
			view.tree = chunkName( data + 24, 16 );
			++headers;
		}
		else if ( part.type == 0x702 || part.type == 0x705 || part.type == 0x706 )
		{
			// a LOD array, the aggregates, the proxies: an array header and a SUB_OBJECT (bone, name) each
			if ( part.type != 0x702 )
				view.extras.append( part.whole, 8 + part.length );
			std::vector<W3DChunk> records;
			if ( !walkChunks( data, part.length, records ) )
				return FALSE;
			for ( size_t r = 0; r < records.size() && part.type != 0x706; ++r )
			{
				if ( records[r].type != 0x704 || records[r].length < 36 )
					continue;
				SubObject sub;
				sub.level = ( part.type == 0x702 ) ? levels : -1;
				memcpy( &sub.bone, records[r].whole + 8, 4 );
				sub.name = chunkName( records[r].whole + 12, 32 );
				( part.type == 0x702 ? view.subs : aggregates ).push_back( sub );
			}
			if ( part.type == 0x702 )
				++levels;
		}
	}
	view.subs.insert( view.subs.end(), aggregates.begin(), aggregates.end() );
	return headers == 1 && levels > 0;
}

/// RenderObjClass::Get_Sub_Object_By_Name's answer: a whole name first, then the part after the first
/// '.', each in order; the bone of what it finds, -1 for nothing.
static Int findSubObjectBone( const std::vector<SubObject> &subs, const std::string &name )
{
	for ( size_t i = 0; i < subs.size(); ++i )
		if ( subs[i].name == name )
			return (Int)subs[i].bone;
	for ( size_t i = 0; i < subs.size(); ++i )
	{
		const size_t dot = subs[i].name.find( '.' );
		if ( ( dot == std::string::npos ? subs[i].name : subs[i].name.substr( dot + 1 ) ) == name )
			return (Int)subs[i].bone;
	}
	return -1;
}

/// Every name findSubObjectBone finds in these sub-objects.
static std::set<std::string> subObjectAnswers( const std::vector<SubObject> &subs )
{
	std::set<std::string> names;
	for ( size_t i = 0; i < subs.size(); ++i )
	{
		names.insert( subs[i].name );
		const size_t dot = subs[i].name.find( '.' );
		if ( dot != std::string::npos && dot + 1 < subs[i].name.size() )
			names.insert( subs[i].name.substr( dot + 1 ) );
	}
	return names;
}

static Bool isTwoDigits( const std::string &s )
{
	const size_t n = s.size();
	return n > 2 && isdigit( (unsigned char)s[n - 2] ) && isdigit( (unsigned char)s[n - 1] );
}

/// A name a copy may answer that the master does not: a coarse level's copy of a master mesh,
/// <name>_L<digit> (what lodtr writes, the form Is_Part_Or_LOD_Copy reads), or a rebuilt detail mesh
/// whose part after the container's '.' starts with the reserved HD_.
static Bool isOwnName( const std::string &name, const std::set<std::string> &masterAnswers )
{
	const size_t dot = name.find( '.' );
	if ( ( dot == std::string::npos ? name : name.substr( dot + 1 ) ).compare( 0, 3, "HD_" ) == 0 )
		return TRUE;
	const size_t n = name.size();
	return n > 3 && name[n - 3] == '_' && name[n - 2] == 'L' && isdigit( (unsigned char)name[n - 1] )
		&& masterAnswers.count( name.substr( 0, n - 3 ) ) != 0;
}

/// The name, or the name less a two-digit tail, is one of these.
static Bool nameOrStemIn( const std::string &name, const std::set<std::string> &names )
{
	return names.count( name ) || ( isTwoDigits( name ) && names.count( name.substr( 0, name.size() - 2 ) ) );
}

/** The copy hands the logic the master's bone for every name it can ask a sub-object for.  The logic
	* falls back to a sub-object for a name no pivot from the second on answers (findSingleSubObj), so:
	* every name the master answers that way is found in the copy on the master's bone; every level of
	* the copy holds a master sub-object's name only on a bone the master gives it; and a name only the
	* copy answers is isOwnName's, no pivot's and, whole or less a two-digit tail, no StandardPublicBone.
	* Mesh geometry, counts, materials and MaxScreenSize are the copy's own: its full level may be a
	* rebuild.  Tools/artpack.py fork_master_problems holds a batch to the same rule before it ships. */
static Bool sameHLodBindings( const HLodView &master, const HLodView &copy, const std::set<std::string> &pivots,
	const std::set<std::string> &publicBones )
{
	if ( master.name != copy.name || master.tree != copy.tree || master.extras != copy.extras )
		return FALSE;

	const std::set<std::string> masterAnswers = subObjectAnswers( master.subs ), copyAnswers = subObjectAnswers( copy.subs );
	for ( std::set<std::string>::const_iterator it = masterAnswers.begin(); it != masterAnswers.end(); ++it )
		if ( !pivots.count( *it ) && findSubObjectBone( copy.subs, *it ) != findSubObjectBone( master.subs, *it ) )
			return FALSE;

	std::multimap<std::string, UnsignedInt> bones;
	for ( size_t i = 0; i < master.subs.size(); ++i )
		bones.insert( std::make_pair( master.subs[i].name, master.subs[i].bone ) );
	for ( size_t i = 0; i < copy.subs.size(); ++i )
	{
		const SubObject &sub = copy.subs[i];
		if ( sub.level < 0 || pivots.count( sub.name ) || !bones.count( sub.name ) )
			continue;
		Bool held = FALSE;
		for ( std::multimap<std::string, UnsignedInt>::const_iterator b = bones.lower_bound( sub.name ); b != bones.upper_bound( sub.name ); ++b )
			held = held || b->second == sub.bone;
		if ( !held )
			return FALSE;
	}

	for ( std::set<std::string>::const_iterator it = copyAnswers.begin(); it != copyAnswers.end(); ++it )
		if ( !masterAnswers.count( *it )
			&& ( !isOwnName( *it, masterAnswers ) || pivots.count( *it ) || nameOrStemIn( *it, publicBones ) ) )
			return FALSE;
	return TRUE;
}

static Bool isWheelPivot( const std::string &name )
{
	return name.size() == 9 && name.compare( 0, 6, "WHEEL_" ) == 0 && ( name[6] == 'L' || name[6] == 'R' ) && isTwoDigits( name );
}

/** A copy's hierarchy against the master's: every sub-chunk the master's bytes, but the header's pivot
	* count, the pivots and their fixups, which may run on past the master's with WHEEL_[LR]<nn> pivots
	* parented to a master pivot - road wheels the client turns.  An animation's channels address the
	* master's pivots by index and the loader sizes its motion to the tree (HRawAnimClass, HCompressed-
	* AnimClass), so an added pivot stands still and moves nothing before it.  The master's pivot names
	* go to pivots, the added ones to wheels, upper case. */
static Bool sameHierarchyButWheels( const W3DChunk &master, const W3DChunk &copy, std::vector<std::string> &pivots,
	std::vector<std::string> &wheels )
{
	const UnsignedInt PIVOT_SIZE = 60, FIXUP_SIZE = 48;
	std::vector<W3DChunk> m, c;
	if ( !walkChunks( master.whole + 8, master.length, m ) || !walkChunks( copy.whole + 8, copy.length, c ) || m.size() != c.size() )
		return FALSE;
	UnsignedInt added = 0;
	for ( size_t i = 0; i < m.size(); ++i )
	{
		if ( m[i].type != c[i].type )
			return FALSE;
		if ( m[i].type != 0x102 )			// HIERARCHY_PIVOTS: name, parent index, transform
			continue;
		const char *md = m[i].whole + 8, *cd = c[i].whole + 8;
		const UnsignedInt ml = m[i].length, cl = c[i].length;
		if ( ml % PIVOT_SIZE || cl < ml || ( cl - ml ) % PIVOT_SIZE || memcmp( md, cd, ml ) != 0 )
			return FALSE;
		const size_t first = pivots.size();
		for ( UnsignedInt at = 0; at < ml; at += PIVOT_SIZE )
			pivots.push_back( chunkName( md + at, 16 ) );
		for ( UnsignedInt at = ml; at < cl; at += PIVOT_SIZE )
		{
			const std::string name = chunkName( cd + at, 16 );
			Int parent;
			memcpy( &parent, cd + at + 16, 4 );
			if ( !isWheelPivot( name ) || parent < 0 || parent >= (Int)( ml / PIVOT_SIZE )
				|| std::find( pivots.begin() + first, pivots.end(), name ) != pivots.end()
				|| std::find( wheels.begin(), wheels.end(), name ) != wheels.end() )
				return FALSE;
			wheels.push_back( name );
		}
		added += ( cl - ml ) / PIVOT_SIZE;
	}
	for ( size_t i = 0; i < m.size(); ++i )
	{
		const char *md = m[i].whole + 8, *cd = c[i].whole + 8;
		const UnsignedInt ml = m[i].length, cl = c[i].length;
		if ( m[i].type == 0x101 )				// HIERARCHY_HEADER: version, name, pivot count at 20, centre
		{
			UnsignedInt mCount, cCount;
			if ( ml != cl || ml < 24 || memcmp( md, cd, 20 ) != 0 || memcmp( md + 24, cd + 24, ml - 24 ) != 0 )
				return FALSE;
			memcpy( &mCount, md + 20, 4 );
			memcpy( &cCount, cd + 20, 4 );
			if ( cCount != mCount + added )
				return FALSE;
		}
		else if ( m[i].type == 0x103 )		// PIVOT_FIXUPS: a matrix per pivot
		{
			if ( cl < ml || cl - ml != added * FIXUP_SIZE || memcmp( md, cd, ml ) != 0 )
				return FALSE;
		}
		else if ( m[i].type != 0x102 && ( ml != cl || memcmp( md, cd, ml ) != 0 ) )
			return FALSE;
	}
	return TRUE;
}

/** An added wheel pivot answers nothing the master answers: doSingleBoneName asks for NAME and NAME01 on
	* as bones before any sub-object, so no added name, nor its stem, is a name the master answers, a
	* numbered run of one, or a StandardPublicBone. */
static Bool wheelsAnswerNothing( const std::vector<std::string> &wheels, const std::set<std::string> &known,
	const std::set<std::string> &publicBones )
{
	for ( size_t w = 0; w < wheels.size(); ++w )
	{
		const std::string &name = wheels[w];
		const std::string stem = name.substr( 0, name.size() - 2 );
		if ( known.count( name ) || known.count( stem ) || publicBones.count( name ) || publicBones.count( stem ) )
			return FALSE;
		for ( std::set<std::string>::const_iterator k = known.begin(); k != known.end(); ++k )
			if ( k->size() == name.size() && isTwoDigits( *k ) && k->compare( 0, stem.size(), stem ) == 0 )
				return FALSE;
	}
	return TRUE;
}

/// The render objects a file defines, upper case: every mesh as <container>.<mesh> (or the mesh alone
/// with no container), every box and null object by its name.
static void readPrototypes( const std::vector<W3DChunk> &chunks, std::set<std::string> &names )
{
	for ( size_t i = 0; i < chunks.size(); ++i )
	{
		const char *data = chunks[i].whole + 8;
		if ( chunks[i].type == 0x000 )			// MESH, its MESH_HEADER3 naming it
		{
			std::vector<W3DChunk> parts;
			if ( !walkChunks( data, chunks[i].length, parts ) )
				continue;
			for ( size_t p = 0; p < parts.size(); ++p )
				if ( parts[p].type == 0x01F && parts[p].length >= 40 )
				{
					const std::string mesh = chunkName( parts[p].whole + 16, 16 ), container = chunkName( parts[p].whole + 32, 16 );
					names.insert( container.empty() ? mesh : container + "." + mesh );
				}
		}
		else if ( chunks[i].type == 0x740 && chunks[i].length >= 40 )		// BOX
			names.insert( chunkName( data + 8, 32 ) );
		else if ( chunks[i].type == 0x750 && chunks[i].length >= 48 )		// NULL_OBJECT
			names.insert( chunkName( data + 16, 32 ) );
	}
}

/** The animation and HModel chunks byte for byte - ANIMATION 0x200, COMPRESSED_ANIMATION 0x280,
	* MORPH_ANIMATION 0x2C0, HMODEL 0x300 - the hierarchy too but for added wheels, every LOD array record
	* naming a render object its own file defines (HLodClass leaves out one it cannot make, so the
	* prediction would lie), and every HLOD's bindings.  An _SKN on a retail _SKL has no hierarchy, so
	* its HLOD is what decides. */
static Bool sameLogicView( const std::string &master, const std::string &copy, Bool &wheeled )
{
	wheeled = FALSE;
	const std::string *files[2] = { &master, &copy };
	std::string fixed[2];
	std::vector<W3DChunk> hierarchies[2];
	std::vector<HLodView> hlods[2];
	for ( Int f = 0; f < 2; ++f )
	{
		std::vector<W3DChunk> chunks;
		if ( !walkChunks( files[f]->data(), (UnsignedInt)files[f]->size(), chunks ) )
			return FALSE;
		std::set<std::string> defined;
		readPrototypes( chunks, defined );
		for ( size_t i = 0; i < chunks.size(); ++i )
		{
			const UnsignedInt type = chunks[i].type;
			if ( type == 0x200 || type == 0x280 || type == 0x2C0 || type == 0x300 )
				fixed[f].append( chunks[i].whole, 8 + chunks[i].length );
			else if ( type == 0x100 )
				hierarchies[f].push_back( chunks[i] );
			else if ( type == 0x700 )
			{
				hlods[f].resize( hlods[f].size() + 1 );
				if ( !readHLod( chunks[i], hlods[f].back() ) )
					return FALSE;
				const std::vector<SubObject> &subs = hlods[f].back().subs;
				for ( size_t s = 0; s < subs.size(); ++s )
					if ( subs[s].level >= 0 && !defined.count( subs[s].name ) )
						return FALSE;
			}
		}
	}
	if ( fixed[0] != fixed[1] || hierarchies[0].size() != hierarchies[1].size() || hlods[0].size() != hlods[1].size() )
		return FALSE;

	std::vector<std::string> pivotNames, wheels;
	for ( size_t i = 0; i < hierarchies[0].size(); ++i )
		if ( !sameHierarchyButWheels( hierarchies[0][i], hierarchies[1][i], pivotNames, wheels ) )
			return FALSE;
	const std::set<std::string> shield( pivotNames.empty() ? pivotNames.end() : pivotNames.begin() + 1, pivotNames.end() );

	std::set<std::string> publicBones;
	if ( TheGlobalData != NULL )
		for ( size_t i = 0; i < TheGlobalData->m_standardPublicBones.size(); ++i )
			publicBones.insert( chunkName( TheGlobalData->m_standardPublicBones[i].str(), TheGlobalData->m_standardPublicBones[i].getLength() ) );

	std::set<std::string> known( pivotNames.begin(), pivotNames.end() );
	for ( size_t i = 0; i < hlods[0].size(); ++i )
	{
		if ( !sameHLodBindings( hlods[0][i], hlods[1][i], shield, publicBones ) )
			return FALSE;
		const std::set<std::string> answers = subObjectAnswers( hlods[0][i].subs );
		known.insert( answers.begin(), answers.end() );
	}
	wheeled = !wheels.empty();
	return wheelsAnswerNothing( wheels, known, publicBones );
}

/// The names of the hierarchies a file defines, upper case.
static void readHierarchyNames( const std::string &file, std::vector<std::string> &names )
{
	std::vector<W3DChunk> chunks;
	if ( !walkChunks( file.data(), (UnsignedInt)file.size(), chunks ) )
		return;
	for ( size_t i = 0; i < chunks.size(); ++i )
	{
		std::vector<W3DChunk> parts;
		if ( chunks[i].type != 0x100 || !walkChunks( chunks[i].whole + 8, chunks[i].length, parts ) )
			continue;
		for ( size_t p = 0; p < parts.size(); ++p )
			if ( parts[p].type == 0x101 && parts[p].length >= 20 )		// HIERARCHY_HEADER: version, then the name
				names.push_back( chunkName( parts[p].whole + 12, 16 ) );
	}
}

Bool FileSystem::wheelTreeRefused( unsigned key ) const
{
	std::map< unsigned, std::vector<std::string> >::const_iterator it = m_wheelTrees.find( key );
	if ( it != m_wheelTrees.end() )
		for ( size_t i = 0; i < it->second.size(); ++i )
			if ( m_plainTrees.count( it->second[i] ) )
				return TRUE;
	return FALSE;
}

Bool FileSystem::takesArchivedModel( const Char *filename )
{
	if ( TheLocalFileSystem == NULL || TheArchiveFileSystem == NULL )
		return FALSE;

	// the W3D loader asks for "Art/W3D/x.w3d", the archive lists "art\w3d\x.w3d"
	std::string path( filename );
	for ( size_t i = 0; i < path.size(); ++i )
		path[i] = ( path[i] == '/' ) ? '\\' : (char)tolower( (unsigned char)path[i] );
	if ( path.size() < 12 || path.compare( 0, 8, "art\\w3d\\" ) != 0 || path.compare( path.size() - 4, 4, ".w3d" ) != 0 )
		return FALSE;

	const unsigned key = TheNameKeyGenerator->nameToLowercaseKey( path.c_str() );
	{
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		std::map<unsigned,bool>::const_iterator it = m_archivedModel.find( key );
		if ( it != m_archivedModel.end() )
			return it->second && !wheelTreeRefused( key );		// a sibling served plain since: the master from now on
	}

	// The tree's holder differs from the retail one only where a Reforged batch went over the model;
	// Classic and Detailed Models off mount none, so they never get past this.
	const AsciiString model( path.c_str() );
	Bool archived = FALSE, wheeled = FALSE;
	std::vector<std::string> trees;
	if ( TheArchiveFileSystem->getArchiveFilenameForFile( model ) != TheArchiveFileSystem->getRetailArchiveFilenameForFile( model )
		&& TheLocalFileSystem->doesFileExist( filename ) )
	{
		std::string loose, packed;
		if ( readWhole( TheLocalFileSystem->openFile( filename, File::READ | File::BINARY ), loose ) )
		{
			readHierarchyNames( loose, trees );
			archived = readWhole( TheArchiveFileSystem->openFile( filename ), packed ) && sameLogicView( loose, packed, wheeled );
		}
		DEBUG_LOG(( "FileSystem::takesArchivedModel - %s from %s\n", filename,
			archived ? "the Reforged batch" : "the loose master, the batch moves what the logic reads" ));
	}

	std::lock_guard<std::mutex> lock( m_fileExistLock );
	if ( archived && wheeled )
	{
		m_wheelTrees[ key ] = trees;
		if ( wheelTreeRefused( key ) )
		{
			DEBUG_LOG(( "FileSystem::takesArchivedModel - %s from the loose master, a file sharing its tree was served without its wheels\n", filename ));
			m_wheelTrees.erase( key );
			archived = FALSE;
		}
	}
	if ( !archived || !wheeled )
		m_plainTrees.insert( trees.begin(), trees.end() );
	m_archivedModel[ key ] = archived;
	return archived;
}

//============================================================================
// FileSystem::doesFileExist
//============================================================================

Bool FileSystem::doesFileExist(const Char *filename) const
{
	USE_PERF_TIMER(FileSystem)

	// a generated map exists wherever its name can be read, which is everywhere - see openFile
	if (isGeneratedMapPath( AsciiString( filename ) ))
		return TRUE;

  unsigned key=TheNameKeyGenerator->nameToLowercaseKey(filename);
	{
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		std::map<unsigned,bool>::iterator i=m_fileExist.find(key);
		if (i!=m_fileExist.end())
			return i->second;
	}

	// asked unlocked, so a slow disk does not hold the other thread up; two threads asking the same
	// name both find the same answer and store it twice
	const Bool exists = TheLocalFileSystem->doesFileExist(filename)
		|| (TheArchiveFileSystem != NULL && TheArchiveFileSystem->doesFileExist(filename));

	std::lock_guard<std::mutex> lock( m_fileExistLock );
	m_fileExist[key]=exists != FALSE;
	return exists;
}

//============================================================================
// FileSystem::getFileListInDirectory
//============================================================================
void FileSystem::getFileListInDirectory(const AsciiString& directory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const
{
	USE_PERF_TIMER(FileSystem)
	TheLocalFileSystem->getFileListInDirectory(AsciiString(""), directory, searchName, filenameList, searchSubdirectories);
	TheArchiveFileSystem->getFileListInDirectory(AsciiString(""), directory, searchName, filenameList, searchSubdirectories);
}

//============================================================================
// FileSystem::getFileInfo
//============================================================================
Bool FileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	USE_PERF_TIMER(FileSystem)
	if (fileInfo == NULL) {
		return FALSE;
	}
	memset(fileInfo, 0, sizeof(fileInfo));

	// a generated map has a size and no timestamp: the bytes follow from the name, so there is no
	// moment at which they were made and nothing that could make them go stale
	const Char *generated = NULL;
	Int generatedSize = 0;
	if (generatedMapBytes(filename, &generated, &generatedSize)) {
		fileInfo->sizeLow = generatedSize;
		return TRUE;
	}

	if (TheLocalFileSystem->getFileInfo(filename, fileInfo)) {
		return TRUE;
	}

	if (TheArchiveFileSystem->getFileInfo(filename, fileInfo)) {
		return TRUE;
	}

	return FALSE;
}

//============================================================================
// FileSystem::createDirectory
//============================================================================
Bool FileSystem::createDirectory(AsciiString directory) 
{
	USE_PERF_TIMER(FileSystem)
	if (TheLocalFileSystem != NULL) {
		return TheLocalFileSystem->createDirectory(directory);
	}
	return FALSE;
}

//============================================================================
// FileSystem::installedLanguage
//============================================================================
/* The registry's Language outlives the files it names: a Steam install switched from German to
	 English keeps Language=german with only EnglishZH.big beside the exe, and GlobalLanguage::init
	 then threw on Data\german\Language.ini before the first frame (player report #39). A German-only
	 install has no english to fall back to either (issue #63), so every retail language is tried.
	 Empty when no Language.ini is found at all. */
static const char *const RetailLanguages[] = { "english", "german", "french", "spanish", "italian",
	"korean", "chinese", "brazilian", "polish", "russian" };

AsciiString FileSystem::installedLanguage(const AsciiString &language) const
{
	AsciiString probe;
	if (!language.isEmpty()) {
		probe.format("Data\\%s\\Language.ini", language.str());
		if (doesFileExist(probe.str()))
			return language;
	}
	for (Int i = 0; i < (Int)ARRAY_SIZE(RetailLanguages); ++i) {
		probe.format("Data\\%s\\Language.ini", RetailLanguages[i]);
		if (doesFileExist(probe.str()))
			return AsciiString(RetailLanguages[i]);
	}
	return AsciiString::TheEmptyString;
}

//============================================================================
// FileSystem::zeroHourLanguage
//============================================================================
/* With no Language in the registry (a Linux or Mac install never ran EA's installer, which writes it)
	 the default english finds the base game's English.big in ZH_Generals, so a German Zero Hour showed
	 Generals' text with Zero Hour's strings missing.  Zero Hour's own language archive says which it is. */
AsciiString FileSystem::zeroHourLanguage() const
{
	AsciiString probe;
	for (Int i = 0; i < (Int)ARRAY_SIZE(RetailLanguages); ++i) {
		probe.format("%sZH.big", RetailLanguages[i]);
		if (TheLocalFileSystem->doesFileExist(probe.str()))
			return AsciiString(RetailLanguages[i]);
	}
	return AsciiString::TheEmptyString;
}

//============================================================================
// FileSystem::areMusicFilesOnCD
//============================================================================
Bool FileSystem::areMusicFilesOnCD()
{
	// No-CD layouts (Steam) ship the security big next to the exe instead of on
	// a disc; finding it there is the same proof of media this check wants, so
	// look locally first and never prompt for a CD the install cannot have.
	File *localSec = TheLocalFileSystem->openFile("genseczh.big");
	if (localSec) {
		localSec->close();
		return TRUE;
	}

	if (!TheCDManager) {
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - No CD Manager; returning false\n"));
		return FALSE;
	}

	AsciiString cdRoot;
	Int dc = TheCDManager->driveCount();
	for (Int i = 0; i < dc; ++i) {
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - checking drive %d\n", i));
		CDDriveInterface *cdi = TheCDManager->getDrive(i);
		if (!cdi) {
			continue;
		}

		cdRoot = cdi->getPath();
		if (!cdRoot.endsWith("\\"))
			cdRoot.concat("\\");
		cdRoot.concat("genseczh.big");
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - checking for %s\n", cdRoot.str()));
		File *musicBig = TheLocalFileSystem->openFile(cdRoot.str());
		if (musicBig)
		{
			DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - found it!\n"));
			musicBig->close();
			return TRUE;
		}
	}
	return FALSE;
}
//============================================================================
// FileSystem::loadMusicFilesFromCD
//============================================================================
void FileSystem::loadMusicFilesFromCD()
{
	if (!TheCDManager) {
		return;
	}

	AsciiString cdRoot;
	Int dc = TheCDManager->driveCount();
	for (Int i = 0; i < dc; ++i) {
		CDDriveInterface *cdi = TheCDManager->getDrive(i);
		if (!cdi) {
			continue;
		}

		cdRoot = cdi->getPath();
		if (TheArchiveFileSystem->loadBigFilesFromDirectory(cdRoot, MUSIC_BIG)) {
			break;
		}
	}
}

//============================================================================
// FileSystem::unloadMusicFilesFromCD
//============================================================================
void FileSystem::unloadMusicFilesFromCD()
{
	if (!(TheAudio && TheAudio->isMusicPlayingFromCD())) {
		return;
	}

	TheArchiveFileSystem->closeArchiveFile( MUSIC_BIG );
}
