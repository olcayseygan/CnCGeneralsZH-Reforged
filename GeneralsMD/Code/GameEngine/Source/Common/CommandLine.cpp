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


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "zhio.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/CommandLine.h"
#include "Common/CRCDebug.h"
#include "Common/LocalFileSystem.h"
#include "Common/OptionsCatalog.h"
#include "Common/version.h"
#include "GameClient/TerrainVisual.h" // for TERRAIN_LOD_MIN definition
#include "GameClient/GameText.h"
#include "GameClient/ChromaKeyboard.h"
#include "GameClient/GameConsole.h" // for -freecam
#include "GameNetwork/GameInfo.h" // for the SlotState -autoskirmish hands the AI slots
#include "GameNetwork/NetworkUtil.h" // for ResolveIP, which -lanip parses its address with
#include "Common/FileSystem.h"
#include "Common/RandomMapGenerator.h" // for -randommap

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif



Bool TheDebugIgnoreSyncErrors = FALSE;
extern Int DX8Wrapper_PreserveFPU;

#ifdef DEBUG_CRC
Int TheCRCFirstFrameToLog = -1;
UnsignedInt TheCRCLastFrameToLog = 0xffffffff;
Bool g_keepCRCSaves = FALSE;
Bool g_crcModuleDataFromLogic = FALSE;
Bool g_crcModuleDataFromClient = FALSE;
Bool g_verifyClientCRC = FALSE; // verify that GameLogic CRC doesn't change from client
Bool g_clientDeepCRC = FALSE;
Bool g_logObjectCRCs = FALSE;
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
extern Bool g_useStringFile;
#endif

// Retval is number of cmd-line args eaten
typedef Int (*FuncPtr)( char *args[], int num );

static const UnsignedByte F_NOCASE = 1; // Case-insensitive

struct CommandLineParam
{
	const char *name;
	FuncPtr func;
};

void ConvertShortMapPathToLongMapPath(AsciiString &mapName)
{
	AsciiString path = mapName;
	AsciiString token;
	AsciiString actualpath;

	if ((path.find('\\') == NULL) && (path.find('/') == NULL))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
		return;
	}
	/* Drive the walk on nextToken's own answer rather than on the token being non-empty: a path
		 that never names a .map file must leave this loop, and leave mapName as the player typed it
		 rather than half-rewritten. */
	Bool haveToken = path.nextToken(&token, "\\/");
	while (haveToken && !token.endsWithNoCase(".map"))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
		haveToken = path.nextToken(&token, "\\/");
	}

	if (!token.endsWithNoCase(".map"))
	{
		DEBUG_CRASH(("Invalid map name %s", mapName.str()));
		return;
	}
	// remove the .map from the end.
	token.removeLastChar();
	token.removeLastChar();
	token.removeLastChar();
	token.removeLastChar();

	// "maps\foo.map" is the short form and grows the directory the map lives in; a path that
	// already names that directory ("maps\foo\foo.map") is left alone instead of growing a third.
	AsciiString dir = token;
	dir.concat('\\');
	if (!actualpath.endsWithNoCase(dir.str()))
	{
		actualpath.concat(token);
		actualpath.concat('\\');
	}
	actualpath.concat(token);
	actualpath.concat(".map");

	mapName = actualpath;
}

//=============================================================================
//=============================================================================
Int parseNoLogOrCrash(char *args[], int)
{
#ifdef ALLOW_DEBUG_UTILS
	DEBUG_CRASH(("-NoLogOrCrash not supported in this build\n"));
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseWin(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_WINDOWED;
		applyWindowMode();
	}
	return 1;
}

//=============================================================================
/* -borderless is fullscreen without the display mode change: a windowed device the size of the
	 desktop, in a window with no caption and no frame, so it covers the screen exactly the way an
	 exclusive fullscreen device does but alt-tabs instantly, never loses the device on focus change,
	 and leaves a second monitor usable.  The window style and size are decided in WinMain, which
	 preparses -borderless off the raw command line long before this parser runs - the window has to
	 exist before the engine does.  What is left here is the back buffer, which has to match the
	 window or the picture is a stretched blit and the cursor no longer lands where it is drawn, and
	 applyWindowMode is what sizes it - the same code an Options.ini that says Borderless goes
	 through, so the switch and the setting cannot drift apart.

	 -xres/-yres after -borderless still win, for a smaller back buffer in a borderless window.

	 Edge scrolling goes back on because retail turns it off for windowed play - the cursor can
	 legitimately sit on the border while you reach for another window - and a window covering the
	 whole display has no such border.  Options.ini's EdgeScrollInWindowedMode is read before the
	 command line, so a player who set it either way is overridden here on purpose. */
//=============================================================================
Int parseBorderless(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_BORDERLESS;
		applyWindowMode();
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMusic(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_musicOn = false;
	}
	return 1;
}


//=============================================================================
//=============================================================================
Int parseNoVideo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_videoOn = false;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseFPUPreserve(char *args[], int argc)
{
	if (argc > 1)
	{
		DX8Wrapper_PreserveFPU = atoi(args[1]);
	}
	return 2;
}

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
//=============================================================================
Int parseUseCSF(char *args[], int)
{
	g_useStringFile = FALSE;
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoInputDisable(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableScriptedInputDisabling = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoFade(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraFade = true;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoMilCap(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableMilitaryCaption = true;
	}
	return 1;
}

#endif // _DEBUG || _INTERNAL

/* The CRC switches are out of the debug block on purpose (ROADMAP M1.2).  A Release build with the
   startup log has DEBUG_CRC compiled in already, so every one of these works there; they were only
   unreachable from the command line, which left a desync report from a tester's Release exe with
   nothing in it to act on. */
//=============================================================================
//=============================================================================
Int parseDebugCRCFromFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCFirstFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseDebugCRCUntilFrame(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		TheCRCLastFrameToLog = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseKeepCRCSave(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_keepCRCSaves = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCLogicModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromLogic = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseCRCClientModuleData(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_crcModuleDataFromClient = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseClientDeepCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_clientDeepCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseVerifyClientCRC(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_verifyClientCRC = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogObjectCRCs(char *args[], int argc)
{
#ifdef DEBUG_CRC
	g_logObjectCRCs = TRUE;
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNetCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		NET_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

//=============================================================================
//=============================================================================
Int parseReplayCRCInterval(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (argc > 1)
	{
		REPLAY_CRC_INTERVAL = atoi(args[1]);
	}
#endif
	return 2;
}

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
//=============================================================================
Int parseNoDraw(char *args[], int argc)
{
#ifdef DEBUG_CRC
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_noDraw = TRUE;
	}
#endif
	return 1;
}

//=============================================================================
//=============================================================================
Int parseLogToConsole(char *args[], int)
{
	DebugSetFlags(DebugGetFlags() | DEBUG_FLAG_LOG_TO_CONSOLE);
	return 1;
}

#endif // _DEBUG || _INTERNAL

//=============================================================================
//=============================================================================
Int parseNoAudio(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_audioOn = false;
		TheWritableGlobalData->m_speechOn = false;
		TheWritableGlobalData->m_soundsOn = false;
		TheWritableGlobalData->m_musicOn = false;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseNoWin(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_FULLSCREEN;
		applyWindowMode();
	}
	return 1;
}

Int parseFullVersion(char *args[], int num)
{
	if (TheVersion && num > 1)
	{
		TheVersion->setShowFullVersion(atoi(args[1]) != 0);
	}
	return 1;
}

Int parseNoShadows(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useShadowVolumes = false;
		TheWritableGlobalData->m_useShadowDecals = false;
	}
	return 1;
}

Int parseMapName(char *args[], int num)
{
	// num is what is *left* on the command line, so the original "== 2" quietly ignored the map
	// whenever another option followed it, and returning 1 left the file name to be re-parsed.
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_mapName.set( args[ 1 ] );
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_mapName);
		return 2;
	}
	return 1;
}

// parseRandomMap =============================================================
/** -randommap <seed> [players] [cells|small|normal|large]: generate a skirmish map from the seed,
	keep it in memory under the name
	"RMG_v<generator version>_<seed>_<players>p_<cells>c", then point -map at it.  The version is in
	the name because it is in the bytes: a build that generates a different map from the same seed
	names a different map rather than a confusing one.  Nothing is written to disk and nothing
	downstream has to know: the file system serves that name out of the generator, so the map cache
	lists it like any hand-made map and -autoskirmish starts on it.  The same seed gives the same
	map on every machine, so both sides of a network game can be handed the same command line.
	The last argument is either a cell count outright or one of the three sizes, which are counts
	that grow with the number of players rather than fixed ones. */
//=============================================================================
Int parseRandomMap(char *args[], int num)
{
	if (TheWritableGlobalData == NULL || num < 2)
		return 1;

	RandomMapSettings settings;
	settings.m_seed = atoi( args[1] );
	// Generate all the start positions the map is allowed to hold, so any -autoskirmish count
	// fits regardless of which of the two options came first on the command line.
	settings.m_numPlayers = RandomMapGenerator::MAX_PLAYERS;

	// players and size are optional and positional, so only eat what still looks like one.
	Int eaten = 2;
	if (num > eaten && isdigit((UnsignedByte)args[eaten][0]))
		settings.m_numPlayers = atoi( args[eaten++] );

	if (num > eaten && isdigit((UnsignedByte)args[eaten][0]))
	{
		settings.m_playableCells = atoi( args[eaten++] );
	}
	else if (num > eaten)
	{
		RandomMapSize size = RANDOM_MAP_SIZE_COUNT;
		if (strcasecmp( args[eaten], "small" ) == 0)
			size = RANDOM_MAP_SIZE_SMALL;
		else if (strcasecmp( args[eaten], "normal" ) == 0)
			size = RANDOM_MAP_SIZE_NORMAL;
		else if (strcasecmp( args[eaten], "large" ) == 0)
			size = RANDOM_MAP_SIZE_LARGE;

		if (size != RANDOM_MAP_SIZE_COUNT)
		{
			settings.m_playableCells = RandomMapGenerator::cellsFor( size, settings.m_numPlayers );
			eaten++;
		}
	}

	AsciiString path;
	if (!stageRandomMap( settings, path ))
		return eaten;

	TheWritableGlobalData->m_mapName = path;
	return eaten;
}

Int parseXRes(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_xResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseYRes(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_yResolution = atoi(args[1]);
		return 2;
	}
	return 1;
}

/* The five synthetic link conditions below used to be debug-build-only, and this fork only ever
	 ships Release, so the switch they drive did not exist in any build anyone could run.  They are
	 the cheapest way to reproduce a lossy or slow link without a second machine on a bad line. */
//=============================================================================
//=============================================================================
Int parseLatencyAverage(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyAverage = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyAmplitude(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyAmplitude = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyPeriod(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyPeriod = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parseLatencyNoise(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_latencyNoise = atoi(args[1]);
	}
	return 2;
}

//=============================================================================
//=============================================================================
Int parsePacketLoss(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_packetLoss = atoi(args[1]);
	}
	return 2;
}

#if defined(_DEBUG) || defined(_INTERNAL)
//=============================================================================
//=============================================================================
Int parseLowDetail(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_MIN;
	}
	return 1;
}

//=============================================================================
//=============================================================================

//=============================================================================
//=============================================================================
Int parseNoStaticLOD(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_enableStaticLOD = FALSE;
	}
	return 1;
}

//=============================================================================
//=============================================================================
Int parseUseWaveEditor(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_usingWaterTrackEditor = TRUE;
	}
	return 1;
}

//=============================================================================
Int parseNoViewLimit(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useCameraConstraints = FALSE;
	}
	return 1;
}

