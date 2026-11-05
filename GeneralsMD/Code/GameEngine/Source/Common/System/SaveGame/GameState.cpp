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

// FILE: GameState.cpp ////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, September 2002
// Desc:   Game state singleton from which to load and save the game state
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"
#include "zhio.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/LocalFileSystem.h"
#include "Common/LatchRestore.h"
#include "Common/MapObject.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Radar.h"
#include "Common/Recorder.h"
#include "Common/Team.h"
#include "Common/WellKnownKeys.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
GameState *TheGameState = NULL;

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static const Char *SAVE_FILE_EOF       = "SG_EOF";
static const Char *SAVE_GAME_EXTENSION = ".sav";
static const Char *ZERO_NAME_ONLY      = "00000000";
static const Int MAX_SAVE_FILE_NUMBER  =  99999999;

// The GameState block's version.  4 adds nothing to the block: it marks the build where logic went
// to 60 ticks a second, and a file written before that cannot load in this one (a mission save
// aside: SaveGameInfo::isLoadable).  The original
// game's files are 2, as were this fork's until 3 added the game speed.
static const XferVersion GAME_STATE_VERSION = 4;

///////////////////////////////////////////////////////////////////////////////////////////////////
#define GAME_STATE_BLOCK_STRING "CHUNK_GameState"  // block of save game data with game info data
#define CAMPAIGN_BLOCK_STRING "CHUNK_Campaign"		 // block of game data that has campaign info

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SaveGameInfo::SaveGameInfo( void )
{

	date.day					= 0;
	date.dayOfWeek		= 0;
	date.hour					= 0;
	date.milliseconds = 0;
	date.minute				= 0;
	date.month				= 0;
	date.second				= 0;
	date.year					= 0;
	missionNumber			= 0;
	saveFileType			= SAVE_FILE_TYPE_NORMAL;
	framesPerSecond		= 0;
	version						= 0;

}  // end SaveGameInfo

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SaveGameInfo::~SaveGameInfo( void )
{

}  // end ~SaveGameInfo

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool SaveGameInfo::isLoadable( void ) const
{

	// a newer one never gets this far: xferVersion refuses it while the header is read
	if( version >= GAME_STATE_VERSION )
		return TRUE;

	// A mission save holds only this block and the campaign's, neither of which counts frames, so
	// the one an older build or the original game wrote at a mission's start still loads.  Version
	// 2 is the first that records the file type.
	return version >= 2 && saveFileType == SAVE_FILE_TYPE_MISSION;

}  // end isLoadable

// ------------------------------------------------------------------------------------------------
/** Is this date newer than the other one passed in? */
// ------------------------------------------------------------------------------------------------
Bool SaveDate::isNewerThan( SaveDate *other )
{

	// year
	if( year > other->year )
		return TRUE;
	else if( year < other->year )
		return FALSE;
	else
	{

		// month
		if( month > other->month )
			return TRUE;
		else if( month < other->month )
			return FALSE;
		else
		{
			
			// day
			if( day > other->day )
				return TRUE;
			else if( day < other->day )
				return FALSE;
			else
			{

				// hour
				if( hour > other->hour )
					return TRUE;
				else if( hour < other->hour )
					return FALSE;
				else
				{

					// minute
					if( minute > other->minute )
						return TRUE;
					else if( minute < other->minute )
						return FALSE;
					else
					{

						// second
						if( second > other->second )
							return TRUE;
						else if( second < other->second )
							return FALSE;
						else
						{

							// millisecond
							if( milliseconds > other->milliseconds )
								return TRUE;
							else
								return FALSE;

						}  // end else

					}  // end else

				}  // end else

			}  // end else

		}  // end else

	}  // end else

}  // end isNewerThan

// ------------------------------------------------------------------------------------------------
/** Find a snapshot block info that matches the token passed in */
// ------------------------------------------------------------------------------------------------
GameState::SnapshotBlock *GameState::findBlockInfoByToken( AsciiString token, SnapshotType which )
{

	// sanity
	if( token.isEmpty() )
		return NULL;

	// search for match our list
	SnapshotBlock *blockInfo;
	SnapshotBlockListIterator it;
	for( it = m_snapshotBlockList[which].begin(); it != m_snapshotBlockList[which].end(); ++it )
	{

		// get info
		blockInfo = &(*it);

		// check for match
		if( blockInfo->blockName == token )
			return blockInfo;

	}  // end for

	// not found
	return NULL;

}  // end findLexiconEntryByToken

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
#if !defined(_WIN32)
#include "Lib/WideCharFns.h"

/* Off Windows the same two strings come from strftime, in whatever LC_TIME the process has - the
	 user's, once the platform layer calls setlocale(LC_TIME, "") at startup, and "C" until then.
	 strftime writes LC_TIME's codeset, which is UTF-8 in practice, while LC_CTYPE stays "C" on
	 purpose (the port's rules say why), so the bytes are decoded as UTF-8 explicitly: mbstowcs would
	 decode them by LC_CTYPE and mangle every non-ASCII month name.  Display only; nothing here
	 reaches the simulation. */
static UnicodeString formatWallClock( const WallClockTime &timeVal, const char *format )
{
	struct tm when;
	memset( &when, 0, sizeof( when ) );
	when.tm_year = timeVal.wYear - 1900;
	when.tm_mon = timeVal.wMonth - 1;
	when.tm_mday = timeVal.wDay;
	when.tm_wday = timeVal.wDayOfWeek;
	when.tm_hour = timeVal.wHour;
	when.tm_min = timeVal.wMinute;
	when.tm_sec = timeVal.wSecond;
	when.tm_isdst = -1;

	char bytes[ 256 ];
	if (strftime( bytes, sizeof( bytes ), format, &when ) == 0)
		bytes[ 0 ] = 0;
	WideChar text[ 256 ];
	WideCharFromUtf8( bytes, text, sizeof( text ) / sizeof( text[ 0 ] ) );
	UnicodeString result;
	result.set( text );
	return result;
}
#endif

/* LOCALE_USER_DEFAULT, not LOCALE_SYSTEM_DEFAULT, in both of these: the system locale is the one
	 the machine was installed with and is what non-Unicode programs get, while the user locale is the
	 one the person sitting there picked in Region settings. On a machine set up in one country and
	 used in another - which is most machines that run this game now - the dates in the replay and
	 save lists came out in a format the owner never chose. */
