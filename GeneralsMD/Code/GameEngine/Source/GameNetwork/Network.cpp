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

// FILE: Network.cpp ////////////////////////////////////////////////////
// Implementation of Network singleton
// Author: Matthew D. Campbell, July 2001
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include <map>
#include "Lib/Clock.h"

#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/CushionMetrics.h"
#include "GameNetwork/udp.h"
#include "GameNetwork/Transport.h"
#include "strtok_r.h"
#include "GameClient/Shell.h"
#include "Common/CRCDebug.h"
#include "Common/LogicThread.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/VictoryConditions.h"

#include "Common/RandomValue.h"


#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"
#include "Common/Recorder.h"
#include "GameClient/MessageBox.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "Common/GameState.h"
#include "Common/LocalFileSystem.h"
#include "Common/GlobalData.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "zhio.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

/* A rejoin checkpoint as it lies on disk: <frame>.sav, the world through the save path; <frame>.rep, this
	 seat's recording as it stood then; and <frame>.ckpt, this, written last, so a checkpoint whose .ckpt reads
	 back whole with both sizes matching is complete. */
struct RejoinCheckpointMeta
{
	UnsignedInt frame;
	UnsignedInt exeCRC;
	UnsignedInt iniCRC;
	Int logicRate;
	Int gameSpeed;
	Int gameMode;
	Int localSlot;
	UnsignedInt savBytes;
	UnsignedInt repBytes;
	UnsignedInt headerBytes;									///< the recording's header, up to its first command
	UnsignedInt written;											///< wall clock seconds, for the sweep of old matches
	GameLogicRandomState random;							///< a save does not carry the logic's random stream
	AsciiString key;													///< the match and the seat (rejoinCheckpointKey)
	AsciiString options;											///< the slot list the recording opened with
	std::vector<RecorderClass::FrameStart> starts;	///< where each frame with commands starts in the .rep
};

/* OFF: a seat that comes back plays the whole match from frame 0, and no seat writes checkpoints.  A load
	 does not yet give back the world that was saved (TODO, Save and Load Parity 9, 11 and 15): the logic CRC
	 walks the same snapshots the save writes, so a member the save leaves out is invisible to the CRC check
	 until it leaks into a saved field, up to about 3000 frames after a load.  A seat that joined on such a
	 load would split the live match.  Turn it on once those items are closed, a parity batch holds (several
	 seeds, computer and human seats, saves taken at several frames, every loaded run ending on the CRC of the
	 run that was never interrupted), and rejoin_check.py passes its keep, none and corrupt cases with
	 computer players in the match.  The verify span below is defence in depth, not the condition. */
static const Bool REJOIN_FROM_CHECKPOINT = FALSE;

/* With it on, the frames played from a checkpoint before the seat joins span at least this much, held
	 against the room's CRCs all the way: the seat loads the newest checkpoint this much older than its newest
	 one (findRejoinCheckpoint), and a join that still comes sooner starts the match again from frame 0.  Twice
	 the one gap measured between a load and its first differing CRC, about 3000 frames. */
static const UnsignedInt REJOIN_MIN_VERIFY_FRAMES = 200 * LOGICFRAMES_PER_SECOND;

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

/* How many logic frames apart a network game checks the world CRC; the host's value travels in the
	 lobby string, so the room runs on the host's.  Release has DEBUG_CRC on, where EA's 1 meant a full
	 getCRC every frame, about 1.6 ms of a late-game tick.  At 10 a split is still caught within ten
	 logic frames, and the CRCSnapshotRing (CRC_SNAPSHOT_RING_SIZE slots, 16 at the retail rate and more
	 as the logic rate rises with the run-ahead) then reaches ten times its size in frames back, past any
	 run-ahead.  -NetCRCInterval 1 for a desync hunt. */
Int NET_CRC_INTERVAL = 10;

// DEFINES ////////////////////////////////////////////////////////////////////

#define RESEND_INTERVAL 1

// PRIVATE TYPES //////////////////////////////////////////////////////////////

/**
 * Connection message - encapsulating info kept by the connection layer about each
 * packet.  These structs make up the in/out buffers at the connection layer.
 */
#pragma pack(push, 1)
struct ConnectionMessage
{
	Int id;
	NetMessageFlags flags;
	UnsignedByte data[MAX_PACKET_SIZE];
	time_t lastSendTime;
	Int retries;
	Int length;
};
#pragma pack(pop)

static const int CmdMsgLen = 6; //< Minimum size of a command packet (Int + Unsigned Short)

// PRIVATE DATA ///////////////////////////////////////////////////////////////



// PUBLIC DATA ////////////////////////////////////////////////////////////////

/// The Network singleton instance
NetworkInterface *TheNetwork = NULL;

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

/**
 * The Network class is used to instantiate a singleton which 
 * implements the interface to all Network operations such as message stream processing and network communications.
 */
class Network : public NetworkInterface
{
public:
	//---------------------------------------------------------------------------------------
	// Setup / Teardown functions
	Network();
	~Network();
	void init( void );																				///< Initialize or re-initialize the instance
	void reset( void );																				///< Reinitialize the network
	void update( void );																			///< Process command list
	void liteupdate( void );																	///< Do a lightweight update to send packets and pass messages.
	void serviceSockets( void );															///< Between passes: announce finished frames, read and write the socket.
	Bool deinit( void );																			///< Shutdown connections, release memory

	void setLocalAddress(UnsignedInt ip, UnsignedInt port);
	inline UnsignedInt getRunAhead(void) { return m_runAhead; }
	inline UnsignedInt getFrameRate(void) { return m_frameRate; }
	UnsignedInt getPacketArrivalCushion(void);								///< Returns the smallest packet arrival cushion since this was last called.
	UnsignedInt getFramesReady(void) { return m_conMgr->countFramesReady(TheGameLogic->getFrame(), m_runAhead); }
	Bool isFrameDataReady( void );
	Bool isPacingLogicFrames( void );
	void parseUserList( const GameInfo *game );
	void startGame(void);																			///< Sets the network game frame counter to -1

	void sendChat(UnicodeString text, SlotMask playerMask);
	void sendDisconnectChat(UnicodeString text);

	void sendFile(AsciiString path, SlotMask playerMask, UnsignedShort commandID);
	UnsignedShort sendFileAnnounce(AsciiString path, SlotMask playerMask);
	Int getFileTransferProgress(Int playerID, AsciiString path);
	Bool areAllQueuesEmpty(void);

	void quitGame();
	virtual void selfDestructPlayer(Int index);


	void voteForPlayerDisconnect(Int slot);
	virtual Bool isPacketRouter( void );

	// Bandwidth metrics
	Real getIncomingBytesPerSecond( void );
	Real getIncomingPacketsPerSecond( void );
	Real getOutgoingBytesPerSecond( void );
	Real getOutgoingPacketsPerSecond( void );
	Real getUnknownBytesPerSecond( void );
	Real getUnknownPacketsPerSecond( void );

	// Multiplayer Load Progress Functions
	void updateLoadProgress( Int percent );
	void loadProgressComplete( void );
	void sendTimeOutGameStart( void );

#if defined(_INTERNAL) || defined(_DEBUG)
	// Disconnect screen testing
	virtual void toggleNetworkOn();
#endif

	// Exposing some info contained in the Connection Manager
	UnsignedInt getLocalPlayerID( void );
	UnicodeString getPlayerName(Int playerNum);
	Int getNumPlayers(void );

	Int getAverageFPS() { return m_conMgr->getAverageFPS(); }
	Int getSlotAverageFPS(Int slot);
	Real getSlotAverageLatency(Int slot) { return m_conMgr ? m_conMgr->getSlotAverageLatency(slot) : 0.0f; }
	Int getSlowestSlot( void ) { return m_conMgr ? m_conMgr->getSlowestSlot() : -1; }

	void attachTransport(Transport *transport);
	void initTransport();

	void setSawCRCMismatch( void );
	Bool sawCRCMismatch( void ) { return m_sawCRCMismatch; }
	Bool isPlayerConnected( Int playerID );

	void notifyOthersOfCurrentFrame();														///< Tells all the other players what frame we are on.
	void notifyOthersOfNewFrame(UnsignedInt frame);								///< Tells all the other players that we are on a new frame.

	Int  getExecutionFrame();																			///< Returns the next valid frame for simultaneous command execution.

	Bool startRejoin( AsciiString replayPath, Int logSlot, RejoinStartMatch startMatch );
	Bool isRejoining( void ) { return m_rejoinFeeding; }

	// For disconnect blame assignment
	UnsignedInt getPingFrame();
	Int getPingsSent();
	Int getPingsRecieved();

protected:
	void GetCommandsFromCommandList();														///< Remove commands from TheCommandList and put them on the Network command list.
	void SendCommandsToConnectionManager();												///< Send the new commands to the ConnectionManager
	Bool AllCommandsReady(UnsignedInt frame);											///< Do we have all the commands for the given frame?
	void RelayCommandsToCommandList(UnsignedInt frame);						///< Put the commands for the given frame onto TheCommandList.
	Bool isTransferCommand(GameMessage *msg);											///< Is this a command that needs to be transfered to the other clients?
	Bool processCommand(GameMessage *msg);												///< Whatever needs to be done as a result of this command, do it now.
	Bool noteLogicFrameAdvance();																	///< Send frame info for every frame the logic has moved past; FALSE while still in pregame.
	void processFrameSynchronizedNetCommand(NetCommandRef *msg);	///< If there is a network command that needs to be executed at the same frame number on all clients, it happens here.
	void processRunAheadCommand(NetRunAheadCommandMsg *msg);			///< Do what needs to be done when we get a new run ahead command.
	void processDestroyPlayerCommand(NetDestroyPlayerCommandMsg *msg);	///< Do what needs to be done when we need to destroy a player.
	void endOfGameCheck();																				///< Checks to see if its ok to leave this game.  If it is, send the apropriate command to the game logic.
	Bool timeForNewFrame();
	Bool feedRejoinFrame();																				///< -rejoin: TRUE while frames still come from the replay
	Bool openRejoinFeed();
	Bool m_rejoinFeedOpen;																				///< the replay is open: on this disk from the start, or once the room sent it
	RejoinStartMatch m_rejoinStartMatch;
	AsciiString m_rejoinPath;
	Bool finishRejoin( UnsignedInt seats, UnsignedInt router );		///< from here frames come over the network; FALSE when it gave up instead
	UnsignedInt m_rejoinRequestMS;																///< when the room was last asked to take the seat back
	Bool m_rejoinFirstOrder;																			///< the next order this seat sends is its first since it came back
	void abandonRejoin( const char *why );												///< stop playing and quit, never join
	Bool m_rejoinAbandoned;

	ConnectionManager *m_conMgr;																	///< The connection manager object

	Bool m_rejoinFeeding;																					///< -rejoin: the frames below m_rejoinFrame come from the replay
	UnsignedInt m_rejoinFrame;																		///< the frame the seat writing the replay stands on
	UnsignedInt m_rejoinStartMS;
	std::map<UnsignedInt, UnsignedInt> m_rejoinOwnCRCs;						///< our CRC by the frame it is of, until the recorded one comes
	std::map<UnsignedInt, UnsignedInt> m_rejoinComparedCRCs;			///< ours already held against one seat's, for the other seats' of the same frame
	Int m_rejoinCRCsCompared;
	Int m_rejoinCRCsDiffering;

