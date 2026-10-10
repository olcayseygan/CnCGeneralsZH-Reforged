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

// GameEngine.cpp /////////////////////////////////////////////////////////////////////////////////
// Implementation of the Game Engine singleton
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "zhio.h"
#include "Common/MessageBoxFlags.h"	// MessageBoxWrapper and its flags
#include "Platform/SleepMilliseconds.h"
#if !defined(_WIN32)
#include <unistd.h>		// getpid, for the model-checksum cache's scratch file
#endif
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"

#include "Common/ActionManager.h"
#include "Common/AudioAffect.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/crc.h"
#include "Common/Radar.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/CDManager.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/JobSystem.h"
#include "GameLogic/TerrainLogic.h"		// -autocamera needs the map extent and the ground height
#include "Common/RandomValue.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModuleFactory.h"
#include "Common/Debug.h"
#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/Science.h"
#include "Common/FunctionLexicon.h"
#include "Common/CommandLine.h"
#include "Common/DamageFX.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/Upgrade.h"
#include "Common/UserPreferences.h"
#include "Common/SkirmishPreferences.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/GameLOD.h"
#include "Common/Registry.h"
#include "Common/GameCommon.h"	// FOR THE ALLOW_DEBUG_CHEATS_IN_RELEASE #define

#include "GameLogic/Armor.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"		// the headless run summary reports the match's pathfinder totals
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "Common/ControlServer.h"		// -control: the WebSocket that drives the game from outside
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScenarioDrill.h"		// -scenario says at the end how much of the file ran
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"

#include "GameClient/ChromaKeyboard.h"
#include "GameClient/CinemaDirector.h"
#include "GameClient/Display.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/ControlBar.h"		// -uidrill works the command bar the way a player does
#include "GameClient/InGameUI.h"		// -resdrill selects a building before it changes the mode
#include "GameClient/ObserverCamera.h"	// -directorscout counts the fights of a headless run
#include "GameLogic/Object.h"

#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"

#include "Common/version.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------

#ifdef DEBUG_CRC
class DeepCRCSanityCheck : public SubsystemInterface
{
public:
	DeepCRCSanityCheck() {}
	virtual ~DeepCRCSanityCheck() {}

	virtual void init(void) {}
	virtual void reset(void);
	virtual void update(void) {}

protected:
};

DeepCRCSanityCheck *TheDeepCRCSanityCheck = NULL;

void DeepCRCSanityCheck::reset(void)
{
	static Int timesThrough = 0;
	static UnsignedInt lastCRC = 0;

	// EA wrote the whole deep CRC out to CRCAfter<n>Maps.dat in the user data folder here, on every
	// player's machine, for a comparison only this function makes and only against the number.  A
	// folder that refused the write threw out of the shell map's first frame as "Uncaught exception
	// in WinMain"; the number alone is all the check needs.
	UnsignedInt thisCRC = TheGameLogic->getCRC( CRC_RECALC );

	DEBUG_LOG(("DeepCRCSanityCheck: CRC is %X\n", thisCRC));
	DEBUG_ASSERTCRASH(timesThrough == 0 || thisCRC == lastCRC,
		("CRC after reset did not match beginning CRC!\nNetwork games won't work after this.\nOld: 0x%8.8X, New: 0x%8.8X",
		lastCRC, thisCRC));
	lastCRC = thisCRC;

	timesThrough++;
}
#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
/// The GameEngine singleton instance
GameEngine *TheGameEngine = NULL;

//-------------------------------------------------------------------------------------------------
SubsystemInterfaceList* TheSubsystemList = NULL;

//-------------------------------------------------------------------------------------------------
template<class SUBSYSTEM>
void initSubsystem(SUBSYSTEM*& sysref, AsciiString name, SUBSYSTEM* sys, Xfer *pXfer,  const char* path1 = NULL, 
									 const char* path2 = NULL, const char* dirpath = NULL)
{
	sysref = sys;
	TheSubsystemList->initSubsystem(sys, path1, path2, dirpath, pXfer, name);
}

//-------------------------------------------------------------------------------------------------
#if defined(_WIN32)
extern HINSTANCE ApplicationHInstance;  ///< our application instance
extern CComModule _Module;		// ATL's module, for the embedded browser's COM; nothing off Windows uses COM
#endif

//-------------------------------------------------------------------------------------------------
static void updateTGAtoDDS();

//-------------------------------------------------------------------------------------------------
/** A file logic reads that no subsystem loads through the INI checksum, as it resolved on this
	* machine, byte for byte. */
//-------------------------------------------------------------------------------------------------
static void checksumFileContents( XferCRC &xferCRC, const char *path )
{
	File *file = TheFileSystem->openFile( path, File::READ );
	Int fileSize = file->size();
	char *contents = file->readEntireAndClose();
	xferCRC.xferUser( contents, fileSize );
	delete [] contents;
}

//-------------------------------------------------------------------------------------------------
/** What a model's bytes hashed to, and the size and write time of the file they were read out of:
	* the archive the model resolved to, or the loose model itself. */
//-------------------------------------------------------------------------------------------------
struct ModelChecksum
{
	FileInfo source;
	UnsignedInt crc;
};
typedef std::map<std::string, ModelChecksum> ModelChecksumMap;	///< keyed "source|model"

static const char *MODEL_CHECKSUM_CACHE = "ModelChecksums.txt";
static const Int MODEL_CHECKSUM_LINE_LENGTH = 1024;

static AsciiString modelChecksumCachePath( void )
{
	AsciiString path = TheGlobalData->getPath_UserData();
	path.concat( MODEL_CHECKSUM_CACHE );
	return path;
}

static ModelChecksumMap readModelChecksumCache( void )
{
	ModelChecksumMap cache;
	FILE *cacheFile = zh_fopen( modelChecksumCachePath().str(), "r" );
	if (cacheFile == NULL)
		return cache;		// the first start on this machine, or the file was deleted

	char line[MODEL_CHECKSUM_LINE_LENGTH];
	while (fgets( line, sizeof( line ), cacheFile ))
	{
		ModelChecksum entry;
		Int keyOffset = 0;
		if (sscanf( line, "%x %d %d %d %d %n", &entry.crc, &entry.source.sizeHigh, &entry.source.sizeLow,
				&entry.source.timestampHigh, &entry.source.timestampLow, &keyOffset ) != 5)
			continue;		// a torn line from a copy killed mid-write reads again from the model

		std::string key( line + keyOffset );
		key.erase( key.find_last_not_of( "\r\n" ) + 1 );
		cache[key] = entry;
	}
	fclose( cacheFile );
	return cache;
}

/** Written beside and moved over the old one, so a second copy starting at the same moment reads
	* one whole file or the other and never half of each. */
static void writeModelChecksumCache( const ModelChecksumMap &cache )
{
	AsciiString finalPath = modelChecksumCachePath();
	AsciiString scratchPath;
#if defined(_WIN32)
	scratchPath.format( "%s.%u", finalPath.str(), (UnsignedInt)GetCurrentProcessId() );
#else
	scratchPath.format( "%s.%u", finalPath.str(), (UnsignedInt)getpid() );
#endif

	FILE *cacheFile = zh_fopen( scratchPath.str(), "w" );
	if (cacheFile == NULL)
		return;		// a read-only user folder costs the next start a full read, nothing else

	for( ModelChecksumMap::const_iterator it = cache.begin(); it != cache.end(); ++it )
	{
		const ModelChecksum &entry = it->second;
		fprintf( cacheFile, "%08X %d %d %d %d %s\n", entry.crc, entry.source.sizeHigh, entry.source.sizeLow,
			entry.source.timestampHigh, entry.source.timestampLow, it->first.c_str() );
	}
	fclose( cacheFile );
	if (!TheLocalFileSystem->moveFileReplacing( scratchPath.str(), finalPath.str() ))
		TheLocalFileSystem->deleteFile( scratchPath.str() );		// another copy holds it open; its own write will do
}

static Bool isSameFile( const FileInfo &left, const FileInfo &right )
{
	return left.sizeHigh == right.sizeHigh && left.sizeLow == right.sizeLow
		&& left.timestampHigh == right.timestampHigh && left.timestampLow == right.timestampLow;
}

//-------------------------------------------------------------------------------------------------
/** Every model's bones place fire points, turret pivots and dock positions in logic, so a model
	* that differs between two machines splits their match.  Each model's bytes go into the checksum
	* as it resolved: a loose model beats every archive, and a later patch archive beats an earlier
	* one.  Reading all of them is about 375 MB, so what a model hashed to is kept in the user folder
	* against the size and write time of the file it came from, and read again only when that file
	* changes. */
//-------------------------------------------------------------------------------------------------
static void checksumModels( XferCRC &xferCRC )
{
	FilenameList models;
	TheFileSystem->getFileListInDirectory( AsciiString( "Art\\W3D\\" ), AsciiString( "*.w3d" ), models, TRUE );

	const ModelChecksumMap cache = readModelChecksumCache();
	ModelChecksumMap current;
	Int modelsRead = 0;

	for( FilenameListIter it = models.begin(); it != models.end(); ++it )
	{
		AsciiString model = *it;
		model.toLower();

		AsciiString source = TheLocalFileSystem->doesFileExist( model.str() )
			? model : TheArchiveFileSystem->getArchiveFilenameForFile( model );

		ModelChecksum entry;
		const Bool sourceKnown = TheLocalFileSystem->getFileInfo( source, &entry.source );

		std::string key( source.str() );
		key.append( "|" );
		key.append( model.str() );

		ModelChecksumMap::const_iterator cached = cache.find( key );
		if (sourceKnown && cached != cache.end() && isSameFile( cached->second.source, entry.source ))
		{
			entry.crc = cached->second.crc;
		}
		else
		{
			File *file = TheFileSystem->openFile( model.str(), File::READ );
			Int fileSize = file->size();
			char *contents = file->readEntireAndClose();
			CRC modelCRC;
			modelCRC.computeCRC( contents, fileSize );
			delete [] contents;
			entry.crc = modelCRC.get();
			++modelsRead;
		}
		if (sourceKnown)
			current[key] = entry;

		xferCRC.xferUser( const_cast<char *>( model.str() ), model.getLength() );
		xferCRC.xferUnsignedInt( &entry.crc );
	}

	if (modelsRead > 0)
		writeModelChecksumCache( current );
	DEBUG_LOG(( "INI CRC covers %d models, %d of them read this start\n", (Int)models.size(), modelsRead ));
}

Int GameEngine::getFramesPerSecondLimit( void )
{
	return m_maxFPS;
}

//-------------------------------------------------------------------------------------------------
static const UnsignedInt LOGIC_RATE_SAMPLE_MS = 500;

/** A half-second sample is a count of whole frames, so a steady 30Hz reads 28 one sample and 32
		the next, and every countdown dividing by it read 20s, 21s, 19s. The average over the last few
		seconds takes that out, and the shown number only moves once the average has left it by a full
		step, so a rate sitting on x.5 does not flip between two values while a real drop still gets
		through in a second or two.

		Until the window has filled, the average is the plain mean of what it has: the first samples
		after a load run far over the real rate, and letting the first one stand for the whole window
		kept that number on screen for the opening seconds of every match. */
void RateReading::add( Real sample )
{
	const Int WINDOW = 8;								// samples that count once there are enough: about four seconds
	const Real MIN_STEP = 0.75f;				// over half, or an average settling on 20 from above sticks at 21
	const Real STEP_FRACTION = 0.02f;		// the step at high rates, where one frame is less than 1%

	if( samples < WINDOW )
		++samples;
	average += ( sample - average ) / samples;
	const Real step = max( MIN_STEP, shown * STEP_FRACTION );
	if( samples == 1 || fabs( average - shown ) >= step )
		shown = REAL_TO_INT( average + 0.5f );
}

/** A build time on screen is a promise about how long you will wait, and the limit is only the
		rate the logic is asked for.  A match that has sunk to 10 frames a second takes three times as
		long over a 300 frame barracks as the 30 it was asked for, so the countdown says 30s, not 10s.
		Client only: the wall clock is in it, and nothing in GameLogic may decide by it. */
void GameEngine::sampleLogicRate( void )
{
	if( TheGameLogic == NULL || TheGameLogic->isGamePaused() )
	{
		m_logicRateSampleMs = 0;
		return;
	}

	const UnsignedInt nowMs = Clock_Milliseconds();
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( m_logicRateSampleMs == 0 || frame < m_logicRateSampleFrame )
	{
		m_logicRateSampleMs = nowMs;
		m_logicRateSampleFrame = frame;
		return;
	}

	const UnsignedInt elapsedMs = nowMs - m_logicRateSampleMs;
	if( elapsedMs < LOGIC_RATE_SAMPLE_MS )
		return;

	m_measuredLogicFps.add( (frame - m_logicRateSampleFrame) * 1000.0f / elapsedMs );
	m_logicRateSampleMs = nowMs;
	m_logicRateSampleFrame = frame;
}

Int GameEngine::getLogicFramesPerSecond( void )
{
	// a stalled network game has no rate at all, and no countdown can say how long that lasts
	if( m_measuredLogicFps.shown > 0 )
		return m_measuredLogicFps.shown;
	return m_maxFPS > 0 ? m_maxFPS : LOGICFRAMES_PER_SECOND;
}

//-------------------------------------------------------------------------------------------------
GameEngine::GameEngine( void )
{
	// Set the time slice size to 1 ms.
	Clock_Begin_Fine_Resolution();

	// initialize to non garbage values
	m_maxFPS = 0;
	m_logicRateSampleMs = 0;
	m_logicRateSampleFrame = 0;
	m_quitting = FALSE;
	m_isActive = FALSE;

#if defined(_WIN32)
	_Module.Init(NULL, ApplicationHInstance);
#endif
}

//-------------------------------------------------------------------------------------------------
GameEngine::~GameEngine()
{
	//extern std::vector<std::string>	preloadTextureNamesGlobalHack;
	//preloadTextureNamesGlobalHack.clear();

	// Debug.cpp puts an assertion up as a modal dialog only while the game is windowed, because a
	// dialog over a fullscreen device deadlocks. Teardown is neither: the device is on its way out
	// under us, and a dialog raised here hangs the process on exit with nothing on screen. Say we
	// are not windowed and every assertion from here on goes to the log instead.
	extern bool DX8Wrapper_IsWindowed;
	DX8Wrapper_IsWindowed = false;

	// close the control socket before anything it can reach is torn down
	ControlServer_shutdown();

	shutdownChromaKeyboard();

	delete TheMapCache;
	TheMapCache = NULL;

//	delete TheShell;
//	TheShell = NULL;

	TheGameResultsQueue->endThreads();

	// Tear the game world down while every subsystem can still see every other one.
	// shutdownAll deletes in reverse registration order, which kills ThePlayerList and
	// TheRadar before TheTeamFactory and TheGameLogic - and Player::~Player NULLs the
	// owning player of each team prototype on its way out, so TeamFactory's own teardown
	// then walked dead pointers and faulted twice on every exit.  Each fault cost a full
	// dbghelp symbolization of an 80MB pdb: that was the several-second stall on quit.
	// resetAll() is the same call made between games, and PlayerList::reset() clears the
	// teams before the players, so afterwards shutdownAll has an empty world to free.
	TheSubsystemList->resetAll();

	TheSubsystemList->shutdownAll();
	delete TheSubsystemList;
	TheSubsystemList = NULL;

	delete TheNetwork;
	TheNetwork = NULL;

	delete TheCommandList;
	TheCommandList = NULL;

	delete TheNameKeyGenerator;
	TheNameKeyGenerator = NULL;

	delete TheFileSystem;
	TheFileSystem = NULL;

	if (TheGameLODManager)
		delete TheGameLODManager;

	Drawable::killStaticImages();

#if defined(_WIN32)
	_Module.Term();
#endif

	/* After everything that could still fork.  parallel_for never returns with work in flight, so
		 there is nothing to drain here - but a worker parked on the semaphore still has to be told
		 to leave, or the process waits on it at exit. */
	JobSystem::shutdown();

#ifdef PERF_TIMERS
	PerfGather::termPerfDump();
#endif

	// Restore the previous time slice for Windows.
	Clock_End_Fine_Resolution();
}

void GameEngine::setFramesPerSecondLimit( Int fps )
{
	DEBUG_LOG(("GameEngine::setFramesPerSecondLimit() - setting max fps to %d (TheGlobalData->m_useFpsLimit == %d)\n", fps, TheGlobalData->m_useFpsLimit));
	m_maxFPS = fps;

	// the speed keys change the rate on purpose; the countdowns start again from the next sample
	// rather than taking the average's seconds to walk over
	m_measuredLogicFps.restart();
}

/* -replay <file>: the name the command line asked for, opened after init()'s resetAll().  See
	 the .rep branch in init() for why it cannot be opened where it is parsed. */
static AsciiString thePendingReplayFile;

/* -loadsave <file>: same deferral, same reason. A save game rebuilds the whole world, and doing
	 that before init()'s resetAll() would have resetAll tear it straight back down again. */
static AsciiString thePendingSaveFile;

/** Open the save game -loadsave named, once every subsystem has been reset.
	*
	* This is doLoadGame() in PopupSaveLoad.cpp without the menu: prepare a single player game, load,
	* and fall back to the shell if the load fails. */
