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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "zhio.h"

#include "Lib/WideCharFns.h"
#include "Common/Recorder.h"
#include "Common/FileWriteQueue.h"
#include "Common/LocalFileSystem.h"
#include "Common/FileSystem.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"

#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GameMessageParser.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameNetwork/Online/OnlineQueues.h"

#include <string>
#include "GameLogic/GameLogic.h"
#include "Common/RandomValue.h"
#include "Common/CRCDebug.h"
#include "Common/version.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

Int REPLAY_CRC_INTERVAL = 100;

const char *replayExtention = ".rep";
const char *lastReplayFileName = "00000000";	// a name the user is unlikely to ever type, but won't cause panic & confusion

static time_t startTime;
static const UnsignedInt startTimeOffset = 6;
static const UnsignedInt endTimeOffset = startTimeOffset + sizeof(time_t);
static const UnsignedInt framesOffset = endTimeOffset + sizeof(time_t);
static const UnsignedInt desyncOffset = framesOffset + sizeof(UnsignedInt);
static const UnsignedInt quitEarlyOffset = desyncOffset + sizeof(Bool);
static const UnsignedInt disconOffset = quitEarlyOffset + sizeof(Bool);

/* The replay being recorded is written by a thread of its own.  It used to be flushed after every
	 command, on the logic's thread: on a busy hard disk Windows holds such a write back for seconds,
	 and the match stood still behind it, as it did behind the debug log's (FileWriteQueue.h).  Every
	 write below goes to this queue instead, in the order the FILE calls made them, so the file comes
	 out byte for byte the same; the writer puts it on disk about once a second.  stopRecording and
	 reset drain it before anything reads or copies the file, and crash paths flush it before _exit.
	 A process killed outright loses the last second of commands. */
static FileWriteQueue theReplayQueue;

/* The Quick Match the recording under way belongs to, which gets the file when stopRecording closes it.
	 Set only by startRecording for an internet game, and empty for a custom one; init (and so every reset
	 and playback) clears it, so a skirmish, a LAN game or a replay watched later never goes out under it. */
static std::string theOnlineMatch;

template <typename T> static void replayWrite(const T &value)
{
	theReplayQueue.write(&value, sizeof(value));
}

/// What WideCharFilePut did on the replay's binary stream: the unit's two bytes, low first.
static void replayWriteWideChar(WideChar c)
{
	const unsigned char unit[2] = { (unsigned char)(c & 0xFF), (unsigned char)((c >> 8) & 0xFF) };
	theReplayQueue.write(unit, sizeof(unit));
}

/// WideCharFileWrite and the WideCharFilePut(0) after it.
static void replayWriteWideString(const WideChar *s)
{
	for (; *s != 0; ++s)
		replayWriteWideChar(*s);
	replayWriteWideChar(0);
}

/* A replay holds commands stamped with logic frames, so one recorded at another logic rate plays a
	 different game: every timer and speed in it runs at the wrong pace.  The rate rides at the end of
	 the slot list as "HZ=<frames a second>;", so the header's layout stays as it was.  The options parser
	 refuses a key it does not know, so readReplayHeader cuts the field off (takeReplayLogicRate) before
	 anything parses the string.  A replay without the field was recorded before it existed, when the
	 logic ran 60 frames a second.  The match's game speed rides the same way as "GS=<percent>;" after
	 it, always written since the speed is chosen per match; playback sets it before the map loads.  A
	 replay without the field is from the builds that wrote none at 100 and played one fixed speed, and
	 is refused: nothing in it says what a tick was worth. */
static const Int REPLAY_LOGIC_RATE_WITHOUT_FIELD = 60;
static const Int REPLAY_GAME_SPEED_WITHOUT_FIELD = 0;

AsciiString RecorderClass::replayLogicRateField( Int gameSpeedPercent )
{
	AsciiString field;
	field.format("HZ=%d;GS=%d;", (Int)LOGICFRAMES_PER_SECOND, gameSpeedPercent);
	return field;
}

// cuts the last "<key><number>;" field off the options and returns the number, or absent
static Int takeReplayHeaderField(AsciiString& options, const char *key, Int absent)
{
	// the last one: the field is appended after the slot list, which ends in ';'
	const size_t keyLength = strlen(key);
	const char *start = options.str();
	const char *field = NULL;
	for (const char *at = strstr(start, key); at != NULL; at = strstr(at + 1, key))
		if (at == start || at[-1] == ';')
			field = at;
	if (field == NULL)
		return absent;

	const Int value = atoi(field + keyLength);
	const char *end = strchr(field, ';');
	std::string cut(start, field - start);
	cut += end ? end + 1 : "";
	options = cut.c_str();
	return value;
}

Int RecorderClass::takeReplayLogicRate(AsciiString& options)
{
	return takeReplayHeaderField(options, "HZ=", REPLAY_LOGIC_RATE_WITHOUT_FIELD);
}

Int RecorderClass::takeReplayGameSpeed(AsciiString& options)
{
	return takeReplayHeaderField(options, "GS=", REPLAY_GAME_SPEED_WITHOUT_FIELD);
}

void RecorderClass::logGameStart(AsciiString options)
{
	if (!m_file)
		return;

	time(&startTime);
	theReplayQueue.writeAt(startTimeOffset, &startTime, sizeof(time_t));

#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheNetwork && TheGlobalData->m_saveStats)
	{
		//if (TheLAN)
		{
			unsigned long bufSize = MAX_COMPUTERNAME_LENGTH + 1;
			char computerName[MAX_COMPUTERNAME_LENGTH + 1];
			if (!GetComputerName(computerName, &bufSize))
			{
				strcpy(computerName, "unknown");
			}
			AsciiString statsFile = TheGlobalData->m_baseStatsDir;
			TheFileSystem->createDirectory(statsFile);
			statsFile.concat(computerName);
			statsFile.concat(".txt");
			FILE *logFP = zh_fopen(statsFile.str(), "a+");
			if (!logFP)
			{
				// try again locally
				TheWritableGlobalData->m_baseStatsDir = TheGlobalData->getPath_UserData();
				statsFile = TheGlobalData->m_baseStatsDir;
				statsFile.concat(computerName);
				statsFile.concat(".txt");
				logFP = zh_fopen(statsFile.str(), "a+");
			}
			if (logFP)
			{
				struct tm *t2 = localtime(&startTime);
				fprintf(logFP, "\nGame start at %s\tOptions are %s\n", asctime(t2), options.str());
				fclose(logFP);
			}
		}
	}
#endif
}

void RecorderClass::logPlayerDisconnect(UnicodeString player, Int slot)
{
	if (!m_file)
		return;

	DEBUG_ASSERTCRASH((slot >= 0) && (slot < MAX_SLOTS), ("Attempting to disconnect an invalid slot number"));
	if ((slot < 0) || (slot >= (MAX_SLOTS)))
	{
		return;
	}
	// save off discon status
	Bool b = TRUE;
	theReplayQueue.writeAt(disconOffset + slot*sizeof(Bool), &b, sizeof(Bool));

#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheGlobalData->m_saveStats)
	{
		unsigned long bufSize = MAX_COMPUTERNAME_LENGTH + 1;
		char computerName[MAX_COMPUTERNAME_LENGTH + 1];
		if (!GetComputerName(computerName, &bufSize))
		{
			strcpy(computerName, "unknown");
		}
		AsciiString statsFile = TheGlobalData->m_baseStatsDir;
		statsFile.concat(computerName);
		statsFile.concat(".txt");
		FILE *logFP = zh_fopen(statsFile.str(), "a+");
		if (logFP)
		{
			time_t t;
			time(&t);
			struct tm *t2 = localtime(&t);
			fprintf(logFP, "\tPlayer %s dropped at %s", WideCharAsUtf8( player.str() ).str(), asctime(t2));
			fclose(logFP);
		}
	}
#endif
}