Int parseWireframe(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_wireframe = TRUE;
	}
	return 1;
}

Int parseShowCollision(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showCollisionExtents = TRUE;
	}
	return 1;
}

Int parseNoShowClientPhysics(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showClientPhysics = FALSE;
	}
	return 1;
}

Int parseShowTerrainNormals(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_showTerrainNormals = TRUE;
	}
	return 1;
}

Int parseStateMachineDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_stateMachineDebug = TRUE;
	}
	return 1;
}

Int parseJabber(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_jabberOn = TRUE;
	}
	return 1;
}

Int parseMunkee(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_munkeeOn = TRUE;
	}
	return 1;
}
#endif // defined(_DEBUG) || defined(_INTERNAL)

Int parseScriptDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_scriptDebug = TRUE;
		TheWritableGlobalData->m_winCursors = TRUE;
	}
	return 1;
}

Int parseParticleEdit(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_particleEdit = TRUE;
		TheWritableGlobalData->m_winCursors = TRUE;
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_WINDOWED;
		applyWindowMode();
	}
	return 1;
}


Int parseBuildMapCache(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_buildMapCache = true;
	}
	return 1;
}


#if defined(_DEBUG) || defined(_INTERNAL)
Int parseDisplayDebug(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_displayDebug = TRUE;
	}
	return 1;
}

Int parseFile(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_initialFile = args[1];
		ConvertShortMapPathToLongMapPath(TheWritableGlobalData->m_initialFile);
	}
	return 2;
}


Int parsePreloadEverything( char *args[], int num )
{
	if( TheWritableGlobalData )
		TheWritableGlobalData->m_preloadEverything = TRUE;
	return 1;
}

Int parseLogAssets( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		FILE *logfile=zh_fopen("PreloadedAssets.txt","w");
		if (logfile)	//clear the file
			fclose(logfile);
		TheWritableGlobalData->m_preloadReport = TRUE;
	}
	return 1;
}

/// begin stuff for VTUNE
Int parseVTune ( char *args[], int num )
{
	if( TheWritableGlobalData )
		TheWritableGlobalData->m_vTune = TRUE;
	return 1;
}
/// end stuff for VTUNE

#endif // defined(_DEBUG) || defined(_INTERNAL)

//=============================================================================
//=============================================================================

Int parseNoFX(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useFX = FALSE;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseNoShroud(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shroudOn = FALSE;
	}
	return 1;
}
#endif

Int parseForceBenchmark(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_forceBenchmark = TRUE;
	}
	return 1;
}

Int parseNoMoveCamera(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraMovement = true;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseNoCinematic(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_disableCameraMovement = true;
		TheWritableGlobalData->m_disableMilitaryCaption = true;
		TheWritableGlobalData->m_disableCameraFade = true;
		TheWritableGlobalData->m_disableScriptedInputDisabling = true;
	}
	return 1;
}
#endif

Int parseSync(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheDebugIgnoreSyncErrors = true;
	}
	return 1;
}

Int parseNoShellMap(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shellMapOn = FALSE;
	}
	return 1;
}

// Turn terrain collision on for every particle system at once.  The shipped
// ParticleSystem.ini lives inside INIZH.big and a loose copy would have to replace the whole
// file, so this is how the effect gets seen before any INI is written.
// ponytail: a preview switch - the real answer is GroundCollision per system in the INI
Int parseParticleBounce(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_particleGroundBounce = TRUE;
	}
	return 1;
}

/* -smoke [thickness]: every particle system with "smoke" in its name burns longer and reads
	 thicker.  Bare -smoke is 2, which is a fire that is still smoking when the wreck has stopped
	 burning; the switch clamps at 8.

	 The number is spent across particle lifetime, opacity and width rather than on any one of
	 them, and it raises the particle ceiling with it: the eviction that enforces the shipped
	 ceiling deletes the oldest particle first, and long-lived smoke is the oldest thing on the
	 field, so without the extra headroom the switch would spend its own smoke to make room for
	 sparks.  particleSmokeBoostResolve in ParticleSys.cpp is where the split lives.

	 Same reason as -particlebounce for this being a switch: the shipped ParticleSystem.ini is
	 inside INIZH.big and a loose copy would have to replace all 1088 systems to change 175. */
Int parseSmoke(char *args[], int num)
{
	const Real DEFAULT_THICKNESS = 2.0f;

	if (TheWritableGlobalData)
	{
		if (num > 1 && args[1][0] != '-')
		{
			TheWritableGlobalData->m_smokeThickness = (Real)atof(args[1]);
			return 2;
		}
		TheWritableGlobalData->m_smokeThickness = DEFAULT_THICKNESS;
	}
	return 1;
}

/* -noDynamicLOD: the detail level does not follow the frame rate, and every effect draws at the highest
	 level (W3DDisplay::draw forces it).  EA had it in the Debug and Internal builds only, so a Release build
	 ignored it (measured: a Release run with it still dropped to Medium); it is for every build now, like
	 -particlecap, because a comparison of two renderers or two machines has to draw the same effects.
	 GameLODManager::init applies the static preset after the command line is parsed and sets
	 m_enableDynamicLOD from it, so the switch is a flag of its own, read where the game acts on dynamic LOD
	 (GlobalData::isDynamicLODEnabled).  m_enableDynamicLOD, the setting the options menu shows and saves, is
	 not touched, so a run with the switch never saves it as the player's choice. */
Int parseNoDynamicLOD(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_noDynamicLODOverride = TRUE;
	}
	return 1;
}

/* -particlecap <n>: stand in for the options menu's particle slider for one run.

	 The slider writes MaxParticleCount into the player's own Options.ini and the LOD manager
	 applies it well after the command line is parsed, so this is read where the ceiling is read
	 rather than written over the top of it.  Worth having because the shipped default is 2500 and
	 a machine whose owner once dragged that slider left is a machine where every effect in the
	 game is starved, which looks exactly like an effect that was never written. */
Int parseParticleCap(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_particleCapOverride = atoi(args[1]);
	}
	return 2;
}

/* The sun's shadow map is how this game draws shadows, and there is no switch for it.  It fills
	 every frame and the stencil volumes stand down behind it; a machine with no Direct3D 11 device
	 fills no map and keeps the volumes on its own, which is the whole of the fallback.  What is
	 left below are the three switches that exist for looking at the shadows rather than for playing
	 with them.  SHADOW-MAP-PLAN.md. */

/* -shadowmapboth: the map and the stencil volumes at once, which is not a picture anybody should
	 play with.  It exists because the two can only be compared in one frame when both are in it. */
Int parseShadowMapBoth(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shadowMap = TRUE;
		TheWritableGlobalData->m_shadowMapOnly = FALSE;
	}
	return 1;
}

/* -nochroma: no Razer lighting for this run.

	 The lighting owns a worker thread and three HTTP round trips every tenth of a second, all of
	 them off the render thread, but a frame measurement wants none of that in the picture at all. */
Int parseNoChroma(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_chromaLighting = FALSE;
	}
	disableChromaKeyboard();
	return 1;
}

Int parseShadowMapReport(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shadowMap = TRUE;
		TheWritableGlobalData->m_shadowMapReport = TRUE;
	}
	return 1;
}

/* -noparticleshadows: take the soft blob back off the ground under every particle cloud.

	 ShadowsForParticles is on by default and the shipped INI has no entry for it, so without this
	 there is no way to photograph a frame with the smoke and without its shadow - which is the
	 only way to say how much of the darkening under a cloud is the decal and how much is the
	 sprites themselves. */
Int parseNoParticleShadows(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shadowsForParticles = FALSE;
	}
	return 1;
}

/* -novolumetricsmoke: smoke and dust out of the sun's map for one run, which is the other half of
	 any picture of what they shade.  The blob under each cloud comes back with it, as it does on a
	 machine with no Direct3D 11 device. */
Int parseNoVolumetricSmoke(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_volumetricSmokeShadows = FALSE;
	}
	return 1;
}

/* -nosmokefirelight: smoke left unlit by the fire beside it, for one run. */
Int parseNoSmokeFireLight(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_smokeFireLighting = FALSE;
	}
	return 1;
}

Int parseNoShaders(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_chipSetType = 1;	//force to a voodoo card which uses least amount of features.
	}
	return 1;
}

