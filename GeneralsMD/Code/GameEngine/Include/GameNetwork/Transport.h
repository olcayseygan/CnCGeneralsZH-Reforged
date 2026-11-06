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

// Transport.h ///////////////////////////////////////////////////////////////
// Transport layer - a thin layer around a UDP socket, with queues.
// Author: Matthew D. Campbell, July 2001

#pragma once

#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include "GameNetwork/udp.h"
#include "GameNetwork/NetworkDefs.h"

/**
 * The transport layer handles the UDP socket for the game, and will packetize and
 * de-packetize multiple ACK/CommandPacket/etc packets into larger aggregates.
 */
/**
 * Where a Transport sends and reads when it has no UDP socket of its own. Internet play sets this for as
 * long as a player is signed in, and packets go over a connection the lobby service brokered. Null everywhere
 * else: LAN, skirmish and -netgame never touch it.
 */
class TransportRoute
{
public:
	virtual ~TransportRoute() {}
	virtual Int send(const unsigned char *buf, UnsignedInt len, UnsignedInt ip, UnsignedShort port) = 0;
	virtual Int recv(unsigned char *buf, UnsignedInt len, sockaddr_in *from) = 0;
};
extern TransportRoute *TheTransportRoute;

// we only ever allocate one of there, and it is quite large, so we really DON'T want
// it to be a MemoryPoolObject (srj)
class Transport //: public MemoryPoolObject
{
	//MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(Transport, "Transport")		
public:

	Transport();
	~Transport();

	Bool init( AsciiString ip, UnsignedShort port );
	Bool init( UnsignedInt ip, UnsignedShort port );
	void reset( void );
	Bool update( void );									///< Call this once a GameEngine tick, regardless of whether the frame advances.

	Bool doRecv( void );		///< call this to service the receive packets
	Bool doSend( void );		///< call this to service the send queue.

	Bool queueSend(UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len /*,
		NetMessageFlags flags, Int id */);				///< Queue a packet for sending to the specified address and port.  This will be sent on the next update() call.

	inline Bool allowBroadcasts(Bool val) { if (!m_udpsock) return false; return (m_udpsock->AllowBroadcasts(val))?true:false; }

	/// The address the system bound when the last init() was refused because it was not the one asked
	/// for (a VPN split tunnel or a firewall redirecting the socket), 0 otherwise
	UnsignedInt getRedirectedIP( void ) const { return m_redirectedIP; }

	/// -relay: the virtual address the relay gave this machine, 0 before the first init() reached it
	static UnsignedInt getRelayIP( void );

	/// -online: HELLO_AUTH with the login's ticket against OnlineGetRelayEndpoint (blocks up to five seconds)
	static Bool openOnlineRelay( void );
	/// -online: pump the relay socket (keepalive) while no Transport is attached; once a frame
	static void keepOnlineRelay( void );
	/// -online: drop the session unless a Transport still uses it
	static void closeOnlineRelay( void );

#if !defined(_WIN32)
	/* Port defect 29 (udp.h): shareAddress(TRUE) before init lets this socket share its port with another
		 copy's broadcast listener; initBroadcastListener binds the wildcard address to take only the
		 broadcasts sent to that port.  Both persist across reset(). */
	void shareAddress( Bool val ) { m_shareAddress = val; }
	Bool initBroadcastListener( UnsignedShort port );
	Bool isOpen( void ) const { return m_udpsock != NULL; }
	/// Moves every received message into free slots of `inbox`'s m_inBuffer, each once; what does not
	/// fit stays here for the next call
	void moveReceivedInto( Transport &inbox );
#endif

	// Latency insertion and packet loss
	void setLatency( Bool val ) { m_useLatency = val; }
	void setPacketLoss( Bool val ) { m_usePacketLoss = val; }

	// Bandwidth metrics
	Real getIncomingBytesPerSecond( void );
	Real getIncomingPacketsPerSecond( void );
	Real getOutgoingBytesPerSecond( void );
	Real getOutgoingPacketsPerSecond( void );
	Real getUnknownBytesPerSecond( void );
	Real getUnknownPacketsPerSecond( void );

	TransportMessage m_outBuffer[MAX_MESSAGES];
	TransportMessage m_inBuffer[MAX_MESSAGES];

	DelayedTransportMessage m_delayedInBuffer[MAX_MESSAGES];		// only used when latency simulation is on

	UnsignedShort m_port;
private:
	Bool m_winsockInit;
	UDP *m_udpsock;
	UnsignedInt m_redirectedIP;
	Bool m_relayed;		///< -relay: no socket of its own; every datagram goes through the one relay socket (Transport.cpp)
#if !defined(_WIN32)
	Bool m_shareAddress;
	Bool m_broadcastsOnly;
#endif

	// Latency insertion and packet loss
	Bool m_useLatency;
	Bool m_usePacketLoss;

	// Bandwidth metrics
	UnsignedInt m_incomingBytes[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_unknownBytes[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_outgoingBytes[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_incomingPackets[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_unknownPackets[MAX_TRANSPORT_STATISTICS_SECONDS];
	UnsignedInt m_outgoingPackets[MAX_TRANSPORT_STATISTICS_SECONDS];
	Int m_statisticsSlot;
	UnsignedInt m_lastSecond;

	Bool isGeneralsPacket( TransportMessage *msg );
	void resetQueues( UnsignedShort port );
	void receivePacket( TransportMessage &msg, Int len, UnsignedInt addr, UnsignedShort port, UnsignedInt now );
	Bool initRelayed( UnsignedInt ip, UnsignedShort port );
	static Bool connectRelay( const AsciiString &host, UnsignedShort relayPort );
	void detachRelay( void );
	Int writeRelayed( TransportMessage &msg );
	static void pumpRelay( void );
};

#endif // _TRANSPORT_H_