void RecorderClass::logCRCMismatch( void )
{
	if (!m_file)
		return;

	// save off desync status
	Bool b = TRUE;
	theReplayQueue.writeAt(desyncOffset, &b, sizeof(Bool));

#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheGlobalData->m_saveStats)
	{
		m_wasDesync = TRUE;
		unsigned long bufSize = MAX_COMPUTERNAME_LENGTH + 1;
		char computerName[MAX_COMPUTERNAME_LENGTH + 1];
		if (!GetComputerName(computerName, &bufSize))
		{
			strcpy(computerName, "unknown");
		}
		AsciiString statsFile = TheGlobalData->m_baseStatsDir;
		statsFile.concat(computerName);
		statsFile.concat(".txt");
		FILE *logFP = zh_fopen(statsFile.str(), "a+");
		if (logFP)
		{
			time_t t;
			time(&t);
			struct tm *t2 = localtime(&t);
			fprintf(logFP, "\tCRC mismatch at %s", asctime(t2));
			fclose(logFP);
		}
	}
#endif
}

void RecorderClass::logGameEnd( void )
{
	if (!m_file)
		return;

	time_t t;
	time(&t);
	UnsignedInt duration = TheGameLogic->getFrame();
	theReplayQueue.writeAt(endTimeOffset, &t, sizeof(time_t));					// end time
	theReplayQueue.writeAt(framesOffset, &duration, sizeof(UnsignedInt));	// duration

#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheNetwork && TheGlobalData->m_saveStats)
	{
		//if (TheLAN)
		{
			unsigned long bufSize = MAX_COMPUTERNAME_LENGTH + 1;
			char computerName[MAX_COMPUTERNAME_LENGTH + 1];
			if (!GetComputerName(computerName, &bufSize))
			{
				strcpy(computerName, "unknown");
			}
			AsciiString statsFile = TheGlobalData->m_baseStatsDir;
			statsFile.concat(computerName);
			statsFile.concat(".txt");
			FILE *logFP = zh_fopen(statsFile.str(), "a+");
			if (logFP)
			{
				struct tm *t2 = localtime(&t);
				duration = t - startTime;
				Int minutes = duration/60;
				Int seconds = duration%60;
				fprintf(logFP, "Game end at   %s(%d:%2.2d elapsed time)\n", asctime(t2), minutes, seconds);
				fclose(logFP);
			}
		}
	}
#endif
}

#ifdef DEBUG_LOGGING
	#if defined(_INTERNAL)
		#define DEBUG_FILE_NAME				"DebugLogFileI.txt"
		#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrevI.txt"
	#elif defined(_DEBUG)
		#define DEBUG_FILE_NAME				"DebugLogFileD.txt"
		#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrevD.txt"
	#else
		#define DEBUG_FILE_NAME				"DebugLogFile.txt"
		#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrev.txt"
	#endif
#endif

void RecorderClass::cleanUpReplayFile( void )
{
	theReplayQueue.flush();		// the copy below may be taken mid-match
#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheGlobalData->m_saveStats)
	{
		char fname[_MAX_PATH+1];
		strlcpy(fname, TheGlobalData->m_baseStatsDir.str(), ARRAY_SIZE(fname));
		strlcat(fname, m_fileName.str(), ARRAY_SIZE(fname));
		DEBUG_LOG(("Saving replay to %s\n", fname));
		AsciiString oldFname;
		oldFname.format("%s%s", getReplayDir().str(), m_fileName.str());
		TheLocalFileSystem->copyFile(oldFname.str(), fname, TRUE);
#ifdef DEBUG_FILE_NAME
		AsciiString debugFname = fname;
		debugFname.removeLastChar();
		debugFname.removeLastChar();
		debugFname.removeLastChar();
		debugFname.concat("txt");
		UnsignedInt fileSize = 0;
		FILE *fp = zh_fopen(DEBUG_FILE_NAME, "rb");
		if (fp)
		{
			fseek(fp, 0, SEEK_END);
			fileSize = ftell(fp);
			fclose(fp);
			fp = NULL;
			DEBUG_LOG(("Log file size was %d\n", fileSize));
		}

		const int MAX_DEBUG_SIZE = 65536;
		if (fileSize <= MAX_DEBUG_SIZE || TheGlobalData->m_saveAllStats)
		{
			DEBUG_LOG(("Using CopyFile to copy %s\n", DEBUG_FILE_NAME));
			TheLocalFileSystem->copyFile(DEBUG_FILE_NAME, debugFname.str(), TRUE);
		}
		else
		{
			DEBUG_LOG(("manual copy of %s\n", DEBUG_FILE_NAME));
			FILE *ifp = zh_fopen(DEBUG_FILE_NAME, "rb");
			FILE *ofp = zh_fopen(debugFname.str(), "wb");
			if (ifp && ofp)
			{
				fseek(ifp, fileSize-MAX_DEBUG_SIZE, SEEK_SET);
				char buf[4096];
				Int len;
				while ( (len=fread(buf, 1, 4096, ifp)) > 0 )
				{
					fwrite(buf, 1, len, ofp);
				}
				fclose(ofp);
				fclose(ifp);
				ifp = NULL;
				ofp = NULL;
			}
			else
			{
				if (ifp) fclose(ifp);
				if (ofp) fclose(ofp);
				ifp = NULL;
				ofp = NULL;
			}
		}
#endif // DEBUG_FILE_NAME
	}
#endif
}

/**
 * The recorder object.
 */
RecorderClass *TheRecorder = NULL;

/**
 * Constructor
 */
RecorderClass::RecorderClass()
{
	m_originalGameMode = GAME_NONE;
	m_playbackFrameDuration = 0;
	m_playbackFramesPerSecond = 0;
	m_mode = RECORDERMODETYPE_RECORD;
	m_file = NULL;
	m_fileName.clear();
	m_currentFilePosition = 0;
	//Added By Sadullah Nader
	//Initializtion(s) inserted
	m_doingAnalysis = FALSE;
	m_nextFrame = 0;
	m_wasDesync = FALSE;
	//
	// playbackFile allocates this and nothing ever freed it: three replays watched in one sitting
	// left three of them behind.  It was also never set to anything before the first playback, so
	// handleCRCMessage read whatever was on the heap if a CRC arrived first.
	//
	m_crcInfo = NULL;

	init(); // just for the heck of it.
}

/**
 * Destructor
 */
RecorderClass::~RecorderClass() {
	delete m_crcInfo;
	m_crcInfo = NULL;
}

/**
 * Initialization
 * The recorder will record by default since every game will be recorded.
 * Obviously a game that is being played back will not be recorded.
 * Since the playback is done through a special interface, that interface
 * will set the recorder mode to RECORDERMODETYPE_PLAYBACK.
 */
void RecorderClass::init() {
	m_originalGameMode = GAME_NONE;
	m_playbackFrameDuration = 0;
	m_playbackFramesPerSecond = 0;
	m_mode = RECORDERMODETYPE_NONE;
	m_file = NULL;
	m_fileName.clear();
	m_currentFilePosition = 0;
	m_gameInfo.clearSlotList();
	m_gameInfo.reset();
	if (TheGlobalData->m_pendingFile.isEmpty())
		m_gameInfo.setMap(TheGlobalData->m_mapName);
	else
		m_gameInfo.setMap(TheGlobalData->m_pendingFile);
	m_gameInfo.setSeed(GetGameLogicRandomSeed());
	m_wasDesync = FALSE;
	m_doingAnalysis = FALSE;
	theOnlineMatch.clear();
}

/**
 * Reset the recorder to the "initialized state."
 */
void RecorderClass::reset() {
	theReplayQueue.stop();		// a recording's last commands into its file before it closes
	if (m_file != NULL) {
		fclose(m_file);
		m_file = NULL;
	}
	m_fileName.clear();

	// the playback that owned it is over; playbackFile makes a fresh one for the next
	delete m_crcInfo;
	m_crcInfo = NULL;

	init();
}

/**
 * update
 * Do the update for this frame.
 */
void RecorderClass::update() {
	if (m_mode == RECORDERMODETYPE_RECORD || m_mode == RECORDERMODETYPE_NONE) {
		updateRecord();
	} else if (m_mode == RECORDERMODETYPE_PLAYBACK) {
		updatePlayback();
	}
}

/**
 * Do the update for the next frame of this playback.
 */