static void startPendingSaveGame( void )
{
	AvailableGameInfo gameInfo;
	gameInfo.next = NULL;
	gameInfo.prev = NULL;
	// A leaf name, not a path: everything downstream calls getFilePathInSaveDirectory on it, so a
	// full path here gets the save directory glued in front of it a second time and loadGame then
	// answers SC_FILE_NOT_FOUND for a file that is plainly there.
	gameInfo.filename = thePendingSaveFile;

	/* Ask first. Neither getSaveGameInfoFromFile nor loadGame survives a name that is not there -
		 the first reads out of an xfer that never opened - and a file name typed on a command line is
		 exactly the sort of thing that is not there. Without this the switch faulted on a typo. */
	if (!TheGameState->doesSaveGameExist( thePendingSaveFile ))
	{
		DEBUG_LOG(("-loadsave: '%s' does not exist\n", gameInfo.filename.str()));
		// A run driven entirely from the command line has nobody at the keyboard, so dropping it
		// into the main menu means a process that never ends. Say what went wrong and stop.
		if (TheGlobalData->m_headless)
			TheGameEngine->setQuitting( TRUE );
		else
			TheWritableGlobalData->m_shellMapOn = TRUE;
		return;
	}

	// getSaveGameInfoFromFile opens the name it is handed as-is, so give it the path (the menu's
	// iterateSaveFiles callback does the same).
	TheGameState->getSaveGameInfoFromFile(
		TheGameState->getFilePathInSaveDirectory( thePendingSaveFile ), &gameInfo.saveGameInfo );

	TheGameLogic->prepareNewGame( GAME_SINGLE_PLAYER, DIFFICULTY_NORMAL, 0 );

	if (TheGameState->loadGame( gameInfo ) != SC_OK)
	{
		DEBUG_LOG(("-loadsave: '%s' could not be loaded\n", gameInfo.filename.str()));
		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData( FALSE );
		if (TheGlobalData->m_headless)
			TheGameEngine->setQuitting( TRUE );
		else
			TheWritableGlobalData->m_shellMapOn = TRUE;
	}
}

/** -----------------------------------------------------------------------------------------------
 * -autoskirmish <n>: build a skirmish slot list from the command line and launch it without going
 * through the menus.  This is reallyDoStart() in SkirmishGameOptionsMenu.cpp minus the GUI: the
 * slots only have to say who is occupied and who is an AI, because GameLogic::startNewGame resolves
 * a -1 faction, colour and start position itself (populateRandomSideAndColor,
 * populateRandomStartPosition).  Meant for unattended runs - eight AI players fighting at whatever
 * frame rate the machine gives, with the local slot watching.
 */
static void startAutoSkirmish( Int numPlayersWanted )
{
	AsciiString mapName = TheGlobalData->m_mapName;
	if (mapName.isEmpty())
	{
		DEBUG_LOG(("-autoskirmish: no -map was given\n"));
		return;
	}

	const MapMetaData *md = TheMapCache->findMap( mapName );
	if (md == NULL)
	{
		DEBUG_LOG(("-autoskirmish: '%s' is not in the map cache\n", mapName.str()));
		return;
	}
	if (!md->m_isMultiplayer)
	{
		DEBUG_LOG(("-autoskirmish: '%s' is not a multiplayer map\n", mapName.str()));
		return;
	}

	Int numPlayers = numPlayersWanted;
	if (numPlayers > md->m_numPlayers)
	{
		DEBUG_LOG(("-autoskirmish: '%s' holds %d players, not %d\n", mapName.str(), md->m_numPlayers, numPlayers));
		numPlayers = md->m_numPlayers;
	}

	if (TheSkirmishGameInfo == NULL)
	{
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	}
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->enterGame();

	/* Watching costs no seat.  GameLogic::startNewGame always adds a "ReplayObserver" side after
		 the slots, and PlayerList::newGame makes the first human side the local player - so a slot
		 list with nothing but AI in it leaves that observer holding the camera, exactly the way a
		 replay does.  Spending a slot on the observer instead would cost a bot, because MAX_SLOTS is
		 8 and that is also the most start positions a map has. */
	/* -side names a faction for a slot instead of letting populateRandomSideAndColor draw one.
		 Resolve every name before a slot list is built, and refuse the whole run on one nobody
		 recognises: falling back to a random draw would still produce a match, still produce numbers,
		 and quietly answer a different question than the one that was asked. */
	Int sideTemplate[ MAX_SLOTS ];
	for( Int s = 0; s < MAX_SLOTS; s++ )
	{
		sideTemplate[ s ] = PLAYERTEMPLATE_RANDOM;
		const AsciiString &sideName = TheGlobalData->m_autoSkirmishSide[ s ];
		if (sideName.isEmpty())
			continue;

		const Int templateIndex = ThePlayerTemplateStore->getTemplateNumByName( sideName );
		if (templateIndex < 0)
		{
			DEBUG_LOG(("-side: slot %d asks for '%s', which is not a player template\n",
								 s, sideName.str()));
			return;
		}
		sideTemplate[ s ] = templateIndex;
	}

	/* -takeover leaves the opponents' seats occupied but driverless. SLOT_TAKEOVER is written into
		 the slot list as an opponent, but startNewGame marks it playerIsHuman, so setPlayerType never
		 news an AIPlayer and nothing on that side thinks. A measurement wants that: an AI building and
		 attacking costs more of the frame than whatever is under test, and a different amount every
		 run. */
	const Bool takeover = TheGlobalData->m_autoSkirmishTakeover;

	/* A scenario names map coordinates, so it needs to know which corner each slot starts in.
		 populateRandomStartPosition leaves alone any slot that already has a position in range; the
		 draw stays random without -scenario so that every existing batch plays exactly what it
		 played before. */
	const Bool fixedStartPositions = !TheGlobalData->m_scenarioFile.isEmpty();

	const Bool observing = TheGlobalData->m_autoSkirmishObserver;
	// the name the skirmish menu would have put in the seat: the one saved there, else the machine's
	SkirmishPreferences preferences;
	const UnicodeString localName = preferences.getUserName();
	for( Int i = 0; i < numPlayers; i++ )
	{
		GameSlot slot;
		if (i == 0 && !observing)
		{
			slot.setState( SLOT_PLAYER, localName );
			slot.setName( localName );
		}
		else if (takeover && (TheGlobalData->m_autoSkirmishTakeoverSlot < 0 || TheGlobalData->m_autoSkirmishTakeoverSlot == i))
		{
			slot.setState( SLOT_TAKEOVER );
		}
		else
		{
			// -aidiff2 gives the odd slots a different rung, so a batch can play one against another
			Int state = TheGlobalData->m_autoSkirmishAIState;
			if ((i & 1) && TheGlobalData->m_autoSkirmishAIStateOdd != 0)
				state = TheGlobalData->m_autoSkirmishAIStateOdd;
			slot.setState( (SlotState)state );
			if (TheGlobalData->m_autoSkirmishSeatName[ i ].isNotEmpty())
			{
				UnicodeString seatName;
				seatName.translate( TheGlobalData->m_autoSkirmishSeatName[ i ] );
				slot.setName( seatName );
			}
		}
		slot.setPlayerTemplate( sideTemplate[ i ] );
		slot.setColor( -1 );			// -1 is "random" to populateRandomSideAndColor
		slot.setStartPos( fixedStartPositions ? i : -1 );		// and -1 to populateRandomStartPosition
		/* -teams splits the lobby into allied blocks: with eight players and two teams the first
			 four are team 0 and the rest team 1, the way the lobby numbers them. GameLogic's own
			 alliance pass reads the slot's team number and does the rest. Without it every slot is
			 -1, which is "no team", and everybody fights everybody. */
		Int teamNumber = -1;
		const Int teams = TheGlobalData->m_autoSkirmishTeams;
		if (teams > 1 && numPlayers >= teams)
		{
			const Int perTeam = (numPlayers + teams - 1) / teams;
			teamNumber = i / perTeam;
			if (teamNumber >= teams)
				teamNumber = teams - 1;		// an uneven split puts the remainder on the last team
		}
		if (TheGlobalData->m_autoSkirmishTeam[ i ] != AUTO_SKIRMISH_TEAM_UNSET)
			teamNumber = TheGlobalData->m_autoSkirmishTeam[ i ];
		slot.setTeamNumber( teamNumber );
		TheSkirmishGameInfo->setSlot( i, slot );
	}
	TheSkirmishGameInfo->setLocalIP( TheSkirmishGameInfo->getSlot(0)->getIP() );
	TheSkirmishGameInfo->setMap( mapName );
	// set on the game rather than on GameLogic, so the replay's header carries it like a lobby's would
	TheSkirmishGameInfo->setIncomeSharing( TheGlobalData->m_incomeSharing );
	TheSkirmishGameInfo->setTechRespawn( TheGlobalData->m_techRespawn );
	TheSkirmishGameInfo->setSupplyPileLimit( TheGlobalData->m_supplyPileLimit );
	TheSkirmishGameInfo->setSuperweaponRestriction( (UnsignedShort)TheGlobalData->m_superweapons );
	if (TheGlobalData->m_startingCash > 0)
	{
		Money cash;
		cash.deposit( (UnsignedInt)TheGlobalData->m_startingCash, FALSE );
		TheSkirmishGameInfo->setStartingCash( cash );
	}

	/* -seed makes the whole run repeatable: the seed drives the factions, the colours, the start
		 positions and every logic random draw after them, so the same command line replays the same
		 match. */
	const Int seed = (TheGlobalData->m_fixedSeed >= 0) ? TheGlobalData->m_fixedSeed : Clock_Milliseconds_Coarse();
	TheSkirmishGameInfo->setSeed( seed );
	TheSkirmishGameInfo->startGame( 0 );

	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheWritableGlobalData->m_playIntro = FALSE;

	/* The menu passes the game speed slider's position here, so pass the same thing and clamp it
		 the same way reallyDoStart() does: MSG_NEW_GAME rejects anything outside 1..1000 by falling
		 back to m_framesPerSecondLimit *unclamped*, which is how a stray -fps would still get through.
		 -noFPSLimit deliberately does not appear here - it uncaps the renderer, not the simulation. */
	Int maxFPS = TheGlobalData->m_framesPerSecondLimit;
	if (maxFPS < 15)
		maxFPS = DEFAULT_MAX_FPS;
	if (maxFPS > 1000)
		maxFPS = 1000;

	InitRandom( seed );
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SKIRMISH );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );
	msg->appendIntegerArgument( 0 );
	msg->appendIntegerArgument( maxFPS );

	DEBUG_LOG(("-autoskirmish: %d slots on '%s', seed %d, starting cash %u, up to %d fps, %s%s%s\n",
		numPlayers, mapName.str(), seed, TheSkirmishGameInfo->getStartingCash().countMoney(), maxFPS,
		observing ? "every slot AI, watching from the free camera" : "slot 0 is the local player",
		takeover ? ", seats driverless" : "",
		fixedStartPositions ? ", start positions fixed to slot order" : ""));
}

/** -----------------------------------------------------------------------------------------------
 * The same door, for -control's "skirmish" command.  The player count is a parameter rather than
 * the global that -autoskirmish sets, because that global is also what marks a run unattended: a
 * match somebody is driving down a socket has to stay up when it ends, not write its numbers out
 * and quit.
 */
void GameEngine_startSkirmish( Int numPlayers )
{
	startAutoSkirmish( numPlayers );
}

/** -----------------------------------------------------------------------------------------------
 * -netgame <ip>[,<ip>...] -netslot <n>: play a LAN game against those addresses, with the slot
 * list, the map and the seed coming from the command line instead of from the lobby.  Same job as
 * startAutoSkirmish() one level up, and the same restriction: the map has to be a multiplayer map
 * that is already in the cache on every machine, because nothing is transferred.
 *
 * Every copy has to be given the same list in the same order and its own -netslot, and the seed
 * has to match too - the factions, the colours and the start positions are all drawn from it, and
 * two machines that disagree about them desync on the first frame.
 */
static void startAutoNetGame( void )
{
	AsciiString mapName = TheGlobalData->m_mapName;
	if (mapName.isEmpty())
	{
		DEBUG_LOG(("-netgame: no -map was given\n"));
		return;
	}

	const MapMetaData *md = TheMapCache->findMap( mapName );
	if (md == NULL)
	{
		DEBUG_LOG(("-netgame: '%s' is not in the map cache\n", mapName.str()));
		return;
	}
	if (!md->m_isMultiplayer)
	{
		DEBUG_LOG(("-netgame: '%s' is not a multiplayer map\n", mapName.str()));
		return;
	}

	UnsignedInt slotIPs[ MAX_SLOTS ];
	Int numSlots = ResolveHostList( TheGlobalData->m_netGameHosts, slotIPs, MAX_SLOTS );
	if (numSlots < 2)
	{
		DEBUG_LOG(("-netgame: '%s' is not a list of 2 to %d addresses\n",
			TheGlobalData->m_netGameHosts.str(), MAX_SLOTS));
		return;
	}

	const Int numSeats = numSlots + TheGlobalData->m_netGameAISlots;
	if (numSeats > md->m_numPlayers || numSeats > MAX_SLOTS)
	{
		DEBUG_LOG(("-netgame: '%s' holds %d players, not %d\n", mapName.str(), md->m_numPlayers, numSeats));
		return;
	}

	/* The seed is what the host would have picked and sent round, so it has to be given here - an
		 unseeded network game is one that disagrees with itself. */
	if (TheGlobalData->m_fixedSeed < 0)
	{
		DEBUG_LOG(("-netgame: needs a -seed, and the same one on every machine\n"));
		return;
	}

	if (TheLAN == NULL)
		TheLAN = NEW LANAPI();

	TheLAN->StartAutomatedGame( mapName, TheGlobalData->m_fixedSeed, slotIPs, numSlots,
		TheGlobalData->m_netGameLocalSlot );
	TheWritableGlobalData->m_netGameStarted = TRUE;
}

/** -----------------------------------------------------------------------------------------------
 * Initialize the game engine by initializing the GameLogic and GameClient.
 */
