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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GlobalData.h /////////////////////////////////////////////////////////////////////////////
// Global data used by both the client and logic
// Author: trolfs, Michae Booth, Colin Day, April 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _GLOBALDATA_H_
#define _GLOBALDATA_H_

#include "Common/GameCommon.h"	// ensure we get DUMP_PERF_STATS, or not
#include "Common/AsciiString.h"
#include "Common/GameType.h"
#include "Common/GameMemory.h"
#include "Common/SubsystemInterface.h"
#include "GameClient/Color.h"
#include "Common/STLTypedefs.h"
#include "Common/GameCommon.h"
#include "Common/Money.h"
#include "Common/WindowMode.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
struct FieldParse;
typedef enum _TerrainLOD;
class GlobalData;
class INI;
class WeaponBonusSet;
enum BodyDamageType;
enum AIDebugOptions;

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////

const Int MAX_GLOBAL_LIGHTS	= 3;

//-------------------------------------------------------------------------------------------------
/** When a unit wears a health bar.
	*
	* Retail drew one only for the selected unit and the one under the cursor.  This fork made them
	* permanent, which reads a battle at a glance and turns a peaceful base into a wall of green.
	* Both are defensible, so both are here, with the useful middle in between. */
enum HealthBarModeType
{
	HEALTH_BAR_ALWAYS			= 0,	///< every unit, all the time
	HEALTH_BAR_SMART			= 1,	///< anything hurt, plus the selection and whatever is under the cursor
	HEALTH_BAR_SELECTION	= 2,	///< the selection and whatever is under the cursor, the way retail did it
	HEALTH_BAR_NEVER			= 3,	///< none, ever

	HEALTH_BAR_MODE_COUNT	= 4,
};

/** How the mouse and the keyboard give orders.  Modern is this fork's: left selects, right orders, the
	* middle button drives the camera and the grid keys run the command bar.  Legacy is the game as it
	* shipped, keys and all, with none of what this fork added to either. */
enum InputSchemeType
{
	INPUT_SCHEME_MODERN		= 0,
	INPUT_SCHEME_LEGACY		= 1,

	INPUT_SCHEME_COUNT		= 2,
};

/** The language the game's words are shown in.  English is the string table EA shipped; every other
	* entry names a translation GameText lays over it.  Speech and video stay what the install has. */
enum TextLanguageType
{
	TEXT_LANGUAGE_ENGLISH	= 0,
	TEXT_LANGUAGE_TURKISH	= 1,

	TEXT_LANGUAGE_COUNT		= 2,
};

//-------------------------------------------------------------------------------------------------
/** Global data container class
  *	Defines all global game data used by the system
	* @todo Change this entire system. Otherwise this will end up a huge class containing tons of variables,
	* and will cause re-compilation dependancies throughout the codebase. 
  * OOPS -- TOO LATE! :) */
//-------------------------------------------------------------------------------------------------
class GlobalData : public SubsystemInterface
{

public:

	GlobalData();
	virtual ~GlobalData();

	void init();
	void reset();
	void update() { }

	Bool setTimeOfDay( TimeOfDay tod );		///< Use this function to set the Time of day;

	static void parseGameDataDefinition( INI* ini );

	//-----------------------------------------------------------------------------------------------
	struct TerrainLighting
	{
		RGBColor ambient;
		RGBColor diffuse;
		Coord3D lightPos;
	};

	//-----------------------------------------------------------------------------------------------
	//-----------------------------------------------------------------------------------------------
	//-----------------------------------------------------------------------------------------------