void RecorderClass::updatePlayback() {
	cullBadCommands();	// Remove any bad commands that have been inserted by the local user that shouldn't be
											// executed during playback.

	if (m_nextFrame == -1) {
		// This is reached if there are no more commands to be executed.
		return;
	}
	UnsignedInt curFrame = TheGameLogic->getFrame();
	if (m_doingAnalysis)
		curFrame = m_nextFrame;

	// While there are commands to be queued up for this frame, do it.
	while (m_nextFrame == curFrame) {
		appendNextCommand();	// append the next command to TheCommandQueue
		readNextFrame();	// Read the next command's frame number for playback.
	}
}

/**
 * Stop the currently running playback. This is probably due either to the user exiting out of the playback or
 * reaching the end of the playback file.
 */
void RecorderClass::stopPlayback() {
	if (m_file != NULL) {
		fclose(m_file);
		m_file = NULL;
	}
	m_fileName.clear();
	// Don't clear the game data if the replay is over - let things continue
//#ifdef DEBUG_CRC
	if (!m_doingAnalysis)
		TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
//#endif
}

/**
 * Update function for recording a game. Basically all the pertinant logic commands for this frame are written out
 * to a file.
 */
void RecorderClass::updateRecord()
{
	static Int lastFrame = -1;
	GameMessage *msg = TheCommandList->getFirstMessage();
	while (msg != NULL) {
		if (msg->getType() == GameMessage::MSG_NEW_GAME &&
			 // Due to the massive amount of scripts that use <local player> in GC and single player,
			 // replays have been cut for them - so shell, single player, none and replay itself are
			 // all out.  See isRecordableGameMode() in GameLogic.h.
			 isRecordableGameMode( msg->getArgument(0)->integer )) 
		{
			m_originalGameMode = msg->getArgument(0)->integer;
			DEBUG_LOG(("RecorderClass::updateRecord() - original game is mode %d\n", m_originalGameMode));
			lastFrame = 0;
			GameDifficulty diff = DIFFICULTY_NORMAL;
			if (msg->getArgumentCount() >= 2)
				diff = (GameDifficulty)msg->getArgument(1)->integer;
			Int rankPoints = 0;
			if (msg->getArgumentCount() >= 3)
				rankPoints = msg->getArgument(2)->integer;
			Int maxFPS = 0;
			if (msg->getArgumentCount() >= 4)
				maxFPS = msg->getArgument(3)->integer;
			// as GameLogicDispatch reads it, which may not have run yet
			Int gameSpeedPercent = NATIVE_GAME_SPEED_PERCENT;
			if (msg->getArgumentCount() >= 5 && GameSpeed_isAllowed(msg->getArgument(4)->integer))
				gameSpeedPercent = msg->getArgument(4)->integer;

			startRecording(diff, m_originalGameMode, rankPoints, maxFPS, gameSpeedPercent);
		} else if (msg->getType() == GameMessage::MSG_CLEAR_GAME_DATA) {
			if (m_file != NULL) {
				lastFrame = -1;
				writeToFile(msg);
				stopRecording();
			}
			m_fileName.clear();
		} else {
			if (m_file != NULL) {
				if ((msg->getType() > GameMessage::MSG_BEGIN_NETWORK_MESSAGES) &&
						(msg->getType() < GameMessage::MSG_END_NETWORK_MESSAGES)) {
					// Only write the important messages to the file.
					writeToFile(msg);
				}
			}
		}
		msg = msg->next();
	}
}

/**
 * Start a new file for recording. This will always overwrite the "LastReplay.rep" file with the new one.
 * So don't call this unless you really mean it.
 */
void RecorderClass::startRecording(GameDifficulty diff, Int originalGameMode, Int rankPoints, Int maxFPS, Int gameSpeedPercent) {
	DEBUG_ASSERTCRASH(m_file == NULL, ("Starting to record game while game is in progress."));

	reset();

	m_mode = RECORDERMODETYPE_RECORD;

	AsciiString filepath = getReplayDir();

	// We have to make sure the replay dir exists. 
	TheFileSystem->createDirectory(filepath);

	m_fileName = getLastReplayFileName();
	m_fileName.concat(getReplayExtention());
	filepath.concat(m_fileName);
	m_file = zh_fopen(filepath.str(), "wb");
	if (m_file == NULL) {
		DEBUG_ASSERTCRASH(m_file != NULL, ("Failed to create replay file"));
		return;
	}
	theReplayQueue.start(m_file);
	theReplayQueue.write("GENREP", 6);
	if (originalGameMode == GAME_INTERNET)
		theOnlineMatch = OnlineClaimQuickMatch().str();

	//
	// save space for stats to be filled in.
	//
	// **** if this changes, change the LAN code above ****
	//
	time_t t = 0;
	replayWrite(t);	// reserve space for start time
	replayWrite(t);	// reserve space for end time

	UnsignedInt frames = 0;
	replayWrite(frames);	// reserve space for duration in frames

	Bool b = FALSE;
	replayWrite(b);	// reserve space for flag (true if we desync)
	replayWrite(b);	// reserve space for flag (true if we quit early)
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		replayWrite(b);	// reserve space for flag (true if player i disconnects)
	}

	// Print out the name of the replay.
	//
	// Each code unit as two bytes, low first, as fputwc wrote it on a binary stream under MSVC, so
	// the format is unchanged; readUnicodeString reads it back with WideCharFileGet.  See
	// Lib/WideCharFns.h.
	UnicodeString replayName;
	replayName = TheGameText->fetch("GUI:LastReplay");
	replayWriteWideString(replayName.str());

	// Date and Time
	WallClockTime systemTime;
	getLocalWallClock( &systemTime );
	replayWrite(systemTime);

	// write out version info
	UnicodeString versionString = TheVersion->getUnicodeVersion();
	UnicodeString versionTimeString = TheVersion->getUnicodeBuildTime();
	UnsignedInt versionNumber = TheVersion->getVersionNumber();
	replayWriteWideString(versionString.str());
	replayWriteWideString(versionTimeString.str());
	replayWrite(versionNumber);
	theReplayQueue.write(&(TheGlobalData->m_exeCRC), sizeof(UnsignedInt));
	theReplayQueue.write(&(TheGlobalData->m_iniCRC), sizeof(UnsignedInt));

	// Number of players
	/*
	Int numPlayers = ThePlayerList->getPlayerCount();
	fwrite(&numPlayers, sizeof(numPlayers), 1, m_file);
	*/

	// Write the slot list, the map named in full and every name whole: the lobby's 400-byte form is
	// the network's business, and the launcher reads the map's name out of this one.
	AsciiString theSlotList;
	Int localIndex = -1;
	if (TheNetwork)
	{
		if (TheLAN)
		{
			GameInfo *game = TheLAN->GetMyGame();
			DEBUG_ASSERTCRASH(game, ("Starting a LAN game with no LANGameInfo object!"));
			theSlotList = GameInfoToReplayString(game);

			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (game->getLocalIP() == game->getSlot(i)->getIP())
				{
					localIndex = i;
					break;
				}
			}
		}
		else
		{
			theSlotList = GameInfoToReplayString(TheGameSpyGame);
			localIndex = TheGameSpyGame->getLocalSlotNum();
		}
	}
	else
	{
    if(TheSkirmishGameInfo)
    {
			// This runs before GameLogic::tryStartNewGame, so a restarted skirmish has to get its
			// pre-random slot setup back here too, or the replay records the first game's draws.
			TheSkirmishGameInfo->handleOriginalSetups();
			TheSkirmishGameInfo->setCRCInterval(REPLAY_CRC_INTERVAL);
      theSlotList = GameInfoToReplayString(TheSkirmishGameInfo);
      DEBUG_LOG(("GameInfo String: %s\n",theSlotList.str()));
			localIndex = 0;
    }
    else
    {
		  // single player.  format the generic (empty) slotlist
			m_gameInfo.setCRCInterval(REPLAY_CRC_INTERVAL);
		  theSlotList = GameInfoToReplayString(&m_gameInfo);
    }
	}
	// the logic rate and game speed the game is recorded at, last in the slot list; readReplayHeader
	// refuses another rate and plays the speed back
	theSlotList.concat(replayLogicRateField(gameSpeedPercent));
	logGameStart(theSlotList);
	DEBUG_LOG(("RecorderClass::startRecording - theSlotList = %s\n", theSlotList.str()));

	// write slot list (starting spots, color, alliances, etc
	theReplayQueue.write(theSlotList.str(), theSlotList.getLength() + 1);
	char localIndexText[ 16 ];
	snprintf(localIndexText, sizeof(localIndexText), "%d", localIndex);
	theReplayQueue.write(localIndexText, strlen(localIndexText) + 1);	// with its terminator

	/*
	/// @todo fix this to use starting spots and player alliances when those are put in the game.
	for (Int i = 0; i < numPlayers; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (player == NULL) {
			continue;
		}
		UnicodeString name = player->getPlayerDisplayName();
		fwprintf(m_file, L"%s", name.str());
		fputwc(0, m_file);
		UnicodeString faction = player->getFaction()->getFactionDisplayName();
		fwprintf(m_file, L"%s", faction.str());
		fputwc(0, m_file);
		Int color = player->getColor()->getAsInt();
		fwrite(&color, sizeof(color), 1, m_file);
		Int team = 0;
		Int startingSpot = 0;
		fwrite(&startingSpot, sizeof(Int), 1, m_file);
		fwrite(&team, sizeof(Int), 1, m_file);
	}
	*/

	// Write the game difficulty.
	theReplayQueue.write(&diff, sizeof(Int));

	// Write original game mode
	replayWrite(originalGameMode);

	// Write rank points to add at game start
	replayWrite(rankPoints);

	// Write maxFPS chosen
	replayWrite(maxFPS);

	DEBUG_LOG(("RecorderClass::startRecording() - diff=%d, mode=%d, FPS=%d\n", diff, originalGameMode, maxFPS));

	/*
	// Write the map name.
	fprintf(m_file, "%s", (TheGlobalData->m_mapName).str());
	fputc(0, m_file);
	*/

	/// @todo Need to write game options when there are some to be written.
}

