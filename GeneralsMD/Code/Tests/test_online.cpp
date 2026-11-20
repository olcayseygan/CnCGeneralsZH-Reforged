/*
 * test_online: the online service's connection (GameNetwork/Online/OnlineConnection).
 *
 * The JSON line reader and writer through round trips and every way a line can be wrong, the line cutter
 * fed a byte at a time and past its limit, then a real TLS connection over loopback to a server running
 * in this process.  The server's certificate is signed by a CA the test makes on the spot, so nothing
 * secret is committed and nothing expires: the client has to accept it against that CA and refuse it
 * against another.
 *
 * Last the adapters (OnlinePeer, OnlineBuddy, OnlineStats): the GameSpy queue interfaces fed the
 * server's lines as online/server.js writes them, checked for the responses EA's WOL screens read and
 * for the lines the screens' requests send.
 */

#include "test_harness.h"

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "Lib/WideCharFns.h"
#include <map>
#include "GameNetwork/Online/OnlineConnection.h"
#include "GameNetwork/Online/OnlineSession.h"
#include "GameNetwork/GameInfo.h"
#include "Common/InstallFiles.h"
#include "GameClient/TankSkin.h"
#include "Common/MultiplayerSettings.h"
#include "Common/GlobalData.h"		// TEXT_LANGUAGE_*

#include <chrono>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <winsock2.h>
	typedef int TestSockLen;
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	typedef socklen_t TestSockLen;
#endif

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

//-------------------------------------------------------------------------------------------------
// the JSON line
//-------------------------------------------------------------------------------------------------

static bool parseText( const std::string &line, OnlineMessage &out )
{
	return OnlineMessage::parse( line.data(), line.size(), out );
}

TEST(online_json_round_trip)
{
	const std::string awkward = std::string( "quote \" backslash \\ slash / newline \n tab \t bell \x01 del \x7f" " end" );
	const std::string turkish = "\xC3\x87" "a\xC4\x9F" "r\xC4\xB1 \xE2\x9C\x93 \xF0\x9F\x98\x80";		// a Turkish name, a check mark and an emoji
	std::vector<std::string> maps;
	maps.push_back( "Tournament Desert" );
	maps.push_back( turkish );
	maps.push_back( "" );
	std::vector<long long> ids;
	ids.push_back( -1 );
	ids.push_back( 0 );
	ids.push_back( 9007199254740993LL );		// 2^53 + 1: lost if numbers went through a double

	OnlineMessage m;
	m.setString( "t", "qm.join" ).setString( "text", awkward ).setString( "nick", turkish )
		.setInt( "exe", 0x89ABCDEFLL ).setInt( "big", 9007199254740993LL ).setInt( "neg", -42 )
		.setNumber( "half", 0.5 ).setNumber( "nan", NAN ).setBool( "yes", true ).setBool( "no", false )
		.setNull( "nothing" ).setStrings( "maps", maps ).setInts( "ids", ids ).setStrings( "none", std::vector<std::string>() );
	m.setInt( "neg", -43 );		// replaces in place

	std::string line;
	CHECK( m.toLine( line ) );
	CHECK( !line.empty() && line[ line.size() - 1 ] == '\n' );
	CHECK_EQ( line.find( '\n' ), line.size() - 1 );		// one line: the newline in the text went out escaped
	CHECK_EQ( line.find( "\"t\":\"qm.join\"" ), (size_t)1 );		// keys keep their first order

	OnlineMessage back;
	CHECK( OnlineMessage::parse( line.data(), line.size() - 1, back ) );
	CHECK_EQ( back.size(), m.size() );
	CHECK( back.type() == "qm.join" );
	CHECK( back.getString( "text" ) == awkward );
	CHECK( back.getString( "nick" ) == turkish );
	CHECK_EQ( back.getInt( "exe" ), 0x89ABCDEFLL );
	CHECK_EQ( back.getInt( "big" ), 9007199254740993LL );
	CHECK_EQ( back.getInt( "neg" ), -43 );
	CHECK_EQ( back.getNumber( "half" ), 0.5 );
	CHECK( back.has( "nan" ) && back.find( "nan" )->kind == OnlineValue::NUL );
	CHECK( back.getBool( "yes" ) );
	CHECK( !back.getBool( "no", true ) );
	CHECK( back.find( "nothing" )->kind == OnlineValue::NUL );
	CHECK( back.getStrings( "maps" ) == maps );
	CHECK( back.getInts( "ids" ) == ids );
	CHECK( back.find( "none" )->kind == OnlineValue::ARRAY && back.find( "none" )->items.empty() );

	// missing keys and wrong kinds give the default
	CHECK( back.getString( "missing", "x" ) == "x" );
	CHECK_EQ( back.getInt( "nick", 7 ), 7 );
	CHECK( back.getString( "exe" ).empty() );

	OnlineMessage empty;
	CHECK( empty.toLine( line ) );
	CHECK( line == "{}\n" );
}

TEST(online_json_reads_escapes_and_spacing)
{
	OnlineMessage m;
	CHECK( parseText( " { \"t\" : \"say\" ,\t\"text\":\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\\u00e7\\u00C7\\u2713\\ud83d\\ude00\" , \"n\" : -1.5e+2 , \"z\":0 , \"e\":1E3 , \"a\" : [ 1 , \"x\" , true , null ] }\r", m ) );
	CHECK( m.getString( "text" ) == "a\"b\\c/d\b\f\n\r\t\xC3\xA7\xC3\x87\xE2\x9C\x93\xF0\x9F\x98\x80" );
	CHECK_EQ( m.getNumber( "n" ), -150.0 );
	CHECK_EQ( m.getInt( "n" ), -150 );
	CHECK_EQ( m.getInt( "z" ), 0 );
	CHECK_EQ( m.getInt( "e" ), 1000 );
	const OnlineValue *a = m.find( "a" );
	CHECK( a && a->kind == OnlineValue::ARRAY && a->items.size() == 4 );
	CHECK( parseText( "{}", m ) && m.size() == 0 );
	CHECK( parseText( "{\"a\":[]}", m ) && m.find( "a" )->items.empty() );
	CHECK( parseText( "{\"\":\"empty key\"}", m ) && m.getString( "" ) == "empty key" );
	// raw UTF-8 passes through untouched
	CHECK( parseText( "{\"t\":\"\xC3\x87\xF0\x9F\x98\x80\"}", m ) && m.type() == "\xC3\x87\xF0\x9F\x98\x80" );
}

TEST(online_json_refuses_what_is_not_a_flat_object)
{
	const char *bad[] = {
		"",
		"   ",
		"[]",
		"\"t\"",
		"{",
		"{\"t\"}",
		"{\"t\":}",
		"{\"t\":\"x\",}",
		"{\"t\":\"x\"} trailing",
		"{\"t\":\"x\"}{}",
		"{\"t\":{\"a\":{\"b\":1}}}",				// an object in an object
		"{\"t\":{\"a\":[{}]}}",
		"{\"t\":[{\"a\":[{}]}]}",
		"{\"t\":[{\"a\":{}}]}",
		"{\"t\":{\"a\":1,\"a\":2}}",				// duplicate key inside
		"{\"t\":{\"a\":1}",
		"{\"t\":[1,[2]]}",
		"{\"t\":[1,]}",
		"{\"t\":[1 2]}",
		"{\"t\":\"a\",\"t\":\"b\"}",		// duplicate key
		"{\"t\":\"unterminated}",
		"{\"t\":\"\\x\"}",							// unknown escape
		"{\"t\":\"\\u12\"}",						// short \u
		"{\"t\":\"\\u12g4\"}",
		"{\"t\":\"\\ud83d\"}",					// high surrogate alone
		"{\"t\":\"\\ud83d\\u0041\"}",		// high surrogate, then not a low one
		"{\"t\":\"\\ude00\"}",					// low surrogate alone
		"{\"t\":\"\\u0000\"}",					// NUL
		"{\"t\":\"a\tb\"}",							// raw control character
		"{\"t\":01}",
		"{\"t\":1.}",
		"{\"t\":.5}",
		"{\"t\":-}",
		"{\"t\":1e}",
		"{\"t\":+1}",
		"{\"t\":tru}",
		"{\"t\":True}",
		"{\"t\":NaN}",
		"{t:1}",
		"{'t':1}",
		"{\"t\":\"\xC0\x80\"}",					// overlong NUL
		"{\"t\":\"\xE0\x80\xAF\"}",			// overlong '/'
		"{\"t\":\"\xED\xA0\x80\"}",			// a surrogate written as UTF-8
		"{\"t\":\"\xF4\x90\x80\x80\"}",	// past U+10FFFF
		"{\"t\":\"\xC3\"}",							// cut short
		"{\"t\":\"\xFF\"}",
	};
	for (size_t i = 0; i < sizeof( bad ) / sizeof( bad[ 0 ] ); ++i)
	{
		OnlineMessage m;
		m.setString( "stale", "x" );
		const bool parsed = parseText( bad[ i ], m );
		if (parsed)
			printf( "  accepted: %s\n", bad[ i ] );
		CHECK( !parsed );
		CHECK_EQ( m.size(), (size_t)0 );		// cleared, never half filled
	}
	// a NUL byte in the middle of the line
	const std::string withNul( "{\"t\":\"a\0b\"}", 11 );
	OnlineMessage m;
	CHECK( !parseText( withNul, m ) );
}

/* One level of object: a ladder's rows and own row, a match's opponents. */
TEST(online_json_reads_one_level_of_objects)
{
	OnlineMessage m;
	CHECK( parseText( "{\"t\":\"ladder\",\"offset\":20,\"rows\":[{\"rank\":21,\"nick\":\"Kim\",\"rating\":31,\"tier\":\"major\",\"wins\":5,\"losses\":2},"
		"{\"rank\":22,\"nick\":\"Lee\",\"rating\":null,\"tier\":null,\"wins\":0,\"losses\":0}, 3],\"me\":{\"rank\":40,\"nick\":\"Tank\",\"tags\":[1,2]},\"none\":null}", m ) );
	const std::vector<OnlineMessage> rows = m.getObjects( "rows" );
	CHECK_EQ( rows.size(), (size_t)2 );		// the 3 is skipped
	if (rows.size() == 2)
	{
		CHECK_EQ( rows[0].getInt( "rank" ), 21 );
		CHECK_STR( rows[0].getString( "nick" ).c_str(), "Kim" );
		CHECK_STR( rows[0].getString( "tier" ).c_str(), "major" );
		CHECK( !rows[1].has( "rating" ) || rows[1].find( "rating" )->kind == OnlineValue::NUL );
		CHECK_EQ( rows[1].getInt( "rating", -1 ), -1 );
	}
	OnlineMessage me;
	CHECK( m.getObject( "me", me ) );
	CHECK_EQ( me.getInt( "rank" ), 40 );
	CHECK_EQ( me.getInts( "tags" ).size(), (size_t)2 );
	CHECK( !m.getObject( "none", me ) );
	CHECK_EQ( me.size(), (size_t)0 );
	CHECK( !m.getObject( "rows", me ) );
	CHECK( m.getObjects( "me" ).empty() );

	CHECK( parseText( "{\"t\":{},\"u\":[{}]}", m ) );
	CHECK( m.getObject( "t", me ) );
	CHECK_EQ( m.getObjects( "u" ).size(), (size_t)1 );

	// an object goes out as it came in (an ai.result's stats); an array of objects never does
	std::string line;
	CHECK( !m.toLine( line ) );
	CHECK( line.empty() );
	OnlineMessage stats, out, back;
	std::vector<long long> pair;
	pair.push_back( 1 );
	pair.push_back( 2 );
	stats.setInt( "unitsBuilt", 12 ).setString( "army", "FactionChina" ).setInts( "pair", pair );
	CHECK( out.setString( "t", "ai.result" ).setObject( "stats", stats ).toLine( line ) );
	CHECK_STR( line.c_str(), "{\"t\":\"ai.result\",\"stats\":{\"unitsBuilt\":12,\"army\":\"FactionChina\",\"pair\":[1,2]}}\n" );
	CHECK( OnlineMessage::parse( line.data(), line.size() - 1, back ) );
	CHECK( back.getObject( "stats", me ) );
	CHECK_EQ( me.getInt( "unitsBuilt" ), 12LL );
	CHECK( !out.setObject( "stats", m ).toLine( line ) );		// an object in an object
	CHECK( line.empty() );
}

TEST(online_json_line_limit)
{
	// {"t":"..."} is 8 bytes around the text
	OnlineMessage m;
	m.setString( "t", std::string( ONLINE_MAX_LINE - 8, 'x' ) );
	std::string line;
	CHECK( m.toLine( line ) );
	CHECK_EQ( line.size(), (size_t)ONLINE_MAX_LINE + 1 );
	OnlineMessage back;
	CHECK( OnlineMessage::parse( line.data(), line.size() - 1, back ) );

	m.setString( "t", std::string( ONLINE_MAX_LINE - 7, 'x' ) );
	CHECK( !m.toLine( line ) );
	CHECK( line.empty() );
	const std::string over = "{\"t\":\"" + std::string( ONLINE_MAX_LINE - 7, 'x' ) + "\"}";
	CHECK( !parseText( over, back ) );

	// escapes count at their written length
	m.setString( "t", std::string( ONLINE_MAX_LINE / 2, '"' ) );
	CHECK( !m.toLine( line ) );
}

TEST(online_json_writer_refuses_bad_text)
{
	OnlineMessage m;
	std::string line;
	CHECK( !m.setString( "t", "\xC3" ).toLine( line ) );
	CHECK( !m.setString( "t", "\xED\xA0\x80" ).toLine( line ) );
	CHECK( !m.setString( "t", std::string( "a\0b", 3 ) ).toLine( line ) );
	m.clear();
	CHECK( !m.setString( "\xFF", "x" ).toLine( line ) );
	m.clear();
	std::vector<std::string> list;
	list.push_back( "ok" );
	list.push_back( "\xC0\x80" );
	CHECK( !m.setStrings( "t", list ).toLine( line ) );
	m.clear();
	OnlineValue nested;
	nested.kind = OnlineValue::ARRAY;
	nested.items.push_back( OnlineValue() );
	nested.items[ 0 ].kind = OnlineValue::ARRAY;
	CHECK( !m.set( "t", nested ).toLine( line ) );
	m.clear();
	OnlineValue number;
	number.kind = OnlineValue::NUMBER;
	number.text = "1e";
	CHECK( !m.set( "t", number ).toLine( line ) );
}

TEST(online_utf8_validator)
{
	CHECK( OnlineIsUtf8( "", 0 ) );
	CHECK( OnlineIsUtf8( "plain", 5 ) );
	CHECK( OnlineIsUtf8( "\xC2\x80", 2 ) );						// U+0080, the first two-byte one
	CHECK( OnlineIsUtf8( "\xEF\xBF\xBF", 3 ) );					// U+FFFF
	CHECK( OnlineIsUtf8( "\xF4\x8F\xBF\xBF", 4 ) );			// U+10FFFF
	CHECK( !OnlineIsUtf8( "\xC1\xBF", 2 ) );
	CHECK( !OnlineIsUtf8( "\xF0\x8F\xBF\xBF", 4 ) );		// overlong
	CHECK( !OnlineIsUtf8( "\x80", 1 ) );
	CHECK( !OnlineIsUtf8( "\xE2\x9C", 2 ) );
	CHECK( !OnlineIsUtf8( "\xE2\x28\x93", 3 ) );
	CHECK( !OnlineIsUtf8( "\xF8\x88\x80\x80\x80", 5 ) );
}

//-------------------------------------------------------------------------------------------------
// the line cutter
//-------------------------------------------------------------------------------------------------

TEST(online_lines_arrive_in_pieces)
{
	const std::string stream = "{\"t\":\"a\"}\r\n\n\r\n{\"t\":\"b\",\"text\":\"\xC3\x87\"}\n{\"t\":\"c\"}";
	OnlineLineReader reader;
	std::vector<std::string> lines;
	std::string line;
	for (size_t i = 0; i < stream.size(); ++i)
	{
		reader.feed( &stream[ i ], 1 );
		OnlineLineReader::Result r;
		while ((r = reader.next( line )) != OnlineLineReader::NONE)
		{
			CHECK_EQ( r, OnlineLineReader::LINE );
			lines.push_back( line );
		}
	}
	CHECK_EQ( lines.size(), (size_t)2 );		// the third has no newline yet
	CHECK( lines.size() == 2 && lines[ 0 ] == "{\"t\":\"a\"}" && lines[ 1 ] == "{\"t\":\"b\",\"text\":\"\xC3\x87\"}" );
	reader.feed( "\n", 1 );
	CHECK_EQ( reader.next( line ), OnlineLineReader::LINE );
	CHECK( line == "{\"t\":\"c\"}" );
	CHECK_EQ( reader.next( line ), OnlineLineReader::NONE );
}

TEST(online_lines_over_the_limit_are_dropped_once)
{
	OnlineLineReader reader;
	std::string line;

	// exactly the limit, with a CRLF, is a line
	std::string full( ONLINE_MAX_LINE, 'a' );
	full += "\r\n";
	reader.feed( full.data(), full.size() );
	CHECK_EQ( reader.next( line ), OnlineLineReader::LINE );
	CHECK_EQ( line.size(), (size_t)ONLINE_MAX_LINE );

	// one over, complete in one feed, followed by a good line
	std::string over( ONLINE_MAX_LINE + 1, 'b' );
	over += "\n{\"t\":\"ok\"}\n";
	reader.feed( over.data(), over.size() );
	CHECK_EQ( reader.next( line ), OnlineLineReader::OVERSIZE );
	CHECK_EQ( reader.next( line ), OnlineLineReader::LINE );
	CHECK( line == "{\"t\":\"ok\"}" );
	CHECK_EQ( reader.next( line ), OnlineLineReader::NONE );

	// a long one arriving in chunks is reported once and thrown away up to its newline, even when the
	// newline and the next line come together
	const std::string chunk( 1000, 'c' );
	int oversize = 0, lines = 0;
	for (int i = 0; i < 30; ++i)
	{
		reader.feed( chunk.data(), chunk.size() );
		OnlineLineReader::Result r;
		while ((r = reader.next( line )) != OnlineLineReader::NONE)
			(r == OnlineLineReader::OVERSIZE ? oversize : lines)++;
	}
	reader.feed( "ccc\n{\"t\":\"after\"}\n", 18 );
	OnlineLineReader::Result r;
	while ((r = reader.next( line )) != OnlineLineReader::NONE)
		(r == OnlineLineReader::OVERSIZE ? oversize : lines)++;
	CHECK_EQ( oversize, 1 );
	CHECK_EQ( lines, 1 );
	CHECK( line == "{\"t\":\"after\"}" );
}

//-------------------------------------------------------------------------------------------------
// TLS over loopback
//-------------------------------------------------------------------------------------------------

static const char *TEST_CA_NAME = "CN=Zero Hour Reforged test CA (test only),O=test_online";

struct TestPki
{
	bool ok;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context random;
	mbedtls_pk_context caKey, otherCaKey, serverKey;
	std::string caPem, otherCaPem, serverPem;
};

static bool makeKey( mbedtls_pk_context &key, mbedtls_ctr_drbg_context &random )
{
	return mbedtls_pk_setup( &key, mbedtls_pk_info_from_type( MBEDTLS_PK_ECKEY ) ) == 0
		&& mbedtls_ecp_gen_key( MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec( key ), mbedtls_ctr_drbg_random, &random ) == 0;
}

static std::string makeCertificate( mbedtls_pk_context &subjectKey, const char *subject, mbedtls_pk_context &issuerKey,
																		const char *issuer, unsigned char serial, bool authority, mbedtls_ctr_drbg_context &random )
{
	mbedtls_x509write_cert crt;
	mbedtls_x509write_crt_init( &crt );
	unsigned char pem[ 4096 ];
	mbedtls_x509write_crt_set_version( &crt, MBEDTLS_X509_CRT_VERSION_3 );
	mbedtls_x509write_crt_set_md_alg( &crt, MBEDTLS_MD_SHA256 );
	mbedtls_x509write_crt_set_subject_key( &crt, &subjectKey );
	mbedtls_x509write_crt_set_issuer_key( &crt, &issuerKey );
	const bool ok = mbedtls_x509write_crt_set_serial_raw( &crt, &serial, 1 ) == 0
		&& mbedtls_x509write_crt_set_validity( &crt, "20250101000000", "20991231235959" ) == 0
		&& mbedtls_x509write_crt_set_subject_name( &crt, subject ) == 0
		&& mbedtls_x509write_crt_set_issuer_name( &crt, issuer ) == 0
		&& mbedtls_x509write_crt_set_basic_constraints( &crt, authority ? 1 : 0, -1 ) == 0
		&& mbedtls_x509write_crt_set_key_usage( &crt, authority
				? (MBEDTLS_X509_KU_KEY_CERT_SIGN | MBEDTLS_X509_KU_CRL_SIGN) : MBEDTLS_X509_KU_DIGITAL_SIGNATURE ) == 0
		&& mbedtls_x509write_crt_pem( &crt, pem, sizeof( pem ), mbedtls_ctr_drbg_random, &random ) == 0;
	mbedtls_x509write_crt_free( &crt );
	return ok ? std::string( (const char *)pem ) : std::string();
}

