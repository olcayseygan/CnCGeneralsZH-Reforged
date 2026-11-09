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

#include "GameNetwork/Connection.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameLogic/GameLogic.h"

// wait this many milliseconds at most to retry things before quitting. EA's 30 seconds held the
// victory screen that long whenever the last opponent had already left: nobody acks the commands
// still queued for a player who is gone, so the queue only ever empties on this timeout. Five
// seconds is still some twenty round trips at 250 ms for our own leave to get through.
enum { MaxQuitFlushTime = 5000 };

/**
 * Jacobson/Karels.  See the comment on CONNECTION_MIN_RETRY_TIME in Connection.h for why the flat
 * 2000ms it replaces had to go, and why EA's own commented-out mean-times-1.5 could not be it.
 */
void Connection_updateRetryTimeout( Real sampleMS, Real &srtt, Real &rttvar, time_t &retryMS )
{
	if (sampleMS < 0.0f) {
		sampleMS = 0.0f;
	}

	if (srtt < 0.0f) {
		// First sample: the mean is the sample and the deviation is half of it, which is the
		// conventional seed and keeps the first timeout from being absurdly tight.
		srtt = sampleMS;
		rttvar = sampleMS / 2.0f;
	} else {
		Real deviation = srtt - sampleMS;
		if (deviation < 0.0f) {
			deviation = -deviation;
		}
		rttvar = (0.75f * rttvar) + (0.25f * deviation);
		srtt = (0.875f * srtt) + (0.125f * sampleMS);
	}

	Real timeout = srtt + (4.0f * rttvar);
	if (timeout < (Real)CONNECTION_MIN_RETRY_TIME) {
		timeout = (Real)CONNECTION_MIN_RETRY_TIME;
	}
	if (timeout > (Real)CONNECTION_MAX_RETRY_TIME) {
		timeout = (Real)CONNECTION_MAX_RETRY_TIME;
	}
	retryMS = (time_t)timeout;
}

/**
 * Exponential backoff on top of the connection's timeout, capped at the ceiling.  numTimesSent is
 * how many times the command has already gone out, so 1 (a single transmission awaiting its first
 * retry) is the un-backed-off case.
 *
 * The number of doublings is deliberately small.  Backoff exists so that a link which is genuinely
 * down is not hammered at the floor rate for as long as the disconnect timer runs, and two
 * doublings already cut that rate to a quarter.  Paying for more than that is not free: this is a
 * lockstep game, so the command being retried is one the whole room is stopped on, and the delay
 * before the next attempt *is* the length of the freeze everybody sees.  Four doublings put that at
 * 1.2 and then 2 seconds after only three and four losses of the same command - which a couple of
 * percent of packet loss produces routinely over a long game with eight players - and there is
 * nothing else in flight to congest with anyway, because a blocked machine has stopped generating
 * frames.  Deciding that a link is actually dead is the disconnect manager's job, on its own ping
 * and its own timers, not the retry timer's.
 */
time_t Connection_retryDelayFor( time_t baseRetryMS, Int numTimesSent )
{
	Int backoff = numTimesSent - 1;
	if (backoff < 0) {
		backoff = 0;
	}
	if (backoff > CONNECTION_MAX_RETRY_BACKOFF_SHIFTS) {
		backoff = CONNECTION_MAX_RETRY_BACKOFF_SHIFTS;
	}

	time_t delay = baseRetryMS << backoff;
	if (delay > CONNECTION_MAX_RETRY_TIME) {
		delay = CONNECTION_MAX_RETRY_TIME;
	}
	return delay;
}

Bool Connection_isRedundantCopyDue( time_t curTime, time_t timeLastOnWire, Int copiesSent )
{
	if (timeLastOnWire == -1) {
		return FALSE;
	}
	if (copiesSent >= CONNECTION_REDUNDANT_COPIES) {
		return FALSE;
	}
	return (curTime - timeLastOnWire) >= CONNECTION_REDUNDANT_SPACING_MS;
}

/**
 * The constructor.
 */
