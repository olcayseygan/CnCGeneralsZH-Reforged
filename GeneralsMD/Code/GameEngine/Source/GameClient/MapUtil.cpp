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

// FILE: MapUtil.cpp //////////////////////////////////////////////////////////////////////////////
// Author: Matt Campbell, December 2001
// Description: Map utility/convenience routines
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "zhio.h"

#include "Lib/WideCharFns.h"

#include "Common/crc.h"
#include "Common/FileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/file.h"
#include "Common/GlobalData.h"
#include "Common/GameState.h"
#include "Common/GameEngine.h"
#include "Common/NameKeyGenerator.h"
#include "Common/DataChunk.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/MessageStream.h"
#include "Common/WellKnownKeys.h"
#include "Common/INI.h"
#include "Common/QuotedPrintable.h"
#include "Common/RandomMapGenerator.h"
#include "Common/SkirmishBattleHonors.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/MapObject.h"
#include "GameClient/GameText.h" 
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Image.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/FPUControl.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkDefs.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static char *mapExtension = ".map";

static Int m_width = 0;						///< Height map width.
static Int m_height = 0;					///< Height map height (y size of array).
static Int m_borderSize = 0;			///< Non-playable border area.
static std::vector<ICoord2D> m_boundaries;	///< All the boundaries we use for the map
static Int m_dataSize = 0;				///< size of m_data.
static UnsignedByte *m_data = 0;	///< array of z(height) values in the height map.
static Dict worldDict = 0;

static WaypointMap *m_waypoints = 0;
static Coord3DList	m_supplyPositions;
static Coord3DList	m_techPositions;

static Int m_mapDX = 0;
static Int m_mapDY = 0;

static void addFileToCRC( CRC &theCRC, File *fp )
{
	UnsignedByte buf[4096];
	Int num;
	while ( (num=fp->read(buf, 4096)) > 0 )
	{
		theCRC.computeCRC(buf, num);
	}

	fp->close();
}

/** GameLogic::startNewGame loads these from the map's folder with INI_LOAD_CREATE_OVERRIDES, and
	* they change armour, upgrades, weapons and objects for the match.  The lobby compares only this
	* CRC, so two players with the same map file and different rules files used to be let in to a
	* match that split on the first shot. */
static const char *const MAP_RULES_FILES[] = { "map.ini", "solo.ini" };
static const Int MAP_RULES_FILE_COUNT = sizeof( MAP_RULES_FILES ) / sizeof( MAP_RULES_FILES[0] );

static AsciiString mapRulesPath( const AsciiString &fname, Int rulesIndex )
{
	// the map's folder, cut the way startNewGame cuts it
	char folder[_MAX_PATH];
	strncpy( folder, fname.str(), _MAX_PATH - 1 );
	folder[_MAX_PATH - 1] = 0;
	char *separator = folder + strlen( folder );
	while ((separator > folder) && (*separator != '\\') && (*separator != '/'))
		--separator;
	*separator = 0;

	AsciiString path;
	path.format( "%s\\%s", folder, MAP_RULES_FILES[rulesIndex] );
	return path;
}

static Bool hasMapRules( const AsciiString &fname )
{
	for( Int i = 0; i < MAP_RULES_FILE_COUNT; ++i )
	{
		if (TheFileSystem->doesFileExist( mapRulesPath( fname, i ).str() ))
			return TRUE;
	}
	return FALSE;
}

static UnsignedInt calcCRC( AsciiString dirName, AsciiString fname )
{
	CRC theCRC;
	theCRC.clear();

	File *fp = TheFileSystem->openFile(fname.str(), File::READ);
	if( !fp )
	{
		DEBUG_CRASH(("Couldn't open '%s'\n", fname.str()));
		return 0;
	}
	addFileToCRC( theCRC, fp );

	for( Int i = 0; i < MAP_RULES_FILE_COUNT; ++i )
	{
		File *rules = TheFileSystem->openFile( mapRulesPath( fname, i ).str(), File::READ );
		if (rules)
			addFileToCRC( theCRC, rules );
	}

	return theCRC.get();
}

static Bool ParseObjectDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	Bool readDict = info->version >= K_OBJECTS_VERSION_2;

	Coord3D loc;
	loc.x = file.readReal();
	loc.y = file.readReal();
	loc.z = file.readReal();
	if (info->version <= K_OBJECTS_VERSION_2) 
	{
		loc.z = 0;
	}

	Real angle = file.readReal();
	Int flags = file.readInt(); 
	AsciiString name = file.readAsciiString();
	Dict d;
	if (readDict)
	{
		d = file.readDict();
	}
	MapObject *pThisOne;
	
	// create the map object
	pThisOne = newInstance( MapObject )( loc, name, angle, flags, &d, 
														TheThingFactory->findTemplate( name, FALSE ) );

//DEBUG_LOG(("obj %s owner %s\n",name.str(),d.getAsciiString(TheKey_originalOwner).str()));

	if (pThisOne->getProperties()->getType(TheKey_waypointID) == Dict::DICT_INT)
	{
		pThisOne->setIsWaypoint();

		// grab useful info
		(*m_waypoints)[pThisOne->getWaypointName()] = loc;
	}
	else if (pThisOne->getThingTemplate() && pThisOne->getThingTemplate()->isKindOf(KINDOF_TECH_BUILDING))
	{
		m_techPositions.push_back(loc);
	}
	else if (pThisOne->getThingTemplate() && pThisOne->getThingTemplate()->isKindOf(KINDOF_SUPPLY_SOURCE_ON_PREVIEW))
	{
		m_supplyPositions.push_back(loc);
	}

	pThisOne->deleteInstance();
	return TRUE;
}

static Bool ParseObjectsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	file.m_currentObject = NULL;
	file.registerParser( AsciiString("Object"), info->label, ParseObjectDataChunk );
	return (file.parse(userData));
}

static Bool ParseWorldDictDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	worldDict = file.readDict();
	return true;
}