// made once and kept for the run; nothing of it is written anywhere
static TestPki &pki()
{
	static TestPki p;
	static bool made = false;
	if (made)
		return p;
	made = true;
	mbedtls_entropy_init( &p.entropy );
	mbedtls_ctr_drbg_init( &p.random );
	mbedtls_pk_init( &p.caKey );
	mbedtls_pk_init( &p.otherCaKey );
	mbedtls_pk_init( &p.serverKey );
	p.ok = mbedtls_ctr_drbg_seed( &p.random, mbedtls_entropy_func, &p.entropy, NULL, 0 ) == 0
		&& makeKey( p.caKey, p.random ) && makeKey( p.otherCaKey, p.random ) && makeKey( p.serverKey, p.random );
	if (p.ok)
	{
		// the other CA has the same name and only another key: refused on the signature alone
		p.caPem = makeCertificate( p.caKey, TEST_CA_NAME, p.caKey, TEST_CA_NAME, 1, true, p.random );
		p.otherCaPem = makeCertificate( p.otherCaKey, TEST_CA_NAME, p.otherCaKey, TEST_CA_NAME, 1, true, p.random );
		p.serverPem = makeCertificate( p.serverKey, "CN=127.0.0.1", p.caKey, TEST_CA_NAME, 2, false, p.random );
		p.ok = !p.caPem.empty() && !p.otherCaPem.empty() && !p.serverPem.empty();
	}
	return p;
}

/* One connection: accept, handshake, read the client's first line, answer with a scripted stream (two
	 lines in one write, a long line in two writes with a pause, a nested line and an oversize one the
	 client must drop, then "bye"), and wait for the client to hang up. */
struct TestServer
{
	mbedtls_net_context listener;
	mbedtls_x509_crt chain;
	unsigned short port;
	int handshake;
	std::string received;
	std::thread thread;

	TestServer() : port( 0 ), handshake( 1 ) { mbedtls_net_init( &listener ); mbedtls_x509_crt_init( &chain ); }
	~TestServer()
	{
		if (thread.joinable())
			thread.join();
		mbedtls_net_free( &listener );
		mbedtls_x509_crt_free( &chain );
	}

	bool start()
	{
		const std::string &pem = pki().serverPem;
		if (mbedtls_x509_crt_parse( &chain, (const unsigned char *)pem.c_str(), pem.size() + 1 ) != 0
			|| mbedtls_net_bind( &listener, "127.0.0.1", "0", MBEDTLS_NET_PROTO_TCP ) != 0)
			return false;
		sockaddr_in address;
		TestSockLen size = sizeof( address );
		if (getsockname( listener.fd, (sockaddr *)&address, &size ) != 0)
			return false;
		port = ntohs( address.sin_port );
		thread = std::thread( &TestServer::serve, this );
		return true;
	}

	static bool writeAll( mbedtls_ssl_context &ssl, const std::string &bytes )
	{
		size_t done = 0;
		while (done < bytes.size())
		{
			const int n = mbedtls_ssl_write( &ssl, (const unsigned char *)bytes.data() + done, bytes.size() - done );
			if (n > 0)
				done += n;
			else if (n != MBEDTLS_ERR_SSL_WANT_WRITE && n != MBEDTLS_ERR_SSL_WANT_READ)
				return false;
		}
		return true;
	}

	void serve()
	{
		mbedtls_net_context client;
		mbedtls_ssl_context ssl;
		mbedtls_ssl_config conf;
		mbedtls_entropy_context entropy;
		mbedtls_ctr_drbg_context random;
		mbedtls_net_init( &client );
		mbedtls_ssl_init( &ssl );
		mbedtls_ssl_config_init( &conf );
		mbedtls_entropy_init( &entropy );
		mbedtls_ctr_drbg_init( &random );
		if (mbedtls_ctr_drbg_seed( &random, mbedtls_entropy_func, &entropy, NULL, 0 ) == 0
			&& mbedtls_net_accept( &listener, &client, NULL, 0, NULL ) == 0
			&& mbedtls_ssl_config_defaults( &conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT ) == 0)
		{
			mbedtls_ssl_conf_rng( &conf, mbedtls_ctr_drbg_random, &random );
			mbedtls_ssl_conf_max_tls_version( &conf, MBEDTLS_SSL_VERSION_TLS1_2 );		// as the client; OnlineConnection.cpp says why
			mbedtls_ssl_conf_read_timeout( &conf, 5000 );
			if (mbedtls_ssl_conf_own_cert( &conf, &chain, &pki().serverKey ) == 0 && mbedtls_ssl_setup( &ssl, &conf ) == 0)
			{
				mbedtls_ssl_set_bio( &ssl, &client, mbedtls_net_send, NULL, mbedtls_net_recv_timeout );
				while ((handshake = mbedtls_ssl_handshake( &ssl )) == MBEDTLS_ERR_SSL_WANT_READ || handshake == MBEDTLS_ERR_SSL_WANT_WRITE)
					;
			}
		}
		if (handshake == 0)
		{
			unsigned char bytes[ 1024 ];
			while (received.find( '\n' ) == std::string::npos)
			{
				const int n = mbedtls_ssl_read( &ssl, bytes, sizeof( bytes ) );
				if (n <= 0)
					break;
				received.append( (const char *)bytes, n );
			}
			const std::string big = "{\"t\":\"said\",\"text\":\"" + std::string( 8000, 'x' ) + "\"}\n";
			writeAll( ssl, "{\"t\":\"welcome\",\"motd\":\"h\\u00e9llo\",\"relayPort\":7001}\n"
										 "{\"t\":\"auth.ok\",\"id\":9007199254740993,\"nick\":\"\xC3\x87" "a\xC4\x9F" "r\xC4\xB1\"}\n" );
			writeAll( ssl, big.substr( 0, 3000 ) );
			std::this_thread::sleep_for( std::chrono::milliseconds( 60 ) );
			writeAll( ssl, big.substr( 3000 ) );
			writeAll( ssl, "{\"t\":[1,[2]]}\n" + std::string( 9000, 'y' ) + "\n{\"t\":\"bye\"}\n" );
			while (mbedtls_ssl_read( &ssl, bytes, sizeof( bytes ) ) > 0)
				;		// until the client's close_notify, or five quiet seconds
		}
		mbedtls_ssl_close_notify( &ssl );
		mbedtls_net_free( &client );
		mbedtls_ssl_free( &ssl );
		mbedtls_ssl_config_free( &conf );
		mbedtls_ctr_drbg_free( &random );
		mbedtls_entropy_free( &entropy );
	}
};

// the messages that arrive until "bye" or the connection ends, ten seconds at most
static std::vector<OnlineMessage> collect( OnlineConnection &c )
{
	std::vector<OnlineMessage> got;
	const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
	while (std::chrono::steady_clock::now() < give_up)
	{
		OnlineMessage m;
		while (c.receive( m ))
			got.push_back( m );
		if ((!got.empty() && got.back().type() == "bye") || c.state() == OnlineConnection::CLOSED)
			break;
		std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
	}
	return got;
}

TEST(online_tls_loopback_against_the_test_ca)
{
	CHECK( pki().ok );
	TestServer server;
	CHECK( server.start() );
	if (!pki().ok || server.port == 0)
		return;

	OnlineConnection c;
	CHECK_EQ( c.state(), OnlineConnection::IDLE );
	CHECK( c.connect( "127.0.0.1", server.port, pki().caPem ) );
	CHECK( !c.connect( "127.0.0.1", server.port, pki().caPem ) );		// one at a time
	OnlineMessage hello;
	hello.setString( "t", "hello" ).setInt( "exe", 0x12345678 ).setInt( "ini", 0x9ABCDEF0LL ).setString( "ver", "test" );
	CHECK( c.send( hello ) );		// queued before the handshake is through, sent once it is

	const std::vector<OnlineMessage> got = collect( c );
	CHECK_EQ( got.size(), (size_t)4 );
	if (got.size() == 4)
	{
		CHECK( got[ 0 ].type() == "welcome" );
		CHECK( got[ 0 ].getString( "motd" ) == "h\xC3\xA9llo" );
		CHECK_EQ( got[ 0 ].getInt( "relayPort" ), 7001 );
		CHECK( got[ 1 ].type() == "auth.ok" );
		CHECK_EQ( got[ 1 ].getInt( "id" ), 9007199254740993LL );
		CHECK( got[ 1 ].getString( "nick" ) == "\xC3\x87" "a\xC4\x9F" "r\xC4\xB1" );
		CHECK( got[ 2 ].type() == "said" );
		CHECK_EQ( got[ 2 ].getString( "text" ).size(), (size_t)8000 );
		CHECK( got[ 3 ].type() == "bye" );
	}
	CHECK_EQ( c.droppedLines(), 2u );		// the nested one and the oversize one
	CHECK_EQ( c.unsentLines(), 0u );			// the hello is written: the answers came after it, on the same thread
	CHECK_EQ( c.state(), OnlineConnection::OPEN );
	if (!c.error().empty())
		printf( "  error: %s\n", c.error().c_str() );
	CHECK( c.error().empty() );

	c.close();
	CHECK_EQ( c.state(), OnlineConnection::CLOSED );
	CHECK( c.error().empty() );		// a hang-up asked for is not an error
	CHECK( !c.send( hello ) );
	if (server.thread.joinable())
		server.thread.join();
	CHECK_EQ( server.handshake, 0 );
	OnlineMessage first;
	CHECK( !server.received.empty() && parseText( server.received.substr( 0, server.received.size() - 1 ), first ) );
	CHECK( first.type() == "hello" );
	CHECK_EQ( first.getInt( "ini" ), 0x9ABCDEF0LL );
}

TEST(online_tls_refuses_a_server_the_ca_did_not_sign)
{
	CHECK( pki().ok );
	TestServer server;
	CHECK( server.start() );
	if (!pki().ok || server.port == 0)
		return;

	OnlineConnection c;
	CHECK( c.connect( "127.0.0.1", server.port, pki().otherCaPem ) );
	OnlineMessage hello;
	hello.setString( "t", "hello" );
	c.send( hello );
	const std::vector<OnlineMessage> got = collect( c );
	CHECK( got.empty() );
	CHECK_EQ( c.state(), OnlineConnection::CLOSED );
	printf( "  refused with: %s\n", c.error().c_str() );
	CHECK( c.error().find( "certificate" ) != std::string::npos );
	if (server.thread.joinable())
		server.thread.join();
	CHECK( server.handshake != 0 );
	CHECK( server.received.empty() );		// the hello never left the client
}

TEST(online_tls_refuses_a_name_the_certificate_does_not_carry)
{
	CHECK( pki().ok );
	TestServer server;
	CHECK( server.start() );
	if (!pki().ok || server.port == 0)
		return;

	OnlineConnection c;
	CHECK( c.connect( "localhost", server.port, pki().caPem ) );		// signed for 127.0.0.1, not this name
	collect( c );
	CHECK_EQ( c.state(), OnlineConnection::CLOSED );
	CHECK( c.error().find( "certificate" ) != std::string::npos );
	if (server.thread.joinable())
		server.thread.join();
}

TEST(online_connect_refuses_bad_arguments)
{
	OnlineConnection c;
	CHECK( !c.connect( "", 7000, "" ) );
	CHECK( !c.error().empty() );
	CHECK( !c.connect( "127.0.0.1", 0, "" ) );
	CHECK( !c.connect( "127.0.0.1", 7000, "-----BEGIN CERTIFICATE-----\nnot base64\n-----END CERTIFICATE-----\n" ) );
	CHECK( c.error().find( "CA" ) != std::string::npos );
	CHECK( c.state() != OnlineConnection::CONNECTING && c.state() != OnlineConnection::OPEN );

	// the built-in Let's Encrypt roots parse
	mbedtls_x509_crt roots;
	mbedtls_x509_crt_init( &roots );
	const std::string pem = OnlineConnection::defaultRoots();
	CHECK_EQ( mbedtls_x509_crt_parse( &roots, (const unsigned char *)pem.c_str(), pem.size() + 1 ), 0 );
	int count = 0;
	for (const mbedtls_x509_crt *crt = &roots; crt != NULL && crt->raw.len > 0; crt = crt->next)
		++count;
	CHECK_EQ( count, 2 );
	mbedtls_x509_crt_free( &roots );
}

//-------------------------------------------------------------------------------------------------
// the adapters, fed scripted server lines
//-------------------------------------------------------------------------------------------------

/* The three queues over a session that sends into `sent` and dials nothing.  feed() is one server line. */
struct AdapterRig
{
	std::vector<OnlineMessage> sent;
	OnlinePeerQueue *peer;
	OnlineBuddyQueue *buddy;
	OnlinePSQueue *ps;

	AdapterRig()
	{
		static bool booted = false;
		if (!booted)
		{
			initMemoryManager();
			booted = true;
		}
		OnlineSession::get().beginTest( &sent );
		peer = new OnlinePeerQueue;
		buddy = new OnlineBuddyQueue;
		ps = new OnlinePSQueue;
	}
	~AdapterRig()
	{
		delete ps;
		delete buddy;
		delete peer;
		OnlineSession::get().beginTest( NULL );
	}

	void feed( const OnlineMessage &m ) { OnlineSession::get().dispatch( m ); }
	void feed( const char *json )
	{
		OnlineMessage m;
		CHECK( parseText( json, m ) );
		feed( m );
	}
	std::vector<PeerResponse> peerResponses()
	{
		std::vector<PeerResponse> out;
		PeerResponse r;
		while (peer->getResponse( r ))
			out.push_back( r );
		return out;
	}
	std::string sentType( size_t i ) const { return i < sent.size() ? sent[i].type() : std::string(); }

	/* Logged in as Tank, id 7, at 10.0.0.3, with the login's answers read and the sent lines cleared.
		 `acks` is a server that answers a report and a replay. */
	void login( bool acks = false )
	{
		BuddyRequest req;
		memset( &req, 0, sizeof(req) );
		req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
		strcpy( req.arg.login.email, "tank@example.com" );
		strcpy( req.arg.login.password, "secret" );
		buddy->addRequest( req );
		feed( acks
			? "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"ticket\":\"00112233445566778899aabbccddeeff\",\"acks\":true}"
			: "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"ticket\":\"00112233445566778899aabbccddeeff\"}" );
		feed( "{\"t\":\"relay.ok\",\"vip\":167772163}" );
		peerResponses();
		BuddyResponse b;
		while (buddy->getResponse( b )) {}
		sent.clear();
	}
};

TEST(online_adapter_login_waits_for_the_relay)
{
	AdapterRig rig;
	BuddyRequest req;
	memset( &req, 0, sizeof(req) );
	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
	strcpy( req.arg.login.nick, "Tank" );
	strcpy( req.arg.login.email, "tank@example.com" );
	strcpy( req.arg.login.password, "secret" );
	rig.buddy->addRequest( req );

	// no install files hashed here: the list still goes, empty, so the server refuses instead of waiting
	CHECK_EQ( rig.sent.size(), (size_t)2 );
	CHECK_STR( rig.sentType( 0 ).c_str(), "files" );
	CHECK_EQ( rig.sent[0].getInt( "seq", -1 ), 0ll );
	CHECK_EQ( rig.sent[0].getInt( "last" ), 1ll );
	CHECK( rig.sent[0].has( "paths" ) && rig.sent[0].getStrings( "paths" ).empty() );
	CHECK( rig.sent[0].has( "hashes" ) && rig.sent[0].getStrings( "hashes" ).empty() );
	CHECK_STR( rig.sentType( 1 ).c_str(), "login" );
	CHECK_STR( rig.sent[1].getString( "email" ).c_str(), "tank@example.com" );
	CHECK_STR( rig.sent[1].getString( "pass" ).c_str(), "secret" );
	CHECK( rig.peer->isConnecting() );
	CHECK( !rig.peer->isConnected() );

	TankSkin_setLocalLoadout( "2" );		// an earlier account's, which the new sign-in replaces
	rig.feed( "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"ticket\":\"00112233445566778899aabbccddeeff\",\"skins\":[1,3]}" );
	CHECK( TankSkin_localLoadout() == ( ( (SkinLoadout)1 << 1 ) | ( (SkinLoadout)1 << 3 ) ) );		// the account's skins, for its own seat
	BuddyResponse b;
	CHECK( rig.buddy->getResponse( b ) );
	CHECK_EQ( (int)b.buddyResponseType, (int)BuddyResponse::BUDDYRESPONSE_LOGIN );
	CHECK_EQ( b.profile, 7 );
	CHECK_EQ( rig.buddy->getLocalProfileID(), 7 );

	// the ticket is out for HELLO_AUTH, but the game's login waits for the relay's answer
	UnsignedByte ticket[16];
	CHECK( OnlineGetRelayTicket( ticket ) );
	CHECK_EQ( ticket[0], 0x00 );
	CHECK_EQ( ticket[1], 0x11 );
	CHECK_EQ( ticket[15], 0xff );
	CHECK_STR( OnlineGetSessionToken().str(), "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" );
	CHECK( rig.peerResponses().empty() );

	rig.feed( "{\"t\":\"relay.ok\",\"vip\":167772163}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)3 );
	if (r.size() == 3)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_LOGIN );
		CHECK_STR( r[0].nick.c_str(), "Tank" );
		CHECK_EQ( r[0].player.profileID, 7 );
		CHECK_EQ( r[0].player.internalIP, 0x0A000003u );
		CHECK_EQ( r[0].player.externalIP, 0x0A000003u );
		CHECK_EQ( (int)r[1].peerResponseType, (int)PeerResponse::PEERRESPONSE_GROUPROOM );
		CHECK_EQ( r[1].groupRoom.id, 1 );
		CHECK_EQ( (int)r[2].peerResponseType, (int)PeerResponse::PEERRESPONSE_GROUPROOM );
		CHECK_EQ( r[2].groupRoom.id, 0 );		// the end of the list
	}
	CHECK( rig.peer->isConnected() );
	CHECK_EQ( OnlineGetRelayVip(), 0x0A000003u );
	CHECK_STR( rig.sentType( 2 ).c_str(), "stats.get" );		// our own stats, as GameSpy's login read them
}

TEST(online_adapter_login_refusal_and_no_register)
{
	AdapterRig rig;
	BuddyRequest req;
	memset( &req, 0, sizeof(req) );
	// accounts are made on the site: a new-account request sends nothing
	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGINNEW;
	strcpy( req.arg.login.nick, "Tank" );
	strcpy( req.arg.login.email, "tank@example.com" );
	strcpy( req.arg.login.password, "secret" );
	rig.buddy->addRequest( req );
	CHECK_EQ( rig.sent.size(), (size_t)0 );

	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
	rig.buddy->addRequest( req );
	CHECK_EQ( rig.sent.size(), (size_t)2 );		// the (empty) install file list, then the login
	CHECK_STR( rig.sentType( 0 ).c_str(), "files" );
	CHECK_STR( rig.sentType( 1 ).c_str(), "login" );
	CHECK_STR( rig.sent[1].getString( "email" ).c_str(), "tank@example.com" );

	Int retry = -1;
	CHECK( OnlineGetLoginFailure( &retry ).isEmpty() );
	rig.feed( "{\"t\":\"auth.fail\",\"code\":\"badlogin\"}" );
	CHECK_STR( OnlineGetLoginFailure( &retry ).str(), "badlogin" );		// the login screen words it
	CHECK_EQ( retry, 0 );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)1 );
	if (r.size() == 1)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_DISCONNECT );
		CHECK_EQ( (int)r[0].discon.reason, (int)DISCONNECT_GP_LOGIN_BAD_PASSWORD );
	}
	CHECK( !rig.peer->isConnecting() );
	UnsignedByte ticket[16];
	CHECK( !OnlineGetRelayTicket( ticket ) );

	// a resume with a saved token is the same login, on the same connection without the list again
	OnlineResume( "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" );
	CHECK_EQ( rig.sent.size(), (size_t)3 );
	CHECK_STR( rig.sentType( 2 ).c_str(), "resume" );
	CHECK_EQ( rig.sent[2].getString( "token" ).size(), (size_t)64 );
	CHECK( OnlineGetLoginFailure( &retry ).isEmpty() );		// a new login forgets the last refusal

	rig.feed( "{\"t\":\"auth.fail\",\"code\":\"throttled\",\"retry\":42}" );
	CHECK_STR( OnlineGetLoginFailure( &retry ).str(), "throttled" );
	CHECK_EQ( retry, 42 );
}

