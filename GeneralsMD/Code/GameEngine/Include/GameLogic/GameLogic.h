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

// FILE: GameLogic.h //////////////////////////////////////////////////////////////////////////////
// GameLogic singleton class - defines interface to GameLogic methods and objects
// Author: Michael S. Booth, October 2000
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _GAME_LOGIC_H_
#define _GAME_LOGIC_H_

#include "Common/GameCommon.h"	// ensure we get DUMP_PERF_STATS, or not
#include "Common/GameType.h"
#include "Common/LogicThread.h"
#include "Common/Snapshot.h"
#include "Common/STLTypedefs.h"
#include "Common/ObjectStatusTypes.h"
#include "Common/RandomValue.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameLogic/CRCSnapshotRing.h"
#include "Common/STLTypedefs.h"
#include "GameLogic/Module/UpdateModule.h"	// needed for DIRECT_UPDATEMODULE_ACCESS

/*
	At one time, we distinguished between sleepy and nonsleepy
	update modules, and kept a separate list for each. however,
	now that the bulk of update modules are sleepy, profiling shows
	that there is no real advantage to having the separate list,
	so to simplify the world, I am removing it. If ALLOW_NONSLEEPY_UPDATES
	is still undefined when we ship, please just nuke all the undefed
	code. (srj)
*/
#define NO_ALLOW_NONSLEEPY_UPDATES

// forward declarations
class AudioEventRTS;
class AIGroup;
class Object;
class Drawable;
class Player;
class ThingTemplate;
class Team;
class CommandList;
class GameMessage;
class LoadScreen;
class WindowLayout;
class TerrainLogic;
class GhostObjectManager;
class CommandButton;
enum BuildableStatus : Int;


typedef const CommandButton* ConstCommandButtonPtr;

// What kind of game we're in.
enum
{
	GAME_SINGLE_PLAYER,
	GAME_LAN,
	GAME_SKIRMISH,
	GAME_REPLAY,
	GAME_SHELL,
	GAME_INTERNET,
	GAME_NONE
};

/* Which of those modes is worth writing a replay for.  EA asked this the other way round, in
   RecorderClass::updateRecord(), as a list of the modes *not* to record - shell, single player and
   none - which quietly said yes to GAME_REPLAY: a replay game whose recorder is in mode NONE (the
   -replay path used to leave it there) started recording the replay it was playing, over the file
   it was reading.  A whitelist cannot make that mistake, and a mode added later has to opt in. */
inline Bool isRecordableGameMode( Int gameMode )
{
	return gameMode == GAME_LAN || gameMode == GAME_SKIRMISH || gameMode == GAME_INTERNET;
}

enum
{
	CRC_CACHED,
	CRC_RECALC
};


/// Function pointers for use by GameLogic callback functions.
typedef void (*GameLogicFuncPtr)( Object *obj, void *userData ); 
//typedef std::hash_map<ObjectID, Object *, rts::hash<ObjectID>, rts::equal_to<ObjectID> > ObjectPtrHash;
//typedef ObjectPtrHash::const_iterator ObjectPtrIter;

typedef std::vector<Object*> ObjectPtrVector;

/// One slot of GameLogic's sleepy-update heap: the module and its friend_getPriority() beside it.
struct SleepyEntry
{
	UnsignedInt priority;
	UpdateModulePtr module;
};

// ------------------------------------------------------------------------------------------------
/**
 * The implementation of GameLogic 
 */
class GameLogic : public SubsystemInterface, public Snapshot
{

public:

	GameLogic( void );
	virtual ~GameLogic();

	// subsytem methods
	virtual void init( void );															///< Initialize or re-initialize the instance
	virtual void reset( void );															///< Reset the logic system
	virtual void update( void );														///< update the world

#if defined(_DEBUG) || defined(_INTERNAL)
	Int getNumberSleepyUpdates() const {return m_sleepyUpdates.size();} //For profiling, so not in Release.
#endif
	void processCommandList( CommandList *list );		///< process the command list

	void prepareNewGame( Int gameMode, GameDifficulty diff, Int rankPoints );						///< prepare for new game 

	/// Logic command list processing.  A NULL orderedGroup means the order is for the sender's selection;
	/// otherwise it is for exactly that group (a shift-queued order coming round), which is destroyed here.
	void logicMessageDispatcher( GameMessage *msg, AIGroup *orderedGroup );

	void registerObject( Object *obj );							///< Given an object, register it with the GameLogic and give it a unique ID

