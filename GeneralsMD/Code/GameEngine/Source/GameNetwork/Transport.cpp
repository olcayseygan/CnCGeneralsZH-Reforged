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

#include "Common/crc.h"
#include "GameNetwork/LinkSimulation.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/NetworkInterface.h"
#include "Common/UserPreferences.h"
#include "GameNetwork/Online/OnlineQueues.h"

#include <chrono>
#include <thread>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//--------------------------------------------------------------------------
// Packet-level encryption is an XOR operation, for speed reasons.  To get
// the max throughput, we only XOR whole 4-byte words, so the last bytes
// can be non-XOR'd.

// This assumes the buf is a multiple of 4 bytes.  Extra is not encrypted.
static inline void encryptBuf( unsigned char *buf, Int len )
{
	UnsignedInt mask = 0x0000Fade;

	UnsignedInt *uintPtr = (UnsignedInt *) (buf);

	for (int i=0 ; i<len/4 ; i++) {
		*uintPtr = (*uintPtr) ^ mask;
		*uintPtr = htonl(*uintPtr);
		uintPtr++;
		mask += 0x00000321; // just for fun
	}
}

// This assumes the buf is a multiple of 4 bytes.  Extra is not encrypted.
static inline void decryptBuf( unsigned char *buf, Int len )
{
	UnsignedInt mask = 0x0000Fade;

	UnsignedInt *uintPtr = (UnsignedInt *) (buf);

	for (int i=0 ; i<len/4 ; i++) {
		*uintPtr = htonl(*uintPtr);
		*uintPtr = (*uintPtr) ^ mask;
		uintPtr++;
		mask += 0x00000321; // just for fun
	}
}

//--------------------------------------------------------------------------
/* -relay host:port.  Two homes behind NAT cannot reach each other, so in relay mode no Transport owns a
	 socket: the process has one, bound to an ephemeral port, and everything goes through the relay in
	 a small envelope (big-endian, every packet opens 'Z','R',type):
		 HELLO   1  us -> relay: exeCRC u32, iniCRC u32, session 8 bytes (0 the first time), wantVip u32 (0 = any),
								name UTF-8 <= 32 bytes
		 WELCOME 2  relay -> us: session 8 bytes, vip u32 - our virtual 10.x.y.z address
		 DATA    3  us -> relay: session 8 bytes, dstVip u32, dstPort u16, srcPort u16, payload
		 DATA    3  relay -> us: srcVip u32, srcPort u16, dstPort u16, payload
	 The relay puts a machine in the room of its (exeCRC, iniCRC) and forgets a session idle for 120 s,
	 and a HELLO with our session goes out every 10 s whenever a Transport is pumped (the multiplayer load
	 screen pumps it too).  The relay forwards nothing to us until a packet carrying the session it sent
	 comes back, so a second HELLO answers the first WELCOME at once.  Every WELCOME carries the session
	 to use from then on: the relay hands out a new one when NAT moves our port.  wantVip is the address
	 the first init() was handed when it is in 10.0.0.0/8, so a -netgame slot list of 10.x addresses gets
	 each copy the slot it names, and after the first WELCOME it is the vip we have, so a relay that
	 restarted or forgot us gives it back.  The lobby asks for nothing at first, because LANAPI::init runs
	 with no address before SetLocalIP picks one.  The ports are the logical ones (the lobby's 8086,
	 the game's 8088): a received DATA is handed to whichever Transport holds dstPort, with the sender's
	 vip and port as its address, so LANAPI and ConnectionManager see a LAN where every machine is a
	 vip and a broadcast is dstVip 255.255.255.255.  Nothing here touches the game logic.

	 -online uses the same socket with HELLO_AUTH (type 4) in place of HELLO: the header above, then the
	 16-byte ticket the service handed out at login, then the name (left empty: the service knows the
	 account).  openOnlineRelay() makes the handshake right after login, against OnlineGetRelayEndpoint,
	 so the vip exists before any Transport does; keepOnlineRelay() sends the keepalive while the lobby
	 has no Transport to pump. */