TEST(online_adapter_room_and_chat)
{
	AdapterRig rig;
	rig.login();

	PeerRequest join;
	join.peerRequestType = PeerRequest::PEERREQUEST_JOINGROUPROOM;
	join.groupRoom.id = 1;
	rig.peer->addRequest( join );
	CHECK_STR( rig.sentType( 0 ).c_str(), "room.join" );
	CHECK_STR( rig.sent[0].getString( "id" ).c_str(), "lobby1" );

	rig.feed( "{\"t\":\"room\",\"id\":\"lobby1\",\"ids\":[7,9],\"nicks\":[\"Tank\",\"Kim\"],\"vips\":[167772163,167772162]}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)3 );
	if (r.size() == 3)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_JOINGROUPROOM );
		CHECK_EQ( r[0].joinGroupRoom.id, 1 );
		CHECK( r[0].joinGroupRoom.ok );
		CHECK_EQ( (int)r[2].peerResponseType, (int)PeerResponse::PEERRESPONSE_PLAYERJOIN );
		CHECK_STR( r[2].nick.c_str(), "Kim" );
		CHECK_EQ( r[2].player.profileID, 9 );
		CHECK_EQ( (int)r[2].player.roomType, (int)GroupRoom );
		CHECK_EQ( r[2].player.IP, OnlineHostToNet( 0x0A000002 ) );		// GameSpy's player IP, network order
	}

	PeerRequest say;
	say.peerRequestType = PeerRequest::PEERREQUEST_MESSAGEROOM;
	say.text = WideCharString( u"merhaba " ) + (WideChar)0x011F;		// g with breve: UTF-8 c4 9f
	say.message.isAction = FALSE;
	rig.peer->addRequest( say );
	CHECK_STR( rig.sentType( 1 ).c_str(), "say" );
	CHECK_STR( rig.sent[1].getString( "room" ).c_str(), "lobby1" );
	CHECK_STR( rig.sent[1].getString( "text" ).c_str(), "merhaba \xc4\x9f" );

	rig.feed( "{\"t\":\"said\",\"room\":\"lobby1\",\"from\":9,\"nick\":\"Kim\",\"text\":\"selam \xc4\x9f\",\"action\":0}" );
	rig.feed( "{\"t\":\"said\",\"room\":\"lobby2\",\"from\":9,\"nick\":\"Kim\",\"text\":\"elsewhere\",\"action\":0}" );
	rig.feed( "{\"t\":\"said\",\"to\":\"Tank\",\"from\":9,\"nick\":\"Kim\",\"text\":\"psst\",\"action\":1}" );
	r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)2 );
	if (r.size() == 2)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_MESSAGE );
		CHECK_STR( r[0].nick.c_str(), "Kim" );
		CHECK( r[0].text == WideCharString( u"selam " ) + (WideChar)0x011F );
		CHECK( !r[0].message.isPrivate );
		CHECK_EQ( r[0].message.profileID, 9 );
		CHECK( r[1].message.isPrivate );
		CHECK( r[1].message.isAction );
	}

	rig.feed( "{\"t\":\"player.left\",\"room\":\"lobby1\",\"id\":9,\"nick\":\"Kim\"}" );
	r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)1 );
	if (r.size() == 1)
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_PLAYERLEFT );
}

TEST(online_adapter_game_list_round_trips_the_host_options)
{
	AdapterRig rig;
	rig.login();

	// host: the CRCs given at creation travel in every game.opts
	PeerRequest create;
	create.peerRequestType = PeerRequest::PEERREQUEST_CREATESTAGINGROOM;
	create.text = u"fun game";
	create.stagingRoomCreation.exeCRC = 0x12345678;
	create.stagingRoomCreation.iniCRC = 0x9abcdef0;
	create.stagingRoomCreation.gameVersion = 3;
	create.stagingRoomCreation.allowObservers = TRUE;
	create.stagingRoomCreation.useStats = TRUE;
	create.stagingRoomCreation.ladPort = 0;
	create.stagingRoomCreation.restrictGameList = FALSE;
	rig.peer->addRequest( create );
	CHECK_STR( rig.sentType( 0 ).c_str(), "game.host" );
	CHECK_STR( rig.sent[0].getString( "name" ).c_str(), "fun game" );

	rig.feed( "{\"t\":\"game.joined\",\"action\":\"joined\",\"id\":6,\"name\":\"fun game\",\"map\":\"-\",\"maxp\":8,\"host\":\"Tank\",\"hostId\":7,\"count\":1,\"opts\":\"\",\"ids\":[7],\"nicks\":[\"Tank\"]}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)2 );
	if (r.size() == 2)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_CREATESTAGINGROOM );
		CHECK_EQ( r[0].createStagingRoom.result, (Int)PEERJoinSuccess );
		// the host itself on the staging room's player list, so its own chat lines show
		CHECK_EQ( (int)r[1].peerResponseType, (int)PeerResponse::PEERRESPONSE_PLAYERINFO );
		CHECK_STR( r[1].nick.c_str(), "Tank" );
		CHECK_EQ( (int)r[1].player.roomType, (int)StagingRoom );
	}

	PeerRequest opts;
	opts.peerRequestType = PeerRequest::PEERREQUEST_SETGAMEOPTIONS;
	opts.options = "M=maps/a.map;S=Tank,0,0";
	opts.gameOptsMapName = "maps/a/a.map";
	opts.gameOptsPlayerNames[0] = "Tank";
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		opts.gameOptions.wins[i] = opts.gameOptions.losses[i] = opts.gameOptions.profileID[i] = 0;
		opts.gameOptions.faction[i] = opts.gameOptions.color[i] = 0;
	}
	opts.gameOptions.wins[0] = 4;
	opts.gameOptions.numPlayers = 1;
	opts.gameOptions.numObservers = 0;
	opts.gameOptions.maxPlayers = 2;
	rig.peer->addRequest( opts );
	CHECK_STR( rig.sentType( 1 ).c_str(), "game.opts" );
	CHECK_STR( rig.sentType( 2 ).c_str(), "utm" );
	CHECK_EQ( rig.sent[2].getInt( "room" ), 6 );		// a staging room is its game id, a number
	CHECK_STR( rig.sent[2].getString( "cmd" ).c_str(), "SL/" );
	CHECK_STR( rig.sent[2].getString( "opts" ).c_str(), "M=maps/a.map;S=Tank,0,0" );

	// the list as another player sees it, from the server's game line carrying those opts
	OnlineMessage game;
	game.setString( "t", "game" ).setString( "action", "add" ).setInt( "id", 6 ).setString( "name", "fun game" );
	game.setString( "map", "-" ).setInt( "maxp", 8 ).setString( "host", "Tank" ).setInt( "hostId", 7 ).setInt( "count", 1 );
	game.setString( "opts", rig.sent.size() > 1 ? rig.sent[1].getString( "opts" ) : std::string() );

	PeerRequest list;
	list.peerRequestType = PeerRequest::PEERREQUEST_STARTGAMELIST;
	list.gameList.restrictGameList = FALSE;
	rig.peer->addRequest( list );
	CHECK_STR( rig.sentType( 3 ).c_str(), "games.watch" );
	rig.feed( game );
	r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)3 );
	if (r.size() == 3)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_STAGINGROOM );
		CHECK_EQ( r[0].stagingRoom.action, PEER_CLEAR );
		CHECK_EQ( (int)r[1].peerResponseType, (int)PeerResponse::PEERRESPONSE_STAGINGROOMLISTCOMPLETE );
		CHECK_EQ( r[2].stagingRoom.action, PEER_ADD );
		CHECK_EQ( r[2].stagingRoom.id, 6 );
		CHECK( r[2].stagingServerName == WideCharString( u"fun game" ) );
		CHECK_EQ( r[2].stagingRoom.exeCRC, 0x12345678u );
		CHECK_EQ( r[2].stagingRoom.iniCRC, 0x9abcdef0u );
		CHECK_EQ( r[2].stagingRoom.version, 3u );
		CHECK( r[2].stagingRoom.allowObservers );
		CHECK_STR( r[2].stagingRoomMapName.c_str(), "maps/a/a.map" );
		CHECK_STR( r[2].stagingRoomPlayerNames[0].c_str(), "Tank" );
		CHECK_EQ( r[2].stagingRoom.wins[0], 4 );
		CHECK_EQ( r[2].stagingRoom.maxPlayers, 2 );
	}
}

TEST(online_adapter_game_options_send_only_the_seated_players_paired)
{
	AdapterRig rig;
	rig.login();
	PeerRequest create;
	create.peerRequestType = PeerRequest::PEERREQUEST_CREATESTAGINGROOM;
	create.text = u"gap";
	create.stagingRoomCreation.exeCRC = 1;
	create.stagingRoomCreation.iniCRC = 2;
	create.stagingRoomCreation.gameVersion = 3;
	create.stagingRoomCreation.allowObservers = FALSE;
	create.stagingRoomCreation.useStats = TRUE;
	create.stagingRoomCreation.ladPort = 0;
	create.stagingRoomCreation.restrictGameList = FALSE;
	rig.peer->addRequest( create );
	rig.feed( "{\"t\":\"game.joined\",\"action\":\"joined\",\"id\":6,\"name\":\"gap\",\"map\":\"-\",\"maxp\":8,\"host\":\"Tank\",\"hostId\":7,\"count\":1,\"opts\":\"\",\"ids\":[7],\"nicks\":[\"Tank\"]}" );
	rig.peerResponses();

	// two seated (Kim in the third seat, the second open), packed as setGameOptions packs them; past them
	// the request holds what a stack union would, and an AI's state (3) among it
	PeerRequest opts;
	opts.peerRequestType = PeerRequest::PEERREQUEST_SETGAMEOPTIONS;
	opts.options = "M=maps/a.map;S=Tank,0,0";
	opts.gameOptsMapName = "maps/a/a.map";
	opts.gameOptsPlayerNames[0] = "Tank";
	opts.gameOptsPlayerNames[1] = "Kim";
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		opts.gameOptions.wins[i] = opts.gameOptions.losses[i] = 3;
		opts.gameOptions.profileID[i] = 3;
		opts.gameOptions.faction[i] = opts.gameOptions.color[i] = 3;
	}
	opts.gameOptions.profileID[0] = 7;
	opts.gameOptions.profileID[1] = 9;
	opts.gameOptions.numPlayers = 2;
	opts.gameOptions.numObservers = 0;
	opts.gameOptions.maxPlayers = 3;
	rig.peer->addRequest( opts );
	CHECK_STR( rig.sentType( 1 ).c_str(), "game.opts" );
	OnlineMessage sent;
	const std::string line = rig.sent.size() > 1 ? rig.sent[1].getString( "opts" ) : std::string();
	CHECK( OnlineMessage::parse( line.data(), line.size(), sent ) );
	CHECK_EQ( sent.getStrings( "names" ).size(), (size_t)2 );
	CHECK_EQ( sent.getInts( "pids" ).size(), (size_t)2 );
	CHECK_EQ( sent.getInts( "factions" ).size(), (size_t)2 );

	OnlineMessage game;
	game.setString( "t", "game" ).setString( "action", "add" ).setInt( "id", 6 ).setString( "name", "gap" );
	game.setString( "map", "-" ).setInt( "maxp", 8 ).setString( "host", "Tank" ).setInt( "hostId", 7 ).setInt( "count", 2 );
	game.setString( "opts", line );
	PeerRequest list;
	list.peerRequestType = PeerRequest::PEERREQUEST_STARTGAMELIST;
	list.gameList.restrictGameList = FALSE;
	rig.peer->addRequest( list );
	rig.feed( game );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)3 );
	if (r.size() == 3)
	{
		CHECK_STR( r[2].stagingRoomPlayerNames[1].c_str(), "Kim" );
		CHECK_EQ( r[2].stagingRoom.profileID[1], 9 );
		CHECK_STR( r[2].stagingRoomPlayerNames[2].c_str(), "" );
		CHECK_EQ( r[2].stagingRoom.profileID[2], 0 );		// an open seat, not the AI the garbage spelled
		CHECK_EQ( r[2].stagingRoom.numPlayers, 2 );
		CHECK_EQ( r[2].stagingRoom.maxPlayers, 3 );
	}
}

TEST(online_adapter_join_utm_and_start)
{
	AdapterRig rig;
	rig.login();

	PeerRequest join;
	join.peerRequestType = PeerRequest::PEERREQUEST_JOINSTAGINGROOM;
	join.stagingRoom.id = 5;
	rig.peer->addRequest( join );
	CHECK_STR( rig.sentType( 0 ).c_str(), "game.join" );
	CHECK_EQ( rig.sent[0].getInt( "id" ), 5 );

	rig.feed( "{\"t\":\"game.joined\",\"action\":\"joined\",\"id\":5,\"name\":\"g\",\"map\":\"-\",\"maxp\":8,\"host\":\"Kim\",\"hostId\":9,\"count\":2,\"opts\":\"\",\"ids\":[9,7],\"nicks\":[\"Kim\",\"Tank\"]}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)3 );
	if (r.size() == 3)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_JOINSTAGINGROOM );
		CHECK( r[0].joinStagingRoom.ok );
		CHECK_EQ( r[0].joinStagingRoom.id, 5 );
		CHECK( r[0].joinStagingRoom.isHostPresent );
		CHECK_STR( r[0].stagingRoomPlayerNames[0].c_str(), "Kim" );
		CHECK_STR( r[0].stagingRoomPlayerNames[1].c_str(), "Tank" );
		// both on the staging room's player list, the host's chat lines need it
		CHECK_EQ( (int)r[1].peerResponseType, (int)PeerResponse::PEERRESPONSE_PLAYERINFO );
		CHECK_STR( r[1].nick.c_str(), "Kim" );
		CHECK_EQ( r[1].player.profileID, 9 );
		CHECK_STR( r[2].nick.c_str(), "Tank" );
	}
	CHECK_STR( rig.sentType( 1 ).c_str(), "stats.get" );		// the host's stats, as GameSpy's join read them
	CHECK_EQ( rig.sent[1].getInt( "id" ), 9 );

	rig.feed( "{\"t\":\"utm\",\"from\":9,\"nick\":\"Kim\",\"cmd\":\"SL/\",\"opts\":\"M=maps/a.map\",\"room\":5}" );
	rig.feed( "{\"t\":\"utm\",\"from\":9,\"nick\":\"Kim\",\"cmd\":\"KICK/\",\"opts\":\"GameFull\",\"to\":\"Tank\"}" );
	rig.feed( "{\"t\":\"utm\",\"from\":9,\"nick\":\"Kim\",\"cmd\":\"X\",\"opts\":\"\",\"room\":\"lobby1\"}" );		// a chat room's: dropped
	r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)2 );
	if (r.size() == 2)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_ROOMUTM );
		CHECK_STR( r[0].nick.c_str(), "Kim" );
		CHECK_STR( r[0].command.c_str(), "SL" );		// the screens compare without GameSpy's slash
		CHECK_STR( r[0].commandOptions.c_str(), "M=maps/a.map" );
		CHECK_EQ( (int)r[1].peerResponseType, (int)PeerResponse::PEERRESPONSE_PLAYERUTM );
		CHECK_STR( r[1].command.c_str(), "KICK" );
	}

	PeerRequest utm;
	utm.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
	utm.nick = "Kim";
	utm.id = "REQ/";
	utm.options = "accept";
	rig.peer->addRequest( utm );
	CHECK_STR( rig.sentType( 2 ).c_str(), "utm" );
	CHECK_STR( rig.sent[2].getString( "to" ).c_str(), "Kim" );
	CHECK_STR( rig.sent[2].getString( "cmd" ).c_str(), "REQ/" );

	// the server removes a started game from the list before it says game.start: no "host left"
	rig.feed( "{\"t\":\"game\",\"action\":\"remove\",\"id\":5}" );
	rig.feed( "{\"t\":\"game.start\",\"match\":\"m1\",\"ids\":[9,7],\"nicks\":[\"Kim\",\"Tank\"],\"vips\":[167772162,167772163]}" );
	r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)2 );
	if (r.size() == 2)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_STAGINGROOM );
		CHECK_EQ( r[0].stagingRoom.action, PEER_REMOVE );
		CHECK_EQ( (int)r[1].peerResponseType, (int)PeerResponse::PEERRESPONSE_GAMESTART );
	}
	CHECK_STR( OnlineSession::get().matchId.c_str(), "m1" );
}

TEST(online_adapter_host_leaving_reads_as_player_left)
{
	AdapterRig rig;
	rig.login();
	PeerRequest join;
	join.peerRequestType = PeerRequest::PEERREQUEST_JOINSTAGINGROOM;
	join.stagingRoom.id = 5;
	rig.peer->addRequest( join );
	rig.feed( "{\"t\":\"game.joined\",\"action\":\"joined\",\"id\":5,\"name\":\"g\",\"map\":\"-\",\"maxp\":8,\"host\":\"Kim\",\"hostId\":9,\"count\":2,\"opts\":\"\",\"ids\":[9,7],\"nicks\":[\"Kim\",\"Tank\"]}" );
	rig.peerResponses();

	rig.feed( "{\"t\":\"game\",\"action\":\"remove\",\"id\":5}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)2 );
	if (r.size() == 2)
	{
		CHECK_EQ( r[0].stagingRoom.action, PEER_REMOVE );
		CHECK_EQ( (int)r[1].peerResponseType, (int)PeerResponse::PEERRESPONSE_PLAYERLEFT );
		CHECK_STR( r[1].nick.c_str(), "Kim" );
		CHECK_EQ( (int)r[1].player.roomType, (int)StagingRoom );
	}
}

TEST(online_adapter_quick_match)
{
	AdapterRig rig;
	rig.login();

	PeerRequest qm;
	qm.peerRequestType = PeerRequest::PEERREQUEST_STARTQUICKMATCH;
	qm.qmMaps.push_back( true );
	qm.qmMaps.push_back( false );
	qm.qmMaps.push_back( true );
	qm.QM.side = 2;
	qm.QM.color = 3;
	rig.peer->addRequest( qm );
	CHECK_STR( rig.sentType( 0 ).c_str(), "qm.join" );
	if (!rig.sent.empty())
	{
		const std::vector<long long> maps = rig.sent[0].getInts( "maps" );
		CHECK_EQ( maps.size(), (size_t)2 );
		CHECK( maps.size() == 2 && maps[0] == 0 && maps[1] == 2 );
		CHECK_EQ( rig.sent[0].getInt( "side" ), 2 );
		CHECK_EQ( rig.sent[0].getInt( "color" ), 3 );
	}

	rig.feed( "{\"t\":\"qm.status\",\"status\":\"searching\",\"pool\":4}" );
	PeerRequest widen;
	widen.peerRequestType = PeerRequest::PEERREQUEST_WIDENQUICKMATCHSEARCH;
	rig.peer->addRequest( widen );
	CHECK_STR( rig.sentType( 1 ).c_str(), "qm.widen" );
	// side -25 is a Random pool (America and Turkiye): this binary has no template store, so a pool is the
	// one kind of side that reaches the seat unchanged here
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q1\",\"seed\":77,\"mapIdx\":1,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],\"vips\":[167772163,167772162],\"sides\":[-25,-1],\"colors\":[3,-1],\"skins\":[\"K1.3\",\"\"]}" );

	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)3 );
	if (r.size() == 3)
	{
		CHECK_EQ( (int)r[0].qmStatus.status, (int)QM_WORKING );
		CHECK_EQ( r[0].qmStatus.poolSize, 4 );
		CHECK_EQ( (int)r[1].qmStatus.status, (int)QM_WIDENINGSEARCH );
		CHECK_EQ( (int)r[2].peerResponseType, (int)PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS );
		CHECK_EQ( (int)r[2].qmStatus.status, (int)QM_MATCHED );
		CHECK_EQ( r[2].qmStatus.seed, 77 );
		CHECK_EQ( r[2].qmStatus.mapIdx, 1 );
		CHECK_STR( r[2].stagingRoomPlayerNames[0].c_str(), "Tank" );
		CHECK_STR( r[2].stagingRoomPlayerNames[1].c_str(), "Kim" );
		CHECK( r[2].stagingRoomPlayerNames[2].empty() );
		CHECK_EQ( r[2].qmStatus.IP[1], 0x0A000002u );
		CHECK_EQ( r[2].qmStatus.side[0], -25 );
		CHECK_EQ( r[2].qmStatus.color[1], -1 );
		CHECK_EQ( r[2].qmStatus.nat[0], 1 );
		CHECK( r[2].qmStatus.skins[0] == ( ( (SkinLoadout)1 << 1 ) | ( (SkinLoadout)1 << 3 ) ) );		// each seat as its account wears them
		CHECK( r[2].qmStatus.skins[1] == 0 );
	}
	CHECK_STR( OnlineSession::get().matchId.c_str(), "q1" );
}

/* A server that reveals: qm.matched shows the opponent, game.start launches; a qm.status in between
	 (the opponent dropped) cancels and nothing starts. */