	/* Rejoin checkpoints (below, with the rejoin).  Every seat of a network match writes its world to disk at
		 an interval; a seat coming back loads its newest one and plays only the frames after it, then holds
		 every CRC it computes against the ones the room recorded, and starts again from frame 0 on any
		 difference. */
	void noteRejoinCheckpoint();																	///< every update: a checkpoint due on this frame
	void saveRejoinCheckpoint( UnsignedInt frame );
	Bool loadRejoinCheckpoint();																	///< FALSE when the seat has to start from frame 0 instead
	Bool findRejoinCheckpoint();																	///< this seat's newest whole checkpoint of the match into m_rejoinCheckpoint
	void restartRejoinFromFrameZero( const char *why );
	UnsignedInt m_checkpointNextFrame;														///< the frame the next checkpoint is taken on, 0 before the first
	size_t m_checkpointLastBytes;																	///< the last one's size, reserved for the next
	AsciiString m_checkpointFolder;																///< this match's, empty until the first
	std::vector<UnsignedInt> m_checkpointFrames;									///< the frames written this match, oldest first
	RejoinCheckpointMeta *m_rejoinCheckpoint;											///< the checkpoint this seat comes back from, NULL for none
	Bool m_rejoinCheckpointPending;																///< found, and loaded once the match is
	UnsignedInt m_rejoinFromFrame;																///< the frame the feed plays from: the checkpoint's, 0 from the start
	Bool m_rejoinMatchStarted;																		///< the match is started here, not by the log's arrival

	UnsignedInt m_lastFrame;																			///< The last game logic frame that was processed.

	NetLocalStatus m_localStatus;															///< My local status as a player in this game.

	Int m_runAhead;																						///< The current run ahead of the game.
	Int m_frameRate;
	Int m_lastExecutionFrame;																	///< The highest frame number that a command could have been executed on.
	Int m_lastFrameCompleted;
	Bool m_didSelfSlug;
	time_t m_stallStart;																			///< When the frame we are waiting on stopped being ready; 0 when not waiting.
	Int m_framesRun;																					///< Logic frames released since the last pacing line in the log.
	Int m_framesSlugged;																			///< How many of those the self-slug held back.
	Int m_longStalls;																					///< Waits on somebody else's commands of 300 ms or more, same window.
	Int m_cushionSum;																					///< The self-slug's cushion reading, summed over the frames of the same window.
	Int m_cushionSamples;
	Int64 m_perfCountFreq;														///< The frequency of the performance counter.

	Int64 m_nextFrameTime;														///< When did we execute the last frame?  For slugging the GameLogic...

	Bool m_frameDataReady;																		///< Is the frame data for the next frame ready to be executed by TheGameLogic?

	// CRC info
	Bool m_checkCRCsThisFrame;
	Bool m_sawCRCMismatch;
	std::vector<UnsignedInt> m_CRC[MAX_SLOTS];
	std::list<Int> m_playersToDisconnect;
	GameWindow *m_messageWindow;

#if defined(_DEBUG) || defined(_INTERNAL)
	Bool m_networkOn;
#endif
};

UnsignedInt Network::getPingFrame()
{
	return (m_conMgr)?m_conMgr->getPingFrame():0;
}

Int Network::getPingsSent()
{
	return (m_conMgr)?m_conMgr->getPingsSent():0;
}

Int Network::getPingsRecieved()
{
	return (m_conMgr)?m_conMgr->getPingsRecieved():0;
}

Bool Network::isPlayerConnected( Int playerID ) {
	if (playerID == getLocalPlayerID()) {
		return m_localStatus == NETLOCALSTATUS_INGAME || m_localStatus == NETLOCALSTATUS_LEAVING;
	}
	return m_conMgr->isPlayerConnected(playerID);
}


// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/**
 * This creates a network object and returns it.
 */
NetworkInterface *NetworkInterface::createNetwork() 
{
	return NEW Network;
}

////////////////////////////////////////////////////////////////////////////////

/**
 * The constructor.
 */
Network::Network()
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_checkCRCsThisFrame = FALSE;
	m_didSelfSlug = FALSE;
	m_stallStart = 0;
	m_framesRun = 0;
	m_framesSlugged = 0;
	m_longStalls = 0;
	m_cushionSum = 0;
	m_cushionSamples = 0;
	m_frameDataReady = FALSE;
	m_sawCRCMismatch = FALSE;
	//
	m_rejoinFeeding = FALSE;
	m_rejoinAbandoned = FALSE;
	m_rejoinFeedOpen = FALSE;
	m_rejoinStartMatch = NULL;
	m_rejoinFrame = 0;
	m_rejoinStartMS = 0;
	m_rejoinRequestMS = 0;
	m_rejoinFirstOrder = FALSE;
	m_rejoinCRCsCompared = 0;
	m_rejoinCRCsDiffering = 0;
	m_checkpointNextFrame = 0;
	m_checkpointLastBytes = 0;
	m_rejoinCheckpoint = NULL;
	m_rejoinCheckpointPending = FALSE;
	m_rejoinFromFrame = 0;
	m_rejoinMatchStarted = FALSE;

	m_conMgr = NULL;
	m_messageWindow = NULL;

#if defined(_DEBUG) || defined(_INTERNAL)
	m_networkOn = TRUE;
#endif
}

/**
 * The destructor.
 */
static void finishRejoinCheckpointWriter( void );		// below, with the rejoin checkpoints
static void dropRejoinCheckpoints( const AsciiString &folder );

Network::~Network()
{
	deinit();
	finishRejoinCheckpointWriter();
	/* The match is over here or left by the menus: nothing comes back to it.  A copy that crashed or was
		 killed never gets here, and its checkpoints stay for the copy that comes back. */
	if (m_checkpointFolder.isNotEmpty() && !m_rejoinFeeding) {
		dropRejoinCheckpoints( m_checkpointFolder );
	}
	delete m_rejoinCheckpoint;
	m_rejoinCheckpoint = NULL;
}

/**
 * This basically releases all the memory.
 */
Bool Network::deinit( void )
{
	if (m_conMgr)
	{
		m_conMgr->destroyGameMessages();
		delete m_conMgr;
		m_conMgr = NULL;
	}
	if (m_messageWindow) {
		TheWindowManager->winDestroy(m_messageWindow);
		m_messageWindow = NULL;
	}

	return true;
}

/**
 * Takes the network back to the initial state.
 */
void Network::reset() {
	init();
}

/**
 * Initializes all the network subsystems.
 */
void Network::init()
{
	if (!deinit())
	{
		DEBUG_LOG(("Could not deinit network prior to init!\n"));
		return;
	}

	m_conMgr = NEW ConnectionManager;
	m_conMgr->init();

	m_lastFrame = 0;
	m_runAhead = min(max((Int)LOGICFRAMES_PER_SECOND, MIN_RUNAHEAD), MAX_FRAMES_AHEAD/2); ///< a second, until the router's first run-ahead command
	m_frameRate = LOGICFRAMES_PER_SECOND;
	m_lastExecutionFrame = m_runAhead - 1; // subtract 1 since we're starting on frame 0
	m_lastFrameCompleted = m_runAhead - 1; // subtract 1 since we're starting on frame 0
	m_frameDataReady = FALSE;
	m_didSelfSlug = FALSE;
	m_stallStart = 0;
	m_framesRun = 0;
	m_framesSlugged = 0;
	m_longStalls = 0;
	m_cushionSum = 0;
	m_cushionSamples = 0;

	m_localStatus = NETLOCALSTATUS_PREGAME;

	m_perfCountFreq = Clock_Ticks_Per_Second();
	m_nextFrameTime = 0;
	m_sawCRCMismatch = FALSE;
	m_checkCRCsThisFrame = FALSE;
	m_rejoinFeeding = FALSE;
	m_rejoinAbandoned = FALSE;
	m_rejoinOwnCRCs.clear();

	DEBUG_LOG(("Network timing values:\n"));
	DEBUG_LOG(("NetworkFPSHistoryLength: %d\n", TheGlobalData->m_networkFPSHistoryLength));
	DEBUG_LOG(("NetworkLatencyHistoryLength: %d\n", TheGlobalData->m_networkLatencyHistoryLength));
	DEBUG_LOG(("NetworkRunAheadMetricsTime: %d\n", TheGlobalData->m_networkRunAheadMetricsTime));
	DEBUG_LOG(("NetworkCushionHistoryLength: %d\n", TheGlobalData->m_networkCushionHistoryLength));
	DEBUG_LOG(("NetworkRunAheadSlack: %d\n", TheGlobalData->m_networkRunAheadSlack));
	DEBUG_LOG(("NetworkKeepAliveDelay: %d\n", TheGlobalData->m_networkKeepAliveDelay));
	DEBUG_LOG(("NetworkDisconnectTime: %d\n", TheGlobalData->m_networkDisconnectTime));
	DEBUG_LOG(("NetworkPlayerSilenceTime: %d\n", TheGlobalData->m_networkPlayerSilenceTime));
	DEBUG_LOG(("NetworkStallCeilingTime: %d\n", TheGlobalData->m_networkStallCeilingTime));
	DEBUG_LOG(("NetworkPlayerTimeoutTime: %d\n", TheGlobalData->m_networkPlayerTimeoutTime));
	DEBUG_LOG(("NetworkSilentPlayerDropTime: %d\n", TheGlobalData->m_networkSilentPlayerDropTime));
	DEBUG_LOG(("NetworkDisconnectScreenNotifyTime: %d\n", TheGlobalData->m_networkDisconnectScreenNotifyTime));
	DEBUG_LOG(("Other network stuff:\n"));
	DEBUG_LOG(("FRAME_DATA_LENGTH = %d\n", FRAME_DATA_LENGTH));
	DEBUG_LOG(("FRAMES_TO_KEEP = %d\n", FRAMES_TO_KEEP));


#if defined(_DEBUG) || defined(_INTERNAL)
	m_networkOn = TRUE;
#endif

	return;
}

void Network::setSawCRCMismatch( void )
{
	m_sawCRCMismatch = TRUE;

	TheScriptActions->closeWindows( TRUE );
	// the tick calls this, beside the render: the window list is the render's until the flush, which
	// runs this behind closeWindows' destroy
	LogicThread::callAfterTick( [this]() { m_messageWindow = TheWindowManager->winCreateFromScript("Menus/CRCMismatch.wnd"); } );
	TheScriptEngine->startEndGameTimer();

	TheRecorder->logCRCMismatch();

	// dump GameLogic random seed
	DEBUG_LOG(("GameLogic frame = %d\n", TheGameLogic->getFrame()));
	DEBUG_LOG(("GetGameLogicRandomSeedCRC() = %d\n", GetGameLogicRandomSeedCRC()));

	// dump CRCs
	{
		DEBUG_LOG(("--- GameState Dump ---\n"));
#ifdef DEBUG_CRC
		outputCRCDumpLines();
#endif
		DEBUG_LOG(("------ End Dump ------\n"));
	}
	{
		DEBUG_LOG(("--- DebugInfo Dump ---\n"));
#ifdef DEBUG_CRC
		outputCRCDebugLines();
#endif
		DEBUG_LOG(("------ End Dump ------\n"));
	}
}

/**
 * Take a user list and build the connection queues and player lists and stuff like that.
 */
void Network::parseUserList( const GameInfo *game )
{
	if (!game)
	{
		DEBUG_LOG(("FAILED parseUserList with a NULL game\n"));
		return;
	}

	m_conMgr->parseUserList(game);
	// run at the game's speed, the native tick, until the router's first run-ahead command
	m_frameRate = m_conMgr->getMatchFps();

	// Now that we have the players in this game, we need to reset the FrameData stuff.
	m_conMgr->destroyGameMessages();
	m_conMgr->zeroFrames(1, m_runAhead-1); ///< we zero out m_runAhead frames +1 because the game actually starts at frame 1.
}

/**
 * Guess what, we're starting a game!
 */
void Network::startGame() {
}

/**
 * Tell the network which ip address the user has chosen to use.  Well ok, they probably didn't choose
 * it explicitly, but regardless, this is the one we're going to use.
 */
void Network::setLocalAddress(UnsignedInt ip, UnsignedInt port) {
	DEBUG_ASSERTCRASH(m_conMgr != NULL, ("Connection manager does not exist."));
	if (m_conMgr != NULL) {
		m_conMgr->setLocalAddress(ip, port);
	}
}

/**
 * Tell the network to initialize the transport object
 */