Int parseNoLogo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_afterIntro = TRUE;
		TheWritableGlobalData->m_playSizzle = FALSE;
	}
	return 1;
}

Int parseNoSizzle( char *args[], int )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_playSizzle = FALSE;
	}
	return 1;
}

Int parseShellMap(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_shellMapName = args[1];
	}
	return 2;
}

Int parseNoWindowAnimation(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_animateWindows = FALSE;
	}
	return 1;
}

Int parseWinCursors(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_winCursors = TRUE;
	}
	return 1;
}

Int parseQuickStart( char *args[], int num )
{
#if (defined(_DEBUG) || defined(_INTERNAL))
  parseNoLogo( args, num );
#else
	//Kris: Patch 1.01 -- Allow release builds to skip the sizzle video, but still force the EA logo to show up.
	//This is for legal reasons.
	parseNoSizzle( args, num );
#endif
	parseNoShellMap( args, num );
	parseNoWindowAnimation( args, num );
	return 1;
}

Int parseConstantDebug( char *args[], int num )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_constantDebugUpdate = TRUE;
	}
	return 1;
}

#if (defined(_DEBUG) || defined(_INTERNAL))
Int parseExtraLogging( char *args[], int num )
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_extraLogging = TRUE;
	}
	return 1;
}
#endif

//-allAdvice feature
/*
Int parseAllAdvice( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_allAdvice = TRUE;
	}
	return 1;
}
*/

Int parseShowTeamDot( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_showTeamDot = TRUE;
	}
	return 1;
}


#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSelectAll( char *args[], int num )
{
	if( TheWritableGlobalData )
	{
		TheWritableGlobalData->m_allowUnselectableSelection = TRUE;
	}
	return 1;
}

Int parseRunAhead( char *args[], Int num )
{
	if (num > 2)
	{
		MIN_RUNAHEAD = atoi(args[1]);
		MAX_FRAMES_AHEAD = atoi(args[2]);
		FRAME_DATA_LENGTH = (MAX_FRAMES_AHEAD + 1)*2;
	}
	return 3;
}
#endif


Int parseSeed(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_fixedSeed = atoi(args[1]);
	}
	return 2;
}

Int parseAutoSkirmish(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int players = atoi(args[1]);
		if (players < 2)
			players = 2;
		if (players > MAX_SLOTS)
			players = MAX_SLOTS;
		TheWritableGlobalData->m_autoSkirmishPlayers = players;
	}
	return 2;
}

Int parseAIDifficulty(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		//
		// The three rungs of the ladder. Anything unrecognised is still the top rung, so a batch
		// script naming a rung that no longer exists gets the hardest AI rather than the easiest.
		//
		AsciiString difficulty = args[1];
		if (difficulty.compareNoCase("easy") == 0)
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_EASY_AI;
		else if (difficulty.compareNoCase("medium") == 0 || difficulty.compareNoCase("med") == 0)
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_MED_AI;
		else
			TheWritableGlobalData->m_autoSkirmishAIState = SLOT_BRUTAL_AI;
	}
	return 2;
}

/** -aidiff2 <name>: give the odd-numbered skirmish slots a different rung from -aidiff.
	*
	* Without this the batch runner can only play a rung against itself, which says nothing about
	* whether the ladder is a ladder.  The whole no-cheat design (AI-ROADMAP.md D6) rests on higher
	* rungs actually beating lower ones through better decisions, and that is not a claim to make
	* without measuring it. */
Int parseAIDifficulty2(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString difficulty = args[1];
		if (difficulty.compareNoCase("easy") == 0)
			TheWritableGlobalData->m_autoSkirmishAIStateOdd = SLOT_EASY_AI;
		else if (difficulty.compareNoCase("medium") == 0 || difficulty.compareNoCase("med") == 0)
			TheWritableGlobalData->m_autoSkirmishAIStateOdd = SLOT_MED_AI;
		else
			TheWritableGlobalData->m_autoSkirmishAIStateOdd = SLOT_BRUTAL_AI;
	}
	return 2;
}

/** -notactics even|odd: those slots keep their rung but fight without its unit tactics, so a batch can
	* play Hard against the same Hard minus one thing, from both sides of the map.  Read only in a
	* single-player skirmish (AIPlayer::doTactics), so a network game never sees it. */
Int parseNoTactics(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString parity = args[1];
		TheWritableGlobalData->m_noTacticsSlotParity = (parity.compareNoCase("even") == 0) ? 0 : 1;
	}
	return 2;
}

/** -aiknobsoff even|odd|all engagegate,answerarmy,massunit: those slots keep their rung with these knobs
	* of its profile off, so a batch can play Hard against Hard without them, or a scenario against the AI
	* as it was.  A measuring aid like -notactics: read only in a single-player skirmish, never in a
	* network game, and a replay made with it is played back with it given again. */
Int parseAIKnobsOff(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2)
	{
		AsciiString parity = args[1];
		TheWritableGlobalData->m_aiKnobsOffParity = parity.compareNoCase("even") == 0 ? 0 : (parity.compareNoCase("odd") == 0 ? 1 : 2);
		AsciiString names = args[2];
		names.toLower();
		Int mask = 0;
		if (strstr(names.str(), "engagegate")) mask |= AIKNOB_ENGAGE_GATE;
		if (strstr(names.str(), "answerarmy")) mask |= AIKNOB_ANSWER_ARMY;
		if (strstr(names.str(), "massunit")) mask |= AIKNOB_MASS_UNIT;
		TheWritableGlobalData->m_aiKnobsOffMask = mask;
	}
	return 3;
}

Int parseObserver(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_autoSkirmishObserver = TRUE;
	}
	return 1;
}

Int parseIncrAGPBuf(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_incrementalAGPBuf = TRUE;
	}
	return 1;
}

Int parseNetMinPlayers(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_netMinPlayers = atoi(args[1]);
	}
	return 2;
}

Int parsePlayStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}

Int parseDemoLoadScreen(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_loadScreenDemo = TRUE;
	}
	return 1;
}

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSaveStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseSaveAllStats(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_saveStats = TRUE;
		TheWritableGlobalData->m_baseStatsDir = args[1];
		TheWritableGlobalData->m_saveAllStats = TRUE;
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseLocalMOTD(char *args[], int num)
{
	if (TheWritableGlobalData  && num > 1)
	{
		TheWritableGlobalData->m_useLocalMOTD = TRUE;
		TheWritableGlobalData->m_MOTDPath = args[1];
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseCameraDebug(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_debugCamera = TRUE;
	}
	return 1;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseBenchmark(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_benchmarkTimer = atoi(args[1]);
		TheWritableGlobalData->m_playStats  = atoi(args[1]);
	}
	return 2;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
#ifdef DUMP_PERF_STATS
Int parseStats(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_statsInterval  = atoi(args[1]);
		// W3DDisplay takes the frame modulo this; -stats 0 or a word divided by zero
		TheWritableGlobalData->m_dumpStatsAtInterval = TheWritableGlobalData->m_statsInterval > 0;
	}
	return 2;
}
#endif
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseIgnoreAsserts(char *args[], int num)
{
	if (TheWritableGlobalData && num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreAsserts = true;
	}
	return 1;
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
Int parseIgnoreStackTrace(char *args[], int num)
{
	if (TheWritableGlobalData && num > 0)
	{
		TheWritableGlobalData->m_debugIgnoreStackTrace = true;
	}
	return 1;
}
#endif

/* -fps <n> is the game speed itself: the rate the logic tick is paced at, the command-line twin of
	 the skirmish menu's game speed slider.  MSG_NEW_GAME only honours 1..1000. */

Int parseFPSLimit(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_framesPerSecondLimit = atoi(args[1]);
	}
	return 2;
}

/* -noFPSLimit uncaps the *renderer*, not the simulation.  It used to raise
	 m_framesPerSecondLimit to 30000 because the old loop ran one logic frame per pass, so the only
	 way to render freely was to let the logic run freely too - and the game then played at whatever
	 speed the machine managed.  GameEngine::update() now paces the logic tick against wall clock
	 and GameEngine::execute() never sleeps for a frame budget, so rendering is already uncapped
	 unconditionally and m_framesPerSecondLimit means game speed and nothing else.  Raising it here
	 would just run the match in fast-forward: superweapon and build timers count real seconds off a
	 clock ticking a thousand times too fast.  Use -fps to ask for that on purpose. */
Int parseNoFPSLimit(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_useFpsLimit = false;
	}
	return 1;
}

/* -headless is the unattended-run switch: no frame is ever drawn, the logic tick runs flat out
	 instead of being paced against wall clock, audio is silenced, and the process quits by itself
	 the moment the match is decided.  A window and a D3D device are still created - W3D reaches into
	 drawables and the asset manager throughout, so a true null display is a much larger change than
	 skipping the one draw call is worth.  That window is forced windowed and tiny: nothing is ever
	 drawn into it, and a fullscreen device would take the desktop's display mode away from whoever
	 is using the machine while the batch runs.  -xres/-yres after -headless still win if a run
	 wants a real back buffer.  Pair it with -autoskirmish and -observer for a soak run.

	 Audio goes off because at several hundred logic frames a second the game hands the mixer a few
	 thousand events a second that nobody will hear; -headless is for a machine, not a listener. */
enum { HEADLESS_RESOLUTION = 100 };	// big enough for a legal back buffer, small enough to ignore

Int parseHeadless(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_headless = TRUE;
		// windowed, and plainly so: a borderless Options.ini would otherwise hand a batch run the
		// whole display to draw a picture nobody is looking at
		TheWritableGlobalData->m_windowMode = WINDOW_MODE_WINDOWED;
		applyWindowMode();
		TheWritableGlobalData->m_xResolution = HEADLESS_RESOLUTION;
		TheWritableGlobalData->m_yResolution = HEADLESS_RESOLUTION;
		TheWritableGlobalData->m_audioOn = FALSE;
		TheWritableGlobalData->m_musicOn = FALSE;
		TheWritableGlobalData->m_soundsOn = FALSE;
		TheWritableGlobalData->m_speechOn = FALSE;
		TheWritableGlobalData->m_videoOn = FALSE;
		// a run that draws nothing has no use for a second device mirroring every buffer it loads
		TheWritableGlobalData->m_direct3D11 = FALSE;
	}
	return 1;
}