TEST(online_adapter_quick_match_reveal_waits_for_game_start)
{
	AdapterRig rig;
	rig.login();
	const char *matchedLine = "{\"t\":\"qm.matched\",\"match\":\"q2\",\"seed\":5,\"mapIdx\":0,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],"
		"\"vips\":[167772163,167772162],\"sides\":[-1,-1],\"colors\":[-1,-1],\"server\":\"Frankfurt\",\"revealMs\":3000,"
		"\"opponents\":[{\"nick\":\"Kim\",\"rating\":31,\"tier\":\"major\"}]}";

	rig.feed( matchedLine );
	CHECK( rig.peerResponses().empty() );		// nothing launches on the reveal
	OnlineMatchReveal rev;
	CHECK( OnlineGetMatchReveal( rev ) );
	CHECK_EQ( rev.revealMs, 3000 );
	CHECK_STR( rev.server.str(), "Frankfurt" );
	CHECK_EQ( rev.opponents.size(), (size_t)1 );
	if (!rev.opponents.empty())
	{
		CHECK_STR( rev.opponents[0].nick.str(), "Kim" );
		CHECK_EQ( rev.opponents[0].rating, 31 );
		CHECK_STR( rev.opponents[0].tier.str(), "major" );
	}
	const UnsignedInt first = rev.serial;

	// the opponent dropped: searching again, and the reveal is gone
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"searching\",\"pool\":3}" );
	CHECK( !OnlineGetMatchReveal( rev ) );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK( r.size() == 1 && r[0].qmStatus.status == QM_WORKING );
	rig.feed( "{\"t\":\"game.start\",\"match\":\"q2\",\"ids\":[7,9],\"nicks\":[\"Tank\",\"Kim\"],\"vips\":[1,2]}" );
	r = rig.peerResponses();
	CHECK( r.size() == 1 && r[0].peerResponseType == PeerResponse::PEERRESPONSE_GAMESTART );		// no QM seats from a cancelled reveal

	rig.feed( matchedLine );
	CHECK( OnlineGetMatchReveal( rev ) );
	CHECK( rev.serial != first );
	rig.feed( "{\"t\":\"game.start\",\"match\":\"q2\",\"ids\":[7,9],\"nicks\":[\"Tank\",\"Kim\"],\"vips\":[167772163,167772170]}" );
	CHECK( !OnlineGetMatchReveal( rev ) );
	r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)1 );
	if (r.size() == 1)
	{
		CHECK_EQ( (int)r[0].qmStatus.status, (int)QM_MATCHED );
		CHECK_EQ( r[0].qmStatus.seed, 5 );
		CHECK_STR( r[0].stagingRoomPlayerNames[1].c_str(), "Kim" );
		CHECK_EQ( r[0].qmStatus.IP[1], 167772170u );		// game.start's address wins
	}
	CHECK_STR( OnlineSession::get().matchId.c_str(), "q2" );

	OnlinePlayerInfo kim;
	CHECK( OnlineGetPlayerInfo( "Kim", kim ) );
	CHECK( kim.hasRating && kim.rating == 31 );
}

TEST(online_adapter_ping_ratings_stats_and_ladder)
{
	AdapterRig rig;
	rig.login();

	rig.feed( "{\"t\":\"ping\",\"n\":12}" );
	CHECK_STR( rig.sentType( 0 ).c_str(), "pong" );
	if (!rig.sent.empty())
		CHECK_EQ( rig.sent[0].getInt( "n" ), 12 );
	rig.sent.clear();

	// a `player` for someone already in the room updates the numbers, it is not a second join
	rig.feed( "{\"t\":\"room\",\"id\":\"lobby1\",\"ids\":[7,9],\"nicks\":[\"Tank\",\"Kim\"]}" );
	rig.peerResponses();
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":9,\"nick\":\"Kim\",\"ping\":45,\"rating\":31.6,\"tier\":\"major\",\"games\":7}" );
	CHECK( rig.peerResponses().empty() );
	OnlinePlayerInfo kim;
	CHECK( OnlineGetPlayerInfo( "Kim", kim ) );
	CHECK_EQ( kim.ping, 45 );
	CHECK( kim.hasRating );
	CHECK_EQ( kim.rating, 32 );
	CHECK_STR( kim.tier.str(), "major" );
	CHECK_EQ( kim.games, 7 );
	CHECK( !kim.hasStats );
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":11,\"nick\":\"Lee\"}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK( r.size() == 1 && r[0].peerResponseType == PeerResponse::PEERRESPONSE_PLAYERJOIN );
	rig.feed( "{\"t\":\"player.left\",\"room\":\"lobby1\",\"id\":11,\"nick\":\"Lee\"}" );
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":11,\"nick\":\"Lee\"}" );
	r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)2 );		// left, then joined again

	// stats{nick}, and a nick the server cannot find
	const UnsignedInt before = OnlinePlayerInfoSerial();
	OnlineRequestStats( "Kim" );
	OnlineRequestStats( "Ghost" );
	CHECK_STR( rig.sentType( 0 ).c_str(), "stats" );
	if (!rig.sent.empty())
		CHECK_STR( rig.sent[0].getString( "nick" ).c_str(), "Kim" );
	rig.feed( "{\"t\":\"stats\",\"id\":9,\"kv\":\"\\\\wins\\\\5\",\"nick\":\"Kim\",\"rating\":null,\"tier\":null,\"wins\":5,\"losses\":2,\"games\":7,\"rank\":12}" );
	rig.feed( "{\"t\":\"error\",\"for\":\"stats\",\"code\":\"noplayer\"}" );
	CHECK( OnlinePlayerInfoSerial() != before );
	CHECK( OnlineGetPlayerInfo( "Kim", kim ) );
	CHECK( kim.hasStats );
	CHECK( !kim.hasRating );		// null clears it
	CHECK( kim.tier.isEmpty() );
	CHECK_EQ( kim.wins, 5 );
	CHECK_EQ( kim.losses, 2 );
	CHECK_EQ( kim.rank, 12 );
	OnlinePlayerInfo ghost;
	CHECK( OnlineGetPlayerInfo( "Ghost", ghost ) );
	CHECK( ghost.notOnline );
	PSResponse ps;
	CHECK( rig.ps->getResponse( ps ) );		// the kv still reaches EA's screens
	CHECK_EQ( ps.player.id, 9 );

	// the ladder
	rig.sent.clear();
	OnlineRequestLadder( 20 );
	CHECK_STR( rig.sentType( 0 ).c_str(), "ladder" );
	Int offset = -1;
	std::vector<OnlineLadderRow> rows;
	OnlineLadderRow me;
	Bool hasMe = TRUE;
	CHECK_EQ( (int)OnlineGetLadder( offset, rows, me, hasMe ), (int)ONLINE_LADDER_WAITING );
	rig.feed( "{\"t\":\"ladder\",\"offset\":20,\"rows\":[{\"rank\":21,\"nick\":\"Kim\",\"rating\":31,\"tier\":\"major\",\"wins\":5,\"losses\":2}],"
		"\"me\":{\"rank\":40,\"nick\":\"Tank\",\"rating\":null,\"tier\":null,\"wins\":1,\"losses\":3}}" );
	CHECK_EQ( (int)OnlineGetLadder( offset, rows, me, hasMe ), (int)ONLINE_LADDER_READY );
	CHECK_EQ( offset, 20 );
	CHECK( rows.size() == 1 && rows[0].rank == 21 && rows[0].rating == 31 && rows[0].wins == 5 );
	CHECK( hasMe && me.rank == 40 && !me.hasRating && me.losses == 3 );
	OnlineRequestLadder( 40 );
	rig.feed( "{\"t\":\"error\",\"for\":\"ladder\",\"code\":\"unavailable\"}" );
	CHECK_EQ( (int)OnlineGetLadder( offset, rows, me, hasMe ), (int)ONLINE_LADDER_FAILED );

	// the armies' win percentages
	rig.sent.clear();
	OnlineRequestArmies();
	CHECK_STR( rig.sentType( 0 ).c_str(), "armies" );
	std::vector<OnlineArmyRow> armies;
	CHECK_EQ( (int)OnlineGetArmies( armies ), (int)ONLINE_LADDER_WAITING );
	rig.feed( "{\"t\":\"armies\",\"sides\":[{\"side\":\"America\",\"games\":12,\"wins\":7},{\"side\":\"GLA\",\"games\":3,\"wins\":0}]}" );
	CHECK_EQ( (int)OnlineGetArmies( armies ), (int)ONLINE_LADDER_READY );
	CHECK( armies.size() == 2 && armies[0].side == "America" && armies[0].games == 12 && armies[0].wins == 7 );
	CHECK( armies.size() == 2 && armies[1].side == "GLA" && armies[1].wins == 0 );
	OnlineRequestArmies();
	rig.feed( "{\"t\":\"error\",\"for\":\"armies\",\"code\":\"unavailable\"}" );
	CHECK_EQ( (int)OnlineGetArmies( armies ), (int)ONLINE_LADDER_FAILED );
	CHECK( armies.empty() );

	// the verdict, for the match just played only
	Bool rated = FALSE;
	Real delta = 0, rating = 0;
	AsciiString reason;
	OnlineSession::get().matchId = "q7";
	rig.feed( "{\"t\":\"rated\",\"match\":\"q6\",\"delta\":1.5,\"rating\":20}" );
	CHECK( !OnlineGetMatchResult( rated, delta, rating, reason ) );
	rig.feed( "{\"t\":\"rated\",\"match\":\"q7\",\"delta\":-1.25,\"rating\":18.5}" );
	CHECK( OnlineGetMatchResult( rated, delta, rating, reason ) );
	CHECK( rated );
	CHECK_NEAR( delta, -1.25f, 0.001f );
	CHECK_NEAR( rating, 18.5f, 0.001f );
}

/* The Skins screen's lines: auth.ok's owned[], skins.set carrying the whole loadout, skins.ok seating the
	 pick for the next room at once, skins.err and an old server's refusal leaving it as it was. */
TEST(online_adapter_skins_owned_saved_and_refused)
{
	AdapterRig rig;
	const SkinLoadout one = (SkinLoadout)1 << 1, two = (SkinLoadout)1 << 2, three = (SkinLoadout)1 << 3;
	SkinLoadout owned = 1;
	AsciiString error;
	CHECK_EQ( (int)OnlineGetSkins( owned, error ), (int)ONLINE_SKINS_FAILED );		// signed out: nothing to pick
	CHECK_STR( error.str(), "signedout" );
	CHECK( owned == 0 );
	OnlineSaveSkins( two );
	CHECK( rig.sent.empty() );

	rig.feed( "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"t\",\"ticket\":\"00112233445566778899aabbccddeeff\",\"skins\":[1],\"owned\":[1,2,3,64]}" );
	CHECK_EQ( (int)OnlineGetSkins( owned, error ), (int)ONLINE_SKINS_READY );
	CHECK( owned == ( one | two | three ) );		// 64 is past any loadout
	CHECK( TankSkin_localLoadout() == one );

	rig.sent.clear();
	OnlineSaveSkins( two | three );
	CHECK_STR( rig.sentType( 0 ).c_str(), "skins.set" );
	std::vector<long long> want;
	want.push_back( 2 );
	want.push_back( 3 );
	CHECK( rig.sent.size() == 1 && rig.sent[0].getInts( "skins" ) == want );
	CHECK_EQ( (int)OnlineGetSkins( owned, error ), (int)ONLINE_SKINS_WAITING );
	CHECK( TankSkin_localLoadout() == one );		// nothing changes before the site said yes
	rig.feed( "{\"t\":\"skins.ok\",\"owned\":[1,2,3,5],\"skins\":[2,3]}" );
	CHECK_EQ( (int)OnlineGetSkins( owned, error ), (int)ONLINE_SKINS_READY );
	CHECK( owned == ( one | two | three | ( (SkinLoadout)1 << 5 ) ) );
	CHECK( TankSkin_localLoadout() == ( two | three ) );		// the next room's seat

	// Default everywhere is an empty list; a refusal keeps the loadout and says why
	rig.sent.clear();
	OnlineSaveSkins( 0 );
	CHECK( rig.sent.size() == 1 && rig.sent[0].find( "skins" ) && rig.sent[0].getInts( "skins" ).empty() );
	rig.feed( "{\"t\":\"skins.err\",\"code\":\"throttled\"}" );
	CHECK_EQ( (int)OnlineGetSkins( owned, error ), (int)ONLINE_SKINS_FAILED );
	CHECK_STR( error.str(), "throttled" );
	CHECK( TankSkin_localLoadout() == ( two | three ) );

	// skins.get, and a server from before it
	rig.sent.clear();
	OnlineRequestSkins();
	CHECK_STR( rig.sentType( 0 ).c_str(), "skins.get" );
	CHECK_EQ( (int)OnlineGetSkins( owned, error ), (int)ONLINE_SKINS_WAITING );
	rig.feed( "{\"t\":\"error\",\"for\":\"skins.get\",\"code\":\"unknown\"}" );
	CHECK_EQ( (int)OnlineGetSkins( owned, error ), (int)ONLINE_SKINS_FAILED );
	CHECK_STR( error.str(), "unavailable" );

	// signing out forgets both
	OnlineSession::get().disconnect();
	CHECK_EQ( (int)OnlineGetSkins( owned, error ), (int)ONLINE_SKINS_FAILED );
	CHECK( owned == 0 && TankSkin_localLoadout() == 0 );
	rig.feed( "{\"t\":\"skins.ok\",\"owned\":[1],\"skins\":[1]}" );		// a late answer for the account that left
	CHECK( TankSkin_localLoadout() == 0 );
}

/* Play vs AI's lines: auth.ok's AI rating on the card, ai.start carrying the build, ai.ready handed out
	 once, the first ai.tick waiting its half minute, ai.result once and a loss's replay never sent, a win's
	 sent under its match, the verdicts, the refusals and a logout ending a live match with nothing sent. */
TEST(online_adapter_play_vs_ai)
{
	AdapterRig rig;
	OnlineSession &s = OnlineSession::get();
	OnlineAIMatch ai;
	OnlineRequestAIMatch( "Random" );		// signed out
	CHECK( rig.sent.empty() );
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_REFUSED );
	CHECK_STR( ai.reason.str(), "signedout" );

	rig.feed( "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"t\",\"ticket\":\"00112233445566778899aabbccddeeff\",\"aiRating\":12.4,\"aiGames\":3,\"aiWins\":2}" );
	OnlinePlayerInfo card;
	CHECK( OnlineGetPlayerInfo( "Tank", card ) && card.hasAIRating );
	CHECK_NEAR( card.aiRating, 12.4f, 0.001f );
	CHECK_EQ( card.aiGames, 3 );
	CHECK_EQ( card.aiWins, 2 );

	rig.sent.clear();
	OnlineRequestAIMatch( "FactionAmerica" );
	CHECK_STR( rig.sentType( 0 ).c_str(), "ai.start" );
	CHECK( rig.sent.size() == 1 && rig.sent[0].getString( "army" ) == "FactionAmerica" && rig.sent[0].has( "exe" ) && rig.sent[0].has( "ini" ) );
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_WAITING );
	rig.feed( "{\"t\":\"ai.ready\",\"matchId\":\"m1\",\"aiRating\":12.4,\"map\":\"maps\\\\alpine assault\\\\alpine assault.map\",\"seed\":2147483000}" );
	CHECK( !OnlineIsAIMatchLive() );		// live once the hub takes it
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_READY );
	CHECK_STR( ai.match.str(), "m1" );
	CHECK_STR( ai.map.str(), "maps\\alpine assault\\alpine assault.map" );
	CHECK_NEAR( ai.aiRating, 12.4f, 0.001f );
	CHECK_EQ( ai.seed, 2147483000 );
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_IDLE );		// handed out once
	CHECK( OnlineIsAIMatchLive() );
	CHECK_STR( OnlineClaimAIMatch().str(), "m1" );

	rig.sent.clear();
	OnlineSendAITick( 30 );
	CHECK( rig.sent.empty() );		// the first waits its half minute
	s.aiTickMs -= ONLINE_AI_TICK_SECONDS * 1000 + 1;
	OnlineSendAITick( 900 );
	CHECK( rig.sent.size() == 1 && rig.sentType( 0 ) == "ai.tick" && rig.sent[0].getString( "matchId" ) == "m1" && rig.sent[0].getInt( "frame" ) == 900 );

	// a loss, once; its recording is not sent
	const char *path = "test_online_ai_replay.tmp";
	FILE *f = fopen( path, "wb" );
	CHECK( f != NULL );
	if (!f)
		return;
	fwrite( "GENREP", 1, 6, f );
	fclose( f );
	rig.sent.clear();
	OnlineAIResult result;
	result.endFrame = 1200;
	result.blind = 2;
	result.playerArmy = "FactionAmerica";
	result.aiArmy = "FactionGLAToxinGeneral";
	result.unitsBuilt = 14;
	result.structuresKilled = 3;
	result.moneyEarned = 8200;
	OnlineSendAIResult( result );
	result.won = TRUE;
	OnlineSendAIResult( result );
	CHECK_EQ( rig.sent.size(), (size_t)1 );
	CHECK( rig.sentType( 0 ) == "ai.result" && rig.sent[0].getString( "matchId" ) == "m1" && rig.sent[0].getString( "result" ) == "loss" );
	CHECK( rig.sent[0].getInt( "endFrame" ) == 1200 && rig.sent[0].getInt( "blind" ) == 2 && rig.sent[0].has( "seconds" ) );
	CHECK( rig.sent[0].getString( "playerArmy" ) == "FactionAmerica" && rig.sent[0].getString( "aiArmy" ) == "FactionGLAToxinGeneral" );
	OnlineMessage stats;
	CHECK( rig.sent[0].getObject( "stats", stats ) && stats.size() == 7 );
	CHECK( stats.getInt( "unitsBuilt" ) == 14 && stats.getInt( "unitsLost" ) == 0 && stats.getInt( "structuresKilled" ) == 3 );
	CHECK( stats.getInt( "moneyEarned" ) == 8200 && stats.has( "unitsKilled" ) && stats.has( "structuresBuilt" ) && stats.has( "structuresLost" ) );
	std::string line;
	CHECK( rig.sent[0].toLine( line ) );
	CHECK( !OnlineIsAIMatchLive() );
	OnlineSendReplay( "m1", path );
	CHECK_EQ( rig.sent.size(), (size_t)1 );

	// ai.rated moves the card at once
	rig.feed( "{\"t\":\"ai.rated\",\"matchId\":\"m1\",\"before\":12.4,\"after\":11.9,\"games\":4}" );
	OnlineAIVerdict verdict;
	CHECK( OnlineGetAIVerdict( verdict ) && verdict.rated );
	CHECK_NEAR( verdict.before, 12.4f, 0.001f );
	CHECK_NEAR( verdict.after, 11.9f, 0.001f );
	CHECK( OnlineGetPlayerInfo( "Tank", card ) && card.aiGames == 4 && card.aiWins == 2 );
	CHECK_NEAR( card.aiRating, 11.9f, 0.001f );

	// a win: its recording goes up under the match; a new ai.start drops the last verdict
	OnlineRequestAIMatch( "Random" );
	CHECK( !OnlineGetAIVerdict( verdict ) );
	rig.feed( "{\"t\":\"ai.ready\",\"matchId\":\"m2\",\"aiRating\":11.9,\"map\":\"maps\\\\winter wolf\\\\winter wolf.map\",\"seed\":5}" );
	OnlineGetAIMatch( ai );
	result.endFrame = 5000;
	OnlineSendAIResult( result );
	rig.sent.clear();
	OnlineSendReplay( "m2", path );
	CHECK( rig.sent.size() == 1 && rig.sentType( 0 ) == "replay" && rig.sent[0].getString( "match" ) == "m2" && rig.sent[0].getBool( "last" ) );
	// rated without its wins, the card counts the win it reported; with them, it takes the server's
	rig.feed( "{\"t\":\"ai.rated\",\"matchId\":\"m2\",\"before\":11.9,\"after\":12.6,\"games\":5}" );
	CHECK( OnlineGetPlayerInfo( "Tank", card ) && card.aiGames == 5 && card.aiWins == 3 );
	rig.feed( "{\"t\":\"ai.rated\",\"matchId\":\"m2\",\"before\":11.9,\"after\":12.6,\"games\":5,\"wins\":4}" );
	CHECK( OnlineGetPlayerInfo( "Tank", card ) && card.aiWins == 4 );
	rig.feed( "{\"t\":\"ai.unrated\",\"matchId\":\"m2\",\"reason\":\"gap\"}" );
	CHECK( OnlineGetAIVerdict( verdict ) && !verdict.rated );
	CHECK_STR( verdict.reason.str(), "gap" );
	rig.feed( "{\"t\":\"ai.refused\",\"matchId\":\"m2\",\"reason\":\"reported\"}" );		// a result turned down
	CHECK( OnlineGetAIVerdict( verdict ) && !verdict.rated );
	CHECK_STR( verdict.reason.str(), "reported" );
	remove( path );

	// the server's refusal, and a server from before Play vs AI
	OnlineRequestAIMatch( "Random" );
	rig.feed( "{\"t\":\"ai.refused\",\"reason\":\"build\"}" );
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_REFUSED );
	CHECK_STR( ai.reason.str(), "build" );
	OnlineRequestAIMatch( "Random" );
	rig.feed( "{\"t\":\"error\",\"for\":\"ai.start\",\"code\":\"unknown\"}" );
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_REFUSED );
	CHECK_STR( ai.reason.str(), "unavailable" );

	// a logout ends a live match with nothing sent: the server settles it
	OnlineRequestAIMatch( "Random" );
	rig.feed( "{\"t\":\"ai.ready\",\"matchId\":\"m3\",\"aiRating\":0,\"map\":\"maps\\\\winter wolf\\\\winter wolf.map\",\"seed\":1}" );
	OnlineGetAIMatch( ai );
	CHECK( OnlineIsAIMatchLive() );
	s.disconnect();
	CHECK( !OnlineIsAIMatchLive() );
	CHECK( !OnlineGetAIVerdict( verdict ) );
}