void Network::initTransport() {
	DEBUG_ASSERTCRASH(m_conMgr != NULL, ("Connection manager does not exist."));
	if (m_conMgr != NULL) {
		m_conMgr->initTransport();
	}
}

void Network::attachTransport(Transport *transport) {
	DEBUG_ASSERTCRASH(m_conMgr != NULL, ("Connection manager does not exist."));
	if (m_conMgr != NULL) {
		m_conMgr->attachTransport(transport);
	}
}

/**
 * Does this command need to be transfered to the other game clients?
 */
Bool Network::isTransferCommand(GameMessage *msg) {
	if ((msg != NULL) && ((msg->getType() > GameMessage::MSG_BEGIN_NETWORK_MESSAGES) && (msg->getType() < GameMessage::MSG_END_NETWORK_MESSAGES))) {
		return TRUE;
	}
	return FALSE;
}

/**
 * Take commands from TheCommandList and give them to the connection manager for transport.
 */
void Network::GetCommandsFromCommandList() {
	GameMessage *msg = TheCommandList->getFirstMessage();
	GameMessage *next = NULL;
	while (msg != NULL) {
		next = msg->next();
		if (isTransferCommand(msg)) { // Is this something we should be sending to the other players?
			if (m_localStatus == NETLOCALSTATUS_INGAME) {
				if (m_rejoinFirstOrder && msg->getType() != GameMessage::MSG_LOGIC_CRC) {
					m_rejoinFirstOrder = FALSE;
					DEBUG_LOG(("REJOIN: this seat's first order since it came back, %s for frame %d\n",
						msg->getCommandAsAsciiString().str(), getExecutionFrame()));
				}
				m_conMgr->sendLocalGameMessage(msg, getExecutionFrame());
			}
			TheCommandList->removeMessage(msg); // This does not destroy msg's prev and next pointers, so they should still be valid.
			msg->deleteInstance();
		} else {
			if (processCommand(msg)) {
				TheCommandList->removeMessage(msg);
				msg->deleteInstance();
			}
		}
		msg = next;
	}
}

Int Network::getExecutionFrame() {
	Int logicFrame = TheGameLogic->getFrame() + m_runAhead;
	if (logicFrame > m_lastExecutionFrame) {
		m_lastExecutionFrame = logicFrame;
		return (logicFrame);
	}
	return m_lastExecutionFrame;
}

/**
 * This is where any processing that needs to be done for specific game messages.
 * Also check here to see if the logic frame number has changed to see if we need to
 * send our info for the last frame to the other players.
 * Return true if the message should be "eaten" by the network.
 */
Bool Network::processCommand(GameMessage *msg)
{
	if (!noteLogicFrameAdvance())
		return FALSE;

	// Are we leaving the game?
	// This has to happen after the check to see if this is the start of a new logic frame.
	// The reason is that we have to send all the frame info packets necessary to get to the
	// frame where everyone else is going to see that we left.
	if ((msg->getType() == GameMessage::MSG_CLEAR_GAME_DATA) && (m_localStatus == NETLOCALSTATUS_INGAME)) {
		Int executionFrame = getExecutionFrame();
		DEBUG_LOG(("Network::processCommand - local player leaving, executionFrame = %d, player leaving on frame %d\n", executionFrame, executionFrame+1));

		m_conMgr->handleLocalPlayerLeaving(executionFrame+1);
		m_conMgr->processFrameTick(executionFrame); // This is the last command we will execute, so send the command count.
																								// Also, we are guaranteed not to send any more commands for this frame
																								// since the local status will change to "Leaving" so we don't have to
																								// worry about messing up the other players.
		m_conMgr->processFrameTick(executionFrame+1); // since we send it for executionFrame+1, we need to process both ticks
		m_lastFrameCompleted = executionFrame;
		DEBUG_LOG(("Network::processCommand - player leaving on frame %d\n", executionFrame));
		m_localStatus = NETLOCALSTATUS_LEAVING;
		return TRUE;
	}
	return FALSE;
}

/**
 * EA sent the frame info packets from processCommand, which only runs for a message on
 * TheCommandList, and relied on the client's MSG_FRAME_TICK always being there.  It is, once per
 * client pass - but a pass that is catching up runs several logic frames between two of them, and
 * the frames it ran would go unannounced until the next picture.  update() calls this as well, so
 * the announcement follows the logic frame rather than the render pass.
 */
Bool Network::noteLogicFrameAdvance()
{
	if ((m_lastFrame != TheGameLogic->getFrame()) || (m_localStatus == NETLOCALSTATUS_PREGAME)) {
		// If this is the start of a new game logic frame, then tell the connection manager that the last
		// frame is over and that it should now produce a frame info packet for the other players.

		if (m_localStatus == NETLOCALSTATUS_PREGAME) {
			// a sort-of-hack that prevents extraneous frames from being executed before the game actually starts.
			// Idealy this shouldn't be necessary, but I don't think its hurting anything by being here.
			if (TheGameLogic->getFrame() == 1) {
				m_localStatus = NETLOCALSTATUS_INGAME;
				NetCommandList *netcmdlist = m_conMgr->getFrameCommandList(0); // clear out frame 0 since we skipped it
				netcmdlist->deleteInstance();
			} else {
				return FALSE;
			}
		}

		// Only send frame info packets for frames where we are actually going to be in the game.
		// The reason is so we don't have to wait for frame info packets to be sent that aren't going to
		// matter anyways when we actually try to leave the game ourselves.
		if (m_localStatus == NETLOCALSTATUS_INGAME) {
			Int executionFrame = getExecutionFrame();

			// Send command counts for all the frames we can.
			for (Int i = m_lastFrameCompleted + 1; i < executionFrame; ++i) {
				m_conMgr->processFrameTick(i);
				//DEBUG_LOG(("Network::processCommand - calling processFrameTick for frame %d\n", i));
				m_lastFrameCompleted = i;
			}
		}

		//DEBUG_LOG(("Next Execution Frame - %d, last frame completed - %d\n", getExecutionFrame(), m_lastFrameCompleted));
		m_lastFrame = TheGameLogic->getFrame();
	}
	return TRUE;
}

/**
 * Coming back to a seat.  The seat lost its game in the middle of the match and the room dropped it from
 * the lockstep, its side kept, and played on.  This copy asks the room back in (feedRejoinFrame); the
 * packet router names a frame F that every seat takes it back on (SEATREJOIN), and the room stands on F + 1
 * until it has caught up.  Every frame up to F is played here from the replay of a seat still in the match,
 * on this disk (-rejoin) or sent by that seat when it stands on F + 1 (-rejoinfrom, online), as fast as the
 * engine runs frames: the replay holds every order every seat gave, as each seat's command list carried it.
 * Nothing frame-synchronized is filed meanwhile, and nothing of ours is sent but the request.  On F + 1 the
 * network takes over as if it had played every frame up to it (finishRejoin).
 */
Bool Network::startRejoin( AsciiString replayPath, Int logSlot, RejoinStartMatch startMatch )
{
	if (m_conMgr == NULL) {
		return FALSE;
	}
	m_rejoinFeedOpen = FALSE;
	m_rejoinFrame = 0;
	m_rejoinStartMatch = startMatch;
	m_rejoinMatchStarted = (startMatch == NULL);		// -netgame has started it; online the replay starts it
	m_rejoinPath = replayPath;
	m_rejoinFromFrame = 0;
	m_rejoinCheckpointPending = FALSE;
	TheRecorder->noteRejoinMatch();
	if (logSlot >= MAX_SLOTS) {
		// the replay is on this disk already: a -rejoin test of one machine
		if (!openRejoinFeed()) {
			return FALSE;
		}
	}
	m_rejoinFeeding = TRUE;
	m_rejoinStartMS = Clock_Milliseconds();
	m_rejoinRequestMS = 0;
	m_rejoinOwnCRCs.clear();
	m_rejoinComparedCRCs.clear();
	m_rejoinCRCsCompared = 0;
	m_rejoinCRCsDiffering = 0;
	m_conMgr->setRejoining( TRUE, logSlot, logSlot < MAX_SLOTS ? replayPath : AsciiString::TheEmptyString );
	m_conMgr->setRejoinCheckpointFrame( 0 );

	/* This seat's own checkpoint of the match, when it has one and the log comes over the network: the room
		 is asked only once it is loaded, for the frames after it.  Online nothing has started the match yet,
		 and the checkpoint's own slot list starts it, the way a replay's would. */
	if (logSlot < MAX_SLOTS && findRejoinCheckpoint()) {
		if (m_rejoinStartMatch != NULL) {
			TheRecorder->setRejoinMatch( m_rejoinCheckpoint->options );
			m_rejoinMatchStarted = m_rejoinStartMatch( m_rejoinCheckpoint->options, m_rejoinCheckpoint->gameSpeed );
			if (!m_rejoinMatchStarted) {
				DEBUG_LOG(("REJOIN: the match the checkpoint names could not be started, the whole match it is\n"));
				delete m_rejoinCheckpoint;
				m_rejoinCheckpoint = NULL;
			}
		}
		m_rejoinCheckpointPending = (m_rejoinCheckpoint != NULL);
	}
	DEBUG_LOG(("REJOIN: slot %d comes back, the replay %s, %s\n", getLocalPlayerID(),
		logSlot < MAX_SLOTS ? "to come from the room" : "on this disk",
		m_rejoinCheckpointPending ? "from its own checkpoint" : "from frame 0"));
	return TRUE;
}

/// the replay into the feed; a match not started yet is started from it (startMatch)
Bool Network::openRejoinFeed()
{
	UnsignedInt frames = 0;
	if (!TheRecorder->openRejoinFeed( m_rejoinPath, frames, m_rejoinStartMatch == NULL || m_rejoinMatchStarted )) {
		return FALSE;
	}
	if (frames < 2 || frames < m_rejoinFromFrame) {
		DEBUG_LOG(("REJOIN: the replay holds %u frames, no match to come back to from frame %u\n", frames, m_rejoinFromFrame));
		TheRecorder->closeRejoinFeed();
		return FALSE;
	}
	m_rejoinFeedOpen = TRUE;
	m_rejoinFrame = frames;
	// a log sent whole to a seat on its checkpoint: the frames before it are in the checkpoint already
	if (m_rejoinFromFrame > 0) {
		TheRecorder->skipRejoinFramesBefore( m_rejoinFromFrame );
	}
	DEBUG_LOG(("REJOIN: the replay holds frames %u to %u so far, at %u ms\n", m_rejoinFromFrame, frames - 1, Clock_Milliseconds() - m_rejoinStartMS));
	if (m_rejoinStartMatch != NULL && !m_rejoinMatchStarted) {
		Int speed = 0;
		const AsciiString options = TheRecorder->getRejoinOptions( speed );
		if (!m_rejoinStartMatch( options, speed )) {
			DEBUG_LOG(("REJOIN: the match the replay records could not be started\n"));
			return FALSE;
		}
		m_rejoinMatchStarted = TRUE;
	}
	return TRUE;
}

/**
 * A frame while coming back: TRUE when it is the replay's, FALSE once it is the network's (finishRejoin
 * has run and update goes on as for any frame).
 */