void GameEngine::init( void ) {} /// @todo: I changed this to take argc & argv so we can parse those after the GDF is loaded.  We need to rethink this immediately as it is a nasty hack
void GameEngine::init( int argc, char *argv[] )
{
	try {
		//create an INI object to use for loading stuff
		INI ini;

#ifdef DEBUG_LOGGING
		if (TheVersion)
		{
			DEBUG_LOG(("================================================================================\n"));
	#if defined _DEBUG
			const char *buildType = "Debug";
	#elif defined _INTERNAL
			const char *buildType = "Internal";
	#else
			const char *buildType = "Release";
	#endif
			DEBUG_LOG(("Generals version %s (%s)\n", TheVersion->getAsciiVersion().str(), buildType));
			DEBUG_LOG(("Build date: %s\n", TheVersion->getAsciiBuildTime().str()));
			DEBUG_LOG(("Build location: %s\n", TheVersion->getAsciiBuildLocation().str()));
			DEBUG_LOG(("Built by: %s\n", TheVersion->getAsciiBuildUser().str()));
			DEBUG_LOG(("================================================================================\n"));
		}
#endif

	#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)
		DEBUG_LOG(("Calculating CPU frequency for performance timers.\n"));
		InitPrecisionTimer();
	#endif
	#ifdef PERF_TIMERS
		PerfGather::initPerfDump("AAAPerfStats", PerfGather::PERF_NETTIME);
	#endif




	#ifdef DUMP_PERF_STATS////////////////////////////////////////////////////////////
	__int64 startTime64;//////////////////////////////////////////////////////////////
	__int64 endTime64,freq64;///////////////////////////////////////////////////////////
	GetPrecisionTimerTicksPerSec(&freq64);///////////////////////////////////////////////
	GetPrecisionTimer(&startTime64);////////////////////////////////////////////////////
  char Buf[256];//////////////////////////////////////////////////////////////////////
	#endif//////////////////////////////////////////////////////////////////////////////
		
		m_maxFPS = DEFAULT_MAX_FPS;

		/* THREADING-ROADMAP.md 3.1.  Started before anything can fork and joined in ~GameEngine.
			 Costs one thread per spare core sitting on a semaphore until something uses it. */
		JobSystem::init();

		TheSubsystemList = MSGNEW("GameEngineSubsystem") SubsystemInterfaceList;
		
		TheSubsystemList->addSubsystem(this);

		// initialize the random number system
		InitRandom();

		// Create the low-level file system interface
		TheFileSystem = createFileSystem();

		// not part of the subsystem list, because it should normally never be reset!
		TheNameKeyGenerator = MSGNEW("GameEngineSubsystem") NameKeyGenerator;
		TheNameKeyGenerator->init();


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheNameKeyGenerator  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// not part of the subsystem list, because it should normally never be reset!
		TheCommandList = MSGNEW("GameEngineSubsystem") CommandList;
		TheCommandList->init();

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheCommandList  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		XferCRC xferCRC;
		xferCRC.open("lightCRC");


		initSubsystem(TheLocalFileSystem, "TheLocalFileSystem", createLocalFileSystem(), NULL);

		//Kris: Patch 1.01 - November 17, 2003
		//I was unable to resolve the RTPatch method of deleting a shipped file. English, Chinese, and Korean
		//SKU's shipped with two INIZH.big files. One properly in the Run directory and the other in Run\INI\Data.
		//We need to toast the latter in order for the game to patch properly.
		// (C1: moved here from just after createFileSystem, to go through TheLocalFileSystem; nothing
		// between the two places touched the file, and the archives still mount after it is gone.)
		TheLocalFileSystem->deleteFile( "Data\\INI\\INIZH.big" );


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheLocalFileSystem  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheArchiveFileSystem, "TheArchiveFileSystem", createArchiveFileSystem(), NULL); // this MUST come after TheLocalFileSystem creation

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheArchiveFileSystem  = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// Every INI below comes out of INIZH.big beside the exe.  Started anywhere else, the unzipped
		// package folder being the usual place, the first of them failed to open and the player got
		// EA's "viruses, overheated hardware" box, which names nothing they can act on.
		if (!TheFileSystem->doesFileExist("Data\\INI\\Default\\GameData.ini"))
		{
			const AsciiString currentDirectory = TheLocalFileSystem->getCurrentDirectory();
			const Char *gameDirectory = currentDirectory.str();
			DEBUG_LOG(("GameEngine::init - Data\\INI\\Default\\GameData.ini is in no archive under %s\n", gameDirectory));

			AsciiString message;
			message.format("Zero Hour's game files are not in\n\n%s\n\nThis generals.exe has to be in the Zero Hour folder, the one with INIZH.big in it. Run install.bat from the zip instead of starting the game in the folder it was unzipped to.", gameDirectory);
			MessageBoxWrapper( message.str(), "Command & Conquer Generals Zero Hour", MSGBOX_OK | MSGBOX_TASKMODAL | MSGBOX_ICONERROR );
			_exit(1);
		}

		// The water and most of the ground are the base game's, in its Textures.big and Terrain.big.
		// Without them the game still starts, onto a shell map of magenta water and black hills.
		if (!TheFileSystem->doesFileExist("Art\\Textures\\TWWater01.dds"))
		{
			DEBUG_LOG(("GameEngine::init - Art\\Textures\\TWWater01.dds is in no archive, the base game's are missing\n"));

			MessageBoxWrapper( "The original Generals game files are missing.\n\nZero Hour needs them next to it: a Steam install keeps them in the ZH_Generals folder beside generals.exe, with Textures.big and Terrain.big among them. Verify the game's files in Steam, or reinstall Command & Conquer Generals.", "Command & Conquer Generals Zero Hour", MSGBOX_OK | MSGBOX_TASKMODAL | MSGBOX_ICONERROR );
			_exit(1);
		}

		// The first place the archives can answer it, and before anything builds a path under
		// Data\<language>: GlobalLanguage, GameText, the CommandMap, speech and localized art all ask after this.
		{
			const AsciiString registryLanguage = GetRegistryLanguage();
			const AsciiString language = TheFileSystem->installedLanguage( registryLanguage );
			if (language.isEmpty())
			{
				DEBUG_LOG(("GameEngine::init - no Data\\<language>\\Language.ini in any archive\n"));

				MessageBoxWrapper( "Zero Hour's language files are missing: no Language.ini was found for any language.\n\nVerify the game's files in Steam, or reinstall Command & Conquer Generals Zero Hour.", "Command & Conquer Generals Zero Hour", MSGBOX_OK | MSGBOX_TASKMODAL | MSGBOX_ICONERROR );
				_exit(1);
			}
			if (language.compareNoCase( registryLanguage ) != 0)
			{
				DEBUG_LOG(("GameEngine::init - Language=%s has no Data\\%s\\Language.ini, using %s\n",
					registryLanguage.str(), registryLanguage.str(), language.str()));
				SetRegistryLanguage( language );
			}
		}

		initSubsystem(TheWritableGlobalData, "TheWritableGlobalData", MSGNEW("GameEngineSubsystem") GlobalData(), &xferCRC, "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini");


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After  TheWritableGlobalData = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



	#if defined(_DEBUG) || defined(_INTERNAL)
		// If we're in Debug or Internal, load the Debug info as well.
		ini.load( AsciiString( "Data\\INI\\GameDataDebug.ini" ), INI_LOAD_OVERWRITE, NULL );
	#endif
		
		// special-case: parse command-line parameters after loading global data
		parseCommandLine(argc, argv);

		// doesn't require resets so just create a single instance here.
		TheGameLODManager = MSGNEW("GameEngineSubsystem") GameLODManager;
		TheGameLODManager->init();
		
		// after parsing the command line, we may want to perform dds stuff. Do that here.
		if (TheGlobalData->m_shouldUpdateTGAToDDS) {
			// update any out of date targas here.
			updateTGAtoDDS();
		}

		// read the water settings from INI (must do prior to initing GameClient, apparently)
		ini.load( AsciiString( "Data\\INI\\Default\\Water.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Water.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Default\\Weather.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		ini.load( AsciiString( "Data\\INI\\Weather.ini" ), INI_LOAD_OVERWRITE, &xferCRC );



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After water INI's = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#ifdef DEBUG_CRC
		initSubsystem(TheDeepCRCSanityCheck, "TheDeepCRCSanityCheck", MSGNEW("GameEngineSubystem") DeepCRCSanityCheck, NULL, NULL, NULL, NULL);
#endif // DEBUG_CRC
		initSubsystem(TheGameText, "TheGameText", CreateGameTextInterface(), NULL);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameText = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheScienceStore,"TheScienceStore", MSGNEW("GameEngineSubsystem") ScienceStore(), &xferCRC, "Data\\INI\\Default\\Science.ini", "Data\\INI\\Science.ini");
		// New sciences only: a name EA already defined stops the load
		ini.load( AsciiString( "Data\\INI\\ScienceReforged.ini" ), INI_LOAD_MULTIFILE, &xferCRC );
		initSubsystem(TheMultiplayerSettings,"TheMultiplayerSettings", MSGNEW("GameEngineSubsystem") MultiplayerSettings(), &xferCRC, "Data\\INI\\Default\\Multiplayer.ini", "Data\\INI\\Multiplayer.ini");
		initSubsystem(TheTerrainTypes,"TheTerrainTypes", MSGNEW("GameEngineSubsystem") TerrainTypeCollection(), &xferCRC, "Data\\INI\\Default\\Terrain.ini", "Data\\INI\\Terrain.ini");
		initSubsystem(TheTerrainRoads,"TheTerrainRoads", MSGNEW("GameEngineSubsystem") TerrainRoadCollection(), &xferCRC, "Data\\INI\\Default\\Roads.ini", "Data\\INI\\Roads.ini");
		initSubsystem(TheGlobalLanguageData,"TheGlobalLanguageData",MSGNEW("GameEngineSubsystem") GlobalLanguage, NULL); // must be before the game text
		initSubsystem(TheCDManager,"TheCDManager", CreateCDManager(), NULL);
	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheCDManager = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
		initSubsystem(TheAudio,"TheAudio", createAudioManager(), NULL);
		// Whether this run can make a sound, in its log: a device handle exists only if the audio manager
		// went on to open one, which it does not do with audio off (-noaudio, -headless, and off Windows a
		// hidden window or -offscreen).
		if (!TheGlobalData->m_audioOn)
			DEBUG_LOG(("TheAudio: audio off, no device opened\n"));
		else
			DEBUG_LOG(("TheAudio: audio on, device handle %s\n", TheAudio->getDevice() != NULL ? "set" : "none (no device could be opened)"));
		//
		// Missing music used to end the process here, with setQuitting and not one word anywhere: the
		// game started, the window appeared for a moment and it closed again.  A player who deleted
		// Music.big to save space, or a mod that ships without music, got that and no way to find out
		// why.  Music is not a thing the game needs to run - the audio manager already plays nothing
		// when it has nothing - so say so in the log and carry on.
		//
		if (!TheAudio->isMusicAlreadyLoaded())
			DEBUG_LOG(("No music track was found - the game runs without music. Check that Music.big is next to the exe.\n"));

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheAudio = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFunctionLexicon,"TheFunctionLexicon", createFunctionLexicon(), NULL);
		initSubsystem(TheModuleFactory,"TheModuleFactory", createModuleFactory(), NULL);
		initSubsystem(TheMessageStream,"TheMessageStream", createMessageStream(), NULL);
		initSubsystem(TheSidesList,"TheSidesList", MSGNEW("GameEngineSubsystem") SidesList(), NULL);
		initSubsystem(TheCaveSystem,"TheCaveSystem", MSGNEW("GameEngineSubsystem") CaveSystem(), NULL);
		initSubsystem(TheRankInfoStore,"TheRankInfoStore", MSGNEW("GameEngineSubsystem") RankInfoStore(), &xferCRC, NULL, "Data\\INI\\Rank.ini");
		initSubsystem(ThePlayerTemplateStore,"ThePlayerTemplateStore", MSGNEW("GameEngineSubsystem") PlayerTemplateStore(), &xferCRC, "Data\\INI\\Default\\PlayerTemplate.ini", "Data\\INI\\PlayerTemplate.ini");
		initSubsystem(TheParticleSystemManager,"TheParticleSystemManager", createParticleSystemManager(), NULL);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheParticleSystemManager = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
    
    
		initSubsystem(TheFXListStore,"TheFXListStore", MSGNEW("GameEngineSubsystem") FXListStore(), &xferCRC, "Data\\INI\\Default\\FXList.ini", "Data\\INI\\FXList.ini");
		/* The fork's own detonation light, on top of EA's list.  It is a separate file rather than a
			 loose copy of FXList.ini because a loose copy shadows the whole 190K shipped file: it goes
			 stale against every patch, it cannot be reviewed, and - since it lands in the INI CRC below -
			 it silently refuses every multiplayer join from a machine that does not have the same one. */
		ini.load( AsciiString( "Data\\INI\\FXListReforged.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		initSubsystem(TheWeaponStore,"TheWeaponStore", MSGNEW("GameEngineSubsystem") WeaponStore(), &xferCRC, NULL, "Data\\INI\\Weapon.ini");
		initSubsystem(TheObjectCreationListStore,"TheObjectCreationListStore", MSGNEW("GameEngineSubsystem") ObjectCreationListStore(), &xferCRC, "Data\\INI\\Default\\ObjectCreationList.ini", "Data\\INI\\ObjectCreationList.ini");
		/* Lists EA left out or got wrong, before any object names one: a list parsed again is cleared
			 and replaced whole, and a new name is simply added. */
		ini.load( AsciiString( "Data\\INI\\ObjectCreationListReforged.ini" ), INI_LOAD_OVERWRITE, &xferCRC );
		initSubsystem(TheLocomotorStore,"TheLocomotorStore", MSGNEW("GameEngineSubsystem") LocomotorStore(), &xferCRC, NULL, "Data\\INI\\Locomotor.ini");
		initSubsystem(TheSpecialPowerStore,"TheSpecialPowerStore", MSGNEW("GameEngineSubsystem") SpecialPowerStore(), &xferCRC, "Data\\INI\\Default\\SpecialPower.ini", "Data\\INI\\SpecialPower.ini");
		/* Powers edited in place (a scan Frenzy should not give) and the Demolitions General's own
			 Rebel Ambush, before any object names one. */
		ini.load( AsciiString( "Data\\INI\\SpecialPowerReforged.ini" ), INI_LOAD_MULTIFILE, &xferCRC );
		initSubsystem(TheDamageFXStore,"TheDamageFXStore", MSGNEW("GameEngineSubsystem") DamageFXStore(), &xferCRC, NULL, "Data\\INI\\DamageFX.ini");
		initSubsystem(TheArmorStore,"TheArmorStore", MSGNEW("GameEngineSubsystem") ArmorStore(), &xferCRC, NULL, "Data\\INI\\Armor.ini");
		initSubsystem(TheBuildAssistant,"TheBuildAssistant", MSGNEW("GameEngineSubsystem") BuildAssistant, NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheBuildAssistant = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



		initSubsystem(TheThingFactory,"TheThingFactory", createThingFactory(), &xferCRC, "Data\\INI\\Default\\Object.ini", NULL, "Data\\INI\\Object");
		/* The fork's balance, written over EA's numbers after every object, weapon and armor exists.
			 MULTIFILE edits a template in place and leaves every field the file does not name as EA
			 wrote it, so the file holds the changes and nothing else; an Armor block still replaces
			 that armor whole.  It is in the INI CRC like the files it edits. */
		ini.load( AsciiString( "Data\\INI\\BalanceReforged.ini" ), INI_LOAD_MULTIFILE, &xferCRC );
		/* Mistakes in EA's data for the nine generals, patched the same way: a copy that missed the
			 original's change, a wrong faction's sound, an icon naming an upgrade that does not exist. */
		ini.load( AsciiString( "Data\\INI\\FixesReforged.ini" ), INI_LOAD_MULTIFILE, &xferCRC );

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheThingFactory = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
    
    
		initSubsystem(TheUpgradeCenter,"TheUpgradeCenter", MSGNEW("GameEngineSubsystem") UpgradeCenter, &xferCRC, "Data\\INI\\Default\\Upgrade.ini", "Data\\INI\\Upgrade.ini");
		// An upgrade parsed again is edited in place, so this file names only what changes
		ini.load( AsciiString( "Data\\INI\\UpgradeReforged.ini" ), INI_LOAD_MULTIFILE, &xferCRC );
		initSubsystem(TheGameClient,"TheGameClient", createGameClient(), NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameClient = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////

	
		initSubsystem(TheAI,"TheAI", MSGNEW("GameEngineSubsystem") AI(), &xferCRC,  "Data\\INI\\Default\\AIData.ini", "Data\\INI\\AIData.ini");
		initSubsystem(TheGameLogic,"TheGameLogic", createGameLogic(), NULL);
		initSubsystem(TheTeamFactory,"TheTeamFactory", MSGNEW("GameEngineSubsystem") TeamFactory(), NULL);
		initSubsystem(TheCrateSystem,"TheCrateSystem", MSGNEW("GameEngineSubsystem") CrateSystem(), &xferCRC, "Data\\INI\\Default\\Crate.ini", "Data\\INI\\Crate.ini");
		initSubsystem(ThePlayerList,"ThePlayerList", MSGNEW("GameEngineSubsystem") PlayerList(), NULL);
		initSubsystem(TheRecorder,"TheRecorder", createRecorder(), NULL);
		initSubsystem(TheRadar,"TheRadar", createRadar(), NULL);
		initSubsystem(TheVictoryConditions,"TheVictoryConditions", createVictoryConditions(), NULL);



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheVictoryConditions = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		AsciiString fname;
		fname.format("Data\\%s\\CommandMap.ini", GetRegistryLanguage().str());
		initSubsystem(TheMetaMap,"TheMetaMap", MSGNEW("GameEngineSubsystem") MetaMap(), NULL, fname.str(), "Data\\INI\\CommandMapReforged.ini");
		// Classic answers to the game's own map and to nothing this fork binds
		TheMetaMap->loadClassicBindings(fname);

#if defined(_DEBUG) || defined(_INTERNAL)
		ini.load("Data\\INI\\CommandMapDebug.ini", INI_LOAD_MULTIFILE, NULL);
#endif

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		ini.load("Data\\INI\\CommandMapDemo.ini", INI_LOAD_MULTIFILE, NULL);
#endif

		initSubsystem(TheActionManager,"TheActionManager", MSGNEW("GameEngineSubsystem") ActionManager(), NULL);
		//initSubsystem((CComObject<WebBrowser> *)TheWebBrowser,"(CComObject<WebBrowser> *)TheWebBrowser", (CComObject<WebBrowser> *)createWebBrowser(), NULL);
		initSubsystem(TheGameStateMap,"TheGameStateMap", MSGNEW("GameEngineSubsystem") GameStateMap, NULL, NULL, NULL );
		initSubsystem(TheGameState,"TheGameState", MSGNEW("GameEngineSubsystem") GameState, NULL, NULL, NULL );

		// Create the interface for sending game results
		initSubsystem(TheGameResultsQueue,"TheGameResultsQueue", GameResultsInterface::createNewGameResultsInterface(), NULL, NULL, NULL, NULL);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameResultsQueue = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// the control bar parses these without the checksum, and the AI builds and hunts from them
		checksumFileContents( xferCRC, "Data\\INI\\Default\\CommandButton.ini" );
		checksumFileContents( xferCRC, "Data\\INI\\CommandButton.ini" );
		checksumFileContents( xferCRC, "Data\\INI\\CommandSet.ini" );
		checksumFileContents( xferCRC, "Data\\INI\\CommandSetReforged.ini" );
		checksumModels( xferCRC );

		xferCRC.close();
		TheWritableGlobalData->m_iniCRC = xferCRC.getCRC();
		DEBUG_LOG(("INI CRC is 0x%8.8X\n", TheGlobalData->m_iniCRC));

		TheSubsystemList->postProcessLoadAll();

		setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);

		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_musicOn, AudioAffect_Music);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_soundsOn, AudioAffect_Sound);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_sounds3DOn, AudioAffect_Sound3D);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_speechOn, AudioAffect_Speech);
			
		// We're not in a network game yet, so set the network singleton to NULL.
		TheNetwork = NULL;

		//Create a default ini file for options if it doesn't already exist.
		//OptionPreferences prefs( TRUE );

		// If we turn m_quitting to FALSE here, then we throw away any requests to quit that
		// took place during loading. :-\ - jkmcd
		// If this really needs to take place, please make sure that pressing cancel on the audio 
		// load music dialog will still cause the game to quit.
		// m_quitting = FALSE;

		// for fingerprinting, we need to ensure the presence of these files