/* proto 2 on login, the room snapshot's cards, nicks in any case, Stop ending a reveal at once. */
TEST(online_adapter_proto2_room_cards_and_case)
{
	AdapterRig rig;
	BuddyRequest req;
	memset( &req, 0, sizeof(req) );
	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
	strcpy( req.arg.login.email, "tank@example.com" );
	strcpy( req.arg.login.password, "secret" );
	rig.buddy->addRequest( req );
	CHECK( !rig.sent.empty() && rig.sent.back().getInt( "proto" ) == 2 );
	rig.login();

	rig.feed( "{\"t\":\"room\",\"id\":\"lobby1\",\"ids\":[7,9],\"nicks\":[\"Tank\",\"Kim\"],"
		"\"players\":[{\"id\":7,\"nick\":\"Tank\",\"ping\":20},{\"id\":9,\"nick\":\"Kim\",\"ping\":45,\"rating\":31,\"tier\":\"major\"}]}" );
	rig.peerResponses();
	OnlinePlayerInfo kim;
	CHECK( OnlineGetPlayerInfo( "kim", kim ) );		// any case
	CHECK_EQ( kim.ping, 45 );
	CHECK( kim.hasRating && kim.rating == 31 );
	const UnsignedInt serial = kim.serial;
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":9,\"nick\":\"Kim\",\"ping\":45}" );
	CHECK( OnlineGetPlayerInfo( "Kim", kim ) );
	CHECK_EQ( kim.serial, serial );		// the same ping again changes nothing
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":9,\"nick\":\"Kim\",\"ping\":60}" );
	CHECK( OnlineGetPlayerInfo( "Kim", kim ) );
	CHECK( kim.serial != serial );

	// asked as KIM, answered as Kim; the next noplayer then belongs to Ghost, not Kim
	OnlineRequestStats( "KIM" );
	OnlineRequestStats( "Ghost" );
	rig.feed( "{\"t\":\"stats\",\"id\":9,\"nick\":\"Kim\",\"wins\":5,\"losses\":2,\"rank\":3}" );
	rig.feed( "{\"t\":\"error\",\"for\":\"stats\",\"code\":\"noplayer\"}" );
	CHECK( OnlineGetPlayerInfo( "Kim", kim ) );
	CHECK( kim.hasStats && !kim.notOnline && kim.rank == 3 );
	OnlinePlayerInfo ghost;
	CHECK( OnlineGetPlayerInfo( "ghost", ghost ) && ghost.notOnline );

	// Stop during a reveal ends it before the server's qm.status comes back
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q3\",\"seed\":1,\"mapIdx\":0,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],\"revealMs\":3000}" );
	OnlineMatchReveal rev;
	CHECK( OnlineGetMatchReveal( rev ) );
	PeerRequest stop;
	stop.peerRequestType = PeerRequest::PEERREQUEST_STOPQUICKMATCH;
	rig.peer->addRequest( stop );
	CHECK( !OnlineGetMatchReveal( rev ) );
	CHECK_STR( rig.sent.back().type().c_str(), "qm.leave" );
}

/* GameLogic dereferences the template and the colour a seat names (getColor(999)->getColor() was a
	 crash at Quick Match start), so whatever the server says is clamped before a seat sees it. */
TEST(online_seat_side_and_color_are_clamped)
{
	const Int TEMPLATES = 5, COLORS = 8;
	CHECK_EQ( OnlineSeatSide( 0, TEMPLATES ), 0 );
	CHECK_EQ( OnlineSeatSide( 4, TEMPLATES ), 4 );
	CHECK_EQ( OnlineSeatSide( -1, TEMPLATES ), -1 );
	CHECK_EQ( OnlineSeatSide( PLAYERTEMPLATE_RANDOM_POOL - 0x9, TEMPLATES ), PLAYERTEMPLATE_RANDOM_POOL - 0x9 );
	CHECK_EQ( OnlineSeatSide( PLAYERTEMPLATE_RANDOM_POOL - 0x3FFFFFFF, TEMPLATES ), PLAYERTEMPLATE_RANDOM_POOL - 0x3FFFFFFF );
	CHECK_EQ( OnlineSeatSide( 5, TEMPLATES ), -1 );
	CHECK_EQ( OnlineSeatSide( 999, TEMPLATES ), -1 );
	CHECK_EQ( OnlineSeatSide( PLAYERTEMPLATE_OBSERVER, TEMPLATES ), -1 );
	CHECK_EQ( OnlineSeatSide( -3, TEMPLATES ), -1 );
	CHECK_EQ( OnlineSeatSide( PLAYERTEMPLATE_RANDOM_POOL, TEMPLATES ), -1 );
	CHECK_EQ( OnlineSeatSide( PLAYERTEMPLATE_RANDOM_POOL - 0x40000000LL, TEMPLATES ), -1 );
	CHECK_EQ( OnlineSeatSide( (1LL << 32) + 2, TEMPLATES ), -1 );		// not 2 after a truncation
	CHECK_EQ( OnlineSeatSide( -(1LL << 40), TEMPLATES ), -1 );
	CHECK_EQ( OnlineSeatSide( 0, 0 ), -1 );

	CHECK_EQ( OnlineSeatColor( 0, COLORS ), 0 );
	CHECK_EQ( OnlineSeatColor( 7, COLORS ), 7 );
	CHECK_EQ( OnlineSeatColor( -1, COLORS ), -1 );
	CHECK_EQ( OnlineSeatColor( 8, COLORS ), -1 );
	CHECK_EQ( OnlineSeatColor( 999, COLORS ), -1 );
	CHECK_EQ( OnlineSeatColor( -2, COLORS ), -1 );
	CHECK_EQ( OnlineSeatColor( (1LL << 32) + 3, COLORS ), -1 );

	// through the adapter: a hostile qm.matched and a hostile game list line
	AdapterRig rig;
	rig.login();
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q9\",\"seed\":1,\"mapIdx\":-5,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],\"vips\":[1,2],\"sides\":[999,-2],\"colors\":[999,-7]}" );
	OnlineMessage opts;
	opts.setInts( "factions", std::vector<long long>( 2, 999 ) ).setInts( "colors", std::vector<long long>( 2, 4294967296LL ) );
	std::string optsLine;
	CHECK( opts.toLine( optsLine ) );
	OnlineMessage game;
	game.setString( "t", "game" ).setString( "action", "add" ).setInt( "id", 3 ).setString( "host", "Kim" );
	game.setString( "opts", optsLine.substr( 0, optsLine.size() - 1 ) );
	rig.feed( game );

	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)2 );
	if (r.size() == 2)
	{
		CHECK_EQ( (int)r[0].qmStatus.status, (int)QM_MATCHED );
		for (Int i = 0; i < 2; ++i)
		{
			CHECK_EQ( r[0].qmStatus.side[i], -1 );
			CHECK_EQ( r[0].qmStatus.color[i], -1 );
			CHECK_EQ( r[1].stagingRoom.faction[i], -1 );
			CHECK_EQ( r[1].stagingRoom.color[i], -1 );
		}
	}
}

TEST(online_test_server_only_when_the_welcome_says_so)
{
	AdapterRig rig;
	CHECK( !OnlineIsTestServer() );
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094}" );
	CHECK( !OnlineIsTestServer() );
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094,\"testMode\":\"yes\"}" );
	CHECK( !OnlineIsTestServer() );		// a bool, not anything truthy
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094,\"testMode\":true}" );
	CHECK( OnlineIsTestServer() );
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094,\"testMode\":false}" );
	CHECK( !OnlineIsTestServer() );
}

TEST(online_adapter_stats_read_and_writes_dropped)
{
	AdapterRig rig;
	rig.login();

	PSRequest read;
	read.requestType = PSRequest::PSREQUEST_READPLAYERSTATS;
	read.player.id = 9;
	rig.ps->addRequest( read );
	PSRequest write;
	write.requestType = PSRequest::PSREQUEST_UPDATEPLAYERSTATS;
	write.player.id = 7;
	write.player.wins[3] = 1000;
	rig.ps->addRequest( write );
	CHECK_EQ( rig.sent.size(), (size_t)1 );		// the write never leaves
	CHECK_STR( rig.sentType( 0 ).c_str(), "stats.get" );
	CHECK_EQ( rig.sent[0].getInt( "id" ), 9 );

	rig.feed( "{\"t\":\"stats\",\"id\":9,\"kv\":\"\\\\nick\\\\Kim\\\\wins\\\\5\\\\losses\\\\2\\\\games\\\\7\\\\rating\\\\31\"}" );
	PSResponse resp;
	CHECK( rig.ps->getResponse( resp ) );
	CHECK_EQ( (int)resp.responseType, (int)PSResponse::PSRESPONSE_PLAYERSTATS );
	CHECK_EQ( resp.player.id, 9 );
	CHECK_EQ( (int)resp.player.wins[0], 5 );
	CHECK_EQ( (int)resp.player.losses[0], 2 );
	CHECK_EQ( (int)resp.player.games[0], 7 );
	CHECK_EQ( (int)rig.ps->findPlayerStatsByID( 9 ).wins[0], 5 );
	CHECK_EQ( rig.ps->findPlayerStatsByID( 12 ).id, 0 );
}

TEST(online_adapter_report_goes_once_a_match)
{
	AdapterRig rig;
	rig.login();

	OnlineReport report;
	report.seed = 77;
	report.endFrame = 9000;
	report.duration = 9000;
	report.winningTeam = 1;
	report.sawGameEnd = TRUE;
	report.surrendered = TRUE;
	report.numPlayers = 2;
	report.side[0] = "America";
	report.side[1] = "China";
	report.discon[1] = TRUE;

	CHECK( !OnlineSendReport( report ) );
	CHECK( rig.sent.empty() );		// no match yet

	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q1\",\"seed\":77,\"mapIdx\":0,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],\"vips\":[1,2],\"sides\":[0,0],\"colors\":[0,1]}" );
	CHECK( OnlineSendReport( report ) );
	report.winningTeam = -1;		// the score screen reads it off a world already torn down
	CHECK( !OnlineSendReport( report ) );		// the score screen's second call: dropped, says so
	report.winningTeam = 1;
	CHECK_EQ( rig.sent.size(), (size_t)1 );
	if (rig.sent.size() == 1)
	{
		const OnlineMessage &m = rig.sent[0];
		CHECK_STR( m.type().c_str(), "report" );
		CHECK_STR( m.getString( "match" ).c_str(), "q1" );
		CHECK_EQ( m.getInt( "seed" ), 77 );
		CHECK_EQ( m.getInt( "endFrame" ), 9000 );
		CHECK_EQ( m.getInt( "winningTeam" ), 1 );
		CHECK( m.getBool( "surrendered" ) );
		CHECK( !m.getBool( "sawDesync" ) );
		CHECK( m.getInts( "discon" ).size() == 2 && m.getInts( "discon" )[1] == 1 );
		CHECK( m.getStrings( "side" ).size() == 2 && m.getStrings( "side" )[1] == "China" );
	}

	// the next match reports again
	rig.feed( "{\"t\":\"game.start\",\"match\":\"m2\",\"ids\":[7,9],\"nicks\":[\"Tank\",\"Kim\"],\"vips\":[1,2]}" );
	CHECK( OnlineSendReport( report ) );
	CHECK_EQ( rig.sent.size(), (size_t)2 );
}

/* Tank in the lobby's chat room, then hosting game 1 (the server keeps him in the chat room too). */
static void hostGameOne( AdapterRig &rig )
{
	rig.login();
	PeerRequest join;
	join.peerRequestType = PeerRequest::PEERREQUEST_JOINGROUPROOM;
	join.groupRoom.id = 1;
	rig.peer->addRequest( join );
	rig.feed( "{\"t\":\"room\",\"id\":\"lobby1\",\"ids\":[7],\"nicks\":[\"Tank\"],\"vips\":[167772163]}" );
	PeerRequest create;
	create.peerRequestType = PeerRequest::PEERREQUEST_CREATESTAGINGROOM;
	create.text = u"g";
	create.stagingRoomCreation.exeCRC = 1;
	create.stagingRoomCreation.iniCRC = 2;
	create.stagingRoomCreation.gameVersion = 3;
	create.stagingRoomCreation.allowObservers = FALSE;
	create.stagingRoomCreation.useStats = TRUE;
	create.stagingRoomCreation.ladPort = 0;
	rig.peer->addRequest( create );
	rig.feed( "{\"t\":\"game.joined\",\"action\":\"joined\",\"id\":1,\"name\":\"g\",\"map\":\"\",\"maxp\":8,\"host\":\"Tank\",\"hostId\":7,\"count\":1,\"opts\":\"\",\"ids\":[7],\"nicks\":[\"Tank\"],\"vips\":[167772163]}" );
	rig.peerResponses();
	rig.sent.clear();
}

TEST(online_adapter_host_seats_a_joiner_at_its_relay_address)
{
	AdapterRig rig;
	hostGameOne( rig );

	// what the WOL staging room seats a joiner from: PLAYERJOIN, StagingRoom, the IP in network order
	rig.feed( "{\"t\":\"player\",\"game\":1,\"id\":9,\"nick\":\"Kim\",\"vip\":167772162}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)1 );
	if (r.size() == 1)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_PLAYERJOIN );
		CHECK_EQ( (int)r[0].player.roomType, (int)StagingRoom );
		CHECK_STR( r[0].nick.c_str(), "Kim" );
		CHECK_EQ( r[0].player.profileID, 9 );
		CHECK_EQ( r[0].player.IP, OnlineHostToNet( 0x0A000002 ) );
	}

	// and leaving carries it too
	rig.feed( "{\"t\":\"player.left\",\"game\":1,\"id\":9,\"nick\":\"Kim\"}" );
	r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)1 );
	if (r.size() == 1)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_PLAYERLEFT );
		CHECK_EQ( r[0].player.IP, OnlineHostToNet( 0x0A000002 ) );
	}
}

TEST(online_adapter_staging_chat_arrives_once)
{
	AdapterRig rig;
	hostGameOne( rig );

	// staging chat names the game by number; the chat room's line for "lobby1" is not the staging room's
	PeerRequest say;
	say.peerRequestType = PeerRequest::PEERREQUEST_MESSAGEROOM;
	say.text = u"gl hf";
	say.message.isAction = FALSE;
	rig.peer->addRequest( say );
	CHECK_STR( rig.sentType( 0 ).c_str(), "say" );
	CHECK_EQ( rig.sent[0].getInt( "room" ), 1 );

	rig.feed( "{\"t\":\"said\",\"room\":\"lobby1\",\"from\":9,\"nick\":\"Kim\",\"text\":\"hi\",\"action\":0}" );
	rig.feed( "{\"t\":\"said\",\"room\":1,\"from\":9,\"nick\":\"Kim\",\"text\":\"hi\",\"action\":0}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)1 );
	if (r.size() == 1)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_MESSAGE );
		CHECK( r[0].text == WideCharString( u"hi" ) );
	}
}

TEST(online_adapter_logs_the_rating_result)
{
	AdapterRig rig;
	rig.login();
	rig.feed( "{\"t\":\"rated\",\"match\":\"q1\",\"delta\":1.25,\"rating\":-3.5}" );
	CHECK_STR( OnlineSession::get().resultLine.c_str(), "ONLINE RATED delta=1.25 rating=-3.5" );
	CHECK_STR( rig.sentType( 0 ).c_str(), "stats.get" );		// our own numbers again
	rig.feed( "{\"t\":\"unrated\",\"match\":\"m2\",\"reason\":\"custom\"}" );
	CHECK_STR( OnlineSession::get().resultLine.c_str(), "ONLINE UNRATED reason=custom" );
}

TEST(online_adapter_leaves_the_chat_room_for_a_staging_room)
{
	AdapterRig rig;
	rig.login();
	PeerRequest join;
	join.peerRequestType = PeerRequest::PEERREQUEST_JOINGROUPROOM;
	join.groupRoom.id = 1;
	rig.peer->addRequest( join );
	rig.feed( "{\"t\":\"room\",\"id\":\"lobby1\",\"ids\":[7],\"nicks\":[\"Tank\"],\"vips\":[167772163]}" );
	rig.peerResponses();
	rig.sent.clear();

	PeerRequest create;
	create.peerRequestType = PeerRequest::PEERREQUEST_CREATESTAGINGROOM;
	create.text = u"fun game";
	rig.peer->addRequest( create );
	CHECK_STR( rig.sentType( 0 ).c_str(), "room.leave" );
	CHECK_STR( rig.sentType( 1 ).c_str(), "game.host" );

	rig.sent.clear();
	PeerRequest leave;
	leave.peerRequestType = PeerRequest::PEERREQUEST_LEAVEGROUPROOM;
	rig.peer->addRequest( leave );
	CHECK_EQ( rig.sent.size(), (size_t)0 );		// already out
}

static std::string unbase64( const std::string &in )
{
	std::string out;
	UnsignedInt acc = 0;
	int bits = 0;
	for (size_t i = 0; i < in.size() && in[i] != '='; ++i)
	{
		const char c = in[i];
		int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26
			: c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
		if (v < 0)
			return "<bad digit>";
		acc = (acc << 6) | (UnsignedInt)v;
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out += (char)((acc >> bits) & 0xFF);
		}
	}
	return out;
}

/* A Quick Match's replay goes up in order, 4096 bytes a line, once, and only while logged in; a custom
	 game's never does. */
TEST(online_replay_chunks_and_goes_once)
{
	AdapterRig rig;
	const char *path = "test_online_replay.tmp";
	std::string bytes;
	for (int i = 0; i < 2 * ONLINE_REPLAY_CHUNK + 5; ++i)
		bytes += (char)(i * 7 + (i >> 8));
	FILE *f = fopen( path, "wb" );
	CHECK( f != NULL );
	if (!f)
		return;
	fwrite( bytes.data(), 1, bytes.size(), f );
	fclose( f );

	OnlineSession::get().matchId = "m9";
	OnlineSendReplay( "m9", path );		// not logged in: nothing, and the match is used up
	CHECK_EQ( rig.sent.size(), (size_t)0 );

	rig.login();

	// a custom game: game.start leaves nothing to claim
	rig.feed( "{\"t\":\"game.start\",\"match\":\"c1\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "" );
	OnlineSendReplay( "", path );
	CHECK_EQ( rig.sent.size(), (size_t)0 );

	// a Quick Match cancelled in the reveal uses its id up
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q9\",\"revealMs\":3000}" );
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"searching\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "" );

	// a Quick Match: its recording claims the id, and only that one
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"m10\"}" );
	rig.peerResponses();
	rig.sent.clear();
	const AsciiString claimed = OnlineClaimQuickMatch();
	CHECK_STR( claimed.str(), "m10" );
	CHECK_STR( OnlineClaimQuickMatch().str(), "" );		// a second recording under m10 gets nothing
	OnlineSendReplay( claimed.str(), path );
	CHECK_EQ( rig.sent.size(), (size_t)3 );
	std::string joined;
	for (size_t i = 0; i < rig.sent.size(); ++i)
	{
		const OnlineMessage &m = rig.sent[i];
		CHECK_STR( m.type().c_str(), "replay" );
		CHECK_STR( m.getString( "match" ).c_str(), "m10" );
		CHECK_EQ( m.getInt( "seq", -1 ), (long long)i );
		CHECK( m.getBool( "last" ) == (i + 1 == rig.sent.size()) );
		std::string line;
		CHECK( m.toLine( line ) && line.size() <= ONLINE_MAX_LINE );
		joined += unbase64( m.getString( "data" ) );
	}
	CHECK( joined == bytes );

	rig.sent.clear();
	OnlineSendReplay( claimed.str(), path );		// the score screen closing it again
	CHECK_EQ( rig.sent.size(), (size_t)0 );

	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"m11\"}" );
	rig.peerResponses();
	rig.sent.clear();
	CHECK_STR( OnlineClaimQuickMatch().str(), "m11" );
	OnlineSendReplay( "m11", AsciiString::TheEmptyString );		// a desynced recording uses m11 up
	OnlineSendReplay( "m11", path );
	CHECK_EQ( rig.sent.size(), (size_t)0 );
	remove( path );
}