/* -maxframes <n> bounds a headless run in logic frames rather than in seconds, so the cutoff is
	 the same on every machine and in every replay.  A stalemate between eight brutal AIs is a real
	 outcome and it does not end on its own. */
Int parseMaxGameFrames(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_maxGameFrames = atoi(args[1]);
	}
	return 2;
}

/* -screenshot <n>: save one picture of the running game at logic frame n.
	 *
	 * F12 has always taken one, and a key is no use to anything that runs on its own. A renderer
	 * change with no picture to compare against can only be argued about, which is why a row of
	 * graphics work in the upstream ledger sits unclosed - not because the code is hard, because
	 * nobody could see the result. Combine with -autoskirmish, -map and -maxframes; -headless draws
	 * nothing and says so. The file goes next to the save games, as sshotNNN.bmp. */
/** -showHudOverlay: the corner readout on, whatever the build's default (off in Release) and GameData.ini
	* say.  The harnesses whose screenshots are evidence pass it, so every picture names its renderer, clock
	* and frame; the command line is read after GameData.ini, so nothing there turns it back off. */
Int parseShowHudOverlay(char *args[], int)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_showHudOverlay = TRUE;
	return 1;
}

Int parseScreenShot(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int frame = atoi(args[1]);
		if (frame < 1)
			frame = 1;
		TheWritableGlobalData->m_screenShotFrame = frame;
	}
	return 2;
}

/* -video <from> <to> [name]: record logic frames <from> to <to> of the match as a movie.
	 *
	 * -screenshot shows one frame.  A unit turning the wrong way, an animation that hitches or a
	 * formation coming apart is a sequence, and a script cannot see a sequence one launch per picture.
	 * Every logic frame in the range is drawn once and written as frameNNNNNN.bmp under Videos\<name>\
	 * next to the save games; when the range is over, or the game exits first, ffmpeg.exe off the PATH
	 * turns them into Videos\<name>.mp4 and the frames are deleted.  Without ffmpeg they stay.  The name
	 * defaults to video_<from>_<to> and may hold letters, digits, '-' and '_' only, because it becomes a
	 * directory and an argument on ffmpeg's command line. */
static Bool isVideoNameUsable(const char *name)
{
	if (*name == '\0')
		return FALSE;
	for (const char *letter = name; *letter; ++letter)
	{
		const Bool usable = (*letter >= 'a' && *letter <= 'z') || (*letter >= 'A' && *letter <= 'Z')
			|| (*letter >= '0' && *letter <= '9') || *letter == '-' || *letter == '_';
		if (!usable)
			return FALSE;
	}
	return TRUE;
}

Int parseVideo(char *args[], int num)
{
	if (num < 3)
	{
		DEBUG_LOG(("-video: wants the first and the last logic frame to record, got %d arguments\n", num - 1));
		return num;
	}

	Int from = atoi(args[1]);
	if (from < 1)
		from = 1;
	const Int to = atoi(args[2]);

	AsciiString name;
	name.format("video_%d_%d", from, to);
	Int consumed = 3;
	if (num > 3 && args[3][0] != '-')
	{
		consumed = 4;
		if (isVideoNameUsable(args[3]))
			name = args[3];
		else
			DEBUG_LOG(("-video: '%s' is not a usable name (letters, digits, '-' and '_'), recording as %s\n",
				args[3], name.str()));
	}

	if (to < from)
	{
		DEBUG_LOG(("-video: the last frame %d comes before the first frame %d, so nothing is recorded\n", to, from));
		return consumed;
	}

	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_videoStartFrame = from;
		TheWritableGlobalData->m_videoEndFrame = to;
		TheWritableGlobalData->m_videoName = name;
	}
	return consumed;
}

/* -directorrecord [name]: film a whole match with the director camera, as Videos\<name>.mp4.
	 *
	 * -video records a range of frames from wherever the camera happens to be.  This hands the camera
	 * to the observer's director from the first frame, takes the interface off but for the radar, and
	 * records from frame 1 until the match is decided or the replay runs out, then quits.  When two
	 * fights far apart are both worth watching, the picture splits down a leaning line, one fight a
	 * half.  It sets -observer, so an -autoskirmish run is watched rather than played; with -replay the
	 * watcher is the replay's own.  The name defaults to "director" and takes -video's rules. */
Int parseDirectorRecord(char *args[], int num)
{
	AsciiString name = "director";
	Int consumed = 1;
	if (num > 1 && args[1][0] != '-')
	{
		consumed = 2;
		if (isVideoNameUsable(args[1]))
			name = args[1];
		else
			DEBUG_LOG(("-directorrecord: '%s' is not a usable name (letters, digits, '-' and '_'), recording as %s\n",
				args[1], name.str()));
	}

	if (TheWritableGlobalData)
	{
		// no last frame: the run ends with the match, and the display's teardown finishes the movie
		const Int NO_LAST_FRAME = 0x3FFFFFFF;
		TheWritableGlobalData->m_directorRecord = TRUE;
		TheWritableGlobalData->m_videoStartFrame = 1;
		TheWritableGlobalData->m_videoEndFrame = NO_LAST_FRAME;
		TheWritableGlobalData->m_videoName = name;
		TheWritableGlobalData->m_autoSkirmishObserver = TRUE;
	}
	return consumed;
}

/* -directorscout <file> and -directortimeline <file>: the two passes of -directorrecord.  WinMain
	 starts the same match again headless with -directorscout before it films anything, and that run
	 writes down where and when every fight starts and every special power is used; the filming run
	 is handed the file with -directortimeline and arrives at each fight before it starts.  Neither is
	 a switch for a person: WinMain names the file and passes both. */
Int parseDirectorScout(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_directorScoutFile = args[1];
	return 2;
}

Int parseDirectorTimeline(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
		TheWritableGlobalData->m_directorTimelineFile = args[1];
	return 2;
}

/* -wav <from> <to> [name]: record what the game sounds like over logic frames <from> to <to>.
	 *
	 * A movie made by -video has no sound in it: the picture comes from a frame dump and the frames are
	 * saved as fast as the disk takes them, which is nothing like the speed the mixer plays at.  Sound
	 * has to come off a second run at the pace a player would hear it, and the two runs line up because
	 * the same seed and the same shot list play the same match twice.  Everything the game plays goes
	 * through one mastering voice, so the recording is the finished mix - music, effects, speech, the
	 * 3D positioning, the lot - written to Videos\<name>.wav next to the save games.  Mux it onto the
	 * picture afterwards: ffmpeg -i <name>.mp4 -i <name>.wav -c:v copy -shortest <name>_sound.mp4.
	 *
	 * The run logs how long the recording took against how long the frames say it should have, which is
	 * the only thing that can go wrong here: a run that cannot hold 30 frames a second drifts away from
	 * the picture, and the AUDIO line says by how much before anyone edits with it. */
Int parseWav(char *args[], int num)
{
	if (num < 3)
	{
		DEBUG_LOG(("-wav: wants the first and the last logic frame to record, got %d arguments\n", num - 1));
		return num;
	}

	Int from = atoi(args[1]);
	if (from < 1)
		from = 1;
	const Int to = atoi(args[2]);

	AsciiString name;
	name.format("video_%d_%d", from, to);
	Int consumed = 3;
	if (num > 3 && args[3][0] != '-')
	{
		consumed = 4;
		if (isVideoNameUsable(args[3]))
			name = args[3];
		else
			DEBUG_LOG(("-wav: '%s' is not a usable name (letters, digits, '-' and '_'), recording as %s\n",
				args[3], name.str()));
	}

	if (to < from)
	{
		DEBUG_LOG(("-wav: the last frame %d comes before the first frame %d, so nothing is recorded\n", to, from));
		return consumed;
	}

	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_wavStartFrame = from;
		TheWritableGlobalData->m_wavEndFrame = to;
		TheWritableGlobalData->m_wavName = name;
	}
	return consumed;
}

/* -turbo: a run that draws plays its match as fast as the machine draws it, one logic frame a pass,
	 the branch -headless and -video already take.  A -screenshot at frame 3000 otherwise waits 100
	 seconds of wall clock at 30 logic frames a second for a picture that only needs frame 3000 to be
	 reached.  The logic is identical frame for frame (same HEADLESS CRC), and a turbo shot repeats
	 itself, but it is not the paced shot: the cloud shadows scroll on the wall clock, and the two
	 differ on 7% of the pixels.  Compare turbo with turbo.  Ignored in a network game, whose clock
	 is the network's, and under -wav, which has to run at the speed a person hears. */
Int parseTurbo(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_turbo = TRUE;
	}
	return 1;
}

/* -autocamera [seconds]: every so often, put the camera wherever the fighting is.
	 *
	 * A soak run watches from a free camera that never moves, and a camera that never moves is the
	 * one thing a real player's never is.  Everything a moving camera drags in behind it - the
	 * terrain window scrolling, shroud updates, models and textures loading the first time they come
	 * on screen - is invisible to a run that stares at one spot, which is exactly the blind spot a
	 * stutter likes to live in.  Default 5 seconds if no number is given. */
/* -msaa [N]: multisampled back buffer.  Bare means 4x, a number means N (2..16), and anything the
	 device will not give is degraded on its own inside DX8Wrapper.  It is stored as the same index
	 the options menu writes to Options.ini, so the switch and the setting are one value; the switch
	 wins because the command line is parsed after the preferences file. */
Int parseMSAA(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		unsigned samples = 4;	// a bare -msaa means 4x
		Int consumed = 1;
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			samples = (unsigned)atoi(args[1]);
			consumed = 2;
		}
		TheWritableGlobalData->m_msaaLevel = msaaLevelForSamples(samples);
		return consumed;
	}
	return 1;
}