Bool Network::feedRejoinFrame()
{
	// given up: no frame runs until the engine quits
	if (m_rejoinAbandoned) {
		return TRUE;
	}

	/* What the logic and the player put on the list goes nowhere: those frames were played long ago.  Our
		 own CRCs are kept, to hold the recorded ones of this seat against them. */
	GameMessage *msg = TheCommandList->getFirstMessage();
	while (msg != NULL) {
		GameMessage *next = msg->next();
		if (isTransferCommand(msg)) {
			if (msg->getType() == GameMessage::MSG_LOGIC_CRC && msg->getArgumentCount() >= 3) {
				m_rejoinOwnCRCs[(UnsignedInt)msg->getArgument(2)->integer] = (UnsignedInt)msg->getArgument(0)->integer;
			}
			TheCommandList->removeMessage(msg);
			msg->deleteInstance();
		}
		msg = next;
	}

	/* The room is asked to take the seat back, again every two seconds until the seat joins: the router
		 that answered may drop before its frame, and its successor answers with another.  The socket is read
		 for the answers, the room's own account of itself (REJOINROOM) and the log; nothing frame-synchronized
		 is filed meanwhile (setRejoining). */
	m_conMgr->update( FALSE );

	/* The seat's own checkpoint goes in once the match it belongs to is up, and the room is asked only
		 then, for the frames after it: the load takes a second or two, and the room should not stand still
		 for it. */
	if (m_rejoinCheckpointPending) {
		if (!TheGameLogic->isInMultiplayerGame() || TheGameLogic->isLoadingMap()) {
			m_frameDataReady = TRUE;
			return TRUE;
		}
		m_rejoinCheckpointPending = FALSE;
		if (!loadRejoinCheckpoint()) {
			restartRejoinFromFrameZero( "the checkpoint would not load" );
		}
		return TRUE;
	}

	const UnsignedInt now = Clock_Milliseconds();
	if (m_rejoinRequestMS == 0 || now - m_rejoinRequestMS > 2000) {
		m_conMgr->sendRejoinRequest();
		m_rejoinRequestMS = now;
	}

	// the log from the room, once the room stands on this seat's frame and its seat has sent it
	if (!m_rejoinFeedOpen && m_conMgr->hasRejoinLogArrived() && !openRejoinFeed()) {
		abandonRejoin( "the replay the room sent is no replay of this match" );
		return TRUE;
	}

	// the tick that loads the map, and anything before it, has no recorded frame
	if (!TheGameLogic->isInMultiplayerGame() || TheGameLogic->isLoadingMap()) {
		m_frameDataReady = TRUE;
		return TRUE;
	}
	if (!m_rejoinFeedOpen) {
		TheGameLogic->showRejoinProgress( 0, TRUE );
		return TRUE;		// the match is loaded and the log not here yet: no frame runs
	}

	UnsignedInt acceptFrame = 0;
	const Bool accepted = m_conMgr->getRejoinAccept( acceptFrame );
	const UnsignedInt frame = TheGameLogic->getFrame();
	// the frames played against the room's frame, or against what the log holds until the room names one
	const UnsignedInt target = accepted ? acceptFrame + 1 : m_rejoinFrame;
	TheGameLogic->showRejoinProgress( (Int)( (UnsignedInt64)frame * 100 / max( target, (UnsignedInt)1 ) ), FALSE );
	if (accepted) {
		const UnsignedInt joinFrame = acceptFrame + 1;		// SEATREJOIN runs on acceptFrame, the seat's frames count from the next
		if (frame > joinFrame) {
			abandonRejoin( "the room took the seat back on a frame already played here" );
			return TRUE;
		}
		if (frame == joinFrame) {
			// the room as it took the seat back, which follows the answer
			UnsignedInt roomFrame = 0;
			UnsignedInt seats = 0;
			UnsignedInt router = 0;
			if (!m_conMgr->getRejoinRoom( roomFrame, seats, router )) {
				return TRUE;
			}
			return !finishRejoin( seats, router );
		}
	}

	// the room is still on its way to the frame, or this seat is still catching up to what is written
	if (frame >= m_rejoinFrame) {
		const UnsignedInt held = TheRecorder->rejoinFramesHeld();
		if (held > m_rejoinFrame) {
			m_rejoinFrame = held;
		}
	}
	if (frame >= m_rejoinFrame) {
		return TRUE;		// nothing to play yet: no frame runs
	}

	if (!TheRecorder->feedRejoinFrame( frame )) {
		abandonRejoin( "the replay no longer fits the frames played" );
		return TRUE;
	}

	/* Every seat's recorded CRCs against the ones computed here: the seats of a match that held together
		 computed the same CRC for a frame, and the frames after this seat dropped have only the others'.  A CRC
		 goes out a run-ahead after the frame it is of, so ours is always in by the time the recorded one is fed;
		 the recorded ones of frames before a checkpoint have none of ours to meet. */
	for (msg = TheCommandList->getFirstMessage(); msg != NULL; msg = msg->next()) {
		if (msg->getType() != GameMessage::MSG_LOGIC_CRC || msg->getArgumentCount() < 3) {
			continue;
		}
		const UnsignedInt crcFrame = (UnsignedInt)msg->getArgument(2)->integer;
		const UnsignedInt recorded = (UnsignedInt)msg->getArgument(0)->integer;
		UnsignedInt ours = 0;
		std::map<UnsignedInt, UnsignedInt>::iterator own = m_rejoinOwnCRCs.find(crcFrame);
		if (own != m_rejoinOwnCRCs.end()) {
			ours = own->second;
			m_rejoinComparedCRCs[crcFrame] = ours;
			m_rejoinOwnCRCs.erase(own);
		} else {
			std::map<UnsignedInt, UnsignedInt>::const_iterator again = m_rejoinComparedCRCs.find(crcFrame);
			if (again == m_rejoinComparedCRCs.end()) {
				continue;
			}
			ours = again->second;
		}
		++m_rejoinCRCsCompared;
		if (ours != recorded) {
			++m_rejoinCRCsDiffering;
			if (m_rejoinCRCsDiffering <= 5) {
				DEBUG_LOG(("REJOIN: frame %u computes CRC 0x%08X here, slot %d recorded 0x%08X\n", crcFrame, ours, msg->getPlayerIndex(), recorded));
			}
		}
	}
	// the other seats' CRCs of a frame come within a run-ahead of each other
	while (!m_rejoinComparedCRCs.empty() && m_rejoinComparedCRCs.begin()->first + 2 * (UnsignedInt)MAX_FRAMES_AHEAD < frame) {
		m_rejoinComparedCRCs.erase(m_rejoinComparedCRCs.begin());
	}
	/* Played from its own checkpoint, the world came out other than the room's: the load was not exact.  The
		 seat starts its match again from frame 0, and the room, which stands on the frame it took the seat back
		 for, sends the whole log. */
	if (m_rejoinCRCsDiffering > 0 && m_rejoinFromFrame > 0) {
		restartRejoinFromFrameZero( "the frames played from the checkpoint are not the recorded ones" );
		return TRUE;
	}

	m_frameDataReady = TRUE;
	return TRUE;
}

/**
 * The frame the room stands on for this seat, the one after its SEATREJOIN.  The frame data is put back to
 * nothing known: the ring was never used past the opening's zeroed frames, and a zeroed frame reads as
 * ready with no orders.  Every frame below this one counts as announced, since the room played them without
 * this seat; the frames from here to the run-ahead are announced by noteLogicFrameAdvance as the network
 * update goes on.  What the room sent this seat while it caught up waits unacked in the senders' queues and
 * comes in now; a resend request covers what does not.
 */
Bool Network::finishRejoin( UnsignedInt seats, UnsignedInt router )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	/* The seat whose replay this is stands on this frame, waiting for ours, or it has played frames this seat
		 never saw and joining would split the match (an answer meant for an earlier copy of this seat). */
	const UnsignedInt held = TheRecorder->rejoinFramesHeld();
	if (held != frame) {
		DEBUG_LOG(("REJOIN: the replay holds %u frames, the room is not standing on frame %u for this seat\n", held, frame));
		abandonRejoin( "the room has played on past the frame it took this seat back on" );
		return FALSE;
	}
	/* A load that parts from the match can stay hidden from the CRC for thousands of frames, so a checkpoint is
		 trusted only after REJOIN_MIN_VERIFY_FRAMES of frames held against the room's CRCs; a join sooner than
		 that after it, or with none compared, starts from frame 0 instead. */
	if (m_rejoinFromFrame > 0 && (m_rejoinCRCsCompared == 0 || frame < m_rejoinFromFrame + REJOIN_MIN_VERIFY_FRAMES)) {
		restartRejoinFromFrameZero( "the frames played from the checkpoint are too few to trust it" );
		return FALSE;
	}
	if (m_rejoinFromFrame > 0 && m_rejoinCRCsDiffering > 0) {
		restartRejoinFromFrameZero( "the frames played from the checkpoint are not the recorded ones" );
		return FALSE;
	}
	TheRecorder->closeRejoinFeed();

	DEBUG_LOG(("REJOIN: played frames %u to %u %s in %u ms; %d of the room's recorded CRCs held against ours, %d differ; %d computed with none recorded\n",
		m_rejoinFromFrame, frame - 1, m_rejoinFromFrame > 0 ? "from this seat's checkpoint" : "from the start",
		Clock_Milliseconds() - m_rejoinStartMS, m_rejoinCRCsCompared, m_rejoinCRCsDiffering, (Int)m_rejoinOwnCRCs.size()));
	m_rejoinOwnCRCs.clear();
	m_rejoinComparedCRCs.clear();
	// a world that already differs would only be found a run-ahead into the match, by everybody
	if (m_rejoinCRCsDiffering > 0) {
		abandonRejoin( "the world played here is not the recorded one" );
		return FALSE;
	}

	m_rejoinFeeding = FALSE;
	m_conMgr->takeRejoinAccept( seats, router );
	m_conMgr->forgetFrames();
	m_localStatus = NETLOCALSTATUS_INGAME;
	m_lastFrame = frame - 1;
	m_lastFrameCompleted = frame - 1;
	m_lastExecutionFrame = frame - 1;
	m_nextFrameTime = 0;
	m_stallStart = 0;
	m_rejoinFirstOrder = TRUE;
	TheGameLogic->endRejoinScreen();
	TheInGameUI->message(UnicodeString(u"%ls"), TheGameText->fetch("Network:BackInMatch").str());
	DEBUG_LOG(("REJOIN: joining on frame %u as slot %d, run ahead %d\n", frame, getLocalPlayerID(), m_runAhead));
	return TRUE;
}

/**
 * Coming back failed.  Joining anyway would put a world that differs, or one that is missing orders, into
 * the room, where it shows as a mismatch for everybody a run-ahead later; so no frame runs any more and
 * the game quits.  The room goes on holding the seat until its hold runs out.
 */
void Network::abandonRejoin( const char *why )
{
	DEBUG_LOG(("REJOIN FAILED on frame %u: %s, quitting\n", TheGameLogic->getFrame(), why));
	m_rejoinFeeding = TRUE;
	m_rejoinAbandoned = TRUE;
	m_frameDataReady = FALSE;
	TheRecorder->closeRejoinFeed();
	TheGameEngine->setQuitting( TRUE );
}

///////////////////////////////////////////////////////////////////////////////
// Rejoin checkpoints
///////////////////////////////////////////////////////////////////////////////

/* How often a seat writes its world to disk in a network match, and how soon the first.  A seat that comes
	 back plays the frames since its newest one; the room stands still while it does.  The seats take theirs on
	 frames apart (a slot's share of the interval), so one seat's save never meets another's in the same frame
	 of the room. */
static const UnsignedInt REJOIN_CHECKPOINT_FRAMES = 60 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt REJOIN_CHECKPOINT_FIRST_FRAMES = 10 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt REJOIN_CHECKPOINTS_KEPT = 5;		///< five minutes back: one older than the verify span is there however late the seat returns
static const UnsignedInt REJOIN_CHECKPOINT_STALE_SECONDS = 2 * 24 * 60 * 60;		///< a crashed match's checkpoints older than this go
static const char *const REJOIN_CHECKPOINT_FOLDER = "RejoinCheckpoints";
static const UnsignedInt REJOIN_CHECKPOINT_MAGIC = 0x4352485A;		// "ZHRC"
static const UnsignedInt REJOIN_CHECKPOINT_VERSION = 1;
static const UnsignedInt REJOIN_CHECKPOINT_END = 0x31444E45;			// "END1"

/// the options without the map contents mask after "M=", which the map's load fills in (as RecorderClass compares them)
static std::string rejoinWithoutMapContents( const AsciiString &options )
{
	std::string text( options.str() );
	const size_t at = text.find( "M=" );
	if (at != std::string::npos && (at == 0 || text[at - 1] == ';') && at + 4 <= text.size())
		text.erase( at + 2, 2 );
	return text;
}