/* The score screen's "sending results" box: up from the report until the claimed replay is queued, and
	 never for a match that sent no report.  Captured lines count as written, so only the replay waits. */
TEST(online_results_are_sending_until_the_replay_is_queued)
{
	AdapterRig rig;
	const char *path = "test_online_sending.tmp";
	FILE *f = fopen( path, "wb" );
	CHECK( f != NULL );
	if (!f)
		return;
	fputs( "GENREP", f );
	fclose( f );
	rig.login();
	OnlineReport report;
	report.numPlayers = 2;

	CHECK( !OnlineIsSendingResults() );		// no match

	// a custom game: the report goes out, no replay follows
	rig.feed( "{\"t\":\"game.start\",\"match\":\"c3\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "" );
	CHECK( !OnlineIsSendingResults() );		// no report yet
	OnlineSendReport( report );
	CHECK( !OnlineIsSendingResults() );

	// a Quick Match: from the report until its recording closes
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q3\"}" );
	rig.peerResponses();
	const AsciiString claimed = OnlineClaimQuickMatch();
	CHECK_STR( claimed.str(), "q3" );
	CHECK( !OnlineIsSendingResults() );
	OnlineSendReport( report );
	CHECK( OnlineIsSendingResults() );
	OnlineSendReplay( claimed.str(), path );
	CHECK( !OnlineIsSendingResults() );

	// a desynced recording gives its replay up, and nothing is left to wait for
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q4\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "q4" );
	OnlineSendReport( report );
	CHECK( OnlineIsSendingResults() );
	OnlineSendReplay( "q4", AsciiString::TheEmptyString );
	CHECK( !OnlineIsSendingResults() );

	// a logout drops the match: nothing waits under it
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q5\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "q5" );
	OnlineSendReport( report );
	CHECK( OnlineIsSendingResults() );
	OnlineSession::get().forgetAccount();
	CHECK( !OnlineIsSendingResults() );

	// an older server never answers: once the replay is queued nothing waits for a report.ok
	rig.login();
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q6\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "q6" );
	OnlineSendReport( report );
	OnlineSendReplay( "q6", path );
	CHECK( !OnlineIsSendingResults() );
	remove( path );
}

/* A server whose auth.ok says acks: the box waits for report.ok/err and, when this client sent the
	 replay, replay.ok/err, for this match; an answer for another match is not one. */
TEST(online_results_are_sending_until_the_server_answers)
{
	AdapterRig rig;
	const char *path = "test_online_acks.tmp";
	FILE *f = fopen( path, "wb" );
	CHECK( f != NULL );
	if (!f)
		return;
	fputs( "GENREP", f );
	fclose( f );
	rig.login( true );
	OnlineReport report;
	report.numPlayers = 2;

	// a custom game: the report alone, answered ok
	rig.feed( "{\"t\":\"game.start\",\"match\":\"c1\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "" );
	OnlineSendReport( report );
	CHECK( OnlineIsSendingResults() );
	rig.feed( "{\"t\":\"report.ok\",\"match\":\"c0\"}" );		// another match
	rig.feed( "{\"t\":\"replay.ok\",\"match\":\"c1\"}" );		// not the report's answer
	CHECK( OnlineIsSendingResults() );
	rig.feed( "{\"t\":\"report.ok\",\"match\":\"c1\"}" );
	CHECK( !OnlineIsSendingResults() );

	// a Quick Match: the report and the replay, each answered
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q1\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "q1" );
	OnlineSendReport( report );
	OnlineSendReplay( "q1", path );
	CHECK( OnlineIsSendingResults() );
	rig.feed( "{\"t\":\"report.ok\",\"match\":\"q1\"}" );
	CHECK( OnlineIsSendingResults() );		// the replay is not confirmed
	rig.feed( "{\"t\":\"replay.ok\",\"match\":\"c1\"}" );
	CHECK( OnlineIsSendingResults() );
	rig.feed( "{\"t\":\"replay.ok\",\"match\":\"q1\"}" );
	CHECK( !OnlineIsSendingResults() );

	// refusals end the wait just as well
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q2\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "q2" );
	OnlineSendReport( report );
	OnlineSendReplay( "q2", path );
	rig.feed( "{\"t\":\"replay.err\",\"match\":\"q2\",\"code\":\"toolarge\"}" );
	CHECK( OnlineIsSendingResults() );
	rig.feed( "{\"t\":\"report.err\",\"match\":\"q2\",\"code\":\"unknown_match\"}" );
	CHECK( !OnlineIsSendingResults() );

	// a desynced recording gives its replay up: only the report is waited for
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q3\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "q3" );
	OnlineSendReport( report );
	OnlineSendReplay( "q3", AsciiString::TheEmptyString );
	CHECK( OnlineIsSendingResults() );
	rig.feed( "{\"t\":\"report.ok\",\"match\":\"q3\"}" );
	CHECK( !OnlineIsSendingResults() );

	// a logout: no answer comes for the account's match any more
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q4\"}" );
	rig.peerResponses();
	CHECK_STR( OnlineClaimQuickMatch().str(), "q4" );
	OnlineSendReport( report );
	OnlineSendReplay( "q4", path );
	CHECK( OnlineIsSendingResults() );
	OnlineSession::get().forgetAccount();
	CHECK( !OnlineIsSendingResults() );
	remove( path );
}

TEST(online_register_url_from_the_welcome_and_only_a_web_page_opens)
{
	AdapterRig rig;
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094}" );
	CHECK( OnlineGetRegisterUrl().isEmpty() );		// none sent: Create Account hides
	CHECK( OnlineGetResetUrl().isEmpty() );				// and Forgot Password
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094,\"registerUrl\":\"https://example.test/register\","
		"\"resetUrl\":\"https://example.test/forgot\"}" );
	CHECK_STR( OnlineGetRegisterUrl().str(), "https://example.test/register" );
	CHECK_STR( OnlineGetResetUrl().str(), "https://example.test/forgot" );
	// a hostile server's address is refused before anything is asked to open it
	CHECK( !OnlineOpenURL( "file:///C:/Windows/System32/calc.exe" ) );
	CHECK( !OnlineOpenURL( "C:\\Windows\\notepad.exe" ) );
	CHECK( !OnlineOpenURL( "" ) );
	CHECK( !OnlineOpenURL( NULL ) );
}

/* welcome.counts and accountUrl, the 30-second counts line in either spelling, and no counts once the
	 connection is gone. */
TEST(online_counts_and_account_url_from_the_welcome)
{
	AdapterRig rig;
	Int players = -1, games = -1, queue = -1;
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094}" );
	CHECK( !OnlineGetCounts( players, games, queue ) );		// an older server: the screens show a dash
	CHECK( players == 0 && games == 0 && queue == 0 );
	CHECK( OnlineGetAccountUrl().isEmpty() );
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094,\"accountUrl\":\"https://example.test/account\","
		"\"counts\":{\"players\":41,\"games\":6,\"queue\":3}}" );
	CHECK_STR( OnlineGetAccountUrl().str(), "https://example.test/account" );
	CHECK( OnlineGetCounts( players, games, queue ) );
	CHECK( players == 41 && games == 6 && queue == 3 );

	rig.login();
	rig.feed( "{\"t\":\"counts\",\"players\":44,\"games\":7,\"queue\":0}" );
	CHECK( OnlineGetCounts( players, games, queue ) );
	CHECK( players == 44 && games == 7 && queue == 0 );
	rig.feed( "{\"t\":\"counts\",\"counts\":{\"players\":45,\"games\":8,\"queue\":2}}" );
	CHECK( OnlineGetCounts( players, games, queue ) );
	CHECK( players == 45 && games == 8 && queue == 2 );

	OnlineSession::get().disconnect();
	CHECK( !OnlineGetCounts( players, games, queue ) );
}

/* Ratings to two decimals wherever the server sends one; a whole number from an older server still reads. */
TEST(online_ratings_keep_two_decimals)
{
	AdapterRig rig;
	rig.login();
	rig.feed( "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
		"\"ticket\":\"00112233445566778899aabbccddeeff\",\"rating\":21.37,\"aiRating\":14.1,\"aiGames\":3,\"aiWins\":2}" );
	OnlinePlayerInfo me;
	CHECK( OnlineGetPlayerInfo( "Tank", me ) );
	CHECK( me.hasRating );
	CHECK_NEAR( me.exactRating, 21.37, 0.001 );
	CHECK_EQ( me.rating, 21 );
	CHECK( me.hasAIRating );
	CHECK_NEAR( me.aiRating, 14.1, 0.001 );
	CHECK_EQ( me.aiGames, 3 );
	CHECK_EQ( me.aiWins, 2 );

	// another player's card from stats, his AI numbers included
	OnlineRequestStats( "Kim" );
	rig.feed( "{\"t\":\"stats\",\"id\":9,\"nick\":\"Kim\",\"rating\":31.5,\"tier\":\"major\",\"wins\":5,\"losses\":2,\"games\":7,\"rank\":12,"
		"\"aiRating\":9.87,\"aiGames\":11,\"aiWins\":6}" );
	OnlinePlayerInfo kim;
	CHECK( OnlineGetPlayerInfo( "Kim", kim ) );
	CHECK( kim.hasStats );
	CHECK_NEAR( kim.exactRating, 31.5, 0.001 );
	CHECK_EQ( kim.rating, 32 );
	CHECK( kim.hasAIRating );
	CHECK_NEAR( kim.aiRating, 9.87, 0.001 );
	CHECK_EQ( kim.aiGames, 11 );
	CHECK_EQ( kim.aiWins, 6 );

	// a whole number, as an older server sends it; then a move in the second decimal, which redraws the card
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":11,\"nick\":\"Lee\",\"rating\":1500}" );
	OnlinePlayerInfo lee;
	CHECK( OnlineGetPlayerInfo( "Lee", lee ) );
	CHECK( lee.hasRating );
	CHECK_EQ( lee.rating, 1500 );
	CHECK_NEAR( lee.exactRating, 1500.0, 0.001 );
	const UnsignedInt serial = lee.serial;
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":11,\"nick\":\"Lee\",\"rating\":1500.01}" );
	CHECK( OnlineGetPlayerInfo( "Lee", lee ) );
	CHECK( lee.serial != serial );
	CHECK_NEAR( lee.exactRating, 1500.01, 0.001 );
	CHECK_EQ( lee.rating, 1500 );

	// ladder rows
	OnlineRequestLadder( 0 );
	rig.feed( "{\"t\":\"ladder\",\"offset\":0,\"rows\":[{\"rank\":1,\"nick\":\"Kim\",\"rating\":31.49,\"tier\":\"major\",\"wins\":5,\"losses\":2}],"
		"\"me\":{\"rank\":40,\"nick\":\"Tank\",\"rating\":21.37,\"tier\":null,\"wins\":1,\"losses\":3}}" );
	Int offset = -1;
	std::vector<OnlineLadderRow> rows;
	OnlineLadderRow mine;
	Bool hasMe = FALSE;
	CHECK_EQ( (int)OnlineGetLadder( offset, rows, mine, hasMe ), (int)ONLINE_LADDER_READY );
	CHECK_EQ( rows.size(), (size_t)1 );
	if (!rows.empty())
	{
		CHECK_NEAR( rows[0].exactRating, 31.49, 0.001 );
		CHECK_EQ( rows[0].rating, 31 );
	}
	CHECK( hasMe );
	CHECK_NEAR( mine.exactRating, 21.37, 0.001 );

	// the match's verdict, as delta and rating or as before and after
	Bool rated = FALSE;
	Real delta = 0, rating = 0;
	AsciiString reason;
	OnlineSession::get().matchId = "q8";
	rig.feed( "{\"t\":\"rated\",\"match\":\"q8\",\"delta\":1.23,\"rating\":22.6}" );
	CHECK( OnlineGetMatchResult( rated, delta, rating, reason ) );
	CHECK_NEAR( delta, 1.23, 0.001 );
	CHECK_NEAR( rating, 22.6, 0.001 );
	rig.feed( "{\"t\":\"rated\",\"match\":\"q8\",\"before\":22.6,\"after\":24.05}" );
	CHECK( OnlineGetMatchResult( rated, delta, rating, reason ) );
	CHECK_NEAR( delta, 1.45, 0.001 );
	CHECK_NEAR( rating, 24.05, 0.001 );

	// Play vs AI's verdict, which moves the player's own card too
	rig.feed( "{\"t\":\"ai.rated\",\"matchId\":\"a1\",\"before\":14.1,\"after\":15.37,\"games\":4,\"wins\":3}" );
	OnlineAIVerdict verdict;
	CHECK( OnlineGetAIVerdict( verdict ) );
	CHECK_NEAR( verdict.before, 14.1, 0.001 );
	CHECK_NEAR( verdict.after, 15.37, 0.001 );
	CHECK( OnlineGetPlayerInfo( "Tank", me ) );
	CHECK_NEAR( me.aiRating, 15.37, 0.001 );
	CHECK_EQ( me.aiWins, 3 );
}

/* history{id, mode, limit}: asked, waited on, answered with rows and both army tables, refused, asked
	 again with the last answer still on show, forgotten at sign-out. */
TEST(online_history_rows_and_army_tables)
{
	AdapterRig rig;
	OnlineHistory h;
	OnlineRequestHistory( 0, ONLINE_HISTORY_ALL );
	CHECK( rig.sent.empty() );		// not signed in: nothing asked
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_FAILED );
	rig.login();

	OnlineRequestHistory( 0, ONLINE_HISTORY_ALL );
	CHECK_STR( rig.sentType( 0 ).c_str(), "history" );
	if (!rig.sent.empty())
	{
		CHECK( !rig.sent[0].has( "id" ) );		// absent: the signed-in player
		CHECK_STR( rig.sent[0].getString( "mode" ).c_str(), "all" );
		CHECK_EQ( rig.sent[0].getInt( "limit" ), 5 );
	}
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_WAITING );
	CHECK( !h.arrived );
	rig.feed( "{\"t\":\"history\",\"id\":7,\"mode\":\"all\",\"games\":12,\"wins\":7,\"rows\":["
		"{\"mode\":\"qm\",\"result\":\"win\",\"map\":\"Tournament Desert\",\"opponent\":\"Kim\",\"opponentRating\":31.25,"
		"\"playerArmy\":\"FactionAmerica\",\"opponentArmy\":\"FactionChina\",\"seconds\":914,\"change\":1.37,\"age\":3600},"
		"{\"mode\":\"ai\",\"result\":\"loss\",\"map\":\"Twilight Flame\",\"opponent\":\"\",\"opponentRating\":14.1,"
		"\"playerArmy\":\"FactionGLA\",\"opponentArmy\":\"FactionAmericaAirForceGeneral\",\"seconds\":1200,\"change\":-0.42,\"age\":86400}],"
		"\"byArmy\":[{\"army\":\"FactionAmerica\",\"games\":8,\"wins\":5},{\"army\":\"FactionGLA\",\"games\":4,\"wins\":2}],"
		"\"byOpponentArmy\":[{\"army\":\"FactionChina\",\"games\":12,\"wins\":7}]}" );
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_READY );
	CHECK( h.arrived );
	CHECK_EQ( h.id, 7 );
	CHECK_EQ( h.games, 12 );
	CHECK_EQ( h.wins, 7 );
	CHECK_EQ( h.rows.size(), (size_t)2 );
	if (h.rows.size() == 2)
	{
		const OnlineHistoryRow &q = h.rows[0], &a = h.rows[1];
		CHECK( !q.ai && q.won );
		CHECK_STR( q.map.str(), "Tournament Desert" );
		CHECK_STR( q.opponent.str(), "Kim" );
		CHECK_NEAR( q.opponentRating, 31.25, 0.001 );
		CHECK_STR( q.playerArmy.str(), "FactionAmerica" );
		CHECK_STR( q.opponentArmy.str(), "FactionChina" );
		CHECK_EQ( q.seconds, 914 );
		CHECK_NEAR( q.change, 1.37, 0.001 );
		CHECK_EQ( q.age, 3600 );
		CHECK( a.ai && !a.won );
		CHECK( a.opponent.isEmpty() );
		CHECK_NEAR( a.opponentRating, 14.1, 0.001 );		// the rating the computer played at
		CHECK_STR( a.opponentArmy.str(), "FactionAmericaAirForceGeneral" );
		CHECK_NEAR( a.change, -0.42, 0.001 );
		CHECK_EQ( a.age, 86400 );
	}
	CHECK_EQ( h.byArmy.size(), (size_t)2 );
	if (h.byArmy.size() == 2)
	{
		CHECK_STR( h.byArmy[0].army.str(), "FactionAmerica" );
		CHECK( h.byArmy[0].games == 8 && h.byArmy[0].wins == 5 );
		CHECK_STR( h.byArmy[1].army.str(), "FactionGLA" );
		CHECK( h.byArmy[1].games == 4 && h.byArmy[1].wins == 2 );
	}
	CHECK_EQ( h.byOpponentArmy.size(), (size_t)1 );
	if (!h.byOpponentArmy.empty())
	{
		CHECK_STR( h.byOpponentArmy[0].army.str(), "FactionChina" );
		CHECK( h.byOpponentArmy[0].games == 12 && h.byOpponentArmy[0].wins == 7 );
	}
	CHECK_EQ( (int)OnlineGetHistory( 7, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_READY );		// the own id finds it too
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_AI, h ), (int)ONLINE_LADDER_FAILED );		// never asked
	CHECK( !h.arrived );

	// another player's Quick Matches, the limit held to 20; the error fails that ask alone
	rig.sent.clear();
	OnlineRequestHistory( 9, ONLINE_HISTORY_QM, 50 );
	CHECK_STR( rig.sentType( 0 ).c_str(), "history" );
	if (!rig.sent.empty())
	{
		CHECK_EQ( rig.sent[0].getInt( "id" ), 9 );
		CHECK_STR( rig.sent[0].getString( "mode" ).c_str(), "qm" );
		CHECK_EQ( rig.sent[0].getInt( "limit" ), 20 );
	}
	rig.feed( "{\"t\":\"error\",\"for\":\"history\",\"code\":\"unknown\"}" );
	CHECK_EQ( (int)OnlineGetHistory( 9, ONLINE_HISTORY_QM, h ), (int)ONLINE_LADDER_FAILED );
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_READY );

	// asked again: the last answer stays on show until the new one comes
	OnlineRequestHistory( 0, ONLINE_HISTORY_ALL );
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_WAITING );
	CHECK( h.arrived );
	CHECK_EQ( h.rows.size(), (size_t)2 );

	// a line for a mode this build does not know is dropped
	rig.feed( "{\"t\":\"history\",\"id\":7,\"mode\":\"ffa\",\"games\":1,\"wins\":0,\"rows\":[]}" );
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_WAITING );

	OnlineSession::get().forgetAccount();
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_FAILED );
	CHECK( !h.arrived );
}

/* qm.status's window, counted down to the next widening, and qm.matched's chance on the reveal. */
TEST(online_quick_match_window_and_chance)
{
	AdapterRig rig;
	rig.login();
	Real lo = -1, hi = -1;
	Int nextMs = 0;
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"searching\",\"pool\":4}" );
	CHECK( !OnlineGetQuickMatchChanceWindow( lo, hi, nextMs ) );		// an older server
	CHECK_EQ( nextMs, -1 );
	// win-chance points: 45 to 55 at join
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"searching\",\"pool\":4,\"window\":{\"lo\":45,\"hi\":55,\"nextMs\":4000}}" );
	CHECK( OnlineGetQuickMatchChanceWindow( lo, hi, nextMs ) );
	CHECK_NEAR( lo, 45.0, 0.001 );
	CHECK_NEAR( hi, 55.0, 0.001 );
	CHECK( nextMs > 3000 && nextMs <= 4000 );		// counted down from the line's arrival
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"searching\",\"pool\":4,\"window\":{\"lo\":30,\"hi\":70}}" );
	CHECK( OnlineGetQuickMatchChanceWindow( lo, hi, nextMs ) );
	CHECK_NEAR( lo, 30.0, 0.001 );
	CHECK_NEAR( hi, 70.0, 0.001 );
	CHECK_EQ( nextMs, -1 );		// the widest
	rig.peerResponses();

	// the reveal carries the local player's chance, and the match ends the search's window
	const char *head = "{\"t\":\"qm.matched\",\"match\":\"q3\",\"seed\":5,\"mapIdx\":0,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],"
		"\"vips\":[167772163,167772162],\"sides\":[-1,-1],\"colors\":[-1,-1],\"revealMs\":3000,";
	rig.feed( ( std::string( head ) + "\"chance\":0.62,\"opponents\":[{\"nick\":\"Kim\",\"rating\":1550.75}]}" ).c_str() );
	OnlineMatchReveal rev;
	CHECK( OnlineGetMatchReveal( rev ) );
	CHECK( rev.hasChance );
	CHECK_NEAR( rev.chance, 0.62, 0.001 );
	CHECK_EQ( rev.opponents.size(), (size_t)1 );
	if (!rev.opponents.empty())
	{
		CHECK_NEAR( rev.opponents[0].exactRating, 1550.75, 0.001 );
		CHECK_EQ( rev.opponents[0].rating, 1551 );
	}
	CHECK( !OnlineGetQuickMatchChanceWindow( lo, hi, nextMs ) );

	// an older server's reveal has no chance
	rig.feed( ( std::string( head ) + "\"opponents\":[{\"nick\":\"Kim\",\"rating\":1550}]}" ).c_str() );
	CHECK( OnlineGetMatchReveal( rev ) );
	CHECK( !rev.hasChance );
}

