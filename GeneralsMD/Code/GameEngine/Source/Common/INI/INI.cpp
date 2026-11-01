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

// FILE: INI.cpp //////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:   INI Reader
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#define DEFINE_DEATH_NAMES

#include "Common/INI.h"
#include "stringex.h"
#include "Common/INIException.h"

#include "Common/DamageFX.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Science.h"
#include "Common/StackDump.h"	// g_LastErrorDump, see prepFile
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"

#include "GameClient/Anim2D.h"
#include "GameClient/Color.h"
#include "GameClient/FXList.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

static Xfer *s_xfer = NULL;

//-------------------------------------------------------------------------------------------------
/** This is the table of data types we can have in INI files.  To add a new data type
	* block make a new entry in this table and add an appropriate parsing function */
//-------------------------------------------------------------------------------------------------
extern void parseReallyLowMHz( INI* ini);		// yeah, so sue me (srj)
struct BlockParse
{
	const char *token;
	INIBlockParse parse;
};
static const BlockParse theTypeTable[] =
{
	{ "AIData",							INI::parseAIDataDefinition },
	{ "Animation",					INI::parseAnim2DDefinition },
	{ "Armor",							INI::parseArmorDefinition },
	{ "AudioEvent",					INI::parseAudioEventDefinition },
	{ "AudioSettings",			INI::parseAudioSettingsDefinition },
	{ "Bridge",							INI::parseTerrainBridgeDefinition },
	{ "Campaign",						INI::parseCampaignDefinition },
 	{ "ChallengeGenerals",				INI::parseChallengeModeDefinition },
	{ "CommandButton",			INI::parseCommandButtonDefinition },
	{ "CommandButtonHotKey",	INI::parseCommandButtonHotKeyDefinition },
	{ "CommandBarGrid",			INI::parseCommandBarGridDefinition },
	{ "FixedOrderKeys",			INI::parseFixedOrderKeysDefinition },
	{ "CommandMap",					INI::parseMetaMapDefinition },
	{ "CommandSet",					INI::parseCommandSetDefinition },
	{ "ControlBarScheme",		INI::parseControlBarSchemeDefinition },
	{ "CrateData",					INI::parseCrateTemplateDefinition },
	{ "Credits",						INI::parseCredits},
	{ "WindowTransition",		INI::parseWindowTransitions},
	{ "DamageFX",						INI::parseDamageFXDefinition },
	{ "DialogEvent",				INI::parseDialogDefinition },
	{ "DrawGroupInfo",		INI::parseDrawGroupNumberDefinition },
	{ "EvaEvent",						INI::parseEvaEvent },
	{ "FXList",							INI::parseFXListDefinition },
	{ "GameData",						INI::parseGameDataDefinition },
	{ "InGameUI",						INI::parseInGameUIDefinition },
	{ "Locomotor",					INI::parseLocomotorTemplateDefinition },
	{ "Language",						INI::parseLanguageDefinition },
	{ "MapCache",						INI::parseMapCacheDefinition },
	{ "MapData",						INI::parseMapDataDefinition },
	{ "MappedImage",				INI::parseMappedImageDefinition },
	{ "MiscAudio",					INI::parseMiscAudio},
	{ "Mouse",							INI::parseMouseDefinition },
	{ "MouseCursor",				INI::parseMouseCursorDefinition },
	{ "MultiplayerColor",		INI::parseMultiplayerColorDefinition },
  { "MultiplayerStartingMoneyChoice",		INI::parseMultiplayerStartingMoneyChoiceDefinition },
	{ "OnlineChatColors",		INI::parseOnlineChatColorDefinition },
	{ "MultiplayerSettings",INI::parseMultiplayerSettingsDefinition },
	{ "MusicTrack",					INI::parseMusicTrackDefinition },
	{ "Object",							INI::parseObjectDefinition },
	{ "ObjectCreationList",	INI::parseObjectCreationListDefinition },
	{ "ObjectReskin",				INI::parseObjectReskinDefinition },
	{ "ParticleSystem",			INI::parseParticleSystemDefinition },
	{ "PlayerTemplate",			INI::parsePlayerTemplateDefinition },
	{ "Road",								INI::parseTerrainRoadDefinition },
	{ "Science",						INI::parseScienceDefinition },
	{ "Rank",								INI::parseRankDefinition },
	{ "SpecialPower",				INI::parseSpecialPowerDefinition },
	{ "ShellMenuScheme",		INI::parseShellMenuSchemeDefinition },
	{ "Terrain",						INI::parseTerrainDefinition },
	{ "Upgrade",						INI::parseUpgradeDefinition },
	{ "Veterancy",					GlobalData::parseVeterancyDefinition },
	{ "Video",							INI::parseVideoDefinition },
	{ "WaterSet",						INI::parseWaterSettingDefinition },
	{ "WaterTransparency",	INI::parseWaterTransparencyDefinition},
	{ "Weather",	INI::parseWeatherDefinition},
	{ "Weapon",							INI::parseWeaponTemplateDefinition },
	{ "WebpageURL",					INI::parseWebpageURLDefinition },
	{ "HeaderTemplate",			INI::parseHeaderTemplateDefinition },
	{ "StaticGameLOD",			INI::parseStaticGameLODDefinition },
	{ "DynamicGameLOD",			INI::parseDynamicGameLODDefinition },
	{ "LODPreset",					INI::parseLODPreset },
	{	"BenchProfile",				INI::parseBenchProfile },
	{	"ReallyLowMHz",				parseReallyLowMHz },
	{	"ScriptAction",				ScriptEngine::parseScriptAction },
	{	"ScriptCondition",		ScriptEngine::parseScriptCondition },
	
	{ NULL,									NULL },		// keep this last!
};


///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
Bool INI::isValidINIFilename( const char *filename )
{
	if( filename == NULL )
		return FALSE;

	Int len = strlen( filename );
	if( len < 3 )
		return FALSE;

	if( filename[ len - 1 ] != 'I' && filename[ len - 1 ] != 'i' )
		return FALSE;

	if( filename[ len - 2 ] != 'N' && filename[ len - 2 ] != 'n' )
		return FALSE;

	if( filename[ len - 3 ] != 'I' && filename[ len - 3 ] != 'i' )
		return FALSE;

	return TRUE;

} 

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
INI::INI( void )
{

	m_file							= NULL;
  m_readBufferNext=m_readBufferUsed=0;
	m_filename					= "None";
	m_loadType					= INI_LOAD_INVALID;
	m_lineNum						= 0;
	m_seps							= " \n\r\t=";			///< make sure you update m_sepsPercent/m_sepsColon as well
	m_sepsPercent				= " \n\r\t=%%";
	m_sepsColon					= " \n\r\t=:";
	m_sepsQuote					= "\"\n=";				///< stop at " = EOL
	m_blockEndToken			= "END";
	m_endOfFile					= FALSE;
	m_skipUnknownFields	= FALSE;
	m_missingReference	= FALSE;
	m_buffer[0]					= 0;
#if defined(_DEBUG) || defined(_INTERNAL)
	m_curBlockStart[0]	= 0;
#endif

}  // end INI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
INI::~INI( void )
{

}  // end ~INI

//-------------------------------------------------------------------------------------------------
/** Load all INI files in the specified directory (and subdirectories if indicated).
	* If we are to load subdirectories, we will load them *after* we load all the
	* files in the current directory */
