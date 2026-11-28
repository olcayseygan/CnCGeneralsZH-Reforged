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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// FILE: ControlServer.cpp ///////////////////////////////////////////////////////////////////////
//
// -control [port]: a WebSocket on 127.0.0.1 that drives the game from outside it.  See
// ControlServer.h for the command grammar and for why world commands are queued rather than run
// where they arrive.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ControlServer.h"
#include "Common/Energy.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "Common/MessageStream.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ThingTemplate.h"
#include "GameClient/AirliftPanel.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"		// ToggleQuitMenu, for quitmenu
#include "GameClient/InGameUI.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/DirectStrikeHud.h"
#include "GameClient/Shell.h"
#include "GameClient/View.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Mouse.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"		// MAX_SLOTS, for the skirmish command's player count
#include "GameNetwork/Online/OnlineQueues.h"

#include <vector>

// windows.h, which PreRTS.h already pulled in, brings Winsock 1.1 with it.  That is every call
// this file makes, so it takes the one already on the table rather than starting the winsock2
// header fight described in the hard constraints.
//
// Off Windows the same calls are BSD sockets, which Winsock copied.  The three names that are
// only spelled differently get file-local POSIX definitions here, so the call sites below read as
// they always did; the places where the two genuinely behave differently (startup, non-blocking,
// SIGPIPE, EINTR, SO_REUSEADDR) are #if'd where they happen, each with its reason.
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int SOCKET;
static const SOCKET INVALID_SOCKET = -1;
static inline int closesocket( SOCKET s ) { return close( s ); }
#endif

// A send() to a client that has gone away raises SIGPIPE on POSIX, and its default is to end the
// process: the ordinary way a -control session ends would kill the game.  Windows returns an
// error instead.  macOS and the BSDs switch it off per socket (SO_NOSIGPIPE, set on accept below);
// Linux has no such option and takes MSG_NOSIGNAL on each send instead.
#if defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
#define CONTROL_SEND_FLAGS MSG_NOSIGNAL
#else
#define CONTROL_SEND_FLAGS 0
#endif

// ------------------------------------------------------------------------------------------------
// sizes and constants
// ------------------------------------------------------------------------------------------------

static const Int CONTROL_LOOPBACK_ONLY = INADDR_LOOPBACK;
static const Int CONTROL_BACKLOG = 1;
static const Int CONTROL_READ_CHUNK = 4096;
static const Int CONTROL_MAX_REQUEST = 16384;
static const Int CONTROL_MAX_COMMANDS_PER_FRAME = 64;

/// RFC 6455's magic string, appended to the client key before hashing
static const char *CONTROL_WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const Int SHA1_DIGEST_BYTES = 20;
static const Int SHA1_BLOCK_BYTES = 64;

// WebSocket opcodes, from RFC 6455 section 5.2
static const unsigned char WS_OPCODE_TEXT = 0x1;
static const unsigned char WS_OPCODE_CLOSE = 0x8;
static const unsigned char WS_OPCODE_PING = 0x9;
static const unsigned char WS_OPCODE_PONG = 0xA;

/// status samples the shroud on this many points a side, spread over the map, for "revealed"
static const Int STATUS_SHROUD_SAMPLES_PER_SIDE = 8;

// ------------------------------------------------------------------------------------------------
// SHA-1, because the handshake needs one and nothing in GameEngine has one
// ------------------------------------------------------------------------------------------------

struct Sha1State
{
	UnsignedInt digest[ 5 ];
	UnsignedInt byteCount;
	unsigned char block[ SHA1_BLOCK_BYTES ];
	Int blockLength;
};

static UnsignedInt sha1RotateLeft( UnsignedInt value, Int bits )
{
	return (value << bits) | (value >> (32 - bits));
}

static void sha1Init( Sha1State *state )
{
	state->digest[ 0 ] = 0x67452301;
	state->digest[ 1 ] = 0xEFCDAB89;
	state->digest[ 2 ] = 0x98BADCFE;
	state->digest[ 3 ] = 0x10325476;
	state->digest[ 4 ] = 0xC3D2E1F0;
	state->byteCount = 0;
	state->blockLength = 0;
}

