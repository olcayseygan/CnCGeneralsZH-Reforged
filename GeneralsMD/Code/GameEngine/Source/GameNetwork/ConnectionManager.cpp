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
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"

#include "Compression.h"
#include "strtok_r.h"
#include "Common/AudioEventRTS.h"
#include "Common/CRCDebug.h"
#include "Common/Debug.h"
#include "Common/file.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/LocalFileSystem.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"

#include "GameClient/Diplomacy.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameNetwork/ConnectionManager.h"
#include "GameNetwork/FrameResendPolicy.h"
#include "GameNetwork/StallJudgement.h"
#include "GameNetwork/KeepAliveSchedule.h"
#include "GameNetwork/CushionMetrics.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/NetCommandWrapperList.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/VictoryConditions.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/InGameUI.h"
#include "Platform/StrdupAsWindows.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

/**
 * Le destructor.
 */
ConnectionManager::~ConnectionManager(void)
{
	if (m_localUser != NULL) {
		m_localUser->deleteInstance();
		m_localUser = NULL;
	}

//	m_transport = NULL; // Network will delete transports; we just forget them
	if (m_transport != NULL) {
		delete m_transport;
		m_transport = NULL;
	}

	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for (i = 0; i < MAX_SLOTS; ++i) {
		if (m_frameData[i] != NULL) {
			m_frameData[i]->deleteInstance();
			m_frameData[i] = NULL;
		}
	}

	for (i = 0; i < NUM_CONNECTIONS; ++i) {
		if (m_connections[i] != NULL) {
			m_connections[i]->deleteInstance();
			m_connections[i] = NULL;
		}
	}

	if (TheDisconnectMenu != NULL) {
		// This is done here since TheDisconnectMenu should only be there if we are in a network game.
		delete TheDisconnectMenu;
		TheDisconnectMenu = NULL;
	}

	if (m_disconnectManager != NULL) {
		delete m_disconnectManager;
		m_disconnectManager = NULL;
	}

	if (m_pendingCommands != NULL) {
		m_pendingCommands->deleteInstance();
		m_pendingCommands = NULL;
	}

	if (m_relayedCommands != NULL) {
		m_relayedCommands->deleteInstance();
		m_relayedCommands = NULL;
	}

	if (m_netCommandWrapperList != NULL) {
		m_netCommandWrapperList->deleteInstance();
		m_netCommandWrapperList = NULL;
	}

	s_fileCommandMap.clear();
	s_fileRecipientMaskMap.clear();
	for (i = 0; i < MAX_SLOTS; ++i) {
		s_fileProgressMap[i].clear();
	}
}

/**
 * Le constructor
 */
ConnectionManager::ConnectionManager(void)
{
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		m_frameData[i] = NULL;
		m_lastHeardFrom[i] = 0;
	}
	m_keepAliveRoundStart = 0;
	m_keepAliveNextSlot = 0;
	m_transport = NULL;
	m_disconnectManager = NULL;
	m_pendingCommands = NULL;
	m_relayedCommands = NULL;
	m_localAddr = 0;
	m_localPort = 0;
	m_netCommandWrapperList = NULL;
	m_localUser = NULL;
	m_localUser = newInstance(User);
	for (UnsignedInt slot = 0; slot < MAX_SLOTS; ++slot) {
		m_recoveryFrame[slot] = 0; m_recoveryPending[slot] = FALSE;
	}
	m_routerCommandAuthors = 0;
	m_resendWatchFrame = 0;
	m_resendWatchSince = 0;
	m_resendLastRequest = 0;
}

/**
 * Initialize the connection manager and any subsystems.
 */
void ConnectionManager::init() 
{
//	if (m_transport == NULL) {
//		m_transport = new Transport;
//	}
//	m_transport->reset();

	// i is used after the loop; VC6 for-scope let it escape.
	UnsignedInt i;
	for (i = 0; i < NUM_CONNECTIONS; ++i) {
		m_connections[i] = NULL;
	}

	/* A fresh game means nothing is stuck yet; leaving the old stamps behind would make the
		 first blocking frame of the new match look like it had been waiting since the last one. */
	for (UnsignedInt slot = 0; slot < MAX_SLOTS; ++slot) {
		m_recoveryFrame[slot] = 0; m_recoveryPending[slot] = FALSE;
	}
	m_routerCommandAuthors = 0;
	m_resendWatchFrame = 0;
	m_resendWatchSince = 0;
	m_resendLastRequest = 0;

	if (m_pendingCommands == NULL) {
		m_pendingCommands = newInstance(NetCommandList);
		m_pendingCommands->init();
	}
	m_pendingCommands->reset();

	if (m_relayedCommands == NULL) {
		m_relayedCommands = newInstance(NetCommandList);
		m_relayedCommands->init();
	}
	m_relayedCommands->reset();

	/* A new game starts with nobody having been heard from yet, or the silence carried over from
		 the last one would be counted against them. */
	for (i = 0; i < MAX_SLOTS; ++i) {
		m_lastHeardFrom[i] = 0;
	}
	m_keepAliveRoundStart = 0;
	m_keepAliveNextSlot = 0;

	m_localSlot = -1;
	m_lastFrameTickSent = -1;
#ifdef MEMORYPOOL_DEBUG
	TheMemoryPoolFactory->debugSetInitFillerIndex(m_localSlot);
#endif
	m_packetRouterSlot = 0; /// @todo The LAN/WOL interface should be telling us who the packet router is based on machine specs passed around through game options.
	for (i = 0; i < MAX_SLOTS; ++i) {
		m_packetRouterFallback[i] = -1;
	}

	for (i = 0; i < MAX_SLOTS; ++i) {
		if (m_frameData[i] != NULL) {
			m_frameData[i]->deleteInstance();
			m_frameData[i] = NULL;
		}
	}

//	m_averageFps = 30;			// since 30 fps is the desired rate, we'll start off at that.
//	m_averageLatency = (Real)0.2; // 200ms seems like a good starting point.

	for (i = 0; i < MAX_SLOTS; ++i) {
		m_fpsAverages[i] = -1;
	}
	for (i = 0; i < MAX_SLOTS; ++i) {
		m_latencyAverages[i] = 0.0; // using zero since all floating point standards should be able to specify 0.0 accurately.
	}
	m_smallestPacketArrivalCushion = -1;

	m_frameMetrics.init();

	TheDisconnectMenu = NEW DisconnectMenu;
	TheDisconnectMenu->init();

	m_disconnectManager = NEW DisconnectManager;
	m_disconnectManager->init();

	TheDisconnectMenu->attachDisconnectManager(m_disconnectManager);
	TheDisconnectMenu->hideScreen();

	m_netCommandWrapperList = newInstance(NetCommandWrapperList);
	m_netCommandWrapperList->init();

	s_fileCommandMap.clear();
	s_fileRecipientMaskMap.clear();
	for (i = 0; i < MAX_SLOTS; ++i) {
		s_fileProgressMap[i].clear();
	}
}

/**
 * Reset the connection manager and any subsystems.
 */
void ConnectionManager::reset() 
{
//	if (m_transport == NULL) {
//		m_transport = new Transport;
//	}
//	m_transport->reset();
	if (m_transport != NULL) {
		delete m_transport;
		m_transport = NULL;
	}

	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for (i = 0; i < NUM_CONNECTIONS; ++i) {
		if (m_connections[i] != NULL) {
			m_connections[i]->deleteInstance();
			m_connections[i] = NULL;
		}
	}

	for (i=0; i<MAX_SLOTS; ++i)
	{
		if (m_frameData[i] != NULL) {
			m_frameData[i]->deleteInstance();
			m_frameData[i] = NULL;
		}
	}

	if (m_pendingCommands == NULL) {
		m_pendingCommands = newInstance(NetCommandList);
		m_pendingCommands->init();
	}
	m_pendingCommands->reset();

	if (m_relayedCommands == NULL) {
		m_relayedCommands = newInstance(NetCommandList);
		m_relayedCommands->init();
	}
	m_relayedCommands->reset();

	if (m_netCommandWrapperList == NULL) {
		m_netCommandWrapperList = newInstance(NetCommandWrapperList);
		m_netCommandWrapperList->init();
	}
	m_netCommandWrapperList->reset();

	m_localSlot = -1;
	m_lastFrameTickSent = -1;
#ifdef MEMORYPOOL_DEBUG
	TheMemoryPoolFactory->debugSetInitFillerIndex(m_localSlot);
#endif
	m_routerCommandAuthors = 0;
	m_packetRouterSlot = -1;

	for (i = 0; i < TheGlobalData->m_networkFPSHistoryLength; ++i) {
		m_fpsAverages[i] = -1;
	}
	for (i = 0; i < TheGlobalData->m_networkLatencyHistoryLength; ++i) {
		m_latencyAverages[i] = 0.0;
	}

	for (i = 0; i < MAX_SLOTS; ++i) {
		m_packetRouterFallback[i] = -1;
	}

	m_frameMetrics.reset();
}

UnsignedInt ConnectionManager::getPingFrame()
{
	return (m_disconnectManager)?m_disconnectManager->getPingFrame():0;
}

Int ConnectionManager::getPingsSent()
{
	return (m_disconnectManager)?m_disconnectManager->getPingsSent():0;
}

Int ConnectionManager::getPingsRecieved()
{
	return (m_disconnectManager)?m_disconnectManager->getPingsRecieved():0;
}

Bool ConnectionManager::isPlayerConnected( Int playerID )
{
	// Callers hand this a player id straight off the wire, and m_connections holds MAX_SLOTS of
	// them.  A packet claiming to be from player 200 used to index that far past the end.
	if ( playerID < 0 || playerID >= MAX_SLOTS )
		return FALSE;

	return ( playerID == m_localSlot || (m_connections[playerID] && !m_connections[playerID]->isQuitting()) );
}

void ConnectionManager::attachTransport(Transport *transport) {
	if (m_transport != NULL) {
		delete m_transport;
		m_transport = NULL;
	}
	m_transport = transport;
}

/**
 * zero out the command counts for the given frames.  Presently this is used for
 * the start of a game since there won't be any commands for the first few frames due to runahead.
 */
void ConnectionManager::zeroFrames(UnsignedInt startingFrame, UnsignedInt numFrames) {
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_frameData[i] != NULL) {
//			DEBUG_LOG(("Calling zeroFrames on player %d, starting frame %d, numFrames %d\n", i, startingFrame, numFrames));
			m_frameData[i]->zeroFrames(startingFrame, numFrames);
		}
	}
}

/**
 * Destroy any game messages that are left over due to the run ahead.
 */
void ConnectionManager::destroyGameMessages() {
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		// Need to destroy these game messages because when the game ends, there are
		// still some game messages left over because of the run ahead aspect of
		// network play.
		if (m_frameData[i] != NULL) {
			m_frameData[i]->destroyGameMessages();
		}
	}
}

Bool ConnectionManager::isCommandAuthorAllowed(NetCommandMsg *msg) const
{
    UnsignedByte roles = m_routerCommandAuthors;
    if (m_packetRouterSlot < MAX_SLOTS) roles |= (UnsignedByte)(1u << m_packetRouterSlot);
    return NetworkCommandAuthorAllowed(msg, roles);
}

Bool ConnectionManager::receiveSourceAllowed(NetCommandRef *ref)
{
    NetCommandMsg *msg = ref->getCommand();
    const UnsignedInt author = msg->getPlayerID();
    if (!isCommandAuthorAllowed(msg) || (author != m_localSlot && !m_connections[author])) return FALSE;
    const UnsignedByte sources = ref->getSourceMask();
    if (!sources) return FALSE;
    for (UnsignedInt sender = 0; sender < MAX_SLOTS; ++sender) {
        if (!(sources & (1u << sender))) continue;
        if (!m_connections[sender]) return FALSE;
        if (m_connections[sender]->isQuitting()) {
            const NetCommandType type = msg->getNetCommandType();
            if (sender != author || (type != NETCOMMANDTYPE_ACKSTAGE1 &&
                type != NETCOMMANDTYPE_ACKSTAGE2 && type != NETCOMMANDTYPE_ACKBOTH)) return FALSE;
        }
        // MAX_FRAMES_AHEAD is also the retained ring size. No unsolicited recovery,
        // authority-bearing direct controls or frames outside that request window.
        const Bool recovery = m_recoveryPending[sender] &&
            (msg->getNetCommandType() == NETCOMMANDTYPE_WRAPPER ||
             (msg->getExecutionFrame() >= m_recoveryFrame[sender] &&
              msg->getExecutionFrame()-m_recoveryFrame[sender] < MAX_FRAMES_AHEAD));
        if (!NetworkCommandSourceAllowed(msg, sender, m_packetRouterSlot, recovery)) return FALSE;
    }
    return TRUE;
}

/**
 * ConnectionManager::doRelay()
 * Queries the transport for commands that need to be relayed to another client.
 * Get those commands and relay them to the appropriate Connection(s). We make the
 * assumption that a command will only be relayed once.
 */
