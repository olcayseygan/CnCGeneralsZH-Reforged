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

// FILE: W3DDisplay.cpp ///////////////////////////////////////////////////////
//
// W3D Implementation for the Game Display which is responsible for creating
// and maintaning the entire visual display
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

static void drawFramerateBar(void);

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>
#include <algorithm>
#include "Lib/Clock.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(_WIN32)
#include <io.h>
#endif
#include <time.h>
#include "stringex.h"

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Common/ThingFactory.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/Monitors.h"
#include "Common/OptionsCatalog.h"
#include "dx8wrapper.h"
#include "ffprobe.h"
#include "ffshadercache.h"
#include "dx11runtime.h"
#include "Common/PerfTimer.h"
#include "Common/JobSystem.h"
#include "Common/FileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/GameLOD.h"
#include "Common/DrawModule.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Module/PhysicsUpdate.h"

#include "GameClient/Drawable.h"
#include "W3DDevice/GameClient/W3DSmoothMotion.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "GameLogic/Object.h"
#include "GameClient/Keyboard.h"		// TheKeyboard; on Windows WinMain.h brought it too
#include "Platform/SleepMilliseconds.h"
#if !defined(_WIN32)
#include "Platform/RendererName.h"
#endif
#include "GameClient/GameText.h"
#include "GameClient/GameConsole.h"
#include "GameClient/GraphDraw.h"
#include "GameClient/Line2D.h"
#include "GameClient/Mouse.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Water.h"

#include "GameNetwork/NetworkInterface.h"
#include "Common/ModelState.h"
#include "Lib/BaseType.h"
#include "W3DDevice/Common/W3DConvert.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/GameClient/W3DFileSystem.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DVideobuffer.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDebugDisplay.h"
#include "W3DDevice/GameClient/W3DDisplayString.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WWMath/wwmath.h"
#if defined(_WIN32)
#include "WWLib/registry.h"
#endif
#include "WW3D2/ww3d.h"
#include "WW3D2/predlod.h"
#include "WW3D2/part_emt.h"
#include "WW3D2/part_ldr.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/ww3dformat.h"
#include "WW3D2/agg_def.h"
#include "WW3D2/render2dsentence.h"
#include "WW3D2/sortingrenderer.h"
#include "WW3D2/textureloader.h"
#if defined(_WIN32)
#include "WW3D2/dx8webbrowser.h"
#endif
#include "WW3D2/mesh.h"
#include "WW3D2/hlod.h"
#include "WW3D2/meshmatdesc.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/rddesc.h"
#include "TARGA.H"
#include "Lib/BaseType.h"

#include "GameLogic/ScriptEngine.h"		// For TheScriptEngine - jkmcd
#include "GameLogic/GameLogic.h"
#ifdef DUMP_PERF_STATS
#include "GameLogic/PartitionManager.h"
#endif

#if defined(_WIN32)
#include "WinMain.h"
#else
// The window the device draws into: C2's, and null under -headless.  On Windows, WinMain's HWND, which
// RenderWindow is there.
extern RenderWindow ApplicationHWnd;
// What applyWindowFrame and sizeWindowToClient do to the window on Windows, the window's owner does here.
#include "W3DDevice/GameClient/W3DWindowHooks.h"
W3DWindowFrameHook TheW3DWindowFrameHook = NULL;
W3DWindowSizeHook TheW3DWindowSizeHook = NULL;
#endif
#include "zhio.h"		// zh_fopen, zh_remove, zh_mkdir: the engine's Windows-spelled paths (C1)

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// DEFINE AND ENUMS ///////////////////////////////////////////////////////////
#define W3D_DISPLAY_DEFAULT_BIT_DEPTH 32

#define no_SAMPLE_DYNAMIC_LIGHT	1
#ifdef SAMPLE_DYNAMIC_LIGHT
static W3DDynamicLight * theDynamicLight = NULL;
static Real theLightXOffset = 0.1f;
static Real theLightYOffset = 0.07f;
static Int theFlashCount = 0;
#endif

//*****************************************************************************************
//*****************************************************************************************
//**** Start Statistical Dump *************************************************************
//*****************************************************************************************

#ifdef DUMP_PERF_STATS

#include <cstdarg>

class StatDumpClass
{
public:
	StatDumpClass( const char *fname );
	~StatDumpClass();
	void dumpStats( Bool brief = FALSE, Bool flagSpikes = FALSE );

protected:
	FILE *m_fp;
};

//=============================================================================
//Open the file once at the beginning of the game -- everything appends to it.
//=============================================================================
StatDumpClass::StatDumpClass( const char *fname )
{
	char buffer[ _MAX_PATH ];
	GetModuleFileName( NULL, buffer, sizeof( buffer ) );
	char *pEnd = buffer + strlen( buffer );
	while( pEnd != buffer )
	{
		if( *pEnd == '\\' )
		{
			*pEnd = 0;
			break;
		}
		pEnd--;
	}
	AsciiString fullPath;
	fullPath.format( "%s\\%s", buffer, fname );
	m_fp = fopen( fullPath.str(), "wt" );
}

//=============================================================================
//Close the file at the end of the application 
//=============================================================================
StatDumpClass::~StatDumpClass()
{
	if( m_fp )
	{
		fclose( m_fp );
	}
}

static const char *getCurrentTimeString(void)
{
	time_t aclock;
	time(&aclock);
	struct tm *newtime = localtime(&aclock);
	return asctime(newtime);
}

//=============================================================================
//Dump the stats
//=============================================================================


static Bool s_notFirstDump = FALSE;

void StatDumpClass::dumpStats( Bool brief, Bool flagSpikes )
{
	if( !m_fp )
	{
		return;
	}

  
  Bool beBrief = brief & s_notFirstDump;
  s_notFirstDump = TRUE;

	fprintf( m_fp, "----------------------------------------------------------------\n" );
	fprintf( m_fp, "Performance Statistical Dump -- Frame %d\n", TheGameLogic->getFrame() );
  if ( ! beBrief )
  {
	  //static char buf[1024];
	  fprintf( m_fp, "Time:\t%s", getCurrentTimeString() );
	  fprintf( m_fp, "Map:\t%s\n", TheGlobalData->m_mapName.str());
	  fprintf( m_fp, "Side:\t%s\n", ThePlayerList->getLocalPlayer()->getSide().str());
	  fprintf( m_fp, "----------------------------------------------------------------\n" );
  }

	//FPS
	Real fps = TheDisplay->getAverageFPS();
	fprintf( m_fp, "Average FPS: %.1f (%.5f msec)\n", fps, 1000.0f / fps );
  if ( flagSpikes && fps<20.0f )
  	fprintf( m_fp, "                                                                      FPS OUT OF TOLERANCE\n" );


	//Rendering stats
	fprintf( m_fp, "Draws: %d \nSkins: %d \nSortedPolys: %d \nSkinPolys: %d\n",(Int)Debug_Statistics::Get_Draw_Calls(),
		(Int)Debug_Statistics::Get_DX8_Skin_Renders(),
		(Int)Debug_Statistics::Get_Sorting_Polygons(), (Int)Debug_Statistics::Get_DX8_Skin_Polygons());

	Int onScreenParticleCount = TheParticleSystemManager->getOnScreenParticleCount();

  if ( flagSpikes )
  {
    if ( Debug_Statistics::Get_Draw_Calls()>2000 )
  	  fprintf( m_fp, "                                                                      DRAWS OUT OF TOLERANCE(2000)\n" );
    if ( Debug_Statistics::Get_Sorting_Polygons() > (onScreenParticleCount*2) + 300 )
  	  fprintf( m_fp, "                                                                      NON-PARTICLE-SORTS OUT OF TOLERANCE(300)\n" );
    if ( Debug_Statistics::Get_DX8_Skin_Renders()>100 )
  	  fprintf( m_fp, "                                                                      SKINS OUT OF TOLERANCE(100)\n" );
  }


	//Object stats
	UnsignedInt objCount = TheGameLogic->getObjectCount();
	UnsignedInt objScreenCount = TheGameClient->getRenderedObjectCount();
	fprintf( m_fp, "Objects: %d in world (%d onscreen)\n", objCount, objScreenCount );
  if ( flagSpikes && objCount > 800 )
  	fprintf( m_fp, "                                                                      OBJS OUT OF TOLERANCE(800)\n" );

	//AI stats
	UnsignedInt numAI, numMoving, numAttacking, numWaitingForPath, overallFailedPathfinds;
	TheGameLogic->getAIMetricsStatistics( &numAI, &numMoving, &numAttacking, &numWaitingForPath, &overallFailedPathfinds );
	fprintf( m_fp, "\n" );
	fprintf( m_fp, "AI Statistics:\n" );
	fprintf( m_fp, "  Total AI Objects: %d\n", numAI );
	fprintf( m_fp, "    -moving: %d\n", numMoving );
	fprintf( m_fp, "    -attacking: %d\n", numAttacking );
	fprintf( m_fp, "    -waiting for path: %d\n", numWaitingForPath );
	fprintf( m_fp, "  Total failed pathfinds: %d\n", overallFailedPathfinds );
  if ( flagSpikes && overallFailedPathfinds > 0 )
  	fprintf( m_fp, "                                                                      FAILEDPATHFINDS OUT OF TOLERANCE(0)\n" );
	fprintf( m_fp, "\n" );

	// Script stats
	Real timeLastFrame, slowScript1, slowScript2;
	AsciiString slowScripts = TheScriptEngine->getStats(&timeLastFrame, &slowScript1, &slowScript2);
	fprintf( m_fp, "\n" );
	fprintf( m_fp, "Script Engine Statistics:\n" );
	fprintf( m_fp, "  Total time last frame: %.5f msec\n", timeLastFrame*1000 );
	fprintf( m_fp, "    -Slowest 2 scripts      %s\n", slowScripts.str() );
	fprintf( m_fp, "    -Slowest 2 script times %.5f msec, %.5f msec \n", slowScript1*1000, slowScript2*1000 );
  if ( flagSpikes && slowScript1*1000 > 0.2f || slowScript2*1000 > 0.2f )
  	fprintf( m_fp, "                                                                      SLOW SCRIPT OUT OF TOLERANCE(0.2)\n" );
	fprintf( m_fp, "\n" );



	//PartitionMgr stats
	double gcoTimeThisFrameTotal, gcoTimeThisFrameAvg;
	ThePartitionManager->getPMStats(gcoTimeThisFrameTotal, gcoTimeThisFrameAvg);
	fprintf(m_fp, "Partition Manager Statistics:\n");
	fprintf(m_fp, "  Total time for object scans this frame is %.5f msec\n", gcoTimeThisFrameTotal);
	fprintf(m_fp, "  Avg time per object scan this frame is %.5f msec\n", gcoTimeThisFrameAvg);
	fprintf( m_fp, "\n" );

	// setup texture stats
	Debug_Statistics::Record_Texture_Mode(Debug_Statistics::RECORD_TEXTURE_SIMPLE/*RECORD_TEXTURE_NONE*/);

	fprintf( m_fp, "Video Statistics:\n" );
	//Particle system stats
	fprintf( m_fp, "  Particle Systems: %d\n", TheParticleSystemManager->getParticleSystemCount() );
	Int totalParticles = TheParticleSystemManager->getParticleCount();
	fprintf( m_fp, "  Particles: %d in world (%d onscreen)\n", totalParticles, onScreenParticleCount );

  if ( flagSpikes && totalParticles > TheGlobalData->m_maxParticleCount - 10 )
  	fprintf( m_fp, "                                                                      PARTICLES OUT OF TOLERANCE(CAP-10)\n" );
  if ( flagSpikes && onScreenParticleCount > TheGlobalData->m_maxParticleCount - 10 )
  	fprintf( m_fp, "                                                                      ON_SCREEN_PARTICLES OUT OF TOLERANCE(CAP-10)\n" );


	// polygons this frame	
	Int polyPerFrame = Debug_Statistics::Get_DX8_Polygons();
	Int polyPerSecond = (Int)(polyPerFrame * fps);
	fprintf( m_fp, "  Polygons: %d per frame (%d per second)\n", polyPerFrame, polyPerSecond );

	// vertices this frame
	fprintf( m_fp, "  Vertices: %d\n", Debug_Statistics::Get_DX8_Vertices() );

	//
	// I'm adjusting the texture memory usage counter by subtracting 
	// out the terrain alpha texture (since it's really == terrain texture).
	//
	fprintf( m_fp, "  Video RAM: %d\n", Debug_Statistics::Get_Record_Texture_Size() - 1376256 );

	// terrain stats
	fprintf( m_fp, "  3-Way Blends: %d/%d, \n Shoreline Blends: %d/%d\n", TheTerrainRenderObject->getNumExtraBlendTiles(TRUE),TheTerrainRenderObject->getNumExtraBlendTiles(FALSE), TheTerrainRenderObject->getNumShoreLineTiles(TRUE),TheTerrainRenderObject->getNumShoreLineTiles(FALSE));
  if ( flagSpikes && TheTerrainRenderObject->getNumExtraBlendTiles(TRUE) > 2000 )
  	fprintf( m_fp, "                                                                      3-WAYS OUT OF TOLERANCE(2000)\n" );
  if ( flagSpikes && TheTerrainRenderObject->getNumShoreLineTiles(TRUE) > 2000 )
  	fprintf( m_fp, "                                                                      SHORELINES OUT OF TOLERANCE(2000)\n" );

	fprintf( m_fp, "\n" );

#if defined(_DEBUG) || defined(_INTERNAL)
  if ( ! beBrief )
  {
    TheAudio->audioDebugDisplay( NULL, NULL, m_fp );
	  fprintf( m_fp, "\n" );
  }
#endif
	
#ifdef MEMORYPOOL_DEBUG
	//Report memory usage.
	TheMemoryPoolFactory->debugMemoryReport( REPORT_FACTORYINFO | REPORT_POOLINFO, 0, 0, m_fp );
#else
	fprintf( m_fp, "Memory Report -- unavailable \n(build doesn't have MEMORYPOOL_DEBUG defined)\n" );
#endif
	fprintf( m_fp, "\n" );

	fprintf( m_fp, "%s", TheSubsystemList->dumpTimesForAll().str());

  if ( ! beBrief )
  {
	  fprintf( m_fp, "----------------------------------------------------------------\n" );
	  fprintf( m_fp, "END -- Frame %d\n", TheGameLogic->getFrame() );
	  fprintf( m_fp, "----------------------------------------------------------------\n" );
  }
	fprintf( m_fp, "\n\n" );
	fflush(m_fp);
}

StatDumpClass TheStatDump("StatisticsDump.txt");

#endif //DUMP_PERF_STATS

//*****************************************************************************************
//**** End Statistical Dump ***************************************************************
//*****************************************************************************************
//*****************************************************************************************



///////////////////////////////////////////////////////////////////////////////
// DEFINITIONS ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//=============================================================================
RTS3DScene *W3DDisplay::m_3DScene = NULL;
RTS2DScene *W3DDisplay::m_2DScene = NULL;
RTS3DInterfaceScene *W3DDisplay::m_3DInterfaceScene = NULL;
W3DAssetManager *W3DDisplay::m_assetManager = NULL;

//=============================================================================
	// note, can't use the ones from PerfTimer.h 'cuz they are currently
	// only valid when "-vtune" is used... (srj)
inline Int64 getPerformanceCounter()
{
	Int64 tmp;
	tmp = Clock_Ticks();
	return tmp;
}

inline Int64 getPerformanceCounterFrequency()
{
	Int64 tmp;
	tmp = Clock_Ticks_Per_Second();
	return tmp;
}

// W3DDisplay::W3DDisplay =====================================================
/** */
//=============================================================================
W3DDisplay::W3DDisplay()
{
	Int i;

	m_initialized = false;
	m_assetManager = NULL;
	m_3DScene = NULL;
	m_2DScene = NULL;
	m_3DInterfaceScene = NULL;
	m_averageFPS = TheGlobalData->m_framesPerSecondLimit;
#if defined(_DEBUG) || defined(_INTERNAL)
	m_timerAtCumuFPSStart = 0;
#endif
	for (i=0; i<LightEnvironmentClass::MAX_LIGHTS; i++)
		m_myLight[i] = NULL;
	m_2DRender = NULL;
	m_batch2D = FALSE;
	m_batch2DOpen = FALSE;
	m_batch2DKeepOpen = FALSE;
	m_batch2DImage = NULL;
	m_isClippedEnabled = FALSE;
	m_clipRegion.lo.x = 0;
	m_clipRegion.lo.y = 0;
	m_clipRegion.hi.x = 0;
	m_clipRegion.hi.y = 0;

	for (i = 0; i < DisplayStringCount; i++)
		m_displayStrings[i] = NULL;

}  // end W3DDisplay

static void finishVideo(void);

// W3DDisplay::~W3DDisplay ====================================================
/** */
//=============================================================================
// R1's aircraft-lead measurement (smoothMotionApply, below), reported by the destructor.
static double s_aircraftLeadSum = 0.0;
static unsigned long long s_aircraftLeadCount = 0;
static Real s_aircraftLeadMax = 0.0f;
static Real s_aircraftRadiusMax = 0.0f;
static Int64 s_smoothPassTicks = 0;			// R1's blend and restore, together, over the run
static unsigned long long s_smoothPasses = 0;

W3DDisplay::~W3DDisplay()
{
	// R1: how each captured model was treated per tick, for the snap rules' tuning.
	{
		const unsigned long long *counts = SmoothMotion_Counts();
		unsigned long long total = 0;
		for (Int i = 0; i < SMOOTH_SNAP_COUNT; ++i)
			total += counts[i];
		if (total > 0 && getenv("ZH_SMOOTH_MOTION_STATS") != NULL)
		{
			fprintf(stderr, "smooth motion: %llu model ticks:", total);
			for (Int i = 0; i < SMOOTH_SNAP_COUNT; ++i)
				fprintf(stderr, " %s %llu;", SmoothMotion_SnapName((SmoothMotionSnap)i), counts[i]);
			fprintf(stderr, "\n");
		}
		if (s_smoothPasses > 0 && getenv("ZH_SMOOTH_MOTION_STATS") != NULL)
			fprintf(stderr, "smooth motion: blend and restore cost %.1f us a render frame over %llu frames\n",
				(double)s_smoothPassTicks * 1.0e6 / (double)Clock_Ticks_Per_Second() / (double)s_smoothPasses, s_smoothPasses);
		if (s_aircraftLeadCount > 0 && getenv("ZH_SMOOTH_MOTION_STATS") != NULL)
			fprintf(stderr, "smooth motion: aircraft lead (logic position ahead of the drawn one): %llu samples, mean %.2f, max %.2f world units; the largest aircraft's bounding radius %.2f\n",
				s_aircraftLeadCount, s_aircraftLeadSum / (double)s_aircraftLeadCount, s_aircraftLeadMax, s_aircraftRadiusMax);
	}

	// a -video run that ended before its range did, on -maxframes or a decided match, still gets its movie
	finishVideo();

	// What -ffshader did, written from here because WW3D2 is built without RELEASE_DEBUG_LOGGING
	// and its own WWDEBUG_SAY does not exist in a shipping build.  Before W3D shuts down, which is
	// where the cache is released.
	if( CombinerShaders_Are_Enabled() )
	{
		unsigned compiled = 0;
		unsigned refusedDescriptions = 0;
		unsigned long long shadedDraws = 0;
		unsigned long long refusedDraws = 0;
		CombinerShaderCache_Statistics( compiled, refusedDescriptions, shadedDraws, refusedDraws );
		DEBUG_LOG(("-ffshader: %u programs compiled, %u descriptions refused; %I64u draws shaded, "
			"%I64u left on the fixed-function path\n",
			compiled, refusedDescriptions, shadedDraws, refusedDraws));
	}

	// Same reason as above: WW3D2 has no logging in a shipping build, and a -dx11 run that made no
	// device at all would otherwise look exactly like one that made a device nothing drew through.
	if( Direct3D11_Is_Enabled() )
	{
		unsigned pipelines = 0;
		unsigned long long drawsMade = 0;
		unsigned long long drawsRefused = 0;
		unsigned twinBuffers = 0;
		unsigned long long twinBytes = 0;
		unsigned texturesMirrored = 0;
		unsigned texturesReused = 0;
		unsigned texturesRefused = 0;
		Direct3D11_Statistics( pipelines, drawsMade, drawsRefused );
		Direct3D11_Twin_Statistics( twinBuffers, twinBytes );
		Direct3D11_Texture_Statistics( texturesMirrored, texturesReused, texturesRefused );
		DEBUG_LOG(("-dx11: device %s; %u buffers mirrored (%I64u KB); %u textures mirrored, "
			"%u reused, %u refused; %u pipelines built, %I64u draws made, %I64u refused\n",
			Direct3D11_Is_Active() ? "created" : "REFUSED",
			twinBuffers, twinBytes / 1024,
			texturesMirrored, texturesReused, texturesRefused,
			pipelines, drawsMade, drawsRefused));
		unsigned programsShipped = 0;
		unsigned programsHeld = 0;
		Direct3D11_Program_Statistics( programsShipped, programsHeld );
		DEBUG_LOG(("-dx11 programs: %u shipped with the game, %u held at the end\n",
			programsShipped, programsHeld));

		unsigned long long noBuffer = 0;
		unsigned long long noStage = 0;
		unsigned long long noLayout = 0;
		unsigned long long noProgram = 0;
		unsigned long long noObject = 0;
		unsigned long long foreignShader = 0;
		unsigned long long noTexture = 0;
		Direct3D11_Refusals( noBuffer, noStage, noLayout, noProgram, noObject, foreignShader,
			noTexture );
		DEBUG_LOG(("-dx11 refusals: %I64u no buffer, %I64u no texture stage, %I64u no input layout, "
			"%I64u no program, %I64u no device object, %I64u engine shader bound, "
			"%I64u unmirrored texture\n",
			noBuffer, noStage, noLayout, noProgram, noObject, foreignShader, noTexture));

		// The distinct states behind those counts.  There are a few dozen at most - the engine sets
		// the same handful over and over - and each one names what is missing from the picture.
		const unsigned refusedCount = Direct3D11_Refused_Description_Count();
		for( unsigned refused = 0; refused < refusedCount; refused++ )
			DEBUG_LOG(("-dx11 refused: %s\n", Direct3D11_Refused_Description( refused )));

		if( Direct3D11_Texture_First_Refusal()[0] != '\0' )
			DEBUG_LOG(("-dx11 texture: %s\n", Direct3D11_Texture_First_Refusal()));

		const unsigned shapeCount = Direct3D11_Texture_Copy_Shape_Count();
		for( unsigned shape = 0; shape < shapeCount; shape++ )
			DEBUG_LOG(("-dx11 copies: %s\n", Direct3D11_Texture_Copy_Shape( shape )));

		// The text textures lead that list, and these are the strings they were built for.
		W3DDisplayString_logSentenceBuilds();

		unsigned long long targetsBound = 0;
		unsigned long long targetsRestored = 0;
		unsigned long long drawsIntoTargets = 0;
		Direct3D11_Target_Statistics( targetsBound, targetsRestored, drawsIntoTargets );
		DEBUG_LOG(("-dx11 targets: %I64u binds, %I64u restores, %I64u draws landed in a texture\n",
			targetsBound, targetsRestored, drawsIntoTargets));

		if( Direct3D11_Diagnostic()[0] != '\0' )
			DEBUG_LOG(("-dx11 note: %s\n", Direct3D11_Diagnostic()));

		DEBUG_LOG(("-dx11 post: %s\n", Direct3D11_Post_Diagnostic()));

		const unsigned traceCount = Direct3D11_Target_Trace_Count();
		for( unsigned trace = 0; trace < traceCount; trace++ )
			DEBUG_LOG(("-dx11 target trace: %s\n", Direct3D11_Target_Trace( trace )));

		const unsigned pipelineCount = Direct3D11_Pipeline_Report_Count();
		for( unsigned pipeline = 0; pipeline < pipelineCount; pipeline++ )
			DEBUG_LOG(("-dx11 drew: %s\n", Direct3D11_Pipeline_Report( pipeline )));

		const unsigned foreignCount = Direct3D11_Foreign_Report_Count();
		for( unsigned foreign = 0; foreign < foreignCount; foreign++ )
			DEBUG_LOG(("-dx11 foreign: %s\n", Direct3D11_Foreign_Report( foreign )));

		const unsigned noteCount = Direct3D11_Texture_Note_Count();
		for( unsigned note = 0; note < noteCount; note++ )
			DEBUG_LOG(("-dx11 16bit: %s\n", Direct3D11_Texture_Note( note )));

		if( Direct3D11_First_Compiler_Error()[0] != '\0' )
			DEBUG_LOG(("-dx11 compiler: %s\n", Direct3D11_First_Compiler_Error()));
	}

	// get rid of the debug display
	delete m_debugDisplay;

	// delete the display strings
	for (int i = 0; i < DisplayStringCount; i++)
		TheDisplayStringManager->freeDisplayString(m_displayStrings[i]);

	// delete 2D renderer
	if( m_2DRender )
	{

		m_2DRender->Reset();
		delete m_2DRender;
		m_2DRender = NULL;

	}  // end if

	//
	// delete all our views now since they are W3D views and we need to
	// free them BEFORE we shutdown W3D
	//
	Display::deleteViews();

	REF_PTR_RELEASE( m_3DScene );
	REF_PTR_RELEASE( m_2DScene );
	REF_PTR_RELEASE( m_3DInterfaceScene );
	for (Int j=0; j<LightEnvironmentClass::MAX_LIGHTS; j++)
		REF_PTR_RELEASE( m_myLight[j] );

	PredictiveLODOptimizerClass::Free();

	// shutdown
	Debug_Statistics::Shutdown_Statistics();
	// the shader manager and the browser control are the two things init skipped under -nodevice,
	// so they are the two things there is nothing here to take down
	const Bool hadDevice = !(TheGlobalData && TheGlobalData->m_noRenderDevice);
	if( hadDevice )
		W3DShaderManager::shutdown();
	m_assetManager->Free_Assets();
	delete m_assetManager;
	WW3D::Shutdown();
	WWMath::Shutdown();
#if defined(_WIN32)	// the embedded browser: Windows only
	if( hadDevice )
		DX8WebBrowser::Shutdown();
#endif
	delete TheW3DFileSystem;
	TheW3DFileSystem = NULL;

}  // end ~W3DDisplay