static void sha1Transform( Sha1State *state )
{
	UnsignedInt w[ 80 ];
	Int i;

	for( i = 0; i < 16; ++i )
	{
		w[ i ] = ((UnsignedInt)state->block[ i * 4 ] << 24)
					 | ((UnsignedInt)state->block[ i * 4 + 1 ] << 16)
					 | ((UnsignedInt)state->block[ i * 4 + 2 ] << 8)
					 | ((UnsignedInt)state->block[ i * 4 + 3 ]);
	}
	for( i = 16; i < 80; ++i )
		w[ i ] = sha1RotateLeft( w[ i - 3 ] ^ w[ i - 8 ] ^ w[ i - 14 ] ^ w[ i - 16 ], 1 );

	UnsignedInt a = state->digest[ 0 ];
	UnsignedInt b = state->digest[ 1 ];
	UnsignedInt c = state->digest[ 2 ];
	UnsignedInt d = state->digest[ 3 ];
	UnsignedInt e = state->digest[ 4 ];

	for( i = 0; i < 80; ++i )
	{
		UnsignedInt f, k;
		if (i < 20)			{ f = (b & c) | ((~b) & d);				k = 0x5A827999; }
		else if (i < 40)	{ f = b ^ c ^ d;									k = 0x6ED9EBA1; }
		else if (i < 60)	{ f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
		else							{ f = b ^ c ^ d;									k = 0xCA62C1D6; }

		const UnsignedInt temp = sha1RotateLeft( a, 5 ) + f + e + k + w[ i ];
		e = d;
		d = c;
		c = sha1RotateLeft( b, 30 );
		b = a;
		a = temp;
	}

	state->digest[ 0 ] += a;
	state->digest[ 1 ] += b;
	state->digest[ 2 ] += c;
	state->digest[ 3 ] += d;
	state->digest[ 4 ] += e;
}

static void sha1Update( Sha1State *state, const unsigned char *data, Int length )
{
	for( Int i = 0; i < length; ++i )
	{
		state->block[ state->blockLength++ ] = data[ i ];
		++state->byteCount;
		if (state->blockLength == SHA1_BLOCK_BYTES)
		{
			sha1Transform( state );
			state->blockLength = 0;
		}
	}
}

static void sha1Finish( Sha1State *state, unsigned char *digestOut )
{
	const UnsignedInt bitCount = state->byteCount * 8;

	const unsigned char one = 0x80;
	sha1Update( state, &one, 1 );

	const unsigned char zero = 0x00;
	while (state->blockLength != 56)
		sha1Update( state, &zero, 1 );

	unsigned char lengthBytes[ 8 ];
	for( Int i = 0; i < 4; ++i )
		lengthBytes[ i ] = 0;
	lengthBytes[ 4 ] = (unsigned char)((bitCount >> 24) & 0xFF);
	lengthBytes[ 5 ] = (unsigned char)((bitCount >> 16) & 0xFF);
	lengthBytes[ 6 ] = (unsigned char)((bitCount >> 8) & 0xFF);
	lengthBytes[ 7 ] = (unsigned char)(bitCount & 0xFF);
	sha1Update( state, lengthBytes, 8 );

	for( Int i = 0; i < SHA1_DIGEST_BYTES; ++i )
		digestOut[ i ] = (unsigned char)((state->digest[ i / 4 ] >> (24 - 8 * (i % 4))) & 0xFF);
}

// ------------------------------------------------------------------------------------------------
// base64
// ------------------------------------------------------------------------------------------------

static const char *BASE64_ALPHABET =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static Int base64Encode( const unsigned char *data, Int length, char *out, Int outSize )
{
	const Int needed = ((length + 2) / 3) * 4 + 1;
	if (outSize < needed)
		return 0;

	Int written = 0;
	Int at = 0;
	while (at < length)
	{
		const UnsignedInt byte0 = data[ at ];
		const UnsignedInt byte1 = (at + 1 < length) ? data[ at + 1 ] : 0;
		const UnsignedInt byte2 = (at + 2 < length) ? data[ at + 2 ] : 0;
		const UnsignedInt triple = (byte0 << 16) | (byte1 << 8) | byte2;

		out[ written++ ] = BASE64_ALPHABET[ (triple >> 18) & 0x3F ];
		out[ written++ ] = BASE64_ALPHABET[ (triple >> 12) & 0x3F ];
		out[ written++ ] = (at + 1 < length) ? BASE64_ALPHABET[ (triple >> 6) & 0x3F ] : '=';
		out[ written++ ] = (at + 2 < length) ? BASE64_ALPHABET[ triple & 0x3F ] : '=';
		at += 3;
	}

	out[ written ] = 0;
	return written;
}

Bool ControlServer_computeAcceptKey( const char *clientKey, char *out, Int outSize )
{
	if (clientKey == NULL || out == NULL)
		return FALSE;

	Sha1State state;
	sha1Init( &state );
	sha1Update( &state, (const unsigned char *)clientKey, (Int)strlen( clientKey ) );
	sha1Update( &state, (const unsigned char *)CONTROL_WEBSOCKET_GUID,
							(Int)strlen( CONTROL_WEBSOCKET_GUID ) );

	unsigned char digest[ SHA1_DIGEST_BYTES ];
	sha1Finish( &state, digest );

	return base64Encode( digest, SHA1_DIGEST_BYTES, out, outSize ) > 0;
}

// ------------------------------------------------------------------------------------------------
// socket state
// ------------------------------------------------------------------------------------------------

static SOCKET theListenSocket = INVALID_SOCKET;
static SOCKET theClientSocket = INVALID_SOCKET;
#if defined(_WIN32)
static Bool theWinsockStarted = FALSE;
#endif
static Bool theHandshakeDone = FALSE;
static std::vector<char> theIncoming;

/// a world command waiting for the next logic frame; produce, construct, sell and science reply only once they ran
struct PendingCommand
{
	AsciiString line;
	Bool replyWhenRun;
};

/// the verb a reply-when-run order answers as, or NULL for a world command that answers "queued"
static const char *replyWhenRunVerb( ScenarioActionType action )
{
	switch (action)
	{
		case SCENARIO_ACTION_PRODUCE:		return "produce";
		case SCENARIO_ACTION_CONSTRUCT:	return "construct";
		case SCENARIO_ACTION_SELL:			return "sell";
		case SCENARIO_ACTION_SCIENCE:		return "science";
		default:												return NULL;
	}
}
static std::vector<PendingCommand> thePendingCommands;
/* What arrives while a step runs waits here until the step is answered.  A step can hand the pass back
	 between its frames (GameEngine's lockstep burst), so a command queued straight away would land on a
	 frame that depends on how fast the machine went. */
static std::vector<PendingCommand> theHeldCommands;
static Bool theQuitRequested = FALSE;

/// step: the logic runs only the frames a step asks for (GameEngine asks ControlServer_stepFramesOwed)
static Bool theLockstep = FALSE;
static Bool theStepPending = FALSE;			///< a step is running and owes its reply
static UnsignedInt theStepTarget = 0;		///< the frame that step ends on
static const Int STEP_MAX_FRAMES = 300;
/// objects lists at most this many, then says truncated
static const Int OBJECTS_MAX = 600;
static UnsignedInt theSaveAtFrame = 0;		///< saveat: the first poll at or past this frame saves, 0 for none
static AsciiString theSaveAtName;
static Bool theNetAgainPending = FALSE;	///< netagain: clear this -netgame match, then start the next one
static Bool theNetAgainCleared = FALSE;	///< and the clear has been posted

static void closeClient( void )
{
	if (theClientSocket != INVALID_SOCKET)
	{
		closesocket( theClientSocket );
		theClientSocket = INVALID_SOCKET;
	}
	theHandshakeDone = FALSE;
	theIncoming.clear();
}

static Bool setNonBlocking( SOCKET s )
{
#if !defined(_WIN32)
	const int flags = fcntl( s, F_GETFL, 0 );
	return flags != -1 && fcntl( s, F_SETFL, flags | O_NONBLOCK ) == 0;
#else
	unsigned long nonBlocking = 1;
	return ioctlsocket( s, FIONBIO, &nonBlocking ) == 0;
#endif
}

static Bool openListenSocket( Int port )
{
#if defined(_WIN32)
	WSADATA wsaData;
	if (!theWinsockStarted)
	{
		if (WSAStartup( MAKEWORD( 1, 1 ), &wsaData ) != 0)
		{
			DEBUG_LOG(("CONTROL: winsock would not start\n"));
			return FALSE;
		}
		theWinsockStarted = TRUE;
	}
#endif

	theListenSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if (theListenSocket == INVALID_SOCKET)
	{
		DEBUG_LOG(("CONTROL: no socket\n"));
		return FALSE;
	}

	sockaddr_in address;
	memset( &address, 0, sizeof( address ) );
	address.sin_family = AF_INET;
	address.sin_port = htons( (unsigned short)port );
	address.sin_addr.s_addr = htonl( CONTROL_LOOPBACK_ONLY );

#if !defined(_WIN32)
	/* POSIX only, and on purpose.  Here SO_REUSEADDR only lets bind() reuse a port still in
		 TIME_WAIT from the last session; a second live listener is still refused, so "loopback only,
		 therefore no authentication" holds.  Without it a restart within a minute or two gets
		 EADDRINUSE, and the poll below then turns -control off for the whole run.  On Windows the
		 same option lets another process bind over a live listener, so it is not set there. */
	const int reuse = 1;
	setsockopt( theListenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
#endif

	if (bind( theListenSocket, (sockaddr *)&address, sizeof( address ) ) != 0
			|| listen( theListenSocket, CONTROL_BACKLOG ) != 0
			|| !setNonBlocking( theListenSocket ))
	{
		DEBUG_LOG(("CONTROL: cannot listen on 127.0.0.1:%d\n", port));
		closesocket( theListenSocket );
		theListenSocket = INVALID_SOCKET;
		return FALSE;
	}

	DEBUG_LOG(("CONTROL: listening on ws://127.0.0.1:%d\n", port));
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// websocket framing
// ------------------------------------------------------------------------------------------------

static void sendFrame( unsigned char opcode, const char *payload, Int length )
{
	if (theClientSocket == INVALID_SOCKET)
		return;

	std::vector<char> frame;
	frame.push_back( (char)(0x80 | opcode) );

	// a server frame is never masked, so the length is the whole second byte
	if (length < 126)
	{
		frame.push_back( (char)length );
	}
	else if (length < 65536)
	{
		frame.push_back( (char)126 );
		frame.push_back( (char)((length >> 8) & 0xFF) );
		frame.push_back( (char)(length & 0xFF) );
	}
	else
	{
		frame.push_back( (char)127 );
		for( Int i = 0; i < 4; ++i )
			frame.push_back( (char)0 );
		frame.push_back( (char)((length >> 24) & 0xFF) );
		frame.push_back( (char)((length >> 16) & 0xFF) );
		frame.push_back( (char)((length >> 8) & 0xFF) );
		frame.push_back( (char)(length & 0xFF) );
	}

	for( Int i = 0; i < length; ++i )
		frame.push_back( payload[ i ] );

	// The socket is non-blocking and an objects reply runs to a hundred KB, more than a send buffer
	// takes at once: a short send would cut the frame and wreck every frame after it, so wait the
	// buffer out (a second at most per stall, the client is on this machine) and drop the client if not.
	Int sent = 0;
	while (sent < (Int)frame.size())
	{
		const Int n = (Int)send( theClientSocket, &frame[ sent ], (Int)frame.size() - sent, CONTROL_SEND_FLAGS );
		if (n > 0)
		{
			sent += n;
			continue;
		}
#if !defined(_WIN32)
		const Bool wouldBlock = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
#else
		const Bool wouldBlock = n < 0 && WSAGetLastError() == WSAEWOULDBLOCK;
#endif
		fd_set writable;
		FD_ZERO( &writable );
		FD_SET( theClientSocket, &writable );
		timeval wait = { 1, 0 };
		if (!wouldBlock || select( (int)theClientSocket + 1, NULL, &writable, NULL, &wait ) <= 0)
		{
			DEBUG_LOG(("CONTROL: could not send a %d byte reply, closing\n", (Int)frame.size()));
			closeClient();
			return;
		}
	}
}

static void sendText( const char *text )
{
	sendFrame( WS_OPCODE_TEXT, text, (Int)strlen( text ) );
}

/** Pull one complete frame off the front of theIncoming.  Returns FALSE when there is not a whole
	  one there yet, which is the normal case on a non-blocking read. */
static Bool takeFrame( unsigned char *opcodeOut, AsciiString *payloadOut )
{
	const Int have = (Int)theIncoming.size();
	if (have < 2)
		return FALSE;

	const unsigned char byte0 = (unsigned char)theIncoming[ 0 ];
	const unsigned char byte1 = (unsigned char)theIncoming[ 1 ];
	const Bool masked = (byte1 & 0x80) != 0;
	Int payloadLength = byte1 & 0x7F;
	Int at = 2;

	if (payloadLength == 126)
	{
		if (have < at + 2)
			return FALSE;
		payloadLength = ((unsigned char)theIncoming[ at ] << 8) | (unsigned char)theIncoming[ at + 1 ];
		at += 2;
	}
	else if (payloadLength == 127)
	{
		// the four high bytes of a 64-bit length are refused rather than truncated: nothing this
		// socket accepts is four gigabytes long, so a frame claiming to be is a broken client
		if (have < at + 8)
			return FALSE;
		for( Int i = 0; i < 4; ++i )
		{
			if (theIncoming[ at + i ] != 0)
			{
				closeClient();
				return FALSE;
			}
		}
		payloadLength = ((unsigned char)theIncoming[ at + 4 ] << 24)
									| ((unsigned char)theIncoming[ at + 5 ] << 16)
									| ((unsigned char)theIncoming[ at + 6 ] << 8)
									| ((unsigned char)theIncoming[ at + 7 ]);
		at += 8;
	}

	unsigned char mask[ 4 ] = { 0, 0, 0, 0 };
	if (masked)
	{
		if (have < at + 4)
			return FALSE;
		for( Int i = 0; i < 4; ++i )
			mask[ i ] = (unsigned char)theIncoming[ at + i ];
		at += 4;
	}

	if (payloadLength < 0 || have < at + payloadLength)
		return FALSE;

	AsciiString payload;
	if (payloadLength > 0)
	{
		std::vector<char> text;
		text.resize( payloadLength + 1 );
		for( Int i = 0; i < payloadLength; ++i )
			text[ i ] = (char)((unsigned char)theIncoming[ at + i ] ^ (masked ? mask[ i % 4 ] : 0));
		text[ payloadLength ] = 0;
		payload.set( &text[ 0 ] );
	}

	theIncoming.erase( theIncoming.begin(), theIncoming.begin() + at + payloadLength );

	*opcodeOut = byte0 & 0x0F;
	*payloadOut = payload;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// the http upgrade
// ------------------------------------------------------------------------------------------------

/** Read one header's value out of the request; FALSE when the header is absent.  `name` is lower case
	  with its colon.  Case-insensitive on the name, because RFC 7230 says header names are, and browsers
	  and Python libraries disagree about the casing; matched only at the start of a line, so the path in
	  the request line, which a web page chooses, cannot pass for a header. */
static Bool findHeader( const char *request, const char *name, char *valueOut, Int valueOutSize )
{
	const Int nameLength = (Int)strlen( name );

	for( const char *at = request; *at; ++at )
	{
		if (at != request && at[ -1 ] != '\n')
			continue;

		Int i = 0;
		while (i < nameLength && at[ i ] && tolower( (unsigned char)at[ i ] ) == name[ i ])
			++i;
		if (i < nameLength)
			continue;

		const char *value = at + nameLength;
		while (*value == ' ' || *value == '\t')
			++value;

		Int written = 0;
		while (*value && *value != '\r' && *value != '\n' && written < valueOutSize - 1)
			valueOut[ written++ ] = *value++;
		while (written > 0 && (valueOut[ written - 1 ] == ' ' || valueOut[ written - 1 ] == '\t'))
			--written;
		valueOut[ written ] = 0;
		return TRUE;
	}

	return FALSE;
}

/* A browser names the page that opened the socket in Origin, and any web page can open ws://127.0.0.1.
	 So a request with an Origin is let in only from this machine's own pages.  "null" is refused: a
	 sandboxed iframe on any web site sends it.  The project's tools (control_client.py, Node's
	 WebSocket) send no Origin at all. */
Bool ControlServer_originAllowed( const char *request )
{
	char origin[ 256 ];
	if (!findHeader( request, "origin:", origin, sizeof( origin ) ))
		return TRUE;

	static const char *const LOCAL[] = { "http://localhost", "https://localhost", "http://127.0.0.1", "https://127.0.0.1", "http://[::1]", "https://[::1]" };
	for( Int k = 0; k < (Int)ARRAY_SIZE( LOCAL ); ++k )
	{
		const size_t length = strlen( LOCAL[ k ] );
		if (strncmp( origin, LOCAL[ k ], length ) != 0)
			continue;
		const char *port = origin + length;
		if (*port == 0)
			return TRUE;
		if (*port++ != ':' || *port == 0)
			continue;
		while (*port >= '0' && *port <= '9')
			++port;
		if (*port == 0)
			return TRUE;
	}
	return FALSE;
}

static Bool tryHandshake( void )
{
	// the request ends at the blank line; until then there is nothing to answer
	theIncoming.push_back( 0 );
	const char *request = &theIncoming[ 0 ];
	const char *end = strstr( request, "\r\n\r\n" );
	theIncoming.pop_back();

	if (end == NULL)
		return FALSE;

	if (!ControlServer_originAllowed( request ))
	{
		DEBUG_LOG(("CONTROL: a web page's request (Origin not this machine), closing\n"));
		closeClient();
		return FALSE;
	}

	char clientKey[ 256 ];
	char acceptKey[ 64 ];
	if (!findHeader( request, "sec-websocket-key:", clientKey, sizeof( clientKey ) ) || clientKey[ 0 ] == 0
			|| !ControlServer_computeAcceptKey( clientKey, acceptKey, sizeof( acceptKey ) ))
	{
		DEBUG_LOG(("CONTROL: not a websocket request, closing\n"));
		closeClient();
		return FALSE;
	}

	char reply[ 512 ];
	sprintf( reply,
					 "HTTP/1.1 101 Switching Protocols\r\n"
					 "Upgrade: websocket\r\n"
					 "Connection: Upgrade\r\n"
					 "Sec-WebSocket-Accept: %s\r\n\r\n",
					 acceptKey );
	send( theClientSocket, reply, (Int)strlen( reply ), CONTROL_SEND_FLAGS );

	const Int consumed = (Int)(end - request) + 4;
	theIncoming.erase( theIncoming.begin(), theIncoming.begin() + consumed );
	theHandshakeDone = TRUE;
	DEBUG_LOG(("CONTROL: client connected\n"));
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// commands
// ------------------------------------------------------------------------------------------------

static void replyOk( const char *extra )
{
	char reply[ 1024 ];
	if (extra && *extra)
		sprintf( reply, "{\"ok\":true,%s}", extra );
	else
		sprintf( reply, "{\"ok\":true}" );
	sendText( reply );
}

static void replyError( const char *why )
{
	char reply[ 512 ];
	sprintf( reply, "{\"ok\":false,\"error\":\"%s\"}", why );
	sendText( reply );
}

/** {"ok":..,"cmd":"<verb>",<members>}, of any length: a std::string, since AsciiString throws past 32 KB
	  and a late army's objects reply runs well past that. */
static void replyMembers( Bool ok, const char *verb, const char *members )
{
	std::string reply( ok ? "{\"ok\":true,\"cmd\":\"" : "{\"ok\":false,\"cmd\":\"" );
	reply += verb;
	reply += "\"";
	if (members && *members)
	{
		reply += ",";
		reply += members;
	}
	reply += "}";
	sendFrame( WS_OPCODE_TEXT, reply.c_str(), (Int)reply.size() );
}

static Bool isMatchRunning( void )
{
	return TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isLoadingMap();
}

Bool ControlServer_isLockstep( void )
{
	return theLockstep;
}

Int ControlServer_stepFramesOwed( void )
{
	if (!theLockstep || !theStepPending || TheGameLogic == NULL)
		return 0;
	const UnsignedInt frame = TheGameLogic->getFrame();
	return theStepTarget > frame ? (Int)(theStepTarget - frame) : 0;
}

/** Once a pass: answer the step whose frames have run, and let go of lockstep, and of the produce and
	  construct orders still waiting, when the match they were for has gone. */
static void dropCommands( std::vector<PendingCommand> &commands )
{
	for (std::vector<PendingCommand>::const_iterator it = commands.begin(); it != commands.end(); ++it)
	{
		if (it->replyWhenRun)
		{
			const std::string line( it->line.str() );
			replyMembers( FALSE, line.substr( 0, line.find( ' ' ) ).c_str(), "\"error\":\"the match ended before the order ran\"" );
		}
	}
	commands.clear();
}

/** The commands held during a step go to the next logic frame, behind any already waiting. */
static void releaseHeldCommands( void )
{
	thePendingCommands.insert( thePendingCommands.end(), theHeldCommands.begin(), theHeldCommands.end() );
	theHeldCommands.clear();
}

static void updateLockstep( void )
{
	if (!isMatchRunning() && !(TheGameLogic && TheGameLogic->isLoadingMap()))
	{
		dropCommands( thePendingCommands );
		dropCommands( theHeldCommands );
		if (theLockstep)
			DEBUG_LOG(("CONTROL: the match ended, lockstep released\n"));
		theLockstep = FALSE;
		if (theStepPending)
			replyMembers( FALSE, "step", "\"error\":\"the match ended during the step; lockstep released\"" );
		theStepPending = FALSE;
		return;
	}

	if (theStepPending && TheGameLogic->getFrame() >= theStepTarget)
	{
		theStepPending = FALSE;
		releaseHeldCommands();
		char members[ 64 ];
		sprintf( members, "\"frame\":%u", TheGameLogic->getFrame() );
		replyMembers( TRUE, "step", members );
	}
}

/** Share of the map, in percent, the player can see right now, from a grid of samples.  Fogged
	ground, seen once and not watched any more, does not count; a skirmish starts with the whole map
	fogged rather than black. */
static Int revealedPercent( const Player *player )
{
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );

	Int seen = 0;
	for( Int row = 0; row < STATUS_SHROUD_SAMPLES_PER_SIDE; ++row )
	{
		for( Int column = 0; column < STATUS_SHROUD_SAMPLES_PER_SIDE; ++column )
		{
			Coord3D sample;
			sample.x = extent.lo.x + extent.width() * (column + 0.5f) / STATUS_SHROUD_SAMPLES_PER_SIDE;
			sample.y = extent.lo.y + extent.height() * (row + 0.5f) / STATUS_SHROUD_SAMPLES_PER_SIDE;
			sample.z = 0.0f;
			if (ThePartitionManager->getShroudStatusForPlayer( player->getPlayerIndex(), &sample ) == CELLSHROUD_CLEAR)
				++seen;
		}
	}
	return seen * 100 / (STATUS_SHROUD_SAMPLES_PER_SIDE * STATUS_SHROUD_SAMPLES_PER_SIDE);
}

/** frame number, whether a match is running, and what each player owns, with enough of each
	player's state that every console cheat can be seen to have landed: tools/cheat_check.py. */
/* A network match a lobby started, LAN or online.  This machine plays one seat of it, so a world command
	 would split the match, and the census, tally and spectator views would read through the fog: any
	 player can pass -control.  A -netgame match is the harnesses' own, and so is a match on an online
	 server started for a test (OnlineIsTestServer), where online_check.py forces its desync.  A Play vs AI
	 match is guarded on every server, a test one included: the server rates it on this machine's word, and
	 nothing needs to bend it. */
static Bool isGuardedMatch( void )
{
	if (!TheGameLogic)
		return FALSE;
	if (TheGameLogic->isInSkirmishGame() && OnlineIsAIMatchLive())
		return TRUE;
	return TheGameLogic->isInMultiplayerGame() && !TheGlobalData->m_netGameStarted && !OnlineIsTestServer();
}

static void replyStatus( void )
{
	const Bool inGame = TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
	const Bool ownSeatOnly = isGuardedMatch();

	std::vector<char> reply;
	char piece[ 512 ];

	// which front-end screen is up, so a script driving the menus can tell where its clicks landed
	WindowLayout *screen = TheShell ? TheShell->top() : NULL;
	WindowLayout *options = TheShell ? TheShell->getOptionsLayout( FALSE ) : NULL;
	// where the tactical camera looks, so a script can tell whether a key or button moved it
	Coord3D camera = { 0.0f, 0.0f, 0.0f };
	if (TheTacticalView)
		TheTacticalView->getPosition( &camera );
	sprintf( piece, "{\"ok\":true,\"frame\":%d,\"inGame\":%s,\"screen\":\"%s\",\"options\":%s,\"camera\":[%.1f,%.1f],\"players\":[",
					 TheGameLogic ? (Int)TheGameLogic->getFrame() : 0,
					 inGame ? "true" : "false",
					 screen ? screen->getFilename().str() : "",
					 options && !options->isHidden() ? "true" : "false",
					 camera.x, camera.y );
	for( const char *at = piece; *at; ++at )
		reply.push_back( *at );

	if (inGame && ThePlayerList)
	{
		Bool first = TRUE;
		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *player = ThePlayerList->getNthPlayer( i );
			if (player == NULL || (ownSeatOnly && player != ThePlayerList->getLocalPlayer()))
				continue;

			Int units = 0;
			Int heroic = 0;
			Int hurt = 0;
			Int powersReady = 0;
			for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			{
				if (obj->getControllingPlayer() != player || obj->isEffectivelyDead())
					continue;

				++units;
				if (obj->getVeterancyLevel() == LEVEL_HEROIC)
					++heroic;
				const BodyModuleInterface *body = obj->getBodyModule();
				if (body && body->getHealth() < body->getMaxHealth())
					++hurt;
				for( BehaviorModule **module = obj->getBehaviorModules(); *module; ++module )
				{
					SpecialPowerModuleInterface *power = (*module)->getSpecialPower();
					if (power && power->isReady())
						++powersReady;
				}
			}

			UnsignedInt cheats = 0;
			for( Int kind = 0; kind < CHEAT_KIND_COUNT; ++kind )
			{
				if (player->hasCheat( (CheatKind)kind ))
					cheats |= 1 << kind;
			}

			sprintf( piece, "%s{\"index\":%d,\"slot\":%d,\"money\":%u,\"units\":%d,\"points\":%d,\"rank\":%d,"
							 "\"cheats\":%u,\"powered\":%s,\"heroic\":%d,\"hurt\":%d,\"powersReady\":%d,\"revealed\":%d}",
							 first ? "" : ",", i, ThePlayerList->getSlotIndex( i ),
							 player->getMoney()->countMoney(), units, player->getSciencePurchasePoints(),
							 player->getRankLevel(), cheats,
							 player->getEnergy()->hasSufficientPower() ? "true" : "false",
							 heroic, hurt, powersReady, revealedPercent( player ) );
			for( const char *at = piece; *at; ++at )
				reply.push_back( *at );
			first = FALSE;
		}
	}

	reply.push_back( ']' );
	reply.push_back( '}' );
	reply.push_back( 0 );
	sendText( &reply[ 0 ] );
}

/** The save menu's Save into <name>.sav; TRUE when the file was written. */
static Bool saveTo( const AsciiString &name )
{
	AsciiString filename;
	filename.format( "%s.sav", name.str() );
	UnicodeString description;
	description.translate( name );
	return TheGameState->saveGame( filename, description, SAVE_FILE_TYPE_NORMAL ) == SC_OK;
}

// ------------------------------------------------------------------------------------------------
// the shell's windows, for Tools/ui_walk.py: what is on the screen and where a click would go
// ------------------------------------------------------------------------------------------------

static const Int UI_TREE_MAX = 3000;

static void appendJsonChar( std::string &out, UnsignedInt c )
{
	if (c == '"' || c == '\\')
	{
		out += '\\';
		out += (char)c;
	}
	else if (c < 0x20 || c >= 0x7F)
	{
		char hex[ 8 ];
		snprintf( hex, sizeof( hex ), "\\u%04x", c > 0xFFFF ? 0xFFFD : c );
		out += hex;
	}
	else
		out += (char)c;
}

static void appendJsonString( std::string &out, const char *text )
{
	out += '"';
	for( const unsigned char *at = (const unsigned char *)text; at && *at; ++at )
		appendJsonChar( out, *at );
	out += '"';
}

static void appendJsonString( std::string &out, const UnicodeString &text )
{
	out += '"';
	for( const WideChar *at = text.str(); at && *at; ++at )
		appendJsonChar( out, (UnsignedInt)*at );
	out += '"';
}

static const char *uiWindowName( GameWindow *window )
{
	if (window == NULL)
		return "";
	if (BitTest( window->winGetStatus(), WIN_STATUS_DESTROYED ))
		return "DESTROYED";
	return window->winGetInstanceData()->m_decoratedNameString.str();
}

static const char *uiGadgetKind( GameWindow *window )
{
	const UnsignedInt style = window->winGetStyle();
	if (style & GWS_COMBO_BOX)				return "combo";
	if (style & GWS_SCROLL_LISTBOX)		return "list";
	if (style & GWS_PUSH_BUTTON)			return "button";
	if (style & GWS_CHECK_BOX)				return "check";
	if (style & GWS_RADIO_BUTTON)			return "radio";
	if (style & GWS_HORZ_SLIDER)			return "hslider";
	if (style & GWS_VERT_SLIDER)			return "vslider";
	if (style & GWS_ENTRY_FIELD)			return "entry";
	if (style & GWS_TAB_CONTROL)			return "tab";
	if (style & GWS_STATIC_TEXT)			return "static";
	if (style & GWS_PROGRESS_BAR)			return "progress";
	return "window";
}

/** The text's size in the window's own font: one line, or wrapped at wrap pixels when wrap is above 0. */
static void uiMeasureText( GameWindow *window, const UnicodeString &text, Int wrap, Int *width, Int *height )
{
	*width = 0;
	*height = 0;
	GameFont *font = window->winGetFont();
	if (text.isEmpty() || font == NULL || TheDisplayStringManager == NULL)
		return;
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	if (string == NULL)
		return;
	string->setFont( font );
	string->setUseHotkey( BitTest( window->winGetStatus(), WIN_STATUS_HOTKEY_TEXT ) );
	if (wrap > 0)
		string->setWordWrap( wrap );
	string->setText( text );
	string->getSize( width, height );
	TheDisplayStringManager->freeDisplayString( string );
}

/** Who holds the pointer and the keyboard, and which screen is up: the WINOWNERS line, as JSON members. */
static void appendUiState( std::string &out )
{
	char piece[ 256 ];
	WindowLayout *screen = TheShell ? TheShell->top() : NULL;
	snprintf( piece, sizeof( piece ), "\"frame\":%u,\"display\":[%d,%d],\"anim\":%d,\"screens\":%d,\"screen\":",
						TheGameClient ? TheGameClient->getFrame() : 0,
						TheDisplay ? (Int)TheDisplay->getWidth() : 0, TheDisplay ? (Int)TheDisplay->getHeight() : 0,
						TheShell && !TheShell->isAnimFinished() ? 1 : 0, TheShell ? TheShell->getScreenCount() : 0 );
	out += piece;
	appendJsonString( out, screen ? screen->getFilename().str() : "" );
	out += ",\"modal\":";
	appendJsonString( out, uiWindowName( TheWindowManager->winGetModalWindow() ) );
	/* the lone window outlives its list: a pick hides the list (HideListBox) and the pointer is let go only
		 by the next release elsewhere, as in retail, so an open drop-down is a lone combo box whose list shows */
	GameWindow *lone = TheWindowManager->winGetLoneWindow();
	if (lone && BitTest( lone->winGetStyle(), GWS_COMBO_BOX ) && lone->winGetUserData())
	{
		GameWindow *list = ((ComboBoxData *)lone->winGetUserData())->listBox;
		if (list == NULL || list->winIsHidden())
			lone = NULL;
	}
	out += ",\"lone\":";
	appendJsonString( out, uiWindowName( lone ) );
	out += ",\"captor\":";
	appendJsonString( out, uiWindowName( TheWindowManager->winGetCapture() ) );
	out += ",\"grab\":";
	appendJsonString( out, uiWindowName( TheWindowManager->winGetGrabWindow() ) );
	out += ",\"focus\":";
	appendJsonString( out, uiWindowName( TheWindowManager->winGetFocus() ) );
}

/** One visible window and then its visible children, front first as the hit test asks them: index,
	parent index, depth, screen rect, kind, enabled with its parents, takes no input, has an input
	callback of its own (a drawn screen's hot spots live behind one), name, text with its
	one-line size and its height wrapped at the window's width, and the gadget's value. */
static void describeUiWindow( std::string &out, GameWindow *window, Int parent, Int depth, Int *count, Bool withHidden )
{
	if ((!withHidden && window->winIsHidden()) || BitTest( window->winGetStatus(), WIN_STATUS_DESTROYED ) || *count >= UI_TREE_MAX)
		return;
	const Int index = (*count)++;
	Int x = 0, y = 0, w = 0, h = 0;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &w, &h );
	const char *kind = uiGadgetKind( window );

	char piece[ 256 ];
	snprintf( piece, sizeof( piece ), "%s{\"i\":%d,\"p\":%d,\"d\":%d,\"r\":[%d,%d,%d,%d],\"t\":\"%s\",\"e\":%d,\"ni\":%d,\"in\":%d,\"n\":",
						index > 0 ? "," : "", index, parent, depth, x, y, w, h, kind,
						TheWindowManager->isEnabled( window ) ? 1 : 0,
						BitTest( window->winGetStatus(), WIN_STATUS_NO_INPUT ) ? 1 : 0,
						window->winGetInputFunc() != TheWindowManager->getDefaultInput() ? 1 : 0 );
	out += piece;
	appendJsonString( out, uiWindowName( window ) );

	// an entry keeps what was typed in its own string, not in the window's text
	const UnicodeString text = BitTest( window->winGetStyle(), GWS_ENTRY_FIELD ) ? GadgetTextEntryGetText( window ) : window->winGetText();
	if (!text.isEmpty())
	{
		Int lineW = 0, lineH = 0, wrapW = 0, wrapH = 0;
		uiMeasureText( window, text, 0, &lineW, &lineH );
		uiMeasureText( window, text, w, &wrapW, &wrapH );
		out += ",\"tx\":";
		appendJsonString( out, text );
		snprintf( piece, sizeof( piece ), ",\"tw\":%d,\"th\":%d,\"wh\":%d", lineW, lineH, wrapH );
		out += piece;
	}

	piece[ 0 ] = 0;
	const UnsignedInt style = window->winGetStyle();
	if (style & GWS_COMBO_BOX)
	{
		Int selected = -1;
		GadgetComboBoxGetSelectedPos( window, &selected );
		const ComboBoxData *combo = (const ComboBoxData *)window->winGetUserData();
		snprintf( piece, sizeof( piece ), ",\"v\":%d,\"rows\":%d", selected, combo ? combo->entryCount : 0 );
	}
	else if (style & GWS_SCROLL_LISTBOX)
	{
		const ListboxData *list = (const ListboxData *)window->winGetUserData();
		if (list)
			snprintf( piece, sizeof( piece ), ",\"v\":%d,\"rows\":%d,\"sc\":%d,\"sh\":%d,\"dh\":%d",
								list->selectPos, (Int)list->endPos, list->displayPos, list->totalHeight, list->displayHeight );
	}
	else if (style & GWS_ALL_SLIDER)
		snprintf( piece, sizeof( piece ), ",\"v\":%d", GadgetSliderGetPosition( window ) );
	else if (style & (GWS_CHECK_BOX | GWS_RADIO_BUTTON | GWS_PUSH_BUTTON))
		snprintf( piece, sizeof( piece ), ",\"v\":%d", BitTest( window->winGetInstanceData()->m_state, WIN_STATE_SELECTED ) ? 1 : 0 );
	out += piece;
	if (withHidden && TheWindowManager->isHidden( window ))
		out += ",\"h\":1";
	out += '}';

	for( GameWindow *child = window->winGetChild(); child; child = child->winGetNext() )
		describeUiWindow( out, child, index, depth + 1, count, withHidden );
}

/** A top-level window holds the point: GameWindowManager.cpp's topWindowHolds. */
static Bool uiTopWindowHolds( GameWindow *window, Int x, Int y )
{
	Int left = 0, top = 0, w = 0, h = 0;
	window->winGetScreenPosition( &left, &top );
	window->winGetSize( &w, &h );
	if (x >= left && x <= left + w && y >= top && y <= top + h)
		return TRUE;
	return TheControlBar != NULL && TheControlBar->holdsRaisedRadarPoint( window, x, y );
}

/** Where a left click at that pixel goes, between presses: GameWindowManager::winProcessMouseEvent's
	search step for step (captor, open drop-down, modal box, then the window list above, level and
	below), then its rule that a window taking no input hands the click to the combo box it is part of,
	or to nobody.  getWindowUnderCursor is not it: it drops a combo box's own edit box. */
static GameWindow *uiInputTarget( Int x, Int y )
{
	GameWindow *window = NULL;
	GameWindow *captor = TheWindowManager->winGetCapture();
	if (captor)
		window = captor->winPointInChild( x, y );
	else
	{
		GameWindow *lone = TheWindowManager->winGetLoneWindow();
		if (lone && !lone->winIsHidden())
		{
			Int left = 0, top = 0, w = 0, h = 0;
			lone->winGetScreenPosition( &left, &top );
			lone->winGetSize( &w, &h );
			if (x >= left && x <= left + w && y >= top && y <= top + h)
				window = lone->winPointInChild( x, y );
		}
		GameWindow *modal = TheWindowManager->winGetModalWindow();
		if (window == NULL && modal)
			window = modal->winPointInChild( x, y );
		else if (window == NULL)
		{
			for (Int pass = 0; pass < 3 && window == NULL; ++pass)
			{
				for (GameWindow *top = TheWindowManager->winGetWindowList(); top; top = top->winGetNext())
				{
					const UnsignedInt status = top->winGetStatus();
					const Bool level = pass == 0 ? BitTest( status, WIN_STATUS_ABOVE ) != 0
						: (pass == 1 ? !BitTest( status, WIN_STATUS_ABOVE | WIN_STATUS_BELOW ) : BitTest( status, WIN_STATUS_BELOW ) != 0);
					if (!level || BitTest( status, WIN_STATUS_HIDDEN ) || !uiTopWindowHolds( top, x, y ))
						continue;
					if (BitTest( status, WIN_STATUS_ENABLED ))
					{
						window = top->winPointInChild( x, y );
						break;
					}
				}
			}
		}
	}
	if (window && BitTest( window->winGetStatus(), WIN_STATUS_NO_INPUT ))
	{
		GameWindow *parent = window->winGetParent();
		window = parent && BitTest( parent->winGetStyle(), GWS_COMBO_BOX ) ? parent : NULL;
	}
	return window;
}

/** {"x":..,"y":..,"hit":"<name>","chain":[<names up to the top window>]} */
static void appendUiHit( std::string &out, Int x, Int y )
{
	char piece[ 64 ];
	snprintf( piece, sizeof( piece ), "{\"x\":%d,\"y\":%d,\"hit\":", x, y );
	out += piece;
	GameWindow *window = uiInputTarget( x, y );
	appendJsonString( out, uiWindowName( window ) );
	out += ",\"chain\":[";
	for( GameWindow *at = window; at; at = at->winGetParent() )
	{
		if (at != window)
			out += ',';
		appendJsonString( out, uiWindowName( at ) );
	}
	out += "]}";
}

/** The window of that layout name (MainMenu.wnd:ButtonOnline) and its centre on screen, or an offset
	from its top left corner when one is given. */
static GameWindow *uiFindWindow( const char *name, Int *x, Int *y, Bool offset )
{
	GameWindow *window = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( AsciiString( name ) ) );
	if (window == NULL)
		return NULL;
	Int left = 0, top = 0, w = 0, h = 0;
	window->winGetScreenPosition( &left, &top );
	window->winGetSize( &w, &h );
	if (offset)
	{
		*x += left;
		*y += top;
	}
	else
	{
		*x = left + w / 2;
		*y = top + h / 2;
	}
	return window;
}

/** A mouse message the way Mouse::createStreamMessages builds one: the pixel and the modifiers, and for a
	button the time too (0, which every click-or-drag test reads as a quick click). */
static void appendUiMouseButton( GameMessage::Type type, Int x, Int y, Bool button = TRUE )
{
	ICoord2D pixel;
	pixel.x = x;
	pixel.y = y;
	GameMessage *msg = TheMessageStream->appendMessage( type );
	msg->appendPixelArgument( pixel );
	msg->appendIntegerArgument( KEY_STATE_NONE );
	if (button)
		msg->appendIntegerArgument( 0 );
}

/* uitree, uihit, uiclick, uiclickat, uidrag, uirclick, uihover, uiscroll, uirow and uitype: the shell's windows for a
	 script that clicks every control and checks what happened (Tools/ui_walk.py).  The clicks, the wheel
	 and the pointer go down the message stream the way Mouse.cpp sends a real one, so a modal box, a
	 captured pointer or an open drop-down takes them exactly as it would take the player's; uihit asks
	 where a click at a pixel would go without sending one.  Client side only: nothing here is a logic
	 message. */
static Bool handleUiCommand( const AsciiString &command )
{
	const char *line = command.str();
	if (strncmp( line, "ui", 2 ) != 0 || TheWindowManager == NULL)
		return FALSE;

	// uitree [all]: all keeps the hidden windows too, marked "h", for a drawn screen that works its hidden boxes
	if (command == "uitree" || command == "uitree all" || command == "uistate")
	{
		const Bool withHidden = command == "uitree all";
		std::string out( "{\"ok\":true,\"cmd\":\"" );
		out += command == "uistate" ? "uistate" : "uitree";
		out += "\",";
		appendUiState( out );
		if (command != "uistate")
		{
			out += ",\"windows\":[";
			Int count = 0;
			for( GameWindow *window = TheWindowManager->winGetWindowList(); window; window = window->winGetNext() )
				describeUiWindow( out, window, -1, 0, &count, withHidden );
			out += "]";
		}
		out += "}";
		sendFrame( WS_OPCODE_TEXT, out.c_str(), (Int)out.size() );
		return TRUE;
	}

	// uihit <x> <y> [<x> <y> ...]: a grid in one round trip
	if (strncmp( line, "uihit ", 6 ) == 0)
	{
		std::string out( "{\"ok\":true,\"cmd\":\"uihit\",\"hits\":[" );
		const char *at = line + 6;
		Int x = 0, y = 0, used = 0, hits = 0;
		while (sscanf( at, "%d %d%n", &x, &y, &used ) == 2)
		{
			if (hits++ > 0)
				out += ',';
			appendUiHit( out, x, y );
			at += used;
		}
		out += "]}";
		if (hits == 0)
			replyMembers( FALSE, "uihit", "\"error\":\"uihit <x> <y> [<x> <y> ...]\"" );
		else
			sendFrame( WS_OPCODE_TEXT, out.c_str(), (Int)out.size() );
		return TRUE;
	}

	/* uidrag <name> <dx0> <dy0> <dx1> <dy1>: press at the first point from the window's top left, drag to the
		 second in eight steps and let go there, as Mouse.cpp sends a held button moving (a fader, a slider's
		 thumb, a scroll bar) */
	if (strncmp( line, "uidrag ", 7 ) == 0)
	{
		char name[ 256 ] = "";
		Int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (sscanf( line + 7, "%255s %d %d %d %d", name, &x0, &y0, &x1, &y1 ) != 5)
		{
			replyMembers( FALSE, "uidrag", "\"error\":\"uidrag <name> <dx0> <dy0> <dx1> <dy1> from its top left\"" );
			return TRUE;
		}
		Int left = 0, top = 0;
		if (uiFindWindow( name, &left, &top, TRUE ) == NULL)
		{
			replyMembers( FALSE, "uidrag", "\"error\":\"no such window\"" );
			return TRUE;
		}
		const Int startX = left + x0, startY = top + y0;
		std::string members( "\"at\":" );
		appendUiHit( members, startX, startY );
		appendUiMouseButton( GameMessage::MSG_RAW_MOUSE_POSITION, startX, startY, FALSE );
		appendUiMouseButton( GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, startX, startY, TRUE );
		const Int STEPS = 8;
		ICoord2D was = { startX, startY };
		for (Int step = 1; step <= STEPS; ++step)
		{
			ICoord2D now, delta;
			now.x = left + x0 + (x1 - x0) * step / STEPS;
			now.y = top + y0 + (y1 - y0) * step / STEPS;
			delta.x = now.x - was.x;
			delta.y = now.y - was.y;
			GameMessage *drag = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_LEFT_DRAG );
			drag->appendPixelArgument( now );
			drag->appendPixelArgument( delta );
			drag->appendIntegerArgument( KEY_STATE_NONE );
			was = now;
		}
		appendUiMouseButton( GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, was.x, was.y, TRUE );
		if (TheMouse)
			TheMouse->setPosition( was.x, was.y );
		replyMembers( TRUE, "uidrag", members.c_str() );
		return TRUE;
	}

	// uiclickat <x> <y>: uiclick at a screen pixel, for a list's row (uirow says where) or a drawn screen's part
	if (strncmp( line, "uiclickat ", 10 ) == 0)
	{
		Int x = 0, y = 0;
		if (sscanf( line + 10, "%d %d", &x, &y ) != 2)
		{
			replyMembers( FALSE, "uiclickat", "\"error\":\"uiclickat <x> <y>\"" );
			return TRUE;
		}
		if (TheMouse)
			TheMouse->setPosition( x, y );
		std::string members( "\"at\":" );
		appendUiHit( members, x, y );
		appendUiMouseButton( GameMessage::MSG_RAW_MOUSE_POSITION, x, y, FALSE );
		appendUiMouseButton( GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, x, y, TRUE );
		appendUiMouseButton( GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, x, y, TRUE );
		replyMembers( TRUE, "uiclickat", members.c_str() );
		return TRUE;
	}

	// uiclick|uirclick|uihover|uiscroll <name> ...: the window's centre, or <dx> <dy> from its corner
	const Bool isClick = strncmp( line, "uiclick ", 8 ) == 0;
	const Bool isRightClick = strncmp( line, "uirclick ", 9 ) == 0;
	const Bool isHover = strncmp( line, "uihover ", 8 ) == 0;
	const Bool isScroll = strncmp( line, "uiscroll ", 9 ) == 0;
	if (isClick || isRightClick || isHover || isScroll)
	{
		const char *verb = isClick ? "uiclick" : (isRightClick ? "uirclick" : (isHover ? "uihover" : "uiscroll"));
		char name[ 256 ] = "";
		Int a = 0, b = 0;
		const Int read = sscanf( line + strlen( verb ) + 1, "%255s %d %d", name, &a, &b );
		if (read < 1 || (isScroll && read < 2))
		{
			replyMembers( FALSE, verb, isScroll ? "\"error\":\"uiscroll <name> <notches, + up, - down>\""
				: "\"error\":\"<verb> <name> [<dx> <dy> from its top left]\"" );
			return TRUE;
		}
		Int x = isScroll ? 0 : a, y = isScroll ? 0 : b;
		GameWindow *window = uiFindWindow( name, &x, &y, !isScroll && read == 3 );
		if (window == NULL)
		{
			replyMembers( FALSE, verb, "\"error\":\"no such window\"" );
			return TRUE;
		}
		// the game's own pointer only: the Windows one stays where the owner left it
		if (TheMouse)
			TheMouse->setPosition( x, y );
		/* the pointer arrives first, as a hand's does: the move lights the gadget up (GWM_MOUSE_ENTERING),
			 and a check box or a combo takes a release only while it is lit */
		appendUiMouseButton( GameMessage::MSG_RAW_MOUSE_POSITION, x, y, FALSE );
		if (isClick || isRightClick)
		{
			appendUiMouseButton( isClick ? GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN : GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, x, y );
			appendUiMouseButton( isClick ? GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP : GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, x, y );
		}
		else if (isScroll)
		{
			ICoord2D pixel;
			pixel.x = x;
			pixel.y = y;
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_WHEEL );
			msg->appendPixelArgument( pixel );
			msg->appendRealArgument( (Real)a );
			msg->appendIntegerArgument( KEY_STATE_NONE );
		}
		// where it went, read before the press lands (the next pass takes it)
		std::string members( "\"at\":" );
		appendUiHit( members, x, y );
		replyMembers( TRUE, verb, members.c_str() );
		return TRUE;
	}

	// uirow <list or combo> <row>: the centre of that row's first column on screen, while it shows
	if (strncmp( line, "uirow ", 6 ) == 0)
	{
		char name[ 256 ] = "";
		Int row = 0;
		if (sscanf( line + 6, "%255s %d", name, &row ) != 2)
		{
			replyMembers( FALSE, "uirow", "\"error\":\"uirow <list or combo> <row>\"" );
			return TRUE;
		}
		Int x = 0, y = 0;
		GameWindow *window = uiFindWindow( name, &x, &y, FALSE );
		if (window && BitTest( window->winGetStyle(), GWS_COMBO_BOX ) && window->winGetUserData())
			window = ((ComboBoxData *)window->winGetUserData())->listBox;
		const ListboxData *list = window && BitTest( window->winGetStyle(), GWS_SCROLL_LISTBOX ) ? (const ListboxData *)window->winGetUserData() : NULL;
		if (list == NULL || row < 0 || row >= list->endPos)
		{
			replyMembers( FALSE, "uirow", "\"error\":\"not a list or combo box, or no such row\"" );
			return TRUE;
		}
		// getListboxEntryBasedOnCoord, the other way round
		Int left = 0, top = 0, w = 0, h = 0;
		window->winGetScreenPosition( &left, &top );
		window->winGetSize( &w, &h );
		if (window->winGetInstanceData()->getTextLength())
			top += TheWindowManager->winFontHeight( window->winGetInstanceData()->getFont() ) + 1;
		const Int rowTop = list->listData[ row ].listHeight - list->listData[ row ].height - list->displayPos;
		const Int rowBottom = list->listData[ row ].listHeight - list->displayPos;
		const Int firstColumn = list->columnWidth && list->columnWidth[ 0 ] > 0 ? list->columnWidth[ 0 ] : w;
		char piece[ 128 ];
		snprintf( piece, sizeof( piece ), "\"x\":%d,\"y\":%d,\"shown\":%s,\"tx\":", left + (firstColumn < w ? firstColumn : w) / 2, top + (rowTop + rowBottom) / 2,
							rowTop >= 0 && rowBottom <= list->displayHeight ? "true" : "false" );
		std::string members( piece );
		appendJsonString( members, GadgetListBoxGetText( window, row, 0 ) );
		replyMembers( TRUE, "uirow", members.c_str() );
		return TRUE;
	}

	// uitype <text>: each character to the window holding the keyboard, as IMEManager hands it a WM_CHAR
	if (strncmp( line, "uitype ", 7 ) == 0)
	{
		GameWindow *focus = TheWindowManager->winGetFocus();
		if (focus == NULL)
		{
			replyMembers( FALSE, "uitype", "\"error\":\"no window has the keyboard; uiclick the entry first\"" );
			return TRUE;
		}
		/* the socket's text is UTF-8: each sequence is one character, sent wide as WM_CHAR brings it, so a
			 Turkish c-cedilla typed here reaches the box as one letter, the way the player's keyboard types it */
		Int typed = 0;
		for( const unsigned char *at = (const unsigned char *)line + 7; *at; ++typed )
		{
			UnsignedInt c = *at++;
			Int more = c >= 0xF0 ? 3 : ( c >= 0xE0 ? 2 : ( c >= 0xC0 ? 1 : 0 ) );
			if (more > 0)
				c &= 0x3F >> more;
			for( ; more > 0 && ( *at & 0xC0 ) == 0x80; --more )
				c = ( c << 6 ) | ( *at++ & 0x3F );
			TheWindowManager->winSendInputMsg( focus, GWM_IME_CHAR, (WindowMsgData)( c > 0xFFFF ? 0xFFFD : c ), 0 );
		}
		std::string members( "\"to\":" );
		appendJsonString( members, uiWindowName( focus ) );
		char piece[ 32 ];
		snprintf( piece, sizeof( piece ), ",\"typed\":%d", typed );
		members += piece;
		replyMembers( TRUE, "uitype", members.c_str() );
		return TRUE;
	}

	return FALSE;
}