void ConnectionManager::doRelay() {
	NetPacket *packet = NULL;

	for (Int i = 0; i < MAX_MESSAGES; ++i) {
		if (m_transport->m_inBuffer[i].length != 0) {
			// This transport buffer has yet to be processed.

			// Resolve the real endpoint before parsing commands or changing any state.
			UnsignedInt senderSlot = MAX_SLOTS;
			for (Int slot = 0; slot < MAX_SLOTS; ++slot) {
				if (m_connections[slot] &&
					m_connections[slot]->getUser()->GetIPAddr() == m_transport->m_inBuffer[i].addr &&
					m_connections[slot]->getUser()->GetPort() == m_transport->m_inBuffer[i].port) {
					senderSlot = slot; break;
				}
			}
			if (senderSlot == MAX_SLOTS) {
				m_transport->m_inBuffer[i].length = 0;
				continue;
			}
			packet = newInstance(NetPacket)(&(m_transport->m_inBuffer[i]));
			NetCommandList *cmdList = packet->getCommandList();
			NetCommandRef *cmd = cmdList->getFirstMessage();

			// Iterate through the commands in this packet and send them to the proper connections.
			while (cmd != NULL) {
				cmd->setSourceMask((UnsignedByte)(1u << senderSlot));
				if (!receiveSourceAllowed(cmd)) { cmd = cmd->getNext(); continue; }
				// A recovered historical command proves the responder is present, not its author.
				m_lastHeardFrom[senderSlot] = Clock_Milliseconds();
				UnsignedInt author = cmd->getCommand()->getPlayerID();
				if (senderSlot == m_packetRouterSlot) m_lastHeardFrom[author] = Clock_Milliseconds();

				if (CommandRequiresAck(cmd->getCommand())) {
					ackCommand(cmd, m_localSlot, senderSlot);
				}
				if (!processNetCommand(cmd)) {
					sendRemoteCommand(cmd);
				}
				cmd = cmd->getNext();
			}

			// Delete this packet since we won't be needing it anymore.
			packet->deleteInstance();
			packet = NULL;

			cmdList->deleteInstance();
			cmdList = NULL;

			// signal that this has been processed.
			m_transport->m_inBuffer[i].length = 0;
		}
	}

	NetCommandList *cmdList = m_netCommandWrapperList->getReadyCommands();
	NetCommandRef *cmd = cmdList->getFirstMessage();
	while (cmd != NULL) {
		// Reassembly must not turn a relayed wrapper into a direct host command.
		if (!receiveSourceAllowed(cmd)) { cmd = cmd->getNext(); continue; }
		if (CommandRequiresAck(cmd->getCommand())) {
			ackCommand(cmd, m_localSlot);
		}
		if (!processNetCommand(cmd)) {
			sendRemoteCommand(cmd);
		}
		cmd = cmd->getNext();
	}

	// Delete this packet since we won't be needing it anymore.
	packet->deleteInstance();
	packet = NULL;

	cmdList->deleteInstance();
	cmdList = NULL;
}

/**
 * This is where the non-synchronized network commands should be processed.
 * Return TRUE if the command should not be relayed. Return FALSE if it should be relayed.
 */
Bool ConnectionManager::processNetCommand(NetCommandRef *ref) {
	NetCommandMsg *msg = ref->getCommand();

	if ((msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE1) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE2) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH)) {

		processAck(msg);
		return FALSE;
	}

	if ((m_connections[msg->getPlayerID()] == NULL) && (msg->getPlayerID() != m_localSlot)) {
		// if this is from a player that is no longer in the game, then ignore them.
		return TRUE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_WRAPPER) {
		processWrapper(ref); // need to send the NetCommandRef since we have to construct the relay for the wrapped command.
		return FALSE;
	}

	if ((msg->getPlayerID() >= 0) && (msg->getPlayerID() < MAX_SLOTS) && (msg->getPlayerID() != m_localSlot)) {
		if (m_connections[msg->getPlayerID()] == NULL) {
			return TRUE;
		}
	}

	// Don't allow an out of date command to be sent through.
	// Its unnecessary traffic and it could cause problems.
	//
	// This was a fix for a command count bug where a command would be
	// executed, then a command for that old frame would be added to the
	// FrameData for that frame + 256, and would screw up the command count.

	if (IsCommandSynchronized(msg->getNetCommandType())) {
		if (ref->getCommand()->getExecutionFrame() < TheGameLogic->getFrame()) {
			return TRUE;
		}
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) {
		processFrameInfo((NetFrameCommandMsg *)msg);
		
		// need to set the relay so we don't send it to ourselves.
		UnsignedByte relay = ref->getRelay();
		relay = relay & (0xff ^ (1 << m_localSlot));
		ref->setRelay(relay);
		return FALSE;
	}
	
	if (msg->getNetCommandType() == NETCOMMANDTYPE_PROGRESS)
	{
		//DEBUG_LOG(("ConnectionManager::processNetCommand - got a progress net command from player %d\n", msg->getPlayerID()));
		processProgress((NetProgressCommandMsg *) msg);

		// need to set the relay so we don't send it to ourselves.
		UnsignedByte relay = ref->getRelay();
		relay = relay & (0xff ^ (1 << m_localSlot));
		ref->setRelay(relay);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_TIMEOUTSTART)
	{
		DEBUG_LOG(("ConnectionManager::processNetCommand - got a TimeOut GameStart net command from player %d\n", msg->getPlayerID()));
		processTimeOutGameStart(msg);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_RUNAHEADMETRICS) {
		processRunAheadMetrics((NetRunAheadMetricsCommandMsg *)msg);

		// the packet router passes them on to everyone else (updateRunAhead)
		UnsignedByte relay = ref->getRelay();
		relay = relay & (0xff ^ (1 << m_localSlot));
		ref->setRelay(relay);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_KEEPALIVE) {
		return TRUE;
	}

	if ((msg->getNetCommandType() > NETCOMMANDTYPE_DISCONNECTSTART) && (msg->getNetCommandType() < NETCOMMANDTYPE_DISCONNECTEND)) {
		m_disconnectManager->processDisconnectCommand(ref, this);
		return TRUE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTCHAT) {
		processDisconnectChat((NetDisconnectChatCommandMsg *)msg);
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_LOADCOMPLETE)
	{
		DEBUG_LOG(("ConnectionManager::processNetCommand - got a Load Complete net command from player %d\n", msg->getPlayerID()));
		processLoadComplete(msg);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_CHAT) {
		processChat((NetChatCommandMsg *)msg);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FILE) {
		processFile((NetFileCommandMsg *)msg);
		return FALSE;
	} 

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FILEANNOUNCE) {
		processFileAnnounce((NetFileAnnounceCommandMsg *)msg);
		return FALSE;
	} 

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FILEPROGRESS) {
		processFileProgress((NetFileProgressCommandMsg *)msg);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FRAMERESENDREQUEST) {
		processFrameResendRequest((NetFrameResendRequestCommandMsg *)msg);
		return TRUE;
	}

	return FALSE;
}

void ConnectionManager::processFrameResendRequest(NetFrameResendRequestCommandMsg *msg) {
	// first make sure this is a valid slot
	Int playerID = msg->getPlayerID();
	if ((playerID < 0) || (playerID >= MAX_SLOTS)) {	
		return;
	}

	// make sure this player is still in our game.
	if ((m_connections[playerID] == NULL) || (m_connections[playerID]->isQuitting() == TRUE)) {
		return;
	}

	sendFrameDataToPlayer(playerID, msg->getFrameToResend());
}

/**
 * We have received a wrapper for a command too big to fit in a packet.
 */
void ConnectionManager::processWrapper(NetCommandRef *ref) 
{
	NetWrapperCommandMsg *wrapperMsg = (NetWrapperCommandMsg *)(ref->getCommand());
	UnsignedShort commandID = wrapperMsg->getWrappedCommandID();
	DEBUG_LOG(("ConnectionManager::processWrapper() - wrapped commandID is %d, commandID is %d\n",
		commandID, wrapperMsg->getID()));
	Int origProgress = 0;
	FileCommandMap::iterator fcIt = s_fileCommandMap.find(commandID);
	if (fcIt != s_fileCommandMap.end())
	{
		origProgress = s_fileProgressMap[m_localSlot][commandID];
	}
	DEBUG_LOG(("ConnectionManager::processWrapper() - origProgress[%d] == %d for command %d\n",
		m_localSlot, origProgress, commandID));

	m_netCommandWrapperList->processWrapper(ref);

	if (fcIt != s_fileCommandMap.end())
	{
		Int newProgress = m_netCommandWrapperList->getPercentComplete(commandID);
		DEBUG_LOG(("ConnectionManager::processWrapper() - newProgress[%d] == %d for command %d\n",
			m_localSlot, newProgress, commandID));
		if (newProgress > origProgress && newProgress < 100)
		{
			DEBUG_LOG(("ConnectionManager::processWrapper() - sending a NetFileProgressCommandMsg\n"));
			s_fileProgressMap[m_localSlot][commandID] = newProgress;

			Int progressMask = 0xff ^ (1 << m_localSlot);
			NetFileProgressCommandMsg *msg = newInstance(NetFileProgressCommandMsg);
			msg->setPlayerID(m_localSlot);
			msg->setID(0);
			if (DoesCommandRequireACommandID(msg->getNetCommandType()))
			{
				msg->setID(GenerateNextCommandID());
			}
			msg->setFileID(commandID);
			msg->setProgress(newProgress);
			sendLocalCommand(msg, progressMask);
			processFileProgress(msg);
			msg->detach();
		}
	}
}

/**
 * A client has sent us their run ahead metrics, lets store them away for future calculations.
 */
void ConnectionManager::processRunAheadMetrics(NetRunAheadMetricsCommandMsg *msg) 
{
	UnsignedInt player = msg->getPlayerID();
	if ((player >= 0) && (player < MAX_SLOTS) && (isPlayerConnected(player))) {
		m_latencyAverages[player] = msg->getAverageLatency();
		m_fpsAverages[player] = msg->getAverageFps();
		//DEBUG_LOG(("ConnectionManager::processRunAheadMetrics - player %d, fps = %d, latency = %f\n", player, msg->getAverageFps(), msg->getAverageLatency()));
		if (m_fpsAverages[player] > ConvertRetailFrames(100)) {
			// limit the reported frame rate average to 100 retail frames.  This is done because if a
			// user alt-tab's out of the game their frame rate climbs to in the neighborhood of
			// 300, that was deemed "ugly" by the powers that be.
			m_fpsAverages[player] = ConvertRetailFrames(100);
		}
	}
}

void ConnectionManager::processDisconnectChat(NetDisconnectChatCommandMsg *msg) 
{
	UnicodeString unitext;
	UnicodeString name;
	UnsignedByte playerID = msg->getPlayerID();
	if (playerID == m_localSlot) {
		name = m_localUser->GetName();
	} else if (isPlayerConnected(playerID)) {
		name = m_connections[playerID]->getUser()->GetName();
	}
	unitext.format(u"[%ls] %ls", name.str(), msg->getText().str());
//	DEBUG_LOG(("ConnectionManager::processDisconnectChat - got message from player %d, message is %ls\n", playerID, unitext.str()));
	TheDisconnectMenu->showChat(unitext); // <-- need to implement this
}

void ConnectionManager::processChat(NetChatCommandMsg *msg) 
{
	UnicodeString unitext;
	UnicodeString name;
	UnsignedByte playerID = msg->getPlayerID();
	// From the wire, and used below to index m_connections and to ask TheGameInfo for a slot.
	if (playerID >= MAX_SLOTS) {
		return;
	}
	//DEBUG_LOG(("processChat(): playerID = %d\n", playerID));
	if (playerID == m_localSlot) {
		name = m_localUser->GetName();
		//DEBUG_LOG(("connection is NULL, using %ls\n", name.str()));
	} else if (((m_connections[playerID] != NULL) && (m_connections[playerID]->isQuitting() == FALSE))) {
		name = m_connections[playerID]->getUser()->GetName();
		//DEBUG_LOG(("connection is non-NULL, using %ls\n", name.str()));
	}
	unitext.format(u"[%ls] %ls", name.str(), msg->getText().str());
//	DEBUG_LOG(("ConnectionManager::processChat - got message from player %d (mask %8.8X), message is %ls\n", playerID, msg->getPlayerMask(), unitext.str()));
	
	AsciiString playerName;
	playerName.format("player%d", msg->getPlayerID());
	Player *player = ThePlayerList->findPlayerWithNameKey( TheNameKeyGenerator->nameToKey( playerName ) );
	if (!player)
	{
		TheInGameUI->message(UnicodeString(u"%ls"), unitext.str());
		return;
	}
	
	Bool fromObserver = !player->isPlayerActive();
	Bool amIObserver = !ThePlayerList->getLocalPlayer()->isPlayerActive();
	Bool canSeeChat = amIObserver || !fromObserver && !TheGameInfo->getConstSlot(playerID)->isMuted();
	
	if ( ((1<<m_localSlot) & msg->getPlayerMask() ) && canSeeChat  )
	{
		TheInGameUI->chatMessage(player, msg->getText());

		// feedback for received chat messages in-game
		AudioEventRTS audioEvent("GUICommunicatorIncoming");
		TheAudio->addAudioEvent(&audioEvent);
	}
}