/**
 * This will stop the current recording session and close the file. This should always be called at the end of
 * every game.
 */
void RecorderClass::stopRecording() {
	logGameEnd();
	theReplayQueue.stop();		// all of it on disk: cleanUpReplayFile and archiveReplay copy the file
	AsciiString onlinePath;		// stays empty for a desynced recording: the match is used up, nothing sent
	const std::string onlineMatch = m_mode == RECORDERMODETYPE_RECORD ? theOnlineMatch : std::string();
	if (!onlineMatch.empty() && m_file != NULL && !m_wasDesync)
		onlinePath.format("%s%s", getReplayDir().str(), m_fileName.str());
	theOnlineMatch.clear();
	if (TheNetwork)
	{
		//if (TheLAN)
		{
			if (m_wasDesync)
				cleanUpReplayFile();
			m_wasDesync = FALSE;
		}
	}
	if (m_file != NULL) {
		fclose(m_file);
		m_file = NULL;

		if (TheGlobalData->m_archiveReplays)
			archiveReplay(m_fileName);
	}
	if (!onlineMatch.empty())
		OnlineSendReplay(onlineMatch.c_str(), onlinePath);	// after the fclose: the whole file, read back from disk
	m_fileName.clear();
}

/**
 * Copy the replay that was just written to the archive directory, under a timestamped name.
 * Every game overwrites the same LastReplay file, so without this only the most recent one
 * survives unless the player renames it by hand before starting the next match.
 */
void RecorderClass::archiveReplay(const AsciiString& fileName)
{
	WallClockTime st;
	getLocalWallClock(&st);

	AsciiString sourcePath = getReplayDir();
	sourcePath.concat(fileName);

	AsciiString destPath = getReplayArchiveDir();
	TheFileSystem->createDirectory(destPath);

	AsciiString stamp;
	stamp.format("%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	destPath.concat(stamp);
	destPath.concat(getReplayExtention());

	if (!TheLocalFileSystem->copyFile(sourcePath.str(), destPath.str(), FALSE))
		DEBUG_LOG(("RecorderClass::archiveReplay - failed to copy %s to %s\n", sourcePath.str(), destPath.str()));
}

/**
 * Write this game message to the record file. This also writes the game message's execution frame.
 */
void RecorderClass::writeToFile(GameMessage * msg) {
	// Write the frame number for this command.
	UnsignedInt frame = TheGameLogic->getFrame();
	replayWrite(frame);

	// Write the command type
	GameMessage::Type type = msg->getType();
	replayWrite(type);

	// Write the player index
	Int playerIndex = msg->getPlayerIndex();
	replayWrite(playerIndex);

#ifdef DEBUG_LOGGING
	AsciiString commandName = msg->getCommandAsAsciiString();
	if (type < GameMessage::MSG_BEGIN_NETWORK_MESSAGES || type > GameMessage::MSG_END_NETWORK_MESSAGES)
	{
		commandName.concat(" (Non-Network message!)");
	}
	else if (type == GameMessage::MSG_BEGIN_NETWORK_MESSAGES)
	{
		AsciiString tmp;
		tmp.format(" (CRC 0x%8.8X)", msg->getArgument(0)->integer);
		commandName.concat(tmp);
	}

	//DEBUG_LOG(("RecorderClass::writeToFile - Adding %s command from player %d to TheCommandList on frame %d\n",
		//commandName.str(), msg->getPlayerIndex(), TheGameLogic->getFrame()));
#endif // DEBUG_LOGGING

	GameMessageParser *parser = newInstance(GameMessageParser)(msg);
	UnsignedByte numTypes = parser->getNumTypes();
	replayWrite(numTypes);

	GameMessageParserArgumentType *argType = parser->getFirstArgumentType();
	while (argType != NULL) {
		UnsignedByte type = (UnsignedByte)(argType->getType());
		replayWrite(type);

		UnsignedByte argTypeCount = (UnsignedByte)(argType->getArgCount());
		replayWrite(argTypeCount);

		argType = argType->getNext();
	}

	const Int numArgs = msg->getArgumentCount();
	for (Int i = 0; i < numArgs; ++i) {
		writeArgument(msg->getArgumentDataType(i), *(msg->getArgument(i)));
	}

	parser->deleteInstance();
	parser = NULL;
}

void RecorderClass::writeArgument(GameMessageArgumentDataType type, const GameMessageArgumentType arg) {
	if (type == ARGUMENTDATATYPE_INTEGER) {
		replayWrite(arg.integer);
	} else if (type == ARGUMENTDATATYPE_REAL) {
		replayWrite(arg.real);
	} else if (type == ARGUMENTDATATYPE_BOOLEAN) {
		replayWrite(arg.boolean);
	} else if (type == ARGUMENTDATATYPE_OBJECTID) {
		replayWrite(arg.objectID);
	} else if (type == ARGUMENTDATATYPE_DRAWABLEID) {
		replayWrite(arg.drawableID);
	} else if (type == ARGUMENTDATATYPE_TEAMID) {
		replayWrite(arg.teamID);
	} else if (type == ARGUMENTDATATYPE_LOCATION) {
		replayWrite(arg.location);
	} else if (type == ARGUMENTDATATYPE_PIXEL) {
		replayWrite(arg.pixel);
	} else if (type == ARGUMENTDATATYPE_PIXELREGION) {
		replayWrite(arg.pixelRegion);
	} else if (type == ARGUMENTDATATYPE_TIMESTAMP) {
		replayWrite(arg.timestamp);
	} else if (type == ARGUMENTDATATYPE_WIDECHAR) {
		replayWrite(arg.wChar);
	}
}

/**
 * Read in a replay header, for (1) populating a replay listbox or (2) starting playback.  In
 * case (2), set FILE *m_file.
 */
Bool RecorderClass::readReplayHeader(ReplayHeader& header)
{
	AsciiString filepath = getReplayDir();
	filepath.concat(header.filename.str());
	m_file = zh_fopen(filepath.str(), "rb");
	if (m_file == NULL)
	{
		DEBUG_LOG(("Can't open %s (%s)\n", filepath.str(), header.filename.str()));
		return FALSE;
	}

	// Read the GENREP header.
	char genrep[7];
	fread(&genrep, sizeof(char), 6, m_file);
	genrep[6] = 0;
	if (strncmp(genrep, "GENREP", 6)) {
		DEBUG_LOG(("RecorderClass::readReplayHeader - replay file did not have GENREP at the start.\n"));
		fclose(m_file);
		m_file = NULL;
		return FALSE;
	}

	// read in some stats
	fread(&header.startTime, sizeof(time_t), 1, m_file);
	fread(&header.endTime, sizeof(time_t), 1, m_file);

	fread(&header.frameDuration, sizeof(UnsignedInt), 1, m_file);

	fread(&header.desyncGame, sizeof(Bool), 1, m_file);
	fread(&header.quitEarly, sizeof(Bool), 1, m_file);
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		fread(&(header.playerDiscons[i]), sizeof(Bool), 1, m_file);
	}

	// Read the Replay Name.  We don't actually do anything with it.  Oh well.
	header.replayName = readUnicodeString();

	// Read the date and time.  We don't really do anything with this either. Oh well.
	fread(&header.timeVal, sizeof(WallClockTime), 1, m_file);

	// Read in the Version info
	header.versionString = readUnicodeString();
	header.versionTimeString = readUnicodeString();
	fread(&header.versionNumber, sizeof(UnsignedInt), 1, m_file);
	fread(&header.exeCRC, sizeof(UnsignedInt), 1, m_file);
	fread(&header.iniCRC, sizeof(UnsignedInt), 1, m_file);

	// Read in the GameInfo
	header.gameOptions = readAsciiString();
	const Int recordedSpeed = takeReplayGameSpeed(header.gameOptions);
	const Int recordedRate = takeReplayLogicRate(header.gameOptions);
	header.gameSpeedPercent = recordedSpeed;
	if (recordedRate != LOGICFRAMES_PER_SECOND || !GameSpeed_isAllowed(recordedSpeed))
	{
		DEBUG_LOG(("RecorderClass::readReplayHeader - replay was recorded at %d logic frames a second and game speed %d (0: none written), this build runs %d and plays 100, 125, 150 or 200.\n",
			recordedRate, recordedSpeed, (Int)LOGICFRAMES_PER_SECOND));
		fclose(m_file);
		m_file = NULL;
		return FALSE;
	}
	m_gameInfo.reset();
	m_gameInfo.enterGame();
	DEBUG_LOG(("RecorderClass::readReplayHeader - GameInfo = %s\n", header.gameOptions.str()));
	if (!ParseAsciiStringToGameInfo(&m_gameInfo, header.gameOptions))
	{
		DEBUG_LOG(("RecorderClass::readReplayHeader - replay file did not have a valid GameInfo string.\n"));
		fclose(m_file);
		m_file = NULL;
		return FALSE;
	}
	m_gameInfo.startGame(0);

	AsciiString playerIndex = readAsciiString();
	header.localPlayerIndex = atoi(playerIndex.str());
	if (header.localPlayerIndex < -1 || header.localPlayerIndex >= MAX_SLOTS)
	{
		DEBUG_LOG(("RecorderClass::readReplayHeader - invalid local slot number.\n"));
		m_gameInfo.endGame();
		m_gameInfo.reset();
		fclose(m_file);
		m_file = NULL;
		return FALSE;
	}
	if (header.localPlayerIndex >= 0)
	{
		Int localIP = m_gameInfo.getSlot(header.localPlayerIndex)->getIP();
		m_gameInfo.setLocalIP(localIP);
	}

	if (!header.forPlayback)
	{
		m_gameInfo.endGame();
		m_gameInfo.reset();
		fclose(m_file);
		m_file = NULL;
	}
	return TRUE;
}

