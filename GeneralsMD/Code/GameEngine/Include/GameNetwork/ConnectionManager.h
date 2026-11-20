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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

/**
 * The connection manager maintains a collection of Connections to each player.  Messages are queued up
 * from here, as well as removed through here.
 */

#pragma once

#ifndef __CONNECTIONMANAGER_H
#define __CONNECTIONMANAGER_H

#include "GameNetwork/Connection.h"
#include "GameNetwork/NetCommandList.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/FrameDataManager.h"
#include "GameNetwork/FrameMetrics.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/DisconnectManager.h"
#include "GameNetwork/CushionMetrics.h"

class GameInfo;
class NetCommandWrapperList;

typedef std::map<UnsignedShort, AsciiString> FileCommandMap;
typedef std::map<UnsignedShort, SlotMask> FileMaskMap;
typedef std::map<UnsignedShort, Int> FileProgressMap;

class ConnectionManager
{
public:
	ConnectionManager();
	~ConnectionManager();

	virtual void init();				///< Initialize this instance.
	virtual void update(Bool isInGame);			///< Service the Connections being managed by this instance.

	// End SubsystemInterface functions

	void updateRunAhead(Int oldRunAhead, Int frameRate, Bool didSelfSlug, Int nextExecutionFrame);	///< Update the run ahead value.  If we are the current packet router, issue the command.

	void attachTransport(Transport *transport);

	void parseUserList(const GameInfo *game);
	void sendChat(UnicodeString text, SlotMask playerMask, UnsignedInt executionFrame);
	void sendDisconnectChat(UnicodeString text);
	void sendLocalCommand(NetCommandMsg *msg, SlotMask relay = SLOTMASK_ALL) { sendLocalCommandDirect(msg, relay); }	///< Every command goes straight to each player now; see sendLocalCommandDirect.
	void sendLocalCommandDirect(NetCommandMsg *msg, SlotMask relay);		///< Send command straight to each player in the relay, and file it locally when our bit is set.
	void sendLocalGameMessage(GameMessage *msg, UnsignedInt frame);
	void sendCommand(NetCommandMsg *msg);
	Bool allCommandsReady(UnsignedInt frame, Bool justTesting = FALSE);
	UnsignedInt countFramesReady(UnsignedInt fromFrame, UnsignedInt maxFrames);	///< Consecutive frames from fromFrame whose commands are all in, without asking for a resend.
	void handleAllCommandsReady(void);
	NetCommandList *getFrameCommandList(UnsignedInt frame);
//	void AddConnection(User *user, UnsignedInt slot);
	void determineRouterFallbackPlan();
	void zeroFrames(UnsignedInt startingFrame, UnsignedInt numFrames);
	void destroyGameMessages();
	void forgetFrames();					///< -rejoin: every slot's frame data back to nothing known, for a match joined far into it

	/* A dropped seat coming back (Network::startRejoin).  The seat asks; the packet router names a frame F
		 in a frame-synchronized SEATREJOIN, which every seat runs on F (readmitPlayer), and answers the seat
		 with F; the seat plays the match up to F from a replay and joins on F + 1. */
	/// the local seat is coming back: frame-synchronized commands wait in the senders' queues, unacked; the
	/// log is asked of logSlot and written to logPath (MAX_SLOTS: none, the replay is on this disk)
	void setRejoining(Bool rejoining, Int logSlot = MAX_SLOTS, AsciiString logPath = AsciiString::TheEmptyString);
	void sendRejoinRequest();
	Bool getRejoinAccept(UnsignedInt &frame) const;		///< FALSE until the router has answered
	Bool getRejoinRoom(UnsignedInt &frame, UnsignedInt &seats, UnsignedInt &router) const;	///< FALSE until the room has taken the seat back
	Bool hasRejoinLogArrived() const { return m_rejoinLogArrived; }
	void takeRejoinAccept(UnsignedInt seats, UnsignedInt router);	///< the room as the router left it: these seats, that router
	void readmitPlayer(Int slot, UnsignedInt fromFrame);
	void sendPendingRejoinLog();				///< the log asked of us, once we stand on the frame the room took the seat back for
	void updateAbsentSeats();						///< a side whose every enemy has been away 5 minutes wins; the router sends the forfeit
//	void createConnections(UnsignedInt numberOfPlayers, UnsignedInt localSlot);
	void setLocalAddress(UnsignedInt ip, UnsignedInt port);
	void initTransport();
	void processFrameTick(UnsignedInt frame);
	void handleLocalPlayerLeaving(UnsignedInt frame);

	void sendFile(AsciiString path, SlotMask playerMask, UnsignedShort commandID);
	UnsignedShort sendFileAnnounce(AsciiString path, SlotMask playerMask);
	Int getFileTransferProgress(Int playerID, AsciiString path);
	Bool areAllQueuesEmpty(void);

	UnsignedInt getLocalPlayerID();
	UnicodeString getPlayerName(Int playerNum);
	Int getNumPlayers();