/* The hello says proto 2, so a server sends the welcome's counts object; it still carries what an older
	 server reads. */
TEST(online_hello_says_proto_2)
{
	const OnlineMessage hello = OnlineHelloLine( 0xDEADBEEFu, 0x12345678u, "3.4.1" );
	CHECK_STR( hello.type().c_str(), "hello" );
	CHECK_EQ( hello.getInt( "proto" ), 2 );
	CHECK_EQ( hello.getInt( "exe" ), 0xDEADBEEFll );
	CHECK_EQ( hello.getInt( "ini" ), 0x12345678ll );
	CHECK_STR( hello.getString( "ver" ).c_str(), "3.4.1" );
	CHECK_EQ( hello.getInt( "files" ), 1ll );		// the install files' hashes come before the login
	std::string line;
	CHECK( hello.toLine( line ) );
	CHECK( OnlineHelloLine( 1, 2, NULL ).toLine( line ) );		// no version: an empty one
}

/* Before the login, every install file's path and sha256 in files lines under 4096 bytes, numbered, the
	 last marked; once a connection, so a second login on it sends the login alone.  A refusal over a file
	 names it for the login screen. */
TEST(online_login_sends_the_install_files_first)
{
	std::vector<InstallFile> files;
	for (int i = 0; i < 300; ++i)
	{
		InstallFile f;
		char path[64];
		snprintf( path, sizeof(path), "art/textures/a_long_texture_name_number_%03d.tga", i );
		f.path = path;
		f.sha256 = std::string( 64, (char)('a' + i % 6) );
		files.push_back( f );
	}
	InstallFiles_setList( files );
	{
		AdapterRig rig;
		BuddyRequest req;
		memset( &req, 0, sizeof(req) );
		req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
		strcpy( req.arg.login.email, "tank@example.com" );
		strcpy( req.arg.login.password, "secret" );
		rig.buddy->addRequest( req );

		CHECK( rig.sent.size() > 3 );
		std::vector<std::string> paths, hashes;
		size_t lines = 0;
		while (lines < rig.sent.size() && rig.sentType( lines ) == "files")
		{
			const OnlineMessage &m = rig.sent[lines];
			CHECK_EQ( m.getInt( "seq" ), (long long)lines );
			std::string line;
			CHECK( m.toLine( line ) );
			CHECK( line.size() < 4096 );
			const std::vector<std::string> p = m.getStrings( "paths" ), h = m.getStrings( "hashes" );
			CHECK_EQ( p.size(), h.size() );
			paths.insert( paths.end(), p.begin(), p.end() );
			hashes.insert( hashes.end(), h.begin(), h.end() );
			++lines;
			CHECK_EQ( m.getInt( "last", -1 ), lines == rig.sent.size() - 1 ? 1ll : 0ll );
		}
		CHECK( lines > 1 );
		CHECK_EQ( lines, rig.sent.size() - 1 );
		CHECK_STR( rig.sentType( lines ).c_str(), "login" );
		CHECK_EQ( paths.size(), files.size() );
		for (size_t i = 0; i < paths.size() && i < files.size(); ++i)
		{
			CHECK_STR( paths[i].c_str(), files[i].path.c_str() );
			CHECK_STR( hashes[i].c_str(), files[i].sha256.c_str() );
		}

		rig.feed( "{\"t\":\"auth.fail\",\"code\":\"files\",\"file\":\"art/textures/a_long_texture_name_number_007.tga\"}" );
		AsciiString file;
		CHECK_STR( OnlineGetLoginFailure( NULL, &file ).str(), "files" );
		CHECK_STR( file.str(), "art/textures/a_long_texture_name_number_007.tga" );

		rig.sent.clear();
		rig.buddy->addRequest( req );
		CHECK_EQ( rig.sent.size(), (size_t)1 );
		CHECK_STR( rig.sentType( 0 ).c_str(), "login" );
		CHECK( OnlineGetLoginFailure( NULL, &file ).isEmpty() );
		CHECK( file.isEmpty() );
	}
	InstallFiles_setList( std::vector<InstallFile>() );		// the other tests log in with none
}

/* The report names its map for the match history: the folder and file under maps, never the user folder
	 a generated map sits in; a name the line cannot carry is left out and the report still goes. */
TEST(online_report_names_its_map_without_the_user_folder)
{
	AdapterRig rig;
	rig.login();
	OnlineReport report;
	report.endFrame = 900;
	report.numPlayers = 2;
	report.side[0] = "America";
	report.side[1] = "China";
	const char *matched = "{\"t\":\"qm.matched\",\"match\":\"%s\",\"seed\":7,\"mapIdx\":0,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],"
		"\"vips\":[1,2],\"sides\":[0,0],\"colors\":[0,1]}";
	char line[512];
	struct Case { const char *map; const char *sent; } cases[] = {
		{ "c:\\users\\tank\\documents\\command and conquer generals zero hour data\\maps\\rmg_v3_77_2p_96c\\rmg_v3_77_2p_96c.map",
			"maps\\rmg_v3_77_2p_96c\\rmg_v3_77_2p_96c.map" },
		{ "maps\\tournament desert\\tournament desert.map", "maps\\tournament desert\\tournament desert.map" },
		{ "/home/tank/.local/share/zh/maps/rmg_v3_8_2p_96c/rmg_v3_8_2p_96c.map", "maps\\rmg_v3_8_2p_96c\\rmg_v3_8_2p_96c.map" },
		{ "lone.map", "lone.map" },
		{ "maps\\caf\xE9\\caf\xE9.map", NULL },		// a code page's byte, not UTF-8
		{ "", NULL },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		char match[16];
		snprintf( match, sizeof(match), "r%d", (int)i );
		snprintf( line, sizeof(line), matched, match );
		rig.feed( line );
		rig.sent.clear();
		report.map = cases[i].map;
		CHECK( OnlineSendReport( report ) );
		CHECK_EQ( rig.sent.size(), (size_t)1 );
		if (rig.sent.empty())
			continue;
		if (cases[i].sent)
			CHECK_STR( rig.sent[0].getString( "map" ).c_str(), cases[i].sent );
		else
			CHECK( !rig.sent[0].has( "map" ) );
	}

	// over 255 bytes: left out
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"rlong\",\"seed\":7,\"mapIdx\":0,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],\"vips\":[1,2],\"sides\":[0,0],\"colors\":[0,1]}" );
	rig.sent.clear();
	const std::string name( 130, 'm' );
	report.map = ( "maps\\" + name + "\\" + name + ".map" ).c_str();
	CHECK( OnlineSendReport( report ) );
	CHECK( rig.sent.size() == 1 && !rig.sent[0].has( "map" ) );
}

/* What belonged to the connection (counts, the server's name, the round trip) goes with it, however it
	 went: OnlineDisconnect, the line closing on its own and a dial that failed all run connectionGone. */
TEST(online_connection_gone_clears_counts_server_and_ping)
{
	AdapterRig rig;
	OnlineSession &s = OnlineSession::get();
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094,\"counts\":{\"players\":4,\"games\":1,\"queue\":0},"
		"\"server\":{\"name\":\"EU-1\",\"city\":\"Frankfurt\"}}" );
	rig.feed( "{\"t\":\"pong\",\"n\":1}" );
	Int players = 0, games = 0, queue = 0, pingMs = -1;
	AsciiString name, city;
	CHECK( OnlineGetCounts( players, games, queue ) );
	CHECK( OnlineGetServerInfo( name, city, pingMs ) );
	CHECK( pingMs >= 0 );
	s.connectionGone();
	CHECK( !OnlineGetCounts( players, games, queue ) );
	CHECK( !OnlineGetServerInfo( name, city, pingMs ) );
	CHECK( name.isEmpty() && city.isEmpty() );
	CHECK_EQ( pingMs, -1 );
	CHECK_EQ( s.pingSentMs, 0ll );		// the next welcome pings at once
}

/* welcome.server and our own ping{n}: one after the welcome, then every ONLINE_PING_MS, the pong's round
	 trip smoothed; a stale or repeated pong counts nothing, and a server that does not know ping is left
	 alone. */
TEST(online_server_info_and_round_trip)
{
	AdapterRig rig;
	OnlineSession &s = OnlineSession::get();
	AsciiString name, city;
	Int pingMs = 0;
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094}" );
	CHECK( !OnlineGetServerInfo( name, city, pingMs ) );		// an older server: no server object
	CHECK_EQ( pingMs, -1 );
	rig.sent.clear();

	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094,\"server\":{\"name\":\"EU-1\",\"city\":\"Frankfurt\"}}" );
	CHECK( OnlineGetServerInfo( name, city, pingMs ) );
	CHECK_STR( name.str(), "EU-1" );
	CHECK_STR( city.str(), "Frankfurt" );
	CHECK_EQ( pingMs, -1 );		// not measured yet
	CHECK_EQ( rig.sent.size(), (size_t)1 );		// the welcome's ping
	const long long n = rig.sent.empty() ? -1 : rig.sent[0].getInt( "n", -1 );
	CHECK_STR( rig.sentType( 0 ).c_str(), "ping" );
	CHECK( n > 0 );

	// the pong of that ping: a first sample is taken whole
	s.pingSentMs = OnlineNowMs() - 40;
	char line[64];
	snprintf( line, sizeof(line), "{\"t\":\"pong\",\"n\":%lld}", n );
	rig.feed( line );
	CHECK( OnlineGetServerInfo( name, city, pingMs ) );
	CHECK( pingMs >= 40 && pingMs < 60 );
	rig.feed( line );		// the same pong again counts nothing
	CHECK( OnlineGetServerInfo( name, city, pingMs ) && pingMs >= 40 && pingMs < 60 );

	// nothing new before ONLINE_PING_MS, then the next; its 840 ms sample moves the average an eighth
	rig.sent.clear();
	s.pingServer();
	CHECK( rig.sent.empty() );
	s.pingSentMs -= ONLINE_PING_MS + 1;
	s.pingServer();
	CHECK_EQ( rig.sent.size(), (size_t)1 );
	const long long n2 = rig.sent.empty() ? -1 : rig.sent[0].getInt( "n", -1 );
	CHECK_EQ( n2, n + 1 );
	const double before = s.pingSmoothedMs;
	s.pingSentMs = OnlineNowMs() - 840;
	rig.feed( "{\"t\":\"pong\",\"n\":1000}" );		// not ours
	CHECK( s.pingSmoothedMs == before );
	snprintf( line, sizeof(line), "{\"t\":\"pong\",\"n\":%lld}", n2 );
	rig.feed( line );
	CHECK_NEAR( s.pingSmoothedMs, before + (840.0 - before) / 8, 15.0 );

	// a server from before the client's ping answers unknown: no more pings this connection
	rig.feed( "{\"t\":\"error\",\"for\":\"ping\",\"code\":\"unknown\"}" );
	rig.sent.clear();
	s.pingSentMs -= ONLINE_PING_MS + 1;
	s.pingServer();
	CHECK( rig.sent.empty() );
	// the server's own ping is still answered
	rig.feed( "{\"t\":\"ping\",\"n\":5}" );
	CHECK( rig.sent.size() == 1 && rig.sentType( 0 ) == "pong" && rig.sent[0].getInt( "n" ) == 5 );
}

/* history asks: the error that names id and mode settles that key out of order, an older server's error
	 the oldest ask, an ask with no answer fails after ONLINE_HISTORY_TIMEOUT_MS, a key has one ask out at a
	 time, and the serial moves on every change, a second answer included. */
TEST(online_history_errors_timeout_and_serial)
{
	AdapterRig rig;
	OnlineSession &s = OnlineSession::get();
	rig.login();
	OnlineHistory h;
	CHECK_EQ( OnlineGetHistorySerial( 0, ONLINE_HISTORY_ALL ), 0u );		// never asked

	OnlineRequestHistory( 0, ONLINE_HISTORY_ALL );
	OnlineRequestHistory( 9, ONLINE_HISTORY_QM );
	OnlineRequestHistory( 9, ONLINE_HISTORY_AI );
	OnlineRequestHistory( 9, ONLINE_HISTORY_QM );		// already out: not sent again
	CHECK_EQ( rig.sent.size(), (size_t)3 );
	const UnsignedInt mineAsked = OnlineGetHistorySerial( 0, ONLINE_HISTORY_ALL );
	CHECK( mineAsked != 0 );
	CHECK( OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM ) != mineAsked );

	// the throttled second ask answered first, named: only that key fails
	rig.feed( "{\"t\":\"error\",\"for\":\"history\",\"code\":\"throttled\",\"id\":9,\"mode\":\"qm\"}" );
	CHECK_EQ( (int)OnlineGetHistory( 9, ONLINE_HISTORY_QM, h ), (int)ONLINE_LADDER_FAILED );
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_WAITING );
	CHECK_EQ( (int)OnlineGetHistory( 9, ONLINE_HISTORY_AI, h ), (int)ONLINE_LADDER_WAITING );
	// an error naming the mode alone is the player's own
	rig.feed( "{\"t\":\"error\",\"for\":\"history\",\"code\":\"unavailable\",\"mode\":\"all\"}" );
	CHECK_EQ( (int)OnlineGetHistory( 0, ONLINE_HISTORY_ALL, h ), (int)ONLINE_LADDER_FAILED );
	CHECK( OnlineGetHistorySerial( 0, ONLINE_HISTORY_ALL ) != mineAsked );
	// an older server's error names nothing: the oldest ask still out
	rig.feed( "{\"t\":\"error\",\"for\":\"history\",\"code\":\"unknown\"}" );
	CHECK_EQ( (int)OnlineGetHistory( 9, ONLINE_HISTORY_AI, h ), (int)ONLINE_LADDER_FAILED );
	CHECK( s.historyAsked.empty() );

	// an ask the server never answers fails after the timeout, and the key can be asked again
	rig.sent.clear();
	OnlineRequestHistory( 9, ONLINE_HISTORY_QM );
	CHECK_EQ( rig.sent.size(), (size_t)1 );
	const UnsignedInt waiting = OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM );
	CHECK_EQ( (int)OnlineGetHistory( 9, ONLINE_HISTORY_QM, h ), (int)ONLINE_LADDER_WAITING );
	s.history[OnlineHistoryKey( 9, ONLINE_HISTORY_QM )].askedMs -= ONLINE_HISTORY_TIMEOUT_MS + 1;
	CHECK( OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM ) != waiting );
	CHECK_EQ( (int)OnlineGetHistory( 9, ONLINE_HISTORY_QM, h ), (int)ONLINE_LADDER_FAILED );
	CHECK( s.historyAsked.empty() );
	OnlineRequestHistory( 9, ONLINE_HISTORY_QM );
	CHECK_EQ( rig.sent.size(), (size_t)2 );

	// an answer, then a second answer for the same key: the serial moves both times, and reading does not move it
	const char *answer = "{\"t\":\"history\",\"id\":9,\"mode\":\"qm\",\"games\":3,\"wins\":1,\"rows\":[],\"byArmy\":[],\"byOpponentArmy\":[]}";
	rig.feed( answer );
	const UnsignedInt first = OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM );
	CHECK_EQ( (int)OnlineGetHistory( 9, ONLINE_HISTORY_QM, h ), (int)ONLINE_LADDER_READY );
	CHECK_EQ( OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM ), first );
	rig.feed( answer );
	CHECK( OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM ) != first );

	// forgotten at sign-out; asked again after, the serial is new, not one a screen saw before
	const UnsignedInt last = OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM );
	s.forgetAccount();
	CHECK_EQ( OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM ), 0u );
	rig.login();
	OnlineRequestHistory( 9, ONLINE_HISTORY_QM );
	CHECK( OnlineGetHistorySerial( 9, ONLINE_HISTORY_QM ) > last );
}

/* A started custom game stays listed with playing:true: the row reads isStaging FALSE (GameSpy's own
	 meaning), so the lobby can show it as playing and refuse a join; a game in setup is staging. */
TEST(online_game_list_carries_playing)
{
	AdapterRig rig;
	rig.login();
	rig.feed( "{\"t\":\"game\",\"action\":\"add\",\"id\":5,\"name\":\"Fun\",\"map\":\"\",\"maxp\":2,\"host\":\"Kim\",\"hostId\":9,\"count\":2}" );
	rig.feed( "{\"t\":\"game\",\"action\":\"update\",\"id\":5,\"name\":\"Fun\",\"map\":\"\",\"maxp\":2,\"host\":\"Kim\",\"hostId\":9,\"count\":2,\"playing\":true}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK_EQ( r.size(), (size_t)2 );
	if (r.size() == 2)
	{
		CHECK_EQ( (int)r[0].peerResponseType, (int)PeerResponse::PEERRESPONSE_STAGINGROOM );
		CHECK_EQ( r[0].stagingRoom.id, 5 );
		CHECK( r[0].stagingRoom.isStaging );
		CHECK_EQ( (int)r[1].stagingRoom.action, (int)PEER_UPDATE );
		CHECK( !r[1].stagingRoom.isStaging );		// being played
	}
}

/* The abandon lock counted down here: from auth.ok, the player's own card (absent is free) and
	 qm.status{locked,seconds}, never from another player's card or a stats answer; a search clears it. */
TEST(online_quick_match_lock_seconds)
{
	AdapterRig rig;
	rig.login();
	CHECK_EQ( OnlineGetQuickMatchLockSeconds(), 0 );
	rig.feed( "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"t\",\"ticket\":\"00112233445566778899aabbccddeeff\",\"lockSeconds\":492}" );
	Int left = OnlineGetQuickMatchLockSeconds();
	CHECK( left >= 491 && left <= 492 );
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":9,\"nick\":\"Kim\",\"lockSeconds\":60}" );		// someone else's
	rig.feed( "{\"t\":\"stats\",\"id\":7,\"nick\":\"Tank\",\"wins\":1,\"losses\":0}" );		// carries none, says nothing
	left = OnlineGetQuickMatchLockSeconds();
	CHECK( left >= 491 && left <= 492 );
	OnlineSession::get().qmLockUntilMs -= 491500;		// half a second left still reads a second
	CHECK_EQ( OnlineGetQuickMatchLockSeconds(), 1 );
	OnlineSession::get().qmLockUntilMs -= 1000;
	CHECK_EQ( OnlineGetQuickMatchLockSeconds(), 0 );

	// qm.status locked arrives as QM_COULDNOTFINDBOT like any refusal; the seconds tell it apart
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"busy\",\"pool\":2}" );
	CHECK_EQ( OnlineGetQuickMatchLockSeconds(), 0 );
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"locked\",\"pool\":2,\"until\":1900000000000,\"seconds\":300}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK( r.size() == 2 && r[0].qmStatus.status == QM_COULDNOTFINDBOT && r[1].qmStatus.status == QM_COULDNOTFINDBOT );
	left = OnlineGetQuickMatchLockSeconds();
	CHECK( left >= 299 && left <= 300 );
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"locked\",\"pool\":2,\"seconds\":0}" );
	CHECK_EQ( OnlineGetQuickMatchLockSeconds(), 1 );		// its last fraction of a second is still a lock

	// the player's own card without lockSeconds: free
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"locked\",\"pool\":2,\"seconds\":300}" );
	rig.feed( "{\"t\":\"player\",\"room\":\"lobby1\",\"id\":7,\"nick\":\"tank\",\"ping\":30}" );
	CHECK_EQ( OnlineGetQuickMatchLockSeconds(), 0 );
	// and a search taken by the queue
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"locked\",\"pool\":2,\"seconds\":300}" );
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"searching\",\"pool\":3}" );
	CHECK_EQ( OnlineGetQuickMatchLockSeconds(), 0 );
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"locked\",\"pool\":2,\"seconds\":300}" );
	OnlineSession::get().forgetAccount();
	CHECK_EQ( OnlineGetQuickMatchLockSeconds(), 0 );
}

/* Our own copy of match.hold with a ticket hands the ticket to the relay once; another seat's hold, a
	 hold for another match and a resume hand nothing, and the next match drops a ticket nobody took. */