void ConnectionManager::processFile(NetFileCommandMsg *msg) 
{
#ifdef _INTERNAL
	UnicodeString log;
	log.format(u"Saw file transfer: '%hs' of %d bytes from %d", msg->getPortableFilename().str(), msg->getFileLength(), msg->getPlayerID());
	DEBUG_LOG(("%s\n", WideCharAsUtf8( log.str() ).str()));
#endif

	AsciiString realFileName = msg->getRealFilename();

	// Only the host sends map files, and only after announcing each one to the players who need it
	// (FileTransfer.cpp).  A file nobody announced, under another name, or for somebody else, is a
	// peer writing to our disk on its own say-so.
	FileCommandMap::iterator announced = s_fileCommandMap.find(msg->getID());
	FileMaskMap::iterator recipients = s_fileRecipientMaskMap.find(msg->getID());
	if (msg->getPlayerID() != 0 || announced == s_fileCommandMap.end() || announced->second != realFileName ||
			recipients == s_fileRecipientMaskMap.end() || (recipients->second & (1 << m_localSlot)) == 0)
	{
		DEBUG_LOG(("ConnectionManager::processFile - '%s' from player %d was never announced to us, dropped\n",
			realFileName.str(), msg->getPlayerID()));
		return;
	}

	if (!IsSafeTransferPath(msg->getPortableFilename()) || !IsSafeTransferPath(realFileName))
	{
		DEBUG_LOG(("ConnectionManager::processFile - refused '%s'\n", realFileName.str()));
		return;
	}

	if (TheFileSystem->doesFileExist(realFileName.str()))
	{
		DEBUG_LOG(("File exists already!\n"));
		//return;
	}

	UnsignedByte *buf = msg->getFileData();
	Int len = msg->getFileLength();

	// uncompress Targas
#ifdef COMPRESS_TARGAS
	Bool deleteBuf = FALSE;
	if (msg->getFilename().endsWith(".tga") && CompressionManager::isDataCompressed(buf, len))
	{
		Int uncompLen = CompressionManager::getUncompressedSize(buf, len);
		UnsignedByte *uncompBuffer = NEW UnsignedByte[uncompLen];
		Int actualLen = CompressionManager::decompressData(buf, len, uncompBuffer, uncompLen);
		if (actualLen == uncompLen)
		{
			DEBUG_LOG(("Uncompressed Targa after map transfer\n"));
			deleteBuf = TRUE;
			buf = uncompBuffer;
			len = uncompLen;
		}
		else
		{
			DEBUG_LOG(("Failed to uncompress Targa after map transfer\n"));
			delete[] uncompBuffer; // failed to decompress, so just use the source
		}
	}
#endif // COMPRESS_TARGAS

	// the bytes are still in memory here, so check them before any of them reach the disk
	if (!IsValidTransferFileContent(realFileName, buf, (UnsignedInt)len))
	{
		DEBUG_LOG(("ConnectionManager::processFile - '%s' is not what it says it is, dropped\n",
			realFileName.str()));
#ifdef COMPRESS_TARGAS
		if (deleteBuf)
		{
			delete[] buf;
			buf = NULL;
		}
#endif // COMPRESS_TARGAS
		return;
	}

	File *fp = TheFileSystem->openFile(realFileName.str(), File::CREATE | File::BINARY | File::WRITE);
	if (fp)
	{
		fp->write(buf, len);
		fp->close();
		fp = NULL;
		DEBUG_LOG(("Wrote %d bytes to file %s!\n",len,realFileName.str()));

	}
	else
	{
		DEBUG_LOG(("Cannot open file!\n"));
	}

	DEBUG_LOG(("ConnectionManager::processFile() - sending a NetFileProgressCommandMsg\n"));
	
	Int commandID = msg->getID();
	Int newProgress = 100;

	s_fileProgressMap[m_localSlot][commandID] = newProgress;

	Int progressMask = 0xff ^ (1 << m_localSlot);
	NetFileProgressCommandMsg *progressMsg = newInstance(NetFileProgressCommandMsg);
	progressMsg->setPlayerID(m_localSlot);
	progressMsg->setID(0);
	if (DoesCommandRequireACommandID(progressMsg->getNetCommandType()))
	{
		progressMsg->setID(GenerateNextCommandID());
	}
	progressMsg->setFileID(commandID);
	progressMsg->setProgress(newProgress);
	sendLocalCommand(progressMsg, progressMask);
	processFileProgress(progressMsg);
	progressMsg->detach();

#ifdef COMPRESS_TARGAS
	if (deleteBuf)
	{
		delete[] buf;
		buf = NULL;
	}
#endif // COMPRESS_TARGAS
}

void ConnectionManager::processFileAnnounce(NetFileAnnounceCommandMsg *msg) 
{
	DEBUG_LOG(("ConnectionManager::processFileAnnounce() - expecting '%s' (%s) in command %d\n", msg->getPortableFilename().str(), msg->getRealFilename().str(), msg->getFileID()));
	// the host (slot 0) is the one who transfers maps; an announce from anyone else would let
	// processFile accept a file the host never sent
	if (msg->getPlayerID() != 0) {
		DEBUG_LOG(("ConnectionManager::processFileAnnounce() - player %d is not the host, ignored\n", msg->getPlayerID()));
		return;
	}
	s_fileCommandMap[msg->getFileID()] = msg->getRealFilename();
	s_fileRecipientMaskMap[msg->getFileID()] = msg->getPlayerMask();
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if ( (1<<i) & msg->getPlayerMask() )
		{
			s_fileProgressMap[i][msg->getFileID()] = 0;
		}
		else
		{
			s_fileProgressMap[i][msg->getFileID()] = 100; // they don't need to get it, so they're already done.
		}
	}
}

void ConnectionManager::processFileProgress(NetFileProgressCommandMsg *msg) 
{
	DEBUG_LOG(("ConnectionManager::processFileProgress() - command %d is at %d%%\n",
		msg->getFileID(), msg->getProgress()));

	// s_fileProgressMap is an array of MAX_SLOTS maps and this index arrives in the packet.
	// (The file id inside it is a map key, so that one cannot run off anything.)
	const UnsignedInt playerID = msg->getPlayerID();
	if (playerID >= MAX_SLOTS) {
		return;
	}

	Int oldProgress = s_fileProgressMap[playerID][msg->getFileID()];

	s_fileProgressMap[playerID][msg->getFileID()] = max(oldProgress, msg->getProgress());
}

void ConnectionManager::processProgress( NetProgressCommandMsg *msg )
{
	TheGameLogic->processProgress(msg->getPlayerID(), msg->getPercentage());
}

void ConnectionManager::processLoadComplete( NetCommandMsg *msg )
{
	TheGameLogic->processProgressComplete(msg->getPlayerID());
}

void ConnectionManager::processTimeOutGameStart( NetCommandMsg *msg )
{
	TheGameLogic->timeOutGameStart();
}

/**
 * Another client has sent us the command count for a new frame.
 */
void ConnectionManager::processFrameInfo(NetFrameCommandMsg *msg) {
	//stupid frame info, why don't you process yourself?

	UnsignedInt playerID = msg->getPlayerID();

	if ((playerID >= 0) && (playerID < MAX_SLOTS)) {
		if (m_frameData[playerID] != NULL) {
//			DEBUG_LOG(("ConnectionManager::processFrameInfo - player %d, frame %d, command count %d, received on frame %d\n", playerID, msg->getExecutionFrame(), msg->getCommandCount(), TheGameLogic->getFrame()));
			m_frameData[playerID]->setFrameCommandCount(msg->getExecutionFrame(), msg->getCommandCount(), msg->getID());
		}
	}
}

/**
 * We just got a stage 1 ack from someone.  So we should remove it from the connection that sent it so
 * it doesn't keep resending it.
 */
void ConnectionManager::processAckStage1(NetCommandMsg *msg) {
#if defined(_DEBUG) || defined(_INTERNAL)
	Bool doDebug = (msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) ? TRUE : FALSE;
#endif

	UnsignedByte playerID = msg->getPlayerID();
	NetCommandRef *ref = NULL;

#if defined(_DEBUG) || defined(_INTERNAL)
	if (doDebug == TRUE) {
		DEBUG_LOG(("ConnectionManager::processAck - processing ack for command %d from player %d\n", ((NetAckStage1CommandMsg *)msg)->getCommandID(), playerID));
	}
#endif
	
	if ((playerID >= 0) && (playerID < NUM_CONNECTIONS)) {
		if (m_connections[playerID] != NULL) {
			ref = m_connections[playerID]->processAck(msg);
		}
	} else {
		DEBUG_ASSERTCRASH((playerID >= 0) && (playerID < NUM_CONNECTIONS), ("ConnectionManager::processAck - %d is an invalid player number"));
	}

	if (ref != NULL) {
		/* The run-ahead's latency is the round trip of our own frame info through the packet router.
			 An ack from another player, or for another slot's frame info we passed on, is for a frame
			 resend answer (see ackCommand), whose frame went out long before the stopwatch this would
			 read, so it is no sample.  A non-router's queue to the router only ever held its own. */
		if ((ref->getCommand()->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) &&
				((m_localSlot == m_packetRouterSlot) ||
				 ((playerID == m_packetRouterSlot) && (ref->getCommand()->getPlayerID() == m_localSlot)))) {
			m_frameMetrics.processLatencyResponse(((NetFrameCommandMsg *)(ref->getCommand()))->getExecutionFrame());
		}

		ref->deleteInstance();
		ref = NULL;
	}
}

/**
 * We just got a stage 2 ack from someone.  So remove it from the pending commands list so it doesn't
 * get sent in the case of a new packet router.
 */
void ConnectionManager::processAckStage2(NetCommandMsg *msg) {
	UnsignedShort commandID = 0;
	UnsignedByte playerID = 0;
	if (msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE2) {
		commandID = ((NetAckStage2CommandMsg *)msg)->getCommandID();
		playerID = ((NetAckStage2CommandMsg *)msg)->getOriginalPlayerID();
	} else if (msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH) {
		commandID = ((NetAckBothCommandMsg *)msg)->getCommandID();
		playerID = ((NetAckBothCommandMsg *)msg)->getOriginalPlayerID();
	} else {
		return;
	}

	NetCommandRef *ref = m_pendingCommands->findMessage(commandID, playerID);
	if (ref != NULL) {
		//DEBUG_LOG(("ConnectionManager::processAckStage2 - removing command %d from the pending commands list.\n", commandID));
		DEBUG_ASSERTCRASH((m_localSlot == playerID), ("Found a command in the pending commands list that wasn't originated by the local player"));
		m_pendingCommands->removeMessage(ref);
		ref->deleteInstance();
		ref = NULL;
	} else {
		//DEBUG_LOG(("ConnectionManager::processAckStage2 - Couldn't find command %d from player %d in the pending commands list.\n", commandID, playerID));
	}

	ref = m_relayedCommands->findMessage(commandID, playerID);
	if (ref != NULL) {
		//DEBUG_LOG(("ConnectionManager::processAckStage2 - found command ID %d from player %d in the relayed commands list.\n", commandID, playerID));
		UnsignedByte relay = ref->getRelay();
		//DEBUG_LOG(("ConnectionManager::processAckStage2 - relay was %d and is now ", relay));
		relay = relay & ~(1 << msg->getPlayerID());
		//DEBUG_LOG(("%d\n", relay));
		if (relay == 0) {
			//DEBUG_LOG(("ConnectionManager::processAckStage2 - relay is 0, removing command from the relayed commands list.\n"));
			m_relayedCommands->removeMessage(ref);
			NetAckStage2CommandMsg *ackmsg = newInstance(NetAckStage2CommandMsg)(ref->getCommand());
			sendLocalCommand(ackmsg, 1 << ackmsg->getOriginalPlayerID());
			ref->deleteInstance();
			ref = NULL;

			ackmsg->detach();
			ackmsg = NULL;
		} else {
			ref->setRelay(relay);
		}
	}
}

/**
 * We just got a "both" ack from someone.  So process it as both a stage 1 and stage 2 ack.
 */
void ConnectionManager::processAck(NetCommandMsg *msg) {
	if ((msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE1) || (msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH)) {
		processAckStage1(msg);
	}
	if ((msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE2) || (msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH)) {
		processAckStage2(msg);
	}
}

/**
 * A player has just left our game. Delete their connection and frame data manager.
 * return codes are:
 * PLAYERLEAVECODE_UNKNOWN - player didn't have a valid slot number.
 * PLAYERLEAVECODE_CLIENT - someone in the game that wasn't us or the packet router.
 * PLAYERLEAVECODE_LOCAL - We are leaving the game, we could also be the packet router.
 * PLAYERLEAVECODE_PACKETROUTER - The packet router left the game.
 *
 * If we are leaving and are also the packet router, it will return the PLAYERLEAVECODE_LOCAL return code.
 */
PlayerLeaveCode ConnectionManager::processPlayerLeave(NetPlayerLeaveCommandMsg *msg) {
	if (!isCommandAuthorAllowed(msg)) return PLAYERLEAVECODE_UNKNOWN;
	UnsignedByte playerID = msg->getLeavingPlayerID();
	if ((playerID != m_localSlot) && (m_connections[playerID] != NULL)) {
		DEBUG_LOG(("ConnectionManager::processPlayerLeave() - setQuitting() on player %d on frame %d\n", playerID, TheGameLogic->getFrame()));
		m_connections[playerID]->setQuitting();
	}
	DEBUG_ASSERTCRASH(m_frameData[playerID]->getIsQuitting() == FALSE, ("Player %d is already quitting", playerID));
	if ((playerID != m_localSlot) && (m_frameData[playerID] != NULL) && (m_frameData[playerID]->getIsQuitting() == FALSE)) {
		DEBUG_LOG(("ConnectionManager::processPlayerLeave - setQuitFrame on player %d for frame %d\n", playerID, TheGameLogic->getFrame()+1));
		m_frameData[playerID]->setQuitFrame(TheGameLogic->getFrame() + FRAMES_TO_KEEP + 1);
	}

	if (playerID == m_localSlot)
	{
		// we're leaving, so mark our connections and frame datas to go away.
		for (Int i=0; i<MAX_SLOTS; ++i)
		{
			if (m_connections[i])
			{
				m_connections[i]->clearCommandsExceptFrom(m_localSlot);
				m_connections[i]->setQuitting();
			}
		}
	}

	PlayerLeaveCode code = disconnectPlayer(playerID);
	DEBUG_LOG(("ConnectionManager::processPlayerLeave() - just disconnected player %d with ret code %d\n", playerID, code));
	if (code == PLAYERLEAVECODE_PACKETROUTER)
		resendPendingCommands();

	PopulateInGameDiplomacyPopup();
	return code;
}

UnsignedInt ConnectionManager::getPacketRouterFallbackSlot(Int packetRouterNumber) {
	if ((packetRouterNumber >= 0) && (packetRouterNumber < MAX_SLOTS)) {
		return m_packetRouterFallback[packetRouterNumber];
	}
	return MAX_SLOTS;
}

UnsignedInt ConnectionManager::getPacketRouterSlot() {
	return m_packetRouterSlot;
}

Bool ConnectionManager::areAllQueuesEmpty(void) {
	Bool retval = TRUE;
	for (Int i = 0; (i < MAX_SLOTS) && retval; ++i) {
		if (m_connections[i] != NULL) {
			if (m_connections[i]->isQueueEmpty() == FALSE) {
				//DEBUG_LOG(("ConnectionManager::areAllQueuesEmpty() - m_connections[%d] is not empty\n", i));
				//m_connections[i]->debugPrintCommands();
				retval = FALSE;
			}
		}
	}

	return retval;
}

Bool ConnectionManager::canILeave() {
	return areAllQueuesEmpty();
}

/**
 * The local player is leaving. Tell the local player as well as the other players
 * to remove this player at the specified frame.
 */