/** Everything that changes the world is queued; see the header for why. */
static void handleCommand( const AsciiString &command )
{
	if (command.isEmpty())
		return;

	if (command == "ping")
	{
		replyOk( "\"pong\":true" );
		return;
	}

	if (command == "status")
	{
		replyStatus();
		return;
	}

	if (command == "screenshot")
	{
		if (TheDisplay == NULL)
		{
			replyError( "no display" );
			return;
		}
		TheDisplay->takeScreenShot();
		replyOk( "\"screenshot\":\"requested\"" );
		return;
	}

	if (command == "quit")
	{
		// a driven run never reaches the unattended report: net_check reads the census from here
		LogicThread::report();
		theQuitRequested = TRUE;
		// the quit is taken up in a logic frame (ControlServer_runCommands), which lockstep would never run
		theLockstep = FALSE;
		replyOk( "\"quitting\":true" );
		return;
	}

	/* select all: what the SELECT_ALL key does, whatever key the player's command map gives it (Q in
		 retail's; Ctrl+A is the beacon there).  Posted twice because one press takes the screen and the
		 second, with nothing new left on it, takes the whole map.  Not a shell list's row: no window is
		 named "all". */
	/* airliftopen <x> <y> opens the airlift panel of the local player's first airlift power (Ally Aid)
		 aimed at that world point, as the power's click on the ground does; airliftpanel says what the
		 panel shows and where the centre of each of its buttons is on screen, for click.  Both are the
		 client's own, so a picture can be taken of the panel; only OK reaches the logic, as a message. */
	if (strncmp( command.str(), "airliftopen ", 12 ) == 0)
	{
		Coord3D at;
		at.zero();
		if (sscanf( command.str() + 12, "%f %f", &at.x, &at.y ) != 2 || TheTerrainLogic == NULL)
		{
			replyError( "airliftopen <x> <y>" );
			return;
		}
		at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );
		if (AirliftPanel_openForLocalPlayer( &at ))
			replyOk( "\"airlift\":\"open\"" );
		else
			replyError( "the local player has no airlift power" );
		return;
	}

	/* aim <CommandButton> [<Object>]: what pressing that button does, on the local player's first object
		 that owns its power (or, for a button with none, such as Command_Guard, the first unit that can
		 move), of that template when one is named, selected first.  The cursor is then up and follows the
		 pointer, so hover draws the ground marker and click fires it.  Read the way airliftopen reads; for
		 recording the aims' programs (Tools/record-dx11-shaders.ps1 has no way to aim a power). */
	if (strncmp( command.str(), "aim ", 4 ) == 0)
	{
		char buttonName[ 128 ] = "", templateName[ 128 ] = "";
		sscanf( command.str() + 4, "%127s %127s", buttonName, templateName );
		const CommandButton *button = TheControlBar ? TheControlBar->findCommandButton( AsciiString( buttonName ) ) : NULL;
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
		if (button == NULL || player == NULL || TheGameLogic == NULL || TheInGameUI == NULL)
		{
			replyError( "aim <CommandButton>, in a match" );
			return;
		}
		const SpecialPowerTemplate *power = button->getSpecialPowerTemplate();
		Object *caster = NULL;
		for (Object *obj = TheGameLogic->getFirstObject(); obj && caster == NULL; obj = obj->getNextObject())
		{
			if (obj->getControllingPlayer() != player || obj->isEffectivelyDead() || obj->getDrawable() == NULL)
				continue;
			if (templateName[ 0 ] != 0 && obj->getTemplate()->getName().compareNoCase( templateName ) != 0)
				continue;
			if (power ? obj->getSpecialPowerModule( power ) != NULL : ( obj->getAI() != NULL && !obj->isKindOf( KINDOF_STRUCTURE ) ))
				caster = obj;
		}
		if (caster == NULL)
		{
			replyError( "nothing of the local player's fires that button" );
			return;
		}
		TheInGameUI->deselectAllDrawables();
		TheInGameUI->selectDrawable( caster->getDrawable() );
		TheInGameUI->setGUICommand( button );
		replyOk( "\"aimed\":true" );
		return;
	}

	if (command == "airliftpanel")
	{
		char members[ 3072 ];
		AirliftPanel_describe( members, sizeof( members ) );
		char reply[ 3200 ];
		snprintf( reply, sizeof( reply ), "{\"ok\":true,%s}", members );
		sendText( reply );
		return;
	}

	/* lanehud <grid> <col> <row>: where the Direct Strike HUD has the local player's slot on screen and the
		 chips it last raised over a plot unit, for a script that drags with the mouse (DirectStrikeHud_describe) */
	if (strncmp( command.str(), "lanehud ", 8 ) == 0)
	{
		Int grid = 0, col = 0, row = 0;
		if (sscanf( command.str() + 8, "%d %d %d", &grid, &col, &row ) != 3)
		{
			replyError( "lanehud <grid> <col> <row>" );
			return;
		}
		char members[ 2048 ];
		DirectStrikeHud_describe( grid, col, row, members, sizeof( members ) );
		char reply[ 2100 ];
		snprintf( reply, sizeof( reply ), "{\"ok\":true,%s}", members );
		sendText( reply );
		return;
	}

	/* pick <Template> [add]: selects every one of the local player's objects of that template, the way
		 a double click selects a unit's kind but for buildings too; add keeps what is already selected.
		 For a script that gives several buildings one rally point (an rclick on the ground then does). */
	if (strncmp( command.str(), "pick ", 5 ) == 0)
	{
		char templateName[ 128 ] = "", add[ 8 ] = "";
		sscanf( command.str() + 5, "%127s %7s", templateName, add );
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
		if (templateName[ 0 ] == 0 || player == NULL || TheGameLogic == NULL || TheInGameUI == NULL)
		{
			replyError( "pick <Template> [add], in a match" );
			return;
		}
		if (strcmp( add, "add" ) != 0)
			TheInGameUI->deselectAllDrawables();
		Int picked = 0;
		for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
		{
			if (obj->getControllingPlayer() != player || obj->isEffectivelyDead() || obj->getDrawable() == NULL
					|| obj->getTemplate()->getName().compareNoCase( templateName ) != 0)
				continue;
			TheInGameUI->selectDrawable( obj->getDrawable() );
			++picked;
		}
		AsciiString field;
		field.format( "\"picked\":%d", picked );
		replyOk( field.str() );
		return;
	}

	/* rallyat <x> <y>: what a right click on that ground does with buildings selected, every selected
		 object's MSG_SET_RALLY_POINT there (CommandXlat), so a script sets a rally point at a world point
		 rather than at whatever the camera puts under a screen pixel. */
	if (strncmp( command.str(), "rallyat ", 8 ) == 0)
	{
		Coord3D at;
		at.zero();
		if (sscanf( command.str() + 8, "%f %f", &at.x, &at.y ) != 2 || TheTerrainLogic == NULL || TheInGameUI == NULL)
		{
			replyError( "rallyat <x> <y>, in a match" );
			return;
		}
		at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );
		Int sent = 0;
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		for (DrawableListCIt it = selected->begin(); it != selected->end(); ++it)
		{
			if ((*it)->getObject() == NULL)
				continue;
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SET_RALLY_POINT );
			msg->appendObjectIDArgument( (*it)->getObject()->getID() );
			msg->appendLocationArgument( at );
			++sent;
		}
		AsciiString field;
		field.format( "\"rallies\":%d", sent );
		replyOk( field.str() );
		return;
	}

	if (handleUiCommand( command ))
		return;

	if (command == "select all")
	{
		for (Int press = 0; press < 2; ++press)
			TheMessageStream->appendMessage( GameMessage::MSG_META_SELECT_ALL );
		replyOk( "\"selected\":true" );
		return;
	}

	/* press, select and uitext reach a shell window by its layout name (MainMenu.wnd:ButtonOnline), so a
		 script walks the menus without knowing where anything is on the screen.  press sends what a mouse
		 up on a push button sends its owner, select picks a list box row, uitext reads what a window shows. */
	if (strncmp( command.str(), "press ", 6 ) == 0 || strncmp( command.str(), "select ", 7 ) == 0 || strncmp( command.str(), "uitext ", 7 ) == 0)
	{
		const Bool isPress = command.str()[ 0 ] == 'p';
		const Bool isSelect = command.str()[ 0 ] == 's';
		AsciiString name( command.str() + (isPress ? 6 : 7) );
		Int row = 0;
		if (isSelect)
		{
			const char *space = strrchr( name.str(), ' ' );
			if (space == NULL)
			{
				replyError( "select wants a window name and a row" );
				return;
			}
			row = atoi( space + 1 );
			AsciiString nameOnly;
			nameOnly.format( "%.*s", (Int)(space - name.str()), name.str() );
			name = nameOnly;
		}
		name.trim();

		const NameKeyType key = TheNameKeyGenerator->nameToKey( name );
		GameWindow *window = TheWindowManager->winGetWindowFromId( NULL, key );
		if (window == NULL)
		{
			replyError( "no such window" );
			return;
		}

		if (isPress)
		{
			TheWindowManager->winSendSystemMsg( window->winGetOwner(), GBM_SELECTED, (WindowMsgData)window, key );
			replyOk( "\"pressed\":true" );
		}
		else if (isSelect)
		{
			GadgetListBoxSetSelected( window, row );
			replyOk( "\"selected\":true" );
		}
		else
		{
			AsciiString text;
			text.translate( window->winGetText() );
			AsciiString field;
			field.format( "\"text\":\"%s\"", text.str() );
			replyOk( field.str() );
		}
		return;
	}

	/* save <name>: the save menu's Save, into <name>.sav in the save directory, so a script can make
		 the file -loadsave opens.  Letters, digits, - and _ only, since it becomes a path.
		 saveat <frame> <name>: the same, between the logic frames, on the first poll once <frame> is
		 reached, so two builds can each save the same match at the same frame. */
	const Bool isSaveAt = strncmp( command.str(), "saveat ", 7 ) == 0;
	if (isSaveAt || strncmp( command.str(), "save ", 5 ) == 0)
	{
		AsciiString name( command.str() + ( isSaveAt ? 7 : 5 ) );
		name.trim();
		UnsignedInt atFrame = 0;
		if (isSaveAt)
		{
			atFrame = (UnsignedInt)atoi( name.str() );
			const char *space = strchr( name.str(), ' ' );
			name.set( space ? space + 1 : "" );
			name.trim();
			if (atFrame == 0)
			{
				replyError( "saveat wants a frame and a name" );
				return;
			}
		}
		if (name.isEmpty() || strspn( name.str(), "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" ) != (size_t)name.getLength())
		{
			replyError( "save wants a name of letters, digits, - and _" );
			return;
		}
		if (isSaveAt)
		{
			theSaveAtFrame = atFrame;
			theSaveAtName = name;
			replyOk( "\"saveat\":true" );
			return;
		}
		if (TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		{
			replyError( "save needs a match in progress" );
			return;
		}
		if (!saveTo( name ))
		{
			replyError( "the game could not write the save" );
			return;
		}
		replyOk( "\"saved\":true" );
		return;
	}

	/* quitmenu surrender|exit: open the in-match quit menu, press its Surrender or Exit and answer Yes in
		 the confirmation box, each through the window's own callback, so QuitMenu.cpp's surrenderQuitMenu or
		 exitQuitMenu runs as it does for a player's clicks.  Surrender keeps the player in the match, defeated,
		 which "leave" never did; online_check.py's case 2 needs exactly that. */
	if (strncmp( command.str(), "quitmenu ", 9 ) == 0)
	{
		AsciiString which( command.str() + 9 );
		which.trim();
		const char *button = which == "surrender" ? "ButtonRestart" : (which == "exit" ? "ButtonExit" : NULL);
		if (button == NULL)
		{
			replyError( "quitmenu wants surrender or exit" );
			return;
		}
		if (TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || TheInGameUI == NULL)
		{
			replyError( "quitmenu needs a match in progress" );
			return;
		}
		if (!TheInGameUI->isQuitMenuVisible())
			ToggleQuitMenu();
		GameWindow *pressed = NULL;
		const char *const layouts[] = { "QuitNoSave.wnd", "QuitMenu.wnd" };
		for (Int i = 0; i < 2 && (pressed == NULL || pressed->winIsHidden()); ++i)
		{
			AsciiString name;
			name.format( "%s:%s", layouts[ i ], button );
			pressed = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( name ) );
		}
		if (pressed == NULL || pressed->winIsHidden() || !pressed->winGetEnabled())
		{
			replyError( "the quit menu's button is not there or not live" );
			return;
		}
		TheWindowManager->winSendSystemMsg( pressed->winGetOwner(), GBM_SELECTED, (WindowMsgData)pressed, pressed->winGetWindowId() );
		GameWindow *yes = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "QuitMessageBox.wnd:ButtonYes" ) );
		if (yes == NULL)
			yes = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "MessageBox.wnd:ButtonYes" ) );
		if (yes == NULL)
		{
			replyError( "no confirmation box came up" );
			return;
		}
		TheWindowManager->winSendSystemMsg( yes->winGetOwner(), GBM_SELECTED, (WindowMsgData)yes, yes->winGetWindowId() );
		DEBUG_LOG(( "CONTROL: quit menu %s, confirmed\n", which.str() ));
		replyOk( "\"confirmed\":true" );
		return;
	}

	/* The quit menu's Exit, which in a network game is the other half of "quit": the process goes
		 on, the player surrenders and the match is left through the network, so everybody else sees
		 a player leave on an agreed frame rather than a connection drop. exitQuitMenu in QuitMenu.cpp
		 posts the same two messages, and like it spares a side that has already won. */
	if (command == "leave")
	{
		if (TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		{
			replyError( "leave needs a match in progress" );
			return;
		}
		if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInSkirmishGame()
				&& !TheVictoryConditions->isLocalAlliedVictory())
		{
			GameMessage *surrender = TheMessageStream->appendMessage( GameMessage::MSG_SELF_DESTRUCT );
			surrender->appendBooleanArgument( TRUE );
		}
		TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
		// the message needs a logic frame to land, and lockstep would hold the match until the next step
		theLockstep = FALSE;
		replyOk( "\"leaving\":true" );
		return;
	}

	/* kickvote <player index> yes|no: the vote InGameChat's "/kick name", "/yes" and "/no" post, by the
		 target's player index; GameLogic::processKickVote decides on every machine whether it counts. */
	if (strncmp( command.str(), "kickvote ", 9 ) == 0)
	{
		Int target = -1;
		char answer[ 8 ] = { 0 };
		if (sscanf( command.str() + 9, "%d %7s", &target, answer ) != 2 || ( strcmp( answer, "yes" ) != 0 && strcmp( answer, "no" ) != 0 ))
		{
			replyError( "kickvote wants <player index> yes|no" );
			return;
		}
		GameMessage *vote = TheMessageStream->appendMessage( GameMessage::MSG_VOTE_KICK );
		vote->appendIntegerArgument( target );
		vote->appendBooleanArgument( strcmp( answer, "yes" ) == 0 );
		replyOk( "\"voted\":true" );
		return;
	}

	/* key <KEY_name> [ALT] [CTRL] [SHIFT] [HOLD | RELEASE]
		 One press and release, carried the way Keyboard.cpp carries a real one, so the command map, the
		 translators and whatever network message sits behind them all see a key.  The keyboard itself
		 is DirectInput and reads only the foreground window, which a script cannot count on holding.
		 The mouse stays where it is: post the window a WM_MOUSEMOVE first when the key reads it.
		 HOLD sends only the press and RELEASE only the release, for what stays up while a key is held. */
	if (strncmp( command.str(), "key ", 4 ) == 0)
	{
		char words[ 256 ];
		strncpy( words, command.str() + 4, sizeof( words ) - 1 );
		words[ sizeof( words ) - 1 ] = 0;

		Int key = KEY_NONE;
		const char *keyName = strtok( words, " " );
		for( const LookupListRec *name = KeyNames; keyName && name->name; ++name )
		{
			if (strcmp( name->name, keyName ) == 0)
				key = name->value;
		}
		if (key == KEY_NONE)
		{
			replyError( "key wants a KEY_ name from CommandMap.ini, e.g. key KEY_Z ALT" );
			return;
		}

		/* A modifier is pressed before the key and let go after it, as a hand does.  A key carrying
			 CTRL in its state with no Ctrl press of its own left MetaEventTranslator's m_lastModState on
			 Ctrl: the force attack hold began and only ended on the next key without it, so a click sent
			 in between fired instead of selecting. */
		static const struct { const char *word; Int state; Int key; } MODIFIER_KEYS[] =
		{
			{ "ALT", KEY_STATE_LALT, KEY_LALT },
			{ "CTRL", KEY_STATE_LCONTROL, KEY_LCTRL },
			{ "SHIFT", KEY_STATE_LSHIFT, KEY_LSHIFT },
		};
		Int modifiers = KEY_STATE_NONE;
		Bool down = TRUE;
		Bool up = TRUE;
		for( const char *word = strtok( NULL, " " ); word; word = strtok( NULL, " " ) )
		{
			Int m = 0;
			while (m < (Int)ARRAY_SIZE( MODIFIER_KEYS ) && strcmp( word, MODIFIER_KEYS[ m ].word ) != 0)
				++m;
			if (m < (Int)ARRAY_SIZE( MODIFIER_KEYS ))
				modifiers |= MODIFIER_KEYS[ m ].state;
			else if (strcmp( word, "HOLD" ) == 0)
				up = FALSE;
			else if (strcmp( word, "RELEASE" ) == 0)
				down = FALSE;
			else
			{
				replyError( "a modifier is ALT, CTRL or SHIFT, and HOLD or RELEASE sends half the press" );
				return;
			}
		}

		Int held = KEY_STATE_NONE;
		if (down)
		{
			for( Int m = 0; m < (Int)ARRAY_SIZE( MODIFIER_KEYS ); ++m )
			{
				if (!(modifiers & MODIFIER_KEYS[ m ].state))
					continue;
				held |= MODIFIER_KEYS[ m ].state;
				GameMessage *modifier = TheMessageStream->appendMessage( GameMessage::MSG_RAW_KEY_DOWN );
				modifier->appendIntegerArgument( MODIFIER_KEYS[ m ].key );
				modifier->appendIntegerArgument( KEY_STATE_DOWN | held );
			}
			GameMessage *press = TheMessageStream->appendMessage( GameMessage::MSG_RAW_KEY_DOWN );
			press->appendIntegerArgument( key );
			press->appendIntegerArgument( KEY_STATE_DOWN | modifiers );
		}
		if (up)
		{
			GameMessage *release = TheMessageStream->appendMessage( GameMessage::MSG_RAW_KEY_UP );
			release->appendIntegerArgument( key );
			release->appendIntegerArgument( KEY_STATE_UP | modifiers );
			held = modifiers;
			for( Int m = 0; m < (Int)ARRAY_SIZE( MODIFIER_KEYS ); ++m )
			{
				if (!(modifiers & MODIFIER_KEYS[ m ].state))
					continue;
				held &= ~MODIFIER_KEYS[ m ].state;
				GameMessage *modifier = TheMessageStream->appendMessage( GameMessage::MSG_RAW_KEY_UP );
				modifier->appendIntegerArgument( MODIFIER_KEYS[ m ].key );
				modifier->appendIntegerArgument( KEY_STATE_UP | held );
			}
		}
		replyOk( "\"pressed\":true" );
		return;
	}

	/* hover <x> <y>
		 Puts the game's pointer at that screen pixel.  Mouse::createStreamMessages sends the position
		 every frame and the window manager takes it as a real move, so a button lights up under it and
		 stays lit until the real mouse moves.  The Windows pointer goes there too when the game has the
		 focus; without it only the game's own idea of the pointer moves, which is all a window reads. */
	if (strncmp( command.str(), "hover ", 6 ) == 0)
	{
		Int x = 0, y = 0;
		if (sscanf( command.str() + 6, "%d %d", &x, &y ) != 2)
		{
			replyError( "hover wants two screen pixels, x then y" );
			return;
		}
		if (TheMouse == NULL)
		{
			replyError( "no mouse to move" );
			return;
		}
		if (!TheMouse->warpCursor( x, y ))
			TheMouse->setPosition( x, y );
		replyOk( "\"hovered\":true" );
		return;
	}

	/* click <x> <y>, rclick <x> <y>
		 A left (right) press and release at that screen pixel, carried the way Mouse.cpp carries a real
		 one: the windows, the minimap among them, selection and orders all read the message's own
		 position, so a click on a unit selects it and a right click on the radar orders there.  The
		 cursor itself does not move.  Both halves carry time 0, which every click-or-drag test reads
		 as a quick click.  A trailing HOLD sends the press alone, so a button can be shot pressed; the
		 next click lets it go. */
	const Bool isRightClick = strncmp( command.str(), "rclick ", 7 ) == 0;
	if (isRightClick || strncmp( command.str(), "click ", 6 ) == 0)
	{
		ICoord2D pixel;
		char hold[ 8 ] = "";
		const Int read = sscanf( command.str() + (isRightClick ? 7 : 6), "%d %d %7s", &pixel.x, &pixel.y, hold );
		if (read < 2 || (read == 3 && strcmp( hold, "HOLD" ) != 0))
		{
			replyError( "click and rclick want two screen pixels, x then y, and HOLD to keep the button down" );
			return;
		}
		const GameMessage::Type left[] = { GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP };
		const GameMessage::Type right[] = { GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP };
		const GameMessage::Type *halves = isRightClick ? right : left;
		for (Int half = 0; half < (read == 3 ? 1 : 2); ++half)
		{
			GameMessage *msg = TheMessageStream->appendMessage( halves[ half ] );
			msg->appendPixelArgument( pixel );
			msg->appendIntegerArgument( KEY_STATE_NONE );
			msg->appendIntegerArgument( 0 );
		}
		replyOk( "\"clicked\":true" );
		return;
	}

	/* wheel <notches>
		 The mouse wheel turned that many notches, up positive, as Mouse.cpp sends it: the zoom, the
		 freecam's lens and a list box's scroll all read it.  Fractions are a touchpad's small turns. */
	if (strncmp( command.str(), "wheel ", 6 ) == 0)
	{
		Real notches = 0.0f;
		if (sscanf( command.str() + 6, "%f", &notches ) != 1 || TheMouse == NULL)
		{
			replyError( "wheel wants a number of notches, up positive" );
			return;
		}
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_RAW_MOUSE_WHEEL );
		msg->appendPixelArgument( TheMouse->getMouseStatus()->pos );
		msg->appendRealArgument( notches );
		msg->appendIntegerArgument( KEY_STATE_NONE );
		replyOk( "\"wheeled\":true" );
		return;
	}

	/* spectator <action>
		 One of the spectator's actions by name: camera:director, follow:3, fog, flip:camera and the
		 rest that InGameUI::runSpectatorAction knows. */
	if (strncmp( command.str(), "spectator ", 10 ) == 0)
	{
		const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
		if (isGuardedMatch() && (local == NULL || !local->isPlayerObserver()))
		{
			replyError( "a player in a network match cannot use the spectator's views" );
			return;
		}
		TheInGameUI->runSpectatorAction( command.str() + 10 );
		replyOk( "\"done\":true" );
		return;
	}

	/* step <frames> | step release
		 Lockstep: from the first step the logic runs only the frames a step asks for, as fast as the
		 machine goes, and the reply comes once they have run.  An offline skirmish only: a network game
		 has its own clock, and a replay or a guarded match is not this machine's to hold. */
	if (command == "step" || strncmp( command.str(), "step ", 5 ) == 0)
	{
		AsciiString arg( command.str() + 4 );
		arg.trim();
		if (arg == "release")
		{
			const Bool was = theLockstep;
			theLockstep = FALSE;
			if (theStepPending)
				replyMembers( FALSE, "step", "\"error\":\"released before its frames ran\"" );
			theStepPending = FALSE;
			releaseHeldCommands();
			char members[ 64 ];
			sprintf( members, "\"released\":%s,\"frame\":%d", was ? "true" : "false", TheGameLogic ? (Int)TheGameLogic->getFrame() : 0 );
			replyMembers( TRUE, "step", members );
			return;
		}
		const Int frames = atoi( arg.str() );
		if (arg.isEmpty() || strspn( arg.str(), "0123456789" ) != (size_t)arg.getLength() || frames < 1 || frames > STEP_MAX_FRAMES)
		{
			replyMembers( FALSE, "step", "\"error\":\"step wants a frame count from 1 to 300, or release\"" );
			return;
		}
		if (!isMatchRunning())
		{
			replyMembers( FALSE, "step", "\"error\":\"no match is running\"" );
			return;
		}
		if (TheNetwork != NULL || TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInReplayGame() || isGuardedMatch())
		{
			replyMembers( FALSE, "step", "\"error\":\"step only holds an offline skirmish: not a network match, a replay or a guarded match\"" );
			return;
		}
		if (theStepPending)
		{
			replyMembers( FALSE, "step", "\"error\":\"a step is already running\"" );
			return;
		}
		if (!theLockstep)
			DEBUG_LOG(("CONTROL: lockstep from frame %u\n", TheGameLogic->getFrame()));
		theLockstep = TRUE;
		theStepPending = TRUE;
		theStepTarget = TheGameLogic->getFrame() + (UnsignedInt)frames;
		return;		// replied by updateLockstep once the frames have run
	}

	/* objects <slot>: what a seat owns, read now.  In a guarded match only the local player's seat,
		 since the rest is behind the fog. */
	if (strncmp( command.str(), "objects ", 8 ) == 0 || strncmp( command.str(), "buildspot ", 10 ) == 0)
	{
		const Bool isObjects = command.str()[ 0 ] == 'o';
		const char *verb = isObjects ? "objects" : "buildspot";
		Int slot = 0;
		char templateName[ 256 ] = "";
		Real x = 0.0f, y = 0.0f, radius = 0.0f;
		const Bool parsed = isObjects ? sscanf( command.str() + 8, "%d", &slot ) == 1
			: sscanf( command.str() + 10, "%d %255s %f %f %f", &slot, templateName, &x, &y, &radius ) == 5;
		if (!parsed)
		{
			replyMembers( FALSE, verb, isObjects ? "\"error\":\"objects wants <slot>\"" : "\"error\":\"buildspot wants <slot> <template> <x> <y> <radius>\"" );
			return;
		}
		if (!isMatchRunning())
		{
			replyMembers( FALSE, verb, "\"error\":\"no match is running\"" );
			return;
		}
		const Player *local = ThePlayerList->getLocalPlayer();
		if (isGuardedMatch() && (local == NULL || ThePlayerList->getSlotIndex( local->getPlayerIndex() ) != slot))
		{
			replyMembers( FALSE, verb, "\"error\":\"a guarded match shows only the local player's seat\"" );
			return;
		}

		AsciiString members;
		if (isObjects)
		{
			std::string list;
			if (!ScenarioDrill_describeObjects( slot, OBJECTS_MAX, &list ))
			{
				replyMembers( FALSE, verb, "\"error\":\"nobody is sitting in that slot\"" );
				return;
			}
			members.format( "\"frame\":%u,\"slot\":%d,", TheGameLogic->getFrame(), slot );
			replyMembers( TRUE, verb, (members.str() + list).c_str() );
			return;
		}
		if (radius < 0.0f || radius > 2000.0f)
		{
			replyMembers( FALSE, verb, "\"error\":\"radius runs from 0 to 2000\"" );
			return;
		}
		Coord3D found;
		AsciiString why;
		if (!ScenarioDrill_findBuildSpot( slot, AsciiString( templateName ), x, y, radius, &found, &why ))
		{
			members.set( "\"error\":\"" );
			members.concat( why );
			members.concat( "\"" );
			replyMembers( FALSE, verb, members.str() );
			return;
		}
		members.format( "\"x\":%.1f,\"y\":%.1f", found.x, found.y );
		replyMembers( TRUE, verb, members.str() );
		return;
	}

	/* skirmish <players> <seed> <map>
		 The map is whatever is left on the line, spaces and all, because every shipped map has a
		 space in its name and quoting rules would be one more thing to get wrong at the far end. */
	if (strncmp( command.str(), "skirmish ", 9 ) == 0)
	{
		if (TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		{
			replyError( "a match is already running; quit it first" );
			return;
		}

		Int players = 0;
		Int seed = 0;
		char mapName[ 512 ];
		if (sscanf( command.str(), "skirmish %d %d %511[^\n]", &players, &seed, mapName ) != 3)
		{
			replyError( "skirmish wants <players> <seed> <map>" );
			return;
		}
		if (players < 2 || players > MAX_SLOTS)
		{
			replyError( "a skirmish holds between two and eight players" );
			return;
		}

		/* m_autoSkirmishPlayers is deliberately left alone: it is also what marks a run unattended,
			 and a match somebody is driving from here has to stay up when it ends rather than write
			 its numbers out and quit. */
		TheWritableGlobalData->m_mapName.set( mapName );
		TheWritableGlobalData->m_fixedSeed = seed;

		extern void GameEngine_startSkirmish( Int numPlayers );
		GameEngine_startSkirmish( players );
		replyOk( "\"starting\":true" );
		return;
	}

	/* netagain [<speed percent>] [map]: a -netgame copy plays a second match in the same process, on its
		 slot list and seed, at that speed (or -gamespeed's, or retail's) on that map (or the last one).
		 The match under way is cleared locally (no surrender, the room is meant to follow suit or be a
		 fresh copy at the same address) and the next starts from ControlServer_poll once nothing is in
		 game.  A test route for what one match leaves the next. */
	if (command == "netagain" || strncmp( command.str(), "netagain ", 9 ) == 0)
	{
		if (TheGlobalData->m_netGameHosts.isEmpty())
		{
			replyError( "netagain needs a -netgame run" );
			return;
		}
		const char *rest = command.getLength() > 9 ? command.str() + 9 : "";
		Int speed = 0;
		Int used = 0;
		if (sscanf( rest, "%d%n", &speed, &used ) == 1)
		{
			rest += used;
			while (*rest == ' ')
				++rest;
		}
		else
			speed = 0;
		extern void LANAPI_setAutomatedGameSpeed( Int percent );
		LANAPI_setAutomatedGameSpeed( speed );
		if (*rest != 0)
			TheWritableGlobalData->m_mapName.set( rest );
		theNetAgainPending = TRUE;
		theNetAgainCleared = FALSE;
		theLockstep = FALSE;
		replyOk( "\"starting\":true" );
		return;
	}

	/* Anything else is a world command, and world commands are the scenario grammar with the frame
		 number left off - so put one back on and hand it to the same parser the files go through. One
		 grammar, two front ends, and a line that works in a file works down the socket. */
	AsciiString asScenarioLine;
	asScenarioLine.set( "0 " );
	asScenarioLine.concat( command );

	ScenarioAction action;
	const ScenarioParseResult result = ScenarioDrill_parseLine( asScenarioLine.str(), &action );
	if (result != SCENARIO_PARSE_OK)
	{
		replyError( ScenarioDrill_parseResultName( result ) );
		return;
	}

	if (!TheGameLogic || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
	{
		replyError( "no match is running" );
		return;
	}

	/* A world command changes this machine's simulation and nobody else's, and tally reads through the
		 fog.  A network match the lobby started would split on it (isGuardedMatch), and a replay would stop
		 matching its recording; a -netgame match is allowed, on the same terms -scenario is
		 (GameLogic::startNewGame), and so is a match on an online test server, where a split ends the match
		 unrated, which is what online_check.py proves. */
	if (isGuardedMatch() || TheGameLogic->isInReplayGame())
	{
		replyError( "world commands only run in an offline skirmish, a -netgame match or a lobby match on an online test server" );
		return;
	}

	/* The socket's produce, construct, sell and science act for any seat, outside the command stream, so in
		 a -netgame match or on an online test server they would put this machine's game on its own.  spawn and
		 the rest keep the terms above on purpose; these are an offline learner's. */
	const char *orderVerb = replyWhenRunVerb( action.action );
	const Bool replyWhenRun = orderVerb != NULL;
	if (replyWhenRun && (TheGameLogic->isInMultiplayerGame() || TheNetwork != NULL))
	{
		replyMembers( FALSE, orderVerb,
									"\"error\":\"produce, construct, sell and science run only in an offline skirmish: one machine's order would split a network match\"" );
		return;
	}

	if ((Int)(thePendingCommands.size() + theHeldCommands.size()) >= CONTROL_MAX_COMMANDS_PER_FRAME)
	{
		replyError( "too many commands queued for one frame" );
		return;
	}

	/* produce, construct, sell and science answer with what happened, once the logic frame has run them
		 (ScenarioDrill_controlOrder); in lockstep that is inside the next step, ahead of its own reply.
		 Sent during a step, a command waits for that step's reply and runs on the next one's first frame. */
	PendingCommand pending;
	pending.line = command;
	pending.replyWhenRun = replyWhenRun;
	(theStepPending ? theHeldCommands : thePendingCommands).push_back( pending );
	if (!pending.replyWhenRun)
		replyOk( "\"queued\":true" );
}

// ------------------------------------------------------------------------------------------------
// the two ticks
// ------------------------------------------------------------------------------------------------

void ControlServer_poll( void )
{
	if (TheGlobalData == NULL || TheGlobalData->m_controlPort <= 0)
		return;

	if (theSaveAtFrame != 0 && TheGameLogic != NULL && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()
			&& TheGameLogic->getFrame() >= theSaveAtFrame)
	{
		const Bool saved = saveTo( theSaveAtName );
		DEBUG_LOG(("CONTROL: saveat %s %s at frame %u\n", theSaveAtName.str(), saved ? "saved" : "FAILED", TheGameLogic->getFrame()));
		theSaveAtFrame = 0;
	}

	if (theNetAgainPending && TheGameLogic != NULL)
	{
		if (TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		{
			if (!theNetAgainCleared)
			{
				TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
				theNetAgainCleared = TRUE;
				DEBUG_LOG(("CONTROL: netagain clears the match at frame %u\n", TheGameLogic->getFrame()));
			}
		}
		else
		{
			theNetAgainPending = FALSE;
			DEBUG_LOG(("CONTROL: netagain starts '%s'\n", TheGlobalData->m_mapName.str()));
			extern void GameEngine_startNetGame( void );
			GameEngine_startNetGame();
		}
	}

	updateLockstep();

	if (theListenSocket == INVALID_SOCKET)
	{
		if (!openListenSocket( TheGlobalData->m_controlPort ))
		{
			// stop trying: a port that will not bind now will not bind on the next frame either
			TheWritableGlobalData->m_controlPort = 0;
			return;
		}
	}

	if (theClientSocket == INVALID_SOCKET)
	{
		const SOCKET accepted = accept( theListenSocket, NULL, NULL );
		if (accepted == INVALID_SOCKET)
			return;
		theClientSocket = accepted;
		setNonBlocking( theClientSocket );
#if defined(SO_NOSIGPIPE)
		const int noSigPipe = 1;			// see CONTROL_SEND_FLAGS
		setsockopt( theClientSocket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof( noSigPipe ) );
#endif
		theHandshakeDone = FALSE;
		theIncoming.clear();
	}

	char chunk[ CONTROL_READ_CHUNK ];
	for( ;; )
	{
		const Int received = recv( theClientSocket, chunk, sizeof( chunk ), 0 );
		if (received > 0)
		{
			for( Int i = 0; i < received; ++i )
				theIncoming.push_back( chunk[ i ] );
			if ((Int)theIncoming.size() > CONTROL_MAX_REQUEST)
			{
				DEBUG_LOG(("CONTROL: client sent more than %d bytes without a frame, closing\n",
									 CONTROL_MAX_REQUEST));
				closeClient();
				return;
			}
			continue;
		}

		if (received == 0)
		{
			DEBUG_LOG(("CONTROL: client went away\n"));
			closeClient();
			return;
		}

#if !defined(_WIN32)
		// A signal interrupting recv() is not the client; Windows has no counterpart to retry for.
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
#else
		if (WSAGetLastError() != WSAEWOULDBLOCK)
#endif
		{
			closeClient();
			return;
		}
		break;
	}

	if (!theHandshakeDone)
	{
		if (theIncoming.empty() || !tryHandshake())
			return;
	}

	unsigned char opcode = 0;
	AsciiString payload;
	while (theClientSocket != INVALID_SOCKET && takeFrame( &opcode, &payload ))
	{
		if (opcode == WS_OPCODE_CLOSE)
		{
			closeClient();
			return;
		}
		if (opcode == WS_OPCODE_PING)
		{
			sendFrame( WS_OPCODE_PONG, payload.str(), (Int)strlen( payload.str() ) );
			continue;
		}
		if (opcode != WS_OPCODE_TEXT)
			continue;

		handleCommand( payload );
	}
}

void ControlServer_runCommands( void )
{
	if (theQuitRequested && TheGameEngine)
		TheGameEngine->setQuitting( TRUE );

	for( std::vector<PendingCommand>::iterator it = thePendingCommands.begin();
			 it != thePendingCommands.end(); ++it )
	{
		AsciiString asScenarioLine;
		asScenarioLine.set( "0 " );
		asScenarioLine.concat( it->line );

		ScenarioAction action;
		if (ScenarioDrill_parseLine( asScenarioLine.str(), &action ) != SCENARIO_PARSE_OK)
			continue;

		if (it->replyWhenRun)
		{
			AsciiString members;
			const Bool took = ScenarioDrill_controlOrder( action, &members );
			replyMembers( took, replyWhenRunVerb( action.action ), members.str() );
			continue;
		}
		ScenarioDrill_execute( action );
	}

	thePendingCommands.clear();
}

void ControlServer_shutdown( void )
{
	closeClient();

	if (theListenSocket != INVALID_SOCKET)
	{
		closesocket( theListenSocket );
		theListenSocket = INVALID_SOCKET;
	}

#if defined(_WIN32)
	if (theWinsockStarted)
	{
		WSACleanup();
		theWinsockStarted = FALSE;
	}
#endif
}