namespace
{
	enum { RELAY_HELLO = 1, RELAY_WELCOME = 2, RELAY_DATA = 3, RELAY_HELLO_AUTH = 4 };
	const Int RELAY_MAX_PACKET = 1400;
	const Int RELAY_SESSION_BYTES = 8;
	const Int RELAY_TICKET_BYTES = 16;
	const Int RELAY_HELLO_HEADER = 23;					///< 'ZR', type, exeCRC, iniCRC, session, wantVip
	const Int RELAY_WELCOME_SIZE = 15;					///< 'ZR', type, session, vip
	const Int RELAY_DATA_OUT_HEADER = 19;				///< 'ZR', type, session, dstVip, dstPort, srcPort
	const Int RELAY_DATA_IN_HEADER = 11;				///< 'ZR', type, srcVip, srcPort, dstPort
	const Int RELAY_NAME_BYTES = 32;
	const UnsignedInt RELAY_KEEPALIVE_MS = 10000;
	const Int RELAY_HELLO_TRIES = 10;					///< one HELLO every 500 ms: five seconds, then a socket error
	const UnsignedInt RELAY_RETRY_AFTER_MS = 10000;	///< a failed handshake is not tried again for this long
	const Int RELAY_MAX_TRANSPORTS = 4;

	struct RelayLink
	{
		UDP *sock;
		UnsignedInt ip;
		UnsignedShort port;
		unsigned char session[RELAY_SESSION_BYTES];	///< opaque, as the relay sent it; zero before the first WELCOME
		UnsignedInt wantVip;
		UnsignedInt vip;
		UnsignedInt lastHello;
		UnsignedInt failedAt;
		Bool wsaStarted;
		Bool authed;																///< -online: every hello is a HELLO_AUTH carrying ticket
		unsigned char ticket[RELAY_TICKET_BYTES];
		Int nameLength;
		unsigned char name[RELAY_NAME_BYTES];
		Transport *users[RELAY_MAX_TRANSPORTS];
	};
	RelayLink s_relay;	// zero

	void put32( unsigned char *p, UnsignedInt v ) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
	void put16( unsigned char *p, UnsignedShort v ) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
	UnsignedInt get32( const unsigned char *p ) { return ((UnsignedInt)p[0] << 24) | ((UnsignedInt)p[1] << 16) | ((UnsignedInt)p[2] << 8) | p[3]; }
	UnsignedShort get16( const unsigned char *p ) { return (UnsignedShort)((p[0] << 8) | p[1]); }

