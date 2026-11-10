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


#pragma once

#ifndef __NETWORKDEFS_H
#define __NETWORKDEFS_H


#include <stddef.h>	// offsetof, for the wire-layout asserts (B4)
#include "Lib/BaseType.h"
#include "Common/MessageStream.h"

static const Int WOL_NAME_LEN = 64;

/// Max number of commands per frame
static const Int MAX_COMMANDS = 256;

extern Int MAX_FRAMES_AHEAD;
extern Int MIN_RUNAHEAD;

// FRAME_DATA_LENGTH needs to be MAX_FRAMES_AHEAD+1 because a player on a different
// computer can send commands for a frame that is one beyond twice the max runahead.
extern Int FRAME_DATA_LENGTH;
extern Int FRAMES_TO_KEEP;

/// The seats in a network game, and the one place that number is set.  EA's was 8.
static const Int MAX_SLOTS = 32;

enum ConnectionNumbers
{
	MAX_PLAYER = MAX_SLOTS - 1,			// The index of the highest possible player number.  This is 0 based, so the most players allowed in a game is MAX_PLAYER+1.
	NUM_CONNECTIONS
};

/* A set of slots, one bit each: the relay a command still has to reach, a chat's or a file's
	 recipients.  EA's was a byte, which held eight.  On the wire it is all four bytes of this. */
typedef UnsignedInt SlotMask;
static_assert(MAX_SLOTS <= (Int)(sizeof(SlotMask) * 8), "SlotMask needs a bit for every slot");
// shifted down from all ones, so a mask exactly as wide as MAX_SLOTS never shifts by its own width
static const SlotMask SLOTMASK_ALL = ~(SlotMask)0 >> (sizeof(SlotMask) * 8 - MAX_SLOTS);
inline SlotMask slotBit(Int slot) { return (SlotMask)1 << slot; }
/// every slot but this one: what EA wrote as 0xff ^ (1 << slot)
inline SlotMask allSlotsBut(Int slot) { return SLOTMASK_ALL & ~slotBit(slot); }

#pragma pack(push, 1)
struct TransportMessageHeader
{
	UnsignedInt crc;											///< packet-level CRC (must be first in packet)
	UnsignedShort magic;									///< Magic number identifying Generals packets
//	Int id;
//	NetMessageFlags flags;
};
#pragma pack(pop)

/* The network's packed structs (this header, TransportMessage's header; FirewallHelper.h's
	 ManglerData; LANAPI.h's LANMessage) go on the wire as they sit in memory: their sizes are
	 checked where they are declared, and their integers are in the host's byte order, which is
	 little-endian on every machine the game has run on.  A big-endian port would need a byte swap at
	 every read and write of them.  MSVC's targets are all little-endian; other compilers say. */
static_assert(sizeof(TransportMessageHeader) == 6, "TransportMessageHeader is 6 bytes on the wire");
static_assert(offsetof(TransportMessageHeader, magic) == 4, "TransportMessageHeader: magic follows the 4-byte crc");
#if defined(__BYTE_ORDER__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the network structs are sent in little-endian byte order");
#elif !defined(_MSC_VER)
#error "unknown byte order: the network structs are sent in little-endian byte order"
#endif

// 2003's answer was a 512 byte datagram: UDP (8 bytes) + IP header (28 bytes) = 36, so 476 of
// payload.  That is a quarter of what any link today carries, and it is the reason a busy frame
// needs four or five datagrams where it could need one - every extra datagram is another chance to
// arrive late and another entry in the resend machinery.
//
// 1100 is the ceiling we take instead.  It is not the ethernet MTU: a mobile link can hand back an
// MTU of 1340, and PPPoE, IPv6 and tunnelling each eat their own header on top of that, so a
// payload chosen from 1500 fragments on exactly the links that can least afford it.  Nothing here
// discovers the path MTU, so the number has to be safe unasked.
static const Int MAX_UDP_PAYLOAD_SIZE = 1100;