/* Which match and which seat a checkpoint is of.  A LAN match by its slot list, map, seed and options, which
	 the copy started again from the same line (or lobby) has before its match loads; an internet match by the
	 server's match id, which the copy coming back has from match.rejoin.ok before any replay arrives. */
static AsciiString rejoinCheckpointKey( const AsciiString &options, Int slot )
{
	AsciiString key;
	const Bool lan = TheLAN != NULL && TheLAN->GetMyGame() != NULL;
	const AsciiString online = lan ? AsciiString::TheEmptyString : OnlineCurrentMatchId();
	if (online.isNotEmpty())
		key.format( "online %s, slot %d", online.str(), slot );
	else
		key.format( "%s, slot %d", rejoinWithoutMapContents( options ).c_str(), slot );
	return key;
}

static AsciiString rejoinCheckpointRoot( void )
{
	return TheGameState->getFilePathInSaveDirectory( AsciiString( REJOIN_CHECKPOINT_FOLDER ) );
}

static AsciiString rejoinCheckpointFolder( const AsciiString &key )
{
	UnsignedInt hash = 2166136261u;		// FNV-1a: a folder name, nothing rides on its collisions (the .ckpt holds the key)
	for (const char *c = key.str(); *c != 0; ++c)
		hash = (hash ^ (UnsignedByte)*c) * 16777619u;
	AsciiString folder;
	folder.format( "%s\\%08X", rejoinCheckpointRoot().str(), hash );
	return folder;
}

static AsciiString rejoinCheckpointFile( const AsciiString &folder, UnsignedInt frame, const char *extension )
{
	AsciiString path;
	path.format( "%s\\%u.%s", folder.str(), frame, extension );
	return path;
}

static void putU32( std::vector<char> &bytes, UnsignedInt value )
{
	bytes.insert( bytes.end(), (const char *)&value, (const char *)&value + sizeof(value) );
}

static void putString( std::vector<char> &bytes, const AsciiString &text )
{
	putU32( bytes, (UnsignedInt)text.getLength() );
	bytes.insert( bytes.end(), text.str(), text.str() + text.getLength() );
}

/// The .ckpt's layout.  The logic random state starts at byte 52 (rejoin_check.py --corrupt-checkpoint).
static void writeRejoinCheckpointMeta( const RejoinCheckpointMeta &meta, std::vector<char> &bytes )
{
	putU32( bytes, REJOIN_CHECKPOINT_MAGIC );
	putU32( bytes, REJOIN_CHECKPOINT_VERSION );
	putU32( bytes, meta.frame );
	putU32( bytes, meta.exeCRC );
	putU32( bytes, meta.iniCRC );
	putU32( bytes, (UnsignedInt)meta.logicRate );
	putU32( bytes, (UnsignedInt)meta.gameSpeed );
	putU32( bytes, (UnsignedInt)meta.gameMode );
	putU32( bytes, (UnsignedInt)meta.localSlot );
	putU32( bytes, meta.savBytes );
	putU32( bytes, meta.repBytes );
	putU32( bytes, meta.headerBytes );
	putU32( bytes, meta.written );
	for (Int i = 0; i < 6; ++i)
		putU32( bytes, meta.random.seed[i] );
	putU32( bytes, meta.random.baseSeed );
	putString( bytes, meta.key );
	putString( bytes, meta.options );
	putU32( bytes, (UnsignedInt)meta.starts.size() );
	for (size_t i = 0; i < meta.starts.size(); ++i)
	{
		putU32( bytes, meta.starts[i].frame );
		putU32( bytes, meta.starts[i].offset );
	}
	putU32( bytes, REJOIN_CHECKPOINT_END );
}

class RejoinMetaReader
{
public:
	RejoinMetaReader( const std::vector<char> &bytes ) : m_bytes( bytes ), m_at( 0 ), m_ok( TRUE ) {}
	UnsignedInt u32( void )
	{
		UnsignedInt value = 0;
		if (m_at + sizeof(value) > m_bytes.size())
		{
			m_ok = FALSE;
			return 0;
		}
		memcpy( &value, &m_bytes[m_at], sizeof(value) );
		m_at += sizeof(value);
		return value;
	}
	AsciiString text( void )
	{
		const UnsignedInt length = u32();
		if (!m_ok || length > 64 * 1024 || m_at + length > m_bytes.size())
		{
			m_ok = FALSE;
			return AsciiString::TheEmptyString;
		}
		std::string value( &m_bytes[0] + m_at, length );
		m_at += length;
		return AsciiString( value.c_str() );
	}
	Bool ok( void ) const { return m_ok; }
private:
	const std::vector<char> &m_bytes;
	size_t m_at;
	Bool m_ok;
};

static Bool readWholeFile( const AsciiString &path, std::vector<char> &bytes )
{
	FILE *in = zh_fopen( path.str(), "rb" );
	if (in == NULL)
		return FALSE;
	fseek( in, 0, SEEK_END );
	const long length = ftell( in );
	fseek( in, 0, SEEK_SET );
	bytes.resize( length > 0 ? (size_t)length : 0 );
	const Bool read = bytes.empty() || fread( &bytes[0], 1, bytes.size(), in ) == bytes.size();
	fclose( in );
	return read;
}

static long rejoinFileLength( const AsciiString &path )
{
	FILE *in = zh_fopen( path.str(), "rb" );
	if (in == NULL)
		return -1;
	fseek( in, 0, SEEK_END );
	const long length = ftell( in );
	fclose( in );
	return length;
}

static Bool readRejoinCheckpointMeta( const AsciiString &path, RejoinCheckpointMeta &meta )
{
	std::vector<char> bytes;
	if (!readWholeFile( path, bytes ))
		return FALSE;
	RejoinMetaReader in( bytes );
	if (in.u32() != REJOIN_CHECKPOINT_MAGIC || in.u32() != REJOIN_CHECKPOINT_VERSION)
		return FALSE;
	meta.frame = in.u32();
	meta.exeCRC = in.u32();
	meta.iniCRC = in.u32();
	meta.logicRate = (Int)in.u32();
	meta.gameSpeed = (Int)in.u32();
	meta.gameMode = (Int)in.u32();
	meta.localSlot = (Int)in.u32();
	meta.savBytes = in.u32();
	meta.repBytes = in.u32();
	meta.headerBytes = in.u32();
	meta.written = in.u32();
	for (Int i = 0; i < 6; ++i)
		meta.random.seed[i] = in.u32();
	meta.random.baseSeed = in.u32();
	meta.key = in.text();
	meta.options = in.text();
	const UnsignedInt count = in.u32();
	meta.starts.clear();
	for (UnsignedInt i = 0; in.ok() && i < count; ++i)
	{
		RecorderClass::FrameStart start;
		start.frame = in.u32();
		start.offset = in.u32();
		meta.starts.push_back( start );
	}
	return in.u32() == REJOIN_CHECKPOINT_END && in.ok();
}

/* The disk side runs on a thread of its own: the world is serialised on the frame, between two ticks, and the
	 megabytes go out while the match plays on.  The .ckpt goes last, so a copy that dies part way leaves a
	 checkpoint the next copy passes over. */
struct RejoinCheckpointJob
{
	AsciiString folder;
	UnsignedInt frame;
	std::vector<char> sav;
	AsciiString recordingPath;
	UnsignedInt repBytes;
	std::vector<char> meta;
	std::vector<UnsignedInt> drop;			///< older checkpoints to delete once this one is whole
};

static std::thread theCheckpointWriter;
static std::atomic<bool> theCheckpointWriterBusy( false );

static void finishRejoinCheckpointWriter( void )
{
	if (theCheckpointWriter.joinable())
		theCheckpointWriter.join();
}

static Bool writeRejoinFile( const AsciiString &path, const char *data, size_t length )
{
	FILE *out = zh_fopen( path.str(), "wb" );
	if (out == NULL)
		return FALSE;
	const Bool written = length == 0 || fwrite( data, length, 1, out ) == 1;
	return (fclose( out ) == 0) && written;
}

static void deleteRejoinCheckpoint( const AsciiString &folder, UnsignedInt frame )
{
	zh_remove( rejoinCheckpointFile( folder, frame, "ckpt" ).str() );		// first: what is left is no checkpoint
	zh_remove( rejoinCheckpointFile( folder, frame, "sav" ).str() );
	zh_remove( rejoinCheckpointFile( folder, frame, "rep" ).str() );
}

static void runRejoinCheckpointJob( RejoinCheckpointJob *job )
{
	const UnsignedInt startMS = Clock_Milliseconds();
	std::vector<char> recording;
	Bool ok = readWholeFile( job->recordingPath, recording ) && recording.size() >= job->repBytes;
	ok = ok && writeRejoinFile( rejoinCheckpointFile( job->folder, job->frame, "sav" ), job->sav.empty() ? NULL : &job->sav[0], job->sav.size() );
	ok = ok && writeRejoinFile( rejoinCheckpointFile( job->folder, job->frame, "rep" ), recording.empty() ? NULL : &recording[0], job->repBytes );
	ok = ok && writeRejoinFile( rejoinCheckpointFile( job->folder, job->frame, "ckpt" ), &job->meta[0], job->meta.size() );
	if (ok)
	{
		for (size_t i = 0; i < job->drop.size(); ++i)
			deleteRejoinCheckpoint( job->folder, job->drop[i] );
	}
	else
	{
		deleteRejoinCheckpoint( job->folder, job->frame );
	}
	DEBUG_LOG(( "REJOIN CHECKPOINT frame %u %s in %u ms beside the match\n", job->frame,
		ok ? "on the disk" : "could not be written", Clock_Milliseconds() - startMS ));
	delete job;
	theCheckpointWriterBusy = false;
}

/// every checkpoint file of the folder; the writer is finished first
static void dropRejoinCheckpoints( const AsciiString &folder )
{
	finishRejoinCheckpointWriter();
	std::vector<AsciiString> files;
	TheLocalFileSystem->getFilesInDirectory( folder, AsciiString( "*.*" ), files );
	for (size_t i = 0; i < files.size(); ++i)
	{
		AsciiString path;
		path.format( "%s\\%s", folder.str(), files[i].str() );
		TheLocalFileSystem->deleteFile( path.str() );
	}
	if (!files.empty())
		DEBUG_LOG(( "REJOIN CHECKPOINT %d files of '%s' deleted\n", (Int)files.size(), folder.str() ));
}

/// the checkpoints of matches whose copy crashed and never came back, once they are two days old
static void sweepStaleRejoinCheckpoints( void )
{
	AsciiString root = rejoinCheckpointRoot();
	root.concat( "\\" );
	FilenameList found;
	TheLocalFileSystem->getFileListInDirectory( AsciiString::TheEmptyString, root, AsciiString( "*.ckpt" ), found, TRUE );
	const UnsignedInt now = (UnsignedInt)time( NULL );
	for (FilenameListIter it = found.begin(); it != found.end(); ++it)
	{
		RejoinCheckpointMeta meta;
		const Bool read = readRejoinCheckpointMeta( *it, meta );
		if (read && meta.written + REJOIN_CHECKPOINT_STALE_SECONDS > now)
			continue;
		const std::string base( it->str(), it->getLength() >= 4 ? it->getLength() - 4 : 0 );		// without "ckpt"
		TheLocalFileSystem->deleteFile( (base + "sav").c_str() );
		TheLocalFileSystem->deleteFile( (base + "rep").c_str() );
		TheLocalFileSystem->deleteFile( it->str() );
		DEBUG_LOG(( "REJOIN CHECKPOINT '%s' of an old match deleted\n", it->str() ));
	}
}

/**
 * Every update of a network match: the checkpoint due on this frame.  Called between two ticks, after the
 * commands of the last are off the list and before this frame's go on, so what is saved is the world as the
 * frame starts, the state a seat coming back resumes and plays this frame's recorded commands on.
 */