	/// The lobby name, UTF-8, cut at a whole character before RELAY_NAME_BYTES; the relay only shows it
	void relayTakeName( void )
	{
		UnicodeString name;
		if (TheGlobalData->m_lanPlayerName.isNotEmpty())
			name.translate( TheGlobalData->m_lanPlayerName );
		else
			name = LANPreferences().getUserName();
		Int n = 0;
		for (Int i = 0; i < name.getLength(); ++i)
		{
			const UnsignedInt c = (UnsignedInt)name.getCharAt(i);
			const Int size = c < 0x80 ? 1 : (c < 0x800 ? 2 : 3);
			if (n + size > RELAY_NAME_BYTES)
				break;
			if (size == 1)
				s_relay.name[n] = (unsigned char)c;
			else if (size == 2)
			{
				s_relay.name[n] = (unsigned char)(0xC0 | (c >> 6));
				s_relay.name[n + 1] = (unsigned char)(0x80 | (c & 0x3F));
			}
			else
			{
				s_relay.name[n] = (unsigned char)(0xE0 | (c >> 12));
				s_relay.name[n + 1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
				s_relay.name[n + 2] = (unsigned char)(0x80 | (c & 0x3F));
			}
			n += size;
		}
		s_relay.nameLength = n;
	}

	void relaySendHello( void )
	{
		unsigned char buf[RELAY_HELLO_HEADER + RELAY_TICKET_BYTES + RELAY_NAME_BYTES];
		buf[0] = 'Z'; buf[1] = 'R'; buf[2] = s_relay.authed ? RELAY_HELLO_AUTH : RELAY_HELLO;
		put32( buf + 3, TheGlobalData->m_exeCRC );
		put32( buf + 7, TheGlobalData->m_iniCRC );
		memcpy( buf + 11, s_relay.session, RELAY_SESSION_BYTES );
		put32( buf + 19, s_relay.wantVip );
		Int len = RELAY_HELLO_HEADER;
		if (s_relay.authed)
		{
			memcpy( buf + len, s_relay.ticket, RELAY_TICKET_BYTES );
			len += RELAY_TICKET_BYTES;
		}
		memcpy( buf + len, s_relay.name, s_relay.nameLength );
		len += s_relay.nameLength;
		s_relay.sock->Write( buf, len, s_relay.ip, s_relay.port );
		s_relay.lastHello = Clock_Milliseconds();
	}
}

UnsignedInt Transport::getRelayIP( void )
{
	return s_relay.vip;
}

/// Reads everything the relay socket holds: WELCOMEs set the session, DATA goes to the Transport on its dstPort
void Transport::pumpRelay( void )
{
	if (!s_relay.sock)
		return;
	const UnsignedInt now = Clock_Milliseconds();
	if (s_relay.vip && now - s_relay.lastHello >= RELAY_KEEPALIVE_MS)
		relaySendHello();

	unsigned char buf[RELAY_MAX_PACKET];
	sockaddr_in from;
	Int len;
	while ((len = s_relay.sock->Read( buf, sizeof(buf), &from )) > 0)
	{
		if (ntohl(from.sin_addr.s_addr) != s_relay.ip || ntohs(from.sin_port) != s_relay.port
			|| len < 3 || buf[0] != 'Z' || buf[1] != 'R')
			continue;
		if (buf[2] == RELAY_WELCOME && len >= RELAY_WELCOME_SIZE)
		{
			const UnsignedInt vip = get32( buf + 11 );
			memcpy( s_relay.session, buf + 3, RELAY_SESSION_BYTES );
			// The lobby already announced the first one; a relay that forgot us and had given it away moves us
			DEBUG_ASSERTLOG( !s_relay.vip || vip == s_relay.vip, ("Transport: the relay moved us from %d.%d.%d.%d to %d.%d.%d.%d, peers will not find us\n",
				s_relay.vip >> 24, (s_relay.vip >> 16) & 0xff, (s_relay.vip >> 8) & 0xff, s_relay.vip & 0xff,
				vip >> 24, (vip >> 16) & 0xff, (vip >> 8) & 0xff, vip & 0xff) );
			if (!s_relay.vip)
			{
				s_relay.vip = vip;
				s_relay.wantVip = vip;	// every later HELLO asks for it back
			}
		}
		else if (buf[2] == RELAY_DATA && len > RELAY_DATA_IN_HEADER && len - RELAY_DATA_IN_HEADER <= MAX_NETWORK_MESSAGE_LEN)
		{
			const UnsignedShort dstPort = get16( buf + 9 );
			for (Int u = 0; u < RELAY_MAX_TRANSPORTS; ++u)
			{
				Transport *t = s_relay.users[u];
				if (t && t->m_port == dstPort)
				{
					TransportMessage msg;
					memcpy( &msg, buf + RELAY_DATA_IN_HEADER, len - RELAY_DATA_IN_HEADER );
					t->receivePacket( msg, len - RELAY_DATA_IN_HEADER, get32( buf + 3 ), get16( buf + 7 ), now );
					break;
				}
			}
		}
	}
}

/// Opens the relay socket and waits for a WELCOME (at most five seconds); s_relay.wantVip and authed are the caller's
Bool Transport::connectRelay( const AsciiString &host, UnsignedShort relayPort )
{
	if (s_relay.failedAt && Clock_Milliseconds() - s_relay.failedAt < RELAY_RETRY_AFTER_MS)
		return false;
#if defined(_WIN32)
	// the relay socket outlives every Transport, so it keeps winsock up by itself
	if (!s_relay.wsaStarted)
	{
		WSADATA wsadata;
		if (WSAStartup( MAKEWORD(2, 2), &wsadata ) != 0)
			return false;
		s_relay.wsaStarted = true;
	}
#endif
	s_relay.ip = ResolveIP( host );
	s_relay.port = relayPort;
	if (s_relay.ip == 0 || s_relay.ip == INADDR_NONE || s_relay.port == 0)
	{
		DEBUG_LOG(("Transport: relay %s:%d does not resolve to an address and port\n", host.str(), relayPort));
		s_relay.failedAt = Clock_Milliseconds();
		return false;
	}
	if (!s_relay.sock)
	{
		s_relay.sock = NEW UDP();
		if (s_relay.sock->Bind( (UnsignedInt)INADDR_ANY, 0 ) != 0)
		{
			DEBUG_LOG(("Transport: no socket for the relay (error %d)\n", lastSocketError()));
			delete s_relay.sock;
			s_relay.sock = NULL;
			s_relay.failedAt = Clock_Milliseconds();
			return false;
		}
	}
	const UnsignedInt asked = s_relay.wantVip;
	for (Int tries = 0; tries < RELAY_HELLO_TRIES && !s_relay.vip; ++tries)
	{
		relaySendHello();
		for (Int wait = 0; wait < 10 && !s_relay.vip; ++wait)
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
			pumpRelay();
		}
	}
	if (!s_relay.vip)
	{
		DEBUG_LOG(("Transport: no WELCOME from the relay at %d.%d.%d.%d:%d in five seconds\n",
			s_relay.ip >> 24, (s_relay.ip >> 16) & 0xff, (s_relay.ip >> 8) & 0xff, s_relay.ip & 0xff, s_relay.port));
		s_relay.failedAt = Clock_Milliseconds();
		return false;
	}
	relaySendHello();	// answers the WELCOME with its session, after which the relay forwards to us
	DEBUG_LOG(("Transport: the relay calls us %d.%d.%d.%d\n",
		s_relay.vip >> 24, (s_relay.vip >> 16) & 0xff, (s_relay.vip >> 8) & 0xff, s_relay.vip & 0xff));
	// a -netgame slot that did not get its address hears nobody, and the match never starts
	DEBUG_ASSERTLOG( !asked || s_relay.vip == asked, ("Transport: asked the relay for %d.%d.%d.%d, which another session holds\n",
		asked >> 24, (asked >> 16) & 0xff, (asked >> 8) & 0xff, asked & 0xff) );
	return true;
}

