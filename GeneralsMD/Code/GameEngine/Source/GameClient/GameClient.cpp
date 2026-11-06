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

// FILE: GameClient.cpp ////////////////////////////////////////////////////
// Implementation of GameClient singleton
// Author: Michael S. Booth, March 2001
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include <algorithm>
#include "Platform/SleepMilliseconds.h"
#include "Lib/Clock.h"
#include "GameClient/GameClient.h"
#if !defined(_WIN32)
#include "cpudetect.h"		// getPreloadMemoryStatus, off Windows
#endif

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Common/ActionManager.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "Common/GameLOD.h"
#include "GameClient/Anim2D.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawGroupInfo.h"
#include "GameClient/Eva.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/GameConsole.h"
#include "GameClient/GraphDraw.h"
#include "GameClient/GUICommandTranslator.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/HintSpy.h"
#include "GameClient/HotKey.h"
#include "GameClient/IMEManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Mouse.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/PlaceEventTranslator.h"
#include "GameClient/RayEffect.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/Shell.h"
#include "GameClient/Snow.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/View.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/WindowXlat.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"		// For TheScriptEngine - jkmcd
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameNetwork/Transport.h"
#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define DRAWABLE_HASH_SIZE	8192

/// The GameClient singleton instance
GameClient *TheGameClient = NULL;

#ifdef DEBUG_LOGGING
//
// How long the last TheDisplay->DRAW() took.  The engine's frame rate watchdog reports the client
// half of the loop as one number, and that number is two very different things added together: the
// renderer, and everything the client does around it (the window system, the drawables, the UI).
// Splitting the render out is what says which of the two a slow frame is.
//
Real TheClientDrawMS = 0.0f;
//
// And the same question one level down, because the renderer's own time is three very different
// things: the 3D scene, the interface drawn over it, and the window system underneath that.
// W3DDisplay and W3DInGameUI fill these in; the watchdog prints them.
//
Real TheSceneDrawMS = 0.0f;			///< W3DDisplay::drawViews() - the world
Real TheUIDrawMS = 0.0f;				///< TheInGameUI->DRAW() - everything over the world
Real TheUIPostDrawMS = 0.0f;		///< InGameUI::postDraw() - the text lists and the world overlays
Real TheWindowRepaintMS = 0.0f;	///< TheWindowManager->winRepaint() - the control bar and dialogs
//
// Particles cost in three places that have nothing in common: the update that ages and emits them
// once a logic frame, the fill that turns each system into quads, and the translucent flush that
// sorts those quads with every other see-through polygon and draws them.  GameEngine adds these up
// over an unattended run and prints HEADLESS PARTICLECOST.
//
Real TheParticleUpdateMS = 0.0f;			///< ParticleSystemManager::update()
Real TheParticleFillMS = 0.0f;				///< W3DParticleSystemManager::doParticles(), the system loop alone
Real TheTranslucentMS = 0.0f;					///< DoParticles() and SortingRendererClass::Flush() together, fill included
UnsignedInt TheTranslucentDraws = 0;	///< draw calls made by the fill and the flush
UnsignedInt TheSortingPolygonsRefused = 0;	///< polygons the sorting pool had no room for, so never drawn
UnsignedInt TheParticlesPastGroupLimit = 0;	///< on-screen particles doParticles cut at MAX_POINTS_PER_GROUP a system
//
// The scene number above is still four different jobs.  The sun's depth pass draws every caster a
// second time, the post chain runs before the health bars, and the bars themselves walk every
// drawable in view.  A slow second that only says "scene" cannot tell those apart.
//
Real TheShadowMapMS = 0.0f;					///< W3DVolumetricShadowManager::renderShadowMap()
Int TheShadowMapCasters = 0;					///< casters that pass submitted
UnsignedInt TheShadowMapDraws = 0;		///< draw calls that pass made
Real ThePostChainMS = 0.0f;						///< Direct3D11_Finish_Scene(), inside the scene timer
Real TheIconDrawMS = 0.0f;						///< health bars and the other drawable icons
UnsignedInt TheSceneDrawCalls = 0;		///< DX8Wrapper draws from Begin_Render through drawViews
//
// The translucent number is still three jobs that fail for different reasons: sorting every
// see-through polygon, copying the sorted vertices into a dynamic buffer, and handing the runs
// to the device.  The copy is where a buffer lock waits.
//
Real TheSortingSortMS = 0.0f;
Real TheSortingCopyMS = 0.0f;
Real TheSortingDrawMS = 0.0f;
UnsignedInt TheSortingEntries = 0;		///< triangles and quads the flushes sorted
#endif

//-------------------------------------------------------------------------------------------------
/** Fog and shroud hide a drawable from the local player, with one exception: a structure the player
	* has just planned.  It clears no shroud of its own, so a base planned out into fog would otherwise
	* disappear the moment it was placed - it stays drawn, as the silhouette of the finished building,
	* until the builder walks over and the work starts for real. */
//-------------------------------------------------------------------------------------------------
Bool GameClient_hiddenByShroud( ObjectShroudStatus status, Bool ownPlacementSilhouette )
{
	if( ownPlacementSilhouette )
		return FALSE;

	return status >= OBJECTSHROUD_FOGGED;
}

//-------------------------------------------------------------------------------------------------
GameClient::GameClient()
{

	// zero our translator list
	for( Int i = 0; i < MAX_CLIENT_TRANSLATORS; i++ )
		m_translators[ i ] = TRANSLATOR_ID_INVALID;
	m_numTranslators = 0;
	m_commandTranslator = NULL;
	
	// Added By Sadullah Nader
	// Initializations missing and needed
	m_drawableTOC.clear();
	//
	m_textBearingDrawableList.clear();

	m_frame = 0;

	m_drawableList = NULL;
	
	m_nextDrawableID = (DrawableID)1;
	TheDrawGroupInfo = new DrawGroupInfo;
}

//std::vector<std::string>	preloadTextureNamesGlobalHack;
//std::vector<std::string>	preloadTextureNamesGlobalHack2;

//-------------------------------------------------------------------------------------------------
GameClient::~GameClient()
{
	delete TheGameConsole;
	TheGameConsole = NULL;

#ifdef PERF_TIMERS
	delete TheGraphDraw;
	TheGraphDraw = NULL;
#endif

	if (TheDrawGroupInfo) 
	{
		delete TheDrawGroupInfo;
		TheDrawGroupInfo = NULL;
	}

	// clear any drawable TOC we might have
	m_drawableTOC.clear();

	//DEBUG_LOG(("Preloaded texture files ------------------------------------------\n"));
	//for (Int oog=0; oog<preloadTextureNamesGlobalHack2.size(); ++oog)
	//{
	//	DEBUG_LOG(("%s\n", preloadTextureNamesGlobalHack2[oog]));
	//}
	//DEBUG_LOG(("------------------------------------------------------------------\n"));
	//for (oog=0; oog<preloadTextureNamesGlobalHack.size(); ++oog)
	//{
	//	DEBUG_LOG(("%s\n", preloadTextureNamesGlobalHack[oog]));
	//}
	//DEBUG_LOG(("End Texture files ------------------------------------------------\n"));
	if(TheCampaignManager)
		delete TheCampaignManager;
	TheCampaignManager = NULL;

	// destroy all Drawables
	Drawable *draw, *nextDraw;
	for( draw = m_drawableList; draw; draw = nextDraw )
	{
		nextDraw = draw->getNextDrawable();
		destroyDrawable( draw );
	}
	m_drawableList = NULL;
	flushDestroyedDrawables();

	// delete the ray effects
	delete TheRayEffects;
	TheRayEffects = NULL;

	// delete the hot key manager
	delete TheHotKeyManager;
	TheHotKeyManager = NULL;

	// destroy the in-game user interface
	delete TheInGameUI;
	TheInGameUI = NULL;

	delete TheChallengeGenerals;
	TheChallengeGenerals = NULL;

	// delete the shell
	delete TheShell;
	TheShell = NULL;

	delete TheIMEManager;
	TheIMEManager = NULL;

	// delete window manager
	delete TheWindowManager;
	TheWindowManager = NULL;

	// delete the font library
	TheFontLibrary->reset();
	delete TheFontLibrary;
	TheFontLibrary = NULL;

	delete TheMouse;
	TheMouse = NULL;

	///@todo :  TheTerrainVisual used to be the first thing destroyed.
	//I had to put in here so that drawables free their track marks before
	//the terrain visual deletes the track laying system. MW

	// destroy the terrain visual representation
	delete TheTerrainVisual;
	TheTerrainVisual = NULL;

	// destroy the display
	delete TheDisplay;
	TheDisplay = NULL;

	delete TheHeaderTemplateManager;
	TheHeaderTemplateManager = NULL;
	
	delete TheLanguageFilter;
	TheLanguageFilter = NULL;

	delete TheVideoPlayer;
	TheVideoPlayer = NULL;

	// destroy all translators
	for( Int i = 0; i < m_numTranslators; i++ )
		TheMessageStream->removeTranslator( m_translators[ i ] );	
	m_numTranslators = 0;
	m_commandTranslator = NULL;

	delete TheAnim2DCollection;
	TheAnim2DCollection = NULL;	

	delete TheMappedImageCollection;
	TheMappedImageCollection = NULL;	
	
	delete TheKeyboard;
	TheKeyboard = NULL;

	delete TheDisplayStringManager;
	TheDisplayStringManager = NULL;

	delete TheEva;
	TheEva = NULL;

	delete TheSnowManager;
	TheSnowManager = NULL;

}  // end ~GameClient