UnicodeString getUnicodeDateBuffer(WallClockTime timeVal)
{
#if !defined(_WIN32)
	return formatWallClock( timeVal, "%x" );	// the locale's short date, as DATE_SHORTDATE
#else
	// setup date buffer for local region date format
	#define DATE_BUFFER_SIZE 256
	OSVERSIONINFO	osvi;
	osvi.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);
	UnicodeString displayDateBuffer;
	if (GetVersionEx(&osvi))
	{	//check if we're running Win9x variant since they may need different characters
		if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
		{		
			char dateBuffer[ DATE_BUFFER_SIZE ];
			GetDateFormat( LOCALE_USER_DEFAULT,
										 DATE_SHORTDATE,
										 &timeVal,
										 NULL,
										 dateBuffer, sizeof(dateBuffer) );
			displayDateBuffer.translate(dateBuffer);
			return displayDateBuffer;
		}	
	}
	// WideChar, cast at the W call: the same two bytes as WCHAR on Windows.  The size is in characters,
	// as GetDateFormatW takes it; it used to be sizeof(dateBuffer), twice the buffer.
	WideChar dateBuffer[ DATE_BUFFER_SIZE ];
	GetDateFormatW( LOCALE_USER_DEFAULT,
								 DATE_SHORTDATE,
								 &timeVal,
								 NULL,
								 reinterpret_cast<LPWSTR>( dateBuffer ), DATE_BUFFER_SIZE );
	displayDateBuffer.set(dateBuffer);
	return displayDateBuffer;
	//displayDateBuffer.format( L"%ls", dateBuffer );
#endif
}															

UnicodeString getUnicodeTimeBuffer(WallClockTime timeVal) 
{
#if !defined(_WIN32)
	// The locale's own time format.  Unlike TIME_NOSECONDS below it shows seconds: strftime has no
	// "this locale's format without the seconds".
	return formatWallClock( timeVal, "%X" );
#else
	// setup time buffer for local region time format
	UnicodeString displayTimeBuffer;
	OSVERSIONINFO	osvi;
	osvi.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);
	if (GetVersionEx(&osvi))
	{	//check if we're running Win9x variant since they may need different characters
		if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
		{		
			char timeBuffer[ DATE_BUFFER_SIZE ];
			GetTimeFormat( LOCALE_USER_DEFAULT,
										 TIME_NOSECONDS|TIME_FORCE24HOURFORMAT|TIME_NOTIMEMARKER,
										 &timeVal,
										 NULL,
										 timeBuffer, sizeof(timeBuffer) );
			displayTimeBuffer.translate(timeBuffer);
			return displayTimeBuffer;
		}
	}
	// setup time buffer for local region time format
	#define TIME_BUFFER_SIZE 256
	// As dateBuffer above: WideChar, cast at the W call, sized in characters.
	WideChar timeBuffer[ TIME_BUFFER_SIZE ];
	GetTimeFormatW( LOCALE_USER_DEFAULT,
								 TIME_NOSECONDS,
								 &timeVal,
								 NULL,
								 reinterpret_cast<LPWSTR>( timeBuffer ),
								 TIME_BUFFER_SIZE );
	displayTimeBuffer.set(timeBuffer);
	return displayTimeBuffer;
#endif
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
GameState::GameState( void )
{

	m_availableGames = NULL;
	m_isInLoadGame = FALSE;

}  // end GameState

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
GameState::~GameState( void )
{

	// clear our snapshot block list
	for (Int i=0; i<SNAPSHOT_MAX; ++i)
	m_snapshotBlockList[i].clear();

	// make certain that the post process list is clean
	m_snapshotPostProcessList.clear();

	// clear any available game 
	clearAvailableGames();

}  // end ~GameState

// ------------------------------------------------------------------------------------------------
/** Init the game state subsystem */
// ------------------------------------------------------------------------------------------------
void GameState::init( void )
{

	// add all the snapshot objects to our list of data blocks for save game files
	addSnapshotBlock( GAME_STATE_BLOCK_STRING,				TheGameState,							SNAPSHOT_SAVELOAD );
	addSnapshotBlock( CAMPAIGN_BLOCK_STRING,					TheCampaignManager,				SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_GameStateMap",						TheGameStateMap,					SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_TerrainLogic",						TheTerrainLogic,					SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_TeamFactory",						TheTeamFactory,						SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_Players",								ThePlayerList,						SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_GameLogic",							TheGameLogic,							SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_WeaponStore",						TheWeaponStore,						SNAPSHOT_SAVELOAD );	// absent from older saves, which load without it
	addSnapshotBlock( "CHUNK_Radar",								TheRadar,									SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_ScriptEngine",						TheScriptEngine,					SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_SidesList",							TheSidesList,							SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_TacticalView",						TheTacticalView,					SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_GameClient",							TheGameClient,						SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_InGameUI",								TheInGameUI,							SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_Partition",							ThePartitionManager,			SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_ParticleSystem",					TheParticleSystemManager,	SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_TerrainVisual",					TheTerrainVisual,					SNAPSHOT_SAVELOAD );
	addSnapshotBlock( "CHUNK_GhostObject",						TheGhostObjectManager,		SNAPSHOT_SAVELOAD );

	// add all the snapshot objects to our list of data blocks for deep CRCs of logic
	addSnapshotBlock( "CHUNK_TeamFactory",						TheTeamFactory,						SNAPSHOT_DEEPCRC_LOGICONLY );
	addSnapshotBlock( "CHUNK_Players",								ThePlayerList,						SNAPSHOT_DEEPCRC_LOGICONLY );
	addSnapshotBlock( "CHUNK_GameLogic",							TheGameLogic,							SNAPSHOT_DEEPCRC_LOGICONLY );
	addSnapshotBlock( "CHUNK_ScriptEngine",						TheScriptEngine,					SNAPSHOT_DEEPCRC_LOGICONLY );
	addSnapshotBlock( "CHUNK_SidesList",							TheSidesList,							SNAPSHOT_DEEPCRC_LOGICONLY );
	addSnapshotBlock( "CHUNK_Partition",							ThePartitionManager,			SNAPSHOT_DEEPCRC_LOGICONLY );

	m_isInLoadGame = FALSE;

}  // end init

// ------------------------------------------------------------------------------------------------
/** Reset */
// ------------------------------------------------------------------------------------------------a
void GameState::reset( void )
{

	// clear the post process snapshot list
	m_snapshotPostProcessList.clear();

	// clear any available game 
	clearAvailableGames();

	m_isInLoadGame = FALSE;

}  // end reset

// ------------------------------------------------------------------------------------------------
/** Clear any available games entries */
// ------------------------------------------------------------------------------------------------
void GameState::clearAvailableGames( void )
{
	AvailableGameInfo *gameInfo;

	while( m_availableGames )
	{

		gameInfo = m_availableGames->next;
		delete m_availableGames;
		m_availableGames = gameInfo;

	}  // end while

}  // end clearAvailableGames

// ------------------------------------------------------------------------------------------------
/** Add a snapshot and block name pair to the systems used to load and save */
// ------------------------------------------------------------------------------------------------
void GameState::addSnapshotBlock( AsciiString blockName, Snapshot *snapshot, SnapshotType which )
{

	// sanity
	if( blockName.isEmpty() || snapshot == NULL )
	{

		DEBUG_CRASH(( "addSnapshotBlock: Invalid parameters\n" ));
		return;

	}  // end if

	// add to the list
	SnapshotBlock blockInfo;
	blockInfo.snapshot = snapshot;
	blockInfo.blockName = blockName;
	m_snapshotBlockList[which].push_back( blockInfo );

}  // end addSnapshotBlock