static Bool ParseSizeOnly(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	m_width = file.readInt();
	m_height = file.readInt();
	// the game's own reader refuses these (WorldHeightMapData::ParseHeightMapData), so the list does too
	if (m_width < 1 || m_width > K_MAX_HEIGHT_MAP_SIDE || m_height < 1 || m_height > K_MAX_HEIGHT_MAP_SIDE) {
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	if (info->version >= K_HEIGHT_MAP_VERSION_3) {
		m_borderSize = file.readInt();
	} else {
		m_borderSize = 0;
	}

	if (info->version >= K_HEIGHT_MAP_VERSION_4) {
		Int numBorders = file.readCount(2 * sizeof(Int));
		m_boundaries.resize(numBorders);
		for (int i = 0; i < numBorders; ++i) {
			m_boundaries[i].x = file.readInt();
			m_boundaries[i].y = file.readInt();
		}
	}
	return true;

	m_dataSize = file.readInt();
	m_data = NEW UnsignedByte[m_dataSize];	// pool[]ify
	if (m_dataSize <= 0 || (m_dataSize != (m_width*m_height))) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}
	file.readArrayOfBytes((char *)m_data, m_dataSize);
	// Resize me. 
	if (info->version == K_HEIGHT_MAP_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i, j;
		for (i=0; i<newHeight; i++) {
			for (j=0; j<newWidth; j++) {
				m_data[i*newWidth+j] = m_data[2*i*m_width+2*j];
			}
		}
		m_width = newWidth;
		m_height = newHeight;
	}
	return true;
}

static Bool ParseSizeOnlyInChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	return ParseSizeOnly(file, info, userData);
}

static Bool loadMap( AsciiString filename )
{
	char	tempBuf[_MAX_PATH];
	char	filenameBuf[_MAX_PATH];
	AsciiString asciiFile;
	int length = 0;

	// bounded, see above
	strncpy(tempBuf, filename.str(), _MAX_PATH - 1);
	tempBuf[_MAX_PATH - 1] = 0;

	length = strlen( tempBuf );
	if( length >= 4 )
	{
		memset( filenameBuf, '\0', _MAX_PATH);
		strncpy( filenameBuf, tempBuf, length - 4);
	}

	CachedFileInputStream fileStrm;

	asciiFile = filename;
	if( !fileStrm.open(asciiFile) )
	{
		return FALSE;
	}

	ChunkInputStream *pStrm = &fileStrm;

	DataChunkInput file( pStrm );

	m_waypoints = NEW WaypointMap;

	file.registerParser( AsciiString("HeightMapData"), AsciiString::TheEmptyString, ParseSizeOnlyInChunk );
	file.registerParser( AsciiString("WorldInfo"), AsciiString::TheEmptyString, ParseWorldDictDataChunk );
	file.registerParser( AsciiString("ObjectsList"), AsciiString::TheEmptyString, ParseObjectsDataChunk );
	if (!file.parse(NULL)) {
		throw(ERROR_CORRUPT_FILE_FORMAT);
	}

	m_mapDX = m_width  - 2*m_borderSize;
	m_mapDY = m_height - 2*m_borderSize;

	return TRUE;
}

static void resetMap( void )
{
	if (m_data)
	{
		delete[] m_data;
		m_data = 0;
	}

	if (m_waypoints)
	{
		delete m_waypoints;
		m_waypoints = 0;
	}
	m_techPositions.clear();
	m_supplyPositions.clear();
}

static void getExtent( Region3D *extent )
{
	extent->lo.x = 0.0f;

	extent->lo.y = 0.0f;

	// Note - m_mapDX & Y are the number of height map grids wide, so we have to
	// multiply by the grid width.
	extent->hi.x = m_mapDX*MAP_XY_FACTOR;
	extent->hi.y = m_mapDY*MAP_XY_FACTOR;

	extent->lo.z = 0;
	extent->hi.z = 0;
}

//-------------------------------------------------------------------------------

void WaypointMap::update( void )
{
	if (!m_waypoints)
	{
		m_numStartSpots = 1;
		return;
	}

	this->clear();

	AsciiString startingCamName = TheNameKeyGenerator->keyToName(TheKey_InitialCameraPosition);
	WaypointMap::const_iterator it;

	it = m_waypoints->find(startingCamName);
	if (it != m_waypoints->end())
	{
		(*this)[startingCamName] = it->second;
	}

	m_numStartSpots = 0;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		startingCamName.format("Player_%d_Start", i+1); // start pos waypoints are 1-based
		it = m_waypoints->find(startingCamName);
		if (it != m_waypoints->end())
		{
			(*this)[startingCamName] = it->second;
			++m_numStartSpots;
		}
		else
		{
			break;
		}
	}

	m_numStartSpots = max(1, m_numStartSpots);
}

const char * MapCache::m_mapCacheName = "MapCache.ini";

AsciiString MapCache::getMapDir() const 
{ 
	return AsciiString("Maps"); 
}

AsciiString MapCache::getUserMapDir() const
{
	AsciiString tmp = TheGlobalData->getPath_UserData();
	tmp.concat(getMapDir());
	return tmp;
}

AsciiString MapCache::getMapExtension() const
{
	return AsciiString("map");
}

