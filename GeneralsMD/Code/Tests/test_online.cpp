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

#include <chrono>
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
		"{\"t\":{\"nested\":1}}",
		"{\"t\":[1,[2]]}",
		"{\"t\":[1,{}]}",
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

	/* Logged in as Tank, id 7, at 10.0.0.3, with the login's answers read and the sent lines cleared. */
	void login()
	{
		BuddyRequest req;
		memset( &req, 0, sizeof(req) );
		req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
		strcpy( req.arg.login.email, "tank@example.com" );
		strcpy( req.arg.login.password, "secret" );
		buddy->addRequest( req );
		feed( "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"ticket\":\"00112233445566778899aabbccddeeff\"}" );
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

	CHECK_EQ( rig.sent.size(), (size_t)1 );
	CHECK_STR( rig.sentType( 0 ).c_str(), "login" );
	CHECK_STR( rig.sent[0].getString( "email" ).c_str(), "tank@example.com" );
	CHECK_STR( rig.sent[0].getString( "pass" ).c_str(), "secret" );
	CHECK( rig.peer->isConnecting() );
	CHECK( !rig.peer->isConnected() );

	rig.feed( "{\"t\":\"auth.ok\",\"id\":7,\"nick\":\"Tank\",\"token\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"ticket\":\"00112233445566778899aabbccddeeff\"}" );
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
	CHECK_STR( rig.sentType( 1 ).c_str(), "stats.get" );		// our own stats, as GameSpy's login read them
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
	CHECK_EQ( rig.sent.size(), (size_t)1 );
	CHECK_STR( rig.sentType( 0 ).c_str(), "login" );
	CHECK_STR( rig.sent[0].getString( "email" ).c_str(), "tank@example.com" );

	rig.feed( "{\"t\":\"auth.fail\",\"code\":\"badlogin\"}" );
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

	// a resume with a saved token is the same login
	OnlineResume( "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" );
	CHECK_STR( rig.sentType( 1 ).c_str(), "resume" );
	CHECK_EQ( rig.sent[1].getString( "token" ).size(), (size_t)64 );
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
	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q1\",\"seed\":77,\"mapIdx\":1,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],\"vips\":[167772163,167772162],\"sides\":[-25,-1],\"colors\":[3,-1]}" );

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
	}
	CHECK_STR( OnlineSession::get().matchId.c_str(), "q1" );
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

	OnlineSendReport( report );
	CHECK( rig.sent.empty() );		// no match yet

	rig.feed( "{\"t\":\"qm.matched\",\"match\":\"q1\",\"seed\":77,\"mapIdx\":0,\"nicks\":[\"Tank\",\"Kim\"],\"ids\":[7,9],\"vips\":[1,2],\"sides\":[0,0],\"colors\":[0,1]}" );
	OnlineSendReport( report );
	OnlineSendReport( report );		// the score screen's second call
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
	OnlineSendReport( report );
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

TEST(online_register_url_from_the_welcome_and_only_a_web_page_opens)
{
	AdapterRig rig;
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094}" );
	CHECK( OnlineGetRegisterUrl().isEmpty() );		// none sent: Create Account hides
	rig.feed( "{\"t\":\"welcome\",\"config\":\"\",\"motd\":\"\",\"relayPort\":8094,\"registerUrl\":\"https://example.test/register\"}" );
	CHECK_STR( OnlineGetRegisterUrl().str(), "https://example.test/register" );
	// a hostile server's address is refused before anything is asked to open it
	CHECK( !OnlineOpenURL( "file:///C:/Windows/System32/calc.exe" ) );
	CHECK( !OnlineOpenURL( "C:\\Windows\\notepad.exe" ) );
	CHECK( !OnlineOpenURL( "" ) );
	CHECK( !OnlineOpenURL( NULL ) );
}