// ------------------------------------------------------------------------------------------------
/** Given the filename of a save file, find the highest filename number */
// ------------------------------------------------------------------------------------------------
static void findHighFileNumber( AsciiString filename, void *userData )
{

	// sanity
	if( filename.isEmpty() )
		return;

	// sanity check for ".sav" at the end of the filename
	if( filename.endsWithNoCase( SAVE_GAME_EXTENSION ) == FALSE )
		return;

	// strip off the extension at the end of the filename
	AsciiString nameOnly = filename;
	for( Int count = 0; count < strlen( SAVE_GAME_EXTENSION ); count++ )
		nameOnly.removeLastChar();
	
	// convert filename (which is only numbers) to a number
	Int fileNumber = atoi( nameOnly.str() );

	//
	// atoi will return zero if the string could not be converted, if the filename is
	// not literally "00000000.sav" then that means the conversion could not be done so
	// we should reject this filename from further processing
	//
	if( fileNumber == 0 && nameOnly.compare( ZERO_NAME_ONLY ) != 0 )
		return;

	// compare against the file number we're keeping tally of in the user data parameter
	Int *highFileNumber = (Int *)userData;
	if( fileNumber >= *highFileNumber )
		*highFileNumber = fileNumber;

}  // end findHighFileNumber

// ------------------------------------------------------------------------------------------------
/** Given the save files on disk, find the "next" filename to use when saving a game */
// ------------------------------------------------------------------------------------------------
AsciiString GameState::findNextSaveFilename( UnicodeString desc )
{
// works, but needs approval from mgmt (srj)
	// GS activating for patch
#define COCKBEER_DONT_USE_NORMAL_FILE_NAMES_THANKS
#ifdef USE_NORMAL_FILE_NAMES_THANKS

	Int i;
	AsciiString adesc;
	for (i = 0; i < desc.getLength(); ++i)
	{
		char c = (char)desc.getCharAt(i);
		if (isalnum(c))
			adesc.concat(c);
		else
			adesc.concat('_');
	}

	for (i = 1; i <= 9999; ++i)
	{
		AsciiString leaf;
		leaf.format("%s_%04d%s", adesc.str(), i, SAVE_GAME_EXTENSION);

		AsciiString path = getFilePathInSaveDirectory(leaf);
		if( zh_access( path.str(), 0 ) == -1 )
			return leaf;	// note that this returns the leaf, not the full path
	}
#else
	//
	// This method has code to support two modes of finding the next filename, one of
	// them starts with a filename of 00000000.sav and counts up the number looking for
	// a file that doesn't exist and therefore a filename we can use (LOWEST_NUMBER).  
	// The other method iterates all save files and returns a filename that is one larger 
	// than the largest filename number encountered (HIGHEST_NUMBER)
	//
	enum FindNextFileType { LOWEST_NUMBER = 0, HIGHEST_NUMBER = 1 };
	FindNextFileType searchType = LOWEST_NUMBER;

	if( searchType == HIGHEST_NUMBER )
	{

		// iterate all the save files in the directory and find the highest file number
		Int highFileNumber = -1;
		iterateSaveFiles( findHighFileNumber, &highFileNumber );

		// check for a filename that is too big (this is unlikely but theoretically possible)
		if( highFileNumber + 1 > MAX_SAVE_FILE_NUMBER )
			return AsciiString::TheEmptyString;

		// construct filename with a number higher than the highest one we found
		AsciiString filename;
		filename.format( "%08d%s", highFileNumber + 1, SAVE_GAME_EXTENSION );
		return filename;
				
	}  // end if
	else if( searchType == LOWEST_NUMBER )
	{
		AsciiString filename;
		AsciiString fullPath;
		Int i = 0;

		while( TRUE )
		{

			// construct filename (########.sav)
			filename.format( "%08d%s", i, SAVE_GAME_EXTENSION );

			// construct full path to file given the filename
			fullPath = getFilePathInSaveDirectory(filename);

			// if file does not exist we're all good
			if( zh_access( fullPath.str(), 0 ) == -1 )
				return filename;

			// test the text filename
			i++;

			// check for at the max limit (this is highly unlikely but possible)
			if( i > MAX_SAVE_FILE_NUMBER )
				return AsciiString::TheEmptyString;

		}  // end while

	}  // end else if
	else
	{

		DEBUG_CRASH(( "GameState::findNextSaveFilename - Unknown file search type '%d'\n", searchType ));
		return AsciiString::TheEmptyString;

	}  // end else
#endif

	// no appropriate filename could be found, return the empty string
	return AsciiString::TheEmptyString;

}  // end findNextSaveFilename

// ------------------------------------------------------------------------------------------------
/** Save the current state of the engine in a save file
	* NOTE: filename is a *filename only* */
// ------------------------------------------------------------------------------------------------
SaveCode GameState::saveGame( AsciiString filename, UnicodeString desc, 
															SaveFileType saveType, SnapshotType which )
{

	// if there is no filename, this is a new file being created, find an appropriate filename
	if( filename.isEmpty() )
		filename = findNextSaveFilename( desc );
	if( filename.isEmpty() )
	{

		DEBUG_CRASH(( "GameState::saveGame - Unable to find valid filename for save game\n" ));
		return SC_NO_FILE_AVAILABLE;

	}  // end if

	// make absolutely sure the save directory exists
	TheLocalFileSystem->createDirectory( getSaveDirectory() );

	// construct path to file
	AsciiString filepath = getFilePathInSaveDirectory(filename);

	// save description as current description in the game state
	m_gameInfo.description = desc;

	// open the save file
	XferSave xferSave;
	try {
		xferSave.open( filepath );
	} catch(...) {
		// print error message to the user
		TheInGameUI->message( "GUI:Error" );
		DEBUG_LOG(( "Error opening file '%s'\n", filepath.str() ));
		return SC_ERROR;
	}

	// save our save file type
	SaveGameInfo *gameInfo = getSaveGameInfo();
	gameInfo->saveFileType = saveType;

	// save our mission map name if applicable
	if( saveType == SAVE_FILE_TYPE_MISSION )
		gameInfo->missionMapName = TheCampaignManager->getCurrentMap();
	else
		gameInfo->missionMapName.clear();

	// set the pristine map to the current campaign map
	// this is now done during startNewGame()
//	gameInfo->pristineMapName = TheCampaignManager->getCurrentMap();

	// write the save file.  Close is in here too: it is where the bytes reach the disk, and where a
	// full disk shows.  Either failing leaves the slot's earlier save as it was.
	try
	{

		// save file
		xferSaveData( &xferSave, which );
		xferSave.close();

	}  // end try
	catch( ... )
	{

		xferSave.discard();

		UnicodeString ufilepath;
		ufilepath.translate(filepath);

		UnicodeString msg;
		msg.format( TheGameText->fetch("GUI:ErrorSavingGame"), ufilepath.str() );

		MessageBoxOk(TheGameText->fetch("GUI:Error"), msg, NULL);

		return SC_ERROR;

	}  // end catch

	// print message to the user for game successfully saved
	UnicodeString msg = TheGameText->fetch( "GUI:GameSaveComplete" );
	TheInGameUI->message( msg );

	return SC_OK;

}  // end saveGame