Connection::Connection() {
	m_transport = NULL;
	m_user = NULL;
	m_netCommandList = NULL;
	// Start at the ceiling - EA's old flat constant - and let the first acks pull it down to what
	// the link actually is.  Starting low would mean retransmitting before the very first ack of
	// the game had a chance to arrive.
	m_retryTime = CONNECTION_MAX_RETRY_TIME;
	m_smoothedLatency = -1.0f;
	m_latencyVariance = 0.0f;
	m_lastTimeSent = 0;
	m_frameGrouping = 1;
	m_isQuitting = false;
	m_quitTime = 0;
	// Added By Sadullah Nader
	// clearing out the latency tracker
	m_averageLatency = 0.0f;
	Int i;
	for(i = 0; i < CONNECTION_LATENCY_HISTORY_LENGTH; i++)
	{
		m_latencies[i] = 0.0f;
	}
	// End Add
}

/**
 * The destructor.
 */
Connection::~Connection() {
	if (m_user != NULL) {
		m_user->deleteInstance();
		m_user = NULL;
	}

	if (m_netCommandList != NULL) {
		m_netCommandList->deleteInstance();
		m_netCommandList = NULL;
	}
}

/**
 * Initialize the connection and any subsystems.
 */
void Connection::init() {
	m_transport = NULL;

	if (m_user != NULL) {
		m_user->deleteInstance();
		m_user = NULL;
	}

	if (m_netCommandList == NULL) {
		m_netCommandList = newInstance(NetCommandList);
		m_netCommandList->init();
	}
	m_netCommandList->reset();

	m_lastTimeSent = 0;
	m_frameGrouping = 1;
	m_numRetries = 0;
	m_retryMetricsTime = 0;

	// A reset connection knows nothing about the link again: back to the ceiling, no samples.
	m_retryTime = CONNECTION_MAX_RETRY_TIME;
	m_smoothedLatency = -1.0f;
	m_latencyVariance = 0.0f;

	for (Int i = 0; i < CONNECTION_LATENCY_HISTORY_LENGTH; ++i) {
		m_latencies[i] = 0;
	}
	m_averageLatency = 0;
	m_isQuitting = FALSE;
	m_quitTime = 0;
}

/**
 * Take the connection back to the initial state.
 */
void Connection::reset() {
	init();
}

/**
 * Doesn't really do anything.
 */
void Connection::update() {
}

/**
 * Attach the transport object that this connection should use.
 */
void Connection::attachTransport(Transport *transport) {
	m_transport = transport;
}

/**
 * Assign this connection a user.  This is the user to whome we send all our packetized goodies.
 */
void Connection::setUser(User *user) {
	if (m_user != NULL) {
		m_user->deleteInstance();
	}

	m_user = user;
}

/**
 * Return the user object.
 */
User * Connection::getUser() {
	return m_user;
}

/**
 * Add this network command to the send queue for this connection.
 * The relay is the mask specifying the people the person we are sending to should send to.
 * The relay mostly has to do with the packet router.
 */