Bool IS_FOUR_BY_THREE_ASPECT( Real x, Real y )
{
  if ( y == 0 )
    return FALSE;
  
  Real aspectRatio = fabs( x / y ); 
  return (( aspectRatio > 1.332f) && ( aspectRatio < 1.334f));
  
}


void W3DDisplay::setGamma(Real gamma, Real bright, Real contrast, Bool calibrate)
{
	if (m_windowed)
		return;	//we don't allow gamma to change in window because it would affect desktop.

	DX8Wrapper::Set_Gamma(gamma,bright,contrast,calibrate, false);
}

/*Giant hack in order to keep the game from getting stuck when alt-tabbing*/
void Reset_D3D_Device(bool active)
{
	if (TheDisplay && WW3D::Is_Initted() && !TheDisplay->getWindowed())
	{
		// On Windows nothing owns the display exclusively under either renderer, so leaving loses no
		// device: the window covering the monitor goes, and comes back with the game.
#if defined(_WIN32)
		DX8Wrapper::Apply_Fullscreen_Display(active);
		return;
#endif
		if (active)
		{	
			//switch back to desired mode when user alt-tabs back into game
			WW3D::Set_Render_Device( WW3D::Get_Render_Device(),TheDisplay->getWidth(),TheDisplay->getHeight(),TheDisplay->getBitDepth(),TheDisplay->getWindowed(),true, true);
#if defined(_WIN32)	// Windows 9x's alt-tab; there is no counterpart anywhere else
			OSVERSIONINFO	osvi;
			osvi.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);
			if (GetVersionEx(&osvi))
			{	//check if we're running Win9x variant since they have buggy alt-tab that requires
				//reloading all textures.
				if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
				{	//only do this on Win9x boxes because it makes alt-tab very slow.
						WW3D::_Invalidate_Textures();
				}
			}
#endif
		}
		else
		{
			//switch to windowed mode whenever the user alt-tabs out of game. Don't restore assets after reset since we'll do it when returning.
			WW3D::Set_Render_Device( WW3D::Get_Render_Device(),TheDisplay->getWidth(),TheDisplay->getHeight(),TheDisplay->getBitDepth(),TheDisplay->getWindowed(),true, true, false);
		}
	}
}

//=============================================================================
/** The monitor Options.ini names, or the primary. */
//=============================================================================
static MonitorEntry chosenMonitor( void )
{
	return findMonitor( TheGlobalData ? TheGlobalData->m_monitor.str() : "" );
}

//=============================================================================
/** Dress the application window for one WindowModeType, while the game is running.
	*
	* WinMain picks this style once, before the engine exists, out of the command line and
	* Options.ini - which is why changing the setting in the options screen only ever showed up on
	* the next run.  The rules are WinMain's own (initializeAppWindows): a frame and a caption for a
	* plain window, neither for the two that own the screen, and a system menu on all of them so
	* alt+F4 still closes it.
	*
	* ApplicationIsBorderless has to travel with it: WndProc reads it on every WM_SIZE to keep a
	* borderless window pinned at the origin. */
//=============================================================================
static void applyWindowFrame( Int mode )
{
#if defined(_WIN32)
	extern RenderWindow ApplicationHWnd;	// WinMain's HWND on Windows
	extern Bool ApplicationIsBorderless;

	// a headless run has no picture at all, so there is nothing for any of the three to mean
	if( ApplicationHWnd == NULL || ( TheGlobalData && TheGlobalData->m_headless ) )
		return;

	ApplicationIsBorderless = ( mode == WINDOW_MODE_BORDERLESS );

	UnsignedInt style = WS_POPUP | WS_VISIBLE | WS_SYSMENU;
	if( mode == WINDOW_MODE_WINDOWED )
		style |= WS_DLGFRAME | WS_CAPTION | WS_MINIMIZEBOX;		// see WinMain: windowed means a real window

	::SetWindowLong( ApplicationHWnd, GWL_STYLE, style );

	//
	// The resize that follows keeps the top left corner where it is (SWP_NOMOVE), so a window that
	// is about to cover the display is put at the monitor's corner now - otherwise it hangs off the
	// bottom right by however far down the screen it happened to be sitting.
	//
	const RenderRect screen = chosenMonitor().rect;
	const UINT move =( mode == WINDOW_MODE_WINDOWED ) ? SWP_NOMOVE : 0;
	::SetWindowPos( ApplicationHWnd,
									( mode == WINDOW_MODE_WINDOWED ) ? HWND_TOP : HWND_TOPMOST,
									screen.left, screen.top, 0, 0, SWP_NOSIZE | SWP_FRAMECHANGED | move );
#else
	// the window is C2's off Windows: the same rules, and the platform layer's hook does the dressing
	extern RenderWindow ApplicationHWnd;	// SdlGameEngine's window (PosixMain.cpp)
	extern Bool ApplicationIsBorderless;

	if( ApplicationHWnd == NULL || ( TheGlobalData && TheGlobalData->m_headless ) )
		return;

	ApplicationIsBorderless = ( mode == WINDOW_MODE_BORDERLESS );
	if( TheW3DWindowFrameHook != NULL )
		TheW3DWindowFrameHook( mode );
#endif
}

//=============================================================================
/** Give the window a client area exactly this big, wearing whatever frame it now has.  A plain
	* window goes back to the middle of its monitor, where one is born; a borderless one covers the
	* monitor from its corner. Fullscreen is not our business - D3D owns that window. */
//=============================================================================
static void sizeWindowToClient( Int mode, Int width, Int height )
{
#if defined(_WIN32)
	extern RenderWindow ApplicationHWnd;	// WinMain's HWND on Windows

	if( ApplicationHWnd == NULL || ( TheGlobalData && TheGlobalData->m_headless ) )
		return;
	if( mode == WINDOW_MODE_FULLSCREEN )
		return;

	RenderRect rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = width;
	rect.bottom = height;
	::AdjustWindowRect( &rect, ::GetWindowLong( ApplicationHWnd, GWL_STYLE ), FALSE );

	const Int outerW = rect.right - rect.left;
	const Int outerH = rect.bottom - rect.top;

	const RenderRect screen = chosenMonitor().rect;
	Int x = screen.left, y = screen.top;
	if( mode == WINDOW_MODE_WINDOWED )
	{
		x += ( screen.right - screen.left - outerW ) / 2;
		y += ( screen.bottom - screen.top - outerH ) / 2;
	}

	::SetWindowPos( ApplicationHWnd,
									( mode == WINDOW_MODE_WINDOWED ) ? HWND_TOP : HWND_TOPMOST,
									x, y, outerW, outerH, SWP_NOACTIVATE );
#else
	extern RenderWindow ApplicationHWnd;	// SdlGameEngine's window (PosixMain.cpp)

	if( ApplicationHWnd == NULL || ( TheGlobalData && TheGlobalData->m_headless ) )
		return;
	if( mode == WINDOW_MODE_FULLSCREEN )
		return;

	if( TheW3DWindowSizeHook != NULL )
		TheW3DWindowSizeHook( mode, width, height, chosenMonitor().rect );
#endif
}

//=============================================================================
// GlobalData carries the whole chain by default and -dx11post replaces it, "off" included.  FXAA is
// always in the default: the Direct3D 11 swap chain takes one sample, and a multisampled scene
// target would break the screen filters, which redirect the scene into a texture of the same size
// and share the back buffer's depth with it.
static void pushDirect3D11PostChain( void )
{
	// Classic graphics is the game's own picture, which is what -d3d9 draws: hard edged sprites.
	Direct3D11_Allow_Soft_Particles( !TheGlobalData->m_classicGraphics );
	const AsciiString & requested = TheGlobalData->m_direct3D11PostChain;
	if( !Direct3D11_Post_Chain( requested.str() ) && !requested.isEmpty() )
	{
		DEBUG_LOG(( "-dx11post: '%s' names no effect this build has, so no chain runs\n", requested.str() ));
	}
}

/** Set resolution of display */
//=============================================================================
Bool W3DDisplay::setDisplayMode( UnsignedInt xres, UnsignedInt yres, UnsignedInt bitdepth, Bool windowed )
{
	extern Bool ApplicationIsBorderless;

	// WW3D2 cannot see GlobalData, so a vsync change has to be pushed in before the reset below
	// the way the sample count is pushed in before the device exists.  The monitor too: a fullscreen
	// game changes that monitor's mode and nobody else's.
	if( TheGlobalData )
	{
		DX8Wrapper::Set_Requested_VSync( TheGlobalData->m_vsync != FALSE );
		Direct3D11_Set_VSync( TheGlobalData->m_vsync != FALSE );
		DX8Wrapper::Set_Requested_Fullscreen_Keep_Aspect( TheGlobalData->m_fullscreenScaling == FULLSCREEN_SCALING_KEEP_ASPECT );
		pushDirect3D11PostChain();
	}
	DX8Wrapper::Set_Requested_Monitor( chosenMonitor().device );

	//
	// Which of the three the window is wearing, and which it is being asked for.  The test cannot be
	// on the windowed flag alone: borderless and windowed are both windowed devices and differ only
	// in the frame, so a switch between those two would look like no change at all and the caption
	// would stay on a window covering the display.
	//
	const Bool wasWindowed = getWindowed();
	const Int wasMode = wasWindowed
											? ( ApplicationIsBorderless ? WINDOW_MODE_BORDERLESS : WINDOW_MODE_WINDOWED )
											: WINDOW_MODE_FULLSCREEN;
	const Int mode = windowed
									 ? ( ( TheGlobalData && TheGlobalData->m_windowMode == WINDOW_MODE_BORDERLESS )
											 ? WINDOW_MODE_BORDERLESS : WINDOW_MODE_WINDOWED )
									 : WINDOW_MODE_FULLSCREEN;

	//
	// Windowed against fullscreen is a different device rather than a resize, and
	// Set_Device_Resolution says so itself - "TODO: support changing windowed status" - by quietly
	// resetting the old one and returning success.  So the mode dropdown used to take a restart.
	// Set_Render_Device with reset_device is the call that rebuilds the device, the same one the
	// alt-tab handler above uses; the window's frame goes on first, because the resize inside it
	// measures whatever frame the window is wearing.
	//
	if( mode != wasMode )
	{
		//
		// Which way round the frame and the device go depends on which way we are travelling.
		//
		// Into fullscreen, the frame comes off first: the resize inside Set_Render_Device measures
		// whatever frame the window is wearing.  Coming *out* of fullscreen it is the other way
		// round, and this is not a preference.  While the device still owns the display, hanging a
		// caption on its window loses the device, and Reset_Device answers a lost device by
		// returning false without trying - so the switch failed, the fallback put fullscreen back,
		// and the mode dropdown looked like it did nothing at all in that one direction.
		//
		const Bool leavingFullscreen = ( wasMode == WINDOW_MODE_FULLSCREEN );

		if( !leavingFullscreen )
			applyWindowFrame( mode );

		Bool ok = ( WW3D_ERROR_OK == WW3D::Set_Render_Device( WW3D::Get_Render_Device(), xres, yres,
																												 bitdepth, windowed, true, true, true ) );
		if( ok == FALSE )
		{
			// a device that was lost for a moment cannot be reset until it is not; one more go
			ok = ( WW3D_ERROR_OK == WW3D::Set_Render_Device( WW3D::Get_Render_Device(), xres, yres,
																											 bitdepth, windowed, true, true, true ) );
		}

		if( ok )
		{
			if( leavingFullscreen )
				applyWindowFrame( mode );

			sizeWindowToClient( mode, xres, yres );

			Render2DClass::Set_Screen_Resolution( RectClass( 0, 0, xres, yres ) );
			Display::setDisplayMode( xres, yres, bitdepth, windowed );

			DEBUG_LOG(( "Window mode %d -> %d at %dx%d\n", wasMode, mode, xres, yres ));
			return TRUE;
		}

		// the device is still the old one, so put the frame back on the window that goes with it
		DEBUG_LOG(( "Window mode %d -> %d refused by the device; staying put\n", wasMode, mode ));
		applyWindowFrame( wasMode );
		WW3D::Set_Render_Device( WW3D::Get_Render_Device(), getWidth(), getHeight(), getBitDepth(),
														 wasWindowed, true, true, true );
		sizeWindowToClient( wasMode, getWidth(), getHeight() );
		Render2DClass::Set_Screen_Resolution( RectClass( 0, 0, getWidth(), getHeight() ) );
		Display::setDisplayMode( getWidth(), getHeight(), getBitDepth(), wasWindowed );
		return FALSE;
	}

	if (WW3D_ERROR_OK == WW3D::Set_Device_Resolution(xres,yres,bitdepth,windowed,true))
	{
		// the resize above keeps the window's corner, which is on the old monitor when the options
		// menu has just picked another one
		sizeWindowToClient( mode, xres, yres );
		Render2DClass::Set_Screen_Resolution(RectClass(0, 0, xres, yres));
		Display::setDisplayMode(xres, yres, bitdepth, windowed);
		return TRUE;
	}

	//set back to the original mode.
	WW3D::Set_Device_Resolution(getWidth(),getHeight(),getBitDepth(),getWindowed(), true);
	Render2DClass::Set_Screen_Resolution(RectClass(0, 0, getWidth(),getHeight()));
	Display::setDisplayMode(getWidth(),getHeight(),getBitDepth(), getWindowed());
	return FALSE;	//did not change to a new mode.
}

/** Set width of display */
//=============================================================================
void W3DDisplay::setWidth( UnsignedInt width )
{

	// extending functionality
	Display::setWidth( width );

	// our 2D renderer will use mapping coords to make (0,0) the upper left
	// of the screen with (width,height) at the lower right
	m_2DRender->Set_Coordinate_Range( RectClass( 0, 0, getWidth(), getHeight() ) );

}  // end set width

// W3DDisplay::setHeight ======================================================
/** Set height of display */
//=============================================================================
void W3DDisplay::setHeight( UnsignedInt height )
{

	// extending functionality
	Display::setHeight( height );

	// our 2D renderer will use mapping coords to make (0,0) the upper left
	// of the screen with (width,height) at the lower right
	m_2DRender->Set_Coordinate_Range( RectClass( 0, 0, getWidth(), getHeight() ) );

}  // end set height

// W3DDisplay::initAssets =====================================================
/** */
//=============================================================================
void W3DDisplay::initAssets( void )
{

}  // end initAssets

// W3DDisplay::init3DScene ====================================================
/** */
//=============================================================================
void W3DDisplay::init3DScene( void )
{

}  // end init3DScene

// W3DDisplay::init2DScene ====================================================
/** This is the 2D scene, you can use it to draw on a 2D plane over the
	* 3D background */
//=============================================================================
void W3DDisplay::init2DScene( void )
{

}  // end init2DScene

// W3DDisplay::init ===========================================================
/** Initialize or re-initialize the W3D display system.  Here we need to
  * create our window, and get our 3D hardware setup and online */
//=============================================================================
void W3DDisplay::init( void )
{

	//
	// call our base class init, this method should be able to handle re-entry
	// with its own logic
	//
	Display::init();

	// handle re-entry for ourselves
	if( m_initialized )
	{

		/// @todo W3DDisplay needs RE-init logic!
		return;

	}  // end if
	// Override the W3D File system
	TheW3DFileSystem = NEW W3DFileSystem;

	// init the Westwood math library
	WWMath::Init();

	// create our 3D interface scene
	m_3DInterfaceScene = NEW_REF( RTS3DInterfaceScene, () );
	m_3DInterfaceScene->Set_Ambient_Light( Vector3( 1, 1, 1 ) );

	// create our 2D scene
	m_2DScene = NEW_REF( RTS2DScene, () );
	m_2DScene->Set_Ambient_Light( Vector3( 1, 1, 1 ) );

	// create our 3D scene
	m_3DScene =NEW_REF( RTS3DScene, () );
#if defined(_DEBUG) || defined(_INTERNAL)
	if( TheGlobalData->m_wireframe )
		m_3DScene->Set_Polygon_Mode( SceneClass::LINE );
#endif
//============================================================================
	// m_myLight = NEW_REF
//============================================================================
	Int lindex;
	for (lindex=0; lindex<TheGlobalData->m_numGlobalLights; lindex++) 
	{	m_myLight[lindex] = NEW_REF( LightClass, (LightClass::DIRECTIONAL) );
	}

	setTimeOfDay( TheGlobalData->m_timeOfDay );	//set each light to correct values for given time

	for (lindex=0; lindex<TheGlobalData->m_numGlobalLights; lindex++) 
	{	m_3DScene->setGlobalLight( m_myLight[lindex], lindex );
	}

#ifdef SAMPLE_DYNAMIC_LIGHT
	theDynamicLight = NEW_REF(W3DDynamicLight, ());
	Real red = 1;
	Real green = 1;
	Real blue = 0;
	if(red==0 && blue==0 && green==0) {
		red = green = blue = 1;
	}
	theDynamicLight->Set_Ambient( Vector3( red, green, blue ) );
	theDynamicLight->Set_Diffuse( Vector3( red, green, blue) );
	theDynamicLight->Set_Position(Vector3(0, 0, 4));
	theDynamicLight->Set_Far_Attenuation_Range(1, 8);
	// Note: Don't Add_Render_Object dynamic lights. 
	m_3DScene->addDynamicLight( theDynamicLight );
#endif

	// create a new asset manager
	m_assetManager = NEW W3DAssetManager;	
	m_assetManager->Register_Prototype_Loader(&_ParticleEmitterLoader );
	m_assetManager->Register_Prototype_Loader(&_AggregateLoader);
	m_assetManager->Set_WW3D_Load_On_Demand( true );


	if (TheGlobalData->m_incrementalAGPBuf)
	{
		SortingRendererClass::SetMinVertexBufferSize(1);
	}
	if (WW3D::Init( ApplicationHWnd ) != WW3D_ERROR_OK)
		throw ERROR_INVALID_D3D;	//failed to initialize.  User probably doesn't have DX 8.1

	// The sorting pool's depth pass and batch copy split across the job pool.  WW3D2 cannot see it,
	// so it is handed over here; parallel_for runs inline when the pool has no workers.
	SortingRendererClass::Set_Parallel_For( JobSystem::parallel_for );

	WW3D::Set_Prelit_Mode( WW3D::PRELIT_MODE_LIGHTMAP_MULTI_PASS );
	WW3D::Set_Collision_Box_Display_Mask(0x00);	///<set to 0xff to make collision boxes visible
	WW3D::Enable_Static_Sort_Lists(true);
	WW3D::Set_Thumbnail_Enabled(false);
	// The bias is half a pixel taken off every 2D vertex, and it makes text look good on Direct3D 9
	// because a pixel's centre sits at an integer screen coordinate there.  It stays on under the
	// Direct3D 11 backend as well: that backend shifts its viewport half a pixel instead, so a
	// vertex lands where Direct3D 9 lands it and the bias means the same thing on both.  Turning it
	// off there as well was tried, and with the viewport shift in place it moves the 2D layer twice.
	WW3D::Set_Screen_UV_Bias( TRUE );
	WW3D::Set_Texture_Bitdepth(32);
			
	setWindowed( TheGlobalData->m_windowed );

	// create a 2D renderer helper
	m_2DRender = NEW Render2DClass;
	DEBUG_ASSERTCRASH( m_2DRender, ("Cannot create Render2DClass") );

	// set our default width and height and bit depth
	/// @todo we should set this according to options read from a file
	setWidth( TheGlobalData->m_xResolution );
	setHeight( TheGlobalData->m_yResolution );
	setBitDepth( W3D_DISPLAY_DEFAULT_BIT_DEPTH );

	// Multisampling has to be known before the device exists, and WW3D2 cannot read GlobalData, so
	// the sample count is handed to it here.  msaaSamplesForLevel turns the stored index into 0, 2,
	// 4, 8 or 16; the device degrades an unsupported one on its own.  While Direct3D 11 presents,
	// the Direct3D 9 frame is never shown, so its samples would be memory spent on nothing; the
	// Direct3D 11 frame is smoothed by the FXAA in its default chain instead.
	DX8Wrapper::Set_Requested_MultiSample_Level( TheGlobalData->m_direct3D11
		? 0 : msaaSamplesForLevel( TheGlobalData->m_msaaLevel ) );
	DX8Wrapper::Set_Requested_VSync( TheGlobalData->m_vsync != FALSE );
	Direct3D11_Set_VSync( TheGlobalData->m_vsync != FALSE );
	DX8Wrapper::Set_Requested_Monitor( chosenMonitor().device );
	DX8Wrapper::Set_Requested_Fullscreen_Keep_Aspect( TheGlobalData->m_fullscreenScaling == FULLSCREEN_SCALING_KEEP_ASPECT );

	// Same reason: WW3D2 cannot see GlobalData, so the backend choice is pushed in from here.  The
	// fixed-function probe and the generated combiner shaders are always on.
	FixedFunctionProbe_Enable( true );
	CombinerShaders_Enable( true );
	Direct3D11_Enable( TheGlobalData->m_direct3D11 != FALSE );
	Direct3D11_Present_Enable( TheGlobalData->m_direct3D11 != FALSE );
	Direct3D11_Dump_Programs_To( TheGlobalData->m_direct3D11DumpPath.str() );
	// The compiled-program cache is the player's, like Options.ini: an installed game cannot write
	// next to its exe, and the shipped programs there are read-only anyway.
	Direct3D11_Set_Shader_Cache_Directory( TheGlobalData->getPath_UserData().str() );
	pushDirect3D11PostChain();
	// Classic graphics is read here once and not again: a tile size cannot change under a loaded
	// map.  The menu says the setting waits for the next launch.
	// Before any map is read: every tile and the atlas are sized by it.  A headless run draws no
	// ground, so it keeps EA's tile and the memory, and classic graphics keeps EA's tile to look it.
	TheTilePixelExtent = (TheGlobalData->m_headless || TheGlobalData->m_classicGraphics)
		? SOURCE_TILE_PIXEL_EXTENT : MAX_TILE_PIXEL_EXTENT;

	// Same problem, same answer: the filter table is built the moment the device exists and WW3D2
	// cannot see GlobalData, so the player's texture filtering goes in here. Nothing in the game
	// ever called Set_Texture_Filter, so before this the mode was whatever the WW3D2 default was.
	WW3D::Set_Requested_Texture_Filter( TheGlobalData->m_textureFilterMode );
	TextureFilterClass::_Set_Requested_Anisotropy( (unsigned int)TheGlobalData->m_anisotropyLevel );

	/* -nodevice: never ask for a device at all.  D3D9 answers CreateDevice with D3DERR_DEVICELOST
		 while the workstation is locked, and init then threw ERROR_INVALID_D3D and took the run with
		 it, so an unattended measurement needed somebody logged in with the screen awake.
		 Enumeration above still happens - DX8Wrapper::Init only counts adapters and modes, which
		 works locked - so anything that asks what the hardware is still gets an answer.
		 Everything below this point is the device and the things drawn with it: the shader manager,
		 the debug display's font, the browser control.  None of it has a caller in a run that draws
		 nothing. */
	if( TheGlobalData && TheGlobalData->m_noRenderDevice )
	{
		// The subsystems the device would normally have brought up, minus the ones that are D3D
		// resources.  Every render object's constructor reaches for a preset vertex material, so
		// without these the first drawable built takes the process down.
		DX8Wrapper::Do_Onetime_Device_Independent_Inits();
		DEBUG_LOG(("W3DDisplay::init - headless: no render device, nothing will be drawn\n"));
		m_initialized = true;
		return;
	}

	if( WW3D::Set_Render_Device( 0,
															 getWidth(),
															 getHeight(),
															 getBitDepth(),
															 getWindowed(),
															 true ) != WW3D_ERROR_OK )
	{
		// Getting the device at the default bit depth (32) didn't work, so try
		// getting a 16 bit display.  (Voodoo 1-3 only supported 16 bit.) jba.
		setBitDepth( 16 );
		if( WW3D::Set_Render_Device( 0, 
																 getWidth(), 
																 getHeight(), 
																 getBitDepth(), 
																 getWindowed(), 
																 true ) != WW3D_ERROR_OK ) 
		{

			WW3D::Shutdown();
			WWMath::Shutdown();
			throw ERROR_INVALID_D3D;	//failed to initialize.  User probably doesn't have DX 8.1
			DEBUG_ASSERTCRASH( 0, ("Unable to set render device\n") );
			return;
		}

	}  // end if

	// Which backend draws what is on screen.  d3d9.dll is loaded either way, because the Direct3D 9
	// device is always made and the Direct3D 11 one mirrors it, so the loaded dll says nothing.
#if defined(_WIN32)
	DEBUG_LOG(("W3DDisplay::init - renderer runtime: %s\n",
						 Direct3D11_Present_Is_Enabled() ? "Direct3D 11"
						                                 : "Direct3D 9"));
	DEBUG_LOG(("W3DDisplay::init - D3DX: %s\n", D3DX9_Runtime_Name()));
#else
	DEBUG_LOG(("W3DDisplay::init - renderer runtime: the POSIX Direct3D 9 device (posixd3d9)\n"));
	// On Windows Set_Render_Device's resize_window gives the window a client area of the resolution;
	// off Windows dx8wrapper leaves the window to its owner, so the display asks for that here.
	{
		extern Bool ApplicationIsBorderless;
		sizeWindowToClient( getWindowed() ? ( ApplicationIsBorderless ? WINDOW_MODE_BORDERLESS : WINDOW_MODE_WINDOWED )
																			: WINDOW_MODE_FULLSCREEN,
												getWidth(), getHeight() );
	}
#endif
	// multisampling is opt-in with "-msaa" / "-msaa N" and silently degrades to whatever the
	// device supports, so log what was actually granted
	DEBUG_LOG(("W3DDisplay::init - multisampling: %ux\n", DX8Wrapper::Get_MultiSample_Level()));
#if defined(_WIN32)
	DEBUG_LOG(("W3DDisplay::init - vsync: %s\n", DX8Wrapper::Get_Requested_VSync() ? "on" : "off"));
#else
	// The POSIX device never reads D3D9's presentation interval: SdlGpuFrame claims the window with SDL's
	// defaults, whose present mode is VSYNC.  So "off" was only the request, and read as the truth it sent a
	// 119 fps reading (a locked session: no drawable) looking for a vsync bug.
	DEBUG_LOG(("W3DDisplay::init - vsync: always on (the SDL GPU swapchain's VSYNC; D3D9 asked for %s, which is not used;"
		" -offscreen paces by ZH_OFFSCREEN_HZ instead)\n", DX8Wrapper::Get_Requested_VSync() ? "on" : "off"));
#endif
	DEBUG_LOG(("W3DDisplay::init - present: %s\n", DX8Wrapper::Is_Flip_Present() ? "flip" : "discard"));
	if (Direct3D11_Present_Is_Enabled())
		DEBUG_LOG(("W3DDisplay::init - dx11 swap chain: %s\n",
							 Direct3D11_Can_Tear() ? "flip, tearing allowed" : "blt, held to the refresh"));
	DEBUG_LOG(("W3DDisplay::init - adapter: %s\n",
						 WW3D::Get_Render_Device_Name(WW3D::Get_Render_Device())));
	{
		const char *lodName = "off";
		if (TheGameLODManager && TheGlobalData && TheGlobalData->isDynamicLODEnabled())
		{
			const DynamicGameLODLevel lod = TheGameLODManager->getDynamicLODLevel();
			if (lod >= DYNAMIC_GAME_LOD_LOW && lod < DYNAMIC_GAME_LOD_COUNT)
				lodName = TheGameLODManager->getDynamicGameLODLevelName(lod);
		}
		DEBUG_LOG(("W3DDisplay::init - quality: filter %d aniso %d particles %d (in force %d) shadows vol %d decal %d trees %d heat %d dynamicLOD %s\n",
							 TheGlobalData ? TheGlobalData->m_textureFilterMode : -1,
							 TheGlobalData ? TheGlobalData->m_anisotropyLevel : -1,
							 TheGlobalData ? TheGlobalData->m_maxParticleCount : -1,
							 TheGlobalData ? TheGlobalData->getEffectiveParticleCap() : -1,
							 TheGlobalData ? (Int)TheGlobalData->m_useShadowVolumes : -1,
							 TheGlobalData ? (Int)TheGlobalData->m_useShadowDecals : -1,
							 TheGlobalData ? (Int)TheGlobalData->m_useTrees : -1,
							 TheGlobalData ? (Int)TheGlobalData->m_useHeatEffects : -1,
							 lodName));
	}

	//Check if level was never set and default to setting most suitable for system.
	if (TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)
		TheGameLODManager->setStaticLODLevel(TheGameLODManager->findStaticLODLevel());
	else
	{	//Static LOD level was applied during GameLOD manager init except for texture reduction
		//which needs to be applied here.
		Int txtReduction=TheWritableGlobalData->m_textureReductionFactor;
		if (txtReduction > 0)
		{		WW3D::Set_Texture_Reduction(txtReduction,32);
				//Tell LOD manager that texture reduction was applied.
				TheGameLODManager->setCurrentTextureReduction(txtReduction);
		}
	}

	if (TheGlobalData->m_displayGamma != 1.0f)
		setGamma(TheGlobalData->m_displayGamma,0.0f,1.0f,FALSE);

	initAssets();
	init2DScene();
	init3DScene();
	W3DShaderManager::init();

	// Create and initialize the debug display
	m_nativeDebugDisplay = NEW W3DDebugDisplay();
	m_debugDisplay = m_nativeDebugDisplay;
	if ( m_nativeDebugDisplay )
	{
		m_nativeDebugDisplay->init();
		GameFont *font;

		if (TheGlobalLanguageData && TheGlobalLanguageData->m_nativeDebugDisplay.name.isNotEmpty())
		{
			font=TheFontLibrary->getFont(
				TheGlobalLanguageData->m_nativeDebugDisplay.name,
				TheGlobalLanguageData->m_nativeDebugDisplay.size,
				TheGlobalLanguageData->m_nativeDebugDisplay.bold);
		}
		else
			font=TheFontLibrary->getFont( AsciiString("FixedSys"), 8, FALSE );

		m_nativeDebugDisplay->setFont( font );
		m_nativeDebugDisplay->setFontHeight( 13 );
		m_nativeDebugDisplay->setFontWidth( 9 );
	}

#if defined(_WIN32)	// the embedded browser: Windows only
	DX8WebBrowser::Initialize();
#endif

	// we're now online
	m_initialized = true;
	if( TheGlobalData->m_displayDebug )
	{
		m_debugDisplayCallback = StatDebugDisplay;
	}
}  // end init