void ConnectionManager::handleLocalPlayerLeaving(UnsignedInt frame) {
	NetPlayerLeaveCommandMsg *msg = newInstance(NetPlayerLeaveCommandMsg);

	msg->setLeavingPlayerID(m_localSlot);
	msg->setExecutionFrame(frame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setPlayerID(m_localSlot);

	DEBUG_LOG(("ConnectionManager::handleLocalPlayerLeaving - Local player leaving on frame %d\n", frame));
	DEBUG_ASSERTCRASH(m_packetRouterSlot >= 0, ("ConnectionManager::handleLocalPlayerLeaving, packet router is %d, illegal value.", m_packetRouterSlot));

	sendLocalCommand(msg);

	msg->detach();
}

/**
 * We just got a message that needs to be ack'd, so ack it!
 */
void ConnectionManager::ackCommand(NetCommandRef *ref, UnsignedInt localSlot, UnsignedInt senderSlot) {
	NetCommandMsg *msg = ref->getCommand();
	NetCommandMsg *ackmsg;
	UnsignedShort commandID;
	UnsignedByte originalPlayerID;
	UnsignedByte sendRelay = 0;

	/* A copy that a player other than the packet router sent us straight, which is how a frame
		 resend request is answered: the asked player's own commands and the frames it holds for every
		 other slot.  That copy sits in the sender's queue to us.  EA acked it to the packet router,
		 which only clears the router's own queue, or, on the router, to the command's author, so in a
		 room of three or more the sender never heard back and resent the copy every retry timeout for
		 the rest of the match.  It goes back to the sender now, as a stage 1 ack only: we have it, but
		 only the router can say everyone has.  Two players never take this branch - one of the two is
		 the router, and the router's own traffic and its author's sends route as before. */
	const Bool directReply = (CommandRequiresDirectSend(msg) == FALSE) && (senderSlot < MAX_SLOTS) &&
		(senderSlot != m_packetRouterSlot) && (m_connections[senderSlot] != NULL) &&
		!((m_localSlot == m_packetRouterSlot) && (senderSlot == msg->getPlayerID()));

	// Make send relay a bitmask for the connections that the relay will actually be sent to. This is
	// necessary to determine whether or not we have to wait to send a stage 2 ack.
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (((m_connections[i] != NULL) && (m_connections[i]->isQuitting() == FALSE))) {
			sendRelay = sendRelay | (1 << i);
		}
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	Bool doDebug = (msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) ? TRUE : FALSE;
#endif

	sendRelay = sendRelay & ref->getRelay();
	if ((sendRelay == 0) && (directReply == FALSE)) {
		NetAckBothCommandMsg *bothmsg = newInstance(NetAckBothCommandMsg)(ref->getCommand());
		ackmsg = bothmsg;
		commandID = bothmsg->getCommandID();
		originalPlayerID = bothmsg->getOriginalPlayerID();
#if defined(_DEBUG) || defined(_INTERNAL)
		if (doDebug) {
			DEBUG_LOG(("ConnectionManager::ackCommand - doing ack both for command %d from player %d\n", bothmsg->getCommandID(), bothmsg->getOriginalPlayerID()));
		}
#endif
	} else {
		NetAckStage1CommandMsg *stage1msg = newInstance(NetAckStage1CommandMsg)(ref->getCommand());
		ackmsg = stage1msg;
		commandID = stage1msg->getCommandID();
		originalPlayerID = stage1msg->getOriginalPlayerID();
#if defined(_DEBUG) || defined(_INTERNAL)
		if (doDebug) {
			DEBUG_LOG(("ConnectionManager::ackCommand - doing ack stage 1 for command %d from player %d\n", stage1msg->getCommandID(), stage1msg->getOriginalPlayerID()));
		}
#endif
	}

	ackmsg->setPlayerID(localSlot); // Tell the player who this ack is coming from.

	if (CommandRequiresDirectSend(msg) && CommandRequiresAck(msg)) {
		// Send this ack directly back to the sending player, don't go through the packet router.
		if ((msg->getPlayerID() >= 0) && (msg->getPlayerID() < MAX_SLOTS)) {
			if (m_connections[msg->getPlayerID()] != NULL) {
				m_connections[msg->getPlayerID()]->sendNetCommandMsg(ackmsg, 1 << msg->getPlayerID());
			}
		}
	} else if (directReply) {
		m_connections[senderSlot]->sendNetCommandMsg(ackmsg, 1 << senderSlot);
	} else {
		// The local connection may be the packet router, in that case, the connection would be NULL.  So do something about it!
		if ((m_packetRouterSlot >= 0) && (m_packetRouterSlot < MAX_SLOTS)) {
			if (m_connections[m_packetRouterSlot] != NULL) {
//				DEBUG_LOG(("ConnectionManager::ackCommand - acking command %d from player %d to packet router.\n", commandID, m_packetRouterSlot));
				m_connections[m_packetRouterSlot]->sendNetCommandMsg(ackmsg, 1 << m_packetRouterSlot);
			} else if (m_localSlot == m_packetRouterSlot) {
				// we are the packet router, send the ack to the player that sent the command.
				if ((msg->getPlayerID() >= 0) && (msg->getPlayerID() < MAX_SLOTS)) {
					if (m_connections[msg->getPlayerID()] != NULL) {
//						DEBUG_LOG(("ConnectionManager::ackCommand - acking command %d from player %d directly to player.\n", commandID, msg->getPlayerID()));
						m_connections[msg->getPlayerID()]->sendNetCommandMsg(ackmsg, 1 << msg->getPlayerID());
					} else {
	//					DEBUG_ASSERTCRASH(m_connections[msg->getPlayerID()] != NULL, ("Connection to player is NULL"));
					}
				} else {
					DEBUG_ASSERTCRASH((msg->getPlayerID() >= 0) && (msg->getPlayerID() < MAX_SLOTS), ("Command sent by an invalid player ID."));
				}
			} else {
				DEBUG_ASSERTCRASH(m_connections[m_packetRouterSlot] != NULL, ("Connection to packet router is NULL"));
			}
		} else {
			DEBUG_ASSERTCRASH((m_packetRouterSlot >= 0) && (m_packetRouterSlot < MAX_SLOTS), ("I don't know who the packet router is."));
		}
	}

	ackmsg->detach();
}

/**
 * This is where we relay a command from one client to others (including ourselves).
 * This should only be done by the current packet router.
 */
void ConnectionManager::sendRemoteCommand(NetCommandRef *msg) {
	UnsignedByte actualRelay = 0;
	if (msg->getCommand() == NULL) {
		return;
	}

	/* The per-command and per-frame lines of the network code are DEBUG_LOG_DEV: in a Release log
		 they were nearly all of it, 60 MB in a 100 s three-player match, and most of the 0.5-19 GB logs
		 players sent.  The Debug build still writes them. */
	DEBUG_LOG_DEV(("ConnectionManager::sendRemoteCommand - sending net command %d of type %s from player %d, relay is 0x%x\n",
		msg->getCommand()->getID(), GetAsciiNetCommandType(msg->getCommand()->getNetCommandType()).str(), msg->getCommand()->getPlayerID(), msg->getRelay()));

	UnsignedByte relay = msg->getRelay();
	// The sender id is in the packet and m_frameData holds MAX_SLOTS entries.
	const UnsignedInt senderID = msg->getCommand()->getPlayerID();
	FrameDataManager *senderFrameData = (senderID < MAX_SLOTS) ? m_frameData[senderID] : NULL;
	if ((relay & (1 << m_localSlot)) && (senderFrameData != NULL)) {
		if (IsCommandSynchronized(msg->getCommand()->getNetCommandType())) {
			DEBUG_LOG_DEV(("ConnectionManager::sendRemoteCommand - adding net command of type %s to player %d for frame %d\n", GetAsciiNetCommandType(msg->getCommand()->getNetCommandType()).str(), msg->getCommand()->getPlayerID(), msg->getCommand()->getExecutionFrame()));
			senderFrameData->addNetCommandMsg(msg->getCommand());
		}
	}

	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if ((relay & (1 << i)) && ((m_connections[i] != NULL) && (m_connections[i]->isQuitting() == FALSE))) {
			DEBUG_LOG_DEV(("ConnectionManager::sendRemoteCommand - relaying command %d to player %d\n", msg->getCommand()->getID(), i));
			m_connections[i]->sendNetCommandMsg(msg->getCommand(), 1 << i);
			actualRelay = actualRelay | (1 << i);
		}
	}

	if ((actualRelay != 0) && (CommandRequiresAck(msg->getCommand()) == TRUE)) {
		NetCommandRef *ref = m_relayedCommands->addMessage(msg->getCommand());
		if (ref != NULL) {
			ref->setRelay(actualRelay);
			//DEBUG_LOG(("ConnectionManager::sendRemoteCommand - command %d added to relayed commands with relay %d\n", msg->getCommand()->getID(), ref->getRelay()));
		}
	}

	// Do some metrics to find the minimum packet arrival cushion.
	if (IsCommandSynchronized(msg->getCommand()->getNetCommandType())) {
//		DEBUG_LOG(("ConnectionManager::sendRemoteCommand - about to call allCommandsReady\n"));
		if (allCommandsReady(msg->getCommand()->getExecutionFrame(), TRUE)) {
			Int cushion = frameCushion(msg->getCommand()->getExecutionFrame(), TheGameLogic->getFrame());
			if (((UnsignedInt)cushion < m_smallestPacketArrivalCushion) || (m_smallestPacketArrivalCushion == -1)) {
				m_smallestPacketArrivalCushion = cushion;
			}
			m_frameMetrics.addCushion(cushion);
//			DEBUG_LOG(("Adding %d to cushion for frame %d\n", cushion, msg->getCommand()->getExecutionFrame()));
		}
	}
}

/**
 * ConnectionManager::update
 * Update the connections. Tell them to do the receive and send.  Also relay
 * commands to their final destinations as necessary.
 */
void ConnectionManager::update(Bool isInGame) {
//
// 1. do this
// 2. do that
// 3. do something else
// 4. blow something up
// 5. bust some cap
//

	if ((m_localAddr == 0) || (m_localPort == 0)) {
		// we don't have a local address or port yet, this is bad.
		DEBUG_ASSERTCRASH((m_localAddr != 0) && (m_localPort != 0), ("ConnectionManager doesn't have a local address."));
		return;
	}

	m_transport->doRecv();

	if (isInGame) {
		m_disconnectManager->update(this);
	}

	// take the packets from the transport, break them up into commands, and give them to the appropriate connections.
	doRelay();

	// send any necessary keep-alive packets.
	doKeepAlive();

	for (Int i = 0; i < NUM_CONNECTIONS; ++i) {
		if (m_connections[i] != NULL) {
			/*
			if (m_connections[i]->isQueueEmpty() == FALSE) {
//				DEBUG_LOG(("ConnectionManager::update - calling doSend on connection %d\n", i));
			}
			*/

			m_connections[i]->doSend();

			if (m_connections[i]->isQuitting() && m_connections[i]->isQueueEmpty())
			{
				DEBUG_LOG(("ConnectionManager::update - deleting connection for slot %d\n", i));
				m_connections[i]->deleteInstance();
				m_connections[i] = NULL;
			}
		}

		if ((m_frameData[i] != NULL) && (m_frameData[i]->getIsQuitting() == TRUE)) {
			if (m_frameData[i]->getQuitFrame() == TheGameLogic->getFrame()) {
				DEBUG_LOG(("ConnectionManager::update - deleting frame data for slot %d on quitting frame %d\n", i, m_frameData[i]->getQuitFrame()));
				m_frameData[i]->deleteInstance();
				m_frameData[i] = NULL;
			}
		}
	}

	m_transport->doSend();
}

