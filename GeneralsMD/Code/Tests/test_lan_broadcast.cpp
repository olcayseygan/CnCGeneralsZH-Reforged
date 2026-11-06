/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

/*
 * Port defect 29: a POSIX LAN lobby heard no broadcasts, because BSD and Linux sockets bound to one unicast
 * address are not handed them (Windows' are).  The fix (udp.h) keeps a second, wildcard socket per lobby
 * that takes only datagrams sent to 255.255.255.255.  This drives the real Transport and UDP classes
 * through the layout LANAPI builds, two copies of the game on one host:
 *
 *   copy A (the lobby at 127.0.0.1):  its unicast socket 127.0.0.1:P and its broadcast listener *:P
 *   copy L (the lobby at this machine's first non-loopback address): L:P and its listener *:P
 *
 * and checks that
 *   - every bind succeeds, and a second socket on A's own address and port still fails;
 *   - L's broadcast reaches A's listener and L's own listener once each, and neither unicast socket, so
 *     the lobby's inbox gets it once; L's own copy carries L's address, which LANAPI::update's
 *     own-address filter drops;
 *   - a message sent to A's address reaches A's unicast socket once and no listener;
 *   - a unicast datagram reaching a wildcard listener (sent to a local address nobody bound) is dropped;
 *     the control: a plain wildcard socket in its place receives it, so it is the filter that drops it;
 *   - moveReceivedInto moves each message once into free slots and leaves the rest when the inbox is
 *     full.
 *
 * What this cannot see: a broadcast from ANOTHER machine (one host's datagrams never leave the kernel;
 * a real LAN adds a switch, and on Linux the same socket rules are expected by its documentation but
 * not measured here), and a Windows peer.  It needs a non-loopback IPv4 address that is up: a machine
 * without one cannot send a broadcast at all, and the test FAILS there rather than pass unseen.
 */

#include "test_harness.h"

#include "Common/GlobalData.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/udp.h"

#include <dirent.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)

namespace {

enum { PORT = 28190, PORT_ALONE = 28191, PORT_CONTROL = 28192, PORT_HELD = 28193, PORT_FIRST = 28194, PORT_SECOND = 28195 };
const UnsignedInt LOOPBACK = 0x7F000001;

/// The first up, non-loopback IPv4 address, host order; 0 when there is none
UnsignedInt firstNonLoopback( void )
{
	IPEnumeration ips;
	for (EnumeratedIP *e = ips.getAddresses(); e != NULL; e = e->getNext())
		if ((e->getIP() >> 24) != 127)
			return e->getIP();
	return 0;
}

/// How many received messages in `t` carry `text`; those are cleared
Int take( Transport &t, const char *text )
{
	Int n = 0;
	for (Int i = 0; i < MAX_MESSAGES; ++i)
	{
		TransportMessage &m = t.m_inBuffer[i];
		if (m.length == (Int)strlen( text ) + 1 && memcmp( m.data, text, m.length ) == 0)
		{
			++n;
			m.length = 0;
		}
	}
	return n;
}

/// The address a message carrying `text` came from, without clearing it; 0 when none has
UnsignedInt senderOf( Transport &t, const char *text )
{
	for (Int i = 0; i < MAX_MESSAGES; ++i)
	{
		TransportMessage &m = t.m_inBuffer[i];
		if (m.length == (Int)strlen( text ) + 1 && memcmp( m.data, text, m.length ) == 0)
			return m.addr;
	}
	return 0;
}

/// Sends `text` from `from` to addr:port, then services every receiver for half a second
void sendAndPump( Transport &from, UnsignedInt addr, UnsignedShort port, const char *text, Transport **receivers, Int count )
{
	from.queueSend( addr, port, (const UnsignedByte *)text, (Int)strlen( text ) + 1 );
	from.update();
	// wall time, not clock(): that counts CPU time, which a loop that mostly sleeps hardly uses
	struct timespec now;
	clock_gettime( CLOCK_MONOTONIC, &now );
	const double end = now.tv_sec + now.tv_nsec * 1e-9 + 0.5;
	struct timespec pause = { 0, 5 * 1000 * 1000 };
	for (;;)
	{
		clock_gettime( CLOCK_MONOTONIC, &now );
		if (now.tv_sec + now.tv_nsec * 1e-9 >= end)
			break;
		for (Int i = 0; i < count; ++i)
			receivers[i]->update();
		nanosleep( &pause, NULL );
	}
}

/// How many descriptors this process has open, from /dev/fd (Linux: /proc/self/fd); the listing's own
/// descriptor is not counted
Int openDescriptors( void )
{
	DIR *dir = opendir( "/dev/fd" );
	if (dir == NULL)
		return -1;
	Int n = 0;
	for (struct dirent *e = readdir( dir ); e != NULL; e = readdir( dir ))
		if (e->d_name[0] != '.')
			++n;
	closedir( dir );
	return n - 1;
}

struct Globals
{
	GlobalData *saved;
	Globals() : saved( TheWritableGlobalData ) { TheWritableGlobalData = NEW GlobalData; }
	~Globals() { delete TheWritableGlobalData; TheWritableGlobalData = saved; }
};

}	// namespace