// W3DDisplay::reset ===========================================================
/** Reset the W3D display system.  Here we need to
  * remove the objects from the previous map. */
//=============================================================================
void W3DDisplay::reset( void )
{

	Display::reset();

	// Remove all render objects.

	SceneIterator *sceneIter = m_3DScene->Create_Iterator();
	sceneIter->First();
	while(!sceneIter->Is_Done()) {
		RenderObjClass * robj = sceneIter->Current_Item();
		robj->Add_Ref();
		m_3DScene->Remove_Render_Object(robj);
		robj->Release_Ref();
		sceneIter->Next();
	}
	m_3DScene->Destroy_Iterator(sceneIter);

	m_isClippedEnabled = FALSE;

	// release any unused assets from W3D
	/// @todo really need that "scene abstraction", having this stuff in the display is icky
	m_assetManager->Release_Unused_Assets();

	if (TheWritableGlobalData)
		TheWritableGlobalData->m_drawSkyBox =0;
}

const UnsignedInt START_CUMU_FRAME = LOGICFRAMES_PER_SECOND / 2;	// skip first half-sec

/** Update a moving average of the last 30 fps measurements.  Also try to filter out temporary spikes.
	This code is designed to be used by the GameLOD sytems to determine the correct dynamic LOD setting.
*/
void W3DDisplay::updateAverageFPS(void)
{
	const Real MaximumFrameTimeCutoff = 0.5f;	//largest frame interval (seconds) we accept before ignoring it as a momentary "spike"
	const Int FPS_HISTORY_SIZE = 30;	//keep track of the last 30 frames

	static Int64 lastUpdateTime64 = 0;
	static Int historyOffset = 0;
	static Int numSamples = 0;
	static double fpsHistory[FPS_HISTORY_SIZE];

	Int64 freq64 = getPerformanceCounterFrequency();
	Int64 time64 = getPerformanceCounter();

#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheGameLogic->getFrame() == START_CUMU_FRAME)
	{
		m_timerAtCumuFPSStart = time64;
	}
#endif

	Int64 timeDiff = time64 - lastUpdateTime64;

	// convert elapsed time to seconds
	double elapsedSeconds = (double)timeDiff/(double)(freq64);

	if (elapsedSeconds <= MaximumFrameTimeCutoff)	//make sure it's not a spike
	{
		// append new sameple to fps history.
		if (historyOffset >= FPS_HISTORY_SIZE)
			historyOffset = 0;

		double currentFPS = 1.0/elapsedSeconds; 
		fpsHistory[historyOffset++] = currentFPS;
		numSamples++;
		if (numSamples > FPS_HISTORY_SIZE)
			numSamples = FPS_HISTORY_SIZE;
	}

	if (numSamples)
	{	
		// determine average frame rate over our past history.
		Real average=0;
		for (Int i=0,j=historyOffset-1; i<numSamples; i++,j--)
		{
			if (j < 0)
				j=FPS_HISTORY_SIZE-1;	// wrap around to front of buffer
			average += fpsHistory[j];
		}

		m_averageFPS = average / (Real)numSamples;
	}

	lastUpdateTime64 = time64;
}

#if defined(_DEBUG) || defined(_INTERNAL)	//debug hack to view object under mouse stats
ICoord2D TheMousePos;
#endif

// W3DDisplay::gatherDebugStats ===================================================
/** Compute and display debug stats on screen */
//=============================================================================
void W3DDisplay::gatherDebugStats( void )
{
	static UnsignedInt s_framesRenderedSinceLastUpdate = 0;
	static Int64 s_lastUpdateTime64 = 0;
	static double s_timeSinceLastUpdateInSecs = 0.0;
	static Int s_drawCallsSinceLastUpdate = 0;
	static Int s_sortedPolysSinceLastUpdate = 0;

	// allocate the display strings if needed
	if( m_displayStrings[0] == NULL )
	{
		GameFont *font;
		if (TheGlobalLanguageData && TheGlobalLanguageData->m_nativeDebugDisplay.name.isNotEmpty())
		{
			font=TheFontLibrary->getFont(
				TheGlobalLanguageData->m_nativeDebugDisplay.name,
				TheGlobalLanguageData->m_nativeDebugDisplay.size,
				TheGlobalLanguageData->m_nativeDebugDisplay.bold);
		}
		else
			font = TheFontLibrary->getFont( AsciiString("FixedSys"), 8, FALSE );

		for (int i = 0; i < DisplayStringCount; i++)
		{
			if (m_displayStrings[i] == NULL)
			{
				m_displayStrings[i] = TheDisplayStringManager->newDisplayString();
				DEBUG_ASSERTCRASH( m_displayStrings[i], ("Failed to create DisplayString") );
				m_displayStrings[i]->setFont( font );
			}
		}

	}  // end if

	if (m_benchmarkDisplayString == NULL)
	{
		GameFont *thisFont = TheFontLibrary->getFont( AsciiString("FixedSys"), 8, FALSE );
		m_benchmarkDisplayString = TheDisplayStringManager->newDisplayString();
		DEBUG_ASSERTCRASH( m_benchmarkDisplayString, ("Failed to create DisplayString") );
		m_benchmarkDisplayString->setFont( thisFont );
	}

	++s_framesRenderedSinceLastUpdate;
  s_drawCallsSinceLastUpdate += Debug_Statistics::Get_Draw_Calls();
	s_sortedPolysSinceLastUpdate += Debug_Statistics::Get_Sorting_Polygons();

	Int64 freq64 = getPerformanceCounterFrequency();
	Int64 time64 = getPerformanceCounter();

	s_timeSinceLastUpdateInSecs = ((double)(time64 - s_lastUpdateTime64) / (double)(freq64));

#ifdef EXTENDED_STATS
		static FILE *pListFile = NULL;
		static Int64 lastFrameTime=0;
		static samples = 0;
		if (pListFile == NULL) {
			pListFile = fopen("FrameRateLog.txt", "w");
		}
		samples++;
		if (pListFile && lastFrameTime && samples<100) {
			float timeSinceLastFrame = (float)((double)(time64-lastFrameTime) / (double)(freq64));
			fprintf(pListFile, "%d ", (int)(1/timeSinceLastFrame));
		}
		lastFrameTime = time64;
#endif

	// we update stats on a delay
	const Real UPDATE_RATE_SECS = 2.0;
	if( s_timeSinceLastUpdateInSecs >= UPDATE_RATE_SECS || TheGlobalData->m_constantDebugUpdate )
	{	
		UnicodeString unibuffer, unibuffer2;
		UnicodeString fpsString;
			
		// setup texture stats
		Debug_Statistics::Record_Texture_Mode(Debug_Statistics::RECORD_TEXTURE_SIMPLE/*RECORD_TEXTURE_NONE*/);

		// frames per second	
		double fps = (Real)s_framesRenderedSinceLastUpdate / s_timeSinceLastUpdateInSecs;
		double drawsPerFrame = Debug_Statistics::Get_Draw_Calls(); //(Real)s_drawCallsSinceLastUpdate / (Real)s_framesRenderedSinceLastUpdate;
		double sortPolysPerFrame = Debug_Statistics::Get_Sorting_Polygons();  //(Real)s_sortedPolysSinceLastUpdate / (Real)s_framesRenderedSinceLastUpdate;
		double skinDrawsPerFrame = Debug_Statistics::Get_DX8_Skin_Renders();

		if (fps<0.1) fps = 0.1;

		double ms = 1000.0f/fps;


#if defined(_DEBUG) || defined(_INTERNAL)
		double cumuTime = ((double)(time64 - m_timerAtCumuFPSStart) / (double)(freq64));
		if (cumuTime < 0.0) cumuTime = 0.0;
		Int numFrames = (Int)TheGameLogic->getFrame() - (Int)START_CUMU_FRAME;
		double cumuFPS = (numFrames > 0 && cumuTime > 0.0) ? (numFrames / cumuTime) : 0.0;
		double skinPolysPerFrame = Debug_Statistics::Get_DX8_Skin_Polygons();

		Int LOD = TheGlobalData->m_terrainLOD;
		//unibuffer.format( L"FPS: %.2f, %.2fms mapLOD=%d [cumu FPS=%.2f] draws: %.2f sort: %.2f", fps, ms, LOD, cumuFPS, drawsPerFrame,sortPolysPerFrame);
		if (TheGlobalData->m_useFpsLimit) 
				unibuffer.format( u"%.2f/%d FPS, ", fps, TheGameEngine->getFramesPerSecondLimit());
		else
				unibuffer.format( u"%.2f FPS, ", fps);

		unibuffer2.format( u"%.2fms [cumuFPS=%.2f] draws: %d skins: %d sortP: %d skinP: %d LOD %d", ms, cumuFPS, (Int)drawsPerFrame,(Int)skinDrawsPerFrame,(Int)sortPolysPerFrame, (Int)skinPolysPerFrame, LOD);
		unibuffer.concat(unibuffer2);
#else
		//Int LOD = TheGlobalData->m_terrainLOD;
		//unibuffer.format( L"FPS: %.2f, %.2fms mapLOD=%d draws: %.2f sort %.2f", fps, ms, LOD, drawsPerFrame,sortPolysPerFrame);
		unibuffer.format( u"FPS: %.2f, %.2fms draws: %.2f skins: %.2f sort %.2f", fps, ms, drawsPerFrame,skinDrawsPerFrame,sortPolysPerFrame);
		if (TheGlobalData->m_useFpsLimit) 
		{
			unibuffer2.format(u", FPSLock %d",TheGlobalData->m_framesPerSecondLimit);
			unibuffer.concat(unibuffer2);
		}
#endif

		fpsString.format( u"FPS: %.2f", fps);
		m_benchmarkDisplayString->setText( fpsString );

		Int polyPerFrame = Debug_Statistics::Get_DX8_Polygons();

#ifdef EXTENDED_STATS
		static float gameOverheadMS = 0.0f;
		static float consoleMS = 0.0f;
		static float threeDOverheadMS = 0.0f;
		static float terrainMS = 0.0f;
		static float objectMS = 0.0f;
		static float overlapMS = 0.0f;
		static int  extendedStats = 0;
		const int SHOW_STATS_TIME=12; // show extended stats for 5 cycles == 10 seconds.
		static enum {disabled, sync, gameOverhead, console, threeDOverhead, terrain, objects, overlap, normal} statMode = disabled;

		if (statMode == sync) {
			extendedStats = SHOW_STATS_TIME;
			statMode = gameOverhead;
		} else if (statMode == gameOverhead) {
			gameOverheadMS = ms;
			statMode = console;
			DX8Wrapper::stats.m_disableTerrain = true;
			DX8Wrapper::stats.m_disableOverhead = true;
			DX8Wrapper::stats.m_disableWater = true;
			DX8Wrapper::stats.m_disableObjects = true;
			DX8Wrapper::stats.m_disableConsole = false;
			DX8Wrapper::stats.m_debugLinesToShow = 1;
		} else if (statMode == console) {
			consoleMS = ms;
			statMode = threeDOverhead;
			DX8Wrapper::stats.m_disableTerrain = true;
			DX8Wrapper::stats.m_disableOverhead = true;
			DX8Wrapper::stats.m_disableWater = true;
			DX8Wrapper::stats.m_disableObjects = true;
			DX8Wrapper::stats.m_disableConsole = true;
			DX8Wrapper::stats.m_debugLinesToShow = 1;
		} else if (statMode == threeDOverhead) {				 
			threeDOverheadMS = ms;
			statMode = terrain;
			DX8Wrapper::stats.m_disableTerrain = false;
			DX8Wrapper::stats.m_disableOverhead = true;
			DX8Wrapper::stats.m_disableWater = true;
			DX8Wrapper::stats.m_disableObjects = true;
			DX8Wrapper::stats.m_disableConsole = true;
			DX8Wrapper::stats.m_debugLinesToShow = 1;
		} else if (statMode == terrain) {
			terrainMS = ms;
			statMode = objects;
			DX8Wrapper::stats.m_disableOverhead = true;
			DX8Wrapper::stats.m_disableTerrain = true;
			DX8Wrapper::stats.m_disableWater = true;
			DX8Wrapper::stats.m_disableObjects = false;
			DX8Wrapper::stats.m_disableConsole = true;
			DX8Wrapper::stats.m_debugLinesToShow = 1;
		} else if (statMode == objects) {
			objectMS = ms;
			statMode = overlap;
			DX8Wrapper::stats.m_disableOverhead = false;
			DX8Wrapper::stats.m_disableTerrain = false;
			DX8Wrapper::stats.m_disableWater = false;
			DX8Wrapper::stats.m_disableObjects = false;
			DX8Wrapper::stats.m_disableConsole = true;
			DX8Wrapper::stats.m_sleepTime = (int)(terrainMS);
			DX8Wrapper::stats.m_debugLinesToShow = 1;
		} else if (statMode == overlap) {
			overlapMS = ms;
			statMode = normal;
			DX8Wrapper::stats.m_disableOverhead = false;
			DX8Wrapper::stats.m_disableTerrain = false;
			DX8Wrapper::stats.m_disableWater = false;
			DX8Wrapper::stats.m_disableObjects = false;
			DX8Wrapper::stats.m_disableConsole = true;
			DX8Wrapper::stats.m_sleepTime = 0;
			DX8Wrapper::stats.m_debugLinesToShow = 1;
		} else if (statMode == normal) {
			overlapMS = (ms + ((int)terrainMS) - overlapMS );
			statMode = disabled;
			extendedStats = SHOW_STATS_TIME;

			// Done collecting stats. Re-enable stuff
			DX8Wrapper::stats.m_disableConsole = false;
			DX8Wrapper::stats.m_debugLinesToShow = -1;
		} else if (!DX8Wrapper::stats.m_showingStats) {
			// start collecting extended info. 
			DX8Wrapper::stats.m_showingStats = true;
			DX8Wrapper::stats.m_disableOverhead = false;
			DX8Wrapper::stats.m_disableTerrain = true;
			DX8Wrapper::stats.m_disableWater = true;
			DX8Wrapper::stats.m_disableObjects = true;
			DX8Wrapper::stats.m_disableConsole = true;
			DX8Wrapper::stats.m_debugLinesToShow = 1;
			statMode = sync;
			gameOverheadMS = 0.0f;
			threeDOverheadMS = 0.0f;
			terrainMS = 0.0f;
			objectMS = 0.0f;
		}
		if (statMode != disabled) {
			unibuffer.format(u"FPS: %.2f, %.2fms - Collecting extended stats.", fps, ms);
		} else if (extendedStats>0) {
			extendedStats--;
			unibuffer.format( u"FPS: %.2f, %.2fms - OH %.2fms, Console %.2fms, 3D OH %.2fms, Terrain %.2fms, Obs %.2fms, CPU %.2fms", 
				fps, ms, gameOverheadMS, consoleMS, threeDOverheadMS, terrainMS, objectMS, overlapMS);
			if (extendedStats==SHOW_STATS_TIME-2) {
				char bufferA[ 256 ];
				sprintf( bufferA, "FPS: %.2f, %.2fms - OH %.2fms, Console %.2fms, 3D OH %.2fms, Terrain %.2fms, Obs %.2fms, CPU %.2fms\n", 
					fps, ms, gameOverheadMS, consoleMS, threeDOverheadMS, terrainMS, objectMS, overlapMS);
				::OutputDebugString(bufferA);
				if (pListFile) {
					fprintf(pListFile, "\n%s", bufferA);
				}				
				sprintf( bufferA, "Polygons: per frame %d, per second %d\n", polyPerFrame,
						(Int)(polyPerFrame*fps));
				::OutputDebugString(bufferA);
				if (pListFile) {
					fprintf(pListFile, "%s", bufferA);
					fflush(pListFile);
				}				
			}
		} 
 		if (pListFile) {
			fprintf(pListFile, "\nFPS: %.2f, %.2fms\n", fps, ms);
			fflush(pListFile);
		}				
		if (pListFile) {
			samples = 0;
			if (statMode != disabled) {
				fprintf(pListFile, "Stat%d-", statMode);
			} 
		}				

#endif
		// check for debug D3D
		Bool debugD3D=false;
#if defined(_WIN32)	// the Direct3D debug runtime is a registry switch, and Windows's
		RegistryClass registry ("Software\\Microsoft\\Direct3d");
		if (registry.Is_Valid ()) {
			if (registry.Get_Int ("LoadDebugRuntime", 0) == 1) {
				debugD3D = true;
			}
		}
#endif
		if (debugD3D) {
			unibuffer.concat(u", DEBUG D3D");
		}
#ifdef _DEBUG
		unibuffer.concat(u", DEBUG app");
#endif

		m_displayStrings[FPS]->setText( unibuffer );

		// Actual GameLogic frame number
		unibuffer.format(u"Frame: %d", TheGameLogic->getFrame());
		m_displayStrings[Frame]->setText( unibuffer );

		// polygons this frame	
		unibuffer.format( u"Polygons: per frame %d, per second %d", polyPerFrame,
				(Int)(polyPerFrame*fps));
		m_displayStrings[Polygons]->setText( unibuffer );

		// vertices this frame
		unibuffer.format( u"Vertices: %d", Debug_Statistics::Get_DX8_Vertices() );
		m_displayStrings[Vertices]->setText( unibuffer );		

		//
		// I'm adjusting the texture memory usage counter by subtracting 
		// out the terrain alpha texture (since it's really == terrain texture).
		//
		unibuffer.format( u"Video RAM: %d", Debug_Statistics::Get_Record_Texture_Size() - 1376256 );
		m_displayStrings[VideoRam]->setText( unibuffer );

		s_lastUpdateTime64 = time64;
		s_timeSinceLastUpdateInSecs = 0.0f;
		s_framesRenderedSinceLastUpdate = 0;
		s_drawCallsSinceLastUpdate = 0;
		s_sortedPolysSinceLastUpdate = 0;

		// terrain stats
		unibuffer.format( u"3-Way Blends: %d/%d, Shoreline Blends: %d/%d", TheTerrainRenderObject->getNumExtraBlendTiles(TRUE),
			TheTerrainRenderObject->getNumExtraBlendTiles(FALSE),
			TheTerrainRenderObject->getNumShoreLineTiles(TRUE),
			TheTerrainRenderObject->getNumShoreLineTiles(FALSE));
		m_displayStrings[TerrainStats]->setText( unibuffer );

		// misc debug info
		Coord3D camPos;
		TheTacticalView->getPosition(&camPos);
		Real zoom = TheTacticalView->getZoom();
		Real pitch = TheTacticalView->getPitch();
		Real FXPitch = TheTacticalView->getFXPitch();
		Real angle = TheTacticalView->getAngle();
		Real FOV = TheTacticalView->getFieldOfView();
		//Real desiredHeight = TheTacticalView->getHeightAboveGround();
		Real terrainHeight = TheTacticalView->getTerrainHeightUnderCamera();
		Real actualHeightAboveGround = TheTacticalView->getCurrentHeightAboveGround();

		unibuffer.format( u"Camera zoom: %g, pitch: %g/%g, yaw: %g, pos: %g, %g, %g, FOV: %g\n       Height above ground: %g Terrain height: %g",
												zoom,
												pitch,
												FXPitch,
												angle,
												camPos.x, camPos.y, camPos.z,
												FOV,
												/*
												zoom,
												pitch * 180.0f / PI,
												FXPitch * 180.0f / PI,
												angle * 180.0f / PI,
												camPos.x, camPos.y, camPos.z,
												FOV * 180.0f / PI,
												*/
												actualHeightAboveGround, terrainHeight );
		m_displayStrings[DebugInfo]->setText( unibuffer );

		// display the keyboard modifier and mouse states.
		unibuffer.format( u"States: " );
		if( TheKeyboard->isShift() )
		{
			unibuffer.concat( u"Shift(" );
			if( TheKeyboard->getModifierFlags() & KEY_STATE_LSHIFT )
			{
				unibuffer.concat( u"L" );
			}
			if( TheKeyboard->getModifierFlags() & KEY_STATE_RSHIFT )
			{
				unibuffer.concat( u"R" );
			}
			unibuffer.concat( u") " );
		}
		if( TheKeyboard->isCtrl() )
		{
			unibuffer.concat( u"Ctrl(" );
			if( TheKeyboard->getModifierFlags() & KEY_STATE_LCONTROL )
			{
				unibuffer.concat( u"L" );
			}
			if( TheKeyboard->getModifierFlags() & KEY_STATE_RCONTROL )
			{
				unibuffer.concat( u"R" );
			}
			unibuffer.concat( u") " );
		}
		if( TheKeyboard->isAlt() )
		{
			unibuffer.concat( u"Alt(" );
			if( TheKeyboard->getModifierFlags() & KEY_STATE_LALT )
			{
				unibuffer.concat( u"L" );
			}
			if( TheKeyboard->getModifierFlags() & KEY_STATE_RALT )
			{
				unibuffer.concat( u"R" );
			}
			unibuffer.concat( u") " );
		}

		const MouseIO *mouseStatus = TheMouse->getMouseStatus();

		if( mouseStatus->leftState )
		{
			unibuffer.concat( u"LMB " );
		}
		if( mouseStatus->middleState )
		{
			unibuffer.concat( u"MMB " );
		}
		if( mouseStatus->rightState )
		{
			unibuffer.concat( u"RMB " );
		}

		Object *object = NULL;
#if defined(_DEBUG) || defined(_INTERNAL)	//debug hack to view object under mouse stats
		Drawable *draw = 	TheTacticalView->pickDrawable(&TheMousePos, FALSE, (PickType)0xffffffff );
#else
		Drawable *draw = TheGameClient->findDrawableByID( TheInGameUI->getMousedOverDrawableID() );
#endif
		if( draw  )
			object = draw->getObject();
		if( object )
		{
			unibuffer2.format( u"Moused over object: %S (%d) ", object->getTemplate()->getName().str(), object->getID() );
			unibuffer.concat( unibuffer2 );
		}
		else
		{
			unibuffer.concat( u"Moused over object: TERRAIN " );
		}
		
		m_displayStrings[ KEY_MOUSE_STATES ]->setText( unibuffer );

		//display the x and y mouse coordinates
		const MouseIO *mouseIO = TheMouse->getMouseStatus();
		Coord3D worldPos;
		if( TheTacticalView->screenToTerrain(&mouseIO->pos, &worldPos) )
			unibuffer.format( u"Mouse position: screen: (%d, %d), world: (%g, %g, %g)", mouseIO->pos.x, mouseIO->pos.y,
				worldPos.x, worldPos.y, worldPos.z);
		else
			unibuffer.format( u"Mouse position: screen: (%d, %d), world: none", mouseIO->pos.x, mouseIO->pos.y);
		m_displayStrings[MousePosition]->setText( unibuffer );
		
		//display the number of particles in the world and being displayed on screen
		Int totalParticles = TheParticleSystemManager->getParticleCount();
		Int onScreenParticleCount = TheParticleSystemManager->getOnScreenParticleCount();
		unibuffer.format( u"Particles: %d in world, %d being displayed", totalParticles, onScreenParticleCount );
		m_displayStrings[Particles]->setText( unibuffer );

		//display the number of objects in the world
		UnsignedInt objCount = TheGameLogic->getObjectCount();
		UnsignedInt objScreenCount = TheGameClient->getRenderedObjectCount();

		unibuffer.format(u"Objects: %d in world, %d being displayed", objCount, objScreenCount );
		m_displayStrings[Objects]->setText( unibuffer );

		// Network incoming bandwidth stats
		if (TheNetwork != NULL) {
			unibuffer.format(u"IN: %.2f bytes/sec, %.2f packets/sec",
				TheNetwork->getIncomingBytesPerSecond(), TheNetwork->getIncomingPacketsPerSecond());
			m_displayStrings[NetIncoming]->setText( unibuffer );

			// Network outgoing bandwidth stats
			unibuffer.format(u"OUT: %.2f bytes/sec, %.2f packets/sec",
				TheNetwork->getOutgoingBytesPerSecond(), TheNetwork->getOutgoingPacketsPerSecond());
			m_displayStrings[NetOutgoing]->setText( unibuffer );

			// Network performance stats
			unibuffer.format(u"Run Ahead: %d, Net FPS: %d, Packet arrival cushion: %d",
				TheNetwork->getRunAhead(), TheNetwork->getFrameRate(), TheNetwork->getPacketArrivalCushion());
			m_displayStrings[NetStats]->setText( unibuffer );

			// Client frame rate averages for all players in the game.  This only works right for the packet router.
			unibuffer.clear();
			Int numPlayers = TheNetwork->getNumPlayers();
			for (Int i = 0; i < numPlayers; ++i) {
				UnicodeString tempstr;
				tempstr.format(u"%s: %d ", TheNetwork->getPlayerName(i).str(), TheNetwork->getSlotAverageFPS(i));
				unibuffer.concat(tempstr);
			}
			m_displayStrings[NetFPSAverages]->setText( unibuffer );
		} else {
//			unibuffer.format(L"IN: 0.0 bytes/sec, 0.0 packets/sec");
//			m_displayStrings[NetIncoming]->setText( unibuffer );

			// Network outgoing bandwidth stats
//			unibuffer.format(L"OUT: 0.0 bytes/sec, 0.0 packets/sec");
//			m_displayStrings[NetOutgoing]->setText( unibuffer );
      unibuffer.format(u"");
//			unibuffer.format(L"Network not present");
			m_displayStrings[NetOutgoing]->setText(unibuffer);
			m_displayStrings[NetIncoming]->setText(unibuffer);
			m_displayStrings[NetStats]->setText(unibuffer);
			m_displayStrings[NetFPSAverages]->setText( unibuffer );
		}

		// selected object info stats
		unibuffer.format( u"Select Info: '%d' drawables selected", TheInGameUI->getSelectCount() );
		


		//Sorry, guys. I need a special kluge here to get constantdebug results for angry mob.
		//Do no be cross with me.
		//if there is not exactly one drawable selected it will report on the moused-over drawable
		if (TheInGameUI->getSelectCount() == 1)
			draw = TheInGameUI->getFirstSelectedDrawable();


		if( draw )
		{
			Object *obj = draw->getObject();
			AsciiString objectName;

			objectName.set( "No-Name" );
			if( obj && obj->getName().isEmpty() == FALSE )
				objectName = obj->getName();

			unibuffer.format( u"Select Info: '%S'(%S) at (%.3f,%.3f,%.3f)",
												draw->getTemplate()->getName().str(),
												objectName.str(),
												draw->getPosition()->x,
												draw->getPosition()->y,
												draw->getPosition()->z
											);

			const PhysicsBehavior *physics = obj->getPhysics();
			PhysicsTurningType turnType = physics ? physics->getTurning() : TURN_NONE;

			const DrawableLocoInfo *locoInfo = draw->getLocoInfo();
			if( locoInfo )
			{
				unibuffer2.format( u"\nPhysics Info -- Turn: %d, Pitch(accel): %.3f(%.3f), Roll(accel): %.3f(%.3f)",
													 turnType,
													 locoInfo->m_accelerationPitch, locoInfo->m_accelerationPitchRate,
													 locoInfo->m_accelerationRoll, locoInfo->m_accelerationRollRate );
				unibuffer.concat( unibuffer2 );
			}



		
			

			// (gth) compute some stats about the rendering cost of this drawable
#if defined(_DEBUG) || defined(_INTERNAL)	
			RenderCost rcost;
			for (DrawModule** dm = draw->getDrawModules(); *dm; ++dm)
			{
				(*dm)->getRenderCost(rcost);
			}
			if (rcost.getDrawCallCount() > 0) 
			{
				unibuffer2.format( u"\ndraw calls: %d(+%d) sort meshes: %d skins: %d  bones: %d",rcost.getDrawCallCount(),rcost.getShadowDrawCount(),rcost.getSortedMeshCount(),rcost.getSkinMeshCount(),rcost.getBoneCount());
				unibuffer.concat( unibuffer2 );
			}
#endif

			unibuffer.concat( u"\nModelStates: " );
			ModelConditionFlags mcFlags = draw->getModelConditionFlags();
			const int numEntriesPerLine = 4;
			int lineCount = 0;

			for( int i = 0; i < MODELCONDITION_COUNT; i++ )
			{
				if( mcFlags.test( i ) )
				{
					unibuffer2.format( u"%S ", ModelConditionFlags::getBitNames()[ i ] );
					unibuffer.concat( unibuffer2 );
					lineCount++;
					if( lineCount == numEntriesPerLine )
					{
						lineCount = 0;
						unibuffer.concat( u"\n" );
					}
				}
			}

			//Render ALL modelcondition statii

		}  // end if
		m_displayStrings[ SelectedInfo ]->setText( unibuffer );

	}

}