#if !defined(_INTERNAL) && !defined(_DEBUG)
		AsciiString dirName;
    dirName = TheArchiveFileSystem->getArchiveFilenameForFile("generalsbzh.sec");

    if (dirName.compareNoCase("genseczh.big") != 0)
		{
			DEBUG_LOG(("generalsbzh.sec was not found in genseczh.big - it was in '%s'\n", dirName.str()));
			m_quitting = TRUE;
		}
		
		dirName = TheArchiveFileSystem->getArchiveFilenameForFile("generalsazh.sec");
		const char *noPath = dirName.reverseFind('\\');
		if (noPath) {
			dirName = noPath + 1;
		}

		if (dirName.compareNoCase("musiczh.big") != 0)
		{
			DEBUG_LOG(("generalsazh.sec was not found in musiczh.big - it was in '%s'\n", dirName.str()));
			m_quitting = TRUE;
		}
#endif


		// initialize the MapCache
		TheMapCache = MSGNEW("GameEngineSubsystem") MapCache;
		TheMapCache->updateCache();


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheMapCache->updateCache = %f seconds \n",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		if (TheGlobalData->m_buildMapCache)
		{
			// just quit, since the map cache has already updated
			//populateMapListbox(NULL, true, true);
			m_quitting = TRUE;
		}
		
		// load the initial shell screen
		//TheShell->push( AsciiString("Menus/MainMenu.wnd") );
		
		// This allows us to run a map/reply from the command line
		if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
		{
			AsciiString fname = TheGlobalData->m_initialFile;
			fname.toLower();

			if (fname.endsWithNoCase(".map"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = TheGlobalData->m_initialFile;

				// shutdown the top, but do not pop it off the stack
	//			TheShell->hideShell();

				// send a message to the logic for a new game
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(DIFFICULTY_NORMAL);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
			else if (fname.endsWithNoCase(".rep"))
			{
				/* Playback cannot start here.  init() ends with TheSubsystemList->resetAll(), and
					 RecorderClass::reset() closes the file it is playing back and drops the mode to
					 NONE - after which the queued MSG_NEW_GAME starts a replay game with no command
					 stream behind it and the recorder, seeing mode NONE, starts *recording* that empty
					 game over the replay it was asked to play.  The menu path is unaffected because by
					 then the reset is long past.  So remember the name and open it below.

					 The shell map goes off for the same reason the .map branch turns it off: it is a
					 live game of its own, and a replay that fails to open would otherwise leave the
					 process sitting in it, which from the outside looks exactly like a hung playback. */
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				thePendingReplayFile = fname;
			}
			else if (fname.endsWithNoCase(".sav"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				thePendingSaveFile = TheGlobalData->m_initialFile;	// the name as given, not lowercased
			}
		}
		else if (TheGlobalData->m_netGameHosts.isNotEmpty())
		{
			/* Deferred for the same reason the replay is: the network game has to survive the
				 resetAll() that ends init(), and TheRecorder has to be past its reset before the
				 MSG_NEW_GAME that makes it start recording is queued. */
			TheWritableGlobalData->m_shellMapOn = FALSE;
			TheWritableGlobalData->m_playIntro = FALSE;
		}
		else if (TheGlobalData->m_autoSkirmishPlayers > 0)
		{
			startAutoSkirmish( TheGlobalData->m_autoSkirmishPlayers );
		}

		// 
		if (TheMapCache && TheGlobalData->m_shellMapOn)
		{
			AsciiString lowerName = TheGlobalData->m_shellMapName;
			lowerName.toLower();

			MapCache::const_iterator it = TheMapCache->find(lowerName);
			if (it == TheMapCache->end())
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
			}
		}

		if(!TheGlobalData->m_playIntro)
			TheWritableGlobalData->m_afterIntro = TRUE;

		//initDisabledMasks();
		
	}
	catch (ErrorCode ec)
	{
		/* Every ErrorCode but ERROR_INVALID_D3D used to leave here without a word, and init then ran
			 on to its tail with half its subsystems missing - the first frame faulted on a NULL
			 TheGameLogic and the crash log blamed GameEngine::update() for a failure that happened
			 during startup.  Say which code it was and where it left the engine. */
		DEBUG_LOG(("GameEngine::init - ErrorCode %d escaped initialization (TheGameLogic=%p, TheGameClient=%p)\n",
			(Int)ec, TheGameLogic, TheGameClient));
		if (ec == ERROR_INVALID_D3D)
		{
			RELEASE_CRASHLOCALIZED("ERROR:D3DFailurePrompt", "ERROR:D3DFailureMessage");
		}
		/* Anything else stops here as well.  Running on means running frames against subsystems that
			 were never built, and the access violation that follows names update() instead of the real
			 failure - which is how a bad -map argument came to read as "the game crashed on quit". */
		AsciiString why;
		why.format("GameEngine::init failed with ErrorCode 0x%08x.", (UnsignedInt)ec);
		RELEASE_CRASH((why.str()));
	}
	catch (INIException e)
	{
		if (e.mFailureMessage)
			RELEASE_CRASH((e.mFailureMessage));
		else
			RELEASE_CRASH(("Uncaught Exception during initialization."));

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught Exception during initialization."));
	}

	if(!TheGlobalData->m_playIntro)
		TheWritableGlobalData->m_afterIntro = TRUE;

	initKindOfMasks();
	initDisabledMasks();
	initDamageTypeFlags();

	TheSubsystemList->resetAll();
	HideControlBar();

	/* Now that every subsystem has been reset, the recorder can be handed the replay and keep
		 it: the file stays open, the mode stays PLAYBACK, and the MSG_NEW_GAME appended here is
		 the one the logic acts on. */
	if (thePendingReplayFile.isNotEmpty())
	{
		if (!TheRecorder->playbackFile( thePendingReplayFile ))
		{
			DEBUG_LOG(("-replay: '%s' could not be played back\n", thePendingReplayFile.str()));
			TheWritableGlobalData->m_shellMapOn = TRUE;
		}
		thePendingReplayFile.clear();
	}

	/* Same point in the sequence, same reason: the world a save builds has to survive resetAll().
		 The menu path (doLoadGame in PopupSaveLoad.cpp) prepares a single player game first and falls
		 back to the shell if the load fails; do both here. */
	if (thePendingSaveFile.isNotEmpty())
	{
		startPendingSaveGame();
		thePendingSaveFile.clear();
	}

	if (TheGlobalData->m_netGameHosts.isNotEmpty() && TheGlobalData->m_initialFile.isEmpty())
	{
		startAutoNetGame();
	}
}  // end init

/** -----------------------------------------------------------------------------------------------
	* Reset all necessary parts of the game engine to be ready to accept new game data 
	*/
void GameEngine::reset( void )
{

	WindowLayout *background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(background,("We Couldn't Load Menus/BlankWindow.wnd"));
	// NULL when the file could not be read, which with the stock data means its drive has gone
	// (GameDataGone normally ends the game first); the backdrop is cosmetic, so go on without it
	if (background != NULL)
	{
		background->hide(FALSE);
		background->bringForward();
		background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	}
	Bool deleteNetwork = false;
	if (TheGameLogic->isInMultiplayerGame())
		deleteNetwork = true;

	TheSubsystemList->resetAll();

	if (deleteNetwork)
	{
		DEBUG_ASSERTCRASH(TheNetwork, ("Deleting NULL TheNetwork!"));
		if (TheNetwork)
			delete TheNetwork;
		TheNetwork = NULL;
	}
	if(background)
	{
		background->destroyWindows();
		background->deleteInstance();
		background = NULL;
	}
}

/// -----------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(GameEngine_update)

/** -----------------------------------------------------------------------------------------------
 * Wall-clock pacing for the logic tick: update() may be called at any render rate, but a logic
 * frame is only "due" every 1000/logicFps milliseconds.
 *
 * A render pass longer than one logic frame owes the simulation more than one tick, and the debt
 * has to be payable or game speed silently becomes render speed - at 20fps a 30Hz match runs a
 * third slow. So the accumulator carries up to LOGIC_CATCHUP_MAX_FRAMES frames of debt and the
 * caller drains it in a loop, skipping the client half while it catches up.
 *
 * The cap is what keeps that from spiralling: when the logic frame itself is what blew the budget,
 * paying the debt would just queue more of the same work. Past the cap the surplus is dropped and
 * the match honestly runs slow - which is what the HUD's 'hz' readout reports.
 *
 * Free function (not a member, not static) so test_gameengine can link straight to it.
 */
Int GameEngine_logicCatchupMaxFrames( Int logicFps )
{
	if (logicFps <= 0)
		return 1;
	const Int frames = (Int)(logicFps * LOGIC_CATCHUP_MAX_MS / 1000.0f + 0.5f);
	return frames < 1 ? 1 : frames;
}

Bool GameEngine_mayStartAnotherCatchupTick( Int ticksSoFar, Int maxTicks, Real elapsedMsInLoop )
{
	if (ticksSoFar >= maxTicks)
		return FALSE;
	return elapsedMsInLoop < LOGIC_CATCHUP_BUDGET_MS;
}

/* R1, smooth motion: when the last logic tick finished and how long a tick lasts, so the renderer can
	 show models between their last two logic states (W3DSmoothMotion.h).  Read, never written, by the
	 client; nothing here reaches the logic. */
static Int64 s_lastLogicTickTicks = 0;
static Real s_msPerLogicTick = 0.0f;

void GameEngine_noteLogicTickDone( Int logicFps, Bool fastMode )
{
	s_lastLogicTickTicks = Clock_Ticks();
	// Fast mode runs one tick per pass however long it takes: there is no tick length to blend over.
	s_msPerLogicTick = (logicFps > 0 && !fastMode) ? 1000.0f / (Real)logicFps : 0.0f;
}

/* -recordfps: which of the logic frame's pictures the coming draw is.  The logic loop moves it on once a
	 pass and runs the next frame after the last one (the fast-mode branch in GameEngine::update). */
static Int s_videoPicture = 0;