void Network::noteRejoinCheckpoint()
{
	if (!REJOIN_FROM_CHECKPOINT || m_rejoinFeeding || m_conMgr == NULL || m_localStatus != NETLOCALSTATUS_INGAME
			|| !TheGameLogic->isInMultiplayerGame() || TheGameLogic->isLoadingMap())
		return;
	const UnsignedInt frame = TheGameLogic->getFrame();
	const Int slot = (Int)getLocalPlayerID();
	if (m_checkpointNextFrame == 0)
	{
		UnsignedInt headerBytes = 0, recordedBytes = 0;
		std::vector<RecorderClass::FrameStart> starts;
		if (!TheRecorder->getRecordingIndex( headerBytes, recordedBytes, starts ) || slot < 0 || slot >= MAX_SLOTS)
		{
			DEBUG_LOG(( "REJOIN CHECKPOINT none in this match: no recording to go back to\n" ));
			m_checkpointNextFrame = (UnsignedInt)-1;
			return;
		}
		m_checkpointFolder = rejoinCheckpointFolder( rejoinCheckpointKey( TheRecorder->getRecordedOptions(), slot ) );
		TheLocalFileSystem->createDirectory( TheGameState->getSaveDirectory() );
		TheLocalFileSystem->createDirectory( rejoinCheckpointRoot() );
		TheLocalFileSystem->createDirectory( m_checkpointFolder );
		// a match begun here: whatever this seat kept of an earlier one under the same key is not of it
		if (m_rejoinPath.isEmpty())
			dropRejoinCheckpoints( m_checkpointFolder );
		sweepStaleRejoinCheckpoints();
		m_checkpointNextFrame = frame + REJOIN_CHECKPOINT_FIRST_FRAMES + (UnsignedInt)slot * (REJOIN_CHECKPOINT_FRAMES / MAX_SLOTS);
		DEBUG_LOG(( "REJOIN CHECKPOINT every %u frames into '%s', the first on frame %u\n", REJOIN_CHECKPOINT_FRAMES,
			m_checkpointFolder.str(), m_checkpointNextFrame ));
	}
	if (frame < m_checkpointNextFrame)
		return;
	if (theCheckpointWriterBusy)
	{
		m_checkpointNextFrame = frame + LOGICFRAMES_PER_SECOND;		// the last one is still going to the disk
		return;
	}
	m_checkpointNextFrame = frame + REJOIN_CHECKPOINT_FRAMES;
	saveRejoinCheckpoint( frame );
}

void Network::saveRejoinCheckpoint( UnsignedInt frame )
{
	const UnsignedInt startMS = Clock_Milliseconds();
	finishRejoinCheckpointWriter();

	RejoinCheckpointMeta meta;
	if (!TheRecorder->getRecordingIndex( meta.headerBytes, meta.repBytes, meta.starts ))
		return;
	TheRecorder->flushRecording();		// the bytes the writer copies are on the disk before it starts

	RejoinCheckpointJob *job = new RejoinCheckpointJob;
	// the first of a match has no last one to go by: an early match is 7 to 9 MB through the save path
	const size_t reserve = max( m_checkpointLastBytes + m_checkpointLastBytes / 8, (size_t)( 12 * 1024 * 1024 ) );
	if (!TheGameState->saveCheckpointToMemory( job->sav, reserve ))
	{
		DEBUG_LOG(( "REJOIN CHECKPOINT frame %u could not be saved; no more this match\n", frame ));
		delete job;
		m_checkpointNextFrame = (UnsignedInt)-1;
		return;
	}
	const UnsignedInt savedMS = Clock_Milliseconds();
	const Bool grew = job->sav.capacity() > reserve;
	m_checkpointLastBytes = job->sav.size();

	meta.frame = frame;
	meta.exeCRC = TheGlobalData->m_exeCRC;
	meta.iniCRC = TheGlobalData->m_iniCRC;
	meta.logicRate = LOGICFRAMES_PER_SECOND;
	meta.gameSpeed = GAME_SPEED_PERCENT;
	meta.gameMode = TheGameLogic->getGameMode();
	meta.localSlot = (Int)getLocalPlayerID();
	meta.savBytes = (UnsignedInt)job->sav.size();
	meta.written = (UnsignedInt)time( NULL );
	meta.random = GetGameLogicRandomState();
	meta.options = TheRecorder->getRecordedOptions();
	meta.key = rejoinCheckpointKey( meta.options, meta.localSlot );
	writeRejoinCheckpointMeta( meta, job->meta );

	job->folder = m_checkpointFolder;
	job->frame = frame;
	job->recordingPath = TheRecorder->getRecordingPath();
	job->repBytes = meta.repBytes;
	m_checkpointFrames.push_back( frame );
	while (m_checkpointFrames.size() > REJOIN_CHECKPOINTS_KEPT)
	{
		job->drop.push_back( m_checkpointFrames.front() );
		m_checkpointFrames.erase( m_checkpointFrames.begin() );
	}
	theCheckpointWriterBusy = true;
	theCheckpointWriter = std::thread( runRejoinCheckpointJob, job );
	DEBUG_LOG(( "REJOIN CHECKPOINT frame %u: %u KB of world saved in %u ms on the frame (%s), %u KB of recording, %u ms in all\n",
		frame, meta.savBytes / 1024, savedMS - startMS, grew ? "outgrew its reserve" : "inside its reserve", meta.repBytes / 1024,
		Clock_Milliseconds() - startMS ));
}

/**
 * The seat coming back: this seat's newest checkpoint of this match that is whole, of this build, data, logic
 * rate and slot.  The frames before it are not played again; the room sends the rest.
 */
Bool Network::findRejoinCheckpoint()
{
	delete m_rejoinCheckpoint;
	m_rejoinCheckpoint = NULL;
	if (!REJOIN_FROM_CHECKPOINT)
		return FALSE;
	const Int slot = (Int)getLocalPlayerID();
	const AsciiString key = rejoinCheckpointKey( TheRecorder->getRejoinMatch(), slot );
	const AsciiString folder = rejoinCheckpointFolder( key );
	std::vector<AsciiString> files;
	TheLocalFileSystem->getFilesInDirectory( folder, AsciiString( "*.ckpt" ), files );
	std::vector<RejoinCheckpointMeta> valid;
	UnsignedInt newest = 0;
	RejoinCheckpointMeta best;
	Bool found = FALSE;
	for (size_t i = 0; i < files.size(); ++i)
	{
		AsciiString path;
		path.format( "%s\\%s", folder.str(), files[i].str() );
		RejoinCheckpointMeta meta;
		const char *refusal = NULL;
		if (!readRejoinCheckpointMeta( path, meta ))
			refusal = "it does not read whole";
		else if (meta.key != key)
			refusal = "it is of another match or seat";
		else if (meta.exeCRC != TheGlobalData->m_exeCRC || meta.iniCRC != TheGlobalData->m_iniCRC)
			refusal = "another build or other data wrote it";
		else if (meta.logicRate != LOGICFRAMES_PER_SECOND || !GameSpeed_isAllowed( meta.gameSpeed ) || meta.localSlot != slot)
			refusal = "another logic rate, game speed or slot";
		else if (rejoinFileLength( rejoinCheckpointFile( folder, meta.frame, "sav" ) ) != (long)meta.savBytes
						 || rejoinFileLength( rejoinCheckpointFile( folder, meta.frame, "rep" ) ) != (long)meta.repBytes)
			refusal = "its world or its recording is cut short";
		if (refusal != NULL)
		{
			DEBUG_LOG(( "REJOIN: checkpoint '%s' passed over: %s\n", path.str(), refusal ));
			continue;
		}
		valid.push_back( meta );
		newest = max( newest, meta.frame );
	}
	/* The crashed copy wrote its newest checkpoint before it died, and the room cannot play on without the
		 seat until it drops it, so the room's frame is past the newest one.  A checkpoint REJOIN_MIN_VERIFY_FRAMES
		 older than the newest is that far from the join at least, and finishRejoin holds the span to it. */
	for (size_t i = 0; i < valid.size(); ++i)
	{
		if (valid[i].frame + REJOIN_MIN_VERIFY_FRAMES <= newest && (!found || valid[i].frame > best.frame))
		{
			best = valid[i];
			found = TRUE;
		}
	}
	if (!found)
	{
		DEBUG_LOG(( "REJOIN: no checkpoint of this match in '%s' %u frames older than its newest (%d files), the whole match is played\n",
			folder.str(), REJOIN_MIN_VERIFY_FRAMES, (Int)files.size() ));
		return FALSE;
	}
	m_rejoinCheckpoint = new RejoinCheckpointMeta( best );
	m_checkpointFolder = folder;
	DEBUG_LOG(( "REJOIN: this seat's checkpoint of frame %u, %u KB, in '%s'\n", best.frame, best.savBytes / 1024, folder.str() ));
	return TRUE;
}

/// the reset keeps this network for as long as this lives, an exception included
struct KeepNetworkThroughReset
{
	KeepNetworkThroughReset() { GameEngine_keepNetworkThroughReset( TRUE ); }
	~KeepNetworkThroughReset() { GameEngine_keepNetworkThroughReset( FALSE ); }
};

/**
 * The checkpoint in, the match loaded as it was on its frame: the engine reset that comes first keeps this
 * network, and the recording goes on from the copy the checkpoint kept.  The room is asked from here, for the
 * frames from the checkpoint on.  FALSE leaves what the reset left; the caller starts the match again.
 */
Bool Network::loadRejoinCheckpoint()
{
	const RejoinCheckpointMeta meta = *m_rejoinCheckpoint;
	const UnsignedInt startMS = Clock_Milliseconds();
	std::vector<char> recording;
	if (!readWholeFile( rejoinCheckpointFile( m_checkpointFolder, meta.frame, "rep" ), recording ) || recording.size() != meta.repBytes)
	{
		DEBUG_LOG(( "REJOIN: the checkpoint's recording would not read\n" ));
		return FALSE;
	}
	const AsciiString onlineMatch = TheRecorder->getClaimedOnlineMatch();
	const AsciiString savPath = rejoinCheckpointFile( m_checkpointFolder, meta.frame, "sav" );
	Bool loaded = FALSE;
	try
	{
		KeepNetworkThroughReset keep;
		loaded = TheGameState->loadRejoinCheckpoint( savPath, [&]() {
			return TheRecorder->resumeRecording( recording, meta.headerBytes, meta.starts, meta.frame, meta.gameMode, meta.options, onlineMatch );
		} );
	}
	catch( ... )
	{
		loaded = FALSE;		// the caller starts the match again from frame 0
	}
	if (!loaded)
	{
		DEBUG_LOG(( "REJOIN: the checkpoint of frame %u would not load\n", meta.frame ));
		return FALSE;
	}
	SetGameLogicRandomState( meta.random );
	if (TheGameLogic->getFrame() != meta.frame || !TheGameLogic->isInMultiplayerGame())
	{
		DEBUG_LOG(( "REJOIN: the checkpoint of frame %u loaded on frame %u, mode %d\n", meta.frame, TheGameLogic->getFrame(), TheGameLogic->getGameMode() ));
		return FALSE;
	}
	m_rejoinFromFrame = meta.frame;
	m_conMgr->setRejoinCheckpointFrame( meta.frame );
	m_rejoinRequestMS = 0;		// the room is asked now
	m_rejoinOwnCRCs.clear();
	m_rejoinComparedCRCs.clear();
	m_rejoinCRCsCompared = 0;
	m_rejoinCRCsDiffering = 0;
	DEBUG_LOG(( "REJOIN: loaded this seat's checkpoint of frame %u in %u ms, %u ms after the start; the room is asked for the frames from it\n",
		meta.frame, Clock_Milliseconds() - startMS, Clock_Milliseconds() - m_rejoinStartMS ));
	return TRUE;
}