	AsciiString m_mapName;  ///< hack for now, this whole this is going away
	AsciiString m_moveHintName;
	Bool m_useTrees;
	Bool m_useTreeSway;
	Bool m_useDrawModuleLOD;
	Bool m_useHeatEffects;
	Bool m_useFpsLimit;
	Bool m_dumpAssetUsage;
	Int m_framesPerSecondLimit;
	Int	m_chipSetType;	///<See W3DShaderManager::ChipsetType for options
	Bool m_windowed;
	Int m_windowMode;					///< WindowModeType: fullscreen, borderless or windowed.  m_windowed
														///< is derived from it and is what the device layer reads.
	AsciiString m_monitor;		///< the monitor the game is on, as its GDI device name ("\\.\DISPLAY2");
														///< empty is the primary.  See Common/Monitors.h.
	Int m_msaaLevel;					///< multisampling, as an index into the levels the options menu offers
	Bool m_vsync;						///< wait for the monitor; off is the uncapped picture the frame-rate cap removal shipped
	Bool m_direct3D11;			///< draw and present through the Direct3D 11 backend; -d3d9 and -headless turn it off
	AsciiString m_direct3D11DumpPath;	///< -dx11dump: where to write each generated program
	AsciiString m_direct3D11PostChain;	///< -dx11post: the effects run over the finished D3D11 frame
	Int m_xResolution;
	Int m_yResolution;
	Int m_maxShellScreens;  ///< this many shells layouts can be loaded at once
	Bool m_useCloudMap;
	Int  m_use3WayTerrainBlends;	///< 0 is none, 1 is normal, 2 is debug.
	Bool m_useLightMap;
	Bool m_bilinearTerrainTex;
	Bool m_trilinearTerrainTex;
	Bool m_multiPassTerrain;
	Bool m_adjustCliffTextures;
	Bool m_stretchTerrain;
	Bool m_useHalfHeightMap;
	Bool m_drawEntireTerrain;
	_TerrainLOD m_terrainLOD;
	Bool m_enableDynamicLOD;
	Bool m_enableStaticLOD;
	Int m_terrainLODTargetTimeMS;
	Bool m_clientRetaliationModeEnabled;
	Bool m_doubleClickAttackMove;
	Bool m_rightMouseAlwaysScrolls;
	Bool m_useWaterPlane;
	Bool m_useCloudPlane;
	Bool m_useShadowVolumes;
	Bool m_useShadowVolumesForSkins;	// "UseShadowVolumesForSkins": also cast volume shadows off skinned meshes
	Bool m_useShadowDecals;
	Bool m_shadowsForProjectiles;	// "ShadowsForProjectiles": missiles and bombs that have no shadow of their own get a decal one
	Bool m_startAtMaxZoom;					// "StartAtMaxZoom": a game opens as far out as the wheel goes, not at the map's own default
	Bool m_shadowsForProps;				// "ShadowsForProps": fences, rubbish, shrubs - scenery the art gave no shadow at all
	Bool m_shadowsForParticles;		// "ShadowsForParticles": big alpha-blended particle clouds drop a soft blob on the ground
	Bool m_classicGraphics;				// "ClassicGraphics": the game's own art, tile, shadows and picture; read at startup
	Bool m_shadowMap;							// "-shadowmap": draw the casters into the sun's depth buffer as well
	Bool m_shadowMapReport;				// "-shadowmapreport": log what ended up in that buffer, once a second
	Bool m_shadowMapOnly;					// "-shadowmaponly": the map's shadows with the stencil volumes' own darkening off
	Real m_shadowMapPenumbra;			// "-shadowtune": penumbra per world unit of gap under the caster, 0 for the built-in
	Real m_shadowMapSkyFill;			// how much of a wide shadow the sky fills back in
	Real m_shadowMapStrength;			// how dark a fully blocked pixel goes
	Bool m_contactShadows;					// "ContactShadows": the soft patch under a structure's footprint
	Real m_shadowMapWidest;				// how far the filter may open, in texels
	Bool m_particleGroundBounce;	// "-particlebounce": terrain collision on for every particle system
	Real m_smokeThickness;				// "-smoke": how much longer and thicker every smoke system runs, 0 for shipped behaviour
	Int  m_particleCapOverride;		// "-particlecap": stand in for the options slider's MaxParticleCount, 0 to use it
	Int  m_textureReductionFactor;	//how much to cut texture resolution: 2 is half, 3 is quarter, etc.
	Bool m_enableBehindBuildingMarkers;
	Real m_waterPositionX;
	Real m_waterPositionY;
	Real m_waterPositionZ;
	Real m_waterExtentX;	
	Real m_waterExtentY;
	Int	m_waterType;
	Bool m_showSoftWaterEdge;
	Bool m_usingWaterTrackEditor;
	Bool m_isWorldBuilder;

	Int m_featherWater;