static Bool isVideoFrame( Int frame )
{
	if (TheGlobalData->m_videoEndFrame <= 0 || TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return FALSE;
	return frame >= TheGlobalData->m_videoStartFrame && frame <= TheGlobalData->m_videoEndFrame;
}

/* -recordfps's pictures of a recorded logic frame.  A network game's clock is the network's, one logic
	 frame a pass or more, and -headless draws no picture to make more of */
static Int videoPicturesOn( Int frame )
{
	if (TheNetwork != NULL || TheGlobalData->m_headless || !isVideoFrame( frame ))
		return 1;
	return TheGlobalData->m_videoPictures;
}

Bool GameEngine_isVideoFrame( void )
{
	return TheGameLogic != NULL && isVideoFrame( (Int)TheGameLogic->getFrame() );
}

Int GameEngine_videoPictures( void )
{
	return TheGameLogic == NULL ? 1 : videoPicturesOn( (Int)TheGameLogic->getFrame() );
}

Bool GameEngine_videoBlends( void )
{
	if (TheGameLogic == NULL)
		return FALSE;
	const Int frame = (Int)TheGameLogic->getFrame();
	return videoPicturesOn( frame ) > 1 || videoPicturesOn( frame + 1 ) > 1;
}

Int GameEngine_videoFramesPerSecond( void )
{
	return TheNetwork != NULL || TheGlobalData->m_headless ? LOGICFRAMES_PER_SECOND
		: LOGICFRAMES_PER_SECOND * TheGlobalData->m_videoPictures;
}

Int GameEngine_videoPicture( void )
{
	// a match that ended between two pictures of a frame leaves the count where it was
	return GameEngine_videoPictures() > 1 ? s_videoPicture : 0;
}

Real GameEngine_pictureFrame( void )
{
	const Int pictures = GameEngine_videoPictures();
	return (Real)TheGameLogic->getFrame() - (Real)(pictures - 1 - GameEngine_videoPicture()) / (Real)pictures;
}

UnsignedInt GameEngine_pictureMilliseconds( void )
{
	const Int64 pictures = GameEngine_videoPictures();
	const Int64 shown = (Int64)TheGameLogic->getFrame() * pictures - (pictures - 1 - GameEngine_videoPicture());
	return shown <= 0 ? 0 : (UnsignedInt)(shown * 1000 / (LOGICFRAMES_PER_SECOND * pictures));
}

Real GameEngine_logicTickFraction( void )
{
	const Int pictures = GameEngine_videoPictures();
	if (pictures > 1)
		return (Real)(GameEngine_videoPicture() + 1) / (Real)pictures;
	if (s_msPerLogicTick <= 0.0f)
		return 1.0f;
	const Real ms = (Real)(Clock_Ticks() - s_lastLogicTickTicks) * 1000.0f / (Real)Clock_Ticks_Per_Second();
	const Real fraction = ms / s_msPerLogicTick;
	return fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
}

Bool GameEngine_isLogicFrameDue( Real& accumMs, Real elapsedMs, Int logicFps )
{
	if (logicFps <= 0)
		return TRUE;
	const Real msPerLogicFrame = 1000.0f / logicFps;
	const Real maxAccumMs = msPerLogicFrame * GameEngine_logicCatchupMaxFrames(logicFps);
	if (elapsedMs > maxAccumMs)
		elapsedMs = maxAccumMs;
	accumMs += elapsedMs;
	if (accumMs > maxAccumMs)
		accumMs = maxAccumMs;
	if (accumMs < msPerLogicFrame)
		return FALSE;
	accumMs -= msPerLogicFrame;
	return TRUE;
}

/** -----------------------------------------------------------------------------------------------
 * Update the game engine by updating the GameClient and GameLogic singletons.
 * The client runs as fast as possible; TheGameLogic is limited to a fixed wall-clock
 * framerate (m_maxFPS, the game-speed setting), so game speed does not scale with the
 * render framerate.
 */
#ifdef DEBUG_LOGGING
//
// Frame rate watchdog.  A slow logic frame and a slow render both show up to the player as the same
// stutter, and the logic-side slow-frame log cannot tell them apart because it never sees the client
// half of the loop.  So time both halves of every pass and, once a second, say what the rate was and
// where the time went - but only when the rate actually dropped, so a smooth match logs nothing.
//
extern Real TheClientDrawMS;			///< GameClient.cpp: how long the last TheDisplay->DRAW() took
extern Real TheSceneDrawMS;				///< ...of which the 3D world
extern Real TheUIDrawMS;					///< ...and the interface over it
extern Real TheUIPostDrawMS;			///< ...of which the overlays and the HUD strips
extern Real TheWindowRepaintMS;		///< ...and the window system's repaint
extern Real TheStripGatherMS;			///< ...of the overlays, the production strip's sweep
extern Real TheStripDrawMS;				///< ...and the production strip's own drawing
extern Real TheParticleUpdateMS;		///< GameClient.cpp: the three places particles cost
extern Real TheParticleFillMS;
extern Real TheTranslucentMS;
extern UnsignedInt TheTranslucentDraws;
extern UnsignedInt TheSortingPolygonsRefused;
extern UnsignedInt TheParticlesPastGroupLimit;
extern Real TheShadowMapMS;
extern Int TheShadowMapCasters;
extern UnsignedInt TheShadowMapDraws;
extern Real ThePostChainMS;
extern Real TheIconDrawMS;
extern UnsignedInt TheSceneDrawCalls;
extern Real TheSortingSortMS;
extern Real TheSortingCopyMS;
extern Real TheSortingDrawMS;
extern UnsignedInt TheSortingEntries;

/** Particle cost summed over an unattended run, per drawn frame, for HEADLESS PARTICLECOST. */
struct ParticleCostTotals
{
	Int			m_passes;
	double	m_updateMS;
	double	m_fillMS;
	double	m_flushMS;
	double	m_draws;
	double	m_particles;
	double	m_onScreen;
	double	m_refusedPolygons;
	double	m_pastGroupLimit;
	UnsignedInt	m_peakParticles;
};
static ParticleCostTotals theParticleCost;

#endif

// Outside the DEBUG_LOGGING block above on purpose: the frame-time histogram below is not debug
// instrumentation, it ships, and it needs this.
static Real engineElapsedMS( const Int64 &from, const Int64 &to )
{
	Int64 freq;
	freq = Clock_Ticks_Per_Second();
	if( freq < 1 )
		return 0.0f;
	return (Real)((double)(to - from) * 1000.0 / (double)freq);
}

/** -----------------------------------------------------------------------------------------------
 * Frame time as a distribution, not as an average.
 *
 * A mean is exactly the statistic that hides a stutter: a match that runs at 3ms and hitches to
 * 120ms four times a minute has a beautiful average and is horrible to play.  What a player feels
 * is the tail - so this keeps the tail and throws the mean in as an afterthought.
 *
 * A fixed histogram, because the alternative is keeping every sample: 0.25ms buckets out to 64ms
 * plus one overflow bucket, which is 257 Ints of static storage, no allocation, and one add per
 * frame.  Percentiles come out of the buckets (so they are accurate to a quarter of a millisecond,
 * which is far finer than anything that matters here); the worst frame is kept exactly, because the
 * worst frame is the one that gets complained about.
 */
class FrameTimeHistogram
{
public:
	enum { BUCKET_COUNT = 257, LAST_BUCKET = BUCKET_COUNT - 1 };
	static const Real BUCKET_MS;			// width of one bucket

	void reset( void )
	{
		m_count = 0;
		m_sumMS = 0.0;
		m_worstMS = 0.0f;
		m_worstAtFrame = 0;
		for( Int i = 0; i < BUCKET_COUNT; ++i )
			m_buckets[ i ] = 0;
	}

	void note( Real ms, UnsignedInt logicFrame )
	{
		if( ms < 0.0f )
			return;										// a clock that went backwards is not a frame time

		++m_count;
		m_sumMS += ms;
		if( ms > m_worstMS )
		{
			m_worstMS = ms;
			m_worstAtFrame = logicFrame;
		}

		Int bucket = (Int)(ms / BUCKET_MS);
		if( bucket > LAST_BUCKET )
			bucket = LAST_BUCKET;		// everything past 64ms lands together; it is all "terrible"
		++m_buckets[ bucket ];
	}

	Int count( void ) const { return m_count; }
	Real worstMS( void ) const { return m_worstMS; }
	UnsignedInt worstAtFrame( void ) const { return m_worstAtFrame; }
	Real meanMS( void ) const { return m_count ? (Real)(m_sumMS / m_count) : 0.0f; }

	/** The bucket boundary at or below which `fraction` of the frames fall.  Reported as the top of
		the bucket, so it never claims a frame was faster than it was. */
	Real percentileMS( Real fraction ) const
	{
		if( m_count <= 0 )
			return 0.0f;
		const Int target = (Int)(fraction * m_count);
		Int running = 0;
		for( Int i = 0; i < BUCKET_COUNT; ++i )
		{
			running += m_buckets[ i ];
			if( running > target )
				return (i == LAST_BUCKET) ? m_worstMS : (Real)(i + 1) * BUCKET_MS;
		}
		return m_worstMS;
	}

	/** How many frames took longer than `ms`.  This is the stutter count: pick the budget the
		frame is supposed to fit in and this says how often it did not. */
	Int countOver( Real ms ) const
	{
		Int over = 0;
		const Int firstBad = (Int)(ms / BUCKET_MS) + 1;
		for( Int i = firstBad; i < BUCKET_COUNT; ++i )
			over += m_buckets[ i ];
		return over;
	}

private:
	Int m_buckets[ BUCKET_COUNT ];
	Int m_count;
	double m_sumMS;
	Real m_worstMS;
	UnsignedInt m_worstAtFrame;
};

const Real FrameTimeHistogram::BUCKET_MS = 0.25f;

/* One for the whole loop pass - what the player's eye is on - and one for the logic tick alone,
	 because those are two different stutters with two different fixes: a long render frame drops a
	 picture, a logic tick over its 33ms budget drops the whole simulation behind the wall clock. */
static FrameTimeHistogram theFrameTimes;
static FrameTimeHistogram theLogicTimes;
static Bool theFrameTimesStarted = FALSE;

void GameEngine_noteFrameTime( Real ms, UnsignedInt logicFrame )
{
	/* The histogram deliberately ignores the opening second of a match, and the opening second of a
		 match is exactly where a player feels the game stutter.  So every long pass gets a line of its
		 own from the first frame onwards, whether or not the histogram has started.  Read it beside
		 SLOW LOGIC FRAME: a long pass with no logic line under it was spent on the client side. */
	const Real SLOW_PASS_MS = 25.0f;
	if( ms > SLOW_PASS_MS && TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
	{
		/* A crowd over the bar every pass used to write and fflush a line 30 times
			 a second. Once a second is enough to see the pass is still slow. */
		static UnsignedInt s_lastSlowPassLogFrame = 0;
		static Int s_slowPassSkipped = 0;
		if( logicFrame < s_lastSlowPassLogFrame )
		{
			s_lastSlowPassLogFrame = 0;
			s_slowPassSkipped = 0;
		}
		if( logicFrame >= s_lastSlowPassLogFrame + LOGICFRAMES_PER_SECOND )
		{
			if( s_slowPassSkipped )
				DEBUG_LOG(("SLOW PASS frame %d: %.1f ms (%d more since last)\n", logicFrame, ms, s_slowPassSkipped));
			else
				DEBUG_LOG(("SLOW PASS frame %d: %.1f ms\n", logicFrame, ms));
			s_lastSlowPassLogFrame = logicFrame;
			s_slowPassSkipped = 0;
		}
		else
			++s_slowPassSkipped;
	}

	if( !theFrameTimesStarted )
		return;
	theFrameTimes.note( ms, logicFrame );
}

/* The world's object count once a logic tick, debris and dying objects included, so a data change
	 that adds objects (more wreckage, more projectiles) is argued with a number.  Read-only, and only
	 in an unattended run that started counting. */
static UnsignedInt theObjectPeak = 0;
static UnsignedInt theObjectPeakFrame = 0;
static double theObjectSum = 0.0;
static Int theObjectTicks = 0;

void GameEngine_noteLogicTime( Real ms, UnsignedInt logicFrame )
{
	if( !theFrameTimesStarted )
		return;
	theLogicTimes.note( ms, logicFrame );

	const UnsignedInt objects = TheGameLogic ? TheGameLogic->getObjectCount() : 0;
	theObjectSum += objects;
	++theObjectTicks;
	if( objects > theObjectPeak )
	{
		theObjectPeak = objects;
		theObjectPeakFrame = logicFrame;
	}
}

/** Start counting.  Called when an unattended run actually begins, so the map load, the first
	 asset pass and the settling second are not counted as stutters - they are, but they are not the
	 kind anybody can do anything about, and leaving them in buries the ones that matter. */
static void startFrameTimeStats( void )
{
	theFrameTimes.reset();
	theLogicTimes.reset();
	theObjectPeak = 0;
	theObjectPeakFrame = 0;
	theObjectSum = 0.0;
	theObjectTicks = 0;
	theFrameTimesStarted = TRUE;
#ifdef DEBUG_LOGGING
	memset( &theParticleCost, 0, sizeof(theParticleCost) );
#endif
}

static void reportFrameTimeStats( void )
{
	if( theFrameTimes.count() <= 0 )
		return;

	const Real over16 = 100.0f * theFrameTimes.countOver( 16.7f ) / theFrameTimes.count();
	const Real over33 = 100.0f * theFrameTimes.countOver( 33.3f ) / theFrameTimes.count();
	DEBUG_LOG(("HEADLESS FRAMETIME: %d frames | mean %.2f p50 %.2f p95 %.2f p99 %.2f p99.9 %.2f worst %.2f ms (frame %d) | over 16.7ms %d (%.2f%%) | over 33.3ms %d (%.2f%%)\n",
						 theFrameTimes.count(), theFrameTimes.meanMS(),
						 theFrameTimes.percentileMS( 0.50f ), theFrameTimes.percentileMS( 0.95f ),
						 theFrameTimes.percentileMS( 0.99f ), theFrameTimes.percentileMS( 0.999f ),
						 theFrameTimes.worstMS(), theFrameTimes.worstAtFrame(),
						 theFrameTimes.countOver( 16.7f ), over16,
						 theFrameTimes.countOver( 33.3f ), over33));
	if (TheGlobalData)
	{
		const char *lodName = "off";
		if (TheGameLODManager && TheGlobalData->isDynamicLODEnabled())
		{
			const DynamicGameLODLevel lod = TheGameLODManager->getDynamicLODLevel();
			if (lod >= DYNAMIC_GAME_LOD_LOW && lod < DYNAMIC_GAME_LOD_COUNT)
				lodName = TheGameLODManager->getDynamicGameLODLevelName(lod);
		}
		DEBUG_LOG(("QUALITY: filter %d aniso %d particles %d (in force %d) msaaLevel %d vsync %d shadows vol %d decal %d trees %d heat %d dynamicLOD %s\n",
							 TheGlobalData->m_textureFilterMode,
							 TheGlobalData->m_anisotropyLevel,
							 TheGlobalData->m_maxParticleCount,
							 TheGlobalData->getEffectiveParticleCap(),
							 TheGlobalData->m_msaaLevel,
							 (Int)TheGlobalData->m_vsync,
							 (Int)TheGlobalData->m_useShadowVolumes,
							 (Int)TheGlobalData->m_useShadowDecals,
							 (Int)TheGlobalData->m_useTrees,
							 (Int)TheGlobalData->m_useHeatEffects,
							 lodName));
	}

#ifdef DEBUG_LOGGING
	if( theParticleCost.m_passes > 0 )
	{
		const double passes = (double)theParticleCost.m_passes;
		DEBUG_LOG(("HEADLESS PARTICLECOST: per drawn frame | update %.3f fill %.3f translucent flush %.3f ms | %.1f translucent draws | %.0f particles, %.0f on screen, peak %u | not drawn: %.0f polygons refused by the sorting pool, %.0f particles past the per-system limit\n",
							 theParticleCost.m_updateMS / passes, theParticleCost.m_fillMS / passes,
							 theParticleCost.m_flushMS / passes, theParticleCost.m_draws / passes,
							 theParticleCost.m_particles / passes, theParticleCost.m_onScreen / passes,
							 theParticleCost.m_peakParticles,
							 theParticleCost.m_refusedPolygons / passes, theParticleCost.m_pastGroupLimit / passes));
	}
#endif

	if( theLogicTimes.count() > 0 )
	{
		DEBUG_LOG(("HEADLESS LOGICTIME: %d ticks | mean %.2f p50 %.2f p95 %.2f p99 %.2f p99.9 %.2f worst %.2f ms (frame %d) | over 33.3ms %d\n",
							 theLogicTimes.count(), theLogicTimes.meanMS(),
							 theLogicTimes.percentileMS( 0.50f ), theLogicTimes.percentileMS( 0.95f ),
							 theLogicTimes.percentileMS( 0.99f ), theLogicTimes.percentileMS( 0.999f ),
							 theLogicTimes.worstMS(), theLogicTimes.worstAtFrame(),
							 theLogicTimes.countOver( 33.3f )));
	}

	if( theObjectTicks > 0 )
	{
		DEBUG_LOG(("HEADLESS OBJECTS: peak %u (frame %u) | mean %.0f over %d ticks\n",
							 theObjectPeak, theObjectPeakFrame, theObjectSum / theObjectTicks, theObjectTicks));
	}
}

/** -----------------------------------------------------------------------------------------------
 * -autocamera <n>: every n seconds, put the camera where the fighting is.
 *
 * A soak run watches from a free camera that never moves, and everything a moving camera drags in
 * behind it - the terrain window scrolling, the shroud, models and textures loading the first time
 * they come on screen - is invisible to a run that stares at one spot.  That is exactly the blind
 * spot a stutter likes to live in, so the measurement has to move the camera itself.
 *
 * "Where the fighting is" is decided the cheapest way that actually works: a coarse grid over the
 * map, one tally pass over every playable player's units, and the cell holding units from the most
 * sides wins.  A cell with two armies in it beats a cell with a bigger single army, which is the
 * difference between watching a battle and watching a base.  One walk of the object lists every n
 * seconds costs nothing next to the logic frame it rides on.
 */
enum { AUTOCAM_GRID = 16 };
static Int s_autoCamUnits[ AUTOCAM_GRID * AUTOCAM_GRID ];
static UnsignedInt s_autoCamSides[ AUTOCAM_GRID * AUTOCAM_GRID ];

struct AutoCamTally
{
	UnsignedInt sideBit;
	Real originX, originY, spanX, spanY;
};

static void autoCameraTallyObject( Object *obj, void *userData )
{
	AutoCamTally *t = (AutoCamTally *)userData;
	if( obj == NULL )
		return;
	// Units, not buildings: a base sits still and is not what anyone would look at.
	if( !obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_VEHICLE ) &&
			!obj->isKindOf( KINDOF_AIRCRAFT ) )
		return;

	const Coord3D *pos = obj->getPosition();
	Int cx = (Int)(( pos->x - t->originX ) / t->spanX * AUTOCAM_GRID);
	Int cy = (Int)(( pos->y - t->originY ) / t->spanY * AUTOCAM_GRID);
	if( cx < 0 ) cx = 0;
	if( cy < 0 ) cy = 0;
	if( cx >= AUTOCAM_GRID ) cx = AUTOCAM_GRID - 1;
	if( cy >= AUTOCAM_GRID ) cy = AUTOCAM_GRID - 1;

	const Int cell = cy * AUTOCAM_GRID + cx;
	++s_autoCamUnits[ cell ];
	s_autoCamSides[ cell ] |= t->sideBit;
}

static Int autoCameraCountBits( UnsignedInt v )
{
	Int n = 0;
	while( v ) { n += (v & 1); v >>= 1; }
	return n;
}

/** -----------------------------------------------------------------------------------------------
 * -camera <x> <y>: point the camera at one map position, once, when the match starts.
 *
 * Applied on the first in-game frame rather than at parse time: TheTacticalView does not exist yet
 * while the command line is read, and the map's own starting view is set during load and would win.
 */
static void updateFixedCamera( void )
{
	if( !TheGlobalData->m_cameraLookSet || TheTacticalView == NULL || TheTerrainLogic == NULL )
		return;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	Coord3D look;
	look.x = TheGlobalData->m_cameraLook.x;
	look.y = TheGlobalData->m_cameraLook.y;
	look.z = TheTerrainLogic->getGroundHeight( look.x, look.y );
	TheTacticalView->lookAt( &look );

	// held every frame, not set once: the map's own starting view and the shell's reset both land
	// after the first in-game frame and put the camera back on the player's base.
	static Bool logged = FALSE;
	if( !logged )
	{
		logged = TRUE;
		DEBUG_LOG(("CAMERA: holding (%.0f,%.0f)\n", look.x, look.y));
	}
}

/** -----------------------------------------------------------------------------------------------
 * -uidrill <n>: minimise the command bar, put it back, and re-apply its scheme, over and over.
 *
 * The bar drifted up the screen a few pixels at a time and only under a sequence no unattended run
 * performs: a panel that is away when its layout is rebuilt.  Nothing the AI does touches the bar,
 * -scenario gives orders and never opens a menu, and the drift is small enough per rebuild that
 * one cycle by hand looks like nothing.  So the drill does the two halves on alternate ticks - a
 * toggle, then a rebuild once the slide has arrived - and writes where the bar landed each time.
 * A run that holds one pair of numbers for its whole life is a bar that stays put; a run whose
 * numbers walk is the bug.
 */
static void updateUIDrill( void )
{
	if( TheGlobalData->m_uiDrill <= 0 || TheControlBar == NULL || TheGameLogic == NULL )
		return;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	// once per logic frame, not once per render frame: the renderer is uncapped and would run the
	// whole cycle several times over on the one frame it is due
	static UnsignedInt lastTickFrame = 0xffffffff;
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame < (UnsignedInt)TheGlobalData->m_uiDrill || frame == lastTickFrame )
		return;
	if( ( frame % (UnsignedInt)TheGlobalData->m_uiDrill ) != 0 )
		return;
	lastTickFrame = frame;

	static Int tick = 0;
	const Bool toggling = ( ( tick++ & 1 ) == 0 );

	if( toggling )
	{
		TheControlBar->toggleControlBarStage();
	}
	else if( ThePlayerList )
	{
		// what setControlBarSchemeByPlayer reaches: ControlBarScheme::init, which calls layoutPanels
		TheControlBar->setControlBarSchemeByPlayer( ThePlayerList->getLocalPlayer() );
	}

	ControlBar_logPlacement( toggling ? "toggle" : "relayout", (Int)frame );
}

/** -----------------------------------------------------------------------------------------------
 * -resdrill <frame> [w] [h]: change the resolution from inside a running match.
 *
 * The options menu is the only way a player reaches this, and a script cannot open a menu.  What
 * happens after the device resets - the shell thrown away and rebuilt, the command bar built again
 * while the match that owns it is still running - is where it crashed, and the whole sequence lives
 * in OptionsMenu.cpp so that this drill runs the real one rather than a copy of it.
 */
/* The rebuild frees the CommandButton table and builds a new one.  Nothing dereferences the old
	 entries unless the bar is showing a command set, and the bar shows one only while something is
	 selected - so a drill that selects nothing walks straight past the defect this is here to catch.
	 Pick a structure: its command set is the one with production buttons on it. */
static void resDrillSelectObject( Object *obj, void *userData )
{
	Drawable **found = (Drawable **)userData;
	if( *found != NULL || obj == NULL || !obj->isKindOf( KINDOF_STRUCTURE ) )
		return;
	*found = obj->getDrawable();
}