void Connection::sendNetCommandMsg(NetCommandMsg *msg, UnsignedByte relay) {
	static NetPacket *packet = NULL;

	// this is done so we don't have to allocate and delete a packet every time we send a message.
	if (packet == NULL) {
		packet = newInstance(NetPacket);
	}


	if (m_isQuitting)
		return;

	if (m_netCommandList != NULL) {
		// check to see if this command will fit in a packet.  If not, we need to split it up.
		// we are splitting up the command here so that the retry logic will not try to 
		// resend the ENTIRE command (i.e. multiple packets work of data) and only do the retry
		// one wrapper command at a time.
		packet->reset();

		NetCommandRef *tempref = NEW_NETCOMMANDREF(msg);

		Bool msgFits = packet->addCommand(tempref);
		tempref->deleteInstance(); // delete the temporary reference.
		tempref = NULL;

		if (!msgFits) {
			NetCommandRef *origref = NEW_NETCOMMANDREF(msg);
			origref->setRelay(relay);
			// the message doesn't fit in a single packet, need to split it up.
			NetPacketList packetList = NetPacket::ConstructBigCommandPacketList(origref);
			NetPacketListIter tempPacketPtr = packetList.begin();

			while (tempPacketPtr != packetList.end()) {
				NetPacket *tempPacket = (*tempPacketPtr);

				NetCommandList *list = tempPacket->getCommandList();
				NetCommandRef *ref1 = list->getFirstMessage();
				while (ref1 != NULL) {
					// NULL when the queue already holds a command with this author and id: a relayed
					// command is wrapped under our own id counter (NetPacket.cpp), which can match one.
					NetCommandRef *ref2 = m_netCommandList->addMessage(ref1->getCommand());
					if (ref2 != NULL) {
						ref2->setRelay(relay);
					}

					ref1 = ref1->getNext();
				}

				tempPacket->deleteInstance();
				tempPacket = NULL;
				++tempPacketPtr;

				list->deleteInstance();
				list = NULL;
			}

			origref->deleteInstance();
			origref = NULL;

			return;
		}

		// the message fits in a packet, add to the command list normally.
		NetCommandRef *ref = m_netCommandList->addMessage(msg);

		if (ref != NULL) {

/*
#if ((defined(_DEBUG)) || (defined(_INTERNAL)))
			if (msg->getNetCommandType() == NETCOMMANDTYPE_GAMECOMMAND) {
				DEBUG_LOG(("Connection::sendNetCommandMsg - added game command %d to net command list for frame %d.\n",
					msg->getID(), msg->getExecutionFrame()));
			} else if (msg->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) {
				DEBUG_LOG(("Connection::sendNetCommandMsg - added frame info for frame %d\n", msg->getExecutionFrame()));
			}
#endif // _DEBUG || _INTERNAL
*/

			ref->setRelay(relay);
		}
	}
}

void Connection::clearCommandsExceptFrom( Int playerIndex )
{
	NetCommandRef *tmp = m_netCommandList->getFirstMessage();
	while (tmp)
	{
		// removeMessage nulls the node's own next pointer (NetCommandList::removeMessage), so the
		// successor has to be taken before the node leaves the list.  Read after, and this loop
		// stopped dead on the first command it cleared and left every later one in the queue.
		NetCommandRef *next = tmp->getNext();
		NetCommandMsg *msg = tmp->getCommand();
		if (msg->getPlayerID() != playerIndex)
		{
			DEBUG_LOG(("Connection::clearCommandsExceptFrom(%d) - clearing a command from %d for frame %d\n",
				playerIndex, tmp->getCommand()->getPlayerID(), tmp->getCommand()->getExecutionFrame()));
			m_netCommandList->removeMessage(tmp);
			tmp->deleteInstance();
		}
		tmp = next;
	}
}

Bool Connection::isQueueEmpty() {
	if (m_netCommandList->getFirstMessage() == NULL) {
		return TRUE;
	}
	return FALSE;
}

void Connection::setQuitting( void )
{
	m_isQuitting = TRUE;
	m_quitTime = Clock_Milliseconds();
	DEBUG_LOG(("Connection::setQuitting() at time %d\n", m_quitTime));
}

/**
 * This is the good part. We take all the network commands queued up for this connection,
 * packetize them and put them on the transport's send queue for actual sending.
 */