// W3DDisplay::drawDebugStats =================================================
/** Draw debug statistics */
//=============================================================================
void W3DDisplay::drawDebugStats( void )
{
	Int	x = 3;
	Int	y = 3;
	Color textColor = GameMakeColor( 255, 255, 255, 255 );
	Color dropColor = GameMakeColor( 0, 0, 0, 255 );

	int linesOfStrings = DisplayStringCount;
#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_debugLinesToShow > -1) 
	{
		linesOfStrings = DX8Wrapper::stats.m_debugLinesToShow;
	}

#endif


	Int w, h;
	for (int i = 0; i < linesOfStrings; i++)
	{
		m_displayStrings[i]->draw( x, y, textColor, dropColor );
		m_displayStrings[i]->getSize(&w, &h);
		y += h;
	}

}  // end drawDebugStats

// W3DDisplay::drawFPSStats =================================================
/** Draw the FPS on the screen */
//=============================================================================
void W3DDisplay::drawFPSStats( void )
{
	Int	x = 3;
	Int	y = 20;
	Color textColor = GameMakeColor( 255, 255, 255, 255 );
	Color dropColor = GameMakeColor( 0, 0, 0, 255 );

	int linesOfStrings = 1;

	for (int i = 0; i < linesOfStrings; i++)
	{
		m_benchmarkDisplayString->draw( x, y, textColor, dropColor );
	}
}


//=============================================================================
void StatDebugDisplay( DebugDisplayInterface *, void *, FILE *fp )
{
	DEBUG_CRASH(("This should never be called directly, but is just a placeholder for drawDebugStats()"));
}

// W3DDisplay::drawCurrentDebugDisplay =================================================
/** Draw current debug display */
//=============================================================================
void W3DDisplay::drawCurrentDebugDisplay( void )
{
	if (m_debugDisplayCallback == StatDebugDisplay)
	{
		drawDebugStats();
	}
	else
	{
		if ( m_debugDisplay && m_debugDisplayCallback )
		{
			m_debugDisplay->reset();
			// third parameter is the optional dump-to-file the stat dumper uses; on
			// screen there is no file, so NULL - Zero Hour grew the typedef and
			// never updated this one call.
			m_debugDisplayCallback( m_debugDisplay, m_debugDisplayUserData, NULL );
		}
	}
}  // end drawCurrentDebugDisplay

// W3DDisplay::calculateTerrainLOD =================================================
/** Calculates an adequately speedy terrain Level Of Detail. */
//=============================================================================
void W3DDisplay::calculateTerrainLOD( void )
{
	const Int NUM_SAMPLES=20;
	const Int NUM_TO_DISCARD=5;
	
	Int64 freq64 = getPerformanceCounterFrequency();

	char buf[_MAX_PATH];
	float frameTime = 0;
	float maxTimeLimit = TheGlobalData->m_terrainLODTargetTimeMS/1000.0f;
	TerrainLOD goodLOD = TERRAIN_LOD_MIN;
	TerrainLOD curLOD = TERRAIN_LOD_AUTOMATIC;
	Int count = 0;
#ifdef _DEBUG
	// just go to TERRAIN_LOD_NO_WATER, mirror off.
	TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_NO_WATER;
	m_3DScene->drawTerrainOnly(false);
	TheTerrainRenderObject->adjustTerrainLOD(0);
	return;
#endif
	do {
		Int i;
		float timeForFrame=0;
		frameTime = 0;
		switch(curLOD) {
			default: curLOD = TERRAIN_LOD_DISABLE; break;
			case TERRAIN_LOD_AUTOMATIC: curLOD = TERRAIN_LOD_MAX; break;
			case TERRAIN_LOD_MAX: curLOD = TERRAIN_LOD_NO_WATER; break;
			case TERRAIN_LOD_HALF_CLOUDS: curLOD = TERRAIN_LOD_DISABLE; break;
			case TERRAIN_LOD_NO_WATER: curLOD = TERRAIN_LOD_HALF_CLOUDS; break;
		}
		if (curLOD == TERRAIN_LOD_DISABLE) {
			break;
		}
		TheWritableGlobalData->m_terrainLOD = curLOD;
		m_3DScene->drawTerrainOnly(true);
		TheTerrainRenderObject->adjustTerrainLOD(0);
		for (i=0; i<NUM_SAMPLES; i++) {
			Int64 startTime64 = getPerformanceCounter();
			// start render block
			updateViews();
			if (WW3D::Begin_Render( true, true, Vector3( 0.0f, 0.0f, 0.0f ) ) == WW3D_ERROR_OK)
			{	// draw all views of the world
				drawViews();
				// render is all done!
				WW3D::End_Render();
			}
			Int64 time64 = getPerformanceCounter();
			timeForFrame = (float)((double)(time64-startTime64) / (double)(freq64));
			sprintf(buf, "%.2fms ", timeForFrame*1000.0f);
#if defined(_WIN32)
			::OutputDebugString(buf);
#else
			DEBUG_LOG(("%s", buf));
#endif
			if (i>=NUM_TO_DISCARD) {
				frameTime += timeForFrame;
				if (i>NUM_TO_DISCARD+1 && 
					(timeForFrame / ((i+1)-NUM_TO_DISCARD)) > 2*maxTimeLimit) {
					i++;
					break;
				}
			}
		}
		frameTime /= ((i)-NUM_TO_DISCARD);
		count++;
		sprintf(buf, "\n LOD %d, time %.2fms\n", curLOD, frameTime*1000.0f);
#if defined(_WIN32)
		::OutputDebugString(buf);
#else
		DEBUG_LOG(("%s", buf));
#endif
		if (frameTime<maxTimeLimit && goodLOD<curLOD) {
			goodLOD = curLOD;
		}
		if (frameTime < maxTimeLimit) break;
	} while (count<10);

	TheWritableGlobalData->m_terrainLOD = goodLOD;
	m_3DScene->drawTerrainOnly(false);
	TheTerrainRenderObject->adjustTerrainLOD(0);
#ifdef _DEBUG
	DEBUG_ASSERTCRASH(count<10, ("calculateTerrainLOD") );
#endif

}


Real W3DDisplay::getAverageFPS()
{
	return m_averageFPS;
}

Int W3DDisplay::getLastFrameDrawCalls()
{
	return Debug_Statistics::Get_Draw_Calls();
}

DECLARE_PERF_TIMER(BigAssRenderLoop)

// W3DDisplay::draw ===========================================================
/** Draw the entire W3D Display */
//=============================================================================
DECLARE_PERF_TIMER(W3DDisplay_draw)
static Bool s_screenShotPending = FALSE;	// F12 pressed: save the frame at the end of the next draw()
static void saveScreenShot(void);
static void captureVideoFrame(void);
static Bool videoRecordingFrame(void);

#ifdef DEBUG_LOGGING
//
// The frame rate watchdog in GameEngine.cpp reports the renderer as one number.  These split it:
// the world, and the interface drawn on top of it.  A slow frame is one or the other and they have
// nothing to do with each other.
//
extern Real TheSceneDrawMS;
extern Real TheUIDrawMS;
extern Real TheParticleUpdateMS;
extern UnsignedInt TheSceneDrawCalls;

//=============================================================================
// R1, smooth motion (W3DSmoothMotion.h).  Three steps around the scene's render, and only the picture:
//   smoothMotionBegin    before updateViews: the blend's alpha, and on a new tick each drawable's position
//                        (the camera's lock follows the blended one)
//   smoothMotionApply    after the particles, before the render targets and the scene: on a new tick each
//                        model's transform is captured, then every model is shown blended
//   smoothMotionRestore  after the render loop: the logic transforms go back before picking and the logic
//=============================================================================
bool TheSmoothMotionActive = false;
float TheSmoothMotionAlpha = 1.0f;
static UnsignedInt s_smoothPositionFrame = 0xFFFFFFFFu;
static UnsignedInt s_smoothModelFrame = 0xFFFFFFFFu;
static Bool s_smoothApplied = FALSE;

static void smoothMotionBegin()
{
	// -recordfps's pictures between two logic frames are nothing but the blend, so it is on for them
	// whatever the option and the interface say, from the frame before the first so that one is captured
	TheSmoothMotionActive = !TheGlobalData->m_headless
		&& ((TheGlobalData->m_smoothMotion && !TheGlobalData->isClassicUI()) || GameEngine_videoBlends());
	TheSmoothMotionAlpha = TheSmoothMotionActive ? GameEngine_logicTickFraction() : 1.0f;
	if (!TheSmoothMotionActive)
		return;
	const UnsignedInt frame = TheGameClient->getFrame();
	if (frame == s_smoothPositionFrame)
		return;
	s_smoothPositionFrame = frame;
	for (Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable())
		draw->smoothMotionCapturePosition(frame);
}

static void smoothMotionApply()
{
	if (!TheSmoothMotionActive)
		return;
	const Int64 passStart = Clock_Ticks();
	const UnsignedInt frame = TheGameClient->getFrame();
	const Bool newTick = frame != s_smoothModelFrame;
	s_smoothModelFrame = frame;
	for (Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable())
	{
		DrawModule **modules = draw->getDrawModules();
		if (modules == NULL)
			continue;
		if (newTick)
		{
			const Bool marked = draw->isMotionDiscontinuous();
			for (DrawModule **dm = modules; *dm; ++dm)
				(*dm)->smoothMotionCapture(frame, marked);
			draw->clearMotionDiscontinuity();
			// The armed control of R1's proof, and nothing else: ZH_R1_LEAK=1 writes the blended position back
			// into the Object - exactly what smooth motion must never do - so a run with it has to end on a
			// different CRC than one without.  Never set outside that test.
			static const Bool leak = getenv("ZH_R1_LEAK") != NULL;
			Coord3D shown;
			if (leak && draw->getObject() != NULL && draw->getSmoothMotionPosition(0.5f, &shown))
				draw->getObject()->setPosition(&shown);
		}
		for (DrawModule **dm = modules; *dm; ++dm)
			(*dm)->smoothMotionApply(TheSmoothMotionAlpha);
		// With ZH_SMOOTH_MOTION_STATS, how far an aircraft's logic position - where its exhaust and its health
		// bar are, which stay on the ticks - leads the place it is drawn (a question about fast units).
		static const Bool stats = getenv("ZH_SMOOTH_MOTION_STATS") != NULL;
		Coord3D shown;
		if (stats && draw->getObject() != NULL && draw->getObject()->isKindOf(KINDOF_AIRCRAFT)
			&& draw->getSmoothMotionPosition(TheSmoothMotionAlpha, &shown))
		{
			const Coord3D *logic = draw->getPosition();
			const Real dx = logic->x - shown.x, dy = logic->y - shown.y, dz = logic->z - shown.z;
			const Real lead = sqrtf(dx * dx + dy * dy + dz * dz);
			s_aircraftLeadSum += lead;
			++s_aircraftLeadCount;
			if (lead > s_aircraftLeadMax)
				s_aircraftLeadMax = lead;
			const Real radius = draw->getObject()->getGeometryInfo().getBoundingSphereRadius();
			if (radius > s_aircraftRadiusMax)
				s_aircraftRadiusMax = radius;
		}
	}
	s_smoothApplied = TRUE;
	s_smoothPassTicks += Clock_Ticks() - passStart;
	++s_smoothPasses;
}

static void smoothMotionRestore()
{
	if (!s_smoothApplied)
		return;
	s_smoothApplied = FALSE;
	const Int64 passStart = Clock_Ticks();
	for (Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable())
	{
		DrawModule **modules = draw->getDrawModules();
		if (modules == NULL)
			continue;
		for (DrawModule **dm = modules; *dm; ++dm)
			(*dm)->smoothMotionRestore();
	}
	s_smoothPassTicks += Clock_Ticks() - passStart;
}

static Real w3dElapsedMS( const Int64 &from, const Int64 &to )
{
	Int64 freq;
	freq = Clock_Ticks_Per_Second();
	if( freq < 1 )
		return 0.0f;
	return (Real)((double)(to - from) * 1000.0 / (double)freq);
}
#endif