void ConnectionManager::updateRunAhead(Int oldRunAhead, Int frameRate, Bool didSelfSlug, Int nextExecutionFrame) {
	static time_t lasttimesent = 0;
	time_t curTime = Clock_Milliseconds();

	if ((lasttimesent == 0) || ((curTime - lasttimesent) > TheGlobalData->m_networkRunAheadMetricsTime)) {
		m_latencyAverages[m_localSlot] = m_frameMetrics.getAverageLatency();

		// FrameMetrics measures the logic frame rate again (it briefly measured the display rate,
		// see FrameMetrics::doPerFrameMetrics), so EA's original rule applies once more: a
		// self-slug is a *voluntary* slowdown to let the room catch up, not this machine running
		// out of capacity.  Reporting the slugged rate as our capability would feed the slowdown
		// back into minFps and shrink the run-ahead that the slug was buying margin for.  EA left
		// this half commented out for the other seats, so only the packet router's own slug was
		// forgiven and the room was commanded down to a frame or two under 30 on every lossy link.
		if (didSelfSlug) {
			m_fpsAverages[m_localSlot] = frameRate;
		} else {
			m_fpsAverages[m_localSlot] = m_frameMetrics.getAverageFPS();
		}

		/* EA sent these to the packet router alone, so only the router knew who was slow.  Everyone
			 gets them now, the router's own included, and the Tab screen shows each player's ping and
			 frame rate on every machine.  The router relays them like a chat line (processNetCommand). */
		NetRunAheadMetricsCommandMsg *metrics = newInstance(NetRunAheadMetricsCommandMsg);
		metrics->setPlayerID(m_localSlot);
		metrics->setID(GenerateNextCommandID());
		metrics->setAverageLatency(m_latencyAverages[m_localSlot]);
		metrics->setAverageFps(m_fpsAverages[m_localSlot]);
		sendLocalCommand(metrics, 0xff ^ (1 << m_localSlot));
		metrics->detach();

		if (m_localSlot == m_packetRouterSlot) {
			// We are the packet router, time to compute a new run ahead for this game.
			Int minFps;
			Int minFpsPlayer;
			getMinimumFps(minFps, minFpsPlayer);
			DEBUG_LOG(("ConnectionManager::updateRunAhead - max latency = %f, min fps = %d, min fps player = %d old FPS = %d\n", getMaximumLatency(), minFps, minFpsPlayer, frameRate));
			/* EA kept the current rate whenever the reported minimum was within 10 % below it, to stop
				 the measurement's own rounding from ratcheting the room down a frame at a time.  The
				 probe below does that job (a reported 29 is probed straight back to 30) and the two
				 together oscillate: the band swallows a genuinely slow player for as long as the probe
				 keeps the rate within 10 % of them, so a room held at 12 by one machine walked
				 12-13-14-15-12 for ever.  One mechanism, not two. */
			// the ceiling is the match's speed, which turbo mode doubles on every machine alike
			minFps = settleRoomFrameRate(minFps, getMatchFps());
			DEBUG_LOG(("ConnectionManager::updateRunAhead - minFps after adjustment is %d\n", minFps));

			/* The rate everybody is actually told to run at.  Deliberately a step above the slowest
				 reported rate: a machine pinned at the room's rate reports exactly that rate back, so
				 without the step the reported minimum can never rise and one player's hitch pins the
				 room for the rest of the match - see CushionMetrics.h.  The run-ahead below is sized
				 on the settled rate rather than this one: it is the room's honest measured speed, and
				 sizing a safety window on the rate we are hoping for is how the window comes up short. */
			Int commandedFps = probeRoomFrameRate(minFps, getMatchFps());
			// the rate crosses the wire as an UnsignedByte, and -fps is in logic frames now (twice retail)
			if (commandedFps > UCHAR_MAX)
				commandedFps = UCHAR_MAX;
			Int newRunAhead = computeRunAhead(getMaximumLatency(), minFps,
				TheGlobalData->m_networkRunAheadSlack, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2);

			NetRunAheadCommandMsg *msg = newInstance(NetRunAheadCommandMsg);
			msg->setPlayerID(m_localSlot);
			if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
				msg->setID(GenerateNextCommandID());
			}

			// needs to be set to the greater of getExecutionFrame and TheGameLogic->getFrame() + oldRunAhead
			// This prevents the case of...
			// run ahead starts at 30
			// run ahead changes to 10 at frame 31 (the command was created on frame 1)
			// run ahead changes to 10 at frame 56 (the command was created on frame 46)
			// notice that 56 is within the previous run ahead of 30 which has triggered
			// the frame command count being set for frames 1 through 60 since run ahead
			// didn't change for the first time till frame 31.  This creates an extra command
			// for frame 56 that isn't accounted for in the frame command count that is sent
			// out in the NetFrameCommandMsg.  sheesh.
			if (nextExecutionFrame > (TheGameLogic->getFrame() + oldRunAhead)) {
				msg->setExecutionFrame(nextExecutionFrame);
			} else {
				msg->setExecutionFrame(TheGameLogic->getFrame() + oldRunAhead);
			}

			msg->setRunAhead(newRunAhead);
			msg->setFrameRate(commandedFps);
			//DEBUG_LOG(("ConnectionManager::updateRunAhead - new run ahead = %d, new frame rate = %d, execution frame %d\n", newRunAhead, minFps, msg->getExecutionFrame()));
			sendLocalCommand(msg, 0xff ^ (1 << minFpsPlayer)); // Send the packet to everyone but the lowest FPS player.

			NetRunAheadCommandMsg *msg2 = newInstance(NetRunAheadCommandMsg);
			msg2->setPlayerID(m_localSlot);
			if (DoesCommandRequireACommandID(msg2->getNetCommandType())) {
				/*
				 * Ok there needs to be a big friggin comment about this change...
				 * What happens is that the two run ahead commands get sent to different players
				 * using different command ID's.  So player 1 has the run ahead command as command x
				 * and player 2 has the command as command x+1.  This is all good except when it comes
				 * to players being disconnected.  With the new disconnect scheme player 1 could potentially
				 * send his run ahead command to player 2 (or the other way around) to let player 2 catch
				 * up to him.  So if player 2 has his run ahead command as x+1 and now he gets player 1's
				 * command list with the run ahead command listed as command x, he won't see them as being
				 * the same command and will now think he has two different run ahead commands for that frame
				 * and thus his command list will have an extra command and he will never be able to recover.
				 * So to fix this we have to use the same command ID for both run ahead commands.  That way
				 * when the commands are copied places for the disconnect screen they will be seen as the
				 * same command, and all will be good.
				 */
//				msg2->setID(GenerateNextCommandID());
				msg2->setID(msg->getID());
			}
			if (nextExecutionFrame > (TheGameLogic->getFrame() + oldRunAhead)) {
				msg2->setExecutionFrame(nextExecutionFrame);
			} else {
				msg2->setExecutionFrame(TheGameLogic->getFrame() + oldRunAhead);
			}

			/* EA gave this step to the slowest player alone; it now goes to everyone, so the two
				 messages carry the same rate.  They stay split because the recipients still have to be
				 split - see the command ID note above, which is about disconnect recovery, not about
				 the frame rate.  (The step used to be capped at a hardcoded 30 while the settled rate
				 was capped at FramesPerSecondLimit, so a room configured above 30 slowed its slowest
				 player down to 30 while everybody else ran faster.) */
			msg2->setRunAhead(newRunAhead);
			msg2->setFrameRate(commandedFps);

			sendLocalCommand(msg2, 1 << minFpsPlayer);

			msg->detach();
			msg2->detach();
		}
		lasttimesent = curTime;
	}
}

Real ConnectionManager::getMaximumLatency() {
	// EA: "This works for 2 player games because the latency for the packet router is always 0."
	// It is not, so it is left out by name - see roomLatencySum.
	Bool connected[MAX_SLOTS];
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		connected[i] = isPlayerConnected(i);
	}
	return roomLatencySum(m_latencyAverages, connected, MAX_SLOTS, m_packetRouterSlot);
}

void ConnectionManager::getMinimumFps(Int &minFps, Int &minFpsPlayer) {
	minFps = -1;
	minFpsPlayer = -1;
//	DEBUG_LOG(("ConnectionManager::getMinimumFps -"));
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if ((m_connections[i] != NULL) || (i == m_localSlot)) {
//			DEBUG_LOG((" %d: %d,", i, m_fpsAverages[i]));
			if (m_fpsAverages[i] != -1) {
				if ((minFps == -1) || (m_fpsAverages[i] < minFps)) {
					minFps = m_fpsAverages[i];
					minFpsPlayer = i;
				}
			}
		}
	}
//	DEBUG_LOG(("\n"));
}

/**
 * The smallest cushion measured over the current window, or a negative number if nothing has been
 * measured yet.  This used to return UnsignedInt while FrameMetrics returns Int, so the "no sample
 * yet" sentinel came out the other side as four billion frames of margin - see CushionMetrics.h.
 */
Int ConnectionManager::getMinimumCushion() {
	return m_frameMetrics.getMinimumCushion();
}

/**
 * The commands for the given frame are all ready, time to send out our command count for that frame.
 */
void ConnectionManager::processFrameTick(UnsignedInt frame) {
	if ((m_frameData[m_localSlot] == NULL) || (m_frameData[m_localSlot]->getIsQuitting() == TRUE)) {
		// if the local frame data stuff is NULL, we must be leaving the game.
		return;
	}
	UnsignedShort commandCount = m_frameData[m_localSlot]->getCommandCount(frame);
	NetFrameCommandMsg *msg = newInstance(NetFrameCommandMsg);
	msg->setExecutionFrame(frame);
	msg->setCommandCount(commandCount);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setPlayerID(m_localSlot);

	m_frameMetrics.doPerFrameMetrics(frame);
	if ((Int)frame > m_lastFrameTickSent)
		m_lastFrameTickSent = (Int)frame;

	DEBUG_LOG_DEV(("ConnectionManager::processFrameTick - sending frame info for frame %d, ID %d, command count %d\n", frame, msg->getID(), commandCount));

	sendLocalCommand(msg, 0xff & ~(1 << m_localSlot));

	msg->detach();
}

/**
 * Set the local address.
 */
void ConnectionManager::setLocalAddress(UnsignedInt ip, UnsignedInt port) {
	DEBUG_LOG(("ConnectionManager::setLocalAddress() - local address is %X:%d\n", ip, port));
	m_localAddr = ip;
	m_localPort = port;
}

/**
 * Initialize the transport object
 */
void ConnectionManager::initTransport() {
	DEBUG_ASSERTCRASH((m_transport == NULL), ("m_transport already exists when trying to init it."));
	DEBUG_LOG(("ConnectionManager::initTransport - Initializing Transport\n"));
	if (m_transport != NULL) {
		delete m_transport;
		m_transport = NULL;
	}
	m_transport = new Transport;
	m_transport->reset();
	m_transport->init(m_localAddr, m_localPort);
}

/**
 * This is where the commands from the local client are sent to the other clients in
 * the game.  This is also where the local commands are put into the frame data for
 * future execution.
 */
void ConnectionManager::sendLocalGameMessage(GameMessage *msg, UnsignedInt frame) {
	UnsignedShort currentID = 0;
	if (DoesCommandRequireACommandID(NETCOMMANDTYPE_GAMECOMMAND)) {
		currentID = GenerateNextCommandID();
	}

	NetCommandMsg *netmsg = newInstance(NetGameCommandMsg)(msg);
	netmsg->setExecutionFrame(frame);
	netmsg->setPlayerID(m_localSlot);
	netmsg->setID(currentID);

	sendLocalCommand(netmsg);

	netmsg->detach();
}

/**
 * This is a NetCommandMsg that originated on the local computer. Send this to everyone specified
 * in the relay field.  Commands sent in this way go through the packet router.
 */
void ConnectionManager::sendLocalCommand(NetCommandMsg *msg, UnsignedByte relay /* = 0xff by default*/) {
	if (CommandRequiresDirectSend(msg) || (m_packetRouterSlot < 0) || (m_packetRouterSlot >= MAX_SLOTS) || (m_connections[m_packetRouterSlot] == NULL)) {
		sendLocalCommandDirect(msg, relay);
		return;
	}
	msg->attach();

	DEBUG_LOG_DEV(("ConnectionManager::sendLocalCommand - sending net command %d of type %s\n", msg->getID(),
		GetAsciiNetCommandType(msg->getNetCommandType()).str()));

	if (relay & (1 << m_localSlot)) {
		DEBUG_LOG_DEV(("ConnectionManager::sendLocalCommand - adding net command of type %s to player %d for frame %d\n", GetAsciiNetCommandType(msg->getNetCommandType()).str(), msg->getPlayerID(), msg->getExecutionFrame()));
		m_frameData[m_localSlot]->addNetCommandMsg(msg);
	}

	// Send the packet to everyone else
	if (m_localSlot == m_packetRouterSlot) {
		// I am the packet router, I need to send this packet to everyone individually.
		for (Int i = 0; i < MAX_SLOTS; ++i) {
			// Send it to all open connections.
			if (((m_connections[i] != NULL) && (m_connections[i]->isQuitting() == FALSE)) && (relay & (1 << i))) {
				// Set the relay mask to only go to this player so he knows not to relay it to anyone else.
				UnsignedByte temprelay = 1 << i;
				m_connections[i]->sendNetCommandMsg(msg, temprelay); // This will create a new copy of netmsg for this connection.
			}
		}
	} else {
		// Send the command to everyone else via the packet router.
		UnsignedByte temprelay = relay & ~(1 << m_localSlot);	// Tell the packet router to relay the message to everyone but myself.
														// Hopefully the packet router is smart enough to not send it
														// to slots that are not in the game.

		m_connections[m_packetRouterSlot]->sendNetCommandMsg(msg, temprelay); // This will create a new copy of netmsg for this connection.

		if (CommandRequiresAck(msg)) {
			NetCommandRef *ref = m_pendingCommands->addMessage(msg);
			//DEBUG_LOG(("ConnectionManager::sendLocalCommand - added command %d to pending commands list.\n", msg->getID()));
			if (ref != NULL) {
				ref->setRelay(temprelay);
			}
		}
	}

	msg->detach(); // detach from the command msg.
}

/**
 * This is a NetCommandMsg that originated on the local computer.  Send this to everyone specified
 * in the relay field.  Commands sent in this way do not go through the packet router.
 */
void ConnectionManager::sendLocalCommandDirect(NetCommandMsg *msg, UnsignedByte relay) {
	msg->attach();

	if (((relay & (1 << m_localSlot)) != 0) && (m_frameData[m_localSlot] != NULL)) {
		if (IsCommandSynchronized(msg->getNetCommandType()) == TRUE) {
			DEBUG_LOG_DEV(("ConnectionManager::sendLocalCommandDirect - adding net command of type %s to player %d for frame %d\n", GetAsciiNetCommandType(msg->getNetCommandType()).str(), msg->getPlayerID(), msg->getExecutionFrame()));
			m_frameData[m_localSlot]->addNetCommandMsg(msg);
		}
	}

	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if ((relay & (1 << i)) != 0) {
			if ((m_connections[i] != NULL) && (m_connections[i]->isQuitting() == FALSE)) {
				UnsignedByte temprelay = 1 << i;
				m_connections[i]->sendNetCommandMsg(msg, temprelay);
				DEBUG_LOG_DEV(("ConnectionManager::sendLocalCommandDirect - Sending direct command %d of type %s to player %d\n", msg->getID(), GetAsciiNetCommandType(msg->getNetCommandType()).str(), i));
			}
		}
	}

	msg->detach();
}

Int commandsReadyDebugSpewage = 0;

/**
 * Returns true if all the commands for the given frame are ready to be executed.
 */
/**
 * How many frames the room could play right now without waiting, counted from fromFrame.  A read
 * only: allCommandsReady asks for a resend when it finds a frame in a bad state, and this is for a
 * readout, which must not change what goes on the wire.
 */
UnsignedInt ConnectionManager::countFramesReady(UnsignedInt fromFrame, UnsignedInt maxFrames) {
	for (UnsignedInt ready = 0; ready < maxFrames; ++ready) {
		const UnsignedInt frame = fromFrame + ready;
		for (Int i = 0; i < MAX_SLOTS; ++i) {
			if ((m_frameData[i] == NULL) || m_frameData[i]->getIsQuitting()) {
				continue;
			}
			if (m_frameData[i]->getFrameCommandCount(frame) != m_frameData[i]->getCommandCount(frame)) {
				return ready;
			}
		}
	}
	return maxFrames;
}