UnsignedInt Connection::doSend() {
	Int numpackets = 0;
	time_t curtime = Clock_Milliseconds();
	Bool couldQueue = TRUE;

	// Do this check first, since it's an important fail-safe
	if (m_isQuitting && curtime > m_quitTime + MaxQuitFlushTime)
	{
		DEBUG_LOG(("Timed out a quitting connection.  Deleting all %d messages\n", m_netCommandList->length()));
		m_netCommandList->reset();
		return 0;
	}

	if ((curtime - m_lastTimeSent) < m_frameGrouping) {
//		DEBUG_LOG(("not sending packet, time = %d, m_lastFrameSent = %d, m_frameGrouping = %d\n", curtime, m_lastTimeSent, m_frameGrouping));
		return 0;
	}
	
	// iterate through all the messages and put them into a packet(s).
	NetCommandRef *msg = m_netCommandList->getFirstMessage();

	while ((msg != NULL) && couldQueue) {
		NetPacket *packet = newInstance(NetPacket);
		packet->init();
		packet->setAddress(m_user->GetIPAddr(), m_user->GetPort());

		Bool notDone = TRUE;

		// add the command messages until either we run out of messages or the packet is full.
		while ((msg != NULL) && notDone) {
			NetCommandRef *next = msg->getNext(); // Need this since msg could be deleted

			time_t timeLastSent = msg->getTimeLastSent();

			time_t retryDelay = Connection_retryDelayFor(m_retryTime, msg->getNumTimesSent());

			if (((curtime - timeLastSent) > retryDelay) || (timeLastSent == -1)) {
				notDone = packet->addCommand(msg);
				if (notDone) {
					// the msg command was added to the packet.
					if (CommandRequiresAck(msg->getCommand())) {
						if (timeLastSent != -1) {
							++m_numRetries;
						}
						doRetryMetrics();
						msg->markSent(curtime);
					} else {
						m_netCommandList->removeMessage(msg);
						msg->deleteInstance();
					}
				}
			}
			msg = next;
		}

		if (msg != NULL) {
			DEBUG_LOG(("didn't finish sending all commands in connection\n"));
		} else {
			addRedundantCopies(packet, curtime);
		}

		++numpackets;

		/// @todo Make the act of giving the transport object a packet to send more efficient.  Make the transport take a NetPacket object rather than the raw data, thus avoiding an extra memcpy.
		if (packet->getNumCommands() > 0) {
			// If the packet actually has any information to give, give it to the transport object
			// for transmission.
			couldQueue = m_transport->queueSend(packet->getAddr(), packet->getPort(), packet->getData(), packet->getLength());
			m_lastTimeSent = curtime;
		}
		if (packet != NULL) {
			packet->deleteInstance(); // delete the packet now that we're done with it.
		}
	}

	return numpackets;
}

/**
 * Fill what is left of the last packet of a send with copies of the synchronized commands still
 * waiting for their ack.  They only ever ride behind everything that was due, so a copy never
 * pushes a first send into the next packet.  See CONNECTION_REDUNDANT_COPIES.
 */
void Connection::addRedundantCopies(NetPacket *packet, time_t curTime) {
	for (NetCommandRef *ref = m_netCommandList->getFirstMessage(); ref != NULL; ref = ref->getNext()) {
		if (!IsCommandSynchronized(ref->getCommand()->getNetCommandType())) {
			continue;
		}
		if (!Connection_isRedundantCopyDue(curTime, ref->getTimeLastOnWire(), ref->getNumCopiesSent())) {
			continue;
		}
		if (!packet->addCommand(ref)) {
			return;
		}
		ref->markCopied(curTime);
	}
}

NetCommandRef * Connection::processAck(NetAckStage1CommandMsg *msg) {
	return processAck(msg->getCommandID(), msg->getOriginalPlayerID());
}

NetCommandRef * Connection::processAck(NetAckBothCommandMsg *msg) {
	return processAck(msg->getCommandID(), msg->getOriginalPlayerID());
}

NetCommandRef * Connection::processAck(NetCommandMsg *msg) {
	if (msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE1) {
		NetAckStage1CommandMsg *ackmsg = (NetAckStage1CommandMsg *)msg;
		return processAck(ackmsg);
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH) {
		NetAckBothCommandMsg *ackmsg = (NetAckBothCommandMsg *)msg;
		return processAck(ackmsg);
	}

	return NULL;
}

/**
 * The person we are sending to has ack'd one of the messages we sent him.
 * Take that message off the list of commands to send.
 */