/// -online: the HELLO_AUTH handshake with the ticket of the login that just succeeded
Bool Transport::openOnlineRelay( void )
{
	AsciiString host;
	UnsignedShort relayPort = 0;
	if (!OnlineGetRelayTicket( s_relay.ticket ) || !OnlineGetRelayEndpoint( host, relayPort ))
		return false;
	closeOnlineRelay();		// a second login gets a new session from a new ticket
	s_relay.authed = true;
	s_relay.wantVip = 0;
	s_relay.failedAt = 0;	// a login is a person asking; do not hold the last failure against it
	s_relay.nameLength = 0;
	return connectRelay( host, relayPort );
}

/// -online: the relay keepalive while the lobby and staging room have no Transport to pump it
void Transport::keepOnlineRelay( void )
{
	if (s_relay.authed)
		pumpRelay();
}

/// -online: forgets the session, so the next login starts clean.  Transports still attached keep their slot.
void Transport::closeOnlineRelay( void )
{
	if (!s_relay.authed)
		return;
	for (Int u = 0; u < RELAY_MAX_TRANSPORTS; ++u)
	{
		if (s_relay.users[u])
			return;		// a match still runs on it
	}
	if (s_relay.sock)
	{
		delete s_relay.sock;
		s_relay.sock = NULL;
	}
	memset( s_relay.session, 0, RELAY_SESSION_BYTES );
	s_relay.vip = 0;
	s_relay.wantVip = 0;
	s_relay.authed = false;
}

/// Joins the relay socket, opening it and waiting for a WELCOME (at most five seconds) the first time
Bool Transport::initRelayed( UnsignedInt ip, UnsignedShort port )
{
	if (!s_relay.vip)
	{
		if (s_relay.authed)
			return false;		// -online: the login's handshake failed or the session was closed
		s_relay.wantVip = (ip >> 24) == 10 ? ip : 0;
		relayTakeName();
		if (!connectRelay( TheGlobalData->m_relayHost, TheGlobalData->m_relayPort ))
			return false;
	}

	for (Int u = 0; u < RELAY_MAX_TRANSPORTS; ++u)
	{
		if (!s_relay.users[u])
		{
			s_relay.users[u] = this;
			m_relayed = true;
			resetQueues( port );
			return true;
		}
	}
	DEBUG_CRASH(("Transport: more than %d transports on the relay\n", RELAY_MAX_TRANSPORTS));
	return false;
}

void Transport::detachRelay( void )
{
	for (Int u = 0; u < RELAY_MAX_TRANSPORTS; ++u)
	{
		if (s_relay.users[u] == this)
			s_relay.users[u] = NULL;
	}
	m_relayed = false;
}