// What goes on the wire is the header plus the payload, and the payload is what a packet may hold.
static const Int MAX_NETWORK_MESSAGE_LEN = MAX_UDP_PAYLOAD_SIZE;
static const Int MAX_PACKET_SIZE = MAX_UDP_PAYLOAD_SIZE - sizeof(TransportMessageHeader);

// The LAN lobby's own broadcast is a fixed struct read at fixed offsets, one datagram each.  EA's was
// 476; a 32-seat room's options need several times that, so it takes the whole payload and the
// options go out in parts (LANAPI.h).
static const Int MAX_LANAPI_PACKET_SIZE = MAX_PACKET_SIZE;

/**
 * Command packet - contains frame #, total # of commands, and each command.  This is what gets sent
 * to each player every frame
 */
// twice as many in flight: the send and receive rings both used to fill under load, and a full ring
// is a dropped message that the other end waits for
#define MAX_MESSAGES 256
static const Int numCommandsPerCommandPacket = (MAX_PACKET_SIZE - sizeof(UnsignedInt) - sizeof(UnsignedShort))/sizeof(GameMessage);
#pragma pack(push, 1)
struct CommandPacket
{
	UnsignedInt m_frame;
	UnsignedShort m_numCommands;
	unsigned char m_commands[numCommandsPerCommandPacket * sizeof(GameMessage)];
};
#pragma pack(pop)

#define MAX_TRANSPORT_STATISTICS_SECONDS 30

/**
 * Transport message - encapsulating info kept by the transport layer about each
 * packet.  These structs make up the in/out buffers at the transport layer.
 */
#pragma pack(push, 1)
struct TransportMessage
{
	TransportMessageHeader header;
	UnsignedByte data[MAX_PACKET_SIZE];
	Int length;
	UnsignedInt addr;
	UnsignedShort port;
};
#pragma pack(pop)

/**
 * A received packet the transport is holding back, to reproduce a slow link on one machine.
 * Built in every configuration - it used to be debug-only, and this fork only ever ships Release.
 */
#pragma pack(push, 1)
struct DelayedTransportMessage
{
	UnsignedInt deliveryTime;
	TransportMessage message;
};
#pragma pack(pop)

/* The transport's packed layouts (B4), each from its definition at pack(1): the header's 6, then
	 MAX_PACKET_SIZE (1094) bytes of data, then length (4), addr (4) and port (2); a delayed message is a
	 4-byte delivery time in front of one.  MSVC compiles these too: a pack(1) layout that differed there
	 fails the Windows build rather than a game between the two. */
static_assert(offsetof(TransportMessage, data) == 6, "TransportMessage: data follows the 6-byte header");
static_assert(offsetof(TransportMessage, length) == 6 + MAX_PACKET_SIZE, "TransportMessage: length follows the data");
static_assert(offsetof(TransportMessage, addr) == 10 + MAX_PACKET_SIZE, "TransportMessage: addr follows length");
static_assert(offsetof(TransportMessage, port) == 14 + MAX_PACKET_SIZE, "TransportMessage: port is the last member");
static_assert(sizeof(TransportMessage) == 16 + MAX_PACKET_SIZE, "TransportMessage is its members, unpadded");
static_assert(offsetof(DelayedTransportMessage, message) == 4, "DelayedTransportMessage: message follows the delivery time");
static_assert(sizeof(DelayedTransportMessage) == 4 + sizeof(TransportMessage), "DelayedTransportMessage is its members, unpadded");

/**
 * Message types
 */
enum NetMessageFlag {
	MSG_ACK = 1,
	MSG_NEEDACK = 2,
	MSG_SEQUENCED = 4,
	MSG_SUPERCEDING = 8
};
typedef UnsignedByte NetMessageFlags;