//-------------------------------------------------------------------------------------------------
/** Initialize resources for the game client */
//-------------------------------------------------------------------------------------------------
void GameClient::init( void )
{

	setFrameRate(MSEC_PER_LOGICFRAME_REAL);		// from GameCommon.h... tell W3D what our expected framerate is

	INI ini;
	// Load the DrawGroupInfo here, before the Display Manager is loaded.
	ini.load("Data\\INI\\DrawGroupInfo.ini", INI_LOAD_OVERWRITE, NULL);
	
	// Override the ini values with localized versions:
	if (TheGlobalLanguageData && TheGlobalLanguageData->m_drawGroupInfoFont.name.isNotEmpty())
	{
		TheDrawGroupInfo->m_fontName = TheGlobalLanguageData->m_drawGroupInfoFont.name;
		TheDrawGroupInfo->m_fontSize = TheGlobalLanguageData->m_drawGroupInfoFont.size;
		TheDrawGroupInfo->m_fontIsBold = TheGlobalLanguageData->m_drawGroupInfoFont.bold;
	}

	// create the display string factory
	TheDisplayStringManager = createDisplayStringManager();
	if( TheDisplayStringManager )	{
		TheDisplayStringManager->init();
		TheDisplayStringManager->setName("TheDisplayStringManager");
	}
	
	// create the keyboard
	TheKeyboard = createKeyboard();
	TheKeyboard->init();
	TheKeyboard->setName("TheKeyboard");

	// allocate and load image collection for the GUI and just load the 256x256 ones for now
	TheMappedImageCollection = MSGNEW("GameClientSubsystem") ImageCollection;
	TheMappedImageCollection->load( 512 );

	// now that we have all the images loaded ... load any animation definitions from those images
	TheAnim2DCollection = MSGNEW("GameClientSubsystem") Anim2DCollection;
	TheAnim2DCollection->init();
 	TheAnim2DCollection->setName("TheAnim2DCollection");

	// register message translators
	if( TheMessageStream )
	{

		//
		// NOTE: Make sure m_translators[] is large enough to accomodate all the translators you
		// are loading here.  See MAX_CLIENT_TRANSLATORS
		//

		// since we only allocate one of each, don't bother pooling 'em
		// ahead of the window system, so the console owns the key above Tab and every key it types
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") GameConsoleTranslator, 5 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") WindowTranslator,     10 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") MetaEventTranslator,	20 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") HotKeyTranslator,	25 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") PlaceEventTranslator,	30 );
		m_translators[ m_numTranslators++ ] = TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") GUICommandTranslator, 40 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") SelectionTranslator,	50 );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") LookAtTranslator,			60 );
		m_translators[ m_numTranslators ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") CommandTranslator,		70 );
		// we keep a pointer to the command translator because it's useful
		m_commandTranslator = (CommandTranslator *)TheMessageStream->findTranslator( m_translators[ m_numTranslators++ ] );
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") HintSpyTranslator,		100 );

		//
		// the client message translator should probably remain as the last reaction of the
		// client before the messages are given to the network for processing.  This
		// lets all systems in the client give events that can be processed by the
		// client message translator
		//
		m_translators[ m_numTranslators++ ] =	TheMessageStream->attachTranslator( MSGNEW("GameClientSubsystem") GameClientMessageDispatcher, 999999999 );

	}  

	// create the font library
	TheFontLibrary = createFontLibrary();
	if( TheFontLibrary )
		TheFontLibrary->init();

	// create the mouse
	TheMouse = createMouse();
	TheMouse->parseIni();
	TheMouse->initCursorResources();
 	TheMouse->setName("TheMouse");

	// instantiate the display
	TheDisplay = createGameDisplay();
	if( TheDisplay ) {
		TheDisplay->init();
 		TheDisplay->setName("TheDisplay");
	}
	
	TheHeaderTemplateManager = MSGNEW("GameClientSubsystem") HeaderTemplateManager;
	if(TheHeaderTemplateManager){
		TheHeaderTemplateManager->init();
	}

	// create the window manager
	TheWindowManager = createWindowManager();
	if( TheWindowManager )
	{

		TheWindowManager->init();
 		TheWindowManager->setName("TheWindowManager");
//		TheWindowManager->initTestGUI();

	}  // end if

	// create the IME manager
	TheIMEManager = CreateIMEManagerInterface();
	if ( TheIMEManager )
	{
		TheIMEManager->init();
 		TheIMEManager->setName("TheIMEManager");
	}

	// create the shell
	TheShell = MSGNEW("GameClientSubsystem") Shell;
	if( TheShell ) {
		TheShell->init();
 		TheShell->setName("TheShell");
	}

	// instantiate the in-game user interface
	TheInGameUI = createInGameUI();
	if( TheInGameUI ) {
		TheInGameUI->init();
 		TheInGameUI->setName("TheInGameUI");
	}

 	TheChallengeGenerals = createChallengeGenerals();
 	if( TheChallengeGenerals ) {
 		TheChallengeGenerals->init();
 	}

	TheHotKeyManager = MSGNEW("GameClientSubsystem") HotKeyManager;
	if( TheHotKeyManager ) {
		TheHotKeyManager->init();
 		TheHotKeyManager->setName("TheHotKeyManager");
	}

	// instantiate the terrain visual display
	TheTerrainVisual = createTerrainVisual();
	if( TheTerrainVisual ) {
		TheTerrainVisual->init();
 		TheTerrainVisual->setName("TheTerrainVisual");
	}

	// allocate the ray effects manager
	TheRayEffects = MSGNEW("GameClientSubsystem") RayEffectSystem;
	if( TheRayEffects )	{
		TheRayEffects->init();
 		TheRayEffects->setName("TheRayEffects");
	}

	TheMouse->init();	//finish initializing the mouse.

	// set the limits of the mouse now that we've created the display and such
	if( TheMouse )
	{
		TheMouse->setPosition( 0, 0 );
		TheMouse->setMouseLimits();
 		TheMouse->setName("TheMouse");
	}  // end if

	// create the video player
	TheVideoPlayer = createVideoPlayer();
	if ( TheVideoPlayer )
	{
		TheVideoPlayer->init();
 		TheVideoPlayer->setName("TheVideoPlayer");
	}

	// create the language filter.
	TheLanguageFilter = createLanguageFilter();
	if (TheLanguageFilter)
	{
		TheLanguageFilter->init();
 		TheLanguageFilter->setName("TheLanguageFilter");
	}

	TheCampaignManager = MSGNEW("GameClientSubsystem") CampaignManager;
	TheCampaignManager->init();

	TheEva = MSGNEW("GameClientSubsystem") Eva;
	TheEva->init();
 	TheEva->setName("TheEva");

	TheDisplayStringManager->postProcessLoad();

	TheSnowManager = createSnowManager();
	if (TheSnowManager)
	{
		TheSnowManager->init();
		TheSnowManager->setName("TheSnowManager");
	}

#ifdef PERF_TIMERS
	TheGraphDraw = new GraphDraw;
#endif

	TheGameConsole = new GameConsole;

}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset the game client for a new game */
void GameClient::reset( void )
{
	Drawable *draw;
//	m_drawableHash.clear();
//	m_drawableHash.resize(DRAWABLE_HASH_SIZE);

	m_drawableVector.clear();
	m_drawableVector.resize(DRAWABLE_HASH_SIZE, NULL);

	// last frame's health bars belong to the map we are leaving
	m_healthBarPickList.clear();

	// and so does the colour table: it is built out of a roster and a set of alliances that the
	// next map will not have
	invalidatePlayerColorScheme();

	// need to reset the in game UI to clear drawables before they are destroyed
	TheInGameUI->reset();

	// the team keys pressed in the match we are leaving will never land, and the next one counts
	// its frames from zero again (init attaches no translators without a message stream)
	if (TheSelectionTranslator)
		TheSelectionTranslator->forgetPendingSquads();

	// destroy all Drawables.  The clear-game tick resets from inside the tick, where what the tick
	// destroyed before it stays on the list until the sync point; destroying one of those again queued
	// it twice, and the flush below deleted it twice: the "Pure virtual function called" box on
	// leaving a match.
	std::vector<Drawable *> listed;
	for( draw = m_drawableList; draw; draw = draw->getNextDrawable() )
	{
		if( std::find( m_destroyedInTick.begin(), m_destroyedInTick.end(), draw ) == m_destroyedInTick.end() )
			listed.push_back( draw );
	}
	for( size_t i = 0; i < listed.size(); ++i )
		destroyDrawable( listed[ i ] );
	// the clear-game tick resets from inside the tick: what it made before that was never linked
	for( size_t i = 0; i < m_madeInTick.size(); ++i )
	{
		if( std::find( m_destroyedInTick.begin(), m_destroyedInTick.end(), m_madeInTick[ i ] ) == m_destroyedInTick.end() )
			destroyDrawable( m_madeInTick[ i ] );
	}
	m_madeInTick.clear();
	m_drawableList = NULL;
	// the clear-game tick resets from inside the tick, and what it and the ticks before left goes
	// before the scene does
	flushDestroyedDrawables();

	TheDisplay->reset();
	TheTerrainVisual->reset();
	TheRayEffects->reset();
	TheVideoPlayer->reset();
	TheEva->reset();
	if (TheSnowManager)
		TheSnowManager->reset();

	// clear any drawable TOC we might have
	m_drawableTOC.clear();

}  // end reset