TEST(online_match_hold_relay_ticket)
{
	AdapterRig rig;
	rig.login();
	rig.feed( "{\"t\":\"game.start\",\"match\":\"m1\",\"ids\":[7,9],\"vips\":[167772163,167772169]}" );
	UnsignedByte ticket[16];
	const char *mine = "{\"t\":\"match.hold\",\"match\":\"m1\",\"player\":7,\"vip\":167772163,\"seconds\":240,\"ticket\":\"ffeeddccbbaa99887766554433221100\"}";
	rig.feed( "{\"t\":\"match.hold\",\"match\":\"m1\",\"player\":9,\"vip\":167772169,\"seconds\":240,\"ticket\":\"ffeeddccbbaa99887766554433221100\"}" );
	rig.feed( "{\"t\":\"match.hold\",\"match\":\"old\",\"player\":7,\"vip\":167772163,\"seconds\":240,\"ticket\":\"ffeeddccbbaa99887766554433221100\"}" );
	rig.feed( "{\"t\":\"match.resume\",\"match\":\"m1\",\"player\":7,\"vip\":167772163}" );
	CHECK( !OnlineTakeRelayTicket( ticket ) );
	rig.feed( mine );
	CHECK( OnlineTakeRelayTicket( ticket ) );
	CHECK_EQ( (int)ticket[0], 0xff );
	CHECK_EQ( (int)ticket[15], 0x00 );
	CHECK( !OnlineTakeRelayTicket( ticket ) );
	rig.feed( mine );
	rig.feed( "{\"t\":\"game.start\",\"match\":\"m2\"}" );
	CHECK( !OnlineTakeRelayTicket( ticket ) );
}

/* match.rejoinable gives the hub its card; Rejoin sends match.rejoin for it, and match.rejoin.ok comes out
	 once as a RejoinInfo and makes the match ours.  A refusal that the match is over takes the card with it,
	 one about the build leaves it; a result for the match takes it too, and a sign-out forgets it. */
TEST(online_rejoin_card_and_answer)
{
	AdapterRig rig;
	rig.login();
	OnlineRejoinable offer;
	RejoinInfo info;
	AsciiString refusal;
	CHECK( !OnlineGetRejoinable( offer ) );
	const char *rejoinable = "{\"t\":\"match.rejoinable\",\"match\":\"m1\",\"ranked\":true,\"map\":\"\",\"elapsed\":720,\"logPeer\":9,"
		"\"players\":[{\"id\":9,\"nick\":\"Kim\",\"side\":-1,\"present\":true},{\"id\":7,\"nick\":\"tank\",\"side\":999,\"present\":false}]}";
	rig.feed( rejoinable );
	CHECK( OnlineGetRejoinable( offer ) );
	CHECK_STR( offer.match.str(), "m1" );
	CHECK( offer.ranked );
	CHECK( offer.elapsedSeconds >= 720 && offer.elapsedSeconds <= 721 );
	CHECK_EQ( offer.logPeer, 9LL );
	CHECK_EQ( offer.players.size(), (size_t)2 );
	if (offer.players.size() == 2)
	{
		CHECK_STR( offer.players[0].nick.str(), "Kim" );
		CHECK( offer.players[0].present );
		CHECK( !offer.players[1].present );
		CHECK_EQ( offer.players[1].side, -1 );		// past the templates: random
	}
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_IDLE );

	// the build: refused, the card stays
	rig.sent.clear();
	OnlineRequestRejoin();
	CHECK_EQ( rig.sent.size(), (size_t)1 );
	if (rig.sent.size() == 1)
	{
		CHECK_STR( rig.sent[0].type().c_str(), "match.rejoin" );
		CHECK_STR( rig.sent[0].getString( "match" ).c_str(), "m1" );
	}
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_WAITING );
	rig.feed( "{\"t\":\"match.refused\",\"match\":\"other\",\"code\":\"crc\"}" );		// not ours: still waiting
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_WAITING );
	rig.feed( "{\"t\":\"match.refused\",\"match\":\"m1\",\"code\":\"crc\"}" );
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_REFUSED );
	CHECK_STR( refusal.str(), "crc" );
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_IDLE );		// handed out once
	CHECK( OnlineGetRejoinable( offer ) );

	// back in: the seats, our new vip, the setup, and the match is ours
	OnlineRequestRejoin();
	rig.feed( "{\"t\":\"match.rejoin.ok\",\"match\":\"m1\",\"ranked\":true,\"ids\":[9,7],\"nicks\":[\"Kim\",\"tank\"],"
		"\"vips\":[167772169,167772300],\"me\":1,\"exe\":1,\"ini\":2,\"logPeer\":9,\"elapsed\":725,\"seed\":42,"
		"\"sides\":[-1,999],\"colors\":[-1,-1]}" );
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_READY );
	CHECK_STR( info.match.str(), "m1" );
	CHECK_EQ( info.me, 1 );
	CHECK_EQ( info.ids.size(), (size_t)2 );
	CHECK_EQ( info.vips.size(), (size_t)2 );
	if (info.vips.size() == 2)
		CHECK_EQ( info.vips[1], 167772300u );
	CHECK_EQ( info.seed, 42 );
	CHECK_EQ( info.logPeer, 9LL );
	CHECK_EQ( info.sides.size(), (size_t)2 );
	if (info.sides.size() == 2)
		CHECK_EQ( info.sides[1], -1 );
	CHECK_STR( OnlineSession::get().matchId.c_str(), "m1" );
	CHECK( !OnlineGetRejoinable( offer ) );		// the card goes: we are in it
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_IDLE );

	// over while offered: a settled refusal, or the match's result, takes the card
	rig.feed( rejoinable );
	OnlineRequestRejoin();
	rig.feed( "{\"t\":\"match.refused\",\"match\":\"m1\",\"code\":\"settled\"}" );
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_REFUSED );
	CHECK( !OnlineGetRejoinable( offer ) );
	rig.feed( rejoinable );
	rig.feed( "{\"t\":\"unrated\",\"match\":\"m1\",\"reason\":\"stalled\"}" );
	CHECK( !OnlineGetRejoinable( offer ) );
	OnlineRequestRejoin();		// nothing offered: refused here, nothing sent
	CHECK_EQ( (int)OnlineGetRejoin( info, refusal ), (int)ONLINE_REJOIN_REFUSED );
	CHECK_STR( refusal.str(), "none" );

	rig.feed( rejoinable );
	OnlineSession::get().forgetAccount();
	CHECK( !OnlineGetRejoinable( offer ) );
}

/* qm.matched's opponents carry their seat's side and colour, clamped as a seat clamps them. */
TEST(online_reveal_opponents_carry_side_and_color)
{
	AdapterRig rig;
	rig.login();
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q9\",\"seed\":5,\"mapIdx\":0,\"nicks\":[\"Tank\",\"Kim\",\"Lee\",\"Max\"],\"ids\":[7,9,11,12],"
		"\"vips\":[1,2,3,4],\"sides\":[-1,-25,999,-1],\"colors\":[-1,3,-1,-1],\"revealMs\":3000,"
		"\"opponents\":[{\"nick\":\"Kim\",\"side\":-25,\"color\":3},{\"nick\":\"Lee\",\"side\":999,\"color\":99},{\"nick\":\"Max\"}]}" );
	OnlineMatchReveal rev;
	CHECK( OnlineGetMatchReveal( rev ) );
	CHECK_EQ( rev.opponents.size(), (size_t)3 );
	if (rev.opponents.size() == 3)
	{
		// this binary has no template store or colour list: a Random pool is the one pick that comes through
		CHECK_EQ( rev.opponents[0].side, -25 );
		CHECK_EQ( rev.opponents[0].color, TheMultiplayerSettings ? 3 : -1 );
		CHECK_EQ( rev.opponents[1].side, -1 );		// past the templates: random
		CHECK_EQ( rev.opponents[1].color, -1 );
		CHECK_EQ( rev.opponents[2].side, -1 );		// an older server sends neither
		CHECK_EQ( rev.opponents[2].color, -1 );
	}
	// a ladder row has no seat
	OnlineLadderRow row;
	CHECK( row.side == -1 && row.color == -1 );
}

/* The score screen matches the AI verdict to the match this client reported: the id stays after the
	 result ends the match, and the verdict names the same match. */
TEST(online_last_ai_match_id_outlives_the_match)
{
	AdapterRig rig;
	rig.login();
	CHECK( OnlineGetLastAIMatchId().isEmpty() );
	OnlineAIMatch ai;
	OnlineRequestAIMatch( "FactionAmerica" );
	rig.feed( "{\"t\":\"ai.ready\",\"matchId\":\"0a1b2c3d4e5f6071\",\"aiRating\":12.4,\"map\":\"maps\\\\alpine assault\\\\alpine assault.map\",\"seed\":7}" );
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_READY );
	CHECK( OnlineGetLastAIMatchId().isEmpty() );		// nothing sent yet
	OnlineAIResult result;
	result.won = TRUE;
	result.playerArmy = "FactionAmerica";
	result.aiArmy = "FactionChina";
	OnlineSendAIResult( result );
	CHECK( !OnlineIsAIMatchLive() );
	CHECK_STR( OnlineGetLastAIMatchId().str(), "0a1b2c3d4e5f6071" );
	rig.feed( "{\"t\":\"ai.rated\",\"matchId\":\"0a1b2c3d4e5f6071\",\"before\":12.4,\"after\":13.1,\"games\":4,\"wins\":3}" );
	OnlineAIVerdict verdict;
	CHECK( OnlineGetAIVerdict( verdict ) );
	CHECK( verdict.match == OnlineGetLastAIMatchId() );
	OnlineSession::get().forgetAccount();
	CHECK( OnlineGetLastAIMatchId().isEmpty() );
}

/* The server's notice{kind,en,tr,until}: the hello asks for it, it is shown until an info with no words
	 replaces it or the connection goes, a maintenance with no words still shows, and each change moves the
	 serial.  The words come in the player's language, the other one when that is empty. */
TEST(online_notice_parse_and_clear)
{
	CHECK_EQ( OnlineHelloLine( 1, 2, "4.1.0" ).getInt( "notice" ), 1ll );

	AdapterRig rig;
	OnlineNotice n;
	CHECK( !OnlineGetNotice( n ) );
	const UnsignedInt first = n.serial;

	rig.feed( "{\"t\":\"notice\",\"kind\":\"info\",\"en\":\"Ladder resets Sunday\",\"tr\":\"Lig pazar s\\u0131f\\u0131rlan\\u0131yor\",\"until\":\"\"}" );
	CHECK( OnlineGetNotice( n ) );
	CHECK( !n.maintenance );
	CHECK_STR( OnlineNoticeWords( n, TEXT_LANGUAGE_ENGLISH ).str(), "Ladder resets Sunday" );
	CHECK_STR( OnlineNoticeWords( n, TEXT_LANGUAGE_TURKISH ).str(), "Lig pazar s\xC4\xB1" "f\xC4\xB1rlan\xC4\xB1yor" );
	CHECK_STR( OnlineNoticeWords( n, TEXT_LANGUAGE_GERMAN ).str(), "Ladder resets Sunday" );
	CHECK( n.serial != first );

	rig.feed( "{\"t\":\"notice\",\"kind\":\"maintenance\",\"en\":\"Deploy\",\"tr\":\"\",\"until\":\"2026-11-18T22:00:00.000Z\"}" );
	CHECK( OnlineGetNotice( n ) );
	CHECK( n.maintenance );
	CHECK_STR( n.until.str(), "2026-11-18T22:00:00.000Z" );
	CHECK_STR( OnlineNoticeWords( n, TEXT_LANGUAGE_TURKISH ).str(), "Deploy" );		// no Turkish: the English

	const UnsignedInt before = n.serial;
	rig.feed( "{\"t\":\"notice\",\"kind\":\"maintenance\",\"en\":\"\",\"tr\":\"\",\"until\":\"\"}" );
	CHECK( OnlineGetNotice( n ) );		// maintenance with no words still shows
	CHECK( n.maintenance );
	CHECK( n.serial != before );

	rig.feed( "{\"t\":\"notice\",\"kind\":\"info\",\"en\":\"\",\"tr\":\"\",\"until\":\"\"}" );
	CHECK( !OnlineGetNotice( n ) );		// an info with no words clears it

	rig.feed( "{\"t\":\"notice\",\"kind\":\"info\",\"en\":\"Hello\",\"tr\":\"Merhaba\"}" );
	CHECK( OnlineGetNotice( n ) );
	OnlineSession::get().connectionGone();		// a sign-out or a dropped line takes it down
	CHECK( !OnlineGetNotice( n ) );
}

/* until as the server writes it (Node's toISOString, or with an offset) in Unix seconds; anything else is
	 no time and is shown as the server's own text. */
TEST(online_notice_until_parses_utc)
{
	CHECK_EQ( OnlineParseUtc( "1970-01-01T00:00:00Z" ), 0ll );
	CHECK_EQ( OnlineParseUtc( "2026-11-18T22:00:00.000Z" ), 1795039200ll );
	CHECK_EQ( OnlineParseUtc( "2026-11-18T22:00Z" ), 1795039200ll );
	CHECK_EQ( OnlineParseUtc( "2026-11-19T01:00:00+03:00" ), 1795039200ll );
	CHECK_EQ( OnlineParseUtc( "2026-11-18T21:30:00-00:30" ), 1795039200ll );
	CHECK_EQ( OnlineParseUtc( "2024-02-29T12:00:00Z" ), 1709208000ll );
	CHECK_EQ( OnlineParseUtc( "2026-11-18T22:00:00" ), -1ll );		// no zone
	CHECK_EQ( OnlineParseUtc( "22:00" ), -1ll );
	CHECK_EQ( OnlineParseUtc( "2026-13-01T00:00:00Z" ), -1ll );
	CHECK_EQ( OnlineParseUtc( "2026-11-18T22:00:00Zjunk" ), -1ll );
	CHECK_EQ( OnlineParseUtc( "" ), -1ll );
	CHECK_EQ( OnlineParseUtc( NULL ), -1ll );
}

/* Maintenance: a login refused with its until (the notice's when the line has none), and a new lobby, game,
	 Quick Match or Play vs AI refused, each handed once to the screen that asked and dropped by the next such
	 request.  A Quick Match the server empties is maintenance only when the player did not leave himself.
	 Under maintenance the host waits for the server's game.start before it launches; a start refused for any
	 reason launches nothing and goes to the staging screen alone. */
TEST(online_maintenance_refusals)
{
	AdapterRig rig;
	AsciiString until, file;

	// the login
	BuddyRequest req;
	memset( &req, 0, sizeof(req) );
	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
	strcpy( req.arg.login.email, "tank@example.com" );
	strcpy( req.arg.login.password, "secret" );
	rig.buddy->addRequest( req );
	rig.feed( "{\"t\":\"auth.fail\",\"code\":\"maintenance\",\"until\":\"2026-11-18T22:00:00.000Z\"}" );
	CHECK_STR( OnlineGetLoginFailure( NULL, &file, &until ).str(), "maintenance" );
	CHECK_STR( until.str(), "2026-11-18T22:00:00.000Z" );
	rig.peerResponses();
	rig.feed( "{\"t\":\"notice\",\"kind\":\"maintenance\",\"en\":\"\",\"tr\":\"\",\"until\":\"2026-11-18T23:00:00Z\"}" );
	rig.buddy->addRequest( req );
	rig.feed( "{\"t\":\"auth.fail\",\"code\":\"maintenance\",\"until\":\"\"}" );
	OnlineGetLoginFailure( NULL, NULL, &until );
	CHECK_STR( until.str(), "2026-11-18T23:00:00Z" );		// the notice's
	CHECK( !OnlineTakeMaintenanceRefusal( until ) );		// a login is the login screen's, not a refusal
	rig.peerResponses();

	rig.login();
	const char *const errors[] = { "game.host", "game.join", "qm.join", "room.join" };
	for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i)
	{
		char line[128];
		snprintf( line, sizeof(line), "{\"t\":\"error\",\"for\":\"%s\",\"code\":\"maintenance\"}", errors[i] );
		rig.feed( line );
		until.clear();
		CHECK( OnlineTakeMaintenanceRefusal( until ) );
		CHECK_STR( until.str(), "2026-11-18T23:00:00Z" );
		CHECK( !OnlineTakeMaintenanceRefusal( until ) );		// once
	}
	rig.peerResponses();
	rig.feed( "{\"t\":\"error\",\"for\":\"game.host\",\"code\":\"full\"}" );
	CHECK( !OnlineTakeMaintenanceRefusal( until ) );

	// Play vs AI
	OnlineRequestAIMatch( "FactionAmerica" );
	rig.feed( "{\"t\":\"ai.refused\",\"reason\":\"maintenance\"}" );
	OnlineAIMatch ai;
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_REFUSED );
	CHECK_STR( ai.reason.str(), "maintenance" );
	CHECK( OnlineTakeMaintenanceRefusal( until ) );
	OnlineRequestAIMatch( "FactionAmerica" );
	rig.feed( "{\"t\":\"error\",\"for\":\"ai.start\",\"code\":\"maintenance\"}" );
	CHECK_EQ( (int)OnlineGetAIMatch( ai ), (int)ONLINE_AI_REFUSED );
	CHECK_STR( ai.reason.str(), "maintenance" );
	CHECK( OnlineTakeMaintenanceRefusal( until ) );

	// the queue: emptied by the server is maintenance, our own leave is not
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"left\",\"pool\":0}" );
	CHECK( OnlineTakeMaintenanceRefusal( until ) );
	PeerRequest stop;
	stop.peerRequestType = PeerRequest::PEERREQUEST_STOPQUICKMATCH;
	rig.peer->addRequest( stop );
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"searching\",\"pool\":1}" );		// still on its way before our left
	rig.feed( "{\"t\":\"qm.status\",\"status\":\"left\",\"pool\":0}" );
	CHECK( !OnlineTakeMaintenanceRefusal( until ) );
	rig.peerResponses();

	// a new request drops a refusal nobody took
	rig.feed( "{\"t\":\"error\",\"for\":\"game.join\",\"code\":\"maintenance\"}" );
	PeerRequest host;
	host.peerRequestType = PeerRequest::PEERREQUEST_STARTQUICKMATCH;
	rig.peer->addRequest( host );
	CHECK( !OnlineTakeMaintenanceRefusal( until ) );
	rig.peerResponses();
	rig.sent.clear();

	// the host's start waits on the server under maintenance
	rig.feed( "{\"t\":\"game.joined\",\"id\":4,\"host\":\"Tank\",\"hostId\":7,\"ids\":[7],\"nicks\":[\"Tank\"]}" );
	rig.peerResponses();
	PeerRequest start;
	start.peerRequestType = PeerRequest::PEERREQUEST_STARTGAME;
	rig.peer->addRequest( start );
	CHECK( OnlineIsHostStartWaiting() );
	rig.feed( "{\"t\":\"error\",\"for\":\"game.start\",\"code\":\"maintenance\"}" );
	CHECK( !OnlineIsHostStartWaiting() );
	AsciiString code;
	CHECK( OnlineTakeStartRefusal( code, until ) );		// the staging screen's alone
	CHECK_STR( code.str(), "maintenance" );
	CHECK_STR( until.str(), "2026-11-18T23:00:00Z" );
	CHECK( !OnlineTakeStartRefusal( code, until ) );
	CHECK( !OnlineTakeMaintenanceRefusal( until ) );		// no other screen sees it
	CHECK( rig.peerResponses().empty() );		// nothing launches
	rig.peer->addRequest( start );
	rig.feed( "{\"t\":\"error\",\"for\":\"game.start\",\"code\":\"throttled\"}" );		// any refusal lets Start go
	CHECK( !OnlineIsHostStartWaiting() );
	CHECK( OnlineTakeStartRefusal( code, until ) );
	CHECK_STR( code.str(), "throttled" );
	rig.peer->addRequest( start );
	CHECK( OnlineIsHostStartWaiting() );
	rig.feed( "{\"t\":\"game.start\",\"match\":\"m1\",\"ids\":[7],\"nicks\":[\"Tank\"],\"vips\":[167772163]}" );
	std::vector<PeerResponse> r = rig.peerResponses();
	CHECK( r.size() == 1 && r[0].peerResponseType == PeerResponse::PEERRESPONSE_GAMESTART );
	CHECK( !OnlineIsHostStartWaiting() );

	// without maintenance the host launches on its click, as before
	rig.feed( "{\"t\":\"notice\",\"kind\":\"info\",\"en\":\"\",\"tr\":\"\"}" );
	rig.feed( "{\"t\":\"game.joined\",\"id\":5,\"host\":\"Tank\",\"hostId\":7,\"ids\":[7],\"nicks\":[\"Tank\"]}" );
	rig.peerResponses();
	rig.peer->addRequest( start );
	CHECK( !OnlineIsHostStartWaiting() );
	rig.feed( "{\"t\":\"game.start\",\"match\":\"m2\",\"ids\":[7],\"nicks\":[\"Tank\"],\"vips\":[167772163]}" );
	CHECK( rig.peerResponses().empty() );
}