NetCommandRef * Connection::processAck(UnsignedShort commandID, UnsignedByte originalPlayerID) {
	NetCommandRef *temp = m_netCommandList->getFirstMessage();
	while ((temp != NULL) && ((temp->getCommand()->getID() != commandID) || (temp->getCommand()->getPlayerID() != originalPlayerID))) {

		// cycle through the commands till we find the one we need to remove.
		// Need to check for both the command ID and the player ID.
		temp = temp->getNext();
	}
	if (temp == NULL) {
		return NULL;
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	Bool doDebug = FALSE;
	if (temp->getCommand()->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) {
		doDebug = TRUE;
	}
#endif

	// Karn's rule: an ack for a command that went out more than once cannot be timed, because
	// there is no way to tell which of the sends it is acking.  Timing it against the last send
	// reads far too short and would drag the timeout down exactly when the link is losing packets.
	// Nor can a load-complete ack: it comes back when the other copy has finished loading and pumps
	// its network again, seconds later, so it times the other machine's load and not the link.  Two
	// seats that are not the packet router send each other nothing else before the match, so that
	// one sample was the whole estimate and pinned their link at the 2000ms ceiling.
	//
	// And a command this queue never put on the wire has nothing to time at all.  The likely way in:
	// a frame resend answer queues commands the other machine already acked once, and a late ack for
	// one of the earlier copies (each redundant copy is acked too) lands before the new one goes out.
	// Its send time is still -1, so the sample was the clock itself: 2528656.8 ms of average latency in a relay
	// match, an srtt of seconds that decayed over a hundred acks, and a 0.8 s room latency (the
	// plausible ceiling) that pushed the run-ahead to 30 frames.  The entry is still removed below:
	// the other machine has the command.
	if (temp->getNumTimesSent() >= 1) {
		Int index = temp->getCommand()->getID() % CONNECTION_LATENCY_HISTORY_LENGTH;
		m_averageLatency -= ((Real)(m_latencies[index])) / CONNECTION_LATENCY_HISTORY_LENGTH;
		Real lat = (Real)(Clock_Milliseconds() - temp->getTimeLastSent());
		m_averageLatency += lat / CONNECTION_LATENCY_HISTORY_LENGTH;
		m_latencies[index] = lat;

		if ((temp->getNumTimesSent() == 1) && (temp->getCommand()->getNetCommandType() != NETCOMMANDTYPE_LOADCOMPLETE)) {
			Connection_updateRetryTimeout(lat, m_smoothedLatency, m_latencyVariance, m_retryTime);
		}
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	if (doDebug == TRUE) {
		DEBUG_LOG(("Connection::processAck - disconnect frame command %d found, removing from command list.\n", commandID));
	}
#endif
	m_netCommandList->removeMessage(temp);
	return temp;
}

void Connection::setFrameGrouping(time_t frameGrouping) {
	m_frameGrouping = frameGrouping;
	// (EA had "m_retryTime = frameGrouping * 4" here.  The timeout is measured from acks now -
	// see Connection_updateRetryTimeout - so the send cadence no longer dictates it.)
}

void Connection::doRetryMetrics() {
	static Int numSeconds = 0;
	time_t curTime = Clock_Milliseconds();

	if ((curTime - m_retryMetricsTime) > 10000) {
		m_retryMetricsTime = curTime;
		++numSeconds;
		DEBUG_LOG(("Connection::doRetryMetrics - retries in the last 10s = %d, average latency = %.1fms, srtt = %.1fms, rttvar = %.1fms, retry timeout = %dms\n",
			m_numRetries, m_averageLatency, m_smoothedLatency, m_latencyVariance, (Int)m_retryTime));
		m_numRetries = 0;
	}
}

#if defined(_DEBUG) || (_INTERNAL)
void Connection::debugPrintCommands() {
	NetCommandRef *ref = m_netCommandList->getFirstMessage();
	while (ref != NULL) {
		DEBUG_LOG(("Connection::debugPrintCommands - ID: %d\tType: %s\tRelay: 0x%X for frame %d\n",
			ref->getCommand()->getID(), GetAsciiNetCommandType(ref->getCommand()->getNetCommandType()).str(),
			ref->getRelay(), ref->getCommand()->getExecutionFrame()));
		ref = ref->getNext();
	}
}
#endif