	// these are for WATER_TYPE_3 vertex animated water
	enum { MAX_WATER_GRID_SETTINGS = 4 };
	AsciiString m_vertexWaterAvailableMaps[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterHeightClampLow[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterHeightClampHi[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAngle[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterXPosition[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterYPosition[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterZPosition[ MAX_WATER_GRID_SETTINGS ];
	Int m_vertexWaterXGridCells[ MAX_WATER_GRID_SETTINGS ];
	Int m_vertexWaterYGridCells[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterGridSize[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationA[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationB[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationC[ MAX_WATER_GRID_SETTINGS ];
	Real m_vertexWaterAttenuationRange[ MAX_WATER_GRID_SETTINGS ];

	Real m_downwindAngle;
	Real m_skyBoxPositionZ;
	Real m_drawSkyBox;
	Real m_skyBoxScale;
	Real m_cameraPitch;
	Real m_cameraYaw;
	Real m_cameraHeight;
	Real m_maxCameraHeight;
	Real m_minCameraHeight;
	Real m_terrainHeightAtEdgeOfMap;
	Real m_unitDamagedThresh;
	Real m_unitReallyDamagedThresh;
	Real m_groundStiffness;
	Real m_structureStiffness;
	Real m_gravity;	// acceleration due to gravity, in dist/frame^2
	Real m_stealthFriendlyOpacity;
	UnsignedInt m_defaultOcclusionDelay;	///<time to delay building occlusion after object is created.

	Bool m_preloadEverything;			///< Preload everything, everywhere (for debugging only)
	Bool m_preloadReport;					///< dump a log of all W3D assets that are being preloaded.

	Real m_partitionCellSize;

	Coord3D m_ammoPipWorldOffset;
	Coord3D m_containerPipWorldOffset;
	Coord2D m_ammoPipScreenOffset;
	Coord2D m_containerPipScreenOffset;
	Real m_ammoPipScaleFactor;
	Real m_containerPipScaleFactor;

	UnsignedInt m_historicDamageLimit;

	//Settings for terrain tracks left by vehicles with treads or wheels
	Int m_maxTerrainTracks; ///<maximum number of objects allowed to generate tracks.
	Int m_maxTankTrackEdges;	///<maximum length of tank track
	Int m_maxTankTrackOpaqueEdges;	///<maximum length of tank track before it starts fading.
	Int m_maxTankTrackFadeDelay;	///<maximum amount of time a tank track segment remains visible.
	
	AsciiString m_levelGainAnimationName; ///< The animation to play when a level is gained.
	Real m_levelGainAnimationDisplayTimeInSeconds;		///< time to play animation for
	Real m_levelGainAnimationZRisePerSecond;					///< rise animation up while playing

	AsciiString m_getHealedAnimationName; ///< The animation to play when emergency repair does its thing.
	Real m_getHealedAnimationDisplayTimeInSeconds;		///< time to play animation for
	Real m_getHealedAnimationZRisePerSecond;					///< rise animation up while playing

	TimeOfDay	m_timeOfDay;
	Weather m_weather;
	Bool m_makeTrackMarks;
	Bool m_hideGarrisonFlags;
	Bool m_forceModelsToFollowTimeOfDay;
	Bool m_forceModelsToFollowWeather;

	TerrainLighting	m_terrainLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];
	TerrainLighting	m_terrainObjectsLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];

	//Settings for each global light
	RGBColor m_terrainAmbient[MAX_GLOBAL_LIGHTS];
	RGBColor m_terrainDiffuse[MAX_GLOBAL_LIGHTS];
	Coord3D m_terrainLightPos[MAX_GLOBAL_LIGHTS];

	Real m_infantryLightScale[TIME_OF_DAY_COUNT];
	Real m_scriptOverrideInfantryLightScale;

	Real m_soloPlayerHealthBonusForDifficulty[PLAYERTYPE_COUNT][DIFFICULTY_COUNT];

	Int m_maxVisibleTranslucentObjects;
	Int m_maxVisibleOccluderObjects;
	Int m_maxVisibleOccludeeObjects;
	Int m_maxVisibleNonOccluderOrOccludeeObjects;
	Real m_occludedLuminanceScale;

	Int m_numGlobalLights;	//number of active global lights
	Int m_maxRoadSegments;
	Int m_maxRoadVertex;
	Int m_maxRoadIndex;
	Int m_maxRoadTypes;

	Bool m_audioOn;
	Bool m_musicOn;
	Bool m_soundsOn;
	Bool m_sounds3DOn;
	Bool m_speechOn;
	Bool m_videoOn;
	Bool m_disableCameraMovement;

	Bool m_useFX;									///< If false, don't render effects
	Bool m_showClientPhysics;
	Bool m_showTerrainNormals;

	UnsignedInt m_noDraw;					///< Used to disable drawing, to profile game logic code.
	AIDebugOptions m_debugAI;			///< Used to display AI debug information
	Bool m_debugSupplyCenterPlacement; ///< Dumps to log everywhere it thinks about placing a supply center
	Bool m_debugAIObstacles;			///< Used to display AI obstacle debug information
	Bool m_showObjectHealth;			///< debug display object health
	Int m_healthBarMode;					///< HealthBarModeType: which units wear a bar at all
	Int m_playerColorScheme;			///< PlayerColorSchemeType: whose colour the client draws (client only)
	Int m_textLanguage;						///< TextLanguageType: the translation GameText lays over the CSF, read once at startup (client only)
	Int m_inputScheme;						///< InputSchemeType: which mouse and keyboard the client answers to, read on every click and key (client only)
	Bool isLegacyInput( void ) const { return m_inputScheme == INPUT_SCHEME_LEGACY; }
	Bool m_wasdCamera;						///< W A S D scroll the camera and the unit keys move to F G H J K; Modern input only (client only)
	Bool isWasdCamera( void ) const { return m_wasdCamera && !isLegacyInput(); }
	Bool m_showOrderLines;				///< draw a line from each selected unit to where it is going, and its queue (client only)
	Bool m_scriptDebug;						///< Should we attempt to load the script debugger window (.DLL)
	Bool m_particleEdit;					///< Should we attempt to load the particle editor (.DLL)
	Bool m_displayDebug;					///< Used to display display debug info
	Bool m_winCursors;						///< Should we force use of windows cursors?
	Bool m_constantDebugUpdate;		///< should we update the debug stats constantly, vs every 2 seconds?
	Bool m_showTeamDot;						///< Shows the little colored team dot representing which team you are controlling.
	
#ifdef DUMP_PERF_STATS
	Bool m_dumpPerformanceStatistics;
  Bool  m_dumpStatsAtInterval;///< should I automatically dum stats every in N frames
  Int   m_statsInterval;       ///< if so, how many is N?
#endif
	
	Bool m_forceBenchmark;	///<forces running of CPU detection benchmark, even on known cpu's.

	Int m_fixedSeed;							///< fixed random seed for game logic (less than 0 to disable)

	Real m_particleScale;					///< Global size modifier for particle effects.

	AsciiString m_autoFireParticleSmallPrefix;
	AsciiString m_autoFireParticleSmallSystem;
	Int m_autoFireParticleSmallMax;
	AsciiString m_autoFireParticleMediumPrefix;
	AsciiString m_autoFireParticleMediumSystem;
	Int m_autoFireParticleMediumMax;
	AsciiString m_autoFireParticleLargePrefix;
	AsciiString m_autoFireParticleLargeSystem;
	Int m_autoFireParticleLargeMax;
	AsciiString m_autoSmokeParticleSmallPrefix;
	AsciiString m_autoSmokeParticleSmallSystem;
	Int m_autoSmokeParticleSmallMax;
	AsciiString m_autoSmokeParticleMediumPrefix;
	AsciiString m_autoSmokeParticleMediumSystem;
	Int m_autoSmokeParticleMediumMax;
	AsciiString m_autoSmokeParticleLargePrefix;
	AsciiString m_autoSmokeParticleLargeSystem;
	Int m_autoSmokeParticleLargeMax;
	AsciiString m_autoAflameParticlePrefix;
	AsciiString m_autoAflameParticleSystem;
	Int m_autoAflameParticleMax;

	// Latency insertion, packet loss for network debugging
	Int m_netMinPlayers;					///< Min players needed to start a net game

	UnsignedInt m_defaultIP;			///< preferred IP address for LAN
	UnsignedInt m_firewallBehavior;	///< Last detected firewall behavior
	Bool m_firewallSendDelay;			///< Use send delay for firewall connection negotiations
	UnsignedInt m_firewallPortOverride;	///< User-specified port to be used
	Short m_firewallPortAllocationDelta; ///< the port allocation delta last detected.

	Int m_baseValuePerSupplyBox;
	Real m_BuildSpeed;
	Real m_MinDistFromEdgeOfMapForBuild;
	Real m_SupplyBuildBorder;
	Real m_allowedHeightVariationForBuilding;  ///< how "flat" is still flat enough to build on
	Real m_MinLowEnergyProductionSpeed;
	Real m_MaxLowEnergyProductionSpeed;
	Real m_LowEnergyPenaltyModifier;
	Real m_MultipleFactory;
	Real m_RefundPercent;

	Real m_commandCenterHealRange;		///< radius in which close by ally things are healed
	Real m_commandCenterHealAmount;   ///< health per logic frame close by things are healed
	Int m_maxLineBuildObjects;				///< line style builds can be no longer than this
	Int m_maxTunnelCapacity;					///< Max people in Player's tunnel network
	Real m_horizontalScrollSpeedFactor;	///< Factor applied to the game screen scrolling speed.
	Real m_verticalScrollSpeedFactor;		///< Seperated because of our aspect ratio
	Real m_scrollAmountCutoff;				///< Scroll speed to not adjust camera height
	Real m_cameraAdjustSpeed;					///< Rate at which we adjust camera height
	Bool m_enforceMaxCameraHeight;		///< Enfoce max camera height while scrolling?
	Bool m_useCameraConstraints;		///< constrain camera movement to the map and its configured margin
	Int m_cameraBoundaryMargin;		///< world units permitted beyond the map; 0 uses the original framing limit
	Bool m_edgeScrollInWindowedMode;	///< allow screen-edge scrolling while running in a window
	Bool m_snapBuildPlacementTo45;		///< quantize the drag-to-rotate build placement angle to 45 degrees
	Bool m_snapCameraRotateTo45;		///< quantize the camera heading to 45 degrees when a middle-drag rotate ends
	Bool m_gridBuildPlacement;			///< quantize structure placement to the pathfinder's build grid
	Bool m_nudgeBuildPlacement;			///< slide a blocked structure to the nearest spot it does fit
	Int m_moneyPerMinute;				///< income every player gets once a minute regardless of supply lines; 0 = off
	Real m_buildPlacementOpacity;		///< how solid the structure riding the cursor is drawn, 0..1
	Bool m_buildPlacementShadows;		///< whether that structure casts a shadow while it rides the cursor
	Bool m_zoomToCursor;				///< the mouse wheel zooms toward whatever the cursor is over
	Bool m_isometricCamera;				///< the tactical view from far off down a narrow cone, near enough orthographic
	Bool m_formationDrag;				///< dragging the right button spreads the selection along the line drawn
	Bool m_showAllyCursors;				///< in a network game, draw where each ally's mouse is pointing
	Bool m_chromaLighting;				///< put the state of the match on Razer hardware
	Int m_menuTransitionSpeed;			///< percent of the authored speed the menus slide and fade at; 100 = as drawn
	Int m_textureFilterMode;			///< 0 bilinear, 1 trilinear, 2 anisotropic
	Int m_anisotropyLevel;				///< samples anisotropic filtering may take; 0 = whatever the card offers
	Bool m_showHudOverlay;				///< draw the fps / elapsed time / income line in the corner
	Bool m_showPlacementRangeRing;		///< while placing a structure, ring its weapon range
	Bool m_showSkillStrip;					///< watching, every general's bought promotions, bottom right
	Bool m_showSuperweaponStrip;		///< the superweapon countdown cameos, top right
	Bool m_workersReturnToSupply;		///< a worker that finishes a build job goes back to the dock it left
	Bool m_detailedBuildTooltips;		///< put build time, weapon range and damage in the build tooltip
	Bool m_archiveReplays;					///< keep a timestamped copy of every replay, instead of only the last one
	Int m_bloomIntensity;				///< bloom strength in percent, 0 = off
	Int m_bloomThreshold;				///< brightness in percent below which nothing blooms
	Bool m_buildMapCache;
	AsciiString m_initialFile;				///< If this is specified, load a specific map/replay from the command-line
	AsciiString m_pendingFile;				///< If this is specified, use this map at the next game start
	Int m_autoSkirmishPlayers;				///< -autoskirmish <n>: start an n player skirmish straight from the command line (0 = off)
	Int m_autoSkirmishAIState;				///< SlotState the AI slots of an auto-started skirmish get (-aidiff)
	Bool m_autoSkirmishObserver;			///< -observer: the local slot of an auto-started skirmish watches instead of playing
	Bool m_headless;							///< -headless: never draw a frame, never pace the logic tick, quit when the match ends
	Bool m_turbo;									///< -turbo: draw, but run one logic frame a pass instead of pacing it to the wall clock
	Int m_autoSkirmishAIStateOdd;		///< -aidiff2 <name>: rung for the odd-numbered slots (0 = same as -aidiff)
	Int m_noTacticsSlotParity;			///< -notactics even|odd: those slots of a skirmish fight without Hard's unit tactics; -1 none
	Int m_autoSkirmishTeams;				///< -teams <n>: split the auto-skirmish slots into n allied teams (0 or 1 = free-for-all)
	Int m_peaceTime;								///< -peacetime <n>: the lobby's peace time, in minutes, for an -autoskirmish run
	Bool m_unitLimit;								///< -unitlimit: the lobby's unit limit for an -autoskirmish run
	Int m_incomeSharing;						///< -incomesharing <n>: the lobby's income sharing, an IncomeSharing, for an -autoskirmish run
	Int m_techRespawn;							///< -techrespawn <n>: the lobby's tech building respawn, in minutes, for an -autoskirmish run
	Int m_maxGameFrames;						///< -maxframes <n>: quit after n logic frames however the match is going (0 = no limit)
	Int m_screenShotFrame;					///< -screenshot <n>: save one picture when the run reaches logic frame n (0 = never)
	Int m_videoStartFrame;					///< -video <from> <to> [name]: the first logic frame recorded
	Int m_videoEndFrame;						///< -video: the last logic frame recorded (0 = no video)
	AsciiString m_videoName;				///< -video: the recording is Videos\<name>.mp4 next to the save games
	Int m_wavStartFrame;						///< -wav <from> <to> [name]: the first logic frame of the sound recording
	Int m_wavEndFrame;							///< -wav: the last logic frame recorded (0 = no sound recording)
	AsciiString m_wavName;					///< -wav: the recording is Videos\<name>.wav next to the save games
	Int m_autoCameraSeconds;				///< -autocamera <n>: every n seconds, move the camera to wherever the fighting is (0 = off)
	Bool m_cameraLookSet;						///< -camera <x> <y>: point the camera at one map position once the match starts
	Coord2D m_cameraLook;						///< where -camera pointed it
	Int m_traceMoveID;							///< -tracemove [id]: log one movement line a frame for this object (0 = off, -1 = the first unit that gets blocked)
	Real m_slowFrameMS;							///< -slowframe <ms>: a logic frame over this long logs its own breakdown (default 20)
	Int m_drawDelayMS;							///< -drawdelay <ms>: sleep this long in every client pass, a slow graphics card on demand (0 = off)
	Int m_drawDelayJitterMS;				///< -drawdelay <ms> <jitter>: up to this much more, different every pass
	Bool m_showLanes;							///< -showlanes: draw every moving unit's route, the lane it was handed and the offset it kept
	Int m_uiDrill;								///< -uidrill <n>: every n frames, minimise the command bar and re-apply its scheme, logging where it landed (0 = off)
	Int m_resDrillFrame;					///< -resdrill <frame> [w] [h]: change the resolution at that logic frame, from inside a running match (0 = off)
	Int m_resDrillX;							///< the width -resdrill asks for (0 = the next mode the device offers)
	Int m_resDrillY;							///< the height -resdrill asks for
	Bool m_resDrillKeep;					///< -resdrillkeep: answer the confirmation with Ok and stay on the new mode
	Bool m_noRenderDevice;					///< -nodevice: run without a Direct3D device at all, for a machine whose session is locked
	Int m_controlPort;							///< -control [port]: listen for WebSocket commands on 127.0.0.1 (0 = off)
	AsciiString m_scenarioFile;			///< -scenario <name>: play Run/Scenarios/<name>.txt instead of leaving the match to a person or an AI (empty = off)
	AsciiString m_cinemaScript;			///< -cinema <name>: interface off, camera flown by Run/Cinema/<name>.txt (empty = off)
	Bool m_autoSkirmishTakeover;		///< -takeover: give every -autoskirmish slot a driverless human seat, so nothing thinks unless a scenario says so
	AsciiString m_autoSkirmishSide[ MAX_PLAYER_COUNT ];	///< -side <slot> <faction>: name that slot's faction instead of drawing it from the seed
	AsciiString m_netGameHosts;				///< -netgame <ip>[,<ip>...]: the slot list of a LAN game started from the command line (empty = off)
	Bool m_netGameStarted;						///< that -netgame passed its checks and StartAutomatedGame ran, so every seat has this command line
	Int m_netGameLocalSlot;						///< -netslot <n>: which of those addresses this copy of the game is
	Int m_netGameAISlots;							///< -netai <n>: that many AI seats after the addresses, at the -aidiff rung
	AsciiString m_lanPlayerName;			///< -lanname <name>: the name this copy takes in the LAN lobby (empty = the one in the preferences)
	Bool m_lanLobbyOnStart;						///< -lanlobby: open the LAN lobby instead of stopping at the main menu
	Bool m_skirmishLobbyOnStart;			///< -skirmishlobby: open the skirmish staging room instead of stopping at the main menu
	Bool m_optionsMenuOnStart;				///< -optionsmenu: open the options screen over the main menu at startup
	Bool m_randomMapsInMenus;					///< -randommaps: offer the generated maps in the map lists (off until the generator is finished)

	Int m_maxParticleCount;						///< maximum number of particles that can exist
	Int m_maxFieldParticleCount;			///< maximum number of field-type particles that can exist (roughly)
	WeaponBonusSet* m_weaponBonusSet;
	Real m_healthBonus[LEVEL_COUNT];			///< global bonuses to health for veterancy.
	Real m_defaultStructureRubbleHeight;	///< for rubbled structures, compress height to this if none specified

	AsciiString m_shellMapName;				///< Holds the shell map name
	Bool m_shellMapOn;								///< User can set the shell map not to load
	Bool m_playIntro;									///< Flag to say if we're to play the intro or not
	Bool m_playSizzle;								///< Flag to say whether we play the sizzle movie after the logo movie.
	Bool m_afterIntro;								///< we need to tell the game our intro is done
	Bool m_allowExitOutOfMovies;			///< flag to allow exit out of movies only after the Intro has played

	Bool m_loadScreenRender;						///< flag to disallow rendering of almost everything during a loadscreen

	Real m_keyboardScrollFactor;			///< Factor applied to game scrolling speed via keyboard scrolling
	Real m_keyboardDefaultScrollFactor;			///< Factor applied to game scrolling speed via keyboard scrolling
	
  Real m_musicVolumeFactor;         ///< Factor applied to loudness of music volume
  Real m_SFXVolumeFactor;           ///< Factor applied to loudness of SFX volume
  Real m_voiceVolumeFactor;         ///< Factor applied to loudness of voice volume
  Bool m_3DSoundPref;               ///< Whether user wants to use 3DSound or not

	Bool m_animateWindows;						///< Should we animate window transitions?

	Bool m_incrementalAGPBuf;
	
	UnsignedInt m_iniCRC;							///< CRC of important INI files
	UnsignedInt m_exeCRC;							///< CRC of the executable

	BodyDamageType m_movementPenaltyDamageState;	///< at this body damage state, we have movement penalties

	Int m_groupSelectMinSelectSize;		// min number of units to treat as group select for audio feedback
	Real m_groupSelectVolumeBase;			// base volume for group select sound
	Real m_groupSelectVolumeIncrement;// increment to volume for selecting more units
	Int m_maxUnitSelectSounds;				// max number of select sounds to play per selection

	Real m_selectionFlashSaturationFactor; /// how colorful should the selection flash be? 0-4
	Bool m_selectionFlashHouseColor ;  /// skip the house color and just use white.

	Real m_cameraAudibleRadius;				///< If the camera is being used as the position of audio, then how far can we hear?
	Real m_groupMoveClickToGatherFactor; /** if you take all the selected units and calculate the smallest possible rectangle 
																			 that contains them all, and click within that, all the selected units will break 
																			 formation and gather at the point the user clicked (if the value is 1.0). If it's 0.0,
																			 units will always keep their formation. If it's <1.0, then the user must click a 
																			 smaller area within the rectangle to order the gather. */

	Int m_antiAliasBoxValue;          ///< value of selected antialias from combo box in options menu
	Bool m_languageFilterPref;        ///< Bool if user wants to filter language
	Bool m_loadScreenDemo;						///< Bool if true, run the loadscreen demo movie
	Bool m_disableRender;							///< if true, no rendering!

	Bool m_saveCameraInReplay;
	Bool m_useCameraInReplay;

	Real m_shakeSubtleIntensity;			///< Intensity for shaking a camera with SHAKE_SUBTLE
	Real m_shakeNormalIntensity;			///< Intensity for shaking a camera with SHAKE_NORMAL
	Real m_shakeStrongIntensity;			///< Intensity for shaking a camera with SHAKE_STRONG
	Real m_shakeSevereIntensity;			///< Intensity for shaking a camera with SHAKE_SEVERE
	Real m_shakeCineExtremeIntensity; ///< Intensity for shaking a camera with SHAKE_CINE_EXTREME
	Real m_shakeCineInsaneIntensity;  ///< Intensity for shaking a camera with SHAKE_CINE_INSANE
	Real m_maxShakeIntensity;					///< The maximum shake intensity we can have
	Real m_maxShakeRange;							///< The maximum shake range we can have

	Real m_sellPercentage;						///< when objects are sold, you get this much of the cost it would take to build it back
	Real m_baseRegenHealthPercentPerSecond;	///< auto healing for bases
	UnsignedInt m_baseRegenDelay;			///< delay in frames we must be damage free before we can auto heal

#ifdef ALLOW_SURRENDER
	Real m_prisonBountyMultiplier;		///< the cost of the unit is multiplied by this and given to the player when prisoners are returned to the a prison with KINDOF_COLLECTS_PRISON_BOUNTY
	Color m_prisonBountyTextColor;		///< color of the text that displays the money acquired at the prison
#endif

	Color m_hotKeyTextColor;					///< standard color for all hotkeys.

  //THis is put on ice until later - M Lorenzen
  //	Int m_cheaterHasBeenSpiedIfMyLowestBitIsTrue; ///< says it all.. this lives near other "colors" cause it is masquerading as one
	
	AsciiString m_specialPowerViewObjectName;	///< Created when certain special powers are fired so players can watch.

	std::vector<AsciiString> m_standardPublicBones;

	Real m_standardMinefieldDensity;
	Real m_standardMinefieldDistance;

	
	Bool  m_showMetrics;								///< whether or not to show the metrics.
	Money m_defaultStartingCash;				///< The amount of cash a player starts with by default.

	/* Peace time, the lobby option.  The radius is measured from a command center's centre and the
		 damage is dealt once a second to every enemy unit inside it, so 250 kills a Crusader in about
		 four seconds - long enough to reverse out of, short enough that scouting a base under the
		 truce costs the scout. */
	Real m_peaceTimeBaseRadius;					///< how far an enemy command center's ground burns during peace time
	Real m_peaceTimeBaseDamage;					///< damage per second dealt inside that radius


	Bool m_debugShowGraphicalFramerate;		///< Whether or not to show the graphical framerate bar.

	Int m_powerBarBase;										///< Logrithmic base for the power bar scale
	Real m_powerBarIntervals;							///< how many logrithmic intervals the width will be divided into
	Int m_powerBarYellowRange;						///< Red if consumption exceeds production, yellow if consumption this close but under, green if further under
	Real m_displayGamma;									///<display gamma that's adjusted with "brightness" control on options screen.

	UnsignedInt m_unlookPersistDuration;	///< How long after unlook until the sighting info executes the undo

	Bool m_shouldUpdateTGAToDDS;					///< Should we attempt to update old TGAs to DDS stuff on loadup?
  
	UnsignedInt m_doubleClickTimeMS;	///< What is the maximum amount of time that can seperate two clicks in order
																		///< for us to generate a double click message?

	RGBColor m_shroudColor;						///< What color should the shroud be?  Remember, this is a lighting multiply, not an add
	UnsignedByte m_clearAlpha;				///< 255 means perfect visibility
	UnsignedByte m_fogAlpha;					///< 127 means fog is half as obscuring as shroud
	UnsignedByte m_shroudAlpha;				///< 0 makes this opaque, but they may get fancy

	// network timing values.
	UnsignedInt m_networkFPSHistoryLength;		      	///< The number of fps history entries
	UnsignedInt m_networkLatencyHistoryLength;      	///< The number of ping history entries.
	UnsignedInt m_networkCushionHistoryLength;      	///< The number of cushion values to keep.
	UnsignedInt m_networkRunAheadMetricsTime;	      	///< The number of miliseconds between run ahead metrics things
	UnsignedInt m_networkKeepAliveDelay;			      	///< The number of seconds between when the connections to each player send a keep-alive packet.
	UnsignedInt m_networkRunAheadSlack;				      	///< The amount of slack in the run ahead value. This is the percentage of the calculated run ahead that is added.
	UnsignedInt m_networkDisconnectTime;			      	///< The number of milliseconds between when the game gets stuck on a frame for a network stall and when the disconnect dialog comes up.
	UnsignedInt m_networkPlayerSilenceTime;		      	///< The number of milliseconds we can go without hearing anything at all from a player before a stall is treated as a disconnect rather than a slow game.
	UnsignedInt m_networkStallCeilingTime;		      	///< The number of milliseconds a stall can last with everyone still talking before we bring up the disconnect screen anyway.
	UnsignedInt m_networkPlayerTimeoutTime;		      	///< The number of milliseconds between when a player's last keep alive command was recieved and when they are considered disconnected from the game.
	UnsignedInt	m_networkDisconnectScreenNotifyTime;  ///< The number of milliseconds between when the disconnect screen comes up and when the other players are notified that we are on the disconnect screen.
	
	Real				m_keyboardCameraRotateSpeed;    ///< How fast the camera rotates when rotated via keyboard controls.
  Int					m_playStats;									///< Int whether we want to log play stats or not, if <= 0 then we don't log

#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	Bool m_specialPowerUsesDelay ;
#endif
  Bool m_TiVOFastMode;            ///< When true, the client speeds up the framerate... set by HOTKEY!

	/* Synthetic link conditions - the transport can hold incoming packets back and throw some of
		 them away, so a slow or lossy line can be reproduced on one machine.  These lived in the
		 debug-only block below, and this fork only ever ships Release, so the switch they drive did
		 not exist in any build anyone could run.  All five default to off. */
	Int m_latencyAverage;					///< Average latency to insert, in milliseconds
	Int m_latencyAmplitude;				///< Amplitude of the sinusoidal modulation of that latency, in milliseconds
	Int m_latencyPeriod;					///< Period of that modulation, in milliseconds; 0 means no modulation
	Int m_latencyNoise;						///< Max amplitude of the jitter thrown on top, in milliseconds
	Int m_packetLoss;							///< Percent of incoming packets to drop
  


#if defined(_DEBUG) || defined(_INTERNAL)
	Bool m_wireframe;
	Bool m_stateMachineDebug;
	Bool m_shroudOn;
	Bool m_fogOfWarOn;
	Bool m_jabberOn;
	Bool m_munkeeOn;
	Bool m_allowUnselectableSelection;			///< Are we allowed to select things that are unselectable?
	Bool m_disableCameraFade;								///< if true, script commands affecting camera are disabled
	Bool m_disableScriptedInputDisabling;		///< if true, script commands can't disable input
	Bool m_disableMilitaryCaption;					///< if true, military briefings go fast
	Int m_benchmarkTimer;										///< how long to play the game in benchmark mode?
  Bool m_checkForLeaks;
	Bool m_vTune;
	Bool m_debugCamera;						///< Used to display Camera debug information
	Bool m_debugVisibility;						///< Should we actively debug the visibility
	Int m_debugVisibilityTileCount;		///< How many tiles we should show when debugging visibility
	Real m_debugVisibilityTileWidth;	///< How wide should these tiles be?
	Int m_debugVisibilityTileDuration;	///< How long should these tiles stay around, in frames?
	Bool m_debugThreatMap;						///< Should we actively debug the threat map
	UnsignedInt m_maxDebugThreat;			///< This value (and any values greater) will appear full RED.
	Int m_debugThreatMapTileDuration;	///< How long should these tiles stay around, in frames?
	Bool m_debugCashValueMap;					///< Should we actively debug the threat map
	UnsignedInt m_maxDebugValue;			///< This value (and any values greater) will appear full GREEN.
	Int m_debugCashValueMapTileDuration;	///< How long should these tiles stay around, in frames?
	RGBColor m_debugVisibilityTargettableColor;	///< What color should the targettable cells be?
	RGBColor m_debugVisibilityDeshroudColor;			///< What color should the deshrouding cells be?
	RGBColor m_debugVisibilityGapColor;					///< What color should the gap generator cells be?
	Bool m_debugProjectilePath;						///< Should we actively debug the bezier paths on projectiles
	Real m_debugProjectileTileWidth;			///< How wide should these tiles be?
	Int m_debugProjectileTileDuration;		///< How long should these tiles stay around, in frames?
	RGBColor m_debugProjectileTileColor;	///< What color should these tiles be?
	Bool m_debugIgnoreAsserts;						///< Ignore all asserts.
	Bool m_debugIgnoreStackTrace;					///< No stacktraces for asserts.
	Bool m_showCollisionExtents;	///< Used to display collision extents
  Bool m_showAudioLocations;    ///< Used to display audio markers and ambient sound radii
	Bool m_saveStats;
	Bool m_saveAllStats;
	Bool m_useLocalMOTD;
	AsciiString m_baseStatsDir;
	AsciiString m_MOTDPath;
	Bool m_extraLogging;					///< More expensive debug logging to catch crashes.
#endif

	Bool				m_isBreakableMovie;							///< if we enter a breakable movie, set this flag
	Bool				m_breakTheMovie;								///< The user has hit escape!
	AsciiString m_modDir;
	AsciiString m_modBIG;

	//-allAdvice feature
	//Bool m_allAdvice;


	// the trailing '\' is included!
  const AsciiString &getPath_UserData() const { return m_userDataDir; }

private:

	static const FieldParse s_GlobalDataFieldParseTable[];

	// this is private, since we read the info from Windows and cache it for
	// future use. No one is allowed to change it, ever. (srj)
	AsciiString m_userDataDir;
	
	static GlobalData *m_theOriginal;		///< the original global data instance (no overrides)
	GlobalData *m_next;									///< next instance (for overrides)
	GlobalData *newOverride( void );		/** create a new override, copy data from previous
																			override, and return it */


	GlobalData(const GlobalData& that) { DEBUG_CRASH(("unimplemented")); }
	GlobalData& operator=(const GlobalData& that) { DEBUG_CRASH(("unimplemented")); return *this; }

};

// singleton
extern GlobalData* TheWritableGlobalData;

#define TheGlobalData ((const GlobalData*)TheWritableGlobalData)

/** Derive m_windowed - and, for borderless, the resolution and edge scrolling - from m_windowMode.
	*
	* Anything that writes m_windowMode calls this afterwards: the preferences pass, and every command
	* line switch that picks a mode.  Keeping the derivation in one place is what stops -borderless and
	* a borderless Options.ini from disagreeing about what borderless entails. */
extern void applyWindowMode( void );

#endif
