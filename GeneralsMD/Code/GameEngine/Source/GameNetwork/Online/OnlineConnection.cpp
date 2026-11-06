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

// FILE: OnlineConnection.cpp /////////////////////////////////////////////////////////////////////
// Desc:   The game's line to the online service: TLS over TCP, one flat JSON object per line.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* Built as a library of its own (CMake target onlineconnection), not with the engine's precompiled
	 header: it knows nothing of the engine, and test_online links it alone. */

#include "GameNetwork/Online/OnlineConnection.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	typedef SOCKET OnlineSocket;
	#define ONLINE_BAD_SOCKET INVALID_SOCKET
	static void closeSocket( OnlineSocket s ) { closesocket( s ); }
	static bool connectPending() { return WSAGetLastError() == WSAEWOULDBLOCK; }
	static void setBlocking( OnlineSocket s, bool block ) { u_long on = block ? 0 : 1; ioctlsocket( s, FIONBIO, &on ); }
	typedef int OnlineSockLen;
#else
	#include <errno.h>
	#include <fcntl.h>
	#include <netdb.h>
	#include <netinet/in.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#include <unistd.h>
	typedef int OnlineSocket;
	#define ONLINE_BAD_SOCKET (-1)
	static void closeSocket( OnlineSocket s ) { ::close( s ); }
	static bool connectPending() { return errno == EINPROGRESS || errno == EINTR; }
	static void setBlocking( OnlineSocket s, bool block )
	{
		const int flags = fcntl( s, F_GETFL, 0 );
		fcntl( s, F_SETFL, block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK) );
	}
	typedef socklen_t OnlineSockLen;
#endif

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

//-------------------------------------------------------------------------------------------------
// UTF-8 and the JSON line
//-------------------------------------------------------------------------------------------------

bool OnlineIsUtf8( const char *bytes, size_t count )
{
	const unsigned char *s = (const unsigned char *)bytes;
	size_t i = 0;
	while (i < count)
	{
		const unsigned c = s[ i ];
		if (c < 0x80)
		{
			++i;
			continue;
		}
		size_t more;
		unsigned code, least;
		if ((c & 0xE0) == 0xC0)				{ more = 1; code = c & 0x1F; least = 0x80; }
		else if ((c & 0xF0) == 0xE0)	{ more = 2; code = c & 0x0F; least = 0x800; }
		else if ((c & 0xF8) == 0xF0)	{ more = 3; code = c & 0x07; least = 0x10000; }
		else return false;
		if (more >= count - i)
			return false;
		for (size_t k = 1; k <= more; ++k)
		{
			const unsigned b = s[ i + k ];
			if ((b & 0xC0) != 0x80)
				return false;
			code = (code << 6) | (b & 0x3F);
		}
		if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
			return false;
		i += more + 1;
	}
	return true;
}

// what may go on the wire as a string: UTF-8 with no NUL, which the reader refuses as \u0000
static bool isCleanText( const std::string &s )
{
	return memchr( s.data(), 0, s.size() ) == NULL && OnlineIsUtf8( s.data(), s.size() );
}

namespace
{
	struct Cursor
	{
		const char *p;
		const char *end;
		bool atEnd() const { return p >= end; }
	};
}

// one line, so a newline is never whitespace here
static void skipSpace( Cursor &c )
{
	while (!c.atEnd() && (*c.p == ' ' || *c.p == '\t' || *c.p == '\r'))
		++c.p;
}

static bool isDigit( char ch ) { return ch >= '0' && ch <= '9'; }

static bool parseNumber( Cursor &c, std::string &out )
{
	const char *start = c.p;
	if (!c.atEnd() && *c.p == '-')
		++c.p;
	if (c.atEnd())
		return false;
	if (*c.p == '0')
		++c.p;
	else if (isDigit( *c.p ))
		while (!c.atEnd() && isDigit( *c.p )) ++c.p;
	else
		return false;
	if (!c.atEnd() && *c.p == '.')
	{
		++c.p;
		if (c.atEnd() || !isDigit( *c.p ))
			return false;
		while (!c.atEnd() && isDigit( *c.p )) ++c.p;
	}
	if (!c.atEnd() && (*c.p == 'e' || *c.p == 'E'))
	{
		++c.p;
		if (!c.atEnd() && (*c.p == '+' || *c.p == '-'))
			++c.p;
		if (c.atEnd() || !isDigit( *c.p ))
			return false;
		while (!c.atEnd() && isDigit( *c.p )) ++c.p;
	}
	out.assign( start, c.p );
	return true;
}

