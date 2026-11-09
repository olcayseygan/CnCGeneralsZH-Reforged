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

/**
 * The Connection class handles queues for individual players, one connection per player.
 * Connections are identified by their names (m_name).  This should accomodate changing IPs
 * in the face of modem disconnects, NAT irregularities, etc.
 * Messages can be guaranteed or non-guaranteed, sequenced or not.
 *
 *
 * So how this works is this:
 * The connection contains the incoming and outgoing commands to and from the player this
 * connection object represents.  The incoming packets are separated into the different frames
 * of execution.  This is done for efficiency reasons since we will be keeping track of commands
 * that are to be executed on many different frames, and they will need to be retrieved by the
 * connection manager on a per-frame basis.  We don't really care what frame the outgoing
 * commands are executed on as they all need to be sent out right away to ensure that the commands
 * get to their destination in time.
 */

#pragma once

#ifndef __CONNECTION_H
#define __CONNECTION_H

#include "GameNetwork/NetCommandList.h"
#include "GameNetwork/User.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/NetPacket.h"

#define CONNECTION_LATENCY_HISTORY_LENGTH 200

/**
 * Retransmit timeout bounds, in milliseconds.
 *
 * EA shipped a flat 2000ms retry (Connection::Connection), so on a 40ms link one lost command
 * packet cost every player in the game a two-second stall before it was even tried again.  Their
 * own intent is commented out in doRetryMetrics(): "m_retryTime = m_averageLatency * 1.5".  It
 * could not be switched on as written - m_averageLatency is a 200-slot rolling mean over an array
 * that starts full of zeroes, so early in a game it reads near zero and a 1.5x multiple of that is
 * a packet storm.  What is used instead is the standard Jacobson/Karels estimator, which carries
 * its own variance term and therefore widens on a jittery link instead of hugging the mean.
 *
 * The floor keeps a LAN game from retransmitting faster than the ack can physically come back.
 * The ceiling is EA's old constant, so no link is ever served worse than the shipped behaviour.
 */
#define CONNECTION_MIN_RETRY_TIME 150
#define CONNECTION_MAX_RETRY_TIME 2000

// How many times the per-command retry wait may double while a command goes unacked.  See
// Connection_retryDelayFor - in a lockstep game this delay is the length of the freeze.
#define CONNECTION_MAX_RETRY_BACKOFF_SHIFTS 2

/**
 * Fold one round-trip sample into the smoothed estimate and rewrite the timeout.  Jacobson/Karels:
 * srtt tracks the mean, rttvar the mean deviation, and the timeout sits four deviations out so
 * ordinary jitter does not trigger a retransmit.  srtt < 0 means "no samples yet" and seeds both.
 * Free function (not a member, not static) so test_gameengine can link straight to it.
 */
void Connection_updateRetryTimeout( Real sampleMS, Real &srtt, Real &rttvar, time_t &retryMS );

/**
 * How long to wait before sending this particular command again, given the connection's current
 * timeout and how many times the command has already gone out.  Repeated failures back off
 * exponentially up to the ceiling, so a link that is genuinely down is not hammered.
 * Free function (not a member, not static) so test_gameengine can link straight to it.
 */
time_t Connection_retryDelayFor( time_t baseRetryMS, Int numTimesSent );

/**
 * Redundant sends.  A synchronized command (orders, frame info, run-ahead) that is still unacked
 * goes out again in the next packets this many times, each copy at least the spacing after the
 * last time it was on the wire, before any retry timer runs out.  On a lossy link the retry is
 * what the room waits for: the timeout is a full round trip through the packet router and more,
 * 600 to 800ms at 120ms and 5% loss, and the run-ahead covers only the one-way trip, so every lost
 * packet was a stall.  A copy 30ms behind the original costs a few bytes and gets there before the
 * run-ahead runs out.  Only synchronized commands are copied because their receiving end drops a
 * duplicate by itself (FrameData::addCommand, and the old-frame test in processNetCommand); chat,
 * disconnect and file traffic keep the plain retry.  A copy does not count as a send for Karn's
 * rule, so the first ack still times the link.
 */
#define CONNECTION_REDUNDANT_COPIES 2
#define CONNECTION_REDUNDANT_SPACING_MS 30

/**
 * Whether a command last put on the wire at timeLastOnWire, already copied copiesSent times, is
 * due another redundant copy at curTime.  -1 is a command that has never gone out.
 * Free function (not a member, not static) so test_gameengine can link straight to it.
 */
Bool Connection_isRedundantCopyDue( time_t curTime, time_t timeLastOnWire, Int copiesSent );

class Connection : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(Connection, "Connection")		
public:
	Connection();
	//~Connection();

	void update();
	void init();
	void reset();

	UnsignedInt doSend();
	void doRecv();

	Bool allCommandsReady(UnsignedInt frame);
	Bool isQueueEmpty();
	void attachTransport(Transport *transport);
	void setUser(User *user);
	User *getUser();
	void setFrameGrouping(time_t frameGrouping);

	void sendNetCommandMsg(NetCommandMsg *msg, SlotMask relay);

	// These two processAck calls do the same thing, just take different types of ACK commands.
	NetCommandRef * processAck(NetAckBothCommandMsg *msg);
	NetCommandRef * processAck(NetAckStage1CommandMsg *msg);
	NetCommandRef * processAck(NetCommandMsg *msg);
	NetCommandRef * processAck(UnsignedShort commandID, UnsignedByte originalPlayerID);

	void clearCommandsExceptFrom( Int playerIndex );

	void setQuitting( void );
	Bool isQuitting( void ) { return m_isQuitting; }

	/** This connection's measured retry timeout, in milliseconds.  How long a packet on this link is
		  given before it is worth sending again - see FrameResendPolicy.h. */
	UnsignedInt getRetryTimeMS( void ) const { return (UnsignedInt)m_retryTime; }
	/** FALSE while the retry timeout is still the unmeasured ceiling. */
	Bool hasLatencySample( void ) const { return m_smoothedLatency >= 0.0f; }

#if defined(_DEBUG) || defined(_INTERNAL)
	void debugPrintCommands();
#endif

protected:
	void doRetryMetrics();
	void addRedundantCopies(NetPacket *packet, time_t curTime);

	Bool m_isQuitting;
	UnsignedInt m_quitTime;

	Transport *m_transport;
	User *m_user;

	NetCommandList *m_netCommandList;
	time_t m_retryTime;						///< The time between sending retry packets for this connection.  Time is in milliseconds.  Derived from the two estimators below, never a constant.
	Real m_smoothedLatency;			///< Jacobson/Karels srtt, in milliseconds.  Negative until the first untainted round-trip sample arrives.
	Real m_latencyVariance;			///< Jacobson/Karels rttvar (mean deviation), in milliseconds.
	Real m_averageLatency;			///< The average time between sending a command and receiving an ACK.  Reporting only - the timeout is driven by m_smoothedLatency.
	Real m_latencies[CONNECTION_LATENCY_HISTORY_LENGTH];	///< List of the last 100 latencies.

	time_t m_frameGrouping;				///< The minimum time between packet sends.
	time_t m_lastTimeSent;				///< The time of the last packet send.
	Int m_numRetries;							///< The number of retries for the last second.
	time_t m_retryMetricsTime;		///< The start time of the current retry metrics thing.
};

#endif