static void updateResDrill( void )
{
	if( TheGlobalData->m_resDrillFrame <= 0 || TheGameLogic == NULL )
		return;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;
	if( TheGameLogic->getFrame() < (UnsignedInt)TheGlobalData->m_resDrillFrame )
		return;

	/* Two stages, two seconds apart.  The first presses Accept, which leaves the "keep this
		 resolution?" box on screen; the second answers it with Cancel, which is DeclineResolution -
		 the mode goes back and the shell and the command bar are built a second time, from inside the
		 box's own handler, over what the first rebuild left.  Answering in the same breath as Accept
		 would be a sequence no player can perform.

		 The wait is on the wall clock and not on logic frames because stage one opens the quit menu,
		 the way a player reaches the options screen in a match, and that menu pauses the game.  A
		 paused single-player game runs no logic at all, so a frame count here would never come due. */
	const UnsignedInt RES_DRILL_DISMISS_DELAY_MS = 2000;

	static Bool applied = FALSE;
	static Bool dismissed = FALSE;
	static UnsignedInt appliedTimeMs = 0;

	if( applied )
	{
		if( dismissed || Clock_Milliseconds() - appliedTimeMs < RES_DRILL_DISMISS_DELAY_MS )
			return;
		dismissed = TRUE;
		ResolutionDrillDismiss( TheGlobalData->m_resDrillKeep );
		return;
	}
	applied = TRUE;
	appliedTimeMs = Clock_Milliseconds();

	if( TheGlobalData->m_headless )
	{
		// a headless run has a 100x100 device it never draws to; resetting it proves nothing
		DEBUG_LOG(("RESDRILL: refused, this run is headless\n"));
		dismissed = TRUE;
		return;
	}

	Drawable *pick = NULL;
	if( ThePlayerList && TheInGameUI )
	{
		Player *local = ThePlayerList->getLocalPlayer();
		if( local )
			local->iterateObjects( resDrillSelectObject, &pick );
		if( pick )
		{
			TheInGameUI->deselectAllDrawables();
			TheInGameUI->selectDrawable( pick );
			DEBUG_LOG(("RESDRILL: selected drawable %d so the bar is showing a command set\n",
				(Int)pick->getID()));
		}
		else
		{
			DEBUG_LOG(("RESDRILL: nothing to select, the bar will be empty\n"));
		}
	}

	ResolutionDrillApply( TheGlobalData->m_resDrillX, TheGlobalData->m_resDrillY );
}

/** -----------------------------------------------------------------------------------------------
 * The two switches that decide whether the game answers the player at all.
 *
 * A paused match runs no logic: production stops, units stand still, the camera keys do nothing.
 * A disabled UI is the other half - WindowXlat hands every mouse and key message to the window
 * system first and only then marks it used, so the menus go on answering clicks while nothing in
 * the game moves.  Together they are the state a player describes as "the interface works but the
 * game does not", and neither of them shows up in any other line of the log.
 *
 * Written down whenever either one moves, because the player who meets this is not the person who
 * can reproduce it on demand with a switch turned on.
 */
static void updateInputWatch( void )
{
	static Int lastInputEnabled = -1;
	static Int lastPaused = -1;

	if( TheInGameUI == NULL || TheGameLogic == NULL )
		return;

	static Int lastShellActive = -1;

	const Int inputEnabled = TheInGameUI->getInputEnabled() ? 1 : 0;
	const Int paused = TheGameLogic->isGamePaused() ? 1 : 0;
	const Int shellActive = ( TheShell != NULL && TheShell->isShellActive() ) ? 1 : 0;

	if( inputEnabled == lastInputEnabled && paused == lastPaused && shellActive == lastShellActive )
		return;

	lastInputEnabled = inputEnabled;
	lastPaused = paused;
	lastShellActive = shellActive;

	DEBUG_LOG(("INPUTWATCH: frame %d, input enabled %d, game paused %d, shell active %d, quit menu %d, in game %d\n",
		TheGameLogic->getFrame(), inputEnabled, paused, shellActive,
		TheInGameUI->isQuitMenuVisible() ? 1 : 0,
		( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() ) ? 1 : 0));
}

static void updateAutoCamera( void )
{
	if( TheGlobalData->m_autoCameraSeconds <= 0 || TheTacticalView == NULL || TheTerrainLogic == NULL )
		return;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	/* This runs once per *render* frame, and the renderer is uncapped - so a plain modulo of the
		 logic frame fires ten times over on the one frame it is due, each time walking every object
		 list in the game.  Remember the frame it last acted on instead. */
	static UnsignedInt lastMoveFrame = 0xffffffff;
	const UnsignedInt frame = TheGameLogic->getFrame();
	const UnsignedInt period = (UnsignedInt)TheGlobalData->m_autoCameraSeconds * LOGICFRAMES_PER_SECOND;
	if( frame == lastMoveFrame || (frame % period) != 0 )
		return;
	lastMoveFrame = frame;

	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	AutoCamTally tally;
	tally.originX = extent.lo.x;
	tally.originY = extent.lo.y;
	tally.spanX = extent.width();
	tally.spanY = extent.height();
	if( tally.spanX <= 1.0f || tally.spanY <= 1.0f )
		return;						// no map to speak of

	for( Int i = 0; i < AUTOCAM_GRID * AUTOCAM_GRID; ++i )
	{
		s_autoCamUnits[ i ] = 0;
		s_autoCamSides[ i ] = 0;
	}

	for( Int p = 0; p < ThePlayerList->getPlayerCount() && p < MAX_PLAYER_COUNT; ++p )
	{
		Player *player = ThePlayerList->getNthPlayer( p );
		if( !player->isPlayableSide() || player->isPlayerObserver() )
			continue;
		tally.sideBit = (UnsignedInt)1 << p;
		player->iterateObjects( autoCameraTallyObject, &tally );
	}

	/* Contested first, crowded second.  Squaring the number of sides present is enough to make any
		 two-sided cell outrank any one-sided one at realistic army sizes, without a special case. */
	Int bestCell = -1;
	Int bestScore = 0;
	for( Int c = 0; c < AUTOCAM_GRID * AUTOCAM_GRID; ++c )
	{
		if( s_autoCamUnits[ c ] == 0 )
			continue;
		const Int sides = autoCameraCountBits( s_autoCamSides[ c ] );
		const Int score = s_autoCamUnits[ c ] * sides * sides;
		if( score > bestScore )
		{
			bestScore = score;
			bestCell = c;
		}
	}
	if( bestCell < 0 )
		return;						// nobody has a unit anywhere; leave the camera alone

	Coord3D look;
	look.x = tally.originX + ( (bestCell % AUTOCAM_GRID) + 0.5f ) * tally.spanX / AUTOCAM_GRID;
	look.y = tally.originY + ( (bestCell / AUTOCAM_GRID) + 0.5f ) * tally.spanY / AUTOCAM_GRID;
	look.z = TheTerrainLogic->getGroundHeight( look.x, look.y );
	TheTacticalView->lookAt( &look );

	DEBUG_LOG(("AUTOCAMERA: frame %d -> (%.0f,%.0f), %d units from %d sides\n",
						 frame, look.x, look.y,
						 s_autoCamUnits[ bestCell ], autoCameraCountBits( s_autoCamSides[ bestCell ] )));
}

/** -----------------------------------------------------------------------------------------------
 * Why an unattended run is over, or NULL while it is still going.  The string is what the log line
 * says, so the reason and the report of it cannot drift apart.
 *
 * VictoryConditions publishes the end frame for a free-camera observer too, so nothing here
 * re-derives who won.  It has one trap: 'not decided yet' and 'decided on frame 0' are the same
 * stored zero, and the check starts running before a map has finished placing its objects - at
 * which point every player owns nothing and therefore looks eliminated.  Requiring the end frame to
 * be past the first second is what keeps an unpopulated map from reading as an instant draw.
 */
const char *GameEngine_headlessRunResult( UnsignedInt frame, UnsignedInt victoryEndFrame, Int maxGameFrames )
{
	const UnsignedInt SETTLE_FRAMES = 30;
	if (victoryEndFrame > SETTLE_FRAMES)
		return "decided";
	if (maxGameFrames > 0 && frame >= (UnsignedInt)maxGameFrames)
		return "frame limit reached";
	return NULL;
}

extern void AIUpdate_resetMoveTrace( void );	///< -tracemove: forget the unit the last match followed

/** -----------------------------------------------------------------------------------------------
 * -maxframes' own frame.  One engine pass can run several logic frames to catch up with the clock, and
 * the end of an unattended run is only looked at after the pass, so under load a run could end a frame
 * or two past its limit: two copies of a network game stopped on 1801 and 1802 and were compared
 * there, which can only disagree.  So the catch-up stops on the limit's frame, and that frame's CRC is
 * logged the moment the logic finishes it ("HEADLESS CRC AT LIMIT"), whatever happens after.
 *
 * ZH_TEST_FRAME_LIMIT_OVERSHOOT=<n>, a test's switch and never a player's: the run goes on n frames
 * past the limit, so a harness can show that it compares at the limit however far a run went.
 */
static Int frameLimitOvershoot( void )
{
	static Int overshoot = -1;
	if (overshoot < 0)
	{
		const char *value = getenv( "ZH_TEST_FRAME_LIMIT_OVERSHOOT" );
		overshoot = value != NULL ? atoi( value ) : 0;
		if (overshoot < 0)
			overshoot = 0;
	}
	return overshoot;
}

/// TRUE once the logic has finished -maxframes' frame; on that frame itself, its CRC goes to the log
static Bool noteFrameLimit( void )
{
	static Bool logged = FALSE;
	const Int limit = TheGlobalData->m_maxGameFrames;
	if (limit <= 0 || TheGameLogic->getFrame() < (UnsignedInt)limit)
		return FALSE;
	if (!logged && TheGameLogic->getFrame() == (UnsignedInt)limit)
	{
		logged = TRUE;
		DEBUG_LOG(("HEADLESS CRC AT LIMIT: 0x%08X at frame %d\n", TheGameLogic->getCRC( CRC_RECALC ), limit));
	}
	return TRUE;
}

/** -----------------------------------------------------------------------------------------------
 * -headless: decide whether the unattended run is finished, and if it is, write down how it went
 * and quit.  Two ways to finish: the match is decided, or -maxframes ran out.
 */
/** -----------------------------------------------------------------------------------------------
 * -screenshot <n> when there is no match to count frames of: photograph a menu.
 *
 * The unattended block below counts logic frames, and a run sitting in the shell has none - the
 * logic frame counter stays where the last game left it, which for -skirmishlobby is zero, so the
 * request never fires and a layout change to a menu could not be looked at from a script.  Here n
 * counts engine passes instead, which is a duration rather than a frame number: at a vsynced 60Hz
 * 200 is about three seconds, long enough for the shell to build the screen.
 *
 * -autoskirmish runs pass through the shell on their way into the match, so this stands down for
 * them: their n means the frame of the match, not the wait for it.
 *
 * Counting starts when the shell has a screen on its stack.  The passes before that are the intro
 * and the first load, which took 1500 of them here, and a picture taken during them is of nothing.
 */