static bool isJsonNumber( const std::string &text )
{
	Cursor c = { text.data(), text.data() + text.size() };
	std::string ignored;
	return parseNumber( c, ignored ) && c.atEnd();
}

static bool parseHex4( Cursor &c, unsigned &out )
{
	if (c.end - c.p < 4)
		return false;
	out = 0;
	for (int i = 0; i < 4; ++i)
	{
		const char ch = *c.p++;
		out <<= 4;
		if (ch >= '0' && ch <= '9')				out |= ch - '0';
		else if (ch >= 'a' && ch <= 'f')	out |= ch - 'a' + 10;
		else if (ch >= 'A' && ch <= 'F')	out |= ch - 'A' + 10;
		else return false;
	}
	return true;
}

static void appendUtf8( std::string &out, unsigned code )
{
	if (code < 0x80)
		out += (char)code;
	else if (code < 0x800)
	{
		out += (char)(0xC0 | (code >> 6));
		out += (char)(0x80 | (code & 0x3F));
	}
	else if (code < 0x10000)
	{
		out += (char)(0xE0 | (code >> 12));
		out += (char)(0x80 | ((code >> 6) & 0x3F));
		out += (char)(0x80 | (code & 0x3F));
	}
	else
	{
		out += (char)(0xF0 | (code >> 18));
		out += (char)(0x80 | ((code >> 12) & 0x3F));
		out += (char)(0x80 | ((code >> 6) & 0x3F));
		out += (char)(0x80 | (code & 0x3F));
	}
}

// raw bytes were checked as UTF-8 for the whole line before this runs, and an escape makes UTF-8 itself
static bool parseString( Cursor &c, std::string &out )
{
	if (c.atEnd() || *c.p != '"')
		return false;
	++c.p;
	while (!c.atEnd())
	{
		const unsigned char ch = (unsigned char)*c.p++;
		if (ch == '"')
			return true;
		if (ch < 0x20)
			return false;
		if (ch != '\\')
		{
			out += (char)ch;
			continue;
		}
		if (c.atEnd())
			return false;
		switch (*c.p++)
		{
			case '"':		out += '"'; break;
			case '\\':	out += '\\'; break;
			case '/':		out += '/'; break;
			case 'b':		out += '\b'; break;
			case 'f':		out += '\f'; break;
			case 'n':		out += '\n'; break;
			case 'r':		out += '\r'; break;
			case 't':		out += '\t'; break;
			case 'u':
			{
				unsigned code;
				if (!parseHex4( c, code ))
					return false;
				if (code >= 0xD800 && code <= 0xDBFF)
				{
					unsigned low;
					if (c.end - c.p < 2 || c.p[ 0 ] != '\\' || c.p[ 1 ] != 'u')
						return false;
					c.p += 2;
					if (!parseHex4( c, low ) || low < 0xDC00 || low > 0xDFFF)
						return false;
					code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
				}
				else if (code >= 0xDC00 && code <= 0xDFFF)
					return false;
				if (code == 0)
					return false;		// a NUL would cut every C string the game keeps it in
				appendUtf8( out, code );
				break;
			}
			default:
				return false;
		}
	}
	return false;
}

static bool parseLiteral( Cursor &c, const char *word )
{
	const size_t n = strlen( word );
	if ((size_t)(c.end - c.p) < n || memcmp( c.p, word, n ) != 0)
		return false;
	c.p += n;
	return true;
}