Bool ConnectionManager::allCommandsReady(UnsignedInt frame, Bool justTesting /* = FALSE */) {
	Bool retval = TRUE;
	/* Read after the loop, and the loop can exit before ever assigning it. */
	FrameDataReturnType frameRetVal = FRAMEDATA_READY;
	Int notReadyPlayer = -1;
//	retval = FALSE;  // ****for testing purposes only!!!!!!****
	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for (i = 0; (i < MAX_SLOTS) && retval; ++i) {
		if ((m_frameData[i] != NULL) && (m_frameData[i]->getIsQuitting() == FALSE)) {
/*
			if (!(m_frameData[i]->allCommandsReady(frame, (frame != commandsReadyDebugSpewage) && (justTesting == FALSE)))) {
				if ((frame != commandsReadyDebugSpewage) && (justTesting == FALSE)) {
					DEBUG_LOG(("ConnectionManager::allCommandsReady, frame %d player %d not ready.\n", frame, i));
					commandsReadyDebugSpewage = frame;
				}
				retval = FALSE;
			} else {
//				DEBUG_LOG(("ConnectionManager::allCommandsReady, frame %d player %d is ready.\n", frame, i));
			}
*/

			frameRetVal = m_frameData[i]->allCommandsReady(frame, (frame != commandsReadyDebugSpewage) && (justTesting == FALSE));
			if (frameRetVal == FRAMEDATA_NOTREADY) {
				notReadyPlayer = i;
				retval = FALSE;
			} else if (frameRetVal == FRAMEDATA_RESEND) {
				requestFrameDataResend(i, frame);
				retval = FALSE;
			}
		}
	}

	if (frameRetVal == FRAMEDATA_RESEND) {
		// this frame's data is really screwed up, we need to clean it out so it can be resent to us.
		for (i = 0; i < MAX_SLOTS; ++i) {
			if ((m_frameData[i] != NULL) && (i != m_localSlot)) {
				m_frameData[i]->resetFrame(frame, FALSE);
			}
		}
	}

	/* A frame that never got its FRAMEINFO stays NOTREADY for ever on its own - FrameData escalates
		 to FRAMEDATA_RESEND only when it holds more commands than were announced, never when the
		 announcement is the thing that was lost.  Ask for it.  See FrameResendPolicy.h for why the
		 wait is the connection's own retry timeout rather than a constant. */
	if ((notReadyPlayer >= 0) && (justTesting == FALSE)) {
		time_t now = Clock_Milliseconds();

		if (frame != m_resendWatchFrame) {
			m_resendWatchFrame = frame;
			m_resendWatchSince = now;
			m_resendLastRequest = 0;			// nothing asked for this frame yet
		}

		UnsignedInt stalledMS = (UnsignedInt)(now - m_resendWatchSince);
		UnsignedInt sinceLastRequestMS = (m_resendLastRequest == 0)
			? stalledMS
			: (UnsignedInt)(now - m_resendLastRequest);

		/* The frame comes the way every frame comes, through the packet router unless we are the
			 router, so that link's timeout is the wait.  The request and its answer go straight between
			 us and the player we wait on, though, so once that link has been measured a slower one
			 stretches the wait, or we ask again before the answer can land.  Unmeasured, it sits at its
			 2000 ms ceiling, which made every such stall wait two seconds before asking.  With two
			 players the router is the player we wait on, so nothing changes there. */
		UnsignedInt retryTimeoutMS = FRAME_RESEND_MIN_TIMEOUT_MS;
		Connection *direct = m_connections[notReadyPlayer];
		if ((m_packetRouterSlot < MAX_SLOTS) && (m_connections[m_packetRouterSlot] != NULL)) {
			retryTimeoutMS = m_connections[m_packetRouterSlot]->getRetryTimeMS();
			if ((direct != NULL) && direct->hasLatencySample() && (direct->getRetryTimeMS() > retryTimeoutMS)) {
				retryTimeoutMS = direct->getRetryTimeMS();
			}
		} else if (direct != NULL) {
			retryTimeoutMS = direct->getRetryTimeMS();
		}

		/* Ask everyone the frame is still missing from, not only the first.  The loop above stops at
			 the first player that is not ready, so with three or more seats a frame lost from two of
			 them was asked for one seat at a time, the second only after the first had answered and a
			 whole new wait had run out: 1.5 to 3 seconds frozen on frame 60 at the start of every lossy
			 three-player match.  The players before notReadyPlayer were ready, or the loop would have
			 stopped on them. */
		if (shouldRequestFrameResend(stalledMS, sinceLastRequestMS, retryTimeoutMS)) {
			for (i = notReadyPlayer; i < MAX_SLOTS; ++i) {
				if ((m_frameData[i] == NULL) || m_frameData[i]->getIsQuitting() ||
						(m_frameData[i]->getFrameCommandCount(frame) == m_frameData[i]->getCommandCount(frame))) {
					continue;
				}
				DEBUG_LOG(("ConnectionManager::allCommandsReady - frame %d still missing from player %d after %d ms, asking again (retry timeout %d ms)\n",
					frame, i, stalledMS, retryTimeoutMS));
				requestFrameDataResend(i, frame);
			}
			m_resendLastRequest = now;
		}
	}

	if ((retval == TRUE) && (justTesting == FALSE)) {
		m_disconnectManager->allCommandsReady(TheGameLogic->getFrame(), this);
		retval = m_disconnectManager->allowedToContinue(); // allow the disconnect manager to keep us on this frame
																											// in case we are waiting for a new packet router or something.
	}

	return retval;
}

void ConnectionManager::handleAllCommandsReady(void)
{
	m_disconnectManager->allCommandsReady(TheGameLogic->getFrame(), this, FALSE);
}


/**
 * Only call this after making sure that all the commands are there for this frame.
 * After calling this the commands for this frame will be removed from the connection.
 *
 * BGC - To account for the case where the host disconnects without sending the
 *       same commands to all players, we now have to keep around the last 'run ahead'
 *       frames so we can potentially send those commands to the other players in the
 *       game so they can catch up.
 */
NetCommandList *ConnectionManager::getFrameCommandList(UnsignedInt frame) 
{
	NetCommandList *retlist = newInstance(NetCommandList);
	retlist->init();

	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_frameData[i] != NULL) {
			retlist->appendList(m_frameData[i]->getFrameCommandList(frame));
			if (frame > FRAMES_TO_KEEP) {
				m_frameData[i]->resetFrame(frame - FRAMES_TO_KEEP);	// After getting the commands for that frame from this
													// FrameDataManager object, we need to tell it that we're
													// done with the messages for that frame.
				DEBUG_LOG_DEV(("getFrameCommandList - called reset frame on player %d for frame %d\n", i, frame - FRAMES_TO_KEEP));
			}
		}
	}

	return retlist; // retlist deallocated by calling function.
}

void ConnectionManager::setFrameGrouping(time_t frameGrouping) {
	// Since we are the packet router, we should send more packets per second since we
	// may become the latency bottleneck for sending packets from one player to the next.
	// This is probably ok since the packet router should have the fastest connection of all
	// the players in the game.
	if (m_localSlot == m_packetRouterSlot) {
		frameGrouping = frameGrouping / 2;
	}
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_connections[i] != NULL) {
			m_connections[i]->setFrameGrouping(frameGrouping);
		}
	}
}

/*
void ConnectionManager::determineRouterFallbackPlan() {
	memset(m_packetRouterFallback, 0, sizeof(m_packetRouterFallback));
	Int curnum = 1;
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_connections[i] != NULL) {
			m_packetRouterFallback[i] = curnum;
			if (curnum == 1) {
				m_packetRouterSlot = i;
			}
			++curnum;
		}
	}
}
*/

void ConnectionManager::doKeepAlive() {
	time_t curTime = Clock_Milliseconds();

	if (m_keepAliveRoundStart == 0) {
		m_keepAliveRoundStart = curTime;
		m_keepAliveNextSlot = 0;
	}

	/* EA counted whole seconds against MAX_SLOTS here, in two function statics, which made the
		 interval a hardcoded 7-8 s and left NetworkKeepAliveDelay parsed, logged and never read.
		 The round is the knob now, bounded - see KeepAliveSchedule.h. */
	UnsignedInt roundMS = keepAliveRoundMS( TheGlobalData->m_networkKeepAliveDelay );
	UnsignedInt elapsedMS = (UnsignedInt)(curTime - m_keepAliveRoundStart);
	Int due = keepAliveSlotsDue( elapsedMS, roundMS, MAX_SLOTS );

	while (m_keepAliveNextSlot < due) {
		if (m_connections[m_keepAliveNextSlot] != NULL) {
			NetKeepAliveCommandMsg *msg = newInstance(NetKeepAliveCommandMsg);
			msg->setPlayerID(m_localSlot);
			if (DoesCommandRequireACommandID(msg->getNetCommandType()) == TRUE) {
				msg->setID(GenerateNextCommandID());
			}
//			DEBUG_LOG(("ConnectionManager::doKeepAlive - sending keep alive message to player %d\n", m_keepAliveNextSlot));
			sendLocalCommandDirect(msg, 1 << m_keepAliveNextSlot);
			msg->detach();
		}
		++m_keepAliveNextSlot;
	}

	if (keepAliveRoundIsOver( elapsedMS, roundMS )) {
		m_keepAliveNextSlot = 0;
		m_keepAliveRoundStart = curTime;
	}
}

PlayerLeaveCode ConnectionManager::disconnectPlayer(Int slot) {
	// Need to do the deletion of the slot's connection and frame data here.
	PlayerLeaveCode retval = PLAYERLEAVECODE_CLIENT;
	DEBUG_LOG(("ConnectionManager::disconnectPlayer - disconnecting slot %d on frame %d\n", slot, TheGameLogic->getFrame()));

	if ((slot < 0) || (slot >= MAX_SLOTS)) {
		return PLAYERLEAVECODE_UNKNOWN;
	}

	if (TheGameInfo)
	{
		GameSlot *gSlot = TheGameInfo->getSlot( slot );
		if (gSlot && !gSlot->lastFrameInGame())
		{
			DEBUG_LOG(("ConnectionManager::disconnectPlayer(%d) - slot is last in the game on frame %d\n",
				slot, TheGameLogic->getFrame()));
			gSlot->setLastFrameInGame(TheGameLogic->getFrame());
		}
	}

	UnicodeString unicodeName;
	unicodeName = getPlayerName(slot);
	if (unicodeName.getLength() > 0 && m_connections[slot]) {
		UnicodeString left;
		left.format( TheGameText->fetch( "Network:PlayerLeftGame" ), unicodeName.str() );
		AsciiString slotName;
		slotName.format( "player%d", slot );
		Player *player = ThePlayerList->findPlayerWithNameKey( TheNameKeyGenerator->nameToKey( slotName ) );
		if( player )
			TheInGameUI->playerMessage( player, left );
		else
			TheInGameUI->message( UnicodeString( u"%ls" ), left.str() );

		// People are boneheads. Also play a sound
		static AudioEventRTS leftGameSound("GUIMessageReceived");
		TheAudio->addAudioEvent(&leftGameSound);
	}

	if ((m_frameData[slot] != NULL) && (m_frameData[slot]->getIsQuitting() == FALSE)) {
		DEBUG_LOG(("ConnectionManager::disconnectPlayer - deleting player %d frame data\n", slot));
		m_frameData[slot]->deleteInstance();
		m_frameData[slot] = NULL;
	}

	if (m_connections[slot] != NULL && !m_connections[slot]->isQuitting()) {
		DEBUG_LOG(("ConnectionManager::disconnectPlayer - deleting player %d connection\n", slot));
		m_connections[slot]->deleteInstance();
		m_connections[slot] = NULL;
	}

//	if (playerID == m_localSlot) {
//		TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
//	}

	if (slot == m_packetRouterSlot) {
		// The same walk as getNextPacketRouterSlot, and it used to run off the end of the plan in the
		// same way - see nextPacketRouterSlot in CushionMetrics.cpp.  MAX_SLOTS out means the plan is
		// exhausted, which every reader of m_packetRouterSlot already tests for.
		if (m_packetRouterSlot < MAX_SLOTS) m_routerCommandAuthors |= (UnsignedByte)(1u << m_packetRouterSlot);
		m_packetRouterSlot = nextPacketRouterSlot(m_packetRouterFallback, MAX_SLOTS, m_packetRouterSlot);
		DEBUG_LOG(("Packet router left.  New packet router is slot %d\n", m_packetRouterSlot));
		retval = PLAYERLEAVECODE_PACKETROUTER;
	}
	if (m_localSlot == slot) {
		DEBUG_LOG(("Disconnecting self\n"));
		retval = PLAYERLEAVECODE_LOCAL;
	}

	// Take the player out of the fallback plan
	Int fallbackindex = 0;
	while ((fallbackindex < MAX_SLOTS) && (m_packetRouterFallback[fallbackindex] != slot)) {
		++fallbackindex;
	}

	for (Int i = fallbackindex; i < MAX_SLOTS-1; ++i) {
		m_packetRouterFallback[i] = m_packetRouterFallback[i+1];
	}
	m_packetRouterFallback[MAX_SLOTS-1] = -1;

	return retval;
}

void ConnectionManager::quitGame() {
	// Need to do the NetDisconnectPlayerCommandMsg creation and sending here.
	NetDisconnectPlayerCommandMsg *disconnectMsg = newInstance(NetDisconnectPlayerCommandMsg);
	disconnectMsg->setDisconnectSlot(m_localSlot);
	disconnectMsg->setPlayerID(m_localSlot);
	// Every other machine reads this to know which frame we left on; quitting through the
	// disconnection menu was the one route that never filled it in.
	disconnectMsg->setDisconnectFrame(TheGameLogic->getFrame());
	if (DoesCommandRequireACommandID(disconnectMsg->getNetCommandType())) {
		disconnectMsg->setID(GenerateNextCommandID());
	}
	//DEBUG_LOG(("ConnectionManager::disconnectLocalPlayer - about to send disconnect command\n"));
	sendLocalCommandDirect(disconnectMsg, 0xff ^ (1 << m_localSlot));

	//DEBUG_LOG(("ConnectionManager::disconnectLocalPlayer - about to flush connections\n"));
	flushConnections(); // need to do this so our packet actually gets sent before the connections are deleted.
	//DEBUG_LOG(("ConnectionManager::disconnectLocalPlayer - done flushing connections\n"));

	disconnectMsg->detach();

	disconnectLocalPlayer();
}