//-------------------------------------------------------------------------------------------------
void INI::loadDirectory( AsciiString dirName, Bool subdirs, INILoadType loadType, Xfer *pXfer )
{
	// sanity
	if( dirName.isEmpty() )
		throw INI_INVALID_DIRECTORY;

	try
	{
		FilenameList filenameList;
		dirName.concat('\\');
		TheFileSystem->getFileListInDirectory(dirName, "*.ini", filenameList, TRUE);
		// Load the INI files in the dir now, in a sorted order.  This keeps things the same between machines
		// in a network game.
		FilenameList::const_iterator it = filenameList.begin();
		while (it != filenameList.end())
		{
			AsciiString tempname;
			tempname = (*it).str() + dirName.getLength();

			if ((tempname.find('\\') == NULL) && (tempname.find('/') == NULL)) {
				// this file doesn't reside in a subdirectory, load it first.
				load( *it, loadType, pXfer );
			}
			++it;
		}

		it = filenameList.begin();
		while (it != filenameList.end())
		{
			AsciiString tempname;
			tempname = (*it).str() + dirName.getLength();

			if ((tempname.find('\\') != NULL) || (tempname.find('/') != NULL)) {
				load( *it, loadType, pXfer );
			}
			++it;
		}
	} 
	catch (...) 
	{
		// propagate the exception
		throw;
	}

}  // end loadDirectory

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::prepFile( AsciiString filename, INILoadType loadType )
{
	// if we have a file open already -- we can't do another one
	if( m_file != NULL )
	{

		DEBUG_CRASH(( "INI::load, cannot open file '%s', file already open\n", filename.str() ));
		throw INI_FILE_ALREADY_OPEN;

	}  // end if

	// open the file
	m_file = TheFileSystem->openFile(filename.str(), File::READ);
	if( m_file == NULL )
	{

		// The INI_* codes live in an *unnamed* enum, so GameEngine::init's catch (ErrorCode)
		// never matched INI_CANT_OPEN_FILE and its catch (...) said only "Uncaught Exception
		// during initialization.".  An INIException carries the name to the crash report.
		DEBUG_CRASH(( "INI::load, cannot open file '%s'\n", filename.str() ));
		char buff[1024];
		snprintf(buff, ARRAY_SIZE(buff), "Could not open INI file '%s'\n", filename.str());
		throw INIException(buff);

	}  // end if

	m_file = m_file->convertToRAMFile();

	// save our filename
	m_filename = filename;

	// save our load time
	m_loadType = loadType;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::unPrepFile()
{
	// close the file
	m_file->close();
	m_file = NULL;
  m_readBufferUsed=m_readBufferNext=0;
	m_filename = "None";
	m_loadType = INI_LOAD_INVALID;
	m_lineNum = 0;
	m_endOfFile = FALSE;
	s_xfer = NULL;
}

//-------------------------------------------------------------------------------------------------
/** The blocks a map's rules file (map.ini, solo.ini, read INI_LOAD_CREATE_OVERRIDES) may hold: the
	* ones whose stores put the map's edits back at the end of the match.  Every other block in the
	* table either wrote over the game's own data for good (the mouse, the in-game UI, multiplayer
	* colours, terrain types, campaigns, videos) or refused a name an earlier map had added (roads,
	* bridges, animations, EVA events), so the same map read differently on a machine that had played
	* another first.  No shipped map uses one; one in a custom map is skipped and the rest of the file
	* still loads, on every machine alike.  MappedImage stays: an image is drawn, never read by
	* the simulation, and custom maps bring their own cameos with it. */
//-------------------------------------------------------------------------------------------------
static Bool isMapINIBlock(const char* token)
{
	static const char *const MapINIBlocks[] =
	{
		"AIData", "Armor", "AudioEvent", "CommandButton", "CommandSet", "CrateData", "DamageFX",
		"DialogEvent", "FXList", "GameData", "Locomotor", "MappedImage", "MusicTrack", "Object",
		"ObjectCreationList", "ObjectReskin", "ParticleSystem", "PlayerTemplate", "Rank", "Science",
		"SpecialPower", "Upgrade", "WaterSet", "WaterTransparency", "Weather", "Weapon",
		NULL
	};
	for (const char *const *name = MapINIBlocks; *name; ++name)
	{
		if (strcmp(*name, token) == 0)
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
static INIBlockParse findBlockParse(const char* token)
{
	for (const BlockParse* parse = theTypeTable; parse->token; ++parse)
	{
		if (strcmp( parse->token, token ) == 0)
		{
			return parse->parse;
		}
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------
static INIFieldParseProc findFieldParse(const FieldParse* parseTable, const char* token, int& offset, const void*& userData)
{
	// parse is read after the loop (the catch-all entry with a null token), so
	// it cannot live in the for-scope the way VC6 let it.
	const FieldParse* parse;
	for (parse = parseTable; parse->token; ++parse)
	{
		if (strcmp( parse->token, token ) == 0)
		{
			offset = parse->offset;
			userData = parse->userData;
			return parse->parse;
		}
	}

	if (!parse->token && parse->parse) 
	{
		offset = parse->offset;
		userData = token;
		return parse->parse;
	}
	else
	{
		return NULL;
	}
}

//-------------------------------------------------------------------------------------------------
/** Load and parse an INI file */
//-------------------------------------------------------------------------------------------------
void INI::load( AsciiString filename, INILoadType loadType, Xfer *pXfer )
{
	setFPMode(); // so we have consistent Real values for GameLogic -MDC

	s_xfer = pXfer;
	prepFile(filename, loadType);

	try
	{

		// read all lines in the file
		DEBUG_ASSERTCRASH( m_endOfFile == FALSE, ("INI::load, EOF at the beginning!\n") );
		while( m_endOfFile == FALSE )
		{
			// read this line
			readLine();

			AsciiString currentLine = m_buffer;

			// the first word is the type of data we're processing
			const char *token = strtok( m_buffer, m_seps );
			if( token )
			{
				INIBlockParse parse = findBlockParse(token);
				if (parse && m_loadType == INI_LOAD_CREATE_OVERRIDES && !isMapINIBlock(token))
				{
					/* Read past it and go on with the file.  Nothing parses it, so its End is found by
						 counting: an End closes, a line with no '=' opens a sub-block (Mission, SideSounds,
						 a radius cursor), every other line is a field.  The count is the same on every
						 machine, so is what follows.
						 ponytail: a field written without its '=' counts as an opening and the skip runs
						 on past the block; a real parse of each refused type if a map ever needs it. */
					DEBUG_LOG(("[LINE: %d - FILE: '%s'] a map's rules file may not hold a '%s' block, skipped\n",
										 getLineNum(), getFilename().str(), token));
					Int depth = 1;
					while( depth > 0 && m_endOfFile == FALSE )
					{
						readLine();
						const Bool isField = strchr( m_buffer, '=' ) != NULL;
						const char *word = strtok( m_buffer, m_seps );
						if( word == NULL )
							continue;
						if( strcasecmp( word, m_blockEndToken ) == 0 )
							--depth;
						else if( !isField )
							++depth;
					}
					continue;
				}
				if (parse)
				{
					#if defined(_DEBUG) || defined(_INTERNAL)
					strcpy(m_curBlockStart, m_buffer);
					#endif
					try {
						(*parse)( this );

					} catch (...) {
						DEBUG_CRASH(("Error parsing block '%s' in INI file '%s'\n", token, m_filename.str()) );
						/* The line the block *started* on, which is what this used to report on its own,
							 is rarely the line that is wrong: the parser has read its way down the block by
							 the time anything throws.  m_lineNum is where it actually got to, and the last
							 thing it read is usually the answer.  Without it a report of this reads "one of
							 these nine fields", and the only way on is to guess. */
						char buff[1024];
						snprintf(buff, ARRAY_SIZE(buff), "Error parsing INI file '%s' (Line: '%s', reached line %d: '%s')\n",
							m_filename.str(), currentLine.str(), m_lineNum, m_buffer);

						throw INIException(buff);
					}
					#if defined(_DEBUG) || defined(_INTERNAL)
						strcpy(m_curBlockStart, "NO_BLOCK");
					#endif
				}
				else
				{
					DEBUG_ASSERTCRASH( 0, ("[LINE: %d - FILE: '%s'] Unknown block '%s'\n",
														 getLineNum(), getFilename().str(), token ) );
					// a stray End, a misspelt block name: name it, since the catch below cannot
					char buff[1024];
					snprintf(buff, ARRAY_SIZE(buff), "Error parsing INI file '%s' (line %d: unknown block '%s')\n",
						m_filename.str(), m_lineNum, token);
					throw INIException(buff);
				}

			}  // end if

		}  // end while
	}
	catch (INIException &)
	{
		unPrepFile();
		throw;
	}
	catch (...)
	{
		/* Anything else thrown between blocks (a bare INI_* code from readLine, a fault the SEH
			 translator turned into a throw) used to reach GameEngine::init's catch (...) as
			 "Uncaught Exception during initialization." with no file named. */
		char buff[1024];
		snprintf(buff, ARRAY_SIZE(buff), "Error reading INI file '%s' (reached line %d: '%s')\n",
			m_filename.str(), m_lineNum, m_buffer);
		unPrepFile();
		throw INIException(buff);
	}

	unPrepFile();

}  // end load

//-------------------------------------------------------------------------------------------------
/** -modfactions: one pass of path per -mod, each seeing only its own files and reading its own names
	* with the mod's prefix.  With no full -mod this does nothing. */
//-------------------------------------------------------------------------------------------------
void INI::loadModPasses( const char *path, Xfer *pXfer, Bool directory )
{
	// the engine tests bring the control bar and the particle systems up without any GlobalData
	if (TheGlobalData == NULL || !TheGlobalData->m_modFactions)
		return;

	for (Int mod = 0; mod < (Int)TheGlobalData->m_mods.size(); ++mod)
	{
		TheFileSystem->setFocus( mod );
		TheModNamespace.setActive( mod );
		INI ini;
		if (directory)
			ini.loadDirectory( AsciiString( path ), TRUE, INI_LOAD_OVERWRITE, pXfer );
		else if (TheFileSystem->doesFileExist( path ))
			ini.load( AsciiString( path ), INI_LOAD_OVERWRITE, pXfer );
		TheModNamespace.setActive( -1 );
		TheFileSystem->setFocus( FILE_FOCUS_ALL );
	}
}

//-------------------------------------------------------------------------------------------------
// ModNamespace
//-------------------------------------------------------------------------------------------------
ModNamespace TheModNamespace;

const char *const ModNamespace::FactionFiles[] =
{
	"Data\\INI\\Science.ini", "Data\\INI\\PlayerTemplate.ini", "Data\\INI\\FXList.ini", "Data\\INI\\Weapon.ini",
	"Data\\INI\\ObjectCreationList.ini", "Data\\INI\\Locomotor.ini", "Data\\INI\\SpecialPower.ini",
	"Data\\INI\\DamageFX.ini", "Data\\INI\\Armor.ini", "Data\\INI\\Object", "Data\\INI\\Upgrade.ini",
	"Data\\INI\\CommandButton.ini", "Data\\INI\\CommandSet.ini", "Data\\INI\\ParticleSystem.ini",
	"Data\\INI\\MappedImages", "Data\\INI\\Music.ini",
	"Data\\INI\\SoundEffects.ini", "Data\\INI\\Speech.ini", "Data\\INI\\Voice.ini",
	NULL
};

/// where a mod's art and sound sit; a word in its INI that names one of these files is read as the mod's copy
static const char *const ModAssetFolders[] = { "Art\\W3D\\", "Art\\Textures\\", "Data\\Audio\\" };

/// the block keywords whose second word is a name the mod defines
static const struct { const char *keyword; ModNameType type; } ModBlockTypes[] =
{
	{ "Object", MODNAME_OBJECT }, { "ObjectReskin", MODNAME_OBJECT }, { "ChildObject", MODNAME_OBJECT },
	{ "Weapon", MODNAME_WEAPON }, { "Armor", MODNAME_ARMOR }, { "DamageFX", MODNAME_DAMAGEFX },
	{ "Upgrade", MODNAME_UPGRADE }, { "Science", MODNAME_SCIENCE }, { "SpecialPower", MODNAME_SPECIALPOWER },
	{ "CommandSet", MODNAME_COMMANDSET }, { "CommandButton", MODNAME_COMMANDBUTTON }, { "FXList", MODNAME_FXLIST },
	{ "ObjectCreationList", MODNAME_OCL }, { "ParticleSystem", MODNAME_PARTICLESYSTEM },
	{ "Locomotor", MODNAME_LOCOMOTOR }, { "PlayerTemplate", MODNAME_PLAYERTEMPLATE },
	{ "MappedImage", MODNAME_MAPPEDIMAGE },
	{ "AudioEvent", MODNAME_AUDIOEVENT }, { "DialogEvent", MODNAME_AUDIOEVENT }, { "MusicTrack", MODNAME_AUDIOEVENT },
};

void ModNamespace::scanMods( void )
{
	m_mods.assign( TheGlobalData->m_mods.size(), Names() );
	for (Int mod = 0; mod < (Int)m_mods.size(); ++mod)
	{
		TheFileSystem->setFocus( mod );
		FilenameList files;
		for (const char *const *path = FactionFiles; *path != NULL; ++path)
		{
			if (strstr( *path, ".ini" ) == NULL)
			{
				AsciiString dir( *path );
				dir.concat( '\\' );
				TheFileSystem->getFileListInDirectory( dir, "*.ini", files, TRUE );
			}
			else if (TheFileSystem->doesFileExist( *path ))
			{
				files.insert( AsciiString( *path ) );
			}
		}
		for (FilenameListIter it = files.begin(); it != files.end(); ++it)
		{
			File *file = TheFileSystem->openFile( it->str(), File::READ );
			const Int size = file->size();
			char *contents = file->readEntireAndClose();
			std::string text( contents, size );
			delete [] contents;
			// as INI::readLine: a byte order mark would hide the first block's name
			if (text.compare( 0, 3, "\xEF\xBB\xBF" ) == 0)
				text.erase( 0, 3 );
			for (size_t start = 0; start < text.size(); )
			{
				size_t end = text.find( '\n', start );
				if (end == std::string::npos)
					end = text.size();
				scanLine( mod, text.substr( start, end - start ).c_str() );
				start = end + 1;
			}
		}
		finishScan( mod );

		FilenameList assetFiles;
		for (size_t folder = 0; folder < ARRAY_SIZE( ModAssetFolders ); ++folder)
			TheFileSystem->getFileListInDirectory( AsciiString( ModAssetFolders[ folder ] ), "*.*", assetFiles, TRUE );
		for (FilenameListIter it = assetFiles.begin(); it != assetFiles.end(); ++it)
			addAsset( mod, it->str() );
		TheGameText->loadModStrings( mod );
		TheFileSystem->setFocus( FILE_FOCUS_ALL );

		const Names &names = m_mods[ mod ];
		DEBUG_LOG(( "MODNS M%d~ is '%s': %d names from %d files, %d objects, %d weapons, %d upgrades, %d sciences, %d player templates, %d images, %d audio events, %d asset names\n",
			mod + 1, TheGlobalData->m_modNames[ mod ].str(), (Int)names.all.size(), (Int)files.size(),
			(Int)names.byType[ MODNAME_OBJECT ].size(), (Int)names.byType[ MODNAME_WEAPON ].size(),
			(Int)names.byType[ MODNAME_UPGRADE ].size(), (Int)names.byType[ MODNAME_SCIENCE ].size(),
			(Int)names.byType[ MODNAME_PLAYERTEMPLATE ].size(), (Int)names.byType[ MODNAME_MAPPEDIMAGE ].size(),
			(Int)names.byType[ MODNAME_AUDIOEVENT ].size(), (Int)names.assets.size() ));
	}
}

void ModNamespace::scanLine( Int mod, const char *line )
{
	static const char *SPACE = " \t\r\n";
	static const char *WORD_END = " \t\r\n=";
	std::string text( line );
	const size_t comment = text.find( ';' );
	if (comment != std::string::npos)
		text.erase( comment );

	const size_t keywordAt = text.find_first_not_of( SPACE );
	if (keywordAt == std::string::npos)
		return;
	const size_t keywordEnd = text.find_first_of( WORD_END, keywordAt );
	if (keywordEnd == std::string::npos)
		return;
	const std::string keyword = text.substr( keywordAt, keywordEnd - keywordAt );
	size_t nameAt = text.find_first_not_of( SPACE, keywordEnd );
	if (nameAt == std::string::npos)
		return;

	if ((Int)m_mods.size() <= mod)
		m_mods.resize( mod + 1 );
	Names &names = m_mods[ mod ];
	if (keyword == "Side" || keyword == "BaseSide")
	{
		if (text[ nameAt ] == '=')
			nameAt = text.find_first_not_of( SPACE, nameAt + 1 );
		if (nameAt != std::string::npos)
			names.sides.insert( text.substr( nameAt, text.find_first_of( WORD_END, nameAt ) - nameAt ) );
		return;
	}
	// "Weapon = PRIMARY X" is a field, "Weapon X" a definition
	if (text[ nameAt ] == '=')
		return;

	for (size_t i = 0; i < ARRAY_SIZE( ModBlockTypes ); ++i)
	{
		if (keyword == ModBlockTypes[ i ].keyword)
		{
			const std::string name = text.substr( nameAt, text.find_first_of( WORD_END, nameAt ) - nameAt );
			names.all.insert( name );
			names.byType[ ModBlockTypes[ i ].type ].insert( name );
			return;
		}
	}
}

void ModNamespace::finishScan( Int mod )
{
	// a side is compared as text everywhere (Side = GLA), and The End of Days names a CommandSet "GLA"
	Names &names = m_mods[ mod ];
	for (std::set<std::string>::const_iterator it = names.sides.begin(); it != names.sides.end(); ++it)
		names.all.erase( *it );
	// nor a word the parser reads for itself: The Power of the West has an "FXList No", and every
	// "IsGrantable = No" became "M2~No", which is no Bool
	static const char *const Reserved[] = { "none", "no", "yes", "true", "false", "end", "default" };
	for (std::set<std::string>::iterator it = names.all.begin(); it != names.all.end(); )
	{
		Bool reserved = FALSE;
		for (size_t i = 0; i < ARRAY_SIZE( Reserved ) && !reserved; ++i)
			reserved = strcasecmp( it->c_str(), Reserved[ i ] ) == 0;
		if (reserved)
			it = names.all.erase( it );
		else
			++it;
	}
}

/// A-Z only: the result goes into the INI checksum, so no locale may decide it
static std::string foldAscii( const std::string &text )
{
	std::string folded( text );
	for (size_t i = 0; i < folded.size(); ++i)
	{
		if (folded[ i ] >= 'A' && folded[ i ] <= 'Z')
			folded[ i ] = (char)(folded[ i ] - 'A' + 'a');
	}
	return folded;
}

void ModNamespace::addAsset( Int mod, const char *fileName )
{
	const char *slash = strrchr( fileName, '\\' );
	const std::string name = foldAscii( slash != NULL ? slash + 1 : fileName );
	const size_t dot = name.rfind( '.' );
	if (dot == std::string::npos)
		return;
	const std::string base = name.substr( 0, dot );
	const std::string extension = name.substr( dot + 1 );
	if ((Int)m_mods.size() <= mod)
		m_mods.resize( mod + 1 );
	std::set<std::string> &assets = m_mods[ mod ].assets;
	// INI names a model or a sound without its extension, and a texture as .tga when the file is a .dds
	if (extension == "w3d")
		assets.insert( base );
	else if (extension == "wav" || extension == "mp3")
	{
		// Sounds names the base, a MusicTrack or streamed event's Filename the whole file name
		assets.insert( base );
		assets.insert( name );
	}
	else if (extension == "dds" || extension == "tga")
	{
		assets.insert( base + ".tga" );
		assets.insert( base + ".dds" );
	}
	else
		assets.insert( name );
}

Bool ModNamespace::isAsset( const Names &names, const std::string &word )
{
	const std::string name = foldAscii( word );
	if (names.assets.count( name ) != 0)
		return TRUE;
	// an animation is "<hierarchy>.<animation>", and the file is the animation's
	const size_t dot = name.find( '.' );
	if (dot != std::string::npos)
		return names.assets.count( name.substr( dot + 1 ) ) != 0;
	// a shadow or radius decal names its texture with no extension, and the engine appends ".tga"
	return names.assets.count( name + ".tga" ) != 0;
}

Bool ModNamespace::isAssetField( const std::string &field )
{
	// what a mod pass reads that names a file: draw modules, trees and props, debris, beams, particles,
	// shadows, mapped images and audio.  ShowSubObject and the bone fields stay out, their words are
	// names inside a model and can match a file
	static const char *const AssetFields[] =
	{
		"Model", "Animation", "IdleAnimation", "ModelName", "ModelNames", "AnimationSet", "TextureName",
		"Texture", "ParticleName", "TrackMarks", "ShadowTexture",
		"Sounds", "SoundsMorning", "SoundsEvening", "SoundsNight", "Attack", "Decay", "Filename",
	};
	for (size_t i = 0; i < ARRAY_SIZE( AssetFields ); ++i)
	{
		if (strcasecmp( field.c_str(), AssetFields[ i ] ) == 0)
			return TRUE;
	}
	return FALSE;
}

/// "0", "6", "-1.5", "25%": digits with at most a sign, points and a closing percent
static Bool isNumber( const std::string &word )
{
	Bool digit = FALSE;
	for (size_t i = 0; i < word.size(); ++i)
	{
		const char c = word[ i ];
		if (c >= '0' && c <= '9')
			digit = TRUE;
		else if (!(c == '.' || ((c == '-' || c == '+') && i == 0) || (c == '%' && i + 1 == word.size())))
			return FALSE;
	}
	return digit;
}

Bool ModNamespace::isNameOrAsset( const std::string &word, Bool assetField ) const
{
	// A number is a number.  An animation's distance and repeat count sit in a file field, and with a
	// texture called 6.dds in the mod "IdleAnimation = X.X 0 6" read "M2~6", which is no Int: a parse
	// error at load under -modfactions.
	if (isNumber( word ))
		return FALSE;
	const Names &names = m_mods[ m_active ];
	return names.all.count( word ) != 0 || (assetField && isAsset( names, word ));
}

Bool ModNamespace::isModLabel( const std::string &word, Bool ) const
{
	// the engine tests read mod INI with no GameText up
	return word.find( ':' ) != std::string::npos && TheGameText != NULL
		&& TheGameText->hasModLabel( m_active, word.c_str() );
}

Bool ModNamespace::rewriteLine( char *line, size_t capacity ) const
{
	return prefixWords( line, capacity, &ModNamespace::isNameOrAsset );
}

Bool ModNamespace::rewriteLabels( char *line, size_t capacity ) const
{
	return prefixWords( line, capacity, &ModNamespace::isModLabel );
}

Bool ModNamespace::prefixWords( char *line, size_t capacity, WordTest test ) const
{
	char prefix[ 16 ];
	snprintf( prefix, sizeof( prefix ), "M%d~", m_active + 1 );

	// INI's own separators; readLine has already turned every other control character into a space
	std::string out;
	Bool first = TRUE;
	Bool assetField = FALSE;
	for (const char *p = line; *p != 0; )
	{
		if (*p == ' ' || *p == '=')
		{
			out += *p++;
			continue;
		}
		const char *start = p;
		while (*p != 0 && *p != ' ' && *p != '=')
			++p;
		const std::string word( start, p - start );
		if (first)
		{
			// only a field that names a file: a bone or sub-object can share a model's name
			assetField = isAssetField( word );
			out += word;
			first = FALSE;
			continue;
		}
		// a file field may name a subfolder ("Sub\x.mp3"); the file is its last part.  "OCL:OCL_X" is a
		// parser's keyword and a name: only the name may be the mod's
		const size_t at = assetField ? lastPathPart( word ) : namedAfterKeyword( word );
		out.append( word, 0, at );
		if ((this->*test)( word.substr( at ), assetField ))
			out += prefix;
		out.append( word, at, std::string::npos );
	}
	if (out.size() >= capacity)
		return FALSE;
	memcpy( line, out.c_str(), out.size() + 1 );
	return TRUE;
}

size_t ModNamespace::lastPathPart( const std::string &word )
{
	const size_t slash = word.find_last_of( "\\/" );
	return slash == std::string::npos ? 0 : slash + 1;
}

size_t ModNamespace::namedAfterKeyword( const std::string &word )
{
	// the keywords the colon parsers (OCLUpdate, BridgeBehavior, BoneFXUpdate, TransitionDamageFX,
	// AutoDepositUpdate) read before a name; a CSF label such as "OBJECT:Ranger" is none of them
	static const char *const Keywords[] = { "Faction", "OCL", "FX", "FXList", "PSys", "UpgradeType" };
	const size_t colon = word.find( ':' );
	if (colon == std::string::npos)
		return 0;
	const std::string keyword = word.substr( 0, colon );
	for (size_t i = 0; i < ARRAY_SIZE( Keywords ); ++i)
	{
		if (strcasecmp( keyword.c_str(), Keywords[ i ] ) == 0)
			return colon + 1;
	}
	return 0;
}

Bool ModNamespace::findRetailName( const AsciiString &name, ModNameType type, AsciiString *retailName ) const
{
	const char *text = name.str();
	// image and sound names may have been lowercased on the way here
	if (m_mods.empty() || (text[ 0 ] != 'M' && text[ 0 ] != 'm'))
		return FALSE;
	char *tilde = NULL;
	const long mod = strtol( text + 1, &tilde, 10 );
	if (tilde == text + 1 || *tilde != '~' || mod < 1 || mod > (long)m_mods.size())
		return FALSE;
	if (m_mods[ mod - 1 ].byType[ type ].count( tilde + 1 ) != 0)
		return FALSE;
	*retailName = tilde + 1;
	return TRUE;
}

Bool ModNamespace::findRetailKey( NameKeyType key, ModNameType type, NameKeyType *retailKey ) const
{
	AsciiString retailName;
	if (m_mods.empty() || !findRetailName( TheNameKeyGenerator->keyToName( key ), type, &retailName ))
		return FALSE;
	*retailKey = TheNameKeyGenerator->nameToKey( retailName );
	return TRUE;
}

AsciiString ModNamespace::modName( Int mod, ModNameType type, const AsciiString &name ) const
{
	if (mod >= (Int)m_mods.size() || m_mods[ mod ].byType[ type ].count( name.str() ) == 0)
		return name;
	AsciiString prefixed;
	prefixed.format( "M%d~%s", mod + 1, name.str() );
	return prefixed;
}

//-------------------------------------------------------------------------------------------------
/** Read a line from the already open file.  Any comments will be remved and
	* therefore ignored from any given line */
//-------------------------------------------------------------------------------------------------
void INI::readLine( void )
{
	// sanity
	DEBUG_ASSERTCRASH( m_file, ("readLine(), file pointer is NULL\n") );

  if (m_endOfFile)
    *m_buffer=0;
  else
  {
    char *p=m_buffer;
    while (p!=m_buffer+INI_MAX_CHARS_PER_LINE)
    {
      // get next character
      if (m_readBufferNext==m_readBufferUsed)
      {
        // refill buffer
        m_readBufferNext=0;
        m_readBufferUsed=m_file->read(m_readBuffer,INI_READ_BUFFER);

        // EOF?
        if (!m_readBufferUsed)
        {
          m_endOfFile=true;
          *p=0;
          break;
        }
      }
      *p=m_readBuffer[m_readBufferNext++];

      // CR?
      if (*p=='\n')
      {
        *p=0;
        break;
      }

      DEBUG_ASSERTCRASH(*p != '\t', ("tab characters are not allowed in INI files (%s). please check your editor settings. Line Number %d\n",m_filename.str(), getLineNum()));

      // comment?
      if (*p==';')
        *p=0;
      // whitespace?
      else if (*p>0&&*p<32)
        *p=' ';
      p++;
    }
    *p=0;

		// increase our line count
		m_lineNum++;

		// A UTF-8 byte order mark, which some editors write, glued itself to the first word and the
		// block was unknown.  No file that had one ever loaded, so dropping it moves no checksum.
		if (m_lineNum == 1 && strncmp(m_buffer, "\xEF\xBB\xBF", 3) == 0)
			memmove(m_buffer, m_buffer + 3, strlen(m_buffer + 3) + 1);

		// check for at the max
		if ( p == m_buffer+INI_MAX_CHARS_PER_LINE )
		{

			DEBUG_ASSERTCRASH( 0, ("Buffer too small (%d) and was truncated, increase INI_MAX_CHARS_PER_LINE\n", 
														 INI_MAX_CHARS_PER_LINE) );

		}  // end if
  }

	// -modfactions: the mod's own names, before the checksum below reads the line
	if (TheModNamespace.getActive() >= 0 && !TheModNamespace.rewriteLine(m_buffer, sizeof(m_buffer)))
	{
		g_LastErrorDump.format("INI::readLine: line %d of '%s' is too long once its mod names are prefixed\n", m_lineNum, m_filename.str());
		DEBUG_LOG(("%s", g_LastErrorDump.str()));
		throw INI_BUFFER_TOO_SMALL;
	}

	if (s_xfer)
	{
		s_xfer->xferUser( m_buffer, sizeof( char ) * strlen( m_buffer ) );
		//DEBUG_LOG(("Xfer val is now 0x%8.8X in %s, line %s\n", ((XferCRC *)s_xfer)->getCRC(),
			//m_filename.str(), m_buffer));
	}

	if (TheModNamespace.getActive() >= 0 && !TheModNamespace.rewriteLabels(m_buffer, sizeof(m_buffer)))
	{
		g_LastErrorDump.format("INI::readLine: line %d of '%s' is too long once its mod labels are prefixed\n", m_lineNum, m_filename.str());
		DEBUG_LOG(("%s", g_LastErrorDump.str()));
		throw INI_BUFFER_TOO_SMALL;
	}
}

//-------------------------------------------------------------------------------------------------
/** Parse UnsignedByte from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseUnsignedByte( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Int value = scanInt(token);
	if (value < 0 || value > 255)
	{
		DEBUG_CRASH(("Bad value INI::parseUnsignedByte"));
		throw ERROR_BUG;
	}
	*(Byte *)store = (Byte)value;
} 

//-------------------------------------------------------------------------------------------------
/** Parse signed short from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseShort( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Int value = scanInt(token);
	if (value < -32768 || value > 32767)
	{
		DEBUG_CRASH(("Bad value INI::parseShort"));
		throw ERROR_BUG;
	}
	*(Short *)store = (Short)value;
} 

//-------------------------------------------------------------------------------------------------
/** Parse unsigned short from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseUnsignedShort( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	/* sscanf's %d on a number past Int is undefined, and the CRTs disagree: a map's BuildCost of
		 99999999999 came out in range on one machine and was refused on another.  strtoll is defined
		 there (it saturates), and reads every in-range token the way %d did. */
	char *end;
	long long value = strtoll(token, &end, 10);
	if (end == token)
		throw INI_INVALID_DATA;
	if ((value < 0 || value > 65535) && ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		DEBUG_LOG(("[LINE: %d in '%s'] %s does not fit 0..65535, the line is ignored\n", ini->getLineNum(), ini->getFilename().str(), token));
		return;
	}
	if (value < 0 || value > 65535)
	{
		DEBUG_CRASH(("Bad value INI::parseUnsignedShort"));
		throw ERROR_BUG;
	}
	*(UnsignedShort *)store = (UnsignedShort)value;
} 

//-------------------------------------------------------------------------------------------------
/** Parse integer from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseInt( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	*(Int *)store = scanInt(token);

} 

//-------------------------------------------------------------------------------------------------
/** Parse unsigned integer from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseUnsignedInt( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	*(UnsignedInt *)store = scanUnsignedInt(token);

}

//-------------------------------------------------------------------------------------------------
/** Parse real from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseReal( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	*(Real *)store = scanReal(token);

} 

//-------------------------------------------------------------------------------------------------
/** Parse real from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parsePositiveNonZeroReal( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	*(Real *)store = scanReal(token);
	if (*(Real *)store <= 0.0f)
	{
		DEBUG_CRASH(("invalid Real value %f -- expected > 0\n",*(Real*)store));
		throw INI_INVALID_DATA;
	}

}

//-------------------------------------------------------------------------------------------------
/** A real that a map's rules file can set to something the game cannot live with: a build time
	* that overflows the frame count, a snow box that divides by zero.  Refused before it is stored,
	* and NaN with it, since a NaN fails both comparisons.  In a map's rules file only the line goes:
	* the value stays what it was and the rest of the file loads.  The game's own data stops. */
//-------------------------------------------------------------------------------------------------
static void refuseOutOfRange( INI *ini, const char *what )
{
	DEBUG_LOG(("[LINE: %d in '%s'] %s is out of range, refused\n", ini->getLineNum(), ini->getFilename().str(), what));
	if (ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES)
		throw INI_INVALID_DATA;
}

void INI::parseRealInRange( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	const RealRange *range = (const RealRange *)userData;
	const char *token = ini->getNextToken();
	Real value = scanReal(token);
	if (!(value >= range->lo && value <= range->hi))
	{
		refuseOutOfRange(ini, token);
		return;
	}
	*(Real *)store = value;
}

//-------------------------------------------------------------------------------------------------
/** The integer twin.  strtoll rather than sscanf's %d, whose answer for a number past Int is
	* undefined and differs between the CRTs (see parseUnsignedShort). */
//-------------------------------------------------------------------------------------------------
void INI::parseIntInRange( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	const IntRange *range = (const IntRange *)userData;
	const char *token = ini->getNextToken();
	char *end;
	long long value = strtoll(token, &end, 10);
	if (end == token)
		throw INI_INVALID_DATA;
	if (value < range->lo || value > range->hi)
	{
		refuseOutOfRange(ini, token);
		return;
	}
	*(Int *)store = (Int)value;
}

//-------------------------------------------------------------------------------------------------
/** Parse a degree value (0 to 360) and store the radian value of that degree
	* in a Real */
//-------------------------------------------------------------------------------------------------
void INI::parseAngleReal( INI *ini, void * /*instance*/, 
																			void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	const Real RADS_PER_DEGREE = PI / 180.0f;
	*(Real *)store = scanReal( token ) * RADS_PER_DEGREE;

}

//-------------------------------------------------------------------------------------------------
/** Parse an angular velocity in degrees-per-sec and store the rads-per-frame value of that degree
	* in a Real */
//-------------------------------------------------------------------------------------------------
void INI::parseAngularVelocityReal( INI *ini, void * /*instance*/, 
																			void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	// scan the int and convert to radian and store as a real
	*(Real *)store = ConvertAngularVelocityInDegreesPerSecToRadsPerFrame(scanReal( token ));

}

//-------------------------------------------------------------------------------------------------
/** Parse Bool from buffer and assign at location 'store'.  The buffer token must
	* be in the form of a string "Yes" or "No" (case is ignored) */
//-------------------------------------------------------------------------------------------------
void INI::parseBool( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	*(Bool*)store = INI::scanBool(ini->getNextToken());
}

//-------------------------------------------------------------------------------------------------
/** Parse Bool from buffer; if true, or in MASK, otherwise and out MASK. The buffer token must
	* be in the form of a string "Yes" or "No" (case is ignored) */
//-------------------------------------------------------------------------------------------------
void INI::parseBitInInt32( INI *ini, void *instance, void *store, const void* userData )
{
	UnsignedInt* s = (UnsignedInt*)store;
	UnsignedInt mask = (UnsignedInt)(uintptr_t)userData;

	if (INI::scanBool(ini->getNextToken()))
		*s |= mask;
	else
		*s &= ~mask;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/*static*/ Bool INI::scanBool(const char* token)
{
	// translate string yes/no into TRUE/FALSE
	token = withoutModPrefix( token );
	if( strcasecmp( token, "yes" ) == 0 )
		return TRUE;
	else if( strcasecmp( token, "no" ) == 0 )
		return FALSE;
	else
	{
		DEBUG_CRASH(("invalid boolean token %s -- expected Yes or No\n",token));
		throw INI_INVALID_DATA;
		return false;	// keep compiler happy
	}

}

//-------------------------------------------------------------------------------------------------
/** Parse an *ASCII* string from buffer and assign at location 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseAsciiString( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	AsciiString* asciiString = (AsciiString *)store;
	*asciiString = ini->getNextAsciiString();
}

//-------------------------------------------------------------------------------------------------
/** Parse an *ASCII* string from buffer and assign at location 'store'. Has better support for quoted strings.
We don't really need this function, but parseString() is broken and we want to leave it broken to
maintain existing code.
 */
//-------------------------------------------------------------------------------------------------
void INI::parseQuotedAsciiString( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	AsciiString* asciiString = (AsciiString *)store;
	*asciiString = ini->getNextQuotedAsciiString();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseAsciiStringVector( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	std::vector<AsciiString>* asv = (std::vector<AsciiString>*)store;
	asv->clear();
	for (const char *token = ini->getNextTokenOrNull(); token != NULL; token = ini->getNextTokenOrNull())
	{
		asv->push_back(token);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseAsciiStringVectorAppend( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	std::vector<AsciiString>* asv = (std::vector<AsciiString>*)store;
	// nope, don't clear. duh.
	// asv->clear();
	for (const char *token = ini->getNextTokenOrNull(); token != NULL; token = ini->getNextTokenOrNull())
	{
		asv->push_back(token);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/* static */void INI::parseScienceVector( INI *ini, void * /*instance*/, void *store, const void *userData )
{
	ScienceVec* asv = (ScienceVec*)store;
	const ScienceVec before = *asv;
	Bool missing = FALSE;
	asv->clear();
	for (const char *token = ini->getNextTokenOrNull(); token != NULL; token = ini->getNextTokenOrNull())
	{
		if (strcasecmp(token, "None") == 0)
		{
			asv->clear();
			return;
		}
		ScienceType science = INI::scanScience( ini, token );
		if (science != SCIENCE_INVALID)
			asv->push_back(science);
		else
			missing = TRUE;
	}
	// a patch whose every science the mod lacks keeps the mod's own: Contra has no Chem_SCIENCE_RebelAmbush1,
	// and an emptied list left its Toxin General's ambush buttons with nothing to buy or show
	if (missing && asv->empty())
		*asv = before;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AsciiString INI::getNextQuotedAsciiString()
{
	AsciiString result;
	char buff[INI_MAX_CHARS_PER_PREFIXED_LINE];	// a token of m_buffer, prefixes and all

	const char *token = getNextTokenOrNull();	// if null, just leave an empty string
	if (token != NULL)
	{
		if (token[0] != '\"') 
		{	
			// if token is simply "
			result.set( token );	// Start following the "
		}
		else
		{	int strLen=0;
			Bool done=FALSE;
			if ((strLen=strlen(token)) > 1)
			{
				strcpy(buff, &token[1]);	//skip the starting quote
				//Check for end of quoted string.  Checking here fixes cases where quoted string on same line with other data.
				if (buff[strLen-2]=='"')	//skip ending quote if present
				{	buff[strLen-2]='\0';
					done=TRUE;
				}
			}

			if (!done)
			{
				token = getNextToken(getSepsQuote());
				
				if (strlen(token) > 1 && token[1] != '\t')
				{
					strlcat(buff, " ", ARRAY_SIZE(buff));
					strlcat(buff, token, ARRAY_SIZE(buff));
				}
				else
				{	Int buflen=strlen(buff);
					if (buff[buflen-1]=='\"')
						buff[buflen-1]='\0';
				}
			}
			result.set(buff);
		}
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AsciiString INI::getNextAsciiString()
{
	AsciiString result;

	const char *token = getNextTokenOrNull();	// if null, just leave an empty string
	if (token != NULL)
	{
		if (token[0] != '\"') 
		{	
			// if token is simply "
			result.set( token );	// Start following the "
		}
		else
		{
			static char buff[INI_MAX_CHARS_PER_PREFIXED_LINE];
			buff[0] = 0;
			if (strlen(token) > 1)
			{
				strlcpy(buff, &token[1], ARRAY_SIZE(buff));
			}

			token = getNextTokenOrNull(getSepsQuote());
			if (token) {
				if (strlen(token) > 1 && token[1] != '\t')
				{
					strlcat(buff, " ", ARRAY_SIZE(buff));
				}
				strlcat(buff, token, ARRAY_SIZE(buff));
				result.set(buff);
			} else {
				Int len = strlen(buff);
				if (len && buff[len-1] == '"') { // strip off trailing quote jba. [2/12/2003]
					buff[len-1] = 0;
				}
				result.set(buff);
			}
		}
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
/** Parse a string label, get the *translated* actual text from the label and store
	* into a *UNICODE* string. */
//-------------------------------------------------------------------------------------------------
void INI::parseAndTranslateLabel( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	// translate
	UnicodeString translated = TheGameText->fetch( token );
	if( translated.isEmpty() )
		throw INI_INVALID_DATA;

	// save the translated text
	UnicodeString *theString = (UnicodeString *)store;
	theString->set( translated.str() );

}  // end parseAndTranslateLabel

//-------------------------------------------------------------------------------------------------
/** Parse a string label assumed as an image as part of the image collection.  Translate
	* to an image pointer for storage */
//-------------------------------------------------------------------------------------------------
void INI::parseMappedImage( INI *ini, void * /*instance*/, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	if( TheMappedImageCollection )
	{
		typedef const Image* ConstImagePtr;
		*(ConstImagePtr*)store = TheMappedImageCollection->findImageByName( AsciiString( token ) );
	}
	
	//KM: If we are in the worldbuilder, we want to parse commandbuttons for informational purposes,
	//but we don't care about the images -- because we never access them. In RTS/GUIEdit, they always
	//exist -- and in those cases, it will never call this code anyways because it'll throw long before.
	//else
	//	throw INI_UNKNOWN_ERROR;

}  // end parseMappedImage

// ------------------------------------------------------------------------------------------------
/** Parse a string label assumed as a Anim2D template name.  Translate that name to an 
	* actual template pointer for storage */
// ------------------------------------------------------------------------------------------------
/*static*/ void INI::parseAnim2DTemplate( INI *ini, void *instance, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	if( TheAnim2DCollection )
	{
		Anim2DTemplate **anim2DTemplate = (Anim2DTemplate **)store;
		*anim2DTemplate = TheAnim2DCollection->findTemplate( AsciiString( token ) );
	}  // end if
	else
	{

		DEBUG_CRASH(( "INI::parseAnim2DTemplate - TheAnim2DCollection is NULL\n" ));
		throw INI_UNKNOWN_ERROR;

	}  // end else

}  // end parseAnim2DTemplate

//-------------------------------------------------------------------------------------------------
/** Parse a percent in int or real form such as "23%" or "95.4%" and assign
	* to location 'store' as a number from 0.0 to 1.0 */
//-------------------------------------------------------------------------------------------------
void INI::parsePercentToReal( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken(ini->getSepsPercent());
	Real *theReal = (Real *)store;
	*theReal = scanPercentToReal(token);

}  // end parsePercentToReal

//-------------------------------------------------------------------------------------------------
/** 'store' points to an 32 bit unsigned integer.  We will zero that integer, parse each token
	* in the buffer, if the token is in the userData table of strings, we will set the
	* according bit flag for it */
//-------------------------------------------------------------------------------------------------
void INI::parseBitString8( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	UnsignedInt tmp = *(Byte*)store;
	INI::parseBitString32(ini, NULL, &tmp, userData);
	if (tmp & 0xffffff00)
	{
		DEBUG_CRASH(("Bad bitstring list INI::parseBitString8"));
		throw ERROR_BUG;
	}
	*(Byte*)store = (Byte)tmp;
}

//-------------------------------------------------------------------------------------------------
/** 'store' points to an 32 bit unsigned integer.  We will zero that integer, parse each token
	* in the buffer, if the token is in the userData table of strings, we will set the
	* according bit flag for it */
//-------------------------------------------------------------------------------------------------
void INI::parseBitString32( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	ConstCharPtrArray flagList = (ConstCharPtrArray)userData;
	UnsignedInt *bits = (UnsignedInt *)store;

	if( flagList == NULL || flagList[ 0 ] == NULL)
	{
		DEBUG_ASSERTCRASH( flagList, ("INTERNAL ERROR! parseBitString32: No flag list provided!\n") );
		throw INI_INVALID_NAME_LIST;
	}

	Bool foundNormal = false;
	Bool foundAddOrSub = false;

	// loop through all tokens
	for (const char *token = ini->getNextTokenOrNull(); token != NULL; token = ini->getNextTokenOrNull())
	{
		if (strcasecmp(token, "NONE") == 0)
		{
			if (foundNormal || foundAddOrSub)
			{
				DEBUG_CRASH(("you may not mix normal and +- ops in bitstring lists"));
				throw INI_INVALID_NAME_LIST;
			}
			*bits = 0;
			break;
		}

		if (token[0] == '+')
		{
			if (foundNormal)
			{
				DEBUG_CRASH(("you may not mix normal and +- ops in bitstring lists"));
				throw INI_INVALID_NAME_LIST;
			}
			Int bitIndex = INI::scanIndexList(token+1, flagList);	// this throws if the token is not found
			*bits |= (1 << bitIndex);
			foundAddOrSub = true;
		}
		else if (token[0] == '-')
		{
			if (foundNormal)
			{
				DEBUG_CRASH(("you may not mix normal and +- ops in bitstring lists"));
				throw INI_INVALID_NAME_LIST;
			}
			Int bitIndex = INI::scanIndexList(token+1, flagList);	// this throws if the token is not found
			*bits &= ~(1 << bitIndex);
			foundAddOrSub = true;
		}
		else
		{
			if (foundAddOrSub)
			{
				DEBUG_CRASH(("you may not mix normal and +- ops in bitstring lists"));
				throw INI_INVALID_NAME_LIST;
			}

			if (!foundNormal)
				*bits = 0;

			Int bitIndex = INI::scanIndexList(token, flagList);	// this throws if the token is not found
			*bits |= (1 << bitIndex);
			foundNormal = true;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Parse a color in the form of
	*
	* RGB_COLOR = R:100 G:114 B:245
	* and store in "RGBColor" structure pointed to by 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseRGBColor( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char* names[3] = { "R", "G", "B" };
	Int colors[3];
	for( Int i = 0; i < 3; i++ )
	{
		colors[i] = scanInt(ini->getNextSubToken(names[i]));
		if( colors[ i ] < 0 )
			throw INI_INVALID_DATA;
		if( colors[ i ] > 255 )
			throw INI_INVALID_DATA;
	}

	// assign the color components to the "RGBColor" pointer at 'store'
	RGBColor *theColor = (RGBColor *)store;
	theColor->red		= (Real)colors[ 0 ] / 255.0f;
	theColor->green = (Real)colors[ 1 ] / 255.0f;
	theColor->blue	= (Real)colors[ 2 ] / 255.0f;

}

//-------------------------------------------------------------------------------------------------
/** Parse a color in the form of
	*
	* RGB_COLOR = R:100 G:114 B:245 [A:233]
	* and store in "RGBAColorInt" structure pointed to by 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseRGBAColorInt( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char* names[4] = { "R", "G", "B", "A" };
	Int colors[4];
	for( Int i = 0; i < 4; i++ )
	{
		const char* token = ini->getNextTokenOrNull(ini->getSepsColon());
		if (token == NULL)
		{
			if (i < 3)
			{
				throw INI_INVALID_DATA;
			}
			else
			{
				// it's ok for A to be omitted.
				colors[i] = 255;
			}
		}
		else
		{
			// if present, the token must match.
			if (strcasecmp(token, names[i]) != 0)
			{
				throw INI_INVALID_DATA;				
			}
			colors[i] = scanInt(ini->getNextToken(ini->getSepsColon()));
		}
		if( colors[ i ] < 0 )
			throw INI_INVALID_DATA;
		if( colors[ i ] > 255 )
			throw INI_INVALID_DATA;
	}

	//
	// assign the color components to the "RGBColorInt" pointer at 'store', keep
	// the numbers as between 0 and 255
	//
	RGBAColorInt *theColor = (RGBAColorInt *)store;
	theColor->red		= colors[ 0 ];
	theColor->green = colors[ 1 ];
	theColor->blue	= colors[ 2 ];
	theColor->alpha = colors[ 3 ];

}  // end parseRGBAColorInt

//-------------------------------------------------------------------------------------------------
/** Parse a color in the form of
	*
	* RGB_COLOR = R:100 G:114 B:245 [A:233]
	* and store in "Color" structure pointed to by 'store' */
//-------------------------------------------------------------------------------------------------
void INI::parseColorInt( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char* names[4] = { "R", "G", "B", "A" };
	Int colors[4];
	for( Int i = 0; i < 4; i++ )
	{
		const char* token = ini->getNextTokenOrNull(ini->getSepsColon());
		if (token == NULL)
		{
			if (i < 3)
			{
				throw INI_INVALID_DATA;
			}
			else
			{
				// it's ok for A to be omitted.
				colors[i] = 255;
			}
		}
		else
		{
			// if present, the token must match.
			if (strcasecmp(token, names[i]) != 0)
			{
				throw INI_INVALID_DATA;				
			}
			colors[i] = scanInt(ini->getNextToken(ini->getSepsColon()));
		}
		if( colors[ i ] < 0 )
			throw INI_INVALID_DATA;
		if( colors[ i ] > 255 )
			throw INI_INVALID_DATA;
	}

	//
	// assign the color components to the "Color" pointer at 'store', keep
	// the numbers as between 0 and 255
	//
	Color *theColor = (Color *)store;
	*theColor = GameMakeColor(colors[0], colors[1], colors[2], colors[3]);

}  // end parseColorInt

//-------------------------------------------------------------------------------------------------
/** Parse a 3D coordinate of reals in the form of:
	* FIELD_NAME = X:400 Y:-214.3 Z:8.6 */
//-------------------------------------------------------------------------------------------------
void INI::parseCoord3D( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	Coord3D *theCoord = (Coord3D *)store;

	theCoord->x = scanReal(ini->getNextSubToken("X"));
	theCoord->y = scanReal(ini->getNextSubToken("Y"));
	theCoord->z = scanReal(ini->getNextSubToken("Z"));

}  // end parseCoord3D

//-------------------------------------------------------------------------------------------------
/** Parse a 2D coordinate of reals in the form of:
	* FIELD_NAME = X:400 Y:-214.3 */
//-------------------------------------------------------------------------------------------------
void INI::parseCoord2D( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	Coord2D *theCoord = (Coord2D *)store;

	theCoord->x = scanReal(ini->getNextSubToken("X"));
	theCoord->y = scanReal(ini->getNextSubToken("Y"));

}  // end parseCoord2D

//-------------------------------------------------------------------------------------------------
/** Parse a 2D coordinate of Ints in the form of:
	* FIELD_NAME = X:400 Y:-214 */
//-------------------------------------------------------------------------------------------------
void INI::parseICoord2D( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	ICoord2D *theCoord = (ICoord2D *)store;

	theCoord->x = scanInt(ini->getNextSubToken("X"));
	theCoord->y = scanInt(ini->getNextSubToken("Y"));

}  // end parseICoord2D

//-------------------------------------------------------------------------------------------------
/** Parse an audio event and assign to the 'AudioEventRTS*' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseDynamicAudioEventRTS( INI *ini, void * /*instance*/, void *store, const void* userData )
{
	const char *token = ini->getNextToken();
	DynamicAudioEventRTS** theSound = (DynamicAudioEventRTS**)store;
	
	// translate the string into a sound
	if (strcasecmp(token, "NoSound") == 0) 
	{
		if (*theSound)
		{
			(*theSound)->deleteInstance();
			*theSound = NULL;
		}
	}
	else
	{
		if (*theSound == NULL)
			*theSound = newInstance(DynamicAudioEventRTS);
		(*theSound)->m_event.setEventName(AsciiString(token));
	}
	
	if (*theSound)
		TheAudio->getInfoForAudioEvent(&(*theSound)->m_event);
}

//-------------------------------------------------------------------------------------------------
/** Parse an audio event and assign to the 'AudioEventRTS*' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseAudioEventRTS( INI *ini, void * /*instance*/, void *store, const void* userData )
{
	const char *token = ini->getNextToken();

	AudioEventRTS *theSound = (AudioEventRTS*)store;
	
	// translate the string into a sound
	if (strcasecmp(token, "NoSound") != 0) {
		theSound->setEventName(AsciiString(token));
	}

	TheAudio->getInfoForAudioEvent(theSound);
}

//-------------------------------------------------------------------------------------------------
/** Parse an ThingTemplate and assign to the 'ThingTemplate *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseThingTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	if (!TheThingFactory)
	{
		DEBUG_CRASH(("TheThingFactory not inited yet"));
		throw ERROR_BUG;
	}

	typedef const ThingTemplate *ConstThingTemplatePtr;
	ConstThingTemplatePtr* theThingTemplate = (ConstThingTemplatePtr*)store;		

	if (strcasecmp(token, "None") == 0)
	{
		*theThingTemplate = NULL;
	}
	else
	{
		const ThingTemplate *tt = TheThingFactory->findTemplate(token);	// could be null!
		DEBUG_ASSERTCRASH(tt, ("ThingTemplate %s not found!\n",token));
		// assign it, even if null!
		*theThingTemplate = tt;
	}

} 

//-------------------------------------------------------------------------------------------------
/** Parse an ArmorTemplate and assign to the 'ArmorTemplate *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseArmorTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	typedef const ArmorTemplate *ConstArmorTemplatePtr;
	ConstArmorTemplatePtr* theArmorTemplate = (ConstArmorTemplatePtr*)store;		

	if (strcasecmp(token, "None") == 0)
	{
		*theArmorTemplate = NULL;
	}
	else
	{
		const ArmorTemplate *tt = TheArmorStore->findArmorTemplate(token);	// could be null!
		DEBUG_ASSERTCRASH(tt, ("ArmorTemplate %s not found!\n",token));
		// assign it, even if null!
		*theArmorTemplate = tt;
	}

} 

//-------------------------------------------------------------------------------------------------
/** Parse an WeaponTemplate and assign to the 'WeaponTemplate *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseWeaponTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	typedef const WeaponTemplate *ConstWeaponTemplatePtr;
	ConstWeaponTemplatePtr* theWeaponTemplate = (ConstWeaponTemplatePtr*)store;		

	const WeaponTemplate *tt = TheWeaponStore->findWeaponTemplate(token);	// could be null!
	DEBUG_ASSERTCRASH(tt || strcasecmp(token, "None") == 0, ("WeaponTemplate %s not found!\n",token));
	// assign it, even if null!
	*theWeaponTemplate = tt;

} 

//-------------------------------------------------------------------------------------------------
/** Parse an FXList and assign to the 'FXList *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseFXList( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	typedef const FXList *ConstFXListPtr;
	ConstFXListPtr* theFXList = (ConstFXListPtr*)store;		

	const FXList *fxl = TheFXListStore->findFXList(token);	// could be null!
	DEBUG_ASSERTCRASH(fxl != NULL || strcasecmp(token, "None") == 0, ("FXList %s not found!\n",token));
	// assign it, even if null!
	*theFXList = fxl;

} 

//-------------------------------------------------------------------------------------------------
/** Parse a particle system and assign to 'ParticleSystemTemplate *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseParticleSystemTemplate( INI *ini, void * /*instance*/, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	const ParticleSystemTemplate *pSystemT = TheParticleSystemManager->findTemplate( AsciiString( token ) );
	DEBUG_ASSERTCRASH( pSystemT || strcasecmp( token, "None" ) == 0, ("ParticleSystem %s not found!\n",token) );

	typedef const ParticleSystemTemplate* ConstParticleSystemTemplatePtr;
	ConstParticleSystemTemplatePtr* theParticleSystemTemplate = (ConstParticleSystemTemplatePtr*)store;		

	*theParticleSystemTemplate = pSystemT;

}  // end parseParticleSystemTemplate

//-------------------------------------------------------------------------------------------------
/** Parse an DamageFX and assign to the 'DamageFX *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseDamageFX( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	typedef const DamageFX *ConstDamageFXPtr;
	ConstDamageFXPtr* theDamageFX = (ConstDamageFXPtr*)store;		

	if (strcasecmp(token, "None") == 0)
	{
		*theDamageFX = NULL;
	}
	else
	{
		const DamageFX *fxl = TheDamageFXStore->findDamageFX(token);	// could be null!
		DEBUG_ASSERTCRASH(fxl, ("DamageFX %s not found!\n",token));
		// assign it, even if null!
		*theDamageFX = fxl;
	}

} 

//-------------------------------------------------------------------------------------------------
/** Parse an ObjectCreationList and assign to the 'ObjectCreationList *' at store */
//-------------------------------------------------------------------------------------------------
void INI::parseObjectCreationList( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	typedef const ObjectCreationList *ConstObjectCreationListPtr;
	ConstObjectCreationListPtr* theObjectCreationList = (ConstObjectCreationListPtr*)store;		

	const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList(token);	// could be null!
	if (!ocl && strcasecmp(token, "None") != 0 && ini->getLoadType() == INI_LOAD_MULTIFILE)
	{
		// a patch INI loaded over a mod, as with a missing special power: FixesReforged.ini's Emperor
		// Gattling upgrade names Tank_OCL_OverlordGattlingCannon, which Contra does not have
		DEBUG_LOG(("[LINE: %d in '%s'] object creation list '%s' does not exist; the reference is skipped.\n", ini->getLineNum(), ini->getFilename().str(), token));
		ini->setMissingReference( TRUE );
		return;
	}
	DEBUG_ASSERTCRASH(ocl || strcasecmp(token, "None") == 0, ("ObjectCreationList %s not found!\n",token));
	// assign it, even if null!
	*theObjectCreationList = ocl;

} 

//-------------------------------------------------------------------------------------------------
/** Parse a upgrade template string and store as template pointer */
//-------------------------------------------------------------------------------------------------
void INI::parseUpgradeTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	if (!TheUpgradeCenter)
	{
		DEBUG_CRASH(("TheUpgradeCenter not inited yet"));
		throw ERROR_BUG;
	}

	const UpgradeTemplate *uu = TheUpgradeCenter->findUpgrade( AsciiString( token ) );
	DEBUG_ASSERTCRASH( uu || strcasecmp( token, "None" ) == 0, ("Upgrade %s not found!\n",token) );

	typedef const UpgradeTemplate* ConstUpgradeTemplatePtr;
	ConstUpgradeTemplatePtr* theUpgradeTemplate = (ConstUpgradeTemplatePtr *)store;		
	*theUpgradeTemplate = uu;
} 

//-------------------------------------------------------------------------------------------------
/** Parse a special power template string and store as template pointer */
//-------------------------------------------------------------------------------------------------
void INI::parseSpecialPowerTemplate( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();

	if (!TheSpecialPowerStore)
	{
		DEBUG_CRASH(("TheSpecialPowerStore not inited yet"));
		throw ERROR_BUG;
	}

	const SpecialPowerTemplate *sPowerT = TheSpecialPowerStore->findSpecialPowerTemplate( AsciiString( token ) );
	if( !sPowerT && strcasecmp( token, "None" ) != 0 && ini->getLoadType() == INI_LOAD_MULTIFILE )
	{
		// a patch INI loaded over a mod, as with a missing science (scanScience): Contra has no Nuke_ carpet
		// bomb.  A module naming it is dropped; a button keeps the mod's own power, or Contra's Toxin
		// General lost his ambush shortcut to Chem_SuperweaponRebelAmbush.
		DEBUG_LOG(("[LINE: %d in '%s'] special power '%s' does not exist; the reference is skipped.\n", ini->getLineNum(), ini->getFilename().str(), token));
		ini->setMissingReference( TRUE );
		return;
	}
	else if( !sPowerT && strcasecmp( token, "None" ) != 0 )
	{
		DEBUG_CRASH( ("[LINE: %d in '%s'] Specialpower %s not found!\n", ini->getLineNum(), ini->getFilename().str(), token) );
	}

	typedef const SpecialPowerTemplate* ConstSpecialPowerTemplatePtr;
	ConstSpecialPowerTemplatePtr* theSpecialPowerTemplate = (ConstSpecialPowerTemplatePtr *)store;		
	*theSpecialPowerTemplate = sPowerT;
} 

//-------------------------------------------------------------------------------------------------
/** Parse a science string and store as science type */
//-------------------------------------------------------------------------------------------------
/* static */void INI::parseScience( INI *ini, void * /*instance*/, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	if (!TheScienceStore)
	{
		DEBUG_CRASH(("TheScienceStore not inited yet"));
		throw ERROR_BUG;
	}

	// a skipped reference leaves the field as the loaded data had it
	ScienceType science = INI::scanScience(ini, token);
	if (science != SCIENCE_INVALID)
		*((ScienceType *)store) = science;

}

//-------------------------------------------------------------------------------------------------
/** Parse a single string token, check for that token in the index list
	* of names provided and store the index into that list.
	*
	* NOTE: Is is assumed that we are going to store the index into
	*				a 4 byte integer.  This works well for INT and ENUM definitions */
//-------------------------------------------------------------------------------------------------
void INI::parseIndexList( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	ConstCharPtrArray nameList = (ConstCharPtrArray)userData;
	*(Int *)store = scanIndexList(ini->getNextToken(), nameList);
} 

//-------------------------------------------------------------------------------------------------
/** Parse a single string token, check for that token in the index list
	* of names provided and store the index into that list.
	*
	* NOTE: Is is assumed that we are going to store the index into
	*				a 4 byte integer.  This works well for INT and ENUM definitions */
//-------------------------------------------------------------------------------------------------
void INI::parseByteSizedIndexList( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	ConstCharPtrArray nameList = (ConstCharPtrArray)userData;
	Int value = scanIndexList(ini->getNextToken(), nameList);
	if (value < 0 || value > 255)
	{
		DEBUG_CRASH(("Bad index list INI::parseByteSizedIndexList"));
		throw ERROR_BUG;
	}
	*(Byte *)store = (Byte)value;
} 

//-------------------------------------------------------------------------------------------------
/** Parse a single string token, check for that token in the index list
	* of names provided and store the associated value into that list.
	*
	* NOTE: Is is assumed that we are going to store the index into
	*				a 4 byte integer.  This works well for INT and ENUM definitions */
//-------------------------------------------------------------------------------------------------
void INI::parseLookupList( INI* ini, void * /*instance*/, void *store, const void* userData )
{
	ConstLookupListRecArray lookupList = (ConstLookupListRecArray)userData;
	*(Int *)store = scanLookupList(ini->getNextToken(), lookupList);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

	
//-------------------------------------------------------------------------------------------------
void MultiIniFieldParse::add(const FieldParse* f, UnsignedInt e)
{
	if (m_count < MAX_MULTI_FIELDS)
	{
		m_fieldParse[m_count] = f;
		m_extraOffset[m_count] = e;
		++m_count;
	}
	else
	{
		DEBUG_CRASH(("too many multi-fields in INI::initFromINIMultiProc"));
		throw ERROR_BUG;
	}
}

//-------------------------------------------------------------------------------------------------
void INI::initFromINI( void *what, const FieldParse* parseTable )
{
	MultiIniFieldParse p;
	p.add(parseTable);
	initFromINIMulti(what, p);
}

//-------------------------------------------------------------------------------------------------
void INI::initFromINIMultiProc( void *what, BuildMultiIniFieldProc proc )
{
	MultiIniFieldParse p;
	(*proc)(p);
	initFromINIMulti(what, p);
}

//-------------------------------------------------------------------------------------------------
void INI::initFromINIMulti( void *what, const MultiIniFieldParse& parseTableList )
{
	Bool done = FALSE;

	if( what == NULL )
	{
		DEBUG_ASSERTCRASH( 0, ("INI::initFromINI - Invalid parameters supplied!\n") );
		throw INI_INVALID_PARAMS;
	}

	// read each of the data fields
	while( !done )
	{

		// read next line
		readLine();

		// check for end token
		const char* field = strtok( m_buffer, INI::getSeps() );
		if( field )
		{

			if( strcasecmp( field, m_blockEndToken ) == 0 )
			{
				done = TRUE;
			}
			else
			{
				Bool found = false;
				for (int ptIdx = 0; ptIdx < parseTableList.getCount(); ++ptIdx)
				{
					int offset = 0;
					// findFieldParse takes this by non-const reference and the
					// parse procs take it as const void*, so const it is.
					const void* userData = 0;
					INIFieldParseProc parse = findFieldParse(parseTableList.getNthFieldParse(ptIdx), field, offset, userData);
					if (parse)
					{
						// parse this block and check for parse errors
						try {

						(*parse)( this, what, (char *)what + offset + parseTableList.getNthExtraOffset(ptIdx), userData );

						} catch (...) {
							DEBUG_CRASH( ("[LINE: %d - FILE: '%s'] Error reading field '%s' of block '%s'\n",
																 INI::getLineNum(), INI::getFilename().str(), field, m_curBlockStart) );


							char buff[1024];
							snprintf(buff, ARRAY_SIZE(buff), "[LINE: %d - FILE: '%s'] Error reading field '%s'\n", INI::getLineNum(), INI::getFilename().str(), field);
							throw INIException(buff);
						}
						
						found = true;
						break;
						
					}
				}

				if (!found && m_skipUnknownFields)
				{
					DEBUG_LOG(("[LINE: %d - FILE: '%s'] skipping unknown field '%s'\n",
										 INI::getLineNum(), INI::getFilename().str(), field));
				}
				else if (!found)
				{
					DEBUG_ASSERTCRASH( 0, ("[LINE: %d - FILE: '%s'] Unknown field '%s' in block '%s'\n",
														 INI::getLineNum(), INI::getFilename().str(), field, m_curBlockStart) );
					throw INI_UNKNOWN_TOKEN;
				}

			}  // end else

		}  // end if

		// sanity check for reaching end of file with no closing end token
		if( done == FALSE && INI::isEOF() == TRUE )
		{

			done = TRUE;
			DEBUG_ASSERTCRASH( 0, ("Error parsing block '%s', in INI file '%s'.  Missing '%s' token\n",
												 m_curBlockStart, getFilename().str(), m_blockEndToken) );
			throw INI_MISSING_END_TOKEN;

		}  // end if

	}  // end while

}

//-------------------------------------------------------------------------------------------------
/*static*/ const char* INI::getNextToken(const char* seps)
{
	if (!seps) seps = getSeps();
	const char *token = ::strtok(NULL, seps);
	if (!token) 
		throw INI_INVALID_DATA;
	return token;
}

//-------------------------------------------------------------------------------------------------
/*static*/ const char* INI::getNextTokenOrNull(const char* seps)
{
	if (!seps) seps = getSeps();
	const char *token = ::strtok(NULL, seps);
	return token;
}

//-------------------------------------------------------------------------------------------------
/*static*/ ScienceType INI::scanScience(INI* ini, const char* token)
{
	// a patch INI loaded over a mod can name a science the mod renamed or removed
	if (ini->getLoadType() == INI_LOAD_MULTIFILE && !TheScienceStore->isValidScience((ScienceType)NAMEKEY(token)))
	{
		DEBUG_LOG(("[LINE: %d in '%s'] science '%s' does not exist; the reference is skipped.\n", ini->getLineNum(), ini->getFilename().str(), token));
		return SCIENCE_INVALID;
	}
	return TheScienceStore->friend_lookupScience( token );
}

//-------------------------------------------------------------------------------------------------
/*static*/ Int INI::scanInt(const char* token)
{
	Int value;
	if (sscanf( token, "%d", &value ) != 1)
		throw INI_INVALID_DATA;
	return value;
}

//-------------------------------------------------------------------------------------------------
/*static*/ UnsignedInt INI::scanUnsignedInt(const char* token)
{
	UnsignedInt value;
	if (sscanf( token, "%u", &value ) != 1)	// unsigned int is %u, not %d
		throw INI_INVALID_DATA;
	return value;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Real INI::scanReal(const char* token)
{
	Real value;
	if (sscanf( token, "%f", &value ) != 1)
		throw INI_INVALID_DATA;
	return value;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Real INI::scanPercentToReal(const char* token)
{
	Real value;
	if (sscanf( token, "%f", &value ) != 1)
		throw INI_INVALID_DATA;
	return value / 100.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Int INI::scanIndexList(const char* token, ConstCharPtrArray nameList)
{
	if( nameList == NULL || nameList[ 0 ] == NULL )
	{

		DEBUG_ASSERTCRASH( 0, ("INTERNAL ERROR! scanIndexList, invalid name list\n") );
		throw INI_INVALID_NAME_LIST;

	}

	// search for matching name
	token = withoutModPrefix( token );
	Int count = 0;
	for(ConstCharPtrArray name = nameList; *name; name++, count++ )
	{
		if( strcasecmp( *name, token ) == 0 )
		{
			return count;
		}
	}

	DEBUG_CRASH(("token %s is not a valid member of the index list\n",token));
	throw INI_INVALID_DATA;
	return 0;	// never executed, but keeps compiler happy

}
//-------------------------------------------------------------------------------------------------
/*static*/ Int INI::scanLookupList(const char* token, ConstLookupListRecArray lookupList)
{
	if( lookupList == NULL || lookupList[ 0 ].name == NULL )
	{
		DEBUG_ASSERTCRASH( 0, ("INTERNAL ERROR! scanLookupList, invalid name list\n") );
		throw INI_INVALID_NAME_LIST;
	}

	// search for matching name
	token = withoutModPrefix( token );
	Bool found = false;
	for( const LookupListRec* lookup = &lookupList[0]; lookup->name; lookup++ )
	{
		if( strcasecmp( lookup->name, token ) == 0 )
		{
			return lookup->value;
			found = true;
			break;
		}
	}

	DEBUG_CRASH(("token %s is not a valid member of the lookup list\n",token));
	throw INI_INVALID_DATA;
	return 0;	// never executed, but keeps compiler happy

}

//-------------------------------------------------------------------------------------------------
const char* INI::getNextSubToken(const char* expected)
{
	const char* token = getNextToken(getSepsColon());
	if (strcasecmp(token, expected) != 0)
		throw INI_INVALID_DATA;
	return getNextToken(getSepsColon());
}

//-------------------------------------------------------------------------------------------------
/**
 * Parse a "random variable".
 * The format is "FIELD = low high [distribution]".
 */
void INI::parseGameClientRandomVariable( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	GameClientRandomVariable *var = static_cast<GameClientRandomVariable *>(store);

	const char* token;

	token = ini->getNextToken();
	Real low = INI::scanReal(token);

	token = ini->getNextToken();
	Real high = INI::scanReal(token);

	// if omitted, assume uniform
	GameClientRandomVariable::DistributionType type = GameClientRandomVariable::UNIFORM;
	token = ini->getNextTokenOrNull();
	if (token)
		type = (GameClientRandomVariable::DistributionType)INI::scanIndexList(token, GameClientRandomVariable::DistributionTypeNames);

	// set the range of the random variable
	var->setRange( low, high, type );
}

//-------------------------------------------------------------------------------------------------
// parse a duration in msec and convert to duration in frames
void INI::parseDurationReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	Real val = scanReal(ini->getNextToken());
	*(Real *)store = ConvertDurationFromMsecsToFrames(val);
}

//-------------------------------------------------------------------------------------------------
// parse a duration in msec and convert to duration in integral number of frames, (unsignedint) rounding UP
void INI::parseDurationUnsignedInt( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	UnsignedInt val = scanUnsignedInt(ini->getNextToken());
	*(UnsignedInt *)store = floatToUnsignedAsMsvc(ceilf(ConvertDurationFromMsecsToFrames((Real)val)));	// S8: as MSVC x64 converts it
}

// ------------------------------------------------------------------------------------------------
// parse a duration in msec and convert to duration in integral number of frames, (unsignedshort) rounding UP
void INI::parseDurationUnsignedShort( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	UnsignedInt val = scanUnsignedInt(ini->getNextToken());
	*(UnsignedShort *)store = floatToUnsignedShortAsMsvc(ceilf(ConvertDurationFromMsecsToFrames((Real)val)));	// S8
}

//-------------------------------------------------------------------------------------------------
// parse acceleration in (dist/sec) and convert to (dist/frame)
void INI::parseVelocityReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Real val = scanReal(token);
	*(Real *)store = ConvertVelocityInSecsToFrames(val);
}

//-------------------------------------------------------------------------------------------------
// parse acceleration in (dist/sec^2) and convert to (dist/frame^2)
void INI::parseAccelerationReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	const char *token = ini->getNextToken();
	Real val = scanReal(token);
	*(Real *)store = ConvertAccelerationInSecsToFrames(val);
}

//-------------------------------------------------------------------------------------------------
// fields that EA wrote as a count of 30 Hz frames, or as an amount applied every 30 Hz frame
void INI::parseRetailFramesUnsignedInt( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	*(UnsignedInt *)store = ConvertRetailFrames(scanUnsignedInt(ini->getNextToken()));
}

void INI::parseRetailFramesInt( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	*(Int *)store = ConvertRetailFrames(scanInt(ini->getNextToken()));
}

void INI::parseRetailFramesReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	*(Real *)store = ConvertRetailFrames(scanReal(ini->getNextToken()));
}

void INI::parseRetailPerFrameReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	*(Real *)store = ConvertRetailPerFrameRate(scanReal(ini->getNextToken()));
}

void INI::parseRetailPerFrameFactorReal( INI *ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	*(Real *)store = ConvertRetailPerFrameFactor(scanReal(ini->getNextToken()));
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseVeterancyLevelFlags(INI* ini, void* /*instance*/, void* store, const void* /*userData*/)
{
	VeterancyLevelFlags flags = VETERANCY_LEVEL_FLAGS_ALL;
	for (const char* token = ini->getNextToken(); token; token = ini->getNextTokenOrNull())
	{
		if (strcasecmp(token, "ALL") == 0)
		{
			flags = VETERANCY_LEVEL_FLAGS_ALL;
			continue;
		}
		else if (strcasecmp(token, "NONE") == 0)
		{
			flags = VETERANCY_LEVEL_FLAGS_NONE;
			continue;
		}
		else if (token[0] == '+')
		{
			VeterancyLevel dt = (VeterancyLevel)INI::scanIndexList(token+1, TheVeterancyNames);
			flags = setVeterancyLevelFlag(flags, dt);
			continue;
		}
		else if (token[0] == '-')
		{
			VeterancyLevel dt = (VeterancyLevel)INI::scanIndexList(token+1, TheVeterancyNames);
			flags = clearVeterancyLevelFlag(flags, dt);
			continue;
		}
		else
		{
			throw INI_UNKNOWN_TOKEN;
		}
	}
	*(VeterancyLevelFlags*)store = flags;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseSoundsList( INI* ini, void *instance, void *store, const void* /*userData*/ )
{
	std::vector<AsciiString> *vec = (std::vector<AsciiString>*) store;
	vec->clear();

	const char* SEPS = " \t,=";
	const char *c = ini->getNextTokenOrNull(SEPS);
	while ( c )
	{
		vec->push_back( c );
		c = ini->getNextTokenOrNull(SEPS);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseDamageTypeFlags(INI* ini, void* /*instance*/, void* store, const void* /*userData*/)
{
	DamageTypeFlags flags = DAMAGE_TYPE_FLAGS_NONE;
	flags.flip();

	for (const char* token = ini->getNextToken(); token; token = ini->getNextTokenOrNull())
	{
		if (strcasecmp(token, "ALL") == 0)
		{
			flags = DAMAGE_TYPE_FLAGS_NONE;
			flags.flip();
			continue;
		}
		if (strcasecmp(token, "NONE") == 0)
		{
			flags = DAMAGE_TYPE_FLAGS_NONE;
			continue;
		}
		if (token[0] == '+')
		{
			DamageType dt = (DamageType)scanIndexList(token+1, DamageTypeFlags::getBitNames());
			flags = setDamageTypeFlag(flags, dt);
			continue;
		}
		if (token[0] == '-')
		{
			DamageType dt = (DamageType)scanIndexList(token+1, DamageTypeFlags::getBitNames());
			flags = clearDamageTypeFlag(flags, dt);
			continue;
		}
		throw INI_UNKNOWN_TOKEN;
	}
	*(DamageTypeFlags*)store = flags;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseDeathTypeFlags(INI* ini, void* /*instance*/, void* store, const void* /*userData*/)
{
	DeathTypeFlags flags = DEATH_TYPE_FLAGS_ALL;
	for (const char* token = ini->getNextToken(); token; token = ini->getNextTokenOrNull())
	{
		if (strcasecmp(token, "ALL") == 0)
		{
			flags = DEATH_TYPE_FLAGS_ALL;
			continue;
		}
		if (strcasecmp(token, "NONE") == 0)
		{
			flags = DEATH_TYPE_FLAGS_NONE;
			continue;
		}
		if (token[0] == '+')
		{
			DeathType dt = (DeathType)INI::scanIndexList(token+1, TheDeathNames);
			flags = setDeathTypeFlag(flags, dt);
			continue;
		}
		if (token[0] == '-')
		{
			DeathType dt = (DeathType)INI::scanIndexList(token+1, TheDeathNames);
			flags = clearDeathTypeFlag(flags, dt);
			continue;
		}
		throw INI_UNKNOWN_TOKEN;
	}
	*(DeathTypeFlags*)store = flags;
}

//-------------------------------------------------------------------------------------------------
// parse the line and return whether the given line is a Block declaration of the form
// [whitespace] blockType [whitespace] blockName [EOL]
// both blockType and blockName are case insensitive
Bool INI::isDeclarationOfType( AsciiString blockType, AsciiString blockName, char *bufferToCheck )
{
	Bool retVal = true;
	if (!bufferToCheck || blockType.isEmpty() || blockName.isEmpty()) {
		return false;
	}
	// DO NOT RETURN EARLY FROM THIS FUNCTION. (beyond this point)
	// we have to restore the bufferToCheck to its previous state before returning, so 
	// it is important to get through all the checks.
	
	char restoreChar;
	char *tempBuff = bufferToCheck;
	int blockTypeLength = blockType.getLength();
	int blockNameLength = blockName.getLength();

	while (isspace(*tempBuff)) {
		++tempBuff;
	}
	
	if (strlen(tempBuff) > blockTypeLength) {
		restoreChar = tempBuff[blockTypeLength];
		tempBuff[blockTypeLength] = 0;
		
		if (strcasecmp(blockType.str(), tempBuff) != 0) {
			retVal = false;
		}

		tempBuff[blockTypeLength] = restoreChar;
		tempBuff = tempBuff + blockTypeLength;
	} else {
		retVal = false;
	}

	while (isspace(*tempBuff)) {
		++tempBuff;
	}

	if (strlen(tempBuff) > blockNameLength) {
		restoreChar = tempBuff[blockNameLength];
		tempBuff[blockNameLength] = 0;
		
		if (strcasecmp(blockName.str(), tempBuff) != 0) {
			retVal = false;
		}

		tempBuff[blockNameLength] = restoreChar;
		tempBuff = tempBuff + blockNameLength;
	} else {
		retVal = false;
	}

	while (strlen(tempBuff)) {
		retVal = retVal && isspace(tempBuff[0]);
		++tempBuff;
	}

	return retVal;
}

//-------------------------------------------------------------------------------------------------
// parse the line and return whether the given line is a Block declaration of the form
// [whitespace] end [EOL]
Bool INI::isEndOfBlock( char *bufferToCheck )
{
	Bool retVal = true;
	if (!bufferToCheck) {
		return false;
	}

	// DO NOT RETURN EARLY FROM THIS FUNCTION (beyond this point)
	// we have to restore the bufferToCheck to its previous state before returning, so 
	// it is important to get through all the checks.
	
	static const char* endString = "End";
	int endStringLength = strlen(endString);
	char restoreChar;
	char *tempBuff = bufferToCheck;
	

	while (isspace(*tempBuff)) {
		++tempBuff;
	}
	
	if (strlen(tempBuff) > endStringLength) {
		restoreChar = tempBuff[endStringLength];
		tempBuff[endStringLength] = 0;
		
		if (strcasecmp(endString, tempBuff) != 0) {
			retVal = false;
		}

		tempBuff[endStringLength] = restoreChar;
		tempBuff = tempBuff + endStringLength;
	} else {
		retVal = false;
	}

	while (strlen(tempBuff)) {
		retVal = retVal && isspace(tempBuff[0]);
		++tempBuff;
	}

	return retVal;
}