#if defined _DEBUG || defined _INTERNAL
Bool RecorderClass::analyzeReplay( AsciiString filename )
{
	m_doingAnalysis = TRUE;
	return playbackFile(filename);
}

Bool RecorderClass::isAnalysisInProgress( void )
{
	return m_mode == RECORDERMODETYPE_PLAYBACK && m_nextFrame != -1;
}
#endif

AsciiString RecorderClass::getCurrentReplayFilename( void )
{
	if (m_mode == RECORDERMODETYPE_PLAYBACK)
	{
		return m_currentReplayFilename;
	}
	return AsciiString::TheEmptyString;
}

UnsignedInt RecorderClass::getPlaybackFrameDuration( void )
{
	return m_mode == RECORDERMODETYPE_PLAYBACK ? m_playbackFrameDuration : 0;
}

Int RecorderClass::getPlaybackFramesPerSecond( void )
{
	return m_mode == RECORDERMODETYPE_PLAYBACK ? m_playbackFramesPerSecond : 0;
}

CRCInfo::CRCInfo()
{
	m_localPlayer = ~0;
	m_alignment = ALIGN_NONE;
	m_sawCRCMismatch = FALSE;
}

Bool replayMayLackFirstCRC( Int originalGameMode )
{
	return originalGameMode == GAME_LAN || originalGameMode == GAME_INTERNET;
}

void CRCInfo::addCRC(UnsignedInt val)
{
	m_data.push_back(val);
	//DEBUG_LOG(("CRCInfo::addCRC() - crc %8.8X pushes list to %d entries (full=%d)\n", val, m_data.size(), !m_data.empty()));
}

UnsignedInt CRCInfo::readCRC(void)
{
	if (m_data.empty())
	{
		//DEBUG_LOG(("CRCInfo::readCRC() - bailing, full=0, size=%d\n", m_data.size()));
		return 0;
	}

	UnsignedInt val = m_data.front();
	m_data.pop_front();
	//DEBUG_LOG(("CRCInfo::readCRC() - returning %8.8X, full=%d, size=%d\n", val, !m_data.empty(), m_data.size()));
	return val;
}

UnsignedInt CRCInfo::readCRCFor(UnsignedInt recorded)
{
	if (m_alignment == ALIGN_PENDING)
	{
		std::list<UnsignedInt>::const_iterator it = m_data.begin();
		if (it != m_data.end() && *it == recorded)
			m_alignment = ALIGN_FROM_FRAME_0;
		else if (it != m_data.end() && ++it != m_data.end() && *it == recorded)
		{
			m_alignment = ALIGN_FRAME_0_MISSING;
			m_data.pop_front();
		}
		else
			m_alignment = ALIGN_UNDECIDED;
	}
	return readCRC();
}

void RecorderClass::handleCRCMessage(UnsignedInt newCRC, Int playerIndex, Bool fromPlayback)
{
	if (fromPlayback)
	{
		//DEBUG_LOG(("RecorderClass::handleCRCMessage() - Adding CRC of %X from %d to m_crcInfo\n", newCRC, playerIndex));
		m_crcInfo->addCRC(newCRC);
		return;
	}

	Int localPlayerIndex = m_crcInfo->getLocalPlayer();
	Bool samePlayer = FALSE;
	AsciiString playerName;
	playerName.format("player%d", localPlayerIndex);
	const Player *p = ThePlayerList->getNthPlayer(playerIndex);
	if (!p || (p->getPlayerNameKey() == NAMEKEY(playerName)))
		samePlayer = TRUE;
	if (samePlayer || (localPlayerIndex < 0))
	{
		const Bool aligning = (m_crcInfo->getAlignment() == CRCInfo::ALIGN_PENDING);
		UnsignedInt playbackCRC = m_crcInfo->readCRCFor(newCRC);
		if (aligning)
		{
			// say which kind of recording this was, so a reader of the log can tell (Recorder.h)
			const CRCInfo::Alignment how = m_crcInfo->getAlignment();
			DEBUG_LOG(("Replay CRCs: %s\n", how == CRCInfo::ALIGN_FROM_FRAME_0 ? "recorded from frame 0"
				: how == CRCInfo::ALIGN_FRAME_0_MISSING ? "legacy: frame 0 missing"
				: "neither alignment fits the first recorded CRC"));
		}
		//DEBUG_LOG(("RecorderClass::handleCRCMessage() - Comparing CRCs of %8.8X/%8.8X from %d\n", newCRC, playbackCRC, playerIndex));
		if (TheGameLogic->getFrame() > 0 && newCRC != playbackCRC && !m_crcInfo->sawCRCMismatch())
		{
			m_crcInfo->setSawCRCMismatch();

			/* The DEBUG_CRASH below is compiled out of a release build, so a replay that played back a
				 different game than the one that was recorded used to do it in complete silence.  Say so in
				 the log, which release builds do write. */
			DEBUG_LOG(("Replay has gone out of sync on frame %d: recorded %8.8X, played back %8.8X\n",
				TheGameLogic->getFrame(), newCRC, playbackCRC));

			//Kris: Patch 1.01 November 10, 2003 (integrated changes from Matt Campbell)
			// Since we don't seem to have any *visible* desyncs when replaying games, but get this warning
			// virtually every replay, the assumption is our CRC checking is faulty.  Since we're at the
			// tail end of patch season, let's just disable the message, and hope the users believe the
			// problem is fixed. -MDC 3/20/2003
			//TheInGameUI->message("GUI:CRCMismatch");
			DEBUG_CRASH(("Replay has gone out of sync!  All bets are off!\nRecorded:%8.8X Played back:%8.8X\nFrame:%d",
				newCRC, playbackCRC, TheGameLogic->getFrame()));
		}
		return;
	}

	//DEBUG_LOG(("RecorderClass::handleCRCMessage() - Skipping CRC of %8.8X from %d (our index is %d)\n", newCRC, playerIndex, localPlayerIndex));
}