/** -----------------------------------------------------------------------------------------------
 * Return a new unique object id.
 */
DrawableID GameClient::allocDrawableID( void )
{
	/// @todo Find unused value in current set
	DrawableID ret = m_nextDrawableID;
	m_nextDrawableID = (DrawableID)((UnsignedInt)m_nextDrawableID + 1);
	return ret;
}

/** -----------------------------------------------------------------------------------------------
 * Given a drawable, register it with the GameClient and give it a unique ID.
 */
void GameClient::registerDrawable( Drawable *draw )
{

	// assign this drawable a unique ID, this will add it to the fast lookup table too
	draw->setID( allocDrawableID() );

	// LOGIC-THREAD.md phase 3: the list and the lookup do not change inside a tick, which the render
	// will run beside.  What a tick makes waits in m_madeInTick (addDrawableToLookupTable put it there)
	// for linkDrawablesOfTick, and only the tick's own lookups find it before that.
	if( Drawable::tickDefersLifetime() )
		return;

	// add the drawable to the master list
	draw->prependToList( &m_drawableList );

}  // end registerDrawable

/** -----------------------------------------------------------------------------------------------
 * -online, once a client pass.  The relay session gets its keepalive while the lobby has no Transport
 * to pump it, and a match's report goes out on the first pass after the match is decided, without
 * waiting for the score screen a player may never reach.  Here, before prepareOverlap, no tick runs
 * beside it, so reading the victory conditions and the score keepers is safe.
 */
static void updateOnline( void )
{
	if (!OnlineIsEnabled())
		return;
	Transport::keepOnlineRelay();

	static Bool s_reported = FALSE;
	if (!TheGameLogic->isInGame() || TheGameLogic->getGameMode() != GAME_INTERNET
		|| !TheGameSpyGame || !TheGameSpyGame->isGameInProgress())
	{
		s_reported = FALSE;
		return;
	}
	if (!s_reported && TheVictoryConditions && TheVictoryConditions->getEndFrame() > 0)
	{
		s_reported = TRUE;
		TheGameSpyGame->sendOnlineReport( "endframe" );
	}
}

/** -----------------------------------------------------------------------------------------------
 * Redraw all views, update the GUI, play sound effects, etc.
 */