void MapCache::writeCacheINI( Bool userDir )
{
	AsciiString mapDir;
	if (!userDir || TheGlobalData->m_buildMapCache)
	{
		mapDir = getMapDir();
	}
	else
	{
		mapDir = getUserMapDir();
	}

	AsciiString filepath = mapDir;
	filepath.concat('\\');

	TheFileSystem->createDirectory(mapDir);

	filepath.concat(m_mapCacheName);
	FILE *fp = zh_fopen(filepath.str(), "w");
	DEBUG_ASSERTCRASH(fp != NULL, ("Failed to create %s", filepath.str()));
	if (fp == NULL) {
		return;
	}
	fprintf(fp, "; FILE: %s /////////////////////////////////////////////////////////////\n", filepath.str());
	fprintf(fp, "; This INI file is auto-generated - do not modify\n");
	fprintf(fp, "; /////////////////////////////////////////////////////////////////////////////\n");
	mapDir.toLower();

	MapCache::iterator it = begin();
	MapMetaData md;
	while (it != end())
	{
		// a generated map is rebuilt from its name whenever it is asked for, so an entry for one is
		// a line about a file that is not there - and this file is the one place a random map could
		// still leave something behind on disk
		if (it->first.startsWithNoCase(mapDir.str()) && !isGeneratedMapPath(it->first))
		{
			md = it->second;
			fprintf(fp, "\nMapCache %s\n", AsciiStringToQuotedPrintable(it->first.str()).str());
			fprintf(fp, "  fileSize = %u\n", md.m_filesize);
			fprintf(fp, "  fileCRC = %u\n", md.m_CRC);
			fprintf(fp, "  timestampLo = %d\n", md.m_timestamp.m_lowTimeStamp);
			fprintf(fp, "  timestampHi = %d\n", md.m_timestamp.m_highTimeStamp);
			fprintf(fp, "  isOfficial = %s\n", (md.m_isOfficial)?"yes":"no");

			fprintf(fp, "  isMultiplayer = %s\n", (md.m_isMultiplayer)?"yes":"no");
			fprintf(fp, "  numPlayers = %d\n", md.m_numPlayers);

			fprintf(fp, "  extentMin = X:%2.2f Y:%2.2f Z:%2.2f\n", md.m_extent.lo.x, md.m_extent.lo.y, md.m_extent.lo.z);
			fprintf(fp, "  extentMax = X:%2.2f Y:%2.2f Z:%2.2f\n", md.m_extent.hi.x, md.m_extent.hi.y, md.m_extent.hi.z);

// BAD AND NOW UNUSED:  the mapcache.ini should not contain localized data... using the lookup tag instead
//			fprintf(fp, "  displayName = %s\n", UnicodeStringToQuotedPrintable(md.m_displayName).str());
			fprintf(fp, "  nameLookupTag = %s\n", md.m_nameLookupTag.str());

			Coord3D pos;
			WaypointMap::iterator itw = md.m_waypoints.begin();
			while (itw != md.m_waypoints.end())
			{
				pos = itw->second;
				fprintf(fp, "  %s = X:%2.2f Y:%2.2f Z:%2.2f\n", itw->first.str(), pos.x, pos.y, pos.z);
				++itw;
			}
			Coord3DList::iterator itc3d = md.m_techPositions.begin();
			while (itc3d != md.m_techPositions.end())
			{
				pos = *itc3d;
				fprintf(fp, "  techPosition = X:%2.2f Y:%2.2f Z:%2.2f\n", pos.x, pos.y, pos.z);
				itc3d++;
			}
			
			itc3d = md.m_supplyPositions.begin();
			while (itc3d != md.m_supplyPositions.end())
			{
				pos = *itc3d;
				fprintf(fp, "  supplyPosition = X:%2.2f Y:%2.2f Z:%2.2f\n", pos.x, pos.y, pos.z);
				itc3d++;
			}
			fprintf(fp, "END\n\n");
		}
		else
		{
			//DEBUG_LOG(("%s does not start %s\n", mapDir.str(), it->first.str()));
		}
		++it;
	}

	fclose(fp);
}

void MapCache::updateCache( void )
{
	setFPMode();

	TheFileSystem->createDirectory(getUserMapDir());

	// this runs again every time a map list is populated: every menu that shows maps calls it on
	// open, and again on each toggle between the shipped and the user map directory.  Re-parsing
	// the shipped MapCache.ini out of the archive on each of those is what made those menus hitch.
	const Bool userMapsChanged = loadUserMaps();
	if (userMapsChanged)
	{
		writeCacheINI( TRUE );
	}
	// we shall overwrite info from matching user maps to prevent munkees from getting rowdy :)
	// the shipped cache cannot change while the game runs, so read it once - and again whenever a
	// user map was (re)parsed, since that pass is what could have overwritten an official entry.
	if (m_doLoadStandardMapCacheINI || userMapsChanged)
	{
		loadStandardMaps();
		m_doLoadStandardMapCacheINI = FALSE;
	}
#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheLocalFileSystem->doesFileExist(getMapDir().str()))
	{
		// only create the map cache file if "Maps" exist
		Bool wasBuildMapCache = TheGlobalData->m_buildMapCache;
		TheWritableGlobalData->m_buildMapCache = true;
		loadUserMaps();
		TheWritableGlobalData->m_buildMapCache = wasBuildMapCache;
		writeCacheINI( FALSE );
	}
#endif
}

Bool MapCache::clearUnseenMaps( AsciiString dirName )
{
	dirName.toLower();
	Bool erasedSomething = FALSE;

	std::map<AsciiString, Bool>::iterator it = m_seen.begin();

	while (it != m_seen.end())
	{
		AsciiString mapName = it->first;
		if (it->second == FALSE && mapName.startsWithNoCase(dirName.str()))
		{
			// not seen in the dir - clear it out.
			erase(mapName);
			erasedSomething = TRUE;
		}
		++it;
	}
	return erasedSomething;
}

void MapCache::loadStandardMaps(void)
{
	INI ini;
	AsciiString fname;
	fname.format("%s\\%s", getMapDir().str(), m_mapCacheName);
#if defined(_DEBUG) || defined(_INTERNAL)
	File *fp = TheFileSystem->openFile(fname.str(), File::READ);
	if (fp != NULL)
	{
		fp->close();
		fp = NULL;
#endif
		ini.load( fname, INI_LOAD_OVERWRITE, NULL );
#if defined(_DEBUG) || defined(_INTERNAL)
	}
#endif
}