static bool parseScalar( Cursor &c, OnlineValue &out )
{
	if (c.atEnd())
		return false;
	switch (*c.p)
	{
		case '"':
			out.kind = OnlineValue::STRING;
			return parseString( c, out.text );
		case 't':
			out.kind = OnlineValue::BOOLEAN;
			out.boolean = true;
			return parseLiteral( c, "true" );
		case 'f':
			out.kind = OnlineValue::BOOLEAN;
			out.boolean = false;
			return parseLiteral( c, "false" );
		case 'n':
			out.kind = OnlineValue::NUL;
			return parseLiteral( c, "null" );
		default:
			out.kind = OnlineValue::NUMBER;
			return parseNumber( c, out.text );
	}
}

static bool parseValue( Cursor &c, OnlineValue &out )
{
	if (c.atEnd() || *c.p != '[')
		return parseScalar( c, out );
	out.kind = OnlineValue::ARRAY;
	++c.p;
	skipSpace( c );
	if (!c.atEnd() && *c.p == ']')
	{
		++c.p;
		return true;
	}
	for (;;)
	{
		OnlineValue item;
		if (!parseScalar( c, item ))
			return false;
		out.items.push_back( item );
		skipSpace( c );
		if (c.atEnd())
			return false;
		const char ch = *c.p++;
		if (ch == ']')
			return true;
		if (ch != ',')
			return false;
		skipSpace( c );
	}
}

bool OnlineMessage::parse( const char *line, size_t length, OnlineMessage &out )
{
	out.clear();
	if (length > ONLINE_MAX_LINE || !OnlineIsUtf8( line, length ))
		return false;
	Cursor c = { line, line + length };
	skipSpace( c );
	if (c.atEnd() || *c.p++ != '{')
		return false;
	skipSpace( c );
	bool ok = false;
	if (!c.atEnd() && *c.p == '}')
	{
		++c.p;
		ok = true;
	}
	else
	{
		for (;;)
		{
			std::string key;
			OnlineValue value;
			if (!parseString( c, key ) || out.has( key.c_str() ))
				break;
			skipSpace( c );
			if (c.atEnd() || *c.p++ != ':')
				break;
			skipSpace( c );
			if (!parseValue( c, value ))
				break;
			out.m_fields.push_back( std::make_pair( key, value ) );
			skipSpace( c );
			if (c.atEnd())
				break;
			const char ch = *c.p++;
			if (ch == '}')
			{
				ok = true;
				break;
			}
			if (ch != ',')
				break;
			skipSpace( c );
		}
	}
	skipSpace( c );
	if (!ok || !c.atEnd())
	{
		out.clear();
		return false;
	}
	return true;
}

static void appendQuoted( std::string &out, const std::string &s )
{
	out += '"';
	for (size_t i = 0; i < s.size(); ++i)
	{
		const unsigned char ch = (unsigned char)s[ i ];
		switch (ch)
		{
			case '"':		out += "\\\""; break;
			case '\\':	out += "\\\\"; break;
			case '\n':	out += "\\n"; break;
			case '\r':	out += "\\r"; break;
			case '\t':	out += "\\t"; break;
			case '\b':	out += "\\b"; break;
			case '\f':	out += "\\f"; break;
			default:
				if (ch < 0x20)
				{
					char escape[ 8 ];
					snprintf( escape, sizeof( escape ), "\\u%04x", ch );
					out += escape;
				}
				else
					out += (char)ch;
		}
	}
	out += '"';
}

static bool appendScalar( std::string &out, const OnlineValue &v )
{
	switch (v.kind)
	{
		case OnlineValue::NUL:			out += "null"; return true;
		case OnlineValue::BOOLEAN:	out += v.boolean ? "true" : "false"; return true;
		case OnlineValue::NUMBER:
			if (!isJsonNumber( v.text ))
				return false;
			out += v.text;
			return true;
		case OnlineValue::STRING:
			if (!isCleanText( v.text ))
				return false;
			appendQuoted( out, v.text );
			return true;
		default:
			return false;
	}
}