	void addObjectToLookupTable( Object *obj );			///< add object ID to hash lookup table
	void removeObjectFromLookupTable( Object *obj );///< remove object ID from hash lookup table

	/// @todo Change this to refer to a Region3D as an extent of the world
	void setWidth( Real width );										///< Sets the width of the world
	Real getWidth( void );													///< Returns the width of the world
	void setHeight( Real height );									///< Sets the height of the world
	Real getHeight( void );													///< Returns the height of the world

	/** TRUE on the thread running GameLogic::update, inside it.  Per thread (LOGIC-THREAD.md phase 3): once
		* the render runs beside the tick, the main thread is not inside it and its client work runs at once. */
	Bool isInGameLogicUpdate( void ) const { return s_isInUpdate; }
	Bool isNewGamePending( void ) const { return m_startNewGame; }	///< the next update starts a map (LogicThread keeps that tick on the main thread)
	UnsignedInt getFrame( void );										///< Returns the current simulation frame number
	/** LOGIC-THREAD.md phase 3: the frame as the render's sync point found it, which getFrame answers the
		* main thread with inside the overlap window, where the tick may be advancing m_frame beside it. */
	void takeFrameForDraw( void ) { m_frameForDraw = m_frame; m_pausedForDraw = m_gamePaused; }
	UnsignedInt getFrameForClient( void ) const { return m_frameForClient; }	///< the frame the last tick ran, for the client's clock (LogicThread::runTick)
	UnsignedInt getCRC( Int mode = CRC_CACHED, AsciiString deepCRCFileName = AsciiString::TheEmptyString );		///< Returns the CRC

	void setObjectIDCounter( ObjectID nextObjID ) { m_nextObjID = nextObjID; m_nextObjIDFromSave = nextObjID; }
	ObjectID getObjectIDCounter( void ) { return m_nextObjID; }