enum NetCommandType {
	NETCOMMANDTYPE_UNKNOWN = -1,
	NETCOMMANDTYPE_ACKBOTH = 0,
	NETCOMMANDTYPE_ACKSTAGE1,
	NETCOMMANDTYPE_ACKSTAGE2,
	NETCOMMANDTYPE_FRAMEINFO,
	NETCOMMANDTYPE_GAMECOMMAND,
	NETCOMMANDTYPE_PLAYERLEAVE,
	NETCOMMANDTYPE_RUNAHEADMETRICS,
	NETCOMMANDTYPE_RUNAHEAD,
	NETCOMMANDTYPE_DESTROYPLAYER,
	NETCOMMANDTYPE_KEEPALIVE,
	NETCOMMANDTYPE_DISCONNECTCHAT,
	NETCOMMANDTYPE_CHAT,
	NETCOMMANDTYPE_MANGLERQUERY,
	NETCOMMANDTYPE_MANGLERRESPONSE,
	NETCOMMANDTYPE_PROGRESS,
	NETCOMMANDTYPE_LOADCOMPLETE,
	NETCOMMANDTYPE_TIMEOUTSTART,
	NETCOMMANDTYPE_WRAPPER,							// A wrapper command that holds a command thats too big to fit in a single packet.
	NETCOMMANDTYPE_FILE,
	NETCOMMANDTYPE_FILEANNOUNCE,
	NETCOMMANDTYPE_FILEPROGRESS,
	NETCOMMANDTYPE_FRAMERESENDREQUEST,

	// Disconnect menu command section.
	NETCOMMANDTYPE_DISCONNECTSTART,
	NETCOMMANDTYPE_DISCONNECTKEEPALIVE,
	NETCOMMANDTYPE_DISCONNECTPLAYER,
	NETCOMMANDTYPE_PACKETROUTERQUERY,
	NETCOMMANDTYPE_PACKETROUTERACK,
	NETCOMMANDTYPE_DISCONNECTVOTE,
	NETCOMMANDTYPE_DISCONNECTFRAME,
	NETCOMMANDTYPE_DISCONNECTSCREENOFF,
	NETCOMMANDTYPE_DISCONNECTEND,

	NETCOMMANDTYPE_MAX
};

enum NetLocalStatus {
	NETLOCALSTATUS_PREGAME = 0,
	NETLOCALSTATUS_INGAME,
	NETLOCALSTATUS_LEAVING,
	NETLOCALSTATUS_LEFT,
	NETLOCALSTATUS_POSTGAME
};

enum PlayerLeaveCode {
	PLAYERLEAVECODE_CLIENT = 0,
	PLAYERLEAVECODE_LOCAL,
	PLAYERLEAVECODE_PACKETROUTER,
	PLAYERLEAVECODE_UNKNOWN
};

// Magic number for identifying a Generals packet.  It is the protocol version too: a packet with
// another one is dropped as noise, so a build whose wire format differs never reads the other's.
// 0xF00D was EA's, with a one-byte relay mask and eight slots; 0xF032 has the 32-bit SlotMask and 32.
static const UnsignedShort GENERALS_MAGIC_NUMBER = 0xF032;

// The number of fps history entries.
//static const Int NETWORK_FPS_HISTORY_LENGTH = 30;

// The number of ping history entries.
//static const Int NETWORK_LATENCY_HISTORY_LENGTH = 200;

// The number of miliseconds between run ahead metrics things
//static const Int NETWORK_RUN_AHEAD_METRICS_TIME = 5000;

// The number of cushion values to keep.
//static const Int NETWORK_CUSHION_HISTORY_LENGTH = 10;

// The amount of slack in the run ahead value.  This is the percentage of the calculated run ahead that is added.
//static const Int NETWORK_RUN_AHEAD_SLACK = 20;

// The number of seconds between when the connections to each player send a keep-alive packet.
// This should be less than 30 just to keep firewall ports open.
//static const Int NETWORK_KEEPALIVE_DELAY = 20;

// The number of milliseconds between when the game gets stuck on a frame for a network stall and
// and when the disconnect dialog comes up.
//static const Int NETWORK_DISCONNECT_TIME = 5000;

// The number of miliseconds between when a player's last disconnect keep alive command
// was recieved and when they are considered disconnected from the game.
//static const Int NETWORK_PLAYER_TIMEOUT_TIME = 60000;

// The base port number used for the transport socket.  A players slot number is added to this
// value to get their actual port number.
static const Int NETWORK_BASE_PORT_NUMBER = 8088;

// the singleton
class NetworkInterface;
extern NetworkInterface *TheNetwork;


#endif