// ------------------------------------------------------------------------------------------------
/** A mission save */
// ------------------------------------------------------------------------------------------------
SaveCode GameState::missionSave( void )
{

	// get campaign
	Campaign *campaign = TheCampaignManager->getCurrentCampaign();

	// get mission #
	Int missionNumber = TheCampaignManager->getCurrentMissionNumber() + 1;

	// format a string for the mission save description
	UnicodeString format = TheGameText->fetch( "GUI:MissionSave" );
	UnicodeString desc;
	desc.format( format, TheGameText->fetch( campaign->m_campaignNameLabel ).str(), missionNumber );

	// do an automatic mission save
	return TheGameState->saveGame( AsciiString(""), desc, SAVE_FILE_TYPE_MISSION );

}  // end missionSave

// ------------------------------------------------------------------------------------------------
/** A replay's rewind checkpoint.  saveGame without the save list: any path, no description, and
	* nothing told to the player, since one is taken every half minute of the replay. */
// ------------------------------------------------------------------------------------------------
Bool GameState::saveCheckpoint( AsciiString filepath )
{
	getSaveGameInfo()->saveFileType = SAVE_FILE_TYPE_NORMAL;
	getSaveGameInfo()->missionMapName.clear();

	XferSave xferSave;
	try
	{
		xferSave.open( filepath );
		xferSaveData( &xferSave, SNAPSHOT_SAVELOAD );
		xferSave.close();
	}
	catch( ... )
	{
		// the destructor throws away what was written; an object whose module cannot save
		// costs the replay its rewind, not the whole game (report 1858)
		DEBUG_LOG(( "GameState::saveCheckpoint - '%s' could not be written\n", filepath.str() ));
		return FALSE;
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** loadGame for a checkpoint, with the recorder's turn between the reset and the load: the reset
	* closes the replay, and the load's new game reads the players out of it.  FALSE when the load
	* failed part way: the half-loaded match is cleared, as a replay's end clears it, and the player
	* is told, so an object whose module cannot load ends the replay rather than the game. */
// ------------------------------------------------------------------------------------------------
Bool GameState::loadCheckpoint( AsciiString filepath, const std::function< void( void ) > &afterReset )
{
	TheGameStateMap->clearScratchPadMaps();

	XferLoad xferLoad;
	xferLoad.open( filepath );

	TheGameEngine->reset();
	afterReset();

	// The post-process is part of the load, as it is in loadGame: a Cold Fusion plant's Control Rods
	// put its power back there, and outside the load that browned the player out or in again and
	// idled buildings whose update modules were not back on the sleepy heap yet, "sleepy update module
	// illegal index" on a replay rewind.  The save already holds every building's power state.
	LatchRestore<Bool> inLoadGame( m_isInLoadGame, TRUE );

	TheGhostObjectManager->saveLockGhostObjects( TRUE );
	TheGameLogic->setLoadingWithoutScreen( TRUE );
	Bool error = FALSE;
	try
	{
		xferSaveData( &xferLoad, SNAPSHOT_SAVELOAD );
	}
	catch( ... )
	{
		error = TRUE;
	}
	TheGameLogic->setLoadingWithoutScreen( FALSE );
	xferLoad.close();
	TheGhostObjectManager->saveLockGhostObjects( FALSE );

	if( !error )
	{
		try
		{
			gameStatePostProcessLoad();
		}
		catch( ... )
		{
			error = TRUE;
		}
	}

	if( error )
	{
		DEBUG_LOG(( "GameState::loadCheckpoint - '%s' could not be loaded, the replay stops\n", filepath.str() ));
		const AsciiString replayFile = TheRecorder->getCurrentReplayFilename();
		TheGameLogic->clearGameData();

		UnicodeString ufilepath;
		ufilepath.translate( replayFile );
		UnicodeString msg;
		msg.format( TheGameText->fetch( "GUI:ErrorLoadingGame" ), ufilepath.str() );
		MessageBoxOk( TheGameText->fetch( "GUI:Error" ), msg, NULL );
		return FALSE;
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** Load the save game pointed to by filename */
// ------------------------------------------------------------------------------------------------
SaveCode GameState::loadGame( AvailableGameInfo gameInfo )
{

	// sanity check for file
	if( doesSaveGameExist( gameInfo.filename ) == FALSE )
		return SC_FILE_NOT_FOUND;

	// the original game's saves and this fork's older ones read as garbage here
	if( refuseUnloadableSave( gameInfo.saveGameInfo ) )
		return SC_INVALID_DATA;

	// clear game data just like loading from the debug map load screen for mission saves
	if( gameInfo.saveGameInfo.saveFileType == SAVE_FILE_TYPE_MISSION )
	{

		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData( FALSE );

	}  // end if

	//
	// clear the save directory of any temporary "scratch pad" maps that were extracted
	// from any previously loaded save game files
	//
	TheGameStateMap->clearScratchPadMaps();

	// construct path to file
	AsciiString filepath = getFilePathInSaveDirectory(gameInfo.filename);

	// open the save file
	XferLoad xferLoad;
	xferLoad.open( filepath );

	// A match still recording ends here, before the engine reset closes its file with no length
	// written into the header.
	if( TheRecorder->getMode() == RECORDERMODETYPE_RECORD )
		TheRecorder->stopRecording();

	// clear out the game engine
	TheGameEngine->reset();

	// lock creation of new ghost objects
	TheGhostObjectManager->saveLockGhostObjects( TRUE );

	LatchRestore<Bool> inLoadGame(m_isInLoadGame, TRUE);

	// load the save data
	Bool error = FALSE;
	try
	{

		// load file
		xferSaveData( &xferLoad, SNAPSHOT_SAVELOAD );

	}  // end try
	catch( ... )
	{
		error = TRUE;
	}  // end catch

	// close the file
	xferLoad.close();

	// un-savelock the ghost objects
	TheGhostObjectManager->saveLockGhostObjects( FALSE );

	try
	{
		// do the post-process from a save game load
		gameStatePostProcessLoad();
	}
	catch (...)
	{
		error = TRUE;
	}

	// check for error
	if( error == TRUE )
	{
		// clear it out, again
		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData( FALSE );
		TheGameEngine->reset();

		// print error message to the user
		UnicodeString ufilepath;
		ufilepath.translate(filepath);

		UnicodeString msg;
		msg.format( TheGameText->fetch("GUI:ErrorLoadingGame"), ufilepath.str() );

		MessageBoxOk(TheGameText->fetch("GUI:Error"), msg, NULL);

		return SC_INVALID_DATA;	// you can't use a naked "throw" outside of a catch statement!

	}  // end if

	// A loaded match played at the default 30 whatever speed it was saved at: the reset during the
	// load puts the default back, and nothing kept the speed. A mission save starts its map afresh. A
	// save comes back at normal speed or in turbo mode, whichever is nearer what it was saved at, so
	// one from the old half-speed settings plays at normal.
	if( getSaveGameInfo()->saveFileType != SAVE_FILE_TYPE_MISSION && getSaveGameInfo()->framesPerSecond > 0 )
		TheGameEngine->setFramesPerSecondLimit( ClampPlayerGameSpeed( getSaveGameInfo()->framesPerSecond ) );

	//
	// when loading a mission save, we want to do as much normal loading stuff as we
	// can cause we don't have any real save game data to load other than the
	// game state map info and campaign manager stuff
	//
	if( getSaveGameInfo()->saveFileType == SAVE_FILE_TYPE_MISSION )
	{

		InitRandom(0);

		TheWritableGlobalData->m_pendingFile = getSaveGameInfo()->missionMapName;
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
		msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
		msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
		msg->appendIntegerArgument(GameEngine_matchFramesPerSecond(TheGlobalData->m_turboMode));

		// remove the mission save data, we've got all we need and have started the load
		SaveGameInfo *gameInfo = getSaveGameInfo();
		gameInfo->saveFileType = SAVE_FILE_TYPE_NORMAL;
		gameInfo->missionMapName.clear();

	}  // end if
		
	return SC_OK;

}  // end loadGame

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool GameState::refuseUnloadableSave( const SaveGameInfo &saveGameInfo )
{

	if( saveGameInfo.isLoadable() )
		return FALSE;

	DEBUG_LOG(( "GameState - refused a save with GameState version %d, this build loads %d\n",
							saveGameInfo.version, GAME_STATE_VERSION ));
	MessageBoxOk( TheGameText->fetch( "GUI:Error" ), TheGameText->fetch( "GUI:SaveFromOlderGame" ), NULL );
	return TRUE;

}  // end refuseUnloadableSave

//-------------------------------------------------------------------------------------------------
AsciiString GameState::getSaveDirectory() const
{
	AsciiString tmp = TheGlobalData->getPath_UserData();
	tmp.concat("Save\\");
	return tmp;
}

//-------------------------------------------------------------------------------------------------
AsciiString GameState::getFilePathInSaveDirectory(const AsciiString& leaf) const
{
	AsciiString tmp = getSaveDirectory();
	tmp.concat(leaf);
	return tmp;
}

//-------------------------------------------------------------------------------------------------
Bool GameState::isInSaveDirectory(const AsciiString& path) const
{
	return path.startsWithNoCase(getSaveDirectory());
}

// ------------------------------------------------------------------------------------------------
AsciiString GameState::getMapLeafName(const AsciiString& in) const
{
	const char* p = strrchr(in.str(), '\\');
	if (p)
	{
		//
		// p points to the last '\' (if found), however, if a '\' was found there better
		// be another character beyond it, otherwise the map filename would actually
		// be a *directory*  Just move to the first character beyond it so we are looking
		// at the name only
		//
		++p;
		DEBUG_ASSERTCRASH( p != NULL && *p != 0, ("GameState::xfer - Illegal map name encountered\n") );
		return p;
	}
	else
	{
		return in;
	}
}

// ------------------------------------------------------------------------------------------------
static const char* findLastBackslashInRangeInclusive(const char* start, const char* end)
{
	while (end >= start)
	{
		if (*end == '\\')
			return end;
		--end;
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------
static AsciiString getMapLeafAndDirName(const AsciiString& in)
{
	const char* start = in.str();
	const char* end = in.str() + in.getLength() - 1;
	const char* p = findLastBackslashInRangeInclusive(start, end);
	if (p)
	{
		const char* p2 = findLastBackslashInRangeInclusive(start, p-1);
		if (p2)
		{
			// we have something like:
			//	maps\foo\foo.map
			//	c:\mydocs\c&cdata\maps\foo\foo.map
			return p2 + 1;
		}
		else
		{
			// we have something like:
			//  save\foo.map
			return in;
		}
	}
	else
	{
		DEBUG_CRASH(("Illegal map-dir-name... should have at least one backslash"));
		return in;
	}
}

// ------------------------------------------------------------------------------------------------
static AsciiString removeExtension(const AsciiString& in)
{
	char buf[1024];
	strcpy(buf, in.str());
	char* p = strrchr(buf, '.');
	if (p)
	{
		*p = 0;
	}
	return AsciiString(buf);
}

// ------------------------------------------------------------------------------------------------
const char* PORTABLE_SAVE				= "Save\\";
const char* PORTABLE_MAPS				= "Maps\\";
const char* PORTABLE_USER_MAPS	= "UserData\\Maps\\";

// ------------------------------------------------------------------------------------------------
AsciiString GameState::realMapPathToPortableMapPath(const AsciiString& in) const
{
	AsciiString prefix;
	if (in.startsWithNoCase(getSaveDirectory()))
	{
		prefix = PORTABLE_SAVE;
		prefix.concat(getMapLeafName(in));
	}
	else if (in.startsWithNoCase(TheMapCache->getMapDir()))
	{
		prefix = PORTABLE_MAPS;
		prefix.concat(getMapLeafAndDirName(in));
	}
	else if (in.startsWithNoCase(TheMapCache->getUserMapDir()))
	{
		prefix = PORTABLE_USER_MAPS;
		prefix.concat(getMapLeafAndDirName(in));
	}
	else
	{
		DEBUG_CRASH(("Map file was not found in any of the expected directories; this is impossible"));
		//throw INI_INVALID_DATA;
		// uncaught exceptions crash us. better to just use a bad path.
		prefix = in;
	}
	prefix.toLower();
	return prefix;
}

// ------------------------------------------------------------------------------------------------
AsciiString GameState::portableMapPathToRealMapPath(const AsciiString& in) const
{
	AsciiString prefix;
	if (in.startsWithNoCase(PORTABLE_SAVE))
	{
		// the save dir ends with "\\"
		prefix = getSaveDirectory();
		prefix.concat(getMapLeafName(in));
	}
	else if (in.startsWithNoCase(PORTABLE_MAPS))
	{
		// the map dir DOES NOT end with "\\", must add it
		prefix = TheMapCache->getMapDir();
		prefix.concat("\\");
		prefix.concat(getMapLeafAndDirName(in));
	}
	else if (in.startsWithNoCase(PORTABLE_USER_MAPS))
	{
		// the map dir DOES NOT end with "\\", must add it
		prefix = TheMapCache->getUserMapDir();
		prefix.concat("\\");
		prefix.concat(getMapLeafAndDirName(in));
	}
	else
	{
		DEBUG_CRASH(("Map file was not found in any of the expected directories; this is impossible"));
		//throw INI_INVALID_DATA;
		// uncaught exceptions crash us. better to just use a bad path.
		prefix = in;
	}
	prefix.toLower();
	return prefix;
}

// ------------------------------------------------------------------------------------------------
/** Does the save game file exist */
// ------------------------------------------------------------------------------------------------
Bool GameState::doesSaveGameExist( AsciiString filename ) 
{

	// construct full path to file
	AsciiString filepath = getFilePathInSaveDirectory(filename);

	// open file
	XferLoad xfer;
	try
	{

		// try to open it
		xfer.open( filepath );

	}  // end try
	catch( ... )
	{

		// unable to open file, it must not be here
		return FALSE;

	}  // end catch
	
	// close the file, we don't want to to anything with it right now
	xfer.close();

	return TRUE;

}  // doesSaveGameExist

// ------------------------------------------------------------------------------------------------
/** Get save game info from the filename specified */
// ------------------------------------------------------------------------------------------------
void GameState::getSaveGameInfoFromFile( AsciiString filename, SaveGameInfo *saveGameInfo )
{
	AsciiString token;
	Int blockSize;
	Bool done = FALSE;
	SnapshotBlock *blockInfo;

	// sanity
	if( filename.isEmpty() == TRUE || saveGameInfo == NULL )
	{

		DEBUG_CRASH(( "GameState::getSaveGameInfoFromFile - Illegal parameters\n" ));
		return;

	}  // end if

	// open file for partial loading
	XferLoad xferLoad;
	xferLoad.open( filename );

	//
	// disable post processing cause we're not really doing a load of game data that
	// needs post processing and we don't want to keep track of any snapshots we loaded
	// 
	xferLoad.setOptions( XO_NO_POST_PROCESSING );

	// read all data blocks in the file
	while( done == FALSE )
	{

		// read next token
		xferLoad.xferAsciiString( &token );

		// check for end of file token
		if( token.compareNoCase( SAVE_FILE_EOF ) == 0 )
		{

			// we should never get here, if we did, we didn't find block of data we needed
			DEBUG_CRASH(( "GameState::getSaveGameInfoFromFile - Game info not found in file '%s'\n", filename.str() ));
			done = TRUE;

		}  // end if
		else
		{

			// find matching token in the save file lexicon
			blockInfo = findBlockInfoByToken( token, SNAPSHOT_SAVELOAD );
			if( blockInfo == NULL )
				throw SC_UNKNOWN_BLOCK;

			// read the data size of this block
			blockSize = xferLoad.beginBlock();

			// is this the block of game info data
			if( strcasecmp( token.str(), GAME_STATE_BLOCK_STRING ) == 0 )
			{
				GameState tempGameState;

				// parse this data
				try
				{

					// load data
					xferLoad.xferSnapshot( &tempGameState );

				}  // end try
				catch( ... )
				{

					DEBUG_CRASH(( "GameState::getSaveGameInfoFromFile - Error loading block '%s' in file '%s'\n",
												blockInfo->blockName.str(), filename.str() ));
					throw;

				}  // end catch

				// data was found, copy game state info over
				*saveGameInfo = *tempGameState.getSaveGameInfo();
				
				// we're all done with this file now
				done = TRUE;

			}  // end if
			else
			{

				// not a block we care about, just skip it
				xferLoad.skip( blockSize );

				// end of block
				xferLoad.endBlock();

			}  // end else

		}  // end else, valid data block token

	}  // end while, not done

	// close the file
	xferLoad.close();

}  // end getSaveGameInfoFromFile

// ------------------------------------------------------------------------------------------------
/** Create game info and add to available list */
// ------------------------------------------------------------------------------------------------
static void addGameToAvailableList( AsciiString filename, void *userData )
{
	AvailableGameInfo **listHead = (AvailableGameInfo **)userData;

	// sanity
	DEBUG_ASSERTCRASH( listHead != NULL, ("addGameToAvailableList - Illegal parameters\n") );
	DEBUG_ASSERTCRASH( filename.isEmpty() == FALSE, ("addGameToAvailableList - Illegal filename\n") );
 
	try {
	// get header info from this listbox.  The path, not the leaf: iterateSaveFiles used to change
	// into the save directory around this, and no longer does (C1).
	SaveGameInfo saveGameInfo;
	TheGameState->getSaveGameInfoFromFile( TheGameState->getFilePathInSaveDirectory( filename ), &saveGameInfo );

	// allocate new info 
	AvailableGameInfo *newInfo = new AvailableGameInfo;

	// assign data
	newInfo->prev = NULL;
	newInfo->next = NULL;
	newInfo->saveGameInfo = saveGameInfo;
	newInfo->filename = filename;

	// attach to list
	if( *listHead == NULL )
		*listHead = newInfo;
	else
	{
		AvailableGameInfo *curr, *prev;

		// insert this info so that the most recent games are always at the top of this list
		for( curr = *listHead; curr != NULL; curr = curr->next )
		{

			// save current as previous
			prev = curr;

			// check to see if curr is older than the new info, if so, put new info just ahead of curr
			if( newInfo->saveGameInfo.date.isNewerThan( &curr->saveGameInfo.date ) )
			{

				if( curr->prev )
					curr->prev->next = newInfo;
				else
					*listHead = newInfo;
				newInfo->prev = curr->prev;
				curr->prev = newInfo;
				newInfo->next = curr;

				break;

			}  // end if

		}  // end for

		// if not inserted, put at end
		if( curr == NULL )
		{

			prev->next = newInfo;
			newInfo->prev = prev;

		}  // end if

	}  // end else
	} catch(...) {
		// Do nothing - just return.
	}


}  // end addGameToAvailableList

// ------------------------------------------------------------------------------------------------
/** Populate the listbox passed in with a list of the save games present on the hard drive */
// ------------------------------------------------------------------------------------------------
void GameState::populateSaveGameListbox( GameWindow *listbox, SaveLoadLayoutType layoutType )
{
	Int index;

	// sanity
	if( listbox == NULL )
		return;

	// first clear all entries in the listbox
	GadgetListBoxReset( listbox );

	// setup the first entry of the listbox to be a new game when saving is allowed
	if( layoutType != SLLT_LOAD_ONLY )
	{
		UnicodeString newGameText = TheGameText->fetch( "GUI:NewSaveGame" );
		Color newGameColor = GameMakeColor( 200, 200, 255, 255 );

		index = GadgetListBoxAddEntryText( listbox, newGameText, newGameColor, -1 );
		GadgetListBoxSetItemData( listbox, NULL, index );
	
	}  // end if

	// clear the available games
	clearAvailableGames();

	// iterate all the save files in the directory and populate the listbox
	iterateSaveFiles( addGameToAvailableList, &m_availableGames );

	// add all games found to the list box
	AvailableGameInfo *info;
	SaveGameInfo *saveGameInfo;
	WallClockTime systemTime;
	UnsignedInt count = 0;
	for( info = m_availableGames; info; info = info->next, count++ )
	{

		// get save game info
		saveGameInfo = &info->saveGameInfo;

		// setup a system time structure given the data we saved in the file
		systemTime.wYear = saveGameInfo->date.year;
		systemTime.wMonth = saveGameInfo->date.month;
		systemTime.wDayOfWeek = saveGameInfo->date.dayOfWeek;
		systemTime.wDay = saveGameInfo->date.day;
		systemTime.wHour = saveGameInfo->date.hour;
		systemTime.wMinute = saveGameInfo->date.minute;
		systemTime.wSecond = saveGameInfo->date.second;
		systemTime.wMilliseconds = saveGameInfo->date.milliseconds;

		// setup date buffer for local region date format
		UnicodeString displayDateBuffer = getUnicodeDateBuffer(systemTime);

		// setup time buffer for local region time format
		UnicodeString displayTimeBuffer = getUnicodeTimeBuffer(systemTime);

		// description string
		UnicodeString displayLabel = saveGameInfo->description;
		if( displayLabel.isEmpty() == TRUE )
		{
			Bool exists = FALSE;
			
			displayLabel = TheGameText->fetch( saveGameInfo->mapLabel, &exists );
			if( exists == FALSE )
				displayLabel.format( u"%S", saveGameInfo->mapLabel.str() );

		}  // end if

		// pick color for text (we alternate it each game)
		Color color;
		if( saveGameInfo->saveFileType == SAVE_FILE_TYPE_MISSION )
			color = GameMakeColor( 200, 255, 200, 255 );
		else if( count & 0x1 )
			color = GameMakeColor( 255, 255, 255, 255 );
		else
			color = GameMakeColor( 170, 170, 235, 255 );

		// add string to listbox
		index = GadgetListBoxAddEntryText( listbox, displayLabel, color, -1, 0 );
		GadgetListBoxAddEntryText( listbox, displayTimeBuffer, color, index, 1 );
		GadgetListBoxAddEntryText( listbox, displayDateBuffer, color, index, 2 );

		// add this available game info in the user data pointer of that listbox item
		GadgetListBoxSetItemData( listbox, info, index );

	}  // end for, info

	// select the top "new game" entry
	GadgetListBoxSetSelected( listbox, 0 );

}  // end pupulateSaveGameListbox

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS ////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** Iterate the save game files */
// ------------------------------------------------------------------------------------------------
void GameState::iterateSaveFiles( IterateSaveFileCallback callback, void *userData )
{

	// sanity
	if( callback == NULL )
		return;

	// every file in the save directory, listed there rather than by changing into it (C1); the
	// callbacks get the leaf, as before, and build the path with getFilePathInSaveDirectory
	std::vector< AsciiString > files;
	TheLocalFileSystem->getFilesInDirectory( getSaveDirectory(), AsciiString( "*" ), files );

	for( size_t i = 0; i < files.size(); ++i )
	{

		// see if there is a ".sav" at end of this filename
		const Char *c = strrchr( files[ i ].str(), '.' );
		if( c && strcasecmp( c, ".sav" ) == 0 )
			callback( files[ i ], userData );

	}  // end for

}  // end iterateSaveFiles

// ------------------------------------------------------------------------------------------------
/** Save game to xfer or load game using xfer */
// ------------------------------------------------------------------------------------------------
void GameState::friend_xferSaveDataForCRC( Xfer *xfer, SnapshotType which )
{
	DEBUG_LOG(("GameState::friend_xferSaveDataForCRC() - SnapshotType %d\n", which));
	SaveGameInfo *gameInfo = getSaveGameInfo();
	gameInfo->description.clear();
	gameInfo->saveFileType = SAVE_FILE_TYPE_NORMAL;
	gameInfo->missionMapName.clear();
	// cleared for the CRC's sake and put back after: a loaded save still needs to know the map it
	// was started on (SidesList::isForkAIMapPath), and the next save writes it out again
	const AsciiString pristine = gameInfo->pristineMapName;
	gameInfo->pristineMapName.clear();

	xferSaveData(xfer, which);
	gameInfo->pristineMapName = pristine;
}

// ------------------------------------------------------------------------------------------------
/** Save game to xfer or load game using xfer */
// ------------------------------------------------------------------------------------------------
void GameState::xferSaveData( Xfer *xfer, SnapshotType which )
{

	// sanity
	if( xfer == NULL )
		throw SC_INVALID_XFER;

	// save or load all blocks
	if( xfer->getXferMode() == XFER_SAVE )
	{
		DEBUG_LOG(("GameState::xferSaveData() - XFER_SAVE\n"));

		// save all blocks
		AsciiString blockName;
		SnapshotBlock *blockInfo;
		SnapshotBlockListIterator it;
		for( it = m_snapshotBlockList[which].begin(); it != m_snapshotBlockList[which].end(); ++it )
		{
		
			// get list data
			blockInfo = &(*it);

			// get block name
			blockName = blockInfo->blockName;

			DEBUG_LOG(("Looking at block '%s'\n", blockName.str()));

			//
			// for mission save files, we only save the game state block and campaign manager
			// because anything else is not needed.
			//
			if( getSaveGameInfo()->saveFileType != SAVE_FILE_TYPE_MISSION || 
					(blockName.compareNoCase( GAME_STATE_BLOCK_STRING ) == 0 ||
					 blockName.compareNoCase( CAMPAIGN_BLOCK_STRING ) == 0) )
			{

				// xfer block name
				xfer->xferAsciiString( &blockName );

				// xfer this block
				try
				{

					// begin new data block
					xfer->beginBlock();

					// xfer block data
					xfer->xferSnapshot( blockInfo->snapshot );

					// end this block
					xfer->endBlock();

				}  // end try
				catch( ... )
				{

					// the DEBUG_CRASH is compiled out of a release build; the module that threw logged itself
					DEBUG_LOG(( "GameState::xferSaveData - error saving block '%s' in file '%s'\n",
											blockName.str(), xfer->getIdentifier().str() ));
					DEBUG_CRASH(( "Error saving block '%s' in file '%s'\n",
												blockName.str(), xfer->getIdentifier().str() ));
					throw;

				}  // end catch

			}  // end if

		}  // end for, all snapshots

		// write an end of file token
		AsciiString eofToken = SAVE_FILE_EOF;
		xfer->xferAsciiString( &eofToken );
				
	}  // end if, save
	else
	{
		DEBUG_LOG(("GameState::xferSaveData() - not XFER_SAVE\n"));
		AsciiString token;
		Int blockSize;
		Bool done = FALSE;
		SnapshotBlock *blockInfo;

		// read all data blocks in the file
		while( done == FALSE )
		{

			// read next token
			xfer->xferAsciiString( &token );

			// check for end of file token
			if( token.compareNoCase( SAVE_FILE_EOF ) == 0 )
			{

				// all done
				done = TRUE;

			}  // end if
			else
			{

				// find matching token in the save file lexicon
				blockInfo = findBlockInfoByToken( token, which );
				if( blockInfo == NULL )
				{

					// log the block not found
					DEBUG_LOG(( "GameState::xferSaveData - Skipping unknown block '%s'\n", token.str() ));

					//
					// block was not found, this could have been a block from an older file
					// format where the block was removed, skip the block data and try to continue
					//
					Int dataSize = xfer->beginBlock();
					xfer->skip( dataSize );

					// continue with while loop reading block tokens
					continue;

				}  // end if

				// DEBUG_CRASH below is compiled out of a release build, so this is the only word of which
				// block a load died in
				DEBUG_LOG(("Loading block '%s'\n", token.str()));
				try
				{

					// read block start
					blockSize = xfer->beginBlock();
				
					// parse this data
					xfer->xferSnapshot( blockInfo->snapshot );

					// read block end
					xfer->endBlock();

				}  // end try
				catch( ... )
				{

					// the DEBUG_CRASH is compiled out of a release build; the module that threw logged itself
					DEBUG_LOG(( "GameState::xferSaveData - error loading block '%s' in file '%s'\n",
											blockInfo->blockName.str(), xfer->getIdentifier().str() ));
					DEBUG_CRASH(( "Error loading block '%s' in file '%s'\n",
												blockInfo->blockName.str(), xfer->getIdentifier().str() ));
					throw;

				}  // end catch

			}  // end else, valid data block token

		}  // end while, not done

	}  // end else, load

}  // end xferSaveData

// ------------------------------------------------------------------------------------------------
/** Add a snapshot to the post process list for later */
// ------------------------------------------------------------------------------------------------
void GameState::addPostProcessSnapshot( Snapshot *snapshot )
{

	// sanity
	if( snapshot == NULL )
	{

		DEBUG_CRASH(( "GameState::addPostProcessSnapshot - invalid parameters\n" ));
		return;

	}  // end if

/*
	//
	// This is n^2 and gets real real, REAL slow on game maps.  jba.
	// Please keep this code around tho, it can be useful in debugging save games
	//

	// verify the snapshot isn't in the list already
	SnapshotListIterator it;
	for( it = m_snapshotPostProcessList.begin(); it != m_snapshotPostProcessList.end(); ++it )
	{

		if( (*it) == snapshot )
		{

			DEBUG_CRASH(( "GameState::addPostProcessSnapshot - snapshot is already in list!\n" ));
			return;

		}  // end if

	}  // end for, it
*/

	// add to the list
	m_snapshotPostProcessList.push_back( snapshot );

}  // end addPostProcessSnapshot

// ------------------------------------------------------------------------------------------------
/** Post process entry point after all game data has been xferd from disk */
// ------------------------------------------------------------------------------------------------
void GameState::gameStatePostProcessLoad( void )
{

	// post process each snapshot that registered with us
	SnapshotListIterator it;
	Snapshot *snapshot;
	for( it = m_snapshotPostProcessList.begin(); it != m_snapshotPostProcessList.end(); /*emtpy*/ )
	{

		// get snapshot
		snapshot = *it;

		// increment iterator
		++it;

		// do processing
		snapshot->loadPostProcess();

	}  // end for

	// clear the snapshot post process list as we are now done with it
	m_snapshotPostProcessList.clear();

	// evil... must ensure this is updated prior to the script engine running the first time.
	ThePartitionManager->update();
	ThePartitionManager->stopKeepingCellsFromSave();

	// Prepare immutable presentation data after every saved map input is final.
	if (TheTerrainVisual) TheTerrainVisual->prewarmMapRenderData();

	// last, so nothing the load did can draw from the stream after it is put back
	TheGameLogic->restoreLoadedRandomState();

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** Xfer method for the game state itself 
	* Version Info:
	* 1: Initial version 
	* 2: Added save file type and mission map name (regular save vs automatic mission save)
	* 3: Added the game speed
	* 4: Nothing new in the block; see GAME_STATE_VERSION */
// ------------------------------------------------------------------------------------------------
void GameState::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = GAME_STATE_VERSION;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// get structure for our current game info
	SaveGameInfo *saveGameInfo = getSaveGameInfo();
	saveGameInfo->version = version;

	// version 3: the game speed, read back by loadGame. It goes first because the rest of this
	// block ends in branches.
	if( xfer->getXferMode() == XFER_SAVE )
		saveGameInfo->framesPerSecond = TheGameEngine->getFramesPerSecondLimit();
	else
		saveGameInfo->framesPerSecond = 0;
	if( version >= 3 )
		xfer->xferInt( &saveGameInfo->framesPerSecond );

	// version 2
	if( version >= 2 )
	{

		// file type
		xfer->xferUser( &saveGameInfo->saveFileType, sizeof( SaveFileType ) );

		// mission map name
		xfer->xferAsciiString( &saveGameInfo->missionMapName );

	}  // end if

	// current system time
	WallClockTime systemTime;
	getLocalWallClock( &systemTime );

	// date and time
	saveGameInfo->date.year = systemTime.wYear;
	xfer->xferUnsignedShort( &saveGameInfo->date.year );
	saveGameInfo->date.month = systemTime.wMonth;
	xfer->xferUnsignedShort( &saveGameInfo->date.month );
	saveGameInfo->date.day = systemTime.wDay;
	xfer->xferUnsignedShort( &saveGameInfo->date.day );
	saveGameInfo->date.dayOfWeek = systemTime.wDayOfWeek;
	xfer->xferUnsignedShort( &saveGameInfo->date.dayOfWeek );
	saveGameInfo->date.hour = systemTime.wHour;
	xfer->xferUnsignedShort( &saveGameInfo->date.hour );
	saveGameInfo->date.minute = systemTime.wMinute;
	xfer->xferUnsignedShort( &saveGameInfo->date.minute );
	saveGameInfo->date.second = systemTime.wSecond;
	xfer->xferUnsignedShort( &saveGameInfo->date.second );
	saveGameInfo->date.milliseconds = systemTime.wMilliseconds;
	xfer->xferUnsignedShort( &saveGameInfo->date.milliseconds );

	// user description
	xfer->xferUnicodeString( &saveGameInfo->description );

	Bool exists = FALSE;
	Dict *dict = MapObject::getWorldDict();
	if( dict )
		saveGameInfo->mapLabel = dict->getAsciiString( TheKey_mapName, &exists );

	// if no label was found, we'll use the map name (just filename, no directory info)
	if( exists == FALSE || saveGameInfo->mapLabel == AsciiString::TheEmptyString )
	{
		// either separator can end the directory part: a map named on the command line may use '/'
		const char *mapName = TheGlobalData->m_mapName.str();
		const char *back = strrchr( mapName, '\\' );
		const char *forward = strrchr( mapName, '/' );
		const char *p = back;
		if( forward != NULL && (p == NULL || forward > p) )
			p = forward;
		if( p == NULL )
			saveGameInfo->mapLabel = TheGlobalData->m_mapName;
		else
			saveGameInfo->mapLabel.set( p + 1 );  // skip the separator we're on

	}  // end if

	// xfer map label
	xfer->xferAsciiString( &saveGameInfo->mapLabel );

	// campaign info
	Campaign *campaign = TheCampaignManager->getCurrentCampaign();
	if( campaign )
	{

		// campaign side
		saveGameInfo->campaignSide = campaign->m_name;
		xfer->xferAsciiString( &saveGameInfo->campaignSide );

		// campaign mission number
		saveGameInfo->missionNumber = TheCampaignManager->getCurrentMissionNumber();
		xfer->xferInt( &saveGameInfo->missionNumber );

		
	}  // end if
	else
	{

		// write empty campaign side
		saveGameInfo->campaignSide = AsciiString::TheEmptyString;
		xfer->xferAsciiString( &saveGameInfo->campaignSide );

		// invalid mission number
		saveGameInfo->missionNumber = CampaignManager::INVALID_MISSION_NUMBER;
		xfer->xferInt( &saveGameInfo->missionNumber );

	}  // end else

}  // end xfer