void ConnectionManager::disconnectLocalPlayer() {
	// kill the frame data and the connections for all the other players.
	DEBUG_LOG(("ConnectionManager::disconnectLocalPlayer()\n"));
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (i != m_localSlot) {
			disconnectPlayer(i);
		}
	}
}

/**
 * Takes all the commands that are ready to send and sends them right now.
 */
void ConnectionManager::flushConnections() {
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_connections[i] != NULL) {
//			DEBUG_LOG(("ConnectionManager::flushConnections - flushing connection to player %d\n", i));
			/*
			if (m_connections[i]->isQueueEmpty()) {
//				DEBUG_LOG(("ConnectionManager::flushConnections - connection queue empty\n"));
			}
			*/
			m_connections[i]->doSend();
		}
	}

	if (m_transport != NULL) {
		m_transport->doSend();
	}
}

void ConnectionManager::resendPendingCommands() {
	//DEBUG_LOG(("ConnectionManager::resendPendingCommands()\n"));
	if (m_pendingCommands == NULL) {
		return;
	}

	NetCommandRef *ref = m_pendingCommands->getFirstMessage();
	while (ref != NULL) {
		//DEBUG_LOG(("ConnectionManager::resendPendingCommands - resending command %d\n", ref->getCommand()->getID()));
		sendLocalCommand(ref->getCommand(), ref->getRelay());
		ref = ref->getNext();
	}
}

UnsignedInt ConnectionManager::getLocalPlayerID() {
	return m_localSlot;
}

UnicodeString ConnectionManager::getPlayerName(Int playerNum) {
	UnicodeString retval;
	if( playerNum == m_localSlot ) {
		retval = m_localUser->GetName();
	}	else if (((m_connections[playerNum] != NULL) && (m_connections[playerNum]->isQuitting() == FALSE))) {
		retval = m_connections[playerNum]->getUser()->GetName();
	}
	return retval;
}

/**
 * Take a user list and make connections and frame data manager objects for each of the players.
 * For now, this is also how we'll determine the packet router fallback plan.
 */
void ConnectionManager::parseUserList(const GameInfo *game)
{
	if (!game)
		return;

	// every machine reads the same game options, so every machine starts the room at the same speed
	m_frameMetrics.setMatchFps(GameEngine_matchFramesPerSecond(game->getTurbo()));

	Int i;
	Int numUsers = 0;
	m_localSlot = -1;
	DEBUG_LOG(("Local slot is %d\n", game->getLocalSlotNum()));
	for (i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);	// badness, but since we cast right back to const, we should be ok
		if (slot->isHuman())
		{
			if (game->getLocalSlotNum() == i)
			{
				m_localSlot = i;
   			m_localUser->setName(slot->getName());
			}

			if (m_localSlot != i)
			{
				m_connections[i] = newInstance(Connection)();
				m_connections[i]->init();
				m_connections[i]->attachTransport(m_transport);
//				UnsignedShort port = (TheNAT)?TheNAT->getSlotPort(i):8088;
				UnsignedShort port = slot->getPort();
				m_connections[i]->setUser(newInstance(User)(slot->getName(), slot->getIP(), port));
				m_frameData[i] = newInstance(FrameDataManager)(FALSE);
				DEBUG_LOG(("Remote user is at %X:%d\n", slot->getIP(), slot->getPort()));
			}
			else
			{
				DEBUG_LOG(("Local user is %d (%X:%d)\n", m_localSlot, slot->getIP(), slot->getPort()));
				m_frameData[i] = newInstance(FrameDataManager)(TRUE);
			}
			m_frameData[i]->init();
			m_frameData[i]->reset();

			m_packetRouterFallback[numUsers] = i;

			++numUsers;

		}
	}

	// The router is the head of the fallback plan, which is what EA's own commented-out
	// determineRouterFallbackPlan did.  init() hardcodes slot 0 instead, and slot 0 is only in the
	// plan at all if slot 0 holds a human player - otherwise the router was a slot nobody relays for,
	// and the first succession then walked a plan the router was not in.  Built from the shared slot
	// list, so every machine computes the same head without exchanging anything.
	m_packetRouterSlot = m_packetRouterFallback[0];
	m_routerCommandAuthors = m_packetRouterSlot < MAX_SLOTS ? (UnsignedByte)(1u << m_packetRouterSlot) : 0;
#ifdef MEMORYPOOL_DEBUG
	TheMemoryPoolFactory->debugSetInitFillerIndex(m_localSlot);
#endif

	/*
	if ( numUsers < 2 || m_localSlot == -1 )
	{
		DEBUG_CRASH(("FAILED parseUserList - network game won't work as expected\n"));
		return;
	}

	char * list = strdupAsWindows(buf);
	char *listPtr = list;
	if (!list)
		return;

	User users[MAX_SLOTS];
	int localUser = -1;
	int i;

	for (i=0; i<MAX_SLOTS; i++)
	{
		users[i].setName("");
	}

	char *userStr, *nameStr, *addrStr, *portStr;
	AsciiString addrAsciiStr;

	char *listPos;

	DEBUG_LOG(("ConnectionManager::parseUserList - looking for local user at %d.%d.%d.%d:%d\n",
		(m_localAddr >> 24) & 0xff,
		(m_localAddr >> 16) & 0xff,
		(m_localAddr >> 8) & 0xff,
		m_localAddr & 0xff,
		m_localPort));

	int numUsers = 0;
	while ( (userStr=strtok_r(listPtr, ",", &listPos)) != NULL )
	{
		listPtr = NULL;
		char *pos = NULL;

		nameStr = strtok_r(userStr, "@", &pos);
		addrStr = strtok_r(NULL, "@:", &pos);
		portStr = strtok_r(NULL, ": ", &pos);

		if (!portStr || numUsers >= MAX_SLOTS)
		{
			DEBUG_LOG(("ConnectionManager::parseUserList - (numUsers = %d) FAILED parseUserList with list [%s]\n", numUsers, buf));
			return;
		}

		addrAsciiStr = addrStr;
		UnsignedInt addr = ResolveIP(addrAsciiStr);
		UnsignedInt port = atoi(portStr);

//		if ((m_localAddr != addr) || (m_localPort != port)) {
		if (loginName.compare(nameStr) != 0) {
			m_connections[numUsers] = newInstance(Connection)();
			m_connections[numUsers]->init();
			m_connections[numUsers]->attachTransport(m_transport);
			m_connections[numUsers]->setUser(newInstance(User)(nameStr, addr, port));

			m_frameData[numUsers] = newInstance(FrameDataManager)(FALSE);

			DEBUG_LOG(("ConnectionManager::parseUserList - User %d is %s\n", numUsers, nameStr));
		} else {
			m_localSlot = numUsers;
			m_localUser.setName(nameStr);

			DEBUG_LOG(("ConnectionManager::parseUserList - User %d is %s\n", numUsers, nameStr));
			DEBUG_LOG(("Local user is %d\n", m_localSlot));

			m_frameData[numUsers] = newInstance(FrameDataManager)(TRUE);
		}
		m_frameData[numUsers]->init();
		m_frameData[numUsers]->reset();

		m_packetRouterFallback[numUsers] = numUsers;

		numUsers++;
	}

	if (numUsers < 2 || m_localSlot == -1)
	{
		DEBUG_LOG(("ConnectionManager::parseUserList - FAILED (local user = %d, num players = %d) with list [%s]\n", m_localSlot, numUsers, buf));
		return;
	}

	if (list != NULL) {
		free(list); // from the strdup above.
		list = NULL;
	}
	*/
}

/**
 * Return the number of incoming bytes per second averaged over 30 sec.
 */
Real ConnectionManager::getIncomingBytesPerSecond( void )
{
	if (m_transport)
		return m_transport->getIncomingBytesPerSecond();
	else
	  return 0.0;
}

/**
 * Return the number of incoming packets per second averaged over the last 30 sec.
 */
Real ConnectionManager::getIncomingPacketsPerSecond( void )
{
	if (m_transport)
		return m_transport->getIncomingPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * Return the number of outgoing bytes per second averaged over the last 30 sec.
 */
Real ConnectionManager::getOutgoingBytesPerSecond( void )
{
	if (m_transport)
		return m_transport->getOutgoingBytesPerSecond();
	else
	  return 0.0;
}

/**
 * Return the number of outgoing packets per second averaged over the last 30 sec.
 */
Real ConnectionManager::getOutgoingPacketsPerSecond( void )
{
	if (m_transport) {
		return m_transport->getOutgoingPacketsPerSecond();
	} else {
	  return 0.0;
	}
}

/**
 * Return the number of bytes not from generals clients received per second averaged over the last 30 sec.
 */
Real ConnectionManager::getUnknownBytesPerSecond( void )
{
	if (m_transport)
		return m_transport->getUnknownBytesPerSecond();
	else
	  return 0.0;
}

/**
 * Return the number ov packets not from generals clients received per second averaged over the last 30 sec.
 */
Real ConnectionManager::getUnknownPacketsPerSecond( void )
{
	if (m_transport)
		return m_transport->getUnknownPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * Return the smallest packet arrival cushion since this was last called.
 */
UnsignedInt ConnectionManager::getPacketArrivalCushion() {
	UnsignedInt retval = m_smallestPacketArrivalCushion;
	m_smallestPacketArrivalCushion = -1;
	return retval;
}

void ConnectionManager::sendChat(UnicodeString text, Int playerMask, UnsignedInt executionFrame)
{
	NetChatCommandMsg *msg = newInstance(NetChatCommandMsg);
	msg->setText(text);
	msg->setPlayerMask(playerMask);
	msg->setPlayerID(m_localSlot);
	msg->setID(0);
	msg->setExecutionFrame(executionFrame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType()))
	{
		msg->setID(GenerateNextCommandID());
	}
	DEBUG_LOG(("Chat message has ID of %d, mask of %8.8X, text of %s\n", msg->getID(), msg->getPlayerMask(), WideCharAsUtf8( msg->getText().str() ).str()));

	sendLocalCommand(msg, 0xff ^ (1 << m_localSlot));
	processChat(msg);

	msg->detach();
}

void ConnectionManager::sendDisconnectChat(UnicodeString text) {
	NetDisconnectChatCommandMsg *msg = newInstance(NetDisconnectChatCommandMsg);
	msg->setPlayerID(m_localSlot);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setText(text);

	sendLocalCommandDirect(msg, 0xff ^ (1 << m_localSlot));
	processDisconnectChat(msg);
}

UnsignedShort ConnectionManager::sendFileAnnounce(AsciiString path, UnsignedByte playerMask)
{
	File *theFile = TheLocalFileSystem->openFile(path.str());
	if (!theFile || !theFile->size())
	{
		UnicodeString log;
		log.format(u"Not sending file '%hs' to %X\n", path.str(), playerMask);
		DEBUG_LOG(("%s\n", WideCharAsUtf8( log.str() ).str()));
		if (TheLAN)
			TheLAN->OnChat(UnicodeString(u"sendFile"), 0, log, LANAPI::LANCHAT_SYSTEM);
		return 0;
	}

	theFile->close();

	Int announceMask = 0xff ^ (1 << m_localSlot);
	NetFileAnnounceCommandMsg *announceMsg = newInstance(NetFileAnnounceCommandMsg);
	announceMsg->setPlayerID(m_localSlot);
	if (DoesCommandRequireACommandID(announceMsg->getNetCommandType()) == TRUE) {
		announceMsg->setID(GenerateNextCommandID());
	}
	announceMsg->setRealFilename(path);
	announceMsg->setPlayerMask(playerMask);
	UnsignedShort fileID = GenerateNextCommandID();
	announceMsg->setFileID(fileID);
	DEBUG_LOG(("ConnectionManager::sendFileAnnounce() - creating announce message with ID of %d from %d to mask %X for '%s' going to %X as command %d\n",
		announceMsg->getID(), announceMsg->getPlayerID(), announceMask, announceMsg->getRealFilename().str(),
		announceMsg->getPlayerMask(), announceMsg->getFileID()));

	processFileAnnounce(announceMsg); // set up things for the host

	DEBUG_LOG(("Sending file announce to %X\n", announceMask));
	sendLocalCommand(announceMsg, announceMask);
	announceMsg->detach();

	return fileID;
}

void ConnectionManager::sendFile(AsciiString path, UnsignedByte playerMask, UnsignedShort commandID)
{
	File *theFile = TheLocalFileSystem->openFile(path.str());
	if (!theFile || !theFile->size())
	{
		UnicodeString log;
		log.format(u"Not sending file '%hs' to %X\n", path.str(), playerMask);
		DEBUG_LOG(("%s\n", WideCharAsUtf8( log.str() ).str()));
		if (TheLAN)
			TheLAN->OnChat(UnicodeString(u"sendFile"), 0, log, LANAPI::LANCHAT_SYSTEM);
		return;
	}

	Int len = theFile->size();
	char *buf = theFile->readEntireAndClose();

	// compress Targas
#ifdef COMPRESS_TARGAS
	char *compressedBuf = NULL;
	Int compressedLen = path.endsWith(".tga")?CompressionManager::getMaxCompressedSize(len, CompressionManager::getPreferredCompression()):0;
	Int compressedSize = 0;
	if (compressedLen)
		compressedSize = CompressionManager::compressData(CompressionManager::getPreferredCompression(),
		buf, len, compressedBuf, compressedLen);

	if (compressedBuf && !compressedSize)
	{
		delete[] compressedBuf;
		compressedBuf = NULL;
	}
#endif // COMPRESS_TARGAS

	NetFileCommandMsg *fileMsg = newInstance(NetFileCommandMsg);
	fileMsg->setPlayerID(m_localSlot);
	fileMsg->setID(commandID);
	fileMsg->setRealFilename(path);
#ifdef COMPRESS_TARGAS
	if (compressedBuf)
	{
		DEBUG_LOG(("Compressed '%s' from %d to %d (%g%%) before transfer\n", path.str(), len, compressedSize,
			(Real)compressedSize/(Real)len*100.0f));
		fileMsg->setFileData((unsigned char *)compressedBuf, compressedSize);
	}
	else
#endif // COMPRESS_TARGAS
	{
		fileMsg->setFileData((unsigned char *)buf, len);
	}

	DEBUG_LOG(("ConnectionManager::sendFile() - creating file message with ID of %d for '%s' going to %X from %d, size of %d\n",
		fileMsg->getID(), fileMsg->getRealFilename().str(), playerMask, fileMsg->getPlayerID(), fileMsg->getFileLength()));

	delete[] buf;
	buf = NULL;
#ifdef COMPRESS_TARGAS
	if (compressedBuf)
	{
		delete[] compressedBuf;
		compressedBuf = NULL;
	}
#endif // COMPRESS_TARGAS

	DEBUG_LOG(("Sending file: '%s', len %d, to %X\n", path.str(), len, playerMask));

	sendLocalCommand(fileMsg, playerMask);

	fileMsg->detach();
}

Int ConnectionManager::getFileTransferProgress(Int playerID, AsciiString path)
{
	FileCommandMap::iterator commandIt = s_fileCommandMap.begin();
	while (commandIt != s_fileCommandMap.end())
	{
		//DEBUG_LOG(("ConnectionManager::getFileTransferProgress(%s): looking at existing transfer of '%s'\n",
		//	path.str(), commandIt->second.str()));
		if (commandIt->second == path)
		{
			return s_fileProgressMap[playerID][commandIt->first];
		}
		++commandIt;
	}
	//DEBUG_LOG(("Falling back to 0, since we couldn't find the map\n"));
	DEBUG_LOG(("ConnectionManager::getFileTransferProgress: path %s not found\n",path.str()));
	return 0;
}


void ConnectionManager::voteForPlayerDisconnect(Int slot) {
	if (m_disconnectManager != NULL) {
		m_disconnectManager->voteForPlayerDisconnect(slot, this);
	}
}

Int ConnectionManager::getNumPlayers()
{
	Int retval = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (isPlayerConnected(i)) {
			++retval;
		}
	}

	return retval;
}