/// One queued message into a DATA envelope; returns what Write returns
Int Transport::writeRelayed( TransportMessage &msg )
{
	if (!s_relay.sock)
		return -1;
	const Int len = msg.length + sizeof(TransportMessageHeader);
	unsigned char buf[RELAY_DATA_OUT_HEADER + MAX_NETWORK_MESSAGE_LEN];
	buf[0] = 'Z'; buf[1] = 'R'; buf[2] = RELAY_DATA;
	memcpy( buf + 3, s_relay.session, RELAY_SESSION_BYTES );
	put32( buf + 11, msg.addr );
	put16( buf + 15, msg.port );
	put16( buf + 17, m_port );
	memcpy( buf + RELAY_DATA_OUT_HEADER, &msg, len );
	return s_relay.sock->Write( buf, RELAY_DATA_OUT_HEADER + len, s_relay.ip, s_relay.port );
}

//--------------------------------------------------------------------------

TransportRoute *TheTransportRoute = NULL;

Transport::Transport(void)
{
	m_winsockInit = false;
	m_udpsock = NULL;
	m_redirectedIP = 0;
	m_relayed = false;
	// EA never initialized these two.  It did not bite because everything that read them was
	// compiled out of the shipping build; now that the simulator is built in every configuration,
	// a Transport that is used before init() would have run on whatever was on the heap.
	m_useLatency = false;
	m_usePacketLoss = false;
#if !defined(_WIN32)
	m_shareAddress = false;
	m_broadcastsOnly = false;
#endif
}

Transport::~Transport(void)
{
	reset();
}

Bool Transport::init( AsciiString ip, UnsignedShort port )
{
	return init(ResolveIP(ip), port);
}

Bool Transport::init( UnsignedInt ip, UnsignedShort port )
{
	detachRelay();
	// -relay, or an -online login whose relay handshake went through (openOnlineRelay)
	if (TheGlobalData->m_relayHost.isNotEmpty() || s_relay.authed)
	{
		m_redirectedIP = 0;
		if (m_udpsock)
		{
			delete m_udpsock;
			m_udpsock = NULL;
		}
		return initRelayed( ip, port );
	}

	// ----- Initialize Winsock -----
	// (Windows only: POSIX sockets need no start-up, so m_winsockInit stays false there.)
#if defined(_WIN32)
	if (!m_winsockInit)
	{
		WORD verReq = MAKEWORD(2, 2);
		WSADATA wsadata;

		int err = WSAStartup(verReq, &wsadata);
		if (err != 0) {
			return false;
		}

		if ((LOBYTE(wsadata.wVersion) != 2) || (HIBYTE(wsadata.wVersion) !=2)) {
			WSACleanup();
			return false;
		}
		m_winsockInit = true;
	}
#endif

	// ------- Bind our port --------
	m_redirectedIP = 0;
	if (m_udpsock)
		delete m_udpsock;
	m_udpsock = NULL;
	if (TheTransportRoute)
	{
		m_port = port;
		return true;
	}
	m_udpsock = NEW UDP();

	if (!m_udpsock)
		return false;
	
	int retval = -1;
	time_t now = Clock_Milliseconds();
#if !defined(_WIN32)
	m_udpsock->ShareAddress(m_shareAddress);
#endif
	while ((retval != 0) && ((Clock_Milliseconds() - now) < 1000)) {
#if !defined(_WIN32)
		retval = m_broadcastsOnly ? m_udpsock->BindForBroadcasts(port) : m_udpsock->Bind(ip, port);
#else
		retval = m_udpsock->Bind(ip, port);
#endif
	}

	if (retval != 0) {
		DEBUG_CRASH(("Could not bind to 0x%8.8X:%d\n", ip, port));
		DEBUG_LOG(("Transport::init - Failure to bind socket with error code %x\n", retval));
		delete m_udpsock;
		m_udpsock = NULL;
		return false;
	}

	/* A VPN's split tunnel (a WFP bind redirect) or a firewall can move the socket to another adapter
		 after bind() succeeds.  Everything then leaves from an address the lobby does not know as its own:
		 the player hears his own announcements as a stranger's and nobody on the chosen network hears him.
		 Refuse the socket and say which address the system bound.  A wildcard bind has nothing to compare. */
	UnsignedInt boundIP;
	UnsignedShort boundPort;
	m_udpsock->getLocalAddr(boundIP, boundPort);
	if (ip != INADDR_ANY && boundIP != ip)
	{
		DEBUG_LOG(("Transport::init - asked for %d.%d.%d.%d:%d, the system bound %d.%d.%d.%d:%d (a VPN split tunnel or a firewall redirected the socket)\n",
			ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, port,
			boundIP >> 24, (boundIP >> 16) & 0xff, (boundIP >> 8) & 0xff, boundIP & 0xff, boundPort));
		m_redirectedIP = boundIP;
		delete m_udpsock;
		m_udpsock = NULL;
		return false;
	}

	resetQueues( port );
	return true;
}