/**
 * Return true if this version of the file is the same as our version of the game
 */
Bool RecorderClass::testVersionPlayback(AsciiString filename)
{

	ReplayHeader header;
	header.forPlayback = TRUE;
	header.filename = filename;
	Bool success = readReplayHeader( header );
	if (!success)
	{
		// A header we cannot read used to answer "compatible", and the caller then went straight
		// into playback on a file it had already failed to parse. Warn instead: the prompt is the
		// same one a version difference gets, which is at least true - we do not know what this is.
		return TRUE;
	}
	Bool versionStringDiff = header.versionString != TheVersion->getUnicodeVersion();
	Bool versionTimeStringDiff = header.versionTimeString != TheVersion->getUnicodeBuildTime();
	Bool versionNumberDiff = header.versionNumber != TheVersion->getVersionNumber();
	Bool exeCRCDiff = header.exeCRC != TheGlobalData->m_exeCRC;
	Bool exeDifferent = versionStringDiff || versionTimeStringDiff || versionNumberDiff || exeCRCDiff;
	Bool iniDifferent = header.iniCRC != TheGlobalData->m_iniCRC;

	if(exeDifferent || iniDifferent)
	{
		return TRUE;
	}
	return FALSE;

}

/**
 * Start playback of the file. Return true or false depending on if the file is
 * a valid replay file or not.
 */
Bool RecorderClass::playbackFile(AsciiString filename)
{
	if (!m_doingAnalysis)
	{
		if (TheGameLogic->isInGame())
		{
			TheGameLogic->clearGameData();
		}
	}

	Int difficulty = 0;
	Int rankPoints = 0;
	Int maxFPS = 0;
	Int gameSpeedPercent = NATIVE_GAME_SPEED_PERCENT;
	if (!openPlayback(filename, difficulty, rankPoints, maxFPS, gameSpeedPercent))
		return FALSE;

	if (replayMayLackFirstCRC( m_originalGameMode ))
	{
		m_crcInfo->allowMissingFirstCRC();
	}

	m_nextFrame = 0;	// the last playback may have left -1 here, and readNextFrame reads forward from it
	readNextFrame();

	// send a message to the logic for a new game
	if (!m_doingAnalysis)
	{
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_REPLAY);
		msg->appendIntegerArgument(difficulty);
		msg->appendIntegerArgument(rankPoints);
		msg->appendIntegerArgument(maxFPS);					// 0, none recorded, leaves the pace alone
		msg->appendIntegerArgument(gameSpeedPercent);	// the match's own game speed
		InitRandom( m_gameInfo.getSeed() );
	}

	return TRUE;
}

RecorderClass::PlaybackCursor RecorderClass::getPlaybackCursor( void )
{
	PlaybackCursor cursor;
	cursor.filePosition = ftell(m_file);
	cursor.nextFrame = m_nextFrame;
	cursor.crcInfo = *m_crcInfo;
	return cursor;
}

/**
 * Pick a replay up again where getPlaybackCursor left it, once an engine reset has closed it.  The
 * header is read the same way playbackFile reads it, for the players the new game is built from.
 */
Bool RecorderClass::resumePlayback(AsciiString filename, const PlaybackCursor &cursor)
{
	Int difficulty = 0;
	Int rankPoints = 0;
	Int maxFPS = 0;
	Int gameSpeedPercent = NATIVE_GAME_SPEED_PERCENT;
	if (!openPlayback(filename, difficulty, rankPoints, maxFPS, gameSpeedPercent))
		return FALSE;
	// no new game message comes: the speed is set here, between matches (the checkpoint's own header
	// sets it again as its load starts)
	GameSpeed_set(gameSpeedPercent);
	// the checkpoint names its own map, and no new game will come along to use this one up
	TheWritableGlobalData->m_pendingFile.clear();
	// the load starts a game, and a replay's random sides and start positions are drawn off this
	// seed: drawn off any other, the sides come out different and the checkpoint's teams do not fit
	InitRandom( m_gameInfo.getSeed() );
	fseek(m_file, cursor.filePosition, SEEK_SET);
	m_nextFrame = cursor.nextFrame;
	*m_crcInfo = cursor.crcInfo;
	return TRUE;
}

/**
 * Open a replay and read everything in front of its first command: the header, the difficulty, the
 * original game mode, the rank points and the frame rate the game was played at.
 */
Bool RecorderClass::openPlayback(AsciiString filename, Int &difficulty, Int &rankPoints, Int &maxFPS, Int &gameSpeedPercent)
{
	m_mode = RECORDERMODETYPE_PLAYBACK;

	ReplayHeader header;
	header.forPlayback = TRUE;
	header.filename = filename;
	Bool success = readReplayHeader( header );
	if (!success)
	{
		return FALSE;
	}
	m_playbackFrameDuration = header.frameDuration;
	gameSpeedPercent = header.gameSpeedPercent;
#ifdef DEBUG_LOGGING

	Bool versionStringDiff = header.versionString != TheVersion->getUnicodeVersion();
	Bool versionTimeStringDiff = header.versionTimeString != TheVersion->getUnicodeBuildTime();
	Bool versionNumberDiff = header.versionNumber != TheVersion->getVersionNumber();
	Bool exeCRCDiff = header.exeCRC != TheGlobalData->m_exeCRC;
	Bool exeDifferent = versionStringDiff || versionTimeStringDiff || versionNumberDiff || exeCRCDiff;
	Bool iniDifferent = header.iniCRC != TheGlobalData->m_iniCRC;

	AsciiString debugString;
	AsciiString tempStr;
	if (exeDifferent)
	{
		debugString = "EXE is different:\n";
		if (versionStringDiff)
		{
			tempStr.format("   Version [%s] vs [%s]\n", WideCharAsUtf8( TheVersion->getUnicodeVersion().str() ).str(), WideCharAsUtf8( header.versionString.str() ).str());
			debugString.concat(tempStr);
		}
		if (versionTimeStringDiff)
		{
			tempStr.format("   Build Time [%s] vs [%s]\n", WideCharAsUtf8( TheVersion->getUnicodeBuildTime().str() ).str(), WideCharAsUtf8( header.versionTimeString.str() ).str());
			debugString.concat(tempStr);
		}
		if (versionNumberDiff)
		{
			tempStr.format("   Version Number %8.8X vs %8.8X\n", TheVersion->getVersionNumber(), header.versionNumber);
			debugString.concat(tempStr);
		}
		if (exeCRCDiff)
		{
			tempStr.format("   CRC %8.8X vs %8.8X\n", TheGlobalData->m_exeCRC, header.exeCRC);
			debugString.concat(tempStr);
		}
	}
	if (iniDifferent)
	{
		debugString.concat("INIs are different:\n");
		tempStr.format("   CRC %8.8X vs %8.8X\n", TheGlobalData->m_iniCRC, header.iniCRC);
		debugString.concat(tempStr);
	}
	DEBUG_ASSERTCRASH(!exeDifferent && !iniDifferent, (debugString.str()));
#endif

	TheWritableGlobalData->m_pendingFile = m_gameInfo.getMap();

#ifdef DEBUG_LOGGING
	if (header.localPlayerIndex >= 0)
	{
		DEBUG_LOG(("Local player is %s (slot %d, IP %8.8X)\n",
			WideCharAsUtf8( m_gameInfo.getSlot(header.localPlayerIndex)->getName().str() ).str(), header.localPlayerIndex, m_gameInfo.getSlot(header.localPlayerIndex)->getIP()));
	}
#endif

	delete m_crcInfo;		// a replay started without a reset in between would leak the last one
	m_crcInfo = NEW CRCInfo;
	m_crcInfo->setLocalPlayer(header.localPlayerIndex);
	REPLAY_CRC_INTERVAL = m_gameInfo.getCRCInterval();
	DEBUG_LOG(("Player index is %d, replay CRC interval is %d\n", m_crcInfo->getLocalPlayer(), REPLAY_CRC_INTERVAL));

	fread(&difficulty, sizeof(difficulty), 1, m_file);

	fread(&m_originalGameMode, sizeof(m_originalGameMode), 1, m_file);

	fread(&rankPoints, sizeof(rankPoints), 1, m_file);

	fread(&maxFPS, sizeof(maxFPS), 1, m_file);
	m_playbackFramesPerSecond = maxFPS;

	DEBUG_LOG(("RecorderClass::playbackFile() - original game was mode %d\n", m_originalGameMode));

	m_currentReplayFilename = filename;
	return TRUE;
}

