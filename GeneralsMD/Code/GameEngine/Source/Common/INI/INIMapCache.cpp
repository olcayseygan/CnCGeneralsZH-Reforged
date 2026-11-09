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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: INIMapCache.cpp ///////////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell, February 2002
// Desc:   Parsing MapCache INI entries
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/BaseType.h"
#include "Common/INI.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameText.h"
#include "GameNetwork/NetworkDefs.h"
#include "Common/NameKeyGenerator.h"
#include "Common/WellKnownKeys.h"
#include "Common/QuotedPrintable.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

class MapMetaDataReader
{
public:
	Region3D m_extent;
	Int m_numPlayers;
	Bool m_isMultiplayer;
	AsciiString m_asciiDisplayName;
	AsciiString m_asciiNameLookupTag;

	Bool m_isOfficial;
	WinTimeStamp m_timestamp;
	UnsignedInt m_filesize;
	UnsignedInt m_CRC;

	Coord3D m_waypoints[MAX_SLOTS];
	static_assert( MAX_SLOTS == 32, "one START_SPOT per slot in m_mapFieldParseTable" );
	Coord3D m_initialCameraPosition;
	Coord3DList m_supplyPositions;
	Coord3DList m_techPositions;
	static const FieldParse m_mapFieldParseTable[];		///< the parse table for INI definition
	const FieldParse *getFieldParse( void ) const { return m_mapFieldParseTable; }
};


void parseSupplyPositionCoord3D( INI* ini, void * instance, void * /*store*/, const void* /*userData*/ )
{
	MapMetaDataReader *mmdr = (MapMetaDataReader *)instance;
	Coord3D coord3d;
	INI::parseCoord3D(ini, NULL, &coord3d,NULL );
	mmdr->m_supplyPositions.push_front(coord3d);

}

void parseTechPositionsCoord3D( INI* ini, void * instance, void * /*store*/, const void* /*userData*/ )
{
	MapMetaDataReader *mmdr = (MapMetaDataReader *)instance;
	Coord3D coord3d;
	INI::parseCoord3D(ini, NULL, &coord3d,NULL );
	mmdr->m_techPositions.push_front(coord3d);

}

const FieldParse MapMetaDataReader::m_mapFieldParseTable[] = 
{

	{ "isOfficial",							INI::parseBool,			NULL,	offsetof( MapMetaDataReader, m_isOfficial ) },
	{ "isMultiplayer",					INI::parseBool,			NULL,	offsetof( MapMetaDataReader, m_isMultiplayer ) },
	{ "extentMin",							INI::parseCoord3D,	NULL, offsetof( MapMetaDataReader, m_extent.lo ) },
	{ "extentMax",							INI::parseCoord3D,	NULL, offsetof( MapMetaDataReader, m_extent.hi ) },
	{ "numPlayers",							INI::parseInt,			NULL,	offsetof( MapMetaDataReader, m_numPlayers ) },
	{ "fileSize",								INI::parseUnsignedInt,	NULL,	offsetof( MapMetaDataReader, m_filesize ) },
	{ "fileCRC",								INI::parseUnsignedInt,	NULL,	offsetof( MapMetaDataReader, m_CRC ) },
	{ "timestampLo",						INI::parseInt,			NULL,	offsetof( MapMetaDataReader, m_timestamp.m_lowTimeStamp ) },
	{ "timestampHi",						INI::parseInt,			NULL,	offsetof( MapMetaDataReader, m_timestamp.m_highTimeStamp ) },
	{ "displayName",						INI::parseAsciiString,	NULL,	offsetof( MapMetaDataReader, m_asciiDisplayName ) },
	{ "nameLookupTag",					INI::parseAsciiString,	NULL,	offsetof( MapMetaDataReader, m_asciiNameLookupTag ) },

	{ "supplyPosition",					parseSupplyPositionCoord3D,	NULL, NULL },
	{ "techPosition",						parseTechPositionsCoord3D,	NULL, NULL },

	// one line per start spot MapCache::writeCacheINI can write, which is one per slot
#define START_SPOT( n ) { "Player_" #n "_Start", INI::parseCoord3D, NULL, offsetof( MapMetaDataReader, m_waypoints ) + sizeof(Coord3D) * ( n - 1 ) }
	START_SPOT( 1 ),  START_SPOT( 2 ),  START_SPOT( 3 ),  START_SPOT( 4 ),  START_SPOT( 5 ),  START_SPOT( 6 ),
	START_SPOT( 7 ),  START_SPOT( 8 ),  START_SPOT( 9 ),  START_SPOT( 10 ), START_SPOT( 11 ), START_SPOT( 12 ),
	START_SPOT( 13 ), START_SPOT( 14 ), START_SPOT( 15 ), START_SPOT( 16 ), START_SPOT( 17 ), START_SPOT( 18 ),
	START_SPOT( 19 ), START_SPOT( 20 ), START_SPOT( 21 ), START_SPOT( 22 ), START_SPOT( 23 ), START_SPOT( 24 ),
	START_SPOT( 25 ), START_SPOT( 26 ), START_SPOT( 27 ), START_SPOT( 28 ), START_SPOT( 29 ), START_SPOT( 30 ),
	START_SPOT( 31 ), START_SPOT( 32 ),
#undef START_SPOT

	{ "InitialCameraPosition",	INI::parseCoord3D,	NULL,	offsetof( MapMetaDataReader, m_initialCameraPosition ) },

	{ NULL,					NULL,						NULL,						0 }  // keep this last

};