bool OnlineMessage::toLine( std::string &out ) const
{
	out = "{";
	bool ok = true;
	for (size_t i = 0; i < m_fields.size() && ok; ++i)
	{
		const OnlineValue &v = m_fields[ i ].second;
		ok = isCleanText( m_fields[ i ].first );
		if (!ok)
			break;
		if (i > 0)
			out += ',';
		appendQuoted( out, m_fields[ i ].first );
		out += ':';
		if (v.kind == OnlineValue::ARRAY)
		{
			out += '[';
			for (size_t k = 0; k < v.items.size() && ok; ++k)
			{
				if (k > 0)
					out += ',';
				ok = appendScalar( out, v.items[ k ] );
			}
			out += ']';
		}
		else
			ok = appendScalar( out, v );
	}
	out += '}';
	if (!ok || out.size() > ONLINE_MAX_LINE)
	{
		out.clear();
		return false;
	}
	out += '\n';
	return true;
}

//-------------------------------------------------------------------------------------------------
// OnlineMessage's accessors
//-------------------------------------------------------------------------------------------------

const OnlineValue *OnlineMessage::find( const char *key ) const
{
	for (size_t i = 0; i < m_fields.size(); ++i)
		if (m_fields[ i ].first == key)
			return &m_fields[ i ].second;
	return NULL;
}

bool OnlineMessage::has( const char *key ) const
{
	return find( key ) != NULL;
}

std::string OnlineMessage::getString( const char *key, const char *otherwise ) const
{
	const OnlineValue *v = find( key );
	return (v && v->kind == OnlineValue::STRING) ? v->text : std::string( otherwise );
}

// an integer spelling exactly through strtoll, so ids past 2^53 survive; anything with a fraction or an
// exponent through strtod.  Both read the C locale's '.', which is the only one the game runs in.
static long long numberAsInt( const std::string &text )
{
	if (text.find_first_of( ".eE" ) == std::string::npos)
		return strtoll( text.c_str(), NULL, 10 );
	return (long long)strtod( text.c_str(), NULL );
}

long long OnlineMessage::getInt( const char *key, long long otherwise ) const
{
	const OnlineValue *v = find( key );
	return (v && v->kind == OnlineValue::NUMBER) ? numberAsInt( v->text ) : otherwise;
}

double OnlineMessage::getNumber( const char *key, double otherwise ) const
{
	const OnlineValue *v = find( key );
	return (v && v->kind == OnlineValue::NUMBER) ? strtod( v->text.c_str(), NULL ) : otherwise;
}

bool OnlineMessage::getBool( const char *key, bool otherwise ) const
{
	const OnlineValue *v = find( key );
	return (v && v->kind == OnlineValue::BOOLEAN) ? v->boolean : otherwise;
}

std::vector<std::string> OnlineMessage::getStrings( const char *key ) const
{
	std::vector<std::string> result;
	const OnlineValue *v = find( key );
	if (v && v->kind == OnlineValue::ARRAY)
		for (size_t i = 0; i < v->items.size(); ++i)
			if (v->items[ i ].kind == OnlineValue::STRING)
				result.push_back( v->items[ i ].text );
	return result;
}

std::vector<long long> OnlineMessage::getInts( const char *key ) const
{
	std::vector<long long> result;
	const OnlineValue *v = find( key );
	if (v && v->kind == OnlineValue::ARRAY)
		for (size_t i = 0; i < v->items.size(); ++i)
			if (v->items[ i ].kind == OnlineValue::NUMBER)
				result.push_back( numberAsInt( v->items[ i ].text ) );
	return result;
}

OnlineMessage &OnlineMessage::set( const char *key, const OnlineValue &value )
{
	for (size_t i = 0; i < m_fields.size(); ++i)
		if (m_fields[ i ].first == key)
		{
			m_fields[ i ].second = value;
			return *this;
		}
	m_fields.push_back( std::make_pair( std::string( key ), value ) );
	return *this;
}

OnlineMessage &OnlineMessage::setString( const char *key, const std::string &value )
{
	OnlineValue v;
	v.kind = OnlineValue::STRING;
	v.text = value;
	return set( key, v );
}

static OnlineValue intValue( long long value )
{
	OnlineValue v;
	v.kind = OnlineValue::NUMBER;
	char text[ 32 ];
	snprintf( text, sizeof( text ), "%lld", value );
	v.text = text;
	return v;
}

OnlineMessage &OnlineMessage::setInt( const char *key, long long value )
{
	return set( key, intValue( value ) );
}