TEST(lan_lobby_hears_broadcasts_on_posix)
{
	Globals globals;		// Transport::init reads the link simulation's settings
	const UnsignedInt local = firstNonLoopback();
	CHECK( local != 0 );		// no address, no broadcast: see the header
	if (local == 0)
		return;

	Transport aUnicast, aListener, lUnicast, lListener, duplicate;
	aUnicast.shareAddress( TRUE );
	lUnicast.shareAddress( TRUE );
	duplicate.shareAddress( TRUE );
	CHECK( aUnicast.init( LOOPBACK, PORT ) );
	CHECK( aListener.initBroadcastListener( PORT ) );
	CHECK( lUnicast.init( local, PORT ) );
	CHECK( lUnicast.allowBroadcasts( TRUE ) );
	CHECK( lListener.initBroadcastListener( PORT ) );
	// sharing the port with the listeners must not let two sockets share one address and port
	CHECK( !duplicate.init( LOOPBACK, PORT ) );

	Transport *everyone[] = { &aUnicast, &aListener, &lUnicast, &lListener };

	// L's lobby announces a game
	sendAndPump( lUnicast, INADDR_BROADCAST, PORT, "announce", everyone, 4 );
	if(senderOf(lListener, "announce") != local) printf("broadcast source %08x, selected %08x\n", senderOf(lListener, "announce"), local);
	CHECK_EQ( senderOf( lListener, "announce" ), local );		// L's own copy: its filter drops it
	CHECK_EQ( take( aListener, "announce" ), 1 );		// A hears it, once
	CHECK_EQ( take( lListener, "announce" ), 1 );
	CHECK_EQ( take( aUnicast, "announce" ), 0 );		// and no unicast socket does, so no inbox gets two
	CHECK_EQ( take( lUnicast, "announce" ), 0 );

	// L asks A to join: only A's unicast socket
	sendAndPump( lUnicast, LOOPBACK, PORT, "join", everyone, 4 );
	CHECK_EQ( take( aUnicast, "join" ), 1 );
	CHECK_EQ( take( aListener, "join" ), 0 );
	CHECK_EQ( take( lListener, "join" ), 0 );

	// A answers L: only L's unicast socket
	sendAndPump( aUnicast, local, PORT, "accept", everyone, 4 );
	CHECK_EQ( take( lUnicast, "accept" ), 1 );
	CHECK_EQ( take( aListener, "accept" ), 0 );
	CHECK_EQ( take( lListener, "accept" ), 0 );
}