void W3DDisplay::draw( void )
{
	USE_PERF_TIMER(W3DDisplay_draw)
	static UnsignedInt syncTime = 0;

	extern RenderWindow ApplicationHWnd;	// WinMain's HWND on Windows
#if defined(_WIN32)	// off Windows a minimised window is C2's to report
	if (ApplicationHWnd && ::IsIconic(ApplicationHWnd)) {
		// A network game keeps its logic running while minimized (Win32GameEngine::update), and
		// the particle update below is the only thing that retires a particle system, so skipping
		// it with the draw stacked up every system the match made in the meantime, all of them to
		// start emitting at once on return (TheSuperHackers #2709 reproduce it with Propaganda
		// Towers).  Same bookkeeping as the lost-device branch below.
		TheParticleSystemManager->update();
		return;
	}
#endif

	// -nodevice: there is no device to begin a scene on.  The load screen asks for a draw of its
	// own while a map loads, so this is reached before the first frame and not only from the loop.
	if (TheGlobalData && TheGlobalData->m_noRenderDevice) {
		return;
	}

	updateAverageFPS();
	if (TheGlobalData->isDynamicLODEnabled() && TheGameLogic->getShowDynamicLOD())
	{
		DynamicGameLODLevel lod=TheGameLODManager->findDynamicLODLevel(m_averageFPS);
		TheGameLODManager->setDynamicLODLevel(lod);
	}
	else
	{	//if dynamic LOD is turned off, force highest LOD
		TheGameLODManager->setDynamicLODLevel(DYNAMIC_GAME_LOD_VERY_HIGH);
	}

	if (TheGlobalData->m_terrainLOD == TERRAIN_LOD_AUTOMATIC && TheTerrainRenderObject) 
	{
		calculateTerrainLOD();
	}
#ifdef EXTENDED_STATS
AGAIN:
#endif

#ifdef DUMP_PERF_STATS
	if( TheGlobalData->m_dumpPerformanceStatistics )
	{
		TheStatDump.dumpStats( FALSE, TRUE );
		TheWritableGlobalData->m_dumpPerformanceStatistics = FALSE;
	}
  //The <= GAME_REPLAY essentially means, GAME_SINGLE_PLAYER || GAME_LAN || GAME_SKIRMISH || GAME_REPLAY
  else if ( TheGlobalData->m_dumpStatsAtInterval && TheGameLogic->getGameMode() <= GAME_REPLAY )
  {
    Int interval = TheGlobalData->m_statsInterval;
    if ( TheGameLogic->getFrame() > 0 && (TheGameLogic->getFrame() % interval) == 0 )
    {
  	  TheStatDump.dumpStats( TRUE, TRUE );
    	TheInGameUI->message( UnicodeString( u"-stats is running, at interval: %d." ), TheGlobalData->m_statsInterval );
    }
  }




#endif

	// compute debug statistics for display later
	if ( m_debugDisplayCallback == StatDebugDisplay 
#if defined(_DEBUG) || defined(_INTERNAL)
				|| TheGlobalData->m_benchmarkTimer > 0
#endif
			)
	{
		gatherDebugStats();
	}
#ifdef EXTENDED_STATS
	else 
	{
		DX8Wrapper::stats.m_showingStats = false;
	}
#endif

#ifdef SAMPLE_DYNAMIC_LIGHT
	Vector3 loc;
	loc = theDynamicLight->Get_Position();
	loc.X += theLightXOffset;
	if(loc.X>128) theLightXOffset = -theLightXOffset;
	if(loc.X<0) theLightXOffset = -theLightXOffset;
	loc.Y += theLightYOffset;
	if(loc.Y>128) theLightYOffset = -theLightYOffset;
	if(loc.Y<0) theLightYOffset = -theLightYOffset;
	theDynamicLight->Set_Position(loc);
#endif


	/// @todo Make more explicit drawing layers(ground, ground UI, objects, object UI, overlay UI)

	///@todo: Ask Vegas why the LOD optimizer hangs particle system.
 	//
  	// Predictive LOD optimizer optimizes the mesh LOD levels to match 
  	// the given polygon budget
  	//
	//PredictiveLODOptimizerClass::Optimize_LODs( 5000 );

	// Only a scripted frozen-time camera pan may hold us inside the render do-loop
	// below. The "client outran logic" case (lastFrame == getFrame()) must NOT: with
	// rendering decoupled from the logic tick that is the common case, and looping on
	// it kept draw() spinning - message pump dead - until the shell map's camera
	// movement of the moment finished.
	Bool loopForCameraMovement = TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished()
		&& !TheScriptEngine->isTimeFrozenDebug() && !TheScriptEngine->isTimeFrozenScript()
		&& !TheGameLogic->isGamePaused();

	Bool freezeTime = TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished();
	freezeTime = freezeTime || TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript();
	freezeTime = freezeTime || TheGameLogic->isGamePaused();

	// hack to let client spin fast in network games but still do effects at the same pace. -MDC
	// A -recordfps picture is a step of its own: the clock moves on each picture of a logic frame and
	// stands still on a pane's second draw of one
	static UnsignedInt lastFrame = ~0;
	const UnsignedInt picture = TheGameClient->getFrame() * GameEngine_videoPictures() + GameEngine_videoPicture();
	freezeTime = freezeTime || (lastFrame == picture);
	lastFrame = picture;

	/// @todo: I'm assuming the first view is our main 3D view.
	W3DView *primaryW3DView=(W3DView *)getFirstView();
	if (!freezeTime && TheScriptEngine->isTimeFast())
	{
		primaryW3DView->updateCameraMovements();  // Update camera motion effects.
		syncTime += TheW3DFrameLengthInMsec;
		return;
	}

	Debug_Statistics::Begin_Statistics();	//reset all counters (polygons, vertices, etc) before drawing

	//update state of all the terrain tracks (fade, remove, etc.)
	/// @todo: Is there a better place to put per-frame updates like this?

	if(TheGlobalData->m_loadScreenRender != TRUE)
	{

		if (TheTerrainTracksRenderObjClassSystem)
			TheTerrainTracksRenderObjClassSystem->update();

		//Shroud data is needed to render all other views, so handle this first.
		if (TheTerrainRenderObject)
		{	
			//update the shroud surface here since it may be needed by reflections
			if (TheTerrainRenderObject->getMap())	//make sure a valid map is loaded into terrain.
			{
				if (TheTerrainRenderObject->getShroud())
				{
					TheTerrainRenderObject->getShroud()->render(primaryW3DView->get3DCamera());
				}
			}
		}
	}

	if (!freezeTime)
	{
		//
		// This is W3D's clock: every skeletal animation, texture flipbook and W3D-driven effect
		// reads its time from it. EA advanced it a constant TheW3DFrameLengthInMsec per rendered
		// frame - the @todo below - which is only the right answer while the renderer is pinned to
		// 30fps. It is not any more, so advance it by the real time that has actually passed and
		// the animations keep their authored speed at whatever frame rate the machine manages.
		//
		static UnsignedInt prevSyncMs = 0;
		const UnsignedInt nowSyncMs = Clock_Milliseconds();
		if (prevSyncMs == 0)
			prevSyncMs = nowSyncMs;

		UnsignedInt deltaMs = nowSyncMs - prevSyncMs;
		prevSyncMs = nowSyncMs;
		if (deltaMs > TheW3DFrameLengthInMsec * 4)
			deltaMs = TheW3DFrameLengthInMsec * 4;	// a level load must not fast-forward everything
		// a recorded picture is its share of a logic frame however long it took to draw and save, so the
		// clock moves that much too, or the footage plays every animation fast
		if (videoRecordingFrame())
		{
			const Int pictures = GameEngine_videoPictures();
			const Int shown = GameEngine_videoPicture();
			deltaMs = TheW3DFrameLengthInMsec * (shown + 1) / pictures - TheW3DFrameLengthInMsec * shown / pictures;
		}

		syncTime += deltaMs;
		// allow W3D to update its internals
		//	WW3D::Sync( GetTickCount() );
	}
	WW3D::Sync( syncTime );

	// Fast & Frozen time limits the time to 33 fps.
	Int minTime = 30;
	static Int prevTime = Clock_Milliseconds(), now;

	now=Clock_Milliseconds();
	if (TheTacticalView->getTimeMultiplier()>1) 
	{
		static Int timeMultiplierCounter = 1;
		timeMultiplierCounter--;
		if (timeMultiplierCounter>1) 
			return;
		timeMultiplierCounter = TheTacticalView->getTimeMultiplier();
		// limit the framerate, because while fast time is on, the game logic is running as fast as it can.
	}	
	else 
	{
		now = Clock_Milliseconds();
		prevTime = now - minTime;		 // do the first frame immediately.
	} 


	do {
		
		{
			if(TheGlobalData->m_loadScreenRender != TRUE)
			{
			
				//
				// The frame rate is no longer capped: this used to hold every frame to 33fps
				// whenever m_useFpsLimit was set, which is any game started from the menus, and
				// everything that rode on the render rate has been put on a clock of its own
				// instead. The scripted-camera loop below is the exception - it advances the
				// camera one step per iteration and has no clock, so a cinematic pan would run
				// through in a few milliseconds. That one still gets paced.
				//
				while(loopForCameraMovement && (now - prevTime) < minTime-1)
				{
					sleepMilliseconds(1);	// ::Sleep on Windows; was a pure spin; this loop can run for whole camera pans
					now = Clock_Milliseconds();
				}
				prevTime = now;
			}
		}

		// update all views of the world - recomputes data which will affect drawing
		if (DX8Wrapper::_Get_D3D_Device() && (DX8Wrapper::_Get_D3D_Device()->TestCooperativeLevel()) == D3D_OK)
		{	//Checking if we have the device before updating views because the heightmap crashes otherwise while
			//trying to refresh the visible terrain geometry.
			smoothMotionBegin();
//			if(TheGlobalData->m_loadScreenRender != TRUE)
				updateViews();
#ifdef DEBUG_LOGGING
			Int64 tParticleStart, tParticleEnd;
			tParticleStart = Clock_Ticks();
#endif
     		TheParticleSystemManager->update();//LORENZEN AND WILCZYNSKI MOVED THIS FROM ITS NATIVE POSITION, ABOVE
                                           //FOR THE PURPOSE OF LETTING THE PARTICLE SYSTEM LOOK UP THE RENDER OBJECT"S
                                           //TRANSFORM MATRIX, WHILE IT IS STILL VALID (HAVING DONE ITS CLIENT TRANSFORMS
                                           //BUT NOT YET RESETTING TOT HE LOGICAL TRANSFORM)
                                           //THE RESULT IS THAT PARTICLESYSTEMS LINKED TO BONES IN DRAWABLES.OBJECTS
                                           //MOVE WITH THE CLIENT TRANSFORMS, NOW.
                                           //REVOLUTIONARY!
                                           //-LORENZEN

#ifdef DEBUG_LOGGING
			tParticleEnd = Clock_Ticks();
			TheParticleUpdateMS = w3dElapsedMS( tParticleStart, tParticleEnd );
#endif

			// R1: after the particles read the logic bones (Lorenzen's note above), before anything renders.
			smoothMotionApply();

			// After the smooth motion, so a lamp's light sits where its model is drawn this frame.
			if (primaryW3DView && primaryW3DView->get3DCamera())
				W3DModelDraw::lightHeadlights(primaryW3DView->get3DCamera()->Get_Position());

			if (TheWaterRenderObj)
				TheWaterRenderObj->updateRenderTargetTextures(primaryW3DView->get3DCamera());	//do a render into each texture

			//Can't render into textures while rendering to screen so these textures need to be updated
			//before we enter main rendering loop.
			if (TheW3DProjectedShadowManager)
				TheW3DProjectedShadowManager->updateRenderTargetTextures();
		}
		else
		{
			//
			// The particle manager's update is bookkeeping, not drawing: it ages particles, retires
			// the systems that have finished and frees them.  Skipping it along with the rendering
			// meant that for as long as the device was gone the game went on creating particle
			// systems and destroyed none.  Locking the Windows session is the reliable way to see
			// it - a hundred rocket buggies firing at the ground push that list into six figures in
			// under a minute, every walk of it gets slower as it grows, and what you unlock to is a
			// game that has either run out of memory or stopped responding.
			//
			// Above it runs right after updateViews on purpose (see Lorenzen's note), so the systems
			// read the client transform while it is still valid.  There is no updateViews here and
			// nothing to draw the result of, so a frame's worth of stale transforms costs nothing.
			//
			TheParticleSystemManager->update();
		}

		Debug_Statistics::End_Statistics();	//record number of polygons rendered in RenderTargetTextures.

		//Store number of polygons rendered in renderTargetTextures.
		Int numRenderTargetPolygons=Debug_Statistics::Get_DX8_Polygons();
		Int numRenderTargetVertices=Debug_Statistics::Get_DX8_Vertices();

		// start render block
		#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
    if ( (TheGameLogic->getFrame() % 30 == 1) || ( ! ( !TheGameLogic->isGamePaused() && TheGlobalData->m_TiVOFastMode) ) )
		#else
	    if ( (TheGameLogic->getFrame() % 30 == 1) || ( ! (!TheGameLogic->isGamePaused() && TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame())) )
    #endif
		{
			USE_PERF_TIMER(BigAssRenderLoop)
			static Bool couldRender = true;
			if ((TheGlobalData->m_breakTheMovie == FALSE) && (TheGlobalData->m_disableRender == false) && WW3D::Begin_Render( true, true, Vector3( 0.0f, 0.0f, 0.0f ), TheWaterTransparency->m_minWaterOpacity ) == WW3D_ERROR_OK)		
			{
				
				if(TheGlobalData->m_loadScreenRender == TRUE)
				{	
					TheInGameUI->draw();
					if( TheMouse )
						TheMouse->draw();	//keep applying the current cursor style so it remains hidden if needed.
					WW3D::End_Render();	
					continue;
				}
				couldRender = true;
				// add the number of verts/polygons drawn before the main scene
				if (numRenderTargetPolygons || numRenderTargetVertices)
					Debug_Statistics::Record_DX8_Polys_And_Vertices(numRenderTargetPolygons,numRenderTargetVertices,ShaderClass::_PresetOpaqueShader);

				// draw all views of the world
#ifdef DEBUG_LOGGING
				Int64 tSceneStart, tSceneEnd, tUIEnd;
				tSceneStart = Clock_Ticks();
#endif
				drawViews();
#ifdef DEBUG_LOGGING
				tSceneEnd = Clock_Ticks();
#endif

				// W3DView::draw has normally run the chain already, at the point where the world
				// was finished and before the health bars went over it.  This is the frame that
				// drew no view at all: without it the scene would sit in the offscreen texture with
				// the command bar drawn over a black screen.
				Direct3D11_Finish_Scene();

				// draw the user interface
				TheInGameUI->DRAW();
#ifdef DEBUG_LOGGING
				tUIEnd = Clock_Ticks();
				TheSceneDrawMS = w3dElapsedMS( tSceneStart, tSceneEnd );
				TheUIDrawMS = w3dElapsedMS( tSceneEnd, tUIEnd );
				TheSceneDrawCalls = DX8Wrapper::Get_Draw_Calls();
#endif

				// end of video example code

				//
				// The movie goes down before the cursor, not after it. Drawn the other way round a
				// full screen video covered the pointer, so through the intro and every in-game
				// cinematic there was nothing on screen to aim the click with.
				//
				if ( m_videoStream && m_videoBuffer )
				{
					drawVideoBuffer( m_videoBuffer, 0, 0, getWidth(), getHeight() );
				}

				// draw the mouse
				if( TheMouse )
					TheMouse->DRAW();

				if( m_copyrightDisplayString )
				{
					Int x, y, dX, dY;
					m_copyrightDisplayString->getSize(&dX, &dY);
					x = (getWidth() / 2) - (dX /2);
					y = getHeight()  - dY - 20 ;
					m_copyrightDisplayString->draw(x, y, GameMakeColor(0,0,0,255), GameMakeColor(0,0,0,0),0,0);
				}
				// render letter box before debug display so debug info isn't hidden
				renderLetterBox(now);

				// display cinematicText over the black
				if( m_cinematicText != AsciiString::TheEmptyString && m_cinematicTextFrames != 0)
				{
					DisplayString *displayString = TheDisplayStringManager->newDisplayString();

					// set word wrap if neccessary

					Int wordWrapWidth = TheDisplay->getWidth() - 20;
					displayString->setWordWrap( wordWrapWidth );
					displayString->setWordWrapCentered( TRUE );

					UnicodeString text;
					text.translate( m_cinematicText );
					displayString->setText( text );
					Color color = GameMakeColor( 255, 255, 255, 255 );  // white
					Color backColor = GameMakeColor( 0, 0, 0, 0 );      // black
					displayString->setFont( m_cinematicFont );
					Int height = TheDisplay->getHeight() * .9;

					Int width;
					if( displayString->getWidth() > TheDisplay->getWidth() )
						width = 20;
					else
						width = ( TheDisplay->getWidth() - displayString->getWidth() ) / 2;
					displayString->draw( width, height, color, backColor );

					m_cinematicTextFrames--;
				}

				if ( m_debugDisplayCallback )
				{
					// draw the current debug display
					drawCurrentDebugDisplay();
				}

#if defined(_DEBUG) || defined(_INTERNAL)
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					drawFPSStats();
				}
#endif


#if defined(_DEBUG) || defined(_INTERNAL)
				if (TheGlobalData->m_debugShowGraphicalFramerate)
				{
					drawFramerateBar();
				}
#endif

#ifdef PERF_TIMERS
				TheGraphDraw->render();
				TheGraphDraw->clear();
#endif
				// last of the 2D overlays, so the console covers everything it drops over
				if( TheGameConsole )
					TheGameConsole->render();

				if (s_screenShotPending)
				{
					s_screenShotPending = FALSE;
					saveScreenShot();
				}
				captureVideoFrame();
				// render is all done!  -directorrecord's second fight is for the recording only and is
				// never presented: the frame drawn next over it is
				WW3D::End_Render(!TheObserverCamera.isDrawingSecond());

				/* A pipeline is compiled and a texture copied the first time it reaches a Direct3D 11
					 draw, which is the frame a new explosion first shows up on.  A frame that spent more
					 than a few milliseconds doing that says so, with the logic frame to set beside the
					 SLOW PASS line it caused. */
				{
					const double DX11_FRAME_COST_REPORT_MS = 5.0;
					double pipelineMS = 0.0;
					unsigned pipelines = 0;
					double textureMS = 0.0;
					unsigned textures = 0;
					unsigned depthCopies = 0;
					Direct3D11_Take_Frame_Cost( pipelineMS, pipelines, textureMS, textures, depthCopies );
					if( pipelineMS + textureMS > DX11_FRAME_COST_REPORT_MS )
						DEBUG_LOG(("DX11 FRAME COST frame %d: %u pipelines built in %.1f ms, %u textures copied in %.1f ms, %u soft particle depth copies\n",
							TheGameLogic->getFrame(), pipelines, pipelineMS, textures, textureMS, depthCopies));

					long presentResult = 0;
					if( Direct3D11_Take_Present_Failure( presentResult ) )
						DEBUG_LOG(("DX11 PRESENT refused at frame %d: 0x%08X, logged once\n",
							TheGameLogic->getFrame(), (UnsignedInt)presentResult));
				}

				/* End_Render is where a lost device is noticed and reset, and that reset is the most
					 dangerous thing this process does: it hands every default-pool resource back and asks
					 the driver to rebuild the swap chain.  WW3D2 is built without RELEASE_DEBUG_LOGGING, so its own
					 "Resetting device" line does not exist in a shipping build and the log went straight
					 from an ordinary frame to a crash dump.  This is on the GameEngineDevice side of the
					 fence, where logging is compiled in. */
				{
					static Bool s_wasDeviceLost = FALSE;
					const Bool lost = DX8Wrapper::Is_Device_Lost();
					if (lost != s_wasDeviceLost)
					{
						s_wasDeviceLost = lost;
						DEBUG_LOG(("Direct3D device %s\n",
							lost ? "LOST - End_Render will try to reset it" : "recovered"));
					}
				}
			}
			else
			{
				if (couldRender)
				{
					couldRender = false;
					DEBUG_LOG(("Could not do WW3D::Begin_Render()!  Are we ALT-Tabbed out?\n"));
				}
			}
		}
					
		if (TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript() || TheGameLogic->isGamePaused())
		{
			freezeTime = false; // We're frozen for debug or for pause, and need to continue out of the loop.
		}

		// A scripted pan holds us in here for seconds - keep the window responsive
		// (same pattern as LoadScreen::update).
		if (loopForCameraMovement)
			TheGameEngine->serviceWindowsOS();

	} while (loopForCameraMovement && !TheTacticalView->isCameraMovementFinished());

	// R1: the logic transforms back on every model, before picking (the message stream) and the logic.
	smoothMotionRestore();

#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableOverhead) {
		goto AGAIN;
	}
#endif
}  // end draw

#define LETTER_BOX_FADE_TIME	1000.0f		///1000 ms.

/** Render letter-box border at top/bottom of display
*/
void W3DDisplay::renderLetterBox(UnsignedInt currentTime)
{
		if (m_letterBoxEnabled)
		{	if (m_letterBoxFadeLevel != 1.0f)
			{
				m_letterBoxFadeLevel = (currentTime - m_letterBoxFadeStartTime)/LETTER_BOX_FADE_TIME;
				if (m_letterBoxFadeLevel > 1.0f)
					m_letterBoxFadeLevel = 1.0f;
			}

			UnsignedInt lbcolor = (Int)(m_letterBoxFadeLevel * 255.0f) << 24;

#ifdef SLIDE_LETTERBOX
			Int height = (Int)(getHeight() * 0.12f * m_letterBoxFadeLevel);
			TheTacticalView->setOrigin(0, height);
#else
			drawFillRect( 0, 0, m_width, (m_height-(m_width / m_letterBoxAspect))*0.5f, lbcolor );
			drawFillRect( 0, m_height-(m_height-(m_width / m_letterBoxAspect))*0.5f, m_width, m_height, lbcolor );
#endif
		}
		else
		{	//letter box is disabled, but may still be fading out
			if (m_letterBoxFadeLevel != 0.0f)
			{
				m_letterBoxFadeLevel = 1.0f - (currentTime - m_letterBoxFadeStartTime)/LETTER_BOX_FADE_TIME;
				if (m_letterBoxFadeLevel < 0.0f)
					m_letterBoxFadeLevel = 0.0f;

				UnsignedInt lbcolor = (Int)(m_letterBoxFadeLevel * 255.0f) << 24;

#ifdef SLIDE_LETTERBOX
				Int height = (Int)(getHeight() * 0.12f * m_letterBoxFadeLevel);
				TheTacticalView->setOrigin(0, height);
#else
				drawFillRect( 0, 0, m_width, (m_height-(m_width / m_letterBoxAspect))*0.5f, lbcolor );
				drawFillRect( 0, m_height-(m_height-(m_width / m_letterBoxAspect))*0.5f, m_width, m_height, lbcolor );
#endif
			}
			else
			{	//box has finished fading out
#ifdef SLIDE_LETTERBOX
				TheTacticalView->setOrigin(0, 0);
#else
				m_letterBoxEnabled = FALSE;
#endif
			}
		}
}

Bool W3DDisplay::isLetterBoxFading(void)
{
	if (m_letterBoxEnabled && m_letterBoxFadeLevel != 1.0f)
		return TRUE;
	if (!m_letterBoxEnabled && m_letterBoxFadeLevel != 0.0f)
		return TRUE;
	return FALSE;
}

//WST 10/2/2002 added query function.  JSC Integrated 5/20/03
Bool W3DDisplay::isLetterBoxed(void)
{
	return (m_letterBoxEnabled);
}

// W3DDisplay::createLightPulse ===============================================
/** Create a "light pulse" which is a dynamic light that grows, decays 
	* and vanishes over several frames */
//=============================================================================
void W3DDisplay::createLightPulse( const Coord3D *pos, const RGBColor *color, 
																	 Real innerRadius, Real attenuationWidth, 
																	 UnsignedInt increaseFrameTime, 
																	 UnsignedInt decayFrameTime//, Bool donut
																	 )
{
	if (innerRadius+attenuationWidth<2.0*PATHFIND_CELL_SIZE_F + 1.0f) {
		return; // it basically won't make any visual difference.  jba.
	}
	W3DDynamicLight * theDynamicLight = m_3DScene->getADynamicLight();
	// turn it on.
	theDynamicLight->setEnabled(true);

	theDynamicLight->Set_Ambient( Vector3( color->red, color->green, color->blue ) );
	theDynamicLight->Set_Diffuse( Vector3( color->red, color->green, color->blue) );
	theDynamicLight->Set_Position(Vector3(pos->x, pos->y, pos->z));
	theDynamicLight->Set_Far_Attenuation_Range(innerRadius, innerRadius + attenuationWidth);
	theDynamicLight->setFrameFade(increaseFrameTime, decayFrameTime);
	theDynamicLight->setDecayRange();
	theDynamicLight->setDecayColor();
	//theDynamicLight->setDonut(donut);
	// (gth) CNC3 enable far attenuation.  C&C3 defaults to disabled.  Must enable to match Generals. MW 8-06-03
	theDynamicLight->Set_Flag(LightClass::FAR_ATTENUATION,true);
}

void W3DDisplay::toggleLetterBox(void)
{
	m_letterBoxEnabled = !m_letterBoxEnabled;
	m_letterBoxFadeStartTime = Clock_Milliseconds();

	//WST  9/18/2002 This is not a script api to prevent cheat. JSC Integrated 5/20/03
	if( TheTacticalView )
	{
		TheTacticalView->setZoomLimited( !m_letterBoxEnabled );
	}  
}

void W3DDisplay::enableLetterBox(Bool enable)
{
	if (enable)
	{
		if (!m_letterBoxEnabled)
		{	//letterbox mode not previously enabled
			m_letterBoxEnabled = TRUE;
			m_letterBoxFadeStartTime = Clock_Milliseconds();

			//WST  9/18/2002 - This is not a script api to prevent cheat.  JSC Integrated 5/20/03
			if( TheTacticalView )
			{
				TheTacticalView->setZoomLimited( 0 );
			}  
		}
	}
	else
	{
		if (m_letterBoxEnabled)
		{	//letterbox mode no previously disabled
			m_letterBoxEnabled = FALSE;
			m_letterBoxFadeStartTime = Clock_Milliseconds();

			//WST  9/18/2002. JSC Integrated 5/20/03
			if( TheTacticalView )
			{
				TheTacticalView->setZoomLimited( 1 );
			}
		}
	}
}