	UnsignedInt getPacketRouterFallbackSlot(Int packetRouterNumber);	///< Returns the slot of the given packet router number in the fallback plan.
	UnsignedInt getPacketRouterSlot();																///< Returns the current packet router's slot.
	PlayerLeaveCode disconnectPlayer(Int slot, Bool mayComeBack = FALSE);		///< Disconnect this player immediately.  This should only be called by the disconnect manager.
	void showPlayerLine(Int slot, const char *label);								///< the chat line "label" formatted with the slot's name, with the message sound
	void notePlayerGone(Int slot);																	///< the seat's side is destroyed: a seat shown as lost now reads as left
	void disconnectLocalPlayer();																			///< Does whatever is necessary to get TheNetwork to realize that it should be leaving the game now.
	void quitGame();																									///< Disconnect from the game RIGHT NOW!!  Tell everyone else we are disconnecting.

	void voteForPlayerDisconnect(Int slot);														///< Register a vote for a player to be disconnected.

	void setFrameGrouping(time_t frameGrouping);				///< Set the number of frames that are grouped together into packets.

	PlayerLeaveCode processPlayerLeave(NetPlayerLeaveCommandMsg *msg);
	Bool canILeave();																///< Returns true if the local player is allowed to leave.

	// Bandwidth metrics
	Real getIncomingBytesPerSecond( void );
	Real getIncomingPacketsPerSecond( void );
	Real getOutgoingBytesPerSecond( void );
	Real getOutgoingPacketsPerSecond( void );
	Real getUnknownBytesPerSecond( void );
	Real getUnknownPacketsPerSecond( void );
	UnsignedInt getPacketArrivalCushion( void );

	Int getMinimumCushion();
	void addNetworkStall(time_t ms) { m_frameMetrics.addNetworkStall(ms); }
	Int getMatchFps() const { return m_frameMetrics.getMatchFps(); }	///< the game's speed, from parseUserList

	void flushConnections();

	void processChat(NetChatCommandMsg *msg); // this actually needs to be public because it is frame-synchronized
	
	void updateLoadProgress( Int progress );
	void loadProgressComplete( void );
	void sendTimeOutGameStart( void );
	
	Bool isPacketRouter( void );

	Bool isPlayerConnected( Int playerID );

	void notifyOthersOfCurrentFrame(Int frame);
	void sendFrameDataToPlayer(UnsignedInt playerID, UnsignedInt startingFrame);
	void sendSingleFrameToPlayer(UnsignedInt playerID, UnsignedInt frame);
	void sendSlotFrameToPlayer(UnsignedInt playerID, UnsignedInt slot, UnsignedInt frame);
	void notifyOthersOfNewFrame(UnsignedInt frame);

	UnsignedInt getNextPacketRouterSlot(UnsignedInt playerID); ///< returns the packet router player that comes after the given player.
	
	Int getAverageFPS( void );
	Int getSlotAverageFPS(Int slot);
	Real getSlotAverageLatency(Int slot);
	Int getSlowestSlot( void );

#if defined(_DEBUG) || defined(_INTERNAL)
	void debugPrintConnectionCommands();
#endif

	/* How long since anything at all arrived that originated with this player - a command, an ack,
		 a keep-alive.  A stalled game where this is small for everyone is slow; one where it is large
		 for somebody is a disconnect.  See StallJudgement.h. */
	UnsignedInt getTimeSinceLastPacketFrom( Int slot ) const;
	UnsignedInt getWorstSilenceMS( void ) const;

	// For disconnect blame assignment
	UnsignedInt getPingFrame();
	Int getPingsSent();
	Int getPingsRecieved();

private:
	void doRelay();
	void doKeepAlive();
	void sendRemoteCommand(NetCommandRef *msg);
	void ackCommand(NetCommandRef *ref, UnsignedInt localSlot, UnsignedInt senderSlot = MAX_SLOTS);	///< senderSlot: whose connection the copy came in on, MAX_SLOTS when unknown

	Bool processNetCommand(NetCommandRef *ref);
	void processAckStage1(NetCommandMsg *msg);
	void processAck(NetCommandMsg *msg);
	void processFrameInfo(NetFrameCommandMsg *msg);
	void processRunAheadMetrics(NetRunAheadMetricsCommandMsg *msg);
	void processDisconnectChat(NetDisconnectChatCommandMsg *msg);
	void processProgress( NetProgressCommandMsg *msg );
	void processLoadComplete( NetCommandMsg *msg );
	void processTimeOutGameStart( NetCommandMsg *msg );
	void processWrapper(NetCommandRef *ref);
	void processFrameResendRequest(NetFrameResendRequestCommandMsg *msg);
	void processRejoinRequest(NetCommandMsg *msg);
	void processRejoinAccept(NetCommandMsg *msg);
	void processRejoinRoom(NetCommandMsg *msg);
	void processRejoinLog(NetCommandMsg *msg);
	void sendPlayerForfeit(Int slot);
	void sendRejoinRoom(Int slot, UnsignedInt fromFrame);