/* -d3d9: draw and present with Direct3D 9 alone, the way the game did before the Direct3D 11
	 * backend became the renderer.
	 *
	 * The Direct3D 11 frame is the default, and this is the reference it is measured against:
	 * dx11-check.ps1 photographs one frame both ways, and a frame-time comparison of the two
	 * backends needs a run where the second device does not exist at all. */
Int parseDirect3D9(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_direct3D11 = FALSE;
	}
	return 1;
}

/* -language <english|turkish|german>: the language the game's words are in for this run, over whatever
	 * the Options menu saved.  The string table is built once, while the game starts and after this
	 * line is read, so this is how the launcher lets a player pick a language before the first menu.
	 * The names follow TextLanguageType.  A name this build does not know leaves the saved one. */
Int parseTextLanguage(char *args[], int num)
{
	static const char *const TheTextLanguageNames[ TEXT_LANGUAGE_COUNT ] = { "english", "turkish", "german" };

	if (TheWritableGlobalData && num > 1)
	{
		for (Int language = 0; language < TEXT_LANGUAGE_COUNT; ++language)
		{
			if (strcasecmp(args[1], TheTextLanguageNames[language]) == 0)
			{
				TheWritableGlobalData->m_textLanguage = language;
				DEBUG_LOG(("-language: %s\n", TheTextLanguageNames[language]));
				return 2;
			}
		}
		DEBUG_LOG(("-language: '%s' is not a language this build knows, keeping the saved one\n", args[1]));
		return 2;
	}
	return 1;
}

/* -interface <classic|reforged>: the interface for this run, the only way to pick it; the launcher
	 * passes it, and without it the run is Classic.  Client only. */
Int parseInterfaceStyle(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		if (strcasecmp(args[1], "classic") == 0)
			TheWritableGlobalData->m_interfaceStyle = INTERFACE_STYLE_CLASSIC;
		else if (strcasecmp(args[1], "reforged") == 0)
			TheWritableGlobalData->m_interfaceStyle = INTERFACE_STYLE_REFORGED;
		DEBUG_LOG(("-interface: %s\n", TheWritableGlobalData->m_interfaceStyle == INTERFACE_STYLE_CLASSIC ? "classic" : "reforged"));
		return 2;
	}
	return 1;
}

/* -dx11dump <directory>: write every program the Direct3D 11 backend generates into that
	 * directory as it is built, named by the order it was built in with the state it came from on
	 * its first line.  A generated program that draws the wrong thing cannot be read any other way:
	 * the state is a key and the key is not the code. */
Int parseDirect3D11Dump(char *args[], int num)
{
	if (num > 1 && TheWritableGlobalData)
	{
		TheWritableGlobalData->m_direct3D11DumpPath = args[1];
		return 2;
	}
	return 1;
}

/* -dx11post [chain]: run a pixel shader over the finished Direct3D 11 frame before it is shown.
	 *
	 * The chain is the effects in the order they run, comma separated: "bloom", "fxaa", "sharpen",
	 * "copy" for the pass that changes nothing, or "off".  Bare -dx11post is "fxaa", which is the
	 * cheapest one worth having: the swap chain asks for a single sample, so an edge in the D3D11
	 * frame has nothing else working on it.  Under -d3d9 there is no such frame and the chain does
	 * nothing.
	 *
	 * "bloom" has to come first and it changes what the scene is kept in.  Explosion particles are
	 * blended additively, so a stack of them is brighter than white before it is written down, and
	 * an eight bit target throws that away: the middle of a fireball is the same white as its edge.
	 * With bloom in the chain the scene goes into half floats, the bright pass thresholds on the
	 * amount by which something beat white, and the frame comes back to eight bits through a tone
	 * curve whose knee leaves everything ordinary exactly where it was.
	 *
	 * Without the switch the chain is "bloom,fxaa,sharpen", set in GlobalData, since v1.0.0.  The
	 * rest of the backend exists to draw the frame Direct3D 9 draws and this exists to draw a
	 * different one, so dx11-check.ps1 and tree-check.ps1 pass "-dx11post off". */
Int parseDirect3D11Post(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		if (num > 1 && args[1][0] != '-')
		{
			TheWritableGlobalData->m_direct3D11PostChain = args[1];
			return 2;
		}
		TheWritableGlobalData->m_direct3D11PostChain = "fxaa";
	}
	return 1;
}

/* -camera <x> <y>: point the camera at one map position and leave it there.
	 *
	 * -screenshot only made a picture; it could not say of what.  The camera starts at the local
	 * player's own base, so anything the player does not own - a river, a bridge, a piece of terrain
	 * a shader change is about - cannot be got into the frame from a script at all, and -autocamera
	 * follows the fighting, which is somewhere else again.  Map coordinates, the same ones the log
	 * prints; the ground height is looked up. */
Int parseCameraLook(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2 && args[1] && args[2])
	{
		TheWritableGlobalData->m_cameraLook.x = (Real)atof(args[1]);
		TheWritableGlobalData->m_cameraLook.y = (Real)atof(args[2]);
		TheWritableGlobalData->m_cameraLookSet = TRUE;
		return 3;
	}
	return 1;
}

Int parseAutoCamera(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		Int seconds = 5;
		Int consumed = 1;
		// The value is optional, so only take the next word if it is actually a number.
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			seconds = atoi(args[1]);
			consumed = 2;
		}
		if (seconds < 1)
			seconds = 1;
		TheWritableGlobalData->m_autoCameraSeconds = seconds;
		return consumed;
	}
	return 1;
}

/* -tracemove [id]: one line a frame, for one unit, naming every value that can zero its speed.
	 *
	 * A jam is an argument between four numbers - the speed the unit wants, the ceiling a collision
	 * put on it, the decaying bump limit and the frames it has spent blocked - and none of them can
	 * be seen from outside the object.  The aggregate counters say a run had 40000 blocked frames;
	 * they cannot say which line of code stopped the tank.  With no id the trace attaches itself to
	 * the first unit that gets blocked and follows that one for the rest of the run, which is what
	 * you want when the jam is somewhere in a batch and nobody knows any object's id in advance. */
Int parseTraceMove(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		Int id = -1; // no number: follow the first unit that gets blocked
		Int consumed = 1;
		// The value is optional, so only take the next word if it is actually a number.
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			id = atoi(args[1]);
			consumed = 2;
		}
		if (id == 0)
			id = -1; // 0 is not a valid object id, and it is how the feature is switched off
		TheWritableGlobalData->m_traceMoveID = id;
		return consumed;
	}
	return 1;
}

/* -showlanes draws the band model on top of the world.

	 A movement change that measures well in a batch and cannot be seen in a game is a change nobody
	 has any reason to believe, and two different failures - a lane that was never handed out, and a
	 lane that was handed out and then refused - look exactly alike from the camera. So the overlay
	 draws both halves separately: what the ordering group asked for, and what the unit ended up
	 steering at. Release, because the machine that has the complaint is the one running Release. */
Int parseShowLanes(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_showLanes = TRUE;
	return 1;
}

/* -uidrill <n> works the command bar the way a player does, from a script.

	 The bar is laid out again every time its scheme is set, and a panel that was minimised at that
	 moment used to come back a little higher than it went down - a few pixels a rebuild, until the
	 build tooltip anchored to it was off the top of the screen. Nothing in an unattended run ever
	 presses the minimise button or changes the player's scheme, so the drift could only be found by
	 hand. This does both every n frames and logs where the bar actually landed, so a long run turns
	 the bug into a column of numbers that either holds still or climbs. */
Int parseUIDrill(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		Int frames = 120;
		Int eaten = 1;
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			frames = atoi(args[1]);
			eaten = 2;
		}
		if (frames < 2)
			frames = 2;		// the bar slides; give it a frame to arrive before measuring it again
		TheWritableGlobalData->m_uiDrill = frames;
		return eaten;
	}
	return 1;
}

/* -resdrill <frame> [w] [h] changes the resolution from inside a running match.

	 That is the one path the options menu has that no switch could reach: the device is reset, the
	 shell is thrown away and rebuilt, and the command bar is built again while the match it belongs
	 to keeps running. It crashed, and reproducing it meant a person in the options menu of a live
	 game. With no width given it takes the next mode the device offers that is not the one already
	 on screen, so the drill needs to know nothing about the monitor it runs on. */
Int parseResDrill(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		Int frame = 600;
		Int eaten = 1;
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			frame = atoi(args[1]);
			eaten = 2;
			if (num > 3 && args[2] && args[3] && args[2][0] >= '0' && args[2][0] <= '9' && args[3][0] >= '0' && args[3][0] <= '9')
			{
				TheWritableGlobalData->m_resDrillX = atoi(args[2]);
				TheWritableGlobalData->m_resDrillY = atoi(args[3]);
				eaten = 4;
			}
		}
		if (frame < 1)
			frame = 1;
		TheWritableGlobalData->m_resDrillFrame = frame;
		return eaten;
	}
	return 1;
}

/* -resdrillkeep answers the "keep this resolution?" box with Ok instead of Cancel.

	 The two answers are different code: Cancel is DeclineResolution, which puts the old mode back
	 and rebuilds the shell and the command bar a second time, while Ok keeps the new mode and
	 rebuilds nothing. A player who wanted the resolution he asked for presses Ok, so that branch
	 needs a drill of its own. */
Int parseResDrillKeep(char *args[], int)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_resDrillKeep = TRUE;
	}
	return 1;
}

/* -control [port] opens a WebSocket on 127.0.0.1 and lets something outside the game drive it.

	 The scenario file answers "play this match the same way twice". This answers the other half:
	 poke a running game and ask it what happened. Start a match, spawn a unit, send a selection
	 somewhere, take a picture, read the frame number and everybody's money back - from Python, from
	 a browser console, from anything that speaks WebSocket.

	 Loopback only, and deliberately so: the socket can create units, so it has to be unreachable
	 from another machine, and binding to 127.0.0.1 is what makes that true rather than a promise.
	 There is no authentication and none would help; anything that can reach the port can already
	 run programs on this machine. */
