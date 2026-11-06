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

// FILE: OnlineConnection.h ///////////////////////////////////////////////////////////////////////
// Desc:   The game's line to the online service: TLS over TCP, one flat JSON object per line.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _ONLINE_CONNECTION_H_
#define _ONLINE_CONNECTION_H_

#include <string>
#include <utility>
#include <vector>

/* The lobby protocol (docs/plans/ONLINE-SERVICE.md in the owner's workspace) sends one JSON object per
	 line, at most ONLINE_MAX_LINE bytes before the newline, its type in "t".  A value is a scalar (string,
	 number, true, false, null) or an array of scalars; nothing nests further, which is what keeps this
	 reader a page long.  Nothing in this file or its .cpp knows the engine: it builds as a library of its
	 own, so test_online drives it without linking the game. */

enum { ONLINE_MAX_LINE = 8192 };

struct OnlineValue
{
	enum Kind { NUL, BOOLEAN, NUMBER, STRING, ARRAY };
	Kind kind;
	bool boolean;
	std::string text;									///< a STRING's UTF-8, or a NUMBER's JSON spelling (kept as text, so a 64-bit id survives)
	std::vector<OnlineValue> items;		///< an ARRAY's scalars

	OnlineValue() : kind( NUL ), boolean( false ) {}
};

class OnlineMessage
{
public:
	/* Reading.  A missing key or one of another kind gives the default. */
	bool has( const char *key ) const;
	const OnlineValue *find( const char *key ) const;
	std::string getString( const char *key, const char *otherwise = "" ) const;
	long long getInt( const char *key, long long otherwise = 0 ) const;
	double getNumber( const char *key, double otherwise = 0.0 ) const;
	bool getBool( const char *key, bool otherwise = false ) const;
	std::vector<std::string> getStrings( const char *key ) const;		///< an array's strings; other items are skipped
	std::vector<long long> getInts( const char *key ) const;				///< an array's numbers as integers
	std::string type() const { return getString( "t" ); }

	/* Writing.  Setting a key that is there replaces it; keys keep the order they were first set in. */
	OnlineMessage &setString( const char *key, const std::string &value );
	OnlineMessage &setInt( const char *key, long long value );
	OnlineMessage &setNumber( const char *key, double value );		///< NaN and infinity are written as null: JSON has neither
	OnlineMessage &setBool( const char *key, bool value );
	OnlineMessage &setNull( const char *key );
	OnlineMessage &setStrings( const char *key, const std::vector<std::string> &values );
	OnlineMessage &setInts( const char *key, const std::vector<long long> &values );
	OnlineMessage &set( const char *key, const OnlineValue &value );

	void clear() { m_fields.clear(); }
	size_t size() const { return m_fields.size(); }
	const std::vector< std::pair<std::string, OnlineValue> > &fields() const { return m_fields; }

	/* The object as one line, newline included.  False when a string is not valid UTF-8 or the line would
		 be longer than ONLINE_MAX_LINE; out is left empty then. */
	bool toLine( std::string &out ) const;

	/* One line, without its newline (a trailing CR is allowed).  False on anything but a flat object of
		 scalars and arrays of scalars in valid UTF-8: a nested object or array, a duplicate key, a bad
		 escape, a lone surrogate, \u0000, a raw control character, trailing text.  out is cleared first. */
	static bool parse( const char *line, size_t length, OnlineMessage &out );

private:
	std::vector< std::pair<std::string, OnlineValue> > m_fields;
};

/* Cuts a byte stream into lines.  A line longer than ONLINE_MAX_LINE is dropped whole, up to and
	 including its newline, and reported once as OVERSIZE; blank lines are skipped. */
class OnlineLineReader
{
public:
	enum Result { NONE, LINE, OVERSIZE };

	OnlineLineReader() : m_skipping( false ) {}
	void feed( const char *bytes, size_t count );
	Result next( std::string &line );		///< line without its newline or a trailing CR

private:
	std::string m_buffer;
	bool m_skipping;			///< inside an oversize line, throwing bytes away until its newline
};

/* True when the bytes are UTF-8 as RFC 3629 has it: no overlong forms, no surrogates, nothing past U+10FFFF. */
bool OnlineIsUtf8( const char *bytes, size_t count );

/* A TLS client to the service on a thread of its own, so a slow DNS answer, an unreachable host or a
	 handshake over a bad link never holds up a game frame.  The game calls receive() once a pass and
	 send() whenever it likes; both only touch queues.

		 OnlineConnection c;
		 c.connect( "lobby.example", 7000, caPem );		// caPem empty: the Let's Encrypt roots below
		 ...
		 OnlineMessage m;
		 while (c.receive( m )) handle( m );
		 if (c.state() == OnlineConnection::CLOSED) report( c.error() );

	 The certificate is checked against the CA in caPem and the host name given to connect().  TLS 1.2
	 only for now; the .cpp says why. */
class OnlineConnection
{
public:
	enum State { IDLE, CONNECTING, OPEN, CLOSED };

	OnlineConnection();
	~OnlineConnection();		///< closes: tens of milliseconds, or what is left of a DNS lookup under way

	/* Starts the thread.  False when a connection is already running (error() is then left alone), and
		 false with error() set when the address is empty or the CA does not parse; nothing has touched the
		 network then. */
	bool connect( const std::string &host, unsigned short port, const std::string &caPem );

	bool send( const OnlineMessage &message );		///< queued; false when not CONNECTING or OPEN, or toLine() refuses it
	bool receive( OnlineMessage &message );				///< the next message that arrived, if any
	void close();																	///< hangs up and joins the thread; state() is CLOSED after

	State state() const;
	std::string error() const;			///< why the connection ended or failed to start; empty while it is fine
	unsigned droppedLines() const;	///< received lines that were oversize or not a flat JSON object

	/* ISRG Root X1 and X2, the roots Let's Encrypt chains to, as PEM. */
	static const char *defaultRoots();

private:
	OnlineConnection( const OnlineConnection & );
	OnlineConnection &operator=( const OnlineConnection & );

	struct Impl;
	Impl *m_impl;
};

#endif // _ONLINE_CONNECTION_H_