DECLARE_PERF_TIMER(GameClient_update)
DECLARE_PERF_TIMER(GameClient_draw)
void GameClient::update( void )
{
	USE_PERF_TIMER(GameClient_update)
	updateOnline();
	// create the FRAME_TICK message
	GameMessage *frameMsg = TheMessageStream->appendMessage( GameMessage::MSG_FRAME_TICK );
	frameMsg->appendTimestampArgument( getFrame() );
	static Bool playSizzle = FALSE;
	// We need to show the movie first.
	if(TheGlobalData->m_playIntro && !TheDisplay->isMoviePlaying())
	{
		if(TheGameLODManager && TheGameLODManager->didMemPass())
			TheDisplay->playLogoMovie("EALogoMovie", 5000, 3000);
		else
			TheDisplay->playLogoMovie("EALogoMovie640", 5000, 3000);
		DEBUG_LOG(("GameClient: the EA logo movie %s\n", TheDisplay->isMoviePlaying() ? "is playing" : "did not start (no stream: video off, or no file)"));
		// Escape gets you out of this one too. The flag is what the key handler asks, and it was
		// only ever raised once the logo had finished playing itself out, so the one movie you see
		// on every single launch was the one you could not skip.
		TheWritableGlobalData->m_allowExitOutOfMovies = TRUE;
		TheWritableGlobalData->m_playIntro = FALSE;
		TheWritableGlobalData->m_afterIntro = TRUE;
		playSizzle = TRUE;
	}

	//Initial Game Codition.  We must show the movie first and then we can display the shell	
	if(TheGlobalData->m_afterIntro && !TheDisplay->isMoviePlaying())
	{
		if( playSizzle && TheGlobalData->m_playSizzle )
		{
			TheWritableGlobalData->m_allowExitOutOfMovies = TRUE;
			if(TheGameLODManager && TheGameLODManager->didMemPass())
				TheDisplay->playMovie("Sizzle");
			else
				TheDisplay->playMovie("Sizzle640");
			playSizzle = FALSE;
		}
		else
		{
			TheWritableGlobalData->m_breakTheMovie = TRUE;
			TheWritableGlobalData->m_allowExitOutOfMovies = TRUE;

			if(TheGameLODManager && !TheGameLODManager->didMemPass())
			{
				TheWritableGlobalData->m_breakTheMovie = FALSE;

				WindowLayout *legal = TheWindowManager->winCreateLayout("Menus/LegalPage.wnd");
				if(legal)
				{				
					legal->hide(FALSE);
					legal->bringForward();
					Int beginTime = Clock_Milliseconds();
					while(beginTime + 4000 > Clock_Milliseconds() )
					{
						TheWindowManager->update();
						// redraw all views, update the GUI
						TheDisplay->draw();
						sleepMilliseconds( 100 );
					}
					setFPMode();


					legal->destroyWindows();
					legal->deleteInstance();

				}
				TheWritableGlobalData->m_breakTheMovie = TRUE;

				
			}

			TheShell->showShellMap(TRUE);
			TheShell->showShell();
			// A start from the command line (-file, a replay, a save) has no main menu: showShell returns
			// before pushing one, and MainMenuInit is what clears this flag.  Left set, W3DDisplay never
			// begins a frame, so a campaign map started that way, whose load screen does not clear it either,
			// drew nothing and presented nothing for the whole mission.  Render-only: nothing else reads it.
			if( !TheGlobalData->m_initialFile.isEmpty() )
				TheWritableGlobalData->m_breakTheMovie = FALSE;
			TheWritableGlobalData->m_afterIntro = FALSE;
		}
	}

	//Update snow particles.
	if (TheSnowManager)
		TheSnowManager->UPDATE();

	// update animation 2d collection
	TheAnim2DCollection->UPDATE();

	// update the keyboard
	if( TheKeyboard )
	{
		TheKeyboard->UPDATE();
		TheKeyboard->createStreamMessages();

	}  // end if

	// Update the Eva stuff
	TheEva->UPDATE();

	// update the mouse
	if( TheMouse )
	{
		TheMouse->UPDATE();
		TheMouse->createStreamMessages();

	}  // end if
	

  if (TheInGameUI->isCameraTrackingDrawable())
  {
    // Stop following a unit the moment it can no longer be seen - stealthed, or gone into fog or
    // shroud for the player whose fog is drawn - or the camera shows where it went.
    Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
    const Object *object = draw ? draw->getObject() : NULL;
    Bool isVisible = object != NULL && !draw->isDrawableEffectivelyHidden();
    if ( isVisible )
    {
      const ObjectShroudStatus shroudStatus = object->getShroudedStatus( TheObserverCamera.getShroudPlayerIndex() );
      isVisible = shroudStatus == OBJECTSHROUD_CLEAR || shroudStatus == OBJECTSHROUD_PARTIAL_CLEAR;
    }

    if ( isVisible )
    {
      const Coord3D *pos = draw->getPosition();
      TheTacticalView->lookAt( pos );
    }
    else
      TheInGameUI->setCameraTrackingDrawable( FALSE );
  }

	if(TheGlobalData->m_playIntro || TheGlobalData->m_afterIntro)
	{
		//
		// The cursor stays hidden until the main menu comes up, so through the logos and the
		// sizzle reel the pointer was gone: clicking through them was aiming blind, and on a
		// second monitor it looked like the game had hung.
		//
		if( TheMouse )
			TheMouse->setVisibility( TRUE );

		// Update before drawing, the way every other pass does it.  The other order drew the
		// frame the update was about to change, so the intro was always one frame behind.
		{
			TheDisplay->UPDATE();
		}
		{
			TheDisplay->DRAW();
		}
		return;
	}

	// update the window system itself
	{
		TheWindowManager->UPDATE();
	}

	// update the video player
	{
		TheVideoPlayer->UPDATE();
	}

	// Who is drawn in which colour can move while a match runs: the options screen is reachable
	// from inside a game, and a script can make or break an alliance. A model's tint is baked into
	// its render object when the model is built and nothing repaints itself, so when the table
	// actually changes every drawable is handed its colour again. The draw module compares before
	// rebuilding, so the ones whose colour did not move cost a comparison.
	if( updatePlayerColorScheme() )
	{
		for( Drawable *draw = m_drawableList; draw; draw = draw->getNextDrawable() )
			draw->refreshIndicatorColor();

		// the observer bar writes its names' colours once, when it is filled, so it is filled again
		if( TheControlBar->isObserverControlBarOn() )
		{
			TheControlBar->populateObserverList();
			TheControlBar->populateObserverInfoWindow();
		}
	}

	Bool freezeTime = TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished();
	freezeTime = freezeTime || TheScriptEngine->isTimeFrozenDebug();
	freezeTime = freezeTime || TheScriptEngine->isTimeFrozenScript();
	freezeTime = freezeTime || TheGameLogic->isGamePaused();
	// the player whose fog is drawn: this machine's, or the one a watcher looks through with fog on
	Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;

	// hack to let client spin fast in network games but still do effects at the same pace. -MDC
	static UnsignedInt lastFrame = ~0;
	freezeTime = freezeTime || (lastFrame == m_frame);
	lastFrame = m_frame;

	if (!freezeTime)
	{
		Int numNonLocalPlayers=0;
		Int nonLocalPlayerIndices[MAX_PLAYER_COUNT];

#if defined(_DEBUG) || defined(_INTERNAL)
		if (TheGlobalData->m_shroudOn)
#else
		if (true)
#endif
		{	
			// Whether every player's fog memory is kept is a runtime question now: an observer, and
			// anyone who can take a player over mid-match, needs all of them.  It used to be the
			// compile-time DEBUG_FOG_MEMORY, so in a build anyone plays it was never on and
			// switching player handed you the first player's fog.
			if (TheGhostObjectManager->trackAllPlayers())
			{
				//Find indices of all active players
				Int numPlayers=ThePlayerList->getPlayerCount();
				for (Int i=0; i<numPlayers; i++)
				{	Player *player=ThePlayerList->getNthPlayer(i);
					if (player->getPlayerIndex() != localPlayerIndex)
						nonLocalPlayerIndices[numNonLocalPlayers++]=player->getPlayerIndex();
				}
				//update ghostObjects which don't have drawables or objects.
				TheGhostObjectManager->updateOrphanedObjects(nonLocalPlayerIndices,numNonLocalPlayers);
			}
			else
			{
				TheGhostObjectManager->updateOrphanedObjects(NULL,0);
			}
		}


		// call the update for all client drawables
		Drawable* draw = firstDrawable();
		while (draw)
		{	// update() could free the Drawable, so go ahead and grab 'next'
			Drawable* next = draw->getNextDrawable();
#if defined(_DEBUG) || defined(_INTERNAL)
			if (TheGlobalData->m_shroudOn)
#else
			if (true)
#endif
			{	//immobile objects need to take snapshots whenever they become fogged
				//so need to refresh their status.  We can't rely on external calls
				//to getShroudStatus() because they are only made for visible on-screen
				//objects.
				Object *object=draw->getObject();
				if (object)
				{
					// keep the other players' view of this object current too, so their fog memory is
					// right the moment you become one of them.  Fog memory is the ghost's snapshot, and
					// only an immobile object has a ghost (PartitionData::attachToObject); asking for a
					// unit kept nothing and cost an observer a status call per player for every unit on
					// the map every pass.  Whoever asks about a unit later works its answer out then.
					const PartitionData *partition = object->friend_getPartitionData();
					if (numNonLocalPlayers > 0 && partition != NULL && partition->getGhostObject() != NULL)
					{
						Int *playerIndex=nonLocalPlayerIndices;
						for (Int p=0; p<numNonLocalPlayers; p++, playerIndex++)
							object->getShroudedStatus(*playerIndex);
					}
					ObjectShroudStatus ss=object->getShroudedStatus(localPlayerIndex);
					if (ss >= OBJECTSHROUD_FOGGED && draw->getShroudClearFrame()!=0) {
						UnsignedInt limit = 2*LOGICFRAMES_PER_SECOND;
						if (object->isEffectivelyDead()) {
							// extend the time, so we can see the dead plane blow up & crash.
							limit += 3*LOGICFRAMES_PER_SECOND;
						}
						if (TheGameLogic->getFrame() < limit + draw->getShroudClearFrame()) {
							// It's been less than 2 seconds since we could see them clear, so keep showing them.
							ss = OBJECTSHROUD_CLEAR;
						}
					}
					//
					// a structure the local player has planned but not started is drawn wherever it was
					// put, fog or no fog: it reveals nothing (Object_shroudClearingRange gives it no
					// sight at all), so without this it would be invisible to the player who placed it
					// and there would be nothing to click on to cancel it.
					//
					Bool ownPlacementSilhouette = object->isLocallyControlled()
						&& Object_isAwaitingBuilder( object->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ),
																				 object->getConstructionPercent() );

					draw->setFullyObscuredByShroud( GameClient_hiddenByShroud( ss, ownPlacementSilhouette ) );
				}
			}
			draw->updateDrawable();
			draw = next;
		}
	}

	// LOGIC-THREAD.md phase 3: what the draw below asks of the shroud, asked here, on every pass and
	// frozen time included, as the draw asked it.  The walk above has filled the cache for this player,
	// so this reads it; the draw no longer calls into the partition beside a tick.
	for( Drawable *draw = m_drawableList; draw; draw = draw->getNextDrawable() )
	{
		Object *object = draw->getObject();
		Drawable::ObjectForDraw forDraw = draw->getObjectForDraw();
		forDraw.hasObject = object != NULL;
		if( object )
		{
			draw->setShroudForDraw( object->getShroudedStatus( localPlayerIndex ), object->isEffectivelyDead() );
			// and the rest of what the scene asks of it
			forDraw.awaitingBuilder = Object_isAwaitingBuilder( object->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ),
																													object->getConstructionPercent() );
			forDraw.empDisabled = object->isDisabledByType( DISABLED_EMP );
			forDraw.outage = object->isKindOf( KINDOF_STRUCTURE )
				&& ( object->isDisabledByType( DISABLED_UNDERPOWERED ) || object->isDisabledByType( DISABLED_SCRIPT_UNDERPOWERED )
					|| object->isDisabledByType( DISABLED_HACKED ) );
			const Player *player = object->getControllingPlayer();
			forDraw.playerIndex = player->getPlayerIndex();
			forDraw.playerColor = clientPlayerColor( player );
			forDraw.safeOcclusionFrame = object->getSafeOcclusionFrame();
			forDraw.layer = object->getLayer();
			if( forDraw.layer != LAYER_GROUND )
				forDraw.layerHeight = TheTerrainLogic->getLayerHeight( draw->getPosition()->x, draw->getPosition()->y, forDraw.layer );
			draw->setObjectForDraw( forDraw );
			continue;
		}
		forDraw.awaitingBuilder = FALSE;
		draw->setObjectForDraw( forDraw );
		//Hack for prison camps which contain enemy prisoner drawables: another object holds the shroud
		ObjectShroudStatus status = OBJECTSHROUD_CLEAR;
		const ObjectID shroudObjectID = draw->getDrawableInfo()->m_shroudStatusObjectID;
		if( shroudObjectID != INVALID_ID )
		{
			const Object *shroudObject = TheGameLogic->findObjectByID( shroudObjectID );
			if( shroudObject )
				status = shroudObject->getShroudedStatus( localPlayerIndex );
		}
		draw->setShroudForDraw( status, FALSE );
	}

	// update all particle systems
	if( !freezeTime )
	{
		// update particle systems
		TheParticleSystemManager->setLocalPlayerIndex(localPlayerIndex);
//		TheParticleSystemManager->update();

	}  // end if

	// update the terrain visuals
	{
		TheTerrainVisual->UPDATE();
	}

	// update display
	{
		TheDisplay->UPDATE();
	}

	{
		USE_PERF_TIMER(GameClient_draw)

	// redraw all views, update the GUI
	//if(TheGameLogic->getFrame() >= 2)

		/* -headless never draws.  The device and every drawable stay alive - this skips the frame,
			 not the client - which is what makes an unattended run fast: rendering is the larger half
			 of the loop's wall clock in a normal match.  -jumpToFrame skips it the same way until its
			 frame, past the first, which draws the load away. */
		const Bool jumping = TheGlobalData->m_noDraw > TheGameLogic->getFrame() && TheGameLogic->getFrame() > 0;
		if (!TheGlobalData->m_headless && !jumping)
		{
#ifdef DEBUG_LOGGING
			Int64 drawStart, drawEnd;
			drawStart = Clock_Ticks();
#endif
			// LOGIC-THREAD.md phase 3, step 3b: at 1 and 2 the message stream and the pacer run here, so a
			// due tick can go to the logic thread when the draw's sync point is done
			LogicThread::prepareOverlap();
			/* -directorrecord's panes: every pane past the first is drawn first, for the recording only
				 and never presented, then the frame everybody sees.  W3D's clock stands still for every
				 draw after the first on one client frame, so nothing animates twice. */
			for( Int pane = 1; pane < TheObserverCamera.getDrawnPaneCount(); pane++ )
			{
				TheObserverCamera.beginPanePass( pane );
				TheDisplay->DRAW();
				TheObserverCamera.endPanePass();
			}
			TheDisplay->DRAW();
#ifdef DEBUG_LOGGING
			drawEnd = Clock_Ticks();
			Int64 freq;
			freq = Clock_Ticks_Per_Second();
			if( freq > 0 )
				TheClientDrawMS = (Real)((double)(drawEnd - drawStart) * 1000.0 / (double)freq);
#endif
		}
		else
		{
			//
			// The particle manager is updated from inside the draw, and its update is the only thing
			// that ever retires a particle system.  Skipping the draw therefore left every system the
			// logic created standing for the rest of the run: a four-player brutal match on one seed
			// held 1,008 of them at frame 6,000, 13,861 at 18,000 and 26,811 at 30,000, growing in a
			// straight line and never coming down.  30,000 frames is the length of a batch match.
			//
			// It cannot move the simulation: ParticleSys.cpp draws on GameClientRandomValue in all
			// thirty-three places it wants a random number and never on the logic stream, so this is
			// the same bookkeeping a rendered run does, on a run that has no picture.  The same seed
			// ends with 138 systems instead of 26,811, and the run takes 19.1s against 18.8 and 19.0.
			//
			if( TheParticleSystemManager )
				TheParticleSystemManager->update();
			// The light pulses are the same story: only the render faded and parked them, so a headless
			// match kept every muzzle flash of the match switched on, and each new one walked them all.
			TheDisplay->stepDynamicLights();
		}
	}

	{
		// let display string factory handle its update
		TheDisplayStringManager->update();
	}

	{
		// update the shell
		TheShell->UPDATE();
	}

	{
		// update the in game UI 
		TheInGameUI->UPDATE();
	}
}  // end update