/**
 * The checkpoint did not hold: it would not load, or the frames played from it came out other than the
 * room's.  The seat starts its match again from frame 0 and asks for the whole log, a later attempt, and the
 * seat the log comes from sends it again, since the room still stands on the frame it took this seat back
 * for.  The checkpoints go: whatever made this one part from the match would make the next part too.
 */
void Network::restartRejoinFromFrameZero( const char *why )
{
	DEBUG_LOG(( "REJOIN: from frame 0 after all, on frame %u: %s\n", TheGameLogic->getFrame(), why ));
	TheRecorder->closeRejoinFeed();
	if (m_checkpointFolder.isNotEmpty())
		dropRejoinCheckpoints( m_checkpointFolder );
	delete m_rejoinCheckpoint;
	m_rejoinCheckpoint = NULL;
	m_rejoinCheckpointPending = FALSE;
	m_rejoinFeedOpen = FALSE;
	m_rejoinFrame = 0;
	m_rejoinFromFrame = 0;
	m_rejoinOwnCRCs.clear();
	m_rejoinComparedCRCs.clear();
	m_rejoinCRCsCompared = 0;
	m_rejoinCRCsDiffering = 0;
	m_rejoinRequestMS = 0;
	m_frameDataReady = FALSE;
	m_conMgr->askRejoinLogAgain( 0 );

	const Int speed = GAME_SPEED_PERCENT;
	{
		KeepNetworkThroughReset keep;
		TheGameEngine->reset();
	}
	/* The reset leaves the mode the checkpoint's match had, and feedRejoinFrame would take the empty world
		 for the loaded match and play the log on it.  No match until the new one's map is in, as at a start. */
	TheGameLogic->setGameMode( GAME_NONE );
	if (m_rejoinStartMatch != NULL)
	{
		m_rejoinMatchStarted = FALSE;		// the whole replay starts it as it arrives, as it does online
		return;
	}
	// a LAN match, started as its lobby or -netgame started it the first time
	GameInfo *game = (TheLAN != NULL) ? TheLAN->GetMyGame() : NULL;
	if (game == NULL)
	{
		abandonRejoin( "the match cannot be started again" );
		return;
	}
	/* The first start drew the random sides, colours and spots into the slots.  The recording opens before
		 the new start puts the drawn ones back to random, and a recording that names them is another match to
		 every seat's check and plays back other draws; so they go back here, as a restarted skirmish's do. */
	game->handleOriginalSetups();
	TheWritableGlobalData->m_pendingFile = game->getMap();
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_LAN );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );
	msg->appendIntegerArgument( 0 );
	GameEngine_appendNewGameRates( msg, speed );
	InitRandom( game->getSeed() );
	m_rejoinMatchStarted = TRUE;
}

/**
 * returns true if all the commands are ready for the given frame.
 */
Bool Network::AllCommandsReady(UnsignedInt frame) {
	if (m_conMgr == NULL) {
		return TRUE;
	}

	if (m_localStatus == NETLOCALSTATUS_PREGAME) {
		return TRUE;
	}

	if (m_localStatus == NETLOCALSTATUS_POSTGAME) {
		return TRUE;
	}

	return m_conMgr->allCommandsReady(frame);// && m_conMgr->allCRCsReady(frame);
}

/**
 * Take commands from the connection manager and put them on TheCommandList.
 * The commands need to be put on in the same order across all clients.
 */
void Network::RelayCommandsToCommandList(UnsignedInt frame) {
	if ((m_conMgr == NULL) || (m_localStatus == NETLOCALSTATUS_PREGAME)) {
		return;
	}
	m_checkCRCsThisFrame = FALSE;
	NetCommandList *netcmdlist = m_conMgr->getFrameCommandList(frame);
	NetCommandRef *msg = netcmdlist->getFirstMessage();
	while (msg != NULL) {
		NetCommandType cmdType = msg->getCommand()->getNetCommandType();
		if (cmdType == NETCOMMANDTYPE_GAMECOMMAND) {
			//DEBUG_LOG(("Network::RelayCommandsToCommandList - appending command %d of type %s to command list on frame %d\n", msg->getCommand()->getID(), ((NetGameCommandMsg *)msg->getCommand())->constructGameMessage()->getCommandAsAsciiString().str(), TheGameLogic->getFrame()));
			TheCommandList->appendMessage(((NetGameCommandMsg *)msg->getCommand())->constructGameMessage());
		} else {
			processFrameSynchronizedNetCommand(msg);
		}
		msg = msg->getNext();
	}

	for (std::list<Int>::iterator selfDestructIt = m_playersToDisconnect.begin(); selfDestructIt != m_playersToDisconnect.end(); ++selfDestructIt)
	{
		//Int playerToDisconnect = *selfDestructIt;
		//GameMessage *msg = newInstance(GameMessage)( GameMessage::MSG_SELF_DESTRUCT );
		//msg->friend_setPlayerIndex(playerToDisconnect);
		//msg->appendBooleanArgument(TRUE);
		//TheCommandList->appendMessage(msg);
	}
	m_playersToDisconnect.clear();

	netcmdlist->deleteInstance();
}

/**
 * This is where network commands that need to be executed on the same frame should be executed.
 */
void Network::processFrameSynchronizedNetCommand(NetCommandRef *msg) {
	NetCommandMsg *cmdMsg = msg->getCommand();
	if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_PLAYERLEAVE) {
		PlayerLeaveCode retval = m_conMgr->processPlayerLeave((NetPlayerLeaveCommandMsg *)cmdMsg);
		if (retval == PLAYERLEAVECODE_LOCAL) {
			DEBUG_LOG(("Network::processFrameSynchronizedNetCommand - Local player left the game on frame %d.\n", TheGameLogic->getFrame()));
			m_localStatus = NETLOCALSTATUS_LEFT;
		} else if (retval == PLAYERLEAVECODE_PACKETROUTER) {
			DEBUG_LOG(("Network::processFrameSynchronizedNetCommand - Packet router left the game on frame %d\n", TheGameLogic->getFrame()));
		} else {
			DEBUG_LOG(("Network::processFrameSynchronizedNetCommand - Client left the game on frame %d\n", TheGameLogic->getFrame()));
		}
	}
	else if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_RUNAHEAD) {
		NetRunAheadCommandMsg *netmsg = (NetRunAheadCommandMsg *)cmdMsg;
		processRunAheadCommand(netmsg);
		DEBUG_LOG(("command to set run ahead to %d and frame rate to %d on frame %d actually executed on frame %d\n", netmsg->getRunAhead(), netmsg->getFrameRate(), netmsg->getExecutionFrame(), TheGameLogic->getFrame()));
	}
	else if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_SEATREJOIN) {
		// every seat on this one frame: the seat plays again from the next
		m_conMgr->readmitPlayer( (Int)((NetDestroyPlayerCommandMsg *)cmdMsg)->getPlayerIndex(), TheGameLogic->getFrame() + 1 );
	}
	else if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_DESTROYPLAYER) {
		NetDestroyPlayerCommandMsg *netmsg = (NetDestroyPlayerCommandMsg *)cmdMsg;
		processDestroyPlayerCommand(netmsg);
		//DEBUG_LOG(("CRC command (%8.8X) on frame %d actually executed on frame %d\n", netmsg->getCRC(), netmsg->getExecutionFrame(), TheGameLogic->getFrame()));
	}
}

void Network::processRunAheadCommand(NetRunAheadCommandMsg *msg) {
	m_runAhead = msg->getRunAhead();
	m_frameRate = msg->getFrameRate();
	/* No send grouping: whatever is queued goes out on the pass that queued it.  EA held every send
		 for half a run-ahead, later capped at one logic frame, and acks waited it out too, so each
		 round trip the game measured carried up to two frames of its own waiting on top of the wire.
		 The measured round trip sizes the run-ahead, so that waiting was input delay: loopback measured
		 14 to 18 ms of srtt with nothing on the wire and 5 to 7 ms without it, a 50 ms ping 79 ms and
		 65 ms.  It costs packets: three seats through the relay went from 56 to 131 a second a seat,
		 66 to each peer, 18 KB a second into the relay for the whole room. */
	m_conMgr->setFrameGrouping(1);
}

void Network::processDestroyPlayerCommand(NetDestroyPlayerCommandMsg *msg)
{
	UnsignedInt playerIndex = msg->getPlayerIndex();
	DEBUG_ASSERTCRASH(playerIndex < MAX_SLOTS, ("Bad player index"));
	if (playerIndex >= MAX_SLOTS)
		return;

	AsciiString playerName;
	playerName.format("player%d", playerIndex);
	Player *pPlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
	if (pPlayer)
	{
		GameMessage *msg = newInstance(GameMessage)(GameMessage::MSG_SELF_DESTRUCT);
		msg->appendBooleanArgument(FALSE);
		msg->friend_setPlayerIndex(pPlayer->getPlayerIndex());
		TheCommandList->appendMessage(msg);
	}
	m_conMgr->notePlayerGone((Int)playerIndex);

	DEBUG_LOG(("Saw DestroyPlayer from %d about %d for frame %d on frame %d\n", msg->getPlayerID(), msg->getPlayerIndex(),
		msg->getExecutionFrame(), TheGameLogic->getFrame()));
}

/**
 * Service queues, process message stream, etc
 */
void Network::update( void )
{
//
// 1. Take Commands off TheCommandList, hand them off to the ConnectionManager.
// 2. Call ConnectionManager->update;
// 3. Check to see if all the commands for the next frame are there.
// 4. If all commands are there, put that frame's commands on TheCommandList.
//
	m_frameDataReady = FALSE;

#if defined(_DEBUG) || defined(_INTERNAL)
	if (m_networkOn == FALSE) {
		return;
	}
#endif

	if (m_rejoinFeeding && feedRejoinFrame()) {
		return;
	}

	noteLogicFrameAdvance();
	GetCommandsFromCommandList(); // Remove commands from TheCommandList and send them to the connection manager.
	// between two ticks, before this frame's commands go on the list: the world as the frame starts
	noteRejoinCheckpoint();
	if (m_conMgr != NULL) {
		if (m_localStatus == NETLOCALSTATUS_INGAME) {
			m_conMgr->updateRunAhead(m_runAhead, m_frameRate, m_didSelfSlug, getExecutionFrame());
			m_didSelfSlug = FALSE;
			m_conMgr->sendPendingRejoinLog();
			m_conMgr->updateAbsentSeats();
		}
		//m_conMgr->update(); // Do the priority thing, packetize thing. This also calls the Transport::update function.
									 // depacketizes the incoming packets and puts them on the Network command list.
	}

	liteupdate();

	if ((m_localStatus == NETLOCALSTATUS_LEFT)) {// || (m_localStatus == NETLOCALSTATUS_LEAVING)) {
		endOfGameCheck();
	}

	/* Wall time spent here waiting on somebody else's commands.  FrameMetrics takes it out of the
		 logic rate this machine reports, which is a capacity, not a count of frames that happened. */
	Bool ready = AllCommandsReady(TheGameLogic->getFrame());
	if (m_conMgr != NULL && m_localStatus == NETLOCALSTATUS_INGAME) {
		if (!ready && m_stallStart == 0) {
			m_stallStart = Clock_Milliseconds();
		} else if (ready && m_stallStart != 0) {
			time_t stalledMS = Clock_Milliseconds() - m_stallStart;
			m_conMgr->addNetworkStall(stalledMS);
			if (stalledMS >= 300) {
				++m_longStalls;
			}
			m_stallStart = 0;
		}
	}

	if (ready) { // If all the commands are ready for the next frame...
		m_conMgr->handleAllCommandsReady();
//		DEBUG_LOG(("Network::update - frame %d is ready\n", TheGameLogic->getFrame()));
		if (timeForNewFrame()) { // This needs to come after any other pre-frame execution checks as this changes the timing variables.
			RelayCommandsToCommandList(TheGameLogic->getFrame());	// Put the commands for the next frame on TheCommandList.
			m_frameDataReady = TRUE; // Tell the GameEngine to run the commands for the new frame.

			/* How the room is actually paced, one line every ten seconds of logic: the frames the
				 self-slug held back are the run-ahead's margin running out without a stall, the long
				 waits are it running out with one.  Both say whether the window fits the link. */
			++m_framesRun;
			if (m_didSelfSlug) {
				++m_framesSlugged;
			}
			Int cushion = m_conMgr->getMinimumCushion();
			if (cushion >= 0) {
				m_cushionSum += cushion;
				++m_cushionSamples;
			}
			if (m_framesRun >= 10 * LOGICFRAMES_PER_SECOND) {
				DEBUG_LOG(("Network::update - frame %d: %d of the last %d frames self-slugged, %d stalls of 300 ms or more, run ahead %d, mean cushion %.2f\n",
					TheGameLogic->getFrame(), m_framesSlugged, m_framesRun, m_longStalls, m_runAhead,
					m_cushionSamples ? (Real)m_cushionSum / (Real)m_cushionSamples : -1.0f));
				m_framesRun = 0;
				m_framesSlugged = 0;
				m_longStalls = 0;
				m_cushionSum = 0;
				m_cushionSamples = 0;
			}
		}
	}
}