	void processFile(NetFileCommandMsg *ref);
	void processFileAnnounce(NetFileAnnounceCommandMsg *ref);
	void processFileProgress(NetFileProgressCommandMsg *ref);

	//	void doPerFrameMetrics(UnsignedInt frame);
	void getMinimumFps(Int &minFps, Int &minFpsPlayer);			///< Returns the smallest FPS in the m_fpsAverages list.
	Real getMaximumLatency(); ///< The worst round trip any connected player reported, seconds (roomWorstLatency).
	Real getMaximumJitter();  ///< The worst rttvar any connected player reported, seconds.

	void requestFrameDataResend(Int playerID, UnsignedInt frame); ///< request of this player that he send the specified frame's data.

	// The connections are set up like the slot list. The connection corresponding to the local
	// player's position in the slot list will be NULL.  Connections corresponding to slots that
	// do not have a player will also be NULL.
	Connection *m_connections[MAX_SLOTS];

	Transport *m_transport;
	UnsignedInt m_localSlot;
	Int m_lastFrameTickSent;															///< the newest frame processFrameTick has announced; its command count is final
	UnsignedInt m_packetRouterSlot;
	UnsignedInt m_packetRouterFallback[MAX_SLOTS];
	UnsignedInt m_localAddr;
	UnsignedInt m_localPort;
	User* m_localUser;

	DisconnectManager *m_disconnectManager;						///< Controls the disconnect dialog.

	/* The frame the logic is currently stuck on, and how long it has been stuck, so a frame whose
		 FRAMEINFO was lost can be asked for again instead of waited out - see FrameResendPolicy.h. */
	UnsignedInt m_resendWatchFrame;										///< frame allCommandsReady last blocked on
	time_t m_resendWatchSince;												///< when it started blocking
	time_t m_resendLastRequest;												///< when we last asked for it; 0 = not asked

	FrameDataManager *m_frameData[MAX_SLOTS];

	FrameMetrics m_frameMetrics;

	NetCommandWrapperList *m_netCommandWrapperList;

	// These variables are used to keep track of the other players' average fps and latency.
	// yup.
	Real m_latencyAverages[MAX_SLOTS];
	Real m_latencyJitters[MAX_SLOTS];		///< seconds, each player's worst rttvar; the run-ahead's jitter margin is sized on the largest
	Int  m_fpsAverages[MAX_SLOTS];

	/* Wall clock of the last thing we heard from each player, zero for "nothing yet". */
	time_t m_lastHeardFrom[MAX_SLOTS];

	Bool m_rejoining;										///< the local seat is coming back and plays from a replay
	Bool m_rejoinAccepted;
	UnsignedInt m_rejoinAcceptFrame;		///< F, the frame the room runs SEATREJOIN on
	Bool m_rejoinRoomTaken;							///< REJOINROOM came: the room as it took the seat back
	UnsignedInt m_rejoinRoomFrame;
	UnsignedInt m_rejoinRoomSeats;			///< the seats playing from F + 1, a bit a slot
	UnsignedInt m_rejoinRoomRouter;
	Int m_rejoinLogSlot;								///< the seat the log is asked of, MAX_SLOTS for none
	AsciiString m_rejoinLogPath;				///< where it is written
	Bool m_rejoinLogArrived;
	UnsignedInt m_rejoinFrameFor[MAX_SLOTS];	///< the router's F for a seat it answered, 0 for none
	Bool m_rejoinLogFor[MAX_SLOTS];			///< the seat asked us for its log
	Int m_rejoinLogTo;									///< the seat our log goes to, -1 for none
	UnsignedInt m_rejoinLogAtFrame;			///< once we stand on this frame
	UnsignedInt m_packetFromAddr;				///< the address of the packet doRelay is reading, 0 outside it
	UnsignedInt m_absentSince[MAX_SLOTS];	///< the frame the seat left the lockstep, 0 while it plays
	Bool m_forfeitSent[MAX_SLOTS];			///< the router sent its forfeit
	Int m_awayNoticeSeconds;						///< the countdown the last away notice showed, -1 for none
	Bool m_lostShown[MAX_SLOTS];				///< the seat left the lockstep as "lost connection" and has not been shown leaving for good

	/* Where the keep-alive round has got to.  These were function statics in doKeepAlive, so they
		 survived across games and across ConnectionManager instances. */
	time_t m_keepAliveRoundStart;
	Int m_keepAliveNextSlot;
	Int  m_minFpsPlayer;
	Int  m_minFps;
	UnsignedInt m_smallestPacketArrivalCushion;
	UnsignedInt m_cushionMeasured[CUSHION_RING_FRAMES];		///< Per frame in flight, the frame number whose arrival cushion was measured (arrivalMeasuresCushion).
	Bool m_didSelfSlug;

	// -----------------------------------------------------------------------------
	FileCommandMap s_fileCommandMap;
	FileMaskMap s_fileRecipientMaskMap;
	FileProgressMap s_fileProgressMap[MAX_SLOTS];
	// -----------------------------------------------------------------------------
};

#endif