Bool MapCache::loadUserMaps()
{
	// Read in map list from disk
	AsciiString mapDir;
	Bool userCacheBroken = FALSE;
	if (TheGlobalData->m_buildMapCache)
	{
		mapDir = getMapDir();
	}
	else
	{
		mapDir = getUserMapDir();

		// once this has been read, what is in memory is a superset of it: writeCacheINI writes the
		// file back out of memory, and every later pass re-parses whatever changed on disk anyway.
		if (m_doLoadUserMapCacheINI)
		{
			m_doLoadUserMapCacheINI = FALSE;

			INI ini;
			AsciiString fname;
			fname.format("%s\\%s", mapDir.str(), m_mapCacheName);
			File *fp = TheFileSystem->openFile(fname.str(), File::READ);
			if (fp)
			{
				fp->close();
				// the user folder's cache is ours to rebuild, and a hand edit or a torn write used to
				// stop the game at startup.  Forget what it held; every map below parses fresh, and
				// returning TRUE has updateCache write a clean file over the broken one.
				try
				{
					ini.load( fname, INI_LOAD_OVERWRITE, NULL );
				}
				catch (...)
				{
					DEBUG_LOG(("MapCache: %s does not parse, rebuilding it\n", fname.str()));
					clear();
					userCacheBroken = TRUE;
				}
			}

		}
	}

	// mark all as unseen
	m_seen.clear();
	MapCache::iterator it = begin();
	while (it != end())
	{
		m_seen[it->first] = FALSE;
		++it;
	}

	FilenameList filenameList;
	FilenameListIter iter;
	AsciiString toplevelPattern;
	toplevelPattern.format("%s\\", mapDir.str());
	Bool parsedAMap = FALSE;
	AsciiString filenamepattern;
	filenamepattern.format("*.%s", getMapExtension().str());

	TheFileSystem->getFileListInDirectory(toplevelPattern, filenamepattern, filenameList, TRUE);

	iter = filenameList.begin();

	while (iter != filenameList.end()) {
		FileInfo fileInfo;
		AsciiString tempfilename;
		tempfilename = (*iter);
		tempfilename.toLower();

		const char *s = tempfilename.reverseFind('\\');
		if (!s)
		{
			DEBUG_CRASH(("Couldn't find \\ in map name!"));
		}
		else
		{
			AsciiString endingStr;
			AsciiString fname = s+1;
			for (Int i=0; i<strlen(mapExtension); ++i)
				fname.removeLastChar();

			endingStr.format("%s\\%s%s", fname.str(), fname.str(), mapExtension);

			Bool skipMap = FALSE;
			if (TheGlobalData->m_buildMapCache)
			{
				std::set<AsciiString>::const_iterator sit = m_allowedMaps.find(fname);
				if (m_allowedMaps.size() != 0 && sit == m_allowedMaps.end())
				{
					//DEBUG_LOG(("Skipping map: '%s'\n", fname.str()));
					skipMap = TRUE;
				}
				else
				{
					//DEBUG_LOG(("Parsing map: '%s'\n", fname.str()));
				}
			}

			if (!skipMap)
			{
				if (!tempfilename.endsWithNoCase(endingStr.str()))
				{
					DEBUG_CRASH(("Found map '%s' in wrong spot (%s)", fname.str(), tempfilename.str()));
				}
				else
				{
					if (TheFileSystem->getFileInfo(tempfilename, &fileInfo)) {
						char funk[_MAX_PATH];
						// bounded, see above
						strncpy(funk, tempfilename.str(), _MAX_PATH - 1);
						funk[_MAX_PATH - 1] = 0;
						char *filenameptr = funk;
						char *tempchar = funk;
						while (*tempchar != 0) {
							if ((*tempchar == '\\') || (*tempchar == '/')) {
								filenameptr = tempchar+1;
							}
							++tempchar;
						}

						m_seen[tempfilename] = TRUE;
						parsedAMap |= addMap(mapDir, *iter, &fileInfo, TheGlobalData->m_buildMapCache);
					} else {
						DEBUG_CRASH(("Could not get file info for map %s", (*iter).str()));
					}
				}
			}
		}
		iter++;
	}

	//
	// The generated maps are in memory rather than in that directory, so the scan above cannot find
	// them.  They are added here instead, off the list the generator is holding, which is what puts
	// a random map in the list with its players, its start positions and its preview without any of
	// it having been written anywhere.
	//
	std::vector<AsciiString> generated;
	generatedMapPaths(generated);
	for (std::vector<AsciiString>::const_iterator gen = generated.begin(); gen != generated.end(); ++gen)
	{
		FileInfo generatedInfo;
		if (!TheFileSystem->getFileInfo(*gen, &generatedInfo))
			continue;

		m_seen[*gen] = TRUE;
		parsedAMap |= addMap(mapDir, *gen, &generatedInfo, FALSE);
	}

	// clean out unseen maps
	if (clearUnseenMaps(mapDir))
		return TRUE;

	return parsedAMap || userCacheBroken;
}

// The file name after the last separator of either kind, or the whole name when there is none.
// reverseFind('\\') + 1 was a pointer one past NULL for a name without a backslash.
static const char *mapFileNamePart( const AsciiString& fname )
{
	const char *back = fname.reverseFind('\\');
	const char *forward = fname.reverseFind('/');
	const char *sep = back;
	if (forward != NULL && (sep == NULL || forward > sep))
		sep = forward;
	return sep ? sep + 1 : fname.str();
}

// A generated map's name in the list: "<template> (Random) (N)", or "Random (N)" with no template,
// where its file name would say "rmg_v17_..." to the player.  FALSE for any other map.
static Bool generatedMapDisplayName( const MapCache& cache, const AsciiString& fname, Int numPlayers, UnicodeString& displayName )
{
	if (!isGeneratedMapPath( fname ))
		return FALSE;

	UnicodeString random = TheGameText->fetch( "GUI:Random" );
	AsciiString templatePath;
	if (generatedMapTemplate( fname, templatePath ))
	{
		MapCache::const_iterator shipped = cache.find( templatePath );
		if (shipped != cache.end() && !shipped->second.m_nameLookupTag.isEmpty())
			displayName = TheGameText->fetch( shipped->second.m_nameLookupTag );
		else
		{
			AsciiString name = mapFileNamePart( templatePath );
			for (Int i = 0; i < 4; ++i)
				name.removeLastChar();
			displayName.translate( name );
		}
		displayName.concat( u" (" );
		displayName.concat( random );
		displayName.concat( u")" );
	}
	else
		displayName = random;

	UnicodeString players;
	players.format( u" (%d)", numPlayers );
	displayName.concat( players );
	return TRUE;
}