void ConnectionManager::updateLoadProgress( Int progress )
{
	NetProgressCommandMsg *msg = newInstance(NetProgressCommandMsg);
	msg->setPercentage( progress );
	msg->setPlayerID( m_localSlot );
	if (DoesCommandRequireACommandID(msg->getNetCommandType()) == TRUE) {
		msg->setID(GenerateNextCommandID());
	}
	processProgress(msg);
	sendLocalCommand(msg, 0xff ^ (1 << m_localSlot));

	msg->detach();
}

void ConnectionManager::loadProgressComplete()
{
	NetCommandMsg *msg = newInstance(NetCommandMsg);
	msg->setPlayerID( m_localSlot );
	if (DoesCommandRequireACommandID(msg->getNetCommandType()) == TRUE) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setNetCommandType(NETCOMMANDTYPE_LOADCOMPLETE);
	processLoadComplete(msg);
	sendLocalCommand(msg, 0xff ^ (1 << m_localSlot));

	msg->detach();
}

void ConnectionManager::sendTimeOutGameStart()
{
	NetCommandMsg *msg = newInstance(NetCommandMsg);
	msg->setPlayerID( m_localSlot );
	msg->setNetCommandType(NETCOMMANDTYPE_TIMEOUTSTART);
	if (DoesCommandRequireACommandID(msg->getNetCommandType()) == TRUE) {
		msg->setID(GenerateNextCommandID());
	}
	processTimeOutGameStart(msg);
	sendLocalCommand(msg, 0xff ^ (1 << m_localSlot));

	msg->detach();
}

Bool ConnectionManager::isPacketRouter( void )
{
	return m_localSlot == m_packetRouterSlot;
}

Int ConnectionManager::getAverageFPS( void )
{
	return m_frameMetrics.getAverageFPS();
}

Int ConnectionManager::getSlotAverageFPS(Int slot) {
	if ((slot < 0) || (slot >= MAX_SLOTS)) {
		return -1;
	}
	return m_fpsAverages[slot];
}

/// the slot's last reported round trip to the packet router, in seconds (FrameMetrics), or 0
Real ConnectionManager::getSlotAverageLatency(Int slot) {
	// the router's own figure times every client's acks, so one laggy client made the host look laggy
	if ((slot < 0) || (slot >= MAX_SLOTS) || (slot == (Int)m_packetRouterSlot)) {
		return 0.0f;
	}
	return m_latencyAverages[slot];
}

/// the player whose frame rate sets the room's speed, as the packet router picks him, or -1 when
/// he keeps up with the match anyway
Int ConnectionManager::getSlowestSlot( void ) {
	Int minFps;
	Int minFpsPlayer;
	getMinimumFps(minFps, minFpsPlayer);
	// nobody is holding the room back while everyone keeps within 10 % of the match's speed; a frame
	// or two of jitter under the cap would otherwise light a different player every half second
	if (minFps * 10 >= getMatchFps() * 9) {
		return -1;
	}
	return minFpsPlayer;
}

/**
 * How long since anything at all arrived that originated with this player.  Zero for a slot we
 * have no connection to, and for one we have never heard from - in both cases there is nothing
 * to hold against them.
 */
UnsignedInt ConnectionManager::getTimeSinceLastPacketFrom( Int slot ) const
{
	if ((slot < 0) || (slot >= MAX_SLOTS))
		return 0;
	if (m_connections[slot] == NULL)
		return 0;
	if (m_lastHeardFrom[slot] == 0)
		return 0;

	time_t now = Clock_Milliseconds();
	if (now <= m_lastHeardFrom[slot])
		return 0;

	return (UnsignedInt)(now - m_lastHeardFrom[slot]);
}

/**
 * The longest any connected player has gone without sending us anything.  This is the number that
 * separates "the game is slow" from "somebody has gone away" - see StallJudgement.h.
 */
UnsignedInt ConnectionManager::getWorstSilenceMS( void ) const
{
	UnsignedInt worst = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (i == (Int)m_localSlot)
			continue;					// we always hear ourselves

		UnsignedInt silence = getTimeSinceLastPacketFrom( i );
		if (silence > worst)
			worst = silence;
	}
	return worst;
}

#if defined(_DEBUG) || defined(_INTERNAL)
void ConnectionManager::debugPrintConnectionCommands() {
	DEBUG_LOG(("ConnectionManager::debugPrintConnectionCommands - begin commands\n"));
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_connections[i] != NULL) {
			DEBUG_LOG(("ConnectionManager::debugPrintConnectionCommands - commands for connection %d\n", i));
			m_connections[i]->debugPrintCommands();
		}
	}
	DEBUG_LOG(("ConnectionManager::debugPrintConnectionCommands - end commands\n"));
}
#endif

void ConnectionManager::notifyOthersOfCurrentFrame(Int frame) {
	NetDisconnectFrameCommandMsg *msg = newInstance(NetDisconnectFrameCommandMsg);

	msg->setPlayerID(m_localSlot);
	msg->setDisconnectFrame(frame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}

	DEBUG_LOG(("ConnectionManager::notifyOthersOfCurrentFrame - sending disconnect frame of %d, command ID = %d\n", frame, msg->getID()));
	sendLocalCommandDirect(msg, 0xff ^ (1 << m_localSlot));
	NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
	ref->setRelay(1 << m_localSlot);
	m_disconnectManager->processDisconnectCommand(ref, this);
	ref->deleteInstance();

	msg->detach();

	DEBUG_LOG(("ConnectionManager::notifyOthersOfCurrentFrame - start screen on debug stuff\n"));
#if defined(_DEBUG) || defined(_INTERNAL)
	debugPrintConnectionCommands();
#endif
}

void ConnectionManager::notifyOthersOfNewFrame(UnsignedInt frame) {
	NetDisconnectScreenOffCommandMsg *msg = newInstance(NetDisconnectScreenOffCommandMsg);

	msg->setPlayerID(m_localSlot);
	msg->setNewFrame(frame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}

	sendLocalCommandDirect(msg, 0xff ^ (1 << m_localSlot));
	NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
	ref->setRelay(1 << m_localSlot);
	m_disconnectManager->processDisconnectCommand(ref, this);
	ref->deleteInstance();

	msg->detach();
}

void ConnectionManager::sendFrameDataToPlayer(UnsignedInt playerID, UnsignedInt startingFrame) {
	DEBUG_LOG(("ConnectionManager::sendFrameDataToPlayer - sending frame data to player %d starting with frame %d\n", playerID, startingFrame));
	for (UnsignedInt frame = startingFrame; frame < TheGameLogic->getFrame(); ++frame) {
		sendSingleFrameToPlayer(playerID, frame);
	}

	/* EA stopped at the frame this machine is on, which is enough for a player who fell behind and
		 nothing for one on the same frame: when both machines lose the other's frame info for the
		 frame they are both on, each asks, each finds nothing below its own frame to send, and the
		 match hangs until the disconnect screen.  -latAvg 120 -packetloss 5 did it at frame 30 on the
		 retail pacing as well as with the network catch-up.  Our own frames from here up to the last
		 one announced are final - processFrameTick only runs once no command can land on that frame
		 any more - so those go too.  Other players' frames past ours are not ours to vouch for. */
	const Int firstUnplayed = ((Int)startingFrame > (Int)TheGameLogic->getFrame()) ? (Int)startingFrame : (Int)TheGameLogic->getFrame();
	for (Int frame = firstUnplayed; frame <= m_lastFrameTickSent; ++frame) {
		sendSlotFrameToPlayer(playerID, m_localSlot, frame);
	}
	DEBUG_LOG(("ConnectionManager::sendFrameDataToPlayer - done sending commands to player %d\n", playerID));
}

void ConnectionManager::sendSingleFrameToPlayer(UnsignedInt playerID, UnsignedInt frame) {
	if (frameIsTooOldToResend(TheGameLogic->getFrame(), frame, FRAMES_TO_KEEP)) {
		DEBUG_LOG(("ConnectionManager::sendSingleFrameToPlayer - player %d requested frame %d when we are on frame %d, this is too far in the past.\n", playerID, frame, TheGameLogic->getFrame()));
		return;
	}

	DEBUG_LOG(("ConnectionManager::sendFrameDataToPlayer - sending data for frame %d\n", frame));
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if ((m_frameData[i] != NULL) && (i != playerID)) { // no need to send his own commands to him.
			sendSlotFrameToPlayer(playerID, i, frame);
		}
	}
}

/** One slot's commands for one frame, then its frame info, the way sendSingleFrameToPlayer always
		sent every slot's. */
void ConnectionManager::sendSlotFrameToPlayer(UnsignedInt playerID, UnsignedInt slot, UnsignedInt frame) {
	UnsignedByte relay = 1 << playerID;

	NetCommandList *list = m_frameData[slot]->getFrameCommandList(frame);
	if (list != NULL) {
		NetCommandRef *ref = list->getFirstMessage();
		while (ref != NULL) {
			DEBUG_LOG(("ConnectionManager::sendFrameDataToPlayer - sending command %d from player %d to player %d using relay 0x%x\n", ref->getCommand()->getID(), slot, playerID, relay));
			sendLocalCommandDirect(ref->getCommand(), relay);
			ref = ref->getNext();
		}
	}
	/* A command is known by its author and id together, and ours is the only id counter we own.  EA
		 stamped another slot's frame info with an id from that counter, and the counters run nearly in
		 step, so the copy could carry the very id of a command that slot really had in flight: the
		 receiver's ack then cleared that command from a queue still owing it, and a packet holding both
		 kept one.  Another slot's frame info goes out under the id its author gave it, and not at all
		 when it never reached us, since then we have no count to vouch for either. */
	Int frameInfoID = (slot == m_localSlot) ? (Int)GenerateNextCommandID() : m_frameData[slot]->getFrameInfoID(frame);
	if (frameInfoID < 0) {
		return;
	}
	UnsignedInt frameCommandCount = m_frameData[slot]->getFrameCommandCount(frame);
	NetFrameCommandMsg *msg = newInstance(NetFrameCommandMsg);
	msg->setExecutionFrame(frame);
	msg->setCommandCount(frameCommandCount);
	msg->setID((UnsignedShort)frameInfoID);
	msg->setPlayerID(slot);
	DEBUG_LOG(("ConnectionManager::sendFrameDataToPlayer - sending frame info from player %d to player %d for frame %d with command count %d and ID %d and relay %d\n", slot, playerID, msg->getExecutionFrame(), msg->getCommandCount(), msg->getID(), relay));
	sendLocalCommandDirect(msg, relay);
	msg->detach();
}

UnsignedInt ConnectionManager::getNextPacketRouterSlot(UnsignedInt playerID) {
	return nextPacketRouterSlot(m_packetRouterFallback, MAX_SLOTS, playerID);
}

void ConnectionManager::requestFrameDataResend(Int playerID, UnsignedInt frame) {
	NetFrameResendRequestCommandMsg *msg = newInstance(NetFrameResendRequestCommandMsg);
	msg->setPlayerID(m_localSlot);
	msg->setFrameToResend(frame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}

	if (isPlayerConnected(playerID) == FALSE) {
		playerID = 0;
		while ((playerID < MAX_SLOTS) && (isPlayerConnected(playerID) == FALSE)) {
			++playerID;
		}
	}

	if (playerID < MAX_SLOTS) {
		m_recoveryPending[playerID] = TRUE;
		m_recoveryFrame[playerID] = frame;
		sendLocalCommandDirect(msg, 1 << playerID);
	}

	msg->detach();
}