/**
 * Read a unicode string from the current file position. The string is assumed to be 0-terminated.
 */
UnicodeString RecorderClass::readUnicodeString() {
	// Was UnsignedShort: VC6's wchar_t was a typedef for it, so L"" initialised it
	// and UnicodeString took it.  wchar_t is its own type now.
	WideChar str[1024] = u"";
	Int index = 0;

	Int c = WideCharFileGet(m_file);
	if (c == EOF) {
		str[index] = 0;
	}
	str[index] = c;

	// Was index < 1024: the body pre-increments and then writes str[index], so the
	// last iteration stored one element past the end of the buffer.
	while (index < 1023 && str[index] != 0) {
		++index;
		Int c = WideCharFileGet(m_file);
		if (c == EOF) {
			str[index] = 0;
			break;
		}
		str[index] = c;
	}
	str[1023] = u'\0';

	UnicodeString retval(str);
	return retval;
}

/**
 * Read an ascii string from the current file position. The string is assumed to be 0-terminated.
 */
AsciiString RecorderClass::readAsciiString() {
	/* Read to the terminator.  EA's buffer stopped at 1023 bytes and left the rest of a longer string
		 to be read as the next field, and a 32-seat room's slot list runs well past that. */
	std::string str;
	Int c;
	while ((c = fgetc(m_file)) != EOF && c != 0)
		str.push_back((char)c);
	return AsciiString(str.c_str());
}

/**
 * Read the frame number for the next command in the playback file. If the end of the file is reached, the playback
 * is stopped and the next frame is said to be -1.
 */
void RecorderClass::readNextFrame() {
	const UnsignedInt lastFrame = m_nextFrame;
	Int retcode = fread(&m_nextFrame, sizeof(m_nextFrame), 1, m_file);
	//
	// Commands are written in frame order and none after the duration the header is closed with, so a
	// frame behind the last one, or past that end, is a damaged file.  Playback used to wait for that
	// frame, which never comes back or comes after the game is over, and a headless run never ended.
	// A header left at 0 is a game that never closed its file, and has no end to hold the frame to.
	//
	const Bool damaged = retcode == 1 && ( m_nextFrame < lastFrame ||
		( m_playbackFrameDuration != 0 && m_nextFrame > m_playbackFrameDuration ) );
	if (retcode != 1 || damaged) {
		if (damaged)
			DEBUG_LOG(("RecorderClass::readNextFrame - next command on frame %u after one on frame %u, replay ends on frame %u: file is damaged\n",
				m_nextFrame, lastFrame, m_playbackFrameDuration));
		else
			DEBUG_LOG(("RecorderClass::readNextFrame - fread failed on frame %d\n", TheGameLogic->getFrame()));
		m_nextFrame = -1;
		stopPlayback();
	}
}

/**
 * This reads the next command from the replay file and appends it to TheCommandList.
 */
void RecorderClass::appendNextCommand() {
	GameMessage::Type type;
	Int retcode = fread(&type, sizeof(type), 1, m_file);
	if (retcode != 1) {
		DEBUG_LOG(("RecorderClass::appendNextCommand - fread failed on frame %d\n", m_nextFrame/*TheGameLogic->getFrame()*/));
		return;
	}

	GameMessage *msg = newInstance(GameMessage)(type);
	if (type == GameMessage::MSG_BEGIN_NETWORK_MESSAGES || type == GameMessage::MSG_CLEAR_GAME_DATA)
	{
	}
	else
	{
		if (!m_doingAnalysis)
		{
			TheCommandList->appendMessage(msg);
		}
	}

#ifdef DEBUG_LOGGING
	AsciiString commandName = msg->getCommandAsAsciiString();
	if (type < GameMessage::MSG_BEGIN_NETWORK_MESSAGES || type > GameMessage::MSG_END_NETWORK_MESSAGES)
	{
		commandName.concat(" (Non-Network message!)");
	}
	else if (type == GameMessage::MSG_BEGIN_NETWORK_MESSAGES)
	{
		commandName.concat(" (CRC message!)");
	}
#endif // DEBUG_LOGGING

	Int playerIndex = -1;
	fread(&playerIndex, sizeof(playerIndex), 1, m_file);
	msg->friend_setPlayerIndex(playerIndex);

	// don't debug log this if we're debugging sync errors, as it will cause diff problems between a game and it's replay...
#ifdef DEBUG_LOGGING
	Bool logCommand = true;
#ifdef DEBUG_CRC
	if (!m_doingAnalysis)
		logCommand = false;
#endif
	if (logCommand)
	{
		DEBUG_LOG(("RecorderClass::appendNextCommand - Adding %s command from player %d to TheCommandList on frame %d\n",
			commandName.str(), (type == GameMessage::MSG_BEGIN_NETWORK_MESSAGES)?0:msg->getPlayerIndex(), m_nextFrame/*TheGameLogic->getFrame()*/));
	}
#endif

	UnsignedByte numTypes = 0;
	Int totalArgs = 0;
	fread(&numTypes, sizeof(numTypes), 1, m_file);

	GameMessageParser *parser = newInstance(GameMessageParser)();
	for (UnsignedByte i = 0; i < numTypes; ++i) {
		UnsignedByte type = (UnsignedByte)ARGUMENTDATATYPE_UNKNOWN;
		fread(&type, sizeof(type), 1, m_file);
		UnsignedByte numArgs = 0;
		fread(&numArgs, sizeof(numArgs), 1, m_file);
		parser->addArgType((GameMessageArgumentDataType)type, numArgs);
		totalArgs += numArgs;
	}

	GameMessageParserArgumentType *parserArgType = parser->getFirstArgumentType();
	GameMessageArgumentDataType lasttype = ARGUMENTDATATYPE_UNKNOWN;
	Int argsLeftForType = 0;
	if (parserArgType != NULL) {
		lasttype = parserArgType->getType();
		argsLeftForType = parserArgType->getArgCount();
	}
	for (Int j = 0; j < totalArgs; ++j) {
		readArgument(lasttype, msg);

		--argsLeftForType;
		if (argsLeftForType == 0) {
			DEBUG_ASSERTCRASH(parserArgType != NULL, ("parserArgType was NULL when it shouldn't have been."));
			if (parserArgType == NULL) {
				return;
			}

			parserArgType = parserArgType->getNext();
			// parserArgType is allowed to be NULL here, this is the case if there are no more arguments.
			if (parserArgType != NULL) {
				argsLeftForType = parserArgType->getArgCount();
				lasttype = parserArgType->getType();
			}
		}
	}

	if (type == GameMessage::MSG_CLEAR_GAME_DATA || type == GameMessage::MSG_BEGIN_NETWORK_MESSAGES)
	{
		msg->deleteInstance();
		msg = NULL;
	}

	if (m_doingAnalysis)
	{
		msg->deleteInstance();
		msg = NULL;
	}

	parser->deleteInstance();
	parser = NULL;
}