OnlineMessage &OnlineMessage::setNumber( const char *key, double value )
{
	if (!std::isfinite( value ))
		return setNull( key );
	OnlineValue v;
	v.kind = OnlineValue::NUMBER;
	char text[ 40 ];
	snprintf( text, sizeof( text ), "%.17g", value );
	v.text = text;
	return set( key, v );
}

OnlineMessage &OnlineMessage::setBool( const char *key, bool value )
{
	OnlineValue v;
	v.kind = OnlineValue::BOOLEAN;
	v.boolean = value;
	return set( key, v );
}

OnlineMessage &OnlineMessage::setNull( const char *key )
{
	return set( key, OnlineValue() );
}

OnlineMessage &OnlineMessage::setStrings( const char *key, const std::vector<std::string> &values )
{
	OnlineValue v;
	v.kind = OnlineValue::ARRAY;
	for (size_t i = 0; i < values.size(); ++i)
	{
		OnlineValue item;
		item.kind = OnlineValue::STRING;
		item.text = values[ i ];
		v.items.push_back( item );
	}
	return set( key, v );
}

OnlineMessage &OnlineMessage::setInts( const char *key, const std::vector<long long> &values )
{
	OnlineValue v;
	v.kind = OnlineValue::ARRAY;
	for (size_t i = 0; i < values.size(); ++i)
		v.items.push_back( intValue( values[ i ] ) );
	return set( key, v );
}

//-------------------------------------------------------------------------------------------------
// OnlineLineReader
//-------------------------------------------------------------------------------------------------

void OnlineLineReader::feed( const char *bytes, size_t count )
{
	m_buffer.append( bytes, count );
}

OnlineLineReader::Result OnlineLineReader::next( std::string &line )
{
	for (;;)
	{
		const size_t newline = m_buffer.find( '\n' );
		if (m_skipping)
		{
			if (newline == std::string::npos)
			{
				m_buffer.clear();
				return NONE;
			}
			m_buffer.erase( 0, newline + 1 );
			m_skipping = false;
			continue;
		}
		if (newline == std::string::npos)
		{
			// one byte over the limit is still a line if it is the CR of a CRLF, so wait for one more
			if (m_buffer.size() > (size_t)ONLINE_MAX_LINE + 1)
			{
				m_buffer.clear();
				m_skipping = true;
				return OVERSIZE;
			}
			return NONE;
		}
		size_t length = newline;
		if (length > 0 && m_buffer[ length - 1 ] == '\r')
			--length;
		if (length > ONLINE_MAX_LINE)
		{
			m_buffer.erase( 0, newline + 1 );
			return OVERSIZE;
		}
		if (length == 0)
		{
			m_buffer.erase( 0, newline + 1 );
			continue;
		}
		line.assign( m_buffer, 0, length );
		m_buffer.erase( 0, newline + 1 );
		return LINE;
	}
}

//-------------------------------------------------------------------------------------------------
// OnlineConnection
//-------------------------------------------------------------------------------------------------

/* ponytail: TLS 1.2 only.  The stock mbedtls_config.h has no MBEDTLS_THREADING_C, and mbedTLS 3.6 runs
	 TLS 1.3 on PSA's global key store, which is only safe from one thread at a time; test_online's server
	 and this client each run on their own.  TLS 1.2 keeps every piece of state in the connection's own
	 contexts.  Lift it with MBEDTLS_THREADING_C (an ALT mutex on Windows, which has no pthread). */
static const int READ_SLICE_MS = 20;				// how long a read waits before the queue to send is looked at again
static const int CONNECT_TIMEOUT_MS = 10000;
static const int HANDSHAKE_TIMEOUT_MS = 10000;

struct OnlineConnection::Impl
{
	mutable std::mutex lock;
	std::thread thread;
	std::atomic<bool> stop;
	State state;
	std::string error;
	std::deque<std::string> outgoing;
	std::deque<OnlineMessage> incoming;
	unsigned dropped;
	std::string host;
	unsigned short port;
	mbedtls_x509_crt ca;

	Impl() : stop( false ), state( IDLE ), dropped( 0 ), port( 0 ) { mbedtls_x509_crt_init( &ca ); }
	~Impl() { mbedtls_x509_crt_free( &ca ); }