// W3DDisplay::setTimeOfDay ===================================================
/** */
//=============================================================================
void W3DDisplay::setTimeOfDay( TimeOfDay tod )
{
	const GlobalData::TerrainLighting *ol=&TheGlobalData->m_terrainObjectsLighting[tod][0];

	if( m_3DScene )
	{
		m_3DScene->Set_Ambient_Light( Vector3(ol->ambient.red, ol->ambient.green, ol->ambient.blue) );
	}

	for (Int i=0; i<LightEnvironmentClass::MAX_LIGHTS; i++)
	{
		if( m_myLight[i] )
		{
			ol=&TheGlobalData->m_terrainObjectsLighting[tod][i];

			m_myLight[i]->Set_Ambient( Vector3( 0.0f, 0.0f, 0.0f ) );
			m_myLight[i]->Set_Diffuse( Vector3(ol->diffuse.red, ol->diffuse.green, ol->diffuse.blue ) );
			m_myLight[i]->Set_Specular( Vector3(0,0,0) );
			Matrix3D mtx;
			mtx.Set(Vector3(1,0,0), Vector3(0,1,0), Vector3(ol->lightPos.x, ol->lightPos.y, ol->lightPos.z), Vector3(0,0,0));
			m_myLight[i]->Set_Transform(mtx);
		}
	}
	if(TheTerrainRenderObject) {
		TheTerrainRenderObject->setTimeOfDay(tod);
		TheTacticalView->forceRedraw();
	}
}

// W3DDisplay::setup2D ========================================================
/** Get the 2D renderer ready for the geometry a drawing call is about to add.  Returns TRUE when
	* the renderer was emptied for it and the caller has to set its state up - the texture it wants,
	* or none - and FALSE when a batch is already open in exactly that state and the call may simply
	* add to it.  Whatever was waiting in a state this call cannot share is drawn first, so nothing
	* ever changes the order things appear in. */
//=============================================================================
Bool W3DDisplay::setup2D( const Image *image, Bool batchable )
{

	if( m_batch2D && batchable && m_batch2DOpen && m_batch2DKeepOpen && m_batch2DImage == image )
		return FALSE;		// what is waiting was set up for exactly this: add to it

	flush2D();

	m_batch2DImage = image;
	m_batch2DKeepOpen = batchable;
	m_batch2DOpen = TRUE;
	m_2DRender->Reset();
	return TRUE;

}  // end setup2D

// W3DDisplay::finish2D =======================================================
/** The geometry is in.  Outside a batch - and for the calls whose renderer state nobody else can
	* share - that means draw it now, which is what every one of these calls always did. */
//=============================================================================
void W3DDisplay::finish2D( void )
{

	if( m_batch2D == FALSE || m_batch2DKeepOpen == FALSE )
		flush2D();

}  // end finish2D

// W3DDisplay::flush2D ========================================================
/** Draw whatever is waiting in the 2D renderer. */
//=============================================================================
void W3DDisplay::flush2D( void )
{

	if( m_batch2DOpen )
	{
		m_2DRender->Render();
		m_batch2DOpen = FALSE;
		m_batch2DKeepOpen = FALSE;
		m_batch2DImage = NULL;
	}

}  // end flush2D

// W3DDisplay::beginBatch2D ===================================================
//=============================================================================
void W3DDisplay::beginBatch2D( void )
{

	flush2D();
	m_batch2D = TRUE;

}  // end beginBatch2D

// W3DDisplay::endBatch2D =====================================================
//=============================================================================
void W3DDisplay::endBatch2D( void )
{

	flush2D();
	m_batch2D = FALSE;

}  // end endBatch2D

// W3DDisplay::flushBatch2D ===================================================
/** draw what the batch is holding, for a caller that is about to draw around it */
//=============================================================================
void W3DDisplay::flushBatch2D( void )
{

	flush2D();

}  // end flushBatch2D

// W3DDisplay::drawLine =======================================================
/** draw a line on the display in pixel coordinates with the specified color */
//=============================================================================
void W3DDisplay::drawLine( Int startX, Int startY,
													 Int endX, Int endY,
													 Real lineWidth,
													 UnsignedInt lineColor )
{

	if( setup2D( NULL, TRUE ) )
		m_2DRender->Enable_Texturing( FALSE );
	m_2DRender->Add_Line( Vector2( startX, startY ), Vector2( endX, endY ),
												lineWidth, lineColor );
	finish2D();

}  // end drawLine

// W3DDisplay::drawLine =======================================================
/** draw a line on the display in pixel coordinates with the specified color */
//=============================================================================
void W3DDisplay::drawLine( Int startX, Int startY, 
													 Int endX, Int endY, 
													 Real lineWidth,
													 UnsignedInt lineColor1,UnsignedInt lineColor2 )
{
	
	if( setup2D( NULL, TRUE ) )
		m_2DRender->Enable_Texturing( FALSE );
	m_2DRender->Add_Line( Vector2( startX, startY ), Vector2( endX, endY ),
												lineWidth, lineColor1, lineColor2 );
	finish2D();

}  // end drawLine


// W3DDisplay::drawOpenRect ===================================================
//=============================================================================
void W3DDisplay::drawOpenRect( Int startX, Int startY, Int width, Int height,
															 Real lineWidth, UnsignedInt lineColor )
{
	
	if (m_isClippedEnabled)
	{
		ICoord2D start, end, returnStart, returnEnd;
		start.x = startX;
		start.y = startY;

		end.x = start.x;
		end.y = start.y + height;
		if(ClipLine2D(&start, &end, &returnStart, &returnEnd, &m_clipRegion ))
			drawLine( returnStart.x, returnStart.y, returnEnd.x, returnEnd.y, lineWidth, lineColor);
			
		end.x = start.x + width;
		end.y = start.y;
		if(ClipLine2D(&start, &end, &returnStart, &returnEnd, &m_clipRegion ))
			drawLine( returnStart.x, returnStart.y, returnEnd.x, returnEnd.y, lineWidth, lineColor);

		start.x = startX + width;
		start.y = startY;
		end.x = start.x;
		end.y = start.y + height;
		if(ClipLine2D(&start, &end, &returnStart, &returnEnd, &m_clipRegion ))
			drawLine( returnStart.x, returnStart.y, returnEnd.x, returnEnd.y, lineWidth, lineColor);

		start.x = startX;
		start.y = startY + height;
		end.x = start.x + width;
		end.y = start.y;
		if(ClipLine2D(&start, &end, &returnStart, &returnEnd, &m_clipRegion ))
			drawLine( returnStart.x, returnStart.y, returnEnd.x, returnEnd.y, lineWidth, lineColor);
	}
	else
	{
		if( setup2D( NULL, TRUE ) )
			m_2DRender->Enable_Texturing( FALSE );

		m_2DRender->Add_Outline( RectClass( startX, startY,
																				startX + width, startY + height ),
														 lineWidth, lineColor );

		finish2D();
	}

}  // end drawOpenRect

// W3DDisplay::drawFillRect ===================================================
//=============================================================================
void W3DDisplay::drawFillRect( Int startX, Int startY, Int width, Int height,
															 UnsignedInt color )
{

	if( setup2D( NULL, TRUE ) )
		m_2DRender->Enable_Texturing( FALSE );
	m_2DRender->Add_Rect( RectClass( startX, startY,
																	 startX + width, startY + height ),
												0, 0, color );

	finish2D();

}  // end drawFillRect