	//-----------------------------------------------------------------------------------------------
	void setBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus bs);
	Bool findBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus& bs) const;

	void setControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr commandButton);
	Bool findControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr& commandButton) const;

	//-----------------------------------------------------------------------------------------------
	/// create an object given the thing template. (Only for use by ThingFactory.)
	Object *friend_createObject( const ThingTemplate *thing, const ObjectStatusMaskType &objectStatusMask, Team *team );
	void destroyObject( Object *obj );							///< Mark object as destroyed for later deletion
	Object *findObjectByID( ObjectID id );								///< Given an ObjectID, return a pointer to the object.
 	Object *getFirstObject( void );									///< Returns the "first" object in the world. When used with the object method "getNextObject()", all objects in the world can be iterated.
	ObjectID allocateObjectID( void );							///< Returns a new unique object id
	void growObjectLookupForTick( void );						///< LogicThread's sync point: room for the next tick's objects, so it never reallocates the lookup beside the render

	// super hack
	void startNewGame( Bool loadSaveGame );
	void refuseCorruptMap( void );									///< a start whose map turned out unreadable goes back to the menus
	void loadMapINI( AsciiString mapName );

	void updateLoadProgress( Int progress );
	void deleteLoadScreen( void );
	
	//Kris: Cut setGameLoading() and replaced with setLoadingMap() and setLoadingSave() -- reason: nomenclature
	//void setGameLoading( Bool loading ) { m_loadingScene = loading; }
	void setLoadingMap( Bool loading ) { m_loadingMap = loading; }
	void setLoadingSave( Bool loading ) { m_loadingSave = loading; }
	void setLoadingWithoutScreen( Bool without ) { m_loadingWithoutScreen = without; }	///< a replay's rewind: the picture holds instead
	void setClearingGameData( Bool clearing ) { m_clearingGameData = clearing; }
	
	void setGameMode( Int mode );
	Int getGameMode( void );
	Bool isInGame( void );
	Bool isInLanGame( void );
	Bool isInSinglePlayerGame( void );
	Bool isInSkirmishGame( void );
	Bool isInReplayGame( void );
	Bool isInInternetGame( void );
	Bool isInShellGame( void );
	Bool isInMultiplayerGame( void );

	//Kris: Cut isLoadingGame() and replaced with isLoadingMap() and isLoadingSave() -- reason: nomenclature
	//Bool isLoadingGame() const { return m_loadingScene; }		// This is the old function that isn't very clear on it's definition.
	Bool isLoadingMap() const { return m_loadingMap; }			// Whenever a map is in the process of loading.
	Bool isLoadingSave() const { return m_loadingSave; }		// Whenever a saved game is in the process of loading.
	Bool isClearingGameData() const { return m_clearingGameData; }

	void enableScoring(Bool score) { m_isScoringEnabled = score; }
	Bool isScoringEnabled() const { return m_isScoringEnabled; }

	void setShowBehindBuildingMarkers(Bool b) { m_showBehindBuildingMarkers = b; }
	Bool getShowBehindBuildingMarkers() const { return m_showBehindBuildingMarkers; }

	void setDrawIconUI(Bool b) { m_drawIconUI = b; }
	Bool getDrawIconUI() const { return m_drawIconUI; }

	void setShowDynamicLOD(Bool b) { m_showDynamicLOD = b; }
	Bool getShowDynamicLOD() const { return m_showDynamicLOD; }

	void setHulkMaxLifetimeOverride(Int b) { m_scriptHulkMaxLifetimeOverride = b; }
	Int getHulkMaxLifetimeOverride() const { return m_scriptHulkMaxLifetimeOverride; }

	Bool isIntroMoviePlaying();

	void updateObjectsChangedTriggerAreas(void) {m_frameObjectsChangedTriggerAreas = m_frame;}
	UnsignedInt getFrameObjectsChangedTriggerAreas(void) {return m_frameObjectsChangedTriggerAreas;}

	void clearGameData(Bool showScoreScreen = TRUE);														///< Clear the game data
	void closeWindows( void );

	void sendObjectCreated( Object *obj );
	void sendObjectDestroyed( Object *obj );

	void bindObjectAndDrawable(Object* obj, Drawable* draw);

	void setGamePaused( Bool paused, Bool pauseMusic = TRUE );
	Bool isGamePaused( void );
	Bool getInputEnabledMemory( void ) { return m_inputEnabledMemory; }

	void processProgress(Int playerId, Int percentage);
	void processProgressComplete(Int playerId);
	Bool isProgressComplete( void );
	void timeOutGameStart( void );
	void initTimeOutValues( void );
	UnsignedInt getObjectCount( void );

	Int getRankLevelLimit() const { return m_rankLevelLimit; }
	void setRankLevelLimit(Int limit) 
	{ 
		if (limit < 1) limit = 1;
		m_rankLevelLimit = limit; 
	}
	
	// We need to allow access to this, because on a restartGame, we need to restart with the settings we started with
	Int getRankPointsToAddAtGameStart() const { return m_rankPointsToAddAtGameStart; }

  UnsignedShort getSuperweaponRestriction( void ) const; ///< Get any optional limits on superweapons
  void setSuperweaponRestriction( void );

	/** The lobby's peace time: until this frame nobody may shoot anybody, and anything standing in
			an enemy command center's yard burns.  0 when the option is off, which is every campaign
			mission and every game the option was not set for. */
	UnsignedInt getPeaceTimeEndFrame( void ) const { return m_peaceTimeEndFrame; }
	Bool isPeaceTime( void ) const { return m_peaceTimeEndFrame > m_frame; }
	/// TRUE while the truce forbids these two hurting each other; the one question both gates ask
	Bool peaceTimeForbids( const Object *attacker, const Object *victim ) const;

	/** The vote kick (MSG_VOTE_KICK, "/kick name" in chat).  One vote at a time, open for 30 seconds;
			it passes when every living human player but the one on the block has voted yes, and one no
			ends it.  It is logic state, moved only by commands, so every machine decides it alike. */
	void processKickVote( Player *voter, Int targetIndex, Bool yes );
	Int getKickVoteTarget( void ) const { return m_kickVoteTarget; }	///< player index on the block, -1 when no vote is open
	UnsignedInt getKickVoteEndFrame( void ) const { return m_kickVoteEndFrame; }
	Int countKickVoters( void ) const;										///< the players whose yes the open vote needs
	Int countKickVotes( void ) const;											///< how many of them have given it

	/** Each player's share of the lobby's unit limit, the same number for everybody.  0 when the
			limit is off, which is every campaign mission and every game it was not ticked for. */
	UnsignedInt getUnitCap( void ) const { return m_unitCap; }

	/** Whether this match is played under Pro Rules (PRO-RULES.md): a skirmish, LAN or online game
			whose lobby box is ticked, never a campaign mission or a Generals Challenge.  Fixed when the match starts and
			carried by a save, so a loaded game agrees with the one that was saved. */
	Bool isProRules( void ) const { return m_proRules; }

	/** The lobby's income sharing, an IncomeSharing from GameInfo.h.  Fixed when the match starts and
			carried by a save; INCOME_SHARING_OFF in every campaign mission. */
	Int getIncomeSharing( void ) const { return m_incomeSharing; }

	/** The lobby's tech building respawn, in frames: how long a destroyed tech building lies in ruins
			before a neutral one stands on its spot again.  0 when the option is off, which is every
			campaign mission and every game it was not set for. */
	UnsignedInt getTechRespawnDelay( void ) const { return m_techRespawnDelay; }
	/// a tech building has just died; stand a neutral one on its spot once the delay has run out
	void scheduleTechRespawn( const Object *ruin );

	/** The lobby's supply pile limit: how many players may have gatherers on one supply pile at the
			same time, allies counted one each.  0 when the option is off.  Fixed when the match starts
			and carried by a save. */
	Int getSupplyPileLimit( void ) const { return m_supplyPileLimit; }

	/** A save carries the logic random stream as it stood, but the load itself draws from the stream
			and the engine reset before it reseeded from the clock.  GameState calls this after every
			block's post-process, the last thing a load does, so play goes on from the saved stream. */
	void restoreLoadedRandomState( void );