/** -----------------------------------------------------------------------------------------------
 * Call the given callback function for each object contained within the given region.
 */
void GameClient::iterateDrawablesInRegion( Region3D *region, GameClientFuncPtr userFunc, void *userData )
{
	Drawable *draw, *nextDrawable;

	for( draw = m_drawableList; draw; draw=nextDrawable )
	{
		nextDrawable = draw->getNextDrawable();

		Coord3D pos = *draw->getPosition();
		if( region == NULL ||
			  (pos.x >= region->lo.x && pos.x <= region->hi.x &&
			   pos.y >= region->lo.y && pos.y <= region->hi.y &&
				 pos.z >= region->lo.z && pos.z <= region->hi.z) )
		{
			(*userFunc)( draw, userData );
		}
	}
}

/**Helper function to update fake GLA structures to become visible to certain players.
We should only call this during critical moments, such as changing teams, changing to
observer, etc.*/
void GameClient::updateFakeDrawables(void)
{
	for( Drawable *draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
	{
		const Object *object=draw->getObject();

		if( object && object->isKindOf( KINDOF_FS_FAKE ) )
		{	
			Relationship rel=ThePlayerList->getLocalPlayer()->getRelationship(object->getTeam());
			if (rel == ALLIES || rel == NEUTRAL)
				draw->setTerrainDecal(TERRAIN_DECAL_SHADOW_TEXTURE);
			else
				draw->setTerrainDecal(TERRAIN_DECAL_NONE);
		}
	}
}

/** -----------------------------------------------------------------------------------------------
 * Destroy the drawable immediately.
 */
void GameClient::destroyDrawable( Drawable *draw )
{
	// remove any notion of the Drawable in the in-game user interface
	TheInGameUI->disregardDrawable( draw );

	//
	// because drawables and objects are tightly coupled, not only MUST we maintain
	// our links in all instances, but it is NECESSARY for the client to actually
	// modify data in the logic, that is the pointer in an object to *this* drawable
	//
	Object *obj = draw->getObject();
	if( obj )
	{

		DEBUG_ASSERTCRASH( obj->getDrawable() == draw, ("Object/Drawable pointer mismatch!\n") );
		obj->friend_bindToDrawable( NULL );

	}  // end if

	/* LOGIC-THREAD.md phase 2, M4.  Inside a tick the drawable forgets its object, which the tick may
		 delete before the flush, and leaves the selection after the tick (disregardDrawable queues it).
		 Phase 3: it stays in the list and the lookup, which the render walks beside the tick, and only
		 the tick's own lookups stop finding it; linkDrawablesOfTick takes it out after the tick.  Its draw
		 modules, render objects, shadows and sounds go in flushDestroyedDrawables, once the flush has run
		 the work other drawables deferred. */
	if( Drawable::tickDefersLifetime() )
	{
		draw->friend_forgetObject();
		m_destroyedInTick.push_back( draw );
		return;
	}

	// remove from the master list
	draw->removeFromList(&m_drawableList);

	LOGIC_CENSUS( CENSUS_DESTROY_DRAWABLE );

	// what the tick deferred on it happens now or never: the batched trees and props are made, placed
	// and destroyed in one go by startNewGame, and placing them is what hands them to the terrain
	draw->flushPendingTransform( TRUE );

	// remove the drawable from our hash of drawables
	removeDrawableFromLookupTable( draw );

	// free storage
	draw->deleteInstance();

}

//-------------------------------------------------------------------------------------------------
void GameClient::flushDestroyedDrawables( void )
{
	for( size_t i = 0; i < m_destroyedInTick.size(); ++i )
	{
		LOGIC_CENSUS( CENSUS_DESTROY_DRAWABLE );
		// what the tick deferred on it before it died, which flushPendingClientWork no longer finds by ID:
		// the last mine's detonation FX, and the model's last place as on the immediate path above.  One
		// the same tick made was never drawn, and building its render objects only to delete them is not
		// worth the effect: it keeps the transform alone, as before
		Drawable *draw = m_destroyedInTick[ i ];
		if( draw->isRenderPending() )
			draw->flushPendingTransform( FALSE );
		else
			draw->doPendingClientWork( FALSE );
		draw->deleteInstance();
	}
	m_destroyedInTick.clear();
}

//-------------------------------------------------------------------------------------------------
/** LOGIC-THREAD.md phase 3: the list and the lookup take the tick's changes here, at the sync point,
	in the order the tick made them, so the list ends up as it did when they were made at the call.
	First of the flush: the drawables' deferred work and the effects look drawables up. */
void GameClient::linkDrawablesOfTick( void )
{
	for( size_t i = 0; i < m_destroyedInTick.size(); ++i )
	{
		Drawable *draw = m_destroyedInTick[ i ];
		// one the same tick made was never linked
		if( draw->getID() < m_drawableVector.size() && m_drawableVector[ draw->getID() ] == draw )
		{
			draw->removeFromList( &m_drawableList );
			removeDrawableFromLookupTable( draw );
		}
	}
	for( size_t i = 0; i < m_madeInTick.size(); ++i )
	{
		Drawable *draw = m_madeInTick[ i ];
		// ponytail: a linear search per drawable made, a few dozen a tick; a flag on the drawable if it ever shows
		if( std::find( m_destroyedInTick.begin(), m_destroyedInTick.end(), draw ) != m_destroyedInTick.end() )
			continue;
		addDrawableToLookupTable( draw );
		draw->prependToList( &m_drawableList );
	}
	m_madeInTick.clear();
}

/** -----------------------------------------------------------------------------------------------
 * Given an object id, return the associated object.
 * This method is the primary interface for accessing objects, and should be used
 * instead of pointers to "attach" objects to each other.
 */
Drawable* GameClient::findDrawableByID( const DrawableID id )
{
	LOGIC_CENSUS( CENSUS_GAMECLIENT );
	if( id == INVALID_DRAWABLE_ID )
		return NULL;

	Drawable *draw = (Int)id < m_drawableVector.size() ? m_drawableVector[(Int)id] : NULL;

	// inside the tick the table has not taken the tick's changes yet (linkDrawablesOfTick): the tick
	// still finds what it made and no longer what it destroyed, as when the table changed at the call
	if( Drawable::tickDefersLifetime() )
	{
		if( draw == NULL )
		{
			for( size_t i = 0; i < m_madeInTick.size(); ++i )
				if( m_madeInTick[ i ]->getID() == id )
					draw = m_madeInTick[ i ];
		}
		if( draw && std::find( m_destroyedInTick.begin(), m_destroyedInTick.end(), draw ) != m_destroyedInTick.end() )
			return NULL;
	}
	return draw;
}

// ------------------------------------------------------------------------------------------------
/** Add drawable to lookup table for fast id searching */
// ------------------------------------------------------------------------------------------------
void GameClient::addDrawableToLookupTable(Drawable *draw )
{

	// sanity
	if( draw == NULL )
		return;

	// made inside a tick: linkDrawablesOfTick adds it (registerDrawable)
	if( Drawable::tickDefersLifetime() )
	{
		m_madeInTick.push_back( draw );
		return;
	}

	// add to lookup
//	m_drawableHash[ draw->getID() ] = draw;
	DrawableID newID = draw->getID();
	while( newID >= m_drawableVector.size() ) // Fail case is hella rare, so faster to double up on size() call
		m_drawableVector.resize(m_drawableVector.size() * 2, NULL);

	m_drawableVector[ newID ] = draw;

}  // end addDrawableToLookupTable

// ------------------------------------------------------------------------------------------------
/** Remove drawable from lookup table of fast id searching */
// ------------------------------------------------------------------------------------------------
void GameClient::removeDrawableFromLookupTable( Drawable *draw )
{

	// sanity
	if( draw == NULL )
		return;

	// remove from table
//	m_drawableHash.erase( draw->getID() );
	m_drawableVector[ draw->getID() ] = NULL;

}  // end removeDrawableFromLookupTable

//-------------------------------------------------------------------------------------------------
/** Load a map into the game interface */
Bool GameClient::loadMap( AsciiString mapName )
{

	// sanity
	if( mapName.isEmpty() )
		return false;

	assert( 0 );  // who calls this?

	return TRUE;

}  // end loadMap

//-------------------------------------------------------------------------------------------------
/** Unload a map from the game interface */
void GameClient::unloadMap( AsciiString mapName )
{

	assert( 0 );  // who calls this?

}  // end unloadMap

//-------------------------------------------------------------------------------------------------
void GameClient::setTimeOfDay( TimeOfDay tod )
{
	Drawable *draw = firstDrawable();

	while( draw )
	{
		draw->setTimeOfDay( tod );

		draw = draw->getNextDrawable();
	}
}

//-------------------------------------------------------------------------------------------------
void GameClient::assignSelectedDrawablesToGroup( Int group )
{
/*
	Drawable *draw = firstDrawable();
	while( draw )
	{
	
		if( draw->isSelected() && draw->getObject()->isLocallyControlled())
		{
			draw->setDrawableGroup( group );
		}
		else if( draw->getDrawableGroup() == group )
		{
			draw->setDrawableGroup( 0 );
		}

		draw = draw->getNextDrawable();

	}
*/
}

//-------------------------------------------------------------------------------------------------
void GameClient::selectDrawablesInGroup( Int group )
{
/*
	Drawable *draw = firstDrawable();

	// create a message that will assign a group ID to all the selected drawables, this
	// way when we do things with this current selected group of units, we only have
	// to refer to the group ID and not each individual object ID
	GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

	//We are creating a new group from scratch.
	teamMsg->appendBooleanArgument( true );

	while( draw )
	{
		int counter = 0;
	
		const Object *object = draw->getObject();

		if( object && draw->getDrawableGroup() == group && object->isLocallyControlled() && !object->isContained() )
		{
			//Only select the object if it is locally controlled and not contained by anything.
			TheInGameUI->selectDrawable(draw);
			teamMsg->appendObjectIDArgument( draw->getObject()->getID() );
		}
		else
		{
			TheInGameUI->deselectDrawable(draw);
		}

		draw = draw->getNextDrawable();
		counter++;
	}

*/
}

// ------------------------------------------------------------------------------------------------
void GameClient::addTextBearingDrawable( Drawable *tbd )
{
	if ( tbd != NULL )
		m_textBearingDrawableList.push_back( tbd );
}
// ------------------------------------------------------------------------------------------------
void GameClient::flushTextBearingDrawables( void )
{

	/////////////////////////////
	// WALK THIS LIST AND CALL EACH DRAWABLES TEXTY STUFF
	/////////////////////////////

	for( TextBearingDrawableListIterator it = m_textBearingDrawableList.begin(); it != m_textBearingDrawableList.end(); ++it )
	  (*it)->drawUIText();

	m_textBearingDrawableList.clear();
}

// ------------------------------------------------------------------------------------------------
Bool GameClient_healthBarPickHit( const IRegion2D& region, const ICoord2D& screen, Int pad )
{
	return screen.x >= region.lo.x - pad && screen.x <= region.hi.x + pad &&
				 screen.y >= region.lo.y - pad && screen.y <= region.hi.y + pad;
}

// ------------------------------------------------------------------------------------------------
Int GameClient_healthBarPickScore( const IRegion2D& region, const ICoord2D& screen )
{
	const Int dx = screen.x - (region.lo.x + region.hi.x) / 2;
	const Int dy = screen.y - (region.lo.y + region.hi.y) / 2;

	return dx * dx + dy * dy;
}

// ------------------------------------------------------------------------------------------------
void GameClient::addHealthBarPickRegion( Drawable *draw, const IRegion2D& region )
{
	HealthBarPickEntry entry;

	entry.region = region;
	entry.id = draw->getID();

	m_healthBarPickList.push_back( entry );
}

// ------------------------------------------------------------------------------------------------
void GameClient::clearHealthBarPickRegions( void )
{
	m_healthBarPickList.clear();
}

// ------------------------------------------------------------------------------------------------
/** Pick by health bar.  This is a *fallback*: the callers reach it only when the pick ray found
	* nothing at all, so it can never take a click away from the unit whose model was actually under
	* the cursor - a bar floats above its own unit and so sits over whatever is standing behind it,
	* and "I clicked the tank at the back and got the one in front" would be worse than the miss it
	* is fixing.  It is also selection-only: the command translators keep the ray, or right-clicking
	* bare ground under a bar would order an attack instead of a move.
	*
	* And it is off entirely while the player already holds a selection.  With a selection in hand
	* the next click is aimed at the world, not shopping for a unit, and every bar on screen is then
	* a trap: a click on bare ground that happens to fall under someone's bar throws the selection
	* away and hands back the unit standing behind it. */
// ------------------------------------------------------------------------------------------------
Drawable *GameClient::pickDrawableByHealthBar( const ICoord2D *screen )
{
	if( TheInGameUI->getSelectCount() > 0 )
		return NULL;

	// the bar is three pixels of art; the click target is the bar plus a finger's width of slack
	const Int pad = REAL_TO_INT_CEIL( 3.0f * TheUIScale() );

	Drawable *best = NULL;
	Int bestScore = 0;

	for( HealthBarPickListIterator it = m_healthBarPickList.begin(); it != m_healthBarPickList.end(); ++it )
	{
		if( !GameClient_healthBarPickHit( it->region, *screen, pad ) )
			continue;

		const Int score = GameClient_healthBarPickScore( it->region, *screen );
		if( best != NULL && score >= bestScore )
			continue;

		//
		// the list is last frame's drawing and this is this frame's click, so a drawable that died in
		// between is genuinely gone
		//
		Drawable *draw = findDrawableByID( it->id );
		if( draw == NULL )
			continue;

		best = draw;
		bestScore = score;
	}

	return best;
}

// ------------------------------------------------------------------------------------------------
GameMessage::Type GameClient::evaluateContextCommand( Drawable *draw,
																											const Coord3D *pos,
																											CommandTranslator::CommandEvaluateType cmdType )
{

	if( m_commandTranslator )
		return m_commandTranslator->evaluateContextCommand( draw, pos, cmdType );
	else
		return GameMessage::MSG_INVALID;

}  // end evaluateContextCommand

//-------------------------------------------------------------------------------------------------
/** Get the ray effect data for a drawable */
void GameClient::getRayEffectData( Drawable *draw, RayEffectData *effectData )
{

	TheRayEffects->getRayEffectData( draw, effectData );

}  // end getRayEffectData

//-------------------------------------------------------------------------------------------------
/** remove the drawble from the ray effects sytem if present */
void GameClient::removeFromRayEffects( Drawable *draw )
{

	TheRayEffects->deleteRayEffect( draw );

}  // end removeFromRayEffects

/** frees all shadow resources used by this module - used by Options screen.*/
void GameClient::releaseShadows(void)
{
	Drawable *draw;
	for( draw = firstDrawable(); draw; draw = draw->getNextDrawable() )
		draw->releaseShadows();
}

/** create shadow resources if not already present. Used by Options screen.*/
void GameClient::allocateShadows(void)
{
	Drawable *draw;
	for( draw = firstDrawable(); draw; draw = draw->getNextDrawable() )
		draw->allocateShadows();
}

//-------------------------------------------------------------------------------------------------
/* The memory figures preloadAssets logs before and after each step: the three it reads of Windows'
	 MEMORYSTATUS.  Windows fills them from GlobalMemoryStatus, as preloadAssets always did.  Elsewhere
	 they come from CPUDetectClass::Query_Memory, the platform code CPUDetectClass itself uses (sysctl
	 and Mach on Darwin, sysconf on Linux), asked again each time and unclamped: CPUDetectClass's own
	 getters stop at 2GB-1, and before and after would read the same on any machine with 2GB free. */
struct PreloadMemoryStatus
{
	size_t availPageFile;
	size_t availPhys;
	size_t availVirtual;
};

static void getPreloadMemoryStatus( PreloadMemoryStatus &status )
{
#if defined(_WIN32)
	MEMORYSTATUS memory;
	GlobalMemoryStatus( &memory );
	status.availPageFile = memory.dwAvailPageFile;
	status.availPhys = memory.dwAvailPhys;
	status.availVirtual = memory.dwAvailVirtual;
#else
	unsigned long long totalPhys, availPhys, totalPage, availPage, totalVirtual, availVirtual;
	CPUDetectClass::Query_Memory( totalPhys, availPhys, totalPage, availPage, totalVirtual, availVirtual );
	status.availPageFile = (size_t)availPage;
	status.availPhys = (size_t)availPhys;
	status.availVirtual = (size_t)availVirtual;
#endif
}

//-------------------------------------------------------------------------------------------------
/** Preload assets for the currently loaded map.  Those assets include all the damage states
	* for every building loaded, as well as any faction units/structures we can build and
	* all their damage states */
//-------------------------------------------------------------------------------------------------
void GameClient::preloadAssets( TimeOfDay timeOfDay )
{

	PreloadMemoryStatus before, after;
	getPreloadMemoryStatus(before);

	// first, for every drawable in the map load the assets for all states we care about
	Drawable *draw;
	for( draw = firstDrawable(); draw; draw = draw->getNextDrawable() )
		draw->preloadAssets( timeOfDay );

	//
	// now create a temporary drawble for each of the faction things we can create, preload
	// their assets, and dump the drawable
	//
	AsciiString side;
	const ThingTemplate *tTemplate;
	Int preloaded = 0, considered = 0;
	Int64 preloadStart, preloadEnd, preloadFreq = 0;
	preloadStart = Clock_Ticks();
	for( tTemplate = TheThingFactory->firstTemplate();
			 tTemplate;
			 tTemplate = tTemplate->friend_getNextTemplate() )
	{
		++considered;

		// if this isn't one of the objects that can be preloaded ignore it.  A parachute and what hangs
		// under it (a Supply Drop Zone crate) are made in the tick and asked for their bones in the same
		// tick (ParachuteContain), before their flush; made here first, their module data has the bones
		// already and the tick loads no model (LOGIC-THREAD.md M8)
		if( tTemplate->isKindOf( KINDOF_PRELOAD ) == FALSE && tTemplate->isKindOf( KINDOF_PARACHUTE ) == FALSE
				&& tTemplate->isKindOf( KINDOF_PARACHUTABLE ) == FALSE && !TheGlobalData->m_preloadEverything )
			continue;

		// create the drawable and do the preloading
		draw = TheThingFactory->newDrawable( tTemplate );
		if( draw )
		{
			++preloaded;

			// preload the assets
			draw->preloadAssets( timeOfDay );

			// destroy the drawable
			TheGameClient->destroyDrawable( draw );

		}  // end if

	}  // end for
	preloadEnd = Clock_Ticks();
	preloadFreq = Clock_Ticks_Per_Second();
	DEBUG_LOG(("PRELOAD: %d of %d templates in %.0f ms\n", preloaded, considered,
						 preloadFreq ? (Real)((double)(preloadEnd - preloadStart) * 1000.0 / (double)preloadFreq) : 0.0f));
	getPreloadMemoryStatus(after);

	DEBUG_LOG(("Preloading memory dwAvailPageFile %d --> %d : %d\n",
		before.availPageFile, after.availPageFile, before.availPageFile - after.availPageFile));
	DEBUG_LOG(("Preloading memory dwAvailPhys     %d --> %d : %d\n",
		before.availPhys, after.availPhys, before.availPhys - after.availPhys));
	DEBUG_LOG(("Preloading memory dwAvailVirtual  %d --> %d : %d\n",
		before.availVirtual, after.availVirtual, before.availVirtual - after.availVirtual));
	/*
	DEBUG_LOG(("Preloading memory dwLength        %d --> %d : %d\n",
		before.dwLength, after.dwLength, before.dwLength - after.dwLength));
	DEBUG_LOG(("Preloading memory dwMemoryLoad    %d --> %d : %d\n",
		before.dwMemoryLoad, after.dwMemoryLoad, before.dwMemoryLoad - after.dwMemoryLoad));
	DEBUG_LOG(("Preloading memory dwTotalPageFile %d --> %d : %d\n",
		before.dwTotalPageFile, after.dwTotalPageFile, before.dwTotalPageFile - after.dwTotalPageFile));
	DEBUG_LOG(("Preloading memory dwTotalPhys     %d --> %d : %d\n",
		before.dwTotalPhys , after.dwTotalPhys, before.dwTotalPhys - after.dwTotalPhys));
	DEBUG_LOG(("Preloading memory dwTotalVirtual  %d --> %d : %d\n",
		before.dwTotalVirtual , after.dwTotalVirtual, before.dwTotalVirtual - after.dwTotalVirtual));
	*/

	getPreloadMemoryStatus(before);
	extern std::vector<AsciiString>	debrisModelNamesGlobalHack;
	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for (i =0; i<debrisModelNamesGlobalHack.size(); ++i)
	{
		TheDisplay->preloadModelAssets(debrisModelNamesGlobalHack[i]);
	}
	getPreloadMemoryStatus(after);
	debrisModelNamesGlobalHack.clear();

	DEBUG_LOG(("Preloading memory dwAvailPageFile %d --> %d : %d\n",
		before.availPageFile, after.availPageFile, before.availPageFile - after.availPageFile));
	DEBUG_LOG(("Preloading memory dwAvailPhys     %d --> %d : %d\n",
		before.availPhys, after.availPhys, before.availPhys - after.availPhys));
	DEBUG_LOG(("Preloading memory dwAvailVirtual  %d --> %d : %d\n",
		before.availVirtual, after.availVirtual, before.availVirtual - after.availVirtual));

	TheControlBar->preloadAssets( timeOfDay );

	getPreloadMemoryStatus(before);
	TheParticleSystemManager->preloadAssets( timeOfDay );
	getPreloadMemoryStatus(after);

	DEBUG_LOG(("Preloading memory dwAvailPageFile %d --> %d : %d\n",
		before.availPageFile, after.availPageFile, before.availPageFile - after.availPageFile));
	DEBUG_LOG(("Preloading memory dwAvailPhys     %d --> %d : %d\n",
		before.availPhys, after.availPhys, before.availPhys - after.availPhys));
	DEBUG_LOG(("Preloading memory dwAvailVirtual  %d --> %d : %d\n",
		before.availVirtual, after.availVirtual, before.availVirtual - after.availVirtual));

	char *textureNames[] = {
		"ptspruce01.tga",
		"exrktflame.tga",
		"cvlimo3_d2.tga",
		"exfthrowerstream.tga",
		"uvrockbug_d1.tga",
		"arcbackgroundc.tga",
		"grade3.tga",
		"framebasec.tga",
		"gradec.tga",
		"frametopc.tga",
		"arcbackgrounda.tga",
		"arcglow2.tga",
		"framebasea.tga",
		"gradea.tga",
		"frametopa.tga",
		"sauserinterface256_002.tga",
		"sauserinterface256_001.tga",
		"unitbackgrounda.tga",
		"sauserinterface256_004.tga",
		"sagentank.tga",
		"sauserinterface256_005.tga",
		"sagenair.tga",
		"sauserinterface256_003.tga",
		"sagenspec.tga",
		"snuserinterface256_003.tga",
		"snuserinterface256_002.tga",
		"unitbackgroundc.tga",
		"snuserinterface256_004.tga",
		"sngenredarm.tga",
		"snuserinterface256_001.tga",
		"sngenspewea.tga",
		"sngensecpol.tga",
		"ciburn.tga",
		"ptmaple02.tga",
		"scuserinterface256_005.tga",
		"scuserinterface256_002.tga",
		"sauserinterface256_006.tga",
		"pmcrates.tga",
		""
	};

	getPreloadMemoryStatus(before);
	for (i=0; *textureNames[i]; ++i)
		TheDisplay->preloadTextureAssets(textureNames[i]);

	getPreloadMemoryStatus(after);

	DEBUG_LOG(("Preloading memory dwAvailPageFile %d --> %d : %d\n",
		before.availPageFile, after.availPageFile, before.availPageFile - after.availPageFile));
	DEBUG_LOG(("Preloading memory dwAvailPhys     %d --> %d : %d\n",
		before.availPhys, after.availPhys, before.availPhys - after.availPhys));
	DEBUG_LOG(("Preloading memory dwAvailVirtual  %d --> %d : %d\n",
		before.availVirtual, after.availVirtual, before.availVirtual - after.availVirtual));

//	preloadTextureNamesGlobalHack2 = preloadTextureNamesGlobalHack;
//	preloadTextureNamesGlobalHack.clear();

}  // end preloadAssets

// ------------------------------------------------------------------------------------------------
/** Given a string name, find the drawable TOC entry (if any) associated with it */
// ------------------------------------------------------------------------------------------------
GameClient::DrawableTOCEntry *GameClient::findTOCEntryByName( AsciiString name )
{

	for( DrawableTOCListIterator it = m_drawableTOC.begin(); it != m_drawableTOC.end(); ++it )
		if( (*it).name == name )
			return &(*it);

	return NULL;

}  // end findTOCEntryByname

// ------------------------------------------------------------------------------------------------
/** Given a drawable TOC identifier, find the drawable TOC if any */
// ------------------------------------------------------------------------------------------------
GameClient::DrawableTOCEntry *GameClient::findTOCEntryById( UnsignedShort id )
{

	for( DrawableTOCListIterator it = m_drawableTOC.begin(); it != m_drawableTOC.end(); ++it )
		if( (*it).id == id )
			return &(*it);

	return NULL;

}  // end findTOCEntryById

// ------------------------------------------------------------------------------------------------
/** Add an drawable TOC entry */
// ------------------------------------------------------------------------------------------------
void GameClient::addTOCEntry( AsciiString name, UnsignedShort id )
{

	DrawableTOCEntry tocEntry;
	tocEntry.name = name;
	tocEntry.id = id;
	m_drawableTOC.push_back( tocEntry );

}  // end addTOCEntry

// ------------------------------------------------------------------------------------------------
static Bool shouldSaveDrawable(const Drawable* draw)
{
	if (draw->testDrawableStatus(DRAWABLE_STATUS_NO_SAVE))
	{
		if (draw->getObject() == NULL)
		{
			return false;
		}
		else
		{
			DEBUG_CRASH(("You should not ever set DRAWABLE_STATUS_NO_SAVE for a Drawable with an object. (%s)\n",draw->getTemplate()->getName().str()));
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
/** Xfer drawable table of contents */
// ------------------------------------------------------------------------------------------------
void GameClient::xferDrawableTOC( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// clear our current table of contents
	m_drawableTOC.clear();

	// xfer the table
	UnsignedInt tocCount = 0;
	if( xfer->getXferMode() == XFER_SAVE )
	{
		AsciiString templateName;

		// generate a new TOC based on the drawables that are in the map
		for( Drawable *draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
		{
			if (!shouldSaveDrawable(draw))
				continue;

			// get the name we're working with
			templateName = draw->getTemplate()->getName();

			// if is this drawable name already in the TOC, skip it
			if( findTOCEntryByName( templateName ) != NULL )
				continue;
			
			// add this entry to the TOC
			addTOCEntry( draw->getTemplate()->getName(), ++tocCount );

		}  // end for obj

		// xfer entries in the TOC
		xfer->xferUnsignedInt( &tocCount );

		// xfer each TOC entry
		DrawableTOCListIterator it;
		DrawableTOCEntry *tocEntry;
		for( it = m_drawableTOC.begin(); it != m_drawableTOC.end(); ++it )
		{

			// get this toc entry
			tocEntry = &(*it);

			// xfer the name
			xfer->xferAsciiString( &tocEntry->name );

			// xfer the paired id
			xfer->xferUnsignedShort( &tocEntry->id );

		}  // end for

	}  // end if
	else
	{
		AsciiString templateName;
		UnsignedShort id;

		// how many entries are we going to read
		xfer->xferUnsignedInt( &tocCount );

		// read all the entries
		for( UnsignedInt i = 0; i < tocCount; ++i )
		{

			// read the name
			xfer->xferAsciiString( &templateName );

			// read the id
			xfer->xferUnsignedShort( &id );
			
			// add this to the TOC
			addTOCEntry( templateName, id );
					
		}  // end for i

	}  // end else

}  // end xferDrawableTOC

// ------------------------------------------------------------------------------------------------
/** Xfer method for Game Client
  * Version History:
  * 1: Initial
  * 2: Adding mission briefing history
	* 3: Added block markers around drawable data, no version checking is done and therefore
	*		 this version breaks compatibility with previous versions. (CBD)
 */
// ------------------------------------------------------------------------------------------------
void GameClient::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// client frame number
	xfer->xferUnsignedInt( &m_frame );

	//
	// note that we do not do the id counter here, we did it in the game state block because
	// it's important to do that part very early in the load process
	//
	// !!!DON'T DO THIS!!! ----> xfer->xferDrawableID( &m_nextDrawableID ); <---- !!!DON'T DO THIS!!! 

	//
	// xfer a table of contents that contain thing template and indentifier pairs.  this
	// table of contents is good for this save file only as unique numbers are only
	// generated and stored for the actual things that are on this map
	//
	xferDrawableTOC( xfer );

	// drawable count
	Drawable *draw;
	UnsignedShort drawableCount = 0;
	for( draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
	{
		if (xfer->getXferMode() == XFER_SAVE && !shouldSaveDrawable(draw))
			continue;
		drawableCount++;
	}
	xfer->xferUnsignedShort( &drawableCount );

	// drawable data
	DrawableTOCEntry *tocEntry;
	ObjectID objectID;
	if( xfer->getXferMode() == XFER_SAVE )
	{
	
		// iterate all drawables
		for( draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
		{
			if (!shouldSaveDrawable(draw))
				continue;

			// get TOC entry for this drawable
			tocEntry = findTOCEntryByName( draw->getTemplate()->getName() );
			if( tocEntry == NULL )
			{

				DEBUG_CRASH(( "GameClient::xfer - Drawable TOC entry not found for '%s'\n", draw->getTemplate()->getName().str() ));
				throw SC_INVALID_DATA;

			}  // end if

			// xfer toc id entry
			xfer->xferUnsignedShort( &tocEntry->id );

			// begin data block
			xfer->beginBlock();

			// write the object ID this drawable is attached to
			objectID = draw->getObject() ? draw->getObject()->getID() : INVALID_ID;
			xfer->xferObjectID( &objectID );

			// write snapshot data
			xfer->xferSnapshot( draw );

			// end data block
			xfer->endBlock();

		}  // end for, draw

	}  // end if, save
	else
	{
		UnsignedShort tocID;
		const ThingTemplate *thingTemplate;
		Int dataSize;

		// read all entries
		for( UnsignedShort i = 0; i < drawableCount; ++i )
		{

			// read toc id entry
			xfer->xferUnsignedShort( &tocID );

			// find TOC entry with this identifier
			tocEntry = findTOCEntryById( tocID );
			if( tocEntry == NULL )
			{

				DEBUG_CRASH(( "GameClient::xfer - No TOC entry match for id '%d'\n", tocID ));
				throw SC_INVALID_DATA;

			}  // end if

			// read data block size
			dataSize = xfer->beginBlock();

			// find matching thing template
			thingTemplate = TheThingFactory->findTemplate( tocEntry->name );
			if( thingTemplate == NULL )
			{

				DEBUG_CRASH(( "GameClient::xfer - Unrecognized thing template '%s', skipping.  ENGINEERS - Are you *sure* it's OK to be ignoring this object from the save file???  Think hard about it!\n",
											tocEntry->name.str() ));
				xfer->skip( dataSize );
				continue;

			}  // end if

			// read the object ID this drawable is attached to (if any)
			xfer->xferObjectID( &objectID );

			//
			// if we have an attached object ID, we won't create a new drawable, we'll use the
			// one that has been created and attached to the object already
			//
			if( objectID != INVALID_ID )
			{
				Object *object = TheGameLogic->findObjectByID( objectID );

				// sanity
				if( object == NULL )
				{

					DEBUG_CRASH(( "GameClient::xfer - Cannot find object '%d' that is supposed to be attached to this drawable '%s'\n",
												objectID, thingTemplate->getName().str() ));
					throw SC_INVALID_DATA;

				}  // end if

				// get the drawable from the object
				draw = object->getDrawable();
				if( draw == NULL )
				{

					DEBUG_CRASH(( "GameClient::xfer - There is no drawable attached to the object '%s' (%d) and there should be\n",
												object->getTemplate()->getName().str(), object->getID() ));
					throw SC_INVALID_DATA;

				}  // end if

				// srj sez: some objects (eg, diguised bombtrucks) may have an "abnormal" drawable. so check.
				//
				// note carefully: we do NOT want to use isEquivalentTo() here, because different object reskins
				// SHOULD count as different templates for our purposes here (which are purely visual). however, we
				// do need to compare getFinalOverride, because draw->getTemplate() is always gonna return the final
				// override, while TheThingFactory->findTemplate does not.
				//
				const ThingTemplate* drawTemplate = draw->getTemplate();
				if (drawTemplate->getFinalOverride() != thingTemplate->getFinalOverride())
				{
					TheGameClient->destroyDrawable( draw );
					draw = TheThingFactory->newDrawable( thingTemplate );
					TheGameLogic->bindObjectAndDrawable(object, draw);
				}

			}  // end if
			else
			{

				//
				// there was no object attached to this drawable when we saved, we need to create a
				// whole brand new drawable now
				//
				draw = TheThingFactory->newDrawable( thingTemplate );
				
				// sanity
				if( draw == NULL )
				{

					DEBUG_CRASH(( "GameClient::xfer - Unable to create drawable for '%s'\n",
												thingTemplate->getName().str() ));
					throw SC_INVALID_DATA;

				}  // end if

			}  // end else

			// xfer the drawable data
			xfer->xferSnapshot( draw );

			// end block (not necessary since this is a no-op but symettrically nice)
			xfer->endBlock();

		}  // end for, i

	}  // end else, load
	
	// xfer the in-game mission briefing history list
	if (version >= 2)
	{
		if( xfer->getXferMode() == XFER_SAVE )
		{
			BriefingList *bList = GetBriefingTextList();
			Int numEntries = bList->size();
			xfer->xferInt(&numEntries);
			DEBUG_LOG(("Saving %d briefing lines\n", numEntries));
			for (BriefingList::const_iterator bIt = bList->begin(); bIt != bList->end(); ++bIt)
			{
				AsciiString tempStr = *bIt;
				DEBUG_LOG(("'%s'\n", tempStr.str()));
				xfer->xferAsciiString(&tempStr);
			}
		}
		else // XFER_LOAD
		{
			Int numEntries = 0;
			xfer->xferInt(&numEntries);
			DEBUG_LOG(("Loading %d briefing lines\n", numEntries));
			UpdateDiplomacyBriefingText(AsciiString::TheEmptyString, TRUE); // clear out briefing list first
			while (numEntries-- > 0)
			{
				AsciiString tempStr;
				xfer->xferAsciiString(&tempStr);
				DEBUG_LOG(("'%s'\n", tempStr.str()));
				UpdateDiplomacyBriefingText(tempStr, FALSE);
			}
		}
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void GameClient::loadPostProcess( void )
{

	//
	// due to the fact that during the load process we have called newDrawable for drawables
	// without objects, and then overwrote their ids with data from the save file, our allocater
	// id may be far higher than it needs to be.  We'll pull it back down as low as we can
	//
	Drawable *draw;
	for( draw = getDrawableList(); draw; draw = draw->getNextDrawable() )
		if( draw->getID() >= m_nextDrawableID )
			m_nextDrawableID = (DrawableID)((UnsignedInt)draw->getID() + 1);

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void GameClient::crc( Xfer *xfer )
{

}  // end crc