void Transport::resetQueues( UnsignedShort port )
{
	// ------- Clear buffers --------
	// i is used after the loop; VC6 for-scope let it escape.
	int i;
	for (i =0; i<MAX_MESSAGES; ++i)
	{
		m_outBuffer[i].length = 0;
		m_inBuffer[i].length = 0;
		m_delayedInBuffer[i].message.length = 0;
	}
	for (i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		m_incomingBytes[i] = 0;
		m_outgoingBytes[i] = 0;
		m_unknownBytes[i] = 0;
		m_incomingPackets[i] = 0;
		m_outgoingPackets[i] = 0;
		m_unknownPackets[i] = 0;
	}
	m_statisticsSlot = 0;
	m_lastSecond = Clock_Milliseconds();

	m_port = port;

	/* Synthetic link conditions, off unless asked for.  Assigned rather than only ever set true:
		 EA's version could not turn either switch back off, so a second game in the same process
		 inherited the first one's settings. */
	m_useLatency = (TheGlobalData->m_latencyAverage > 0 || TheGlobalData->m_latencyNoise != 0);
	m_usePacketLoss = (TheGlobalData->m_packetLoss != 0);

	DEBUG_ASSERTLOG(!m_useLatency && !m_usePacketLoss,
		("Transport: simulating a link with %d ms latency (amp %d, period %d, noise %d) and %d%% packet loss\n",
		TheGlobalData->m_latencyAverage, TheGlobalData->m_latencyAmplitude,
		TheGlobalData->m_latencyPeriod, TheGlobalData->m_latencyNoise, TheGlobalData->m_packetLoss));
}

#if !defined(_WIN32)
Bool Transport::initBroadcastListener( UnsignedShort port )
{
	m_broadcastsOnly = true;
	return init( (UnsignedInt)INADDR_ANY, port );
}

void Transport::moveReceivedInto( Transport &inbox )
{
	Int slot = 0;
	for (Int i = 0; i < MAX_MESSAGES; ++i)
	{
		if (m_inBuffer[i].length <= 0)
			continue;
		while (slot < MAX_MESSAGES && inbox.m_inBuffer[slot].length > 0)
			++slot;
		if (slot == MAX_MESSAGES)
			return;
		inbox.m_inBuffer[slot] = m_inBuffer[i];
		m_inBuffer[i].length = 0;
	}
}
#endif

void Transport::reset( void )
{
	detachRelay();
	if (m_udpsock)
	{
		delete m_udpsock;
		m_udpsock = NULL;
	}

#if defined(_WIN32)
	if (m_winsockInit)
	{
		WSACleanup();
		m_winsockInit = false;
	}
#endif
}

Bool Transport::update( void )
{
	Bool retval = TRUE;
	if (doRecv() == FALSE && m_udpsock && m_udpsock->GetStatus() == UDP::ADDRNOTAVAIL)
	{
		retval = FALSE;
	}
	DEBUG_ASSERTLOG(retval, ("WSA error is %s\n", GetWSAErrorString(lastSocketError()).str()));
	if (doSend() == FALSE && m_udpsock && m_udpsock->GetStatus() == UDP::ADDRNOTAVAIL)
	{
		retval = FALSE;
	}
	DEBUG_ASSERTLOG(retval, ("WSA error is %s\n", GetWSAErrorString(lastSocketError()).str()));
	return retval;
}