#ifdef DUMP_PERF_STATS
	void getAIMetricsStatistics( UnsignedInt *numAI, UnsignedInt *numMoving, UnsignedInt *numAttacking, UnsignedInt *numWaitingForPath, UnsignedInt *overallFailedPathfinds );
	void resetOverallFailedPathfinds() { m_overallFailedPathfinds = 0; }
	void incrementOverallFailedPathfinds() { m_overallFailedPathfinds++; }
	UnsignedInt getOverallFailedPathfinds() const { return m_overallFailedPathfinds; }
#endif
	
	// NOTE: selectObject and deselectObject should be called *only* by logical things, NEVER by the
	// client. These will cause the client to select or deselect the object, if affectClient is true.
	// If createToSelection is TRUE, this object causes a new group to be selected.
	void selectObject(Object *obj, Bool createNewSelection, PlayerMaskType playerMask, Bool affectClient = FALSE);
	void deselectObject(Object *obj, PlayerMaskType playerMask, Bool affectClient = FALSE);

	// this should be called only by UpdateModule, thanks.
	void friend_awakenUpdateModule(Object* obj, UpdateModulePtr update, UnsignedInt whenToWakeUp);

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

private:

	void pushSleepyUpdate(UpdateModulePtr u);
	UpdateModulePtr peekSleepyUpdate() const;
	void popSleepyUpdate();
	void eraseSleepyUpdate(Int i);
	void rebalanceSleepyUpdate(Int i);
	Int rebalanceParentSleepyUpdate(Int i);
	Int rebalanceChildSleepyUpdate(Int i);
	void remakeSleepyUpdate();
	void validateSleepyUpdate() const;

private:

	/**
		overrides to thing template buildable status. doesn't really belong here,
		but has to go somewhere. (srj)
	*/
	typedef std::hash_map< AsciiString, BuildableStatus, rts::hash<AsciiString>, rts::equal_to<AsciiString> > BuildableMap;
	BuildableMap m_thingTemplateBuildableOverrides;

	/**
		overrides to control bars. doesn't really belong here, but has to go somewhere. (srj)
	*/
	typedef std::hash_map< AsciiString, ConstCommandButtonPtr, rts::hash<AsciiString>, rts::equal_to<AsciiString> > ControlBarOverrideMap;
	ControlBarOverrideMap m_controlBarOverrides;

	Real m_width, m_height;																	///< Dimensions of the world
	UnsignedInt m_frame;																		///< Simulation frame number
	UnsignedInt m_frameForClient;														///< m_frame at the top of the last update that got past the intro movie
	UnsignedInt m_frameForDraw = 0;													///< see takeFrameForDraw
	Bool m_pausedForDraw = FALSE;														///< m_gamePaused, taken with m_frameForDraw for the same reason
	
	// CRC cache system -----------------------------------------------------------------------------
	UnsignedInt	m_CRC;																			///< Cache of previous CRC value
	std::map<UnsignedInt, std::map<Int, UnsignedInt> > m_cachedCRCs;	///< CRCs executed this frame, by the frame they were computed on, then by player
	Bool m_shouldValidateCRCs;															///< Should we validate CRCs this frame?
	CRCSnapshotRing m_crcSnapshots;													///< per object CRCs for the last few CRC frames, for mismatch diagnosis
	void writeMismatchDump( Int numPlayers, UnsignedInt crcFrame );	///< write down what the world looked like on the mismatching frame
	//-----------------------------------------------------------------------------------------------

	//Added By Sadullah Nader
	//Used to for load scene
	//Bool m_loadingScene;
	Bool m_loadingMap;
	Bool m_loadingSave;
	Bool m_loadingWithoutScreen;
	Bool m_clearingGameData;

	static thread_local Bool s_isInUpdate;

	Int m_rankPointsToAddAtGameStart;

	Bool m_isScoringEnabled;
	Bool m_showBehindBuildingMarkers;	//used by designers to override the user setting for cinematics
	Bool m_drawIconUI;
	Bool m_showDynamicLOD;	//used by designers to override the user setting for cinematics
	Int m_scriptHulkMaxLifetimeOverride;	///< Scripts can change the lifetime of a hulk -- defaults to off (-1) in frames.

	/// @todo remove this hack
	Bool m_startNewGame;
	WindowLayout *m_background;

	Object* m_objList;																			///< All of the objects in the world.