//Bool MapCache::addMap( AsciiString dirName, AsciiString fname, WinTimeStamp timestamp, UnsignedInt filesize, Bool isOfficial )
Bool MapCache::addMap( AsciiString dirName, AsciiString fname, FileInfo *fileInfo, Bool isOfficial)
{
	if (fileInfo == NULL) {
		return FALSE;
	}

	AsciiString lowerFname;
	lowerFname = fname;
	lowerFname.toLower();
	MapCache::iterator it = find(lowerFname);

	MapMetaData md;
	UnsignedInt filesize = fileInfo->sizeLow;

	if (it != end())
	{
		// Found the map in our cache.  Check to see if it has changed.
		md = it->second;

		if ((md.m_filesize == filesize) &&
				(md.m_CRC != 0))
		{
			// the cache is keyed on the map file's size, which says nothing about its rules files
			if (hasMapRules( fname ))
				(*this)[lowerFname].m_CRC = calcCRC( dirName, fname );

			// Force a lookup so that we don't display the English localization in all builds.
			if (md.m_nameLookupTag.isEmpty())
			{
				// unofficial maps or maps without names
				AsciiString tempdisplayname;
				tempdisplayname = mapFileNamePart(fname);
				(*this)[lowerFname].m_displayName.translate(tempdisplayname);
				if (md.m_numPlayers >= 2)
				{
					UnicodeString extension;
					extension.format(u" (%d)", md.m_numPlayers);
					(*this)[lowerFname].m_displayName.concat(extension);
				}
			}
			else
			{
				// official maps with name tags
				(*this)[lowerFname].m_displayName = TheGameText->fetch(md.m_nameLookupTag);
				if (md.m_numPlayers >= 2)
				{
					UnicodeString extension;
					extension.format(u" (%d)", md.m_numPlayers);
					(*this)[lowerFname].m_displayName.concat(extension);
				}
			}
			generatedMapDisplayName( *this, lowerFname, md.m_numPlayers, (*this)[lowerFname].m_displayName );
//			DEBUG_LOG(("MapCache::addMap - found match for map %s\n", lowerFname.str()));
			return FALSE;	// OK, it checks out.
		}
		DEBUG_LOG(("%s didn't match file in MapCache\n", fname.str()));
		DEBUG_LOG(("size: %d / %d\n", filesize, md.m_filesize));
		DEBUG_LOG(("time1: %d / %d\n", fileInfo->timestampHigh, md.m_timestamp.m_highTimeStamp));
		DEBUG_LOG(("time2: %d / %d\n", fileInfo->timestampLow, md.m_timestamp.m_lowTimeStamp));
//		DEBUG_LOG(("size: %d / %d\n", filesize, md.m_filesize));
//		DEBUG_LOG(("time1: %d / %d\n", timestamp.m_highTimeStamp, md.m_timestamp.m_highTimeStamp));
//		DEBUG_LOG(("time2: %d / %d\n", timestamp.m_lowTimeStamp, md.m_timestamp.m_lowTimeStamp));
	}

	DEBUG_LOG(("MapCache::addMap(): caching '%s' because '%s' was not found\n", fname.str(), lowerFname.str()));

	// Just load for querying the data, since we aren't playing this map.  A user's map is a file
	// nobody here made, and one the chunk reader could not parse (ERROR_CORRUPT_FILE_FORMAT) used to
	// stop the game at startup, along with every good map in the same folder.  It is left out of the
	// list instead.  A shipped map that does not load under -buildMapCache is still an error.
	try
	{
		loadMap(fname);
	}
	catch (ErrorCode ec)
	{
		if (isOfficial)
			throw;
		DEBUG_LOG(("MapCache::addMap(): skipping '%s', it does not load (ErrorCode 0x%08x)\n", fname.str(), (UnsignedInt)ec));
		resetMap();
		return FALSE;
	}
	catch (...)
	{
		if (isOfficial)
			throw;
		DEBUG_LOG(("MapCache::addMap(): skipping '%s', it does not load (not an ErrorCode)\n", fname.str()));
		resetMap();
		return FALSE;
	}

	// The map is now loaded.  Pick out what we need.
	md.m_fileName = lowerFname;
	md.m_filesize = filesize;
	md.m_isOfficial = isOfficial;
	md.m_waypoints.update();
	md.m_numPlayers = md.m_waypoints.m_numStartSpots;
	md.m_isMultiplayer = (md.m_numPlayers >= 2);
	md.m_timestamp.m_highTimeStamp = fileInfo->timestampHigh;
	md.m_timestamp.m_lowTimeStamp = fileInfo->timestampLow;
	md.m_supplyPositions = m_supplyPositions;
	md.m_techPositions = m_techPositions;
	md.m_CRC = calcCRC(dirName, fname);

	Bool exists = false;
	AsciiString munkee = worldDict.getAsciiString(TheKey_mapName, &exists);
	md.m_nameLookupTag = munkee;
	if (!exists || munkee.isEmpty())
	{
		DEBUG_LOG(("Missing TheKey_mapName!\n"));
		AsciiString tempdisplayname;
		tempdisplayname = mapFileNamePart(fname);
		md.m_displayName.translate(tempdisplayname);
		if (md.m_numPlayers >= 2)
		{
			UnicodeString extension;
			extension.format(u" (%d)", md.m_numPlayers);
			md.m_displayName.concat(extension);
		}
		TheGameText->reset();
	}
	else
	{
		AsciiString stringFileName;
		stringFileName.format("%s\\%s", dirName.str(), fname.str());
		for (Int i=0; i<4; ++i)
			stringFileName.removeLastChar();
		stringFileName.concat("\\map.str");
		TheGameText->initMapStringFile(stringFileName);
		md.m_displayName = TheGameText->fetch(munkee);
		if (md.m_numPlayers >= 2)
		{
			UnicodeString extension;
			extension.format(u" (%d)", md.m_numPlayers);
			md.m_displayName.concat(extension);
		}
		DEBUG_LOG(("Map name is now '%s'\n", WideCharAsUtf8( md.m_displayName.str() ).str()));
		TheGameText->reset();
	}
	generatedMapDisplayName( *this, lowerFname, md.m_numPlayers, md.m_displayName );

	getExtent(&(md.m_extent));

	(*this)[lowerFname] = md;

	DEBUG_LOG(("  filesize = %d bytes\n", md.m_filesize));
	DEBUG_LOG(("  displayName = %s\n", WideCharAsUtf8( md.m_displayName.str() ).str()));
	DEBUG_LOG(("  CRC = %X\n", md.m_CRC));
	DEBUG_LOG(("  timestamp = %d\n", md.m_timestamp));
	DEBUG_LOG(("  isOfficial = %s\n", (md.m_isOfficial)?"yes":"no"));

	DEBUG_LOG(("  isMultiplayer = %s\n", (md.m_isMultiplayer)?"yes":"no"));
	DEBUG_LOG(("  numPlayers = %d\n", md.m_numPlayers));

	DEBUG_LOG(("  extent = (%2.2f,%2.2f) -> (%2.2f,%2.2f)\n",
		md.m_extent.lo.x, md.m_extent.lo.y,
		md.m_extent.hi.x, md.m_extent.hi.y));

	Coord3D pos;
	WaypointMap::iterator itw = md.m_waypoints.begin();
	while (itw != md.m_waypoints.end())
	{
		pos = itw->second;
		DEBUG_LOG(("    waypoint %s: (%2.2f,%2.2f)\n", itw->first.str(), pos.x, pos.y));
		++itw;
	}

	resetMap();

	return TRUE;
}

MapCache *TheMapCache = NULL;

// PUBLIC FUNCTIONS //////////////////////////////////////////////////////////////////////////////

Bool WouldMapTransfer( const AsciiString& mapName )
{
	return mapName.startsWithNoCase(TheMapCache->getUserMapDir());
}