void INI::parseMapCacheDefinition( INI* ini )
{
	const char *c;
	AsciiString name;
	MapMetaDataReader mdr;
	MapMetaData md;

	// read the name
	c = ini->getNextToken(" \n\r\t");
	name.set( c );
	name = QuotedPrintableToAsciiString(name);
	md.m_waypoints.clear();

	ini->initFromINI( &mdr, mdr.getFieldParse() );

	md.m_extent = mdr.m_extent;
	md.m_isOfficial = mdr.m_isOfficial != 0;
	md.m_isMultiplayer = mdr.m_isMultiplayer != 0;
	md.m_numPlayers = mdr.m_numPlayers;
	md.m_filesize = mdr.m_filesize;
	md.m_CRC = mdr.m_CRC;
	md.m_timestamp = mdr.m_timestamp;

	md.m_waypoints[TheNameKeyGenerator->keyToName(TheKey_InitialCameraPosition)] = mdr.m_initialCameraPosition;

//	md.m_displayName = QuotedPrintableToUnicodeString(mdr.m_asciiDisplayName);
// this string is never to be used, but we'll leave it in to allow people with an old mapcache.ini to parse it
	md.m_nameLookupTag = QuotedPrintableToAsciiString(mdr.m_asciiNameLookupTag);

	if (md.m_nameLookupTag.isEmpty())
	{
		// maps without localized name tags
		AsciiString tempdisplayname;
		tempdisplayname = name.reverseFind('\\') + 1;
		md.m_displayName.translate(tempdisplayname);
		if (md.m_numPlayers >= 2)
		{
			UnicodeString extension;
			extension.format(u" (%d)", md.m_numPlayers);
			md.m_displayName.concat(extension);
		}
	}
	else
	{
		// official maps with name tags
		md.m_displayName = TheGameText->fetch(md.m_nameLookupTag);
		if (md.m_numPlayers >= 2)
		{
			UnicodeString extension;
			extension.format(u" (%d)", md.m_numPlayers);
			md.m_displayName.concat(extension);
		}
	}

	AsciiString startingCamName;
	for (Int i=0; i<md.m_numPlayers && i<MAX_SLOTS; ++i)
	{
		startingCamName.format("Player_%d_Start", i+1); // start pos waypoints are 1-based
		md.m_waypoints[startingCamName] = mdr.m_waypoints[i];
	}

	Coord3DList::iterator it = mdr.m_supplyPositions.begin();
	while( it != mdr.m_supplyPositions.end())
	{
		md.m_supplyPositions.push_front(*it);
		it++;
	}

	it = mdr.m_techPositions.begin();
	while( it != mdr.m_techPositions.end())
	{
		md.m_techPositions.push_front(*it);
		it++;
	}

	if(TheMapCache && !md.m_displayName.isEmpty())
	{
		AsciiString lowerName = name;
		lowerName.toLower();
		md.m_fileName = lowerName;
//		DEBUG_LOG(("INI::parseMapCacheDefinition - adding %s to map cache\n", lowerName.str()));
		(*TheMapCache)[lowerName] = md;
	}
}