//	ObjectPtrHash m_objHash;																///< Used for ObjectID lookups
	ObjectPtrVector m_objVector;

	// this is a vector, but is maintained as a priority queue.
	// never modify it directly; please use the proper access methods.
	// (for an excellent discussion of priority queues, please see:
	// http://dogma.net/markn/articles/pq_stl/priority.htm)
	// Each entry carries a copy of its module's friend_getPriority() so the heap compares without
	// touching the pooled module. The copy must equal the module's value whenever a heap operation
	// runs: every friend_setNextCallFrame on a queued module writes the entry at its index too.
	std::vector<SleepyEntry> m_sleepyUpdates;
	
#ifdef ALLOW_NONSLEEPY_UPDATES
	// this is a plain old list, not a pq.
	std::list<UpdateModulePtr> m_normalUpdates;
#endif

	UpdateModulePtr					 m_curUpdateModule;

	ObjectPointerList m_objectsToDestroy;										///< List of things that need to be destroyed at end of frame

	ObjectID m_nextObjID;																		///< For allocating object id's
	ObjectID m_nextObjIDFromSave;														///< The counter a save held, for loadPostProcess

	void setDefaults( Bool loadSaveGame );									///< Set default values of class object
	void processDestroyList( void );												///< Destroy all pending objects on the destroy list

	void destroyAllObjectsImmediate();											///< destroy, and process destroy list immediately

	/// factory for TheTerrainLogic, called from init()
	virtual TerrainLogic *createTerrainLogic( void );
	virtual GhostObjectManager *createGhostObjectManager(void);

	Int m_gameMode;
	Int m_rankLevelLimit;
  UnsignedShort m_superweaponRestriction;
	UnsignedInt m_peaceTimeEndFrame;	///< logic frame the lobby's peace time runs out on, 0 = no peace time
	Int m_kickVoteTarget;							///< player index an open vote kick is about, -1 = none
	UnsignedInt m_kickVoteEndFrame;		///< frame that vote runs out on
	UnsignedInt m_kickVoteYesMask;		///< player indices that have voted yes in it
	void expireKickVote( void );			///< closes the vote once its 30 seconds are up
	void endKickVote( const char *label );
	UnsignedInt m_unitCap;						///< units each player may have standing and queued, 0 = no limit
	Bool m_proRules;									///< this match refuses what PRO-RULES.md bans
	Int m_incomeSharing;							///< which earnings allies split, an IncomeSharing
	UnsignedInt m_techRespawnDelay;		///< frames a destroyed tech building stays down, 0 = for good
	Int m_supplyPileLimit;						///< players who may gather from one supply pile at once, 0 = any number

	/// a destroyed tech building waiting to stand again
	struct PendingTechBuilding
	{
		const ThingTemplate *m_template;
		Coord3D m_position;
		Real m_angle;
		ObjectID m_ruinID;							///< what KeepObjectDie left standing; a DestroyDie building leaves nothing
		UnsignedInt m_dueFrame;
	};
	std::vector<PendingTechBuilding> m_pendingTechBuildings;	///< in the order they died, which every machine agrees on
	void techRespawnTick( void );

	/// the logic random stream a save carried, put back once the whole load is done (restoreLoadedRandomState)
	GameLogicRandomState m_loadedRandomState;
	Bool m_loadedRandomPending;

	/// the sleepy update heap as a save wrote it, object and behavior module index per entry, for loadPostProcess
	std::vector< std::pair< ObjectID, Int > > m_loadedSleepyOrder;
	Bool m_loadedSleepyPending;
	LoadScreen *getLoadScreen( Bool loadSaveGame );
	LoadScreen *m_loadScreen;
	Bool m_gamePaused;
	Bool m_inputEnabledMemory;// Latches used to remember what to restore to after we unpause
	Bool m_mouseVisibleMemory;

	Bool m_progressComplete[MAX_SLOTS];
	enum { PROGRESS_COMPLETE_TIMEOUT = 60000 };							///< Timeout we wait for when we've completed our Load
	Int m_progressCompleteTimeout[MAX_SLOTS];
	void testTimeOut( void );
	void lastHeardFrom( Int playerId );
	Bool m_forceGameStartByTimeOut;													///< If we timeout someone we're waiting to load, set this flag to start the game