	void finish( const std::string &why )
	{
		std::lock_guard<std::mutex> guard( lock );
		state = CLOSED;
		if (!stop)
			error = why;
		outgoing.clear();
	}
	void run();
};

static std::string describe( int code )
{
	char text[ 160 ];
	mbedtls_strerror( code, text, sizeof( text ) );
	return text;
}

static long long nowMS()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch() ).count();
}

/* mbedtls_net_connect would do, but it blocks until the operating system gives up on an address, 21 s
	 on Windows, and a game being closed would wait that out.  This one tries each address in turn on a
	 non-blocking socket and looks at the stop flag every 50 ms. */
static std::string openSocket( const std::string &host, unsigned short port, const std::atomic<bool> &stop, int &fd )
{
	char service[ 8 ];
	snprintf( service, sizeof( service ), "%u", port );
	addrinfo hints;
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	addrinfo *list = NULL;
	if (getaddrinfo( host.c_str(), service, &hints, &list ) != 0 || list == NULL)
		return "could not resolve " + host;

	const long long deadline = nowMS() + CONNECT_TIMEOUT_MS;
	std::string why = "could not reach " + host + ":" + service;
	for (addrinfo *a = list; a != NULL && !stop && nowMS() < deadline; a = a->ai_next)
	{
		OnlineSocket s = socket( a->ai_family, a->ai_socktype, a->ai_protocol );
		if (s == ONLINE_BAD_SOCKET)
			continue;
#ifdef SO_NOSIGPIPE
		const int one = 1;
		setsockopt( s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof( one ) );
#endif
		setBlocking( s, false );
		bool connected = ::connect( s, a->ai_addr, (OnlineSockLen)a->ai_addrlen ) == 0;
		bool waiting = !connected && connectPending();
		while (waiting && !stop && nowMS() < deadline)
		{
			fd_set writable, failed;
			FD_ZERO( &writable );
			FD_ZERO( &failed );
			FD_SET( s, &writable );
			FD_SET( s, &failed );
			timeval slice = { 0, 50000 };
			const int ready = select( (int)s + 1, NULL, &writable, &failed, &slice );
			if (ready < 0)
				break;
			if (ready == 0)
				continue;
			int problem = 0;
			OnlineSockLen size = sizeof( problem );
			getsockopt( s, SOL_SOCKET, SO_ERROR, (char *)&problem, &size );
			connected = problem == 0 && FD_ISSET( s, &writable );
			waiting = false;
		}
		if (connected)
		{
			setBlocking( s, true );
			fd = (int)s;		// mbedtls_net_context keeps a socket as int, Windows' SOCKET included
			freeaddrinfo( list );
			return std::string();
		}
		closeSocket( s );
	}
	freeaddrinfo( list );
	return why;
}

// mbedtls_net_send writes with write(), and a peer gone away then raises SIGPIPE, which kills a process
// on Linux.  MSG_NOSIGNAL is Linux's way out, SO_NOSIGPIPE above macOS's; Windows has no signal.
static int sendBio( void *context, const unsigned char *bytes, size_t count )
{
#ifdef MSG_NOSIGNAL
	const int fd = ((mbedtls_net_context *)context)->fd;
	const ssize_t sent = ::send( fd, bytes, count, MSG_NOSIGNAL );
	if (sent >= 0)
		return (int)sent;
	if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
		return MBEDTLS_ERR_SSL_WANT_WRITE;
	if (errno == EPIPE || errno == ECONNRESET)
		return MBEDTLS_ERR_NET_CONN_RESET;
	return MBEDTLS_ERR_NET_SEND_FAILED;
#else
	return mbedtls_net_send( context, bytes, count );
#endif
}

static bool isRetry( int code )
{
	return code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE
		|| code == MBEDTLS_ERR_SSL_TIMEOUT || code == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET;
}