//-------------------------------------------------------------------------------------------------
/** Load the listbox with all the map files available to play */
//-------------------------------------------------------------------------------------------------
Int populateMapListboxNoReset( GameWindow *listbox, Bool useSystemMaps, Bool isMultiplayer, AsciiString mapToSelect )
{
	if(!TheMapCache)
		return -1;

	if (!listbox)
		return -1;
	
	// reset the listbox content
	//GadgetListBoxReset( listbox );
	
	Int numColumns = GadgetListBoxGetNumColumns( listbox );
	const Image *easyImage = NULL;
	const Image *mediumImage = NULL;
	const Image *brutalImage = NULL;
	const Image *maxBrutalImage = NULL;
	SkirmishBattleHonors *battleHonors = NULL;
	Int w = 10, h = 10;
	if (numColumns > 1)
	{
		easyImage = TheMappedImageCollection->findImageByName("Star-Bronze");
		mediumImage = TheMappedImageCollection->findImageByName("Star-Silver");
		brutalImage = TheMappedImageCollection->findImageByName("Star-Gold");
		maxBrutalImage = TheMappedImageCollection->findImageByName("RedYell_Star");
		battleHonors = new SkirmishBattleHonors;

		w = (brutalImage)?brutalImage->getImageWidth():10;
		w = min(GadgetListBoxGetColumnWidth(listbox, 0), w);
		h = w;
	}

	Color color = GameMakeColor( 255, 255, 255, 255 );
	UnicodeString mapDisplayName;

	Int selectionIndex = 0; // always select *something*

	MapCache::iterator it = TheMapCache->begin();
	AsciiString mapDir;
	if (useSystemMaps)
	{
		mapDir = TheMapCache->getMapDir();
	}
	else
	{
		mapDir = TheGlobalData->getPath_UserData();
		mapDir.concat(TheMapCache->getMapDir());
	}
	mapDir.toLower();

typedef std::set<UnicodeString, rts::less_than_nocase<UnicodeString> > MapNameList;
typedef MapNameList::iterator MapNameListIter;

typedef std::map<UnicodeString, AsciiString> MapDisplayToFileNameList;
typedef MapDisplayToFileNameList::iterator MapDisplayToFileNameListIter;

	MapNameList tempCache;
	MapDisplayToFileNameList filenameMap;
	UnsignedInt numMapsListed = 0;
	UnsignedInt curNumPlayersInMap = 0;

	while (numMapsListed < TheMapCache->size()) {

		DEBUG_LOG(("Adding maps with %d players\n", curNumPlayersInMap));
		it = TheMapCache->begin();
		while (it != TheMapCache->end()) {
			const MapMetaData *md = &(it->second);
			if (md != NULL) {
				if (md->m_numPlayers == curNumPlayersInMap) {
					tempCache.insert(it->second.m_displayName);
					filenameMap[it->second.m_displayName] = it->first;
					DEBUG_LOG(("Adding map %s to temp cache.\n", it->first.str()));
					++numMapsListed;
				}
			}
			++it;
		}

		MapNameListIter tempit = tempCache.begin();

		while (tempit != tempCache.end())
		{

			AsciiString asciiMapName;
			asciiMapName = filenameMap[*tempit];
			it = TheMapCache->find(asciiMapName);
			/*
			if (it != TheMapCache->end())
			{
				DEBUG_LOG(("populateMapListbox(): looking at %s (displayName = %ls), mp = %d (== %d?) mapDir=%s (ok=%d)\n",
					it->first.str(), it->second.m_displayName.str(), it->second.m_isMultiplayer, isMultiplayer,
					mapDir.str(), it->first.startsWith(mapDir.str())));
			}
			*/
			
			//Patch 1.03 -- Purposely filter out these broken maps that exist in Generals.
			if( !asciiMapName.compare( "maps\\armored fury\\armored fury.map" ) || 
				!asciiMapName.compare( "maps\\scorched earth\\scorched earth.map" ) )
			{
				++tempit;
				continue;
			}

			DEBUG_ASSERTCRASH(it != TheMapCache->end(), ("Map %s not found in map cache.", *tempit));
			if (it->first.startsWithNoCase(mapDir.str()) && isMultiplayer == it->second.m_isMultiplayer && !it->second.m_displayName.isEmpty())
			{
				/// @todo: mapDisplayName = TheGameText->fetch(it->second.m_displayName.str());
				mapDisplayName = it->second.m_displayName;
				Int index = -1;
				Int imageItemData = -1;
				if (numColumns > 1 && it->second.m_isMultiplayer)
				{
					Int numEasy = battleHonors->getEnduranceMedal(it->first.str(), SLOT_EASY_AI);
					Int numMedium = battleHonors->getEnduranceMedal(it->first.str(), SLOT_MED_AI);
					Int numBrutal = battleHonors->getEnduranceMedal(it->first.str(), SLOT_BRUTAL_AI);
					if (numBrutal)
					{
						int maxBrutalSlots = it->second.m_numPlayers - 1;
						if (numBrutal == maxBrutalSlots)
						{
							index = GadgetListBoxAddEntryImage( listbox, maxBrutalImage, index, 0, w, h, TRUE);
							imageItemData = 4;
						}
						else	
						{
							index = GadgetListBoxAddEntryImage( listbox, brutalImage, index, 0, w, h, TRUE);
							imageItemData = 3;
						}
					}
					else if (numMedium)
					{
						imageItemData = 2;
						index = GadgetListBoxAddEntryImage( listbox, mediumImage, index, 0, w, h, TRUE);
					}
					else if (numEasy)
					{
						imageItemData = 1;
						index = GadgetListBoxAddEntryImage( listbox, easyImage, index, 0, w, h, TRUE);
					}
					else
					{
						imageItemData = 0;
						index = GadgetListBoxAddEntryImage( listbox, NULL, index, 0, w, h, TRUE);
					}
				}
				index = GadgetListBoxAddEntryText( listbox, mapDisplayName, color, index, numColumns-1 );

				if (it->first == mapToSelect)
				{
					selectionIndex = index;
				}

				// now set the char* as the item data.  this works because the map cache isn't being
				// modified while a map listbox is up.
				GadgetListBoxSetItemData( listbox, (void *)(it->first.str()), index );

				if (numColumns > 1)
				{
					GadgetListBoxSetItemData( listbox, (void *)(intptr_t)imageItemData, index, 1 );
				}
			}
			++tempit;
		}

		tempCache.clear();
		filenameMap.clear();
		++curNumPlayersInMap;
	}

	if (battleHonors)
	{
		delete battleHonors;
		battleHonors = NULL;
	}

	GadgetListBoxSetSelected(listbox, &selectionIndex, 1);
	if (selectionIndex >= 0)
	{
		Int topIndex = GadgetListBoxGetTopVisibleEntry(listbox);
		Int bottomIndex = GadgetListBoxGetBottomVisibleEntry(listbox);
		Int rowsOnScreen = bottomIndex - topIndex;

		if (selectionIndex >= bottomIndex)
		{
			Int newTop = max( 0, selectionIndex - max( 1, rowsOnScreen / 2 ) ); 
		//The trouble is that rowsonscreen/2 can be zero if bottom is 1 and top is zero
			GadgetListBoxSetTopVisibleEntry( listbox, newTop );
		}
	}
	return selectionIndex;

}  // end loadMapListbox