#ifdef DUMP_PERF_STATS
	UnsignedInt m_overallFailedPathfinds;
#endif

	UnsignedInt m_frameObjectsChangedTriggerAreas;					///< Last frame objects moved into/outof trigger areas, or were created/destroyed. jba.

	// ----------------------------------------------------------------------------------------------
	struct ObjectTOCEntry
	{
		AsciiString name;
		UnsignedShort id;
	};
	typedef std::list< ObjectTOCEntry > ObjectTOCList;
	typedef ObjectTOCList::iterator ObjectTOCListIterator;
	ObjectTOCList m_objectTOC;															///< the object TOC
	void addTOCEntry( AsciiString name, UnsignedShort id ); ///< add a new name/id TOC pair
	ObjectTOCEntry *findTOCEntryByName( AsciiString name );	///< find ObjectTOC by name
	ObjectTOCEntry *findTOCEntryById( UnsignedShort id );		///< find ObjectTOC by id
	void xferObjectTOC( Xfer *xfer );												///< save/load object TOC for current state of map
	void prepareLogicForObjectLoad( void );									///< prepare engine for object data from game file
		
};

// INLINE /////////////////////////////////////////////////////////////////////////////////////////
inline void GameLogic::setWidth( Real width ) { m_width = width; }
inline Real GameLogic::getWidth( void ) { return m_width; }
inline void GameLogic::setHeight( Real height ) { m_height = height; }
inline Real GameLogic::getHeight( void ) { return m_height; }
inline UnsignedInt GameLogic::getFrame( void ) { return LogicThread::isInsideWindow() ? m_frameForDraw : m_frame; }

inline Bool GameLogic::isInGame( void ) { return !(m_gameMode == GAME_NONE); }

//
// MSG_NEW_GAME arrives over the message stream like any other command, so a game already in
// progress can be handed one.  Starting a second game on top of the first tears down the state the
// first one is still running on, and in a network game the other machines - which were not told to
// start anything - sit in the disconnect screen waiting for a player who is now somewhere else
// entirely, with no frame that will ever arrive to end the wait.  A new game only begins from a
// standing start.
//
inline Bool IsReadyToStartNewGame( Bool inGame, Bool clearingGameData, Bool loadingMap )
{
	return !inGame && !clearingGameData && !loadingMap;
}
inline void GameLogic::setGameMode( Int mode ) { m_gameMode = mode; }
inline Int  GameLogic::getGameMode( void ) { return m_gameMode; }
inline Bool GameLogic::isInLanGame( void ) { return (m_gameMode == GAME_LAN); }
inline Bool GameLogic::isInSkirmishGame( void ) { return (m_gameMode == GAME_SKIRMISH); }
inline Bool GameLogic::isInMultiplayerGame( void ) { return ((m_gameMode == GAME_LAN) || (m_gameMode == GAME_INTERNET)) ; }
inline Bool GameLogic::isInReplayGame( void ) { return (m_gameMode == GAME_REPLAY); }
inline Bool GameLogic::isInInternetGame( void ) { return (m_gameMode == GAME_INTERNET); }
inline Bool GameLogic::isInShellGame( void ) { return (m_gameMode == GAME_SHELL); }
inline UnsignedShort GameLogic::getSuperweaponRestriction() const { return m_superweaponRestriction; }

inline Object* GameLogic::findObjectByID( ObjectID id )
{
	LOGIC_WINDOW_READ( WINDOW_FIND_OBJECT );
	if( id == INVALID_ID )
		return NULL;

//	ObjectPtrHash::iterator it = m_objHash.find(id);
//	if (it == m_objHash.end())
//		return NULL;
//	
//	return (*it).second;
	if( (Int)id < m_objVector.size() )
		return m_objVector[(Int)id];

	return NULL;
}



// the singleton
extern GameLogic *TheGameLogic;

#endif // _GAME_LOGIC_H_