void W3DDisplay::drawRectClock(Int startX, Int startY, Int width, Int height, Int percent, UnsignedInt color)
{
	// sanity
	if(percent < 1 || percent > 100)
		return;

	if( setup2D( NULL, TRUE ) )
		m_2DRender->Enable_Texturing( FALSE );

// The rectanges are numberd as follows
//(x,y)	|---------|
//			| 4  | 1  |
//			|----+----|
//			| 3  | 2  |
//			|---------| (x + width, y + width)
//	
	// we're done, lets just draw one rectangle for it all.
	if(percent == 100)
	{
		m_2DRender->Add_Rect(RectClass( startX, startY, 
																		startX + width, startY + height), 0,0, color);
	}
	else if( percent> 75)
	{
		//rectangle #1 & 2
		m_2DRender->Add_Rect(RectClass( startX + width/2, startY, 
																		startX + width, startY + height), 0,0, color);
		// rectangle #3
		m_2DRender->Add_Rect(RectClass( startX, startY + height/2, 
																		startX + width/2, startY + height), 0,0, color);
		// draw the part of rectangle 4
		Real remain = percent - 75;
		if(remain > 12)
		{
			//draw the full triangle
			m_2DRender->Add_Tri(Vector2(startX, startY), 
													Vector2(startX, startY + height/2),
													Vector2(startX + width/2, startY + height/2),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
			
			// draw the part of triangle
			Real percentDraw = (Real)(remain - 12)/ 13;
			m_2DRender->Add_Tri(Vector2(startX, startY), 
													Vector2(startX + width/2, startY + height/2),
													Vector2(startX + (width/2 * percentDraw), startY),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
		else
		{
			// draw the part of triangle
			Real percentDraw = (Real)(remain)/ 12;
			m_2DRender->Add_Tri(Vector2(startX, startY + height/2 - (height/2 * percentDraw)), 
													Vector2(startX, startY + height/2),
													Vector2(startX + width/2, startY + height/2),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}

	}
	else if( percent > 50)
	{
		//rectangle #1 & 2
		m_2DRender->Add_Rect(RectClass( startX + width/2, startY, 
																		startX + width, startY + height), 0,0, color);
		// draw the part of rectangle 3
		Real remain = percent - 50;
		if(remain > 12)
		{
			//draw the full triangle
			m_2DRender->Add_Tri(Vector2(startX + width/2, startY + height/2), 
													Vector2(startX, startY + height),
													Vector2(startX + width/2, startY + height),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
			
			// draw the part of triangle
			Real percentDraw = (Real)(remain - 12)/ 13;
			m_2DRender->Add_Tri(Vector2(startX, startY + height - (height/2 * percentDraw)), 
													Vector2(startX, startY + height),
													Vector2(startX + width/2, startY + height/2),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
		else
		{
			// draw the part of triangle
			Real percentDraw = (Real)(remain)/ 12;
			m_2DRender->Add_Tri(Vector2(startX + width/2, startY + height),  
													Vector2(startX + width/2, startY + height/2),
													Vector2(startX + width/2 - ( width/2 * percentDraw), startY + height),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
	}
	else if(percent > 25)
	{
		// rectangel #1
		m_2DRender->Add_Rect(RectClass( startX + width/2, startY, 
																		startX + width, startY + height/2), 0,0, color);
		// draw the part of rectangle 2
		Real remain = percent - 25;
		if(remain > 12)
		{
			//draw the full triangle
			m_2DRender->Add_Tri(Vector2(startX + width/2, startY + height/2), 
													Vector2(startX + width, startY + height),
													Vector2(startX + width, startY + height/2),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
			
			// draw the part of triangle
			Real percentDraw = (Real)(remain - 12)/ 13;
			m_2DRender->Add_Tri(Vector2(startX + width/2, startY + height/2), 
													Vector2(startX + width - (width/2 * percentDraw), startY + height),
													Vector2(startX + width, startY + height),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
		else
		{
			// draw the part of triangle
			Real percentDraw = (Real)(remain)/ 12;
			m_2DRender->Add_Tri(Vector2(startX + width, startY + height/2),  
													Vector2(startX + width/2, startY + height/2),
													Vector2(startX + width, startY + height/2 + ( height/2 * percentDraw)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
	}
	else
	{
				// draw the part of rectangle 1
		
		if(percent > 12)
		{
			//draw the full triangle
			m_2DRender->Add_Tri(Vector2(startX + width/2, startY), 
													Vector2(startX + width/2, startY + height/2),
													Vector2(startX + width, startY),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
			
			// draw the part of triangle
			Real percentDraw = (Real)(percent - 12)/ 13;
			m_2DRender->Add_Tri(Vector2(startX + width, startY),
													Vector2(startX + width/2, startY + height/2), 
													Vector2(startX + width, startY + (height/2 * percentDraw)),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
		else
		{
			// draw the part of triangle
			Real percentDraw = (Real)(percent)/ 12;
			m_2DRender->Add_Tri(Vector2(startX + width/2, startY),  
													Vector2(startX + width/2, startY + height/2),
													Vector2(startX + width/2 + (width/2 * percentDraw), startY ),
													Vector2(0,0),Vector2(0,0),Vector2(0,0),color);
		}
	}

	finish2D();

}


//--------------------------------------------------------------------------------------------------------------------
// W3DDisplay::drawRemainingRectClock
// Variation added by Kris -- October 2002
// This version will overlay a clock progress from the specified percentage to 100%. Essentially, this function will
// "reveal" an icon as it progresses towards completion.
//--------------------------------------------------------------------------------------------------------------------
void W3DDisplay::drawRemainingRectClock(Int startX, Int startY, Int width, Int height, Int percent, UnsignedInt color)
{
	// sanity
	if( percent < 0 || percent > 99 )
		return;

	if( setup2D( NULL, TRUE ) )
		m_2DRender->Enable_Texturing( FALSE );

// The rectanges are numbered as follows
//(x,y)	|---------|
//			| 4  | 1  |
//			|----+----|
//			| 3  | 2  |
//			|---------| (x + width, y + width)
//	

	Int midX = startX + width/2;
	Int midY = startY + height/2;
	Int endX = startX + width;
	Int endY = startY + height;
	Int halfWidth = width/2;
	Int halfHeight = height/2;

	if( percent == 0 )
	{
		// We just started, so draw the entire remaining rectangle.
		// #1, #2, #3, and #4
		m_2DRender->Add_Rect( RectClass( startX, startY, endX, endY ), 0, 0, color );
	}
	else if( percent < 25 )
	{
		//1-25%
		//-----

		//Rectangle #3 & 4
		m_2DRender->Add_Rect( RectClass( startX, startY, midX, endY ), 0, 0, color );
		
		//Rectangle #2
		m_2DRender->Add_Rect( RectClass( midX, midY, endX, endY ), 0, 0, color );

		//Handle rectangle #1 than needs partial rendering.
		if( percent < 13 )
		{
			//1-12%
  		//-----

			//Draw the 2nd half of rectangle #1
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( endX, midY ), Vector2( endX, startY ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );

			//Draw the last part of the 1st portion of rectangle #1
			Real percentDraw = (Real)( 13 - percent ) / 13;
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( endX, startY ), Vector2( endX - halfWidth * percentDraw, startY ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
		else
		{
			//13-24%
			//------

			//Draw the last part of the 2nd half of rectangle #1
			Real percentDraw = (Real)( percent - 13 ) / 12;
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( endX, midY ), Vector2( endX, startY + halfHeight * percentDraw ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
	}
	else if( percent < 50 )
	{
		//25-49%
		//------

		//rectangle #3 & 4
		m_2DRender->Add_Rect( RectClass( startX, startY, midX, endY ), 0, 0, color );

		//Handle rectangle #2 that needs partial rendering.
		if( percent < 38 )
		{
			//25-37%
  		//-----

			//Draw the 2nd half of rectangle #2
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( midX, endY ), Vector2( endX, endY ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );

			//Draw the last part of the 1st portion of rectangle #2
			Real percentDraw = (Real)( percent - 25 ) / 13;
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( endX, endY ), Vector2( endX, midY + halfHeight * percentDraw ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
		else
		{
			//38-49%
			//------

			//Draw the last part of the 2nd half of rectangle #1
			Real percentDraw = (Real)( percent - 38 ) / 12;
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( midX, endY ), Vector2( endX - halfWidth * percentDraw, endY ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
	}
	else if( percent < 75 )
	{
		//50-74%
		//------

		//Rectangle #4
		m_2DRender->Add_Rect( RectClass( startX, startY, midX, midY ), 0, 0, color );

		//Handle rectangle #3 that needs partial rendering.
		if( percent < 63 )
		{
			//50-62%
  		//-----

			//Draw the 2nd half of rectangle #3
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( startX, midY ), Vector2( startX, endY ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );

			//Draw the last part of the 1st portion of rectangle #3
			Real percentDraw = (Real)( percent - 50 ) / 13;
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( startX, endY ), Vector2( midX - halfWidth * percentDraw, endY ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
		else
		{
			//62-74%
			//------

			//Draw the last part of the 2nd half of rectangle #3
			Real percentDraw = (Real)( percent - 62 ) / 12;
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( startX, midY ), Vector2( startX, endY - halfHeight * percentDraw ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
	}
	else
	{
		//75-99%
		//------
		
		//Handle rectangle #4 that needs partial rendering.
		if( percent < 87 )
		{
			//75-87%
  		//-----

			//Draw the 2nd half of rectangle #4
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( midX, startY ), Vector2( startX, startY ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );

			//Draw the last part of the 1st portion of rectangle #4
			Real percentDraw = (Real)( percent - 75 ) / 13;
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( startX, startY ), Vector2( startX, midY - halfHeight * percentDraw ), 
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
		else
		{
			//88-99%
			//------

			//Draw the last part of the 2nd half of rectangle #4
			Real percentDraw = (Real)( percent - 88 ) / 12;
			m_2DRender->Add_Tri( Vector2( midX, midY ), Vector2( midX, startY ), Vector2( startX + halfWidth * percentDraw, startY ),
													 Vector2( 0, 0 ), Vector2( 0, 0 ), Vector2( 0, 0 ), color );
		}
	}

	finish2D();
}


// W3DDisplay::drawImage ======================================================
/** Draws an images at the screen coordinates and keeps it within the end
	* screen coords specified */
//=============================================================================
void W3DDisplay::drawImage( const Image *image, Int startX, Int startY, 
														Int endX, Int endY, Color color, DrawImageMode mode)
{

	// sanity
	if( image == NULL )
		return;

	// !!
	// Remember to update the GUIEditDisplay::drawImage when you make
	// changes to this, it technically uses W3D code to render itself,
	// but it not derived on the W3DDisplay
	// !!

	const Region2D *uv = image->getUV();

	Bool doAlphaReset=FALSE;

	//
	// only the plain alpha mode - what nearly every image is drawn in - can share a batch: the
	// others leave the renderer in a state of their own and have to put it back afterwards.
	//
	if( setup2D( image, mode == DRAW_IMAGE_ALPHA ) )
	{
		m_2DRender->Enable_Texturing( TRUE );

		///@todo: Why are we alpha blending all images?  Reduces our fillrate. -MW
		switch (mode)
		{
			case DRAW_IMAGE_ALPHA:	//nothing to do since alpha is the default state
				break;
			case DRAW_IMAGE_GRAYSCALE:
				m_2DRender->Enable_Grayscale(true);
				break;
			case DRAW_IMAGE_ADDITIVE:
				m_2DRender->Enable_Additive(true);
				doAlphaReset = TRUE;
				break;
			case DRAW_IMAGE_SOLID:
				m_2DRender->Enable_Additive(false);
				m_2DRender->Enable_Alpha(false);
				doAlphaReset = TRUE;
			default:
				break;
		}

		// if we have raw texture data we will use it, otherwise we are referencing filenames
		if( BitTest( image->getStatus(), IMAGE_STATUS_RAW_TEXTURE ) )
			m_2DRender->Set_Texture( (TextureClass *)(image->getRawTextureData()) );
		else
			m_2DRender->Set_Texture( image->getFilename().str() );
	}

	RectClass screen_rect(startX,startY,endX,endY);
	RectClass uv_rect(uv->lo.x,uv->lo.y,uv->hi.x,uv->hi.y);

	if (m_isClippedEnabled)
	{	//need to clip this quad to clip rectangle

		//
		//	Check for completely clipped
		//
		if (	endX <= m_clipRegion.lo.x ||
				endY <= m_clipRegion.lo.y)
		{
			return;	//nothing to render
		} else {
			RectClass clipped_rect;
			RectClass clipped_uv_rect;

			if( BitTest( image->getStatus(), IMAGE_STATUS_ROTATED_90_CLOCKWISE ) )
			{

	
				//
				//	Clip the polygons to the specified area
				//
				
				clipped_rect.Left		= __max (screen_rect.Left, m_clipRegion.lo.x);
				clipped_rect.Right	= __min (screen_rect.Right, m_clipRegion.hi.x);
				clipped_rect.Top		= __max (screen_rect.Top, m_clipRegion.lo.y);
				clipped_rect.Bottom	= __min (screen_rect.Bottom, m_clipRegion.hi.y);

				//
				//	Clip the texture to the specified area
				//
				
				float percent				= ((clipped_rect.Left - screen_rect.Left) / screen_rect.Width ());
				clipped_uv_rect.Top		= uv_rect.Top + (uv_rect.Height () * percent);

				percent						= ((clipped_rect.Right - screen_rect.Left) / screen_rect.Width ());
				clipped_uv_rect.Bottom	= uv_rect.Top + (uv_rect.Height () * percent);

				percent						= ((clipped_rect.Top - screen_rect.Top) / screen_rect.Height ());
				clipped_uv_rect.Right	= uv_rect.Right - (uv_rect.Width () * percent);

				percent						= ((clipped_rect.Bottom - screen_rect.Top) / screen_rect.Height ());
				clipped_uv_rect.Left		= uv_rect.Right - (uv_rect.Width () * percent);
			}
			else

			{
			
				//
				//	Clip the polygons to the specified area
				//
				
				clipped_rect.Left		= __max (screen_rect.Left, m_clipRegion.lo.x);
				clipped_rect.Right	= __min (screen_rect.Right, m_clipRegion.hi.x);
				clipped_rect.Top		= __max (screen_rect.Top, m_clipRegion.lo.y);
				clipped_rect.Bottom	= __min (screen_rect.Bottom, m_clipRegion.hi.y);

				//
				//	Clip the texture to the specified area
				//
				
				float percent				= ((clipped_rect.Left - screen_rect.Left) / screen_rect.Width ());
				clipped_uv_rect.Left		= uv_rect.Left + (uv_rect.Width () * percent);

				percent						= ((clipped_rect.Right - screen_rect.Left) / screen_rect.Width ());
				clipped_uv_rect.Right	= uv_rect.Left + (uv_rect.Width () * percent);

				percent						= ((clipped_rect.Top - screen_rect.Top) / screen_rect.Height ());
				clipped_uv_rect.Top		= uv_rect.Top + (uv_rect.Height () * percent);

				percent						= ((clipped_rect.Bottom - screen_rect.Top) / screen_rect.Height ());
				clipped_uv_rect.Bottom	= uv_rect.Top + (uv_rect.Height () * percent);
			}

			//
			//	Use the clipped rectangles to render
			//
			screen_rect = clipped_rect;
			uv_rect		= clipped_uv_rect;
		}
	}

	// if rotated 90 degrees clockwise we have to adjust the uv coords
	if( BitTest( image->getStatus(), IMAGE_STATUS_ROTATED_90_CLOCKWISE ) )
	{

		m_2DRender->Add_Tri( Vector2( screen_rect.Left, screen_rect.Top ), 
												 Vector2( screen_rect.Left, screen_rect.Bottom ),
												 Vector2( screen_rect.Right, screen_rect.Top ),
												 Vector2( uv_rect.Right, uv_rect.Top),
												 Vector2( uv_rect.Left, uv_rect.Top),
												 Vector2( uv_rect.Right, uv_rect.Bottom ),
												 color );

		m_2DRender->Add_Tri( Vector2( screen_rect.Right, screen_rect.Bottom ),
												 Vector2( screen_rect.Right, screen_rect.Top ),
												 Vector2( screen_rect.Left, screen_rect.Bottom ),
												 Vector2( uv_rect.Left, uv_rect.Bottom ),
												 Vector2( uv_rect.Right, uv_rect.Bottom ),
												 Vector2( uv_rect.Left, uv_rect.Top ),
												 color );

	}  // end if
	else
	{

		// just draw as normal
		m_2DRender->Add_Quad( screen_rect, uv_rect, color );

	}  // end else

	finish2D();

	//reset to default states for next time this method is called.
	if( mode != DRAW_IMAGE_ALPHA )
	{
		m_2DRender->Enable_Grayscale(false);	//never leave it in this mode
		if (doAlphaReset)
			m_2DRender->Enable_Alpha(true);
	}

}  // end drawImage

//============================================================================
// W3DDisplay::createVideoBuffer
//============================================================================

VideoBuffer*	W3DDisplay::createVideoBuffer( void )
{
	VideoBuffer::Type format = VideoBuffer::TYPE_UNKNOWN;

	/// @todo query video player for supported formats - we assume bink formats here

	// first try to use the native format

	WW3DFormat displayFormat = DX8Wrapper::getBackBufferFormat();

	if ( DX8Wrapper::Get_Current_Caps()->Support_Texture_Format( displayFormat ))
	{
		format = W3DVideoBuffer::W3DFormatToType( displayFormat );
	}

	if ( format == VideoBuffer::TYPE_UNKNOWN )
	{
		if ( DX8Wrapper::Get_Current_Caps()->Support_Texture_Format( WW3D_FORMAT_X8R8G8B8 ))
		{
			format = VideoBuffer::TYPE_X8R8G8B8;
		}
		else if ( DX8Wrapper::Get_Current_Caps()->Support_Texture_Format( WW3D_FORMAT_R8G8B8 ))
		{
			format = VideoBuffer::TYPE_R8G8B8;
		}
		else if ( DX8Wrapper::Get_Current_Caps()->Support_Texture_Format( WW3D_FORMAT_R5G6B5 ))
		{
			format = VideoBuffer::TYPE_R5G6B5;
		}
		else if ( DX8Wrapper::Get_Current_Caps()->Support_Texture_Format( WW3D_FORMAT_X1R5G5B5 ))
		{
			format = VideoBuffer::TYPE_X1R5G5B5;
		}
		else
		{
			// card does not support any of the formats we need
			return NULL;
		}
	}
	// on low mem machines, render every video in 16bit except for the EA Logo movie.
	// The low-memory half of that test is commented out right there, so this fires on
	// every machine - and it used to fire even when the device had just told us it does
	// not support R5G6B5, handing the video player a format it cannot create.
	if( !TheGlobalData->m_playIntro//&& TheGameLODManager && (!TheGameLODManager->didMemPass() || W3DShaderManager::getChipset() == DC_GEFORCE2))
			&& DX8Wrapper::Get_Current_Caps()->Support_Texture_Format( WW3D_FORMAT_R5G6B5 ) )
		format = VideoBuffer::TYPE_R5G6B5;

	W3DVideoBuffer *buffer = NEW W3DVideoBuffer( format );

	return buffer;
}


//============================================================================
// W3DDisplay::drawVideoBuffer
//============================================================================

void W3DDisplay::drawVideoBuffer( VideoBuffer *buffer, Int startX, Int startY, Int endX, Int endY )
{
	W3DVideoBuffer *vbuffer = (W3DVideoBuffer*) buffer;

	setup2D( NULL, FALSE );
	m_2DRender->Enable_Texturing( TRUE );
	m_2DRender->Set_Texture( vbuffer->texture() );
	m_2DRender->Add_Quad( RectClass( startX, startY, endX, endY ),
												vbuffer->Rect( 0, 0, 1, 1) );
	finish2D();

}

// W3DDisplay::setClipRegion ============================================
/** Set the clipping region for images.
  @todo: Make this work for all primitives, not just drawImage. */
//=============================================================================
void W3DDisplay::setClipRegion( IRegion2D *region )
{
		// assign new region
		m_clipRegion = *region;
		m_isClippedEnabled = TRUE;

}  // end setClipRegion

//=============================================================================
/* we don't really need to override this call, since we will soon be called to
	update every shroud cell explicitly...
*/
void W3DDisplay::clearShroud()
{
	// nothing
}

//=============================================================================
void W3DDisplay::setBorderShroudLevel(UnsignedByte level)
{
	if (TheTerrainRenderObject && TheTerrainRenderObject->getShroud())
	{
		TheTerrainRenderObject->getShroud()->setBorderShroudLevel((W3DShroudLevel)level);
	}
}

//=============================================================================
void W3DDisplay::setShroudLevel( Int x, Int y, CellShroudStatus setting )
{
	if (TheTerrainRenderObject && TheTerrainRenderObject->getShroud())
	{
		#ifdef INTENSE_DEBUG
		TheTerrainRenderObject->getShroud()->setShroudFilter(false);
		#endif
		if( setting == CELLSHROUD_SHROUDED )
			TheTerrainRenderObject->getShroud()->setShroudLevel(x, y, (W3DShroudLevel)TheGlobalData->m_shroudAlpha );
		else if( setting == CELLSHROUD_FOGGED )
			TheTerrainRenderObject->getShroud()->setShroudLevel(x, y, (W3DShroudLevel)TheGlobalData->m_fogAlpha );///< @todo placeholder to get feedback on logic work while graphic side being decided
		else
			TheTerrainRenderObject->getShroud()->setShroudLevel(x, y, (W3DShroudLevel)TheGlobalData->m_clearAlpha );
		//Logic is saying shroud.  We can add alpha levels here in client if needed.  
		// W3DShroud is a 0-255 alpha byte.  Logic shroud is a double reference count.

		TheTerrainRenderObject->notifyShroudChanged();
	
	}
}

//=============================================================================
///Utility function to dump data into a .BMP file
static void CreateBMPFile(char *pszFile, char *image, Int width, Int height)
{
	// A 24-bit bottom-up .bmp written by hand, on every platform: the two headers are little-endian
	// fields, put a byte at a time.  Each row is padded to four bytes, as the format requires, and only
	// the image's own 3 * width * height bytes are read.  The Windows writer this replaces sized the image
	// (width + 7) / 8 * height * 24 bytes and wrote that many out of a 3 * width * height buffer: past its
	// end whenever the width is not a multiple of 8 (a 1366-wide screen), and with unpadded rows, so a
	// skewed picture, whenever it is not a multiple of 4 (port defect 18).  The image comes top row
	// first, the way the screen is read, and the file takes the bottom row first.
	FILE *fp = zh_fopen(pszFile, "wb");
	if (fp == NULL)
		return;
	const UnsignedInt rowBytes = 3 * (UnsignedInt)width;
	const UnsignedInt stride = (rowBytes + 3) & ~3u;
	const UnsignedInt imageBytes = stride * (UnsignedInt)height;
	unsigned char header[54];
	memset(header, 0, sizeof(header));
	struct Put
	{
		static void u16(unsigned char *at, UnsignedInt v) { at[0] = (unsigned char)v; at[1] = (unsigned char)(v >> 8); }
		static void u32(unsigned char *at, UnsignedInt v) { u16(at, v & 0xFFFF); u16(at + 2, v >> 16); }
	};
	Put::u16(header + 0, 0x4d42);			// "BM"
	Put::u32(header + 2, 54 + imageBytes);	// the file's size
	Put::u32(header + 10, 54);				// where the pixels start
	Put::u32(header + 14, 40);				// the info header's size
	Put::u32(header + 18, (UnsignedInt)width);
	Put::u32(header + 22, (UnsignedInt)height);
	Put::u16(header + 26, 1);				// planes
	Put::u16(header + 28, 24);				// bits per pixel
	Put::u32(header + 34, imageBytes);
	fwrite(header, 1, sizeof(header), fp);
	static const unsigned char pad[3] = { 0, 0, 0 };
	for (Int row = height - 1; row >= 0; --row) {
		fwrite(image + row * rowBytes, 1, rowBytes, fp);
		fwrite(pad, 1, stride - rowBytes, fp);
	}
	fclose(fp);
}

// The back buffer (32-bit, not multisampled) copied into *copy, a system-memory surface kept from
// call to call and made again when the size or the format changes.  The copy may still be on its
// way when this returns: locking *copy is what waits for it.  FALSE when the back buffer could not
// be read.  Taken at the end of draw(), before Present: the front-buffer path is a desktop
// capture, and a window presented through a DXGI flip swap chain is one that desktop captures do
// not see - the old code saved black.
static Bool copyBackBuffer(IDirect3DSurface9 **copy)
{
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device();
	IDirect3DSurface9 *bb = NULL;
	if (dev == NULL || Render_Failed(dev->GetBackBuffer(PRIMARY_SWAP_CHAIN, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || bb == NULL)
		return FALSE;

	D3DSURFACE_DESC desc;
	bb->GetDesc(&desc);
	Bool copied = FALSE;
	if (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8)
	{
		// GetRenderTargetData refuses a multisampled source, and with -msaa or the options menu's
		// anti-aliasing set the back buffer is exactly that.  StretchRect between two render
		// targets of the same size is D3D9's resolve, so the samples are averaged down into a
		// plain target first and the readback comes off that.  Miss this and the caller falls
		// through to the front-buffer path, which photographs the desktop: whatever window happens
		// to sit over the game ends up in the screenshot, and nothing says so.
		IDirect3DSurface9 *resolved = NULL;
		IDirect3DSurface9 *source = bb;
		if (desc.MultiSampleType != D3DMULTISAMPLE_NONE
			&& Render_Succeeded(dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
					D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, NULL))
			&& Render_Succeeded(dev->StretchRect(bb, NULL, resolved, NULL, D3DTEXF_NONE)))
		{
			source = resolved;
		}

		if (Render_Succeeded(source->GetDesc(&desc)) && desc.MultiSampleType == D3DMULTISAMPLE_NONE)
		{
			if (*copy != NULL)
			{
				D3DSURFACE_DESC held;
				(*copy)->GetDesc(&held);
				if (held.Width != desc.Width || held.Height != desc.Height || held.Format != desc.Format)
				{
					(*copy)->Release();
					*copy = NULL;
				}
			}
			if (*copy == NULL && Render_Failed(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
					D3DPOOL_SYSTEMMEM, copy, NULL)))
				*copy = NULL;
			copied = *copy != NULL && Render_Succeeded(dev->GetRenderTargetData(source, *copy));
		}

		if (resolved != NULL)
			resolved->Release();
	}
	bb->Release();
	return copied;
}

// A system-memory copy of the back buffer, NULL when that is not possible.
static IDirect3DSurface9 *captureBackBuffer(void)
{
	IDirect3DSurface9 *copy = NULL;
	if (copyBackBuffer(&copy))
		return copy;
	if (copy != NULL)
		copy->Release();
	DEBUG_LOG(("takeScreenShot - the back buffer could not be read, falling back to a desktop capture\n"));
	return NULL;
}

///Save Screen Capture to a file - deferred to the end of the next rendered frame
void W3DDisplay::takeScreenShot(void)
{
	s_screenShotPending = TRUE;
}

/** The frame being drawn, as blue-green-red bytes, top row first, width * 3 bytes a row, in a buffer
	* the caller deletes.  NULL when there was no picture to read, which is what a device that has gone
	* away gives.  Runs before End_Render, while the back buffers still hold it. */
static char *captureFrameRows(Int *rowsWidth, Int *rowsHeight)
{
	// With -dx11present the picture on the screen is the Direct3D 11 one, so that is what a
	// screenshot has to be: reading the D3D9 back buffer here would photograph a frame nobody saw
	// and quietly compare the old renderer against itself.  This runs before End_Render, while the
	// D3D11 back buffer still holds the frame.
	if (Direct3D11_Present_Is_Enabled())
	{
		unsigned captureWidth = 0;
		unsigned captureHeight = 0;
		unsigned capturePitch = 0;
		unsigned char *captured =
			Direct3D11_Capture_Back_Buffer(captureWidth, captureHeight, capturePitch);
		if (captured == NULL)
			return NULL;

		char *rows = NEW char[3*captureWidth*captureHeight];
		for (unsigned row = 0; row < captureHeight; row++)
		{
			const unsigned char *in = captured + row*capturePitch;
			char *out = rows + row*captureWidth*3;
			for (unsigned column = 0; column < captureWidth; column++)
			{
				out[column*3+0] = (char)in[column*4+0];
				out[column*3+1] = (char)in[column*4+1];
				out[column*3+2] = (char)in[column*4+2];
			}
		}
		Direct3D11_Release_Capture(captured);
		*rowsWidth = (Int)captureWidth;
		*rowsHeight = (Int)captureHeight;
		return rows;
	}

	RenderRect bounds;
	IDirect3DSurface9 *fb = captureBackBuffer();
	if (fb != NULL)
	{
		D3DSURFACE_DESC desc;
		fb->GetDesc(&desc);
#if defined(_WIN32)
		SetRect(&bounds, 0, 0, desc.Width, desc.Height);
#else
		bounds.left = 0;
		bounds.top = 0;
		bounds.right = (Int)desc.Width;
		bounds.bottom = (Int)desc.Height;
#endif
	}
	else
	{
#if defined(_WIN32)
		// Lock front buffer and copy
		fb=DX8Wrapper::_Get_DX8_Front_Buffer();

		RenderPoint point;
		GetClientRect(ApplicationHWnd,&bounds);
		point.x=bounds.left; point.y=bounds.top;
		ClientToScreen(ApplicationHWnd, &point);
		bounds.left=point.x; bounds.top=point.y;
		point.x=bounds.right; point.y=bounds.bottom;
		ClientToScreen(ApplicationHWnd, &point);
		bounds.right=point.x; bounds.bottom=point.y;
#else
		// The front buffer is a desktop capture, which only Windows has; the back buffer is all there is.
		bounds.left = bounds.top = bounds.right = bounds.bottom = 0;
#endif
	}

	// The front-buffer path above turns the window's client area into desktop coordinates, and a
	// window can hang off the edge of the desktop - ask for a 1600x1200 window on a 1080-tall
	// monitor and it does. Reading that rectangle walks off the end of the front buffer and faults
	// inside the copy loop below, which is a screenshot taking the game down. Clamp to the surface.
	{
		D3DSURFACE_DESC fbDesc;
		if (fb != NULL && Render_Succeeded(fb->GetDesc(&fbDesc)))
		{
			if (bounds.left < 0) bounds.left = 0;
			if (bounds.top < 0) bounds.top = 0;
			if (bounds.right > (Int)fbDesc.Width) bounds.right = (Int)fbDesc.Width;
			if (bounds.bottom > (Int)fbDesc.Height) bounds.bottom = (Int)fbDesc.Height;
		}

		if (fb == NULL || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
		{
			if (fb != NULL)
				fb->Release();
			return NULL;		// nothing to save; better than a fault
		}
	}

	D3DLOCKED_RECT lrect;

	DX8_ErrorCode(fb->LockRect(&lrect,&bounds,D3DLOCK_READONLY));

	unsigned int x,y,index,index2,width,height;

	width=bounds.right-bounds.left;
	height=bounds.bottom-bounds.top;

	char *image=NEW char[3*width*height];
	//bmp is same byte order
	for (y=0; y<height; y++)
	{
		for (x=0; x<width; x++)
		{
			// index for image
			index=3*(x+y*width);
			// index for fb
			index2=y*lrect.Pitch+4*x;

			image[index]=*((char *) lrect.pBits + index2+0);
			image[index+1]=*((char *) lrect.pBits + index2+1);
			image[index+2]=*((char *) lrect.pBits + index2+2);
		}
	}

	fb->Release();

	*rowsWidth = (Int)width;
	*rowsHeight = (Int)height;
	return image;
}

/** Write the frame being drawn to pathname.  FALSE when there was no picture to read. */
static Bool writeFrameBMP(char *pathname)
{
	Int width = 0;
	Int height = 0;
	char *image = captureFrameRows(&width, &height);
	if (image == NULL)
		return FALSE;
	CreateBMPFile(pathname, image, width, height);
	delete [] image;
	return TRUE;
}

static void saveScreenShot(void)
{
	char leafname[256];
	char pathname[1024];

	static int frame_number = 1;

	Bool done = false;
	while (!done) {
#ifdef CAPTURE_TO_TARGA
		snprintf( leafname, ARRAY_SIZE(leafname), "%s%.3d.tga", "sshot", frame_number++);
#else
		snprintf( leafname, ARRAY_SIZE(leafname), "%s%.3d.bmp", "sshot", frame_number++);
#endif
		strlcpy(pathname, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(pathname));
		strlcat(pathname, leafname, ARRAY_SIZE(pathname));
		if (access( pathname, 0 ) == -1)
			done = true;
	}

	if (!writeFrameBMP(pathname))
		return;

	UnicodeString ufileName;
	ufileName.translate(leafname);
	TheInGameUI->message(TheGameText->fetch("GUI:ScreenCapture"), ufileName.str());
}

//=============================================================================
// -video <from> <to> [name]: one picture per logic frame across the range, written as numbered
// .bmp files under Videos\<name>\ and then handed to ffmpeg for Videos\<name>.mp4.  GameEngine runs
// one logic frame a pass over the range, so a picture is a logic frame and a second of video is
// LOGICFRAMES_PER_SECOND of them.  -recordfps 60 draws each frame twice before the next one runs, and
// a second is twice as many pictures; a picture is numbered by its logic frame times the pictures a
// frame plus which of them it is.
//=============================================================================
static const char VIDEO_FRAME_PATTERN[] = "frame%06d.bmp";
static const char VIDEO_ENCODER[] = "ffmpeg.exe";

static Int videoFramesPerSecond(void)
{
	return GameEngine_videoFramesPerSecond();
}

static Bool s_videoStarted = FALSE;
static Bool s_videoFinished = FALSE;
static UnsignedInt s_videoLastPicture = 0;
static Int s_videoFramesWritten = 0;
static Int s_videoFramesMissed = 0;
static char s_videoDirectory[_MAX_PATH];
/* Every picture of the recording is copied off the GPU into a slot of the record its frame is drawn
	 into, and read back a frame later while the next frame is drawn into the other record: reading the
	 copy as soon as it is asked for waits for the GPU to finish the frame.  -directorrecord's panes past
	 the first are drawn before pane 0 on the same logic frame and have slots of their own; the record
	 keeps where the panes lay when pane 0 was drawn, which is what its picture shows. */
enum { VIDEO_RECORDS = 2 };
enum { VIDEO_PIXEL_BYTES = 4 };		// blue, green, red and a byte nothing reads
static_assert(VIDEO_RECORDS * OBSERVER_MOST_PANES <= DX11_FRAME_COPY_SLOTS,
	"every pane of both records has a Direct3D 11 slot");
struct VideoRecord
{
	Bool pending;			///< pane 0's copy is made and the frame is still to be written
	UnsignedInt picture;
	Bool paneCopied[OBSERVER_MOST_PANES];
	UnsignedInt panePicture[OBSERVER_MOST_PANES];
	Int paneCount;
	Real paneRays[OBSERVER_MOST_PANES];
	Coord2D paneOrigin;
	IRegion2D radarFrame;
	std::vector<IRegion2D> broadcast;
};
static VideoRecord s_videoRecords[VIDEO_RECORDS];
static Int s_videoDrawingRecord = 0;
static IDirect3DSurface9 *s_videoSystemCopies[VIDEO_RECORDS * OBSERVER_MOST_PANES] = { NULL };
static std::vector<UnsignedByte> s_videoJoined;
/* the panes' runs are worked out again only when the picture or where its panes lie changes, which is
	 only while they slide in or out */
struct VideoPaneShape
{
	Int width;
	Int height;
	Int count;
	Coord2D origin;
	Real rays[OBSERVER_MOST_PANES];
};
static VideoPaneShape s_videoPaneRunsShape;
static std::vector<ObserverPaneRun> s_videoPaneRuns;
static std::vector<IRegion2D> s_videoRowKept;
#if defined(_WIN32)
/* -directorrecord writes no frames to disk: they go down a pipe into ffmpeg as they are drawn, which a
	 whole match of 1080p bitmaps would need tens of gigabytes for.  With no ffmpeg it records nothing
	 and quits. */
static Bool s_videoPipeTried = FALSE;
static HANDLE s_videoPipe = NULL;
static HANDLE s_videoEncoder = NULL;
static Int s_videoPipeWidth = 0;
static Int s_videoPipeHeight = 0;

// the directory with its trailing backslash taken off is the movie's own name
static void buildVideoMoviePath(char *moviePath, size_t size)
{
	strlcpy(moviePath, s_videoDirectory, size);
	moviePath[strlen(moviePath) - 1] = '\0';
	strlcat(moviePath, ".mp4", size);
}

// ffmpeg.exe beside generals.exe first, then off the PATH.  FALSE when neither has one
static Bool findVideoEncoder(char *encoderPath, DWORD size)
{
	const DWORD length = GetModuleFileNameA(NULL, encoderPath, size);
	char *slash = strrchr(encoderPath, '\\');
	if (length > 0 && length < size && slash != NULL)
	{
		slash[1] = '\0';
		strlcat(encoderPath, VIDEO_ENCODER, size);
		if (GetFileAttributesA(encoderPath) != INVALID_FILE_ATTRIBUTES)
			return TRUE;
	}
	return SearchPathA(NULL, VIDEO_ENCODER, NULL, size, encoderPath, NULL) != 0;
}
#endif

static void buildVideoFramePath(char *pathname, size_t size, Int index)
{
	char leafname[32];
	snprintf(leafname, ARRAY_SIZE(leafname), VIDEO_FRAME_PATTERN, index);
	strlcpy(pathname, s_videoDirectory, size);
	strlcat(pathname, leafname, size);
}

// frames are numbered from 0 without a gap, so the first name that is not there is the end of them
static void deleteVideoFrames(void)
{
	char pathname[_MAX_PATH];
	for (Int index = 0; ; ++index)
	{
		buildVideoFramePath(pathname, ARRAY_SIZE(pathname), index);
#if defined(_WIN32)
		if (!DeleteFileA(pathname))
			return;
#else
		if (zh_remove(pathname) != 0)
			return;
#endif
	}
}

static void writeVideoRecord(Int recordIndex);

static void releaseFrameCopies(void)
{
	Direct3D11_Release_Frame_Copies();
	for (Int slot = 0; slot < VIDEO_RECORDS * OBSERVER_MOST_PANES; ++slot)
	{
		if (s_videoSystemCopies[slot] != NULL)
			s_videoSystemCopies[slot]->Release();
		s_videoSystemCopies[slot] = NULL;
	}
}

static void finishVideo(void)
{
	if (!s_videoStarted || s_videoFinished)
	{
		// a recording that never started, or stopped for want of ffmpeg, can still hold copies
		releaseFrameCopies();
		return;
	}
	s_videoFinished = TRUE;

	// the last frame drawn is still waiting on its copy
	writeVideoRecord(1 - s_videoDrawingRecord);
	DEBUG_LOG(("VIDEO: %d frames written to %s at %d a second, %d pictures went by unrecorded\n",
		s_videoFramesWritten, s_videoDirectory, videoFramesPerSecond(), s_videoFramesMissed));
	releaseFrameCopies();
	std::vector<UnsignedByte>().swap(s_videoJoined);
	std::vector<ObserverPaneRun>().swap(s_videoPaneRuns);

#if defined(_WIN32)
	// the end of the pipe is the end of the stream: ffmpeg finishes the movie and exits
	if (s_videoEncoder != NULL)
	{
		CloseHandle(s_videoPipe);
		s_videoPipe = NULL;
		WaitForSingleObject(s_videoEncoder, INFINITE);
		DWORD pipedExitCode = 0;
		GetExitCodeProcess(s_videoEncoder, &pipedExitCode);
		CloseHandle(s_videoEncoder);
		s_videoEncoder = NULL;
		RemoveDirectoryA(s_videoDirectory);

		char pipedMoviePath[_MAX_PATH];
		buildVideoMoviePath(pipedMoviePath, ARRAY_SIZE(pipedMoviePath));
		if (pipedExitCode != 0)
			DEBUG_LOG(("VIDEO: %s exited with %u, so %s may be short or missing\n",
				VIDEO_ENCODER, (unsigned)pipedExitCode, pipedMoviePath));
		else
			DEBUG_LOG(("VIDEO: wrote %s\n", pipedMoviePath));
		return;
	}
#endif

	if (s_videoFramesWritten == 0)
		return;

#if !defined(_WIN32)
	// Off Windows the encoder is not started for you: the frames stay, and this is how to make the movie.
	DEBUG_LOG(("VIDEO: encode with: ffmpeg -framerate %d -i \"%s%s\" -c:v libx264 -pix_fmt yuv420p out.mp4\n",
		videoFramesPerSecond(), s_videoDirectory, VIDEO_FRAME_PATTERN));
#else
	char encoderPath[_MAX_PATH];
	if (!findVideoEncoder(encoderPath, ARRAY_SIZE(encoderPath)))
	{
		DEBUG_LOG(("VIDEO: no %s beside the game or on the PATH, so the frames stay where they are\n", VIDEO_ENCODER));
		return;
	}

	char moviePath[_MAX_PATH];
	buildVideoMoviePath(moviePath, ARRAY_SIZE(moviePath));

	// yuv420p is what every player opens and it wants even dimensions, which a window need not have
	char commandLine[4 * _MAX_PATH];
	snprintf(commandLine, ARRAY_SIZE(commandLine),
		"\"%s\" -y -loglevel error -framerate %d -i \"%s%s\" -vf pad=ceil(iw/2)*2:ceil(ih/2)*2 "
		"-c:v libx264 -pix_fmt yuv420p -crf 18 \"%s\"",
		encoderPath, videoFramesPerSecond(), s_videoDirectory, VIDEO_FRAME_PATTERN, moviePath);

	STARTUPINFOA startup;
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process;
	if (!CreateProcessA(encoderPath, commandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL,
			&startup, &process))
	{
		DEBUG_LOG(("VIDEO: %s would not start (error %u), so the frames stay where they are\n",
			encoderPath, (unsigned)GetLastError()));
		return;
	}

	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD exitCode = 0;
	GetExitCodeProcess(process.hProcess, &exitCode);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);

	if (exitCode != 0)
	{
		DEBUG_LOG(("VIDEO: %s exited with %u, so the frames stay where they are\n",
			encoderPath, (unsigned)exitCode));
		return;
	}

	deleteVideoFrames();
	RemoveDirectoryA(s_videoDirectory);
	DEBUG_LOG(("VIDEO: wrote %s\n", moviePath));
#endif
}

/** A -video range is being recorded and this logic frame is inside it. */
static Bool videoRecordingFrame(void)
{
	if (TheGlobalData->m_videoEndFrame <= 0 || s_videoFinished || TheGameLogic == NULL
			|| !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return FALSE;
	const UnsignedInt frame = TheGameLogic->getFrame();
	return frame >= (UnsignedInt)TheGlobalData->m_videoStartFrame && frame <= (UnsignedInt)TheGlobalData->m_videoEndFrame;
}

#if defined(_WIN32)
/** ffmpeg started with its stdin a pipe this end writes raw frames of this size into.  FALSE when
	* there is no ffmpeg or it would not start. */
static Bool openVideoPipe(Int width, Int height)
{
	char encoderPath[_MAX_PATH];
	if (!findVideoEncoder(encoderPath, ARRAY_SIZE(encoderPath)))
	{
		DEBUG_LOG(("VIDEO: no %s beside the game or on the PATH\n", VIDEO_ENCODER));
		return FALSE;
	}

	SECURITY_ATTRIBUTES inherited;
	memset(&inherited, 0, sizeof(inherited));
	inherited.nLength = sizeof(inherited);
	inherited.bInheritHandle = TRUE;
	HANDLE readEnd = NULL;
	HANDLE writeEnd = NULL;
	if (!CreatePipe(&readEnd, &writeEnd, &inherited, 0))
	{
		DEBUG_LOG(("VIDEO: no pipe to %s (error %u)\n", VIDEO_ENCODER, (unsigned)GetLastError()));
		return FALSE;
	}
	// ffmpeg holding the end written to as well would never see the stream end
	SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);

	char moviePath[_MAX_PATH];
	buildVideoMoviePath(moviePath, ARRAY_SIZE(moviePath));
	char commandLine[4 * _MAX_PATH];
	snprintf(commandLine, ARRAY_SIZE(commandLine),
		"\"%s\" -y -loglevel error -f rawvideo -pix_fmt bgr0 -s %dx%d -framerate %d -i - "
		"-vf pad=ceil(iw/2)*2:ceil(ih/2)*2 -c:v libx264 -pix_fmt yuv420p -crf 18 \"%s\"",
		encoderPath, width, height, videoFramesPerSecond(), moviePath);

	STARTUPINFOA startup;
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = readEnd;
	PROCESS_INFORMATION process;
	const BOOL started = CreateProcessA(encoderPath, commandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL,
		&startup, &process);
	CloseHandle(readEnd);
	if (!started)
	{
		CloseHandle(writeEnd);
		DEBUG_LOG(("VIDEO: %s would not start (error %u)\n",
			encoderPath, (unsigned)GetLastError()));
		return FALSE;
	}

	CloseHandle(process.hThread);
	s_videoPipe = writeEnd;
	s_videoEncoder = process.hProcess;
	s_videoPipeWidth = width;
	s_videoPipeHeight = height;
	DEBUG_LOG(("VIDEO: %dx%d frames go straight into %s for %s\n", width, height, encoderPath, moviePath));
	return TRUE;
}
#endif

static Int videoCopySlot(Int recordIndex, Int pane)
{
	return recordIndex * OBSERVER_MOST_PANES + pane;
}

/** Start copying the frame being drawn off the GPU into slot, without waiting for it.  FALSE when there
	* was no picture to copy, which is what a device that has gone away gives. */
static Bool queueFrameCopy(Int slot)
{
	if (Direct3D11_Present_Is_Enabled())
		return Direct3D11_Queue_Frame_Copy((unsigned)slot);
	return copyBackBuffer(&s_videoSystemCopies[slot]);
}

/** slot's copy, VIDEO_PIXEL_BYTES a pixel, top row first, until unmapFrameCopy; NULL when it cannot be read. */
static const UnsignedByte *mapFrameCopy(Int slot, Int *width, Int *height, Int *pitch)
{
	if (Direct3D11_Present_Is_Enabled())
	{
		unsigned mappedWidth = 0;
		unsigned mappedHeight = 0;
		unsigned mappedPitch = 0;
		const unsigned char *pixels = Direct3D11_Map_Frame_Copy((unsigned)slot, mappedWidth, mappedHeight, mappedPitch);
		*width = (Int)mappedWidth;
		*height = (Int)mappedHeight;
		*pitch = (Int)mappedPitch;
		return pixels;
	}
	IDirect3DSurface9 *copy = s_videoSystemCopies[slot];
	D3DLOCKED_RECT locked;
	if (copy == NULL || Render_Failed(copy->LockRect(&locked, NULL, D3DLOCK_READONLY)))
		return NULL;
	D3DSURFACE_DESC desc;
	copy->GetDesc(&desc);
	*width = (Int)desc.Width;
	*height = (Int)desc.Height;
	*pitch = (Int)locked.Pitch;
	return (const UnsignedByte *)locked.pBits;
}

static void unmapFrameCopy(Int slot)
{
	if (Direct3D11_Present_Is_Enabled())
		Direct3D11_Unmap_Frame_Copy((unsigned)slot);
	else
		s_videoSystemCopies[slot]->UnlockRect();
}

static bool keptLeftOf(const IRegion2D &first, const IRegion2D &second)
{
	return first.lo.x < second.lo.x;
}

/** The rectangles that stay pane 0's on row y, the radar's frame and the broadcast's, left to right. */
static void collectRowKept(const VideoRecord &record, Int y)
{
	s_videoRowKept.clear();
	for (size_t index = 0; index <= record.broadcast.size(); ++index)
	{
		const IRegion2D &region = index < record.broadcast.size() ? record.broadcast[index] : record.radarFrame;
		if (y >= region.lo.y && y < region.hi.y && region.lo.x < region.hi.x)
			s_videoRowKept.push_back(region);
	}
	std::sort(s_videoRowKept.begin(), s_videoRowKept.end(), keptLeftOf);
}

static void copyVideoSpan(UnsignedByte *joined, Int width, const UnsignedByte *pane, Int panePitch, Int y, Int x0, Int x1)
{
	if (x1 > x0)
		memcpy(joined + ((size_t)y * width + x0) * VIDEO_PIXEL_BYTES, pane + (size_t)y * panePitch + (size_t)x0 * VIDEO_PIXEL_BYTES,
			(size_t)(x1 - x0) * VIDEO_PIXEL_BYTES);
}

/** -directorrecord's panes joined into pane 0's picture: each pixel is taken from the pane whose wedge
	* it lies in, a row's stretch of one pane at a time.  A pane's camera already slid its picture with the
	* rays' meeting point, so the copy is pixel for pixel; moving the pixels instead read past a picture's
	* edge and smeared it.  Every pane draws the lines and the radar's frame, so a seam needs nothing of its
	* own.  Pane 0 keeps the framed radar and the broadcast's score bar and labels, which only its draw has.
	* A pane with no picture of this frame and size stays pane 0's. */
static void joinVideoPanes(const VideoRecord &record, Int recordIndex, UnsignedByte *joined, Int width, Int height)
{
	VideoPaneShape shape = {};
	shape.width = width;
	shape.height = height;
	shape.count = record.paneCount;
	shape.origin = record.paneOrigin;
	memcpy(shape.rays, record.paneRays, sizeof(Real) * record.paneCount);
	if (memcmp(&shape, &s_videoPaneRunsShape, sizeof(shape)) != 0)
	{
		s_videoPaneRunsShape = shape;
		ObserverCamera_paneRuns(width, height, shape.origin.x, shape.origin.y, shape.rays, shape.count, s_videoPaneRuns);
	}

	const UnsignedByte *panes[OBSERVER_MOST_PANES] = { NULL };
	Int panePitches[OBSERVER_MOST_PANES] = { 0 };
	for (Int pane = 1; pane < record.paneCount; ++pane)
	{
		if (!record.paneCopied[pane] || record.panePicture[pane] != record.picture)
			continue;
		Int paneWidth = 0;
		Int paneHeight = 0;
		panes[pane] = mapFrameCopy(videoCopySlot(recordIndex, pane), &paneWidth, &paneHeight, &panePitches[pane]);
		if (panes[pane] != NULL && (paneWidth != width || paneHeight != height))
		{
			unmapFrameCopy(videoCopySlot(recordIndex, pane));
			panes[pane] = NULL;
		}
	}

	Int keptRow = -1;
	for (size_t index = 0; index < s_videoPaneRuns.size(); ++index)
	{
		const ObserverPaneRun &run = s_videoPaneRuns[index];
		const UnsignedByte *pane = panes[run.pane];
		if (pane == NULL)
			continue;
		if (run.y != keptRow)
		{
			keptRow = run.y;
			collectRowKept(record, run.y);
		}
		Int start = run.x0;
		for (size_t kept = 0; kept < s_videoRowKept.size() && start < run.x1; ++kept)
		{
			const IRegion2D &region = s_videoRowKept[kept];
			if (region.hi.x <= start)
				continue;
			if (region.lo.x >= run.x1)
				break;
			copyVideoSpan(joined, width, pane, panePitches[run.pane], run.y, start, region.lo.x);
			start = region.hi.x;
		}
		copyVideoSpan(joined, width, pane, panePitches[run.pane], run.y, start, run.x1);
	}

	for (Int pane = 1; pane < record.paneCount; ++pane)
	{
		if (panes[pane] != NULL)
			unmapFrameCopy(videoCopySlot(recordIndex, pane));
	}
}

/** One picture of the recording, VIDEO_PIXEL_BYTES a pixel with no gap between rows, into ffmpeg's pipe
	* under -directorrecord, as the next numbered .bmp otherwise. */
static Bool writeVideoFrame(const UnsignedByte *pixels, Int width, Int height, UnsignedInt picture)
{
#if defined(_WIN32)
	if (TheGlobalData->m_directorRecord && !s_videoPipeTried)
	{
		s_videoPipeTried = TRUE;
		openVideoPipe(width, height);
	}
	if (s_videoPipe != NULL)
	{
		// ffmpeg was told one size; a picture of another would shear every frame after it
		if (width != s_videoPipeWidth || height != s_videoPipeHeight)
			return FALSE;
		const DWORD bytes = (DWORD)(VIDEO_PIXEL_BYTES * width * height);
		DWORD written = 0;
		if (WriteFile(s_videoPipe, pixels, bytes, &written, NULL) && written == bytes)
			return TRUE;
		DEBUG_LOG(("VIDEO: %s stopped taking frames (error %u) at logic frame %u\n", VIDEO_ENCODER,
			(unsigned)GetLastError(), picture * LOGICFRAMES_PER_SECOND / videoFramesPerSecond()));
		return FALSE;
	}
#endif
	// a whole match as bitmaps is tens of gigabytes, and one run filled a disk with 13 GB before it
	// was stopped, so without the encoder there is no recording at all
	if (TheGlobalData->m_directorRecord)
	{
		DEBUG_LOG(("VIDEO: -directorrecord records only through %s, beside the game or on the PATH; nothing recorded, quitting\n",
			VIDEO_ENCODER));
		s_videoFinished = TRUE;
#if defined(_WIN32)
		RemoveDirectoryA(s_videoDirectory);
#endif
		TheGameEngine->setQuitting(TRUE);
		return FALSE;
	}
	char pathname[_MAX_PATH];
	buildVideoFramePath(pathname, ARRAY_SIZE(pathname), s_videoFramesWritten);
	char *rows = NEW char[3 * width * height];
	for (Int pixel = 0; pixel < width * height; ++pixel)
		memcpy(rows + pixel * 3, pixels + pixel * VIDEO_PIXEL_BYTES, 3);
	CreateBMPFile(pathname, rows, width, height);
	delete [] rows;
	return TRUE;
}

/** The frame held in a record, pane 0's picture with the other panes' wedges taken into it, written out
	* once. */
static void writeVideoRecord(Int recordIndex)
{
	VideoRecord &record = s_videoRecords[recordIndex];
	if (!record.pending)
		return;
	record.pending = FALSE;

	const Int slot = videoCopySlot(recordIndex, 0);
	Int width = 0;
	Int height = 0;
	Int pitch = 0;
	const UnsignedByte *picture = mapFrameCopy(slot, &width, &height, &pitch);
	if (picture == NULL)
	{
		++s_videoFramesMissed;
		return;
	}

	const Int rowBytes = width * VIDEO_PIXEL_BYTES;
	const UnsignedByte *pixels = picture;
	if (record.paneCount >= 2 || pitch != rowBytes)
	{
		s_videoJoined.resize((size_t)rowBytes * height);
		for (Int y = 0; y < height; ++y)
			memcpy(&s_videoJoined[(size_t)y * rowBytes], picture + (size_t)y * pitch, rowBytes);
		if (record.paneCount >= 2)
			joinVideoPanes(record, recordIndex, &s_videoJoined[0], width, height);
		pixels = &s_videoJoined[0];
	}

	if (writeVideoFrame(pixels, width, height, record.picture))
		++s_videoFramesWritten;
	else
		++s_videoFramesMissed;
	unmapFrameCopy(slot);
}

static void captureVideoFrame(void)
{
	if (TheGlobalData->m_videoEndFrame <= 0 || s_videoFinished || TheGameLogic == NULL
			|| !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return;

	const UnsignedInt frame = TheGameLogic->getFrame();
	if (frame < (UnsignedInt)TheGlobalData->m_videoStartFrame)
		return;

	if (frame > (UnsignedInt)TheGlobalData->m_videoEndFrame)
	{
		finishVideo();
		return;
	}

	// a picture is a logic frame and which of its -recordfps pictures it is
	const UnsignedInt picture = frame * GameEngine_videoPictures() + GameEngine_videoPicture();

	// -directorrecord's panes past the first: kept for pane 0's draw, the last of this picture
	if (TheObserverCamera.isDrawingSecond())
	{
		const Int pane = TheObserverCamera.getDrawingPane();
		VideoRecord &drawing = s_videoRecords[s_videoDrawingRecord];
		drawing.paneCopied[pane] = queueFrameCopy(videoCopySlot(s_videoDrawingRecord, pane));
		drawing.panePicture[pane] = picture;
		return;
	}

	if (!s_videoStarted)
	{
		s_videoStarted = TRUE;
		snprintf(s_videoDirectory, ARRAY_SIZE(s_videoDirectory), "%sVideos\\",
			TheGlobalData->getPath_UserData().str());
#if defined(_WIN32)
		CreateDirectoryA(s_videoDirectory, NULL);
#else
		zh_mkdir(s_videoDirectory);
#endif
		strlcat(s_videoDirectory, TheGlobalData->m_videoName.str(), ARRAY_SIZE(s_videoDirectory));
		strlcat(s_videoDirectory, "\\", ARRAY_SIZE(s_videoDirectory));
#if defined(_WIN32)
		CreateDirectoryA(s_videoDirectory, NULL);
#else
		zh_mkdir(s_videoDirectory);
#endif

		// a directory left over from an earlier run would hand ffmpeg its tail as well
		deleteVideoFrames();
		DEBUG_LOG(("VIDEO: recording logic frames %d to %d into %s\n",
			TheGlobalData->m_videoStartFrame, TheGlobalData->m_videoEndFrame, s_videoDirectory));
	}
	else if (picture <= s_videoLastPicture)
	{
		return;
	}
	else if (picture > s_videoLastPicture + 1)
	{
		s_videoFramesMissed += picture - s_videoLastPicture - 1;
	}
	s_videoLastPicture = picture;

	const Int drawingIndex = s_videoDrawingRecord;
	VideoRecord &drawing = s_videoRecords[drawingIndex];
	if (!queueFrameCopy(videoCopySlot(drawingIndex, 0)))
	{
		++s_videoFramesMissed;
		return;
	}
	drawing.pending = TRUE;
	drawing.picture = picture;
	drawing.paneCount = TheObserverCamera.getDrawnPaneCount();
	memcpy(drawing.paneRays, TheObserverCamera.getPaneRays(), sizeof(drawing.paneRays));
	drawing.paneOrigin = TheObserverCamera.getPaneOrigin();
	drawing.radarFrame = TheObserverCamera.getRadarFrame();
	drawing.broadcast = TheObserverCamera.getBroadcast();

	// the frame before this one is long drawn, so reading its copies waits on nothing
	s_videoDrawingRecord = 1 - drawingIndex;
	writeVideoRecord(s_videoDrawingRecord);
}

/** Start/Stop campturing an AVI movie*/
void W3DDisplay::toggleMovieCapture(void)
{
	WW3D::Toggle_Movie_Capture("Movie",30);
}

/** Asks the device rather than the switch: a machine that cannot make a Direct3D 11 device carries
	* on with Direct3D 9 whatever -d3d9 said, and the corner has to name what is actually drawing.
	* A 64-bit exe names its architecture beside it. */
const WideChar *W3DDisplay::getRendererName(void) const
{
#if defined(_WIN32)
#if defined(_M_ARM64)
	return Direct3D11_Is_Active() ? u"DX11 arm64" : u"DX9 arm64";
#elif defined(_WIN64)
	return Direct3D11_Is_Active() ? u"DX11 x64" : u"DX9 x64";
#else
	return Direct3D11_Is_Active() ? u"DX11" : u"DX9";
#endif
#else
	// Off Windows the picture is drawn by the SDL3 GPU device: its backend, "Metal arm64" or "Vulkan x64",
	// or "Headless" with no window (Platform/RendererName.h).
	return PosixRenderer_Name();
#endif
}


#if defined(_DEBUG) || defined(_INTERNAL)

static FILE *AssetDumpFile=NULL;

void dumpMeshAssets(MeshClass *mesh)
{
	if (mesh)
	{
		TextureClass *texture;
		//MaterialInfoClass	*material = mesh->Get_Material_Info();
		MeshModelClass *model=mesh->Get_Model();
		for (int stage=0;stage<MeshMatDescClass::MAX_TEX_STAGES;++stage)
		{
			for (int pass=0;pass<model->Get_Pass_Count();++pass) 
			{
				if (model->Has_Texture_Array(pass,stage))
				{
					for (int i=0;i<model->Get_Polygon_Count();++i)
					{
						if ((texture=model->Peek_Texture(i,pass,stage)) != NULL)
						{
							fprintf(AssetDumpFile,"\t%s\n",texture->Get_Texture_Name());
						}
					}
				}
				else
				{
					if ((texture=model->Peek_Single_Texture(pass,stage)) != NULL)
					{
						fprintf(AssetDumpFile,"\t%s\n",texture->Get_Texture_Name());
					}
				}
			}
		}
	}
}

void dumpHLODAssets(HLodClass *hlod)
{
	if (hlod)
	{
		//model composed of multiple meshes.
		for (Int i=0; i<hlod->Get_Num_Sub_Objects(); i++)
		{
			RenderObjClass *subObj=hlod->Get_Sub_Object(i);
			if (subObj->Class_ID() == RenderObjClass::CLASSID_HLOD)
				dumpHLODAssets((HLodClass *)subObj);
			else
			if (subObj->Class_ID() == RenderObjClass::CLASSID_MESH)
				dumpMeshAssets((MeshClass *)subObj);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/**  dump all used models/textures to a file.*/
//-------------------------------------------------------------------------------------------------
void W3DDisplay::dumpModelAssets(const char *path)
{
	if (m_3DScene)
	{	
		AssetDumpFile=fopen(path,"w");
		if (AssetDumpFile)
		{
			fprintf(AssetDumpFile,"Models and Textures used on %s:\n\n",TheGlobalData->m_mapName.str());
			SceneIterator *sceneIter = m_3DScene->Create_Iterator();
			sceneIter->First();
			while(!sceneIter->Is_Done())
			{
				RenderObjClass * robj = sceneIter->Current_Item();
				if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD)
				{	fprintf(AssetDumpFile,"%s.W3D:\n",robj->Get_Name());
					dumpHLODAssets((HLodClass *)robj);
				}
				else
				if (robj->Class_ID() == RenderObjClass::CLASSID_MESH)
				{	fprintf(AssetDumpFile,"%s.W3D:\n",robj->Get_Name());
					dumpMeshAssets((MeshClass *)robj);
				}
				sceneIter->Next();
			}
			m_3DScene->Destroy_Iterator(sceneIter);
			fclose(AssetDumpFile);
		}
	}
}
#endif	//only include above code in debug and internal
//-------------------------------------------------------------------------------------------------
/** Preload using the W3D asset manager the model referenced by the string parameter */
//-------------------------------------------------------------------------------------------------
void W3DDisplay::preloadModelAssets( AsciiString model )
{

	if( m_assetManager )
	{
		AsciiString nameWithExtension;

		nameWithExtension.format( "%s.w3d", model.str() );
		m_assetManager->Load_3D_Assets( nameWithExtension.str() );

	}  // end if

}  // end preloadModelAssets

//-------------------------------------------------------------------------------------------------
/** Preload using the W3D asset manager the texture referenced by the string parameter */
//-------------------------------------------------------------------------------------------------
void W3DDisplay::preloadTextureAssets( AsciiString texture )
{

	if( m_assetManager )
	{
		TextureClass *theTexture = m_assetManager->Get_Texture( texture.str() );
		theTexture->Release_Ref();//release reference
	}  // end if

}  // end preloadModelAssets

//-------------------------------------------------------------------------------------------------
/** The asset manager keeps the first TextureClass made under a name, and Render2D's Set_Texture
	* reuses it.  Preloaded with Get_Texture's defaults it had every mip level and went through the
	* Texture Quality reduction, so a 4096 wide bar painting was drawn at 2048 below the top setting.
	* Asked for the way Render2D asks, one level, which the loader never reduces. */
//-------------------------------------------------------------------------------------------------
void W3DDisplay::preloadImageTexture( AsciiString texture )
{
	if( m_assetManager )
	{
		TextureClass *theTexture = m_assetManager->Get_Texture( texture.str(), MIP_LEVELS_1 );
		theTexture->Release_Ref();
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DDisplay::doSmartAssetPurgeAndPreload(const char* usageFileName)
{
	if (!m_assetManager || !usageFileName || !*usageFileName)
		return;

	DynamicVectorClass<StringClass> names(8000);

	// use TheFileSystem here so we can bigify these files
	File* f = TheFileSystem->openFile(usageFileName, File::READ | File::TEXT);
	if (f)
	{
		for (;;)
		{
			AsciiString tmp;
			if (f->scanString(tmp) == FALSE)
				break;

			// allow for comments in the file. Note that this doesn't allow for comments
			// with spaces! doh. oh well. better than nothing.
			if (tmp.str()[0] == ';')
				continue;

			names.Add(StringClass(tmp.str()));
		}
		f->close();
	}

	// just free everything if there's no exclusion list file (send in an empty list)
	m_assetManager->Free_Assets_With_Exclusion_List(names);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
void W3DDisplay::dumpAssetUsage(const char* mapname)
{
	if (!m_assetManager || !mapname || !*mapname)
		return;

	DynamicVectorClass<StringClass> names(8000);
	m_assetManager->Create_Asset_List(names);

	const char* leafname = strrchr(mapname, '\\');
	if (leafname)
		++leafname;					// point to first character after the last backslash
	else
		leafname = mapname;		// point to the start of the filename

	char buf[256];
	int idx = 1;
	while (true)
	{
		sprintf(buf, "AssetUsage_%s_%04d.txt",leafname,idx);
		if (access(buf, 0) != 0)
			break;	// it exists, we're good
		++idx;
	}
	
	FILE *fp = fopen(buf, "w");
	if (fp)
	{
		for (int i=0; i<names.Count(); i++) 
		{
			const char* n = names[i];
			fprintf(fp, "%s\n", n);
		}
		fclose(fp);
	}
}
#endif

//-------------------------------------------------------------------------------------------------
static void drawFramerateBar(void)
{
	static UnsignedInt prevTime = Clock_Milliseconds();
	UnsignedInt now = Clock_Milliseconds();
	Real percTime = (1000.0f / (now - prevTime) ) / (1000.0f / TheGlobalData->m_framesPerSecondLimit);

	if (percTime > 1.0f)
		percTime = 1.0f;
	else if (percTime < 0.0f)
		percTime = 0.0f;
	Int width = REAL_TO_INT(percTime * TheDisplay->getWidth());
	UnsignedInt colorToUse = GameMakeColor( REAL_TO_UNSIGNEDBYTE((1.0f - percTime) * 255), 
																					REAL_TO_UNSIGNEDBYTE(percTime * 255), 
																					0, 
																					0x7F);

	TheDisplay->drawFillRect(1, 1, width, 15, colorToUse);
	prevTime = now;
}