//-------------------------------------------------------------------------------------------------
/** Load the listbox with all the map files available to play */
//-------------------------------------------------------------------------------------------------
Int populateMapListbox( GameWindow *listbox, Bool useSystemMaps, Bool isMultiplayer, AsciiString mapToSelect )
{
	if(!TheMapCache)
		return -1;

	if (!listbox)
		return -1;
	
	// reset the listbox content
	GadgetListBoxReset( listbox );

	return populateMapListboxNoReset( listbox, useSystemMaps, isMultiplayer, mapToSelect );
}
	


//-------------------------------------------------------------------------------------------------
/** Validate a map */
//-------------------------------------------------------------------------------------------------
Bool isValidMap( AsciiString mapName, Bool isMultiplayer )
{
	if(!TheMapCache || mapName.isEmpty())
		return FALSE;
	TheMapCache->updateCache();

	// findMap, not find: a generated map is in the cache only while the generator holds it, and one
	// it has let go of - the room's map after a match, the one remembered from the last run - is
	// still a map, built again from its name
	const MapMetaData *md = TheMapCache->findMap(mapName);
	return md != NULL && isMultiplayer == md->m_isMultiplayer;
}  // end isValidMap

//-------------------------------------------------------------------------------------------------
/** Find a valid map */
//-------------------------------------------------------------------------------------------------
AsciiString getDefaultMap( Bool isMultiplayer )
{
	if(!TheMapCache)
		return AsciiString::TheEmptyString;
	TheMapCache->updateCache();

	MapCache::iterator it = TheMapCache->begin();
	while (it != TheMapCache->end())
	{
		if (isMultiplayer == it->second.m_isMultiplayer)
		{
			return it->first;
		}
		++it;
	}

	return AsciiString::TheEmptyString;
}


AsciiString getDefaultOfficialMap()
{
	if(!TheMapCache)
		return AsciiString::TheEmptyString;
	TheMapCache->updateCache();

	MapCache::iterator it = TheMapCache->begin();
	while (it != TheMapCache->end())
	{
		if (it->second.m_isMultiplayer && it->second.m_isOfficial)
		{
			return it->first;
		}
		++it;
	}
	return AsciiString::TheEmptyString;
}


Bool isOfficialMap( AsciiString mapName )
{
	if(!TheMapCache || mapName.isEmpty())
		return FALSE;
	TheMapCache->updateCache();
	mapName.toLower();
	MapCache::iterator it = TheMapCache->find(mapName);
	if (it != TheMapCache->end())
		return it->second.m_isOfficial;
	return FALSE;
}


const MapMetaData *MapCache::findMap(AsciiString mapName)
{
	mapName.toLower();
	MapCache::iterator it = find(mapName);
	if (it != end())
		return &(it->second);

	//
	// A map that is there but not in the cache is read now and cached like any other rather than
	// reported missing - which is what the map is asked for at, among other places, the point where
	// the start positions are dealt out, and that caller does not survive a no.  Two ways to get
	// here: a generated map is in the cache only if something staged it this run, and a replay, a
	// save or a game somebody else is hosting names one that nothing here has built (the name
	// carries the settings, so it is built on the spot); and a Maps\MapCache.ini that leaves a
	// shipped map out, which is then in the archives but nowhere in the cache.
	//
	FileInfo fileInfo;
	if (!TheFileSystem->getFileInfo(mapName, &fileInfo))
		return NULL;

	const Bool isUserMap = isGeneratedMapPath(mapName) || mapName.startsWithNoCase(getUserMapDir());
	m_seen[mapName] = TRUE;
	addMap(isUserMap ? getUserMapDir() : getMapDir(), mapName, &fileInfo, FALSE);

	it = find(mapName);
	if (it == end())
		return NULL;
	// added as unofficial so a map that does not parse is left out instead of thrown; one from the
	// archives is still a shipped map, and nothing offers to send it to the other players
	if (!isUserMap)
		it->second.m_isOfficial = TRUE;
	return &(it->second);
}

// ------------------------------------------------------------------------------------------------
/** Embed the pristine map into the xfer stream */
// ------------------------------------------------------------------------------------------------
static void copyFromBigToDir( const AsciiString& infile, const AsciiString& outfile )
{
	// open the map file

	File *file = TheFileSystem->openFile( infile.str(), File::READ | File::BINARY );
	if( file == NULL )
	{
		DEBUG_CRASH(( "copyFromBigToDir - Error opening source file '%s'\n", infile.str() ));
		throw SC_INVALID_DATA;
	} // end if

	// how big is the map file
	Int fileSize = file->seek( 0, File::END );


	// rewind to beginning of file
	file->seek( 0, File::START );

	// allocate buffer big enough to hold the entire map file
	char *buffer = NEW char[ fileSize ];
	if( buffer == NULL )
	{
		DEBUG_CRASH(( "copyFromBigToDir - Unable to allocate buffer for file '%s'\n", infile.str() ));
		throw SC_INVALID_DATA;
	} // end if

	// copy the file to the buffer
	if( file->read( buffer, fileSize ) < fileSize )
	{
		DEBUG_CRASH(( "copyFromBigToDir - Error reading from file '%s'\n", infile.str() ));
		throw SC_INVALID_DATA;
	} // end if
	// close the BIG file
	file->close();
	
	File *filenew = TheFileSystem->openFile( outfile.str(), File::WRITE | File::CREATE | File::BINARY );
	
	if( !filenew || filenew->write(buffer, fileSize) < fileSize)
	{
		DEBUG_CRASH(( "copyFromBigToDir - Error writing to file '%s'\n", outfile.str() ));
		throw SC_INVALID_DATA;
	} // end if

	filenew->close();

	// delete the buffer
	delete [] buffer;
} // end embedPristineMap