TEST(lan_broadcast_listener_drops_unicast_and_the_control_shows_it_arrives)
{
	Globals globals;
	Transport listener, plain, sender;
	CHECK( listener.initBroadcastListener( PORT_ALONE ) );
	CHECK( plain.init( (UnsignedInt)INADDR_ANY, PORT_CONTROL ) );		// the control: no filter
	CHECK( sender.init( LOOPBACK, 0 ) );
	Transport *both[] = { &listener, &plain };
	// 127.0.0.1 on these ports is bound by nobody: the wildcard sockets are the only takers
	sendAndPump( sender, LOOPBACK, PORT_ALONE, "stray", both, 2 );
	sendAndPump( sender, LOOPBACK, PORT_CONTROL, "stray", both, 2 );
	CHECK_EQ( take( listener, "stray" ), 0 );
	CHECK_EQ( take( plain, "stray" ), 1 );
}

TEST(lan_received_messages_move_into_the_inbox_once)
{
	Transport listener, inbox;
	for (Int i = 0; i < MAX_MESSAGES; ++i)
	{
		listener.m_inBuffer[i].length = 0;
		inbox.m_inBuffer[i].length = 0;
	}
	listener.m_inBuffer[3].length = 5;
	listener.m_inBuffer[3].addr = 0x0A000001;
	memcpy( listener.m_inBuffer[3].data, "four", 5 );
	inbox.m_inBuffer[0].length = 7;		// already waiting: untouched
	listener.moveReceivedInto( inbox );
	CHECK_EQ( listener.m_inBuffer[3].length, 0 );
	CHECK_EQ( inbox.m_inBuffer[0].length, 7 );
	CHECK_EQ( inbox.m_inBuffer[1].length, 5 );
	CHECK_EQ( inbox.m_inBuffer[1].addr, (UnsignedInt)0x0A000001 );
	CHECK( memcmp( inbox.m_inBuffer[1].data, "four", 5 ) == 0 );
	Int moved = 0;
	for (Int i = 0; i < MAX_MESSAGES; ++i)
		moved += inbox.m_inBuffer[i].length == 5 ? 1 : 0;
	CHECK_EQ( moved, 1 );
	listener.moveReceivedInto( inbox );		// nothing left to move a second time
	moved = 0;
	for (Int i = 0; i < MAX_MESSAGES; ++i)
		moved += inbox.m_inBuffer[i].length == 5 ? 1 : 0;
	CHECK_EQ( moved, 1 );

	// a full inbox takes nothing, and the message waits
	for (Int i = 0; i < MAX_MESSAGES; ++i)
		inbox.m_inBuffer[i].length = 9;
	listener.m_inBuffer[0].length = 5;
	listener.moveReceivedInto( inbox );
	CHECK_EQ( listener.m_inBuffer[0].length, 5 );
}

/* A bind that fails leaves no socket open.  Transport::init retries UDP::Bind for up to a second while the
   port is taken, and each try made a socket that a failed bind never closed: about 100,000 per call, on
   every platform, which on a Mac filled the whole system's file table (two copies of net_check at once).
   The failed init here runs that whole second of retries. */
TEST(udp_a_bind_that_fails_leaves_no_socket_open)
{
	Globals globals;
	Transport holder, second;
	CHECK( holder.init( LOOPBACK, PORT_HELD ) );
	const Int before = openDescriptors();
	CHECK( before > 0 );
	CHECK( !second.init( LOOPBACK, PORT_HELD ) );		// the port is taken: every retry fails
	const Int after = openDescriptors();
	printf( "  descriptors open: %d before the failed init, %d after\n", before, after );
	CHECK_EQ( after, before );
}

/// A UDP bound again drops the socket it had: one socket, not two
TEST(udp_bound_twice_holds_one_socket)
{
	const Int before = openDescriptors();
	{
		UDP udp;
		CHECK_EQ( udp.Bind( LOOPBACK, PORT_FIRST ), (Int)UDP::OK );
		CHECK_EQ( udp.Bind( LOOPBACK, PORT_SECOND ), (Int)UDP::OK );
		CHECK_EQ( openDescriptors(), before + 1 );
	}
	CHECK_EQ( openDescriptors(), before );		// and the destructor closes that one
}

#endif	// !_WIN32