Int parseControlPort(char *args[], int num)
{
	const Int CONTROL_DEFAULT_PORT = 8787;
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_controlPort = CONTROL_DEFAULT_PORT;
		if (num > 1 && args[1] && args[1][0] >= '0' && args[1][0] <= '9')
		{
			const Int port = atoi(args[1]);
			if (port > 0 && port < 65536)
				TheWritableGlobalData->m_controlPort = port;
			return 2;
		}
	}
	return 1;
}

/* -nodevice runs the whole game without a Direct3D device.

	 -headless already draws nothing, but it still takes a 100x100 windowed device, and D3D9 answers
	 CreateDevice with D3DERR_DEVICELOST while the workstation is locked. That puts every unattended
	 run behind somebody being logged in with the screen awake, which is the one thing an unattended
	 run should not need. With this on, the device is never asked for: WW3D2 hands out plain memory
	 where it would have handed out a vertex or index buffer, no texture is loaded, and the render
	 systems that exist only to draw are not brought up at all.

	 Still a work in progress - a match started this way does not survive map load yet - so it is a
	 separate switch rather than something -headless does on its own. */
Int parseNoDevice(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_noRenderDevice = TRUE;
	}
	return 1;
}

/* -scenario <name> plays Run/Scenarios/<name>.txt: spawn this unit here, send it there, on that
	 frame.

	 A measurement needs the same army doing the same thing twice, and -autoskirmish cannot give it.
	 Every slot goes in as PLAYERTEMPLATE_RANDOM and the faction falls out of the seed, so "enough
	 angry mobs on the field to see what they cost" is something a batch waits for rather than
	 something it asks for - and two runs that drew different factions were never comparable. The
	 file names the unit, the place, the count and the frame, so two builds play the same match.

	 The orders it gives exist nowhere in the command stream, so a run driven this
	 way cannot be replayed. Repeatability comes from the file plus -seed instead, which is what an
	 A/B wanted anyway. */
Int parseScenario(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		TheWritableGlobalData->m_scenarioFile = args[1];
		return 2;
	}
	return 1;
}

/* -cinema <name>: take the interface off and fly the camera from Run/Cinema/<name>.txt, for
	 footage. A name with no such file still takes the interface off and leaves the camera to the
	 player. CinemaDirector.cpp has the shot list's grammar. */
Int parseCinema(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		TheWritableGlobalData->m_cinemaScript = args[1];
		return 2;
	}
	return 1;
}

/* -freecam <x> <y> <z> <heading> <tilt> puts the match into the console's freecam at that pose once
	 it is up, the way typing 'freecam x y z heading tilt' would.  A screenshot script cannot fly the
	 camera with the mouse, so this is how a photo-mode picture is taken unattended.  Angles are in
	 degrees, heading 0 along +x, tilt 0 level and negative looking down. */
Int parseFreeCamera(char *args[], int num)
{
	if (num > 5)
	{
		AsciiString pose;
		pose.format("%s %s %s %s %s", args[1], args[2], args[3], args[4], args[5]);
		GameConsole_setStartupFreeCamera(pose.str());
		return 6;
	}
	return 1;
}

/* -side <slot> <faction> nails one -autoskirmish slot to a faction instead of letting the seed
	 pick it.

	 populateRandomSideAndColor only draws for a slot that still reads PLAYERTEMPLATE_RANDOM, so a
	 real template index written into the slot survives untouched and nothing downstream changes.
	 The name is resolved late, in startAutoSkirmish, because PlayerTemplateStore is an INI subsystem
	 and none of it exists yet while the command line is being read. The names are the templates'
	 own: FactionGLA, FactionAmerica, FactionChina and the generals. */
Int parseSide(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2 && args[1] && args[2])
	{
		const Int slot = atoi(args[1]);
		if (slot >= 0 && slot < MAX_SLOTS)
			TheWritableGlobalData->m_autoSkirmishSide[slot] = args[2];
		else
			DEBUG_LOG(("-side: slot %d is outside 0..%d\n", slot, MAX_SLOTS - 1));
		return 3;
	}
	return 1;
}

/* -team <slot> <n> puts one -autoskirmish slot on team n, -1 for none, over whatever -teams gave
	 it.  -teams only splits evenly into blocks; this stages any layout, 4v1, 2v1 or three players
	 alone beside a pair (-team 3 0 -team 4 0 with the rest -1). */
Int parseTeam(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2 && args[1] && args[2])
	{
		const Int slot = atoi(args[1]);
		if (slot >= 0 && slot < MAX_SLOTS)
			TheWritableGlobalData->m_autoSkirmishTeam[slot] = max(atoi(args[2]), -1);
		else
			DEBUG_LOG(("-team: slot %d is outside 0..%d\n", slot, MAX_SLOTS - 1));
		return 3;
	}
	return 1;
}

/* -seatname <slot> <name> calls one -autoskirmish seat by a name instead of its difficulty, the way a
	 player's own name stands on his seat: a cup's bots by their entrants' names, or a long one to see
	 the director's score bar cut it.  ASCII only; it is read before the game text is. */
Int parseSeatName(char *args[], int num)
{
	if (TheWritableGlobalData && num > 2 && args[1] && args[2])
	{
		const Int slot = atoi(args[1]);
		if (slot >= 0 && slot < MAX_SLOTS)
			TheWritableGlobalData->m_autoSkirmishSeatName[slot] = args[2];
		else
			DEBUG_LOG(("-seatname: slot %d is outside 0..%d\n", slot, MAX_SLOTS - 1));
		return 3;
	}
	return 1;
}

/* -takeover empties every -autoskirmish seat instead of filling it with an AI.

	 SLOT_TAKEOVER is an occupied seat with nothing behind it: startNewGame writes playerIsHuman for
	 it, so Player::setPlayerType never news an AIPlayer and that player sits waiting to be told what
	 to do. For a measurement that is exactly the point. An AI that builds, expands and attacks costs
	 more of the frame than whatever is under test, and it costs a different amount every run; with
	 the seats empty, nothing happens at all unless -scenario says it does.

	 -takeover <slot> empties that one seat and leaves the AI in the others: a scenario then plays one
	 side by hand against a computer that plays the whole game, which is how an AI is asked what it
	 does about a given army. */
Int parseTakeover(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_autoSkirmishTakeover = TRUE;
		if (num > 1 && args[1][0] >= '0' && args[1][0] <= '9')
		{
			TheWritableGlobalData->m_autoSkirmishTakeoverSlot = atoi(args[1]);
			return 2;
		}
	}
	return 1;
}

/* -teams <n> splits an -autoskirmish lobby into n allied teams instead of a free-for-all.

	 Free-for-all and 4v4 are not the same load and not the same game. Eight players each fighting
	 seven others spread the fighting over the whole map; two sides of four put every unit on one of
	 two fronts, which is where units bunch up, where the pathfinder earns its money, and where a
	 player says the game is chugging. Slots are handed out in blocks - the first n-th of them are
	 team 0, the next team 1 - which is how the lobby numbers them, and GameLogic's own alliance
	 setup does the rest from each slot's team number. */
Int parseTeams(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		Int teams = atoi(args[1]);
		if (teams < 1)
			teams = 1;								// one team is a free-for-all, same as not asking
		if (teams > MAX_SLOTS)
			teams = MAX_SLOTS;
		TheWritableGlobalData->m_autoSkirmishTeams = teams;
		return 2;
	}
	return 1;
}

/* -peacetime <minutes> gives an -autoskirmish match the lobby's peace time without a lobby.

	 The option itself is a host setting picked from a combo box in the three lobby screens, and none
	 of those exist in an unattended run - so the only way to watch what the truce does to a match, or
	 to prove a change to it did not desync a replay, is to hand the same number to the slot list the
	 command line builds. Clamped the same way GameInfo::setPeaceTime clamps the wire value. */
Int parsePeaceTime(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		Int minutes = atoi(args[1]);
		if (minutes < 0)
			minutes = 0;
		if (minutes > 60)
			minutes = 60;
		TheWritableGlobalData->m_peaceTime = minutes;
		return 2;
	}
	return 1;
}

/* -unitlimit: turn the lobby's unit limit on for an -autoskirmish run.  The check box lives in the
	 lobby settings page and an unattended run has no lobby, so a measurement of what the cap does to
	 a match, or a replay check of it, needs this.  Cleared for a network game, like -peacetime. */
Int parseUnitLimit(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_unitLimit = TRUE;
	}
	return 1;
}

/* -incomesharing <n>: the lobby's income sharing for an -autoskirmish run, 1 for the oil derricks
	 and 2 for every steady income.  It goes into the slot list the command line builds, so the replay
	 carries it; a network game has no such slot list and reads the host's options string. */
Int parseIncomeSharing(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		TheWritableGlobalData->m_incomeSharing = atoi(args[1]);
		return 2;
	}
	return 1;
}

/* -superweapons <n>: the lobby's superweapon rule for an -autoskirmish run, 1 Limit and 2 No,
	 carried the same way as -incomesharing. */
Int parseSuperweapons(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		TheWritableGlobalData->m_superweapons = atoi(args[1]);
		return 2;
	}
	return 1;
}

/* -techrespawn <minutes>: the lobby's tech building respawn for an -autoskirmish run, carried the
	 same way as -incomesharing. */
Int parseTechRespawn(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		TheWritableGlobalData->m_techRespawn = atoi(args[1]);
		return 2;
	}
	return 1;
}

/* -startingcash <n>: the lobby's starting cash for an -autoskirmish run, any amount rather than the
	 lobby's list, carried the same way as -incomesharing. */
Int parseStartingCash(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		TheWritableGlobalData->m_startingCash = atoi(args[1]);
		return 2;
	}
	return 1;
}

/* -supplypilelimit <players>: the lobby's supply pile limit for an -autoskirmish run, carried the
	 same way as -incomesharing. */
Int parseSupplyPileLimit(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		TheWritableGlobalData->m_supplyPileLimit = atoi(args[1]);
		return 2;
	}
	return 1;
}