Image *getMapPreviewImage( AsciiString mapName )
{
	if(!TheGlobalData)
		return NULL;
	DEBUG_LOG(("%s Map Name \n", mapName.str()));
	AsciiString tgaName = mapName;
	AsciiString name;
	AsciiString tempName;
	AsciiString filename;
	tgaName.removeLastChar(); // p
	tgaName.removeLastChar(); // a
	tgaName.removeLastChar(); // m
	tgaName.removeLastChar(); // .
	name = tgaName;//.reverseFind('\\') + 1;
	filename = tgaName.reverseFind('\\') + 1;
	//tgaName = name;
	filename.concat(".tga");
	tgaName.concat(".tga");

	AsciiString portableName = TheGameState->realMapPathToPortableMapPath(name);
	tempName.set(AsciiString::TheEmptyString);
	for(Int i = 0; i < portableName.getLength(); ++i)
	{
		char c = portableName.getCharAt(i);
		if (c == '\\' || c == ':')
			tempName.concat('_');
		else
			tempName.concat(c);
	}
	
	name = tempName;
	name.concat(".tga");

	
	// copy file over	
	// copy source tgaName, to name

	Image *image = (Image *)TheMappedImageCollection->findImageByName(tempName);
	if(!image)
	{

		if(!TheFileSystem->doesFileExist(tgaName.str()))
			return NULL;	
		AsciiString mapPreviewDir;
		mapPreviewDir.format(MAP_PREVIEW_DIR_PATH, TheGlobalData->getPath_UserData().str());
		TheFileSystem->createDirectory(mapPreviewDir);

		mapPreviewDir.concat(name);

		Bool success = false;
		try
		{
			copyFromBigToDir(tgaName, mapPreviewDir);	
			success = true;
		} 
		catch (...)
		{
			success = false;	// no rethrow
		}
		
		if (success)
		{
    	image = newInstance(Image);
			image->setName(tempName);
			//image->setFullPath("mission.tga");
			image->setFilename(name);
			image->setStatus(IMAGE_STATUS_NONE);
			Region2D uv;
			uv.hi.x = 1.0f;
			uv.hi.y = 1.0f;
			uv.lo.x	= 0.0f;
			uv.lo.y = 0.0f;
			image->setUV(&uv);
			image->setTextureHeight(128);
			image->setTextureWidth(128);
			TheMappedImageCollection->addImage(image);
		}
		else
		{
			image = NULL;
		}
	}

	return image;

	
	
/*
	// sanity
	if( mapName.isEmpty() )
		return NULL;
	Region2D uv;
	mapPreviewImage = TheMappedImageCollection->findImageByName("MapPreview");
	if(mapPreviewImage)
		mapPreviewImage->deleteInstance();
	
	mapPreviewImage = TheMappedImageCollection->newImage();
	mapPreviewImage->setName("MapPreview");
	mapPreviewImage->setStatus(IMAGE_STATUS_RAW_TEXTURE);
// allocate our terrain texture
	TextureClass * texture = new TextureClass( size.x, size.y, 
																			 WW3D_FORMAT_X8R8G8B8, TextureClass::MIP_LEVELS_1 );
	uv.lo.x = 0.0f;
	uv.lo.y = 1.0f;
	uv.hi.x = 1.0f;
	uv.hi.y = 0.0f;
	mapPreviewImage->setStatus( IMAGE_STATUS_RAW_TEXTURE );
	mapPreviewImage->setRawTextureData( texture );
	mapPreviewImage->setUV( &uv );
	mapPreviewImage->setTextureWidth( size.x );
	mapPreviewImage->setTextureHeight( size.y );
	mapPreviewImage->setImageSize( &size );


	CachedFileInputStream theInputStream;
	if (theInputStream.open(AsciiString(mapName.str()))) 
	{
		ChunkInputStream *pStrm = &theInputStream;
		pStrm->absoluteSeek(0);
		DataChunkInput file( pStrm );
		if (file.isValidFileType()) {	// Backwards compatible files aren't valid data chunk files.
			// Read the waypoints.
			file.registerParser( AsciiString("MapPreview"), AsciiString::TheEmptyString, parseMapPreviewChunk );
			if (!file.parse(NULL)) {
				DEBUG_ASSERTCRASH(false,("Unable to read MapPreview info."));
				mapPreviewImage->deleteInstance();
				return NULL;
			}
		}
		theInputStream.close();
	}
	else
	{
		mapPreviewImage->deleteInstance();
		return NULL;
	}
	

	return mapPreviewImage;
	
*/
	return NULL;
}

Bool parseMapPreviewChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
/*
	ICoord2D size;
	
	SurfaceClass *surface;
	size.x = file.readInt();
	size.y = file.readInt();


	surface = (TextureClass *)mapPreviewImage->getRawTextureData()->Get_Surface_Level();
	//texture->Get_Surface_Level();
	
	DEBUG_LOG(("BeginMapPreviewInfo\n"));
	UnsignedInt *buffer = new UnsignedInt[size.x * size.y];
	Int x,y;
	for (y=0; y<size.y; y++) {
		for(x = 0; x< size.x; x++)
		{
			surface->DrawPixel( x, y, file.readInt() );
			buffer[y + x] = file.readInt();
			DEBUG_LOG(("x:%d, y:%d, %X\n", x, y, buffer[y + x]));
		}
	}
	mapPreviewImage->setRawTextureData(buffer);
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	DEBUG_LOG(("EndMapPreviewInfo\n"));
	REF_PTR_RELEASE(surface);
	return true;
*/
	return FALSE;
}

void findDrawPositions( Int startX, Int startY, Int width, Int height, Region3D extent,
															 ICoord2D *ul, ICoord2D *lr )
{

	Real ratioWidth;
	Real ratioHeight;
	Coord2D radar;
	ratioWidth = extent.width()/(width * 1.0f);
	ratioHeight = extent.height()/(height* 1.0f);
	
	if( ratioWidth >= ratioHeight)
	{
		radar.x = extent.width() / ratioWidth;
		radar.y = extent.height()/ ratioWidth;
		ul->x = 0;
		ul->y = (height - radar.y) / 2.0f;
		lr->x = radar.x;
		lr->y = height - ul->y;
	}
	else
	{
		radar.x = extent.width() / ratioHeight;
		radar.y = extent.height()/ ratioHeight;
		ul->x = (width - radar.x ) / 2.0f;
		ul->y = 0;
		lr->x = width - ul->x;
		lr->y = radar.y;
	}

	// make them pixel positions
	ul->x += startX;
	ul->y += startY;
	lr->x += startX;
	lr->y += startY;

}  // end findDrawPositions