void Network::liteupdate() {

#if defined(_DEBUG) || defined(_INTERNAL)
	if (m_networkOn == FALSE) {
		return;
	}
#endif

	// coming back: feedRejoinFrame pumps the socket itself, holding back what is frame-synchronized
	if (m_rejoinFeeding) {
		return;
	}

	if (m_conMgr != NULL) {
		if (m_localStatus == NETLOCALSTATUS_PREGAME) {
			m_conMgr->update(FALSE);
		} else {
			m_conMgr->update(TRUE);
		}
	}
}

/**
 * GameEngine::execute calls this while the picture cap waits, between two passes.  The frames the
 * last tick finished are announced and the socket is read and written, acks included, so nothing
 * sits there for the rest of a capped picture.  Nothing goes on or comes off TheCommandList and no
 * frame of the network clock is spent: when a frame runs and with which commands is still update()'s
 * alone.  The tick is not running here, since the pass flushed it before it returned, and the logic
 * frame cannot move until the next pass, so what update() stamps next is what it would have stamped.
 */
void Network::serviceSockets() {
#if defined(_DEBUG) || defined(_INTERNAL)
	if (m_networkOn == FALSE) {
		return;
	}
#endif
	if (m_conMgr == NULL || m_localStatus != NETLOCALSTATUS_INGAME) {
		return;
	}
	noteLogicFrameAdvance();
	liteupdate();
}

void Network::endOfGameCheck() {
	if (m_conMgr != NULL) {
		if (m_conMgr->canILeave()) {
			m_conMgr->disconnectLocalPlayer();
			TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
			m_localStatus = NETLOCALSTATUS_POSTGAME;

			DEBUG_LOG(("Network::endOfGameCheck - about to show the shell\n"));
		}
#if defined(_DEBUG) || defined(_INTERNAL)
		else {
			m_conMgr->debugPrintConnectionCommands();
		}
#endif
	}
}

Bool Network::timeForNewFrame() {
	Int64 curTime;
	curTime = Clock_Ticks();
	Int64 frameDelay = m_perfCountFreq / m_frameRate;

	/*
	 * If we're pushing up against the edge of our run ahead, we should slow the framerate down a bit
	 * to avoid being frozen by spikes in network lag.  This will happen if another user's computer is
	 * running too far behind us, so we need to slow down to let them catch up.
	 */
	if (m_conMgr != NULL) {
		if (shouldSelfSlug(m_conMgr->getMinimumCushion(), m_runAhead, TheGlobalData->m_networkRunAheadSlack)) {
//			DEBUG_LOG(("Average cushion = %f, run ahead percentage = %f.  Adjusting frameDelay from %I64d to ", cushion, runAheadPercentage, frameDelay));
			frameDelay += frameDelay / 10; // temporarily decrease the frame rate by 20%.
//			DEBUG_LOG(("%I64d\n", frameDelay));
			m_didSelfSlug = TRUE;
//		} else {
//			DEBUG_LOG(("Average cushion = %f, run ahead percentage = %f\n", cushion, runAheadPercentage));
		}
	}

	// Check to see if we can run another frame.
	if (curTime >= m_nextFrameTime) {
//		DEBUG_LOG(("Allowing a new frame, frameDelay = %I64d, curTime - m_nextFrameTime = %I64d\n", frameDelay, curTime - m_nextFrameTime));

//		if (m_nextFrameTime + frameDelay < curTime) {
		/* GameEngine::update pays this debt back with logic frames run between two pictures, up to
			 the same count the single player pacer keeps.  EA forgave it past two frames, when a pass
			 only ever ran one; a slow picture then lost the frames it owed and the whole room slowed
			 down to its rate.  The count comes from the game speed and not from the room's rate: tied
			 to the room, a room that slowed down could pay back less and slowed down further, and a
			 250ms picture took two machines from 30 frames a second to 4.  The game speed is the
			 match's, the native tick, set in parseUserList. */
		if ((m_nextFrameTime + (GameEngine_logicCatchupMaxFrames(m_conMgr->getMatchFps()) * frameDelay)) < curTime) {
			// If we get too far behind on our framerate we need to reset the nextFrameTime thing.
			m_nextFrameTime = curTime;
//			DEBUG_LOG(("Initializing m_nextFrameTime to %I64d\n", m_nextFrameTime));
		} else {
			// Set the soonest possible starting time for the next frame.
			m_nextFrameTime += frameDelay;
//			DEBUG_LOG(("m_nextFrameTime = %I64d\n", m_nextFrameTime));
		}

		return TRUE;
	}
//	DEBUG_LOG(("Slowing down frame rate. frame rate = %d, frame delay = %I64d, curTime - m_nextFrameTime = %I64d\n", m_frameRate, frameDelay, curTime - m_nextFrameTime));
	return FALSE;
}

/**
 * Returns true if the game commands for the next frame have been put on the command list.
 */
Bool Network::isFrameDataReady() {
	return (m_frameDataReady || (m_localStatus == NETLOCALSTATUS_LEFT));
}

/**
 * True while timeForNewFrame() above is the thing deciding when a logic frame runs, which is any
 * time isFrameDataReady() is answering from m_frameDataReady rather than short-circuiting on
 * NETLOCALSTATUS_LEFT.  GameEngine::update asks so it can stand its own wall-clock pacing down and
 * leave the network holding the only clock - two independent gates on the same tick multiply, and
 * the simulation ends up slower than either of them alone.
 */
Bool Network::isPacingLogicFrames() {
	return (m_localStatus != NETLOCALSTATUS_LEFT);
}

/**
 * returns the number of incoming bytes per second averaged over the last 30 sec.
 */
Real Network::getIncomingBytesPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getIncomingBytesPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of incoming packets per second averaged over the last 30 sec.
 */
Real Network::getIncomingPacketsPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getIncomingPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of outgoing bytes per second averaged over the last 30 sec.
 */
Real Network::getOutgoingBytesPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getOutgoingBytesPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of outgoing packets per second averaged over the last 30 sec.
 */
Real Network::getOutgoingPacketsPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getOutgoingPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of bytes received per second that are not from a generals client averaged over 30 sec.
 */
Real Network::getUnknownBytesPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getUnknownBytesPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of packets received per second that are not from a generals client averaged over 30 sec.
 */
Real Network::getUnknownPacketsPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getUnknownPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * returns the smallest packet arrival cushion since this was last called.
 */
UnsignedInt Network::getPacketArrivalCushion( void )
{
	if (m_conMgr)
		return m_conMgr->getPacketArrivalCushion();
	else
		return 0;
}

/**
 * Sends a line of chat to the other players
 */
void Network::sendChat(UnicodeString text, SlotMask playerMask) {
	Int executionFrame = getExecutionFrame();
	m_conMgr->sendChat(text, playerMask, executionFrame);
}

/**
 * Sends a line of chat to the other players using the disconnect manager.
 */
void Network::sendDisconnectChat(UnicodeString text) {
	m_conMgr->sendDisconnectChat(text);
}

// send a file.  woohoo.
void Network::sendFile(AsciiString path, SlotMask playerMask, UnsignedShort commandID)
{
	m_conMgr->sendFile(path, playerMask, commandID);
}

// send a file.  woohoo.
UnsignedShort Network::sendFileAnnounce(AsciiString path, SlotMask playerMask)
{
	return m_conMgr->sendFileAnnounce(path, playerMask);
}

Int Network::getFileTransferProgress(Int playerID, AsciiString path)
{
	return m_conMgr->getFileTransferProgress(playerID, path);
}

Bool Network::areAllQueuesEmpty(void)
{
	return m_conMgr->canILeave();
}

/**
 * Quit the game now.  This should only be called from the disconnect screen.
 */
void Network::quitGame() {
	if (m_conMgr != NULL) {
		m_conMgr->quitGame();
	}

	// Blow up / Transfer your units when you quit.  Like a normal quit menu quit, which spares a
	// side that has already won: victory is read off who still stands (exitQuitMenu has the why).
	if (!TheVictoryConditions->isLocalAlliedVictory())
	{
		GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_SELF_DESTRUCT);
		msg->appendBooleanArgument(TRUE);
	}

	TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
	m_localStatus = NETLOCALSTATUS_POSTGAME;
	DEBUG_LOG(("Network::quitGame - quitting game..."));
}

void Network::selfDestructPlayer(Int index)
{
	m_playersToDisconnect.push_back(index);
}


Bool Network::isPacketRouter( void )
{
	return m_conMgr && m_conMgr->isPacketRouter();
}

/**
 * Register a vote towards a player being disconnected.
 */
void Network::voteForPlayerDisconnect(Int slot) {
	if (m_conMgr != NULL) {
		m_conMgr->voteForPlayerDisconnect(slot);
	}
}

void Network::updateLoadProgress( Int percent )
{
	if (m_conMgr != NULL && !m_rejoinFeeding) {
		m_conMgr->updateLoadProgress( percent );
	}
}

void Network::loadProgressComplete( void )
{
	// -rejoin: nobody else is loading, the others are far into the match
	if (m_rejoinFeeding) {
		TheGameLogic->timeOutGameStart();
		return;
	}
	if (m_conMgr != NULL) {
		m_conMgr->loadProgressComplete();
	}
}

void Network::sendTimeOutGameStart( void )
{
	if (m_conMgr != NULL && !m_rejoinFeeding) {
		m_conMgr->sendTimeOutGameStart();
	}
}


UnsignedInt Network::getLocalPlayerID()
{
if (m_conMgr != NULL) {
		return m_conMgr->getLocalPlayerID();
	}
	return 49;
}
UnicodeString Network::getPlayerName(Int playerNum)
{
	if (playerNum == m_conMgr->getLocalPlayerID()) {
		if (m_localStatus != NETLOCALSTATUS_INGAME) {
			return UnicodeString::TheEmptyString;
		}
	}
	if (m_conMgr != NULL) {
		return m_conMgr->getPlayerName( playerNum );
	}
	return UnicodeString::TheEmptyString;
}
Int Network::getNumPlayers()
{
	if (m_conMgr != NULL) {
		return m_conMgr->getNumPlayers();
	}
	return -1;
}

Int Network::getSlotAverageFPS(Int slot) {
	if (m_conMgr != NULL) {
		return m_conMgr->getSlotAverageFPS(slot);
	}
	return -1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
void Network::toggleNetworkOn() {
	if (m_networkOn == TRUE) {
		m_networkOn = FALSE;
	} else {
		m_networkOn = TRUE;
	}
}
#endif

void Network::notifyOthersOfCurrentFrame() {
	if (m_conMgr != NULL) {
		m_conMgr->notifyOthersOfCurrentFrame(TheGameLogic->getFrame());
	}
}

void Network::notifyOthersOfNewFrame(UnsignedInt frame) {
	if (m_conMgr != NULL) {
		m_conMgr->notifyOthersOfNewFrame(frame);
	}
}