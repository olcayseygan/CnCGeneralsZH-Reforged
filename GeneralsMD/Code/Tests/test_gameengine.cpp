/*
 * GameEngine boot coverage: the Phase 3 checkpoint.
 *
 * Linking gameengine.lib proves nothing on its own, so this brings up the
 * pieces the engine boots first - the memory manager, the name key generator,
 * the file system - and then drives a real INI file through INI::load() and
 * checks the values that came out the other side.
 *
 * TheLocalFileSystem normally lives in GameEngineDevice (Win32LocalFile), which
 * is not ported yet, so the test supplies its own out of LocalFile, which is
 * plain CRT _open/_read and does live in gameengine.
 */
/* crc.h pulls winsock2.h, so it has to come before anything that drags in
   windows.h (and with it winsock.h) - otherwise ws2def.h redefines sockaddr. */
#include "Common/crc.h"

#include "test_harness.h"

#include "Common/AsciiString.h"
#include "Common/CommandLine.h"
#include "Common/UnicodeString.h"
#include "Common/GameMemory.h"
#include "Common/NameKeyGenerator.h"
#include "Common/FileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/LocalFile.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/STLTypedefs.h"
#include "Common/DataChunk.h"
#include "Common/MapObject.h"
#include "Common/RandomMapGenerator.h"
#include "Common/StackDump.h"
#include "Lib/Trig.h"
#include "GameNetwork/Connection.h"
#include "GameLogic/CRCSnapshotRing.h"
#include "GameNetwork/GameDataMatch.h"
#include "GameNetwork/FrameResendPolicy.h"
#include "GameLogic/FPUControl.h"
#include "GameNetwork/StallJudgement.h"
#include "GameNetwork/KeepAliveSchedule.h"
#include "GameNetwork/CushionMetrics.h"
#include "GameNetwork/LinkSimulation.h"
#include "Common/Energy.h"
#include "Common/RandomValue.h"
#include "GameClient/ClickTolerance.h"
#include "GameClient/KeyDownInfo.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameLogic/ScenarioDrill.h"
#include "Common/ControlServer.h"
#include "GameLogic/LogicRandomValue.h"
#include "GameClient/ClientRandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/SimulationMathCrc.h"
#include "GameLogic/FPUControl.h"
#include <float.h>
#include "Common/AudioRandomValue.h"
#include "GameNetwork/CrcAgreement.h"
#include "GameLogic/GameLogic.h"
#include "Common/EarlyCommandLine.h"
#include "Common/CommandLine.h"
#include "Common/GlobalData.h"
#include "Common/EarlyOptions.h"
#include "Common/OptionsCatalog.h"
#include "Common/SubsystemInterface.h"
#include "GameClient/GameText.h"
#include "Common/GameLOD.h"
#include "Common/UserPreferences.h"
#include "GameNetwork/NetworkUtil.h"
#include "Common/Recorder.h"
#include "Common/RadarShroudCache.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTabControl.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/Image.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameNetwork/NetCommandList.h"
#include "GameNetwork/NetCommandWrapperList.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"
#include <float.h>
#include "GameClient/Water.h"
#include "GameClient/Shadow.h"
#include "GameClient/UiAnimClock.h"
#include "Common/QuotedPrintable.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameClient/GameClient.h"
#include "Common/GameEngine.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/FXList.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameLogic/AI.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/SpecialAbilityUpdate.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/View.h"
#include "GameLogic/IncomingDamage.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CrowdModel.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/PolygonTrigger.h"
#include "Common/TunnelTracker.h"
#include "Common/StateMachine.h"
#include "Common/XferCRC.h"
#include "Common/ObjectStatusTypes.h"
#include "Common/Player.h"
#include "GameLogic/Module/DefaultProductionExitUpdate.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Module/SupplyCenterProductionExitUpdate.h"
#include "WWMath/matrix3d.h"
#include "Common/JobSystem.h"
#include "Common/CriticalSection.h"
#include "GameClient/Drawable.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameClient/CommandXlat.h"
#include "Common/ActionManager.h"
#include "GameLogic/PartitionManager.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

//////////////////////////////////////////////////////////////////////////////
// Boot scaffolding
//////////////////////////////////////////////////////////////////////////////

/* LocalFile carries the abstract-base flavour of the pool glue, so it cannot be
   instantiated directly - the device layer subclasses it (Win32LocalFile) purely
   to get a pool.  Same trick here; the base does all the work. */
class TestLocalFile : public LocalFile
{
	/* The pool name has to be one MemoryInit.cpp's size table knows, or
	   createMemoryPool throws ERROR_OUT_OF_MEMORY on the -1 sizes; borrow the
	   device class' entry, since that is the class this stands in for. */
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TestLocalFile, "Win32LocalFile")
public:
	TestLocalFile() : LocalFile() {}
};

TestLocalFile::~TestLocalFile() {}

/* Just enough LocalFileSystem to let INI::load() find a file on disk. */
class TestLocalFileSystem : public LocalFileSystem
{
public:
	virtual void init( void ) {}
	virtual void reset( void ) {}
	virtual void update( void ) {}

	virtual File *openFile( const Char *filename, Int access = 0 )
	{
		TestLocalFile *file = newInstance( TestLocalFile );
		if( file->open( filename, access ) == FALSE )
		{
			file->close();
			return NULL;
		}
		file->deleteOnClose();
		return file;
	}

	virtual Bool doesFileExist( const Char *filename ) const
	{
		FILE *fp = fopen( filename, "rb" );
		if( fp == NULL )
			return FALSE;
		fclose( fp );
		return TRUE;
	}

	virtual void getFileListInDirectory( const AsciiString&, const AsciiString&,
																			 const AsciiString&, FilenameList&, Bool ) const {}
	virtual Bool getFileInfo( const AsciiString&, FileInfo* ) const { return FALSE; }
	virtual Bool createDirectory( AsciiString ) { return FALSE; }
};

/* The subsystems below are globals the engine owns; bring them up once and
   leave them up for the whole run, exactly as GameEngine::init() would. */
static Bool bootOnce( void )
{
	static Bool booted = FALSE;
	if( booted )
		return TRUE;

	initMemoryManager();

	TheNameKeyGenerator = NEW NameKeyGenerator;
	TheNameKeyGenerator->init();

	TheLocalFileSystem = NEW TestLocalFileSystem;
	TheFileSystem = NEW FileSystem;

	booted = TRUE;
	return TRUE;
}

static const char *TEST_INI = "test_gameengine_tmp.ini";

/* INI reports every failure by throwing - either an INIException carrying a
   message or a bare enum code - so run the load behind something that turns
   that back into a check failure with the message attached. */
static Bool loadIni( const char *name )
{
	INI ini;
	try
	{
		ini.load( AsciiString( name ), INI_LOAD_OVERWRITE, NULL );
		return TRUE;
	}
	catch( INIException &e )
	{
		printf( "  INI threw: %s\n", e.mFailureMessage ? e.mFailureMessage : "(no message)" );
	}
	catch( ... )
	{
		/* the enum error codes (INI_UNKNOWN_TOKEN et al) come through here */
		printf( "  INI threw an error code\n" );
	}
	return FALSE;
}

static void writeFile( const char *name, const char *text )
{
	FILE *fp = fopen( name, "wb" );
	CHECK( fp != NULL );
	fwrite( text, 1, strlen( text ), fp );
	fclose( fp );
}

//////////////////////////////////////////////////////////////////////////////
// Memory manager and the string types it hands out
//////////////////////////////////////////////////////////////////////////////

TEST(boot_memory_manager)
{
	CHECK( bootOnce() );

	AsciiString a( "Command & Conquer" );
	CHECK_EQ( a.getLength(), 17 );
	a.concat( ": Generals" );
	CHECK_STR( a.str(), "Command & Conquer: Generals" );

	/* AsciiString is refcounted and copy-on-write; a mutation through one
	   handle must not reach the other. */
	AsciiString b = a;
	b.set( "Zero Hour" );
	CHECK_STR( a.str(), "Command & Conquer: Generals" );
	CHECK_STR( b.str(), "Zero Hour" );

	UnicodeString u;
	u.translate( a );
	CHECK( wcscmp( u.str(), L"Command & Conquer: Generals" ) == 0 );
}

/* winnt.h defines BitTest as the _bittest intrinsic, which takes a LONG*, so if
   windows.h ever gets to redefine the game's macro then every bit-flag test in
   the engine either stops compiling or silently changes meaning.  BaseType.h
   pulls windef.h in first and then takes the name back; this fails to compile
   if that ever regresses, and checks the semantics while it is here. */
TEST(bittest_macro_is_the_games_not_the_intrinsics)
{
	UnsignedInt flags = 0x0A;		// bits 1 and 3

	CHECK( BitTest( flags, 0x02 ) );
	CHECK( BitTest( flags, 0x08 ) );
	CHECK( BitTest( flags, 0x04 ) == 0 );
	/* the intrinsic takes a bit *index*, the macro takes a mask - 0x0A & 0x0A */
	CHECK( BitTest( flags, 0x0A ) );
}

TEST(name_key_generator_round_trips)
{
	CHECK( bootOnce() );

	NameKeyType k1 = TheNameKeyGenerator->nameToKey( "TankGeneralUSA" );
	NameKeyType k2 = TheNameKeyGenerator->nameToKey( "TankGeneralUSA" );
	NameKeyType k3 = TheNameKeyGenerator->nameToKey( "TankGeneralChina" );

	CHECK_EQ( (Int)k1, (Int)k2 );
	CHECK_NE( (Int)k1, (Int)k3 );
	CHECK_STR( TheNameKeyGenerator->keyToName( k1 ).str(), "TankGeneralUSA" );

	/* nameToKey is case sensitive, nameToLowercaseKey is not. */
	CHECK_NE( (Int)k1, (Int)TheNameKeyGenerator->nameToKey( "tankgeneralusa" ) );
	CHECK_EQ( (Int)TheNameKeyGenerator->nameToLowercaseKey( "TankGeneralUSA" ),
						(Int)TheNameKeyGenerator->nameToLowercaseKey( "tankgeneralusa" ) );
}

/* nameToLowercaseKey hashes the lowercased text but stores the string as it was given, so a
   later exact lookup of the lowercase spelling lands in the same socket, fails the strcmp
   against the stored mixed case, and gets a second key for what is one name.  Not fixed:
   the two callers of nameToLowercaseKey - the file-exists cache and the image map - use it
   for both storing and looking up, so nothing here goes through both paths, and making the
   stored name canonical would change how many keys get allocated and therefore every key
   number after it.  See UPSTREAM-FARK #3082. */
TEST(namekey_lowercase_then_exact_lookup_makes_a_second_key_DEFECT_3082)
{
	CHECK( bootOnce() );

	NameKeyType lower = TheNameKeyGenerator->nameToLowercaseKey( "DefectMixedCase" );
	NameKeyType exact = TheNameKeyGenerator->nameToKey( "defectmixedcase" );

	CHECK_NE( (Int)lower, (Int)exact );
	CHECK_STR( TheNameKeyGenerator->keyToName( lower ).str(), "DefectMixedCase" );
}

/* AsciiString::toLower used to strcpy into a fixed 2048-byte stack buffer, so a
   longer string - a path, or a map name arriving over the network - wrote past the
   end of it.  2048 is MAX_FORMAT_BUF_LEN, private to the header, so this walks
   across it rather than naming it. */
TEST(asciistring_tolower_survives_a_string_longer_than_the_old_buffer)
{
	CHECK( bootOnce() );

	AsciiString empty;
	empty.toLower();
	CHECK( empty.isEmpty() );			/* no buffer at all is not a special case */

	AsciiString shrt( "MixedCase.INI" );
	shrt.toLower();
	CHECK_STR( shrt.str(), "mixedcase.ini" );

	for( Int len = 2040; len <= 2060; ++len )
	{
		std::string big( (size_t)len, 'A' );
		big[ (size_t)len - 1 ] = 'Z';		/* so a truncated copy is visible at the end */

		AsciiString str( big.c_str() );
		str.toLower();

		CHECK_EQ( str.getLength(), len );
		CHECK_EQ( (Int)str.str()[ 0 ], (Int)'a' );
		CHECK_EQ( (Int)str.str()[ len - 1 ], (Int)'z' );
		CHECK_EQ( (Int)str.str()[ len ], 0 );
	}

	/* a string with nothing to change comes back the same */
	AsciiString already( "already lower" );
	already.toLower();
	CHECK_STR( already.str(), "already lower" );
}

/* The port aliased hash_map onto unordered_map, which meant supplying
   rts::hash<AsciiString>.  STLport's default hashed the pointer, so equal
   strings in different buffers have to land in the same bucket now. */
TEST(asciistring_hash_is_by_content)
{
	CHECK( bootOnce() );

	char buf[ 32 ];
	strcpy( buf, "AmericaTankCrusader" );

	AsciiString fromLiteral( "AmericaTankCrusader" );
	AsciiString fromBuffer( buf );
	CHECK( fromLiteral.str() != fromBuffer.str() );	// genuinely different storage

	rts::hash<AsciiString> hasher;
	CHECK_EQ( (Int)hasher( fromLiteral ), (Int)hasher( fromBuffer ) );
	CHECK_NE( (Int)hasher( fromLiteral ), (Int)hasher( AsciiString( "AmericaTankPaladin" ) ) );

	std::hash_map< AsciiString, Int, rts::hash<AsciiString>, rts::equal_to<AsciiString> > m;
	m[ fromLiteral ] = 7;
	CHECK_EQ( (Int)m.size(), 1 );
	CHECK_EQ( m[ fromBuffer ], 7 );	// same key, not a second entry
	CHECK_EQ( (Int)m.size(), 1 );
}

//////////////////////////////////////////////////////////////////////////////
// The INI parser
//////////////////////////////////////////////////////////////////////////////

/* A WaterSet block exercises four of the field parsers (AsciiString, Real, Int,
   RGBAColorInt) and needs no subsystem beyond the ones booted above. */
TEST(ini_loads_a_waterset_block)
{
	CHECK( bootOnce() );

	writeFile( TEST_INI,
		"; a comment line\r\n"
		"WaterSet NIGHT\r\n"
		"  SkyTexture = TSNightSky.tga\r\n"
		"  WaterTexture = TWWater01.tga\r\n"
		"  WaterRepeatCount = 5\r\n"
		"  SkyTexelsPerUnit = 0.25\r\n"
		"  UScrollPerMS = 0.5\r\n"
		"  VScrollPerMS = -0.5\r\n"
		"  DiffuseColor = R:12 G:34 B:56 A:78\r\n"
		"End\r\n" );

	WaterSetting &night = WaterSettings[ TIME_OF_DAY_NIGHT ];
	night.m_waterRepeatCount = -1;

	CHECK( loadIni( TEST_INI ) );

	CHECK_STR( night.m_skyTextureFile.str(), "TSNightSky.tga" );
	CHECK_STR( night.m_waterTextureFile.str(), "TWWater01.tga" );
	CHECK_EQ( night.m_waterRepeatCount, 5 );
	CHECK_NEAR( night.m_skyTexelsPerUnit, 0.25f, 0.0001f );
	CHECK_NEAR( night.m_uScrollPerMs, 0.5f, 0.0001f );
	CHECK_NEAR( night.m_vScrollPerMs, -0.5f, 0.0001f );
	CHECK_EQ( (Int)night.m_waterDiffuseColor.red, 12 );
	CHECK_EQ( (Int)night.m_waterDiffuseColor.green, 34 );
	CHECK_EQ( (Int)night.m_waterDiffuseColor.blue, 56 );
	CHECK_EQ( (Int)night.m_waterDiffuseColor.alpha, 78 );

	remove( TEST_INI );
}

/* Blocks are keyed by name, so a second load of the same block has to land on
   the same record - that is how the game's override files work. */
TEST(ini_second_load_overwrites_the_same_record)
{
	CHECK( bootOnce() );

	writeFile( TEST_INI,
		"WaterSet MORNING\r\n"
		"  WaterRepeatCount = 3\r\n"
		"End\r\n" );

	CHECK( loadIni( TEST_INI ) );
	CHECK_EQ( WaterSettings[ TIME_OF_DAY_MORNING ].m_waterRepeatCount, 3 );

	writeFile( TEST_INI,
		"WaterSet MORNING\r\n"
		"  WaterRepeatCount = 9\r\n"
		"End\r\n" );

	CHECK( loadIni( TEST_INI ) );
	CHECK_EQ( WaterSettings[ TIME_OF_DAY_MORNING ].m_waterRepeatCount, 9 );

	remove( TEST_INI );
}

/* A missing file throws rather than returning; the loader has no other way to
   report it. */
TEST(ini_missing_file_throws)
{
	CHECK( bootOnce() );

	Bool threw = FALSE;
	INI ini;
	try
	{
		ini.load( AsciiString( "no_such_file_anywhere.ini" ), INI_LOAD_OVERWRITE, NULL );
	}
	catch( ... )
	{
		threw = TRUE;
	}
	CHECK( threw );
}

/* An unknown block name aborts the whole file - INI::load throws
   INI_UNKNOWN_TOKEN and the blocks after it never get read.  Worth pinning:
   nothing in the loader skips unknown blocks, so an INI written for a newer
   build breaks an older one outright. */
TEST(ini_unknown_block_aborts_the_file)
{
	CHECK( bootOnce() );

	WaterSettings[ TIME_OF_DAY_EVENING ].m_waterRepeatCount = -1;

	writeFile( TEST_INI,
		"ThisBlockTypeDoesNotExist SomeName\r\n"
		"  Whatever = 1\r\n"
		"End\r\n"
		"WaterSet EVENING\r\n"
		"  WaterRepeatCount = 4\r\n"
		"End\r\n" );

	CHECK( loadIni( TEST_INI ) == FALSE );
	CHECK_EQ( WaterSettings[ TIME_OF_DAY_EVENING ].m_waterRepeatCount, -1 );

	remove( TEST_INI );
}

/* Data\INI\FXListReforged.ini is the fork's own explosion light: 89 of EA's FXLists, each repeated
	 whole with one LightPulse added.  Whole, because FXListStore::parseFXListDefinition clears an
	 entry before re-reading it - a half-copied block does not add a light, it deletes an explosion.
	 GameEngine::init loads the file and folds it into the multiplayer INI CRC, so both ways of
	 getting it wrong are expensive: a malformed block throws and takes the whole startup down, and
	 a regeneration that loses the LightPulse ships a file that lights nothing and still refuses
	 every player who does not have that exact copy. */
TEST(fxlist_reforged_ini_parses_and_keeps_its_light)
{
	CHECK( bootOnce() );

	if( TheFXListStore == NULL )
		TheFXListStore = NEW FXListStore;

	/* through the game's own parser, not a lookalike */
	CHECK( loadIni( FXLIST_REFORGED_INI ) );

	/* the blocks landed in the store, under the names the game looks them up by */
	CHECK( TheFXListStore->findFXList( "ScudStormMissileDetonation" ) != NULL );
	CHECK( TheFXListStore->findFXList( "FX_BombExplosion" ) != NULL );
	CHECK( TheFXListStore->findFXList( "NoSuchFXListIsDeclaredAnywhere" ) == NULL );

	/* and every block in the file still carries the light it exists for */
	FILE *fp = fopen( FXLIST_REFORGED_INI, "rb" );
	CHECK( fp != NULL );

	Int blocks = 0, lit = 0, open = 0, hasLight = 0;
	char line[ 512 ];
	while( fgets( line, sizeof( line ), fp ) != NULL )
	{
		if( strncmp( line, "FXList ", 7 ) == 0 )
		{
			++blocks;
			open = 1;
			hasLight = 0;
		}
		else if( open && strstr( line, "LightPulse" ) != NULL )
		{
			hasLight = 1;
		}
		else if( open && strncmp( line, "End", 3 ) == 0 )
		{
			/* column 0 closes the FXList; the nuggets inside are indented */
			lit += hasLight;
			open = 0;
		}
	}
	fclose( fp );

	CHECK_EQ( blocks, 89 );
	CHECK_EQ( lit, blocks );
}

/* ReleaseCrashInfo.txt's stack trace is the only stack this port gets - there is
   no debugger on the porting machine - and it came back empty on the first real
   crash, which is worth catching here rather than in the middle of a launch. */
static char s_stackText[ 4096 ];

static void collectStackLine( const char *line )
{
	strncat( s_stackText, line, sizeof( s_stackText ) - strlen( s_stackText ) - 1 );
}

TEST(stackdump_walks_the_callers)
{
	void *frames[ 12 ];
	memset( frames, 0, sizeof( frames ) );
	::FillStackAddresses( frames, 12, 0 );
	CHECK( frames[ 0 ] != NULL );

	s_stackText[ 0 ] = 0;
	::StackDumpFromAddresses( frames, 12, collectStackLine );
	/* Needs the PDB next to the exe; without symbols this is where it shows. */
	if( strstr( s_stackText, "stackdump_walks_the_callers" ) == NULL )
		printf( "%s\n", s_stackText );
	CHECK( strstr( s_stackText, "stackdump_walks_the_callers" ) != NULL );
}

/* Two __asm blocks in headers everything includes wrote to registers that belong
   to the caller.  fast_float_trunc's "xor ebx,ebx" is what killed every run at the
   main menu: W3DTreeBuffer::doLighting keeps its saved ESP in EBX, so the epilogue's
   "mov esp,ebx" set ESP to zero and the following pop faulted.  The witnesses below
   put a sentinel in each register, run the block, and read the register back. */
static unsigned truncEbxWitness( float f )
{
	unsigned ebxOut;
	volatile float t;
	__asm mov ebx, 0x0BADF00D
	t = fast_float_trunc( f );
	__asm mov ebxOut, ebx
	(void)t;
	return ebxOut;
}

TEST(fast_float_trunc_leaves_ebx_alone)
{
	CHECK_EQ( truncEbxWitness( 3.75f ), 0x0BADF00D );
	CHECK_NEAR( fast_float_trunc( 3.75f ), 3.0f, 0.0001f );
	CHECK_NEAR( fast_float_trunc( -3.75f ), -3.0f, 0.0001f );
}

/* REAL_TO_INT and its siblings used to be fast_float2long_round(fast_float_trunc(x)) - an inline
	 assembly mantissa mask followed by an x87 fld/fistp - and are now a plain cast, which on any
	 SSE2 target is the single cvttss2si instruction that does both.  The claim is that nothing
	 changed except the speed, so the witness runs the two side by side.  The assembly helpers are
	 still in BaseType.h (FAST_REAL_TRUNC and the floor/ceil macros use them), so this is the real
	 old expression, not a re-implementation of it. */

static Int oldRealToInt( Real f )
{
	return (Int)(fast_float2long_round(fast_float_trunc(f)));
}

TEST(real_to_int_agrees_with_the_assembly_it_replaced)
{
	/* The values that decide it: either side of zero, either side of one, exact halves, exact
		 integers, and the ends of the range a 32 bit signed conversion is defined over.  Anything
		 whose magnitude reaches 2^31 is out of range for both spellings and both give INT_MIN, so
		 the sweep stops below it. */
	static const Real kInteresting[] =
	{
		0.0f, -0.0f, 0.25f, -0.25f, 0.5f, -0.5f, 0.75f, -0.75f,
		1.0f, -1.0f, 1.5f, -1.5f, 1.9999999f, -1.9999999f,
		2.5f, -2.5f, 3.75f, -3.75f, 30.0f, -30.0f,
		127.5f, -127.5f, 128.0f, -128.0f, 255.9f, -255.9f, 256.0f,
		32767.9f, -32768.0f, 65535.9f, 65536.0f,
		1.0e6f, -1.0e6f, 16777216.0f, -16777216.0f, 16777217.0f,
		1.0e9f, -1.0e9f, 2147483520.0f, -2147483520.0f,
		1.1754944e-38f, -1.1754944e-38f			// smallest normal float, either sign
	};

	for( Int i = 0; i < (Int)(sizeof(kInteresting)/sizeof(kInteresting[0])); ++i )
	{
		Real f = kInteresting[i];
		CHECK_EQ( REAL_TO_INT( f ), oldRealToInt( f ) );

		// the narrowing spellings truncate to Int first, exactly as the long-returning original did
		CHECK_EQ( (Int)REAL_TO_SHORT( f ), (Int)(Short)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_UNSIGNEDSHORT( f ), (Int)(UnsignedShort)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_BYTE( f ), (Int)(Byte)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_UNSIGNEDBYTE( f ), (Int)(UnsignedByte)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_CHAR( f ), (Int)(Char)oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_UNSIGNEDINT( f ), (Int)(UnsignedInt)oldRealToInt( f ) );
	}

	/* And a sweep, because a hand-picked list is a hand-picked list.  The generator is a plain LCG
		 over the float's bit pattern, rejected down to the range the conversion is defined over, so
		 the same 20000 values are tested on every machine that runs this. */
	UnsignedInt bits = 0x12345678;
	Int tested = 0;
	for( Int n = 0; n < 400000 && tested < 20000; ++n )
	{
		bits = bits * 1664525u + 1013904223u;

		Real f;
		memcpy( &f, &bits, sizeof(f) );

		// NaNs, infinities and everything at or past 2^31 are out of range for both spellings
		if( !(f == f) || f >= 2147483520.0f || f <= -2147483520.0f )
			continue;

		++tested;
		CHECK_EQ( REAL_TO_INT( f ), oldRealToInt( f ) );
		CHECK_EQ( (Int)REAL_TO_UNSIGNEDBYTE( f ), (Int)(UnsignedByte)oldRealToInt( f ) );
	}

	CHECK_EQ( tested, 20000 );
}

TEST(real_to_int_does_not_care_what_rounding_mode_it_is_called_in)
{
	/* The old spelling's second half was an fld/fistp, and fistp rounds by the FPU's current mode -
		 its own comment says the mode "tends to be left in unpredictable modes by various system bits
		 of code".  It was only safe because the mantissa mask ran first.  A cast has no mode at all,
		 which is one less way for two machines to disagree about the same simulation. */
	const UnsignedInt callersMode = _controlfp( 0, 0 );

	_controlfp( _RC_CHOP, _MCW_RC );
	const Int chop = REAL_TO_INT( -3.75f ) * 1000 + REAL_TO_INT( 3.75f );

	_controlfp( _RC_UP, _MCW_RC );
	CHECK_EQ( REAL_TO_INT( -3.75f ) * 1000 + REAL_TO_INT( 3.75f ), chop );

	_controlfp( _RC_DOWN, _MCW_RC );
	CHECK_EQ( REAL_TO_INT( -3.75f ) * 1000 + REAL_TO_INT( 3.75f ), chop );

	_controlfp( _RC_NEAR, _MCW_RC );
	CHECK_EQ( REAL_TO_INT( -3.75f ) * 1000 + REAL_TO_INT( 3.75f ), chop );

	// truncation toward zero, in every one of them
	CHECK_EQ( chop, -3 * 1000 + 3 );

	_controlfp( callersMode, _MCW_PC | _MCW_RC );
}

/* The length is a parameter and not a strlen() call on purpose: anything the compiler
   emits between the two blocks below is free to use these registers itself, so the
   witness only stays honest while the call is the only thing in between. */
static void crcRegisterWitness( const char *text, Int len, unsigned *ebxOut, unsigned *esiOut, unsigned *ediOut )
{
	CRC crc;
	__asm
	{
		mov ebx, 0x0BADF00D
		mov esi, 0x0BADBEEF
		mov edi, 0x0BADCAFE
	}
	crc.computeCRC( text, len );
	__asm
	{
		mov eax, ebxOut
		mov dword ptr [eax], ebx
		mov eax, esiOut
		mov dword ptr [eax], esi
		mov eax, ediOut
		mov dword ptr [eax], edi
	}
}

TEST(crc_computecrc_leaves_the_callee_saved_registers_alone)
{
	const char *text = "the quick brown fox";
	unsigned ebxOut = 0, esiOut = 0, ediOut = 0;
	crcRegisterWitness( text, (Int)strlen( text ), &ebxOut, &esiOut, &ediOut );
	CHECK_EQ( ebxOut, 0x0BADF00D );
	CHECK_EQ( esiOut, 0x0BADBEEF );
	CHECK_EQ( ediOut, 0x0BADCAFE );

	/* ...and it still computes what the C++ version in the header's comment does. */
	UnsignedInt expected = 0;
	for( const UnsignedByte *p = (const UnsignedByte *)text; *p; ++p )
	{
		UnsignedInt hibit = ( expected & 0x80000000 ) ? 1 : 0;
		expected <<= 1;
		expected += *p;
		expected += hibit;
	}

	CRC crc;
	crc.computeCRC( text, (Int)strlen( text ) );
	CHECK_EQ( crc.get(), expected );
}

/* Skirmish's Play button (and campaign start) gate on IsFirstCDPresent, which
   is TheFileSystem->areMusicFilesOnCD().  A no-CD layout (Steam) has no disc
   to find but ships the security big next to the exe; the check accepts that
   as the same proof of media instead of prompting for a CD forever. */
TEST(are_music_files_on_cd_accepts_the_local_no_cd_layout)
{
	CHECK( bootOnce() );

	/* No local big and the test's CD manager is the NULL stub: no media. */
	CHECK_EQ( TheFileSystem->areMusicFilesOnCD(), FALSE );

	writeFile( "genseczh.big", "contents are irrelevant, presence is the proof" );
	CHECK_EQ( TheFileSystem->areMusicFilesOnCD(), TRUE );
	remove( "genseczh.big" );
}

/* GameEngine.cpp: rendering is uncapped and the logic tick is paced by wall
   clock instead of by render frames, so game speed must not scale with fps.
   This is the pacing decision function; the statics live in update(). */
extern Bool GameEngine_isLogicFrameDue( Real& accumMs, Real elapsedMs, Int logicFps );

TEST(logic_tick_is_wall_clock_paced_not_render_paced)
{
	/* The catch-up cap is a duration, so the frames it buys follow the game speed: EA's three
	   frames at the retail 30fps, and six at 60 rather than the same three - a tenth of a second
	   either way. */
	const Int cap30 = GameEngine_logicCatchupMaxFrames( 30 );
	CHECK_EQ( cap30, 3 );
	CHECK_EQ( GameEngine_logicCatchupMaxFrames( 60 ), 6 );
	CHECK_EQ( GameEngine_logicCatchupMaxFrames( 15 ), 2 );
	CHECK_EQ( GameEngine_logicCatchupMaxFrames( 0 ), 1 );

	/* A 300fps renderer (3.33ms/frame) at 30fps logic: 900 render frames span
	   3 seconds, which must yield 90 logic frames, not 900. */
	Real accum = 0.0f;
	Int due = 0;
	for( Int i = 0; i < 900; ++i )
		if( GameEngine_isLogicFrameDue( accum, 1000.0f / 300.0f, 30 ) )
			++due;
	CHECK( due >= 89 && due <= 91 );

	/* A renderer slower than the logic rate owes more than one tick per pass, and
	   the debt has to be payable or the simulation just runs at render speed. A
	   100ms pass against 30Hz logic owes three frames: due on the first ask, and
	   still due on the follow-up asks that add no time of their own. */
	accum = 0.0f;
	due = 0;
	while( GameEngine_isLogicFrameDue( accum, due == 0 ? 100.0f : 0.0f, 30 ) )
		if( ++due >= cap30 )
			break;
	CHECK( due > 1 );
	CHECK( due <= cap30 );

	/* ...and the debt is capped there, however far behind the pass fell. A one
	   second stall must not queue thirty frames of catch-up: the surplus is
	   dropped, and the match honestly runs slow instead of spiralling. */
	accum = 0.0f;
	due = 0;
	while( GameEngine_isLogicFrameDue( accum, due == 0 ? 1000.0f : 0.0f, 30 ) )
		if( ++due > cap30 )
			break;
	CHECK_EQ( due, cap30 );

	/* Over a run the pacer still hands out wall-clock time and no more: 3 seconds
	   of 20fps passes is 90 logic frames, not 60 (render-paced) and not 180. */
	accum = 0.0f;
	due = 0;
	for( Int pass = 0; pass < 60; ++pass )
	{
		Int ticks = 0;
		while( GameEngine_isLogicFrameDue( accum, ticks == 0 ? 50.0f : 0.0f, 30 ) )
		{
			++due;
			if( ++ticks >= cap30 )
				break;
		}
	}
	CHECK( due >= 89 && due <= 91 );

	/* A non-positive fps never throttles (the -noFPSLimit style dev mode). */
	CHECK( GameEngine_isLogicFrameDue( accum, 0.0f, 0 ) );

	/* And the case the fixed frame count got wrong. At the 60fps game speed a 16fps render pass
	   (63ms) owes nearly four logic frames; with a cap of three the pacer threw one away every
	   pass and a match that was merely rendering slowly ran in slow motion - about 40Hz where it
	   should have been 60. Ten of those passes must still buy a full 60 frames of simulation. */
	accum = 0.0f;
	due = 0;
	const Int cap60 = GameEngine_logicCatchupMaxFrames( 60 );
	for( Int slowPass = 0; slowPass < 10; ++slowPass )
	{
		Int ticks = 0;
		while( GameEngine_isLogicFrameDue( accum, ticks == 0 ? 63.0f : 0.0f, 60 ) )
		{
			++due;
			if( ++ticks >= cap60 )
				break;
		}
	}
	CHECK( due >= 36 && due <= 38 );		// 630ms of wall clock at 60Hz, and no more
}

/* The count above cannot tell a cheap logic tick from an expensive one, and that is the whole
	 difference between the two situations the catch-up loop is ever in. Measured on Twilight Flame
	 with eight Brutal AI: three 25ms ticks back to back with no picture in between is where the
	 91ms and 113ms frames came from. So the loop is bounded by a clock as well. */
extern Bool GameEngine_mayStartAnotherCatchupTick( Int ticksSoFar, Int maxTicks, Real elapsedMsInLoop );

TEST(catchup_stops_paying_debt_when_the_logic_tick_is_what_is_slow)
{
	const Int cap30 = GameEngine_logicCatchupMaxFrames( 30 );		// 3

	// Behind because the renderer hitched: the ticks are cheap, so pay the debt in full.
	CHECK( GameEngine_mayStartAnotherCatchupTick( 1, cap30, 2.0f ) );
	CHECK( GameEngine_mayStartAnotherCatchupTick( 2, cap30, 4.0f ) );

	// Behind because the simulation is what is slow: one 25ms tick has already spent the budget,
	// so the next two do not run and the freeze is one tick long instead of three.
	CHECK( !GameEngine_mayStartAnotherCatchupTick( 1, cap30, 25.0f ) );
	CHECK( !GameEngine_mayStartAnotherCatchupTick( 2, cap30, 50.0f ) );

	// The frame count still caps it, however cheap the ticks are - the anti-spiral is unchanged.
	CHECK( !GameEngine_mayStartAnotherCatchupTick( cap30, cap30, 0.0f ) );
	CHECK( !GameEngine_mayStartAnotherCatchupTick( cap30 + 1, cap30, 0.0f ) );

	// The budget is a strict bound, so exactly-at-budget stops too.
	CHECK( !GameEngine_mayStartAnotherCatchupTick( 1, cap30, LOGIC_CATCHUP_BUDGET_MS ) );
	CHECK( GameEngine_mayStartAnotherCatchupTick( 1, cap30, LOGIC_CATCHUP_BUDGET_MS - 0.1f ) );

	/* What it is worth, in the shape the loop actually runs: a pass that owes three ticks, where
		 each tick costs 25ms. Old rule: three ticks, 75ms of frozen picture. New rule: one. */
	Real elapsed = 0.0f;
	Int ticks = 0;
	do { elapsed += 25.0f; ++ticks; }
	while( GameEngine_mayStartAnotherCatchupTick( ticks, cap30, elapsed ) );
	CHECK_EQ( 1, ticks );
	CHECK_NEAR( 25.0f, elapsed, 0.01f );

	// ...and the same pass with 2ms ticks still pays all three, because it costs 6ms to do so.
	elapsed = 0.0f;
	ticks = 0;
	do { elapsed += 2.0f; ++ticks; }
	while( GameEngine_mayStartAnotherCatchupTick( ticks, cap30, elapsed ) );
	CHECK_EQ( cap30, ticks );
	CHECK_NEAR( 6.0f, elapsed, 0.01f );
}

/* AnimateWindowManager.cpp: the same argument one layer up. The menu animations
   count fixed steps and were authored against a capped frame rate, so with the
   renderer uncapped they have to be paced off the wall clock too or a whole
   menu transition plays out in a handful of milliseconds. Unlike the logic
   pacer this one owns the clock reading, so it takes 'now' rather than an
   elapsed time - a caller can hand it timeGetTime() and nothing else.

   Declared here rather than by including AnimateWindowManager.h: that header
   does not parse outside the engine's own PreRTS.h include order. */
extern Bool GameClient_isUiAnimStepDue( UnsignedInt &lastMs, Real &accumMs,
																				UnsignedInt nowMs, Real stepsPerSec );

TEST(ui_anim_steps_at_a_fixed_rate_however_fast_the_renderer_is)
{
	/* must match UI_ANIM_STEPS_PER_SEC in GameClient/UiAnimClock.h */
	const Real UI_ANIM_STEPS_PER_SEC = 30.0f;

	/* 3 seconds of a 300fps renderer at 30 steps/sec is 90 steps, not 900. */
	UnsignedInt last = 0;
	Real accum = 0.0f;
	Int due = 0;
	UnsignedInt now = 100000;		/* an arbitrary non-zero clock origin */
	for( Int i = 0; i < 900; ++i )
	{
		now += 3;		/* timeGetTime has 1ms resolution, so ~333fps */
		if( GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC ) )
			++due;
	}
	CHECK( due >= 79 && due <= 81 );		/* 2700ms at 33.3ms/step */

	/* The very first call must not fire: with lastMs seeded to now there is no
	   elapsed time yet, so a freshly started animation holds its first frame
	   for a full step instead of jumping two frames on the frame it starts. */
	last = 0;
	accum = 0.0f;
	CHECK_EQ( GameClient_isUiAnimStepDue( last, accum, 500000, UI_ANIM_STEPS_PER_SEC ), FALSE );

	/* A renderer slower than the step rate steps once per call and never banks a
	   burst - a level load must not fast-forward the transition it returns to. */
	last = 0;
	accum = 0.0f;
	now = 200000;
	GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC );	/* seed */
	for( Int i = 0; i < 20; ++i )
	{
		now += 5000;		/* five seconds of stall = 150 steps, if it banked them */
		CHECK( GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC ) );
	}

	/* And the rate really is the rate: half the step period never fires twice. */
	last = 0;
	accum = 0.0f;
	now = 300000;
	GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC );
	due = 0;
	for( Int i = 0; i < 60; ++i )
	{
		now += 16;		/* ~60fps against a 33.3ms step */
		if( GameClient_isUiAnimStepDue( last, accum, now, UI_ANIM_STEPS_PER_SEC ) )
			++due;
	}
	CHECK( due >= 28 && due <= 30 );		/* 960ms of it */
}

/* PhysicsUpdate.cpp: the forward speed a locomotor steers on is the projection
   of the velocity onto the facing - a plain dot product. It used to be
   sqrt((vx*dx)^2 + (vy*dy)^2), which is exact on the axes but reads only
   sqrt(cos^4 + sin^4) = 0.707 of the true speed at 45 degrees. Since the
   locomotors close speedDelta = goalSpeed - actualSpeed by accelerating, and
   nothing else caps velocity, an under-read made diagonal units settle at
   goalSpeed/0.707 - up to 1.41x their max speed.

   The witness is heading independence: drive at a known speed along the
   facing and the reported forward speed must be that speed at every heading. */
TEST(physics_forward_speed_is_the_projection_not_a_per_axis_norm)
{
	const Real speed = 40.0f;

	for( Int deg = 0; deg <= 360; deg += 5 )
	{
		Real a = (Real)(deg * PI / 180.0);
		Coord3D dir; dir.x = (Real)cos(a); dir.y = (Real)sin(a); dir.z = 0.0f;
		Coord3D vel; vel.x = dir.x * speed; vel.y = dir.y * speed; vel.z = 0.0f;

		/* moving along the facing: full speed, whatever the heading. The old
		   code returned 0.707*speed here at 45/135/225/315 degrees. */
		CHECK_NEAR( PhysicsBehavior::calcForwardSpeed( vel, dir ), speed, 0.01f );

		/* moving backwards along the facing: the same speed, negated. */
		Coord3D back; back.x = -vel.x; back.y = -vel.y; back.z = 0.0f;
		CHECK_NEAR( PhysicsBehavior::calcForwardSpeed( back, dir ), -speed, 0.01f );

		/* moving straight across the facing contributes nothing. The old code
		   returned a positive magnitude here for every off-axis heading. */
		Coord3D side; side.x = -dir.y * speed; side.y = dir.x * speed; side.z = 0.0f;
		CHECK_NEAR( PhysicsBehavior::calcForwardSpeed( side, dir ), 0.0f, 0.01f );
	}

	/* the 3d form is the same dot product, z included. */
	Coord3D up; up.x = 0.0f; up.y = 0.0f; up.z = 1.0f;
	Coord3D climb; climb.x = 0.0f; climb.y = 0.0f; climb.z = 12.0f;
	CHECK_NEAR( PhysicsBehavior::calcForwardSpeed( climb, up ), 12.0f, 0.01f );
}

/* CommandXlat.cpp: only the attack key arms force fire.  Holding ctrl did it too, and ctrl is the
   "one shared pace" modifier, so a ctrl click on the ground shelled the dirt instead of moving. */
extern Bool CommandXlat_isForceAttackTargeting( Bool forceAttackArmed, Bool attackMoveArmed );

TEST(force_fire_is_the_attack_key_and_nothing_else)
{
	/* A armed: the next click is a force fire. */
	CHECK(  CommandXlat_isForceAttackTargeting( true,  false ) );

	/* attack move armed on top of it: the click is the attack move. */
	CHECK( !CommandXlat_isForceAttackTargeting( true,  true ) );

	/* neither key: nothing force fires, which is what ctrl held down now gets. */
	CHECK( !CommandXlat_isForceAttackTargeting( false, false ) );
	CHECK( !CommandXlat_isForceAttackTargeting( false, true ) );
}

/* InGameUI.cpp: Legacy is the game as shipped, where holding ctrl is force fire.  Modern keeps ctrl for
   the shared pace and force fires on the attack key alone. */
extern Bool InGameUI_isForceFireOn( Bool forceAttackArmed, Bool ctrlHeld, Bool legacyInput );

TEST(legacy_ctrl_force_fires_and_modern_ctrl_does_not)
{
	CHECK(  InGameUI_isForceFireOn( false, true,  true  ) );
	CHECK( !InGameUI_isForceFireOn( false, true,  false ) );

	/* the attack key arms it under either scheme */
	CHECK(  InGameUI_isForceFireOn( true,  false, false ) );
	CHECK(  InGameUI_isForceFireOn( true,  false, true  ) );

	CHECK( !InGameUI_isForceFireOn( false, false, true  ) );
}

/* Player.cpp: the lobby's unit limit is 840 units shared out by the players who are not watching. */
TEST(unit_limit_shares_840_between_the_players)
{
	CHECK_EQ( UnitLimitPerPlayer( 2 ), 420 );
	CHECK_EQ( UnitLimitPerPlayer( 3 ), 280 );
	CHECK_EQ( UnitLimitPerPlayer( 8 ), 105 );

	/* a lobby with nobody left in it is not a division by zero */
	CHECK_EQ( UnitLimitPerPlayer( 0 ), 840 );
	CHECK_EQ( UnitLimitPerPlayer( 1 ), 840 );
}

/* Player.cpp: a build is refused when it and its payload would go past the share, not when the
   share is already full. */
TEST(unit_limit_charges_a_transport_for_its_payload)
{
	/* one short of 105: a tank fits, a pair of Red Guards and a Troop Crawler with its eight do not */
	CHECK( !UnitCapRefuses( 104, 1, 105 ) );
	CHECK(  UnitCapRefuses( 104, 2, 105 ) );
	CHECK( !UnitCapRefuses( 103, 2, 105 ) );
	CHECK(  UnitCapRefuses( 104, 9, 105 ) );
	CHECK( !UnitCapRefuses( 96, 9, 105 ) );
	CHECK(  UnitCapRefuses( 105, 1, 105 ) );

	/* no limit in the lobby refuses nothing */
	CHECK( !UnitCapRefuses( 5000, 9, 0 ) );
}

/* CommandXlat.cpp: a right drag spreads the selection along the line drawn, but only when there is
   a selection to spread and no GUI command already waiting for the click. */
extern Bool Command_formationDragArmed( Bool setting, Bool haveMovableSelection,
																				Bool guiCommandPending );

TEST(formation_drag_takes_the_right_button_only_when_it_is_asked_for)
{
	/* on by default: a right drag with your own units selected draws the line. */
	CHECK(  Command_formationDragArmed( true, true, false ) );

	/* off is off. */
	CHECK( !Command_formationDragArmed( false, true, false ) );

	/* nobody to spread, or a GUI command already waiting for this click. */
	CHECK( !Command_formationDragArmed( true, false, false ) );
	CHECK( !Command_formationDragArmed( true, true,  true ) );
}

/* CommandXlat.cpp: the line carries whichever mode the player armed before drawing it. */
extern GameMessage::Type Command_formationMessage( Bool attackMoveArmed, Bool forceAttackArmed,
																									 Bool guardArmed );

TEST(a_formation_line_carries_the_armed_order)
{
	/* nothing armed: the line is a move. */
	CHECK( Command_formationMessage( false, false, false ) == GameMessage::MSG_DO_FORMATION_MOVETO );

	/* one mode at a time is all the UI can arm, and each names its own message. */
	CHECK( Command_formationMessage( true,  false, false ) == GameMessage::MSG_DO_FORMATION_ATTACKMOVETO );
	CHECK( Command_formationMessage( false, true,  false ) == GameMessage::MSG_DO_FORMATION_FORCEATTACK );
	CHECK( Command_formationMessage( false, false, true  ) == GameMessage::MSG_DO_FORMATION_GUARD );
}

/* CommandXlat.cpp: each smoke signal key names its own kind, and nothing else names one - the
   receiving side indexes a table with that value. */
TEST(each_signal_key_drops_its_own_smoke)
{
	CHECK( Command_signalKindForMeta( GameMessage::MSG_META_SIGNAL_ATTACK ) == SIGNAL_ATTACK );
	CHECK( Command_signalKindForMeta( GameMessage::MSG_META_SIGNAL_DEFEND ) == SIGNAL_DEFEND );
	CHECK( Command_signalKindForMeta( GameMessage::MSG_META_SIGNAL_ATTENTION ) == SIGNAL_ATTENTION );
	CHECK( Command_signalKindForMeta( GameMessage::MSG_META_PLACE_BEACON ) == SIGNAL_KIND_COUNT );
}

/* GameLogicDispatch.cpp: one signal a second per player. */
extern Bool Signal_isThrottled( UnsignedInt now, UnsignedInt lastSignalFrame );

TEST(a_signal_waits_a_second_after_the_last_one)
{
	CHECK( Signal_isThrottled( 1000, 1000 ) );
	CHECK( Signal_isThrottled( 1029, 1000 ) );
	CHECK( !Signal_isThrottled( 1030, 1000 ) );

	/* a frame left over from a longer earlier match does not silence the new one */
	CHECK( !Signal_isThrottled( 40, 50000 ) );
}

/* ParticleSys.cpp: tinting a white key by a colour gives that colour.  RGBColor::setFromInt already
   scales to 0..1 and the tint divided by 255 again, so every tinted system came out black. */
#include "GameClient/ParticleSys.h"

TEST(a_tinted_particle_key_takes_the_tint_colour)
{
	ParticleSystemInfo info;
	info.m_colorKey[ 1 ].color.red = 1.0f;
	info.m_colorKey[ 1 ].color.green = 1.0f;
	info.m_colorKey[ 1 ].color.blue = 1.0f;

	info.tintAllColors( 0xFF8000 );

	CHECK_NEAR( info.m_colorKey[ 1 ].color.red, 1.0f, 0.001f );
	CHECK_NEAR( info.m_colorKey[ 1 ].color.green, 128.0f / 255.0f, 0.001f );
	CHECK_NEAR( info.m_colorKey[ 1 ].color.blue, 0.0f, 0.001f );
}

/* DrawnPath.cpp: the drawn curve is measured by arc length, not by segment, so the stations divide
   the whole line however uneven the hand that drew it was. */
#include "Common/DrawnPath.h"

static Coord3D drawnPathPoint( Real x, Real y )
{
	Coord3D p;
	p.x = x; p.y = y; p.z = 0.0f;
	return p;
}

TEST(drawn_path_measures_by_arc_length_not_by_segment)
{
	/* an L: 100 along x, then 100 along y, with the corner deliberately not in the middle of the
	   point list - the second segment is one point, the first is two. */
	std::vector<Coord3D> path;
	path.push_back( drawnPathPoint(   0.0f,   0.0f ) );
	path.push_back( drawnPathPoint(  40.0f,   0.0f ) );
	path.push_back( drawnPathPoint( 100.0f,   0.0f ) );
	path.push_back( drawnPathPoint( 100.0f, 100.0f ) );

	std::vector<Real> arc;
	buildPathArcLengths( path, arc );

	CHECK_EQ( 4, (int)arc.size() );
	CHECK_NEAR( 0.0f, arc[0], 0.001f );
	CHECK_NEAR( 40.0f, arc[1], 0.001f );
	CHECK_NEAR( 200.0f, arc[3], 0.001f );

	/* halfway along the whole curve is the corner, not the middle of either segment. */
	Coord3D at;
	pointAlongPath( path, arc, 100.0f, &at );
	CHECK_NEAR( 100.0f, at.x, 0.001f );
	CHECK_NEAR( 0.0f, at.y, 0.001f );

	/* three quarters along is halfway up the second leg. */
	pointAlongPath( path, arc, 150.0f, &at );
	CHECK_NEAR( 100.0f, at.x, 0.001f );
	CHECK_NEAR( 50.0f, at.y, 0.001f );

	/* the ends stay put. */
	pointAlongPath( path, arc, 0.0f, &at );
	CHECK_NEAR( 0.0f, at.x, 0.001f );
	pointAlongPath( path, arc, 200.0f, &at );
	CHECK_NEAR( 100.0f, at.x, 0.001f );
	CHECK_NEAR( 100.0f, at.y, 0.001f );
}

TEST(drawn_path_orders_units_by_where_they_already_stand)
{
	std::vector<Coord3D> path;
	path.push_back( drawnPathPoint(   0.0f,   0.0f ) );
	path.push_back( drawnPathPoint( 100.0f,   0.0f ) );
	path.push_back( drawnPathPoint( 100.0f, 100.0f ) );

	std::vector<Real> arc;
	buildPathArcLengths( path, arc );

	/* a unit sitting off to one side belongs to the nearest point on the curve. */
	CHECK_NEAR( 30.0f, distanceAlongPath( path, arc, 30.0f, -25.0f ), 0.001f );

	/* one past the corner is measured round it, not across it. */
	CHECK_NEAR( 160.0f, distanceAlongPath( path, arc, 140.0f, 60.0f ), 0.001f );

	/* before the start clamps to the start, past the end to the end. */
	CHECK_NEAR( 0.0f, distanceAlongPath( path, arc, -50.0f, -50.0f ), 0.001f );
	CHECK_NEAR( 200.0f, distanceAlongPath( path, arc, 100.0f, 400.0f ), 0.001f );
}

/* AssaultTransportAIUpdate.cpp: the troop crawler deploys its passengers at a target and used to
   leave them walking behind it for the rest of the attack move once that target died - and on a
   plain attack order it re-boarded them the instant the target died, once per dead enemy.  Both
   orders now board on this one rule. */
extern Bool AssaultTransport_shouldRetrieveMembers( Bool membersOutside, Bool membersFighting, Bool areaClear );
extern Bool AssaultTransport_waitingForBoarding( Bool membersOutside, UnsignedInt framesRemaining );
extern Bool AssaultTransport_nothingLeftToDeploy( Bool isAttacking, Bool anyoneInside, Bool membersAlive );

TEST(troop_crawler_drives_on_once_it_is_empty)
{
	/* attacking with an empty hold and no squad left alive: the crawler's own weapon only unloads
	   troops, so it is standing there doing nothing - resume the order instead. */
	CHECK( AssaultTransport_nothingLeftToDeploy( true, false, false ) );

	/* the squad it dropped is still out there fighting: hold, or they would be left behind. */
	CHECK( !AssaultTransport_nothingLeftToDeploy( true, false, true ) );

	/* somebody still aboard: stay put, he is about to be deployed. */
	CHECK( !AssaultTransport_nothingLeftToDeploy( true, true, false ) );

	/* not attacking at all: whatever it is doing is not our business. */
	CHECK( !AssaultTransport_nothingLeftToDeploy( false, false, false ) );
}

TEST(troop_crawler_picks_its_troops_back_up_only_when_the_fight_is_over)
{
	/* deployed, nobody shooting, nothing hostile in range: climb back in. */
	CHECK( AssaultTransport_shouldRetrieveMembers( true, false, true ) );

	/* a member still has a victim - leave them to it. */
	CHECK( !AssaultTransport_shouldRetrieveMembers( true, true, true ) );

	/* nobody has a target yet, but something hostile is still in range: wait for them to engage. */
	CHECK( !AssaultTransport_shouldRetrieveMembers( true, false, false ) );

	/* everybody is already aboard: nothing to order. */
	CHECK( !AssaultTransport_shouldRetrieveMembers( false, false, true ) );
}

TEST(troop_crawler_waits_for_boarding_but_not_forever)
{
	/* troops outside and time on the clock: hold position while they board. */
	CHECK( AssaultTransport_waitingForBoarding( true, 300 ) );
	CHECK( AssaultTransport_waitingForBoarding( true, 1 ) );

	/* the wait ran out - roll on rather than stalling on a member that cannot get back. */
	CHECK( !AssaultTransport_waitingForBoarding( true, 0 ) );

	/* all aboard: continue the attack move immediately. */
	CHECK( !AssaultTransport_waitingForBoarding( false, 300 ) );
}

/* AssaultTransportAIUpdate.cpp: the ten second wait was re-armed the frame it hit zero, so one man
   who could not path back aboard parked the crawler on the spot and it never resumed the attack
   move.  The last frame of the wait now cuts the stragglers loose instead. */
extern Bool AssaultTransport_boardingWaitJustExpired( Bool membersOutside, UnsignedInt framesRemaining );

TEST(troop_crawler_gives_up_on_a_man_who_cannot_board)
{
	/* last frame of the wait, somebody still on the ground: let him go and drive on. */
	CHECK( AssaultTransport_boardingWaitJustExpired( true, 1 ) );

	/* still time on the clock: keep waiting. */
	CHECK( !AssaultTransport_boardingWaitJustExpired( true, 2 ) );
	CHECK( !AssaultTransport_boardingWaitJustExpired( true, 300 ) );

	/* no wait running - never let go of a squad we are not waiting on. */
	CHECK( !AssaultTransport_boardingWaitJustExpired( true, 0 ) );

	/* everybody is aboard: nobody to release. */
	CHECK( !AssaultTransport_boardingWaitJustExpired( false, 1 ) );
}

/* AssaultTransportAIUpdate.cpp: the "is the fight over?" scan used the raw INI range (50), which is
   shorter than the rifles the men are carrying - they killed the one enemy they were deployed for
   and boarded again with the next one standing in range, unshot. */
extern Real AssaultTransport_clearScanRange( Real iniRange, Real weaponRange );

TEST(troop_crawler_scans_at_least_as_far_as_its_troops_shoot)
{
	/* a rifle outranges the INI knob: look as far as the man can shoot. */
	CHECK_NEAR( AssaultTransport_clearScanRange( 50.0f, 150.0f ), 150.0f, 0.001f );

	/* the knob is the wider of the two (a modded value): honour it. */
	CHECK_NEAR( AssaultTransport_clearScanRange( 200.0f, 150.0f ), 200.0f, 0.001f );

	/* no weapon at all - getLargestWeaponRange returns -1; never scan a negative radius. */
	CHECK_NEAR( AssaultTransport_clearScanRange( 50.0f, -1.0f ), 50.0f, 0.001f );
}

/* AIStates.cpp: an attack move leashes a human player's ground unit so a target
   that backs away cannot walk it off the order.  Aircraft must be exempt: they
   acquire out to weapon range, which is far beyond the leash, so they broke it on
   the way in every time and disengaged before ever being in range to fire - which
   also meant they never spent the ammunition that sends them home to reload. */
extern Bool AIAttackMove_leashBroken( Bool isHumanPlayer, Bool isAirborne, Real dx, Real dy, Int leashCells );

TEST(attack_move_leashes_ground_units_but_never_aircraft)
{
	/* PATHFIND_CELL_SIZE_F is 10 and ATTACK_MOVE_LEASH_CELLS is 12, so the leash
	   is 120 world units for a human player's ground unit. */
	const Real cell = 10.0f;
	const Int LEASH = 12;		/* ATTACK_MOVE_LEASH_CELLS, which is protected on the state */

	/* a ground unit: inside the leash it keeps fighting, past it the fight is off. */
	CHECK( !AIAttackMove_leashBroken( true, false, 0.0f,       0.0f,       LEASH ) );
	CHECK( !AIAttackMove_leashBroken( true, false, 11.0f*cell, 0.0f,       LEASH ) );
	CHECK(  AIAttackMove_leashBroken( true, false, 13.0f*cell, 0.0f,       LEASH ) );
	CHECK(  AIAttackMove_leashBroken( true, false, 0.0f,       13.0f*cell, LEASH ) );
	/* measured as a radius, not per axis */
	CHECK(  AIAttackMove_leashBroken( true, false, 10.0f*cell, 10.0f*cell, LEASH ) );

	/* an aircraft is never leashed, however far the fight has taken it - this is
	   the case that used to fire on the way in to every single target. */
	CHECK( !AIAttackMove_leashBroken( true, true, 13.0f*cell,  0.0f, LEASH ) );
	CHECK( !AIAttackMove_leashBroken( true, true, 100.0f*cell, 0.0f, LEASH ) );
	CHECK( !AIAttackMove_leashBroken( true, true, 500.0f*cell, 500.0f*cell, LEASH ) );

	/* the computer and scripts keep retail chase behaviour: never leashed here,
	   airborne or not. */
	CHECK( !AIAttackMove_leashBroken( false, false, 500.0f*cell, 0.0f, LEASH ) );
	CHECK( !AIAttackMove_leashBroken( false, true,  500.0f*cell, 0.0f, LEASH ) );
}

/* AIStates.cpp: telling a firing pass from a failed approach when the attack move
   disengages.  A fight the unit spent rounds on was real however short; only one it
   never fired in is charged the re-acquire delay.  The ammunition count is the state
   that was left uninitialized and unsaved - see the zero cases below. */
extern Bool AIAttackMove_engageWasADud( Bool victimStillAlive, Int ammoAtEngage, Int ammoNow,
																				UnsignedInt engageStartFrame, UnsignedInt now, Int dudFrames );

TEST(attack_move_charges_the_reacquire_delay_only_for_a_fight_that_never_happened)
{
	const Int DUD = 15;		/* ATTACK_MOVE_DUD_ENGAGE_FRAMES, protected on the state */

	/* stood next to it for a moment, fired nothing, it is still alive: a dud. */
	CHECK( AIAttackMove_engageWasADud( true, 8, 8, 1000, 1000, DUD ) );
	CHECK( AIAttackMove_engageWasADud( true, 8, 8, 1000, 1014, DUD ) );

	/* the same non-fight, but long enough that it was a real attempt, not a bounce. */
	CHECK( !AIAttackMove_engageWasADud( true, 8, 8, 1000, 1015, DUD ) );
	CHECK( !AIAttackMove_engageWasADud( true, 8, 8, 1000, 9999, DUD ) );

	/* it died. whatever we did worked, so go straight back to scanning. */
	CHECK( !AIAttackMove_engageWasADud( false, 8, 8, 1000, 1000, DUD ) );

	/* one round spent inside two frames is the aircraft firing pass: a real fight,
	   and charging it a re-acquire delay is exactly backwards - the load is what
	   sends the aircraft home, so it should be spent as fast as it can be. */
	CHECK( !AIAttackMove_engageWasADud( true, 8, 7, 1000, 1002, DUD ) );
	CHECK( !AIAttackMove_engageWasADud( true, 8, 0, 1000, 1001, DUD ) );

	/* an ammunition count of zero at engage time can never read as "we fired": the
	   count now is a sum of remaining rounds and is never negative.  That is what a
	   save written before the count existed, and the constructor, both load as - so
	   the answer there is the conservative "dud", not whatever was in the block. */
	CHECK( AIAttackMove_engageWasADud( true, 0, 0, 1000, 1000, DUD ) );
	CHECK( AIAttackMove_engageWasADud( true, 0, 12, 1000, 1000, DUD ) );
	CHECK( !AIAttackMove_engageWasADud( true, 0, 12, 1000, 1015, DUD ) );

	/* a unit with no ammunition at all on either side of a fight it could not start. */
	CHECK( AIAttackMove_engageWasADud( true, 0, 0, 0, 0, DUD ) );

	/* both frames are unsigned: an engage stamped after 'now' must not wrap the
	   subtraction into a small number and read as a fresh dud. */
	CHECK( !AIAttackMove_engageWasADud( true, 8, 8, 1000, 999, DUD ) );
	CHECK( !AIAttackMove_engageWasADud( true, 8, 8, 0xffffffff, 0, DUD ) );
}

/* AIStates.cpp: on attack move, whoever is shooting us wins the target selection
   over whatever the scan would otherwise have picked. */
extern Bool AIAttackMove_shouldRetaliate( UnsignedInt lastDamageFrame, UnsignedInt now, Int windowFrames,
																					Bool alreadyFightingTheAttacker, Bool canAttackTheAttacker );

TEST(attack_move_turns_on_whoever_is_shooting_it)
{
	const Int WINDOW = 30;		/* ATTACK_MOVE_RETALIATE_FRAMES, protected on the state */

	/* hit this second, can shoot back, busy with somebody else: turn on the shooter. */
	CHECK( AIAttackMove_shouldRetaliate( 1000, 1000, WINDOW, false, true ) );
	CHECK( AIAttackMove_shouldRetaliate( 1000, 1029, WINDOW, false, true ) );
	CHECK( AIAttackMove_shouldRetaliate( 1000, 1030, WINDOW, false, true ) );

	/* an old grudge is not a fight. */
	CHECK( !AIAttackMove_shouldRetaliate( 1000, 1031, WINDOW, false, true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 1000, 9999, WINDOW, false, true ) );

	/* already fighting them, or cannot touch them: leave the current target alone,
	   otherwise the fight restarts every scan and nothing is ever shot. */
	CHECK( !AIAttackMove_shouldRetaliate( 1000, 1000, WINDOW, true,  true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 1000, 1000, WINDOW, false, false ) );

	/* the two timestamps that mean "never hit". Both are unsigned, and 0xffffffff
	   plus the window wraps to a small number - read naively, a unit that has never
	   been damaged would retaliate against whatever object id happened to be there. */
	CHECK( !AIAttackMove_shouldRetaliate( 0, 10, WINDOW, false, true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 0, 0, WINDOW, false, true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 0xffffffff, 10, WINDOW, false, true ) );
	CHECK( !AIAttackMove_shouldRetaliate( 0xffffffff, 0xfffffff0, WINDOW, false, true ) );
}

/* AIStates.cpp: joinTeam clears a reinforcement's own waypoint before copying the state of a unit
	already in its team.  If that state follows a waypoint path as a group, the reinforcement has to
	pick up the team's current waypoint before the state reads its location. */
extern const Waypoint *AIFollowWaypointPath_groupInitialWaypoint( const Waypoint *goalWaypoint,
																								 const Waypoint *teamWaypoint );

TEST(a_reinforcement_joins_the_waypoint_path_its_team_is_following)
{
	const Waypoint *ownWaypoint = (const Waypoint *)0x100;
	const Waypoint *teamWaypoint = (const Waypoint *)0x200;

	/* An explicit order stays authoritative; joining must not rewind the unit to the team's point. */
	CHECK_EQ( AIFollowWaypointPath_groupInitialWaypoint( ownWaypoint, teamWaypoint ), ownWaypoint );

	/* This is the crash case: joinTeam left no local goal, but the active team has one. */
	CHECK_EQ( AIFollowWaypointPath_groupInitialWaypoint( NULL, teamWaypoint ), teamWaypoint );

	/* A team whose path has ended has nothing safe to dereference; the state must fail instead. */
	CHECK( AIFollowWaypointPath_groupInitialWaypoint( NULL, NULL ) == NULL );
}

/* BitFlags used to be templated on the bit count alone, and its name table is a static
   member of the instantiation - so two flag sets with the same count were the SAME type
   and shared one s_bitNameList.  The linker folded the two definitions and kept whichever
   it liked.  From the outside that looked like one INI parser suddenly refusing to
   recognise any of its own keywords, in a file with no connection to the change that
   caused it: adding a single KindOf took KINDOF_COUNT to 117, which is what
   MODELCONDITION_COUNT already was, and airforcegeneral.ini stopped parsing on a
   ConditionState line.

   Each typedef now carries its own tag, so equal counts are harmless.  This test keeps
   watch anyway - a tag that gets copied and pasted rather than incremented puts the trap
   straight back, and it is cheaper to read it here than to find it there. */
#include "Common/KindOf.h"
#include "Common/ModelState.h"
#include "Common/DisabledTypes.h"
#include "Common/ObjectStatusTypes.h"
#include "Common/SpecialPowerMaskType.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Damage.h"
#include "GameLogic/WeaponSetFlags.h"

TEST(bitflags_types_keep_their_own_name_tables)
{
	/* KINDOF_COUNT and MODELCONDITION_COUNT are in fact equal - both 117 - which is exactly
	   the case that used to break.  The tags are what keeps them apart now. */
	struct Named { const char *name; const char **names; const char *firstExpected; };
	const Named tables[] = {
		{ "KINDOF",					KindOfMaskType::getBitNames(),				"OBSTACLE" },
		{ "MODELCONDITION",	ModelConditionFlags::getBitNames(),	NULL },
		{ "DISABLED",				DisabledMaskType::getBitNames(),		NULL },
		{ "OBJECT_STATUS",	ObjectStatusMaskType::getBitNames(),NULL },
		{ "SPECIALPOWER",		SpecialPowerMaskType::getBitNames(),NULL },
		{ "ARMORSET",				ArmorSetFlags::getBitNames(),				NULL },
		{ "DAMAGE",					DamageTypeFlags::getBitNames(),			NULL },
		{ "WEAPONSET",			WeaponSetFlags::getBitNames(),			NULL },
	};
	const Int n = sizeof(tables) / sizeof(tables[0]);

	/* every table is somebody else's table only if two flag sets ended up the same type */
	for( Int i = 0; i < n; ++i )
	{
		CHECK( tables[i].names != NULL );
		CHECK( tables[i].names[0] != NULL );
		for( Int j = i + 1; j < n; ++j )
		{
			if( tables[i].names == tables[j].names )
			{
				printf( "  BitFlags name table shared between %s and %s - they are the same type\n",
								tables[i].name, tables[j].name );
			}
			CHECK( tables[i].names != tables[j].names );
		}
	}

	/* and the KindOf table is the KindOf table, not whatever the linker felt like */
	CHECK_STR( KindOfMaskType::getBitNames()[0], "OBSTACLE" );
	CHECK_EQ( KindOfMaskType::getSingleBitFromName("STRUCTURE"), (Int)KINDOF_STRUCTURE );
	CHECK_EQ( ModelConditionFlags::getSingleBitFromName("RUBBLE"), (Int)MODELCONDITION_RUBBLE );
}

/* GameWindowManager.cpp: destroying a modal window that is not the top of the modal stack
   used to leave its entry behind, holding a pointer to freed memory. */
#include "GameClient/GameWindow.h"
#include "GameClient/Mouse.h"

TEST(destroying_a_modal_window_takes_it_out_of_the_stack_wherever_it_sits)
{
	/* the window pointers are never dereferenced, only compared - which is the whole point:
	   by the time the stack is cleaned up the window is being freed. */
	GameWindow *bottom = (GameWindow *)0x1000;
	GameWindow *middle = (GameWindow *)0x2000;
	GameWindow *top    = (GameWindow *)0x3000;

	ModalWindow *head = NULL;
	GameWindow *order[3] = { bottom, middle, top };
	for( Int i = 0; i < 3; i++ )
	{
		ModalWindow *entry = newInstance(ModalWindow);
		entry->window = order[i];
		entry->next = head;
		head = entry;			/* pushed like winSetModal does: top of the stack is 'top' */
	}

	/* the middle one goes away while another modal window still sits above it. */
	head = ModalStack_removeWindow( head, middle );
	CHECK( head != NULL );
	CHECK_EQ( head->window, top );
	CHECK( head->next != NULL );
	CHECK_EQ( head->next->window, bottom );
	CHECK( head->next->next == NULL );

	/* a window that is not in the stack at all leaves it alone. */
	head = ModalStack_removeWindow( head, (GameWindow *)0x4000 );
	CHECK_EQ( head->window, top );
	CHECK_EQ( head->next->window, bottom );

	/* the same window set modal twice loses both entries, not one. */
	for( Int j = 0; j < 2; j++ )
	{
		ModalWindow *dup = newInstance(ModalWindow);
		dup->window = bottom;
		dup->next = head;
		head = dup;
	}
	head = ModalStack_removeWindow( head, bottom );
	CHECK( head != NULL );
	CHECK_EQ( head->window, top );
	CHECK( head->next == NULL );

	/* emptying it gives a null head back, not a stale one. */
	head = ModalStack_removeWindow( head, top );
	CHECK( head == NULL );
	CHECK( ModalStack_removeWindow( NULL, top ) == NULL );
}

/* AIStates.cpp: a dud fight and a broken leash suspend the approach, never the shooting -
   a unit on attack move drove past whatever stood next to it during those windows. */
extern Bool AIAttackMove_mayTakeTarget( Bool targetIsInRangeNow, Bool targetIsTheLastDud,
																				Bool owesProgressToGoal, UnsignedInt now, UnsignedInt frameToApproachOn );

TEST(attack_move_still_shoots_what_it_can_already_reach)
{
	/* nothing owed: take anything, in range or not. */
	CHECK( AIAttackMove_mayTakeTarget( true,  false, false, 1000, 0 ) );
	CHECK( AIAttackMove_mayTakeTarget( false, false, false, 1000, 0 ) );

	/* inside the reacquire window after a dud fight: what we can shoot from here is still
	   taken, what we would have to drive to is not. This is the bug being fixed. */
	CHECK(  AIAttackMove_mayTakeTarget( true,  false, false, 1000, 1030 ) );
	CHECK( !AIAttackMove_mayTakeTarget( false, false, false, 1000, 1030 ) );

	/* owing the goal a leash length of progress after a broken leash: same answer. */
	CHECK(  AIAttackMove_mayTakeTarget( true,  false, true, 1000, 0 ) );
	CHECK( !AIAttackMove_mayTakeTarget( false, false, true, 1000, 0 ) );

	/* the window is over the moment the frame arrives. */
	CHECK( AIAttackMove_mayTakeTarget( false, false, false, 1030, 1030 ) );

	/* the one target the window does refuse is the one we just failed on - it is usually
	   still in range (out of ammo, cannot hurt it), so taking it again every scan would
	   park the unit there for the rest of the match. */
	CHECK( !AIAttackMove_mayTakeTarget( true, true, false, 1000, 1030 ) );
	CHECK(  AIAttackMove_mayTakeTarget( true, true, false, 1030, 1030 ) );
}

/* AI.cpp: on attack move the scan ranks what it finds by threat instead of handing
   back the nearest thing. */
extern Int AI_threatScore( Int templateThreatValue, Int buildCost, Bool threatensMe,
													 Real dist, Real distanceModifier );

TEST(attack_move_shoots_the_biggest_threat_first)
{
	const Real MOD = 100.0f;		/* AttackPriorityDistanceModifier, shipped value */
	const Real HERE = 0.0f;

	/* a mod that sets ThreatValue decides outright; build cost only stands in for it. */
	CHECK( AI_threatScore( 50, 4000, false, HERE, MOD ) > AI_threatScore( 40, 5000, false, HERE, MOD ) );

	/* the shipped INI sets ThreatValue nowhere, so cost has to do the ranking:
	   an Overlord-priced tank over a Humvee-priced one. */
	CHECK( AI_threatScore( 0, 1200, true, HERE, MOD ) > AI_threatScore( 0, 700, true, HERE, MOD ) );

	/* anything that can shoot back outranks anything that cannot, whatever it cost -
	   a Command Center is worth more than a Gattling tank and is still not what is
	   killing us - and no distance may move a target out of its group, or a unit would
	   walk past the artillery firing at it to reach a far off barracks. */
	CHECK( AI_threatScore( 0, 1, true, HERE, MOD ) > AI_threatScore( 0, 5000, false, HERE, MOD ) );
	CHECK( AI_threatScore( 1, 0, true, HERE, MOD ) > AI_threatScore( 9999, 0, false, HERE, MOD ) );
	CHECK( AI_threatScore( 0, 1, true, 9999.0f, MOD ) > AI_threatScore( 0, 5000, false, HERE, MOD ) );

	/* free civilian junk still scores, otherwise a scan that found only that would
	   report "nothing here" and the unit would walk past what it was told to clear. */
	CHECK( AI_threatScore( 0, 0, false, HERE, MOD ) > 0 );
	CHECK( AI_threatScore( 0, 0, false, 9999.0f, MOD ) > 0 );

	/* worth halves every modifier of distance. */
	CHECK_EQ( AI_threatScore( 0, 1000, false, MOD, MOD ), 500 );
	CHECK_EQ( AI_threatScore( 0, 1000, false, 3.0f*MOD, MOD ), 250 );

	/* so the same unit closer up wins, and a big enough threat still outranks a cheap
	   one underfoot: an Overlord a modifier away beats a Ranger at our feet. */
	CHECK( AI_threatScore( 0, 1200, true, 10.0f, MOD ) > AI_threatScore( 0, 1200, true, 200.0f, MOD ) );
	CHECK( AI_threatScore( 0, 2000, true, MOD, MOD ) > AI_threatScore( 0, 225, true, HERE, MOD ) );

	/* equal threats at equal range must tie exactly: the scan is sorted near to far and
	   keeps the first of a tie, which is how "then the closest one" gets honoured. */
	CHECK_EQ( AI_threatScore( 0, 900, true, 50.0f, MOD ), AI_threatScore( 0, 900, true, 50.0f, MOD ) );

	/* the modifier is 0 until the AI INI is read, and a target can measure as behind us
	   from a bounding sphere: neither may divide by zero or invert the ranking. */
	CHECK_EQ( AI_threatScore( 0, 1000, false, 500.0f, 0.0f ), 1000 );
	CHECK_EQ( AI_threatScore( 0, 1000, false, -5.0f, MOD ), 1000 );
}

//////////////////////////////////////////////////////////////////////////////
// Particle ground collision
//
// particleGroundBounce is the whole of the terrain collision added to
// Particle::update.  It touches no subsystem, so it can be driven directly.
//////////////////////////////////////////////////////////////////////////////

static Coord3D partCoord(Real x, Real y, Real z) { Coord3D c; c.set(x, y, z); return c; }

TEST(particle_above_the_ground_is_left_alone)
{
	Coord3D pos = partCoord(0.0f, 0.0f, 10.0f);
	Coord3D vel = partCoord(1.0f, 0.0f, -2.0f);
	const Coord3D up = partCoord(0.0f, 0.0f, 1.0f);

	CHECK(!particleGroundBounce(&pos, &vel, 5.0f, &up, 0.5f, 0.5f));
	CHECK_NEAR(pos.z, 10.0f, 1e-5f);
	CHECK_NEAR(vel.z, -2.0f, 1e-5f);
}

TEST(particle_bounces_off_flat_ground_and_keeps_the_restitution)
{
	Coord3D pos = partCoord(0.0f, 0.0f, 4.0f);
	Coord3D vel = partCoord(3.0f, 0.0f, -10.0f);
	const Coord3D up = partCoord(0.0f, 0.0f, 1.0f);

	CHECK(particleGroundBounce(&pos, &vel, 5.0f, &up, 0.5f, 0.8f));
	CHECK_NEAR(pos.z, 5.0f, 1e-5f);
	CHECK_NEAR(vel.z, 5.0f, 1e-5f);
	CHECK_NEAR(vel.x, 2.4f, 1e-5f);
	CHECK_NEAR(vel.y, 0.0f, 1e-5f);
}

TEST(particle_with_zero_bounce_stops_dead_and_slides)
{
	Coord3D pos = partCoord(0.0f, 0.0f, -1.0f);
	Coord3D vel = partCoord(4.0f, 0.0f, -6.0f);
	const Coord3D up = partCoord(0.0f, 0.0f, 1.0f);

	CHECK(particleGroundBounce(&pos, &vel, 0.0f, &up, 0.0f, 0.5f));
	CHECK_NEAR(vel.z, 0.0f, 1e-5f);
	CHECK_NEAR(vel.x, 2.0f, 1e-5f);
}

TEST(particle_on_a_slope_is_deflected_downhill_not_straight_up)
{
	// A 45 degree face whose normal leans towards +x, so downhill is +x.  Dropped straight
	// down onto it, the particle must carry on down the slope - not stop, and not be thrown
	// back up the way a flat Z flip would throw it.
	const Real k = 0.70710678f;
	Coord3D pos = partCoord(0.0f, 0.0f, -1.0f);
	Coord3D vel = partCoord(0.0f, 0.0f, -10.0f);
	const Coord3D slope = partCoord(k, 0.0f, k);

	CHECK(particleGroundBounce(&pos, &vel, 0.0f, &slope, 0.0f, 1.0f));
	CHECK_NEAR(vel.x, 5.0f, 1e-4f);		// pushed downhill
	CHECK_NEAR(vel.z, -5.0f, 1e-4f);	// and still going down, at the slope's angle
}

TEST(particle_already_leaving_the_surface_is_only_lifted_clear)
{
	// the resting case: gravity has just dragged it a hair under, but it is moving away
	Coord3D pos = partCoord(0.0f, 0.0f, -0.01f);
	Coord3D vel = partCoord(1.0f, 0.0f, 2.0f);
	const Coord3D up = partCoord(0.0f, 0.0f, 1.0f);

	CHECK(particleGroundBounce(&pos, &vel, 0.0f, &up, 0.5f, 0.5f));
	CHECK_NEAR(pos.z, 0.0f, 1e-5f);
	CHECK_NEAR(vel.z, 2.0f, 1e-5f);
	CHECK_NEAR(vel.x, 1.0f, 1e-5f);
}

//////////////////////////////////////////////////////////////////////////////
// The ground blob under a particle system
//
// particleShadowBlob* is the whole of the decision: which systems earn a soft
// shadow on the terrain, where it goes, how big it is and how dark.  Pure
// arithmetic over the live particles, so it can be driven directly.
//////////////////////////////////////////////////////////////////////////////

static void blobFeed(ParticleShadowBlob *blob, Int count, Real size, Real alpha)
{
	for (Int i = 0; i < count; i++)
		particleShadowBlobAdd(blob, (Real)i, 0.0f, size, alpha);
}

TEST(blob_reset_leaves_nothing_to_resolve)
{
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	CHECK_EQ(blob.m_count, 0);
	CHECK(!particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
}

TEST(blob_rejects_a_bullet_trail_because_its_particles_are_tiny)
{
	// plenty of particles, all of them a couple of units across - this is the case that
	// keeps trails, sparks and muzzle flashes from staining the ground
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	blobFeed(&blob, 40, 3.0f, 1.0f);
	CHECK(!particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
}

TEST(blob_rejects_a_puff_of_one_or_two_particles)
{
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	blobFeed(&blob, 2, 50.0f, 1.0f);
	CHECK(!particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
}

TEST(blob_rejects_a_cloud_that_has_faded_to_nothing)
{
	// big particles, enough of them, but no alpha left: a decal here would be a black
	// smear under smoke that is no longer drawn
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	blobFeed(&blob, 10, 60.0f, 0.0f);
	CHECK(!particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
}

TEST(blob_centres_on_the_particles_and_covers_their_spread_plus_one_particle)
{
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	particleShadowBlobAdd(&blob, 100.0f, 200.0f, 20.0f, 0.5f);
	particleShadowBlobAdd(&blob, 140.0f, 200.0f, 30.0f, 0.5f);
	particleShadowBlobAdd(&blob, 120.0f, 260.0f, 10.0f, 0.5f);

	CHECK(particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
	CHECK_NEAR(x, 120.0f, 1e-4f);					// midpoint of 100..140
	CHECK_NEAR(y, 230.0f, 1e-4f);					// midpoint of 200..260
	CHECK_NEAR(sx, 40.0f + 30.0f, 1e-4f);	// spread plus the biggest particle's width
	CHECK_NEAR(sy, 60.0f + 30.0f, 1e-4f);
	CHECK(op > 0);
}

TEST(blob_darkens_with_more_smoke_then_saturates_below_opaque)
{
	ParticleShadowBlob thin, thick, absurd;
	Real x, y, sx, sy;
	Int thinOp, thickOp, absurdOp;

	particleShadowBlobReset(&thin);
	blobFeed(&thin, 4, 40.0f, 0.25f);
	CHECK(particleShadowBlobResolve(&thin, &x, &y, &sx, &sy, &thinOp));

	particleShadowBlobReset(&thick);
	blobFeed(&thick, 8, 40.0f, 1.0f);		// past the saturation point
	CHECK(particleShadowBlobResolve(&thick, &x, &y, &sx, &sy, &thickOp));

	particleShadowBlobReset(&absurd);
	blobFeed(&absurd, 200, 40.0f, 1.0f);
	CHECK(particleShadowBlobResolve(&absurd, &x, &y, &sx, &sy, &absurdOp));

	CHECK(thinOp < thickOp);
	CHECK(thickOp <= absurdOp);
	CHECK(absurdOp < 255);		// smoke shades the ground, it never blacks it out
	CHECK_EQ(thickOp, absurdOp);	// and past saturation more smoke changes nothing
}

TEST(blob_never_smears_wider_than_the_cap)
{
	// a wind-blown system whose particles have drifted right across the map
	ParticleShadowBlob blob;
	Real x, y, sx, sy;
	Int op;

	particleShadowBlobReset(&blob);
	particleShadowBlobAdd(&blob, 0.0f, 0.0f, 40.0f, 1.0f);
	particleShadowBlobAdd(&blob, 5000.0f, 4000.0f, 40.0f, 1.0f);
	particleShadowBlobAdd(&blob, 2500.0f, 2000.0f, 40.0f, 1.0f);

	CHECK(particleShadowBlobResolve(&blob, &x, &y, &sx, &sy, &op));
	CHECK(sx <= 300.0f);
	CHECK(sy <= 300.0f);
}

//////////////////////////////////////////////////////////////////////////////
// Thicker, longer-lived smoke
//////////////////////////////////////////////////////////////////////////////

/*
 * "-smoke <thickness>" is three decisions and all three are free functions: how the one
 * number is spent, which systems it lands on, and how much headroom the particle ceiling
 * needs so the switch does not evict its own smoke.
 */

TEST(smoke_boost_is_off_at_or_below_the_shipped_thickness)
{
	SmokeBoost boost;

	CHECK(!particleSmokeBoostResolve(0.0f, &boost));		// the switch not given
	CHECK(!particleSmokeBoostResolve(1.0f, &boost));		// given, asking for nothing
	CHECK(!particleSmokeBoostResolve(0.5f, &boost));		// given, asking for less
}

TEST(smoke_boost_spends_most_of_the_number_on_time_and_the_rest_on_look)
{
	SmokeBoost boost;

	CHECK(particleSmokeBoostResolve(3.0f, &boost));
	CHECK_NEAR(boost.m_lifetimeScale, 3.0f, 1e-4f);

	// opacity and width move, but far less than time does - a cloud three times as opaque is
	// a grey brick
	CHECK(boost.m_alphaScale > 1.0f);
	CHECK(boost.m_sizeScale > 1.0f);
	CHECK(boost.m_alphaScale < boost.m_lifetimeScale);
	CHECK(boost.m_sizeScale < boost.m_alphaScale);
}

TEST(smoke_boost_clamps_an_absurd_thickness)
{
	SmokeBoost boost;

	CHECK(particleSmokeBoostResolve(1000.0f, &boost));
	CHECK_NEAR(boost.m_lifetimeScale, 8.0f, 1e-4f);
}

TEST(smoke_name_match_is_case_insensitive_and_anywhere_in_the_name)
{
	CHECK(particleSmokeNameMatches("SmokeTrail"));
	CHECK(particleSmokeNameMatches("BuildingSmokeLarge"));
	CHECK(particleSmokeNameMatches("GenericBlackSMOKE"));
	CHECK(particleSmokeNameMatches("smoke"));
}

TEST(smoke_name_match_leaves_everything_else_alone)
{
	CHECK(!particleSmokeNameMatches("FireballLarge"));
	CHECK(!particleSmokeNameMatches("SparkShower"));
	CHECK(!particleSmokeNameMatches("Smok"));					// a prefix of the word is not the word
	CHECK(!particleSmokeNameMatches(""));
	CHECK(!particleSmokeNameMatches(NULL));
}

TEST(smoke_cap_leaves_the_shipped_ceiling_alone_when_the_switch_is_off)
{
	CHECK_EQ(particleSmokeParticleCap(2500, 0.0f), 2500);
	CHECK_EQ(particleSmokeParticleCap(2500, 1.0f), 2500);
}

TEST(smoke_cap_never_overrides_a_player_who_asked_for_no_particles)
{
	// the options slider goes to zero and that means zero, switch or no switch
	CHECK_EQ(particleSmokeParticleCap(0, 8.0f), 0);
}

TEST(smoke_cap_grows_with_the_thickness_and_stops_at_the_ceiling)
{
	CHECK_EQ(particleSmokeParticleCap(2500, 2.0f), 5000);
	CHECK(particleSmokeParticleCap(2500, 4.0f) > particleSmokeParticleCap(2500, 2.0f));
	CHECK_EQ(particleSmokeParticleCap(20000, 8.0f), 20000);
}

//////////////////////////////////////////////////////////////////////////////
// The in-flight damage ledger
//////////////////////////////////////////////////////////////////////////////

/*
 * A delayed shot - a projectile in flight, or a hitscan weapon far enough away that its
 * damage is scheduled for a later frame - used to leave no trace anywhere between the
 * muzzle and the impact.  Every other unit went on reading full health off the victim and
 * kept firing, so a squad routinely spent a whole volley killing something the first two
 * shots had already killed.  IncomingDamageTracker books each such shot against its victim
 * and auto-targeting reads it back.
 *
 * These drive the ledger with hand-picked frame numbers, which is why the frame is a
 * parameter rather than a read of TheGameLogic - there is no game logic in this binary.
 */

static const ObjectID VICTIM_A = (ObjectID)101;
static const ObjectID VICTIM_B = (ObjectID)102;
static const ObjectID SHOOTER_1 = (ObjectID)201;
static const ObjectID SHOOTER_2 = (ObjectID)202;

TEST(incomingdamage_books_and_sums_per_victim)
{
	IncomingDamageTracker::reset();
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 100, 110);
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_2, 25.0f, 100, 112);
	IncomingDamageTracker::bookShot(VICTIM_B, SHOOTER_1, 10.0f, 100, 110);

	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 65.0f, 0.001f);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_B), 10.0f, 0.001f);

	IncomingDamageTracker::reset();
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);
}

TEST(incomingdamage_doomed_only_once_the_booking_covers_the_health)
{
	IncomingDamageTracker::reset();

	// nothing booked: never doomed, however little health is left
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 1.0f));

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 100, 110);
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 100.0f));
	CHECK(IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 40.0f));	// exactly lethal counts

	// a second shooter's shell tips it over: this is the overkill the ledger exists to stop
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_2, 70.0f, 100, 112);
	CHECK(IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 100.0f));

	// and the victim next to it is unaffected
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_B, 1.0f));

	IncomingDamageTracker::reset();
}

TEST(incomingdamage_landing_releases_one_booking_from_that_shooter)
{
	IncomingDamageTracker::reset();

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 100, 110);
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 101, 111);
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_2, 25.0f, 100, 112);

	// one landing releases one shot, not the shooter's whole account
	IncomingDamageTracker::shotLanded(VICTIM_A, SHOOTER_1);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 65.0f, 0.001f);

	IncomingDamageTracker::shotLanded(VICTIM_A, SHOOTER_1);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 25.0f, 0.001f);

	// a landing nobody booked is not an error, and takes nothing with it
	IncomingDamageTracker::shotLanded(VICTIM_A, SHOOTER_1);
	IncomingDamageTracker::shotLanded(VICTIM_B, SHOOTER_1);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 25.0f, 0.001f);

	IncomingDamageTracker::shotLanded(VICTIM_A, SHOOTER_2);
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);

	IncomingDamageTracker::reset();
}

/*
 * The reservation has to lapse on its own, or a missile shot down by a point defence - or
 * lured away by countermeasures - would reserve its target forever and the squad would
 * stand there holding its fire.
 */
TEST(incomingdamage_booking_lapses_after_the_impact_it_predicted)
{
	IncomingDamageTracker::reset();

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 100, 110);

	// still honored while the shot is plausibly in the air
	IncomingDamageTracker::update(110);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 40.0f, 0.001f);

	// ...and released once the impact is well past and nothing landed
	IncomingDamageTracker::update(200);
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 1.0f));

	IncomingDamageTracker::reset();
}

/*
 * An impact frame that has already gone by (or is this very frame) must not wrap the
 * unsigned subtraction into a four-billion-frame reservation.
 */
TEST(incomingdamage_impact_in_the_past_still_expires)
{
	IncomingDamageTracker::reset();

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 40.0f, 500, 400);
	CHECK_NEAR(IncomingDamageTracker::getBookedDamage(VICTIM_A), 40.0f, 0.001f);

	IncomingDamageTracker::update(600);
	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);

	IncomingDamageTracker::reset();
}

/* A shot that would do nothing is not worth reserving a target over. */
TEST(incomingdamage_ignores_harmless_shots)
{
	IncomingDamageTracker::reset();

	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, 0.0f, 100, 110);
	IncomingDamageTracker::bookShot(VICTIM_A, SHOOTER_1, -5.0f, 100, 110);
	IncomingDamageTracker::bookShot(INVALID_ID, SHOOTER_1, 40.0f, 100, 110);

	CHECK_EQ(IncomingDamageTracker::getBookedDamage(VICTIM_A), 0.0f);
	CHECK(!IncomingDamageTracker::isAlreadyDoomed(VICTIM_A, 1.0f));

	IncomingDamageTracker::reset();
}

//////////////////////////////////////////////////////////////////////////////
// Drag-to-aim building placement
//////////////////////////////////////////////////////////////////////////////

/*
 * The 45 degree snap used to pick its rounding by sign: floor(x + 0.5) above zero but
 * floor(x - 0.5) below it, which rounds *away* from zero.  REAL_TO_INT_FLOOR is a true
 * floor, so the second branch pushed every negative heading a whole step out - and
 * Coord2D::toAngle returns -PI..PI, so that is half the circle.  Dragging into it meant
 * fighting the snap: the building faced 45 degrees past where the mouse was pointing.
 */
TEST(placement_snap_takes_the_nearest_45_on_both_halves_of_the_circle)
{
	const Real step = PI / 4.0f;

	/* dead on a spoke stays put */
	for (Int i = -4; i <= 4; i++)
		CHECK_NEAR(InGameUI::snapAngleTo45(i * step), i * step, 0.0001f);

	/* just short of a spoke rounds up to it, just past rounds back down to it - same both signs */
	CHECK_NEAR(InGameUI::snapAngleTo45(step - 0.1f), step, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(step + 0.1f), step, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(-step - 0.1f), -step, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(-step + 0.1f), -step, 0.0001f);

	/* the old sign branch failed exactly here: -0.2 rad is nearest to 0, not to -45 degrees */
	CHECK_NEAR(InGameUI::snapAngleTo45(-0.2f), 0.0f, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(-1.0f), -step, 0.0001f);
	CHECK_NEAR(InGameUI::snapAngleTo45(-2.0f), -3.0f * step, 0.0001f);	/* -114.6 deg is nearer -135 than -90 */

	/* nothing is ever more than half a step of mouse travel from its snap */
	for (Int deg = -180; deg <= 180; deg += 3)
	{
		Real angle = deg * PI / 180.0f;
		Real snapped = InGameUI::snapAngleTo45(angle);

		CHECK(fabsf(snapped - angle) <= step / 2.0f + 0.0001f);

		/* and it really is a multiple of 45 degrees */
		CHECK_NEAR(snapped / step, (Real)REAL_TO_INT_FLOOR(snapped / step + 0.5f), 0.0001f);
	}
}

/*
 * GridBuildPlacement.  The grid is the pathfinder's, 10 world units a cell, and what has to
 * land on it is the footprint's *edges*, not its centre: an odd number of cells wide means
 * the centre sits in the middle of a cell, an even number means it sits on the line between
 * two.  Get that backwards and every second structure straddles a cell it only half fills,
 * which is the gap-you-cannot-walk-through this snap exists to remove.
 */
TEST(command_availability_rank_is_not_the_enum_order)
{
	/* the whole point of the helper: the enum is declared RESTRICTED, AVAILABLE, ACTIVE, HIDDEN,
	 * NOT_READY, CANT_AFFORD, so taking the numeric max over a multi-selection would rank
	 * "cannot afford" above "available" and "hidden" above both */
	CHECK((Int)COMMAND_HIDDEN > (Int)COMMAND_AVAILABLE);
	CHECK((Int)COMMAND_CANT_AFFORD > (Int)COMMAND_AVAILABLE);

	/* permissiveness, most to least */
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_ACTIVE) >
	      ControlBar::commandAvailabilityRank(COMMAND_AVAILABLE));
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_AVAILABLE) >
	      ControlBar::commandAvailabilityRank(COMMAND_CANT_AFFORD));
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_CANT_AFFORD) >
	      ControlBar::commandAvailabilityRank(COMMAND_NOT_READY));
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_NOT_READY) >
	      ControlBar::commandAvailabilityRank(COMMAND_RESTRICTED));
	CHECK(ControlBar::commandAvailabilityRank(COMMAND_RESTRICTED) >
	      ControlBar::commandAvailabilityRank(COMMAND_HIDDEN));

	/* a group of four barracks where only the last can still buy the upgrade: the button is
	 * offered, because the best answer in the group wins */
	CommandAvailability group[] = { COMMAND_RESTRICTED, COMMAND_RESTRICTED,
	                                COMMAND_RESTRICTED, COMMAND_AVAILABLE };
	CommandAvailability best = group[0];
	for (Int i = 1; i < 4; i++)
		if (ControlBar::commandAvailabilityRank(group[i]) > ControlBar::commandAvailabilityRank(best))
			best = group[i];
	CHECK_EQ((Int)best, (Int)COMMAND_AVAILABLE);
}

/*
 * NudgeBuildPlacement.  The whole promise of the feature is "nearest spot that fits", and the
 * only thing that makes "first candidate that fits" mean that is the order the candidates come
 * out in.  Ring by ring is not that order - from three rings out a ring's own diagonal is
 * farther than the next ring's straight neighbour - and the square has to be filled in, not
 * just sampled along the eight compass directions, or the search walks past the gap one cell
 * to the side of them.
 */
TEST(placement_nudge_offsets_come_out_nearest_first)
{
	const Real step = (Real)InGameUI::PLACEMENT_CELL;
	const Int rings = InGameUI::PLACEMENT_NUDGE_RINGS;
	Real dx[ InGameUI::PLACEMENT_NUDGE_TRIES ], dy[ InGameUI::PLACEMENT_NUDGE_TRIES ];
	Int i, j;

	for (i = 0; i < InGameUI::PLACEMENT_NUDGE_TRIES; i++)
		InGameUI::placementNudgeOffset(i, step, &dx[i], &dy[i]);

	/* nearest first, and never standing still - the cursor's own spot is the one that failed */
	for (i = 0; i < InGameUI::PLACEMENT_NUDGE_TRIES; i++)
	{
		Real d = dx[i] * dx[i] + dy[i] * dy[i];

		CHECK(d > 0.0f);
		if (i > 0)
			CHECK(d >= dx[i-1] * dx[i-1] + dy[i-1] * dy[i-1] - 0.0001f);
	}

	/* the first move offered is one cell straight, not a diagonal */
	CHECK_NEAR(dx[0] * dx[0] + dy[0] * dy[0], step * step, 0.0001f);

	/* the whole square is covered, once each: every cell of it is in the list exactly once, so
	 * a gap one cell to the side of a compass direction is found like any other */
	CHECK_EQ(InGameUI::PLACEMENT_NUDGE_TRIES, (2 * rings + 1) * (2 * rings + 1) - 1);
	for (i = 0; i < InGameUI::PLACEMENT_NUDGE_TRIES; i++)
		for (j = i + 1; j < InGameUI::PLACEMENT_NUDGE_TRIES; j++)
			CHECK(fabsf(dx[i] - dx[j]) > 0.0001f || fabsf(dy[i] - dy[j]) > 0.0001f);

	/* every candidate lands on the same build grid the snap uses, so a nudged structure still
	 * shares its edges with the ones already down, and none of them leaves the square */
	for (i = 0; i < InGameUI::PLACEMENT_NUDGE_TRIES; i++)
	{
		CHECK_NEAR(dx[i] / step, (Real)REAL_TO_INT_FLOOR(dx[i] / step + 0.5f), 0.0001f);
		CHECK_NEAR(dy[i] / step, (Real)REAL_TO_INT_FLOOR(dy[i] / step + 0.5f), 0.0001f);
		CHECK(fabsf(dx[i]) <= rings * step + 0.0001f && fabsf(dy[i]) <= rings * step + 0.0001f);
	}

	/* and the far corner really is last - the list is the whole search, not a prefix of it */
	CHECK_NEAR(fabsf(dx[InGameUI::PLACEMENT_NUDGE_TRIES - 1]), rings * step, 0.0001f);
	CHECK_NEAR(fabsf(dy[InGameUI::PLACEMENT_NUDGE_TRIES - 1]), rings * step, 0.0001f);
}

TEST(placement_grid_snap_puts_footprint_edges_on_cell_lines)
{
	const Real cell = 10.0f;

	/* The pathfinder files a position under floor((v + 0.5) / 10), so its cell lines sit at
	 * k*10 - 0.5, not at k*10.  Everything below is measured against those. */
	CHECK_NEAR(InGameUI::placementGridLine(0), -0.5f, 0.0001f);
	CHECK_NEAR(InGameUI::placementGridLine(3), 29.5f, 0.0001f);

	/* 3 cells wide (extent 15) - centre on a cell centre, whatever it started as */
	CHECK_NEAR(InGameUI::snapPlacementAxis(0.0f, 15.0f), 4.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(4.0f, 15.0f), 4.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(9.0f, 15.0f), 4.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(11.0f, 15.0f), 14.5f, 0.0001f);

	/* 4 cells wide (extent 20) - centre on a cell line */
	CHECK_NEAR(InGameUI::snapPlacementAxis(4.0f, 20.0f), -0.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(6.0f, 20.0f), 9.5f, 0.0001f);
	CHECK_NEAR(InGameUI::snapPlacementAxis(123.0f, 20.0f), 119.5f, 0.0001f);

	/* a snap never moves anything more than half a cell */
	for (Int i = 0; i < 400; i++)
	{
		Real v = i * 0.7f;

		CHECK(fabsf(InGameUI::snapPlacementAxis(v, 15.0f) - v) <= cell / 2.0f + 0.0001f);
		CHECK(fabsf(InGameUI::snapPlacementAxis(v, 20.0f) - v) <= cell / 2.0f + 0.0001f);
	}

	/* and both edges of the footprint end up on the pathfinder's own cell lines, odd width and
	 * even alike - which is what makes the cells a structure blocks exactly the cells it covers */
	for (Int cells = 1; cells <= 8; cells++)
	{
		Real extent = cells * cell * 0.5f;
		Real centre = InGameUI::snapPlacementAxis(37.3f, extent);
		Real lo = (centre - extent - InGameUI::placementGridLine(0)) / cell;
		Real hi = (centre + extent - InGameUI::placementGridLine(0)) / cell;

		CHECK_NEAR(lo, (Real)REAL_TO_INT_FLOOR(lo + 0.5f), 0.0001f);
		CHECK_NEAR(hi, (Real)REAL_TO_INT_FLOOR(hi + 0.5f), 0.0001f);

		/* the cell the pathfinder puts each edge in is the first and last cell covered: no
		 * half-unit sliver of a neighbouring cell left over for the building next door */
		CHECK_EQ(REAL_TO_INT_FLOOR((centre - extent + 0.5f) / cell),
		         REAL_TO_INT_FLOOR((centre - extent + 0.5f + 0.1f) / cell));
		CHECK_EQ(REAL_TO_INT_FLOOR((centre + extent + 0.5f) / cell) - 1,
		         REAL_TO_INT_FLOOR((centre + extent + 0.5f - 0.1f) / cell));
	}

	/* a template with no footprint worth the name still lands on a whole cell, not on nothing */
	CHECK_NEAR(InGameUI::snapPlacementAxis(13.0f, 0.0f), 14.5f, 0.0001f);
}


/* The shipped AIData.ini values, so the numbers below are the ones a real game uses. */
static const Real AIDATA_TEAM_SECONDS   = 10.0f;
static const Int  AIDATA_POOR           = 2000;
static const Int  AIDATA_WEALTHY        = 7000;
static const Real AIDATA_TEAM_POOR_MOD  = 0.6f;
static const Real AIDATA_TEAM_RICH_MOD  = 2.0f;
static const Real SKIRMISH_RATE         = 10.0f/3.0f;

static Int teamDelay(Int money, Real rate)
{
	return AIPlayer::computeBuildDelay(AIDATA_TEAM_SECONDS, money, AIDATA_POOR, AIDATA_WEALTHY,
	                                   AIDATA_TEAM_POOR_MOD, AIDATA_TEAM_RICH_MOD, rate);
}

TEST(ai_build_delay_follows_the_players_bank_account)
{
	/* A campaign AI runs at the rate the data literally says: 10 seconds flat, faster when it
	 * is rich, slower when it is broke. */
	CHECK_EQ(teamDelay(4000, 1.0f), 10 * LOGICFRAMES_PER_SECOND);
	CHECK_EQ(teamDelay(9000, 1.0f),  5 * LOGICFRAMES_PER_SECOND);
	CHECK_EQ(teamDelay(1000, 1.0f), (Int)(10.0f / 0.6f * LOGICFRAMES_PER_SECOND));

	/* The thresholds are exclusive on both sides - sitting exactly on Poor or exactly on
	 * Wealthy is neither. */
	CHECK_EQ(teamDelay(AIDATA_POOR, 1.0f), 10 * LOGICFRAMES_PER_SECOND);
	CHECK_EQ(teamDelay(AIDATA_WEALTHY, 1.0f), 10 * LOGICFRAMES_PER_SECOND);
}

TEST(skirmish_build_rate_keeps_the_old_pace_but_not_the_old_flat_clamp)
{
	/* The skirmish AI used to clamp both of its timers to a flat 3 seconds every frame they
	 * counted down.  With the shipped TeamSeconds of 10 that clamp fired for every player at
	 * every wealth, so TeamsWealthyRate and TeamsPoorRate did nothing at all.  As a rate the
	 * neutral case still lands on the same 3 seconds... */
	CHECK_EQ(teamDelay(4000, SKIRMISH_RATE), 3 * LOGICFRAMES_PER_SECOND);

	/* ...and the modifiers around it are alive again: rich presses harder, broke backs off.
	 * Under the clamp all three of these were 90. */
	CHECK_EQ(teamDelay(9000, SKIRMISH_RATE), (Int)(1.5f * LOGICFRAMES_PER_SECOND));
	CHECK_EQ(teamDelay(1000, SKIRMISH_RATE), (Int)(5.0f * LOGICFRAMES_PER_SECOND));
	CHECK(teamDelay(9000, SKIRMISH_RATE) < teamDelay(4000, SKIRMISH_RATE));
	CHECK(teamDelay(1000, SKIRMISH_RATE) > teamDelay(4000, SKIRMISH_RATE));

	/* The SET_BASE_CONSTRUCTION_SPEED script action writes the seconds this reads, and the
	 * clamp is what used to eat anything it asked for above 3 seconds. */
	const Int scripted = AIPlayer::computeBuildDelay(30.0f, 4000, AIDATA_POOR, AIDATA_WEALTHY,
	                                                 AIDATA_TEAM_POOR_MOD, AIDATA_TEAM_RICH_MOD,
	                                                 SKIRMISH_RATE);
	CHECK_EQ(scripted, 9 * LOGICFRAMES_PER_SECOND);
	CHECK(scripted > 3 * LOGICFRAMES_PER_SECOND);

	/* StructureSeconds ships as 0, so structures stay on the "as soon as the build delay lets
	 * you" path they are on today, whatever the rate is. */
	CHECK_EQ(AIPlayer::computeBuildDelay(0.0f, 9000, AIDATA_POOR, AIDATA_WEALTHY, 0.6f, 2.0f, SKIRMISH_RATE), 0);
}

TEST(skirmish_team_move_actions_are_the_throttled_ones)
{
	/* Every action listed here reaches AIGroup::friend_computeGroundPath, which runs a full-map A*
	 * synchronously.  Six of them landed on one logic frame in a 1v7 skirmish and cost 80ms of a
	 * 33ms budget, so the sequential-script step lets one through per frame.  Pin the set: adding a
	 * team-move action without adding it here quietly puts the pile-up back. */
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::MOVE_TEAM_TO));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::TEAM_FOLLOW_WAYPOINTS));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::TEAM_FOLLOW_WAYPOINTS_EXACT));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::SKIRMISH_FOLLOW_APPROACH_PATH));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::SKIRMISH_MOVE_TO_APPROACH_PATH));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::CREATE_REINFORCEMENT_TEAM));
	CHECK(ScriptEngine::isThrottledTeamMoveAction(ScriptAction::SKIRMISH_ATTACK_NEAREST_GROUP_WITH_VALUE));

	/* ...and nothing cheap is throttled, or the AI would crawl for no reason. */
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(ScriptAction::DEBUG_MESSAGE_BOX));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(ScriptAction::ENABLE_SCRIPT));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(ScriptAction::SET_FLAG));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(ScriptAction::NO_OP));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(-1));
	CHECK(!ScriptEngine::isThrottledTeamMoveAction(0x7fffffff));
}

TEST(ai_players_do_not_all_check_in_on_the_same_frame)
{
	/* Every AIPlayer used to be built with the same timers on the same frame, and every one of
	 * its repeating checks re-arms itself from a constant, so seven bots ran their base building,
	 * team building and bridge repair together for the whole match and the cost of all seven
	 * landed on one logic frame.  The phase is what pulls them apart. */
	const Int cycle = 2 * LOGICFRAMES_PER_SECOND;
	Int seen[ MAX_PLAYER_COUNT ];
	Int i, j;
	for (i = 0; i < MAX_PLAYER_COUNT; i++)
	{
		seen[i] = AIPlayer::computeUpdatePhase(i, cycle);
		/* A phase is a slot inside the cycle, never a cycle of its own - the check still runs
		 * exactly as often as it did, just not at the same moment as the neighbour's. */
		CHECK(seen[i] >= 0);
		CHECK(seen[i] < cycle);
	}
	for (i = 0; i < MAX_PLAYER_COUNT; i++)
		for (j = i + 1; j < MAX_PLAYER_COUNT; j++)
			CHECK_NE(seen[i], seen[j]);

	/* It is a function of the player index and nothing else - no clock, no random - or the
	 * lockstep simulation would desync the moment two machines disagreed. */
	CHECK_EQ(AIPlayer::computeUpdatePhase(3, cycle), AIPlayer::computeUpdatePhase(3, cycle));

	/* A one-second cycle has fewer frames than MAX_PLAYER_COUNT has players, so the slots
	 * collide there; what must not happen is a phase outside the cycle. */
	for (i = 0; i < MAX_PLAYER_COUNT; i++)
	{
		const Int shortPhase = AIPlayer::computeUpdatePhase(i, LOGICFRAMES_PER_SECOND);
		CHECK(shortPhase >= 0);
		CHECK(shortPhase < LOGICFRAMES_PER_SECOND);
	}

	/* Garbage in stays harmless: an unset index or a zero cycle means "no offset". */
	CHECK_EQ(AIPlayer::computeUpdatePhase(-1, cycle), 0);
	CHECK_EQ(AIPlayer::computeUpdatePhase(0, cycle), 0);
	CHECK_EQ(AIPlayer::computeUpdatePhase(5, 0), 0);
}

//////////////////////////////////////////////////////////////////////////////
// Pathfinder cell info pool
//////////////////////////////////////////////////////////////////////////////

/* The A* open and closed lists are drawn from one fixed pool of PathfindCellInfo
   records.  A search that walks away from a cell without handing its record back
   leaks one entry, every time it runs, for the rest of the match - and once the
   pool is dry every pathfind on the map fails and the units stop taking orders.
   The pool is private, so the only way to measure it is to drain it. */
enum { CELL_POOL_PROBE_CAP = 400000 };

static Int drainCellInfoPool( void )
{
	PathfindCell *cells = new PathfindCell[ CELL_POOL_PROBE_CAP ];
	Int count = 0;
	while( count < CELL_POOL_PROBE_CAP )
	{
		ICoord2D pos;
		pos.x = count & 0xff;
		pos.y = (count >> 8) & 0xff;
		if( !cells[ count ].allocateInfo( pos ) )
			break;
		count++;
	}
	for( Int i = 0; i < count; i++ )
		cells[ i ].releaseInfo();
	delete [] cells;
	return count;
}

TEST(pathfind_pool_comes_back_whole_after_a_search_is_started)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int baseline = drainCellInfoPool();
	CHECK(baseline > 1000);
	CHECK(baseline < CELL_POOL_PROBE_CAP);

	{
		PathfindCell start, goal;
		ICoord2D sp, gp;
		sp.x = 10; sp.y = 10;
		gp.x = 20; gp.y = 30;
		CHECK(start.allocateInfo(sp));
		CHECK(goal.allocateInfo(gp));

		/* startPathfind used to mark the start cell as sitting on the open list.  It is not:
		 * the caller assigns it to m_openList by hand rather than linking it in.  All the flag
		 * did was make releaseInfo() refuse to hand the record back, so every search that gave
		 * up early - wrong zone, no path, pool empty - leaked its start cell. */
		start.startPathfind(&goal);
		CHECK(!start.getOpen());
		CHECK(!start.getClosed());

		start.releaseInfo();
		CHECK(!start.hasInfo());
		goal.releaseInfo();
		CHECK(!goal.hasInfo());
	}

	CHECK_EQ(drainCellInfoPool(), baseline);
	PathfindCellInfo::releaseCellInfos();
}

TEST(pathfind_cell_drops_its_parent_link_even_when_it_keeps_its_record)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int baseline = drainCellInfoPool();

	{
		PathfindCell keeper, parent;
		ICoord2D kp, pp;
		kp.x = 5; kp.y = 5;
		pp.x = 6; pp.y = 5;
		CHECK(keeper.allocateInfo(kp));
		CHECK(parent.allocateInfo(pp));
		keeper.setParentCell(&parent);
		CHECK(keeper.getParentCell() == &parent);

		/* An obstacle cell hangs on to its record - releaseInfo() returns early for it.  It
		 * used to return before clearing the parent link as well, so the cell went on pointing
		 * at a record the very next search hands out to some other cell, and walking the path
		 * backwards from there reads a stranger's data. */
		keeper.setType(PathfindCell::CELL_OBSTACLE);
		keeper.releaseInfo();
		CHECK(keeper.hasInfo());
		CHECK(keeper.getParentCell() == NULL);

		keeper.setType(PathfindCell::CELL_CLEAR);
		keeper.releaseInfo();
		CHECK(!keeper.hasInfo());
		parent.releaseInfo();
		CHECK(!parent.hasInfo());
	}

	CHECK_EQ(drainCellInfoPool(), baseline);
	PathfindCellInfo::releaseCellInfos();
}

TEST(pathfind_obstacle_state_no_longer_borrows_a_search_record)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int baseline = drainCellInfoPool();

	{
		PathfindCell cell;
		CHECK(!cell.hasInfo());

		/* Which object stands on a cell, whether it is a fence, whether you can see through it
		 * and whether an ally is in the way all used to be stored in a pooled search record, so
		 * a cell had to hold one open for as long as the wall stood on it - and releaseInfo()
		 * refuses to reclaim an obstacle cell, so every building and fence on the map was a
		 * permanent bite out of the pool the search draws from.  They are cell state now, and a
		 * cell answers for them with no record at all. */
		cell.setBlockedByAlly(TRUE);
		CHECK(cell.isBlockedByAlly());
		cell.setBlockedByAlly(FALSE);
		CHECK(!cell.isBlockedByAlly());
		CHECK(cell.getObstacleID() == INVALID_ID);
		CHECK(!cell.isObstacleFence());
		CHECK(!cell.isObstacleTransparent());
		CHECK(!cell.isObstaclePresent((ObjectID)17));

		CHECK(!cell.hasInfo());
	}

	CHECK_EQ(drainCellInfoPool(), baseline);
	PathfindCellInfo::releaseCellInfos();
}


/* The tunnel network keeps the contain list for every tunnel a player owns, and
 * TunnelContain::killAllContained has to take that whole list away from the tracker before it
 * kills anything: a Terrorist that explodes on death kills the tunnel, and the tunnel's own death
 * walks the same list the outer call is still standing in.  Taking the list away is only half of
 * it - the tracker's cached size has to follow, or the game reads a count that no longer matches
 * the list.  No Object is touched here, only the bookkeeping. */
TEST(tunneltracker_handing_the_contain_list_over_takes_the_count_with_it)
{
	CHECK(bootOnce());

	TunnelTracker *tracker = newInstance(TunnelTracker);
	CHECK_EQ((Int)tracker->getContainCount(), 0);

	ContainedItemsList seeded;
	seeded.push_back((Object *)0x100);
	seeded.push_back((Object *)0x200);
	seeded.push_back((Object *)0x300);

	tracker->swapContainedItemsList(seeded);
	CHECK(seeded.empty());
	CHECK_EQ((Int)tracker->getContainCount(), 3);
	CHECK_EQ((Int)tracker->getContainedItemsList()->size(), 3);

	/* what killAllContained does: take the list, leave the tracker empty and consistent */
	ContainedItemsList taken;
	tracker->swapContainedItemsList(taken);
	CHECK_EQ((Int)taken.size(), 3);
	CHECK_EQ((Int)tracker->getContainCount(), 0);
	CHECK(tracker->getContainedItemsList()->empty());

	taken.clear();
	tracker->deleteInstance();
}

/* StateMachine is reference counted so that a state update which destroys the
   machine's owner does not pull the machine out from under updateStateMachine.
   The owner's deleteInstance() is now just "drop my reference". */
static Bool s_witnessMachineDestroyed = FALSE;

class WitnessStateMachine : public StateMachine
{
	/* borrow the real machine's pool - same size, and MemoryInit.cpp knows the name */
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(WitnessStateMachine, "StateMachinePool")
public:
	WitnessStateMachine() : StateMachine(NULL, "witness") {}
};

WitnessStateMachine::~WitnessStateMachine() { s_witnessMachineDestroyed = TRUE; }

TEST(statemachine_outlives_the_owner_that_lets_go_of_it_mid_update)
{
	CHECK(bootOnce());

	s_witnessMachineDestroyed = FALSE;
	StateMachine *machine = newInstance(WitnessStateMachine);
	CHECK_EQ(machine->Num_Refs(), 1);

	/* what updateStateMachine holds while m_currentState->update() runs */
	machine->Add_Ref();
	CHECK_EQ(machine->Num_Refs(), 2);

	/* the state update kills the owning object, which deletes its machine */
	machine->deleteInstance();
	CHECK(!s_witnessMachineDestroyed);
	CHECK_EQ(machine->Num_Refs(), 1);

	/* update() returns, updateStateMachine drops its reference - now it goes */
	machine->Release_Ref();
	CHECK(s_witnessMachineDestroyed);

	/* and deleteInstance() still tolerates a NULL machine, the way the pool one did */
	machine = NULL;
	machine->deleteInstance();
}


/* ------------------------------------------------------------------------------------------------
 * The A* open list.
 *
 * `PathfindCell::putOnSortedOpenList` used to walk from the head on every insert, past every cell
 * of equal cost, which is O(open list) per expanded cell - 62 million walk steps over 108 slow
 * frames in a real skirmish.  It now keeps a tail pointer and enters from whichever end is nearer
 * the new cost.  The insertion *point* has to stay exactly where it was, or the search expands a
 * different set of cells and lockstep breaks, so these check the resulting order against a stable
 * sort of the same insertion sequence: ascending cost, ties in insertion order.
 * ---------------------------------------------------------------------------------------------- */
static PathfindCell *theOpenTestCells = NULL;

static Bool openListOrderMatches( PathfindCell *list, const std::vector<Int>& expected )
{
	UnsignedInt n = 0;
	UnsignedInt prevCost = 0;
	for( PathfindCell *c = list; c; c = c->getNextOpen() )
	{
		if( n >= expected.size() )
			return false;											// longer than it should be
		if( c->getTotalCost() < prevCost )
			return false;											// not sorted
		if( (Int)(c - theOpenTestCells) != expected[ n ] )
			return false;											// right costs, wrong tie order
		prevCost = c->getTotalCost();
		n++;
	}
	return n == expected.size();
}

TEST(pathfind_open_list_insert_keeps_ascending_cost_and_insertion_order_on_ties)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int count = 400;
	PathfindCell *cells = MSGNEW("PathfindCellInfo") PathfindCell[ count ];
	theOpenTestCells = cells;

	/* costs with a lot of ties (the grid quantises everything to multiples of ten) and no
		 monotonic order, so every branch of the new insert gets used */
	std::vector<Int> expected;
	PathfindCell *list = NULL;
	Int seed = 12345;
	Int i;
	for( i = 0; i < count; i++ )
	{
		seed = seed * 1103515245 + 12345;
		UnsignedInt cost = 10 * (UnsignedInt)(((seed >> 16) & 0x7fff) % 25);
		ICoord2D pos;
		pos.x = (UnsignedShort)(i % 64);
		pos.y = (UnsignedShort)(i / 64);
		CHECK(cells[ i ].allocateInfo( pos ));
		cells[ i ].setTotalCost( cost );

		/* the reference: insert after every cell of equal or lower cost */
		std::vector<Int>::iterator it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= cost )
			++it;
		expected.insert( it, i );

		list = cells[ i ].putOnSortedOpenList( list );
	}
	CHECK(openListOrderMatches( list, expected ));

	/* pull cells out - including the head and the tail, the two the fast paths depend on - and
		 put fresh ones back, which is exactly what an A* loop does */
	Int removed[ 5 ];
	removed[ 0 ] = expected.front();
	removed[ 1 ] = expected.back();
	removed[ 2 ] = expected[ expected.size() / 2 ];
	removed[ 3 ] = expected[ 1 ];
	removed[ 4 ] = expected[ expected.size() - 2 ];
	for( i = 0; i < 5; i++ )
	{
		list = cells[ removed[ i ] ].removeFromOpenList( list );
		expected.erase( std::find( expected.begin(), expected.end(), removed[ i ] ) );
	}
	CHECK(openListOrderMatches( list, expected ));

	for( i = 0; i < 5; i++ )
	{
		Int c = removed[ i ];
		UnsignedInt cost = 10 * (UnsignedInt)(i * 7 % 25);
		cells[ c ].setTotalCost( cost );
		std::vector<Int>::iterator it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= cost )
			++it;
		expected.insert( it, c );
		list = cells[ c ].putOnSortedOpenList( list );
	}
	CHECK(openListOrderMatches( list, expected ));

	/* emptying it one at a time from the tail end must not leave a stale tail behind */
	while( !expected.empty() )
	{
		Int c = expected.back();
		expected.pop_back();
		list = cells[ c ].removeFromOpenList( list );
	}
	CHECK(list == NULL);

	cells[ 0 ].setTotalCost( 100 );
	list = cells[ 0 ].putOnSortedOpenList( NULL );
	cells[ 1 ].setTotalCost( 50 );
	list = cells[ 1 ].putOnSortedOpenList( list );
	CHECK(list == &cells[ 1 ]);
	CHECK(list->getNextOpen() == &cells[ 0 ]);

	PathfindCell::releaseOpenList( list );
	for( i = 0; i < count; i++ )
		cells[ i ].releaseInfo();
	delete [] cells;
	theOpenTestCells = NULL;
	PathfindCellInfo::releaseCellInfos();
}


/* The open list is not walked any more, it is indexed by cost: a bucket per possible m_totalCost,
	 with a three level bit index over the occupied ones.  That index is the part that can be wrong
	 in ways the previous test would not notice, because its costs all live inside one 32 bit word of
	 the bottom level.  This one spreads them across the whole 16 bit key space, so finding the
	 nearest occupied bucket below a new cost has to climb the summaries and come back down, and it
	 empties buckets from both ends so the clear paths run too.

	 It also reproduces the one thing the game does that the index cannot see coming: raising a
	 cell's cost while it is still linked into the list, and only then taking it off.  That is
	 findAttackPath's decrease-key, and it is why a cell remembers which bucket it went into. */
TEST(pathfind_open_list_bucket_index_finds_the_right_neighbour_across_the_whole_cost_range)
{
	CHECK(bootOnce());
	PathfindCellInfo::allocateCellInfos();

	const Int count = 300;
	const Int spares = 8;												// held back, to be inserted at costs that were vacated
	PathfindCell *cells = MSGNEW("PathfindCellInfo") PathfindCell[ count + spares ];
	theOpenTestCells = cells;

	std::vector<Int> expected;
	PathfindCell *list = NULL;
	Int seed = 987654321;
	Int i;

	/* costs spread over the whole UnsignedShort range, including both ends, with enough repeats to
		 keep the tie order under test as well */
	for( i = 0; i < count; i++ )
	{
		seed = seed * 1103515245 + 12345;
		UnsignedInt cost;
		switch( i % 10 )
		{
			case 0:  cost = 0; break;											// the very bottom bucket
			case 1:  cost = 65535; break;									// the very top one
			case 2:  cost = 31; break;										// last bit of word 0
			case 3:  cost = 32; break;										// first bit of word 1
			case 4:  cost = 1023; break;									// last bucket under summary word 0
			case 5:  cost = 1024; break;									// first bucket over it
			case 6:  cost = 32767; break;									// last bucket under the top word 0
			case 7:  cost = 32768; break;									// first bucket in top word 1
			default: cost = (UnsignedInt)((seed >> 8) & 0xFFFF); break;
		}
		ICoord2D pos;
		pos.x = (UnsignedShort)(i % 64);
		pos.y = (UnsignedShort)(i / 64);
		CHECK(cells[ i ].allocateInfo( pos ));
		cells[ i ].setTotalCost( cost );

		std::vector<Int>::iterator it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= cost )
			++it;
		expected.insert( it, i );

		list = cells[ i ].putOnSortedOpenList( list );
	}
	CHECK(openListOrderMatches( list, expected ));

	/* the game's decrease-key, in the game's order: change the cost first, take the cell off
		 second.  If the removal is filed under the new cost instead of the bucket the cell is
		 actually in, the old bucket is left pointing at a cell that is no longer on the list and
		 the next insert at that cost links itself into nothing. */
	Int rekeyed[ 6 ];
	rekeyed[ 0 ] = expected.front();
	rekeyed[ 1 ] = expected.back();
	rekeyed[ 2 ] = expected[ expected.size() / 3 ];
	rekeyed[ 3 ] = expected[ expected.size() / 2 ];
	rekeyed[ 4 ] = expected[ 1 ];
	rekeyed[ 5 ] = expected[ expected.size() - 2 ];
	for( i = 0; i < 6; i++ )
	{
		Int c = rekeyed[ i ];
		UnsignedInt was = cells[ c ].getTotalCost();
		UnsignedInt cost = (UnsignedInt)(i * 9973) & 0xFFFF;
		cells[ c ].setTotalCost( cost );										// cost changes while still linked
		list = cells[ c ].removeFromOpenList( list );
		expected.erase( std::find( expected.begin(), expected.end(), c ) );

		std::vector<Int>::iterator it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= cost )
			++it;
		expected.insert( it, c );
		list = cells[ c ].putOnSortedOpenList( list );
		CHECK(openListOrderMatches( list, expected ));

		/* and now a fresh cell at the cost the rekeyed one used to have.  This is what makes the
			 stale bucket visible: if the removal was filed under the new cost, the old cost still
			 points at the rekeyed cell, which has since moved somewhere else entirely, and this
			 insert lands next to it instead of where its own cost belongs. */
		Int spare = count + i;
		ICoord2D spos;
		spos.x = (UnsignedShort)(spare % 64);
		spos.y = (UnsignedShort)(spare / 64);
		CHECK(cells[ spare ].allocateInfo( spos ));
		cells[ spare ].setTotalCost( was );
		it = expected.begin();
		while( it != expected.end() && cells[ *it ].getTotalCost() <= was )
			++it;
		expected.insert( it, spare );
		list = cells[ spare ].putOnSortedOpenList( list );
		CHECK(openListOrderMatches( list, expected ));
	}

	/* empty it from the cheap end - an A* pop loop - and check after every pop, because a bucket
		 left marked occupied after its last cell leaves is exactly the failure that would send the
		 next insert to a freed cell */
	while( expected.size() > 100 )
	{
		Int c = expected.front();
		expected.erase( expected.begin() );
		list = cells[ c ].removeFromOpenList( list );
		CHECK(openListOrderMatches( list, expected ));
	}

	/* and the rest from the dear end, which drains the top summary word */
	while( !expected.empty() )
	{
		Int c = expected.back();
		expected.pop_back();
		list = cells[ c ].removeFromOpenList( list );
		CHECK(openListOrderMatches( list, expected ));
	}
	CHECK(list == NULL);

	/* an emptied index must be empty: two cells at costs that were both heavily used above, in
		 the wrong order, still come out sorted */
	cells[ 0 ].setTotalCost( 65535 );
	list = cells[ 0 ].putOnSortedOpenList( NULL );
	cells[ 1 ].setTotalCost( 0 );
	list = cells[ 1 ].putOnSortedOpenList( list );
	cells[ 2 ].setTotalCost( 32768 );
	list = cells[ 2 ].putOnSortedOpenList( list );
	CHECK(list == &cells[ 1 ]);
	CHECK(list->getNextOpen() == &cells[ 2 ]);
	CHECK(list->getNextOpen()->getNextOpen() == &cells[ 0 ]);
	CHECK(list->getNextOpen()->getNextOpen()->getNextOpen() == NULL);

	PathfindCell::releaseOpenList( list );
	for( i = 0; i < count + spares; i++ )
		cells[ i ].releaseInfo();
	delete [] cells;
	theOpenTestCells = NULL;
	PathfindCellInfo::releaseCellInfos();
}


//-------------------------------------------------------------------------------------------------
// Zone equivalency sets (pathfindZoneFind / pathfindZoneUnion / pathfindZoneFlatten).
//
// The pathfinder merges zone ids with a union-find over the equivalency array instead of EA's
// relabel-the-whole-array loop.  The contract the rest of the pathfinder relies on is exact, not
// approximate: after flattening, array[i] must be the *smallest* id in i's set, which is what the
// relabelling version produced.  This drives a long random merge sequence through both the real
// implementation and a straight-line reference relabeller and requires the two arrays to match
// entry for entry.
//-------------------------------------------------------------------------------------------------

// EA's original merge: canonicalize both, keep the lower, relabel every entry.
static void referenceResolveZones( zoneStorageType *zones, Int srcZone, Int targetZone, Int numZones )
{
	srcZone = zones[srcZone];
	targetZone = zones[targetZone];
	zoneStorageType finalZone = (targetZone < srcZone) ? zones[targetZone] : zones[srcZone];
	for (Int i = 0; i < numZones; i++) {
		zoneStorageType ze = zones[i];
		if (ze == targetZone || ze == srcZone) {
			zones[i] = finalZone;
		}
	}
}

TEST(pathfind_zone_union_find_matches_the_relabelling_merge_it_replaced)
{
	enum { NUM_ZONES = 500, NUM_MERGES = 4000 };
	static zoneStorageType real[ NUM_ZONES ];
	static zoneStorageType ref[ NUM_ZONES ];
	Int i;
	for (i = 0; i < NUM_ZONES; i++) {
		real[ i ] = (zoneStorageType)i;
		ref[ i ] = (zoneStorageType)i;
	}

	// A fixed LCG, so a failure is reproducible.
	UnsignedInt seed = 12345;
	Int mismatches = 0;
	Int merges;
	for (merges = 0; merges < NUM_MERGES; merges++) {
		seed = seed * 1103515245 + 12345;
		Int a = 1 + (Int)((seed >> 16) % (NUM_ZONES - 1));
		seed = seed * 1103515245 + 12345;
		Int b = 1 + (Int)((seed >> 16) % (NUM_ZONES - 1));

		pathfindZoneUnion( real, a, b );
		referenceResolveZones( ref, a, b, NUM_ZONES );

		// Reading a set representative mid-sequence must agree too - the merge loops in
		// calculateZones compare representatives between merges to decide what to merge next.
		if (pathfindZoneFind( real, a ) != ref[ a ]) mismatches++;
		if (pathfindZoneFind( real, b ) != ref[ b ]) mismatches++;
	}
	CHECK_EQ( mismatches, 0 );

	pathfindZoneFlatten( real, NUM_ZONES );
	Int diffs = 0;
	for (i = 0; i < NUM_ZONES; i++) {
		if (real[ i ] != ref[ i ]) diffs++;
	}
	CHECK_EQ( diffs, 0 );

	// The whole array must be flat afterwards: array[array[i]] == array[i].
	Int notFlat = 0;
	for (i = 0; i < NUM_ZONES; i++) {
		if (real[ real[ i ] ] != real[ i ]) notFlat++;
	}
	CHECK_EQ( notFlat, 0 );

	// And every representative must be the minimum id of its set.
	Int notMinimum = 0;
	for (i = 0; i < NUM_ZONES; i++) {
		if (real[ i ] > (zoneStorageType)i) notMinimum++;
	}
	CHECK_EQ( notMinimum, 0 );
}

TEST(pathfind_zone_flatten_collapses_a_deep_chain_in_one_pass)
{
	// pathfindZoneFlatten is a single ascending pass, which is only correct because every link
	// points from a higher id to a lower one.  Build the deepest chain the union can produce -
	// 1 <- 2 <- 3 <- ... - by merging in an order that never gives path compression a chance.
	enum { NUM_ZONES = 64 };
	zoneStorageType zones[ NUM_ZONES ];
	Int i;
	for (i = 0; i < NUM_ZONES; i++) {
		zones[ i ] = (zoneStorageType)i;
	}
	for (i = NUM_ZONES - 1; i >= 2; i--) {
		zones[ i ] = (zoneStorageType)(i - 1);		// hand-built chain, no compression
	}
	CHECK_EQ( (Int)zones[ NUM_ZONES - 1 ], NUM_ZONES - 2 );

	pathfindZoneFlatten( zones, NUM_ZONES );
	Int notOne = 0;
	for (i = 1; i < NUM_ZONES; i++) {
		if (zones[ i ] != 1) notOne++;
	}
	CHECK_EQ( notOne, 0 );
	CHECK_EQ( (Int)zones[ 0 ], 0 );
}

/* The two bits of arithmetic behind the command bar's new numbers.  Declared here rather than by
   including ControlBar.h for them: they are free functions in the ControlBar sources, and the
   header carries the class, not these. */
extern Int ControlBar_secondsFromFrames( Real frames );
extern Int ControlBar_secondsFromFramesAt( Real frames, Int logicFps );
extern Int ControlBar_experiencePercent( Int currentExp, Int levelExp, Int nextLevelExp );
extern Int ControlBar_purchaseScienceRank( Int column, Int depth, Int *indexOut );

TEST(controlbar_promotion_columns_map_to_the_screens_three_rows)
{
	Int index = -1;

	/* GeneralsExpPoints.wnd puts the 1 point row across the top: one button per column, and the
	   fifth column - the one only the 3 point block is wide enough to have - has none */
	CHECK_EQ( ControlBar_purchaseScienceRank( 0, 0, &index ), 1 );
	CHECK_EQ( index, 0 );
	CHECK_EQ( ControlBar_purchaseScienceRank( 3, 0, &index ), 1 );
	CHECK_EQ( index, 3 );
	CHECK_EQ( ControlBar_purchaseScienceRank( 4, 0, &index ), 0 );
	CHECK_EQ( index, -1 );

	/* the 3 point block is filled down each column in turn - Rank3Number0..2 are the first
	   column, 3..5 the second - which is the whole reason a column is a chain and not a row */
	CHECK_EQ( ControlBar_purchaseScienceRank( 0, 1, &index ), 3 );
	CHECK_EQ( index, 0 );
	CHECK_EQ( ControlBar_purchaseScienceRank( 0, 3, &index ), 3 );
	CHECK_EQ( index, 2 );
	CHECK_EQ( ControlBar_purchaseScienceRank( 1, 1, &index ), 3 );
	CHECK_EQ( index, 3 );
	CHECK_EQ( ControlBar_purchaseScienceRank( 4, 3, &index ), 3 );
	CHECK_EQ( index, MAX_PURCHASE_SCIENCE_RANK_3 - 1 );

	/* the 5 point row along the bottom, four wide like the top one */
	CHECK_EQ( ControlBar_purchaseScienceRank( 0, PURCHASE_SCIENCE_COLUMN_DEPTH - 1, &index ), 8 );
	CHECK_EQ( index, 0 );
	CHECK_EQ( ControlBar_purchaseScienceRank( 4, PURCHASE_SCIENCE_COLUMN_DEPTH - 1, &index ), 0 );

	/* every column reaches exactly the depths the layout has, and nothing past them */
	CHECK_EQ( ControlBar_purchaseScienceRank( 0, PURCHASE_SCIENCE_COLUMN_DEPTH, &index ), 0 );
	CHECK_EQ( ControlBar_purchaseScienceRank( PURCHASE_SCIENCE_COLUMNS, 0, &index ), 0 );
	CHECK_EQ( ControlBar_purchaseScienceRank( -1, 0, &index ), 0 );
	CHECK_EQ( ControlBar_purchaseScienceRank( 0, -1, &index ), 0 );

	/* no two cells name the same button: the mapping is a bijection onto the 23 windows */
	Int seen1 = 0, seen3 = 0, seen8 = 0;
	for( Int column = 0; column < PURCHASE_SCIENCE_COLUMNS; column++ )
	{
		for( Int depth = 0; depth < PURCHASE_SCIENCE_COLUMN_DEPTH; depth++ )
		{
			Int slot = -1;
			switch( ControlBar_purchaseScienceRank( column, depth, &slot ) )
			{
				case 1:	CHECK( (seen1 & (1 << slot)) == 0 ); seen1 |= (1 << slot); break;
				case 3:	CHECK( (seen3 & (1 << slot)) == 0 ); seen3 |= (1 << slot); break;
				case 8:	CHECK( (seen8 & (1 << slot)) == 0 ); seen8 |= (1 << slot); break;
			}
		}
	}
	CHECK_EQ( seen1, (1 << MAX_PURCHASE_SCIENCE_RANK_1) - 1 );
	CHECK_EQ( seen3, (1 << MAX_PURCHASE_SCIENCE_RANK_3) - 1 );
	CHECK_EQ( seen8, (1 << MAX_PURCHASE_SCIENCE_RANK_8) - 1 );
}

TEST(controlbar_seconds_round_up_and_never_reach_zero_early)
{
	/* nothing left is the only thing that reads as no number at all */
	CHECK_EQ( ControlBar_secondsFromFrames( 0.0f ), 0 );
	CHECK_EQ( ControlBar_secondsFromFrames( -5.0f ), 0 );

	/* a whole second is a whole second, at LOGICFRAMES_PER_SECOND to the second */
	CHECK_EQ( ControlBar_secondsFromFrames( (Real)LOGICFRAMES_PER_SECOND ), 1 );
	CHECK_EQ( ControlBar_secondsFromFrames( (Real)LOGICFRAMES_PER_SECOND * 6.0f ), 6 );
	CHECK_EQ( ControlBar_secondsFromFrames( (Real)LOGICFRAMES_PER_SECOND * 120.0f ), 120 );

	/* and anything in between rounds up: 1.1s must not print as 1s and then sit there */
	CHECK_EQ( ControlBar_secondsFromFrames( (Real)LOGICFRAMES_PER_SECOND + 1.0f ), 2 );

	/* the one that matters - a single frame of work left still says 1s, because a button with
	   work left on it reading 0s looks finished when it is not */
	CHECK_EQ( ControlBar_secondsFromFrames( 1.0f ), 1 );
}

TEST(controlbar_seconds_are_real_seconds_at_the_current_game_speed)
{
	/* The number on a build button is a promise about how long you will be waiting, and the logic
	   rate is a knob in this fork (the game speed keys move it between 5 and 200). The same 600
	   frames of work is 20 seconds at the nominal rate and 10 at double speed. */
	const Real frames = 600.0f;
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, LOGICFRAMES_PER_SECOND ), 20 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, LOGICFRAMES_PER_SECOND * 2 ), 10 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, LOGICFRAMES_PER_SECOND / 2 ), 40 );

	/* A rate of zero is the engine before it has one, not a division to attempt: fall back to the
	   nominal rate. This is also the path the whole suite runs on - the test binary has no
	   GameEngine, so ControlBar_secondsFromFrames() above resolves the rate to 0. */
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, 0 ), 20 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( frames, -5 ), 20 );

	/* and the round-up and the never-zero floor hold at any rate */
	CHECK_EQ( ControlBar_secondsFromFramesAt( 61.0f, 60 ), 2 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( 1.0f, 200 ), 1 );
	CHECK_EQ( ControlBar_secondsFromFramesAt( 0.0f, 200 ), 0 );
}

TEST(controlbar_experience_percent_fills_the_rank_and_clamps)
{
	/* a fresh unit at the bottom of its rank */
	CHECK_EQ( ControlBar_experiencePercent( 0, 0, 100 ), 0 );
	CHECK_EQ( ControlBar_experiencePercent( 50, 0, 100 ), 50 );
	CHECK_EQ( ControlBar_experiencePercent( 99, 0, 100 ), 99 );

	/* the window is between two thresholds, not from zero: half way from 100 to 300 is 50% */
	CHECK_EQ( ControlBar_experiencePercent( 200, 100, 300 ), 50 );

	/* experience past the next threshold (the level has not been applied yet) pins the bar full
	   rather than overflowing it, and a sink that took points away pins it empty */
	CHECK_EQ( ControlBar_experiencePercent( 400, 100, 300 ), 100 );
	CHECK_EQ( ControlBar_experiencePercent( 50, 100, 300 ), 0 );

	/* no next rank to fill towards - top rank, or a template with no thresholds at all - is the
	   "draw no bar" answer, and must not be a division by zero */
	CHECK_EQ( ControlBar_experiencePercent( 500, 300, 300 ), -1 );
	CHECK_EQ( ControlBar_experiencePercent( 0, 0, 0 ), -1 );
	CHECK_EQ( ControlBar_experiencePercent( 10, 300, 100 ), -1 );
}

/* ---------------------------------------------------------------------------------------------
   Multiplayer pacing.

   FrameMetrics feeds ConnectionManager::updateRunAhead the answer to "how fast can this machine
   advance the simulation", and that number becomes the input delay everyone in the room lives
   with. It used to be sampled from TheDisplay->getAverageFPS(), which was the same number as the
   logic rate only while EA's renderer was locked to the logic tick. This fork uncapped the
   renderer, so the two came apart and the run-ahead started being sized off a GPU measurement.
   --------------------------------------------------------------------------------------------- */
extern Real FrameMetrics_logicFpsSample( UnsignedInt frame, UnsignedInt windowStartFrame, time_t windowMS );

TEST(network_fps_metric_measures_logic_frames_not_rendered_ones)
{
	/* The case that broke: a weak GPU drawing 12fps while the CPU still steps logic 30 times a
	   second must report 30, because 30 is what the run-ahead has to cover. Nothing in the sample
	   can see the render rate - the only inputs are logic frame numbers and a wall-clock window. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1030, 1000, 1000 ), 30.0f, 0.001f );

	/* A machine that genuinely cannot keep up reports what it managed, so the run-ahead grows to
	   cover it. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1012, 1000, 1000 ), 12.0f, 0.001f );

	/* The window is whatever it turned out to be, not an assumed 1000ms: the sampler fires on the
	   first logic frame at or past a second, which on a stuttering machine can be well past it.
	   30 frames spread over 1500ms is 20fps, not 30. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1030, 1000, 1500 ), 20.0f, 0.001f );
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1030, 1000,  500 ), 60.0f, 0.001f );

	/* A window with no time in it is not a division to attempt. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1030, 1000, 0 ), 0.0f, 0.001f );

	/* A window with no frames in it is a real answer - the simulation did not advance at all -
	   and must read zero rather than wrapping the unsigned subtraction. */
	CHECK_NEAR( FrameMetrics_logicFpsSample( 1000, 1000, 1000 ), 0.0f, 0.001f );
}

/* ---------------------------------------------------------------------------------------------
   Adaptive retransmit. EA shipped a flat 2000ms retry, so on a fast link one lost command packet
   cost the whole room a two-second stall. Their own commented-out intent (average latency * 1.5)
   could not be switched on as written - the average is a rolling mean over an array that starts
   full of zeroes - so this is Jacobson/Karels instead, which carries a variance term and widens on
   a jittery link rather than hugging the mean.
   --------------------------------------------------------------------------------------------- */
extern void Connection_updateRetryTimeout( Real sampleMS, Real &srtt, Real &rttvar, time_t &retryMS );
extern time_t Connection_retryDelayFor( time_t baseRetryMS, Int numTimesSent );

TEST(connection_retry_timeout_follows_the_link_and_never_leaves_its_bounds)
{
	/* A steady 40ms LAN link: the first sample seeds srtt=40, rttvar=20, so the timeout starts at
	   40+80=120 and is lifted to the 150ms floor. Feeding the same number repeatedly drives the
	   deviation to zero, so it settles on the floor - which is the whole point: a lost packet on
	   this link is retried in 150ms, not 2000. */
	Real srtt = -1.0f, rttvar = 0.0f;
	time_t retry = CONNECTION_MAX_RETRY_TIME;
	Connection_updateRetryTimeout( 40.0f, srtt, rttvar, retry );
	CHECK_EQ( (Int)retry, CONNECTION_MIN_RETRY_TIME );
	for( Int i = 0; i < 100; ++i )
		Connection_updateRetryTimeout( 40.0f, srtt, rttvar, retry );
	CHECK_NEAR( srtt, 40.0f, 0.5f );
	CHECK( rttvar < 1.0f );
	CHECK_EQ( (Int)retry, CONNECTION_MIN_RETRY_TIME );

	/* A steady 300ms link settles above the floor, at about the mean, because the deviation
	   collapses. It must not sit at EA's ceiling. */
	srtt = -1.0f; rttvar = 0.0f; retry = CONNECTION_MAX_RETRY_TIME;
	for( Int i = 0; i < 200; ++i )
		Connection_updateRetryTimeout( 300.0f, srtt, rttvar, retry );
	CHECK( retry > CONNECTION_MIN_RETRY_TIME );
	CHECK( retry < CONNECTION_MAX_RETRY_TIME );
	CHECK_NEAR( (Real)retry, 300.0f, 30.0f );

	/* The reason the variance term exists: a link whose mean is 100ms but which swings between
	   40 and 160 must be given more room than a rock-steady 100ms link, or every swing is read as
	   a loss and retransmitted. */
	Real steadySrtt = -1.0f, steadyVar = 0.0f;
	time_t steadyRetry = CONNECTION_MAX_RETRY_TIME;
	Real jumpySrtt = -1.0f, jumpyVar = 0.0f;
	time_t jumpyRetry = CONNECTION_MAX_RETRY_TIME;
	for( Int i = 0; i < 200; ++i )
	{
		Connection_updateRetryTimeout( 100.0f, steadySrtt, steadyVar, steadyRetry );
		Connection_updateRetryTimeout( (i & 1) ? 160.0f : 40.0f, jumpySrtt, jumpyVar, jumpyRetry );
	}
	CHECK( jumpyRetry > steadyRetry );

	/* Neither bound can be crossed: a satellite link is capped at EA's old constant so nobody is
	   served worse than the shipped behaviour, and a zero-latency loopback still waits the floor. */
	srtt = -1.0f; rttvar = 0.0f; retry = 0;
	for( Int i = 0; i < 50; ++i )
		Connection_updateRetryTimeout( 5000.0f, srtt, rttvar, retry );
	CHECK_EQ( (Int)retry, CONNECTION_MAX_RETRY_TIME );

	srtt = -1.0f; rttvar = 0.0f; retry = 0;
	for( Int i = 0; i < 50; ++i )
		Connection_updateRetryTimeout( 0.0f, srtt, rttvar, retry );
	CHECK_EQ( (Int)retry, CONNECTION_MIN_RETRY_TIME );
}

TEST(connection_retry_backs_off_when_a_command_keeps_going_unacked)
{
	/* A command that has gone out once waits the connection's plain timeout. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 1 ), 200 );

	/* Each further attempt doubles, so a link that is genuinely down is not hammered at the
	   floor rate for as long as the disconnect timer runs. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 2 ), 400 );
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 3 ), 800 );

	/* ...but the doubling stops there.  This is a lockstep game: the command being retried is one
	   the whole room is stopped on, so this delay is the length of the freeze, and it used to reach
	   1.2 and then 2 seconds after three and four losses of the same command.  Quartering the retry
	   rate is all the protection a dead link needs from here; deciding a link is dead belongs to the
	   disconnect manager. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 4 ), 800 );
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 5 ), 800 );
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 50 ), 800 );

	/* The freeze a command can cost on a link fast enough to sit on the retry floor, however many
	   times in a row it is lost. */
	CHECK_EQ( (Int)Connection_retryDelayFor( CONNECTION_MIN_RETRY_TIME, 50 ),
						CONNECTION_MIN_RETRY_TIME << CONNECTION_MAX_RETRY_BACKOFF_SHIFTS );
	CHECK( (Connection_retryDelayFor( CONNECTION_MIN_RETRY_TIME, 50 ) * 1000) <
				 (CONNECTION_MAX_RETRY_TIME * 1000) );

	/* A slow link still reaches the ceiling honestly - its own round trip put it there, not the
	   backoff - and the ceiling still holds. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 600, 3 ), 2000 );
	CHECK_EQ( (Int)Connection_retryDelayFor( CONNECTION_MAX_RETRY_TIME, 3 ), CONNECTION_MAX_RETRY_TIME );

	/* Monotonic, and never below the connection's own timeout. */
	for( Int sent = 0; sent < 20; ++sent )
	{
		time_t here = Connection_retryDelayFor( 200, sent );
		time_t next = Connection_retryDelayFor( 200, sent + 1 );
		CHECK( here >= 200 );
		CHECK( next >= here );
	}

	/* A command that has never been sent is not a retry, and must not read as a negative shift. */
	CHECK_EQ( (Int)Connection_retryDelayFor( 200, 0 ), 200 );
}

// ------------------------------------------------------------------------------------------------
// CRCSnapshotRing - the evidence a mismatch report is missing.  A mismatch is detected several
// frames after the frame it happened on, so the ring has to still hold that frame when asked, and
// it has to be able to name it from nothing but a CRC value.
// ------------------------------------------------------------------------------------------------

static void fillSnapshot( CRCSnapshotRing &ring, UnsignedInt frame, UnsignedInt totalCRC, Int numObjects )
{
	ring.beginSnapshot( frame );
	for( Int i = 0; i < numObjects; ++i )
		ring.addObject( 100 + i, 0xAB000000 + frame * 1000 + i, (Real)i, (Real)(i * 2), 0.0f, 50.0f + i );
	ring.endSnapshot( totalCRC, 0xF00D0000 + frame );
}

TEST(crcsnapshotring_names_the_diverging_frame_from_a_crc_value)
{
	CRCSnapshotRing ring;
	CHECK_EQ( ring.getNewestSlot(), -1 );
	CHECK_EQ( ring.getNumSnapshots(), 0 );

	fillSnapshot( ring, 30, 0x11111111, 3 );
	fillSnapshot( ring, 60, 0x22222222, 4 );
	fillSnapshot( ring, 90, 0x33333333, 5 );

	CHECK_EQ( ring.getNumSnapshots(), 3 );

	// the mismatch is reported on a later frame; all we have to go on is our own CRC for that frame
	Int slot = ring.findSlotByCRC( 0x22222222 );
	CHECK( slot >= 0 );

	const CRCSnapshot *snap = ring.getSlot( slot );
	CHECK( snap != NULL );
	if( snap == NULL )
		return;
	CHECK_EQ( (Int)snap->m_frame, 60 );
	CHECK_EQ( (Int)snap->m_objects.size(), 4 );
	CHECK_EQ( (Int)snap->m_randomSeed, (Int)(0xF00D0000 + 60) );

	// the per object running CRCs are what a diff between two players' dumps compares
	CHECK_EQ( (Int)snap->m_objects[0].m_id, 100 );
	CHECK_EQ( (Int)snap->m_objects[3].m_id, 103 );
	CHECK_EQ( (Int)snap->m_objects[2].m_runningCRC, (Int)(0xAB000000 + 60 * 1000 + 2) );
	CHECK_NEAR( snap->m_objects[3].m_health, 53.0f, 0.001f );

	// a CRC nobody recorded names no frame at all, rather than the wrong one
	CHECK_EQ( ring.findSlotByCRC( 0x44444444 ), -1 );
}

TEST(crcsnapshotring_still_holds_the_frame_a_late_mismatch_report_asks_for)
{
	CRCSnapshotRing ring;

	// the run ahead can be 64 frames; at the default CRC interval the ring has to outlive that
	for( Int i = 1; i <= CRC_SNAPSHOT_RING_SIZE; ++i )
		fillSnapshot( ring, i * 10, 0x1000 + i, 2 );

	CHECK_EQ( ring.getNumSnapshots(), CRC_SNAPSHOT_RING_SIZE );
	CHECK( ring.findSlotByCRC( 0x1000 + 1 ) >= 0 );

	// one more frame pushes the oldest out, and nothing else
	fillSnapshot( ring, (CRC_SNAPSHOT_RING_SIZE + 1) * 10, 0x2000, 2 );
	CHECK_EQ( ring.getNumSnapshots(), CRC_SNAPSHOT_RING_SIZE );
	CHECK_EQ( ring.findSlotByCRC( 0x1000 + 1 ), -1 );
	CHECK( ring.findSlotByCRC( 0x1000 + 2 ) >= 0 );
	CHECK( ring.findSlotByCRC( 0x2000 ) >= 0 );

	// newest first ordering survives the wrap
	const CRCSnapshot *newest = ring.getSlot( ring.getNthNewestSlot( 0 ) );
	const CRCSnapshot *older  = ring.getSlot( ring.getNthNewestSlot( 1 ) );
	CHECK( newest != NULL && older != NULL );
	if( newest == NULL || older == NULL )
		return;
	CHECK_EQ( (Int)newest->m_frame, (CRC_SNAPSHOT_RING_SIZE + 1) * 10 );
	CHECK_EQ( (Int)older->m_frame, CRC_SNAPSHOT_RING_SIZE * 10 );
	CHECK_EQ( ring.getNthNewestSlot( CRC_SNAPSHOT_RING_SIZE ), -1 );
	CHECK_EQ( ring.getNthNewestSlot( -1 ), -1 );
}

TEST(crcsnapshotring_never_hands_back_a_half_written_frame)
{
	CRCSnapshotRing ring;
	fillSnapshot( ring, 30, 0x11111111, 3 );

	// a CRC pass that starts but does not finish must not become the newest snapshot
	ring.beginSnapshot( 60 );
	ring.addObject( 100, 0xDEADBEEF, 0.0f, 0.0f, 0.0f, 1.0f );

	CHECK_EQ( ring.getNumSnapshots(), 1 );
	const CRCSnapshot *snap = ring.getSlot( ring.getNewestSlot() );
	CHECK( snap != NULL );
	if( snap == NULL )
		return;
	CHECK_EQ( (Int)snap->m_frame, 30 );
	CHECK_EQ( ring.findSlotByCRC( 0x11111111 ), ring.getNewestSlot() );

	ring.clear();
	CHECK_EQ( ring.getNumSnapshots(), 0 );
	CHECK_EQ( ring.getNewestSlot(), -1 );
	CHECK_EQ( ring.findSlotByCRC( 0x11111111 ), -1 );

	// addObject with no snapshot open is a no-op, not a write through a bad index
	ring.addObject( 1, 2, 0.0f, 0.0f, 0.0f, 0.0f );
	CHECK_EQ( ring.getNumSnapshots(), 0 );
}

/* --------------------------------------------------------------------------------------------
	 A lockstep game only stays in step if both machines feed the same numbers into it, and the
	 join request is the last moment at which that is a refused join instead of a desynced match.
	 EA wrote the LAN check and left it commented out; it is back, and this pins what it decides. */

TEST(gamedatamatch_lets_identical_data_through_and_stops_everything_else)
{
	// same build, same INI set: the only case that may start a game
	CHECK_EQ( compareGameData( 0xAABBCCDD, 0x11223344, 0xAABBCCDD, 0x11223344 ), GAMEDATA_MATCHES );

	// a different INI set is the usual case, and the one a player can go and fix
	CHECK_EQ( compareGameData( 0xAABBCCDD, 0x11223345, 0xAABBCCDD, 0x11223344 ), GAMEDATA_INI_DIFFERS );

	// a different build reads the same INI files into different code
	CHECK_EQ( compareGameData( 0xAABBCCDE, 0x11223344, 0xAABBCCDD, 0x11223344 ), GAMEDATA_EXE_DIFFERS );

	// one bit is a mismatch: this is a data check, not a similarity score
	CHECK_EQ( compareGameData( 0x11223344, 0x00000001, 0x11223344, 0x00000003 ), GAMEDATA_INI_DIFFERS );
}

TEST(gamedatamatch_blames_the_executable_when_both_differ)
{
	/* Both CRCs differ.  Saying "different INI set" would send the player off to compare data
		 files that their build would read differently anyway. */
	CHECK_EQ( compareGameData( 1, 2, 3, 4 ), GAMEDATA_EXE_DIFFERS );
}

TEST(gamedatamatch_refuses_a_machine_that_reports_no_data_at_all)
{
	/* Nothing computes zero - the executable CRC always folds in the version number and the INI
		 CRC always folds in megabytes of text.  Zero means the other machine never checked. */
	CHECK_EQ( compareGameData( 0, 0x11223344, 0xAABBCCDD, 0x11223344 ), GAMEDATA_UNKNOWN );
	CHECK_EQ( compareGameData( 0xAABBCCDD, 0, 0xAABBCCDD, 0x11223344 ), GAMEDATA_UNKNOWN );

	// and it outranks the CRCs that do match, rather than being reported as a plain match
	CHECK_EQ( compareGameData( 0, 0, 0, 0 ), GAMEDATA_UNKNOWN );

	// every result names itself for the log
	CHECK_STR( gameDataMatchName( GAMEDATA_MATCHES ), "same data" );
	CHECK_STR( gameDataMatchName( GAMEDATA_EXE_DIFFERS ), "different executable or multiplayer scripts" );
	CHECK_STR( gameDataMatchName( GAMEDATA_INI_DIFFERS ), "different INI set" );
	CHECK_STR( gameDataMatchName( GAMEDATA_UNKNOWN ), "data not reported" );
}

/* --------------------------------------------------------------------------------------------
	 Two machines only compute the same floats if the FPU control word says the same thing on both.
	 Nothing in the process guarantees that - Direct3D sets it when it creates a device, and any DLL
	 in the process can set it and never put it back - so GameLogic::update re-asserts it at the top
	 of every logic frame.  These pin what "asserts it" means. */

TEST(setfpmode_pins_the_control_word_from_whatever_it_finds)
{
	UnsignedInt entry = _controlfp( 0, 0 );		// leave the process the way we found it

	// the mode the simulation runs in: 24-bit precision, round to nearest
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );
	CHECK_EQ( getFPMode() & _MCW_PC, (UnsignedInt)(_PC_24 & _MCW_PC) );
	CHECK_EQ( getFPMode() & _MCW_RC, (UnsignedInt)(_RC_NEAR & _MCW_RC) );

	// what a driver that grabbed the FPU and never gave it back looks like
	_controlfp( _PC_64 | _RC_CHOP, _MCW_PC | _MCW_RC );
	CHECK_NE( getFPMode(), expectedFPMode() );
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );

	// and the other direction, so this is not just "setFPMode lowers the precision"
	_controlfp( _PC_53 | _RC_UP, _MCW_PC | _MCW_RC );
	CHECK_NE( getFPMode(), expectedFPMode() );
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );

	// it is idempotent - a second logic frame does not move it
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );

	_controlfp( entry, _MCW_PC | _MCW_RC );
}

TEST(setfpmode_leaves_the_exception_mask_in_a_known_state)
{
	UnsignedInt entry = _controlfp( 0, 0 );

	/* setFPMode writes only the precision and rounding fields, but it calls _fpreset() first, so
		 the rest of the word - the exception masks above all - lands in the same known state on
		 every machine rather than wherever the last DLL left it.  An unmasked invalid-operation
		 exception on one machine and a masked one on another is a crash on one side and a quiet NaN
		 on the other. */
	_controlfp( 0, _MCW_EM );		// unmask everything: the state a debugger or a bad DLL can leave
	setFPMode();
	CHECK_EQ( _controlfp( 0, 0 ) & _MCW_EM, (UnsignedInt)_MCW_EM );

	// and the x87 stack is reset with it, so an __asm block that pushed and forgot to pop is
	// not carried into the next logic frame
	CHECK_EQ( getFPMode(), expectedFPMode() );

	_controlfp( entry, _MCW_PC | _MCW_RC | _MCW_EM );
}

// ---------------------------------------------------------------------------------------------
// The disconnect screen's decision (MULTIPLAYER 2.3).  DisconnectManager::update used to bring
// the screen up on stall duration alone, which cannot tell a slow game from a broken one.
// ---------------------------------------------------------------------------------------------

// the thresholds the shipped GlobalData defaults use, so the witnesses below describe the game
static const UnsignedInt DISCONNECT_MS = 8000;
static const UnsignedInt SILENCE_MS    = 12000;
static const UnsignedInt WEDGED_MS     = 20000;

static StallVerdict judge( UnsignedInt stallMS, UnsignedInt silenceMS )
{
	return judgeStall( stallMS, silenceMS, DISCONNECT_MS, SILENCE_MS, WEDGED_MS );
}

TEST(judgestall_a_short_stall_is_not_a_disconnect)
{
	// the common case: the game is waiting on a frame and everybody is still sending
	CHECK_EQ( (int)judge( 0, 0 ), (int)STALL_RUNNING );
	CHECK_EQ( (int)judge( 4000, 500 ), (int)STALL_RUNNING );
	CHECK_EQ( (int)judge( DISCONNECT_MS, 500 ), (int)STALL_RUNNING );	// boundary is exclusive

	// and a short stall stays running even if somebody has been quiet a while: below the
	// disconnect time we do not look at silence at all, because we are not stalled yet
	CHECK_EQ( (int)judge( 1000, 30000 ), (int)STALL_RUNNING );

	CHECK( !stallNeedsDisconnectScreen( STALL_RUNNING ) );
}

TEST(judgestall_a_long_stall_with_everyone_talking_is_only_slow)
{
	// this is the case EA got wrong: 5s of no frame progress, but packets are still arriving from
	// every player, so the game is behind and will catch up.  No screen, no vote, no drop.
	CHECK_EQ( (int)judge( DISCONNECT_MS + 1, 0 ), (int)STALL_WAITING );
	CHECK_EQ( (int)judge( 15000, 3000 ), (int)STALL_WAITING );
	CHECK_EQ( (int)judge( WEDGED_MS - 1, SILENCE_MS - 1 ), (int)STALL_WAITING );

	CHECK( !stallNeedsDisconnectScreen( STALL_WAITING ) );
}

TEST(judgestall_silence_from_a_player_is_a_disconnect)
{
	// stalled, and somebody has stopped sending entirely - that is what the screen is for
	CHECK_EQ( (int)judge( DISCONNECT_MS + 1, SILENCE_MS ), (int)STALL_SILENT );
	CHECK_EQ( (int)judge( 9000, 60000 ), (int)STALL_SILENT );

	CHECK( stallNeedsDisconnectScreen( STALL_SILENT ) );
}

TEST(judgestall_silence_shorter_than_the_keepalive_round_is_not_silence)
{
	// ConnectionManager::doKeepAlive walks one slot per second and resets at MAX_SLOTS, so a
	// player who is merely stalled themselves still only reaches us every 8s.  The silence
	// threshold has to sit above that or the fix would call a slow player a disconnected one.
	CHECK( SILENCE_MS > 8000 );
	CHECK_EQ( (int)judge( 19000, 8000 ), (int)STALL_WAITING );
	CHECK_EQ( (int)judge( 19000, 11999 ), (int)STALL_WAITING );
}

TEST(judgestall_a_stall_past_the_ceiling_gives_up_anyway)
{
	// packets are arriving but the frame has not moved in 20 seconds: whatever is wrong, it is
	// not going to fix itself, and refusing to ever show the screen would hang the game forever
	CHECK_EQ( (int)judge( WEDGED_MS, 0 ), (int)STALL_WEDGED );
	CHECK_EQ( (int)judge( 120000, 0 ), (int)STALL_WEDGED );

	CHECK( stallNeedsDisconnectScreen( STALL_WEDGED ) );

	// silence still wins over the ceiling: the report should name the real cause
	CHECK_EQ( (int)judge( 120000, SILENCE_MS ), (int)STALL_SILENT );
}

TEST(judgestall_the_verdict_is_monotonic_in_the_stall_time)
{
	// once the screen is warranted, waiting longer must never take it back away again
	Bool seenScreen = FALSE;
	for( UnsignedInt ms = 0; ms <= 40000; ms += 250 )
	{
		Bool needs = stallNeedsDisconnectScreen( judge( ms, 30000 ) );
		if( needs )
			seenScreen = TRUE;
		else
			CHECK( !seenScreen );
	}
	CHECK( seenScreen );

	// every verdict has a name for the log
	CHECK( strlen( stallVerdictName( STALL_RUNNING ) ) > 0 );
	CHECK( strlen( stallVerdictName( STALL_WAITING ) ) > 0 );
	CHECK( strlen( stallVerdictName( STALL_SILENT ) ) > 0 );
	CHECK( strlen( stallVerdictName( STALL_WEDGED ) ) > 0 );
}

// ---------------------------------------------------------------------------------------------
// The keep-alive round (MULTIPLAYER 2.4).  NetworkKeepAliveDelay was parsed, logged and never
// read; doKeepAlive counted whole seconds against MAX_SLOTS in two function statics instead.
// ---------------------------------------------------------------------------------------------

TEST(keepalive_the_round_is_bounded_whatever_the_ini_says)
{
	// the shipped GameData.ini asks for 20 s, which is exactly where consumer NAT mappings of the
	// era expired - refreshing the hole at the moment it closes is no refresh at all
	CHECK_EQ( keepAliveRoundMS( 20 ), (UnsignedInt)KEEPALIVE_MAX_ROUND_SECONDS * 1000 );
	CHECK_EQ( keepAliveRoundMS( 3600 ), (UnsignedInt)KEEPALIVE_MAX_ROUND_SECONDS * 1000 );

	// and a zero would be a divide by zero one function along
	CHECK_EQ( keepAliveRoundMS( 0 ), (UnsignedInt)KEEPALIVE_MIN_ROUND_SECONDS * 1000 );
	CHECK_EQ( keepAliveRoundMS( 1 ), (UnsignedInt)KEEPALIVE_MIN_ROUND_SECONDS * 1000 );

	// in between, the knob is the knob
	CHECK_EQ( keepAliveRoundMS( 4 ), (UnsignedInt)4000 );
	CHECK_EQ( keepAliveRoundMS( 8 ), (UnsignedInt)8000 );

	// the ceiling is what EA's counting loop actually produced, so the default behaviour is the
	// behaviour the game shipped with
	CHECK_EQ( (int)KEEPALIVE_MAX_ROUND_SECONDS, 8 );
	CHECK( KEEPALIVE_MIN_ROUND_SECONDS < KEEPALIVE_MAX_ROUND_SECONDS );
}

TEST(keepalive_slots_are_staggered_across_the_round)
{
	// eight slots over an eight second round: one per second, which is what EA's loop did
	CHECK_EQ( keepAliveSlotsDue( 0, 8000, 8 ), 1 );
	CHECK_EQ( keepAliveSlotsDue( 999, 8000, 8 ), 1 );
	CHECK_EQ( keepAliveSlotsDue( 1000, 8000, 8 ), 2 );
	CHECK_EQ( keepAliveSlotsDue( 6999, 8000, 8 ), 7 );
	CHECK_EQ( keepAliveSlotsDue( 7000, 8000, 8 ), 8 );

	// past the end of the round nothing more comes due - the round restarts instead
	CHECK_EQ( keepAliveSlotsDue( 8000, 8000, 8 ), 8 );
	CHECK_EQ( keepAliveSlotsDue( 100000, 8000, 8 ), 8 );

	// a shorter round packs the same eight into less time rather than dropping any
	CHECK_EQ( keepAliveSlotsDue( 0, 2000, 8 ), 1 );
	CHECK_EQ( keepAliveSlotsDue( 250, 2000, 8 ), 2 );
	CHECK_EQ( keepAliveSlotsDue( 1750, 2000, 8 ), 8 );
}

TEST(keepalive_the_count_never_goes_backwards_and_never_overruns)
{
	// doKeepAlive walks m_keepAliveNextSlot up to this number and indexes m_connections with it,
	// so a value outside [0, MAX_SLOTS] would be an out of bounds write in a network path
	Int last = 0;
	for( UnsignedInt ms = 0; ms <= 20000; ms += 17 )
	{
		Int due = keepAliveSlotsDue( ms, 8000, 8 );
		CHECK( due >= last );
		CHECK( due >= 0 && due <= 8 );
		last = due;
	}
	CHECK_EQ( last, 8 );

	// degenerate inputs cannot produce an index either
	CHECK_EQ( keepAliveSlotsDue( 0, 8000, 0 ), 0 );
	CHECK_EQ( keepAliveSlotsDue( 0, 0, 8 ), 8 );			// round shorter than a slot: all at once
	CHECK_EQ( keepAliveSlotsDue( 0, 4, 8 ), 8 );
}

TEST(keepalive_every_player_gets_one_within_the_round)
{
	// the property that matters to a NAT: no slot waits longer than the round for its packet
	UnsignedInt roundMS = keepAliveRoundMS( 20 );		// what the shipped INI produces
	CHECK( !keepAliveRoundIsOver( roundMS - 1, roundMS ) );
	CHECK( keepAliveRoundIsOver( roundMS, roundMS ) );

	// the last slot is due strictly before the round ends, so it is never skipped by the restart
	CHECK_EQ( keepAliveSlotsDue( roundMS - 1, roundMS, 8 ), 8 );
	CHECK( roundMS <= 15000 );		// under the shortest NAT UDP timeouts seen in the wild
}

// ---------------------------------------------------------------------------------------------
// The synthetic link simulator (MULTIPLAYER 6).  The transport can hold packets back and throw
// some away, so a lossy or slow line can be reproduced without a second machine.  Both decisions
// are pure, so both can be pinned here - including the two things EA got wrong in them, which
// nobody could have noticed because the whole facility was compiled out of the shipping build.
// ---------------------------------------------------------------------------------------------

TEST(linksim_packet_loss_percentage_is_the_percentage)
{
	// the count of losing rolls over the hundred a GameClientRandomValue(1, 100) can produce is
	// the percentage itself.  EA rolled (0, 100) - a hundred and one outcomes - against the same
	// comparison, so every setting was one point too lossy...
	Int pct;
	for( pct = 0; pct <= 100; pct++ )
	{
		Int lost = 0;
		for( Int roll = 1; roll <= 100; roll++ )
			if (linkSimPacketIsLost( pct, roll ))
				lost++;
		CHECK_EQ( lost, pct );
	}

	// ...and the setting that says "do not drop anything" dropped one packet in a hundred and one
	CHECK( !linkSimPacketIsLost( 0, 1 ) );
	CHECK( linkSimPacketIsLost( 100, 100 ) );
	CHECK( linkSimPacketIsLost( 1, 1 ) );
	CHECK( !linkSimPacketIsLost( 1, 2 ) );
}

TEST(linksim_delivery_is_the_average_plus_the_jitter)
{
	// no modulation asked for: the delay is exactly what was asked for
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 0, 0, 0 ), 1040u );
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 0, 0, 15 ), 1055u );
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 0, 0, -15 ), 1025u );

	// an amplitude with no period is not a modulation, and must not become one
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 500, 0, 0 ), 1040u );

	// nothing is ever delivered before it arrived, however the jitter falls
	CHECK_EQ( linkSimDeliveryTime( 1000, 40, 0, 0, -400 ), 1000u );
	CHECK_EQ( linkSimDeliveryTime( 0, 0, 0, 0, -400 ), 0u );
}

TEST(linksim_the_modulation_has_the_period_it_says_it_has)
{
	// a period in milliseconds, read the way the field documents itself: zero phase at the start of
	// every period, the peak a quarter of the way in, the trough three quarters in.
	const Int period = 8000;
	const Int amp = 100;

	CHECK_EQ( linkSimDeliveryTime( 0, 200, amp, period, 0 ), 200u );					// sin 0
	CHECK_EQ( linkSimDeliveryTime( period/4, 200, amp, period, 0 ), (UnsignedInt)(period/4 + 300) );	// sin pi/2
	CHECK_EQ( linkSimDeliveryTime( period/2, 200, amp, period, 0 ), (UnsignedInt)(period/2 + 200) );	// sin pi
	CHECK_EQ( linkSimDeliveryTime( 3*period/4, 200, amp, period, 0 ), (UnsignedInt)(3*period/4 + 100) );	// sin 3pi/2

	// and it repeats: the same point of the next period gives the same delay
	CHECK_EQ( linkSimDeliveryTime( period, 200, amp, period, 0 ) - period, 200u );
	CHECK_EQ( linkSimDeliveryTime( period + period/4, 200, amp, period, 0 ) - (period + period/4), 300u );

	/* EA's own line was sin(now * period), which at any usable setting moves the argument by whole
		 radians every millisecond - a second noise source, not a modulation - and overflows an
		 UnsignedInt after a minute of uptime.  The property that pins the difference: over one
		 period the delay is a single smooth hump, so consecutive milliseconds differ by almost
		 nothing.  Under EA's reading they differ by the whole amplitude. */
	UnsignedInt a = linkSimDeliveryTime( 1000000, 200, amp, period, 0 ) - 1000000;
	UnsignedInt b = linkSimDeliveryTime( 1000001, 200, amp, period, 0 ) - 1000001;
	CHECK( (a > b ? a - b : b - a) <= 1u );

	// still true where EA's version wrapped: uptimes past the point now * period overflows
	UnsignedInt late = 0xFFFFF000u;
	UnsignedInt c = linkSimDeliveryTime( late, 200, amp, period, 0 ) - late;
	UnsignedInt d = linkSimDeliveryTime( late + 1, 200, amp, period, 0 ) - (late + 1);
	CHECK( c >= 100u && c <= 300u );
	CHECK( (c > d ? c - d : d - c) <= 1u );
}

// ---------------------------------------------------------------------------------------------
// Whether the room actually disagreed about a frame (MULTIPLAYER 1.1, upstream #2796).
// ---------------------------------------------------------------------------------------------

TEST(crcagreement_everyone_still_playing_has_to_have_reported)
{
	Bool reported[4]  = { TRUE, TRUE, TRUE, FALSE };
	Bool connected[4] = { TRUE, TRUE, TRUE, TRUE };
	UnsignedInt crc[4] = { 0x1111u, 0x1111u, 0x1111u, 0u };

	// four connected players, three hashes: the fourth packet is not here yet, which is not a
	// mismatch.  Decide on the next CRC frame instead.
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 4 ), CRC_AGREEMENT_TOO_FEW );

	// once it arrives, and it agrees
	reported[3] = TRUE;
	crc[3] = 0x1111u;
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 4 ), CRC_AGREEMENT_AGREE );
}

TEST(crcagreement_a_player_who_has_left_does_not_get_a_vote)
{
	// slot 3 is gone, but their last hash - computed on a machine that was already tearing the
	// game down - arrived this frame.  EA compared it against everybody who is still playing and
	// ended the match on it.
	Bool reported[4]  = { TRUE, TRUE, TRUE, TRUE };
	Bool connected[4] = { TRUE, TRUE, TRUE, FALSE };
	UnsignedInt crc[4] = { 0x1111u, 0x1111u, 0x1111u, 0xDEADBEEFu };

	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 3 ), CRC_AGREEMENT_AGREE );

	// the leaver is also not counted towards the quorum: three connected players, and the two
	// hashes that are here are not enough.
	reported[2] = FALSE;
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 3 ), CRC_AGREEMENT_TOO_FEW );
}

TEST(crcagreement_two_players_who_are_both_here_and_disagree_is_a_mismatch)
{
	Bool reported[4]  = { TRUE, TRUE, TRUE, TRUE };
	Bool connected[4] = { TRUE, TRUE, TRUE, TRUE };
	UnsignedInt crc[4] = { 0x1111u, 0x1111u, 0x2222u, 0x1111u };

	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 4 ), CRC_AGREEMENT_MISMATCH );

	// the disagreement is found wherever it sits, including against the first reporter
	crc[0] = 0x2222u; crc[1] = 0x1111u; crc[2] = 0x1111u; crc[3] = 0x1111u;
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 4 ), CRC_AGREEMENT_MISMATCH );

	// but only between players who are both still connected
	connected[0] = FALSE;
	CHECK_EQ( crcAgreement( reported, crc, connected, 4, 3 ), CRC_AGREEMENT_AGREE );
}

TEST(crcagreement_a_room_with_nobody_left_in_it_is_not_a_mismatch)
{
	Bool reported[2]  = { FALSE, FALSE };
	Bool connected[2] = { FALSE, FALSE };
	UnsignedInt crc[2] = { 0u, 0u };

	// nothing to compare, and nothing expected: agreement by default, never a mismatch.  EA read
	// begin() unconditionally here.
	CHECK_EQ( crcAgreement( reported, crc, connected, 2, 0 ), CRC_AGREEMENT_AGREE );

	// one player alone in the room agrees with themselves
	reported[0] = TRUE; connected[0] = TRUE; crc[0] = 0x99u;
	CHECK_EQ( crcAgreement( reported, crc, connected, 2, 1 ), CRC_AGREEMENT_AGREE );
}

// ---------------------------------------------------------------------------------------------
// Reading the shared random stream without spending it.  Client-side effects - a sound the player
// muted, a particle the detail settings threw away - used to roll from the logic stream, so
// whether they happened at all changed how the simulation unfolded on that machine.
// ---------------------------------------------------------------------------------------------

TEST(logicrandom_unchanged_does_not_move_the_shared_seed)
{
	InitRandom( 12345 );
	const UnsignedInt before = GetGameLogicRandomSeedCRC();

	for (int i = 0; i < 50; ++i)
	{
		GetGameLogicRandomValueUnchanged( 0, 99 );
		GetGameLogicRandomValueRealUnchanged( 0.0f, 1.0f );
	}

	// the whole point: a machine that made these calls and a machine that skipped them are still
	// holding the same seed, so they still agree about the next thing the simulation rolls.
	CHECK_EQ( GetGameLogicRandomSeedCRC(), before );

	// and the ordinary call still does move it, or the check above proves nothing
	GetGameLogicRandomValue( 0, 99, __FILE__, __LINE__ );
	CHECK_NE( GetGameLogicRandomSeedCRC(), before );
}

TEST(logicrandom_unchanged_is_the_same_answer_on_every_machine)
{
	InitRandom( 777 );
	const Int first = GetGameLogicRandomValueUnchanged( 0, 1000000 );

	// same seed state, same answer - repeatedly, because it does not consume anything
	for (int i = 0; i < 10; ++i)
		CHECK_EQ( GetGameLogicRandomValueUnchanged( 0, 1000000 ), first );

	// a second machine that reached this point through the same logic holds the same seed and gets
	// the same answer, which is what made this safe to use for a scripted sound's variant pick
	InitRandom( 777 );
	CHECK_EQ( GetGameLogicRandomValueUnchanged( 0, 1000000 ), first );

	// once the simulation itself rolls, the answer moves on with it
	GetGameLogicRandomValue( 0, 99, __FILE__, __LINE__ );
	InitRandom( 778 );
	CHECK_NE( GetGameLogicRandomValueUnchanged( 0, 1000000 ), first );
}

TEST(logicrandom_unchanged_honours_its_bounds)
{
	InitRandom( 4242 );
	for (int i = 0; i < 200; ++i)
	{
		GetGameLogicRandomValue( 0, 99, __FILE__, __LINE__ );	// walk the seed along

		const Int n = GetGameLogicRandomValueUnchanged( 7, 11 );
		CHECK( n >= 7 && n <= 11 );

		const Real r = GetGameLogicRandomValueRealUnchanged( -2.0f, 2.0f );
		CHECK( r >= -2.0f && r <= 2.0f );
	}

	// degenerate ranges answer the way the ordinary versions do
	CHECK_EQ( GetGameLogicRandomValueUnchanged( 5, 5 ), 5 );
	CHECK_EQ( GetGameLogicRandomValueRealUnchanged( 3.0f, 3.0f ), 3.0f );
}

// ---------------------------------------------------------------------------------------------
// Looking a script up by name.  This used to walk every side's script list and every group inside
// it, on every call, and a subroutine call does it twice.
// ---------------------------------------------------------------------------------------------

TEST(scriptengine_finds_a_script_by_name_wherever_it_lives)
{
	SidesList sides;
	SidesList *savedSides = TheSidesList;
	TheSidesList = &sides;

	Dict emptyDict;
	sides.addSide( &emptyDict );
	sides.addSide( &emptyDict );

	// side 0: one loose script, and one inside a group
	ScriptList *list0 = newInstance(ScriptList);
	sides.getSideInfo( 0 )->setScriptList( list0 );

	Script *loose = newInstance(Script);
	loose->setName( AsciiString( "LooseScript" ) );
	list0->addScript( loose, 0 );

	ScriptGroup *group = newInstance(ScriptGroup);
	group->setName( AsciiString( "TheGroup" ) );
	list0->addGroup( group, 0 );

	Script *inGroup = newInstance(Script);
	inGroup->setName( AsciiString( "GroupedScript" ) );
	group->addScript( inGroup, 0 );

	// side 1: another loose one, to prove the search does not stop at the first side
	ScriptList *list1 = newInstance(ScriptList);
	sides.getSideInfo( 1 )->setScriptList( list1 );

	Script *otherSide = newInstance(Script);
	otherSide->setName( AsciiString( "OtherSideScript" ) );
	list1->addScript( otherSide, 0 );

	ScriptEngine engine;
	CHECK( engine.findScriptByName( AsciiString( "LooseScript" ) ) == loose );
	CHECK( engine.findScriptByName( AsciiString( "GroupedScript" ) ) == inGroup );
	CHECK( engine.findScriptByName( AsciiString( "OtherSideScript" ) ) == otherSide );
	CHECK( engine.findScriptByName( AsciiString( "NoSuchScript" ) ) == NULL );
	CHECK( engine.findScriptByName( AsciiString() ) == NULL );

	// asking twice must answer the same, whether the index was built on this call or the last one
	CHECK( engine.findScriptByName( AsciiString( "GroupedScript" ) ) == inGroup );

	// a new map throws the index away; the same engine must pick up the new scripts
	sides.getSideInfo( 0 )->setScriptList( NULL );
	list0->deleteInstance();
	sides.getSideInfo( 1 )->setScriptList( NULL );
	list1->deleteInstance();

	ScriptList *reloaded = newInstance(ScriptList);
	sides.getSideInfo( 0 )->setScriptList( reloaded );
	Script *afterLoad = newInstance(Script);
	afterLoad->setName( AsciiString( "AfterLoad" ) );
	reloaded->addScript( afterLoad, 0 );

	engine.newMap();
	CHECK( engine.findScriptByName( AsciiString( "AfterLoad" ) ) == afterLoad );
	CHECK( engine.findScriptByName( AsciiString( "LooseScript" ) ) == NULL );

	sides.getSideInfo( 0 )->setScriptList( NULL );
	reloaded->deleteInstance();
	TheSidesList = savedSides;
}

// ---------------------------------------------------------------------------------------------
// The particle system manager's bookkeeping.  Finding a system by id, and unlinking a dead one,
// both used to be linear walks of every live system - on paths the renderer and the logic take
// thousands of times a frame.
// ---------------------------------------------------------------------------------------------

// A particle system stamps itself with TheGameClient's frame, so the test needs one of those too.
// The base class' constructor only zeroes counters, but its destructor tears down half the world's
// globals (the shell, the in-game UI, the campaign manager), so the stub is made once and never
// destroyed.
class TestGameClient : public GameClient
{
public:
	virtual void createRayEffectByTemplate( const Coord3D *, const Coord3D *, const ThingTemplate * ) {}
	virtual void addScorch( const Coord3D *, Real, Scorches ) {}
	virtual Drawable *friend_createDrawable( const ThingTemplate *, DrawableStatus = DRAWABLE_STATUS_NONE ) { return NULL; }
	virtual void setTeamColor( Int, Int, Int ) {}
	virtual void adjustLOD( Int ) {}
	virtual void notifyTerrainObjectMoved( Object * ) {}
	virtual Display *createGameDisplay( void ) { return NULL; }
	virtual InGameUI *createInGameUI( void ) { return NULL; }
	virtual GameWindowManager *createWindowManager( void ) { return NULL; }
	virtual FontLibrary *createFontLibrary( void ) { return NULL; }
	virtual DisplayStringManager *createDisplayStringManager( void ) { return NULL; }
	virtual VideoPlayerInterface *createVideoPlayer( void ) { return NULL; }
	virtual TerrainVisual *createTerrainVisual( void ) { return NULL; }
	virtual Keyboard *createKeyboard( void ) { return NULL; }
	virtual Mouse *createMouse( void ) { return NULL; }
	virtual SnowManager *createSnowManager( void ) { return NULL; }
	virtual void setFrameRate( Real ) {}
};

static GameClient *theTestGameClient = NULL;

static GameClient *getTestGameClient( void )
{
	if( theTestGameClient == NULL )
		theTestGameClient = new TestGameClient;
	return theTestGameClient;
}

// The real managers live in GameEngineDevice; all we need is something concrete to file systems in.
class TestParticleSystemManager : public ParticleSystemManager
{
public:
	virtual Int getOnScreenParticleCount( void ) { return 0; }
	virtual void doParticles( RenderInfoClass & ) {}
	virtual void queueParticleRender() {}
	virtual void preloadAssets( TimeOfDay ) {}
};

TEST(particlesys_find_answers_by_id_and_forgets_a_dead_system)
{
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	ParticleSystemManager *savedManager = TheParticleSystemManager;

	{
		TestParticleSystemManager mgr;
		TheParticleSystemManager = &mgr;

		ParticleSystemTemplate *tmpl = mgr.newTemplate( AsciiString( "TestParticleSystem" ) );
		CHECK( tmpl != NULL );

		ParticleSystem *a = mgr.createParticleSystem( tmpl, FALSE );
		ParticleSystem *b = mgr.createParticleSystem( tmpl, FALSE );
		ParticleSystem *c = mgr.createParticleSystem( tmpl, FALSE );
		CHECK( a != NULL && b != NULL && c != NULL );

		const ParticleSystemID idA = a->getSystemID();
		const ParticleSystemID idB = b->getSystemID();
		const ParticleSystemID idC = c->getSystemID();
		CHECK( idA != idB && idB != idC && idA != idC );

		CHECK( mgr.findParticleSystem( idA ) == a );
		CHECK( mgr.findParticleSystem( idB ) == b );
		CHECK( mgr.findParticleSystem( idC ) == c );
		CHECK( mgr.findParticleSystem( (ParticleSystemID)0x7fffffff ) == NULL );
		CHECK( mgr.findParticleSystem( INVALID_PARTICLE_SYSTEM_ID ) == NULL );
		CHECK_EQ( mgr.getParticleSystemCount(), 3 );

		// killing one out of the middle must not disturb the other two, and its id must stop resolving
		b->deleteInstance();
		CHECK( mgr.findParticleSystem( idB ) == NULL );
		CHECK( mgr.findParticleSystem( idA ) == a );
		CHECK( mgr.findParticleSystem( idC ) == c );
		CHECK_EQ( mgr.getParticleSystemCount(), 2 );

		a->deleteInstance();
		c->deleteInstance();
		CHECK_EQ( mgr.getParticleSystemCount(), 0 );
		CHECK( mgr.findParticleSystem( idA ) == NULL );
		CHECK( mgr.findParticleSystem( idC ) == NULL );
	}

	TheParticleSystemManager = savedManager;
	TheGameClient = savedClient;
}

/* Loading a save has to put each system back under the id it was saved with.  It used to build the
	 system through createParticleSystem, which hands out the next id in sequence and files the system
	 under it, and only then read the saved id over the top - so the manager's index pointed at the
	 wrong system and every lookup by id missed, which is how a master lost its slaves across a save.
	 A system built with no id now stays out of the index until it has one. */
TEST(particlesys_built_without_an_id_stays_out_of_the_index_until_it_has_one)
{
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	ParticleSystemManager *savedManager = TheParticleSystemManager;

	{
		TestParticleSystemManager mgr;
		TheParticleSystemManager = &mgr;

		ParticleSystemTemplate *tmpl = mgr.newTemplate( AsciiString( "TestParticleSystem" ) );
		CHECK( tmpl != NULL );

		ParticleSystem *normal = mgr.createParticleSystem( tmpl, FALSE );
		CHECK_EQ( mgr.getParticleSystemCount(), 1 );

		// the shape the load path uses: no id yet, so nothing to file it under
		ParticleSystem *loading = newInstance(ParticleSystem)( tmpl, INVALID_PARTICLE_SYSTEM_ID, FALSE );
		CHECK( loading != NULL );
		CHECK_EQ( mgr.getParticleSystemCount(), 1 );				// still just the first one
		CHECK( mgr.findParticleSystem( INVALID_PARTICLE_SYSTEM_ID ) == NULL );

		// and taking it away again does not disturb what is filed
		loading->deleteInstance();
		CHECK_EQ( mgr.getParticleSystemCount(), 1 );
		CHECK( mgr.findParticleSystem( normal->getSystemID() ) == normal );

		normal->deleteInstance();
		CHECK_EQ( mgr.getParticleSystemCount(), 0 );
	}

	TheParticleSystemManager = savedManager;
	TheGameClient = savedClient;
}

TEST(particlesys_reset_lets_the_ids_start_over_without_ghosts)
{
	GameClient *savedClient = TheGameClient;
	TheGameClient = getTestGameClient();
	ParticleSystemManager *savedManager = TheParticleSystemManager;

	{
		TestParticleSystemManager mgr;
		TheParticleSystemManager = &mgr;

		ParticleSystemTemplate *tmpl = mgr.newTemplate( AsciiString( "TestParticleSystem" ) );
		CHECK( tmpl != NULL );

		for( Int i = 0; i < 8; ++i )
			CHECK( mgr.createParticleSystem( tmpl, FALSE ) != NULL );
		CHECK_EQ( mgr.getParticleSystemCount(), 8 );

		mgr.reset();
		CHECK_EQ( mgr.getParticleSystemCount(), 0 );

		// reset rewinds the id counter, so an entry left behind would answer for a brand new system
		ParticleSystem *fresh = mgr.createParticleSystem( tmpl, FALSE );
		CHECK( fresh != NULL );
		CHECK( mgr.findParticleSystem( fresh->getSystemID() ) == fresh );

		mgr.reset();
		CHECK_EQ( mgr.getParticleSystemCount(), 0 );
	}

	TheParticleSystemManager = savedManager;
	TheGameClient = savedClient;
}

// ---------------------------------------------------------------------------------------------
// Per-game state that a reused Player has to give back.
// ---------------------------------------------------------------------------------------------

TEST(energy_a_new_game_does_not_inherit_the_last_games_sabotage)
{
	Energy e;

	// a sabotage that runs to frame 30000 of the game that is being left
	e.setPowerSabotagedTillFrame( 30000 );
	CHECK_EQ( e.getPowerSabotagedTillFrame(), 30000u );

	// the Player array is reused, so the next game gets init() rather than a constructor.  EA left
	// the stamp behind: the new game's frame counter starts at 0, so 30000 is still in the future
	// and that player produced no power at all until frame 30000 - on the machines that saw the
	// sabotage, and nowhere else.
	e.init( NULL );
	CHECK_EQ( e.getPowerSabotagedTillFrame(), 0u );
}

// ---------------------------------------------------------------------------------------------
// The cushion, and the self-slug it drives (MULTIPLAYER 3.3).  Two type defects met in the
// middle here: an unsigned frame subtraction that could wrap, and an Int sentinel returned
// through an UnsignedInt.
// ---------------------------------------------------------------------------------------------

TEST(framecushion_is_the_margin_and_never_negative)
{
	CHECK_EQ( frameCushion( 100, 100 ), 0 );
	CHECK_EQ( frameCushion( 130, 100 ), 30 );
	CHECK_EQ( frameCushion( 100, 99 ), 1 );

	// the defect: a command for a frame that has already run.  EA's unsigned subtraction made this
	// 4294967295, which arrives at FrameMetrics::addCushion(Int) as -1 - that class's "no sample
	// yet" sentinel - and wiped the minimum cushion for the window.
	CHECK_EQ( frameCushion( 99, 100 ), 0 );
	CHECK_EQ( frameCushion( 0, 1 ), 0 );
	CHECK_EQ( frameCushion( 100, 5000 ), 0 );

	// and the frame counter is unsigned, so the difference has to survive its wraparound too
	CHECK_EQ( frameCushion( 5, 0xFFFFFFFEu ), 7 );
	CHECK_EQ( frameCushion( 0xFFFFFFFEu, 5 ), 0 );
}

TEST(selfslug_does_not_fire_on_a_cushion_nobody_has_measured)
{
	// FrameMetrics::init leaves the minimum cushion at -1.  ConnectionManager::getMinimumCushion
	// returned UnsignedInt, so that reached Network::timeForNewFrame as four billion frames of
	// margin: the largest cushion representable, produced by the state that knows the least.
	CHECK( !shouldSelfSlug( -1, 20, 10 ) );
	CHECK( !shouldSelfSlug( -100, 20, 10 ) );

	// nor on a run-ahead that has not been established
	CHECK( !shouldSelfSlug( 0, 0, 10 ) );
	CHECK( !shouldSelfSlug( 0, -1, 10 ) );
}

TEST(selfslug_fires_once_the_margin_eats_into_the_slack)
{
	// run-ahead 20, slack 10% -> the threshold is 2 frames of margin
	CHECK( shouldSelfSlug( 0, 20, 10 ) );
	CHECK( shouldSelfSlug( 1, 20, 10 ) );
	CHECK( !shouldSelfSlug( 2, 20, 10 ) );
	CHECK( !shouldSelfSlug( 19, 20, 10 ) );

	// a bigger run-ahead is given proportionally more margin before it worries
	CHECK( shouldSelfSlug( 5, 60, 10 ) );
	CHECK( !shouldSelfSlug( 6, 60, 10 ) );

	// a run-ahead too small for the slack to round to a whole frame still gets the brake: see
	// selfslug_threshold_has_a_floor_the_shipped_run_ahead_cannot_undercut
	CHECK( shouldSelfSlug( 0, 9, 10 ) );
	CHECK( shouldSelfSlug( 1, 9, 10 ) );
	CHECK( !shouldSelfSlug( 2, 9, 10 ) );
}

TEST(selfslug_threshold_has_a_floor_the_shipped_run_ahead_cannot_undercut)
{
	/* The run-ahead a real room plays on is a handful of frames - MIN_RUNAHEAD is four, and
		 computeRunAhead only clears that above about 240 ms round trip - so NetworkRunAheadSlack's
		 10 % of it is worth well under a frame.  A brake that waits until less than one frame of
		 margin is left has already lost: at 30 Hz that is under 33 ms of warning for a hitch that
		 takes longer than that to signal, let alone correct.  Hence the floor. */
	CHECK_EQ( selfSlugThreshold( 10, 10 ), SELFSLUG_MIN_THRESHOLD_FRAMES );

	// the floor holds wherever the arithmetic would land under it, including at zero slack
	CHECK_EQ( selfSlugThreshold( 10, 0 ), SELFSLUG_MIN_THRESHOLD_FRAMES );
	CHECK_EQ( selfSlugThreshold( 1, 10 ), SELFSLUG_MIN_THRESHOLD_FRAMES );
	CHECK_EQ( selfSlugThreshold( 19, 10 ), SELFSLUG_MIN_THRESHOLD_FRAMES );

	// and gets out of the way as soon as the configured slack is worth more than it
	CHECK_EQ( selfSlugThreshold( 30, 10 ), 3 );
	CHECK_EQ( selfSlugThreshold( 64, 10 ), 6 );
	CHECK_EQ( selfSlugThreshold( 20, 50 ), 10 );

	// the floor is what shouldSelfSlug actually uses - no second copy of the arithmetic
	for( Int runAhead = 1; runAhead <= 64; ++runAhead )
	{
		Int threshold = selfSlugThreshold( runAhead, 10 );
		CHECK( threshold >= SELFSLUG_MIN_THRESHOLD_FRAMES );
		CHECK( shouldSelfSlug( threshold - 1, runAhead, 10 ) );
		CHECK( !shouldSelfSlug( threshold, runAhead, 10 ) );
	}

	// a floor is a floor, not a slug-always: the brake still lets go
	CHECK( !shouldSelfSlug( SELFSLUG_MIN_THRESHOLD_FRAMES, 10, 10 ) );
}

/* How far ahead the room schedules its commands.  This is the number that decides whether a
	 click is answered promptly or the match stalls waiting for a packet, and until now it was
	 neither: the floor under it (MIN_RUNAHEAD, ten frames) was larger than the formula's own answer
	 on every link anyone plays on, so every game ran at 333 ms of input delay and the arithmetic
	 that was supposed to adapt the window never got a vote. */

TEST(the_run_ahead_covers_the_trip_it_is_sized_for)
{
	/* The one thing a run-ahead must never do is come out shorter than the wire.  getMaximumLatency
		 sums the two worst average round trips, so the trip a command has to survive is half of it,
		 and the window has to cover that at the rate the room is running.  EA truncated the division:
		 a 150 ms round trip is 2.25 frames of wire at 30 Hz and truncates to 2 - 66 ms of window for
		 75 ms of travel, which arrives late every single time. */
	for( Int ms = 0; ms <= 800; ms += 5 )
	{
		Real latency = (Real)ms / 1000.0f;
		for( Int fps = 5; fps <= 30; fps += 5 )
		{
			Int runAhead = computeRunAhead( latency, fps, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 );

			// the window, in seconds, against the one-way trip it has to cover
			CHECK( (Real)runAhead / (Real)fps >= (latency / 2.0f) );
		}
	}
}

/* Measured in a real LAN match: the host lost one FRAMEINFO packet, sat on the frame for twenty
	 seconds, and its own latency samples came back as 1.79 s and then 7.64 s on a link whose srtt was
	 51 ms.  The run-ahead sized on them went 5 -> 29 -> 64 frames, which is 2.1 seconds of input
	 delay, and it stayed there while the samples sat in the average. */
TEST(a_stalled_frame_is_not_filed_as_round_trip_time)
{
	/* an ordinary link passes through untouched - the clamp must not be a tax on healthy games */
	CHECK_NEAR( sanitizeLatencySample( 0.051f, MAX_PLAUSIBLE_LATENCY_SECONDS ), 0.051f, 0.0001f );
	CHECK_NEAR( sanitizeLatencySample( 0.300f, MAX_PLAUSIBLE_LATENCY_SECONDS ), 0.300f, 0.0001f );
	CHECK( MAX_PLAUSIBLE_LATENCY_SECONDS <= 0.5f );		// a slower link than this is not a game
	CHECK_NEAR( sanitizeLatencySample( MAX_PLAUSIBLE_LATENCY_SECONDS, MAX_PLAUSIBLE_LATENCY_SECONDS ),
							MAX_PLAUSIBLE_LATENCY_SECONDS, 0.0001f );

	/* the two the match actually recorded */
	CHECK_NEAR( sanitizeLatencySample( 1.785224f, MAX_PLAUSIBLE_LATENCY_SECONDS ),
							MAX_PLAUSIBLE_LATENCY_SECONDS, 0.0001f );
	CHECK_NEAR( sanitizeLatencySample( 7.636834f, MAX_PLAUSIBLE_LATENCY_SECONDS ),
							MAX_PLAUSIBLE_LATENCY_SECONDS, 0.0001f );

	/* a clock that ran backwards says nothing; it must not read as a zero-latency link */
	CHECK_NEAR( sanitizeLatencySample( -3.0f, MAX_PLAUSIBLE_LATENCY_SECONDS ), 0.0f, 0.0001f );

	/* and the point of all of it: what the run-ahead does with the clamped numbers.  Two players at
		 the ceiling still cost far less than the 64 frames the raw samples bought. */
	Real clamped = sanitizeLatencySample( 7.636834f, MAX_PLAUSIBLE_LATENCY_SECONDS )
								 + sanitizeLatencySample( 1.785224f, MAX_PLAUSIBLE_LATENCY_SECONDS );
	Int runAhead = computeRunAhead( clamped, 30, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 );
	CHECK( runAhead < 64 );
	CHECK( runAhead <= 18 );			// 0.6 s at 30 Hz, and only with both players at the ceiling
	CHECK( runAhead <= (Int)(MAX_PLAUSIBLE_LATENCY_SECONDS * 30.0f * 1.2f) + RUNAHEAD_JITTER_FRAMES );

	/* a LAN is untouched by any of this */
	CHECK_EQ( computeRunAhead( sanitizeLatencySample( 0.051f, MAX_PLAUSIBLE_LATENCY_SECONDS ) * 2.0f,
														 30, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 ),
						computeRunAhead( 0.102f, 30, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 ) );
}

/* The other half of that match: nothing ever asked for the lost frame.  FrameData escalates to
	 FRAMEDATA_RESEND only when it holds more commands than were announced, so a missing announcement
	 waits for ever - zero FRAMERESENDREQUEST commands went out in the whole game, and what freed it
	 was the disconnect screen's twenty-second handshake. */
TEST(a_missing_frame_is_asked_for_again_once_the_wire_has_had_its_chance)
{
	const UnsignedInt retry = 227;			// the retry timeout that match measured
	const UnsignedInt wait = frameResendWaitMS( retry );

	/* Every frame in the run-ahead window is legitimately not ready for a while.  Asking inside that
		 window is pure traffic, and traffic is what loses packets in the first place. */
	CHECK( shouldRequestFrameResend( 0, 0, retry ) == FALSE );
	CHECK( shouldRequestFrameResend( wait - 1, wait - 1, retry ) == FALSE );

	/* past it, ask - "not asked yet" is sinceLastRequestMS == stalledMS */
	CHECK( shouldRequestFrameResend( wait, wait, retry ) == TRUE );
	CHECK( shouldRequestFrameResend( 20000, 20000, retry ) == TRUE );

	/* having asked, do not ask again until the answer has had time to arrive */
	CHECK( shouldRequestFrameResend( 20000, 0, retry ) == FALSE );
	CHECK( shouldRequestFrameResend( 20000, wait - 1, retry ) == FALSE );
	CHECK( shouldRequestFrameResend( 20000, wait, retry ) == TRUE );

	/* the wait comes from the link, not from a constant, and a wild measurement cannot run away
		 with it in either direction */
	CHECK( frameResendWaitMS( 500 ) > frameResendWaitMS( 120 ) );
	CHECK_EQ( (Int)frameResendWaitMS( 0 ),
						(Int)(FRAME_RESEND_MIN_TIMEOUT_MS * FRAME_RESEND_TIMEOUTS_TO_WAIT) );
	CHECK_EQ( (Int)frameResendWaitMS( 999999 ),
						(Int)(FRAME_RESEND_MAX_TIMEOUT_MS * FRAME_RESEND_TIMEOUTS_TO_WAIT) );

	/* and the whole point: the twenty seconds that match lost become one wait */
	CHECK( wait < 1000 );
}

TEST(the_run_ahead_carries_a_margin_the_percentage_alone_cannot_give)
{
	/* An average round trip is exceeded half the time, so a window sized to exactly the average is
		 wrong half the time.  The proportional slack was meant to be that margin and cannot be: 10 %
		 of a four frame window is zero frames, 10 % of a ten frame window is one.  The fixed
		 allowance is what actually covers the jitter, and it is what makes a low floor safe. */
	for( Int ms = 0; ms <= 800; ms += 5 )
	{
		Real latency = (Real)ms / 1000.0f;
		Int runAhead = computeRunAhead( latency, 30, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 );

		// at least RUNAHEAD_JITTER_FRAMES of room beyond the trip itself, at every latency
		Real slackSeconds = ( (Real)runAhead / 30.0f ) - ( latency / 2.0f );
		CHECK( slackSeconds >= ( (Real)RUNAHEAD_JITTER_FRAMES / 30.0f ) - 0.0005f );
	}
}

TEST(the_run_ahead_is_shorter_than_the_shipped_floor_on_the_links_that_do_not_need_it)
{
	/* What the change is for.  A LAN game and a good broadband game used to be given the same third
		 of a second of input delay as a transatlantic one, because the floor was ten frames and the
		 formula never beat it. */
	const Int shippedFloor = 10;			// EA's MIN_RUNAHEAD, 333 ms of input delay at 30 Hz

	CHECK( computeRunAhead( 0.000f, 30, 10, MIN_RUNAHEAD, 64 ) < shippedFloor );		// LAN
	CHECK( computeRunAhead( 0.040f, 30, 10, MIN_RUNAHEAD, 64 ) < shippedFloor );		// 40 ms
	CHECK( computeRunAhead( 0.100f, 30, 10, MIN_RUNAHEAD, 64 ) < shippedFloor );		// 100 ms
	CHECK( computeRunAhead( 0.160f, 30, 10, MIN_RUNAHEAD, 64 ) < shippedFloor );		// 160 ms

	// and the shortest of them is the floor itself, not something under it
	CHECK_EQ( computeRunAhead( 0.000f, 30, 10, MIN_RUNAHEAD, 64 ), MIN_RUNAHEAD );

	/* And what it is not: the links that were relying on the floor get more window than the floor
		 gave them, not less.  600 ms summed round trip is where EA's formula finally reached ten. */
	CHECK( computeRunAhead( 0.300f, 30, 10, MIN_RUNAHEAD, 64 ) >= 7 );
	CHECK( computeRunAhead( 0.600f, 30, 10, MIN_RUNAHEAD, 64 ) > shippedFloor );
	CHECK( computeRunAhead( 1.000f, 30, 10, MIN_RUNAHEAD, 64 ) > 16 );
}

TEST(the_run_ahead_stays_inside_the_bounds_the_network_buffers_are_built_for)
{
	// the window indexes frame buffers sized from MAX_FRAMES_AHEAD; it may not walk out of them
	for( Int ms = 0; ms <= 20000; ms += 25 )
	{
		Int runAhead = computeRunAhead( (Real)ms / 1000.0f, 30, 10, MIN_RUNAHEAD, MAX_FRAMES_AHEAD / 2 );
		CHECK( runAhead >= MIN_RUNAHEAD );
		CHECK( runAhead <= MAX_FRAMES_AHEAD / 2 );
	}

	// a rate of zero is not a division by zero, and a negative latency is not a negative window
	CHECK_EQ( computeRunAhead( 0.0f, 0, 10, MIN_RUNAHEAD, 64 ), MIN_RUNAHEAD );
	CHECK_EQ( computeRunAhead( -1.0f, 30, 10, MIN_RUNAHEAD, 64 ), MIN_RUNAHEAD );
}

TEST(the_shipped_run_ahead_floor_leaves_the_self_slug_brake_room_to_work)
{
	/* The floor and the brake are one decision, not two.  The self-slug fires when the measured
		 cushion drops below selfSlugThreshold(runAhead), so a floor at or below that threshold means
		 a room at its shortest window is braking permanently - the stall the brake exists to avoid,
		 applied continuously. */
	CHECK( MIN_RUNAHEAD > SELFSLUG_MIN_THRESHOLD_FRAMES );
	CHECK( selfSlugThreshold( MIN_RUNAHEAD, 10 ) < MIN_RUNAHEAD );
	CHECK( !shouldSelfSlug( MIN_RUNAHEAD, MIN_RUNAHEAD, 10 ) );

	// and the buffers the window is drawn from are big enough for the largest window it can ask for
	CHECK( MIN_RUNAHEAD <= MAX_FRAMES_AHEAD / 2 );
	CHECK( FRAME_DATA_LENGTH > 2 * MAX_FRAMES_AHEAD );
	CHECK( FRAMES_TO_KEEP > MAX_FRAMES_AHEAD / 2 );
}

/* The room's logic rate.  ConnectionManager::updateRunAhead runs on the packet router: it takes
	 the slowest frame rate any player reported, settles it into the allowed range, and broadcasts
	 the result as the rate every machine paces its logic on.  The reported rate is the rate a
	 player *achieved*, so a player who is keeping up reports back exactly the rate they were told
	 to run at - which is why the broadcast rate has to be a step above the measured minimum or it
	 can never rise again.  simulateRoom below is that whole loop, with each player achieving the
	 smaller of their own capability and the rate they were commanded. */

static Int simulateRoom( const Int *capability, Int numPlayers, Int startRate, Int rounds,
												 Int fpsLimit )
{
	Int rate = startRate;
	for( Int round = 0; round < rounds; ++round )
	{
		Int minFps = -1;
		for( Int player = 0; player < numPlayers; ++player )
		{
			Int reported = capability[player] < rate ? capability[player] : rate;
			if( minFps == -1 || reported < minFps )
				minFps = reported;
		}
		rate = probeRoomFrameRate( settleRoomFrameRate( minFps, fpsLimit ), fpsLimit );
	}
	return rate;
}

TEST(room_frame_rate_settles_into_the_allowed_range)
{
	CHECK_EQ( settleRoomFrameRate( 30, 30 ), 30 );
	CHECK_EQ( settleRoomFrameRate( 17, 30 ), 17 );

	// nobody plays at two frames a second, whatever the metrics claim
	CHECK_EQ( settleRoomFrameRate( 2, 30 ), ROOM_FRAME_RATE_FLOOR );
	CHECK_EQ( settleRoomFrameRate( 0, 30 ), ROOM_FRAME_RATE_FLOOR );
	CHECK_EQ( settleRoomFrameRate( -1, 30 ), ROOM_FRAME_RATE_FLOOR );

	// and the room never runs faster than the game is configured to
	CHECK_EQ( settleRoomFrameRate( 100, 30 ), 30 );
	CHECK_EQ( settleRoomFrameRate( 100, 60 ), 60 );
	CHECK_EQ( settleRoomFrameRate( 45, 60 ), 45 );
}

TEST(room_frame_rate_probe_always_moves_by_at_least_one_frame)
{
	/* The step is what breaks the latch, so a step of zero is the bug.  Integer division eats it
		 below ten frames a second: (9 * 110) / 100 is 9. */
	for( Int settled = ROOM_FRAME_RATE_FLOOR; settled < 30; ++settled )
		CHECK( probeRoomFrameRate( settled, 30 ) > settled );

	CHECK_EQ( probeRoomFrameRate( 5, 30 ), 6 );
	CHECK_EQ( probeRoomFrameRate( 9, 30 ), 10 );
	CHECK_EQ( probeRoomFrameRate( 10, 30 ), 11 );
	CHECK_EQ( probeRoomFrameRate( 20, 30 ), 22 );

	// but never past the limit, and at the limit it is a no-op rather than an overshoot
	CHECK_EQ( probeRoomFrameRate( 30, 30 ), 30 );
	CHECK_EQ( probeRoomFrameRate( 29, 30 ), 30 );
	CHECK_EQ( probeRoomFrameRate( 28, 30 ), 30 );
	CHECK_EQ( probeRoomFrameRate( 55, 60 ), 60 );

	/* EA capped this step at a hardcoded 30 while the settled rate was capped at
		 FramesPerSecondLimit, so a room configured above 30 told its slowest player to slow down. */
	CHECK_EQ( probeRoomFrameRate( 40, 60 ), 44 );
}

TEST(room_frame_rate_climbs_back_after_one_player_hitches)
{
	/* The defect this pins: every machine paces its logic on the broadcast rate, so a machine that
		 is keeping up measures exactly that rate and reports it back.  With the room commanded at
		 the reported minimum, the minimum is then whatever it already was - for ever.  One player's
		 two second hitch dropped the room to 12 and the whole match stayed in slow motion. */
	const Int fpsLimit = 30;
	Int capable[4] = { 30, 30, 30, 30 };

	// the room is at 12 because somebody hitched; they have recovered, everyone can do 30 now
	Int rate = simulateRoom( capable, 4, 12, 1, fpsLimit );
	CHECK( rate > 12 );

	// and it keeps climbing, round after round, until it is back at the limit
	Int previous = 12;
	for( Int round = 1; round <= 20; ++round )
	{
		rate = simulateRoom( capable, 4, 12, round, fpsLimit );
		CHECK( rate >= previous );
		previous = rate;
	}
	CHECK_EQ( simulateRoom( capable, 4, 12, 20, fpsLimit ), 30 );

	// from the floor as well, and from a two player room, and with a raised limit
	CHECK_EQ( simulateRoom( capable, 4, ROOM_FRAME_RATE_FLOOR, 40, fpsLimit ), 30 );
	CHECK_EQ( simulateRoom( capable, 2, 6, 40, fpsLimit ), 30 );

	Int capable60[2] = { 60, 60 };
	CHECK_EQ( simulateRoom( capable60, 2, 10, 40, 60 ), 60 );
}

TEST(room_frame_rate_still_pins_to_a_genuinely_slow_player_without_oscillating)
{
	/* The step must not turn into a speed wobble: a player who really cannot do better than 12
		 has to hold the room near 12 and hold it *steady*.  EA's "keep the current rate if the
		 minimum is within 10 % of it" band is what would break this - it ignores the slow player
		 for exactly as long as the step keeps the rate within 10 % of them, so the room walks
		 12-13-14-15 and falls back to 12.  The band is gone; the step does its job alone. */
	const Int fpsLimit = 30;
	Int mixed[3] = { 30, 30, 12 };

	Int rate = simulateRoom( mixed, 3, 30, 30, fpsLimit );
	CHECK( rate >= 12 );
	CHECK( rate <= 14 );

	// settled means settled: over the last twenty rounds the rate must not move at all
	Int settledRate = simulateRoom( mixed, 3, 30, 10, fpsLimit );
	for( Int round = 10; round <= 30; ++round )
		CHECK_EQ( simulateRoom( mixed, 3, 30, round, fpsLimit ), settledRate );

	// the room does slow down for them, though - that part is the point of the mechanism
	CHECK( settledRate < 30 );

	// a machine below the floor cannot drag the room under it either
	Int hopeless[2] = { 30, 1 };
	CHECK_EQ( simulateRoom( hopeless, 2, 30, 30, fpsLimit ),
						probeRoomFrameRate( ROOM_FRAME_RATE_FLOOR, fpsLimit ) );
}

TEST(resend_window_is_measured_from_the_start_of_the_match_not_four_billion)
{
	/* ConnectionManager::sendSingleFrameToPlayer refuses to resend a frame older than
		 FRAMES_TO_KEEP, which is (MAX_FRAMES_AHEAD / 2) + 1 = 65 - about two seconds at 30 Hz, and
		 exactly the window a stalled player can be behind by, since nobody advances while the room
		 is waiting for them. */
	const Int KEEP = 65;

	// the frame we are on, and everything inside the window, is resendable
	CHECK( !frameIsTooOldToResend( 1000, 1000, KEEP ) );
	CHECK( !frameIsTooOldToResend( 1000, 999, KEEP ) );
	CHECK( !frameIsTooOldToResend( 1000, 1000 - KEEP, KEEP ) );

	// one frame past the window is not
	CHECK( frameIsTooOldToResend( 1000, 1000 - KEEP - 1, KEEP ) );
	CHECK( frameIsTooOldToResend( 1000, 0, KEEP ) );

	/* The defect: EA computed `(currentFrame - FRAMES_TO_KEEP) > requestedFrame` on UnsignedInts,
		 so for the first 65 frames of every match the left side wrapped to about four billion and
		 every request was refused.  A stall in the opening two seconds could not be repaired by the
		 resend at all - it had to wait for the retry and then the disconnect screen. */
	CHECK( !frameIsTooOldToResend( 0, 0, KEEP ) );
	CHECK( !frameIsTooOldToResend( 1, 0, KEEP ) );
	CHECK( !frameIsTooOldToResend( 10, 3, KEEP ) );
	CHECK( !frameIsTooOldToResend( KEEP, 0, KEEP ) );
	CHECK( frameIsTooOldToResend( KEEP + 1, 0, KEEP ) );

	// a frame we have not reached is not old; we simply have nothing for it yet
	CHECK( !frameIsTooOldToResend( 100, 101, KEEP ) );
	CHECK( !frameIsTooOldToResend( 100, 1000000, KEEP ) );

	// and the frame counter itself wrapping does not reopen or close the window by accident
	CHECK( !frameIsTooOldToResend( 5, 0xFFFFFFFEu, KEEP ) );
	CHECK( frameIsTooOldToResend( 5, 0xFFFFFF00u, KEEP ) );
}

/* The packet router fallback plan: who relays for everybody, and in what order they take over.
	 Walked in two places, both of which used to be able to read one entry past the end of it. */
TEST(packet_router_succession_never_walks_off_the_end_of_the_plan)
{
	const Int SLOTS = 8;
	const UnsignedInt NONE = (UnsignedInt)SLOTS;		// the "plan is exhausted" answer
	const UnsignedInt EMPTY = (UnsignedInt)-1;			// what an emptied entry is left holding

	/* A full eight player game.  The last entry is the case EA's walk got wrong: the loop stopped at
		 MAX_SLOTS-1 whether it matched or not, the unconditional ++ then made the index MAX_SLOTS, and
		 the read landed on the member after the array. */
	const UnsignedInt full[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	CHECK_EQ( nextPacketRouterSlot( full, SLOTS, 0 ), 1u );
	CHECK_EQ( nextPacketRouterSlot( full, SLOTS, 5 ), 6u );
	CHECK_EQ( nextPacketRouterSlot( full, SLOTS, 6 ), 7u );
	CHECK_EQ( nextPacketRouterSlot( full, SLOTS, 7 ), NONE );

	/* A four player game leaves the rest of the plan as -1.  Reading that as a slot number gives
		 four billion, which is the same wrong answer by a different route. */
	const UnsignedInt four[8] = { 0, 1, 2, 3, EMPTY, EMPTY, EMPTY, EMPTY };
	CHECK_EQ( nextPacketRouterSlot( four, SLOTS, 0 ), 1u );
	CHECK_EQ( nextPacketRouterSlot( four, SLOTS, 2 ), 3u );
	CHECK_EQ( nextPacketRouterSlot( four, SLOTS, 3 ), NONE );

	/* Every leave compacts the plan, so the entries are slot numbers with gaps and the tail fills
		 with -1 from the end. */
	const UnsignedInt compacted[8] = { 1, 2, 4, 5, 6, 7, EMPTY, EMPTY };
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 1 ), 2u );
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 2 ), 4u );
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 7 ), NONE );

	/* Asking about a slot that is not in the plan at all - the state the hardcoded initial router
		 could produce when slot 0 held no human player. */
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 0 ), NONE );
	CHECK_EQ( nextPacketRouterSlot( compacted, SLOTS, 3 ), NONE );

	/* Last player standing. */
	const UnsignedInt alone[8] = { 3, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY };
	CHECK_EQ( nextPacketRouterSlot( alone, SLOTS, 3 ), NONE );

	/* The succession is a walk of the plan, never a re-election: repeated calls march down the plan
		 in order and stop, so two machines running it on the same plan cannot end up disagreeing
		 about who is relaying.  That property is why the plan is not reordered from local latency
		 measurements - see MULTIPLAYER.md 2.5. */
	UnsignedInt router = compacted[0];
	Int steps = 0;
	while( router < (UnsignedInt)SLOTS )
	{
		UnsignedInt next = nextPacketRouterSlot( compacted, SLOTS, router );
		CHECK( next == NONE || next > router );
		router = next;
		++steps;
		CHECK( steps <= SLOTS );
	}
	CHECK_EQ( steps, 6 );		// six live entries, then the plan is out

}

TEST(selfslug_is_monotonic_in_the_cushion)
{
	// once there is enough margin the answer must stay no, or the frame rate would oscillate
	Bool seenNo = FALSE;
	for( Int cushion = 0; cushion <= 200; ++cushion )
	{
		Bool slug = shouldSelfSlug( cushion, 60, 10 );
		if( !slug )
			seenNo = TRUE;
		else
			CHECK( !seenNo );
	}
	CHECK( seenNo );
}

// ---------------------------------------------------------------------------------------------
// TerrainLogic used to answer "which waypoint is called X" and "which trigger area is called X"
// by walking the whole list, on every call, and the scripts ask constantly.  Both are indexed now,
// and the index has to notice the list changing under it.
// ---------------------------------------------------------------------------------------------
class TestTerrainLogic : public TerrainLogic
{
public:
	void addTestWaypoint( UnsignedInt id, AsciiString name )
	{
		Coord3D loc;
		loc.x = (Real)id;
		loc.y = 0.0f;
		loc.z = 0.0f;
		Waypoint *way = newInstance(Waypoint)( (WaypointID)id, name, &loc,
																					 AsciiString::TheEmptyString,
																					 AsciiString::TheEmptyString,
																					 AsciiString::TheEmptyString, FALSE );
		linkWaypoint( way );
	}

	void dropAllWaypoints( void ) { deleteWaypoints(); }
};

// A missing waypoint reads as id 0 / name "<none>" rather than a null dereference, so a broken
// index reports itself instead of taking the whole run down.
static UnsignedInt waypointID( Waypoint *way ) { return way ? (UnsignedInt)way->getID() : 0; }
static AsciiString waypointName( Waypoint *way ) { return way ? way->getName() : AsciiString("<none>"); }

TEST(terrainlogic_finds_a_waypoint_by_name_and_by_id)
{
	TestTerrainLogic tl;

	tl.addTestWaypoint( 1, "Alpha" );
	tl.addTestWaypoint( 2, "Bravo" );
	tl.addTestWaypoint( 3, "Charlie" );

	CHECK( tl.getWaypointByName( "Bravo" ) != NULL );
	CHECK_EQ( waypointID( tl.getWaypointByName( "Bravo" ) ), (UnsignedInt)2 );
	CHECK( tl.getWaypointByID( 3 ) != NULL );
	CHECK_STR( waypointName( tl.getWaypointByID( 3 ) ).str(), "Charlie" );
	CHECK( tl.getWaypointByName( "Nobody" ) == NULL );
	CHECK( tl.getWaypointByID( 99 ) == NULL );

	// the index was built by those lookups; a waypoint added afterwards still has to be found
	tl.addTestWaypoint( 4, "Delta" );
	CHECK( tl.getWaypointByName( "Delta" ) != NULL );
	CHECK_EQ( waypointID( tl.getWaypointByName( "Delta" ) ), (UnsignedInt)4 );
	CHECK( tl.getWaypointByName( "Alpha" ) != NULL );

	// the list is built by pushing onto the head and the old search returned the first match from
	// the head, so the most recently added of two waypoints sharing a name is the one that answers
	tl.addTestWaypoint( 5, "Alpha" );
	CHECK_EQ( waypointID( tl.getWaypointByName( "Alpha" ) ), (UnsignedInt)5 );

	tl.dropAllWaypoints();
	CHECK( tl.getWaypointByName( "Alpha" ) == NULL );
	CHECK( tl.getWaypointByID( 1 ) == NULL );
}

TEST(terrainlogic_finds_a_trigger_area_by_name_and_notices_edits)
{
	PolygonTrigger::deleteTriggers();

	TestTerrainLogic tl;

	PolygonTrigger *zone1 = newInstance(PolygonTrigger)( 4 );
	zone1->setTriggerName( "Zone1" );
	PolygonTrigger::addPolygonTrigger( zone1 );

	PolygonTrigger *zone2 = newInstance(PolygonTrigger)( 4 );
	zone2->setTriggerName( "Zone2" );
	PolygonTrigger::addPolygonTrigger( zone2 );

	CHECK( tl.getTriggerAreaByName( "Zone1" ) == zone1 );
	CHECK( tl.getTriggerAreaByName( "Zone2" ) == zone2 );
	CHECK( tl.getTriggerAreaByName( "Zone3" ) == NULL );

	// added after the index was built
	PolygonTrigger *zone3 = newInstance(PolygonTrigger)( 4 );
	zone3->setTriggerName( "Zone3" );
	PolygonTrigger::addPolygonTrigger( zone3 );
	CHECK( tl.getTriggerAreaByName( "Zone3" ) == zone3 );

	// renamed after the index was built
	zone3->setTriggerName( "Zone9" );
	CHECK( tl.getTriggerAreaByName( "Zone9" ) == zone3 );
	CHECK( tl.getTriggerAreaByName( "Zone3" ) == NULL );

	// unlinked after the index was built
	PolygonTrigger::removePolygonTrigger( zone2 );
	CHECK( tl.getTriggerAreaByName( "Zone2" ) == NULL );
	CHECK( tl.getTriggerAreaByName( "Zone1" ) == zone1 );
	zone2->deleteInstance();

	PolygonTrigger::deleteTriggers();
	CHECK( tl.getTriggerAreaByName( "Zone1" ) == NULL );
}

// A restarted skirmish must draw the same random colors, start positions and factions as the first
// run did.  The draws only happen for a slot whose value is still negative, so the setup captured
// before the first draw has to be put back before the second start.
TEST(gameslot_restores_the_pre_randomization_setup_on_a_restart)
{
	GameSlot slot;

	// nothing captured yet, and the map's own "random" markers are what the slot starts with
	CHECK( !slot.hasSavedOriginalSetup() );
	CHECK_EQ( slot.getColor(), -1 );
	CHECK_EQ( slot.getStartPos(), -1 );
	CHECK_EQ( slot.getPlayerTemplate(), (Int)PLAYERTEMPLATE_RANDOM );

	// first start: capture, then let the draws resolve every one of them
	slot.saveOriginalSetup();
	CHECK( slot.hasSavedOriginalSetup() );
	CHECK_EQ( slot.getOriginalColor(), -1 );
	CHECK_EQ( slot.getOriginalStartPos(), -1 );
	CHECK_EQ( slot.getOriginalPlayerTemplate(), (Int)PLAYERTEMPLATE_RANDOM );

	slot.setColor( 3 );
	slot.setStartPos( 5 );
	slot.setPlayerTemplate( 2 );
	CHECK_EQ( slot.getColor(), 3 );
	CHECK_EQ( slot.getStartPos(), 5 );
	CHECK_EQ( slot.getPlayerTemplate(), 2 );

	// second start: the capture is still there, so the slot is rolled back instead of re-captured
	CHECK( slot.hasSavedOriginalSetup() );
	slot.setColor( slot.getOriginalColor() );
	slot.setStartPos( slot.getOriginalStartPos() );
	slot.setPlayerTemplate( slot.getOriginalPlayerTemplate() );
	CHECK_EQ( slot.getColor(), -1 );
	CHECK_EQ( slot.getStartPos(), -1 );
	CHECK_EQ( slot.getPlayerTemplate(), (Int)PLAYERTEMPLATE_RANDOM );

	// and the originals survive the rollback, so a third start rolls back to the same place
	CHECK_EQ( slot.getOriginalColor(), -1 );
	CHECK_EQ( slot.getOriginalStartPos(), -1 );
	CHECK_EQ( slot.getOriginalPlayerTemplate(), (Int)PLAYERTEMPLATE_RANDOM );

	// a slot that was set up by hand is captured as-is, not as the random markers
	GameSlot fixed;
	fixed.setColor( 4 );
	fixed.setStartPos( 1 );
	fixed.setPlayerTemplate( 6 );
	fixed.saveOriginalSetup();
	CHECK_EQ( fixed.getOriginalColor(), 4 );
	CHECK_EQ( fixed.getOriginalStartPos(), 1 );
	CHECK_EQ( fixed.getOriginalPlayerTemplate(), 6 );

	// leaving the lobby clears the capture, so the next game captures its own setup
	fixed.reset();
	CHECK( !fixed.hasSavedOriginalSetup() );
	CHECK_EQ( fixed.getOriginalColor(), -1 );
}

// The game's fingerprint over a BitFlags used to be taken with sizeof(this) - the size of the
// pointer - so only the first four bytes of any flag set were in it.  ObjectStatusMaskType is 47
// bits wide, so everything from RIDER1 upwards could differ between two machines without the CRC
// ever noticing.
static UnsignedInt crcOfStatusMask( ObjectStatusMaskType mask )
{
	XferCRC xfer;
	xfer.open( "test" );
	mask.xfer( &xfer );
	xfer.close();
	return xfer.getCRC();
}

TEST(bitflags_crc_covers_every_word_of_the_flag_set)
{
	ObjectStatusMaskType empty;
	empty.clear();

	// a bit inside the first word has always been covered
	ObjectStatusMaskType low;
	low.clear();
	low.set( OBJECT_STATUS_DESTROYED );
	CHECK_NE( crcOfStatusMask( low ), crcOfStatusMask( empty ) );

	// ...and so is everything past it now
	CHECK( (Int)OBJECT_STATUS_IMMOBILE >= 32 );
	ObjectStatusMaskType high;
	high.clear();
	high.set( OBJECT_STATUS_IMMOBILE );
	CHECK_NE( crcOfStatusMask( high ), crcOfStatusMask( empty ) );

	ObjectStatusMaskType deployed;
	deployed.clear();
	deployed.set( OBJECT_STATUS_DEPLOYED );
	CHECK_NE( crcOfStatusMask( deployed ), crcOfStatusMask( empty ) );

	// two different high bits are two different fingerprints, not one
	CHECK_NE( crcOfStatusMask( high ), crcOfStatusMask( deployed ) );

	// and the same mask still fingerprints the same way twice
	CHECK_EQ( crcOfStatusMask( high ), crcOfStatusMask( high ) );

	// the whole set is hashed, not a prefix of it: clearing a high bit off a full mask changes it
	ObjectStatusMaskType full, fullMinusOne;
	full.clear();
	fullMinusOne.clear();
	for( Int i = 0; i < (Int)OBJECT_STATUS_COUNT; ++i )
	{
		full.set( i );
		if( i != (Int)OBJECT_STATUS_DEPLOYED )
			fullMinusOne.set( i );
	}
	CHECK_NE( crcOfStatusMask( full ), crcOfStatusMask( fullMinusOne ) );
}

// ---------------------------------------------------------------------------------------------
// Network command ids are sixteen bits and wrap.  NetCommandList insert-sorts by them, so the
// comparison has to survive the wrap or a player's own orders come out of order - and, because a
// broken comparison makes the sorted insert depend on arrival order, come out differently on two
// machines that received the same commands in different orders.
// ---------------------------------------------------------------------------------------------
TEST(network_command_ids_are_ordered_across_the_sixteen_bit_wrap)
{
	// away from the wrap, this is plain ordering
	CHECK( IsCommandIdNewer( 1, 0 ) );
	CHECK( !IsCommandIdNewer( 0, 1 ) );
	CHECK( !IsCommandIdNewer( 7, 7 ) );			// the same id is not newer than itself

	// across the wrap - the raw > comparison gets both of these backwards
	CHECK( IsCommandIdNewer( 0, 65535 ) );
	CHECK( !IsCommandIdNewer( 65535, 0 ) );
	CHECK( IsCommandIdNewer( 3, 65530 ) );
	CHECK( !IsCommandIdNewer( 65530, 3 ) );

	// half the id space ahead is still ahead; one past that is read as behind
	CHECK( IsCommandIdNewer( 0x7FFF, 0 ) );
	CHECK( !IsCommandIdNewer( 0x8000, 0 ) );

	// every consecutive pair across a run that crosses 65535 -> 0 is ordered, both ways round
	Bool allOrdered = TRUE;
	UnsignedShort id = 65500;
	for( Int i = 0; i < 100; ++i )
	{
		UnsignedShort next = (UnsignedShort)(id + 1);
		if( !IsCommandIdNewer( next, id ) || IsCommandIdNewer( id, next ) )
			allOrdered = FALSE;
		id = next;
	}
	CHECK( allOrdered );
	CHECK_EQ( (Int)id, 64 );					// the run really did cross the wrap
}

//////////////////////////////////////////////////////////////////////////////
// Control groups
//////////////////////////////////////////////////////////////////////////////

/* The ten control group keys produce squad numbers 0..NUM_HOTKEY_SQUADS-1.  The camera-jump
	 handler tested "1 through 10" instead, so the 0 key never centred on its group and the number
	 it did accept was one past the last squad.  All four handlers ask this one function now. */
TEST(hotkey_squad_index_covers_group_zero_and_stops_at_the_last_squad)
{
	CHECK_EQ( (Int)NUM_HOTKEY_SQUADS, 10 );

	CHECK( isValidHotkeySquadIndex( 0 ) );					// the "0" key, the one that was dropped
	CHECK( isValidHotkeySquadIndex( 1 ) );
	CHECK( isValidHotkeySquadIndex( NUM_HOTKEY_SQUADS - 1 ) );

	CHECK( !isValidHotkeySquadIndex( NUM_HOTKEY_SQUADS ) );	// the one it used to accept
	CHECK( !isValidHotkeySquadIndex( -1 ) );

	// every squad the keys can name is a squad Player::getHotkeySquad will actually look up
	Bool allInRange = TRUE;
	for( Int group = 0; group < 10; ++group )
		if( !isValidHotkeySquadIndex( group ) )
			allInRange = FALSE;
	CHECK( allInRange );
}

//////////////////////////////////////////////////////////////////////////////
// Network command list ordering
//////////////////////////////////////////////////////////////////////////////

/* Helper: an ack for command "commandID" from player "playerID".  An ack's own id is always zero;
	 the number it sorts by is the id of the command it acknowledges. */
static NetAckStage1CommandMsg *makeAck( UnsignedShort commandID, UnsignedByte playerID )
{
	NetAckStage1CommandMsg *ack = newInstance( NetAckStage1CommandMsg );
	ack->setCommandID( commandID );
	ack->setOriginalPlayerID( playerID );
	ack->setPlayerID( playerID );
	ack->setID( 0 );
	return ack;
}

/* The command list sorts by type, then player, then sort number - and for an ack the sort number
	 is the acknowledged command's id, not the ack's own id, which is always zero.  The insertion
	 shortcut compared ids instead, so IsCommandIdNewer(0, 0) was false for every ack and the
	 shortcut was dead for the one message that arrives in bulk. */
TEST(netcommand_ordering_uses_the_sort_number_not_the_always_zero_ack_id)
{
	NetAckStage1CommandMsg *older = makeAck( 3, 2 );
	NetAckStage1CommandMsg *newer = makeAck( 7, 2 );

	// what the old comparison looked at: identical, and so never "newer"
	CHECK_EQ( (Int)older->getID(), 0 );
	CHECK_EQ( (Int)newer->getID(), 0 );
	CHECK( !IsCommandIdNewer( newer->getID(), older->getID() ) );

	// what the list actually orders by
	CHECK_EQ( older->getSortNumber(), 3 );
	CHECK_EQ( newer->getSortNumber(), 7 );

	CHECK( IsCommandFromSamePlayerGroup( older, newer ) );
	CHECK( IsCommandNewerInSamePlayerGroup( newer, older ) );
	CHECK( !IsCommandNewerInSamePlayerGroup( older, newer ) );

	// a different player is a different group, however the numbers compare
	NetAckStage1CommandMsg *otherPlayer = makeAck( 7, 5 );
	CHECK( !IsCommandFromSamePlayerGroup( newer, otherPlayer ) );
	CHECK( !IsCommandNewerInSamePlayerGroup( otherPlayer, older ) );

	// the full ordering is lexicographic: type, then player, then sort number
	CHECK( IsCommandNewer( otherPlayer, newer ) );			// same type, higher player
	CHECK( !IsCommandNewer( newer, otherPlayer ) );
	CHECK( IsCommandNewer( newer, older ) );				// same type and player, higher sort number

	NetAckStage2CommandMsg *laterType = newInstance( NetAckStage2CommandMsg );
	laterType->setCommandID( 0 );
	laterType->setPlayerID( 2 );
	CHECK( IsCommandNewer( laterType, newer ) );				// higher type beats a higher sort number
	CHECK( !IsCommandNewer( newer, laterType ) );

	older->detach();
	newer->detach();
	otherPlayer->detach();
	laterType->detach();
}

/* Acks sort to the head of the list, so the shortcut is what keeps a burst of them linear.  Order
	 has to come out the same whether it is taken or not: in order, out of order, and duplicated. */
TEST(netcommand_list_orders_a_burst_of_acks_by_acknowledged_command)
{
	NetCommandList *list = newInstance( NetCommandList );
	list->init();

	// in ascending order - the path the shortcut is there for
	static const UnsignedShort inOrder[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	Int i;
	for( i = 0; i < 8; ++i )
	{
		NetAckStage1CommandMsg *ack = makeAck( inOrder[i], 1 );
		list->addMessage( ack );
		ack->detach();
	}

	// out of order, and one exact duplicate of a command already on the list
	static const UnsignedShort outOfOrder[] = { 12, 9, 11, 10, 5 };
	for( i = 0; i < 5; ++i )
	{
		NetAckStage1CommandMsg *ack = makeAck( outOfOrder[i], 1 );
		list->addMessage( ack );
		ack->detach();
	}

	CHECK_EQ( list->length(), 12 );			// the duplicate 5 was dropped, 1..12 remain

	Int expected = 1;
	Bool ascending = TRUE;
	for( NetCommandRef *ref = list->getFirstMessage(); ref != NULL; ref = ref->getNext() )
	{
		if( ref->getCommand()->getSortNumber() != expected )
			ascending = FALSE;
		++expected;
	}
	CHECK( ascending );
	CHECK_EQ( expected, 13 );

	list->deleteInstance();
}

/* The file transfer messages carry a filename the sender chooses the length of, and the reader
	 copied it into a _MAX_PATH stack buffer a byte at a time with nothing stopping it.  A name
	 longer than that overwrote the stack of whoever was parsing the packet - a host feeding a
	 client, or a client feeding the host.  The copy is bounded now; the read offset still steps
	 over the whole name, so everything after it in the message is still read from the right place. */
struct NetPacketFileReader : public NetPacket
{
	using NetPacket::readFileMessage;
	using NetPacket::readFileAnnounceMessage;
	using NetPacket::GetBufferSizeNeededForCommand;
	using NetPacket::FillBufferWithCommand;
};

/* Sending a command out of band asks how many bytes it needs, allocates that, and then lets the
	 writer fill it.  Three of those size functions disagreed with their own writer: the run-ahead
	 metrics one counted the average frame rate as a byte where the writer sends a short, and the
	 load-complete and timeout-start ones did not count the command id at all, though the writer
	 always emits it.  Each of those is a write past the end of a freshly sized heap block.

	 So measure it: fill a canary buffer, find how far the writer really went, and hold the
	 advertised size against it. */
static Int bytesActuallyWritten( NetCommandRef *ref, Int *advertised )
{
	static const Int CANARY_LEN = 512;
	static const UnsignedByte CANARY = 0xCD;
	UnsignedByte buffer[CANARY_LEN];
	memset( buffer, CANARY, sizeof(buffer) );

	*advertised = (Int)NetPacketFileReader::GetBufferSizeNeededForCommand( ref->getCommand() );
	NetPacketFileReader::FillBufferWithCommand( buffer, ref );

	Int last = -1;
	Int i;
	for( i = 0; i < CANARY_LEN; ++i )
		if( buffer[i] != CANARY )
			last = i;
	return last + 1;
}

static NetCommandRef *wrapForSend( NetCommandMsg *msg, NetCommandType type )
{
	msg->setNetCommandType( type );
	msg->setPlayerID( 3 );
	msg->setID( 0x1234 );
	msg->setExecutionFrame( 99 );
	NetCommandRef *ref = newInstance( NetCommandRef )( msg );
	ref->setRelay( 0 );
	msg->detach();
	return ref;
}

TEST(a_commands_advertised_size_covers_what_its_writer_puts_in_the_buffer)
{
	Int advertised;

	NetRunAheadMetricsCommandMsg *metrics = newInstance( NetRunAheadMetricsCommandMsg );
	metrics->setAverageLatency( 0.125f );
	metrics->setAverageFps( 300 );					// wider than a byte, which is the point
	NetCommandRef *metricsRef = wrapForSend( metrics, NETCOMMANDTYPE_RUNAHEADMETRICS );
	CHECK( bytesActuallyWritten( metricsRef, &advertised ) <= advertised );
	metricsRef->deleteInstance();

	NetCommandMsg *loadDone = newInstance( NetCommandMsg );
	NetCommandRef *loadRef = wrapForSend( loadDone, NETCOMMANDTYPE_LOADCOMPLETE );
	CHECK( bytesActuallyWritten( loadRef, &advertised ) <= advertised );
	loadRef->deleteInstance();

	NetCommandMsg *timeout = newInstance( NetCommandMsg );
	NetCommandRef *timeoutRef = wrapForSend( timeout, NETCOMMANDTYPE_TIMEOUTSTART );
	CHECK( bytesActuallyWritten( timeoutRef, &advertised ) <= advertised );
	timeoutRef->deleteInstance();
}

/* The size of nothing is nothing.  This one hands back a byte count, but its guard against a null
	 command returned TRUE - so a caller sizing a buffer reserved one byte for a command that is not
	 there. */
TEST(sizing_a_null_command_asks_for_no_bytes)
{
	CHECK_EQ( (Int)NetPacketFileReader::GetBufferSizeNeededForCommand( NULL ), 0 );
}

TEST(an_overlong_transfer_filename_is_truncated_instead_of_overflowing)
{
	static const Int NAME_LEN = 400;					// _MAX_PATH is 260
	UnsignedByte buf[NAME_LEN + 1 + sizeof(UnsignedInt) + 4];
	Int at = 0;
	Int k;
	for( k = 0; k < NAME_LEN; ++k )
		buf[at++] = (UnsignedByte)('a' + (k % 26));
	buf[at++] = 0;

	UnsignedInt dataLength = 4;
	memcpy( buf + at, &dataLength, sizeof(dataLength) );
	at += sizeof(dataLength);
	buf[at++] = 0xDE; buf[at++] = 0xAD; buf[at++] = 0xBE; buf[at++] = 0xEF;

	Int i = 0;
	NetCommandMsg *msg = NetPacketFileReader::readFileMessage( buf, i );
	NetFileCommandMsg *fileMsg = (NetFileCommandMsg *)msg;

	// the name is cut to the buffer rather than written past it
	CHECK_EQ( fileMsg->getPortableFilename().getLength(), _MAX_PATH - 1 );

	// ...but the offset walked the whole name, so what follows it still parses
	CHECK_EQ( i, at );
	CHECK_EQ( (Int)fileMsg->getFileLength(), 4 );
	CHECK_EQ( (Int)fileMsg->getFileData()[0], 0xDE );
	CHECK_EQ( (Int)fileMsg->getFileData()[3], 0xEF );

	msg->detach();
}

TEST(an_overlong_announced_filename_is_truncated_instead_of_overflowing)
{
	static const Int NAME_LEN = 400;
	UnsignedByte buf[NAME_LEN + 1 + sizeof(UnsignedShort) + sizeof(UnsignedByte)];
	Int at = 0;
	Int k;
	for( k = 0; k < NAME_LEN; ++k )
		buf[at++] = (UnsignedByte)('a' + (k % 26));
	buf[at++] = 0;

	UnsignedShort fileID = 0x1234;
	memcpy( buf + at, &fileID, sizeof(fileID) );
	at += sizeof(fileID);
	buf[at++] = 0x5A;									// player mask

	Int i = 0;
	NetCommandMsg *msg = NetPacketFileReader::readFileAnnounceMessage( buf, i );
	NetFileAnnounceCommandMsg *announce = (NetFileAnnounceCommandMsg *)msg;

	CHECK_EQ( announce->getPortableFilename().getLength(), _MAX_PATH - 1 );
	CHECK_EQ( i, at );
	CHECK_EQ( (Int)announce->getFileID(), 0x1234 );
	CHECK_EQ( (Int)announce->getPlayerMask(), 0x5A );

	msg->detach();
}

/* A map transfer arrives as a filename and a block of bytes, both chosen by the other machine, and
	 nothing checked either one: whatever it named was written wherever the name resolved to, with
	 whatever was in it.  A host could hand a joining player any file it liked, anywhere the name
	 reached. */
static UnsignedByte *makeTga( Int payloadBytes, Bool withFooter )
{
	static UnsignedByte tga[512];
	memset( tga, 0, sizeof(tga) );
	if( withFooter )
	{
		UnsignedByte *sig = tga + payloadBytes - 18;
		memcpy( sig, "TRUEVISION-XFILE", 16 );
		sig[16] = '.';
		sig[17] = '\0';
	}
	return tga;
}

TEST(a_transferred_file_has_to_be_what_its_name_says_it_is)
{
	UnsignedByte text[64];
	memset( text, 'x', sizeof(text) );

	// the six kinds a transfer carries are taken
	CHECK( IsValidTransferFileContent( "maps\\foo\\foo.map", text, sizeof(text) ) );
	CHECK( IsValidTransferFileContent( "maps\\foo\\foo.ini", text, sizeof(text) ) );
	CHECK( IsValidTransferFileContent( "maps\\foo\\foo.str", text, sizeof(text) ) );
	CHECK( IsValidTransferFileContent( "maps\\foo\\foo.txt", text, sizeof(text) ) );
	CHECK( IsValidTransferFileContent( "maps\\foo\\foo.wak", text, sizeof(text) ) );

	// anything else is not
	CHECK( !IsValidTransferFileContent( "maps\\foo\\foo.exe", text, sizeof(text) ) );
	CHECK( !IsValidTransferFileContent( "maps\\foo\\foo.dll", text, sizeof(text) ) );
	CHECK( !IsValidTransferFileContent( "maps\\foo\\foo.bat", text, sizeof(text) ) );
	CHECK( !IsValidTransferFileContent( "maps\\foo\\noextension", text, sizeof(text) ) );

	// an INI that holds null bytes is a binary wearing the name
	UnsignedByte binary[64];
	memset( binary, 'x', sizeof(binary) );
	binary[13] = 0;
	CHECK( !IsValidTransferFileContent( "maps\\foo\\foo.ini", binary, sizeof(binary) ) );
	CHECK( IsValidTransferFileContent( "maps\\foo\\foo.txt", binary, sizeof(binary) ) );	// a txt may

	// the size ceiling is per kind
	CHECK( !IsValidTransferFileContent( "maps\\foo\\foo.wak", text, 256 * 1024 ) );
	CHECK( IsValidTransferFileContent( "maps\\foo\\foo.map", text, 256 * 1024 ) );

	// a targa needs its footer, and enough bytes to have one
	CHECK( IsValidTransferFileContent( "maps\\foo\\foo.tga", makeTga( 128, TRUE ), 128 ) );
	CHECK( !IsValidTransferFileContent( "maps\\foo\\foo.tga", makeTga( 128, FALSE ), 128 ) );
	CHECK( !IsValidTransferFileContent( "maps\\foo\\foo.tga", text, 8 ) );
}

/* The name a joining player sends goes into the game state string the lobby passes around, and that
	 string is parsed on commas, colons and semicolons - so a name holding one of those rewrites
	 everyone's idea of who is in the room.  It was taken exactly as sent. */
TEST(a_player_name_from_another_machine_cannot_rewrite_the_lobby)
{
	CHECK( IsUsablePlayerName( L"Olcay" ) );
	CHECK( IsUsablePlayerName( L"[GLA] scud" ) );
	CHECK( IsUsablePlayerName( L"\x00fcmit" ) );				// accents and non-latin are fine

	// the three separators the game state string is cut on
	CHECK( !IsUsablePlayerName( L"a,b" ) );
	CHECK( !IsUsablePlayerName( L"a:b" ) );
	CHECK( !IsUsablePlayerName( L"a;b" ) );

	// control characters, in both bands, and the line separators
	CHECK( !IsUsablePlayerName( L"a\nb" ) );
	CHECK( !IsUsablePlayerName( L"a\x0085" "b" ) );
	CHECK( !IsUsablePlayerName( L"a\x2028" "b" ) );

	// half a surrogate pair is not a character
	CHECK( !IsUsablePlayerName( L"a\xd800" "b" ) );

	// a name has to have something in it
	CHECK( !IsUsablePlayerName( L"" ) );
	CHECK( !IsUsablePlayerName( L"   " ) );
	CHECK( !IsUsablePlayerName( L"\x3000\x00a0" ) );			// ideographic and no-break spaces
	CHECK( !IsUsablePlayerName( NULL ) );
}

TEST(a_transferred_name_cannot_climb_out_of_its_directory)
{
	CHECK( IsSafeTransferPath( "maps\\foo\\foo.map" ) );
	CHECK( IsSafeTransferPath( "MAPS\\some map\\some map.map" ) );

	CHECK( !IsSafeTransferPath( "maps\\..\\..\\windows\\system32\\evil.map" ) );
	CHECK( !IsSafeTransferPath( "..\\evil.map" ) );
	CHECK( !IsSafeTransferPath( "maps/../evil.map" ) );
}

/* The 2003 datagram was 512 bytes all in, 476 of it payload - a quarter of what any link carries
	 today, and the reason a busy frame needs four or five datagrams where one would do.  The payload
	 is sized from a safe UDP ceiling now, and the three numbers have to stay in step: what goes on
	 the wire is the header plus the payload, and the payload is exactly what a packet may hold. */
TEST(a_datagram_is_one_udp_payload_and_the_payload_is_what_a_packet_holds)
{
	TransportMessage msg;

	CHECK_EQ( (Int)sizeof(msg.header) + MAX_PACKET_SIZE, MAX_NETWORK_MESSAGE_LEN );
	CHECK_EQ( (Int)sizeof(msg.data), MAX_PACKET_SIZE );
	CHECK( MAX_NETWORK_MESSAGE_LEN <= MAX_UDP_PAYLOAD_SIZE );
	CHECK( MAX_PACKET_SIZE > 476 );					// bigger than the 2003 datagram

	// everything that has to live inside one payload still does
	CHECK( (Int)sizeof(CommandPacket) <= MAX_PACKET_SIZE );
	CHECK( (Int)sizeof(LANMessage) <= MAX_LANAPI_PACKET_SIZE );
	CHECK( MAX_LANAPI_PACKET_SIZE <= MAX_PACKET_SIZE );
}

/* A chat message's length goes on the wire in one byte, and every site that packed or measured one
	 assigned the character count straight to it.  256 characters became a length of zero; 300 became
	 44 - and the size estimate, the room check and the copy each wrapped separately, so what went
	 out declared one length and carried another. */
static NetCommandRef *makeChat( const WideChar *body, Int length )
{
	NetChatCommandMsg *chat = newInstance( NetChatCommandMsg );
	UnicodeString text;
	Int k;
	for( k = 0; k < length; ++k )
		text.concat( body ? body[k] : (WideChar)(L'a' + (k % 26)) );
	chat->setText( text );
	chat->setPlayerMask( 0xFF );
	chat->setPlayerID( 1 );
	chat->setID( 1 );
	chat->setExecutionFrame( 0 );
	chat->setNetCommandType( NETCOMMANDTYPE_CHAT );

	NetCommandRef *ref = newInstance( NetCommandRef )( chat );
	ref->setRelay( 0 );
	chat->detach();									// the ref holds it now
	return ref;
}

TEST(an_overlong_chat_line_keeps_its_length_instead_of_wrapping_to_a_short_one)
{
	NetCommandRef *ref = makeChat( NULL, 300 );

	NetPacket *packet = newInstance( NetPacket );
	packet->init();

	CHECK( packet->addCommand( ref ) );
	CHECK_EQ( packet->getNumCommands(), 1 );

	/* 300 characters clamp to 255, which is 510 bytes of text.  With the wrapped byte the length
		 came out as 44 and the packet carried 88 bytes - so the size alone tells the two apart. */
	CHECK( packet->getLength() >= 510 );

	packet->deleteInstance();
	ref->deleteInstance();
}

/* An ally cursor is two floats on a command of its own, and the packet writer and the packet
	 reader are two separate hand-written walks over the same bytes - a field added to one and not
	 the other reads the next command's header as position data.  So the round trip is pinned: what
	 goes in comes out, on the right slot, as the right command type. */
TEST(an_ally_cursor_survives_the_round_trip_through_a_packet)
{
	const Real sentX = 1234.5f;
	const Real sentY = -678.25f;

	NetAllyCursorCommandMsg *cursor = newInstance( NetAllyCursorCommandMsg );
	cursor->setPosition( sentX, sentY );
	cursor->setPlayerID( 3 );
	cursor->setID( 0 );
	cursor->setExecutionFrame( 0 );

	NetCommandRef *ref = newInstance( NetCommandRef )( cursor );
	ref->setRelay( 0x0F );
	cursor->detach();								// the ref holds it now

	NetPacket *packet = newInstance( NetPacket );
	packet->init();
	CHECK( packet->addCommand( ref ) );
	CHECK_EQ( packet->getNumCommands(), 1 );

	NetCommandList *list = packet->getCommandList();
	NetCommandRef *read = list->getFirstMessage();
	CHECK( read != NULL );
	if( read )
	{
		NetCommandMsg *msg = read->getCommand();
		CHECK_EQ( (Int)msg->getNetCommandType(), (Int)NETCOMMANDTYPE_ALLYCURSOR );
		CHECK_EQ( (Int)msg->getPlayerID(), 3 );
		CHECK_EQ( ((NetAllyCursorCommandMsg *)msg)->getX(), sentX );
		CHECK_EQ( ((NetAllyCursorCommandMsg *)msg)->getY(), sentY );
	}

	list->deleteInstance();
	packet->deleteInstance();
	ref->deleteInstance();
}

/* Nothing about an ally cursor may reach the simulation: it is not acked, not resent, not held for
	 a frame and not written to a replay.  The three predicates the network layer sorts commands by
	 are where that is decided, so they are the thing to pin. */
TEST(an_ally_cursor_is_never_acked_resent_or_frame_synchronised)
{
	NetAllyCursorCommandMsg *cursor = newInstance( NetAllyCursorCommandMsg );

	CHECK( !DoesCommandRequireACommandID( NETCOMMANDTYPE_ALLYCURSOR ) );
	CHECK( !IsCommandSynchronized( NETCOMMANDTYPE_ALLYCURSOR ) );
	CHECK( !CommandRequiresAck( cursor ) );

	cursor->detach();								// the constructor's own reference is the only one
}

/* addCommand's own guard against a null reference sat below the line that followed it, so the one
	 call it exists to survive was the one call that crashed. */
TEST(adding_a_null_command_to_a_packet_is_refused_not_followed)
{
	NetPacket *packet = newInstance( NetPacket );
	packet->init();

	CHECK( packet->addCommand( NULL ) );			// nothing to add, and nothing to dereference
	CHECK_EQ( packet->getNumCommands(), 0 );

	packet->deleteInstance();
}

/* A command too big for one packet is sent as numbered chunks and reassembled into one heap block
	 sized from the first chunk's claim.  Each later chunk then said where it went and how long it
	 was, and neither number was checked before the memcpy - so a peer could place its bytes anywhere
	 it liked relative to that block.  The offset, the length and their sum are all checked now, and
	 a refused chunk no longer counts towards the command being complete. */
static NetWrapperCommandMsg *makeWrapperChunk( UnsignedInt chunkNumber, UnsignedInt numChunks,
	UnsignedInt totalDataLength, UnsignedInt offset, UnsignedByte *data, UnsignedInt dataLength )
{
	NetWrapperCommandMsg *msg = newInstance( NetWrapperCommandMsg );
	msg->setWrappedCommandID( 42 );
	msg->setNumChunks( numChunks );
	msg->setTotalDataLength( totalDataLength );
	msg->setChunkNumber( chunkNumber );
	msg->setDataOffset( offset );
	msg->setData( data, dataLength );
	return msg;
}

TEST(a_wrapped_command_chunk_cannot_write_outside_its_own_buffer)
{
	UnsignedByte first[4]  = { 0x11, 0x22, 0x33, 0x44 };
	UnsignedByte second[4] = { 0x55, 0x66, 0x77, 0x88 };

	NetWrapperCommandMsg *chunk0 = makeWrapperChunk( 0, 2, 8, 0, first, 4 );
	NetCommandWrapperListNode *node = newInstance( NetCommandWrapperListNode )( chunk0 );

	CHECK_EQ( (Int)node->getRawDataLength(), 8 );

	node->copyChunkData( chunk0 );					// the honest first half
	CHECK( !node->isComplete() );
	CHECK_EQ( node->getPercentComplete(), 50 );

	// an offset past the end of the block
	NetWrapperCommandMsg *farOffset = makeWrapperChunk( 1, 2, 8, 4096, second, 4 );
	node->copyChunkData( farOffset );
	CHECK( !node->isComplete() );
	CHECK_EQ( node->getPercentComplete(), 50 );		// refused, and not counted as received
	farOffset->detach();

	// a length no packet could have carried.  The chunk's own source buffer is that long for real:
	// setData copies dataLength bytes out of it, so handing it a short array reads off the end here
	// instead of testing the guard on the other side.
	UnsignedByte oversized[MAX_PACKET_SIZE + 1];
	memset( oversized, 0x99, sizeof(oversized) );
	NetWrapperCommandMsg *hugeLength = makeWrapperChunk( 1, 2, 8, 4, oversized, MAX_PACKET_SIZE + 1 );
	node->copyChunkData( hugeLength );
	CHECK( !node->isComplete() );
	hugeLength->detach();

	// in range on its own, but the tail runs off the end
	NetWrapperCommandMsg *overhang = makeWrapperChunk( 1, 2, 8, 6, second, 4 );
	node->copyChunkData( overhang );
	CHECK( !node->isComplete() );
	overhang->detach();

	// and the honest second half still completes the command
	NetWrapperCommandMsg *chunk1 = makeWrapperChunk( 1, 2, 8, 4, second, 4 );
	node->copyChunkData( chunk1 );
	CHECK( node->isComplete() );
	CHECK_EQ( node->getPercentComplete(), 100 );
	CHECK_MEM( first, node->getRawData(), 4 );
	CHECK_MEM( second, node->getRawData() + 4, 4 );
	chunk1->detach();

	node->deleteInstance();
	chunk0->detach();
}

//////////////////////////////////////////////////////////////////////////////
// Starting a new game
//////////////////////////////////////////////////////////////////////////////

/* MSG_NEW_GAME comes down the message stream like any other command, and the dispatcher used to
	 act on it whatever the game was doing.  Handed one mid-match, a machine tears down the game the
	 others are still playing and they wait in the disconnect screen for frames that never come. */
TEST(a_new_game_only_starts_from_a_standing_start)
{
	// nothing running: the only state a new game may begin from
	CHECK( IsReadyToStartNewGame( FALSE, FALSE, FALSE ) );

	// each of the three on its own is enough to refuse
	CHECK( !IsReadyToStartNewGame( TRUE,  FALSE, FALSE ) );		// a match is already running
	CHECK( !IsReadyToStartNewGame( FALSE, TRUE,  FALSE ) );		// the last one is still being torn down
	CHECK( !IsReadyToStartNewGame( FALSE, FALSE, TRUE  ) );		// a map is part way through loading

	// and no combination of them lets one through
	Bool refusedEveryBusyState = TRUE;
	for( Int bits = 1; bits < 8; ++bits )
		if( IsReadyToStartNewGame( (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0 ) )
			refusedEveryBusyState = FALSE;
	CHECK( refusedEveryBusyState );
}

/* A network command names the object it acts on by id, and nothing in the message ties that id to
	 the machine that sent it.  The special power cases ran whatever object they were handed, so a
	 doctored command could fire another player's superweapon.  They ask this first now.  The
	 predicate compares identity only and never dereferences either pointer, so the test can use
	 stand-in addresses for players. */
TEST(a_command_may_only_name_an_object_its_sender_controls)
{
	const Player *alice = (const Player *)0x100;
	const Player *bob   = (const Player *)0x200;

	CHECK( isPlayerCommandingOwnObject( alice, alice ) );	// your own unit
	CHECK( !isPlayerCommandingOwnObject( alice, bob ) );	// somebody else's
	CHECK( !isPlayerCommandingOwnObject( bob, alice ) );

	// an object with no controller is nobody's to command, and neither is anything at all when the
	// message names a player slot that is not in the game
	CHECK( !isPlayerCommandingOwnObject( alice, NULL ) );
	CHECK( !isPlayerCommandingOwnObject( NULL, alice ) );
	CHECK( !isPlayerCommandingOwnObject( NULL, NULL ) );
}

/* A player index that arrives from outside this machine is only a number: the replay reader freads
	 it over a -1 and never checks the read, and a network command carries whatever the sending
	 machine put in it.  getNthPlayer answers NULL for anything outside the list, and the logic
	 dispatcher used to assert - i.e. do nothing in a release build - and dereference it anyway. */
TEST(a_player_index_from_outside_this_machine_is_bounded_before_use)
{
	// the whole list, and nothing either side of it
	CHECK( isValidPlayerIndex( 0 ) );
	CHECK( isValidPlayerIndex( MAX_PLAYER_COUNT - 1 ) );
	CHECK( !isValidPlayerIndex( MAX_PLAYER_COUNT ) );
	CHECK( !isValidPlayerIndex( -1 ) );		// what a failed fread of a replay leaves behind

	// a truncated or hostile value off the wire
	CHECK( !isValidPlayerIndex( 0x7fffffff ) );
	CHECK( !isValidPlayerIndex( (Int)0x80000000 ) );
	CHECK( !isValidPlayerIndex( -12345 ) );

	Bool acceptedOnlyTheList = TRUE;
	for( Int i = -64; i < 64; ++i )
		if( isValidPlayerIndex( i ) != (i >= 0 && i < MAX_PLAYER_COUNT) )
			acceptedOnlyTheList = FALSE;
	CHECK( acceptedOnlyTheList );
}

/* Starting a game used to seed only the logic stream.  The client and audio streams kept the
	 time_t the process was seeded with at startup, so they read differently on every machine in the
	 room and differently again on every playback of the same replay - and any draw that leaks out of
	 the client stream into something the simulation can see is then a divergence.  A game's seed now
	 sets all three, and the logic stream still lands exactly where it did. */
TEST(a_games_seed_sets_the_client_and_audio_streams_as_well_as_the_logic_one)
{
	const Int DRAWS = 16;
	Int client[DRAWS], audio[DRAWS], logic[DRAWS];

	InitRandom( 31337 );
	const UnsignedInt logicSeedAfterInit = GetGameLogicRandomSeedCRC();
	for( Int i = 0; i < DRAWS; ++i )
	{
		client[i] = GameClientRandomValue( 0, 1000000 );
		audio[i]  = GameAudioRandomValue( 0, 1000000 );
		logic[i]  = GetGameLogicRandomValue( 0, 1000000, __FILE__, __LINE__ );
	}

	// the same seed on the machine next to you, or on the same machine tomorrow, reads the same
	InitRandom( 31337 );
	CHECK_EQ( GetGameLogicRandomSeedCRC(), logicSeedAfterInit );
	Bool clientRepeated = TRUE, audioRepeated = TRUE, logicRepeated = TRUE;
	for( Int i = 0; i < DRAWS; ++i )
	{
		if( GameClientRandomValue( 0, 1000000 ) != client[i] ) clientRepeated = FALSE;
		if( GameAudioRandomValue( 0, 1000000 )  != audio[i]  ) audioRepeated  = FALSE;
		if( GetGameLogicRandomValue( 0, 1000000, __FILE__, __LINE__ ) != logic[i] ) logicRepeated = FALSE;
	}
	CHECK( clientRepeated );
	CHECK( audioRepeated );
	CHECK( logicRepeated );

	// and a different game is a different game, in all three
	InitRandom( 31338 );
	CHECK_NE( GetGameLogicRandomSeedCRC(), logicSeedAfterInit );
	Bool clientMoved = FALSE, audioMoved = FALSE, logicMoved = FALSE;
	for( Int i = 0; i < DRAWS; ++i )
	{
		if( GameClientRandomValue( 0, 1000000 ) != client[i] ) clientMoved = TRUE;
		if( GameAudioRandomValue( 0, 1000000 )  != audio[i]  ) audioMoved  = TRUE;
		if( GetGameLogicRandomValue( 0, 1000000, __FILE__, __LINE__ ) != logic[i] ) logicMoved = TRUE;
	}
	CHECK( clientMoved );
	CHECK( audioMoved );
	CHECK( logicMoved );
}

/* A map's INI overrides an object template by hanging a copy off the end of the original's chain,
	 under the same name.  findTemplate hands back the head of that chain; Object's constructor walks
	 to the end.  WeaponSet::xfer wrote a live object's template name into the save and looked it up
	 again on load without walking, so a unit on an overridden map came back holding the weapon set of
	 the template the override replaced.  Both sites go through finalOverrideOf now.

	 The chain lives entirely in Overridable, and finalOverrideOf touches nothing else, so the test
	 builds it out of plain Overridables: a ThingTemplate cannot be constructed here, its constructor
	 reads TheGlobalData.  ThingTemplate's only base is Overridable, non-virtually, so the cast is an
	 identity on the pointer. */
TEST(a_template_looked_up_by_name_is_taken_to_the_end_of_its_override_chain)
{
	Overridable *base     = newInstance( Overridable );
	Overridable *override = newInstance( Overridable );
	Overridable *later    = newInstance( Overridable );

	#define AS_TEMPLATE(p) ((const ThingTemplate *)(const Overridable *)(p))

	// nothing overrides it yet, so the answer is the template itself
	CHECK( finalOverrideOf( AS_TEMPLATE(base) ) == AS_TEMPLATE(base) );

	// one override, then a second hung off the first - which is how newOverride builds them
	base->setNextOverride( override );
	CHECK( finalOverrideOf( AS_TEMPLATE(base) ) == AS_TEMPLATE(override) );

	override->setNextOverride( later );
	CHECK( finalOverrideOf( AS_TEMPLATE(base) ) == AS_TEMPLATE(later) );
	CHECK( finalOverrideOf( AS_TEMPLATE(override) ) == AS_TEMPLATE(later) );
	CHECK( finalOverrideOf( AS_TEMPLATE(later) ) == AS_TEMPLATE(later) );

	// and a name that matched nothing stays nothing rather than being dereferenced
	CHECK( finalOverrideOf( NULL ) == NULL );

	#undef AS_TEMPLATE

	// one delete, not three: ~Overridable deletes whatever the chain still points at
	base->deleteInstance();
}

/* The simulation math fingerprint exists so that two players comparing mismatch dumps can tell
	 "our arithmetic differs" from "our game states diverged".  That only works if the number is a
	 function of the machine's math alone - in particular, it must not depend on whatever FPU mode
	 the caller happened to be in, and it must not leave that mode changed behind it. */
TEST(the_simulation_math_fingerprint_is_the_machines_math_and_not_the_callers_fpu_mode)
{
	/* Three callers, three FPU modes, none of them the one the simulation runs in unless it happens
		 to be: 53-bit precision is what the C runtime starts a process in and what a driver that
		 resets the FPU leaves behind, and round-to-chop is a mode the game never wants but nothing
		 stops a plugin from leaving set.  All three must produce the same fingerprint. */

	setFPMode();
	const UnsignedInt fromSimulationMode = SimulationMathCrc::calculate();

	// a real value, not a CRC of nothing at all, and repeatable
	CHECK( fromSimulationMode != 0 );
	CHECK_EQ( SimulationMathCrc::calculate(), fromSimulationMode );

	_controlfp( _PC_53, _MCW_PC );
	const UnsignedInt modeIn53 = getFPMode();
	CHECK( modeIn53 != expectedFPMode() );
	CHECK_EQ( SimulationMathCrc::calculate(), fromSimulationMode );
	CHECK_EQ( getFPMode(), modeIn53 );	// and the caller's mode is still the caller's

	_controlfp( _PC_64 | _RC_CHOP, _MCW_PC | _MCW_RC );
	const UnsignedInt modeInChop = getFPMode();
	CHECK( modeInChop != expectedFPMode() );
	CHECK_EQ( SimulationMathCrc::calculate(), fromSimulationMode );
	CHECK_EQ( getFPMode(), modeInChop );

	// and back where the rest of the tests expect to find it
	setFPMode();
	CHECK_EQ( getFPMode(), expectedFPMode() );
}

// ------------------------------------------------------------------------------------------------
// The radar's shroud layer.  It used to be poked one pixel at a time straight into a Direct3D
// texture, with a lock and an unlock around each pixel; it is a main-memory buffer now, and the
// texture hears about it once a frame, over the rectangle that changed.

TEST(radar_shroud_cache_starts_owing_the_whole_texture_a_write)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );

	CHECK( cache.isDirty() );
	CHECK_EQ( cache.getDirtyMinX(), 0 );
	CHECK_EQ( cache.getDirtyMinY(), 0 );
	CHECK_EQ( cache.getDirtyMaxX(), 127 );
	CHECK_EQ( cache.getDirtyMaxY(), 127 );
	CHECK_EQ( (Int)cache.getAlpha( 0, 0 ), 0 );
	CHECK_EQ( (Int)cache.getAlpha( 127, 127 ), 0 );
}

TEST(radar_shroud_cache_grows_its_dirty_rectangle_around_what_changed)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );

	// pretend the frame it was created in has been drawn
	UnsignedByte surface[ 128 * 128 * 4 ];
	cache.flushTo( surface, 128 * 4, 4 );
	CHECK( !cache.isDirty() );

	cache.setAlpha( 10, 20, 255 );
	CHECK( cache.isDirty() );
	CHECK_EQ( cache.getDirtyMinX(), 10 );
	CHECK_EQ( cache.getDirtyMaxX(), 10 );
	CHECK_EQ( cache.getDirtyMinY(), 20 );
	CHECK_EQ( cache.getDirtyMaxY(), 20 );

	// a second, far away pixel: the rectangle is the union, not the last one written
	cache.setAlpha( 3, 90, 127 );
	CHECK_EQ( cache.getDirtyMinX(), 3 );
	CHECK_EQ( cache.getDirtyMaxX(), 10 );
	CHECK_EQ( cache.getDirtyMinY(), 20 );
	CHECK_EQ( cache.getDirtyMaxY(), 90 );

	// and one inside it changes nothing about the bounds
	cache.setAlpha( 5, 50, 1 );
	CHECK_EQ( cache.getDirtyMinX(), 3 );
	CHECK_EQ( cache.getDirtyMaxX(), 10 );
	CHECK_EQ( cache.getDirtyMinY(), 20 );
	CHECK_EQ( cache.getDirtyMaxY(), 90 );

	CHECK_EQ( (Int)cache.getAlpha( 10, 20 ), 255 );
	CHECK_EQ( (Int)cache.getAlpha( 3, 90 ), 127 );
	CHECK_EQ( (Int)cache.getAlpha( 5, 50 ), 1 );
}

TEST(radar_shroud_cache_ignores_writes_that_change_nothing)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );
	UnsignedByte surface[ 128 * 128 * 4 ];
	cache.flushTo( surface, 128 * 4, 4 );

	// the shroud is written cell by cell every time a unit moves, and most of those writes say
	// what the pixel already said
	cache.setAlpha( 40, 40, 0 );
	CHECK( !cache.isDirty() );

	cache.setAlpha( 40, 40, 255 );
	CHECK( cache.isDirty() );
	cache.flushTo( surface, 128 * 4, 4 );
	cache.setAlpha( 40, 40, 255 );
	CHECK( !cache.isDirty() );
}

TEST(radar_shroud_cache_drops_points_off_the_texture)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );
	UnsignedByte surface[ 128 * 128 * 4 ];
	cache.flushTo( surface, 128 * 4, 4 );

	// the caller walks a rectangle of map cells and some of them fall outside the radar
	cache.setAlpha( -1, 40, 255 );
	cache.setAlpha( 40, -1, 255 );
	cache.setAlpha( 128, 40, 255 );
	cache.setAlpha( 40, 128, 255 );
	cache.setAlpha( 10000, 10000, 255 );
	CHECK( !cache.isDirty() );
	CHECK_EQ( (Int)cache.getAlpha( -1, 40 ), 0 );
	CHECK_EQ( (Int)cache.getAlpha( 128, 128 ), 0 );

	// the last legal pixel is legal
	cache.setAlpha( 127, 127, 255 );
	CHECK( cache.isDirty() );
	CHECK_EQ( (Int)cache.getAlpha( 127, 127 ), 255 );
}

TEST(radar_shroud_cache_writes_only_the_dirty_rectangle_into_the_surface)
{
	enum { W = 16, H = 8, PITCH = 128 };			// a pitch wider than the rows, like a real lock
	RadarShroudCache cache;
	cache.setSize( W, H );

	UnsignedByte surface[ PITCH * H ];
	memset( surface, 0xCD, sizeof( surface ) );
	cache.flushTo( surface, PITCH, 4 );				// the opening flush covers everything
	CHECK( !cache.isDirty() );

	// past the end of each row nothing was touched
	for( Int y = 0; y < H; y++ )
		for( Int x = W * 4; x < PITCH; x++ )
			CHECK_EQ( (Int)surface[ y * PITCH + x ], 0xCD );

	memset( surface, 0xCD, sizeof( surface ) );
	cache.setAlpha( 2, 3, 255 );
	cache.setAlpha( 4, 5, 127 );
	cache.flushTo( surface, PITCH, 4 );

	// rows 3..5, columns 2..4, and not one byte more
	Int written = 0;
	for( Int y = 0; y < H; y++ )
	{
		for( Int x = 0; x < W; x++ )
		{
			UnsignedInt pixel;
			memcpy( &pixel, surface + y * PITCH + x * 4, sizeof( pixel ) );
			const Bool inside = ( y >= 3 && y <= 5 && x >= 2 && x <= 4 );
			if( inside )
			{
				++written;
				CHECK_EQ( pixel, ((UnsignedInt)cache.getAlpha( x, y )) << 24 );
			}
			else
			{
				CHECK_EQ( pixel, 0xCDCDCDCD );
			}
		}
	}
	CHECK_EQ( written, 9 );
	CHECK( !cache.isDirty() );

	// and a flush with nothing to say does not touch the surface at all
	memset( surface, 0xCD, sizeof( surface ) );
	cache.flushTo( surface, PITCH, 4 );
	for( Int i = 0; i < (Int)sizeof( surface ); i++ )
		CHECK_EQ( (Int)surface[ i ], 0xCD );
}

TEST(radar_shroud_cache_puts_the_alpha_where_the_pixel_format_wants_it)
{
	enum { W = 4, H = 2 };
	RadarShroudCache cache;
	cache.setSize( W, H );
	cache.setAlpha( 1, 1, 255 );
	cache.setAlpha( 2, 1, 127 );

	// eight bits of alpha, in the top byte, black underneath: GameMakeColor( 0, 0, 0, alpha )
	UnsignedInt wide[ W * H ];
	memset( wide, 0, sizeof( wide ) );
	cache.flushTo( wide, W * 4, 4 );
	CHECK_EQ( wide[ 1 * W + 1 ], 0xFF000000 );
	CHECK_EQ( wide[ 1 * W + 2 ], 0x7F000000 );
	CHECK_EQ( wide[ 0 * W + 0 ], 0u );

	// four bits of it, in the top nibble.  The DrawPixel this replaces masked the colour with
	// 0xFFFF here and threw the whole alpha away.
	cache.clear( 0 );
	cache.setAlpha( 1, 1, 255 );
	cache.setAlpha( 2, 1, 127 );
	UnsignedShort narrow[ W * H ];
	memset( narrow, 0, sizeof( narrow ) );
	cache.flushTo( narrow, W * 2, 2 );
	CHECK_EQ( (Int)narrow[ 1 * W + 1 ], 0xF000 );
	CHECK_EQ( (Int)narrow[ 1 * W + 2 ], 0x7000 );
	CHECK_EQ( (Int)narrow[ 0 * W + 0 ], 0 );
}

TEST(radar_shroud_cache_clear_owes_the_whole_texture_a_write_again)
{
	RadarShroudCache cache;
	cache.setSize( 128, 128 );
	UnsignedByte surface[ 128 * 128 * 4 ];
	cache.flushTo( surface, 128 * 4, 4 );
	cache.setAlpha( 60, 60, 255 );
	cache.flushTo( surface, 128 * 4, 4 );

	cache.clear( 0 );
	CHECK( cache.isDirty() );
	CHECK_EQ( cache.getDirtyMinX(), 0 );
	CHECK_EQ( cache.getDirtyMinY(), 0 );
	CHECK_EQ( cache.getDirtyMaxX(), 127 );
	CHECK_EQ( cache.getDirtyMaxY(), 127 );
	CHECK_EQ( (Int)cache.getAlpha( 60, 60 ), 0 );
}

// ---------------------------------------------------------------------------------------------
// A listbox scroll offset is a pixel offset into the running height of every entry above it.
// Both of the fields that carry it used to be Short while the running total they are compared
// with, and assigned from, is an Int.
// ---------------------------------------------------------------------------------------------
TEST(listbox_scroll_offset_survives_a_list_taller_than_a_signed_short)
{
	ListboxData list;
	memset( &list, 0, sizeof( list ) );

	// four thousand chat lines or replay files at fifteen pixels a row
	list.totalHeight = 60000;
	list.displayHeight = 200;
	list.displayPos = 45000;

	CHECK_EQ( list.displayPos, 45000 );
	CHECK( list.displayPos > 0 );
	CHECK( list.displayPos + list.displayHeight <= list.totalHeight );

	// scrolling to the bottom, the way the slider and the mouse wheel do it
	list.displayPos = list.totalHeight - list.displayHeight;
	CHECK_EQ( list.displayPos, 59800 );
	CHECK( list.displayPos > 0 );
}

// ------------------------------------------------------------------------------------------------
// The trigonometry the simulation runs on.
//
// IEEE 754 pins +, -, *, / and sqrt: every machine rounds those identically, so the simulation was
// always safe there.  It says nothing at all about sin, cos, atan2, asin, acos or pow, and the
// implementations duly differ - ucrtbase dispatches on the host CPU and ships with Windows rather
// than with the game, and x87's FSIN and FCOS are microcoded differently by Intel and by AMD.  In a
// lockstep simulation one bit of disagreement about a unit's facing is two different games a second
// later.  Those calls now go through WWMath's DetTrig, which is an integer table.
// ------------------------------------------------------------------------------------------------

/* The trig fingerprint is the other half of the mismatch dump.  The first number in that dump is
	 the machine's C runtime and is allowed to differ between two players; this one is not, because
	 it is what the simulation actually computes with.  If it ever differs across two machines the
	 desync is in the arithmetic and nowhere else. */
TEST(simulation_trig_fingerprint_is_pinned)
{
	setFPMode();

	const UnsignedInt fingerprint = SimulationMathCrc::calculateSimulationTrig();

	/* Pinned to a literal on purpose: a table regenerated from Tools/gentrigtables.py, a changed
		 scale in dettrig.cpp, or a compiler that starts contracting the interpolation differently all
		 move every angle in the simulation, and this is where that gets noticed rather than in
		 somebody's replay.  Update it deliberately, never to make the build green. */
	const UnsignedInt expected = 0xEACAF02Bu;
	if (fingerprint != expected)
		printf("    simulation trig fingerprint is 0x%8.8X, expected 0x%8.8X\n", fingerprint, expected);
	CHECK_EQ(fingerprint, expected);

	// and, like the runtime one, it must not depend on the FPU mode the caller was in
	CHECK_EQ(SimulationMathCrc::calculateSimulationTrig(), fingerprint);
	_controlfp(_PC_53, _MCW_PC);
	CHECK_EQ(SimulationMathCrc::calculateSimulationTrig(), fingerprint);
	_controlfp(_PC_64 | _RC_CHOP, _MCW_PC | _MCW_RC);
	CHECK_EQ(SimulationMathCrc::calculateSimulationTrig(), fingerprint);

	setFPMode();
	CHECK_EQ(getFPMode(), expectedFPMode());
}

/* Strip C and C++ comments and the contents of string and character literals, so the scanner below
	 reads code and nothing else.  EA's own comments talk about sin() and acos() in several places. */
static void stripCommentsAndLiterals(char *text, size_t length)
{
	size_t i = 0;
	while (i < length)
	{
		if (text[i] == '/' && i + 1 < length && text[i + 1] == '/')
		{
			while (i < length && text[i] != '\n')
				text[i++] = ' ';
		}
		else if (text[i] == '/' && i + 1 < length && text[i + 1] == '*')
		{
			text[i++] = ' ';
			text[i++] = ' ';
			while (i < length && !(text[i] == '*' && i + 1 < length && text[i + 1] == '/'))
				text[i] = (text[i] == '\n') ? '\n' : ' ', ++i;
			if (i < length) text[i++] = ' ';
			if (i < length) text[i++] = ' ';
		}
		else if (text[i] == '"' || text[i] == '\'')
		{
			const char quote = text[i];
			text[i++] = ' ';
			while (i < length && text[i] != quote)
			{
				if (text[i] == '\\' && i + 1 < length)
					text[i++] = ' ';
				if (i < length)
					text[i++] = ' ';
			}
			if (i < length) text[i++] = ' ';
		}
		else
		{
			++i;
		}
	}
}

static bool isIdentChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* The C runtime names the simulation may not call.  Everything here is implementation-defined;
	 sqrt, fabs, floor, ceil and fmod are absent because IEEE pins them and they are fine.

	 Bare "log" is absent too, and that is a compromise: <math.h>'s log collides with the engine's
	 own logging methods, so scanning for it is all false positives.  logf and log10 cover the form
	 anything doing real math would actually write. */
static const char *const theForbiddenMathNames[] = {
	"sin", "sinf", "cos", "cosf", "tan", "tanf",
	"asin", "asinf", "acos", "acosf", "atan", "atanf", "atan2", "atan2f",
	"pow", "powf", "exp", "expf", "logf", "log10", "log10f",
	"sinh", "sinhf", "cosh", "coshf", "tanh", "tanhf",
	NULL
};

static int scanSourceForRuntimeMath(const char *path, const char *displayName)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return 0;

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *text = (char *)malloc((size_t)size + 1);
	size_t got = fread(text, 1, (size_t)size, fp);
	fclose(fp);
	text[got] = 0;

	stripCommentsAndLiterals(text, got);

	int hits = 0;
	for (int n = 0; theForbiddenMathNames[n] != NULL; ++n)
	{
		const char *name = theForbiddenMathNames[n];
		const size_t len = strlen(name);
		const char *at = text;
		while ((at = strstr(at, name)) != NULL)
		{
			const char *after = at + len;
			const char *before = (at == text) ? NULL : at - 1;

			// a whole identifier, called: nothing glued to either end, an open paren after it
			bool wholeWord = (before == NULL || !isIdentChar(*before)) && !isIdentChar(*after);
			const char *p = after;
			while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
			bool called = (*p == '(');

			/* Not a member: "->log(", ".log(" and "::log(" are the engine's own, and a declaration
				 like "void log(" is one too - only a call has a value in front of it. */
			const char *q = before;
			while (q != NULL && q > text && (*q == ' ' || *q == '\t')) --q;
			bool member = (q != NULL && (*q == '.' || *q == '>' || *q == ':'));

			if (wholeWord && called && !member)
			{
				if (hits < 4)
				{
					int line = 1;
					for (const char *c = text; c < at; ++c)
						if (*c == '\n') ++line;
					printf("    %s:%d calls %s()\n", displayName, line, name);
				}
				++hits;
			}
			at = after;
		}
	}

	free(text);
	return hits;
}

static int scanTreeForRuntimeMath(const char *dir, const char *display, int *filesScanned)
{
	char pattern[MAX_PATH];
	sprintf(pattern, "%s\\*", dir);

	WIN32_FIND_DATAA find;
	HANDLE h = FindFirstFileA(pattern, &find);
	if (h == INVALID_HANDLE_VALUE)
		return 0;

	int hits = 0;
	do
	{
		if (find.cFileName[0] == '.')
			continue;

		char child[MAX_PATH];
		char childDisplay[MAX_PATH];
		sprintf(child, "%s\\%s", dir, find.cFileName);
		sprintf(childDisplay, "%s/%s", display, find.cFileName);

		if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			hits += scanTreeForRuntimeMath(child, childDisplay, filesScanned);
			continue;
		}

		const char *dot = strrchr(find.cFileName, '.');
		if (dot == NULL || (strcmp(dot, ".cpp") != 0 && strcmp(dot, ".h") != 0))
			continue;

		/* SimulationMathCrc.cpp is the deliberate exception: its whole job is to fingerprint the
			 machine's own runtime math, so it has to call it.  MiniLog defines a method named log. */
		if (strcmp(find.cFileName, "SimulationMathCrc.cpp") == 0
			|| strcmp(find.cFileName, "MiniLog.cpp") == 0
			|| strcmp(find.cFileName, "MiniLog.h") == 0)
			continue;

		++(*filesScanned);
		hits += scanSourceForRuntimeMath(child, childDisplay);
	}
	while (FindNextFileA(h, &find));

	FindClose(h);
	return hits;
}

/* Promised by Libraries/Include/Lib/Trig.h, and the only thing that keeps the conversion from
	 rotting: nothing stops the next person from typing sinf(). */
TEST(simulation_uses_no_runtime_trig)
{
	static const char *const roots[] = {
		"Source\\GameLogic", "Source\\Common", "Include\\GameLogic", "Include\\Common", NULL
	};

	int filesScanned = 0;
	int hits = 0;
	for (int i = 0; roots[i] != NULL; ++i)
	{
		char dir[MAX_PATH];
		sprintf(dir, "%s\\%s", GAMEENGINE_SOURCE_DIR, roots[i]);
		hits += scanTreeForRuntimeMath(dir, roots[i], &filesScanned);
	}

	// a scanner that found nothing because it looked nowhere would pass silently
	CHECK(filesScanned > 500);
	if (hits != 0)
		printf("    %d runtime math call(s) in the simulation; use Lib/Trig.h\n", hits);
	CHECK_EQ(hits, 0);
}

/* The build placement preview draws the door of the structure it is carrying before that structure
	 exists, so it cannot go through ExitInterface - it asks the production exit module's *data* for
	 the two points instead.  A module that produces nothing must keep answering no, or every wall
	 segment would sprout a rally line out of (0,0,0). */
TEST(production_exit_points_come_off_the_module_data_before_there_is_an_object)
{
	Coord3D create, natural;

	DefaultProductionExitUpdateModuleData def;
	def.m_unitCreatePoint.x = 10.0f;   def.m_unitCreatePoint.y = -3.0f;  def.m_unitCreatePoint.z = 1.0f;
	def.m_naturalRallyPoint.x = 40.0f; def.m_naturalRallyPoint.y = 5.0f; def.m_naturalRallyPoint.z = 0.0f;

	CHECK( def.getProductionExitPointsInModelSpace( create, natural ) );
	CHECK_NEAR( create.x, 10.0f, 0.0001f );
	CHECK_NEAR( create.y, -3.0f, 0.0001f );
	CHECK_NEAR( create.z, 1.0f, 0.0001f );
	CHECK_NEAR( natural.x, 40.0f, 0.0001f );
	CHECK_NEAR( natural.y, 5.0f, 0.0001f );

	// the war factory / barracks style queue module and the supply center answer the same way
	QueueProductionExitUpdateModuleData queue;
	queue.m_unitCreatePoint.x = 7.0f;
	queue.m_naturalRallyPoint.x = 70.0f;
	CHECK( queue.getProductionExitPointsInModelSpace( create, natural ) );
	CHECK_NEAR( create.x, 7.0f, 0.0001f );
	CHECK_NEAR( natural.x, 70.0f, 0.0001f );

	SupplyCenterProductionExitUpdateModuleData supply;
	supply.m_unitCreatePoint.x = -8.0f;
	supply.m_naturalRallyPoint.x = -80.0f;
	CHECK( supply.getProductionExitPointsInModelSpace( create, natural ) );
	CHECK_NEAR( create.x, -8.0f, 0.0001f );
	CHECK_NEAR( natural.x, -80.0f, 0.0001f );

	// and anything that is not a production exit module says no and touches nothing
	UpdateModuleData notAnExit;
	create.x = 1234.0f;
	natural.x = 5678.0f;
	CHECK( notAnExit.getProductionExitPointsInModelSpace( create, natural ) == FALSE );
	CHECK_NEAR( create.x, 1234.0f, 0.0001f );
	CHECK_NEAR( natural.x, 5678.0f, 0.0001f );
}

/* Those points are in model space, and the preview line is only useful if it swings around with
	 the building as the player wheels it.  This is the transform the renderer applies: a structure
	 dropped at (100,200) facing a quarter turn left puts a door that is 10 units "ahead" of the
	 model origin 10 units to the north of the building, not 10 units east of it. */
TEST(a_placed_structures_exit_point_turns_with_the_structure)
{
	Coord3D create, natural;

	DefaultProductionExitUpdateModuleData def;
	def.m_unitCreatePoint.x = 10.0f;   def.m_unitCreatePoint.y = 0.0f;   def.m_unitCreatePoint.z = 0.0f;
	def.m_naturalRallyPoint.x = 30.0f; def.m_naturalRallyPoint.y = 0.0f; def.m_naturalRallyPoint.z = 0.0f;
	CHECK( def.getProductionExitPointsInModelSpace( create, natural ) );

	Matrix3D transform;
	transform.Make_Identity();
	transform.Rotate_Z( PI / 2.0f );
	transform.Set_Translation( Vector3( 100.0f, 200.0f, 5.0f ) );

	Vector3 exitLoc( create.x, create.y, create.z );
	transform.Transform_Vector( transform, exitLoc, &exitLoc );
	CHECK_NEAR( exitLoc.X, 100.0f, 0.001f );
	CHECK_NEAR( exitLoc.Y, 210.0f, 0.001f );
	CHECK_NEAR( exitLoc.Z, 5.0f, 0.001f );

	Vector3 rallyLoc( natural.x, natural.y, natural.z );
	transform.Transform_Vector( transform, rallyLoc, &rallyLoc );
	CHECK_NEAR( rallyLoc.X, 100.0f, 0.001f );
	CHECK_NEAR( rallyLoc.Y, 230.0f, 0.001f );

	// the two points are distinct, which is what tells the renderer to draw a line and not just a puck
	CHECK( !(exitLoc == rallyLoc) );
}

/** SnapCameraRotateTo45 quantizes the camera heading instead of easing to it, so the arithmetic is
	 the whole feature: the heading is always the nearest eighth, and a rotate key moves it exactly one
	 eighth from wherever it stands.  Both signs matter - the old copies of this rounded the wrong way
	 below zero, which put a heading of a few degrees left of north a whole eighth further left. */
TEST(camera_heading_snaps_to_the_nearest_eighth_on_both_sides_of_zero)
{
	const Real step = PI / 4.0f;

	CHECK_NEAR( View_snapAngleToEighth( 0.0f ), 0.0f, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( step ), step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -step ), -step, 0.0001f );

	// just off an eighth, either way, stays on it
	CHECK_NEAR( View_snapAngleToEighth( 0.1f ), 0.0f, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -0.1f ), 0.0f, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( step + 0.1f ), step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -step - 0.1f ), -step, 0.0001f );

	// past the halfway point it belongs to the next one, on both sides
	CHECK_NEAR( View_snapAngleToEighth( step * 0.6f ), step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -step * 0.6f ), -step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( step * 0.4f ), 0.0f, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -step * 0.4f ), 0.0f, 0.0001f );

	CHECK_NEAR( View_snapAngleToEighth( 3.0f * step - 0.05f ), 3.0f * step, 0.0001f );
	CHECK_NEAR( View_snapAngleToEighth( -3.0f * step + 0.05f ), -3.0f * step, 0.0001f );
}

/** One press of a rotate key is one eighth, from whatever the heading happens to be - including a
	 heading a script left off the grid, which is snapped first so the key never lands between two
	 eighths. */
TEST(a_rotate_key_press_moves_the_camera_exactly_one_eighth)
{
	const Real step = PI / 4.0f;

	CHECK_NEAR( View_stepAngleByEighths( 0.0f, 1 ), step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( 0.0f, -1 ), -step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( step, 1 ), 2.0f * step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( -step, -1 ), -2.0f * step, 0.0001f );

	// eight presses come back to where they started
	Real a = 0.0f;
	for( Int i = 0; i < 8; ++i )
		a = View_stepAngleByEighths( a, 1 );
	CHECK_NEAR( a, 2.0f * PI, 0.0001f );

	// off the grid: snapped first, then stepped, so the result is still an eighth
	CHECK_NEAR( View_stepAngleByEighths( 0.1f, 1 ), step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( -0.1f, -1 ), -step, 0.0001f );
	CHECK_NEAR( View_stepAngleByEighths( 0.0f, 0 ), 0.0f, 0.0001f );
}

/** Placing a structure buys a plan, not a building.  The object does go down at once - it is paid
	 for, it holds the ground, and it can be clicked and cancelled - but nothing is built until a
	 builder walks over to it, so it is drawn as the same translucent silhouette that was following
	 the cursor a moment earlier.  A plan on the map must never read as a building already standing
	 there. */
TEST(a_structure_waiting_for_its_builder_is_drawn_as_a_silhouette)
{
	CHECK( Object_isAwaitingBuilder( TRUE, 0.0f ) == TRUE );

	CHECK_NEAR( Drawable_effectiveOpacity( 1.0f, 1.0f, TRUE ), PLACEMENT_SILHOUETTE_OPACITY, 0.0001f );

	// what followed the cursor and what landed on the map are drawn at the same opacity
	CHECK_NEAR( Drawable_effectiveOpacity( PLACEMENT_SILHOUETTE_OPACITY, 1.0f, FALSE ),
							Drawable_effectiveOpacity( 1.0f, 1.0f, TRUE ), 0.0001f );

	// and it is see-through, which is the whole reason the renderer puts it in the translucent pass
	CHECK( Drawable_effectiveOpacity( 1.0f, 1.0f, TRUE ) != 1.0f );
}

/** A plan is not a target.  It clears no shroud and is drawn only for the player who placed it, so
	 an enemy has no way of seeing it - and a unit that stopped and shot at one told that enemy
	 exactly where a base was going up.  WeaponSet::getAbleToAttackSpecificObject rejects a victim
	 this says nothing is standing on. */
TEST(a_structure_waiting_for_its_builder_cannot_be_shot_at)
{
	CHECK( Object_isAttackableStructure( TRUE, 0.0f ) == FALSE );

	// the builder arrives and it is a building like any other
	CHECK( Object_isAttackableStructure( TRUE, 0.1f ) == TRUE );
	CHECK( Object_isAttackableStructure( TRUE, 99.9f ) == TRUE );

	// and everything that is not a structure going up is a target: a finished building carries
	// CONSTRUCTION_COMPLETE, a unit is simply not under construction
	CHECK( Object_isAttackableStructure( FALSE, CONSTRUCTION_COMPLETE ) == TRUE );
	CHECK( Object_isAttackableStructure( FALSE, 0.0f ) == TRUE );
}

/** A plan nobody can see or shoot must not hold up the end of the match.  A player left with only a
	 plan in the fog is beaten; the first percent of work makes it a building that counts. */
TEST(a_structure_waiting_for_its_builder_does_not_keep_its_player_alive)
{
	CHECK( Object_keepsOwnerAlive( TRUE, 0.0f ) == FALSE );
	CHECK( Object_keepsOwnerAlive( TRUE, 0.1f ) == TRUE );
	CHECK( Object_keepsOwnerAlive( FALSE, CONSTRUCTION_COMPLETE ) == TRUE );
	CHECK( Object_keepsOwnerAlive( FALSE, 0.0f ) == TRUE );
}

/** The first percent of work is what ends the plan, so the moment the builder arrives and starts
	 the structure turns solid and stays solid for the rest of its life.  A finished building carries
	 CONSTRUCTION_COMPLETE (-1) and must not be mistaken for one sitting at zero. */
TEST(a_structure_the_builder_has_reached_is_drawn_solid)
{
	CHECK( Object_isAwaitingBuilder( TRUE, 0.1f ) == FALSE );
	CHECK( Object_isAwaitingBuilder( TRUE, 99.9f ) == FALSE );
	CHECK( Object_isAwaitingBuilder( FALSE, 0.0f ) == FALSE );
	CHECK( Object_isAwaitingBuilder( FALSE, CONSTRUCTION_COMPLETE ) == FALSE );

	CHECK_NEAR( Drawable_effectiveOpacity( 1.0f, 1.0f, FALSE ), 1.0f, 0.0001f );
}

/** A plan is not a wall.  Its footprint stays out of the pathfind map until the builder puts the
	 first percent in, so units walk over a site that is only planned - a structure queued behind a
	 busy builder used to stand there as a solid obstacle for as long as it waited.  Selling runs the
	 percent back down through the same call and must not re-lay a footprint that is already down. */
TEST(a_planned_structure_becomes_an_obstacle_when_its_builder_arrives)
{
	// the builder arrives: this is the one transition that lays the footprint
	CHECK( Object_constructionFootprintGoesDown( TRUE, 0.0f, 0.1f ) == TRUE );

	// still waiting - construct() sets zero on a plan that is already at zero
	CHECK( Object_constructionFootprintGoesDown( TRUE, 0.0f, 0.0f ) == FALSE );

	// already building: the footprint went down on the first percent, not on every one after it
	CHECK( Object_constructionFootprintGoesDown( TRUE, 50.0f, 51.0f ) == FALSE );

	// selling a finished building winds it down from 100 to 0 - it is in the map and stays there
	CHECK( Object_constructionFootprintGoesDown( FALSE, 100.0f, 99.9f ) == FALSE );
	CHECK( Object_constructionFootprintGoesDown( FALSE, 0.1f, 0.0f ) == FALSE );

	// completion clears the status first and only then sets CONSTRUCTION_COMPLETE (-1)
	CHECK( Object_constructionFootprintGoesDown( FALSE, 99.9f, CONSTRUCTION_COMPLETE ) == FALSE );
}

/** The silhouette scales whatever opacity the drawable already asked for instead of replacing it,
	 so a stealthed or half-faded drawable is not dragged back up to 45% by being a plan, and one
	 that has been faded all the way out stays out. */
TEST(the_placement_silhouette_scales_the_opacity_it_is_given)
{
	CHECK_NEAR( Drawable_effectiveOpacity( 0.5f, 1.0f, TRUE ), 0.5f * PLACEMENT_SILHOUETTE_OPACITY, 0.0001f );
	CHECK_NEAR( Drawable_effectiveOpacity( 1.0f, 0.5f, TRUE ), 0.5f * PLACEMENT_SILHOUETTE_OPACITY, 0.0001f );
	CHECK_NEAR( Drawable_effectiveOpacity( 0.0f, 1.0f, TRUE ), 0.0f, 0.0001f );
}

/** A bridge has no object to hang a bar on: GenericBridge is an empty draw module with one immortal
	 hit point, and the span is drawn by the terrain, so the bar floated in mid air over the water.
	 Landmark bridges and bridge towers are the same case. */
TEST(a_bridge_draws_no_health_bar)
{
	CHECK( Drawable_structureShowsHealthBar( TRUE, TRUE, FALSE, FALSE ) == FALSE );
	CHECK( Drawable_structureShowsHealthBar( TRUE, FALSE, FALSE, FALSE ) == FALSE );

	// even a bridge somebody could walk into stays bare - it is still art the terrain draws
	CHECK( Drawable_structureShowsHealthBar( TRUE, TRUE, TRUE, TRUE ) == FALSE );
}

/** The map's civilian masonry is nobody's - the houses, and the concrete apron each one stands on,
	 which is a 2000 hit point object in its own right - so a city map no longer comes up wearing a
	 bar over every building and a second one at its feet.  A building troops can be put in keeps
	 its bar, and so does one that can be taken: both are decisions its health answers. */
TEST(only_a_garrisonable_or_capturable_civilian_building_wears_a_health_bar)
{
	CHECK( Drawable_structureShowsHealthBar( FALSE, TRUE, FALSE, FALSE ) == FALSE );
	CHECK( Drawable_structureShowsHealthBar( FALSE, TRUE, TRUE, FALSE ) == TRUE );

	// a hospital or an artillery platform: walk in or level it, and the bar is what decides
	CHECK( Drawable_structureShowsHealthBar( FALSE, TRUE, FALSE, TRUE ) == TRUE );
}

/** Anything a player owns keeps its bar - a barracks is neither garrisonable nor capturable, and
	 both it and a captured tech building are things the fight turns on. */
TEST(an_owned_structure_always_wears_a_health_bar)
{
	CHECK( Drawable_structureShowsHealthBar( FALSE, FALSE, FALSE, FALSE ) == TRUE );
	CHECK( Drawable_structureShowsHealthBar( FALSE, FALSE, TRUE, FALSE ) == TRUE );
}

/** Being carried is not a malfunction, so a unit inside a transport keeps its owner's colour.
	 Anything else that disables it turns the bar blue - and the pair together used to fail: the old
	 test asked `isDisabled() && !isDisabledByType(DISABLED_HELD)`, so a held unit that was then EMP'd
	 answered no and looked perfectly healthy inside the transport it could no longer leave. */
TEST(a_held_and_disabled_object_still_shows_a_blue_health_bar)
{
	CHECK( Drawable_disabledShowsBlueHealthBar( DISABLEDMASK_NONE ) == FALSE );
	CHECK( Drawable_disabledShowsBlueHealthBar( MAKE_DISABLED_MASK( DISABLED_HELD ) ) == FALSE );

	CHECK( Drawable_disabledShowsBlueHealthBar( MAKE_DISABLED_MASK( DISABLED_EMP ) ) == TRUE );
	CHECK( Drawable_disabledShowsBlueHealthBar( MAKE_DISABLED_MASK( DISABLED_HACKED ) ) == TRUE );
	CHECK( Drawable_disabledShowsBlueHealthBar( MAKE_DISABLED_MASK( DISABLED_UNDERPOWERED ) ) == TRUE );

	// the case the old condition got wrong
	CHECK( Drawable_disabledShowsBlueHealthBar( MAKE_DISABLED_MASK2( DISABLED_HELD, DISABLED_EMP ) ) == TRUE );
	CHECK( Drawable_disabledShowsBlueHealthBar( MAKE_DISABLED_MASK2( DISABLED_HELD, DISABLED_SUBDUED ) ) == TRUE );
}

/** Always and never are the two ends and answer without looking at the object at all. */
TEST(health_bar_always_and_never_ignore_everything_else)
{
	for( Int selected = 0; selected <= 1; ++selected )
		for( Int moused = 0; moused <= 1; ++moused )
			for( Int damaged = 0; damaged <= 1; ++damaged )
			{
				CHECK( Drawable_healthBarModeShows( HEALTH_BAR_ALWAYS, selected, moused, damaged ) == TRUE );
				CHECK( Drawable_healthBarModeShows( HEALTH_BAR_NEVER, selected, moused, damaged ) == FALSE );
			}
}

/** Retail's rule: the bar answers a question you asked about one unit, so it appears when you
	 select it or point at it and at no other time. */
TEST(health_bar_selection_mode_is_what_retail_did)
{
	CHECK( Drawable_healthBarModeShows( HEALTH_BAR_SELECTION, FALSE, FALSE, FALSE ) == FALSE );
	CHECK( Drawable_healthBarModeShows( HEALTH_BAR_SELECTION, TRUE, FALSE, FALSE ) == TRUE );
	CHECK( Drawable_healthBarModeShows( HEALTH_BAR_SELECTION, FALSE, TRUE, FALSE ) == TRUE );

	// damage is not what selection mode is asking about - a hurt tank nobody is pointing at stays bare
	CHECK( Drawable_healthBarModeShows( HEALTH_BAR_SELECTION, FALSE, FALSE, TRUE ) == FALSE );
}

/** Smart is selection plus every damaged thing.  A bar over a unit at full health says only what
	 the absence of a bar would have said, and a base full of those is what makes always-on tiring;
	 a bar over something that has been hit is the reason to have them at all. */
TEST(health_bar_smart_mode_marks_the_hurt)
{
	CHECK( Drawable_healthBarModeShows( HEALTH_BAR_SMART, FALSE, FALSE, TRUE ) == TRUE );
	CHECK( Drawable_healthBarModeShows( HEALTH_BAR_SMART, FALSE, FALSE, FALSE ) == FALSE );

	// and the selection stays readable whether or not it has been shot at
	CHECK( Drawable_healthBarModeShows( HEALTH_BAR_SMART, TRUE, FALSE, FALSE ) == TRUE );
	CHECK( Drawable_healthBarModeShows( HEALTH_BAR_SMART, FALSE, TRUE, FALSE ) == TRUE );
}

/** The setting is stored, clamped and read back like every other catalog row, and its default is
	 what this fork has always done - nobody's game changes until they change it. */
TEST(health_bar_mode_round_trips_through_options_ini)
{
	const OptionDef *def = findOptionDef( "HealthBars" );
	CHECK( def != NULL );
	CHECK_EQ( (Int)def->kind, (Int)OPTION_ENUM );
	CHECK_EQ( (Int)def->apply, (Int)APPLY_LIVE );
	CHECK_EQ( def->lo, 0 );
	CHECK_EQ( def->hi, HEALTH_BAR_MODE_COUNT - 1 );

	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;

	CHECK_EQ( TheGlobalData->m_healthBarMode, (Int)HEALTH_BAR_ALWAYS );

	TheWritableGlobalData->m_healthBarMode = HEALTH_BAR_SMART;

	UserPreferences pref;
	saveOptionsToPreferences( pref );
	CHECK_STR( pref[ AsciiString( "HealthBars" ) ].str(), "1" );

	TheWritableGlobalData->m_healthBarMode = HEALTH_BAR_NEVER;
	loadOptionsFromPreferences( pref );
	CHECK_EQ( TheGlobalData->m_healthBarMode, (Int)HEALTH_BAR_SMART );

	// a hand-edited file cannot ask for a mode that does not exist
	pref[ AsciiString( "HealthBars" ) ] = AsciiString( "99" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( TheGlobalData->m_healthBarMode, (Int)HEALTH_BAR_MODE_COUNT - 1 );

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

TEST(player_color_scheme_round_trips_through_options_ini)
{
	const OptionDef *def = findOptionDef( "PlayerColors" );
	CHECK( def != NULL );
	CHECK_EQ( (Int)def->kind, (Int)OPTION_ENUM );
	CHECK_EQ( (Int)def->apply, (Int)APPLY_LIVE );
	CHECK_EQ( def->lo, 0 );
	CHECK_EQ( def->hi, PLAYER_COLOR_SCHEME_COUNT - 1 );

	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;

	// nobody's colours change until they ask
	CHECK_EQ( TheGlobalData->m_playerColorScheme, (Int)PLAYER_COLORS_ORIGINAL );

	TheWritableGlobalData->m_playerColorScheme = PLAYER_COLORS_RELATION;

	UserPreferences pref;
	saveOptionsToPreferences( pref );
	CHECK_STR( pref[ AsciiString( "PlayerColors" ) ].str(), "1" );

	TheWritableGlobalData->m_playerColorScheme = PLAYER_COLORS_ORIGINAL;
	loadOptionsFromPreferences( pref );
	CHECK_EQ( TheGlobalData->m_playerColorScheme, (Int)PLAYER_COLORS_RELATION );

	pref[ AsciiString( "PlayerColors" ) ] = AsciiString( "99" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( TheGlobalData->m_playerColorScheme, (Int)PLAYER_COLOR_SCHEME_COUNT - 1 );

	TheWritableGlobalData->m_playerColorScheme = PLAYER_COLORS_ORIGINAL;
	invalidatePlayerColorScheme();

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

/** Every colour the scheme can hand out has to be opaque and has to be visible: an alpha of zero
	 draws nothing at all, and a member of an alliance past the fourth is a darkened repeat that must
	 not reach black. */
TEST(scheme_colors_are_opaque_and_never_black)
{
	for( Int family = 0; family < PLAYER_COLOR_FAMILY_COUNT; ++family )
	{
		for( Int shade = 0; shade < PLAYER_COLOR_SHADE_COUNT * 3; ++shade )
		{
			const Color c = playerSchemeColor( family, shade );

			CHECK_EQ( (Int)((c >> 24) & 0xFF), 255 );
			CHECK( (c & 0x00FFFFFF) != 0 );

			const Int red   = (c >> 16) & 0xFF;
			const Int green = (c >>  8) & 0xFF;
			const Int blue  =  c        & 0xFF;
			CHECK( red + green + blue >= 96 );
		}
	}

	// four members of one alliance are four different colours, which is the whole point of shades
	for( Int a = 0; a < PLAYER_COLOR_SHADE_COUNT; ++a )
		for( Int b = a + 1; b < PLAYER_COLOR_SHADE_COUNT; ++b )
			CHECK( playerSchemeColor( PLAYER_COLOR_FAMILY_RED, a ) != playerSchemeColor( PLAYER_COLOR_FAMILY_RED, b ) );

	// and the first shade of two families is never the same colour
	for( Int f1 = 0; f1 < PLAYER_COLOR_FAMILY_COUNT; ++f1 )
		for( Int f2 = f1 + 1; f2 < PLAYER_COLOR_FAMILY_COUNT; ++f2 )
			CHECK( playerSchemeColor( f1, 0 ) != playerSchemeColor( f2, 0 ) );
}

/** Night is the day colour a quarter of the way to white, and never darker: the reason the shipped
	 palette carries a separate night colour at all is that a dark unit on a night map is a hole. */
TEST(scheme_night_colors_are_lighter_than_their_day)
{
	for( Int family = 0; family < PLAYER_COLOR_FAMILY_COUNT; ++family )
	{
		const Color day = playerSchemeColor( family, 0 );
		const Color night = playerSchemeNightColor( day );

		CHECK_EQ( (Int)((night >> 24) & 0xFF), 255 );
		CHECK( (Int)((night >> 16) & 0xFF) >= (Int)((day >> 16) & 0xFF) );
		CHECK( (Int)((night >>  8) & 0xFF) >= (Int)((day >>  8) & 0xFF) );
		CHECK( (Int)( night        & 0xFF) >= (Int)( day        & 0xFF) );
	}

	// white has nowhere lighter to go
	CHECK_EQ( (Int)(playerSchemeNightColor( 0xFFFFFFFF ) & 0x00FFFFFF), 0x00FFFFFF );
}

/** The scheme is a translation and nothing more, so the setting nobody changed has to leave every
	 colour exactly as it arrived, and a colour that belongs to no player has to pass through under
	 any setting - a crate's floating text and a script's own colour both come through here. */
TEST(original_scheme_and_unknown_colors_pass_through_untouched)
{
	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;
	invalidatePlayerColorScheme();

	const Color sample[] = { 0xFF102030, 0xE6FF0000, 0x00000000, 0xFFFFFFFF };

	for( Int i = 0; i < 4; ++i )
		CHECK_EQ( (Int)clientColor( sample[ i ] ), (Int)sample[ i ] );

	TheWritableGlobalData->m_playerColorScheme = PLAYER_COLORS_TEAM;
	invalidatePlayerColorScheme();

	// there is no player list in a test, so no colour is anybody's and all four still come back
	for( Int i = 0; i < 4; ++i )
		CHECK_EQ( (Int)clientColor( sample[ i ] ), (Int)sample[ i ] );

	// nothing to draw for nobody
	CHECK_EQ( (Int)clientPlayerColor( NULL ), 0 );
	CHECK_EQ( (Int)clientPlayerNightColor( NULL ), 0 );

	TheWritableGlobalData->m_playerColorScheme = PLAYER_COLORS_ORIGINAL;
	invalidatePlayerColorScheme();

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

/** The bar is a 2D overlay drawn after the scene, so the pick ray knows nothing about it and a
	 click that lands on one used to hit bare ground.  Every bar is on in this fork, which makes it
	 the one part of a unit that is never behind a building - and at full zoom out a rifleman is a
	 few pixels of model under a bar that is easier to hit than the man.  The bar is three pixels
	 tall, so the click target is the drawn rectangle plus slack on every side. */
TEST(a_click_lands_on_a_health_bar_plus_its_slack)
{
	IRegion2D bar;
	bar.lo.x = 100;
	bar.lo.y = 50;
	bar.hi.x = 140;
	bar.hi.y = 53;

	ICoord2D onTheBar = { 120, 51 };
	CHECK( GameClient_healthBarPickHit( bar, onTheBar, 0 ) == TRUE );

	// the drawn edges are in
	ICoord2D corner = { 100, 50 };
	CHECK( GameClient_healthBarPickHit( bar, corner, 0 ) == TRUE );

	// four pixels above the bar is a miss on the art and a hit on the target you can actually click
	ICoord2D justAbove = { 120, 46 };
	CHECK( GameClient_healthBarPickHit( bar, justAbove, 0 ) == FALSE );
	CHECK( GameClient_healthBarPickHit( bar, justAbove, 4 ) == TRUE );

	// and the slack is slack, not a free pass: the rest of the screen is on no bar at all
	ICoord2D elsewhere = { 400, 300 };
	CHECK( GameClient_healthBarPickHit( bar, elsewhere, 4 ) == FALSE );
}

/** A column of tanks seen side on stacks its bars on top of each other, so one pixel can be on two
	 of them.  Whichever bar the cursor is nearest the middle of wins - the alternative is list
	 order, which is drawing order, which is nothing the player can see. */
TEST(overlapping_health_bars_go_to_the_one_the_cursor_is_nearest)
{
	IRegion2D nearBar;
	nearBar.lo.x = 100;
	nearBar.lo.y = 52;
	nearBar.hi.x = 140;
	nearBar.hi.y = 55;

	IRegion2D farBar;
	farBar.lo.x = 110;
	farBar.lo.y = 50;
	farBar.hi.x = 150;
	farBar.hi.y = 53;

	// dead centre of the near bar, and inside the far one too
	ICoord2D pixel = { 120, 53 };
	CHECK( GameClient_healthBarPickHit( nearBar, pixel, 0 ) == TRUE );
	CHECK( GameClient_healthBarPickHit( farBar, pixel, 0 ) == TRUE );

	CHECK( GameClient_healthBarPickScore( nearBar, pixel ) < GameClient_healthBarPickScore( farBar, pixel ) );

	// the score is the squared pixel distance to the centre, so it is zero on the middle of a bar
	CHECK_EQ( GameClient_healthBarPickScore( nearBar, pixel ), 0 );

	// three pixels to the right of centre is nine, not three - ordering only, never a distance
	ICoord2D offCentre = { 123, 53 };
	CHECK_EQ( GameClient_healthBarPickScore( nearBar, offCentre ), 9 );
}

/** Ground you have already scouted stays buildable after your units leave it, so a base can be
	 planned out into the fog and the builder sent to walk there.  Shroud - terrain nobody of yours
	 has ever laid eyes on - is still off limits, which is what stops the map being read through a
	 placement cursor. */
TEST(a_base_can_be_planned_into_fog_but_not_into_shroud)
{
	CHECK( BuildAssistant_shroudBlocksBuilding( CELLSHROUD_CLEAR ) == FALSE );
	CHECK( BuildAssistant_shroudBlocksBuilding( CELLSHROUD_FOGGED ) == FALSE );
	CHECK( BuildAssistant_shroudBlocksBuilding( CELLSHROUD_SHROUDED ) == TRUE );
}

/** A plan is not a scout.  A structure that has been placed but not started opens no shroud at all,
	 so drawing a base out into the fog cannot be used to see what is standing there.  Once the
	 builder arrives EA's own rule takes over - the structure sees itself and no further - and a
	 finished building goes back to the sight its template gives it. */
TEST(a_planned_structure_opens_no_shroud_until_the_work_starts)
{
	const Real templateRange = 300.0f;
	const Real boundingRadius = 40.0f;

	CHECK_NEAR( Object_shroudClearingRange( templateRange, TRUE, 0.0f, boundingRadius ), 0.0f, 0.0001f );
	CHECK_NEAR( Object_shroudClearingRange( templateRange, TRUE, 0.1f, boundingRadius ), boundingRadius, 0.0001f );
	CHECK_NEAR( Object_shroudClearingRange( templateRange, TRUE, 99.9f, boundingRadius ), boundingRadius, 0.0001f );
	CHECK_NEAR( Object_shroudClearingRange( templateRange, FALSE, CONSTRUCTION_COMPLETE, boundingRadius ),
							templateRange, 0.0001f );

	// a structure that clears no shroud at all is not the same as one with no vision by template
	CHECK_NEAR( Object_shroudClearingRange( 0.0f, FALSE, CONSTRUCTION_COMPLETE, boundingRadius ), 0.0f, 0.0001f );
}

/** A unit sees half as far again as its longest weapon reaches. Artillery that outranges its own sight
	 keeps the sight, and a unit with no weapon keeps what its template gives it. */
TEST(an_armed_unit_sees_little_further_than_it_can_shoot)
{
	const Real noWeapon = -1.0f;

	CHECK_NEAR( Object_armedShroudClearingRange( 200.0f, 100.0f ), 150.0f, 0.0001f );
	CHECK_NEAR( Object_armedShroudClearingRange( 200.0f, 350.0f ), 200.0f, 0.0001f );
	CHECK_NEAR( Object_armedShroudClearingRange( 200.0f, 150.0f ), 200.0f, 0.0001f );
	CHECK_NEAR( Object_armedShroudClearingRange( 200.0f, noWeapon ), 200.0f, 0.0001f );
	CHECK_NEAR( Object_armedShroudClearingRange( 0.0f, 100.0f ), 0.0f, 0.0001f );
}

/** Three units of range for every unit of height over the target, never more than doubling the
	 range, and nothing taken away for standing lower. */
TEST(high_ground_reaches_further_and_low_ground_no_shorter)
{
	CHECK_NEAR( Weapon_elevationRangeBonus( 200.0f, 20.0f ), 60.0f, 0.0001f );
	CHECK_NEAR( Weapon_elevationRangeBonus( 200.0f, 400.0f ), 200.0f, 0.0001f );
	CHECK_NEAR( Weapon_elevationRangeBonus( 200.0f, FLT_MAX ), 200.0f, 0.0001f );
	CHECK_NEAR( Weapon_elevationRangeBonus( 200.0f, 0.0f ), 0.0f, 0.0001f );
	CHECK_NEAR( Weapon_elevationRangeBonus( 200.0f, -40.0f ), 0.0f, 0.0001f );
}

/** A hill hides the ground behind it. Standing in the middle of flat ground with a wall of high cells
	 two to the east, the unit sees the wall and nothing past it, and sees everything to the west. Put
	 the eye above the wall, the way an aircraft flies, and the far side comes back. */
TEST(a_unit_does_not_see_over_a_hill)
{
	const Int cellRadius = 4;
	const Int side = 2 * cellRadius + 1;
	const Real cellSize = 40.0f;
	const Real groundZ = 10.0f;
	const Real hillZ = 60.0f;
	const Real tankEyeZ = groundZ + 15.0f;
	const Real aircraftEyeZ = groundZ + 200.0f;
	const Int wallOffsetX = 2;

	Real heights[ side * side ];
	Real sightMargin[ side * side ];
	for( Int cell = 0; cell < side * side; ++cell )
		heights[ cell ] = groundZ;
	for( Int row = 0; row < side; ++row )
		heights[ row * side + cellRadius + wallOffsetX ] = hillZ;

	const Int middleRow = cellRadius * side;
	PartitionManager_findSightMargins( heights, cellRadius, cellSize, tankEyeZ, sightMargin );

	CHECK( sightMargin[ middleRow + cellRadius + 1 ] >= 0.0f );
	CHECK( sightMargin[ middleRow + cellRadius + wallOffsetX ] >= 0.0f );
	CHECK( sightMargin[ middleRow + cellRadius + wallOffsetX + 1 ] < 0.0f );
	CHECK( sightMargin[ middleRow + side - 1 ] < 0.0f );
	CHECK( sightMargin[ middleRow + 0 ] >= 0.0f );
	CHECK( sightMargin[ 0 ] >= 0.0f );

	PartitionManager_findSightMargins( heights, cellRadius, cellSize, aircraftEyeZ, sightMargin );
	CHECK( sightMargin[ middleRow + side - 1 ] >= 0.0f );
}

/** Since the plan reveals nothing, the fog it was placed in would swallow it - and there would be
	 nothing left on screen to click on and cancel.  Your own plan is drawn through the fog; anything
	 else fogged, including an enemy's, is still hidden. */
TEST(your_own_plan_is_drawn_through_the_fog_that_hides_everything_else)
{
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_FOGGED, TRUE ) == FALSE );
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_SHROUDED, TRUE ) == FALSE );

	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_FOGGED, FALSE ) == TRUE );
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_SHROUDED, FALSE ) == TRUE );

	// nothing changes for what the player can see anyway
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_CLEAR, FALSE ) == FALSE );
	CHECK( GameClient_hiddenByShroud( OBJECTSHROUD_PARTIAL_CLEAR, FALSE ) == FALSE );
}

/** Fog was the only thing hiding an enemy's plan, so a unit already looking at the site watched it go
	 down and the fog remembered it afterwards. An enemy is shown nothing until the first percent of
	 work; allies and observers, who are never enemies, see the plan as before. */
TEST(an_enemy_plan_is_hidden_even_where_the_viewer_has_sight)
{
	CHECK( Object_isPlanHiddenFrom( TRUE, 0.0f, ENEMIES ) == TRUE );

	CHECK( Object_isPlanHiddenFrom( TRUE, 0.0f, ALLIES ) == FALSE );
	CHECK( Object_isPlanHiddenFrom( TRUE, 0.0f, NEUTRAL ) == FALSE );

	// started or finished, it is a building the enemy is entitled to see
	CHECK( Object_isPlanHiddenFrom( TRUE, 0.1f, ENEMIES ) == FALSE );
	CHECK( Object_isPlanHiddenFrom( FALSE, CONSTRUCTION_COMPLETE, ENEMIES ) == FALSE );
}

extern Coord3D Locomotor_interceptOffset( const Coord3D& toVictim, const Coord3D& victimVelocity, Real speed );

/** A locked missile curves towards where it will meet a moving victim. Curving towards where the
	 victim is now left a rocket barely faster than a helicopter trailing it with its nose 40 degrees
	 off, never closing. */
TEST(a_locked_missile_aims_where_it_will_meet_a_moving_victim)
{
	Coord3D toVictim;
	toVictim.set( 100.0f, 0.0f, 0.0f );
	Coord3D offset;

	Coord3D standing;
	standing.set( 0.0f, 0.0f, 0.0f );
	offset = Locomotor_interceptOffset( toVictim, standing, 10.0f );
	CHECK_NEAR( offset.x, 100.0f, 0.001f );
	CHECK_NEAR( offset.y, 0.0f, 0.001f );

	// crossing at 6 a frame against 10: |(100, 6t)| = 10t meets at t = 12.5, 75 to the side
	Coord3D crossing;
	crossing.set( 0.0f, 6.0f, 0.0f );
	offset = Locomotor_interceptOffset( toVictim, crossing, 10.0f );
	CHECK_NEAR( offset.x, 100.0f, 0.001f );
	CHECK_NEAR( offset.y, 75.0f, 0.01f );

	// head on at the missile's own speed the two meet halfway
	Coord3D closing;
	closing.set( -10.0f, 0.0f, 0.0f );
	offset = Locomotor_interceptOffset( toVictim, closing, 10.0f );
	CHECK_NEAR( offset.x, 50.0f, 0.001f );

	// running away faster than the missile flies there is no meeting point to lead, so aim at it
	Coord3D fleeing;
	fleeing.set( 12.0f, 0.0f, 0.0f );
	offset = Locomotor_interceptOffset( toVictim, fleeing, 10.0f );
	CHECK_NEAR( offset.x, 100.0f, 0.001f );
}

/** There is nothing to stop about a building that is still going up, so the stop key calls it off
	 instead - the same cancel, refund and all, that the command bar button on that structure does.
	 One structure of your own only: a mixed selection or anything already finished still means
	 stop. */
TEST(the_stop_key_cancels_a_building_that_is_still_going_up)
{
	CHECK( Command_stopMeansCancelConstruction( 1, TRUE, TRUE ) == TRUE );

	CHECK( Command_stopMeansCancelConstruction( 1, TRUE, FALSE ) == FALSE );		// finished building
	CHECK( Command_stopMeansCancelConstruction( 1, FALSE, TRUE ) == FALSE );	// not yours
	CHECK( Command_stopMeansCancelConstruction( 2, TRUE, TRUE ) == FALSE );		// more than one thing
	CHECK( Command_stopMeansCancelConstruction( 0, FALSE, FALSE ) == FALSE );	// nothing selected
}

/** The plan sits in fog on its owner's screen on purpose, and the fog gate on orders would then
	 refuse every click on it: no build cursor, no resume, nothing but selection.  A player's own
	 plan is never hidden from that player's own builders.  Everything else the gate does is
	 untouched - an enemy in fog is still out of reach, the AI and scripts still ignore the gate
	 entirely. */
TEST(the_fog_never_hides_your_own_plan_from_your_own_builder)
{
	CHECK( ActionManager_shroudHidesTarget( TRUE, FALSE, TRUE, TRUE ) == FALSE );		// your own plan
	CHECK( ActionManager_shroudHidesTarget( TRUE, FALSE, TRUE, FALSE ) == TRUE );		// anything else fogged

	CHECK( ActionManager_shroudHidesTarget( TRUE, TRUE, TRUE, FALSE ) == FALSE );		// from a script
	CHECK( ActionManager_shroudHidesTarget( FALSE, FALSE, TRUE, FALSE ) == FALSE );	// asked by the AI
	CHECK( ActionManager_shroudHidesTarget( TRUE, FALSE, FALSE, FALSE ) == FALSE );	// in plain sight
}

//-------------------------------------------------------------------------------------------------
/** A plan is a silhouette, not a building: cancelling it - or an enemy shooting it - must not set
	off the explosion, collapse and rubble of a structure that never stood.  Object::onDie skips
	its die modules and destroys it outright when this says so. */
//-------------------------------------------------------------------------------------------------
TEST(a_plan_dies_without_an_explosion_but_a_building_does_not)
{
	CHECK( Object_deathIsSilent( TRUE, 0.0f ) == TRUE );			// still waiting for its builder
	CHECK( Object_deathIsSilent( TRUE, 0.5f ) == FALSE );			// half up, it blows up like a building
	CHECK( Object_deathIsSilent( TRUE, 100.0f ) == FALSE );		// the last frame of construction
	CHECK( Object_deathIsSilent( FALSE, 0.0f ) == FALSE );		// finished: percent means nothing here
}

//////////////////////////////////////////////////////////////////////////////
// Random map generator
//////////////////////////////////////////////////////////////////////////////

/* One row of the generated map's blend table, in the file's own order. */
struct RMGBlendEntry
{
	Int m_blendTileIndex;
	UnsignedByte m_horizontal, m_vertical, m_rightDiagonal, m_leftDiagonal;
	UnsignedByte m_inverted, m_longDiagonal;
	Int m_customBlendEdgeClass, m_flag;
};

/* What the engine's own chunk reader made of a generated map. */
struct RMGParse
{
	Int m_width, m_height, m_border, m_boundaryX, m_boundaryY, m_dataSize;
	Int m_blendDataSize, m_numBitmapTiles, m_numBlendedTiles, m_numCliffInfo;
	Int m_numTextureClasses, m_firstTile, m_numTiles, m_tileWidth;
	Int m_numSides, m_numTeams, m_numObjects, m_numWaterAreas;
	Int m_weather, m_compression, m_timeOfDay, m_lightingBytesLeftOver, m_blendBytesLeftOver;
	Real m_terrainAmbient[3];
	AsciiString m_textureName;
	std::vector<AsciiString> m_textureNames;
	std::vector<Int> m_firstTiles;
	std::vector<AsciiString> m_waypointNames;
	std::vector<Coord3D> m_waypointPositions;
	std::vector<AsciiString> m_objectNames;
	std::vector<UnsignedByte> m_heights;
	std::vector<Short> m_tiles;
	std::vector<Short> m_blendIndexes;			///< per cell, into m_blends; 0 is no blend
	std::vector<RMGBlendEntry> m_blends;		///< the table itself, entry 0 excepted
	std::vector<Coord3D> m_waterPoints;			///< first point of each water area
	std::vector< std::vector<Coord3D> > m_waterPolygons;	///< and every point of it
	std::vector<Coord3D> m_objectPositions;
	std::vector<Real> m_objectAngles;
	std::vector<Int> m_objectFlags;				///< the road flags, for the objects that carry them

	RMGParse() :
		m_width(0), m_height(0), m_border(0), m_boundaryX(0), m_boundaryY(0), m_dataSize(0),
		m_blendDataSize(0), m_numBitmapTiles(0), m_numBlendedTiles(0), m_numCliffInfo(0),
		m_numTextureClasses(0), m_firstTile(0), m_numTiles(0), m_tileWidth(0),
		m_numSides(0), m_numTeams(0), m_numObjects(0), m_numWaterAreas(0),
		m_weather(-1), m_compression(-1), m_timeOfDay(-1), m_lightingBytesLeftOver(-1),
		m_blendBytesLeftOver(-1)
	{
		m_terrainAmbient[0] = m_terrainAmbient[1] = m_terrainAmbient[2] = 0.0f;
	}
};
static RMGParse theRMGParse;

static Bool RMGParseHeightMap( DataChunkInput &file, DataChunkInfo *info, void * )
{
	theRMGParse.m_width = file.readInt();
	theRMGParse.m_height = file.readInt();
	theRMGParse.m_border = file.readInt();

	Int numBoundaries = file.readInt();
	for( Int i = 0; i < numBoundaries; i++ )
	{
		theRMGParse.m_boundaryX = file.readInt();
		theRMGParse.m_boundaryY = file.readInt();
	}

	theRMGParse.m_dataSize = file.readInt();
	theRMGParse.m_heights.resize( theRMGParse.m_dataSize );
	file.readArrayOfBytes( (char *)&theRMGParse.m_heights[0], theRMGParse.m_dataSize );
	return TRUE;
}

static Bool RMGParseBlendTile( DataChunkInput &file, DataChunkInfo *info, void * )
{
	Int len = file.readInt();
	theRMGParse.m_blendDataSize = len;

	theRMGParse.m_tiles.resize( len );
	file.readArrayOfBytes( (char *)&theRMGParse.m_tiles[0], len * sizeof(Short) );

	theRMGParse.m_blendIndexes.resize( len );
	file.readArrayOfBytes( (char *)&theRMGParse.m_blendIndexes[0], len * sizeof(Short) );

	std::vector<Short> scratch( len );
	file.readArrayOfBytes( (char *)&scratch[0], len * sizeof(Short) );	// extra blend tiles
	file.readArrayOfBytes( (char *)&scratch[0], len * sizeof(Short) );	// cliff info

	theRMGParse.m_numBitmapTiles = file.readInt();
	theRMGParse.m_numBlendedTiles = file.readInt();
	theRMGParse.m_numCliffInfo = file.readInt();

	theRMGParse.m_numTextureClasses = file.readInt();
	for( Int i = 0; i < theRMGParse.m_numTextureClasses; i++ )
	{
		Int firstTile = file.readInt();
		theRMGParse.m_numTiles = file.readInt();
		theRMGParse.m_tileWidth = file.readInt();
		file.readInt();									// legacy field

		AsciiString name = file.readAsciiString();
		theRMGParse.m_firstTiles.push_back( firstTile );
		theRMGParse.m_textureNames.push_back( name );

		if( i == 0 )
		{
			theRMGParse.m_firstTile = firstTile;
			theRMGParse.m_textureName = name;
		}
	}

	file.readInt();										// edge tiles
	file.readInt();										// edge texture classes

	/* The blend table, in ParseBlendTileData's read order.  Entry 0 is the "no blend" default and
		is not in the file, which is why the loop starts at one. */
	for( Int blend = 1; blend < theRMGParse.m_numBlendedTiles; blend++ )
	{
		RMGBlendEntry entry;
		entry.m_blendTileIndex = file.readInt();
		entry.m_horizontal = file.readByte();
		entry.m_vertical = file.readByte();
		entry.m_rightDiagonal = file.readByte();
		entry.m_leftDiagonal = file.readByte();
		entry.m_inverted = file.readByte();
		entry.m_longDiagonal = file.readByte();
		entry.m_customBlendEdgeClass = file.readInt();
		entry.m_flag = file.readInt();
		theRMGParse.m_blends.push_back( entry );
	}

	theRMGParse.m_blendBytesLeftOver = file.atEndOfChunk() ? 0 : 1;
	return TRUE;
}

/* Four times of day, and for each one a terrain light and an objects light with three lights'
	worth of colour and direction apiece, exactly as WHeightMapEdit writes them. */
static Bool RMGParseLighting( DataChunkInput &file, DataChunkInfo *info, void * )
{
	theRMGParse.m_timeOfDay = file.readInt();

	for( Int timeOfDay = 0; timeOfDay < 4; timeOfDay++ )
	{
		for( Int value = 0; value < 54; value++ )
		{
			Real read = file.readReal();
			if( timeOfDay == 0 && value < 3 )
				theRMGParse.m_terrainAmbient[value] = read;
		}
	}

	theRMGParse.m_lightingBytesLeftOver = file.atEndOfChunk() ? 0 : 1;
	return TRUE;
}

static Bool RMGParseWaterAreas( DataChunkInput &file, DataChunkInfo *info, void * )
{
	theRMGParse.m_numWaterAreas = file.readInt();

	for( Int area = 0; area < theRMGParse.m_numWaterAreas; area++ )
	{
		file.readAsciiString();							// trigger name
		file.readAsciiString();							// layer
		file.readInt();									// trigger id

		CHECK( file.readByte() == 1 );					// every one of ours is water
		file.readByte();								// not a river
		file.readInt();									// river start

		Int numPoints = file.readInt();
		CHECK( numPoints >= 3 );

		std::vector<Coord3D> polygon;
		for( Int point = 0; point < numPoints; point++ )
		{
			Coord3D loc;
			loc.x = (Real)file.readInt();
			loc.y = (Real)file.readInt();
			loc.z = (Real)file.readInt();
			polygon.push_back( loc );

			if( point == 0 )
				theRMGParse.m_waterPoints.push_back( loc );
		}

		theRMGParse.m_waterPolygons.push_back( polygon );
	}
	return TRUE;
}

static Bool RMGParseWorldInfo( DataChunkInput &file, DataChunkInfo *info, void * )
{
	Dict d = file.readDict();
	theRMGParse.m_weather = d.getInt( NAMEKEY( "weather" ) );
	theRMGParse.m_compression = d.getInt( NAMEKEY( "compression" ) );
	return TRUE;
}

static Bool RMGParseSides( DataChunkInput &file, DataChunkInfo *info, void * )
{
	theRMGParse.m_numSides = file.readInt();
	Int i;
	for( i = 0; i < theRMGParse.m_numSides; i++ )
	{
		file.readDict();
		Int buildListCount = file.readInt();
		CHECK_EQ( buildListCount, 0 );
	}

	theRMGParse.m_numTeams = file.readInt();
	for( i = 0; i < theRMGParse.m_numTeams; i++ )
		file.readDict();

	return TRUE;
}

static Bool RMGParseObject( DataChunkInput &file, DataChunkInfo *info, void * )
{
	Coord3D loc;
	loc.x = file.readReal();
	loc.y = file.readReal();
	loc.z = file.readReal();
	Real angle = file.readReal();
	Int flags = file.readInt();
	AsciiString name = file.readAsciiString();
	Dict d = file.readDict();

	theRMGParse.m_numObjects++;
	theRMGParse.m_objectNames.push_back( name );
	theRMGParse.m_objectPositions.push_back( loc );
	theRMGParse.m_objectAngles.push_back( angle );
	theRMGParse.m_objectFlags.push_back( flags );

	if( d.getType( NAMEKEY( "waypointID" ) ) == Dict::DICT_INT )
	{
		theRMGParse.m_waypointNames.push_back( d.getAsciiString( NAMEKEY( "waypointName" ) ) );
		theRMGParse.m_waypointPositions.push_back( loc );
	}
	return TRUE;
}

static Bool RMGParseObjects( DataChunkInput &file, DataChunkInfo *info, void *userData )
{
	file.registerParser( AsciiString( "Object" ), info->label, RMGParseObject );
	return file.parse( userData );
}

/* Runs the engine's own DataChunkInput over a generated map, straight out of
	memory - the same reader WorldHeightMap and MapUtil use on a real .map. */
static void parseGeneratedMap( std::vector<char>& bytes )
{
	theRMGParse = RMGParse();

	MemoryChunkInputStream stream( &bytes[0], bytes.size() );
	DataChunkInput input( &stream );

	input.registerParser( AsciiString( "HeightMapData" ), AsciiString::TheEmptyString, RMGParseHeightMap );
	input.registerParser( AsciiString( "BlendTileData" ), AsciiString::TheEmptyString, RMGParseBlendTile );
	input.registerParser( AsciiString( "WorldInfo" ), AsciiString::TheEmptyString, RMGParseWorldInfo );
	input.registerParser( AsciiString( "SidesList" ), AsciiString::TheEmptyString, RMGParseSides );
	input.registerParser( AsciiString( "ObjectsList" ), AsciiString::TheEmptyString, RMGParseObjects );
	input.registerParser( AsciiString( "PolygonTriggers" ), AsciiString::TheEmptyString, RMGParseWaterAreas );
	input.registerParser( AsciiString( "GlobalLighting" ), AsciiString::TheEmptyString, RMGParseLighting );

	CHECK( input.parse( NULL ) == TRUE );
}

//-------------------------------------------------------------------------------------------------
/** The whole point of generating a map from a seed instead of shipping a file: every machine in
	the game has to build identical bytes, or the map CRC that guards the lobby is worthless.
	Nothing but the settings may reach the generator - no clock, no rand(), no options file. */
//-------------------------------------------------------------------------------------------------
TEST(a_seed_generates_the_same_map_bytes_every_single_time)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_seed = 12345;
	settings.m_playableCells = 96;
	settings.m_numPlayers = 4;

	std::vector<char> first, second;
	RandomMapGenerator::generate( settings, first );
	RandomMapGenerator::generate( settings, second );

	CHECK( first.size() > 0 );
	CHECK_EQ( (Int)first.size(), (Int)second.size() );
	CHECK_MEM( &first[0], &second[0], (Int)first.size() );

	// ...and a different seed is a different map, or the seed is not doing anything.
	std::vector<char> other;
	settings.m_seed = 12346;
	RandomMapGenerator::generate( settings, other );
	CHECK( other.size() != first.size() || memcmp( &other[0], &first[0], first.size() ) != 0 );
}

//-------------------------------------------------------------------------------------------------
/** Generating bytes is easy; generating bytes the game will read is the job.  Run the engine's own
	chunk reader over the result and check the fields the terrain and map code go looking for. */
//-------------------------------------------------------------------------------------------------
TEST(a_generated_map_reads_back_through_the_engines_own_chunk_reader)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_seed = 777;
	settings.m_playableCells = 160;
	settings.m_numPlayers = 4;

	std::vector<char> bytes;
	RandomMapGenerator::generate( settings, bytes );
	parseGeneratedMap( bytes );

	// The playable area is what the boundary says; the rest is border the camera looks across.
	CHECK_EQ( theRMGParse.m_boundaryX, 160 );
	CHECK_EQ( theRMGParse.m_boundaryY, 160 );
	CHECK( theRMGParse.m_border > 0 );
	CHECK_EQ( theRMGParse.m_width, 160 + 2 * theRMGParse.m_border );
	CHECK_EQ( theRMGParse.m_height, theRMGParse.m_width );
	CHECK_EQ( theRMGParse.m_dataSize, theRMGParse.m_width * theRMGParse.m_height );
	CHECK_EQ( (Int)theRMGParse.m_heights.size(), theRMGParse.m_dataSize );

	// ParseBlendTileData throws ERROR_CORRUPT_FILE_FORMAT unless these two agree.
	CHECK_EQ( theRMGParse.m_blendDataSize, theRMGParse.m_dataSize );

	CHECK_EQ( theRMGParse.m_numTextureClasses, 4 );
	CHECK_EQ( theRMGParse.m_firstTile, 0 );
	CHECK_EQ( theRMGParse.m_numTiles, 4 );
	CHECK_EQ( theRMGParse.m_tileWidth, 2 );
	CHECK_EQ( theRMGParse.m_numBitmapTiles, 4 * 4 );	// four classes of four tiles
	CHECK( theRMGParse.m_numBlendedTiles > 1 );			// entry 0 is the default; the rest are real
	CHECK_EQ( theRMGParse.m_numCliffInfo, 1 );			// entry 0 is the default, no cliff faces
	CHECK_EQ( theRMGParse.m_blendBytesLeftOver, 0 );	// the blend table ends where the chunk does
	CHECK_STR( theRMGParse.m_textureName.str(), "GrassType1" );
	CHECK_STR( theRMGParse.m_textureNames[1].str(), "SandLargeType1" );
	CHECK_STR( theRMGParse.m_textureNames[2].str(), "DirtType1" );
	CHECK_STR( theRMGParse.m_textureNames[3].str(), "RocksType1" );
	CHECK_EQ( theRMGParse.m_firstTiles[3], 12 );

	CHECK_EQ( theRMGParse.m_weather, 0 );
	CHECK_EQ( theRMGParse.m_compression, 0 );

	// Neutral plus the civilian and skirmish sides, one default team each.
	CHECK_EQ( theRMGParse.m_numSides, 14 );
	CHECK_EQ( theRMGParse.m_numTeams, 14 );

	// One start waypoint per player, and the map is dressed rather than bare.
	CHECK_EQ( (Int)theRMGParse.m_waypointNames.size(), 4 );
	CHECK_STR( theRMGParse.m_waypointNames[0].str(), "Player_1_Start" );
	CHECK_STR( theRMGParse.m_waypointNames[3].str(), "Player_4_Start" );
	CHECK( theRMGParse.m_numObjects > 4 * 3 );

	Int numSupplyDocks = 0, numDerricks = 0, numProps = 0;
	for( Int i = 0; i < (Int)theRMGParse.m_objectNames.size(); i++ )
	{
		if( theRMGParse.m_objectNames[i].compare( "SupplyDock" ) == 0 )
			numSupplyDocks++;
		else if( theRMGParse.m_objectNames[i].compare( "TechOilDerrick" ) == 0 )
			numDerricks++;
		else if( theRMGParse.m_objectNames[i].startsWith( "Tree" ) ||
						 theRMGParse.m_objectNames[i].startsWith( "Rock" ) )
			numProps++;
	}

	CHECK_EQ( numSupplyDocks, 4 * 2 );	// one at home, one to fight over
	CHECK_EQ( numDerricks, 4 * 2 );
	CHECK( numProps > 20 );

	// Lakes flood the hollows the noise left, so how many fit is the map's business, but a map
	// this size has to have found somewhere for at least one.
	CHECK( theRMGParse.m_numWaterAreas >= 1 );
	CHECK( theRMGParse.m_numWaterAreas <= 4 );
	CHECK_EQ( (Int)theRMGParse.m_waterPoints.size(), theRMGParse.m_numWaterAreas );
	CHECK( theRMGParse.m_waterPoints[0].z > 0.0f );

	// Daylight, written into the map rather than left to whatever GameData.ini holds.
	CHECK_EQ( theRMGParse.m_timeOfDay, (Int)TIME_OF_DAY_AFTERNOON );
	CHECK_EQ( theRMGParse.m_lightingBytesLeftOver, 0 );
	CHECK( theRMGParse.m_terrainAmbient[0] > 0.2f );
}

//-------------------------------------------------------------------------------------------------
/** Every tile index has to name a tile the map actually loaded.  WorldHeightMap packs four grid
	quadrants into each source tile, so the index is (sourceTile<<2)|quadrant and the source tile
	has to land inside the one texture class the map declares. */
//-------------------------------------------------------------------------------------------------
TEST(every_generated_tile_index_names_a_tile_the_map_declared)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_seed = 4242;
	settings.m_playableCells = 64;
	settings.m_numPlayers = 2;

	std::vector<char> bytes;
	RandomMapGenerator::generate( settings, bytes );
	parseGeneratedMap( bytes );

	CHECK( theRMGParse.m_tiles.size() > 0 );

	Int worstSource = -1;
	Int quadrantsSeen = 0;
	for( Int i = 0; i < (Int)theRMGParse.m_tiles.size(); i++ )
	{
		Int source = theRMGParse.m_tiles[i] >> 2;
		Int quadrant = theRMGParse.m_tiles[i] & 3;
		if( source > worstSource )
			worstSource = source;
		quadrantsSeen |= (1 << quadrant);
	}

	CHECK( worstSource >= 0 );
	CHECK( worstSource < theRMGParse.m_numTextureClasses * theRMGParse.m_numTiles );
	CHECK_EQ( quadrantsSeen, 0xf );		// all four quadrants of the sheet get used
}

//-------------------------------------------------------------------------------------------------
/** The engine derives passability from the height field alone (BlendTileData below version 7
	makes it run initCliffFlagsFromHeights), marking a cell impassable when its four corners span
	more than PATHFIND_CLIFF_SLOPE_LIMIT_F world units.  The map now has cliffs on purpose - the
	ridge between two players is one - so the thing to check is not that there are none, but that
	they are where the layout wanted them: none near a start, and every start still reachable from
	every other through the chokepoints the ridge leaves open. */
//-------------------------------------------------------------------------------------------------
static Real RMGCellSpan( Int x, Int y )
{
	Int width = theRMGParse.m_width;
	Int a = theRMGParse.m_heights[y * width + x];
	Int b = theRMGParse.m_heights[y * width + x + 1];
	Int c = theRMGParse.m_heights[(y + 1) * width + x];
	Int d = theRMGParse.m_heights[(y + 1) * width + x + 1];

	Int lo = a, hi = a;
	if( b < lo ) lo = b;
	if( b > hi ) hi = b;
	if( c < lo ) lo = c;
	if( c > hi ) hi = c;
	if( d < lo ) lo = d;
	if( d > hi ) hi = d;

	return (Real)(hi - lo) * MAP_HEIGHT_SCALE;
}

/// Flood fill over the walkable cells of the last parsed map, starting at player one.
static void RMGFloodFillFromFirstStart( std::vector<char>& seen )
{
	const Real cliffLimit = 9.8f;						// PATHFIND_CLIFF_SLOPE_LIMIT_F
	Int width = theRMGParse.m_width;
	Int height = theRMGParse.m_height;

	seen.assign( width * height, 0 );

	Int startX = (Int)(theRMGParse.m_waypointPositions[0].x / MAP_XY_FACTOR + 0.5f) + theRMGParse.m_border;
	Int startY = (Int)(theRMGParse.m_waypointPositions[0].y / MAP_XY_FACTOR + 0.5f) + theRMGParse.m_border;

	std::vector<Int> stack;
	seen[startY * width + startX] = 1;
	stack.push_back( startY * width + startX );

	while( !stack.empty() )
	{
		Int index = stack.back();
		stack.pop_back();

		Int x = index % width;
		Int y = index / width;

		static const Int offsetX[4] = { 1, -1, 0, 0 };
		static const Int offsetY[4] = { 0, 0, 1, -1 };

		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 0 || ny < 0 || nx >= width - 1 || ny >= height - 1 )
				continue;
			if( seen[ny * width + nx] )
				continue;
			if( RMGCellSpan( nx, ny ) > cliffLimit )
				continue;

			seen[ny * width + nx] = 1;
			stack.push_back( ny * width + nx );
		}
	}
}

TEST(the_cliffs_are_where_the_layout_put_them_and_never_between_two_players)
{
	CHECK( bootOnce() );

	const Real cliffLimit = 9.8f;						// PATHFIND_CLIFF_SLOPE_LIMIT_F

	RandomMapSettings settings;

	for( Int players = 2; players <= 8; players += 2 )
	{
		settings.m_numPlayers = players;
		settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, players );

		for( Int seed = 1; seed <= 4; seed++ )
		{
			settings.m_seed = seed * 7919 + players;

			std::vector<char> bytes;
			RandomMapGenerator::generate( settings, bytes );
			parseGeneratedMap( bytes );

			Int width = theRMGParse.m_width;
			Int height = theRMGParse.m_height;
			Int lowest = 255, highest = 0;
			Int numCliffCells = 0;

			for( Int y = 0; y < height - 1; y++ )
			{
				for( Int x = 0; x < width - 1; x++ )
				{
					if( RMGCellSpan( x, y ) > cliffLimit )
						numCliffCells++;

					Int h = theRMGParse.m_heights[y * width + x];
					if( h < lowest ) lowest = h;
					if( h > highest ) highest = h;
				}
			}

			CHECK( highest > lowest + 8 );				// terrain, not a parade ground
			CHECK( numCliffCells > 0 );					// the ridge is supposed to be a cliff

			// Nothing steep within a base radius of a start, or the base cannot be laid out.
			for( Int i = 0; i < players; i++ )
			{
				Int cellX = (Int)(theRMGParse.m_waypointPositions[i].x / MAP_XY_FACTOR + 0.5f)
					+ theRMGParse.m_border;
				Int cellY = (Int)(theRMGParse.m_waypointPositions[i].y / MAP_XY_FACTOR + 0.5f)
					+ theRMGParse.m_border;

				// The flat disc is 13 cells, so the square that fits inside it is nine.
				for( Int dy = -9; dy <= 9; dy++ )
				{
					for( Int dx = -9; dx <= 9; dx++ )
						CHECK( RMGCellSpan( cellX + dx, cellY + dy ) <= cliffLimit );
				}
			}

			// Away from the start pads the ground has to roll. A map that is still a stack of
			// tables has almost no cells whose 5x5 neighbourhood varies by more than a byte.
			Int rolling = 0;
			Int sampled = 0;
			for( Int y = theRMGParse.m_border + 2; y < height - theRMGParse.m_border - 3; y += 3 )
			{
				for( Int x = theRMGParse.m_border + 2; x < width - theRMGParse.m_border - 3; x += 3 )
				{
					Bool nearStart = FALSE;
					for( Int i = 0; i < players; i++ )
					{
						Int sx = (Int)(theRMGParse.m_waypointPositions[i].x / MAP_XY_FACTOR + 0.5f)
							+ theRMGParse.m_border;
						Int sy = (Int)(theRMGParse.m_waypointPositions[i].y / MAP_XY_FACTOR + 0.5f)
							+ theRMGParse.m_border;
						Int dx = x - sx;
						Int dy = y - sy;
						if( dx * dx + dy * dy <= 30 * 30 )
						{
							nearStart = TRUE;
							break;
						}
					}
					if( nearStart )
						continue;

					Int lo = 255, hi = 0;
					for( Int dy = -2; dy <= 2; dy++ )
					{
						for( Int dx = -2; dx <= 2; dx++ )
						{
							Int h = theRMGParse.m_heights[(y + dy) * width + x + dx];
							if( h < lo ) lo = h;
							if( h > hi ) hi = h;
						}
					}

					sampled++;
					if( hi - lo >= 2 && hi - lo <= 16 )
						rolling++;
				}
			}

			CHECK( sampled > 0 );
			CHECK( rolling * 3 > sampled );			// at least a third of the open ground rolls

			// Every other start has to come back from player one's flood fill.
			std::vector<char> seen;
			RMGFloodFillFromFirstStart( seen );

			for( Int i = 1; i < players; i++ )
			{
				Int cellX = (Int)(theRMGParse.m_waypointPositions[i].x / MAP_XY_FACTOR + 0.5f)
					+ theRMGParse.m_border;
				Int cellY = (Int)(theRMGParse.m_waypointPositions[i].y / MAP_XY_FACTOR + 0.5f)
					+ theRMGParse.m_border;

				CHECK( seen[cellY * width + cellX] != 0 );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Start positions have to sit inside the playable area with room for a base, spread around the
	map rather than piled together, and each one needs its supply docks.  World coordinates run
	from the playable corner, so the border is not part of the arithmetic. */
//-------------------------------------------------------------------------------------------------
TEST(start_positions_land_inside_the_map_with_room_between_them)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;

	for( Int players = 2; players <= 8; players++ )
	{
		settings.m_numPlayers = players;
		settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, players );
		settings.m_seed = 1000 + players;

		std::vector<char> bytes;
		RandomMapGenerator::generate( settings, bytes );
		parseGeneratedMap( bytes );

		CHECK_EQ( (Int)theRMGParse.m_waypointPositions.size(), players );

		Int numSupplyDocks = 0;
		for( Int k = 0; k < (Int)theRMGParse.m_objectNames.size(); k++ )
		{
			if( theRMGParse.m_objectNames[k].compare( "SupplyDock" ) == 0 )
				numSupplyDocks++;
		}
		CHECK_EQ( numSupplyDocks, players * 2 );

		Real extent = (Real)settings.m_playableCells * MAP_XY_FACTOR;

		/* Enough ground for a base, and then a share of the map behind it: the site search takes
			whichever candidate is furthest from the starts already chosen, which walks every base
			into a corner unless the edge is kept clear of them. */
		Real margin = (Real)settings.m_playableCells * 0.13f * MAP_XY_FACTOR;

		Int i, j;
		for( i = 0; i < players; i++ )
		{
			const Coord3D& p = theRMGParse.m_waypointPositions[i];
			CHECK( p.x > margin && p.x < extent - margin );
			CHECK( p.y > margin && p.y < extent - margin );
		}

		/* No two players may start on top of each other.  Two and four players are chosen out of
			the noise; six and eight sit on a ring snapped to that noise. Either way a base disc
			is 26 cells across, and two of them have to leave room for something in between. */
		for( i = 0; i < players; i++ )
		{
			for( j = i + 1; j < players; j++ )
			{
				Real dx = theRMGParse.m_waypointPositions[i].x - theRMGParse.m_waypointPositions[j].x;
				Real dy = theRMGParse.m_waypointPositions[i].y - theRMGParse.m_waypointPositions[j].y;
				CHECK( sqrtf( dx * dx + dy * dy ) > 32.0f * MAP_XY_FACTOR );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The ground under a start position is flattened so a base can actually be laid out there.
	Check the cells around each start are level, not merely passable. */
//-------------------------------------------------------------------------------------------------
TEST(each_start_position_gets_flat_ground_to_build_on)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_seed = 31337;
	settings.m_numPlayers = 3;
	settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, 3 );

	std::vector<char> bytes;
	RandomMapGenerator::generate( settings, bytes );
	parseGeneratedMap( bytes );

	Int border = theRMGParse.m_border;
	Int width = theRMGParse.m_width;

	for( Int i = 0; i < (Int)theRMGParse.m_waypointPositions.size(); i++ )
	{
		Int cx = (Int)(theRMGParse.m_waypointPositions[i].x / MAP_XY_FACTOR) + border;
		Int cy = (Int)(theRMGParse.m_waypointPositions[i].y / MAP_XY_FACTOR) + border;

		Int at = theRMGParse.m_heights[cy * width + cx];
		for( Int dy = -9; dy <= 9; dy++ )
		{
			for( Int dx = -9; dx <= 9; dx++ )
			{
				Int h = theRMGParse.m_heights[(cy + dy) * width + cx + dx];
				CHECK_EQ( h, at );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A texture class change from one cell to the next is a hard edge on screen unless the cell on
	the low side of it carries a blend of the other texture over the corners they share.  Walk the
	map, find every boundary, and check the cell that is supposed to carry the blend does. */
//-------------------------------------------------------------------------------------------------
TEST(every_texture_boundary_is_blended_rather_than_cut)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_seed = 31337;
	settings.m_playableCells = 128;
	settings.m_numPlayers = 4;

	std::vector<char> bytes;
	RandomMapGenerator::generate( settings, bytes );
	parseGeneratedMap( bytes );

	Int width = theRMGParse.m_width;
	Int height = theRMGParse.m_height;

	CHECK_EQ( (Int)theRMGParse.m_blendIndexes.size(), theRMGParse.m_dataSize );
	CHECK_EQ( (Int)theRMGParse.m_blends.size(), theRMGParse.m_numBlendedTiles - 1 );
	CHECK( theRMGParse.m_numBlendedTiles < 16193 );		// NUM_BLEND_TILES, the reader's ceiling

	// Every entry has to be one the reader will take: a real tile, the alpha blend rather than a
	// custom edge class this map never declares, and the sentinel it asserts on.
	Int i;
	for( i = 0; i < (Int)theRMGParse.m_blends.size(); i++ )
	{
		const RMGBlendEntry& entry = theRMGParse.m_blends[i];
		Int source = entry.m_blendTileIndex >> 2;

		CHECK( source >= 0 );
		CHECK( source < theRMGParse.m_numBitmapTiles );
		CHECK_EQ( entry.m_customBlendEdgeClass, -1 );
		CHECK_EQ( entry.m_flag, 0x7ADA0000 );

		// A blend that asks for nothing is a table row nothing can draw.
		CHECK( entry.m_horizontal || entry.m_vertical || entry.m_rightDiagonal ||
					 entry.m_leftDiagonal );
	}

	Int boundaries = 0;
	Int blended = 0;

	for( Int y = 1; y < height - 1; y++ )
	{
		for( Int x = 1; x < width - 1; x++ )
		{
			Int mine = (theRMGParse.m_tiles[y * width + x] >> 2) / 4;

			/* The strongest of the eight neighbours is the one whose texture bleeds in, and the
				low side of the boundary is the cell that has to carry it.  Diagonals count: a
				corner touching a rock cell is a corner of rock. */
			Int strongest = mine;
			for( Int dy = -1; dy <= 1; dy++ )
			{
				for( Int dx = -1; dx <= 1; dx++ )
				{
					Int theirs = (theRMGParse.m_tiles[(y + dy) * width + x + dx] >> 2) / 4;
					if( theirs > strongest )
						strongest = theirs;
				}
			}

			if( strongest == mine )
				continue;

			boundaries++;

			Int blendIndex = theRMGParse.m_blendIndexes[y * width + x];
			if( blendIndex <= 0 )
				continue;

			CHECK( blendIndex < theRMGParse.m_numBlendedTiles );

			// and the texture painted over it is the neighbour's, not some third one
			Int blendClass = (theRMGParse.m_blends[blendIndex - 1].m_blendTileIndex >> 2) / 4;
			CHECK_EQ( blendClass, strongest );
			blended++;
		}
	}

	CHECK( boundaries > 100 );
	CHECK_EQ( blended, boundaries );
}

//-------------------------------------------------------------------------------------------------
/** Everybody opens on one supply dock and there is a second one out in the map for each of them,
	plus two oil derricks a player.  That is the economy the map promises; a seed that quietly
	drops one of them is a player who starts poorer than the rest. */
//-------------------------------------------------------------------------------------------------
TEST(every_player_gets_two_supply_docks_and_two_derricks_whatever_the_seed)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;

	for( Int players = 2; players <= 8; players += 2 )
	{
		settings.m_numPlayers = players;
		settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, players );

		for( Int seed = 1; seed <= 3; seed++ )
		{
			settings.m_seed = seed * 104729 + players;

			std::vector<char> bytes;
			RandomMapGenerator::generate( settings, bytes );
			parseGeneratedMap( bytes );

			Int numSupplyDocks = 0, numDerricks = 0, numPiles = 0;
			for( Int i = 0; i < (Int)theRMGParse.m_objectNames.size(); i++ )
			{
				if( theRMGParse.m_objectNames[i].compare( "SupplyDock" ) == 0 )
					numSupplyDocks++;
				else if( theRMGParse.m_objectNames[i].compare( "TechOilDerrick" ) == 0 )
					numDerricks++;
				else if( theRMGParse.m_objectNames[i].compare( "SupplyPileSmall" ) == 0 )
					numPiles++;
			}

			CHECK_EQ( numSupplyDocks, players * 2 );
			CHECK_EQ( numDerricks, players * 2 );
			CHECK_EQ( numPiles, players * 2 );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Home docks sit on one ring. A seed that hands one player a dock at 18 cells and the next
	player the same dock at 30 is the independent-search leftover Age of Empires does not do. */
//-------------------------------------------------------------------------------------------------
TEST(home_docks_sit_on_the_same_ring_for_every_player)
{
	CHECK( bootOnce() );

	static const Int thePlayers[] = { 2, 4, 8 };
	const Int numPlayers = sizeof(thePlayers) / sizeof(thePlayers[0]);

	for( Int p = 0; p < numPlayers; p++ )
	{
		Int players = thePlayers[p];
		RandomMapSettings settings;
		settings.m_numPlayers = players;
		settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, players );
		settings.m_seed = 11 + players;

		std::vector<char> bytes;
		RandomMapGenerator::generate( settings, bytes );
		parseGeneratedMap( bytes );

		CHECK_EQ( (Int)theRMGParse.m_waypointPositions.size(), players );

		Real nearestDock[8];
		for( Int i = 0; i < players; i++ )
		{
			Real best = 1.0e9f;
			for( Int k = 0; k < (Int)theRMGParse.m_objectNames.size(); k++ )
			{
				if( theRMGParse.m_objectNames[k].compare( "SupplyDock" ) != 0 )
					continue;
				Real dx = theRMGParse.m_waypointPositions[i].x - theRMGParse.m_objectPositions[k].x;
				Real dy = theRMGParse.m_waypointPositions[i].y - theRMGParse.m_objectPositions[k].y;
				Real d = sqrtf( dx * dx + dy * dy );
				if( d < best )
					best = d;
			}
			nearestDock[i] = best;
			CHECK( best > 16.0f * MAP_XY_FACTOR );
			CHECK( best < 36.0f * MAP_XY_FACTOR );
		}

		Real lo = nearestDock[0], hi = nearestDock[0];
		for( Int i = 1; i < players; i++ )
		{
			if( nearestDock[i] < lo ) lo = nearestDock[i];
			if( nearestDock[i] > hi ) hi = nearestDock[i];
		}
		CHECK( hi - lo < 12.0f * MAP_XY_FACTOR );
	}
}

static Real RMGNearestOf( const Coord3D& from, const char *templateName )
{
	Real best = 1.0e9f;
	for( Int k = 0; k < (Int)theRMGParse.m_objectNames.size(); k++ )
	{
		if( theRMGParse.m_objectNames[k].compare( templateName ) != 0 )
			continue;
		Real dx = from.x - theRMGParse.m_objectPositions[k].x;
		Real dy = from.y - theRMGParse.m_objectPositions[k].y;
		Real d = sqrtf( dx * dx + dy * dy );
		if( d < best )
			best = d;
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
/** Derricks and small piles sit on the same compass from every start, so one seat cannot
	open with them in the yard and another with them a lake away. */
//-------------------------------------------------------------------------------------------------
TEST(derricks_and_piles_are_as_even_as_the_home_docks)
{
	CHECK( bootOnce() );

	static const Int thePlayers[] = { 2, 4, 8 };
	for( Int p = 0; p < 3; p++ )
	{
		Int players = thePlayers[p];
		RandomMapSettings settings;
		settings.m_numPlayers = players;
		settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, players );
		settings.m_seed = 11 + players;

		std::vector<char> bytes;
		RandomMapGenerator::generate( settings, bytes );
		parseGeneratedMap( bytes );

		Real dLo = 1.0e9f, dHi = 0.0f, pLo = 1.0e9f, pHi = 0.0f;
		for( Int i = 0; i < players; i++ )
		{
			Real derrick = RMGNearestOf( theRMGParse.m_waypointPositions[i], "TechOilDerrick" );
			Real pile = RMGNearestOf( theRMGParse.m_waypointPositions[i], "SupplyPileSmall" );
			if( derrick < dLo ) dLo = derrick;
			if( derrick > dHi ) dHi = derrick;
			if( pile < pLo ) pLo = pile;
			if( pile > pHi ) pHi = pile;
		}

		CHECK( dHi - dLo < 14.0f * MAP_XY_FACTOR );
		CHECK( pHi - pLo < 16.0f * MAP_XY_FACTOR );
	}
}

//-------------------------------------------------------------------------------------------------
/** Structures face 45-degree compass points, not a random spin. */
//-------------------------------------------------------------------------------------------------
TEST(placed_objects_face_forty_five_degrees)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_numPlayers = 4;
	settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, 4 );
	settings.m_seed = 15;

	std::vector<char> bytes;
	RandomMapGenerator::generate( settings, bytes );
	parseGeneratedMap( bytes );

	const Real step = PI * 0.25f;
	Int checked = 0;
	for( Int k = 0; k < (Int)theRMGParse.m_objectNames.size(); k++ )
	{
		const AsciiString& name = theRMGParse.m_objectNames[k];
		if( name.compare( "SupplyDock" ) != 0 &&
				name.compare( "TechOilDerrick" ) != 0 &&
				name.compare( "SupplyPileSmall" ) != 0 &&
				name.compare( "CivilianBunker01" ) != 0 &&
				!name.startsWith( "Tree" ) &&
				!name.startsWith( "Rock" ) )
			continue;

		Real angle = theRMGParse.m_objectAngles[k];
		Real n = angle / step;
		Real nearest = floorf( n + 0.5f );
		CHECK( fabsf( n - nearest ) < 0.05f );
		checked++;
	}
	CHECK( checked > 20 );
}

//-------------------------------------------------------------------------------------------------
/** Eight seats on the furthest-from-the-rest search walk the last ones into the corners. They
	sit on a ring instead, still inside the edge inset. */
//-------------------------------------------------------------------------------------------------
TEST(eight_players_sit_on_a_ring_not_in_the_corners)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_numPlayers = 8;
	settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, 8 );
	settings.m_seed = 7;

	std::vector<char> bytes;
	RandomMapGenerator::generate( settings, bytes );
	parseGeneratedMap( bytes );

	CHECK_EQ( (Int)theRMGParse.m_waypointPositions.size(), 8 );

	Real centre = (Real)settings.m_playableCells * 0.5f * MAP_XY_FACTOR;
	Real lo = 1.0e9f, hi = 0.0f;
	for( Int i = 0; i < 8; i++ )
	{
		Real dx = theRMGParse.m_waypointPositions[i].x - centre;
		Real dy = theRMGParse.m_waypointPositions[i].y - centre;
		Real d = sqrtf( dx * dx + dy * dy );
		if( d < lo ) lo = d;
		if( d > hi ) hi = d;
	}

	CHECK( lo > 0.22f * (Real)settings.m_playableCells * MAP_XY_FACTOR );
	CHECK( hi - lo < 0.14f * (Real)settings.m_playableCells * MAP_XY_FACTOR );
}

/// Whether a point falls inside one of the map's water polygons, by the usual crossing count.
static Bool insideAWaterArea( Real x, Real y )
{
	for( Int area = 0; area < (Int)theRMGParse.m_waterPolygons.size(); area++ )
	{
		const std::vector<Coord3D>& polygon = theRMGParse.m_waterPolygons[area];
		Bool inside = FALSE;

		for( Int point = 0, last = (Int)polygon.size() - 1; point < (Int)polygon.size();
				 last = point++ )
		{
			if( (polygon[point].y > y) == (polygon[last].y > y) )
				continue;

			Real crossingX = polygon[point].x + (y - polygon[point].y) *
					(polygon[last].x - polygon[point].x) / (polygon[last].y - polygon[point].y);
			if( x < crossingX )
				inside = !inside;
		}

		if( inside )
			return TRUE;
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** A road object pair does not care what is under it - it paves the ground, lake bed included, and
	a street laid across a lake comes out as tarmac running into the water. The streets are clipped
	to their dry runs and a building on a wet plot is dropped, and this is what says both still
	happen. Towns are placed near water on purpose, so this test needs seeds with lakes in them. */
//-------------------------------------------------------------------------------------------------
TEST(no_street_or_civilian_building_stands_in_a_lake)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_numPlayers = 4;
	settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, 4 );

	Int mapsWithWater = 0;

	for( Int seed = 1; seed <= 6; seed++ )
	{
		settings.m_seed = seed * 7919;

		std::vector<char> bytes;
		RandomMapGenerator::generate( settings, bytes );
		parseGeneratedMap( bytes );

		if( theRMGParse.m_waterPolygons.empty() )
			continue;
		mapsWithWater++;

		for( Int i = 0; i < (Int)theRMGParse.m_objectNames.size(); i++ )
		{
			Bool isStreet = (theRMGParse.m_objectFlags[i] & (FLAG_ROAD_POINT1 | FLAG_ROAD_POINT2)) != 0;
			Bool isBuilding = theRMGParse.m_objectNames[i].startsWith( "Stan" ) ||
												theRMGParse.m_objectNames[i].startsWith( "Asian" ) ||
												theRMGParse.m_objectNames[i].startsWith( "Civilian" );
			if( !isStreet && !isBuilding )
				continue;

			CHECK( !insideAWaterArea( theRMGParse.m_objectPositions[i].x,
																theRMGParse.m_objectPositions[i].y ) );
		}
	}

	CHECK( mapsWithWater > 0 );
}

//-------------------------------------------------------------------------------------------------
/** A lake is a polygon, not a flood: ground outside one that lies below the water surface is not
	filled in, it is drawn as land with the water standing over it, and the lake reads as a puddle
	sitting on top of the map.  Lakes are carved into the lowest ground there is, so this is what the
	noise leaves behind unless the land is lifted out of the water afterwards. */
//-------------------------------------------------------------------------------------------------
TEST(no_dry_ground_lies_below_the_water_surface)
{
	CHECK( bootOnce() );

	Int mapsWithWater = 0;

	for( Int seed = 1; seed <= 4; seed++ )
	{
		RandomMapSettings settings;
		settings.m_seed = seed * 7919;
		settings.m_numPlayers = 4;
		settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_SMALL, 4 );

		std::vector<char> bytes;
		RandomMapGenerator::generate( settings, bytes );
		parseGeneratedMap( bytes );

		if( theRMGParse.m_waterPolygons.empty() )
			continue;
		mapsWithWater++;

		Real waterZ = theRMGParse.m_waterPolygons[0][0].z;
		CHECK( waterZ > 0.0f );

		for( Int y = 0; y < theRMGParse.m_height; y += 2 )
		{
			for( Int x = 0; x < theRMGParse.m_width; x += 2 )
			{
				Real worldX = (Real)(x - theRMGParse.m_border) * MAP_XY_FACTOR;
				Real worldY = (Real)(y - theRMGParse.m_border) * MAP_XY_FACTOR;
				if( insideAWaterArea( worldX, worldY ) )
					continue;

				Real ground = (Real)theRMGParse.m_heights[y * theRMGParse.m_width + x] * MAP_HEIGHT_SCALE;
				CHECK( ground >= waterZ );
			}
		}
	}

	CHECK( mapsWithWater > 0 );
}

//-------------------------------------------------------------------------------------------------
/** The old lakes were a noisy circle: thirty-two radii around a seed. A basin that follows a
	valley is longer in one direction than the other, which is what this measures. */
//-------------------------------------------------------------------------------------------------
TEST(a_generated_lake_follows_the_valley)
{
	CHECK( bootOnce() );

	Int elongated = 0;

	for( Int seed = 1; seed <= 6; seed++ )
	{
		RandomMapSettings settings;
		settings.m_seed = seed * 7919;
		settings.m_numPlayers = 4;
		settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_SMALL, 4 );

		std::vector<char> bytes;
		RandomMapGenerator::generate( settings, bytes );
		parseGeneratedMap( bytes );

		for( Int area = 0; area < (Int)theRMGParse.m_waterPolygons.size(); area++ )
		{
			const std::vector<Coord3D>& polygon = theRMGParse.m_waterPolygons[area];
			if( (Int)polygon.size() < 3 )
				continue;

			Real minX = polygon[0].x, maxX = polygon[0].x;
			Real minY = polygon[0].y, maxY = polygon[0].y;
			for( Int p = 1; p < (Int)polygon.size(); p++ )
			{
				if( polygon[p].x < minX ) minX = polygon[p].x;
				if( polygon[p].x > maxX ) maxX = polygon[p].x;
				if( polygon[p].y < minY ) minY = polygon[p].y;
				if( polygon[p].y > maxY ) maxY = polygon[p].y;
			}

			Real width = maxX - minX;
			Real height = maxY - minY;
			if( width < 1.0f ) width = 1.0f;
			if( height < 1.0f ) height = 1.0f;
			Real aspect = ( width > height ) ? width / height : height / width;
			if( aspect >= 1.55f )
				elongated++;
		}
	}

	CHECK( elongated > 0 );
}

//-------------------------------------------------------------------------------------------------
/** Two towns on one map are two towns, not the same grid stamped twice.  Each rolls its own street
	count, block spacing and rotation out of the seed, so what says the towns are generated rather
	than placed is that the same seed at two player counts does not lay the same number of buildings
	and streets down. */
//-------------------------------------------------------------------------------------------------
TEST(a_town_is_rolled_rather_than_stamped)
{
	CHECK( bootOnce() );

	Int lastBuildings = -1;
	Int differentLayouts = 0;

	for( Int seed = 1; seed <= 5; seed++ )
	{
		RandomMapSettings settings;
		settings.m_seed = seed * 31337;
		settings.m_numPlayers = 4;
		settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, 4 );

		std::vector<char> bytes;
		RandomMapGenerator::generate( settings, bytes );
		parseGeneratedMap( bytes );

		Int buildings = 0, streetPoints = 0;
		for( Int i = 0; i < (Int)theRMGParse.m_objectNames.size(); i++ )
		{
			if( (theRMGParse.m_objectFlags[i] & (FLAG_ROAD_POINT1 | FLAG_ROAD_POINT2)) != 0 )
				streetPoints++;
			else if( theRMGParse.m_objectNames[i].startsWith( "Stan" ) ||
							 theRMGParse.m_objectNames[i].startsWith( "Asian" ) ||
							 theRMGParse.m_objectNames[i].startsWith( "Civilian" ) )
				buildings++;
		}

		CHECK( buildings > 0 );
		CHECK( streetPoints > 0 );
		CHECK( streetPoints % 2 == 0 );			// a road is a pair, or the renderer draws nothing

		if( lastBuildings >= 0 && buildings != lastBuildings )
			differentLayouts++;
		lastBuildings = buildings;
	}

	CHECK( differentLayouts > 0 );
}

//-------------------------------------------------------------------------------------------------
/** A map has to hold the players it is generated for.  The three sizes are three different maps
	for the same game, and every one of them grows with the number of players rather than packing
	eight bases into a duel map. */
//-------------------------------------------------------------------------------------------------
TEST(map_size_is_chosen_and_then_grows_with_the_players)
{
	CHECK( bootOnce() );

	Int size, players;

	for( size = 0; size < RANDOM_MAP_SIZE_COUNT; size++ )
	{
		for( players = RandomMapGenerator::MIN_PLAYERS;
				 players < RandomMapGenerator::MAX_PLAYERS; players++ )
		{
			CHECK( RandomMapGenerator::cellsFor( (RandomMapSize)size, players ) <
						 RandomMapGenerator::cellsFor( (RandomMapSize)size, players + 1 ) );
		}
	}

	for( players = RandomMapGenerator::MIN_PLAYERS;
			 players <= RandomMapGenerator::MAX_PLAYERS; players++ )
	{
		// "small" and "large" are both taken: rpcndr.h defines them as char and int.
		Int smallCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_SMALL, players );
		Int normalCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, players );
		Int largeCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_LARGE, players );

		CHECK( smallCells < normalCells );
		CHECK( normalCells < largeCells );
		CHECK( smallCells >= RandomMapGenerator::MIN_CELLS );
		CHECK( largeCells <= RandomMapGenerator::MAX_CELLS );
	}

	// A count nobody asked for is the normal size for that many players, and the clamp says so
	// rather than generating a map the caller cannot describe.
	RandomMapSettings settings;
	settings.m_numPlayers = 6;
	settings.m_playableCells = 0;
	RandomMapGenerator::clampSettings( settings );
	CHECK_EQ( settings.m_playableCells,
		RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, 6 ) );

	// and the map that comes out is the size the settings say it is
	settings.m_seed = 4;
	std::vector<char> bytes;
	RandomMapGenerator::generate( settings, bytes );
	parseGeneratedMap( bytes );
	CHECK_EQ( theRMGParse.m_boundaryX, settings.m_playableCells );
}

//-------------------------------------------------------------------------------------------------
/** A generated map is not playable if a start cannot walk to the money, or has only one way out
	of its own base. The flood here is the pathfinder's own: four neighbours, a cliff when the
	cell's corners span more than PATHFIND_CLIFF_SLOPE_LIMIT_F, and water polygons are closed.
	RMGFloodFillFromFirstStart ignores water, so a dock on an island still counted as reached. */
//-------------------------------------------------------------------------------------------------
static void RMGWorldToCell( const Coord3D& world, Int *cellX, Int *cellY )
{
	*cellX = (Int)(world.x / MAP_XY_FACTOR + 0.5f) + theRMGParse.m_border;
	*cellY = (Int)(world.y / MAP_XY_FACTOR + 0.5f) + theRMGParse.m_border;
}

static Bool RMGCellIsWalkable( Int x, Int y )
{
	if( x < 0 || y < 0 || x >= theRMGParse.m_width - 1 || y >= theRMGParse.m_height - 1 )
		return FALSE;
	if( RMGCellSpan( x, y ) > 9.8f )
		return FALSE;

	Real worldX = (Real)(x - theRMGParse.m_border) * MAP_XY_FACTOR;
	Real worldY = (Real)(y - theRMGParse.m_border) * MAP_XY_FACTOR;
	if( insideAWaterArea( worldX, worldY ) )
		return FALSE;

	return TRUE;
}

static void RMGFloodFillWalkable( Int startX, Int startY, std::vector<char>& seen )
{
	Int width = theRMGParse.m_width;
	seen.assign( width * theRMGParse.m_height, 0 );

	if( !RMGCellIsWalkable( startX, startY ) )
		return;

	std::vector<Int> stack;
	seen[startY * width + startX] = 1;
	stack.push_back( startY * width + startX );

	static const Int offsetX[4] = { 1, -1, 0, 0 };
	static const Int offsetY[4] = { 0, 0, 1, -1 };

	while( !stack.empty() )
	{
		Int index = stack.back();
		stack.pop_back();

		Int x = index % width;
		Int y = index / width;

		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 0 || ny < 0 || nx >= theRMGParse.m_width - 1 || ny >= theRMGParse.m_height - 1 )
				continue;
			if( seen[ny * width + nx] )
				continue;
			if( !RMGCellIsWalkable( nx, ny ) )
				continue;

			seen[ny * width + nx] = 1;
			stack.push_back( ny * width + nx );
		}
	}
}

/// Walkable gaps in the ring at the base blend radius. One gap is a cul-de-sac.
/// The blend disc is 26 cells; a cell on that circle can still read as a cliff
/// because its far corners sit on the original terrace, so a sample is open if
/// any of the three radii 26, 27 and 28 along the same ray is walkable. A ring
/// that is open all the way round is every way out, not none.
static Int RMGStartRingCrossings( Int startX, Int startY )
{
	const Int samples = 360;
	char walkable[360];
	Int walkableCount = 0;

	for( Int i = 0; i < samples; i++ )
	{
		Real angle = 2.0f * PI * (Real)i / (Real)samples;
		Real dirX = Cos( angle );
		Real dirY = Sin( angle );
		Bool open = FALSE;
		for( Int radius = 26; radius <= 28; radius++ )
		{
			Int x = (Int)((Real)startX + (Real)radius * dirX + 0.5f);
			Int y = (Int)((Real)startY + (Real)radius * dirY + 0.5f);
			if( RMGCellIsWalkable( x, y ) )
			{
				open = TRUE;
				break;
			}
		}
		walkable[i] = open ? 1 : 0;
		if( open )
			walkableCount++;
	}

	if( walkableCount == samples )
		return 2;

	Int crossings = 0;
	Int longestOpen = 0;
	Int run = 0;
	for( Int i = 0; i < samples * 2; i++ )
	{
		Int idx = i % samples;
		Int prev = (idx == 0) ? samples - 1 : idx - 1;
		if( i < samples && walkable[idx] && !walkable[prev] )
			crossings++;

		if( walkable[idx] )
		{
			run++;
			if( run > longestOpen )
				longestOpen = run;
		}
		else
		{
			run = 0;
		}
	}

	if( crossings >= 2 )
		return crossings;

	// One opening that is at least a right angle is a front, not a cul-de-sac.
	if( longestOpen >= 90 )
		return 2;

	return crossings;
}

static Bool RMGSeenHasObjectAt( const std::vector<char>& seen, const Coord3D& world )
{
	Int x, y;
	RMGWorldToCell( world, &x, &y );
	if( x < 0 || y < 0 || x >= theRMGParse.m_width || y >= theRMGParse.m_height )
		return FALSE;
	if( seen[y * theRMGParse.m_width + x] )
		return TRUE;

	static const Int ox[4] = { 1, -1, 0, 0 };
	static const Int oy[4] = { 0, 0, 1, -1 };
	for( Int i = 0; i < 4; i++ )
	{
		Int nx = x + ox[i];
		Int ny = y + oy[i];
		if( nx < 0 || ny < 0 || nx >= theRMGParse.m_width || ny >= theRMGParse.m_height )
			continue;
		if( seen[ny * theRMGParse.m_width + nx] )
			return TRUE;
	}

	return FALSE;
}

static void RMGDescribePlayabilityFailure( Int players, Int seed )
{
	Int width = theRMGParse.m_width;

	for( Int i = 0; i < (Int)theRMGParse.m_waypointPositions.size(); i++ )
	{
		Int startX, startY;
		RMGWorldToCell( theRMGParse.m_waypointPositions[i], &startX, &startY );

		std::vector<char> seen;
		RMGFloodFillWalkable( startX, startY, seen );

		if( !RMGCellIsWalkable( startX, startY ) )
		{
			printf( "  playability fail players %d seed %d start %d: start cell is not walkable\n",
				players, seed, i );
			continue;
		}

		for( Int j = 0; j < (Int)theRMGParse.m_waypointPositions.size(); j++ )
		{
			if( j == i )
				continue;

			Int otherX, otherY;
			RMGWorldToCell( theRMGParse.m_waypointPositions[j], &otherX, &otherY );
			if( !seen[otherY * width + otherX] )
				printf( "  playability fail players %d seed %d start %d: cannot walk to start %d\n",
					players, seed, i, j );
		}

		Int docks = 0, derricks = 0, moneyUnreachable = 0;
		for( Int k = 0; k < (Int)theRMGParse.m_objectNames.size(); k++ )
		{
			Bool isDock = theRMGParse.m_objectNames[k].compare( "SupplyDock" ) == 0;
			Bool isDerrick = theRMGParse.m_objectNames[k].compare( "TechOilDerrick" ) == 0;
			Bool isPile = theRMGParse.m_objectNames[k].compare( "SupplyPileSmall" ) == 0;
			if( !isDock && !isDerrick && !isPile )
				continue;

			if( RMGSeenHasObjectAt( seen, theRMGParse.m_objectPositions[k] ) )
			{
				if( isDock )
					docks++;
				else if( isDerrick )
					derricks++;
			}
			else
			{
				moneyUnreachable++;
			}
		}

		if( docks < 1 )
			printf( "  playability fail players %d seed %d start %d: reachable docks %d\n",
				players, seed, i, docks );
		if( derricks < 2 )
			printf( "  playability fail players %d seed %d start %d: reachable derricks %d\n",
				players, seed, i, derricks );
		if( moneyUnreachable > 0 )
			printf( "  playability fail players %d seed %d start %d: %d docks/derricks unreachable\n",
				players, seed, i, moneyUnreachable );

		Int crossings = RMGStartRingCrossings( startX, startY );
		if( crossings < 2 )
			printf( "  playability fail players %d seed %d start %d: %d ring crossing(s)\n",
				players, seed, i, crossings );
	}
}

static Bool RMGParsedMapIsPlayable( void )
{
	Int width = theRMGParse.m_width;

	for( Int i = 0; i < (Int)theRMGParse.m_waypointPositions.size(); i++ )
	{
		Int startX, startY;
		RMGWorldToCell( theRMGParse.m_waypointPositions[i], &startX, &startY );

		std::vector<char> seen;
		RMGFloodFillWalkable( startX, startY, seen );

		if( !RMGCellIsWalkable( startX, startY ) )
			return FALSE;

		for( Int j = 0; j < (Int)theRMGParse.m_waypointPositions.size(); j++ )
		{
			if( j == i )
				continue;

			Int otherX, otherY;
			RMGWorldToCell( theRMGParse.m_waypointPositions[j], &otherX, &otherY );
			if( !seen[otherY * width + otherX] )
				return FALSE;
		}

		Int docks = 0, derricks = 0;
		for( Int k = 0; k < (Int)theRMGParse.m_objectNames.size(); k++ )
		{
			Bool isDock = theRMGParse.m_objectNames[k].compare( "SupplyDock" ) == 0;
			Bool isDerrick = theRMGParse.m_objectNames[k].compare( "TechOilDerrick" ) == 0;
			Bool isPile = theRMGParse.m_objectNames[k].compare( "SupplyPileSmall" ) == 0;
			if( !isDock && !isDerrick && !isPile )
				continue;

			if( !RMGSeenHasObjectAt( seen, theRMGParse.m_objectPositions[k] ) )
				return FALSE;

			if( isDock )
				docks++;
			else if( isDerrick )
				derricks++;
		}

		if( docks < 1 || derricks < 2 )
			return FALSE;

		if( RMGStartRingCrossings( startX, startY ) < 2 )
			return FALSE;
	}

	return TRUE;
}

TEST(every_start_reaches_its_money_and_has_two_ways_out)
{
	CHECK( bootOnce() );

	/* Before the playability repair, 2-player seed 1 and 4-player seed 12345 (and 32 others on
		this sweep) failed the ring check. They stay in the seed list below. */

	static const Int thePlayers[] = { 2, 4, 8 };
	const Int numPlayers = sizeof(thePlayers) / sizeof(thePlayers[0]);

	Int maps = 0;
	Int failed = 0;

	for( Int p = 0; p < numPlayers; p++ )
	{
		Int players = thePlayers[p];
		std::vector<Int> seeds;
		Int s;
		for( s = 1; s <= 12; s++ )
			seeds.push_back( s );
		for( s = 1; s <= 4; s++ )
			seeds.push_back( s * 7919 + players );
		for( s = 1; s <= 3; s++ )
			seeds.push_back( s * 104729 + players );
		seeds.push_back( 1000 + players );
		if( players == 2 )
			seeds.push_back( 0 );
		if( players == 4 )
			seeds.push_back( 12345 );
		if( players == 8 )
			seeds.push_back( 7 );

		for( s = 0; s < (Int)seeds.size(); s++ )
		{
			Int seed = seeds[s];
			Bool already = FALSE;
			for( Int t = 0; t < s; t++ )
			{
				if( seeds[t] == seed )
					already = TRUE;
			}
			if( already )
				continue;

			RandomMapSettings settings;
			settings.m_seed = seed;
			settings.m_numPlayers = players;
			settings.m_playableCells = RandomMapGenerator::cellsFor( RANDOM_MAP_SIZE_NORMAL, players );

			std::vector<char> bytes;
			RandomMapGenerator::generate( settings, bytes );
			parseGeneratedMap( bytes );
			maps++;

			if( !RMGParsedMapIsPlayable() )
			{
				failed++;
				RMGDescribePlayabilityFailure( players, seed );
			}

			CHECK( RMGParsedMapIsPlayable() );
		}
	}

	CHECK( maps >= 12 * numPlayers );
	if( failed == 0 )
		printf( "PASS every_start_reaches_its_money_and_has_two_ways_out (%d maps)\n", maps );
}

//-------------------------------------------------------------------------------------------------
/** The seed is worthless across two builds unless both builds turn it into the same bytes, and
	nothing warns anybody when they stop doing so.  These numbers are that warning: change the
	generator and this test fails until RANDOM_MAP_GENERATOR_VERSION and the recorded fingerprints
	are both brought up to date, which is exactly the discipline the version was invented for. */
//-------------------------------------------------------------------------------------------------
TEST(the_generator_still_turns_a_seed_into_the_bytes_it_used_to)
{
	CHECK( bootOnce() );

	CHECK_EQ( RANDOM_MAP_GENERATOR_VERSION, 10 );

	struct RMGFingerprint { Int m_seed, m_players, m_cells; UnsignedInt m_crc; };
	static const RMGFingerprint theFingerprints[] =
	{
		{ 0, 2, 64, 0x6FE5FB90 },
		{ 12345, 4, 96, 0x71D5E795 },
		{ 7, 8, 128, 0x9C0240F4 },
	};
	const Int numFingerprints = sizeof(theFingerprints) / sizeof(theFingerprints[0]);

	for( Int i = 0; i < numFingerprints; i++ )
	{
		RandomMapSettings settings;
		settings.m_seed = theFingerprints[i].m_seed;
		settings.m_numPlayers = theFingerprints[i].m_players;
		settings.m_playableCells = theFingerprints[i].m_cells;

		UnsignedInt crc = RandomMapGenerator::fingerprint( settings );
		if( crc != theFingerprints[i].m_crc )
			printf( "  fingerprint seed %d players %d cells %d is 0x%08X\n",
				settings.m_seed, settings.m_numPlayers, settings.m_playableCells, crc );

		CHECK( crc == theFingerprints[i].m_crc );
	}

	/* The height field is x87 arithmetic, so it used to come out differently depending on the
		precision the last DLL left the FPU in - the machine that generated the map and the machine
		that joined it would disagree about the ground. generate() now puts the FPU where the
		simulation keeps it, and this is what says so: the same seed through a control word no
		simulation would ever run in. */
	UnsignedInt entry = _controlfp( 0, 0 );

	RandomMapSettings settings;
	settings.m_seed = theFingerprints[0].m_seed;
	settings.m_numPlayers = theFingerprints[0].m_players;
	settings.m_playableCells = theFingerprints[0].m_cells;

	_controlfp( _PC_64 | _RC_CHOP, _MCW_PC | _MCW_RC );
	UnsignedInt inADriversMode = RandomMapGenerator::fingerprint( settings );
	_controlfp( entry, _MCW_PC | _MCW_RC );

	CHECK( inADriversMode == theFingerprints[0].m_crc );

	// and the caller's mode is handed back rather than left at the simulation's
	CHECK_EQ( _controlfp( 0, 0 ), entry );
}

//-------------------------------------------------------------------------------------------------
/** A generated map is never written anywhere.  It is staged in memory and TheFileSystem serves it,
	so the map cache, the loader, the CRC and the preview all reach it the way they reach a map on
	a disk - and the local file system, which is the only thing here that can touch a disk at all,
	has never heard of it. */
//-------------------------------------------------------------------------------------------------
TEST(a_generated_map_is_served_out_of_memory_and_written_nowhere)
{
	CHECK( bootOnce() );

	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;

	RandomMapSettings settings;
	settings.m_seed = 4242;
	settings.m_numPlayers = 2;
	settings.m_playableCells = 96;

	AsciiString path;
	CHECK( stageRandomMap( settings, path ) );
	CHECK( isGeneratedMapPath( path ) );

	// the settings are in the name, which is what lets any machine rebuild the same bytes
	CHECK( path.endsWithNoCase( "_4242_2p_96c.map" ) );

	// it exists, and it exists nowhere on a disk
	CHECK( TheFileSystem->doesFileExist( path.str() ) );
	CHECK( !TheLocalFileSystem->doesFileExist( path.str() ) );

	std::vector<char> expected;
	RandomMapGenerator::generate( settings, expected );

	FileInfo info;
	CHECK( TheFileSystem->getFileInfo( path, &info ) );
	CHECK_EQ( (Int)info.sizeLow, (Int)expected.size() );

	// and reading it back gives the generator's own bytes
	File *file = TheFileSystem->openFile( path.str(), File::READ | File::BINARY );
	CHECK( file != NULL );
	if( file )
	{
		std::vector<char> read( expected.size() );
		CHECK_EQ( file->read( &read[0], (Int)read.size() ), (Int)expected.size() );
		CHECK( memcmp( &read[0], &expected[0], expected.size() ) == 0 );
		file->close();
	}

	// a map the store has forgotten is rebuilt from its name: this is what plays a replay of a
	// generated map back on a machine that never staged it
	for( Int other = 0; other < 4; other++ )
	{
		RandomMapSettings evict;
		evict.m_seed = 5000 + other;
		evict.m_numPlayers = 2;
		evict.m_playableCells = 96;
		AsciiString evicted;
		CHECK( stageRandomMap( evict, evicted ) );
	}

	File *rebuilt = TheFileSystem->openFile( path.str(), File::READ | File::BINARY );
	CHECK( rebuilt != NULL );
	if( rebuilt )
	{
		std::vector<char> read( expected.size() );
		CHECK_EQ( rebuilt->read( &read[0], (Int)read.size() ), (Int)expected.size() );
		CHECK( memcmp( &read[0], &expected[0], expected.size() ) == 0 );
		rebuilt->close();
	}

	// nothing else is one of ours, and neither is a name from another generator version
	CHECK( !isGeneratedMapPath( AsciiString( "Maps\\Tournament Desert\\Tournament Desert.map" ) ) );
	CHECK( !isGeneratedMapPath( AsciiString( "Maps\\RMG_v1_7_2p_96c\\RMG_v1_7_2p_96c.map" ) ) );

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

//-------------------------------------------------------------------------------------------------
/** getMapPreviewImage looks for a 128x128 tga beside the map and shows nothing at all when it is
	not there, which is what the map list did for every generated map.  The bytes have to be a tga
	an image loader will take: uncompressed true colour, three bytes a pixel, right way up. */
//-------------------------------------------------------------------------------------------------
TEST(a_generated_map_comes_with_a_preview_the_map_list_can_show)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_seed = 99;
	settings.m_playableCells = 128;
	settings.m_numPlayers = 4;

	std::vector<char> tga;
	RandomMapGenerator::generatePreview( settings, tga );

	const Int headerBytes = 18;
	const Int pixels = RandomMapGenerator::PREVIEW_PIXELS;

	CHECK_EQ( (Int)tga.size(), headerBytes + pixels * pixels * 3 );
	CHECK_EQ( (Int)(UnsignedByte)tga[2], 2 );			// uncompressed true colour
	CHECK_EQ( (Int)(UnsignedByte)tga[16], 24 );			// bits per pixel
	CHECK_EQ( (Int)(UnsignedByte)tga[12] + ((Int)(UnsignedByte)tga[13] << 8), pixels );
	CHECK_EQ( (Int)(UnsignedByte)tga[14] + ((Int)(UnsignedByte)tga[15] << 8), pixels );

	// A picture of a map, not a flat fill: the ground, the water and the start marks differ.
	Int distinctColours = 0;
	std::set<Int> colours;
	for( Int i = headerBytes; i + 2 < (Int)tga.size(); i += 3 )
	{
		Int packed = ((Int)(UnsignedByte)tga[i] << 16) | ((Int)(UnsignedByte)tga[i + 1] << 8)
			| (Int)(UnsignedByte)tga[i + 2];
		colours.insert( packed );
	}
	distinctColours = (Int)colours.size();

	CHECK( distinctColours > 20 );

	// Same settings, same picture.
	std::vector<char> again;
	RandomMapGenerator::generatePreview( settings, again );
	CHECK_EQ( (Int)again.size(), (Int)tga.size() );
	CHECK_MEM( &again[0], &tga[0], (Int)tga.size() );
}

//-------------------------------------------------------------------------------------------------
/** Four texture classes are only worth writing if the map uses more than one of them.  Rock wants
	the steep ground, sand wants the lake shores, and the map should still be mostly walkable
	ground rather than a quarry. */
//-------------------------------------------------------------------------------------------------
TEST(the_ground_is_textured_by_what_the_ground_is_doing)
{
	CHECK( bootOnce() );

	RandomMapSettings settings;
	settings.m_seed = 5150;
	settings.m_playableCells = 128;
	settings.m_numPlayers = 4;

	std::vector<char> bytes;
	RandomMapGenerator::generate( settings, bytes );
	parseGeneratedMap( bytes );

	Int cellsPerClass[4] = { 0, 0, 0, 0 };
	for( Int i = 0; i < (Int)theRMGParse.m_tiles.size(); i++ )
	{
		Int source = theRMGParse.m_tiles[i] >> 2;
		Int terrainClass = source / 4;
		CHECK( terrainClass >= 0 && terrainClass < 4 );
		cellsPerClass[terrainClass]++;
	}

	Int total = (Int)theRMGParse.m_tiles.size();
	for( Int i = 0; i < 4; i++ )
		CHECK( cellsPerClass[i] > 0 );

	/* Which class covers the most ground is the seed's business - a map whose terraces mostly sit
		high is a dirt map and one that sits low is a sand map - but no single texture may cover
		the whole thing, and the two the fighting happens on have to carry most of it. Rock is the
		cliff faces, so a map that is mostly rock is a map nobody can drive across. */
	for( Int i = 0; i < 4; i++ )
		CHECK( cellsPerClass[i] < (total * 3) / 4 );

	CHECK( cellsPerClass[0] + cellsPerClass[2] > total / 2 );
	CHECK( cellsPerClass[3] < total / 5 );
}

//////////////////////////////////////////////////////////////////////////////
// Headless runs
//////////////////////////////////////////////////////////////////////////////

extern const char *GameEngine_headlessRunResult( UnsignedInt frame, UnsignedInt victoryEndFrame, Int maxGameFrames );

TEST(headless_run_ends_on_a_decision_or_on_the_frame_limit)
{
	// nothing decided and no limit asked for: the run keeps going, however long that is
	CHECK( GameEngine_headlessRunResult( 5000, 0, 0 ) == NULL );

	// the first second is the trap.  A map that has not finished placing its objects has every
	// player owning nothing, which reads as eliminated, and VictoryConditions stores that as an end
	// frame inside the settle window - an unattended run must not take it for a result.
	CHECK( GameEngine_headlessRunResult( 40, 1, 0 ) == NULL );
	CHECK( GameEngine_headlessRunResult( 40, 30, 0 ) == NULL );
	CHECK_STR( GameEngine_headlessRunResult( 40, 31, 0 ), "decided" );

	// -maxframes is the floor under a stalemate, and the comparison is >= so a limit that was
	// already passed still ends the run rather than never matching
	CHECK( GameEngine_headlessRunResult( 2999, 0, 3000 ) == NULL );
	CHECK_STR( GameEngine_headlessRunResult( 3000, 0, 3000 ), "frame limit reached" );
	CHECK_STR( GameEngine_headlessRunResult( 4000, 0, 3000 ), "frame limit reached" );

	// a real result outranks the limit on the frame they land together
	CHECK_STR( GameEngine_headlessRunResult( 3000, 2000, 3000 ), "decided" );
}

TEST(a_replay_game_is_never_itself_recorded)
{
	// what a replay is worth writing: a game a human played in real time
	CHECK( isRecordableGameMode( GAME_LAN ) );
	CHECK( isRecordableGameMode( GAME_SKIRMISH ) );
	CHECK( isRecordableGameMode( GAME_INTERNET ) );

	// EA's three exclusions, kept
	CHECK( !isRecordableGameMode( GAME_SHELL ) );
	CHECK( !isRecordableGameMode( GAME_SINGLE_PLAYER ) );
	CHECK( !isRecordableGameMode( GAME_NONE ) );

	/* the one the blacklist let through.  RecorderClass::update() runs updateRecord() when the mode
	   is NONE as well as RECORD, so a replay game started while the recorder sat in NONE used to
	   start a recording of the replay - over the file being played back. */
	CHECK( !isRecordableGameMode( GAME_REPLAY ) );

	// and a mode nobody has invented yet has to opt in rather than default to being recorded
	CHECK( !isRecordableGameMode( GAME_NONE + 1 ) );
	CHECK( !isRecordableGameMode( -1 ) );
}

TEST(an_option_read_before_the_parser_exists_is_read_on_word_boundaries)
{
	/* Two options are read straight off the wide command line, before CommandLine.cpp's parser is
	   alive: -logPrefix (the debug log's name is chosen by a static constructor) and -multiInstance
	   (the one-copy mutex is taken in WinMain).  The reader is the whole of that parser. */
	const wchar_t *line = L"\"D:\Run\generals.exe\" -win -multiInstance -logPrefix b_ -maxframes 600";

	CHECK( findCommandLineOptionIn( line, L"-win" ) != NULL );
	CHECK( findCommandLineOptionIn( line, L"-multiInstance" ) != NULL );
	CHECK( findCommandLineOptionIn( line, L"-headless" ) == NULL );
	CHECK( findCommandLineOptionIn( NULL, L"-win" ) == NULL );

	// case does not matter, and an option is only an option on a word boundary
	CHECK( findCommandLineOptionIn( line, L"-LOGPREFIX" ) != NULL );
	CHECK( findCommandLineOptionIn( line, L"-log" ) == NULL );			// not a prefix of a longer word
	CHECK( findCommandLineOptionIn( L"x-win", L"-win" ) == NULL );	// nor a tail of one

	char value[ 32 ];
	CHECK( findCommandLineValueIn( line, L"-logPrefix", value, sizeof( value ) ) );
	CHECK_STR( value, "b_" );
	CHECK( findCommandLineValueIn( line, L"-maxframes", value, sizeof( value ) ) );
	CHECK_STR( value, "600" );

	// an option with nothing after it, or with another option after it, has no value
	CHECK( !findCommandLineValueIn( line, L"-multiInstance", value, sizeof( value ) ) );
	CHECK( !findCommandLineValueIn( L"-logPrefix", L"-logPrefix", value, sizeof( value ) ) );
	CHECK( !findCommandLineValueIn( line, L"-nothere", value, sizeof( value ) ) );

	// and a value longer than the buffer is cut to fit, with room kept for the terminator
	char tiny[ 3 ];	// not "small": rpcndr.h has that as a #define for char
	CHECK( findCommandLineValueIn( L"-logPrefix abcdef", L"-logPrefix", tiny, sizeof( tiny ) ) );
	CHECK_STR( tiny, "ab" );
	CHECK( !findCommandLineValueIn( L"-logPrefix abcdef", L"-logPrefix", tiny, 0 ) );
}

TEST(the_slow_frame_bar_moves_only_for_a_positive_number)
{
	/* -slowframe lowers the bar a logic frame has to clear before it writes its own breakdown,
	   which is how a subsystem's cost gets hunted rather than a stutter.  A value that is not a
	   positive number leaves the default alone rather than silencing the log. */
	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;

	CHECK_NEAR( TheGlobalData->m_slowFrameMS, 20.0f, 0.001f );
	char exe[] = "generals.exe";
	char slowFrame[] = "-slowframe";
	char three[] = "3";
	char *argvSlow[] = { exe, slowFrame, three };
	parseCommandLine( 3, argvSlow );
	CHECK_NEAR( TheGlobalData->m_slowFrameMS, 3.0f, 0.001f );

	char nonsense[] = "later";
	char *argvBad[] = { exe, slowFrame, nonsense };
	parseCommandLine( 3, argvBad );
	CHECK_NEAR( TheGlobalData->m_slowFrameMS, 3.0f, 0.001f );

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

TEST(a_netgame_slot_list_is_the_player_order_on_every_machine)
{
	/* -netgame carries what the LAN lobby otherwise agrees on: who plays, at which address, in
	   which order.  Every machine is handed the same list, and ConnectionManager::parseUserList
	   finds the local player in it by address, so the order has to survive the read exactly. */
	UnsignedInt ips[ MAX_SLOTS ];

	CHECK_EQ( 2, ResolveHostList( "127.0.0.1,127.0.0.2", ips, MAX_SLOTS ) );
	CHECK_EQ( 0x7f000001, ips[0] );	// host order, the order LANGameSlot::setIP wants
	CHECK_EQ( 0x7f000002, ips[1] );

	CHECK_EQ( 3, ResolveHostList( "10.0.0.3,10.0.0.1,10.0.0.2", ips, MAX_SLOTS ) );
	CHECK_EQ( 0x0a000003, ips[0] );	// given order, not sorted
	CHECK_EQ( 0x0a000001, ips[1] );
	CHECK_EQ( 0x0a000002, ips[2] );

	// an empty list is no game, not a one player one
	CHECK_EQ( 0, ResolveHostList( "", ips, MAX_SLOTS ) );

	// more addresses than there are slots is refused rather than quietly cut short
	CHECK_EQ( -1, ResolveHostList( "10.0.0.1,10.0.0.2,10.0.0.3", ips, 2 ) );

	// and so is anything that is not an address, however ResolveIP says so
	CHECK_EQ( -1, ResolveHostList( "10.0.0.1,999.1.1.1", ips, MAX_SLOTS ) );	// INADDR_NONE

	// a stray separator is not a slot - the list is whatever stands between the commas
	CHECK_EQ( 2, ResolveHostList( "10.0.0.1,,10.0.0.2", ips, MAX_SLOTS ) );
}

TEST(two_copies_on_one_machine_each_get_their_own_lobby_address_and_name)
{
	/* The LAN lobby binds one UDP port with no SO_REUSEADDR, and both copies read the same
	   Options.ini and the same LAN preferences - so the address and the name have to come from
	   somewhere the two copies can disagree about, which is the command line. */
	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;

	CHECK_EQ( 0u, TheGlobalData->m_defaultIP );	// nothing chosen: the lobby picks an address itself
	CHECK( TheGlobalData->m_lanPlayerName.isEmpty() );

	char exe[] = "generals.exe";
	char lanIP[] = "-lanip";
	char second[] = "127.0.0.2";
	char *argvIP[] = { exe, lanIP, second };
	parseCommandLine( 3, argvIP );
	CHECK_EQ( 0x7f000002, TheGlobalData->m_defaultIP );	// host order, the order SetLocalIP wants

	// a malformed address leaves the last usable one alone rather than binding INADDR_NONE
	char nonsense[] = "999.1.1.1";
	char *argvBadIP[] = { exe, lanIP, nonsense };
	parseCommandLine( 3, argvBadIP );
	CHECK_EQ( 0x7f000002, TheGlobalData->m_defaultIP );

	char lanName[] = "-lanname";
	char who[] = "Player 2";
	char *argvName[] = { exe, lanName, who };
	parseCommandLine( 3, argvName );
	CHECK_STR( TheGlobalData->m_lanPlayerName.str(), "Player 2" );

	// -lanlobby opens the LAN screen, and turns the shell map off because the main menu it stacks
	// on top of is only pushed when there is no shell map
	CHECK( !TheGlobalData->m_lanLobbyOnStart );
	CHECK( TheGlobalData->m_shellMapOn );
	char lanLobby[] = "-lanlobby";
	char *argvLobby[] = { exe, lanLobby };
	parseCommandLine( 2, argvLobby );
	CHECK( TheGlobalData->m_lanLobbyOnStart );
	CHECK( !TheGlobalData->m_shellMapOn );

	// -skirmishlobby is the same trick for the staging room the lobby settings live on
	TheWritableGlobalData->m_shellMapOn = TRUE;
	CHECK( !TheGlobalData->m_skirmishLobbyOnStart );
	char skirmishLobby[] = "-skirmishlobby";
	char *argvSkirmishLobby[] = { exe, skirmishLobby };
	parseCommandLine( 2, argvSkirmishLobby );
	CHECK( TheGlobalData->m_skirmishLobbyOnStart );
	CHECK( !TheGlobalData->m_shellMapOn );

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

TEST(the_serial_check_is_waived_only_between_two_addresses_on_this_machine)
{
	/* Two copies of the game on one machine are one installation and therefore one CD key, so the
	   host's duplicate-serial check would refuse the join every time.  It is waived for a pair of
	   loopback addresses and for nothing else - anything that could be a second machine still
	   answers to the retail rule. */
	CHECK( IsLoopbackIP( 0x7f000001 ) );
	CHECK( IsLoopbackIP( 0x7f000002 ) );
	CHECK( IsLoopbackIP( 0x7f000000 ) );	// the whole of 127.0.0.0/8 routes to loopback
	CHECK( IsLoopbackIP( 0x7fffffff ) );

	CHECK( !IsLoopbackIP( 0x7e000001 ) );	// 126.x is somebody else
	CHECK( !IsLoopbackIP( 0x80000001 ) );	// and so is 128.x
	CHECK( !IsLoopbackIP( 0x0a000001 ) );	// the private ranges a real LAN game runs on
	CHECK( !IsLoopbackIP( 0xac13287e ) );
	CHECK( !IsLoopbackIP( 0 ) );					// INADDR_ANY is not an address
}

/* A replay is checked by comparing the CRCs it carries against the ones playback recomputes, one
	 for one, out of a queue.  A game played over a network never gets its frame 0 CRC into the file
	 - the logic makes it after that frame's commands have already gone out, so it is never sent,
	 never executed and never recorded - and playback, having no network to lose it to, makes one
	 anyway.  Unless playback throws that one away every comparison after it is a frame out, and a
	 replay that is perfectly in sync reports a desync on its first interval frame. */
TEST(replay_crc_queue_drops_the_frame_the_network_never_recorded)
{
	// only a game that was played over a network is missing that first CRC
	CHECK( replayIsMissingFirstCRC( GAME_LAN ) );
	CHECK( replayIsMissingFirstCRC( GAME_INTERNET ) );
	CHECK( !replayIsMissingFirstCRC( GAME_SKIRMISH ) );
	CHECK( !replayIsMissingFirstCRC( GAME_SINGLE_PLAYER ) );
	CHECK( !replayIsMissingFirstCRC( GAME_REPLAY ) );
	CHECK( !replayIsMissingFirstCRC( GAME_SHELL ) );
	CHECK( !replayIsMissingFirstCRC( GAME_NONE ) );

	// left alone the queue hands back what it was given, in order
	CRCInfo solo;
	solo.addCRC( 0x11111111 );
	solo.addCRC( 0x22222222 );
	solo.addCRC( 0x33333333 );
	CHECK_EQ( 0x11111111, solo.readCRC() );
	CHECK_EQ( 0x22222222, solo.readCRC() );
	CHECK_EQ( 0x33333333, solo.readCRC() );
	CHECK_EQ( 0, solo.readCRC() );		// an empty queue reads as 0

	// armed, it swallows exactly one - the frame the recording is missing - and no more
	CRCInfo net;
	net.skipFirstCRC();
	net.addCRC( 0x11111111 );
	net.addCRC( 0x22222222 );
	net.addCRC( 0x33333333 );
	CHECK_EQ( 0x22222222, net.readCRC() );
	CHECK_EQ( 0x33333333, net.readCRC() );
	CHECK_EQ( 0, net.readCRC() );
}


/** The build queue strip has nine buttons, and that number had become the production cap: every
	 build button greyed out at nine units even though the queue itself is a hundred deep.  The two
	 numbers are unrelated - one is how many entries fit on screen, the other is how many the factory
	 will take (INI MaxQueueEntries). */
TEST(a_factorys_queue_is_as_deep_as_its_ini_says_not_as_wide_as_the_button_strip)
{
	ProductionUpdateModuleData data;

	CHECK_EQ( 100, data.m_maxQueueEntries );
	CHECK( data.m_maxQueueEntries > MAX_BUILD_QUEUE_BUTTONS );
}

/** The bar over a worker's head while it loads or unloads a box fills across the window the dock
	 state announced.  Outside that window there is no bar at all - which is how a worker walking
	 between the pile and the refinery reads, since nothing but the dock state ever moves the
	 window - and the ends of the window are exclusive so a stale one cannot leave a full bar
	 hanging over an idle worker. */
TEST(a_dock_action_bar_fills_across_its_window_and_shows_nothing_outside_it)
{
	// half way through a thirty frame box
	CHECK_NEAR( 0.5f, dockActionProgress( 115, 100, 130 ), 0.0001f );
	CHECK_NEAR( 0.0f, dockActionProgress( 100, 100, 130 ), 0.0001f );

	// the frame it lands on, and every frame after, is over: no bar
	CHECK( dockActionProgress( 130, 100, 130 ) < 0.0f );
	CHECK( dockActionProgress( 400, 100, 130 ) < 0.0f );

	// before it starts, and a window that was never set, are both no bar
	CHECK( dockActionProgress( 99, 100, 130 ) < 0.0f );
	CHECK( dockActionProgress( 0, 0, 0 ) < 0.0f );

	// a zero-length window is not a divide by zero
	CHECK( dockActionProgress( 100, 100, 100 ) < 0.0f );
}

/** A docking ends on the frame the last box changes hands, not one action delay later.  Both ends
	 can finish it: the docker filling up, or the pile running out.  Staying docked past that point
	 cost a worker a whole extra delay per trip standing at a pile it could take nothing from. */
TEST(a_supply_docking_ends_on_the_box_that_finishes_it)
{
	// mid-load: pile has plenty, worker has room
	CHECK( supplyDockHasNextBox( 40, 3, 8 ) );

	// the box that fills the worker is the last one
	CHECK( !supplyDockHasNextBox( 40, 8, 8 ) );
	CHECK( !supplyDockHasNextBox( 40, 9, 8 ) );		// over-full is still full

	// so is the box that empties the pile, even for a worker with room left
	CHECK( !supplyDockHasNextBox( 0, 3, 8 ) );

	// and both at once
	CHECK( !supplyDockHasNextBox( 0, 8, 8 ) );

	// one box left and room for it: worth staying for
	CHECK( supplyDockHasNextBox( 1, 7, 8 ) );
}

/** The whole load changes hands in one action, and that action is priced by the load: the INI's
	 SupplyWarehouseActionDelay buys one box, so a full load of eight waits for eight of them.  Same
	 total time as taking them one at a time, one trip instead of one per box. */
TEST(a_supply_visit_costs_what_the_load_costs)
{
	// empty worker, full pile: the whole load
	CHECK_EQ( 8u * 30u, supplyWarehouseActionDelay( 30, 40, 0, 8 ) );

	// half loaded already: only the room left is paid for
	CHECK_EQ( 5u * 30u, supplyWarehouseActionDelay( 30, 40, 3, 8 ) );

	// a pile with less in it than the worker can carry prices what is actually there
	CHECK_EQ( 2u * 30u, supplyWarehouseActionDelay( 30, 2, 0, 8 ) );

	// nothing to take: one delay, and the worker finds that out when it gets there
	CHECK_EQ( 30u, supplyWarehouseActionDelay( 30, 0, 0, 8 ) );
	CHECK_EQ( 30u, supplyWarehouseActionDelay( 30, 40, 8, 8 ) );
	CHECK_EQ( 30u, supplyWarehouseActionDelay( 30, 40, 9, 8 ) );		// over-full does not go negative

	// one box, one delay - which is what every trip used to cost per box
	CHECK_EQ( 30u, supplyWarehouseActionDelay( 30, 1, 7, 8 ) );
}


/** EA's floor and ceil nudged the value by the largest float below one and truncated.  The nudge
	 does not survive the addition: for anything from 2 upwards, f + 0.99999994 rounds to f + 1, so
	 the "ceil" of a whole number came back one too high - and every countdown drawn over a building
	 is a whole number of seconds, which is why a ten second build said eleven. */
TEST(ceil_and_floor_are_exact_on_whole_numbers)
{
	// the case that was wrong: a whole number is already its own ceiling.  This used to be pinned
	// the other way round, as realtointceil_DEFECT_overshoots_every_whole_number - the screen
	// heights are the values that first turned it up, the seconds over a building are what
	// finally made it worth the compare.
	CHECK_EQ( 10, REAL_TO_INT_CEIL( 10.0f ) );
	CHECK_EQ( 2, REAL_TO_INT_CEIL( 2.0f ) );
	CHECK_EQ( 1, REAL_TO_INT_CEIL( 1.0f ) );
	CHECK_EQ( 0, REAL_TO_INT_CEIL( 0.0f ) );
	CHECK_EQ( 600, REAL_TO_INT_CEIL( 600.0f ) );
	CHECK_EQ( 768, REAL_TO_INT_CEIL( 768.0f ) );
	CHECK_EQ( 1080, REAL_TO_INT_CEIL( 1080.0f ) );
	CHECK_EQ( 1920, REAL_TO_INT_CEIL( 1920.0f ) );
	CHECK_EQ( -3, REAL_TO_INT_CEIL( -3.0f ) );
	CHECK_EQ( -600, REAL_TO_INT_CEIL( -600.0f ) );

	// ... and a fraction still rounds away from zero the way ceil must
	CHECK_EQ( 11, REAL_TO_INT_CEIL( 10.0001f ) );
	CHECK_EQ( 11, REAL_TO_INT_CEIL( 10.9f ) );
	CHECK_EQ( 1, REAL_TO_INT_CEIL( 0.001f ) );
	CHECK_EQ( 601, REAL_TO_INT_CEIL( 600.5f ) );
	CHECK_EQ( 1081, REAL_TO_INT_CEIL( 1080.25f ) );
	CHECK_EQ( -10, REAL_TO_INT_CEIL( -10.9f ) );
	CHECK_EQ( -600, REAL_TO_INT_CEIL( -600.5f ) );

	// floor had the same flaw on the negative side
	CHECK_EQ( -3, REAL_TO_INT_FLOOR( -3.0f ) );
	CHECK_EQ( 10, REAL_TO_INT_FLOOR( 10.0f ) );
	CHECK_EQ( 10, REAL_TO_INT_FLOOR( 10.9f ) );
	CHECK_EQ( -4, REAL_TO_INT_FLOOR( -3.1f ) );
	CHECK_EQ( 0, REAL_TO_INT_FLOOR( 0.9f ) );
	CHECK_EQ( -1, REAL_TO_INT_FLOOR( -0.1f ) );
}


/** The difficulty ladder is a table, not thirty scattered switch statements, so the one thing worth
	 asserting about it is that it is actually a ladder: every rung looks harder than the one below,
	 and no rung is a cheat.  A fat-fingered row in the table is otherwise invisible until somebody
	 plays a hundred matches.

	 TheAI does not exist in this binary, so this reads the shipped defaults the way TAiData's
	 constructor does. */
TEST(the_difficulty_ladder_climbs_in_every_direction_it_should)
{
	TAiData data;

	for( Int i = 1; i < AISKILL_COUNT; ++i )
	{
		const AIDifficultyProfile &lower = data.m_skill[ i - 1 ];
		const AIDifficultyProfile &upper = data.m_skill[ i ];

		// perception: looks more often, acts sooner, thinks more often - never sees more
		CHECK( upper.m_scoutIntervalSeconds <= lower.m_scoutIntervalSeconds );
		CHECK( upper.m_reactionDelaySeconds <= lower.m_reactionDelaySeconds );
		CHECK( upper.m_decisionIntervalSeconds <= lower.m_decisionIntervalSeconds );
		CHECK( upper.m_maxScouts >= lower.m_maxScouts );

		// decisions: counters harder, holds on to its units longer
		CHECK( upper.m_counterCompositionWeight >= lower.m_counterCompositionWeight );
		CHECK( upper.m_retreatTtkRatio >= lower.m_retreatTtkRatio );

		// capabilities only ever switch on as you climb, never off again
		CHECK( upper.m_massBeforeAttacking >= lower.m_massBeforeAttacking );
		CHECK( upper.m_retreatIndividualUnits >= lower.m_retreatIndividualUnits );
		CHECK( upper.m_retreatTeams >= lower.m_retreatTeams );
		CHECK( upper.m_useInfluenceMapForAttackLane >= lower.m_useInfluenceMapForAttackLane );
		CHECK( upper.m_economyBuildings >= lower.m_economyBuildings );
		CHECK( upper.m_focusFire >= lower.m_focusFire );
		CHECK( upper.m_savesSciencePoints >= lower.m_savesSciencePoints );
		CHECK( upper.m_adaptiveHarvesters >= lower.m_adaptiveHarvesters );
		CHECK( upper.m_selfTriggeredExpansion >= lower.m_selfTriggeredExpansion );
		CHECK( upper.m_defendExpansions >= lower.m_defendExpansions );
	}

	// the ends of the ladder are what they say they are: Easy ignores what it is facing and Brutal
	// is the baseline, which means it counters fully and answers the moment it sees something
	CHECK_EQ( 0.0f, data.m_skill[ AISKILL_EASY ].m_counterCompositionWeight );
	CHECK_EQ( 1.0f, data.m_skill[ AISKILL_BRUTAL ].m_counterCompositionWeight );
	CHECK_EQ( 0.0f, data.m_skill[ AISKILL_BRUTAL ].m_reactionDelaySeconds );

	// every rung scouts. An AI that never looks reads as broken, not as easy.
	for( Int i = 0; i < AISKILL_COUNT; ++i )
	{
		CHECK( data.m_skill[ i ].m_scoutIntervalSeconds > 0.0f );
		CHECK( data.m_skill[ i ].m_maxScouts >= 1 );
	}
}


/** B1's arithmetic, the half of "build what counters what he is fielding" that has no engine in it.
	 The bug it replaces was not a bad score, it was no score at all: EA picked at random among the
	 teams sharing the highest static priority, so an AI facing nothing but aircraft went on building
	 tanks. */
TEST(counter_score_answers_what_the_enemy_actually_fields)
{
	// the matchups are the score: a team whose units beat the visible army money for money is the
	// answer, and one whose units lose to it is not
	AIEnemyComposition ground;
	ground.m_armour = 1.0f;
	AITeamCapability antiTank;  antiTank.m_answer = 0.9f;
	AITeamCapability rifles;    rifles.m_answer = 0.2f;
	CHECK_NEAR( 0.9f, aiCounterScore( ground, antiTank ), 0.0001f );
	CHECK( aiCounterScore( ground, antiTank ) > aiCounterScore( ground, rifles ) );

	// stealth is the case A1 created: the AI stopped shooting through fog, so a detector earns its
	// place the moment the enemy fields anything that can hide
	AIEnemyComposition halfStealth;
	halfStealth.m_armour = 1.0f;
	halfStealth.m_stealth = 0.5f;
	AITeamCapability detector = rifles;
	detector.m_detectsStealth = TRUE;
	CHECK( aiCounterScore( halfStealth, detector ) > aiCounterScore( halfStealth, rifles ) );

	// seen nothing yet -> no opinion, whatever the team is. The static priority then decides,
	// which is EA's behaviour and the right fallback.
	AIEnemyComposition unknown;
	AITeamCapability nothingSeen;
	CHECK_NEAR( 0.0f, aiCounterScore( unknown, nothingSeen ), 0.0001f );

	// the score is a share, so it never leaves 0..1 however many terms a team answers
	AITeamCapability everything;
	everything.m_answer = 1.0f;
	everything.m_detectsStealth = TRUE;
	CHECK_NEAR( 1.0f, aiCounterScore( halfStealth, everything ), 0.0001f );
}


/** The shot arithmetic under every matchup, checked against numbers read straight out of Weapon.ini
	 and the Object INI rather than numbers made up to fit. */
TEST(frames_to_kill_counts_shots_delays_and_reloads)
{
	// Crusader on Crusader: 480 health, CrusaderTankGun does 60 a shell every 2000 ms (60 frames),
	// no clip. Eight shells, each paying one delay.
	AIShotPattern crusaderGun;
	crusaderGun.m_damagePerShot = 60.0f;
	crusaderGun.m_delayFrames = 60.0f;
	CHECK_NEAR( 480.0f, aiFramesToKill( 480.0f, crusaderGun ), 0.001f );

	// the same shell through HumanArmor's 10% on a 120-health Rebel: twenty shells, not two
	AIShotPattern crusaderOnInfantry = crusaderGun;
	crusaderOnInfantry.m_damagePerShot = 6.0f;
	CHECK_NEAR( 1200.0f, aiFramesToKill( 120.0f, crusaderOnInfantry ), 0.001f );

	// a clip of two with a long reload: the third shot waits for the reload, not the delay
	AIShotPattern clip;
	clip.m_damagePerShot = 50.0f;
	clip.m_delayFrames = 10.0f;
	clip.m_clipSize = 2;
	clip.m_reloadFrames = 100.0f;
	CHECK_NEAR( 120.0f, aiFramesToKill( 150.0f, clip ), 0.001f );

	// a sniper's aim is paid once, before the first shot
	clip.m_openingFrames = 15.0f;
	CHECK_NEAR( 135.0f, aiFramesToKill( 150.0f, clip ), 0.001f );

	// overkill is one shot, and it still pays its delay: two one-shot kills tie instead of both
	// reading as instant
	CHECK_NEAR( 60.0f, aiFramesToKill( 10.0f, crusaderGun ), 0.001f );

	// a weapon the armour ignores never kills
	AIShotPattern harmless;
	harmless.m_delayFrames = 30.0f;
	CHECK_EQ( AI_CANNOT_KILL, aiFramesToKill( 480.0f, harmless ) );
}


/** One pairing as a score: money for money, log scale, and the two directions of it sum to one. */
TEST(matchup_score_is_money_for_money)
{
	// an even fight at an even price is an even trade
	CHECK_NEAR( 0.5f, aiMatchupScore( 480.0f, 480.0f, 900.0f, 900.0f ), 0.0001f );

	// twice as fast for the same money is better, and the other side of it is worse by the same amount
	const Real faster = aiMatchupScore( 240.0f, 480.0f, 900.0f, 900.0f );
	const Real slower = aiMatchupScore( 480.0f, 240.0f, 900.0f, 900.0f );
	CHECK( faster > 0.5f );
	CHECK_NEAR( 1.0f, faster + slower, 0.0001f );

	// sixteen times better is as good as it gets
	CHECK_NEAR( 1.0f, aiMatchupScore( 30.0f, 480.0f, 900.0f, 900.0f ), 0.0001f );
	CHECK_NEAR( 1.0f, aiMatchupScore( 1.0f, 480.0f, 900.0f, 900.0f ), 0.0001f );

	// an Overlord that kills twice as fast for twice the price is no bargain
	CHECK_NEAR( 0.5f, aiMatchupScore( 240.0f, 480.0f, 1800.0f, 900.0f ), 0.0001f );

	// a unit that cannot hurt the target answers none of it; one the target cannot hurt answers all
	CHECK_NEAR( 0.0f, aiMatchupScore( AI_CANNOT_KILL, 480.0f, 900.0f, 900.0f ), 0.0001f );
	CHECK_NEAR( 1.0f, aiMatchupScore( 480.0f, AI_CANNOT_KILL, 900.0f, 900.0f ), 0.0001f );
	CHECK_NEAR( 0.0f, aiMatchupScore( AI_CANNOT_KILL, AI_CANNOT_KILL, 900.0f, 900.0f ), 0.0001f );

	// a free unit has no price to weigh, so only the kill times count
	CHECK_NEAR( faster, aiMatchupScore( 240.0f, 480.0f, 0.0f, 900.0f ), 0.0001f );
}


/** C1's arithmetic: the ratio of how long a force lasts to how long it needs to finish what is
	 shooting at it.  The word "retreat" did not appear anywhere in the AI before this - teams fought
	 to the last man - and the metric is deliberately not a health percentage: Sins of a Solar
	 Empire's aiRetreatThreshold is a time-to-kill ratio for the reason the third case below shows. */
TEST(retreat_ratio_measures_the_exchange_not_the_health_bar)
{
	// even fight: both sides need the same time to finish the other
	CHECK_NEAR( 1.0f, aiRetreatRatio( 100.0f, 10.0f, 100.0f, 10.0f ), 0.0001f );

	// twice their damage for the same health: they die in half the time I do
	CHECK( aiRetreatRatio( 100.0f, 20.0f, 100.0f, 10.0f ) > 1.0f );
	CHECK( aiRetreatRatio( 100.0f, 5.0f, 100.0f, 10.0f ) < 1.0f );

	// the case a health percentage gets wrong, and the reason for the metric: a unit at a fifth of
	// its health that still out-damages what is shooting it is winning and should stay
	CHECK( aiRetreatRatio( 20.0f, 100.0f, 500.0f, 1.0f ) > 1.0f );
	// ... and a full-health one being melted is losing and should not
	CHECK( aiRetreatRatio( 1000.0f, 1.0f, 10.0f, 500.0f ) < 1.0f );

	// nothing shooting at me is not a fight, at any health
	CHECK( aiRetreatRatio( 1.0f, 1.0f, 0.0f, 0.0f ) > 100.0f );
	CHECK( aiRetreatRatio( 1.0f, 1.0f, 100.0f, 0.0f ) > 100.0f );

	// nothing I can do about it: leave, whatever my health
	CHECK_NEAR( 0.0f, aiRetreatRatio( 10000.0f, 0.0f, 10.0f, 10.0f ), 0.0001f );

	// already dead is not a fight to stay in either
	CHECK_NEAR( 0.0f, aiRetreatRatio( 0.0f, 10.0f, 10.0f, 10.0f ), 0.0001f );

	//
	// The ladder's thresholds mean what they say, and where they sit was measured rather than
	// guessed: at the roadmap's 0.8 a force quit a fight it was very nearly winning, and the top
	// rung lost 4-7 to Easy - which does not know how to retreat and simply kept shooting.  They are
	// "clearly losing" numbers now.  A force that lasts a third as long as the one it faces breaks
	// off at every rung that retreats at all; one at parity never does.
	//
	TAiData data;
	const Real brutal = data.m_skill[ AISKILL_BRUTAL ].m_retreatTtkRatio;
	CHECK( aiRetreatRatio( 30.0f, 10.0f, 100.0f, 10.0f ) < brutal );
	CHECK( aiRetreatRatio( 100.0f, 10.0f, 100.0f, 10.0f ) >= brutal );
	CHECK( aiRetreatRatio( 75.0f, 10.0f, 100.0f, 10.0f ) >= brutal );		// three quarters is not lost
	CHECK( brutal < 0.6f );		// anything above this is quitting fights it can win

	// the bottom rung has no threshold at all: it never quits, which is what makes it Easy
	CHECK_EQ( 0.0f, data.m_skill[ AISKILL_EASY ].m_retreatTtkRatio );
}


/** AIPlayer.cpp: who the retreat is allowed to order home.  Aircraft are not: a move order is what
	 starts a jet's or a helicopter's own round trip, so one handed to a parked aircraft every
	 decision interval took it off the deck, flew it at the base centre, idled it and landed it
	 again, over and over, for as long as a fight near the base read as lost.  JetAIUpdate already
	 owns every reason an aircraft goes home. */
extern Bool AIRetreat_canBeOrderedHome( Bool hasAI, Bool isStructureOrImmobile, Bool ownsItsOwnLanding );

TEST(the_retreat_never_orders_an_aircraft_home)
{
	// a tank in a fight it is losing: this is what the retreat is for
	CHECK( AIRetreat_canBeOrderedHome( true, false, false ) );

	// a jet or a comanche - anything running JetAIUpdate - is left alone, in the air or parked
	CHECK( !AIRetreat_canBeOrderedHome( true, false, true ) );

	// a building cannot fall back, and neither can anything without an AI to give the order to
	CHECK( !AIRetreat_canBeOrderedHome( true, true, false ) );
	CHECK( !AIRetreat_canBeOrderedHome( false, false, false ) );
	CHECK( !AIRetreat_canBeOrderedHome( false, true, true ) );
}


/** The second half of scouting: once every enemy has been placed there is nothing left to search for,
	 and the job becomes keeping the picture of their bases current.  That is the stalest one per step
	 walked, recomputed at every arrival - which is what stopped the scout walking a blind lap of the
	 player list past bases the other scout had just refreshed. */
TEST(scouting_goes_where_the_picture_is_stalest_per_step)
{
	const UnsignedInt NOW = 30000;			// a bit over sixteen minutes in
	const UnsignedInt FRESH = 60 * 30;	// a rung's scouting interval, in frames

	// a place nobody has been to carries the largest age there is, so at equal walks it goes first
	CHECK( aiScoutScore( NOW, 0, 1000.0f, FRESH ) > aiScoutScore( NOW, 1, 1000.0f, FRESH ) );
	CHECK( aiScoutScore( NOW, 0, 1000.0f, FRESH ) > aiScoutScore( NOW, NOW - FRESH, 1000.0f, FRESH ) );

	// ... and among places nobody has been to, the near one goes first
	CHECK( aiScoutScore( NOW, 0, 100.0f, FRESH ) > aiScoutScore( NOW, 0, 4000.0f, FRESH ) );

	// distance is a divisor, not a veto: a picture twenty times as old is worth twice the walk
	CHECK( aiScoutScore( NOW, NOW - 20000, 1000.0f, FRESH ) > aiScoutScore( NOW, NOW - 5000, 500.0f, FRESH ) );

	// and the trade goes the other way when the far one is only slightly staler - a scout that
	// crosses the map for a marginally older picture is a scout that is never anywhere useful
	CHECK( aiScoutScore( NOW, NOW - 4000, 4000.0f, FRESH ) < aiScoutScore( NOW, NOW - 3800, 100.0f, FRESH ) );

	//
	// The trip this used to make every lap for nothing: the round robin walked to the next start
	// position whether or not it had just been there, at 100% certainty about where it was.  A
	// picture younger than the rung's interval now scores zero, which the caller reads as "stay".
	//
	CHECK_EQ( 0.0f, aiScoutScore( NOW, NOW - 10, 100.0f, FRESH ) );
	CHECK_EQ( 0.0f, aiScoutScore( NOW, NOW, 100.0f, FRESH ) );
	CHECK( aiScoutScore( NOW, NOW - FRESH, 100.0f, FRESH ) > 0.0f );		// exactly due is due

	// no rung is allowed to keep a scout parked for ever: every picture goes stale eventually
	CHECK( aiScoutScore( NOW, 1, 100.0f, FRESH ) > 0.0f );

	// standing on the thing does not divide by zero, and is still the best place to look from
	CHECK( aiScoutScore( NOW, 0, 0.0f, FRESH ) > 0.0f );
	CHECK_EQ( aiScoutScore( NOW, 0, 0.0f, FRESH ), aiScoutScore( NOW, 0, 1.0f, FRESH ) );
}


/** The first half of scouting: finding out which start position each enemy is on, which the AI is no
	 longer told.  Elimination is the whole model - every position known to hold an enemy accounts for
	 one of them, so the rest are the unlocated enemies spread over the positions nobody has looked at,
	 and each empty answer makes the remaining ones likelier.

	 The number that matters is the one at the top: certainty arrives by subtraction, and a walk to
	 confirm it is a walk spent on nothing. */
TEST(elimination_narrows_the_start_positions_until_it_knows)
{
	// 1v3 on an eight-position map: seven positions are not mine, three of them hold an enemy
	CHECK_NEAR( 3.0f / 7.0f, aiStartOccupiedOdds( 3, 7 ), 0.0001f );

	// cross an empty one off and the rest get likelier - which is the whole reason to go and look
	CHECK_NEAR( 3.0f / 6.0f, aiStartOccupiedOdds( 3, 6 ), 0.0001f );
	CHECK_NEAR( 3.0f / 5.0f, aiStartOccupiedOdds( 3, 5 ), 0.0001f );
	CHECK( aiStartOccupiedOdds( 3, 5 ) > aiStartOccupiedOdds( 3, 6 ) );
	CHECK( aiStartOccupiedOdds( 3, 6 ) > aiStartOccupiedOdds( 3, 7 ) );

	// finding one instead accounts for it: two enemies left over the six positions still unchecked
	CHECK_NEAR( 2.0f / 6.0f, aiStartOccupiedOdds( 2, 6 ), 0.0001f );

	// three enemies with three places left to be is not a guess, and not worth a walk
	CHECK_EQ( 1.0f, aiStartOccupiedOdds( 3, 3 ) );
	CHECK_EQ( 1.0f, aiStartOccupiedOdds( 3, 2 ) );		// nothing is more certain than certain

	// a two-player map is that same rule at its first step - one enemy, one position - so it is
	// settled before the scout is built, and the scout goes to see what is there rather than where
	CHECK_EQ( 1.0f, aiStartOccupiedOdds( 1, 1 ) );

	// nobody left to find, or nowhere left to look, is the opposite of certainty - not 1.0
	CHECK_EQ( 0.0f, aiStartOccupiedOdds( 0, 7 ) );
	CHECK_EQ( 0.0f, aiStartOccupiedOdds( 3, 0 ) );
	CHECK_EQ( 0.0f, aiStartOccupiedOdds( -1, 7 ) );

	// and in between it stays a probability
	CHECK( aiStartOccupiedOdds( 1, 8 ) > 0.0f );
	CHECK( aiStartOccupiedOdds( 1, 8 ) < 1.0f );
}


/** An oil derrick pays whoever owns it and costs one infantryman to take, so the computer razing the
	 enemy's was the one thing it could do with a derrick that earned nobody anything.  It captures them
	 now, and stops picking them up as targets on its own initiative - but only the ones that are
	 actually worth owning and cannot shoot back. */
TEST(a_capturable_tech_building_is_income_not_a_target)
{
	// the oil derrick: a tech building, capturable, unarmed
	CHECK( aiIsIncomeNotATarget( TRUE, TRUE, FALSE ) );

	//
	// The two that stay targets.  A tech building that acts as a base defence when captured shoots at
	// us, and something that is never shot back at is something the AI walks past for ever.
	//
	CHECK( !aiIsIncomeNotATarget( TRUE, TRUE, TRUE ) );
	// ... and one nobody can capture is only ever a building in the way
	CHECK( !aiIsIncomeNotATarget( TRUE, FALSE, FALSE ) );

	// everything else is untouched: this rule is about tech buildings and nothing else
	CHECK( !aiIsIncomeNotATarget( FALSE, TRUE, FALSE ) );
	CHECK( !aiIsIncomeNotATarget( FALSE, FALSE, FALSE ) );
}


/** C2: whether a finished wave waits at the rally point or goes now.  EA sent every team the moment
	 it was ready, and a string of small waves is free veterancy for whoever is on the other end.
	 Three things override the arithmetic, and each of them is a way of getting the AI stuck if it is
	 missing. */
TEST(massing_waits_for_a_force_but_never_waits_for_ever)
{
	const Real AGGRESSIVE = 0.8f;		// commits with less
	const Real DEFENSIVE = 1.3f;		// wants more in hand first

	// half of what the enemy has: not enough for either role
	CHECK( aiShouldMass( 500.0f, 1000.0f, AGGRESSIVE, FALSE, FALSE ) );
	CHECK( aiShouldMass( 500.0f, 1000.0f, DEFENSIVE, FALSE, FALSE ) );

	// the role is the difference: at parity the aggressive one goes and the defensive one holds
	CHECK( !aiShouldMass( 1000.0f, 1000.0f, AGGRESSIVE, FALSE, FALSE ) );
	CHECK( aiShouldMass( 1000.0f, 1000.0f, DEFENSIVE, FALSE, FALSE ) );

	// the sixty-second valve wins over everything, so a wave can never be parked for ever
	CHECK( !aiShouldMass( 1.0f, 100000.0f, DEFENSIVE, TRUE, FALSE ) );

	// so does a fight at home: nothing sits at a rally point while the base is being hit
	CHECK( !aiShouldMass( 1.0f, 100000.0f, DEFENSIVE, FALSE, TRUE ) );

	// nothing found to mass against is not a reason to wait - that is how a fogged AI would park
	// its whole army for ever having never scouted
	CHECK( !aiShouldMass( 0.0f, 0.0f, DEFENSIVE, FALSE, FALSE ) );

	// and massing switched off is EA's behaviour: send it
	CHECK( !aiShouldMass( 1.0f, 100000.0f, 0.0f, FALSE, FALSE ) );

	// the ladder turns it on at Brutal, which is the rung the roadmap puts it at
	TAiData data;
	CHECK( !data.m_skill[ AISKILL_EASY ].m_massBeforeAttacking );
	CHECK( !data.m_skill[ AISKILL_MEDIUM ].m_massBeforeAttacking );
	CHECK( data.m_skill[ AISKILL_BRUTAL ].m_massBeforeAttacking );
}


/** B2: which enemy to go after.  EA's answer was the nearest one, plus a rule with the sign the
	 wrong way round - an enemy who had lost his units or his production had his distance set to half
	 the map, which is "ignore the one you are about to beat" and is what drags matches out. */
TEST(enemy_choice_prefers_the_weak_the_rich_and_the_near)
{
	const Real CLOSE_BY = 100.0f * 100.0f;
	const Real FAR_OFF = 300.0f * 300.0f;

	// all else equal, the near one
	CHECK( aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 0.0f ) <
				 aiEnemyCost( FAR_OFF,  FALSE, FALSE, FALSE, 0.0f ) );

	// a crippled enemy is an opportunity, not a distraction: this is the sign EA had backwards
	CHECK( aiEnemyCost( CLOSE_BY, TRUE,  FALSE, FALSE, 0.0f ) <
				 aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 0.0f ) );

	// and it is worth walking past a healthy neighbour to finish him
	CHECK( aiEnemyCost( FAR_OFF, TRUE, FALSE, FALSE, 0.0f ) <
				 aiEnemyCost( FAR_OFF, FALSE, FALSE, FALSE, 0.0f ) );

	// the fatter of two enemies at the same distance is the one that decides the match
	CHECK( aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 1.0f ) <
				 aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 0.0f ) );

	// EA's two flat terms are kept: do not gang up, and gently prefer whoever is already on you
	CHECK( aiEnemyCost( CLOSE_BY, FALSE, TRUE,  FALSE, 0.0f ) >
				 aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 0.0f ) );
	CHECK( aiEnemyCost( CLOSE_BY, FALSE, FALSE, TRUE,  0.0f ) <
				 aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 0.0f ) );

	// the gang-up penalty is the big one: it should outweigh being shot at
	CHECK( aiEnemyCost( CLOSE_BY, FALSE, TRUE, TRUE, 0.0f ) >
				 aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 0.0f ) );

	// never negative, whatever the bonuses add up to
	CHECK( aiEnemyCost( 0.0f, TRUE, FALSE, TRUE, 1.0f ) >= 0.0f );

	// an economy share outside 0..1 cannot flip the cost the wrong way
	CHECK( aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 5.0f ) > 0.0f );
	CHECK( aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, -5.0f ) <=
				 aiEnemyCost( CLOSE_BY, FALSE, FALSE, FALSE, 0.0f ) );
}


/** B6: the classic AI disease is sitting on twenty thousand cash while trickling out one unit at a
	 time.  The existing delays move with wealth, but only one step - past the Wealthy threshold a
	 hoard of any size buys the same discount.  This is the second step, and it is a decision to
	 spend faster rather than a production multiplier, which is the line D10 draws. */
TEST(a_cash_hoard_shortens_the_wait_but_only_so_far)
{
	// under the threshold nothing changes at all
	CHECK_EQ( 300, aiHoardAdjustedDelay( 300, 4000, 5000 ) );
	CHECK_EQ( 300, aiHoardAdjustedDelay( 300, 5000, 5000 ) );

	// twice the threshold, half the wait
	CHECK_EQ( 150, aiHoardAdjustedDelay( 300, 10000, 5000 ) );

	// and it stops at four times, however big the pile gets
	CHECK_EQ( 75, aiHoardAdjustedDelay( 300, 20000, 5000 ) );
	CHECK_EQ( 75, aiHoardAdjustedDelay( 300, 200000, 5000 ) );
	CHECK_EQ( 75, aiHoardAdjustedDelay( 300, 2000000, 5000 ) );

	// no threshold is the bottom rung, and is EA's behaviour: never hurry
	CHECK_EQ( 300, aiHoardAdjustedDelay( 300, 999999, 0 ) );

	// a delay never goes to zero, or the AI would try to build on every frame
	CHECK( aiHoardAdjustedDelay( 1, 999999, 100 ) >= 1 );
	CHECK( aiHoardAdjustedDelay( 2, 999999, 100 ) >= 1 );

	// the ladder switches it on at Medium and tightens it as you climb
	TAiData data;
	CHECK_EQ( 0, data.m_skill[ AISKILL_EASY ].m_cashHoardThreshold );
	CHECK( data.m_skill[ AISKILL_MEDIUM ].m_cashHoardThreshold > 0 );
	CHECK( data.m_skill[ AISKILL_BRUTAL ].m_cashHoardThreshold <
				 data.m_skill[ AISKILL_MEDIUM ].m_cashHoardThreshold );
}


/** Faster waits do not spend a hoard when there is nothing left to build.  On Twilight Flame a
	 brutal USA banked 117,206 with every team its script allowed already on the map: the fifth wave
	 teams allow one copy each, so it only ever replaced losses.  A hoard buys more copies. */
TEST(a_cash_hoard_allows_more_copies_of_a_team)
{
	// under two thresholds the data's limit stands
	CHECK_EQ( 1, aiHoardAllowedTeamInstances( 1, 4000, 4000 ) );
	CHECK_EQ( 1, aiHoardAllowedTeamInstances( 1, 7999, 4000 ) );

	// one more copy per two thresholds banked
	CHECK_EQ( 2, aiHoardAllowedTeamInstances( 1, 8000, 4000 ) );
	CHECK_EQ( 7, aiHoardAllowedTeamInstances( 5, 16000, 4000 ) );

	// and three more at most, however big the pile gets
	CHECK_EQ( 4, aiHoardAllowedTeamInstances( 1, 24000, 4000 ) );
	CHECK_EQ( 4, aiHoardAllowedTeamInstances( 1, 999999, 4000 ) );

	// a team the data never lets the AI build stays unbuildable
	CHECK_EQ( 0, aiHoardAllowedTeamInstances( 0, 999999, 4000 ) );

	// no threshold is the bottom rung, and EA's limit
	CHECK_EQ( 1, aiHoardAllowedTeamInstances( 1, 999999, 0 ) );
}


/** B4, second attempt.  The cash map aimed at the enemy's money and walked past his army, 7-9; this
	 one counts the guns the AI knows about along each approach and takes the quietest. */
TEST(an_attack_takes_the_least_defended_lane)
{
	const Real quiet[3] = { 5000.0f, 800.0f, 2400.0f };
	CHECK_EQ( 1, aiLeastDefendedLane( quiet, 3, 0 ) );

	// nothing known anywhere: the lane the script asked for
	const Real unknown[3] = { 0.0f, 0.0f, 0.0f };
	CHECK_EQ( 2, aiLeastDefendedLane( unknown, 3, 2 ) );

	// a tie with the asked-for lane keeps it
	const Real tie[3] = { 800.0f, 800.0f, 2400.0f };
	CHECK_EQ( 0, aiLeastDefendedLane( tie, 3, 0 ) );

	// a lane the map does not have is never taken, however quiet it reads
	const Real missing[3] = { 3000.0f, -1.0f, 1000.0f };
	CHECK_EQ( 2, aiLeastDefendedLane( missing, 3, 0 ) );

	// ... and when the asked-for lane is the missing one, the quietest that exists
	const Real noCenter[3] = { -1.0f, 900.0f, 400.0f };
	CHECK_EQ( 2, aiLeastDefendedLane( noCenter, 3, 0 ) );

	// no lanes at all: the script's
	const Real none[3] = { -1.0f, -1.0f, -1.0f };
	CHECK_EQ( 1, aiLeastDefendedLane( none, 3, 1 ) );

}


/** C2, second attempt: parked attack teams leave together once they are a wave, or once they have
	 waited long enough - and never on a threshold measured against the enemy. */
TEST(a_parked_wave_goes_when_it_is_worth_sending_or_has_waited_long_enough)
{
	const Real WAVE = 6000.0f;
	const UnsignedInt MAX_HOLD = 2700;

	// a lone artillery piece waits for company
	CHECK( !aiReleaseWave( 900.0f, WAVE, 100, MAX_HOLD ) );

	// a wave's worth goes at once
	CHECK( aiReleaseWave( 6000.0f, WAVE, 100, MAX_HOLD ) );
	CHECK( aiReleaseWave( 9000.0f, WAVE, 0, MAX_HOLD ) );

	// and a trickle still attacks eventually
	CHECK( aiReleaseWave( 900.0f, WAVE, MAX_HOLD, MAX_HOLD ) );

	// nothing parked that can fight: nothing to send, however long
	CHECK( !aiReleaseWave( 0.0f, WAVE, MAX_HOLD * 10, MAX_HOLD ) );
}


TEST(the_ladder_switches_on_reading_the_map_and_buying_at_the_top)
{
	TAiData data;
	CHECK( data.m_skill[ AISKILL_BRUTAL ].m_useInfluenceMapForAttackLane );
	CHECK( data.m_skill[ AISKILL_BRUTAL ].m_economyBuildings );
	CHECK( !data.m_skill[ AISKILL_EASY ].m_economyBuildings );
}


/** D8: role is the second axis - difficulty says how well the AI plays, role says what it is
	 trying to do.  The property that has to hold is that a role is a *preference*, not a bonus: they
	 all spend the same money, they spend it at different moments and on different things.  If one
	 role were simply stronger the axis would have quietly become a second difficulty setting. */
TEST(every_role_commits_at_its_own_moment_and_none_of_them_is_a_bonus)
{
	// every role has a threshold, and every threshold is a real fraction of an enemy army
	for( Int r = 0; r < AIROLE_COUNT; ++r )
	{
		CHECK( aiRoleMassFraction( (AIRole)r ) > 0.0f );
		CHECK( aiRoleMassFraction( (AIRole)r ) < 5.0f );
	}

	// the spread is the whole point: the aggressive one goes in with the least in hand and the
	// steamroller with the most
	CHECK( aiRoleMassFraction( AIROLE_AGGRESSIVE ) < aiRoleMassFraction( AIROLE_DEFENSIVE ) );
	CHECK( aiRoleMassFraction( AIROLE_DEFENSIVE ) < aiRoleMassFraction( AIROLE_ECONOMIST ) );
	CHECK( aiRoleMassFraction( AIROLE_ECONOMIST ) < aiRoleMassFraction( AIROLE_STEAMROLLER ) );

	// at parity with the enemy, the aggressive one has already gone and the steamroller has not
	CHECK( !aiShouldMass( 1000.0f, 1000.0f, aiRoleMassFraction( AIROLE_AGGRESSIVE ), FALSE, FALSE ) );
	CHECK( aiShouldMass( 1000.0f, 1000.0f, aiRoleMassFraction( AIROLE_STEAMROLLER ), FALSE, FALSE ) );

	// the supportive role converges on an ally's target instead of paying EA's gang-up penalty -
	// which is the single sign flip that makes allied play work
	const Real dist = 200.0f * 200.0f;
	CHECK( aiEnemyCost( dist, FALSE, TRUE, FALSE, 0.0f, FALSE ) >
				 aiEnemyCost( dist, FALSE, FALSE, FALSE, 0.0f, FALSE ) );
	CHECK( aiEnemyCost( dist, FALSE, TRUE, FALSE, 0.0f, TRUE ) <
				 aiEnemyCost( dist, FALSE, FALSE, FALSE, 0.0f, TRUE ) );

	// and it only flips the term it is about: an untargeted enemy is scored the same either way
	CHECK_NEAR( aiEnemyCost( dist, FALSE, FALSE, FALSE, 0.5f, FALSE ),
							aiEnemyCost( dist, FALSE, FALSE, FALSE, 0.5f, TRUE ), 0.001f );
}


/** A capture in progress is broken by the capturing unit walking away - that is the whole of the
	 rule, and the only movement exempt from it is the turn towards the target that the ability
	 itself orders.  The exemption used to be "the facing has started", a flag that is never cleared
	 once set, so after the first turn nothing the unit did could break the capture: an AI ordering
	 its riflemen to run from a fight left the derrick they were on flashing and sounding its
	 capture tick with nobody there, and it changed hands anyway.  The flag pair that followed had the
	 same hole whenever the turn took more than a frame, so the exemption is now the AI state itself. */
TEST(walking_away_breaks_a_capture_but_turning_towards_it_does_not)
{
	// standing on the building, working: nothing to break
	CHECK( !abilityBrokenByMovement( FALSE, TRUE, FALSE ) );

	// the ability's own turn towards the target: moving, but not leaving
	CHECK( !abilityBrokenByMovement( TRUE, TRUE, TRUE ) );

	// a retreat order replaced the turn: it has left, whoever ordered it
	CHECK( abilityBrokenByMovement( TRUE, TRUE, FALSE ) );

	// and nothing is broken when no power is up: walking is just walking
	CHECK( !abilityBrokenByMovement( TRUE, FALSE, FALSE ) );
}

/* Every AI seat a lobby offers has to survive the trip to the other machines.  The host writes the
	 slot list into one options string and every client reads it back; an AI seat is one letter in
	 there, and the writer used to be three letters plus "everything else is H", so any seat the
	 table did not name went out as Brutal on every machine, the host's included.  These two
	 functions are the one place the letters live now, and this asserts the table is whole. */
TEST(every_ai_seat_survives_the_options_string_the_host_sends_round)
{
	static const SlotState aiStates[] =
	{
		SLOT_EASY_AI, SLOT_MED_AI, SLOT_BRUTAL_AI, SLOT_TAKEOVER
	};
	const Int numAIStates = sizeof(aiStates)/sizeof(aiStates[0]);

	// every state IsAISlotState() claims as a seat has a letter, and it reads back as itself
	for (Int i = 0; i < numAIStates; ++i)
	{
		CHECK( IsAISlotState( aiStates[i] ) );
		SlotState readBack = SLOT_OPEN;
		CHECK( OptionsCharToSlotState( SlotStateToOptionsChar( aiStates[i] ), &readBack ) );
		CHECK_EQ( (Int)aiStates[i], (Int)readBack );
	}

	// and no two of them share one - the bug was several seats sharing one letter
	for (Int a = 0; a < numAIStates; ++a)
		for (Int b = a + 1; b < numAIStates; ++b)
			CHECK_NE( SlotStateToOptionsChar( aiStates[a] ), SlotStateToOptionsChar( aiStates[b] ) );

	// the four letters that shipped keep their meaning, or an old replay reads as a different game
	CHECK_EQ( 'E', SlotStateToOptionsChar( SLOT_EASY_AI ) );
	CHECK_EQ( 'M', SlotStateToOptionsChar( SLOT_MED_AI ) );
	CHECK_EQ( 'H', SlotStateToOptionsChar( SLOT_BRUTAL_AI ) );
	CHECK_EQ( 'P', SlotStateToOptionsChar( SLOT_TAKEOVER ) );

	// a seat that is not an AI one is not written as an AI one, and a letter nobody uses is refused
	CHECK( !IsAISlotState( SLOT_OPEN ) );
	CHECK( !IsAISlotState( SLOT_CLOSED ) );
	CHECK( !IsAISlotState( SLOT_PLAYER ) );
	CHECK( !OptionsCharToSlotState( 'Q', NULL ) );
}

/* Every .wnd layout is drawn at 800x600 and stretched to the screen by the resolution ratio, but
	 the point size written beside it was handed to the font library untouched, and what adjustment
	 there was stopped dead at twice the original.  So from about 1950 pixels wide up the text
	 stopped growing while the panel around it kept going: on a 2560 wide screen the command bar was
	 three times its designed size wearing the same 8pt letters, which is where the build hotkeys,
	 the prices and the build times went. */
TEST(text_keeps_growing_with_the_screen_instead_of_stopping_at_twice)
{
	const Real damping = 0.7f;		// the shipped ResolutionFontAdjustment, and the class default

	// 800x600 is the resolution every layout was drawn at, so nothing may move there - and nothing
	// shrinks below it either
	CHECK_EQ( 8, GlobalLanguage::adjustFontSizeForScreen( 8, 800, 600, damping ) );
	CHECK_EQ( 8, GlobalLanguage::adjustFontSizeForScreen( 8, 640, 480, damping ) );

	// a command button's "Arial 8" used to stop at 16 for ever, 1950 pixels wide and up
	CHECK( GlobalLanguage::adjustFontSizeForScreen( 8, 2560, 1920, damping ) > 16 );
	CHECK( GlobalLanguage::adjustFontSizeForScreen( 8, 3840, 2880, damping ) >
				 GlobalLanguage::adjustFontSizeForScreen( 8, 2560, 1920, damping ) );

	Int previous = 0;
	for( Int width = 800; width <= 3840; width += 32 )
	{
		const Int height = width * 3 / 4;
		const Int size = GlobalLanguage::adjustFontSizeForScreen( 100, width, height, damping );

		// it never outruns the stretch applied to the window it sits in, or the text spills out of
		// its own panel - which is what the old ceiling was there to prevent
		CHECK( size <= 100 * width / 800 );

		// and it never goes backwards as the screen grows
		CHECK( size >= previous );
		previous = size;
	}
}

/* Two shots of the same command bar, one 1920x1080 and one 5120x1440, have to overlay once the
	 second is scaled down by the ratio of the two bars' own scales.  They did not: the bar is laid
	 out at the smaller of width over 800 and height over 600, and the lettering was sized off the
	 width alone.  At 5120x1440 the width says 6.4 and the height says 2.4, so the money readout,
	 the build hotkeys and the clock in the corner came out about twice the size the panel under
	 them had grown to, and the 32:9 screenshot did not look like the 16:9 one. */
TEST(text_grows_with_the_panel_it_sits_in_and_not_with_the_screens_width)
{
	const Real damping = 0.7f;

	// the two screens the complaint was made with.  The bar's own scale is min(w/800, h/600), so
	// 1.8 and 2.4 - and whatever the text does, it has to do it in that same 4:3 ratio
	const Int at1080 = GlobalLanguage::adjustFontSizeForScreen( 100, 1920, 1080, damping );
	const Int at1440 = GlobalLanguage::adjustFontSizeForScreen( 100, 5120, 1440, damping );
	CHECK_NEAR( 100.0f * (1.0f + (1.8f-1.0f)*damping), at1080, 1 );
	CHECK_NEAR( 100.0f * (1.0f + (2.4f-1.0f)*damping), at1440, 1 );

	// widening a screen without making it taller must not change the lettering at all - that is
	// the whole of the bug, and it is what the old width-only formula could not do
	for( Int width = 1920; width <= 5120; width += 160 )
		CHECK_EQ( GlobalLanguage::adjustFontSizeForScreen( 100, 1920, 1080, damping ),
							GlobalLanguage::adjustFontSizeForScreen( 100, width, 1080, damping ) );

	// and a taller screen still grows it
	CHECK( GlobalLanguage::adjustFontSizeForScreen( 100, 1920, 1440, damping ) > at1080 );
}

/* The other half of the same complaint: the camera. Retail pins the horizontal cone at 50 degrees
	 whatever shape the screen is, so a 32:9 monitor spends the same 50 degrees over 5120 pixels that
	 a 16:9 one spends over 1920 - the world comes out 2.7 times the size and the vertical view falls
	 from 29.4 degrees to 14.9. Past 16:9 the vertical half-angle is held instead. */
TEST(the_camera_shows_the_same_world_height_however_wide_the_screen_is)
{
	const Real design = 50.0f * PI / 180.0f;

	// nothing at or below 16:9 moves - 4:3, 5:4 and 16:9 are all still retail's own cone
	CHECK_NEAR( design, ViewHorizontalFovForScreen( 800, 600 ), 0.0001f );
	CHECK_NEAR( design, ViewHorizontalFovForScreen( 1280, 1024 ), 0.0001f );
	CHECK_NEAR( design, ViewHorizontalFovForScreen( 1920, 1080 ), 0.0001f );

	// the vertical half-angle is what has to stand still, and it is a tangent, not the angle
	const Real reference = (Real)tan( ViewHorizontalFovForScreen( 1920, 1080 ) * 0.5f )
												 / ( 1920.0f / 1080.0f );
	const Int widths[]  = { 2560, 3440, 5120, 7680 };
	const Int heights[] = { 1080, 1440, 1440, 2160 };
	for( Int i = 0; i < 4; ++i )
	{
		const Real hfov = ViewHorizontalFovForScreen( widths[i], heights[i] );
		const Real vertical = (Real)tan( hfov * 0.5f ) / ( (Real)widths[i] / (Real)heights[i] );
		CHECK_NEAR( reference, vertical, 0.0001f );

		// and it only ever opens: an ultrawide sees more world, never less
		CHECK( hfov > design );
	}

	// a 32:9 screen used to see under half the world height a 16:9 one did, which is the number
	// the two screenshots were compared on
	CHECK_NEAR( design, ViewHorizontalFovForScreen( 1920, 1080 ), 0.0001f );
	CHECK( ViewHorizontalFovForScreen( 5120, 1440 ) > 1.4f );		// ~86 degrees, was 50

	// a degenerate size does not divide by zero
	CHECK_NEAR( design, ViewHorizontalFovForScreen( 0, 0 ), 0.0001f );
}
/* The lobby, the seat itself, the game info panel and the online browser's tooltip each carried
	 their own switch over the slot states, and no two agreed: EA's shipped strings read "Easy Army"
	 and "Medium Army" where another list wrote "Easy AI", so one drop-down offered a ladder named
	 half one way and half the other - and "GUI:HardAI" sat on the Brutal rung in one file and
	 somewhere else in another.  SlotStateName is the only list now.  (Only the AI rungs are checked
	 here: Open, Closed and the takeover seat go through TheGameText, which no test has.) */
TEST(every_ai_rung_is_named_the_same_way_as_every_other)
{
	static const SlotState rungs[] =
	{
		SLOT_EASY_AI, SLOT_MED_AI, SLOT_BRUTAL_AI
	};
	const Int numRungs = sizeof(rungs)/sizeof(rungs[0]);

	for (Int i = 0; i < numRungs; ++i)
	{
		UnicodeString name = SlotStateName( rungs[i] );

		// a rung with no case of its own used to come back named "Closed"
		CHECK( name.getLength() > 3 );

		// and one style for the lot: "Easy Army" beside "Medium AI" is what this is here to stop
		// (CHECK_STR is narrow-char, and casting a WideChar* into it compares one byte and passes)
		CHECK( wcscmp( name.str() + name.getLength() - 3, L" AI" ) == 0 );
	}

	// no two rungs share a name, or the drop-down cannot say which one you picked
	for (Int a = 0; a < numRungs; ++a)
		for (Int b = a + 1; b < numRungs; ++b)
			CHECK( wcscmp( SlotStateName( rungs[a] ).str(), SlotStateName( rungs[b] ).str() ) != 0 );
}
/* Five is what a column of the strip shows before the rest of the queue folds into the "+N" that
	 closes it as a sixth cell.  It used to be sixteen across the bottom of the screen, which at a
	 busy war factory ran the cameos most of the way over the map. */
TEST(the_production_strip_folds_a_long_queue_into_its_overflow)
{
	CHECK_EQ( 5, (Int)InGameUI::PRODUCTION_STRIP_ROW_MAX );

	// while watching, eight columns share the screen at once, so a column there is never the taller
	// one - and neither cap may outrun the slots the strip has room to remember
	CHECK( (Int)InGameUI::PRODUCTION_STRIP_WATCH_MAX <= (Int)InGameUI::PRODUCTION_STRIP_ROW_MAX );

	//
	// The column, its overflow cell included, has to stand inside the 600 the layout is written in
	// with room to spare for the control bar it stands on: it grows upward out of the corner, and a
	// cell drawn past the top of the screen is a cell nobody can read.
	//
	const Int cells = (Int)InGameUI::PRODUCTION_STRIP_ROW_MAX + 1;
	const Int height = cells * (Int)InGameUI::PRODUCTION_STRIP_TRAY_H;
	CHECK( height < 600 / 2 );
}

/* Every cameo in the strip stands in the tray the general's powers stand in, down in the corner,
	 and it stands in it at the size that bar draws it - stretched, the tray's rail and inner frame
	 collapse into a coloured smudge and it stops reading as that bar at all.  So the strip does not
	 get to pick its own numbers: it borrows that bar's, and these are the ones that make the borrowed
	 art come out right.  Sliding the cameo out of the box painted for it, or over the rail, is the
	 way this goes wrong quietly - it still draws, it just looks like a mistake. */
TEST(a_queue_cameo_sits_in_the_general_power_tray_the_way_that_bar_sits_in_it)
{
	// the cameo is inside its tray, both ways, art and all
	CHECK( (Int)InGameUI::PRODUCTION_STRIP_TRAY_X >= 0 );
	CHECK( (Int)InGameUI::PRODUCTION_STRIP_TRAY_Y >= 0 );
	CHECK( (Int)InGameUI::PRODUCTION_STRIP_TRAY_X + (Int)InGameUI::PRODUCTION_STRIP_QUEUE_W
					<= (Int)InGameUI::PRODUCTION_STRIP_TRAY_W );
	CHECK( (Int)InGameUI::PRODUCTION_STRIP_TRAY_Y + (Int)InGameUI::PRODUCTION_STRIP_QUEUE_H
					<= (Int)InGameUI::PRODUCTION_STRIP_TRAY_H );

	//
	// Six whole trays side by side is what a row costs across - and that has to fit inside the 800
	// the whole layout is written in, or the strip runs off the side of the screen before its
	// overflow count is ever reached.
	//
	const Int rowWidth = (Int)InGameUI::PRODUCTION_STRIP_ROW_MAX * (Int)InGameUI::PRODUCTION_STRIP_TRAY_W;
	CHECK( rowWidth < 800 );
}

/* Both strips now ask the control bar for the tray, and the control bar works the whole geometry
	 out of one number: the size the window loader gave a shortcut slot.  Everything downstream - the
	 hole the cameo goes in, the step a row runs at - is a fraction of the artwork, so a bar the
	 loader scaled to a widescreen resolution scales the strips with it instead of leaving 800x600
	 cameos rattling around inside 1600x900 trays.

	 The numbers checked here are the ones the strip's own constants were authored against: the
	 shortcut slot is 48x41 in the 800x600 the layouts are written in. */
TEST(the_strip_tray_geometry_is_a_fraction_of_whatever_size_the_bar_was_loaded_at)
{
	ICoord2D tray, cameo, hole;
	Int step = 0;

	// nothing to measure is FALSE rather than a division by zero
	CHECK( !ControlBar::trayLayoutFromSlot( 0, 41, &tray, &cameo, &hole, &step ) );
	CHECK( !ControlBar::trayLayoutFromSlot( 48, 0, &tray, &cameo, &hole, &step ) );
	CHECK( !ControlBar::trayLayoutFromSlot( -48, -41, &tray, &cameo, &hole, &step ) );

	// the slot the strip's own constants were written against
	CHECK( ControlBar::trayLayoutFromSlot( (Int)InGameUI::PRODUCTION_STRIP_TRAY_W,
																				 (Int)InGameUI::PRODUCTION_STRIP_TRAY_H,
																				 &tray, &cameo, &hole, &step ) );

	// the tray is the slot, whole: it is never squeezed to fit anything
	CHECK_EQ( (Int)InGameUI::PRODUCTION_STRIP_TRAY_W, tray.x );
	CHECK_EQ( (Int)InGameUI::PRODUCTION_STRIP_TRAY_H, tray.y );

	// and the cameo is inside it, art and all, both ways
	CHECK( hole.x >= 0 && hole.y >= 0 );
	CHECK( hole.x + cameo.x <= tray.x );
	CHECK( hole.y + cameo.y <= tray.y );
	CHECK( cameo.x > 0 && cameo.y > 0 );

	// a row steps a whole tray, so one tray never lies over the cameo in the next
	CHECK_EQ( tray.x, step );

	//
	// Twice the bar, twice everything: an observer borrowing another side's tray gets that side's
	// slot size, and every one of these has to follow it or the borrowed art comes out with the
	// cameo hanging over its rail.
	//
	ICoord2D bigTray, bigCameo, bigHole;
	Int bigStep = 0;
	CHECK( ControlBar::trayLayoutFromSlot( 2 * (Int)InGameUI::PRODUCTION_STRIP_TRAY_W,
																				 2 * (Int)InGameUI::PRODUCTION_STRIP_TRAY_H,
																				 &bigTray, &bigCameo, &bigHole, &bigStep ) );
	CHECK_EQ( 2 * tray.x, bigTray.x );
	CHECK_EQ( 2 * tray.y, bigTray.y );
	CHECK_EQ( 2 * step, bigStep );
	CHECK( bigCameo.x >= 2 * cameo.x - 1 && bigCameo.x <= 2 * cameo.x + 1 );
	CHECK( bigCameo.y >= 2 * cameo.y - 1 && bigCameo.y <= 2 * cameo.y + 1 );
	CHECK( bigHole.x + bigCameo.x <= bigTray.x );
	CHECK( bigHole.y + bigCameo.y <= bigTray.y );

	// every out-parameter is optional: the layout pass only wants the step
	Int stepOnly = 0;
	CHECK( ControlBar::trayLayoutFromSlot( (Int)InGameUI::PRODUCTION_STRIP_TRAY_W,
																				 (Int)InGameUI::PRODUCTION_STRIP_TRAY_H,
																				 NULL, NULL, NULL, &stepOnly ) );
	CHECK_EQ( step, stepOnly );
}

/* No tray lies over another, in either direction: a cell steps a whole tray sideways as well as
	 upward.  A tray sliding under its neighbour cut the rail across the picture next to it, which is
	 what the general's powers and the superweapon countdowns both looked like. */
TEST(a_stacked_tray_does_not_lie_over_the_one_below_it)
{
	// upward: nothing is taken off, so a cell has to clear the whole picture that stands in it
	CHECK( (Int)InGameUI::PRODUCTION_STRIP_TRAY_Y + (Int)InGameUI::PRODUCTION_STRIP_QUEUE_H
					<= (Int)InGameUI::PRODUCTION_STRIP_TRAY_H );

	//
	// the superweapon strip stands in the same trays, three rows of six of them, and that pile has
	// to fit under the corner clock plate rather than run off the bottom of the 800x600 it is
	// written in - at the full tray now, which is the taller pile of the two
	//
	const Int rows = (Int)InGameUI::SUPERWEAPON_STRIP_ROWS;
	const Int pileHeight = rows * (Int)InGameUI::PRODUCTION_STRIP_TRAY_H;
	CHECK( pileHeight < 600 / 2 );

	//
	// Watching, the vertical is the players: a row each, a whole tray apart, piled up off the bottom
	// of the screen.  Eight of them have to leave the top of a 600 tall screen alone, and a row -
	// the few soonest plus the tray the "+N" closes it with - has to stay well inside 800 across,
	// since it is drawn over the battlefield rather than over a bar.
	//
	const Int watchPile = (Int)InGameUI::PRODUCTION_STRIP_ROWS * (Int)InGameUI::PRODUCTION_STRIP_TRAY_H;
	CHECK( watchPile < 2 * 600 / 3 );

	const Int watchWidth = ( (Int)InGameUI::PRODUCTION_STRIP_WATCH_MAX + 1 )
													* (Int)InGameUI::PRODUCTION_STRIP_TRAY_W;
	CHECK( watchWidth < 800 / 2 );

	const Int rowWidth = (Int)InGameUI::SUPERWEAPON_STRIP_COLS * (Int)InGameUI::PRODUCTION_STRIP_TRAY_W;
	CHECK( rowWidth < 800 );
}

/* Buildings going up on the map are not in anybody's queue - they are objects standing on the
	 ground with a percentage on them - but they land in the same column as the queued items, sorted
	 against them on the one thing the two kinds share: how long each still has.  The comparison the
	 column is built with is therefore blind to which kind a slot is, and there is one row while
	 playing, at the front of the array. */
TEST(the_buildings_going_up_stand_in_the_queue_column)
{
	CHECK_EQ( 0, (Int)InGameUI::PRODUCTION_ROW_QUEUE );
	CHECK( (Int)InGameUI::PRODUCTION_ROW_QUEUE < (Int)InGameUI::PRODUCTION_STRIP_ROWS );

	// a site three seconds out goes in front of a tank ten seconds out, and not the other way round
	CHECK( InGameUI::stripSlotGoesBefore( FALSE, 90, FALSE, 300 ) );
	CHECK( !InGameUI::stripSlotGoesBefore( FALSE, 300, FALSE, 90 ) );
}

/* The building you have selected does not get a row of its own: its items lead the queue row, so
	 the front of the strip is what the thing you are looking at is making and the rest of the base
	 follows it.  Inside either block the soonest to finish is still first, and a tie never moves -
	 an item only goes in front of one it beats, so a base full of identical barracks does not
	 shuffle from frame to frame. */
TEST(the_selected_buildings_items_lead_the_queue_row)
{
	// a selected item passes anything that is not selected, however long it has left
	CHECK( InGameUI::stripSlotGoesBefore( TRUE, 9000, FALSE, 1 ) );
	// and nothing that is not selected ever passes one
	CHECK( !InGameUI::stripSlotGoesBefore( FALSE, 1, TRUE, 9000 ) );

	// inside a block, soonest first
	CHECK( InGameUI::stripSlotGoesBefore( FALSE, 100, FALSE, 200 ) );
	CHECK( !InGameUI::stripSlotGoesBefore( FALSE, 200, FALSE, 100 ) );
	CHECK( InGameUI::stripSlotGoesBefore( TRUE, 100, TRUE, 200 ) );
	CHECK( !InGameUI::stripSlotGoesBefore( TRUE, 200, TRUE, 100 ) );

	// a tie holds its place - in both blocks
	CHECK( !InGameUI::stripSlotGoesBefore( FALSE, 100, FALSE, 100 ) );
	CHECK( !InGameUI::stripSlotGoesBefore( TRUE, 100, TRUE, 100 ) );
}

/* That bar grows leftward out of the corner, so the heavy rail of its tray is on the right hand
	 edge of the artwork.  A row running rightward from the left of the screen wants that rail on the
	 other side, so the strip draws the tray mirrored - and it mirrors it in the UV rect alone, over
	 the same texture page, rather than shipping a second copy of the art the other way round.  Every
	 other field has to come across with it or the copy draws the wrong piece of the page at the wrong
	 size, and the original has to be left alone, because the bar in the corner is still drawing it. */
TEST(a_mirrored_image_is_the_same_piece_of_texture_the_other_way_round)
{
	Image *source = newInstance( Image );
	source->setName( AsciiString( "SATraySmall" ) );
	source->setFilename( AsciiString( "SAControlBar512_001.tga" ) );
	source->setTextureWidth( 512 );
	source->setTextureHeight( 512 );

	ICoord2D size = { 60, 56 };
	source->setImageSize( &size );

	Region2D uv;
	uv.lo.x = 413.0f / 512.0f;
	uv.lo.y = 1.0f / 512.0f;
	uv.hi.x = 473.0f / 512.0f;
	uv.hi.y = 57.0f / 512.0f;
	source->setUV( &uv );

	Image *mirrored = newMirroredImage( source );

	// the whole point: the horizontal run of the UV rect is reversed
	CHECK_NEAR( 473.0f / 512.0f, mirrored->getUV()->lo.x, 0.0001f );
	CHECK_NEAR( 413.0f / 512.0f, mirrored->getUV()->hi.x, 0.0001f );

	// and nothing else about it moves
	CHECK_NEAR( 1.0f / 512.0f, mirrored->getUV()->lo.y, 0.0001f );
	CHECK_NEAR( 57.0f / 512.0f, mirrored->getUV()->hi.y, 0.0001f );
	CHECK_STR( "SAControlBar512_001.tga", mirrored->getFilename().str() );
	CHECK_EQ( 512, mirrored->getTextureSize()->x );
	CHECK_EQ( 512, mirrored->getTextureSize()->y );
	CHECK_EQ( 60, mirrored->getImageWidth() );
	CHECK_EQ( 56, mirrored->getImageHeight() );

	// the tray the control bar owns is left exactly as it was found
	CHECK_NEAR( 413.0f / 512.0f, source->getUV()->lo.x, 0.0001f );
	CHECK_NEAR( 473.0f / 512.0f, source->getUV()->hi.x, 0.0001f );

	mirrored->deleteInstance();
	source->deleteInstance();
}

//-------------------------------------------------------------------------------------------------
// THREADING-ROADMAP.md 3.1 - the fork-join pool.
//
// Everything the roadmap puts on top of this pool is a pure function over an array, so the pool
// itself is the only place a data race can be introduced by construction rather than by review -
// and there is no TSan on MSVC/Win32 x86 (section 7).  These tests are therefore about the two
// promises the callers rely on and nothing else: every item runs exactly once, and a worker
// computes floats the way the rest of the game does.
//-------------------------------------------------------------------------------------------------

enum { JOB_TEST_MAX = 4096 };
static Int s_jobVisits[ JOB_TEST_MAX ];
static UnsignedInt s_jobFPMode[ JOB_TEST_MAX ];
static Int s_jobWorkerFlag[ JOB_TEST_MAX ];
static volatile LONG s_jobWorkerSeen = 0;
static volatile LONG s_jobWaitedOnce = 0;

/* The forking thread works the queue too, and a job body that does nothing at all is finished long
	 before a worker is out of WaitForSingleObject - so a test that wants to observe a worker has to
	 hold the first item until one turns up.  One bounded wait per fork, and none at all once a
	 worker has been seen, so this costs nothing when the pool is behaving. */
static void jobTestWaitForAWorker( void )
{
	if( JobSystem::isWorkerThread() )
	{
		InterlockedExchange( (LONG *)&s_jobWorkerSeen, 1 );
		return;
	}
	if( s_jobWorkerSeen || InterlockedExchange( (LONG *)&s_jobWaitedOnce, 1 ) != 0 )
		return;

	const DWORD deadline = ::GetTickCount() + 500;
	while( !s_jobWorkerSeen && ::GetTickCount() < deadline )
		::Sleep( 0 );
}

static void jobTestCount( Int index, void * )
{
	// Each item owns its own slot, so this needs no lock and no atomic - which is the shape every
	// job on the roadmap has to have.
	++s_jobVisits[ index ];
}

static void jobTestRecordFPU( Int index, void * )
{
	jobTestWaitForAWorker();
	s_jobFPMode[ index ] = getFPMode();
	s_jobWorkerFlag[ index ] = JobSystem::isWorkerThread() ? 1 : 0;
}

static void jobTestAllocate( Int index, void * )
{
	// Deliberately breaks the no-allocation rule, to prove the counter that watches for it is not
	// simply stuck at zero.
	jobTestWaitForAWorker();
	Image *img = newInstance( Image );
	s_jobVisits[ index ] = 1;
	img->deleteInstance();
}

static void resetJobVisits( void )
{
	s_jobWorkerSeen = 0;
	s_jobWaitedOnce = 0;
	for( Int i = 0; i < JOB_TEST_MAX; ++i )
	{
		s_jobVisits[ i ] = 0;
		s_jobFPMode[ i ] = 0;
		s_jobWorkerFlag[ i ] = 0;
	}
}

/* The one thing every caller assumes: the range is covered, once, and parallel_for does not come
	 back before that is true.  Run over a spread of counts and granularities, including the ones
	 that make the split uneven and the ones that collapse it to a single chunk. */
TEST(parallel_for_runs_every_item_exactly_once)
{
	static const Int counts[] = { 0, 1, 2, 3, 7, 8, 63, 64, 65, 1000, 4096 };
	static const Int grains[] = { 1, 2, 3, 16, 4096 };

	JobSystem::shutdown();
	JobSystem::init( 4 );
	CHECK_EQ( 4, JobSystem::workerCount() );

	const Int numCounts = (Int)(sizeof(counts)/sizeof(counts[0]));
	const Int numGrains = (Int)(sizeof(grains)/sizeof(grains[0]));
	for( Int c = 0; c < numCounts; ++c )
	{
		for( Int g = 0; g < numGrains; ++g )
		{
			resetJobVisits();
			JobSystem::parallel_for( counts[c], grains[g], jobTestCount, NULL );

			Int seen = 0;
			for( Int i = 0; i < JOB_TEST_MAX; ++i )
			{
				if( i < counts[c] )
					seen += (s_jobVisits[ i ] == 1) ? 1 : 0;
				else
					CHECK_EQ( 0, s_jobVisits[ i ] );		// never runs past the end of the range
			}
			CHECK_EQ( counts[c], seen );
		}
	}

	// A granularity below 1 is a caller mistake, not a division by zero.
	resetJobVisits();
	JobSystem::parallel_for( 10, 0, jobTestCount, NULL );
	for( Int i = 0; i < 10; ++i )
		CHECK_EQ( 1, s_jobVisits[ i ] );

	JobSystem::shutdown();
}

/* A machine with one core, or a build that asks for no pool, has to run the same code rather than
	 a second single-threaded implementation nobody tests. */
TEST(parallel_for_with_no_workers_still_runs_everything)
{
	JobSystem::shutdown();
	JobSystem::init( 0 );
	CHECK_EQ( 0, JobSystem::workerCount() );

	resetJobVisits();
	JobSystem::parallel_for( 100, 4, jobTestCount, NULL );
	for( Int i = 0; i < 100; ++i )
		CHECK_EQ( 1, s_jobVisits[ i ] );

	// and on the calling thread, so nothing inside a job may believe it is on a worker
	resetJobVisits();
	JobSystem::parallel_for( 8, 1, jobTestRecordFPU, NULL );
	for( Int i = 0; i < 8; ++i )
		CHECK_EQ( 0, s_jobWorkerFlag[ i ] );

	JobSystem::shutdown();
}

/* THREADING-ROADMAP.md 1.3.  The FPU control word is per-thread and a new thread starts on the
	 CRT default, not on what setFPMode() left on the main thread.  A worker that computes floats at
	 a different precision or rounding mode produces results that differ from the main thread's for
	 no visible reason - the kind of bug that never crashes and never reproduces. */
TEST(a_job_computes_floats_the_way_the_game_does)
{
	setFPMode();

	JobSystem::shutdown();
	JobSystem::init( 4 );

	resetJobVisits();
	// One item per chunk over a range far bigger than the pool, so every worker takes some.
	JobSystem::parallel_for( 512, 1, jobTestRecordFPU, NULL );

	Int ranOnAWorker = 0;
	for( Int i = 0; i < 512; ++i )
	{
		CHECK_EQ( expectedFPMode(), s_jobFPMode[ i ] );
		ranOnAWorker += s_jobWorkerFlag[ i ];
	}
	// ...and the check above was not vacuous because the main thread ran all of it
	CHECK( ranOnAWorker > 0 );

	// the fork left the calling thread's own mode alone
	CHECK_EQ( expectedFPMode(), getFPMode() );

	JobSystem::shutdown();
}

/* THREADING-ROADMAP.md 1.1: every MemoryPool in the game shares one critical section, so an
	 allocation inside a job does not crash - it quietly serializes the fork.  The counter is the
	 only thing that can see that happen, so it has to be shown to move. */
TEST(an_allocation_inside_a_job_is_counted)
{
	JobSystem::shutdown();
	JobSystem::init( 4 );

	const Int before = JobSystem::workerAllocationCount();

	resetJobVisits();
	JobSystem::parallel_for( 256, 1, jobTestCount, NULL );
	CHECK_EQ( before, JobSystem::workerAllocationCount() );		// a well-behaved job costs nothing

	/* WinMain installs this and nothing else does, so in a test binary the pools are unguarded -
		 and the job below allocates from several threads at once on purpose.  Without it this test
		 corrupts a pool and hangs, which is itself the point of section 1.1: the allocator is safe
		 to call from a worker only because that one global lock is there. */
	CriticalSection poolLock;
	TheMemoryPoolCriticalSection = &poolLock;

	resetJobVisits();
	JobSystem::parallel_for( 256, 1, jobTestAllocate, NULL );

	TheMemoryPoolCriticalSection = NULL;

	CHECK( JobSystem::workerAllocationCount() > before );

	JobSystem::shutdown();
}

/* A fork made the moment the last one returned finds some workers still on their way back to the
	 wait; a fork made after a pause finds them all asleep.  A wake lost in either case hangs this test,
	 and a wake left over from one fork and taken by the next returns before its workers are done,
	 which the visit count sees.  A spin-before-sleep version of the pool hung the full test run once
	 on exactly that, and measured worse on the particle scene besides, so the pool sleeps at once. */
TEST(parallel_for_covers_forks_back_to_back_and_forks_after_a_pause)
{
	static const Int BACK_TO_BACK_FORKS = 2000;
	static const Int PAUSED_FORKS = 20;
	static const DWORD PAUSE_MS = 5;
	static const Int ITEMS = 64;

	JobSystem::shutdown();
	JobSystem::init( 4 );

	for( Int fork = 0; fork < BACK_TO_BACK_FORKS + PAUSED_FORKS; ++fork )
	{
		if( fork >= BACK_TO_BACK_FORKS )
			::Sleep( PAUSE_MS );

		resetJobVisits();
		JobSystem::parallel_for( ITEMS, 1, jobTestCount, NULL );

		Int seen = 0;
		for( Int i = 0; i < ITEMS; ++i )
			seen += (s_jobVisits[ i ] == 1) ? 1 : 0;
		CHECK_EQ( ITEMS, seen );
	}

	JobSystem::shutdown();
}

/* The pool is torn down inside ~GameEngine, on the way out of a process that may be quitting from
	 anywhere.  It has to survive being stopped without ever being started, being stopped twice, and
	 being started again afterwards - none of which may hang. */
TEST(the_pool_survives_being_started_and_stopped_repeatedly)
{
	JobSystem::shutdown();
	JobSystem::shutdown();									// no pool: nothing to do, and no wait to hang on
	CHECK_EQ( 0, JobSystem::workerCount() );

	for( Int round = 0; round < 3; ++round )
	{
		JobSystem::init( 3 );
		CHECK_EQ( 3, JobSystem::workerCount() );

		JobSystem::init( 8 );									// already running: ignored, not a second pool
		CHECK_EQ( 3, JobSystem::workerCount() );

		resetJobVisits();
		JobSystem::parallel_for( 200, 1, jobTestCount, NULL );
		for( Int i = 0; i < 200; ++i )
			CHECK_EQ( 1, s_jobVisits[ i ] );

		JobSystem::shutdown();
		CHECK_EQ( 0, JobSystem::workerCount() );

		// and with the pool down, the same call still covers the range
		resetJobVisits();
		JobSystem::parallel_for( 200, 1, jobTestCount, NULL );
		for( Int i = 0; i < 200; ++i )
			CHECK_EQ( 1, s_jobVisits[ i ] );
	}
}

/* ProductionUpdate.cpp: a producer with a door animation only started opening it once the unit
	 was already finished, and then held the unit inside until the animation ran out - so every
	 tank cost its build time plus the whole door opening time.  The door now starts early enough
	 to finish opening on the frame the unit is done. */
extern Bool Production_shouldOpenDoorEarly( Int framesRemaining, UnsignedInt doorOpeningTime );

TEST(door_starts_opening_before_the_unit_is_finished)
{
	/* a 60 frame door: updateDoors() only leaves the opening state on the frame *after* the time
		 is up, so the door has to start 61 frames out to be open when the unit lands. */
	CHECK( Production_shouldOpenDoorEarly( 61, 60 ) );
	CHECK( Production_shouldOpenDoorEarly( 1, 60 ) );

	/* one frame earlier than that and the door would be sitting open, waiting on the build. */
	CHECK( !Production_shouldOpenDoorEarly( 62, 60 ) );
	CHECK( !Production_shouldOpenDoorEarly( 600, 60 ) );

	/* the unit is done: the finished-production path owns the door from here, not this one. */
	CHECK( !Production_shouldOpenDoorEarly( 0, 60 ) );
	CHECK( !Production_shouldOpenDoorEarly( -5, 60 ) );

	/* a producer whose door opens instantly still gets it on the last frame, and never before. */
	CHECK( Production_shouldOpenDoorEarly( 1, 0 ) );
	CHECK( !Production_shouldOpenDoorEarly( 2, 0 ) );
}

/* ProductionUpdate.cpp: the door ran on a stopwatch - it shut a fixed time after a unit came out,
	 whatever was behind him.  A factory working through a queue therefore closed and hauled its
	 doors straight back up for every vehicle, and there is no artwork for a door that changes its
	 mind partway, so a door caught mid-close snapped wide in a single frame.  The queue closes the
	 doors now: they stay open while a unit is still due, and shut when the queue runs out. */
extern Bool Production_shouldCloseDoorNow( UnsignedInt framesWaitingOpen, UnsignedInt doorWaitOpenTime,
																					Bool holdOpen, Bool moreUnitsComing );

TEST(a_door_stays_open_while_another_unit_is_still_coming)
{
	/* nothing left to build and the door has stood open its full time: shut it. */
	CHECK( Production_shouldCloseDoorNow( 31, 30, false, false ) );

	/* another vehicle behind this one - the door stays up however long it has been open. */
	CHECK( !Production_shouldCloseDoorNow( 31, 30, false, true ) );
	CHECK( !Production_shouldCloseDoorNow( 100000, 30, false, true ) );

	/* the queue emptying is what closes it, so the same door shuts the moment nothing is due. */
	CHECK( Production_shouldCloseDoorNow( 100000, 30, false, false ) );

	/* it still owes its open time to the man walking out: an empty queue does not slam it. */
	CHECK( !Production_shouldCloseDoorNow( 30, 30, false, false ) );
	CHECK( !Production_shouldCloseDoorNow( 0, 30, false, false ) );

	/* somebody holding the door open by hand still wins over both. */
	CHECK( !Production_shouldCloseDoorNow( 31, 30, true, false ) );
	CHECK( !Production_shouldCloseDoorNow( 31, 30, true, true ) );
}

TEST(borderless_asks_for_a_windowed_device_the_size_of_the_desktop)
{
	/* -borderless is borderless fullscreen.  Half of it is the window itself, which WinMain creates
		 before the engine exists and which nothing here can reach; the other half is the back buffer,
		 and that half is this table's.  Getting it wrong is not cosmetic: a back buffer that does not
		 match the window is presented as a stretched blit, and the cursor stops landing where it is
		 drawn. */
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	scratch->m_windowed = FALSE;
	scratch->m_xResolution = 640;
	scratch->m_yResolution = 480;
	scratch->m_edgeScrollInWindowedMode = FALSE;

	char *argv[] = { "generals.exe", "-borderless" };
	parseCommandLine( 2, argv );

	CHECK( scratch->m_windowed );
	CHECK_EQ( scratch->m_xResolution, GetSystemMetrics( SM_CXSCREEN ) );
	CHECK_EQ( scratch->m_yResolution, GetSystemMetrics( SM_CYSCREEN ) );

	/* and edge scrolling comes back on: retail refuses it in a window because the cursor can
		 legitimately sit on the border, and a window covering the display has no such border. */
	CHECK( scratch->m_edgeScrollInWindowedMode );

	/* -xres/-yres after it still win - that is how a smaller back buffer in a borderless window is
		 asked for, and it is the same rule -headless follows. */
	char *argv2[] = { "generals.exe", "-borderless", "-xres", "1280", "-yres", "720" };
	parseCommandLine( 6, argv2 );
	CHECK_EQ( scratch->m_xResolution, 1280 );
	CHECK_EQ( scratch->m_yResolution, 720 );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(every_side_has_three_plates_and_every_general_wears_its_sides)
{
	/* The bar is three panels and each wears a plate cut from its side's own painting.  A scheme
		 names a side like "ChinaTankGeneral", the boss bar calls itself "Boss" and the observer bar
		 "Observer", and none of those three has artwork of its own - so a side that fell through this
		 lookup would leave the whole bar with no background at all. */
	static const char *sides[] =
	{
		"America", "China", "GLA",
		"AmericaSuperWeaponGeneral", "AmericaLaserGeneral", "AmericaAirForceGeneral",
		"ChinaTankGeneral", "ChinaInfantryGeneral", "ChinaNukeGeneral",
		"GLAToxinGeneral", "GLADemolitionGeneral", "GLAStealthGeneral",
		"Boss", "Observer", NULL
	};

	for( const char **s = sides; *s; s++ )
	{
		Int previousRight = -1;
		for( Int p = 0; p < ControlBar::CB_PANEL_COUNT; p++ )
		{
			const ControlBarPlate *plate = ControlBarPlateForSide( AsciiString( *s ), p );
			CHECK( plate != NULL );
			CHECK( plate->filename != NULL && plate->filename[ 0 ] != 0 );

			/* every piece is a rectangle of the shipped painting, which is drawn across the bottom
				 of the .wnd's 800x600 - so it lives in the bottom quarter and nowhere else */
			CHECK( plate->design.width() > 0 && plate->design.height() > 0 );
			CHECK( plate->design.lo.x >= 0 && plate->design.hi.x <= 800 );
			CHECK( plate->design.lo.y >= 399 && plate->design.hi.y <= 600 );

			// left, middle, right, in that order
			CHECK( plate->design.hi.x > previousRight );
			previousRight = plate->design.hi.x;
		}
	}

	/* a side nobody drew art for gets no plate rather than somebody else's */
	CHECK( ControlBarPlateForSide( AsciiString( "Martians" ), ControlBar::CB_PANEL_LEFT ) == NULL );
	CHECK( ControlBarPlateForSide( AsciiString::TheEmptyString, ControlBar::CB_PANEL_LEFT ) == NULL );
	CHECK( ControlBarPlateForSide( AsciiString( "America" ), ControlBar::CB_PANEL_COUNT ) == NULL );
}

TEST(the_three_panels_are_one_bar_at_4x3_and_pull_apart_on_a_wide_screen)
{
	/* Every window and every plate goes through this one piece of arithmetic, so it is where a
		 misplaced money slot or a smeared cameo would come from.  Two things have to hold: at 4:3 it
		 reproduces what the .wnd loader would have done, which is what makes the three plates slide
		 back into the one strip EA drew; and above 4:3 it scales by one factor for both axes and
		 opens the two gaps rather than stretching to fill them. */
	IRegion2D whole;			// the whole authored bar
	whole.lo.x = 0;
	whole.lo.y = 408;
	whole.hi.x = 800;
	whole.hi.y = 600;

	IRegion2D left, centre, right;
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_LEFT,   &whole, 800, 600, &left ) );
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_CENTER, &whole, 800, 600, &centre ) );
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_RIGHT,  &whole, 800, 600, &right ) );

	// at the authored size all three anchors land on the authored rectangle itself
	CHECK_EQ( left.lo.x, 0 );		CHECK_EQ( left.hi.x, 800 );
	CHECK_EQ( centre.lo.x, 0 );	CHECK_EQ( centre.hi.x, 800 );
	CHECK_EQ( right.lo.x, 0 );	CHECK_EQ( right.hi.x, 800 );
	CHECK_EQ( left.lo.y, 408 );	CHECK_EQ( left.hi.y, 600 );

	/* 1920x1080 is 1.8 times the authored height and 2.4 times its width; the smaller one wins, so
		 a square cameo stays square instead of coming out a third wider than it is tall */
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_LEFT,   &whole, 1920, 1080, &left ) );
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_CENTER, &whole, 1920, 1080, &centre ) );
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_RIGHT,  &whole, 1920, 1080, &right ) );

	CHECK_NEAR( (Real)left.width(), 800.0f * 1.8f, 1.5f );
	CHECK_NEAR( (Real)left.height(), 192.0f * 1.8f, 1.5f );

	// left edge to left edge, right edge to right edge, and the middle one centred between them
	CHECK_EQ( left.lo.x, 0 );
	CHECK_EQ( right.hi.x, 1920 );
	CHECK_NEAR( (Real)( centre.lo.x + centre.hi.x ) * 0.5f, 960.0f, 2.0f );

	// and every panel still sits on the bottom edge of the screen
	CHECK_EQ( left.hi.y, 1080 );
	CHECK_EQ( centre.hi.y, 1080 );
	CHECK_EQ( right.hi.y, 1080 );

	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_COUNT, &whole, 1920, 1080, &left ) == FALSE );
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_LEFT, &whole, 0, 0, &left ) == FALSE );
}

TEST(a_plate_covers_the_windows_its_panel_is_responsible_for)
{
	/* The plates are cuts of the shipped painting and the windows are at the coordinates the .wnd
		 and ControlBarScheme.ini give them, so the two only agree if the plate rectangles are right.
		 These are the windows a player looks at: the radar, the money readout, the power rail, the
		 toolbar column, the command grid and the selection portrait.  A plate that has drifted - and
		 an earlier version of this, fitted by eye instead of to the painting, had drifted about four
		 percent - leaves one of them hanging off its own artwork. */
	struct Seated { const char *what; Int panel; Int x0, y0, x1, y1; Bool closedUnder; };
	static const Seated seated[] =
	{
		{ "radar",							ControlBar::CB_PANEL_LEFT,		  7, 443, 174, 595, TRUE },
		{ "money",							ControlBar::CB_PANEL_CENTER,	360, 437, 439, 462, TRUE },
		{ "power bar",					ControlBar::CB_PANEL_CENTER,	259, 469, 538, 476, TRUE },
		{ "toolbar column",			ControlBar::CB_PANEL_CENTER,	184, 490, 220, 592, TRUE },
		{ "command grid",				ControlBar::CB_PANEL_CENTER,	223, 494, 603, 589, TRUE },
		{ "selection portrait",	ControlBar::CB_PANEL_RIGHT,		621, 483, 760, 592, TRUE },
		{ NULL, 0, 0, 0, 0, 0, FALSE }
	};
	static const char *sides[] = { "America", "China", "GLA", NULL };

	for( const char **s = sides; *s; s++ )
		for( const Seated *w = seated; w->what; w++ )
		{
			const ControlBarPlate *plate = ControlBarPlateForSide( AsciiString( *s ), w->panel );
			CHECK( plate != NULL );

			CHECK( plate->design.lo.x <= w->x0 );
			CHECK( plate->design.hi.x >= w->x1 );
			CHECK( plate->design.lo.y <= w->y0 );

			/* And the bottom edge.  Where a plate is meant to be underneath a window it has to reach
				 that window's own bottom, or the battlefield shows through.  The American centre plate
				 was hung from its top for a while and ended ten units short of the others, which put a
				 band of ground under the command grid. */
			if( w->closedUnder )
				CHECK( plate->design.hi.y >= w->y1 );
		}
}

TEST(a_plates_targa_is_the_shape_its_design_rectangle_says)
{
	/* Each plate carries the size of the painting inside its targa, because the targa is padded out
		 to a power of two: the loader hands D3DXCreateTextureFromFileEx D3DX_DEFAULT for width and
		 height, and that stretches a non-power-of-two image into the bigger texture - which left the
		 texture's last column holding a vertically mirrored copy of the painting, and every quad's
		 last pixel column sampling it.  The UV comes from these two numbers, so a typo in them
		 stretches or crops the plate.  The painting is a cut of the side's bar at one scale, so its
		 texels-per-design-unit is the same across and down; that is what this checks. */
	static const char *sides[] = { "America", "China", "GLA", NULL };

	for( const char **s = sides; *s; s++ )
		for( Int p = 0; p < ControlBar::CB_PANEL_COUNT; p++ )
		{
			const ControlBarPlate *plate = ControlBarPlateForSide( AsciiString( *s ), p );
			CHECK( plate != NULL );
			CHECK( plate->artW > 0 );
			CHECK( plate->artH > 0 );

			const Real acrossScale = (Real)plate->artW / (Real)plate->design.width();
			const Real downScale   = (Real)plate->artH / (Real)plate->design.height();
			CHECK_NEAR( acrossScale, downScale, 0.03f );

			// and a power-of-two texture has room for it
			Int potW = 1, potH = 1;
			while( potW < plate->artW ) potW *= 2;
			while( potH < plate->artH ) potH *= 2;
			CHECK( potW >= plate->artW && potW < plate->artW * 2 );
			CHECK( potH >= plate->artH && potH < plate->artH * 2 );
		}
}

TEST(the_grid_and_the_readout_follow_the_plate_that_paints_them)
{
	/* A plate is its side's own painting rather than a cut of the shipped bar, so the money box and
		 the grid field are where the plate draws them and not where ControlBarScheme.ini and the .wnd
		 put the windows.  On the American centre plate both disagree, and both were measured off the
		 targa: the box's dark interior runs 448.5 to 465.4, middle 456.6, nine below the middle of
		 the 19-unit readout ControlBarScheme.ini places at 438 to 457; the striped field runs 224.6
		 to 614.2 and the fourteen command buttons are 223 to 603, so the block sits six units left
		 of centre in it.  China and GLA paint theirs where the windows already are and shift
		 nothing - GLA's box middle is 451.3 against a readout the scheme puts at 443 to 462.

		 The power bar is the third of them and the same measurement: America paints a dark groove at
		 474.8 to 479.2 and under it the silver tube that reads as the gauge, 479.8 to 482.3, against
		 the 6-unit power window ControlBarScheme.ini places at 470 to 476 - which is the pale ledge
		 above both, and at 1280x720 put the bar's left half on the terrain over the plate's sloping
		 top edge.  Ten lands it on the tube.  China's window covers its own rail from 469 and
		 GLA's from 470, so those two shift nothing here either. */
	struct Expect { const char *side; Int readout; Int power; Int grid; };
	static const Expect expected[] =
	{
		{ "America", 9, 10, 6 }, { "China", 0, 0, 0 }, { "GLA", 0, 0, 0 }, { NULL, 0, 0, 0 }
	};

	// what ControlBarScheme.ini gives the readout and the power bar, and the .wnd the button block
	const Int schemeTop = 438, schemeBottom = 457;
	const Int powerTop = 469, powerBottom = 476;
	const Int gridLeft = 223, gridRight = 603;

	for( const Expect *e = expected; e->side; e++ )
	{
		const ControlBarPlate *plate = ControlBarPlateForSide( AsciiString( e->side ), ControlBar::CB_PANEL_CENTER );
		CHECK( plate != NULL );
		CHECK_EQ( plate->readoutShiftY, e->readout );
		CHECK_EQ( plate->powerShiftY, e->power );
		CHECK_EQ( plate->gridShiftX, e->grid );

		// wherever they end up, all three have to land on the plate rather than off an edge of it -
		// a shift big enough to hang any of them in the battlefield is the failure to catch
		CHECK( schemeTop + plate->readoutShiftY >= plate->design.lo.y );
		CHECK( schemeBottom + plate->readoutShiftY <= plate->design.hi.y );
		CHECK( powerTop + plate->powerShiftY >= plate->design.lo.y );
		CHECK( powerBottom + plate->powerShiftY <= plate->design.hi.y );
		CHECK( gridLeft + plate->gridShiftX >= plate->design.lo.x );
		CHECK( gridRight + plate->gridShiftX <= plate->design.hi.x );
	}

	/* The shift is what puts the bar on the tube: America's window without it ends at 476, above
		 the 479.8 the tube starts at, so the whole bar sat on the pale ledge with the tube standing
		 empty below it.  With the shift the window starts no lower than the tube's top edge and
		 reaches past its bottom, which is the ceiling as well as the floor - one unit further and
		 the bar starts below the rail and reads as sitting on the button field. */
	const ControlBarPlate *am = ControlBarPlateForSide( AsciiString( "America" ), ControlBar::CB_PANEL_CENTER );
	CHECK( am != NULL );
	const Int tubeTop = 480, tubeBottom = 482;		// texels 81..85 of the targa, in design units
	CHECK( 476 < tubeTop );																	// unshifted: entirely above the tube
	CHECK( 470 + am->powerShiftY <= tubeTop );
	CHECK( 476 + am->powerShiftY >= tubeBottom );
}

TEST(the_minimised_panel_stops_on_the_tab_its_plate_paints)
{
	/* Each right-hand plate paints its own minimise tab, bezel and arrow, and none of them paints it
		 where ControlBar.wnd puts ButtonLarge - 666,445 to 714,473.  The button itself lands on the
		 painting from ControlBarScheme.ini's MinMaxUL/LR, which is EA's own per-side rectangle; what
		 the plate carries is the tab as painted, bezel included, because that is where the minimised
		 selection panel has to stop.  The rectangles were measured off the three targas. */
	static const char *const sides[] = { "America", "China", "GLA", NULL };

	// what the .wnd says, which is the thing every one of these has to disagree with
	const Int wndLeft = 666, wndTop = 445, wndRight = 714, wndBottom = 473;
	const Int wndWidth = wndRight - wndLeft, wndHeight = wndBottom - wndTop;

	for( const char *const *s = sides; *s; s++ )
	{
		const ControlBarPlate *plate = ControlBarPlateForSide( AsciiString( *s ), ControlBar::CB_PANEL_RIGHT );
		CHECK( plate != NULL );

		// a tab at all - a zeroed rectangle means the panel drops out of sight entirely
		CHECK( plate->minTab.width() > 0 );
		CHECK( plate->minTab.height() > 0 );

		// it has to be on the plate, not hanging in the battlefield beside it
		CHECK( plate->minTab.lo.x >= plate->design.lo.x );
		CHECK( plate->minTab.hi.x <= plate->design.hi.x );
		CHECK( plate->minTab.lo.y >= plate->design.lo.y );
		CHECK( plate->minTab.hi.y <= plate->design.hi.y );

		// and it has to be a tab-sized thing: a fat-fingered number that leaves half the panel on
		// screen, or nothing of it, is the failure worth catching
		CHECK( plate->minTab.width() >= wndWidth / 2 );
		CHECK( plate->minTab.width() <= wndWidth * 2 );
		CHECK( plate->minTab.height() >= wndHeight / 2 );
		CHECK( plate->minTab.height() <= wndHeight * 2 );

		// every one of the three moved; a rectangle equal to the .wnd's would be the bug back
		CHECK( plate->minTab.lo.x != wndLeft || plate->minTab.lo.y != wndTop );

		//
		// The tab is also what stops the minimised bar: the selection panel drops until this
		// rectangle's bottom edge stands on the bottom of the screen, so the tab and the metal it is
		// painted on stay reachable and the portrait below goes off.  A tab whose bottom is the
		// plate's bottom would give a drop of nothing and the panel would not move at all.
		//
		CHECK( plate->minTab.hi.y < plate->design.hi.y );
	}
}

/* Every button on the options screen wears the game's own face.  One authored in Times New Roman
	 reads as a different program's dialog dropped into the menu, and nothing at build time notices. */
TEST(the_options_menu_buttons_all_wear_one_font)
{
	FILE *fp = fopen( OPTIONS_MENU_WND, "rb" );
	CHECK( fp != NULL );
	if( fp == NULL )
		return;

	// walk the file remembering the last window name seen; FONT comes a few lines after NAME
	static const char *const buttons[] =
	{
		"OptionsMenu.wnd:ButtonBack", "OptionsMenu.wnd:ButtonAccept",
		"OptionsMenu.wnd:ButtonDefaults", NULL
	};

	char line[ 1024 ], name[ 256 ];
	Int found = 0, agreed = 0;
	name[ 0 ] = 0;
	while( fgets( line, sizeof( line ), fp ) != NULL )
	{
		const char *at = strstr( line, "NAME = \"" );
		if( at != NULL )
		{
			at += 8;
			Int i = 0;
			while( at[ i ] && at[ i ] != '"' && i < (Int)sizeof( name ) - 1 )
			{
				name[ i ] = at[ i ];
				++i;
			}
			name[ i ] = 0;
			continue;
		}

		at = strstr( line, "FONT = NAME:" );
		if( at == NULL || name[ 0 ] == 0 )
			continue;

		for( const char *const *b = buttons; *b; b++ )
			if( strcmp( name, *b ) == 0 )
			{
				++found;
				if( strstr( at, "\"Generals\"" ) != NULL && strstr( at, "SIZE: 15" ) != NULL )
					++agreed;
				else
					printf( "    %s wears %s", name, at );
			}
		name[ 0 ] = 0;
	}
	fclose( fp );

	// a scan that matched nothing would pass silently
	CHECK_EQ( found, 3 );
	CHECK_EQ( agreed, found );
}

TEST(a_plates_mask_is_solid_under_its_windows_and_open_where_it_paints_nothing)
{
	/* A click on the bar reaches the world where the plate is transparent, and the mask saying where
		 that is comes out of the targa.  So every tracked targa has to decode, the windows a player
		 clicks have to stand on solid art, and every plate has to have some open texels - a mask read
		 off the wrong channel or upside down fails one of those three. */
	struct Seated { Int panel; Int x, y; };
	static const Seated seated[] =
	{
		{ ControlBar::CB_PANEL_LEFT,		 90, 519 },		// radar
		{ ControlBar::CB_PANEL_CENTER,	413, 541 },		// command grid
		{ ControlBar::CB_PANEL_RIGHT,		690, 537 },		// selection portrait
		{ -1, 0, 0 }
	};
	static const char *const sides[] = { "America", "China", "GLA", NULL };

	for( const char *const *s = sides; *s; s++ )
		for( Int p = 0; p < ControlBar::CB_PANEL_COUNT; p++ )
		{
			const ControlBarPlate *plate = ControlBarPlateForSide( AsciiString( *s ), p );
			CHECK( plate != NULL );

			char path[ _MAX_PATH ];
			sprintf( path, "%s/%s", PLATE_TEXTURE_DIR, plate->filename );
			FILE *fp = fopen( path, "rb" );
			CHECK( fp != NULL );
			if( fp == NULL )
				continue;
			std::vector<UnsignedByte> bytes;
			UnsignedByte chunk[ 4096 ];
			for( size_t got; ( got = fread( chunk, 1, sizeof( chunk ), fp ) ) > 0; )
				bytes.insert( bytes.end(), chunk, chunk + got );
			fclose( fp );

			std::vector<UnsignedByte> mask;
			CHECK( ControlBarPlateMaskFromTarga( &bytes[ 0 ], (Int)bytes.size(), plate->artW, plate->artH, mask ) );
			if( (Int)mask.size() != plate->artW * plate->artH )
				continue;

			Int open = 0;
			for( size_t i = 0; i < mask.size(); i++ )
				open += mask[ i ] ? 0 : 1;
			CHECK( open > 0 );
			CHECK( open < (Int)mask.size() );

			for( const Seated *w = seated; w->panel >= 0; w++ )
			{
				if( w->panel != p )
					continue;
				const Int texelX = ( w->x - plate->design.lo.x ) * plate->artW / plate->design.width();
				const Int texelY = ( w->y - plate->design.lo.y ) * plate->artH / plate->design.height();
				CHECK( mask[ texelY * plate->artW + texelX ] != 0 );
			}

			// and a file cut short is refused rather than read past its end
			std::vector<UnsignedByte> scratch;
			CHECK( ControlBarPlateMaskFromTarga( &bytes[ 0 ], (Int)bytes.size() / 2, plate->artW, plate->artH, scratch ) == FALSE );
		}
}

TEST(every_plate_that_touches_a_design_edge_touches_the_screen_edge)
{
	/* The plate rectangles were measured against the paintings, and a painting stops a pixel or two
		 inside its own edge - so the left plates start at design x 0 or 1, the right ones end at 798,
		 799 or 800, and the bottoms land anywhere from 597 to 599.  Multiplied up by the display
		 scale that is a line of battlefield down the side of the screen and along the bottom, which
		 is what the snap in ControlBarPanelDesignToScreen is for.  Nothing may be short of an edge it
		 is supposed to be standing on. */
	static const char *sides[] = { "America", "China", "GLA", NULL };
	struct Res { Int w, h; };
	static const Res screens[] =
	{
		{ 800, 600 }, { 1024, 768 }, { 1366, 768 }, { 1920, 1080 }, { 2560, 1080 }, { 0, 0 }
	};

	for( const char **s = sides; *s; s++ )
		for( const Res *r = screens; r->w; r++ )
		{
			IRegion2D rect;

			const ControlBarPlate *left = ControlBarPlateForSide( AsciiString( *s ), ControlBar::CB_PANEL_LEFT );
			CHECK( left != NULL );
			CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_LEFT, &left->design, r->w, r->h, &rect ) );
			CHECK_EQ( rect.lo.x, 0 );
			CHECK_EQ( rect.hi.y, r->h );

			const ControlBarPlate *centre = ControlBarPlateForSide( AsciiString( *s ), ControlBar::CB_PANEL_CENTER );
			CHECK( centre != NULL );
			CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_CENTER, &centre->design, r->w, r->h, &rect ) );
			CHECK_EQ( rect.hi.y, r->h );

			const ControlBarPlate *right = ControlBarPlateForSide( AsciiString( *s ), ControlBar::CB_PANEL_RIGHT );
			CHECK( right != NULL );
			CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_RIGHT, &right->design, r->w, r->h, &rect ) );
			CHECK_EQ( rect.hi.x, r->w );
			CHECK_EQ( rect.hi.y, r->h );
		}

	/* the snap is for an edge a rectangle was meant to reach, not for one it deliberately stops
		 short of - the two gaps the three panels open at 16:9 are not edges and stay open */
	IRegion2D middle;
	middle.lo.x = 300;
	middle.lo.y = 500;
	middle.hi.x = 500;
	middle.hi.y = 560;

	IRegion2D rect;
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_LEFT, &middle, 1920, 1080, &rect ) );
	CHECK( rect.lo.x > 0 );
	CHECK( rect.hi.y < 1080 );
	CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_RIGHT, &middle, 1920, 1080, &rect ) );
	CHECK( rect.hi.x < 1920 );
}

/* The command bar is drawn at one scale for both axes and the things that stand beside it - the
	 production queue, the superweapon countdowns, the generals' power bar and the rank screen - are
	 drawn at ControlBarUniformScale.  They used to be measured the display's width over 800, which
	 is a third bigger at 16:9 than the bar they sit on, so a queue cameo came out bigger than the
	 command button it is a picture of.  One scale, and it is the bar's own. */
TEST(the_hud_is_measured_at_the_command_bars_own_scale)
{
	static const struct { Int w, h; } screens[] =
	{
		{ 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1080 }, { 3840, 2160 },
	};

	// a design rectangle 100 units wide and 100 tall, so the width and height of what comes back
	// out of the panel transform *is* the scale it was put through
	IRegion2D square;
	square.lo.x = 300;
	square.lo.y = 480;
	square.hi.x = 400;
	square.hi.y = 580;

	for( Int i = 0; i < (Int)( sizeof( screens ) / sizeof( screens[ 0 ] ) ); i++ )
	{
		const Real s = ControlBarUniformScaleFor( screens[ i ].w, screens[ i ].h );

		// never wider than either axis would allow: that is what "not stretched" means
		CHECK( s <= (Real)screens[ i ].w / 800.0f + 0.001f );
		CHECK( s <= (Real)screens[ i ].h / 600.0f + 0.001f );

		// and it is the very number the command bar's own panels are laid out at
		IRegion2D got;
		CHECK( ControlBarPanelDesignToScreen( ControlBar::CB_PANEL_CENTER, &square,
																					screens[ i ].w, screens[ i ].h, &got ) );
		CHECK_NEAR( (Real)got.width(), 100.0f * s, 2.0f );
		CHECK_NEAR( (Real)got.height(), 100.0f * s, 2.0f );

		// square in, square out, to the rounding - the loader's separate x and y scales are what made
		// a cameo a third wider than it is tall
		CHECK( abs( got.width() - got.height() ) <= 1 );
	}

	// 800x600 is the resolution everything was authored at, so nothing is scaled at all there
	CHECK_NEAR( ControlBarUniformScaleFor( 800, 600 ), 1.0f, 0.001f );

	// and nothing shrinks below it, however small a screen somebody asks for
	CHECK_NEAR( ControlBarUniformScaleFor( 640, 480 ), 1.0f, 0.001f );
	CHECK_NEAR( ControlBarUniformScaleFor( 0, 0 ), 1.0f, 0.001f );
}

/* A health bar is drawn in raw pixels over a tank whose own size on screen is set by the camera,
	 and the camera fills the screen's *height*.  So the bar has to follow the height too.  Measured
	 off the width it was two and a half times too wide on a 32:9 screen: the bar of a barracks
	 reached most of the way across its own base. */
TEST(the_health_bar_grows_with_the_unit_under_it_and_not_with_the_screens_width)
{
	// the same number the command bar is laid out at, whatever the shape of the screen
	static const struct { Int w, h; } screens[] =
	{
		{ 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1080 },
		{ 3440, 1440 }, { 5120, 1440 }, { 3840, 2160 },
	};
	for( Int i = 0; i < (Int)( sizeof( screens ) / sizeof( screens[ 0 ] ) ); i++ )
		CHECK_NEAR( UIScaleForScreen( screens[ i ].w, screens[ i ].h ),
								ControlBarUniformScaleFor( screens[ i ].w, screens[ i ].h ), 0.001f );

	// widening the screen alone moves nothing: the world is not magnified by it either
	CHECK_NEAR( UIScaleForScreen( 1920, 1080 ), UIScaleForScreen( 5120, 1080 ), 0.001f );
	CHECK_NEAR( 2.4f, UIScaleForScreen( 5120, 1440 ), 0.001f );

	// a taller screen does move it, and nothing shrinks below what was authored
	CHECK( UIScaleForScreen( 1920, 1440 ) > UIScaleForScreen( 1920, 1080 ) );
	CHECK_NEAR( 1.0f, UIScaleForScreen( 800, 600 ), 0.001f );
	CHECK_NEAR( 1.0f, UIScaleForScreen( 640, 480 ), 0.001f );
	CHECK_NEAR( 1.0f, UIScaleForScreen( 0, 0 ), 0.001f );
}

/* A build order is a message, and the structure it orders does not exist until the logic runs it -
	 on a network game, however long the link takes.  Until then the client remembers the ground it
	 spent, so a shift-held run of clicks does not put two structures on the same square.  What it
	 remembers is the footprint, and two footprints that merely touch are two structures built flush
	 against each other, which is most of a base wall. */
TEST(two_structures_ordered_onto_the_same_ground_are_one_too_many)
{
	Region2D a, b;

	a.lo.x = 0.0f;   a.lo.y = 0.0f;   a.hi.x = 40.0f;  a.hi.y = 40.0f;

	// itself, obviously
	CHECK( InGameUI::footprintsOverlap( &a, &a ) );

	// a corner inside it counts, from either side
	b.lo.x = 39.0f;  b.lo.y = 39.0f;  b.hi.x = 79.0f;  b.hi.y = 79.0f;
	CHECK( InGameUI::footprintsOverlap( &a, &b ) );
	CHECK( InGameUI::footprintsOverlap( &b, &a ) );

	// flush against it does not - a row of buildings on the build grid is not an overlap
	b.lo.x = 40.0f;  b.lo.y = 0.0f;   b.hi.x = 80.0f;  b.hi.y = 40.0f;
	CHECK( !InGameUI::footprintsOverlap( &a, &b ) );
	CHECK( !InGameUI::footprintsOverlap( &b, &a ) );

	// nor does clear of it, on either axis alone
	b.lo.x = 10.0f;  b.lo.y = 41.0f;  b.hi.x = 30.0f;  b.hi.y = 60.0f;
	CHECK( !InGameUI::footprintsOverlap( &a, &b ) );
	b.lo.x = 41.0f;  b.lo.y = 10.0f;  b.hi.x = 60.0f;  b.hi.y = 30.0f;
	CHECK( !InGameUI::footprintsOverlap( &a, &b ) );

	// one wholly inside another - a small structure ordered into a big one's middle
	b.lo.x = 10.0f;  b.lo.y = 10.0f;  b.hi.x = 20.0f;  b.hi.y = 20.0f;
	CHECK( InGameUI::footprintsOverlap( &a, &b ) );
	CHECK( InGameUI::footprintsOverlap( &b, &a ) );

	//
	// and the window it is remembered for has to outlast a bad link: a network game runs the order
	// a handful of frames after the click, and two seconds of logic frames is a long link
	//
	CHECK( (Int)InGameUI::PENDING_PLACEMENT_FRAMES >= 30 );
	CHECK( (Int)InGameUI::PENDING_PLACEMENTS >= 2 );
}

TEST(option_catalog_rows_are_well_formed)
{
	/* The catalog is a table, so the mistakes it invites are table mistakes: two rows claiming the
		 same Options.ini key, a row whose bounds are the wrong way round, a row that forgot half of
		 its accessor pair.  None of those is a compile error and all three fail silently at runtime -
		 the duplicate key means whichever row is written second wins and the first setting appears to
		 forget itself every time the player presses Accept. */
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];

		CHECK( def.iniKey != NULL && def.iniKey[ 0 ] != '\0' );
		CHECK( def.get != NULL );
		CHECK( def.set != NULL );
		CHECK( def.hi >= def.lo );

		if( def.kind == OPTION_BOOL )
		{
			CHECK_EQ( def.lo, 0 );
			CHECK_EQ( def.hi, 1 );
		}

		/* A widget name is a NAMEKEY the menu looks up, and a lookup that misses returns NULL rather
			 than complaining, so a typo here is a control that never fills in.  A row with no control
			 at all spells that as NULL or as the empty string, and findOptionWidget takes either. */
		if( def.widgetName != NULL && def.widgetName[ 0 ] != '\0' )
			CHECK( strncmp( def.widgetName, "OptionsMenu.wnd:", 16 ) == 0 );

		for( Int j = 0; j < i; ++j )
			CHECK_NE( stricmp( TheOptionCatalog[ j ].iniKey, def.iniKey ), 0 );
	}

	/* the table is terminated as well as counted, so a walk may use either */
	CHECK( TheOptionCatalog[ TheOptionCatalogCount ].iniKey == NULL );
}

TEST(string_file_bytes_decode_utf8_and_keep_latin1)
{
	/* A translation .str is UTF-8 and EA's own .str files are ASCII with the odd Latin-1 byte.  Before
		 the decoder every byte became the character of the same number, so "Kışla" drew as "KÄ±ÅŸla".
		 Both kinds of file go through the same function, so both halves are checked: a well-formed
		 sequence becomes its character, and a byte that does not start one stays what it was. */
	WideChar decoded = 0;

	const unsigned char plain[] = "K";
	CHECK_EQ( decodeStringFileCharacter( plain, &decoded ), 1 );
	CHECK_EQ( (Int)decoded, (Int)L'K' );

	const unsigned char dotlessI[] = { 0xC4, 0xB1, 0 };						// U+0131, the ı in Kışla
	CHECK_EQ( decodeStringFileCharacter( dotlessI, &decoded ), 2 );
	CHECK_EQ( (Int)decoded, 0x0131 );

	const unsigned char euro[] = { 0xE2, 0x82, 0xAC, 0 };					// U+20AC, three bytes
	CHECK_EQ( decodeStringFileCharacter( euro, &decoded ), 3 );
	CHECK_EQ( (Int)decoded, 0x20AC );

	const unsigned char latin1Word[] = { 0xE9, 't', 0 };					// a Latin-1 é, not a lead byte here
	CHECK_EQ( decodeStringFileCharacter( latin1Word, &decoded ), 1 );
	CHECK_EQ( (Int)decoded, 0x00E9 );

	const unsigned char truncated[] = { 0xC4, 0 };								// a lead byte at the very end
	CHECK_EQ( decodeStringFileCharacter( truncated, &decoded ), 1 );
	CHECK_EQ( (Int)decoded, 0x00C4 );

	const unsigned char overlong[] = { 0xE0, 0x80, 0xAF, 0 };			// '/' spelled in three bytes
	CHECK_EQ( decodeStringFileCharacter( overlong, &decoded ), 1 );
	CHECK_EQ( (Int)decoded, 0x00E0 );
}

TEST(text_language_row_offers_every_language)
{
	/* The menu builds one combo entry per value up to hi and GameText indexes its overlay table with
		 the same number, so the row and the enum have to end in the same place. */
	const OptionDef *language = findOptionDef( "TextLanguage" );
	CHECK( language != NULL );
	if( language == NULL )
		return;
	CHECK_EQ( language->kind, OPTION_ENUM );
	CHECK_EQ( language->lo, (Int)TEXT_LANGUAGE_ENGLISH );
	CHECK_EQ( language->hi, (Int)TEXT_LANGUAGE_COUNT - 1 );
}

TEST(gameplay_conveniences_are_forced_on_and_left_the_catalog)
{
	/* The Gameplay page is one control now, health bars, and the eight settings that used to share
		 it are decided here instead of by the player.  Two halves have to agree or the removal is a
		 feature switched off by accident: the row must be gone from the catalog, so nothing loads a
		 stale "no" out of an Options.ini written before this change, and the constructor must say
		 TRUE, because with the row gone the constructor is the only thing left that says anything. */
	static const char *const forced[] =
	{
		"GridBuildPlacement", "NudgeBuildPlacement", "SnapBuildPlacementTo45",
		"ShowPlacementRangeRing", "WorkersReturnToSupply", "DetailedBuildTooltips",
		"ShowHudOverlay", "ArchiveReplays", NULL
	};
	for( Int i = 0; forced[ i ] != NULL; ++i )
		CHECK( findOptionDef( forced[ i ] ) == NULL );

	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	CHECK( scratch->m_gridBuildPlacement );
	CHECK( scratch->m_nudgeBuildPlacement );
	CHECK( scratch->m_snapBuildPlacementTo45 );
	CHECK( scratch->m_showPlacementRangeRing );
	CHECK( scratch->m_workersReturnToSupply );
	CHECK( scratch->m_detailedBuildTooltips );
	CHECK( scratch->m_showHudOverlay );
	CHECK( scratch->m_archiveReplays );

	/* HealthBars is what is left, and it is still a menu row. */
	const OptionDef *bars = findOptionDef( "HealthBars" );
	CHECK( bars != NULL );
	CHECK( bars->widgetName != NULL && bars->widgetName[ 0 ] != '\0' );

	/* The camera and mouse habits went the other way: no control, but the row stays, so an
		 Options.ini that names one still wins over the default.  ZoomToCursor came back to the menu. */
	static const char *const hidden[] =
	{
		"FormationDrag", "EdgeScrollInWindowedMode", "SnapCameraRotateTo45", NULL
	};
	for( Int i = 0; hidden[ i ] != NULL; ++i )
	{
		const OptionDef *def = findOptionDef( hidden[ i ] );
		CHECK( def != NULL );
		CHECK( def->widgetName == NULL || def->widgetName[ 0 ] == '\0' );
	}

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(the_input_scheme_is_a_live_menu_choice_that_starts_modern)
{
	/* Legacy is read on every click and key, so the row has to say APPLY_LIVE - a restart note on a
		 setting that already took would send the player looking for a change that is not coming.  A
		 player who never opens the menu keeps the game they already had. */
	const OptionDef *def = findOptionDef( "InputScheme" );
	CHECK( def != NULL );
	if( def == NULL )
		return;
	CHECK_EQ( def->kind, OPTION_ENUM );
	CHECK_EQ( def->apply, APPLY_LIVE );
	CHECK_EQ( def->lo, 0 );
	CHECK_EQ( def->hi, (Int)INPUT_SCHEME_COUNT - 1 );
	CHECK( def->widgetName != NULL && def->widgetName[ 0 ] != '\0' );

	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	CHECK_EQ( scratch->m_inputScheme, (Int)INPUT_SCHEME_MODERN );
	CHECK( !scratch->isLegacyInput() );
	def->set( INPUT_SCHEME_LEGACY );
	CHECK( scratch->isLegacyInput() );
	CHECK_EQ( def->get(), (Int)INPUT_SCHEME_LEGACY );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(wasd_camera_is_a_live_check_box_that_starts_off_and_needs_modern_input)
{
	/* The box moves eleven keys a player already has in their hands, so nobody gets it without asking.
		 A tick saved under Modern is kept through a switch to Legacy, which plays the game's own map,
		 and comes back with Modern. */
	const OptionDef *def = findOptionDef( "WasdCamera" );
	CHECK( def != NULL );
	if( def == NULL )
		return;
	CHECK_EQ( def->kind, OPTION_BOOL );
	CHECK_EQ( def->apply, APPLY_LIVE );
	CHECK( def->widgetName != NULL && def->widgetName[ 0 ] != '\0' );

	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	CHECK( !scratch->m_wasdCamera );
	CHECK( !scratch->isWasdCamera() );
	def->set( 1 );
	CHECK( scratch->isWasdCamera() );
	scratch->m_inputScheme = INPUT_SCHEME_LEGACY;
	CHECK( !scratch->isWasdCamera() );
	CHECK_EQ( def->get(), 1 );
	scratch->m_inputScheme = INPUT_SCHEME_MODERN;
	CHECK( scratch->isWasdCamera() );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(order_lines_are_a_live_check_box_that_starts_on)
{
	/* The lines are rebuilt from the units every frame, so the box takes effect at Accept, and a player
		 who never opens the menu keeps the lines the game has drawn since they were added. */
	const OptionDef *def = findOptionDef( "OrderLines" );
	CHECK( def != NULL );
	if( def == NULL )
		return;
	CHECK_EQ( def->kind, OPTION_BOOL );
	CHECK_EQ( def->apply, APPLY_LIVE );
	CHECK( def->widgetName != NULL && def->widgetName[ 0 ] != '\0' );

	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	CHECK( scratch->m_showOrderLines );
	def->set( 0 );
	CHECK( !scratch->m_showOrderLines );
	CHECK_EQ( def->get(), 0 );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(option_catalog_round_trips_every_key_through_options_ini)
{
	/* Every row has to survive the trip out to Options.ini and back, at both ends of its range.
		 Before the catalog only the way in existed - the fourteen fork settings were parsed at startup
		 and nothing ever wrote them - so the writing half has never been exercised by anything.  A row
		 that writes one spelling and reads another loses the setting on the next start, and does it
		 without a word. */
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	UserPreferences pref;

	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];

		def.set( def.lo );
		saveOptionsToPreferences( pref );
		def.set( def.hi );	// scribble over it, so a load that does nothing cannot pass
		loadOptionsFromPreferences( pref );
		CHECK_EQ( def.get(), def.lo );

		def.set( def.hi );
		saveOptionsToPreferences( pref );
		def.set( def.lo );
		loadOptionsFromPreferences( pref );
		CHECK_EQ( def.get(), def.hi );
	}

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(option_catalog_writes_bools_as_yes_and_no)
{
	/* UserPreferences::setBool writes "1" and "0"; every option key this fork ever added is read by
		 testing the string against "yes".  A catalog that reached for setBool would write a file it
		 could not read back, and the setting would come back off on the next start. */
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	UserPreferences pref;
	const OptionDef *zoom = findOptionDef( "ZoomToCursor" );
	CHECK( zoom != NULL );

	zoom->set( 1 );
	saveOptionsToPreferences( pref );
	CHECK_STR( pref[ AsciiString( "ZoomToCursor" ) ].str(), "yes" );

	zoom->set( 0 );
	saveOptionsToPreferences( pref );
	CHECK_STR( pref[ AsciiString( "ZoomToCursor" ) ].str(), "no" );

	/* Reading is deliberately more forgiving than writing.  The getters this replaces accepted the
		 single string "yes", so a file hand-edited to "true" read as off - which looks like the
		 setting not working rather than like the file being spelled wrong. */
	pref[ AsciiString( "ZoomToCursor" ) ] = AsciiString( "true" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( zoom->get(), 1 );

	zoom->set( 0 );
	pref[ AsciiString( "ZoomToCursor" ) ] = AsciiString( "1" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( zoom->get(), 1 );

	pref[ AsciiString( "ZoomToCursor" ) ] = AsciiString( "no" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( zoom->get(), 0 );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(option_catalog_clamps_and_leaves_an_absent_key_alone)
{
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	UserPreferences pref;
	const OptionDef *speed = findOptionDef( "MenuTransitionSpeed" );
	CHECK( speed != NULL );

	pref[ AsciiString( "MenuTransitionSpeed" ) ] = AsciiString( "5000" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( speed->get(), speed->hi );

	pref[ AsciiString( "MenuTransitionSpeed" ) ] = AsciiString( "-5" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( speed->get(), speed->lo );

	/* An Options.ini written before a setting existed carries no key for it, and that has to leave
		 the GameData.ini default standing.  Treating a missing key as zero would reset every new
		 setting to off for everyone who already has a preferences file, which is everyone. */
	pref.clear();
	speed->set( 142 );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( speed->get(), 142 );

	TheWritableGlobalData = saved;
	delete scratch;
}

/** Bloom is picked as a level and stored as one, and the shader still reads the percentage it
	 always read.  The mapping is the whole setting: get the two directions out of step and the combo
	 box shows one thing while the screen does another, which nothing at build time would notice. */
TEST(bloom_levels_carry_the_percentages_the_shader_reads)
{
	const OptionDef *bloom = findOptionDef( "Bloom" );
	const OptionDef *threshold = findOptionDef( "BloomThreshold" );
	CHECK( bloom != NULL && threshold != NULL );
	CHECK_EQ( (Int)bloom->kind, (Int)OPTION_ENUM );
	CHECK_EQ( (Int)threshold->kind, (Int)OPTION_ENUM );
	CHECK_EQ( bloom->hi, BLOOM_LEVEL_COUNT - 1 );
	CHECK_EQ( threshold->hi, BLOOM_THRESHOLD_LEVEL_COUNT - 1 );

	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	// what GlobalData's constructor put there, before anything below scribbles on it
	const Int shippedIntensity = TheGlobalData->m_bloomIntensity;
	const Int shippedThreshold = TheGlobalData->m_bloomThreshold;

	// off is off, and nothing else is: a level that mapped to 0 would be a silent second Off entry
	bloom->set( 0 );
	CHECK_EQ( TheGlobalData->m_bloomIntensity, 0 );
	for( Int level = 1; level < BLOOM_LEVEL_COUNT; ++level )
	{
		bloom->set( level );
		CHECK( TheGlobalData->m_bloomIntensity > 0 );
		CHECK_EQ( bloom->get(), level );
	}

	// each entry is stronger than the one above it, so the list reads in the order it is drawn
	Int previous = -1;
	for( Int level = 0; level < BLOOM_LEVEL_COUNT; ++level )
	{
		bloom->set( level );
		CHECK( TheGlobalData->m_bloomIntensity > previous );
		previous = TheGlobalData->m_bloomIntensity;
	}

	/* The threshold is a brightness, so it runs the other way: later entries mean more of the
		 picture glows, which is a lower number.  This is the pair that was worth a dropdown. */
	previous = 101;
	for( Int level = 0; level < BLOOM_THRESHOLD_LEVEL_COUNT; ++level )
	{
		threshold->set( level );
		CHECK( TheGlobalData->m_bloomThreshold < previous );
		previous = TheGlobalData->m_bloomThreshold;
		CHECK_EQ( threshold->get(), level );
	}

	/* GameData.ini writes these fields as raw percentages and never learns about levels, so the
		 combo box has to answer with the nearest entry rather than the first one. */
	TheWritableGlobalData->m_bloomIntensity = 62;
	CHECK_EQ( bloom->get(), 2 );
	TheWritableGlobalData->m_bloomThreshold = 66;
	CHECK_EQ( threshold->get(), 1 );

	/* The shipped default has to be one of the entries, not something between two of them.  Opening
		 the options screen and pressing Accept without touching anything must leave the picture where
		 it was, and it only does that if the default round trips through a level exactly. */
	TheWritableGlobalData->m_bloomIntensity = shippedIntensity;
	TheWritableGlobalData->m_bloomThreshold = shippedThreshold;
	bloom->set( bloom->get() );
	threshold->set( threshold->get() );
	CHECK_EQ( TheGlobalData->m_bloomIntensity, shippedIntensity );
	CHECK_EQ( TheGlobalData->m_bloomThreshold, shippedThreshold );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(msaa_levels_map_to_the_counts_a_device_offers)
{
	CHECK_EQ( (Int)msaaSamplesForLevel( 0 ), 0 );
	CHECK_EQ( (Int)msaaSamplesForLevel( 1 ), 2 );
	CHECK_EQ( (Int)msaaSamplesForLevel( 2 ), 4 );
	CHECK_EQ( (Int)msaaSamplesForLevel( 3 ), 8 );
	CHECK_EQ( (Int)msaaSamplesForLevel( 4 ), 16 );

	// out of range is off, not a read past the table
	CHECK_EQ( (Int)msaaSamplesForLevel( -1 ), 0 );
	CHECK_EQ( (Int)msaaSamplesForLevel( OPTION_MSAA_LEVEL_COUNT ), 0 );

	for( Int level = 0; level < OPTION_MSAA_LEVEL_COUNT; ++level )
		CHECK_EQ( msaaLevelForSamples( msaaSamplesForLevel( level ) ), level );

	// "-msaa 1" is not multisampling, and a count between two levels rounds down rather than up:
	// asking for 6 and being given 8 costs bandwidth nobody asked for
	CHECK_EQ( msaaLevelForSamples( 0 ), 0 );
	CHECK_EQ( msaaLevelForSamples( 1 ), 0 );
	CHECK_EQ( msaaLevelForSamples( 6 ), 2 );
	CHECK_EQ( msaaLevelForSamples( 64 ), OPTION_MSAA_LEVEL_COUNT - 1 );
}

TEST(vsync_is_off_until_the_player_asks)
{
	const OptionDef *vsync = findOptionDef( "VSync" );
	CHECK( vsync != NULL );
	CHECK_EQ( (Int)vsync->kind, (Int)OPTION_BOOL );
	CHECK_EQ( (Int)vsync->apply, (Int)APPLY_DEVICE_RESET );
	CHECK( vsync->widgetName != NULL && strstr( vsync->widgetName, "CheckVSync" ) != NULL );

	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	CHECK_EQ( scratch->m_vsync, FALSE );

	vsync->set( 1 );
	CHECK_EQ( TheGlobalData->m_vsync, TRUE );
	vsync->set( 0 );
	CHECK_EQ( TheGlobalData->m_vsync, FALSE );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(texture_filter_defaults_to_anisotropic)
{
	const OptionDef *filter = findOptionDef( "TextureFilter" );
	CHECK( filter != NULL );
	// a combo box on the Graphics page now; Options.ini still stores the same 0..2
	CHECK_EQ( (Int)filter->kind, (Int)OPTION_ENUM );
	CHECK_EQ( filter->lo, 0 );
	CHECK_EQ( filter->hi, 2 );
	CHECK( filter->widgetName != NULL && strstr( filter->widgetName, "ComboBoxTextureFilter" ) != NULL );

	const OptionDef *aniso = findOptionDef( "Anisotropy" );
	CHECK( aniso != NULL );
	CHECK_EQ( aniso->lo, 0 );
	CHECK_EQ( aniso->hi, 16 );
	CHECK( aniso->widgetName != NULL && strstr( aniso->widgetName, "SliderAnisotropy" ) != NULL );

	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	// 2 is anisotropic. 0 anisotropy is "whatever the card offers", not a downgrade to off.
	CHECK_EQ( scratch->m_textureFilterMode, 2 );
	CHECK_EQ( scratch->m_anisotropyLevel, 0 );
	CHECK_EQ( scratch->m_vsync, FALSE );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(high_static_lod_keeps_the_picture_settings)
{
	StaticGameLODInfo high;
	CHECK_EQ( high.m_maxParticleCount, 2500 );
	CHECK_EQ( (Int)high.m_useShadowVolumes, 1 );
	CHECK_EQ( (Int)high.m_useShadowDecals, 1 );
	CHECK_EQ( (Int)high.m_useTrees, 1 );
	CHECK_EQ( (Int)high.m_useHeatEffects, 1 );
	CHECK_EQ( high.m_textureReduction, 0 );
}

/* Ultra sits between High and Custom, so a comparison against a level still reads High as the lower
	 of the two, and Custom keeps its place as the last entry every table is sized by. */
TEST(ultra_static_lod_is_high_with_the_ceiling_raised)
{
	GameLODManager manager;
	CHECK_EQ( manager.getStaticGameLODIndex( "Ultra" ), (Int)STATIC_GAME_LOD_ULTRA );
	CHECK_STR( manager.getStaticGameLODLevelName( STATIC_GAME_LOD_CUSTOM ), "Custom" );
	CHECK( STATIC_GAME_LOD_HIGH < STATIC_GAME_LOD_ULTRA );
	CHECK( STATIC_GAME_LOD_ULTRA < STATIC_GAME_LOD_CUSTOM );

	StaticGameLODInfo high;
	high.m_maxParticleCount = 3000;
	high.m_maxTankTrackFadeDelay = 60000;
	high.m_useHeatEffects = FALSE;
	const StaticGameLODInfo ultra = makeUltraStaticGameLOD( high );
	CHECK_EQ( ultra.m_maxParticleCount, 5000 );
	CHECK_EQ( (Int)ultra.m_enableDynamicLOD, 0 );
	CHECK_EQ( (Int)ultra.m_useHeatEffects, 1 );
	// what Ultra has no opinion about is High's
	CHECK_EQ( ultra.m_maxTankTrackFadeDelay, 60000 );
}

TEST(effects_page_rows_reach_the_fields_the_command_line_switches_set)
{
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	static const char *const shadows[] =
	{
		"UseShadowVolumesForSkins", "ShadowsForProjectiles", "ShadowsForProps", "ShadowsForParticles",
	};
	for( Int i = 0; i < (Int)( sizeof( shadows ) / sizeof( shadows[ 0 ] ) ); ++i )
	{
		const OptionDef *def = findOptionDef( shadows[ i ] );
		CHECK( def != NULL );
		CHECK_EQ( (Int)def->kind, (Int)OPTION_BOOL );
		// on until the player says otherwise, as GameData.ini had them
		CHECK_EQ( def->get(), 1 );
		def->set( 0 );
		CHECK_EQ( def->get(), 0 );
	}
	CHECK_EQ( (Int)scratch->m_shadowsForProps, 0 );

	const OptionDef *smoke = findOptionDef( "Smoke" );
	CHECK( smoke != NULL );
	CHECK_EQ( (Int)smoke->apply, (Int)APPLY_RESTART );
	CHECK_EQ( smoke->hi, SMOKE_LEVEL_COUNT - 1 );
	CHECK_EQ( smoke->get(), 0 );
	smoke->set( 2 );
	CHECK_NEAR( scratch->m_smokeThickness, 4.0f, 0.001f );
	CHECK_EQ( smoke->get(), 2 );
	// a bare -smoke is 2, which the box shows as Thick
	scratch->m_smokeThickness = 2.0f;
	CHECK_EQ( smoke->get(), 1 );

	const OptionDef *bounce = findOptionDef( "ParticleBounce" );
	CHECK( bounce != NULL );
	CHECK_EQ( (Int)bounce->apply, (Int)APPLY_RESTART );
	bounce->set( 1 );
	CHECK_EQ( (Int)scratch->m_particleGroundBounce, 1 );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(window_mode_derives_the_boolean_the_device_layer_reads)
{
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	scratch->m_xResolution = 800;
	scratch->m_yResolution = 600;
	scratch->m_edgeScrollInWindowedMode = FALSE;

	scratch->m_windowMode = WINDOW_MODE_FULLSCREEN;
	applyWindowMode();
	CHECK_EQ( (Int)TheGlobalData->m_windowed, 0 );
	CHECK_EQ( TheGlobalData->m_xResolution, 800 );

	scratch->m_windowMode = WINDOW_MODE_WINDOWED;
	applyWindowMode();
	CHECK_EQ( (Int)TheGlobalData->m_windowed, 1 );
	// an ordinary window keeps the resolution the player chose
	CHECK_EQ( TheGlobalData->m_xResolution, 800 );
	CHECK_EQ( (Int)TheGlobalData->m_edgeScrollInWindowedMode, 0 );

	/* Borderless is the one mode that decides the resolution for itself: a window covering the
		 display with a back buffer of any other size is a stretched blit, and the cursor then stops
		 landing where it is drawn. */
	scratch->m_windowMode = WINDOW_MODE_BORDERLESS;
	applyWindowMode();
	CHECK_EQ( (Int)TheGlobalData->m_windowed, 1 );
	CHECK_EQ( TheGlobalData->m_xResolution, (Int)::GetSystemMetrics( SM_CXSCREEN ) );
	CHECK_EQ( TheGlobalData->m_yResolution, (Int)::GetSystemMetrics( SM_CYSCREEN ) );
	CHECK_EQ( (Int)TheGlobalData->m_edgeScrollInWindowedMode, 1 );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(window_mode_survives_a_round_trip_through_options_ini)
{
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	UserPreferences pref;
	const OptionDef *mode = findOptionDef( "WindowMode" );
	CHECK( mode != NULL );
	CHECK_EQ( (Int)mode->kind, (Int)OPTION_ENUM );
	// changing it while the game is up restyles the window and rebuilds the device; it is only the
	// style the window is *born* with that WinMain reads out of Options.ini before the engine exists
	CHECK_EQ( (Int)mode->apply, (Int)APPLY_DEVICE_RESET );
	CHECK_EQ( mode->hi, (Int)WINDOW_MODE_COUNT - 1 );

	mode->set( WINDOW_MODE_BORDERLESS );
	saveOptionsToPreferences( pref );
	// written as the plain number WinMain's early reader parses, not as a word
	CHECK_STR( pref[ AsciiString( "WindowMode" ) ].str(), "1" );

	mode->set( WINDOW_MODE_FULLSCREEN );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( mode->get(), (Int)WINDOW_MODE_BORDERLESS );

	// a hand-edited nonsense value cannot produce a mode the window code has no case for
	pref[ AsciiString( "WindowMode" ) ] = AsciiString( "99" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( mode->get(), (Int)WINDOW_MODE_COUNT - 1 );

	TheWritableGlobalData = saved;
	delete scratch;
}

TEST(early_options_reads_the_same_file_userpreferences_writes)
{
	/* WinMain reads Options.ini itself, before the engine exists, because the window style is fixed
		 by CreateWindow.  That reader shares no code with UserPreferences, so what it has to agree on
		 is the format - and this feeds it exactly what UserPreferences::write produces. */
	UserPreferences pref;
	pref[ AsciiString( "WindowMode" ) ] = AsciiString( "1" );
	pref[ AsciiString( "MSAA" ) ] = AsciiString( "2" );
	pref[ AsciiString( "WindowModeExtra" ) ] = AsciiString( "7" );

	FILE *fp = ::tmpfile();
	CHECK( fp != NULL );
	for( UserPreferences::const_iterator it = pref.begin(); it != pref.end(); ++it )
		::fprintf( fp, "%s = %s\n", it->first.str(), it->second.str() );

	char value[64];

	::rewind( fp );
	CHECK( findEarlyOptionValueIn( fp, "WindowMode", value, sizeof( value ) ) );
	CHECK_STR( value, "1" );

	::rewind( fp );
	CHECK( findEarlyOptionValueIn( fp, "MSAA", value, sizeof( value ) ) );
	CHECK_STR( value, "2" );

	// a key that is not there leaves the caller on its default
	::rewind( fp );
	CHECK( !findEarlyOptionValueIn( fp, "Bloom", value, sizeof( value ) ) );

	::fclose( fp );

	/* The prefix trap: WindowModeExtra starts with WindowMode, and a reader that only compared the
		 first n characters would hand WinMain a 7 and open a window in a mode that does not exist. */
	fp = ::tmpfile();
	CHECK( fp != NULL );
	::fprintf( fp, "WindowModeExtra = 7\n" );
	::rewind( fp );
	CHECK( !findEarlyOptionValueIn( fp, "WindowMode", value, sizeof( value ) ) );
	::fclose( fp );

	// leading and trailing whitespace, and a duplicated key, which only a hand-edited file has
	fp = ::tmpfile();
	CHECK( fp != NULL );
	::fprintf( fp, "\tWindowMode\t=\t2\t\r\nWindowMode = 1\n" );
	::rewind( fp );
	CHECK( findEarlyOptionValueIn( fp, "WindowMode", value, sizeof( value ) ) );
	CHECK_STR( value, "1" );	// last one wins, the way the engine's own loader resolves it
	::fclose( fp );
}

/** The tab strip's hit test.  Nothing in the shipped game uses the tab control, so its arithmetic
	 has never run against a real click; the settings screen is the first layout to use it. */
TEST(a_click_on_the_tab_strip_names_the_tab_under_it)
{
	// three 100-wide tabs: the boundaries are where a tab starts, not where the last one ended
	CHECK_EQ( GadgetTabControl_tabAtOffset( 0, 100, 3 ), 0 );
	CHECK_EQ( GadgetTabControl_tabAtOffset( 99, 100, 3 ), 0 );
	CHECK_EQ( GadgetTabControl_tabAtOffset( 100, 100, 3 ), 1 );
	CHECK_EQ( GadgetTabControl_tabAtOffset( 299, 100, 3 ), 2 );
}

/** The caller tests the click against the strip with >= and <=, so the pixel column one past the
	 last tab is inside the strip and divided out to tabCount - one off the end of subPaneDisabled,
	 which is read before anything checks it. */
TEST(the_pixel_past_the_last_tab_is_no_tab_at_all)
{
	CHECK_EQ( GadgetTabControl_tabAtOffset( 300, 100, 3 ), -1 );
	CHECK_EQ( GadgetTabControl_tabAtOffset( 100000, 100, 3 ), -1 );

	// and a click that somehow arrives left of the strip is not tab zero by wrapping
	CHECK_EQ( GadgetTabControl_tabAtOffset( -1, 100, 3 ), -1 );
}

/** A .wnd that never set the tab size divided by it.  A .wnd that declares more tabs than the array
	 holds indexed past it.  Both are data, and data is what a mod hands the engine. */
TEST(a_tab_control_with_no_size_or_too_many_tabs_answers_nothing)
{
	CHECK_EQ( GadgetTabControl_tabAtOffset( 40, 0, 3 ), -1 );
	CHECK_EQ( GadgetTabControl_tabAtOffset( 40, -100, 3 ), -1 );
	CHECK_EQ( GadgetTabControl_tabAtOffset( 40, 100, 0 ), -1 );

	// twenty declared tabs, eight panes: the ninth onwards cannot be clicked
	CHECK_EQ( GadgetTabControl_tabAtOffset( 100 * (NUM_TAB_PANES - 1), 100, 20 ), NUM_TAB_PANES - 1 );
	CHECK_EQ( GadgetTabControl_tabAtOffset( 100 * NUM_TAB_PANES, 100, 20 ), -1 );
}

//----------------------------------------------------------------------------------------------------------
// The flow model's three charges.
//
// Each of them is a small piece of integer arithmetic that runs a few million times a frame and
// that nothing else in the game can be used to check.  What matters about all three is the same
// thing: open ground is free, the charge is bounded, and the bound is a stated multiple of a step.
// A version that gets the bound wrong does not look wrong - it looks like the pathfinder got slow.
//----------------------------------------------------------------------------------------------------------

/** Twelve cells from anything, a body pays nothing for the ground it is on.  This is the case that
	 covers most of most maps, and a charge here would be a tax on every route in the game. */
TEST(open_ground_costs_a_route_nothing)
{
	const Int need = PF_CLEARANCE_ORTHO*1 + PF_CLEARANCE_ORTHO/2;		// a one-cell body
	CHECK_EQ( Pathfinder_wallHugCost( 10, PF_CLEARANCE_MAX, need ), 0 );
	CHECK_EQ( Pathfinder_wallHugCost( 14, PF_CLEARANCE_MAX, need ), 0 );

	// and nothing is charged at or past the edge of the band.  Just inside it the charge is real
	// but rounds away against a ten-unit step, which is the intent: the band tapers to nothing
	// rather than stepping off a cliff, so a route is not pulled about by its outer edge.
	CHECK_EQ( Pathfinder_wallHugCost( 10, need + PF_CLEARANCE_SOFT, need ), 0 );
	CHECK_EQ( Pathfinder_wallHugCost( 10, need + PF_CLEARANCE_SOFT - 1, need ), 0 );
	CHECK( Pathfinder_wallHugCost( 10, need + PF_CLEARANCE_SOFT/2, need ) > 0 );
}

/** A cell exactly as tight as the body costs PF_WALLHUG_NUM/DEN of a step and never more, however
	 far into the terrain the cell is.  Unbounded is what sends the search round the whole map. */
TEST(wall_hugging_is_capped_at_its_stated_multiple)
{
	const Int need = PF_CLEARANCE_ORTHO*2 + PF_CLEARANCE_ORTHO/2;		// a two-cell body
	const Int peak = (10 * PF_WALLHUG_NUM) / PF_WALLHUG_DEN;
	CHECK_EQ( Pathfinder_wallHugCost( 10, need, need ), peak );
	CHECK_EQ( Pathfinder_wallHugCost( 10, 0, need ), peak );					// inside a wall: no worse
	CHECK_EQ( Pathfinder_wallHugCost( 10, need - 99, need ), peak );	// and negative slack does not wrap

	// a diagonal step is charged in proportion to itself
	CHECK_EQ( Pathfinder_wallHugCost( 14, need, need ), (14 * PF_WALLHUG_NUM) / PF_WALLHUG_DEN );
}

/** Quadratic, not linear: half way into the band costs about a quarter of the peak.  A linear
	 falloff was the first version of this and it taxed half the map at a third of a step. */
TEST(wall_hugging_falls_off_as_the_square_of_the_room_left)
{
	const Int need = 5;
	const Int peak = Pathfinder_wallHugCost( 100, need, need );
	const Int half = Pathfinder_wallHugCost( 100, need + PF_CLEARANCE_SOFT/2, need );
	CHECK( half * 3 < peak );			// a quarter-ish, comfortably under a third
	CHECK( half > 0 );
}

/** Traffic prices a queue, and it is capped for the same reason wall-hugging is: the heuristic
	 cannot see any of it, so every cost it cannot see is paid in cells expanded. */
TEST(traffic_is_free_when_empty_and_capped_when_full)
{
	CHECK_EQ( Pathfinder_trafficCost( 10, 0 ), 0 );
	CHECK_EQ( Pathfinder_trafficCost( 10, -1 ), 0 );
	const Int peak = (10 * PF_TRAFFIC_NUM) / PF_TRAFFIC_DEN;
	CHECK_EQ( Pathfinder_trafficCost( 10, PF_TRAFFIC_FULL ), peak );
	CHECK_EQ( Pathfinder_trafficCost( 10, PF_TRAFFIC_MAX ), peak );		// saturated, not overflowing
	CHECK( Pathfinder_trafficCost( 10, PF_TRAFFIC_FULL/2 ) < peak );
	CHECK( Pathfinder_trafficCost( 10, PF_TRAFFIC_FULL/2 ) > 0 );
}

/** A claim spills into the buckets either side of its own at half strength, so weights arrive in
	 halves.  One half of one unit is not a crossing and must cost nothing at all - otherwise every
	 cell within a bucket of anybody's plan is charged, which is most of a battlefield. */
TEST(half_a_unit_wanting_a_cell_is_not_a_crossing)
{
	CHECK_EQ( Pathfinder_crossingCost( 10, 0 ), 0 );
	CHECK_EQ( Pathfinder_crossingCost( 10, 1 ), 0 );		// the spill from a neighbouring bucket alone
	CHECK( Pathfinder_crossingCost( 10, 2 ) > 0 );			// one whole unit, at the moment we get there
}

/** And the crossing charge saturates: four units wanting one cell at one moment is as bad as a
	 crossing gets, and a fifth must not make the route look impossible. */
TEST(a_crossing_charge_saturates_rather_than_growing)
{
	const Int peak = (10 * PF_CROSSING_NUM * PF_CLAIM_UNITS_MAX) / PF_CROSSING_DEN;
	CHECK_EQ( Pathfinder_crossingCost( 10, 2*PF_CLAIM_UNITS_MAX ), peak );
	CHECK_EQ( Pathfinder_crossingCost( 10, 2*PF_CLAIM_UNITS_MAX + 40 ), peak );
	CHECK_EQ( Pathfinder_crossingCost( 10, 0xFFFF ), peak );
}

/** The three charges together bound what the flow model can add to one step.  This is the number
	 that decides whether the search still steers or fans out, so it is pinned here rather than left
	 to be rediscovered from a slow frame. */
TEST(the_whole_flow_charge_for_one_step_is_bounded)
{
	const Int base = 10;
	const Int worst = Pathfinder_wallHugCost( base, 0, 5 ) +
										Pathfinder_trafficCost( base, PF_TRAFFIC_MAX ) +
										Pathfinder_crossingCost( base, 0xFFFF );
	CHECK( worst <= 8 * base );		// 7.6 of a step: 1.6 wall-hug, 4 traffic, 2 crossing
	CHECK( worst >= 3 * base );		// and it is not so small that none of it does anything
}

/* Lanes.  The route is a band and each unit rides somewhere across it; these two functions are the
	 whole of that arithmetic, and every degenerate case below is one the map really produces - a
	 doorway with no width at all, a unit that has already been shoved outside its own band, a body
	 wider than the room measured for it. */

TEST(half_is_the_route_itself_however_lopsided_the_room_beside_it_is)
{
	/* The two sides of the band are scaled separately and meet at the centre line.  The first
		 version ran one scale from wall to wall, so half of a lopsided band was the middle of the
		 free ground and a unit holding no particular lane slid sideways whenever the terrain was
		 uneven - a mistake that grew with the probe and was worth 10% of the time spent blocked. */
	CHECK_NEAR( Pathfinder_laneFraction( 0.0f, 20.0f, 20.0f ), 0.5f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneOffset( 0.5f, 20.0f, 20.0f ), 0.0f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneOffset( 0.5f, 30.0f, 10.0f ), 0.0f, 0.0001f );
	// five feet left of the route, in a band with thirty feet of room on that side
	CHECK_NEAR( Pathfinder_laneFraction( 5.0f, 30.0f, 10.0f ), 0.5833f, 0.0001f );
	// the same five feet on the narrow side is a much bigger share of it
	CHECK_NEAR( Pathfinder_laneFraction( -5.0f, 30.0f, 10.0f ), 0.25f, 0.0001f );
}

TEST(a_lane_is_never_the_wall_itself)
{
	// a unit standing well outside its own band still gets a lane inside it
	CHECK_NEAR( Pathfinder_laneFraction( 500.0f, 20.0f, 20.0f ), 0.95f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneFraction( -500.0f, 20.0f, 20.0f ), 0.05f, 0.0001f );
	// and the offset that lane means keeps it off both edges
	CHECK( Pathfinder_laneOffset( 0.95f, 20.0f, 20.0f ) < 20.0f );
	CHECK( Pathfinder_laneOffset( 0.05f, 20.0f, 20.0f ) > -20.0f );
}

TEST(a_band_with_no_width_puts_everybody_on_the_centre_line)
{
	// a doorway, or a body as wide as the room beside it: laneExtent returns zero on both sides
	CHECK_NEAR( Pathfinder_laneFraction( 0.0f, 0.0f, 0.0f ), 0.5f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneFraction( 40.0f, 0.0f, 0.0f ), 0.5f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneOffset( 0.5f, 0.0f, 0.0f ), 0.0f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneOffset( 0.95f, 0.0f, 0.0f ), 0.0f, 0.0001f );
}

TEST(a_lane_fraction_and_its_offset_are_the_same_measurement_backwards)
{
	// what seedLaneFraction reads off the map, computePointOnPath has to be able to steer back to.
	// Only over the interior: the outer 5% at each edge is clamped away on purpose and does not
	// survive the round trip, which is the point of clamping it.
	const Real left = 25.0f, right = 15.0f;
	for (Real lateral = -13.0f; lateral <= 22.0f; lateral += 1.0f)
	{
		Real u = Pathfinder_laneFraction( lateral, left, right );
		CHECK_NEAR( Pathfinder_laneOffset( u, left, right ), lateral, 0.0001f );
	}
}

TEST(only_the_side_with_room_on_it_widens_the_band)
{
	// a route running along a wall: no room right, plenty left.  The lane has to end up left of
	// centre, which is the asymmetry the clearance map cannot see and the reason it is not used here.
	CHECK( Pathfinder_laneOffset( 0.9f, 40.0f, 0.0f ) > 0.0f );
	CHECK_NEAR( Pathfinder_laneOffset( 0.0f, 40.0f, 0.0f ), 0.0f, 0.0001f );		// hard right is the wall, which is the centre line
	// twenty feet into forty feet of room is halfway across that side of the band
	CHECK_NEAR( Pathfinder_laneFraction( 20.0f, 40.0f, 0.0f ), 0.75f, 0.0001f );
	// and a body pushed onto the wall side of a route with no room there stays on the route
	CHECK_NEAR( Pathfinder_laneOffset( Pathfinder_laneFraction( -20.0f, 40.0f, 0.0f ), 40.0f, 0.0f ),
		0.0f, 0.0001f );
}

TEST(a_lane_handed_to_a_group_member_is_a_distance_not_a_share_of_the_group)
{
	// A group's lanes are spaced by the size of its bodies, so what gets handed down has to mean
	// feet.  The reference is the widest band the probe can find, and the same offset therefore
	// means the same distance whether it came from a tight blob or a wide box.
	const Real ref = Pathfinder_laneReference();
	CHECK( ref > 100.0f );		// 16 cells of 10; a tank is about 24 wide, so this is several of them

	CHECK_NEAR( Pathfinder_groupLane( 0.0f ), 0.5f, 0.0001f );
	CHECK_NEAR( Pathfinder_groupLane( ref * 0.5f ), 0.75f, 0.0001f );
	CHECK_NEAR( Pathfinder_groupLane( ref * -0.5f ), 0.25f, 0.0001f );

	// the edges are the clamp, not the wall itself
	CHECK( Pathfinder_groupLane( ref * 4.0f ) < 1.0f );
	CHECK( Pathfinder_groupLane( ref * -4.0f ) > 0.0f );

	// order is preserved: whoever was left of somebody stays left of them
	CHECK( Pathfinder_groupLane( -30.0f ) < Pathfinder_groupLane( 10.0f ) );

	/* Two tanks a body apart have to come out at least a body apart on a full-width band, or the
		 spacing the group did the arithmetic for is thrown away here.  Taken back out through
		 laneOffset at the widest band the probe reports, which is what the group measured against. */
	Real uLeft = Pathfinder_groupLane( 14.0f );
	Real uRight = Pathfinder_groupLane( -14.0f );
	Real apart = Pathfinder_laneOffset( uLeft, ref, ref ) - Pathfinder_laneOffset( uRight, ref, ref );
	CHECK_NEAR( apart, 28.0f, 0.01f );

	// and a narrow road squeezes all of it rather than dropping anybody: quarter band, quarter gap
	Real narrow = Pathfinder_laneOffset( uLeft, ref * 0.25f, ref * 0.25f )
							- Pathfinder_laneOffset( uRight, ref * 0.25f, ref * 0.25f );
	CHECK_NEAR( narrow, 7.0f, 0.01f );
}

TEST(the_band_is_shut_before_the_goal_not_at_it)
{
	const Real cell = PATHFIND_CELL_SIZE_F;
	const Real taper = cell * (Real)PF_LANE_TAPER_CELLS;
	const Real close = cell * (Real)PF_LANE_CLOSE_CELLS;
	CHECK( close < taper );

	// out on the route, nothing is given up
	CHECK_NEAR( Pathfinder_laneTaper( taper * 4.0f ), 1.0f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneTaper( taper ), 1.0f, 0.0001f );

	/* The whole point of the close distance: at the goal, and for the last few cells before it,
		 the lane is gone completely.  A ramp that only reached zero at the goal left a unit riding
		 the edge of a wide band still yards off its own route when it stopped, and a group ordered
		 onto one point arrived spread across it. */
	CHECK_NEAR( Pathfinder_laneTaper( close ), 0.0f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneTaper( 0.0f ), 0.0f, 0.0001f );
	CHECK_NEAR( Pathfinder_laneTaper( -5.0f ), 0.0f, 0.0001f );

	// straight line between the two, and never a step backwards
	CHECK_NEAR( Pathfinder_laneTaper( (taper + close) * 0.5f ), 0.5f, 0.0001f );
	Real last = 0.0f;
	for (Real r = 0.0f; r < taper * 1.5f; r += 2.0f)
	{
		Real f = Pathfinder_laneTaper( r );
		CHECK( f >= last - 0.0001f );
		CHECK( f >= 0.0f && f <= 1.0f );
		last = f;
	}
}

TEST(the_taper_is_long_enough_for_the_band_it_has_to_close)
{
	/* A unit riding the edge of the widest band the probe can find has to be able to get back to
		 the centre line before the lane is switched off, or it arrives beside its own goal.  The
		 taper is that distance, so it has to be at least as long as the offset it is undoing -
		 anything shorter is a unit told to move sideways faster than it drives forwards. */
	const Real taper = PATHFIND_CELL_SIZE_F * (Real)PF_LANE_TAPER_CELLS;
	CHECK( taper >= Pathfinder_laneReference() * 0.5f );
}

TEST(crowd_corridor_measures_distance_along_the_route_not_samples)
{
	/* The sample's own `along` is quantised to the sample step, so a steering point taken from it
		 hops a whole cell forward every time the unit crosses a boundary.  alongOf is the unquantised
		 answer: it has to grow smoothly as the unit advances, and it has to ignore sideways offset. */
	Coord3D pts[ 5 ];
	for (Int k = 0; k < 5; k++)
	{
		pts[ k ].x = (Real)k * 10.0f;
		pts[ k ].y = 0.0f;
		pts[ k ].z = 0.0f;
	}

	CrowdCorridor corr;
	corr.buildForTest( pts, 5, 20.0f );
	CHECK_EQ( corr.count(), 5 );
	CHECK_NEAR( corr.length(), 40.0f, 0.001f );

	Coord3D p;
	p.y = 0.0f;
	p.z = 0.0f;

	Real last = -1.0f;
	for (Real x = 0.0f; x <= 40.0f; x += 1.0f)
	{
		p.x = x;
		const Real a = corr.alongOf( corr.nearest( p, 0 ), p );
		CHECK_NEAR( a, x, 0.001f );
		CHECK( a > last );				// no plateau, which is what a sample-quantised answer would give
		last = a;
	}

	// standing off the line does not move a unit forwards or backwards along it
	p.x = 15.0f;
	p.y = 12.0f;
	CHECK_NEAR( corr.alongOf( corr.nearest( p, 0 ), p ), 15.0f, 0.001f );
}

TEST(crowd_corridor_steering_point_slides_instead_of_hopping)
{
	/* The wobble on light chassis was this: point() lands on a sample, so the aim point jumped ten
		 feet at a time and on a bend the direction handed to the locomotor stepped with it.  pointAt
		 has to be continuous - consecutive queries a foot apart give answers a foot apart - and it has
		 to agree with point() exactly where a sample sits. */
	Coord3D pts[ 6 ];
	pts[ 0 ].x =  0.0f; pts[ 0 ].y =  0.0f;
	pts[ 1 ].x = 10.0f; pts[ 1 ].y =  0.0f;
	pts[ 2 ].x = 20.0f; pts[ 2 ].y =  0.0f;
	pts[ 3 ].x = 28.0f; pts[ 3 ].y =  6.0f;
	pts[ 4 ].x = 34.0f; pts[ 4 ].y = 14.0f;
	pts[ 5 ].x = 36.0f; pts[ 5 ].y = 24.0f;
	for (Int k = 0; k < 6; k++)
		pts[ k ].z = 0.0f;

	CrowdCorridor corr;
	corr.buildForTest( pts, 6, 15.0f );

	// on a sample, the two agree
	for (Int k = 0; k < corr.count(); k++)
	{
		Coord3D a, b;
		corr.point( k, 4.0f, &a );
		corr.pointAt( corr.at( k ).along, 4.0f, &b );
		CHECK_NEAR( a.x, b.x, 0.001f );
		CHECK_NEAR( a.y, b.y, 0.001f );
	}

	// and between them it slides: a tenth of a foot of route never moves the point a foot
	Coord3D prev;
	corr.pointAt( 0.0f, 4.0f, &prev );
	for (Real s = 0.1f; s <= corr.length(); s += 0.1f)
	{
		Coord3D now;
		corr.pointAt( s, 4.0f, &now );
		const Real dx = now.x - prev.x;
		const Real dy = now.y - prev.y;
		CHECK( dx * dx + dy * dy < 1.0f );
		prev = now;
	}

	// the width follows too, and a lane wider than the band is still cut to the band
	CHECK_NEAR( corr.clampLatAt( 12.5f, 4.0f ), 4.0f, 0.001f );
	CHECK_NEAR( corr.clampLatAt( 12.5f, 90.0f ), 15.0f, 0.001f );
	CHECK_NEAR( corr.clampLatAt( 12.5f, -90.0f ), -15.0f, 0.001f );

	// off the ends is clamped, not indexed out of the vector
	Coord3D ends;
	corr.pointAt( -50.0f, 0.0f, &ends );
	CHECK_NEAR( ends.x, pts[ 0 ].x, 0.001f );
	corr.pointAt( corr.length() + 50.0f, 0.0f, &ends );
	CHECK_NEAR( ends.x, pts[ 5 ].x, 0.001f );
}

TEST(crowd_corridor_has_no_band_on_a_bridge_or_at_its_approaches)
{
	/* A bridge deck is not road with room either side of it, and the ground beside a bridge is a
		 riverbank.  A lane held across either one drives the unit off the side, and a lane held on the
		 approach arrives beside the abutment instead of at the entrance - which is a unit that never
		 gets onto the bridge and a queue behind it that never gets anywhere.  The band closes over the
		 deck and for CROWD_BRIDGE_SEAL samples each side of it. */
	Coord3D pts[ 20 ];
	PathfindLayerEnum layers[ 20 ];
	for (Int k = 0; k < 20; k++)
	{
		pts[ k ].x = (Real)k * 10.0f;
		pts[ k ].y = 0.0f;
		pts[ k ].z = 0.0f;
		layers[ k ] = LAYER_GROUND;
	}
	layers[ 10 ] = (PathfindLayerEnum)2;		// the deck: three samples of it, out over the water
	layers[ 11 ] = (PathfindLayerEnum)2;
	layers[ 12 ] = (PathfindLayerEnum)2;

	CrowdCorridor corr;
	corr.buildForTest( pts, 20, 15.0f, layers );
	CHECK_EQ( corr.count(), 20 );

	// open road, far from the bridge either side: the band is what it was built with
	CHECK_NEAR( corr.at( 0 ).left, 15.0f, 0.001f );
	CHECK_NEAR( corr.at( 5 ).right, 15.0f, 0.001f );
	CHECK_NEAR( corr.at( 19 ).left, 15.0f, 0.001f );

	corr.sealBridges();

	const Int sealLo = 10 - (Int)CROWD_BRIDGE_SEAL;
	const Int sealHi = 12 + (Int)CROWD_BRIDGE_SEAL;

	// the deck itself, and the sealed samples each side of it
	for (Int k = sealLo; k <= sealHi; k++)
	{
		CHECK_NEAR( corr.at( k ).left, 0.0f, 0.001f );
		CHECK_NEAR( corr.at( k ).right, 0.0f, 0.001f );
		CHECK_NEAR( corr.clampLat( k, 12.0f ), 0.0f, 0.001f );
		CHECK_NEAR( corr.clampLatAt( corr.at( k ).along, -12.0f ), 0.0f, 0.001f );
	}

	// and the road on either side of that stretch keeps its band
	CHECK_NEAR( corr.at( sealLo - 1 ).left, 15.0f, 0.001f );
	CHECK_NEAR( corr.at( sealHi + 1 ).right, 15.0f, 0.001f );
	CHECK_NEAR( corr.clampLat( sealLo - 1, 12.0f ), 12.0f, 0.001f );

	// a route with no bridge on it is untouched
	CrowdCorridor plain;
	plain.buildForTest( pts, 20, 15.0f );
	plain.sealBridges();
	for (Int k = 0; k < plain.count(); k++)
		CHECK_NEAR( plain.at( k ).left, 15.0f, 0.001f );
}

TEST(the_rescue_ladder_never_runs_out_of_rungs)
{
	/* The promise: a unit that wants to move and is not moving always has something tried on it, in
		 order, and when the ladder runs out it starts again rather than going quiet.  Written against
		 the shape rather than the frame counts, so tuning the thresholds does not break the test and
		 skipping a rung does. */

	// nothing at all while the unit is still getting somewhere
	CHECK_EQ( AIUpdate_stuckRung( 0, 0 ), 0 );
	CHECK_EQ( AIUpdate_stuckRung( 1, 0 ), 0 );

	// long enough, and the rungs come one at a time and in order
	CHECK_EQ( AIUpdate_stuckRung( 1000, 0 ), 1 );
	CHECK_EQ( AIUpdate_stuckRung( 1000, 1 ), 2 );
	CHECK_EQ( AIUpdate_stuckRung( 1000, 2 ), 3 );

	// past the last one it restarts, and only after a further wait
	CHECK_EQ( AIUpdate_stuckRung( 1000, 3 ), -1 );
	CHECK_EQ( AIUpdate_stuckRung( 0, 3 ), 0 );

	/* No rung is ever skipped and none is ever offered early: whatever the frame count, the answer
		 is either nothing or exactly the next one up. */
	for (Int stage = 0; stage <= 3; stage++)
	{
		Int seen = 0;
		for (Int f = 0; f < 400; f++)
		{
			const Int rung = AIUpdate_stuckRung( f, stage );
			CHECK( rung == 0 || rung == stage + 1 || rung == -1 );
			if (rung != 0)
				++seen;
		}
		// and the ladder always eventually offers something, at every stage including the last
		CHECK( seen > 0 );
	}
}

TEST(driving_a_long_way_and_gaining_nothing_is_not_the_same_as_being_slow)
{
	const Real body = 24.0f;			// a tank

	// going somewhere: what it covered is what it gained
	CHECK( !AIUpdate_isDithering( 200.0f, 210.0f, body ) );
	CHECK( !AIUpdate_isDithering( 90.0f, 100.0f, body ) );

	// round a corner, so some of the travel is spent turning: still going somewhere
	CHECK( !AIUpdate_isDithering( 140.0f, 200.0f, body ) );

	// back and forth beside a building: eight body lengths covered, half a body gained
	CHECK( AIUpdate_isDithering( 12.0f, 200.0f, body ) );
	CHECK( AIUpdate_isDithering( 0.0f, 100.0f, body ) );

	/* Slow is not dithering and neither is stopped.  A unit that crept twenty units in three
		 seconds has gained everything it covered, and one that never moved has covered nothing: both
		 belong to the other test, and catching them here is what makes a rule fire on every unit
		 politely slowing down to park. */
	CHECK( !AIUpdate_isDithering( 20.0f, 20.0f, body ) );
	CHECK( !AIUpdate_isDithering( 0.0f, 0.0f, body ) );
	CHECK( !AIUpdate_isDithering( 1.0f, 30.0f, body ) );		// under two body lengths of travel

	// and the floor scales with the body, so infantry are not held to a tank's yardstick
	CHECK( AIUpdate_isDithering( 1.0f, 30.0f, 6.0f ) );
}

TEST(a_steering_point_is_clamped_by_the_pinch_it_has_to_drive_through)
{
	/* A straight road, wide at both ends and one body wide in the middle.  Clamping the aim point
		 against the ground at either end says nothing is wrong with riding 30 units off the centre
		 line, and the straight line between those two points goes through whatever is making the
		 middle narrow.  That is a unit driving into the corner of a building on a route that went
		 round it. */
	Coord3D pts[ 7 ];
	Real width[ 7 ];
	for (Int k = 0; k < 7; k++)
	{
		pts[ k ].x = (Real)k * 10.0f;
		pts[ k ].y = 0.0f;
		pts[ k ].z = 0.0f;
		width[ k ] = (k == 3) ? 4.0f : 40.0f;			// one sample of narrow ground in the middle
	}

	CrowdCorridor corr;
	corr.buildForTest( pts, 7, 40.0f, NULL, width );
	CHECK( !corr.isEmpty() );

	// each end on its own allows the whole offset
	CHECK_NEAR( corr.clampLatAt( 0.0f, 30.0f ), 30.0f, 0.001f );
	CHECK_NEAR( corr.clampLatAt( 60.0f, 30.0f ), 30.0f, 0.001f );

	// asked about the stretch that contains the pinch, it gives the pinch
	CHECK_NEAR( corr.clampLatNarrowest( 0.0f, 60.0f, 30.0f ), 4.0f, 0.001f );
	CHECK_NEAR( corr.clampLatNarrowest( 0.0f, 60.0f, -30.0f ), -4.0f, 0.001f );

	// argument order does not matter, and a stretch past the pinch is not cut by it
	CHECK_NEAR( corr.clampLatNarrowest( 60.0f, 0.0f, 30.0f ), 4.0f, 0.001f );
	CHECK_NEAR( corr.clampLatNarrowest( 40.0f, 60.0f, 30.0f ), 30.0f, 0.001f );

	// and it never widens anything: whatever comes back fits everywhere the walk looked
	CHECK_NEAR( corr.clampLatNarrowest( 0.0f, 60.0f, 2.0f ), 2.0f, 0.001f );
}

TEST(a_group_is_never_more_than_five_lanes_wide)
{
	const Real body = 24.0f;			// about a tank, which is what the spacing is measured from

	// the ground decides, up to the cap: two lanes of room is two lanes
	CHECK_EQ( Crowd_laneCount( 0.0f, body, 0 ), 1 );
	CHECK_EQ( Crowd_laneCount( body, body, 0 ), 2 );
	CHECK_EQ( Crowd_laneCount( body * 3.0f, body, 0 ), 4 );

	// and then it stops.  Open country is not a reason to arrive twenty abreast
	CHECK_EQ( Crowd_laneCount( body * 4.0f, body, 0 ), 5 );
	CHECK_EQ( Crowd_laneCount( body * 40.0f, body, 0 ), 5 );
	CHECK_EQ( Crowd_laneCount( 100000.0f, body, 0 ), 5 );

	// never more lanes than there are bodies to put in them
	CHECK_EQ( Crowd_laneCount( body * 40.0f, body, 3 ), 3 );
	CHECK_EQ( Crowd_laneCount( body * 40.0f, body, 9 ), 5 );

	// degenerate inputs are one lane, not a division by zero or a negative count
	CHECK_EQ( Crowd_laneCount( body * 4.0f, 0.0f, 4 ), 1 );
	CHECK_EQ( Crowd_laneCount( -50.0f, body, 4 ), 1 );
}

TEST(a_turn_costs_what_the_hull_takes_to_swing_it)
{
	// chassis is the cost of one radian: a hull that drives 40 units while turning one radian
	CHECK_EQ( Pathfinder_turnCost( 1.0f, 40, 60 ), 40 );
	CHECK_EQ( Pathfinder_turnCost( 0.5f, 40, 60 ), 20 );

	// the caps bite, and they are the two different ones the search uses
	CHECK_EQ( Pathfinder_turnCost( 3.14159f, 40, 60 ), 60 );
	CHECK_EQ( Pathfinder_turnCost( 3.14159f, 40, 28 ), 28 );

	// a hull that turns on the spot pays nothing, and neither does a search with no hull behind it
	CHECK_EQ( Pathfinder_turnCost( 3.14159f, 0, 60 ), 0 );
	CHECK_EQ( Pathfinder_turnCost( 0.0f, 40, 60 ), 0 );

	// never negative, whatever it is handed
	CHECK_EQ( Pathfinder_turnCost( -1.0f, 40, 60 ), 0 );
	CHECK( Pathfinder_turnCost( 2.0f, 3, 60 ) >= 0 );
}

TEST(crowd_brake_only_reads_closing_time)
{
	const Int frames = 8;
	const Real full = 2.0f;

	// nobody in front of us is gaining on us: no brake at any gap
	CHECK_NEAR( Crowd_brakeSpeed( full, full, 1.0f, frames ), full, 0.0001f );
	CHECK_NEAR( Crowd_brakeSpeed( full, full + 1.0f, 0.0f, frames ), full, 0.0001f );

	// closing, but the gap is more than eight frames of closing: still no brake.  This is the
	// distance-braking bug the measurement caught - a unit ten units back was being slowed to a
	// third of its speed for traffic it was never going to reach
	CHECK_NEAR( Crowd_brakeSpeed( full, 1.0f, 20.0f, frames ), full, 0.0001f );

	// inside that, the answer closes the gap over the eight frames rather than this one
	CHECK_NEAR( Crowd_brakeSpeed( full, 1.0f, 4.0f, frames ), 1.5f, 0.0001f );

	// touching a stopped unit is a stop, and nothing ever comes back negative
	CHECK_NEAR( Crowd_brakeSpeed( full, 0.0f, 0.0f, frames ), 0.0f, 0.0001f );
	CHECK_NEAR( Crowd_brakeSpeed( full, 0.0f, -3.0f, frames ), 0.0f, 0.0001f );

	// and the cap is never a speed-up
	for (Real g = -2.0f; g < 30.0f; g += 0.5f)
		CHECK( Crowd_brakeSpeed( full, 0.5f, g, frames ) <= full + 0.0001f );

	// a zero window is the rule switched off, not a division by zero
	CHECK_NEAR( Crowd_brakeSpeed( full, 0.0f, 0.0f, 0 ), full, 0.0001f );
}

//-------------------------------------------------------------------------------------------------
// A shadow request is filled in field by field by whoever makes it, and the debris draw module
// never touched the name at all.  The shadow manager reads that name the moment its first byte is
// not zero, so an uninitialised stack was being strlen'd and copied as a texture name.
//-------------------------------------------------------------------------------------------------
TEST(a_shadow_request_starts_with_no_texture_name)
{
	// Build the request on top of a scribbled buffer, default-initialised the way a local
	// variable is.  Without a constructor on the struct that leaves the pattern in place -
	// which is the whole failure - so this check cannot pass by accident.
	char raw[ sizeof(Shadow::ShadowTypeInfo) ];
	memset( raw, 0x7F, sizeof(raw) );

	Shadow::ShadowTypeInfo *info = ::new ( (void *)raw ) Shadow::ShadowTypeInfo;

	CHECK_EQ( info->m_ShadowName[ 0 ], '\0' );
	CHECK_EQ( (Int)strlen( info->m_ShadowName ), 0 );
	CHECK_EQ( (Int)info->m_type, (Int)SHADOW_NONE );
	CHECK_NEAR( info->m_sizeX, 0.0f, 0.0001f );
	CHECK_NEAR( info->m_sizeY, 0.0f, 0.0001f );
	CHECK_NEAR( info->m_offsetX, 0.0f, 0.0001f );
	CHECK_NEAR( info->m_offsetY, 0.0f, 0.0001f );
	CHECK( !info->allowUpdates );
	CHECK( !info->allowWorldAlign );
}

//-------------------------------------------------------------------------------------------------
// MenuTransitionSpeed scales the rate the shell's slides and fades step at. The catalog clamps the
// setting, but the function is what the menus actually call and a zero reaching it would be a
// division by nothing, so it clamps again on its own.
//-------------------------------------------------------------------------------------------------
TEST(menu_transition_speed_scales_the_step_rate_and_never_reaches_zero)
{
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;

	// the default is the rate the menus were drawn at, unchanged
	CHECK_EQ( scratch->m_menuTransitionSpeed, 100 );
	CHECK_NEAR( GameClient_menuAnimStepsPerSec(), UI_ANIM_STEPS_PER_SEC, 0.0001f );

	scratch->m_menuTransitionSpeed = 200;
	CHECK_NEAR( GameClient_menuAnimStepsPerSec(), UI_ANIM_STEPS_PER_SEC * 2.0f, 0.0001f );

	scratch->m_menuTransitionSpeed = 50;
	CHECK_NEAR( GameClient_menuAnimStepsPerSec(), UI_ANIM_STEPS_PER_SEC * 0.5f, 0.0001f );

	// out of range on either side lands on the range, and nothing ever comes back at or below zero
	scratch->m_menuTransitionSpeed = 0;
	CHECK_NEAR( GameClient_menuAnimStepsPerSec(), UI_ANIM_STEPS_PER_SEC * 0.25f, 0.0001f );

	scratch->m_menuTransitionSpeed = -1000;
	CHECK( GameClient_menuAnimStepsPerSec() > 0.0f );

	scratch->m_menuTransitionSpeed = 100000;
	CHECK_NEAR( GameClient_menuAnimStepsPerSec(), UI_ANIM_STEPS_PER_SEC * 4.0f, 0.0001f );

	TheWritableGlobalData = saved;
	delete scratch;
}

//-------------------------------------------------------------------------------------------------
// The structure riding the cursor is drawn at BuildPlacementOpacity and casts a shadow or not
// according to BuildPlacementShadows. Both defaults have to be what the game always did, or every
// player who never touches GameData.ini gets a different picture than they had.
//-------------------------------------------------------------------------------------------------
TEST(build_placement_preview_defaults_are_the_ones_the_game_always_used)
{
	GlobalData *scratch = NEW GlobalData;

	CHECK_NEAR( scratch->m_buildPlacementOpacity, PLACEMENT_SILHOUETTE_OPACITY, 0.0001f );
	CHECK( scratch->m_buildPlacementShadows );

	// and the income trickle is off unless somebody asks for it
	CHECK_EQ( scratch->m_moneyPerMinute, 0 );

	// the right button stopped scrolling when the mouse went onto one scheme, which is what freed
	// a right drag to draw a formation line; that is on unless somebody turns it off
	CHECK( scratch->m_formationDrag );

	delete scratch;
}

//-------------------------------------------------------------------------------------------------
// A map name with a non-ASCII byte in it reaches isalnum as a negative number, which is undefined
// and is what the CRT asserted on while building the map cache. Every byte over 127 has to encode,
// and the round trip has to bring it back.
//-------------------------------------------------------------------------------------------------
TEST(quoted_printable_survives_bytes_over_127)
{
	// every high byte, one after another - this is what an accented map name looks like
	char highBytes[ 130 ];
	Int n = 0;
	for( Int b = 128; b <= 255; ++b )
		highBytes[ n++ ] = (char)b;
	highBytes[ n ] = '\0';

	AsciiString original( highBytes );
	AsciiString encoded = AsciiStringToQuotedPrintable( original );

	// nothing high survives into the encoded form: it is all MAGIC_CHAR plus two hex digits
	for( Int i = 0; i < encoded.getLength(); ++i )
		CHECK( (unsigned char)encoded.getCharAt( i ) < 128 );

	CHECK_EQ( encoded.getLength(), original.getLength() * 3 );

	// and it comes back
	AsciiString decoded = QuotedPrintableToAsciiString( encoded );
	CHECK_STR( decoded.str(), original.str() );

	// a plain name is left alone, which is what makes the encoding readable in a cache file
	CHECK_STR( AsciiStringToQuotedPrintable( AsciiString( "Alpine" ) ).str(), "Alpine" );
}

/* Peace time is a truce between people.  A computer player does not honour one - it stands still
	 and then plays the same game - so a lobby with a bot in it has no peace time whatever the host
	 picked.  The rule lives in GameInfo::getPeaceTime(), which is what the options string on the
	 wire, the lobby's combo box and GameLogic all read, so this is the one place it can be checked.
	 The host's pick is masked, not cleared: take the bot back out and it is still there. */
TEST(peace_time_is_off_while_a_computer_player_is_in_the_lobby)
{
	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;

	SkirmishGameInfo game;
	game.init();		// leaves every slot closed; clearSlotList() would name them, and naming a
									// closed slot asks TheGameText, which the harness does not stand up

	UnicodeString name;
	name.translate( AsciiString( "Player" ) );

	GameSlot human;
	human.setState( SLOT_PLAYER, name );
	game.setSlot( 0, human );

	GameSlot other;
	other.setState( SLOT_PLAYER, name );
	game.setSlot( 1, other );

	game.setPeaceTime( 10 );
	CHECK( !game.hasAIPlayers() );
	CHECK_EQ( game.getPeaceTime(), 10 );

	// a bot in any seat turns it off, at every rung of the ladder
	static const SlotState rungs[] = { SLOT_EASY_AI, SLOT_MED_AI, SLOT_BRUTAL_AI };
	for( Int i = 0; i < 3; ++i )
	{
		GameSlot bot;
		bot.setState( rungs[ i ] );
		game.setSlot( 1, bot );
		CHECK( game.hasAIPlayers() );
		CHECK_EQ( game.getPeaceTime(), 0 );
	}

	// the pick was masked, not thrown away
	game.setSlot( 1, other );
	CHECK( !game.hasAIPlayers() );
	CHECK_EQ( game.getPeaceTime(), 10 );

	// and the setter still refuses a value the wire could not have come by honestly
	game.setPeaceTime( -5 );
	CHECK_EQ( game.getPeaceTime(), 0 );
	game.setPeaceTime( 9999 );
	CHECK_EQ( game.getPeaceTime(), 60 );

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

/* nextToken used to return false on a spent source without touching the caller's token, so the
	 token still held the *previous* one.  Every loop written as "walk until the token comes back
	 empty" then never ended. */
TEST(asciistring_nextToken_empties_the_token_when_the_source_runs_out)
{
	AsciiString path( "one\\two" );
	AsciiString tok;

	CHECK( path.nextToken( &tok, "\\/" ) );
	CHECK_STR( tok.str(), "one" );
	CHECK( path.nextToken( &tok, "\\/" ) );
	CHECK_STR( tok.str(), "two" );

	CHECK( !path.nextToken( &tok, "\\/" ) );
	CHECK_EQ( tok.getLength(), 0 );

	// and the same answer when the source was empty to begin with
	AsciiString none;
	AsciiString held( "stale" );
	CHECK( !none.nextToken( &held, "\\/" ) );
	CHECK_EQ( held.getLength(), 0 );

	// a token that is the source itself is still refused, and left as it was
	AsciiString self( "a\\b" );
	CHECK( !self.nextToken( &self, "\\/" ) );
	CHECK_STR( self.str(), "a\\b" );
}

/* -map "Maps\Twilight Flame\Twilight Flame.map" loses its quotes to WinMain's tokenizer and
	 reaches this function as "Maps\Twilight".  With no .map to stop on it grew a string one token
	 at a time until AsciiString's 32767-byte ceiling threw ERROR_OUT_OF_MEMORY out of
	 parseCommandLine, GameEngine::init caught it and ran on to its tail, and the first frame
	 faulted on a NULL TheGameLogic - reported as a crash in update(), with no main menu. */
TEST(map_path_conversion_terminates_on_a_path_with_no_map_file)
{
	AsciiString longForm( "Maps\\Twilight Flame\\Twilight Flame.map" );
	ConvertShortMapPathToLongMapPath( longForm );
	CHECK_STR( longForm.str(), "Maps\\Twilight Flame\\Twilight Flame.map" );

	AsciiString shortForm( "Maps\\Alpine Assault.map" );
	ConvertShortMapPathToLongMapPath( shortForm );
	CHECK_STR( shortForm.str(), "Maps\\Alpine Assault\\Alpine Assault.map" );

	AsciiString truncated( "Maps\\Twilight" );
	ConvertShortMapPathToLongMapPath( truncated );
	CHECK_STR( truncated.str(), "Maps\\Twilight" );

	AsciiString noSeparator( "Twilight Flame.map" );
	ConvertShortMapPathToLongMapPath( noSeparator );
	CHECK_STR( noSeparator.str(), "Twilight Flame.map" );

	AsciiString trailing( "Maps\\Twilight\\" );
	ConvertShortMapPathToLongMapPath( trailing );
	CHECK_STR( trailing.str(), "Maps\\Twilight\\" );
}

/* ControlBar.cpp: layoutPanels used to lift every window in a minimised panel by that panel's
	 slide, including the ones applyPanelSlide never moved.  Each rebuild of the layout lifted them
	 again, placeInPanel then recorded the lifted position as the authored one, and the bar walked
	 off the top of its own strip - taking the build tooltip, which is placed against
	 BackgroundMarker, off the screen with it. */
extern Int ControlBar_slideToUndo( Int slideAppliedToWindow, Int panelSlideOffsetNow );

TEST(the_bar_only_lifts_a_window_the_slide_actually_pushed_down)
{
	// a window the slide moved comes back by exactly what it was moved by
	CHECK_EQ( ControlBar_slideToUndo( 48, 48 ), 48 );

	//
	// and one it skipped is left alone, however far its panel says it went.  A plate marker and
	// anything in a panel that was minimised immediately are both in this case.
	//
	CHECK_EQ( ControlBar_slideToUndo( 0, 48 ), 0 );
	CHECK_EQ( ControlBar_slideToUndo( 0, 121 ), 0 );

	// a window that went down before the panel's slide changed underneath it still comes back whole
	CHECK_EQ( ControlBar_slideToUndo( 30, 0 ), 30 );
	CHECK_EQ( ControlBar_slideToUndo( 30, 90 ), 30 );

	// the round trip is a round trip: pushed down by n, lifted by n, net zero
	static const Int pushes[] = { 0, 1, 17, 48, 121 };
	for( Int i = 0; i < 5; i++ )
	{
		const Int y = 400;
		const Int down = y + pushes[ i ];
		CHECK_EQ( down - ControlBar_slideToUndo( pushes[ i ], 121 ), y );
	}
}

/* ControlBar.cpp: a resolution change throws every window away and builds it again, but the bar
	 itself has to survive - a production queue, a hunt update and the academy all hold a
	 const CommandButton * that would be left pointing at a freed one.  So the window half of init
	 is its own pair of functions, and the teardown half runs twice on the way out: once from the
	 rebuild, once from the destructor. */
TEST(the_bars_windows_can_be_torn_down_twice_without_taking_the_bar_with_them)
{
	ControlBar bar;

	bar.shutdownWindows();
	CHECK( !bar.getShowBuildTooltipLayout() );

	// again, on a bar that is already holding nothing - which is what the destructor does
	bar.shutdownWindows();
	CHECK( !bar.getShowBuildTooltipLayout() );

	// the command buttons are not window state and are not touched by any of it
	CHECK( bar.getCommandButtons() == NULL );
	CHECK( bar.findCommandButton( AsciiString( "NoSuchCommandButton" ) ) == NULL );
}

/* ControlBarScheme.cpp: the scheme places the money readout, the two general's tabs and the toolbar
	 column by reading their parent's screen position and subtracting it, and layoutPanels then runs
	 over the answer.  Asking for the panels back is not the same as having them back - the slide is
	 wall-clocked over a fifth of a second - so a scheme rebuilt while the bar was on its way down
	 read a parent that was still travelling, and every child it placed came out that far too high.
	 layoutPanels lifted the parent back afterwards and dragged them up again, placeInPanel recorded
	 the lifted position as the authored one, and it stuck. Measured in a match: 524 with the panels
	 sent home first, 294 without. clearPanelSlide is the "first". */
TEST(sending_the_panels_home_is_not_the_same_as_them_being_home)
{
	ControlBar bar;
	const Int left = ControlBar::CB_PANEL_LEFT;
	const Int right = ControlBar::CB_PANEL_RIGHT;
	const Int count = ControlBar::CB_PANEL_COUNT;

	// away, and all the way away this instant
	bar.showPanel( left, FALSE, TRUE );
	CHECK_NEAR( bar.getPanelSlideFraction( left ), 1.0f, 0.0001f );
	CHECK( bar.isPanelHidden( left ) );

	//
	// what switchControlBarStage( CONTROL_BAR_STAGE_DEFAULT ) does: it asks. The panel is still down
	// there, and anything reading a window's position on this frame reads it down there.
	//
	bar.showPanel( left, TRUE );
	CHECK_NEAR( bar.getPanelSlideFraction( left ), 1.0f, 0.0001f );

	// clearPanelSlide does not ask, it declares
	bar.clearPanelSlide();
	CHECK_NEAR( bar.getPanelSlideFraction( left ), 0.0f, 0.0001f );
	CHECK( !bar.isPanelHidden( left ) );

	// and it speaks for all three, not just the one that was asked about
	Int p;
	for( p = 0; p < count; p++ )
		bar.showPanel( p, FALSE, TRUE );
	bar.clearPanelSlide();
	for( p = 0; p < count; p++ )
	{
		CHECK_NEAR( bar.getPanelSlideFraction( p ), 0.0f, 0.0001f );
		CHECK( !bar.isPanelHidden( p ) );
	}

	// a panel that never left is left alone
	bar.clearPanelSlide();
	CHECK_NEAR( bar.getPanelSlideFraction( right ), 0.0f, 0.0001f );
}

/* Mouse.cpp: the tooltip box was placed by two flips and nothing else, under EA's own note that
	 the tips still had to be kept somewhere readable.  A flip is the right answer at the right and
	 bottom edges and no answer at the left and top, and both are reachable: setCursorTooltip will
	 wrap a tip to the full width of the display when a caller asks for it, and a caller that asks
	 for less than ten pixels gets a 120-wide column that runs long text hundreds of pixels down. */
TEST(a_tooltip_is_put_back_on_the_screen_after_it_is_flipped)
{
	const Int screenW = 1024, screenH = 768;
	Int x, y;

	// the ordinary case: to the right of the pointer, level with it, untouched
	Mouse::placeTooltip( 400, 300, 286, 102, 0, 0, screenW, screenH, &x, &y );
	CHECK_EQ( x, 420 );
	CHECK_EQ( y, 300 );

	// right edge: the flip is what puts it on the roomier side, and it stays
	Mouse::placeTooltip( 1000, 300, 286, 20, 0, 0, screenW, screenH, &x, &y );
	CHECK_EQ( x, 714 );
	CHECK( x >= 0 && x + 286 <= screenW );

	// bottom edge, same
	Mouse::placeTooltip( 400, 760, 286, 40, 0, 0, screenW, screenH, &x, &y );
	CHECK_EQ( y, 720 );
	CHECK( y >= 0 && y + 40 <= screenH );

	//
	// A screen-wide tip with the pointer past the middle: too wide to fit on the right, and the
	// flip alone lands it at -60.  This is the one a player sees with the left half of the text
	// missing.
	//
	Mouse::placeTooltip( 640, 300, 1000, 30, 0, 0, screenW, screenH, &x, &y );
	CHECK_EQ( x, 0 );

	// a narrow column of long text with the pointer high up: the flip alone puts it above the top
	Mouse::placeTooltip( 400, 300, 120, 500, 0, 0, screenW, screenH, &x, &y );
	CHECK_EQ( y, 0 );

	//
	// Nothing lands outside the screen, whatever is asked for.  A box larger than the screen is
	// pinned to the top left corner, which loses its far edge and keeps its first words.
	//
	static const Int sizes[] = { 20, 120, 286, 700, 1000, 1200 };
	Int i, j, px, py;
	for( i = 0; i < 6; i++ )
		for( j = 0; j < 6; j++ )
			for( px = 0; px <= screenW; px += 128 )
				for( py = 0; py <= screenH; py += 128 )
				{
					Mouse::placeTooltip( px, py, sizes[ i ], sizes[ j ], 0, 0, screenW, screenH, &x, &y );
					CHECK( x >= 0 );
					CHECK( y >= 0 );
					if( sizes[ i ] + 4 <= screenW )
						CHECK( x + sizes[ i ] + 4 <= screenW );
					if( sizes[ j ] + 4 <= screenH )
						CHECK( y + sizes[ j ] + 4 <= screenH );
				}

	// the limits are read, not assumed: a windowed device with an origin away from zero holds too
	Mouse::placeTooltip( 200, 200, 400, 300, 100, 150, 500, 450, &x, &y );
	CHECK( x >= 100 );
	CHECK( y >= 150 );
}

/* The lobby's settings page is 119 layout units tall and the superweapon box sits in its second
	 row, so the three entries it drops reach past the bottom of the panel they are drawn on.  Hit
	 testing walks the parents first and a panel does not contain them, so before this the rows drew,
	 highlighted nothing and could not be picked - the dropdown looked open and was dead.  An open
	 box is asked before its panel is; a closed one is not asked at all, or every click anywhere near
	 the last box the player touched would go to it. */
extern Bool OpenWindowOwnsPoint( Bool isOpen, Int originX, Int originY, Int width, Int height,
																 Int x, Int y );

TEST(an_open_dropdown_owns_the_rows_that_hang_past_its_panel)
{
	/* the starting cash box at 1280x720: 29 pixels tall closed, 133 with its five entries down, on
		 a panel whose bottom edge is at 598.  The last two rows are past that edge. */
	const Int boxX = 748, boxY = 466, boxWidth = 288;
	const Int boxHeightClosed = 29, boxHeightOpen = 133;
	const Int panelBottom = 598;
	const Int rowInsidePanel = 520, rowPastPanel = 585, rowWellPastPanel = 595;

	CHECK( boxY + boxHeightClosed < panelBottom );
	CHECK( boxY + boxHeightOpen > panelBottom );	// or there is nothing here to get wrong

	CHECK( OpenWindowOwnsPoint( TRUE, boxX, boxY, boxWidth, boxHeightOpen, 900, rowInsidePanel ) );
	CHECK( OpenWindowOwnsPoint( TRUE, boxX, boxY, boxWidth, boxHeightOpen, 900, rowPastPanel ) );
	CHECK( OpenWindowOwnsPoint( TRUE, boxX, boxY, boxWidth, boxHeightOpen, 900, rowWellPastPanel ) );

	// the closed box keeps its own row and nothing under it
	CHECK( OpenWindowOwnsPoint( TRUE, boxX, boxY, boxWidth, boxHeightClosed, 900, 480 ) );
	CHECK( !OpenWindowOwnsPoint( TRUE, boxX, boxY, boxWidth, boxHeightClosed, 900, rowPastPanel ) );

	// a box that is not open owns nothing, wherever the pointer is
	CHECK( !OpenWindowOwnsPoint( FALSE, boxX, boxY, boxWidth, boxHeightOpen, 900, rowPastPanel ) );
	CHECK( !OpenWindowOwnsPoint( FALSE, boxX, boxY, boxWidth, boxHeightOpen, 900, 480 ) );

	// and it never claims a click beside itself - PLAY GAME sits under the same rows
	CHECK( !OpenWindowOwnsPoint( TRUE, boxX, boxY, boxWidth, boxHeightOpen, 300, rowPastPanel ) );
	CHECK( !OpenWindowOwnsPoint( TRUE, boxX, boxY, boxWidth, boxHeightOpen, 900, 660 ) );
}

/* The superweapon rule is a mode, and what a mode leaves you depends on who you are playing.  The
	 USA Superweapon General fields three superweapons and pays for them in everything else, so Limit
	 leaves him four of each where it leaves everybody one, and No leaves him one where it bars
	 everybody else outright.  Player::canBuildMoreOfType asks SuperweaponBuildCap and nothing else
	 decides it, so this is the whole rule.  EA shipped the same field as a Yes/No checkbox where Yes
	 meant one of each: that reads back as No Superweapons, not as some truthy value. */
TEST(the_superweapon_rule_is_a_mode_with_one_exception)
{
	const AsciiString superweaponGeneral( "FactionAmericaSuperWeaponGeneral" );
	const AsciiString laserGeneral( "FactionAmericaLaserGeneral" );
	const AsciiString gla( "FactionGLA" );

	// no rule at all: 0 is what canBuildMoreOfType reads as no cap
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_ALLOW, gla ), (Int)SUPERWEAPON_CAP_UNLIMITED );
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_ALLOW, superweaponGeneral ), (Int)SUPERWEAPON_CAP_UNLIMITED );

	// one each, four for the general the whole exception exists for
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_LIMIT, gla ), 1 );
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_LIMIT, laserGeneral ), 1 );
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_LIMIT, superweaponGeneral ), 4 );

	// banned, and he keeps one
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_NONE, gla ), (Int)SUPERWEAPON_CAP_BANNED );
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_NONE, laserGeneral ), (Int)SUPERWEAPON_CAP_BANNED );
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_NONE, superweaponGeneral ), 1 );

	// a player with no template at all - the civilian seat - is nobody's exception
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_NONE, AsciiString::TheEmptyString ), (Int)SUPERWEAPON_CAP_BANNED );

	// and a mode from a build that knows one this one does not leaves the game unrestricted
	CHECK_EQ( SuperweaponBuildCap( 99, superweaponGeneral ), (Int)SUPERWEAPON_CAP_UNLIMITED );
	CHECK_EQ( SuperweaponBuildCap( 99, gla ), (Int)SUPERWEAPON_CAP_UNLIMITED );

	// the checkbox that used to be there, read by the dropdown that replaced it
	CHECK_EQ( SuperweaponRestrictionFromPreference( AsciiString( "Yes" ) ), (Int)SUPERWEAPONS_NONE );
	CHECK_EQ( SuperweaponRestrictionFromPreference( AsciiString( "no" ) ), (Int)SUPERWEAPONS_ALLOW );

	// and what this build writes, which is the mode
	CHECK_EQ( SuperweaponRestrictionFromPreference( AsciiString( "0" ) ), (Int)SUPERWEAPONS_ALLOW );
	CHECK_EQ( SuperweaponRestrictionFromPreference( AsciiString( "1" ) ), (Int)SUPERWEAPONS_LIMIT );
	CHECK_EQ( SuperweaponRestrictionFromPreference( AsciiString( "2" ) ), (Int)SUPERWEAPONS_NONE );

	// a line nobody wrote on purpose leaves the game unrestricted rather than at one of each
	CHECK_EQ( SuperweaponRestrictionFromPreference( AsciiString( "" ) ), (Int)SUPERWEAPONS_ALLOW );
	CHECK_EQ( SuperweaponRestrictionFromPreference( AsciiString( "maybe" ) ), (Int)SUPERWEAPONS_ALLOW );

	// the lobby carries the host's pick untouched - it is the wire's business, not the setter's
	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;

	SkirmishGameInfo game;
	game.init();
	CHECK_EQ( (Int)game.getSuperweaponRestriction(), (Int)SUPERWEAPONS_ALLOW );
	game.setSuperweaponRestriction( SUPERWEAPONS_LIMIT );
	CHECK_EQ( (Int)game.getSuperweaponRestriction(), (Int)SUPERWEAPONS_LIMIT );
	game.setSuperweaponRestriction( SUPERWEAPONS_NONE );
	CHECK_EQ( (Int)game.getSuperweaponRestriction(), (Int)SUPERWEAPONS_NONE );

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

/* The lobby's Pro Rules box starts ticked, so a lobby nobody touched plays the tournament list the
	 way every match did before the box existed.  The wire half, PR= in the options string, needs
	 TheGameState and TheMapCache to build a string at all; replay-check carries that one. */
TEST(pro_rules_box_starts_ticked_and_clears)
{
	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;

	SkirmishGameInfo game;
	game.init();
	CHECK( game.getProRules() );
	game.setProRules( FALSE );
	CHECK( !game.getProRules() );
	game.reset();
	CHECK( game.getProRules() );

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

#include "Common/SpecialPowerType.h"

/* Pro Rules name what they ban by the ending every general's copy shares, so each check below
	 is a template name out of the game's own INI, and each near miss is the thing next to it on the
	 same build menu that must stay buildable. */
TEST(pro_rules_ban_the_listed_things_and_nothing_beside_them)
{
	// rules 4 and 5: every faction's Aurora, the Alpha Aurora being the Air Force General's copy
	CHECK( ProRulesBanThing( AsciiString( "AmericaJetAurora" ) ) );
	CHECK( ProRulesBanThing( AsciiString( "AirF_AmericaJetAurora" ) ) );
	CHECK( ProRulesBanThing( AsciiString( "Lazr_AmericaJetAurora" ) ) );
	CHECK( ProRulesBanThing( AsciiString( "SupW_AmericaJetAurora" ) ) );
	CHECK( !ProRulesBanThing( AsciiString( "AmericaJetRaptor" ) ) );
	CHECK( !ProRulesBanThing( AsciiString( "AmericaJetAuroraHulk" ) ) );
	CHECK( !ProRulesBanThing( AsciiString( "AmericaParticleCannonUplink" ) ) );

	// rule 11: the silo keeps the lobby's rule, every other superweapon is rule 1 and 3's
	CHECK( ProRulesExemptSuperweapon( AsciiString( "ChinaNuclearMissileLauncher" ) ) );
	CHECK( ProRulesExemptSuperweapon( AsciiString( "Nuke_ChinaNuclearMissileLauncher" ) ) );
	CHECK( !ProRulesExemptSuperweapon( AsciiString( "AmericaParticleCannonUplink" ) ) );
	CHECK( !ProRulesExemptSuperweapon( AsciiString( "SupW_AmericaParticleCannonUplink" ) ) );
	CHECK( !ProRulesExemptSuperweapon( AsciiString( "GLAScudStorm" ) ) );
	CHECK( !ProRulesExemptSuperweapon( AsciiString( "Demo_GLAScudStorm" ) ) );

	// rules 1, 3 and 10 are No Superweapons for everyone but one general
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_NONE, AsciiString( "FactionAmericaSuperWeaponGeneral" ) ), 1 );
	CHECK_EQ( SuperweaponBuildCap( SUPERWEAPONS_NONE, AsciiString( "FactionAmericaAirForceGeneral" ) ), (Int)SUPERWEAPON_CAP_BANNED );

	// rule 6, and the Nuke General's other upgrades
	CHECK( ProRulesBanUpgrade( AsciiString( "Upgrade_ChinaTacticalNukeMig" ) ) );
	CHECK( !ProRulesBanUpgrade( AsciiString( "Nuke_Upgrade_HelixNukeBomb" ) ) );
	CHECK( !ProRulesBanUpgrade( AsciiString( "Nuke_Upgrade_ChinaWGUraniumShells" ) ) );

	// rule 2: the missile, for each general that fires one, and not the other superweapons' powers
	CHECK( ProRulesBanSpecialPower( SPECIAL_NEUTRON_MISSILE, 5 ) );
	CHECK( ProRulesBanSpecialPower( NUKE_SPECIAL_NEUTRON_MISSILE, 5 ) );
	CHECK( ProRulesBanSpecialPower( SUPW_SPECIAL_NEUTRON_MISSILE, 5 ) );
	CHECK( !ProRulesBanSpecialPower( SPECIAL_SCUD_STORM, 1 ) );
	CHECK( !ProRulesBanSpecialPower( SPECIAL_PARTICLE_UPLINK_CANNON, 1 ) );
	CHECK( !ProRulesBanSpecialPower( SPECIAL_CLUSTER_MINES, 1 ) );

	// rule 7: a terrorist on a combat cycle, and every other rider still rides
	CHECK( ProRulesBanRider( AsciiString( "GLAInfantryTerrorist" ) ) );
	CHECK( ProRulesBanRider( AsciiString( "Demo_GLAInfantryTerrorist" ) ) );
	CHECK( ProRulesBanRider( AsciiString( "GC_Slth_GLAInfantryTerrorist" ) ) );
	CHECK( !ProRulesBanRider( AsciiString( "Demo_GLAInfantryRebel" ) ) );
	CHECK( !ProRulesBanRider( AsciiString( "GLAInfantryWorker" ) ) );
	CHECK( !ProRulesBanRider( AsciiString( "Demo_GLAInfantryHijacker" ) ) );

	// rule 9: the clearance is past a Patriot's and a Stinger Site's ground reach, 225 in Weapon.ini,
	// so no foundation goes down inside an enemy's own defensive cover
	CHECK( (Int)PRO_RULES_ENEMY_STRUCTURE_CLEARANCE > 225 );
}

/* Rules 12 to 15 ride on the one Pro Rules box like every other rule: three upgrades by name, and
	 the Carpet Bomb until rank 3. */
TEST(pro_rules_ban_rules_12_to_15)
{
	CHECK( ProRulesBanUpgrade( AsciiString( "Demo_Upgrade_SuicideBomb" ) ) );
	CHECK( ProRulesBanUpgrade( AsciiString( "Upgrade_ChinaNeutronShells" ) ) );
	CHECK( ProRulesBanUpgrade( AsciiString( "AirF_Upgrade_StealthComanche" ) ) );

	// the Comanche's rocket pods and the other two shells stay buildable
	CHECK( !ProRulesBanUpgrade( AsciiString( "Upgrade_ComancheRocketPods" ) ) );
	CHECK( !ProRulesBanUpgrade( AsciiString( "Upgrade_ChinaUraniumShells" ) ) );
	CHECK( !ProRulesBanUpgrade( AsciiString( "Upgrade_GLAToxinShells" ) ) );

	// rule 15: rank 3 is where the carpet bomb opens
	CHECK( ProRulesBanSpecialPower( AIRF_SPECIAL_CARPET_BOMB, 2 ) );
	CHECK( !ProRulesBanSpecialPower( AIRF_SPECIAL_CARPET_BOMB, PRO_RULES_CARPET_BOMB_RANK ) );
	CHECK( ProRulesBanSpecialPower( SPECIAL_CARPET_BOMB, 1 ) );
	CHECK( ProRulesBanSpecialPower( SPECIAL_CHINA_CARPET_BOMB, 2 ) );
	CHECK( ProRulesBanSpecialPower( EARLY_SPECIAL_CHINA_CARPET_BOMB, 2 ) );
	CHECK( !ProRulesBanSpecialPower( SPECIAL_CLUSTER_MINES, 1 ) );
}

/* Rule 16: two of a player's own defences per oil derrick cluster.  A cluster is every derrick
	 chained to the next within the radius, so a pair 250 apart is one cluster and a derrick 600 away
	 is another. */
TEST(pro_rules_count_the_defenses_of_one_derrick_cluster)
{
	CHECK( ProRulesIsOilDerrick( AsciiString( "TechOilDerrick" ) ) );
	CHECK( !ProRulesIsOilDerrick( AsciiString( "TechOilDerrickHulk" ) ) );
	CHECK( ProRulesIsDerrickDefense( AsciiString( "AmericaPatriotBattery" ) ) );
	CHECK( ProRulesIsDerrickDefense( AsciiString( "AirF_AmericaPatriotBattery" ) ) );
	CHECK( ProRulesIsDerrickDefense( AsciiString( "Tank_ChinaGattlingCannon" ) ) );
	CHECK( ProRulesIsDerrickDefense( AsciiString( "Demo_GLAStingerSite" ) ) );
	CHECK( !ProRulesIsDerrickDefense( AsciiString( "ChinaHelixGattlingCannon" ) ) );
	CHECK( !ProRulesIsDerrickDefense( AsciiString( "AmericaFireBase" ) ) );
	CHECK( !ProRulesIsDerrickDefense( AsciiString( "GLATunnelNetwork" ) ) );

	const Coord2D derricks[] = { { 0.0f, 0.0f }, { 250.0f, 0.0f }, { 850.0f, 0.0f } };
	const Coord2D defenses[] = { { -100.0f, 50.0f }, { 400.0f, 0.0f }, { 850.0f, 200.0f } };
	const Int derrickCount = ARRAY_SIZE( derricks );
	const Int defenseCount = ARRAY_SIZE( defenses );

	const Coord2D besideTheFirst = { 0.0f, 150.0f };
	CHECK_EQ( ProRulesCountDerrickClusterDefenses( besideTheFirst, derricks, derrickCount, defenses, defenseCount ), 2 );

	// the far end of the pair reaches the same two
	const Coord2D besideTheSecond = { 300.0f, -100.0f };
	CHECK_EQ( ProRulesCountDerrickClusterDefenses( besideTheSecond, derricks, derrickCount, defenses, defenseCount ), 2 );

	const Coord2D besideTheLoneDerrick = { 850.0f, -100.0f };
	CHECK_EQ( ProRulesCountDerrickClusterDefenses( besideTheLoneDerrick, derricks, derrickCount, defenses, defenseCount ), 1 );

	const Coord2D inTheOpen = { 0.0f, 2000.0f };
	CHECK_EQ( ProRulesCountDerrickClusterDefenses( inTheOpen, derricks, derrickCount, defenses, defenseCount ), 0 );
}

/* -scenario's line parser.
 *
 * A scenario file that gets half-read still plays a match and still writes a frame time table, so
 * the run produces numbers that answer a different question than the one asked. Every way a line
 * can be wrong therefore has to come back named. atoi is the specific trap the parser exists to
 * avoid: it reads "banana" as 0, 0 is a real frame number, and a typo in one field would put the
 * whole file on the first frame.
 */
TEST(scenario_parses_a_spawn_line)
{
	ScenarioAction action;

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 spawn 2 GLAInfantryAngryMobNexus 8 1200 900", &action ),
						(Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.frame, 0 );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_SPAWN );
	CHECK_EQ( action.slot, 2 );
	CHECK_STR( action.selector.str(), "GLAInfantryAngryMobNexus" );
	CHECK_EQ( action.count, 8 );
	CHECK_NEAR( action.at.x, 1200.0f, 0.01f );
	CHECK_NEAR( action.at.y, 900.0f, 0.01f );

	// spacing is optional, and the default has to be a real gap rather than zero
	CHECK( action.spacing > 0.0f );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "30 spawn 1 AmericaVehicleHumvee 4 100 200 55", &action ),
						(Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.frame, 30 );
	CHECK_NEAR( action.spacing, 55.0f, 0.01f );
}

TEST(scenario_parses_a_particles_line)
{
	ScenarioAction action;

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "30 particles 0 CINE_JetLenzflareExhaust 250 760 850 25", &action ),
						(Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_PARTICLES );
	CHECK_STR( action.selector.str(), "CINE_JetLenzflareExhaust" );
	CHECK_EQ( action.count, 250 );
	CHECK_NEAR( action.at.x, 760.0f, 0.01f );
	CHECK_NEAR( action.at.y, 850.0f, 0.01f );
	CHECK_NEAR( action.spacing, 25.0f, 0.01f );

	// the same count and position rules as spawn, because it shares spawn's parse
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "30 particles 0 X 0 760 850", &action ), (Int)SCENARIO_PARSE_BAD_COUNT );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "30 particles 0 X 5 760", &action ), (Int)SCENARIO_PARSE_MISSING_ARGS );
}

TEST(scenario_parses_the_order_lines)
{
	ScenarioAction action;

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "120 move 2 * 800 600", &action ), (Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_MOVE );
	CHECK_STR( action.selector.str(), "*" );
	CHECK_NEAR( action.at.x, 800.0f, 0.01f );
	CHECK_NEAR( action.at.y, 600.0f, 0.01f );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "240 attackmove 2 GLAInfantryAngryMobNexus 2400 1500", &action ),
						(Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_ATTACKMOVE );
	CHECK_NEAR( action.at.x, 2400.0f, 0.01f );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "300 attack 2 * 1 AmericaCommandCenter", &action ),
						(Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_ATTACK );
	CHECK_EQ( action.targetSlot, 1 );
	CHECK_STR( action.targetSelector.str(), "AmericaCommandCenter" );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "40 enter 0 AmericaInfantryMissileDefender 0 AmericaVehicleHumvee", &action ),
						(Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_ENTER );
	CHECK_EQ( action.targetSlot, 0 );
	CHECK_STR( action.targetSelector.str(), "AmericaVehicleHumvee" );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "40 enter 0 * 0", &action ), (Int)SCENARIO_PARSE_MISSING_ARGS );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "900 stop 2 *", &action ), (Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_STOP );
}

TEST(scenario_ignores_comments_and_blank_lines)
{
	ScenarioAction action;

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "", &action ), (Int)SCENARIO_PARSE_BLANK );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "   \t  ", &action ), (Int)SCENARIO_PARSE_BLANK );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "# eight mobs on the ridge", &action ), (Int)SCENARIO_PARSE_BLANK );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "   # indented comment", &action ), (Int)SCENARIO_PARSE_BLANK );

	// a text-mode read still leaves the carriage return on a CRLF file
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "900 stop 2 *\r", &action ), (Int)SCENARIO_PARSE_OK );

	// and a comment after a real action does not eat the action
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "900 stop 2 *  # everybody halt", &action ),
						(Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_STOP );
	CHECK_STR( action.selector.str(), "*" );
}

TEST(scenario_refuses_a_line_it_cannot_read)
{
	ScenarioAction action;

	// the atoi trap: none of these may come back as frame 0
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "banana spawn 2 X 1 0 0", &action ), (Int)SCENARIO_PARSE_BAD_FRAME );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "12x spawn 2 X 1 0 0", &action ), (Int)SCENARIO_PARSE_BAD_FRAME );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "-5 stop 2 *", &action ), (Int)SCENARIO_PARSE_BAD_FRAME );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 teleport 2 *", &action ), (Int)SCENARIO_PARSE_BAD_ACTION );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 stop two *", &action ), (Int)SCENARIO_PARSE_BAD_SLOT );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 attack 2 * nobody Thing", &action ), (Int)SCENARIO_PARSE_BAD_SLOT );

	// a count of zero spawns nothing, which is a typo rather than an instruction
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 spawn 2 X 0 100 100", &action ), (Int)SCENARIO_PARSE_BAD_COUNT );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 spawn 2 X lots 100 100", &action ), (Int)SCENARIO_PARSE_BAD_COUNT );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 stop", &action ), (Int)SCENARIO_PARSE_MISSING_ARGS );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 move 2 *", &action ), (Int)SCENARIO_PARSE_MISSING_ARGS );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 move 2 * 100", &action ), (Int)SCENARIO_PARSE_MISSING_ARGS );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "0 spawn 2 X 4 100", &action ), (Int)SCENARIO_PARSE_MISSING_ARGS );

	// and every refusal has something to say for itself in the log
	CHECK_STR( ScenarioDrill_parseResultName( SCENARIO_PARSE_BAD_FRAME ), "frame is not a whole number" );
}

/* A position can name a start instead of two numbers, which is what lets one file play on a
	 generated map whose starts move with the seed.  The number is spelt out for the atoi reason, and
	 an offset that is not two numbers is refused rather than read as zero. */
TEST(scenario_positions_can_name_a_start)
{
	ScenarioAction action;

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "30 spawn 0 AmericaTankCrusader 20 start0", &action ), (Int)SCENARIO_PARSE_OK );
	CHECK_EQ( action.atStart, 0 );
	CHECK_NEAR( action.at.x, 0.0f, 0.01f );
	CHECK_NEAR( action.at.y, 0.0f, 0.01f );
	CHECK_NEAR( action.spacing, 30.0f, 0.01f );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "30 spawn 0 AmericaTankCrusader 20 start1:-250.5:80 40", &action ),
						(Int)SCENARIO_PARSE_OK );
	CHECK_EQ( action.atStart, 1 );
	CHECK_NEAR( action.at.x, -250.5f, 0.01f );
	CHECK_NEAR( action.at.y, 80.0f, 0.01f );
	CHECK_NEAR( action.spacing, 40.0f, 0.01f );

	// plain numbers still mean a position, and say so
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 move 0 * 800 600", &action ), (Int)SCENARIO_PARSE_OK );
	CHECK_EQ( action.atStart, (Int)SCENARIO_NO_START );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 move 0 * start", &action ), (Int)SCENARIO_PARSE_BAD_POSITION );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 move 0 * start1x", &action ), (Int)SCENARIO_PARSE_BAD_POSITION );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 move 0 * start1:100", &action ), (Int)SCENARIO_PARSE_BAD_POSITION );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 move 0 * start1:100:y", &action ), (Int)SCENARIO_PARSE_BAD_POSITION );
}

/* arrive is the choke probe's clock: it takes a position like move and an optional radius. */
TEST(scenario_parses_an_arrive_line)
{
	ScenarioAction action;

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 arrive 0 AmericaTankCrusader start1", &action ), (Int)SCENARIO_PARSE_OK );
	CHECK_EQ( (Int)action.action, (Int)SCENARIO_ACTION_ARRIVE );
	CHECK_EQ( action.atStart, 1 );
	CHECK( action.radius > 0.0f );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 arrive 0 * 1200 900 150", &action ), (Int)SCENARIO_PARSE_OK );
	CHECK_NEAR( action.at.x, 1200.0f, 0.01f );
	CHECK_NEAR( action.radius, 150.0f, 0.01f );

	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 arrive 0 *", &action ), (Int)SCENARIO_PARSE_MISSING_ARGS );
	CHECK_EQ( (Int)ScenarioDrill_parseLine( "60 arrive 0 * 1200", &action ), (Int)SCENARIO_PARSE_MISSING_ARGS );
}

/* -control's WebSocket handshake.
 *
 * The reply to a client key is SHA-1 of the key and the protocol's GUID, base64'd. Nothing in
 * GameEngine had either of those, so both are written out in ControlServer.cpp and both are the
 * kind of thing that is either exactly right or silently useless - a wrong byte gives a reply the
 * client refuses, and the only symptom is a socket that never talks.
 *
 * RFC 6455 section 1.3 ships a worked example, which is what this checks against.
 */
TEST(control_server_answers_the_rfc_handshake_example)
{
	char accept[ 64 ];

	CHECK( ControlServer_computeAcceptKey( "dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof( accept ) ) );
	CHECK_STR( accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" );

	// a second known pair, so that a SHA-1 that happens to be right for one input is not enough
	CHECK( ControlServer_computeAcceptKey( "x3JJHMbDL1EzLkh9GBhXDw==", accept, sizeof( accept ) ) );
	CHECK_STR( accept, "HSmrc0sMlYUkAGmm5OPpG2HaGWk=" );

	// and an empty key still hashes rather than walking off the end of anything
	CHECK( ControlServer_computeAcceptKey( "", accept, sizeof( accept ) ) );
	CHECK_EQ( (Int)strlen( accept ), 28 );

	// a buffer too small to hold the 28 characters and the terminator is refused, not overrun
	char tooSmall[ 8 ];
	CHECK( !ControlServer_computeAcceptKey( "dGhlIHNhbXBsZSBub25jZQ==", tooSmall, sizeof( tooSmall ) ) );
}

/* Telling a click from a drag.
 *
 * Three terms, and each of them has been wrong at some point. The screen distance was compared one
 * axis at a time, which let a diagonal drag of 1.41 times the tolerance pass as a click. The camera
 * term existed in the selection translator and was simply missing from the command translator, so
 * a right click that scrolled the map still counted as a click there.
 */
TEST(click_tolerance_is_a_radius_in_time_screen_and_world)
{
	const Real screenTolerance = 10.0f;
	const Real cameraTolerance = 20.0f;
	const UnsignedInt msTolerance = 250;

	// dead centre, no time, no camera movement
	CHECK( ClickTolerance_isClick( 0.0f, 0.0f, 0.0f, 0, screenTolerance, cameraTolerance, msTolerance ) );

	// held too long
	CHECK( !ClickTolerance_isClick( 0.0f, 0.0f, 0.0f, 251, screenTolerance, cameraTolerance, msTolerance ) );
	CHECK( ClickTolerance_isClick( 0.0f, 0.0f, 0.0f, 250, screenTolerance, cameraTolerance, msTolerance ) );

	// on the axes the tolerance is exactly the tolerance
	CHECK( ClickTolerance_isClick( 10.0f, 0.0f, 0.0f, 0, screenTolerance, cameraTolerance, msTolerance ) );
	CHECK( !ClickTolerance_isClick( 11.0f, 0.0f, 0.0f, 0, screenTolerance, cameraTolerance, msTolerance ) );
	CHECK( ClickTolerance_isClick( 0.0f, -10.0f, 0.0f, 0, screenTolerance, cameraTolerance, msTolerance ) );

	// the diagonal is what the old per-axis test got wrong: 8,8 is 11.3 away, outside a radius of 10
	CHECK( !ClickTolerance_isClick( 8.0f, 8.0f, 0.0f, 0, screenTolerance, cameraTolerance, msTolerance ) );
	CHECK( ClickTolerance_isClick( 7.0f, 7.0f, 0.0f, 0, screenTolerance, cameraTolerance, msTolerance ) );

	// the world moved under a pointer that did not
	CHECK( ClickTolerance_isClick( 0.0f, 0.0f, 20.0f, 0, screenTolerance, cameraTolerance, msTolerance ) );
	CHECK( !ClickTolerance_isClick( 0.0f, 0.0f, 20.5f, 0, screenTolerance, cameraTolerance, msTolerance ) );
}

/* Which modifiers a key is held with.
 *
 * The modifier masks are the KEY_STATE_L* bits, which are 0x04, 0x10 and 0x40 - not consecutive,
 * so the seven combinations cannot index anything directly. Getting that mapping wrong is silent:
 * a released Ctrl would clear the wrong slot and the key's UP command would fire for a combination
 * that is still held, or never fire at all.
 */
TEST(key_down_info_remembers_each_modifier_combination)
{
	const Int CTRL_MASK = KEY_STATE_LCONTROL;
	const Int SHIFT_MASK = KEY_STATE_LSHIFT;
	const Int ALT_MASK = KEY_STATE_LALT;

	// the seven combinations round-trip through their slot numbers, and nothing shares a slot
	Int seen = 0;
	const Int combos[ KeyDownInfo::MOD_STATE_COUNT ] = {
		CTRL_MASK, SHIFT_MASK, ALT_MASK,
		CTRL_MASK | SHIFT_MASK, CTRL_MASK | ALT_MASK, SHIFT_MASK | ALT_MASK,
		CTRL_MASK | SHIFT_MASK | ALT_MASK
	};
	for( Int i = 0; i < KeyDownInfo::MOD_STATE_COUNT; ++i )
	{
		const Int index = KeyDownInfo::indexOfModState( combos[ i ] );
		CHECK( index >= 0 && index < KeyDownInfo::MOD_STATE_COUNT );
		CHECK_EQ( KeyDownInfo::modStateAtIndex( index ), combos[ i ] );
		CHECK( ( seen & ( 1 << index ) ) == 0 );
		seen |= 1 << index;
	}
	CHECK_EQ( seen, ( 1 << KeyDownInfo::MOD_STATE_COUNT ) - 1 );

	// no modifier at all has no slot, and neither does a stray bit such as caps lock
	CHECK_EQ( KeyDownInfo::indexOfModState( 0 ), -1 );
	CHECK_EQ( KeyDownInfo::indexOfModState( KEY_STATE_CAPSLOCK ), -1 );

	// a key pressed under two different combinations remembers both, and releasing one leaves
	// the other alone - this is the case the old single "last modifier state" could not hold
	KeyDownInfo info;
	CHECK( !info.isKeyDown() );

	info.setModState( CTRL_MASK );
	info.setModState( CTRL_MASK | SHIFT_MASK );
	CHECK( info.isKeyDown() );
	CHECK( info.hasModState( CTRL_MASK ) );
	CHECK( info.hasModState( CTRL_MASK | SHIFT_MASK ) );
	CHECK( !info.hasModState( ALT_MASK ) );

	info.clearModState( CTRL_MASK );
	CHECK( !info.hasModState( CTRL_MASK ) );
	CHECK( info.hasModState( CTRL_MASK | SHIFT_MASK ) );
	CHECK( info.isKeyDown() );

	info.clearModState( CTRL_MASK | SHIFT_MASK );
	CHECK( !info.isKeyDown() );
}

/* A button flash for a button the layout does not have.
 *
 * WindowTransitions.ini's single player groups name MainMenu.wnd:ButtonCustomMission, and only the
 * 1.04 patch's MainMenu.wnd carries that button, so on another install the window lookup comes
 * back empty. Every step of the flash skipped an empty window except the pause between fading to
 * the background and fading to the gradient, which asked to draw it anyway. A player's crash log
 * ended there, reading address 0x1F0 inside GameWindow::winGetScreenPosition.
 */
class ButtonFlashWithoutWindow : public ButtonFlashTransition
{
public:
	enum
	{
		PAUSE_FIRST_FRAME = BUTTONFLASHTRANSITION_FADE_TO_BACKGROUND_4 + 1,
		PAUSE_LAST_FRAME = BUTTONFLASHTRANSITION_FADE_TO_GRADE_IN_1 - 1,
		NOTHING_TO_DRAW = -1
	};

	Int drawState( void ) const { return m_drawState; }
};

TEST(button_flash_draws_nothing_for_a_button_the_layout_lacks)
{
	ButtonFlashWithoutWindow flash;
	for( Int frame = ButtonFlashWithoutWindow::PAUSE_FIRST_FRAME; frame <= ButtonFlashWithoutWindow::PAUSE_LAST_FRAME; ++frame )
	{
		flash.update( frame );
		CHECK_EQ( flash.drawState(), (Int)ButtonFlashWithoutWindow::NOTHING_TO_DRAW );
	}
}

// Camera scroll timing must advance during stationary frames as well as moving frames.
#include "GameClient/CameraScrollClock.h"

TEST(camera_scroll_restarts_after_idle_without_a_jump)
{
	CameraScrollClock clock;
	CHECK_NEAR(clock.sample(1000, 33), 0.0f, 0.00001f);
	for (uint32_t now = 1016; now <= 17000; now += 16)
		clock.sample(now, 33); // stationary render frames
	CHECK_NEAR(clock.sample(17016, 33), 16.0f / 33.0f, 0.00001f);
	CHECK_NEAR(clock.sample(17032, 33), 16.0f / 33.0f, 0.00001f);
}

TEST(camera_scroll_elapsed_time_is_independent_of_render_rate)
{
	const int rates[] = {30, 60, 144, 240};
	for (int r = 0; r < 4; ++r)
	{
		CameraScrollClock clock;
		clock.sample(1000, 33);
		float distance = 0.0f;
		for (int frame = 1; frame <= rates[r]; ++frame)
			distance += clock.sample(1000 + frame * 1000 / rates[r], 33);
		CHECK_NEAR(distance, 1000.0f / 33.0f, 0.0001f);
	}
}

TEST(camera_scroll_clocks_are_independent_and_handle_timer_wrap)
{
	CameraScrollClock first, second;
	first.sample(0xfffffff0u, 33);
	second.sample(2000, 33);
	CHECK_NEAR(first.sample(0u, 33), 16.0f / 33.0f, 0.00001f);
	CHECK_NEAR(second.sample(2033, 33), 1.0f, 0.00001f);
	CHECK_NEAR(second.sample(9000, 33), 3.0f, 0.00001f);
}

TEST(camera_preferences_default_to_a_finite_map_margin)
{
	CHECK(bootOnce());
	GlobalData *saved = TheWritableGlobalData;
	GlobalData *scratch = NEW GlobalData;
	TheWritableGlobalData = scratch;
	CHECK(scratch->m_useCameraConstraints);
	CHECK_EQ(scratch->m_cameraBoundaryMargin, 200);
	const OptionDef *bounds = findOptionDef("UseCameraConstraints");
	CHECK(bounds != NULL);
	if (bounds)
	{
		bounds->set(0);
		CHECK(!scratch->m_useCameraConstraints);
	}
	TheWritableGlobalData = saved;
	delete scratch;
}
#include "test_camera_behavior.inc"
#include "test_production_input.inc"
#include "test_minimap_input.inc"
#include "test_selection_priority.inc"
#include "test_supply_center_save.inc"
#include "test_cinema.inc"