/* -slowframe <ms> lowers the bar a logic frame has to clear before it logs its own breakdown.

	 The default of 20ms is a stutter hunt: it catches the frames a player would notice. Chasing a
	 subsystem's cost is a different search - the question is not "which frames were terrible" but
	 "which frames did this cost anything at all" - and for that the bar wants to be a few
	 milliseconds. Every frame over it writes a line and a flush, so a low bar on a long run is a
	 large log and a slower run; it is a measuring tool, not a setting. */
Int parseSlowFrame(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		const Real ms = (Real)atof(args[1]);
		if (ms > 0.0f)
			TheWritableGlobalData->m_slowFrameMS = ms;
		return 2;
	}
	return 1;
}

/* -drawdelay <ms> sleeps that long in every client pass, which is what a weak graphics card looks
	 like to the engine: the picture takes longer while the logic frame costs what it always did.
	 A network game paces itself on the slowest machine in the room, so the question "does one slow
	 renderer slow everybody" needs a slow renderer on this machine, next to a fast one.

	 An optional second number adds up to that much more, different on every pass, because a real
	 card does not take the same time twice and a network catch-up that runs a varying number of
	 logic frames per picture is the case worth testing. */
Int parseDrawDelay(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1 && args[1])
	{
		const Int ms = atoi(args[1]);
		if (ms > 0)
			TheWritableGlobalData->m_drawDelayMS = ms;
		if (num > 2 && args[2] && isdigit((unsigned char)args[2][0]))
		{
			TheWritableGlobalData->m_drawDelayJitterMS = atoi(args[2]);
			return 3;
		}
		return 2;
	}
	return 1;
}

/* -netgame <ip>[,<ip>...] starts a LAN game against those addresses with no lobby in front of it,
	 and -netslot <n> says which of them this copy is.  Every machine is given the same slot list in
	 the same order, which is all the lobby ever agreed on: the slot list, the map and the seed.  A
	 network game is the only kind of game whose replay exercises the multiplayer paths (a CRC per
	 NET_CRC_INTERVAL frames, real remote players, a local slot that is not 0), so without this there
	 is no way to produce one unattended. */
Int parseNetGame(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_netGameHosts = args[1];
	}
	return 2;
}

/* -lanip <ip> is the same idea one level up, for the LAN lobby rather than for a game started
	 without one.  The lobby socket is a single UDP port (8086) and nothing sets SO_REUSEADDR, so the
	 first copy on a machine binds it on every address and the second copy's LAN screen comes up with
	 a socket error and an empty game list.  The address here is what LanLobbyMenu binds instead, so
	 two copies want two addresses out of 127.0.0.0/8 - Windows routes that range to loopback in its
	 entirety, and a broadcast sent from one of them is delivered to the others.

	 Options.ini carries the same setting, and both copies read the same file, which is why this is a
	 switch and not a preference: the whole point is that the two copies disagree about it. */
Int parseLanIP(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		UnsignedInt ip = ResolveIP( AsciiString( args[1] ) );
		if (ip != 0 && ip != INADDR_NONE)
			TheWritableGlobalData->m_defaultIP = ip;
	}
	return 2;
}

/* -lanname <name> is the other half of it.  The lobby name comes out of the preferences, both
	 copies read the same preferences, and a host denies a join whose name it already has in a slot
	 (RET_DUPLICATE_NAME) - so without this the second copy is refused until somebody retypes the
	 name by hand every session. */
Int parseLanName(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_lanPlayerName = args[1];
	}
	return 2;
}

/* -lanlobby starts on the LAN screen rather than the main menu.  Two copies on one machine are
   started by a script and the first thing both of them do is the same four clicks; this is those
   clicks.  It turns the shell map off with it: Shell::showShell only puts the main menu on the
   stack when there is no shell map, and the lobby has to go on top of something or backing out of
   it leaves an empty shell. */
Int parseLanLobby(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_lanLobbyOnStart = TRUE;
		TheWritableGlobalData->m_shellMapOn = FALSE;
	}
	return 1;
}

/* -skirmishlobby opens the skirmish staging room, which is the screen the lobby settings live on.
   No switch could reach it before: the room is behind two main menu clicks, so a change to its
   layout could be argued about but not photographed.  With -screenshot it can now be looked at from
   a script.  Same shell map handling as -lanlobby, and for the same reason - the room goes on top
   of the main menu, and the main menu is only pushed when there is no shell map. */
Int parseSkirmishLobby(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_skirmishLobbyOnStart = TRUE;
		TheWritableGlobalData->m_shellMapOn = FALSE;
	}
	return 1;
}

/* -optionsmenu opens the options screen over the main menu at startup, where a click on Options
   would put it.  A change to that screen can then be photographed page by page from a script that
   only has to press the tabs, instead of first finding the main menu's buttons.  Implies
   -noshellmap for the reason -skirmishlobby does. */
Int parseOptionsMenu(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_optionsMenuOnStart = TRUE;
		TheWritableGlobalData->m_shellMapOn = FALSE;
	}
	return 1;
}

/* -randommaps puts the generated maps back in the skirmish map list.  They are off by default
   because the generator is not finished - a seed can still leave a supply dock behind a cliff, and
   a map list is not the place to find that out.  Everything else about them works from the command
   line, where -randommap says exactly which map is being played. */
Int parseRandomMapsInMenus(char *args[], int num)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_randomMapsInMenus = TRUE;

	return 1;
}

Int parseNetSlot(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		Int slot = atoi(args[1]);
		if (slot < 0)
			slot = 0;
		if (slot >= MAX_SLOTS)
			slot = MAX_SLOTS - 1;
		TheWritableGlobalData->m_netGameLocalSlot = slot;
	}
	return 2;
}

/* -netai <n> puts n AI seats after the -netgame addresses.  Two idle copies play nothing, and a
	 desync that players meet ten minutes into a match needs a match that is being played: AIs
	 building, fighting and dying.  An AI runs inside GameLogic on every machine alike, so every copy
	 has to be given the same number, the way every copy is given the same address list. */
Int parseNetAI(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		TheWritableGlobalData->m_netGameAISlots = max( 0, atoi(args[1]) );
	}
	return 2;
}

/* -replay <file> plays a replay back without the menus, the way -autoskirmish starts a match
	 without them.  The only route into playback was ReplayMenu's list box and the _DEBUG/_INTERNAL
	 -file switch, so a Release build could record a game and then had no way to play it back
	 unattended - which is exactly what checking that a multiplayer replay still plays needs.

	 The name is resolved against the replay directory by RecorderClass::readReplayHeader, so pass
	 the bare file name; the extension is optional. */
Int parseReplay(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString name = args[1];
		if (!name.endsWithNoCase(".rep"))
			name.concat(".rep");
		TheWritableGlobalData->m_initialFile = name;
	}
	return 2;
}

/* -loadsave <file> opens a save game without the menus, the same way -replay opens a replay. The
	 only route into a save was the Load menu, so there was no way to get a machine straight back
	 into a known world - which is what a bug that only shows up ten minutes into a mission needs.
	 The name is resolved against the save directory; the extension is optional. */
Int parseLoadSave(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString name = args[1];
		if (!name.endsWithNoCase(".sav"))
			name.concat(".sav");
		TheWritableGlobalData->m_initialFile = name;
	}
	return 2;
}

Int parseDumpAssetUsage(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_dumpAssetUsage = true;
	}
	return 1;
}

Int parseJumpToFrame(char *args[], int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		parseNoFPSLimit(args, num);
		TheWritableGlobalData->m_noDraw = atoi(args[1]);
		return 2;
	}
	return 1;
}

Int parseUpdateImages(char *args[], int num)
{
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_shouldUpdateTGAToDDS = TRUE;
	}
	return 1;
}

Int parseMod(char *args[], Int num)
{
	if (TheWritableGlobalData && num > 1)
	{
		AsciiString modPath = args[1];
		if (strchr(modPath.str(), ':') || modPath.startsWith("/") || modPath.startsWith("\\"))
		{
			// full path passed in.  Don't append base path.
		}
		else
		{
			modPath.format("%s%s", TheGlobalData->getPath_UserData().str(), args[1]);
		}
		DEBUG_LOG(("Looking for mod '%s'\n", modPath.str()));

		if (!TheLocalFileSystem->doesFileExist(modPath.str()))
		{
			DEBUG_LOG(("Mod does not exist.\n"));
			return 2; // no such file/dir.
		}

		// now check for dir-ness
		struct stat statBuf;
		if (zh_stat(modPath.str(), &statBuf) != 0)
		{
			DEBUG_LOG(("Could not _stat() mod.\n"));
			return 2; // could not stat the file/dir.
		}

		if (statBuf.st_mode & S_IFDIR)
		{
			if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
				modPath.concat('\\');
			DEBUG_LOG(("Mod dir is '%s'.\n", modPath.str()));
			TheWritableGlobalData->m_modDir = modPath;
		}
		else
		{
			DEBUG_LOG(("Mod file is '%s'.\n", modPath.str()));
			TheWritableGlobalData->m_modBIG = modPath;
		}

		return 2;
	}
	return 1;
}