Bool Transport::doSend() {
	if (!m_udpsock && !m_relayed && !TheTransportRoute)
	{
		DEBUG_LOG(("Transport::doSend() - m_udpSock is NULL!\n"));
		return FALSE;
	}

	Bool retval = TRUE;

	// Statistics gathering
	UnsignedInt now = Clock_Milliseconds();
	if (m_lastSecond + 1000 < now)
	{
		m_lastSecond = now;
		m_statisticsSlot = (m_statisticsSlot + 1) % MAX_TRANSPORT_STATISTICS_SECONDS;
		m_outgoingPackets[m_statisticsSlot] = 0;
		m_outgoingBytes[m_statisticsSlot] = 0;
		m_incomingPackets[m_statisticsSlot] = 0;
		m_incomingBytes[m_statisticsSlot] = 0;
		m_unknownPackets[m_statisticsSlot] = 0;
		m_unknownBytes[m_statisticsSlot] = 0;
	}

	// Send all messages
	int i;
	for (i=0; i<MAX_MESSAGES; ++i)
	{
		if (m_outBuffer[i].length != 0)
		{
			int bytesSent = 0;
			// Send this message
			const UnsignedInt wireLen = m_outBuffer[i].length + sizeof(TransportMessageHeader);
			if ((bytesSent = m_relayed ? writeRelayed(m_outBuffer[i])
				: m_udpsock ? m_udpsock->Write((unsigned char *)(&m_outBuffer[i]), wireLen, m_outBuffer[i].addr, m_outBuffer[i].port)
				: TheTransportRoute->send((unsigned char *)(&m_outBuffer[i]), wireLen, m_outBuffer[i].addr, m_outBuffer[i].port)) > 0)
			{
				//DEBUG_LOG(("Sending %d bytes to %d:%d\n", m_outBuffer[i].length + sizeof(TransportMessageHeader), m_outBuffer[i].addr, m_outBuffer[i].port));
				m_outgoingPackets[m_statisticsSlot]++;
				m_outgoingBytes[m_statisticsSlot] += m_outBuffer[i].length + sizeof(TransportMessageHeader);
				m_outBuffer[i].length = 0;  // Remove from queue
//				DEBUG_LOG(("Transport::doSend - sent %d butes to %d.%d.%d.%d:%d\n", bytesSent,
//					(m_outBuffer[i].addr >> 24) & 0xff,
//					(m_outBuffer[i].addr >> 16) & 0xff,
//					(m_outBuffer[i].addr >> 8) & 0xff,
//					m_outBuffer[i].addr & 0xff,
//					m_outBuffer[i].port));
			}
			else
			{
				//DEBUG_LOG(("Could not write to socket!!!  Not discarding message!\n"));
				retval = FALSE;
				//DEBUG_LOG(("Transport::doSend returning FALSE\n"));
			}
		}
	} // for (i=0; i<MAX_MESSAGES; ++i)

	// Latency simulation - deliver anything we're holding on to that is ready
	if (m_useLatency)
	{
		for (i=0; i<MAX_MESSAGES; ++i)
		{
			if (m_delayedInBuffer[i].message.length != 0 && m_delayedInBuffer[i].deliveryTime <= now)
			{
				for (int j=0; j<MAX_MESSAGES; ++j)
				{
					if (m_inBuffer[j].length == 0)
					{
						// Empty slot; use it
						memcpy(&m_inBuffer[j], &m_delayedInBuffer[i].message, sizeof(TransportMessage));
						m_delayedInBuffer[i].message.length = 0;
						break;
					}
				}
			}
		}
	}
	return retval;
}

/// One datagram of len bytes in incomingMessage, from addr:port, into the inbox (or the latency simulator's)
void Transport::receivePacket( TransportMessage &incomingMessage, Int len, UnsignedInt addr, UnsignedShort port, UnsignedInt now )
{
	unsigned char *buf = (unsigned char *)&incomingMessage;

	// Packet loss simulation
	if (m_usePacketLoss)
	{
		if ( linkSimPacketIsLost( TheGlobalData->m_packetLoss, GameClientRandomValue(1, 100) ) )
		{
			return;
		}
	}

	// Decrypt the packet
	decryptBuf(buf, len);

	incomingMessage.length = len - sizeof(TransportMessageHeader);

	if (len <= sizeof(TransportMessageHeader) || !isGeneralsPacket( &incomingMessage ))
	{
		m_unknownPackets[m_statisticsSlot]++;
		m_unknownBytes[m_statisticsSlot] += len;
		return;
	}

	// Something there; stick it somewhere
	m_incomingPackets[m_statisticsSlot]++;
	m_incomingBytes[m_statisticsSlot] += len;

	for (int i=0; i<MAX_MESSAGES; ++i)
	{
		// Latency simulation
		if (m_useLatency)
		{
			if (m_delayedInBuffer[i].message.length == 0)
			{
				// Empty slot; use it
				m_delayedInBuffer[i].deliveryTime = linkSimDeliveryTime( now,
					TheGlobalData->m_latencyAverage, TheGlobalData->m_latencyAmplitude,
					TheGlobalData->m_latencyPeriod,
					GameClientRandomValue(-TheGlobalData->m_latencyNoise, TheGlobalData->m_latencyNoise) );
				m_delayedInBuffer[i].message.length = incomingMessage.length;
				m_delayedInBuffer[i].message.addr = addr;
				m_delayedInBuffer[i].message.port = port;
				memcpy(&m_delayedInBuffer[i].message, buf, len);
				break;
			}
		}
		else
		{
			if (m_inBuffer[i].length == 0)
			{
				// Empty slot; use it
				m_inBuffer[i].length = incomingMessage.length;
				m_inBuffer[i].addr = addr;
				m_inBuffer[i].port = port;
				memcpy(&m_inBuffer[i], buf, len);
				break;
			}
		}
	}
	//DEBUG_ASSERTCRASH(i<MAX_MESSAGES, ("Message lost!"));
}