static void updateShellScreenShot( void )
{
	if (TheGlobalData->m_screenShotFrame <= 0 || TheGlobalData->m_headless ||
			TheGlobalData->m_autoSkirmishPlayers > 0 || TheDisplay == NULL)
		return;
	if (TheGameLogic != NULL && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		return;
	if (TheShell == NULL || TheShell->getScreenCount() <= 0)
		return;

	static UnsignedInt shellPasses = 0;
	static Bool shotTaken = FALSE;
	if (shotTaken)
		return;

	if (++shellPasses >= (UnsignedInt)TheGlobalData->m_screenShotFrame)
	{
		shotTaken = TRUE;
		TheDisplay->takeScreenShot();
		DEBUG_LOG(("-screenshot: asked for one of the shell after %d passes\n", shellPasses));
	}
}

static void updateHeadlessRun( void )
{
	/* -autoskirmish counts as unattended even when it draws.  THREADING-ROADMAP.md section 0 step 4
		 asks for a heavy scenario under a real renderer, and a run that never ends and never writes
		 its numbers down is not a measurement.  This still cannot fire on a game a person started:
		 reaching here needs -headless, -autoskirmish or -netgame, and none of the three is on a menu.

		 -netgame is on that list for the same reason and one of its own: a network game that draws
		 is the only way to photograph anything that only exists between two machines, and until it
		 was here that run had to be -headless to end by itself, which is to say it could not be
		 photographed at all. */
	const Bool unattended = TheGlobalData->m_headless || TheGlobalData->m_autoSkirmishPlayers > 0 ||
													!TheGlobalData->m_netGameHosts.isEmpty() || TheGlobalData->m_directorRecord;

	/* Except that a run with a control socket open is not unattended at all - somebody is driving
		 it from the other end, and tearing the process down the moment a match is decided takes the
		 socket with it.  Whoever is driving says when it ends, by sending "quit". */
	if (TheGlobalData->m_controlPort > 0)
		return;

	/* -directorrecord films one match.  A replay that runs out, or a match that ends some other way
		 than a decision, goes back to the shell, and the run ends there; the display's teardown
		 finishes the movie. */
	static Bool directorRecordSawMatch = FALSE;
	const Bool scouting = !TheGlobalData->m_directorScoutFile.isEmpty();
	if ((TheGlobalData->m_directorRecord || scouting) && !TheGameEngine->getQuitting())
	{
		const Bool inMatch = TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
		if (inMatch)
		{
			directorRecordSawMatch = TRUE;
		}
		else if (directorRecordSawMatch)
		{
			DEBUG_LOG(("-directorrecord: the match is over, quitting\n"));
			if (scouting)
				TheObserverCamera.finishScout();
			TheGameEngine->setQuitting( TRUE );
			return;
		}
	}

	if (!unattended || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return;

	static UnsignedInt runStartTime = 0;
	static UnsignedInt runStartFrame = 0;
	static Int peakUnits[ MAX_PLAYER_COUNT ];
	if (runStartTime == 0)
	{
		runStartTime = Clock_Milliseconds();
		runStartFrame = TheGameLogic->getFrame();
		for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
			peakUnits[ i ] = 0;
		// the shell map and the load did their own pathing; the run's numbers start here
		Pathfinder::resetMatchProfile();
		// and -tracemove with no id picks its unit out of the match, not out of the shell map
		AIUpdate_resetMoveTrace();
		if (TheGlobalData->m_headless && TheGlobalData->m_videoEndFrame > 0)
			DEBUG_LOG(("-video: -headless draws nothing, so there is no video to record\n"));
	}

	const UnsignedInt frame = TheGameLogic->getFrame();

	// -directorrecord's scouting pass: a headless run of the same match counting its fights
	if (scouting)
		TheObserverCamera.scout();

	/* -screenshot <n>: hand the renderer a shot request as the run passes frame n. W3DDisplay only
		 sets a pending flag here and writes the file out of the back buffer on its next draw, so this
		 does something only in a run that is drawing - -autoskirmish without -headless. It is the
		 only way to see what a renderer change did without somebody sitting in front of the machine,
		 which is what half the remaining graphics work needs. The file lands next to the save games
		 as sshotNNN.bmp. */
	static UnsignedInt screenShotRequestedOn = 0;
	if (TheGlobalData->m_screenShotFrame > 0 && screenShotRequestedOn == 0 &&
			frame >= (UnsignedInt)TheGlobalData->m_screenShotFrame)
	{
		screenShotRequestedOn = frame;
		if (TheGlobalData->m_headless)
		{
			DEBUG_LOG(("-screenshot: -headless draws nothing, so there is no picture to save\n"));
		}
		else if (TheDisplay != NULL)
		{
			TheDisplay->takeScreenShot();
			DEBUG_LOG(("-screenshot: asked for one at frame %d\n", frame));
		}
	}

	/* The frame times start a second in, not at frame 0.  The first counted pass carries the tail
		 of the map load and came out at two full seconds, which then *is* the worst frame of the
		 match and buries the 60ms hitch that somebody could actually do something about. */
	if( !theFrameTimesStarted && frame > runStartFrame + LOGICFRAMES_PER_SECOND )
		startFrameTimeStats();

	/* The biggest army each side ever fielded, sampled once a second.  The end-of-match counts are
		 cumulative and cannot tell an AI that massed and traded evenly from one that trickled units
		 out and lost them one at a time.  Once a second is one walk of each player's object list per
		 second, which is nothing next to the logic frame it rides on. */
	if ((frame % LOGICFRAMES_PER_SECOND) == 0)
	{
		KindOfMaskType nothing;
		nothing.clear();
		for( Int i = 0; i < ThePlayerList->getPlayerCount() && i < MAX_PLAYER_COUNT; ++i )
		{
			Player *p = ThePlayerList->getNthPlayer( i );
			if (!p->isPlayableSide() || p->isPlayerObserver())
				continue;
			Int units = p->countObjects( MAKE_KINDOF_MASK( KINDOF_INFANTRY ), nothing ) +
									p->countObjects( MAKE_KINDOF_MASK( KINDOF_VEHICLE ), nothing ) +
									p->countObjects( MAKE_KINDOF_MASK( KINDOF_AIRCRAFT ), nothing );
			if (units > peakUnits[ i ])
				peakUnits[ i ] = units;
		}
	}

	/* A -video range that runs up to or past -maxframes keeps the run alive until its last picture is
		 drawn, which happens on the pass after the logic reaches that frame. */
	Int maxGameFrames = TheGlobalData->m_maxGameFrames;
	if (maxGameFrames > 0 && !TheGlobalData->m_headless && !TheGlobalData->m_directorRecord
			&& TheGlobalData->m_videoEndFrame >= maxGameFrames)
		maxGameFrames = TheGlobalData->m_videoEndFrame + 1;
	if (maxGameFrames > 0)
		maxGameFrames += frameLimitOvershoot();		// a test's overshoot: 0 for every real run

	const char *why = GameEngine_headlessRunResult( frame, TheVictoryConditions->getEndFrame(),
																									maxGameFrames );
	if (why == NULL)
		return;

	/* -directorrecord films on past the decision, so the last player's defeat and the winner's banner
		 are in the movie: it used to stop on the frame the last card fell.  Not past the game's own end,
		 though: the multiplayer scripts' end timer clears the match FRAMES_TO_SHOW_WIN_LOSE_MESSAGE (120)
		 frames after it is decided and drops to the shell, which would leave without these lines.  The
		 decision's own CRC is logged on its frame, the one the scouting pass closed on. */
	static UnsignedInt filmDecidedOn = 0;
	const UnsignedInt FILM_PAST_DECISION_FRAMES = 105;
	if (TheGlobalData->m_directorRecord && strcmp( why, "decided" ) == 0)
	{
		if (filmDecidedOn == 0)
		{
			filmDecidedOn = frame;
			DEBUG_LOG(("-directorrecord: decided on frame %d, CRC 0x%08X, filming %u frames more\n", frame,
								 TheGameLogic->getCRC( CRC_RECALC ), FILM_PAST_DECISION_FRAMES));
		}
		if (frame < filmDecidedOn + FILM_PAST_DECISION_FRAMES)
			return;
	}

	const UnsignedInt wallMs = Clock_Milliseconds() - runStartTime;
	const Real logicFps = wallMs ? (Real)(frame - runStartFrame) * 1000.0f / (Real)wallMs : 0.0f;

	DEBUG_LOG(("HEADLESS RESULT: %s on frame %d (%d frames in %.1fs wall, %.0f logic fps, %.1fx real time)\n",
						 why, frame,
						 frame - runStartFrame, wallMs / 1000.0f, logicFps,
						 logicFps / (Real)TheGameEngine->getFramesPerSecondLimit()));
	//
	// Client bookkeeping an unattended run still has to do: nothing draws, so nothing was retiring
	// particle systems and the count climbed all run (see GameClient::update).  Print it, because a
	// number that is supposed to stay flat is only worth anything if somebody would notice it move.
	//
	DEBUG_LOG(("HEADLESS PARTICLES: %d systems, %d particles\n",
						 TheParticleSystemManager ? TheParticleSystemManager->getParticleSystemCount() : -1,
						 TheParticleSystemManager ? TheParticleSystemManager->getParticleCount() : -1));

	/* The state of every object in the world, in one number, at the frame the run stopped.  Every
		 movement change in PATHFINDING-PLAN.md edits GameLogic, and a change that plays beautifully
		 and desyncs multiplayer is a change that has to be found before it ships, not after somebody
		 fails to join.  Record a fixed-seed match, play its replay back, compare this line: same
		 frame and same CRC means the replay took the same path through the logic, which is the same
		 property a second machine in a network game needs.  One walk of the object list, once, at
		 the end of a run nobody is watching.

		 It is deliberately the recalculated CRC and not the cached one - the cached value is only
		 refreshed on frames a network game asks for it, and a skirmish never asks. */
	DEBUG_LOG(("HEADLESS CRC: 0x%08X at frame %d\n", TheGameLogic->getCRC( CRC_RECALC ), frame));

	// What the pathfinder did over the whole match, not just the one frame that ran over budget.
	// A pathing change is argued with these: search count and time say what it cost, `nopath` and
	// `outofcells` say whether it broke anything, `blocked`/`stuck` say whether it actually
	// reduced the traffic jams it was supposed to reduce.
	DEBUG_LOG(("HEADLESS PATHFIND: %s\n", Pathfinder::getMatchProfileReport()));

	/* And under -scenario, how much of the file actually happened.  A scenario whose spawns all
		 failed still plays a match and still writes every number below it, so the run has to say out
		 loud how many orders it managed rather than leaving that to be inferred from the frame time. */
	if (!TheGlobalData->m_scenarioFile.isEmpty())
	{
		DEBUG_LOG(("HEADLESS SCENARIO: %s\n", ScenarioDrill_report()));
		ScenarioDrill_logArrivals();
	}

	/* Stability, which is a different question from speed and is answered by the tail rather than
		 by the average.  These two lines are the ones a stutter complaint is argued with. */
	reportFrameTimeStats();

	/* THREADING-ROADMAP.md 3.1's safety net: "no allocation inside a job" is a rule, and a rule
		 nothing checks is a comment.  allocs must read 0 - anything else means a job took the one
		 global pool lock and serialized the fork it was supposed to spread. */
	DEBUG_LOG(("HEADLESS JOBS: %d worker threads, %d allocations from a job\n",
						 JobSystem::workerCount(), JobSystem::workerAllocationCount()));

	// THREADING-ROADMAP.md section 0 step 3.  Only a PERF_TIMERS build has anything to say here.
#ifdef PERF_TIMERS
	PerfGather::dumpSummary();
#endif

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		// The list also carries the civilian and neutral slots and, with -observer, the camera the
		// run is watched from.  None of them plays, so none of them has a result worth a line.
		if (!p->isPlayableSide() || p->isPlayerObserver())
			continue;

		// The lobby seat and its -teams block, so a batch can add a team's result up without guessing
		// which player index came from which slot.  A player no slot built has neither.
		const Int slot = ThePlayerList->getSlotIndex( i );
		const Int team = (slot >= 0 && TheGameInfo) ? TheGameInfo->getConstSlot( slot )->getTeamNumber() : -1;

		ScoreKeeper *score = p->getScoreKeeper();
		DEBUG_LOG(("HEADLESS PLAYER %d '%s': %s | score %d | money %d earned %d spent | units %d built %d lost %d killed peak %d | buildings %d built %d lost | slot %d team %d\n",
							 i, WideCharAsUtf8( p->getPlayerDisplayName().str() ).str(),
							 TheVictoryConditions->hasAchievedVictory(p) ? "WON" :
								 (TheVictoryConditions->hasSinglePlayerBeenDefeated(p) ? "eliminated" : "alive"),
							 score->calculateScore(),
							 score->getTotalMoneyEarned(), score->getTotalMoneySpent(),
							 score->getTotalUnitsBuilt(), score->getTotalUnitsLost(), score->getTotalUnitsDestroyed(),
							 peakUnits[ i ],
							 score->getTotalBuildingsBuilt(), score->getTotalBuildingsLost(),
							 slot, team));
		// the exchange in money, on a line of its own so the PLAYER line's readers keep matching it
		DEBUG_LOG(("HEADLESS VALUE %d: units worth %d lost, %d killed\n", i,
							 score->getUnitValueLost(), score->getUnitValueDestroyed()));
	}

	if (scouting)
		TheObserverCamera.finishScout();

	/* Tear the match down the way the benchmark timer does, so the replay of the run is closed and
		 written rather than left half-flushed by the process going away. */
	if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		TheRecorder->stopRecording();
	/* And without the score screen.  Nobody is sitting in front of an unattended run to read one,
		 and building it crashes the teardown: clearGameData pushes Menus/ScoreScreen.wnd, whose init
		 walks the players and calls TheGameInfo->isSandbox(), and TheGameInfo is gone by then. The
		 crash lands after every HEADLESS line is written, so the numbers of a run were never wrong -
		 the process just died on its way out and handed a script exit code 1 either way. */
	TheGameLogic->clearGameData( FALSE );
	TheGameEngine->setQuitting( TRUE );
}

void GameEngine::update( void )
{
	USE_PERF_TIMER(GameEngine_update)
	sampleLogicRate();
	{
#ifdef DEBUG_LOGGING
		static Int fpsFrames = 0;
		static Real fpsClientTotal = 0.0f, fpsClientMax = 0.0f;
		static Real fpsLogicTotal = 0.0f, fpsLogicMax = 0.0f;
		static Int fpsLogicTicks = 0, fpsCatchupPasses = 0;
		static UnsignedInt fpsWindowStart = Clock_Milliseconds();
		static Real fpsRadarTotal = 0.0f, fpsAudioTotal = 0.0f, fpsDrawTotal = 0.0f, fpsDrawMax = 0.0f;
		static Real fpsSceneTotal = 0.0f, fpsUITotal = 0.0f, fpsPostTotal = 0.0f, fpsWinTotal = 0.0f;
		static Real fpsStripGatherTotal = 0.0f, fpsStripDrawTotal = 0.0f;
		static Real fpsShadowTotal = 0.0f, fpsFillTotal = 0.0f, fpsTranslucentTotal = 0.0f;
		static Real fpsPostChainTotal = 0.0f, fpsIconTotal = 0.0f;
		static Real fpsSortTotal = 0.0f, fpsCopyTotal = 0.0f, fpsSortDrawTotal = 0.0f;
		static Int fpsCasterTotal = 0;
		static UnsignedInt fpsShadowDrawTotal = 0, fpsSceneDrawTotal = 0, fpsTranslucentDrawTotal = 0;
		static UnsignedInt fpsSortEntryTotal = 0, fpsOnScreenTotal = 0;
		Int64 tClientStart, tClientEnd, tLogicStart, tLogicEnd, tRadarEnd, tAudioEnd;
		Real clientMS = 0.0f, logicMS = 0.0f, radarMS = 0.0f, audioMS = 0.0f;
		Int logicTicks = 0;
		TheClientDrawMS = TheSceneDrawMS = TheUIDrawMS = TheUIPostDrawMS = TheWindowRepaintMS = 0.0f;
		TheStripGatherMS = TheStripDrawMS = 0.0f;
		TheParticleUpdateMS = TheParticleFillMS = TheTranslucentMS = 0.0f;
		TheTranslucentDraws = TheSortingPolygonsRefused = TheParticlesPastGroupLimit = 0;
		TheShadowMapMS = ThePostChainMS = TheIconDrawMS = 0.0f;
		TheSortingSortMS = TheSortingCopyMS = TheSortingDrawMS = 0.0f;
		TheSortingEntries = 0;
		TheShadowMapCasters = 0;
		TheShadowMapDraws = TheSceneDrawCalls = 0;
		tClientStart = Clock_Ticks();
#endif

		{

			// VERIFY CRC needs to be in this code block.  Please to not pull TheGameLogic->update() inside this block.
			VERIFY_CRC

			TheRadar->UPDATE();
#ifdef DEBUG_LOGGING
			tRadarEnd = Clock_Ticks();
#endif

			/// @todo Move audio init, update, etc, into GameClient update

			TheAudio->UPDATE();
#ifdef DEBUG_LOGGING
			tAudioEnd = Clock_Ticks();
#endif
			TheGameClient->UPDATE();
			if (TheGlobalData->m_drawDelayMS > 0)
			{
				// The jitter is the performance counter's low bits: client side, and no random stream
				// either half of the game draws from is touched.
				Int64 now;
				now = Clock_Ticks();
				const Int jitter = TheGlobalData->m_drawDelayJitterMS > 0
					? (Int)( now % ( TheGlobalData->m_drawDelayJitterMS + 1 ) ) : 0;
				sleepMilliseconds( TheGlobalData->m_drawDelayMS + jitter );
			}
			TheMessageStream->propagateMessages();

			if (TheNetwork != NULL)
			{
				TheNetwork->UPDATE();
			}

			TheCDManager->UPDATE();

			// Reads the command bar and the local player, writes a keyboard frame
			// for the Chroma worker to pick up.  Client only, never touches logic.
			updateChromaKeyboard();
		}
#ifdef DEBUG_LOGGING
		tClientEnd = Clock_Ticks();
		clientMS = engineElapsedMS( tClientStart, tClientEnd );
		radarMS = engineElapsedMS( tClientStart, tRadarEnd );
		audioMS = engineElapsedMS( tRadarEnd, tAudioEnd );
#endif


		// Fast-forward modes run a logic frame on every call; otherwise logic ticks at
		// m_maxFPS Hz of wall clock, however fast the client/renderer above is running.
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		Bool fastMode = TheGlobalData->m_TiVOFastMode;
#else
		Bool fastMode = TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame();
#endif
		fastMode = fastMode || TheTacticalView->getTimeMultiplier() > 1 || TheScriptEngine->isTimeFast();

		/* -headless has no picture to pace the tick against and nobody watching it go by, so it runs
			 a logic frame every pass and the match plays out as fast as the machine manages.  Same
			 branch as fast-forward, different reason: this one is not a cheat, it is the whole point
			 of an unattended run. */
		fastMode = fastMode || TheGlobalData->m_headless;

		/* -turbo: the same for a run that draws, unless a network owns the clock.  A -wav recording does
			 not: its mix moves with the logic frames, not the wall clock (updateSoundCapture).  The five seconds before a -screenshot run at the real rate.  Taken straight out of
			 fast-forward, a shot differed from its own repeat on 5.6% of the pixels; settled first, on
			 0.01%, where two paced runs differ on 0.1%. */
		const Int TURBO_SETTLE_FRAMES = 150;
		const Int turboFrame = (Int)TheGameLogic->getFrame();
		const Bool turboSettling = TheGlobalData->m_screenShotFrame > 0
			&& turboFrame + TURBO_SETTLE_FRAMES >= TheGlobalData->m_screenShotFrame
			&& turboFrame <= TheGlobalData->m_screenShotFrame;
		fastMode = fastMode || ( TheGlobalData->m_turbo && !turboSettling && TheNetwork == NULL );

		/* -video: one logic frame a pass across the range being recorded, so the draw in front of each
			 logic frame is the one picture of it.  Paced by the wall clock, a pass that fell behind would
			 run two logic frames back to back and the video would jump over one. */
		const Int videoLogicFrame = (Int)TheGameLogic->getFrame();
		fastMode = fastMode || ( TheGlobalData->m_videoEndFrame > 0 && TheGameLogic->isInGame()
														 && !TheGameLogic->isInShellGame()
														 && videoLogicFrame + 1 >= TheGlobalData->m_videoStartFrame
														 && videoLogicFrame <= TheGlobalData->m_videoEndFrame );

		static UnsignedInt prevLogicTime = Clock_Milliseconds();
		static Real logicAccumMs = 0.0f;
		UnsignedInt now = Clock_Milliseconds();
		Real elapsedMs = (Real)(now - prevLogicTime);
		prevLogicTime = now;

		const Bool logicMayRun =
				((TheNetwork == NULL && !TheGameLogic->isGamePaused()) || (TheNetwork && TheNetwork->isFrameDataReady()));

		Bool logicFrameDue;
		Bool mayCatchUp = FALSE;
		const Bool networkPaced = TheNetwork != NULL && TheNetwork->isPacingLogicFrames();
		if (networkPaced)
		{
			// A network game already has a clock: Network::timeForNewFrame() paces the tick against
			// the negotiated frame rate and only then publishes the frame's commands.  Gating a
			// second time here against m_maxFPS beats two independent clocks against each other -
			// a frame needs both to say yes, so the effective rate settles *below* either one and
			// drifts, which is a systematic multiplayer-only slowdown.  Let the network own it and
			// keep the accumulator clean for when the game drops back to single player.
			//
			// It owns the debt as well.  Each logic frame this machine manages a second is what it
			// reports to the room, and the room runs at the slowest report, so a pass that ran one
			// logic frame per picture made a slow graphics card everybody's frame rate: 60ms of
			// drawing on one of two machines held both at 15 logic frames a second.  This one runs
			// ahead in this machine's own simulation by as many frames as the network has ready
			// and due, and headless is here too, since the network paces it all the same.
			logicAccumMs = 0.0f;
			logicFrameDue = TRUE;
			mayCatchUp = TRUE;
		}
		else if (fastMode)
		{
			logicAccumMs = 0.0f;
			// -recordfps: a recorded frame is drawn as many times as it has pictures, the logic waiting
			// for the last of them.  A paused game draws its last picture again
			if (logicMayRun)
				s_videoPicture = (s_videoPicture + 1) % GameEngine_videoPictures();
			logicFrameDue = s_videoPicture == 0;
		}
		else
		{
			logicFrameDue = GameEngine_isLogicFrameDue(logicAccumMs, elapsedMs, m_maxFPS);
			// Fast mode means exactly one logic frame per call, by its own definition.
			mayCatchUp = (m_maxFPS > 0);
		}

		if (!logicMayRun)
		{
			// A paused game, or one waiting on the network, is not falling behind - it is stopped.
			// Drop the debt so resuming does not open with a catch-up burst.
			logicAccumMs = 0.0f;
		}
		else if (logicFrameDue)
		{
#ifdef DEBUG_LOGGING
			tLogicStart = Clock_Ticks();
#endif
			// Pay off the wall clock's debt.  Every pass after the first is a logic frame this call
			// already owes, and it runs without a client pass in front of it: a render frame is what
			// gets dropped so that game speed stays put when the frame rate does not.  Both the loop
			// count and the pacer's own accumulator cap stop at LOGIC_CATCHUP_MAX_FRAMES, so a logic
			// frame that is itself over budget cannot pull the loop into a spiral.
			Int logicTicksThisPass = 0;
			const Int maxTicksThisPass = GameEngine_logicCatchupMaxFrames(
				networkPaced ? TheGlobalData->m_framesPerSecondLimit : m_maxFPS );
			/* Bounded by the clock as well as by the count - see LOGIC_CATCHUP_BUDGET_MS.  Three
				 25ms ticks back to back with no picture in between is the 113ms freeze; one of them
				 plus the render is a dropped frame nobody files a bug about. */
			Int64 tCatchupStart, tCatchupNow;
			tCatchupStart = Clock_Ticks();
			const UnsignedInt frameBeforeTicks = TheGameLogic->getFrame();
			for( ;; )
			{
				TheGameLogic->UPDATE();
				++logicTicksThisPass;

				// -maxframes' frame ends the burst, its CRC logged as it finishes (noteFrameLimit)
				if (noteFrameLimit() && frameLimitOvershoot() == 0)
					break;

				if (!mayCatchUp)
					break;
				tCatchupNow = Clock_Ticks();
				if (!GameEngine_mayStartAnotherCatchupTick( logicTicksThisPass, maxTicksThisPass,
																									 engineElapsedMS( tCatchupStart, tCatchupNow ) ))
					break;
				if (networkPaced)
				{
					/* The frame just run may have ended the match, and clearGameData takes the network
						 down with it. */
					if (TheNetwork == NULL)
						break;
					/* A scripted camera move holds the logic for as many updates as the camera takes to
						 finish (freezeTime in GameLogic::update), and the camera only moves on a client
						 pass.  One update a pass is what every machine used to run against it; running
						 several here would make the count depend on how fast this machine draws. */
					if (TheTacticalView->isTimeFrozen())
						break;
					/* What the frame just posted (its CRC, every frame while DEBUG_CRC is on, or a group
						 selection) is sent the way a client pass would send it, before the next frame
						 runs.  The network stamps it with the current logic frame plus the run-ahead, and
						 a machine running one frame a pass stamps it with the frame after the one that
						 posted it; sent a frame later, this machine's CRC would be compared against the
						 others' on another frame and read as a desync.  Asked last, because it has the
						 side effects: it sends and receives, spends a frame of the network clock, and
						 puts the next frame's commands on the list. */
					TheMessageStream->propagateMessages();
					TheNetwork->UPDATE();
					if (!TheNetwork->isFrameDataReady())
						break;
				}
				// Asked last, because it is the one with a side effect: it spends the debt it reports.
				else if (!GameEngine_isLogicFrameDue(logicAccumMs, 0.0f, m_maxFPS))
					break;
			}
			GameEngine_noteLogicTickDone( networkPaced ? TheGlobalData->m_framesPerSecondLimit : m_maxFPS, fastMode );
			// a frame held by a scripted freeze keeps showing its last picture, rather than going back
			// half a frame every other pass
			if (TheGameLogic->getFrame() == frameBeforeTicks)
				s_videoPicture = GameEngine_videoPictures() - 1;
#ifdef DEBUG_LOGGING
			tLogicEnd = Clock_Ticks();
			logicMS = engineElapsedMS( tLogicStart, tLogicEnd );
			logicTicks = logicTicksThisPass;
			// One pass can pay off several ticks of debt; charge the histogram per tick, or a
			// catch-up burst reads as one enormous logic frame that never actually happened.
			GameEngine_noteLogicTime( logicMS / (Real)logicTicksThisPass, TheGameLogic->getFrame() );
#endif
		}

		updateHeadlessRun();
		updateShellScreenShot();
		updateFixedCamera();
		updateAutoCamera();
		CinemaDirector_update();
		updateUIDrill();
		updateResDrill();
		updateInputWatch();
		// -control: read whatever came down the socket. What it asks for is carried out on the next
		// logic frame, by ControlServer_runCommands, because that is where making an object is safe.
		ControlServer_poll();

#ifdef DEBUG_LOGGING
		fpsFrames++;
		fpsClientTotal += clientMS;
		fpsRadarTotal += radarMS;
		fpsAudioTotal += audioMS;
		fpsDrawTotal += TheClientDrawMS;
		fpsSceneTotal += TheSceneDrawMS;
		fpsUITotal += TheUIDrawMS;
		fpsPostTotal += TheUIPostDrawMS;
		fpsWinTotal += TheWindowRepaintMS;
		fpsStripGatherTotal += TheStripGatherMS;
		fpsStripDrawTotal += TheStripDrawMS;
		fpsShadowTotal += TheShadowMapMS;
		fpsFillTotal += TheParticleFillMS;
		fpsTranslucentTotal += TheTranslucentMS;
		fpsPostChainTotal += ThePostChainMS;
		fpsIconTotal += TheIconDrawMS;
		fpsCasterTotal += TheShadowMapCasters;
		fpsShadowDrawTotal += TheShadowMapDraws;
		fpsSceneDrawTotal += TheSceneDrawCalls;
		fpsTranslucentDrawTotal += TheTranslucentDraws;
		fpsSortTotal += TheSortingSortMS;
		fpsCopyTotal += TheSortingCopyMS;
		fpsSortDrawTotal += TheSortingDrawMS;
		fpsSortEntryTotal += TheSortingEntries;
		if( TheParticleSystemManager )
			fpsOnScreenTotal += TheParticleSystemManager->getOnScreenParticleCount();
		if( theFrameTimesStarted && TheParticleSystemManager )
		{
			const UnsignedInt particles = TheParticleSystemManager->getParticleCount();
			++theParticleCost.m_passes;
			theParticleCost.m_updateMS += TheParticleUpdateMS;
			theParticleCost.m_fillMS += TheParticleFillMS;
			theParticleCost.m_flushMS += TheTranslucentMS - TheParticleFillMS;
			theParticleCost.m_draws += TheTranslucentDraws;
			theParticleCost.m_refusedPolygons += TheSortingPolygonsRefused;
			theParticleCost.m_pastGroupLimit += TheParticlesPastGroupLimit;
			theParticleCost.m_particles += particles;
			theParticleCost.m_onScreen += TheParticleSystemManager->getOnScreenParticleCount();
			if( particles > theParticleCost.m_peakParticles )
				theParticleCost.m_peakParticles = particles;
		}
		if( TheClientDrawMS > fpsDrawMax ) fpsDrawMax = TheClientDrawMS;
		fpsLogicTotal += logicMS;
		fpsLogicTicks += logicTicks;
		if( logicTicks > 1 ) fpsCatchupPasses++;
		if( clientMS > fpsClientMax ) fpsClientMax = clientMS;
		if( logicMS > fpsLogicMax ) fpsLogicMax = logicMS;
		{
			const UnsignedInt nowMS = Clock_Milliseconds();
			const UnsignedInt windowMS = nowMS - fpsWindowStart;
			if( windowMS >= 1000 )
			{
				const Real fps = (Real)fpsFrames * 1000.0f / (Real)windowMS;
				if( fps < 50.0f && TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
				{
					// 'hz' is the rate the simulation actually achieved and 'catchup' how many render
					// frames were dropped to hold it there; hz short of m_maxFPS with catchup passes
					// present is the logic side being genuinely over budget, not the pacer.
					DEBUG_LOG(("FPS %.0f over %dms (%d frames, %d hz, %d catchup, logic frame %d) | client avg %.1f max %.1f (render avg %.1f max %.1f [scene %.1f, ui %.1f (post %.1f [strip gather %.1f, draw %.1f], win %.1f), present %.1f], radar %.1f, audio %.1f, rest %.1f) | logic avg %.1f max %.1f | unaccounted %.1fms\n",
										 fps, (Int)windowMS, fpsFrames,
										 (Int)((Real)fpsLogicTicks * 1000.0f / (Real)windowMS + 0.5f), fpsCatchupPasses,
										 TheGameLogic->getFrame(),
										 fpsClientTotal / fpsFrames, fpsClientMax,
										 fpsDrawTotal / fpsFrames, fpsDrawMax,
										 fpsSceneTotal / fpsFrames, fpsUITotal / fpsFrames,
										 fpsPostTotal / fpsFrames,
										 fpsStripGatherTotal / fpsFrames, fpsStripDrawTotal / fpsFrames,
										 fpsWinTotal / fpsFrames,
										 ( fpsDrawTotal - fpsSceneTotal - fpsUITotal ) / fpsFrames,
										 fpsRadarTotal / fpsFrames, fpsAudioTotal / fpsFrames,
										 ( fpsClientTotal - fpsDrawTotal - fpsRadarTotal - fpsAudioTotal ) / fpsFrames,
										 fpsLogicTotal / fpsFrames, fpsLogicMax,
										 (Real)windowMS - fpsClientTotal - fpsLogicTotal));
					DEBUG_LOG(("SCENE SPLIT frame %d: shadow %.1f ms, %d casters, %u shadow draws | fill %.1f translucent %.1f (%u draws) | sort %.1f copy %.1f draw %.1f (%u entries, %u on screen) | icons %.1f | postchain %.1f | scene draws %u\n",
										 TheGameLogic->getFrame(),
										 fpsShadowTotal / fpsFrames, fpsCasterTotal / fpsFrames, fpsShadowDrawTotal / fpsFrames,
										 fpsFillTotal / fpsFrames, fpsTranslucentTotal / fpsFrames,
										 fpsTranslucentDrawTotal / fpsFrames,
										 fpsSortTotal / fpsFrames, fpsCopyTotal / fpsFrames, fpsSortDrawTotal / fpsFrames,
										 fpsSortEntryTotal / fpsFrames, fpsOnScreenTotal / fpsFrames,
										 fpsIconTotal / fpsFrames, fpsPostChainTotal / fpsFrames,
										 fpsSceneDrawTotal / fpsFrames));
				}
				fpsFrames = 0;
				fpsLogicTicks = fpsCatchupPasses = 0;
				fpsClientTotal = fpsLogicTotal = 0.0f;
				fpsRadarTotal = fpsAudioTotal = fpsDrawTotal = fpsDrawMax = 0.0f;
				fpsSceneTotal = fpsUITotal = fpsPostTotal = fpsWinTotal = 0.0f;
				fpsStripGatherTotal = fpsStripDrawTotal = 0.0f;
				fpsShadowTotal = fpsFillTotal = fpsTranslucentTotal = 0.0f;
				fpsPostChainTotal = fpsIconTotal = 0.0f;
				fpsSortTotal = fpsCopyTotal = fpsSortDrawTotal = 0.0f;
				fpsCasterTotal = 0;
				fpsShadowDrawTotal = fpsSceneDrawTotal = fpsTranslucentDrawTotal = 0;
				fpsSortEntryTotal = fpsOnScreenTotal = 0;
				fpsClientMax = fpsLogicMax = 0.0f;
				fpsWindowStart = nowMS;
			}
		}
#endif

	}	// end perfGather

}

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
#if defined(_WIN32)
extern HWND ApplicationHWnd;
#endif

/** -----------------------------------------------------------------------------------------------
 * The "main loop" of the game engine. It will not return until the game exits. 
 */
void GameEngine::execute( void )
{
	UnsignedInt prevLoopTime = Clock_Milliseconds();
#if defined(_DEBUG) || defined(_INTERNAL)
	UnsignedInt startTime = Clock_Milliseconds() / 1000;
#endif

	// pretty basic for now
	while( !m_quitting )
	{
		Real thisFrameMS = 0.0f;		// how long this pass took, for the stutter report at the bottom

		GameDataGoneCheck();		// the game's drive went away under another thread's read: stop here (Debug.h)

		//if (TheGlobalData->m_vTune)
		{
#ifdef PERF_TIMERS
			PerfGather::resetAll();
#endif
		}

		{

#if defined(_DEBUG) || defined(_INTERNAL)
			{
				// enter only if in benchmark mode
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					UnsignedInt currentTime = Clock_Milliseconds() / 1000;
					if (TheGlobalData->m_benchmarkTimer < currentTime - startTime)
					{
						if (TheGameLogic->isInGame())
						{
							if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
							{
								TheRecorder->stopRecording();
							}
							TheGameLogic->clearGameData();
						}
						TheGameEngine->setQuitting(TRUE);
					}
				}
			}
#endif
			
			{
				/* The whole pass, which is what the eye is on: everything between one picture and
					 the next, not just the parts somebody remembered to instrument.  Two
					 QueryPerformanceCounter reads a frame, which is why this is not behind a switch. */
				Int64 tFrameStart, tFrameEnd;
				tFrameStart = Clock_Ticks();
				try
				{
					// compute a frame
					update();
				}
				catch (INIException e)
				{
					// Release CRASH doesn't return, so don't worry about executing additional code.
					if (e.mFailureMessage)
						RELEASE_CRASH((e.mFailureMessage));
					else
						RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
				catch (...)
				{
					// try to save info off
					try 
					{
						if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD && TheRecorder->isMultiplayer())
							TheRecorder->cleanUpReplayFile();
					}
					catch (...)
					{
					}
					RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}	// catch
				tFrameEnd = Clock_Ticks();
				thisFrameMS = engineElapsedMS( tFrameStart, tFrameEnd );
				GameEngine_noteFrameTime( thisFrameMS,
																	TheGameLogic ? TheGameLogic->getFrame() : 0 );
			}	// perf

			{
				// Rendering runs uncapped everywhere now, menus included. Game speed stays
				// constant because the logic tick is paced by wall clock inside update(), and the
				// menus no longer need a capped loop either: AnimateWindowManager::update paces
				// its own stepping off the wall clock, so the window animations keep the cadence
				// they were tuned for however fast the loop runs.
				prevLoopTime = Clock_Milliseconds();

		#if defined(_DEBUG) || defined(_INTERNAL)
				// I'm disabling this in internal because many people need alt-tab capability.  If you happen to be
				// doing performance tuning, please just change this on your local system. -MDC
				if (TheTacticalView->getTimeMultiplier()<=1 && !TheScriptEngine->isTimeFast())
					sleepMilliseconds(1); // give everyone else a tiny time slice.
		#endif
			}

		}	// perfgather for execute_loop

#ifdef PERF_TIMERS
		if (!m_quitting && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isGamePaused())
		{
			PerfGather::accumulateFrame(thisFrameMS);
			PerfGather::dumpAll(TheGameLogic->getFrame());
			PerfGather::displayGraph(TheGameLogic->getFrame());
			PerfGather::resetAll();
		}
#endif

	}

}

/** -----------------------------------------------------------------------------------------------
	* Factory for the message stream
	*/
MessageStream *GameEngine::createMessageStream( void )
{
	// if you change this update the tools that use the engine systems
	// like GUIEdit, it creates a message stream to run in "test" mode
	return MSGNEW("GameEngineSubsystem") MessageStream;
}

//-------------------------------------------------------------------------------------------------
FileSystem *GameEngine::createFileSystem( void )
{
	return MSGNEW("GameEngineSubsystem") FileSystem;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isMultiplayerSession( void )
{
	return TheRecorder->isMultiplayer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#define CONVERT_EXEC1	"..\\Build\\nvdxt -list buildDDS.txt -dxt5 -full -outdir Art\\Textures > buildDDS.out"

void updateTGAtoDDS()
{
	// Here's the scoop. We're going to traverse through all of the files in the Art\Textures folder
	// and determine if there are any .tga files that are newer than associated .dds files. If there 
	// are, then we will re-run the compression tool on them.
	
	File *fp = TheLocalFileSystem->openFile("buildDDS.txt", File::WRITE | File::CREATE | File::TRUNCATE | File::TEXT);
	if (!fp) {
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory("Art\\Textures\\", "", "*.tga", files, TRUE);
	FilenameList::iterator it;
	for (it = files.begin(); it != files.end(); ++it) {
		AsciiString filenameTGA = *it;
		AsciiString filenameDDS = *it;
		FileInfo infoTGA;
		TheLocalFileSystem->getFileInfo(filenameTGA, &infoTGA);

		// skip the water textures, since they need to be NOT compressed
		filenameTGA.toLower();
		if (strstr(filenameTGA.str(), "caust"))
		{
			continue;
		}
		// and the recolored stuff.
		if (strstr(filenameTGA.str(), "zhca"))
		{
			continue;
		}

		// replace tga with dds
		filenameDDS.removeLastChar();	// a
		filenameDDS.removeLastChar();	// g
		filenameDDS.removeLastChar();	// t
		filenameDDS.concat("dds");

		Bool needsToBeUpdated = FALSE;
		FileInfo infoDDS;
		if (TheFileSystem->doesFileExist(filenameDDS.str())) {
			TheFileSystem->getFileInfo(filenameDDS, &infoDDS);
			if (infoTGA.timestampHigh > infoDDS.timestampHigh || 
					(infoTGA.timestampHigh == infoDDS.timestampHigh && 
					 infoTGA.timestampLow > infoDDS.timestampLow)) {
				needsToBeUpdated = TRUE;
			}
		} else {
			needsToBeUpdated = TRUE;
		}

		if (!needsToBeUpdated) {
			continue;
		}

		filenameTGA.concat("\n");
		fp->write(filenameTGA.str(), filenameTGA.getLength());
	}

	fp->close();

	system(CONVERT_EXEC1);
}

//-------------------------------------------------------------------------------------------------
// System things

// If we're using the Wide character version of MessageBox, then there's no additional
// processing necessary. Please note that this is a sleazy way to get this information,
// but pending a better one, this'll have to do.
#if defined(_WIN32)
extern const Bool TheSystemIsUnicode = (((void*) (::MessageBox)) == ((void*) (::MessageBoxW)));
#else
extern const Bool TheSystemIsUnicode = TRUE;		// every text API off Windows takes Unicode (UTF-8)
#endif