static CommandLineParam params[] =
{
	{ "-noshellmap", parseNoShellMap },
	{ "-win", parseWin },
	{ "-borderless", parseBorderless },
	{ "-xres", parseXRes },
	{ "-yres", parseYRes },
	{ "-fullscreen", parseNoWin },
	{ "-fullVersion", parseFullVersion },
	{	"-particleEdit", parseParticleEdit },
	{ "-scriptDebug", parseScriptDebug },
	{ "-playStats", parsePlayStats },
	{ "-mod", parseMod },
	{ "-noshaders", parseNoShaders },
	{ "-particlebounce", parseParticleBounce },
	{ "-smoke", parseSmoke },
	{ "-particlecap", parseParticleCap },
	{ "-noDynamicLOD", parseNoDynamicLOD },
	{ "-nochroma", parseNoChroma },
	{ "-shadowmapreport", parseShadowMapReport },
	{ "-shadowmapboth", parseShadowMapBoth },
	{ "-noparticleshadows", parseNoParticleShadows },
	{ "-novolumetricsmoke", parseNoVolumetricSmoke },
	{ "-nosmokefirelight", parseNoSmokeFireLight },
	{ "-quickstart", parseQuickStart },
	/* In every build: EA kept them to Debug and Internal, so a Release run could not skip the logo or the
		 movies. */
	{ "-nologo", parseNoLogo },
	{ "-novideo", parseNoVideo },

	{ "-packetloss", parsePacketLoss },
	{ "-latAvg", parseLatencyAverage },
	{ "-latAmp", parseLatencyAmplitude },
	{ "-latPeriod", parseLatencyPeriod },
	{ "-latNoise", parseLatencyNoise },
	{ "-DebugCRCFromFrame", parseDebugCRCFromFrame },
	{ "-DebugCRCUntilFrame", parseDebugCRCUntilFrame },
	{ "-KeepCRCSaves", parseKeepCRCSave },
	{ "-CRCLogicModuleData", parseCRCLogicModuleData },
	{ "-CRCClientModuleData", parseCRCClientModuleData },
	{ "-ClientDeepCRC", parseClientDeepCRC },
	{ "-VerifyClientCRC", parseVerifyClientCRC },
	{ "-LogObjectCRCs", parseLogObjectCRCs },
	{ "-NetCRCInterval", parseNetCRCInterval },
	{ "-ReplayCRCInterval", parseReplayCRCInterval },

#if (defined(_DEBUG) || defined(_INTERNAL))
	{ "-nomusic", parseNoMusic },
	{ "-noLogOrCrash", parseNoLogOrCrash },
	{ "-FPUPreserve", parseFPUPreserve },
	{ "-benchmark", parseBenchmark },
#ifdef DUMP_PERF_STATS
	{ "-stats", parseStats }, 
#endif
  { "-saveStats", parseSaveStats },
	{ "-localMOTD", parseLocalMOTD },
	{ "-UseCSF", parseUseCSF },
	{ "-NoInputDisable", parseNoInputDisable },
	{ "-saveAllStats", parseSaveAllStats },
	{ "-noDraw", parseNoDraw },
	{ "-nomilcap", parseNoMilCap },
	{ "-nofade", parseNoFade },
	{ "-nomovecamera", parseNoMoveCamera },
	{ "-nocinematic", parseNoCinematic },
	{ "-noViewLimit", parseNoViewLimit },
	{ "-lowDetail", parseLowDetail },
	{ "-noStaticLOD", parseNoStaticLOD },
	{ "-useWaveEditor", parseUseWaveEditor },
	{ "-wireframe", parseWireframe },
	{ "-showCollision", parseShowCollision },
	{ "-noShowClientPhysics", parseNoShowClientPhysics },
	{ "-showTerrainNormals", parseShowTerrainNormals },
	{ "-stateMachineDebug", parseStateMachineDebug },
	{ "-jabber", parseJabber },
	{ "-munkee", parseMunkee },
	{ "-displayDebug", parseDisplayDebug },
	{ "-file", parseFile },
  
  { "-preloadEverything", parsePreloadEverything },
	{ "-logAssets", parseLogAssets },
	{ "-netMinPlayers", parseNetMinPlayers },
	{ "-DemoLoadScreen", parseDemoLoadScreen },
	{ "-cameraDebug", parseCameraDebug },
	{ "-ignoreAsserts", parseIgnoreAsserts },
	{ "-ignoreStackTrace", parseIgnoreStackTrace },
	{ "-logToCon", parseLogToConsole },
	{ "-vTune", parseVTune },
	{ "-selectTheUnselectable", parseSelectAll },
	{ "-RunAhead", parseRunAhead },
	{ "-noshroud", parseNoShroud },
	{ "-forceBenchmark", parseForceBenchmark },
	{ "-buildmapcache", parseBuildMapCache },
	{ "-noshadowvolumes", parseNoShadows },
	{ "-nofx", parseNoFX },
	{ "-ignoresync", parseSync },
	{ "-shellmap", parseShellMap },
	{ "-noShellAnim", parseNoWindowAnimation },
	{ "-winCursors", parseWinCursors },
	{ "-constantDebug", parseConstantDebug },
	{ "-noagpfix", parseIncrAGPBuf },
	{ "-dumpAssetUsage", parseDumpAssetUsage },
	{ "-jumpToFrame", parseJumpToFrame },
	{ "-updateImages", parseUpdateImages },
	{ "-showTeamDot", parseShowTeamDot },
	{ "-extraLogging", parseExtraLogging },

#endif

	/* Outside the _DEBUG/_INTERNAL block on purpose: an unattended Release run is driven entirely
		 from the command line.  -autoskirmish needs a map to play, -seed makes the run repeatable,
		 -noFPSLimit lets the renderer run free, and -fps is the one knob that changes how fast the
		 match itself is simulated - the command-line twin of the skirmish menu's game speed slider. */
	{ "-map", parseMapName },
	{ "-randommap", parseRandomMap },
	{ "-randommaps", parseRandomMapsInMenus },
	{ "-seed", parseSeed },
	{ "-noFPSLimit", parseNoFPSLimit },
	{ "-fps", parseFPSLimit },
	{ "-autoskirmish", parseAutoSkirmish },
	{ "-aidiff", parseAIDifficulty },
	{ "-aidiff2", parseAIDifficulty2 },
	{ "-notactics", parseNoTactics },
	{ "-aiknobsoff", parseAIKnobsOff },
	{ "-observer", parseObserver },
	{ "-headless", parseHeadless },
	/* -noaudio was in the Debug/Internal block above, so a Release build ignored it and a windowed run
		 opened the audio device.  It turns every sound off as -headless does: the device is never opened. */
	{ "-noaudio", parseNoAudio },
	{ "-maxframes", parseMaxGameFrames },
	{ "-screenshot", parseScreenShot },
	{ "-showHudOverlay", parseShowHudOverlay },
	{ "-video", parseVideo },
	{ "-directorrecord", parseDirectorRecord },
	{ "-directorscout", parseDirectorScout },
	{ "-directortimeline", parseDirectorTimeline },
	{ "-wav", parseWav },
	{ "-turbo", parseTurbo },
	{ "-msaa", parseMSAA },
	{ "-d3d9", parseDirect3D9 },
	{ "-language", parseTextLanguage },
	{ "-interface", parseInterfaceStyle },
	{ "-dx11dump", parseDirect3D11Dump },
	{ "-dx11post", parseDirect3D11Post },
	{ "-autocamera", parseAutoCamera },
	{ "-camera", parseCameraLook },
	{ "-tracemove", parseTraceMove },
	{ "-slowframe", parseSlowFrame },
	{ "-drawdelay", parseDrawDelay },
	{ "-teams", parseTeams },
	{ "-peacetime", parsePeaceTime },
	{ "-unitlimit", parseUnitLimit },
	{ "-incomesharing", parseIncomeSharing },
	{ "-techrespawn", parseTechRespawn },
	{ "-superweapons", parseSuperweapons },
	{ "-supplypilelimit", parseSupplyPileLimit },
	{ "-startingcash", parseStartingCash },
	{ "-showlanes", parseShowLanes },
	{ "-uidrill", parseUIDrill },
	{ "-resdrill", parseResDrill },
	{ "-resdrillkeep", parseResDrillKeep },
	{ "-control", parseControlPort },
	{ "-nodevice", parseNoDevice },
	{ "-scenario", parseScenario },
	{ "-cinema", parseCinema },
	{ "-freecam", parseFreeCamera },
	{ "-side", parseSide },
	{ "-team", parseTeam },
	{ "-seatname", parseSeatName },
	{ "-takeover", parseTakeover },
	{ "-replay", parseReplay },
	{ "-loadsave", parseLoadSave },
	{ "-netgame", parseNetGame },
	{ "-netslot", parseNetSlot },
	{ "-netai", parseNetAI },
	{ "-lanip", parseLanIP },
	{ "-lanname", parseLanName },
	{ "-lanlobby", parseLanLobby },
	{ "-skirmishlobby", parseSkirmishLobby },
	{ "-optionsmenu", parseOptionsMenu },

	//-allAdvice feature
	//{ "-allAdvice", parseAllAdvice },



};

// parseCommandLine ===========================================================
/** Parse command-line parameters. */
//=============================================================================
void parseCommandLine(int argc, char *argv[])
{
	// To parse command-line parameters, we loop through a table holding arguments
	// and functions to handle them.  Comparisons can be case-(in)sensitive, and
	// can check the entire string (for testing the presence of a flag) or check
	// just the start (for a key=val argument).  The handling function can also
	// look at the next argument(s), to accomodate multi-arg parameters, e.g. "-p 1234".
	int arg=1, param;
	Bool found;

#ifdef DEBUG_LOGGING
	DEBUG_LOG(("Command-line args:"));
	int debugFlags = DebugGetFlags();
	DebugSetFlags(debugFlags & ~DEBUG_FLAG_PREPEND_TIME); // turn off timestamps
	for (arg=1; arg<argc; arg++)
	{
		DEBUG_LOG((" %s", argv[arg]));
	}
	DEBUG_LOG(("\n"));
	DebugSetFlags(debugFlags); // turn timestamps back on iff they were on before
	arg = 1;
#endif // DEBUG_LOGGING

	while (arg<argc)
	{
		// Look at arg #i
		found = false;
		for (param=0; !found && param<sizeof(params)/sizeof(params[0]); ++param)
		{
			int len = strlen(params[param].name);
			int len2 = strlen(argv[arg]);
			if (len2 != len)
				continue;
			if (!strncasecmp(argv[arg], params[param].name, len))
			{
				arg += params[param].func(argv+arg, argc-arg);
				found = true;
			}
		}	// for
		if (!found)
		{
			arg++;
		}
	}

	TheArchiveFileSystem->loadMods();
}