Bool Transport::doRecv()
{
	if (m_relayed)
	{
		pumpRelay();
		return TRUE;
	}

	if (!m_udpsock && !TheTransportRoute)
	{
		DEBUG_LOG(("Transport::doRecv() - m_udpSock is NULL!\n"));
		return FALSE;
	}

	Bool retval = TRUE;

	// Read in anything on our socket
	sockaddr_in from;
	UnsignedInt now = Clock_Milliseconds();

	TransportMessage incomingMessage;
	unsigned char *buf = (unsigned char *)&incomingMessage;
	int len = MAX_NETWORK_MESSAGE_LEN;
	while ( (len=(m_udpsock ? m_udpsock->Read(buf, MAX_NETWORK_MESSAGE_LEN, &from) : TheTransportRoute->recv(buf, MAX_NETWORK_MESSAGE_LEN, &from))) > 0 )
	{
		receivePacket( incomingMessage, len, ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), now );
	}

	if (len == -1) {
		// there was a socket error trying to perform a read.
		//DEBUG_LOG(("Transport::doRecv returning FALSE\n"));
		retval = FALSE;
	}

	return retval;
}

Bool Transport::queueSend(UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len /*,
						  NetMessageFlags flags, Int id */)
{
	int i;

	if (len < 1 || len > MAX_PACKET_SIZE)
	{
		return false;
	}

	for (i=0; i<MAX_MESSAGES; ++i)
	{
		if (m_outBuffer[i].length == 0)
		{
			// Insert data here
			m_outBuffer[i].length = len;
			memcpy(m_outBuffer[i].data, buf, len);
			m_outBuffer[i].addr = addr;
			m_outBuffer[i].port = port;
//			m_outBuffer[i].header.flags = flags;
//			m_outBuffer[i].header.id = id;
			m_outBuffer[i].header.magic = GENERALS_MAGIC_NUMBER;

			CRC crc;
			crc.computeCRC( (unsigned char *)(&(m_outBuffer[i].header.magic)), m_outBuffer[i].length + sizeof(TransportMessageHeader) - sizeof(UnsignedInt) );
//			DEBUG_LOG(("About to assign the CRC for the packet\n"));
			m_outBuffer[i].header.crc = crc.get();

			// Encrypt packet
//			DEBUG_LOG(("buffer: "));
			encryptBuf((unsigned char *)&m_outBuffer[i], len + sizeof(TransportMessageHeader));
//			DEBUG_LOG(("\n"));

			return true;
		}
	}
	return false;
}

Bool Transport::isGeneralsPacket( TransportMessage *msg )
{
	if (!msg)
		return false;

	if (msg->length < 0 || msg->length > MAX_PACKET_SIZE)
		return false;

	CRC crc;
//	crc.computeCRC( (unsigned char *)msg->data, msg->length );
	crc.computeCRC( (unsigned char *)(&(msg->header.magic)), msg->length + sizeof(TransportMessageHeader) - sizeof(UnsignedInt) );

	if (crc.get() != msg->header.crc)
		return false;

	if (msg->header.magic != GENERALS_MAGIC_NUMBER)
		return false;

	return true;
}

// Statistics ---------------------------------------------------
Real Transport::getIncomingBytesPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_incomingBytes[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getIncomingPacketsPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_incomingPackets[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getOutgoingBytesPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_outgoingBytes[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getOutgoingPacketsPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_outgoingPackets[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getUnknownBytesPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_unknownBytes[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}

Real Transport::getUnknownPacketsPerSecond( void )
{
	Real val = 0.0;
	for (int i=0; i<MAX_TRANSPORT_STATISTICS_SECONDS; ++i)
	{
		if (i != m_statisticsSlot)
			val += m_unknownPackets[i];
	}
	return val / (MAX_TRANSPORT_STATISTICS_SECONDS-1);
}