void RecorderClass::readArgument(GameMessageArgumentDataType type, GameMessage *msg) {
	if (type == ARGUMENTDATATYPE_INTEGER) {
		Int theint;
		fread(&theint, sizeof(theint), 1, m_file);
		msg->appendIntegerArgument(theint);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Integer argument: %d (%8.8X)\n", theint, theint));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_REAL) {
		Real thereal;
		fread(&thereal, sizeof(thereal), 1, m_file);
		msg->appendRealArgument(thereal);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Real argument: %g (%8.8X)\n", thereal, *(int *)&thereal));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_BOOLEAN) {
		Bool thebool;
		fread(&thebool, sizeof(thebool), 1, m_file);
		msg->appendBooleanArgument(thebool);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Bool argument: %d\n", thebool));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_OBJECTID) {
		ObjectID theid;
		fread(&theid, sizeof(theid), 1, m_file);
		msg->appendObjectIDArgument(theid);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Object ID argument: %d\n", theid));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_DRAWABLEID) {
		DrawableID theid;
		fread(&theid, sizeof(theid), 1, m_file);
		msg->appendDrawableIDArgument(theid);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Drawable ID argument: %d\n", theid));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_TEAMID) {
		UnsignedInt theid;
		fread(&theid, sizeof(theid), 1, m_file);
		msg->appendTeamIDArgument(theid);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Team ID argument: %d\n", theid));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_LOCATION) {
		Coord3D loc;
		fread(&loc, sizeof(loc), 1, m_file);
		msg->appendLocationArgument(loc);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Coord3D argument: %g %g %g (%8.8X %8.8X %8.8X)\n", loc.x, loc.y, loc.z,
				*(int *)&loc.x, *(int *)&loc.y, *(int *)&loc.z));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_PIXEL) {
		ICoord2D pixel;
		fread(&pixel, sizeof(pixel), 1, m_file);
		msg->appendPixelArgument(pixel);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Pixel argument: %d,%d\n", pixel.x, pixel.y));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_PIXELREGION) {
		IRegion2D reg;
		fread(&reg, sizeof(reg), 1, m_file);
		msg->appendPixelRegionArgument(reg);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Pixel Region argument: %d,%d -> %d,%d\n", reg.lo.x, reg.lo.y, reg.hi.x, reg.hi.y));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_TIMESTAMP) {  // Not to be confused with Terrance Stamp... Kneel before Zod!!!
		UnsignedInt stamp;
		fread(&stamp, sizeof(stamp), 1, m_file);
		msg->appendTimestampArgument(stamp);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("Timestamp argument: %d\n", stamp));
		}
#endif
	} else if (type == ARGUMENTDATATYPE_WIDECHAR) {
		WideChar theid;
		fread(&theid, sizeof(theid), 1, m_file);
		msg->appendWideCharArgument(theid);
#ifdef DEBUG_LOGGING
		if (m_doingAnalysis)
		{
			DEBUG_LOG(("WideChar argument: %d (%lc)\n", theid, theid));
		}
#endif
	}
}

/**
 * This needs to be called for every frame during playback. Basically it prevents the user from inserting.
 */
void RecorderClass::cullBadCommands() {
	if (m_doingAnalysis)
		return;

	GameMessage *msg = TheCommandList->getFirstMessage();
	GameMessage *next = NULL;

	while (msg != NULL) {
		next = msg->next();
		if ((msg->getType() > GameMessage::MSG_BEGIN_NETWORK_MESSAGES) &&
				(msg->getType() < GameMessage::MSG_END_NETWORK_MESSAGES) &&
				(msg->getType() != GameMessage::MSG_LOGIC_CRC)) {

			msg->deleteInstance();
		}

		msg = next;
	}
}

/**
 * returns the directory that holds the replay files.
 */
AsciiString RecorderClass::getReplayDir() 
{
	const char* replayDir = "Replays\\";

	AsciiString tmp = TheGlobalData->getPath_UserData();
	tmp.concat(replayDir);
	return tmp;
}

/**
 * returns the directory that holds the archived replay files.
 */
AsciiString RecorderClass::getReplayArchiveDir()
{
	AsciiString tmp = TheGlobalData->getPath_UserData();
	tmp.concat("ArchivedReplays\\");
	return tmp;
}

/**
 * returns the file extention for the replay files.
 */
AsciiString RecorderClass::getReplayExtention() {
	return AsciiString(replayExtention);
}

/**
 * returns the file name used for the replay file that is recorded to.
 */
AsciiString RecorderClass::getLastReplayFileName() 
{
#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheNetwork && TheGlobalData->m_saveStats)
	{
		GameInfo *game = NULL;
		if (TheLAN)
			game = TheLAN->GetMyGame();
		else if (TheGameSpyInfo)
			game = TheGameSpyGame;
		if (game)
		{
			AsciiString players;
			AsciiString full;
			AsciiString fullPlusNum;
			AsciiString mapName = game->getMap();
			const char *fname = mapName.reverseFind('\\');
			if (fname)
				mapName = fname+1;
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				GameSlot *slot = game->getSlot(i);
				if (slot && slot->isHuman())
				{
					AsciiString player;
					player.format("%s_", WideCharAsUtf8( slot->getName().str() ).str());
					players.concat(player);
				}
			}
			full.format("%s%s_%d_%d", players.str(), mapName.str(), game->getSeed(), game->getLocalSlotNum());
			AsciiString testString;
			testString.format("%s%s%s", getReplayDir().str(), full.str(), replayExtention);

			FILE *fp;
			fp = zh_fopen(testString.str(), "rb");
			if (fp)
			{
				fclose(fp);
			}
			else
			{
				return full;
			}
			Int test = 1;
			while (test < 20)
			{
				fullPlusNum.format("%s_%d", full.str(), test);
				testString.format("%s%s%s", getReplayDir().str(), fullPlusNum.str(), replayExtention);
				fp = zh_fopen(testString.str(), "rb");
				if (fp)
				{
					fclose(fp);
					++test;
				}
				else
				{
					return fullPlusNum;
				}
			}
			return fullPlusNum;
		}
	}
#endif
	return AsciiString(lastReplayFileName);
}

/**
 * return the current operating mode of TheRecorder.
 */
RecorderModeType RecorderClass::getMode() {
	return m_mode;
}

///< Show or Hide the Replay controls
void RecorderClass::initControls()
{
	NameKeyType parentReplayControlID = TheNameKeyGenerator->nameToKey( AsciiString("ReplayControl.wnd:ParentReplayControl") );
	GameWindow *parentReplayControl = TheWindowManager->winGetWindowFromId( NULL, parentReplayControlID );

	Bool show = (getMode() != RECORDERMODETYPE_PLAYBACK);
	if (parentReplayControl)
	{
		parentReplayControl->winHide(show);	// show the replay control window.
	}
}

///< is this a multiplayer game (record OR playback)?
Bool RecorderClass::isMultiplayer( void )
{

	if (m_mode == RECORDERMODETYPE_PLAYBACK)
	{
		GameSlot *slot;
		for (int i=0; i<MAX_SLOTS; ++i)
		{
			slot = m_gameInfo.getSlot(i);
			if (slot && slot->isOccupied())	///< slots default to closed for non-networked games
				return true;
		}
	}
	if (TheGameLogic->getGameMode()==GAME_SINGLE_PLAYER) {
		return false; // single player isn't multiplayer.
	}
	if (TheGameLogic->getGameMode()==GAME_SHELL) {
		return false; // shell isn't multiplayer.
	}
	if (TheNetwork || TheSkirmishGameInfo)
		return true;

	return false;
}

/**
 * Create a new recorder object.
 */
RecorderClass * createRecorder() {
	return NEW RecorderClass;
}