void OnlineConnection::Impl::run()
{
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup( MAKEWORD( 2, 2 ), &wsa );
#endif
	mbedtls_net_context net;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context random;
	mbedtls_net_init( &net );
	mbedtls_ssl_init( &ssl );
	mbedtls_ssl_config_init( &conf );
	mbedtls_entropy_init( &entropy );
	mbedtls_ctr_drbg_init( &random );

	std::string why;
	int code = 0;
	do
	{
		static const char personal[] = "zhr online";
		if ((code = mbedtls_ctr_drbg_seed( &random, mbedtls_entropy_func, &entropy,
				(const unsigned char *)personal, sizeof( personal ) - 1 )) != 0)
		{
			why = "no random source: " + describe( code );
			break;
		}
		why = openSocket( host, port, stop, net.fd );
		if (!why.empty())
			break;

		if ((code = mbedtls_ssl_config_defaults( &conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
				MBEDTLS_SSL_PRESET_DEFAULT )) != 0)
		{
			why = describe( code );
			break;
		}
		mbedtls_ssl_conf_authmode( &conf, MBEDTLS_SSL_VERIFY_REQUIRED );
		mbedtls_ssl_conf_ca_chain( &conf, &ca, NULL );
		mbedtls_ssl_conf_rng( &conf, mbedtls_ctr_drbg_random, &random );
		mbedtls_ssl_conf_max_tls_version( &conf, MBEDTLS_SSL_VERSION_TLS1_2 );
		mbedtls_ssl_conf_read_timeout( &conf, READ_SLICE_MS );
		if ((code = mbedtls_ssl_setup( &ssl, &conf )) != 0
			|| (code = mbedtls_ssl_set_hostname( &ssl, host.c_str() )) != 0)
		{
			why = describe( code );
			break;
		}
		mbedtls_ssl_set_bio( &ssl, &net, sendBio, NULL, mbedtls_net_recv_timeout );

		const long long deadline = nowMS() + HANDSHAKE_TIMEOUT_MS;
		while ((code = mbedtls_ssl_handshake( &ssl )) != 0 && isRetry( code ) && !stop && nowMS() < deadline)
			;
		if (code != 0)
		{
			if (code == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
			{
				char text[ 512 ];
				mbedtls_x509_crt_verify_info( text, sizeof( text ), "", mbedtls_ssl_get_verify_result( &ssl ) );
				why = "the server's certificate was refused: " + std::string( text );
				while (!why.empty() && (why[ why.size() - 1 ] == '\n'))
					why.erase( why.size() - 1 );
			}
			else if (isRetry( code ))
				why = "the TLS handshake timed out";
			else
				why = "the TLS handshake failed: " + describe( code );
			break;
		}
		{
			std::lock_guard<std::mutex> guard( lock );
			state = OPEN;
		}

		OnlineLineReader reader;
		std::deque<std::string> sending;
		while (!stop && why.empty())
		{
			{
				std::lock_guard<std::mutex> guard( lock );
				sending.swap( outgoing );
			}
			for (; !sending.empty() && why.empty(); sending.pop_front())
			{
				const std::string &line = sending.front();
				size_t done = 0;
				while (done < line.size() && !stop)
				{
					code = mbedtls_ssl_write( &ssl, (const unsigned char *)line.data() + done, line.size() - done );
					if (code > 0)
						done += code;
					else if (!isRetry( code ))
					{
						why = "the connection broke while sending: " + describe( code );
						break;
					}
				}
			}
			if (!why.empty())
				break;

			unsigned char bytes[ 4096 ];
			code = mbedtls_ssl_read( &ssl, bytes, sizeof( bytes ) );
			if (code > 0)
			{
				reader.feed( (const char *)bytes, code );
				std::string line;
				OnlineLineReader::Result result;
				while ((result = reader.next( line )) != OnlineLineReader::NONE)
				{
					OnlineMessage message;
					const bool ok = result == OnlineLineReader::LINE && OnlineMessage::parse( line.data(), line.size(), message );
					std::lock_guard<std::mutex> guard( lock );
					if (ok)
						incoming.push_back( message );
					else
						++dropped;
				}
			}
			else if (code == 0 || code == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
				why = "the server closed the connection";
			else if (!isRetry( code ))
				why = "the connection broke: " + describe( code );
		}
		if (stop)
			mbedtls_ssl_close_notify( &ssl );
	} while (false);

	mbedtls_net_free( &net );
	mbedtls_ssl_free( &ssl );
	mbedtls_ssl_config_free( &conf );
	mbedtls_ctr_drbg_free( &random );
	mbedtls_entropy_free( &entropy );
#ifdef _WIN32
	WSACleanup();
#endif
	finish( why );
}

OnlineConnection::OnlineConnection() : m_impl( new Impl )
{
}

OnlineConnection::~OnlineConnection()
{
	close();
	delete m_impl;
}

bool OnlineConnection::connect( const std::string &host, unsigned short port, const std::string &caPem )
{
	if (m_impl->thread.joinable())
	{
		if (state() != CLOSED)
			return false;		// error() stays the running connection's
		m_impl->thread.join();
	}

	std::lock_guard<std::mutex> guard( m_impl->lock );
	m_impl->state = CLOSED;
	m_impl->error.clear();
	m_impl->outgoing.clear();
	m_impl->incoming.clear();
	m_impl->dropped = 0;
	m_impl->stop = false;
	if (host.empty() || port == 0)
	{
		m_impl->error = "no server address";
		return false;
	}

	// mbedtls_x509_crt_parse wants a PEM's terminating NUL counted in the length
	const std::string pem = caPem.empty() ? std::string( defaultRoots() ) : caPem;
	mbedtls_x509_crt_free( &m_impl->ca );
	mbedtls_x509_crt_init( &m_impl->ca );
	const int code = mbedtls_x509_crt_parse( &m_impl->ca, (const unsigned char *)pem.c_str(), pem.size() + 1 );
	if (code < 0 || m_impl->ca.raw.len == 0)
	{
		m_impl->error = "the CA certificate does not parse: " + describe( code < 0 ? code : MBEDTLS_ERR_X509_INVALID_FORMAT );
		return false;
	}

	m_impl->host = host;
	m_impl->port = port;
	m_impl->state = CONNECTING;
	m_impl->thread = std::thread( &Impl::run, m_impl );
	return true;
}

bool OnlineConnection::send( const OnlineMessage &message )
{
	std::string line;
	if (!message.toLine( line ))
		return false;
	std::lock_guard<std::mutex> guard( m_impl->lock );
	if (m_impl->state != CONNECTING && m_impl->state != OPEN)
		return false;
	m_impl->outgoing.push_back( line );
	return true;
}

bool OnlineConnection::receive( OnlineMessage &message )
{
	std::lock_guard<std::mutex> guard( m_impl->lock );
	if (m_impl->incoming.empty())
		return false;
	message = m_impl->incoming.front();
	m_impl->incoming.pop_front();
	return true;
}

void OnlineConnection::close()
{
	m_impl->stop = true;
	if (m_impl->thread.joinable())
		m_impl->thread.join();
	std::lock_guard<std::mutex> guard( m_impl->lock );
	if (m_impl->state != IDLE)
		m_impl->state = CLOSED;
}

OnlineConnection::State OnlineConnection::state() const
{
	std::lock_guard<std::mutex> guard( m_impl->lock );
	return m_impl->state;
}

std::string OnlineConnection::error() const
{
	std::lock_guard<std::mutex> guard( m_impl->lock );
	return m_impl->error;
}

unsigned OnlineConnection::droppedLines() const
{
	std::lock_guard<std::mutex> guard( m_impl->lock );
	return m_impl->dropped;
}

// https://letsencrypt.org/certs/isrgrootx1.pem (SHA-256 96:BC:EC:06:...:DF:08:C6, until 2035-06-04) and
// https://letsencrypt.org/certs/isrg-root-x2.pem (SHA-256 69:72:9B:8E:...:CB:14:70, until 2040-09-17).
const char *OnlineConnection::defaultRoots()
{
	return
		"-----BEGIN CERTIFICATE-----\n"
		"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\n"
		"-----END CERTIFICATE-----\n"
		"-----BEGIN CERTIFICATE-----\n"
		"MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n"
		"CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n"
		"R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n"
		"MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n"
		"ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n"
		"EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n"
		"+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n"
		"ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n"
		"AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n"
		"zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n"
		"tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n"
		"/q4AaOeMSQ+2b1tbFfLn\n"
		"-----END CERTIFICATE-----\n";
}
