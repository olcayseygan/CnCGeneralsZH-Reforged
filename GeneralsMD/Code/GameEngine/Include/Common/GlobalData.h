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
#include "GameClient/RadiusDecal.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
struct FieldParse;
enum _TerrainLOD : Int;
class GlobalData;
class INI;
class WeaponBonusSet;
class ThingTemplate;
enum BodyDamageType : Int;
enum AIDebugOptions : Int;

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////

/// the sides whose ranks carry their own bonuses (m_veterancyWeaponBonus and the rest)
enum VeterancySide
{
	VETERANCY_SIDE_NONE = -1,
	VETERANCY_SIDE_AMERICA,
	VETERANCY_SIDE_CHINA,
	VETERANCY_SIDE_GLA,
	VETERANCY_SIDE_TURKIYE,

	VETERANCY_SIDE_COUNT
};

/// America, China, GLA, Turkiye: the side in each rank insignia's image name and each rank's label
extern const char *const TheVeterancySideNames[VETERANCY_SIDE_COUNT];

enum { RANK_GRADES_MAX = 32 };	///< promotions a side's ladder may have; America's 28 is the longest

/// the stats a grade carries, each looked up per grade in RankLadder::m_stat
enum RankStat
{
	RANK_STAT_RATE_OF_FIRE,
	RANK_STAT_DAMAGE,
	RANK_STAT_HEALTH,
	RANK_STAT_RANGE,
	RANK_STAT_SPEED,
	RANK_STAT_SELF_REPAIR,		///< share of max health healed a second, not a multiplier

	RANK_STAT_COUNT
};

/** What a grade carries besides its stats, the Perk_<side> lines.  A grade holds every perk of the
	* grades below it, and of one kind the strongest, so a later line of the same kind replaces an
	* earlier one by being larger. */
enum RankPerk
{
	RANK_PERK_SIGHT,							///< own shroud clearing, a multiplier
	RANK_PERK_STILL_GUARD,				///< share of damage taken away while standing still
	RANK_PERK_MEND,								///< share of max health healed a second once SelfRepairDelay unhurt
	RANK_PERK_SPOTTER,						///< sees as a spotter does (ThingTemplate SpotterSight), and is jammed as one
	RANK_PERK_SCATTER,						///< share of its own weapons' scatter taken away
	RANK_PERK_DEATH_RALLY,				///< on death, allies within its aura radius take this share less damage for RallyTime
	RANK_PERK_DETECT,							///< stealthed enemies within this radius are detected
	RANK_PERK_AURA_RADIUS,				///< every aura radius it gives, a multiplier (DETECT's excepted)
	RANK_PERK_RATE_OF_FIRE,				///< added to its own weapons' rate of fire, on top of the grade's stat
	RANK_PERK_RANGE,							///< added to its own weapons' range, on top of the grade's stat
	RANK_PERK_HEALTH,							///< its max health times 1 + this, on top of the grade's stat
	RANK_PERK_SPEED,							///< its speed times 1 + this, on top of the grade's stat
	RANK_PERK_LAST_STAND,					///< added to its rate of fire while it is below RANK_LAST_STAND_HEALTH
	RANK_PERK_SCAVENGE,						///< each kill it scores pays its player this share of the victim's cost
	RANK_PERK_DESIGNATE,					///< seconds whatever it hurts wears FAERIE_FIRE, which everyone shooting at it fires faster on
	RANK_PERK_DESIGNATE_REVEAL,		///< its mark also detects a stealthed target for as long
	RANK_PERK_SALVAGE_CRATE,			///< on death it leaves the Veterancy block's SalvageCrate
	RANK_PERK_AURA_RATE_OF_FIRE,	///< the first of the aura perks, in RankAura order
	RANK_PERK_AURA_SIGHT,
	RANK_PERK_AURA_GUARD,
	RANK_PERK_AURA_RANGE,
	RANK_PERK_AURA_MEND,
	RANK_PERK_AURA_PACE,
	RANK_PERK_AURA_DRILL,
	RANK_PERK_AURA_STEADY,
	RANK_PERK_AURA_CAPTURE,
	RANK_PERK_AURA_SPOILS,

	RANK_PERK_COUNT
};

/// the share of max health under which the LAST_STAND perk fires faster
#define RANK_LAST_STAND_HEALTH 0.35f

/** Who an aura line reaches besides its leader's allies' units: Perk_<side> = grade AURA_<kind> value
	* radius INFANTRY, or STRUCTURES for units and buildings both. */
enum RankReach
{
	RANK_REACH_UNITS,							///< every allied unit, buildings left out
	RANK_REACH_INFANTRY,					///< allied infantry only
	RANK_REACH_STRUCTURES,				///< allied units and buildings

	RANK_REACH_COUNT
};

/** A leader's bonus to friendly units within its aura radius, refilled once a second.  Of one kind a
	* unit takes the strongest leader's and nothing more.  RALLY is the death rally, timed instead. */
enum RankAura
{
	RANK_AURA_RATE_OF_FIRE,				///< added to weapon rate of fire, up to RankAuraRateOfFireCap
	RANK_AURA_SIGHT,							///< shroud clearing, 1 + this
	RANK_AURA_GUARD,							///< damage taken, 1 - this
	RANK_AURA_RANGE,							///< an artillery weapon's range, added
	RANK_AURA_MEND,								///< share of max health healed a second, hurt or not
	RANK_AURA_PACE,								///< speed, 1 + this
	RANK_AURA_DRILL,							///< experience a second (DrillRate) up to this grade of the unit's own ladder
	RANK_AURA_STEADY,							///< a timed paralysis or EMP runs 1 - this as long, subdual wears off 1 / (1 - this) as fast
	RANK_AURA_CAPTURE,						///< taking a building takes 1 - this as long
	RANK_AURA_SPOILS,							///< each kill it scores pays its player this share of the victim's cost
	RANK_AURA_RALLY,							///< damage taken, 1 - this, until the rally runs out

	RANK_AURA_COUNT,
	RANK_AURA_PASSED = RANK_AURA_RALLY	///< the kinds the once-a-second pass writes
};

extern const char *const TheRankPerkNames[RANK_PERK_COUNT + 1];
extern const char *const TheRankAuraNames[RANK_AURA_COUNT];
extern const char *const TheRankReachNames[RANK_REACH_COUNT + 1];

/** One side's rank ladder, the Veterancy block in BalanceReforged.ini (docs/plans/RANK-LADDER.md).
	* A unit's grade is a pure function of its experience and its template's E, its regular
	* ExperienceValue: the step to grade g costs E x ratio^(g-1), the ratio set so the top grade costs
	* m_top x E. EA's level follows the grade through the pins. Nothing here is saved; the grade is
	* recomputed from the experience whenever it is needed. */
struct RankLadder
{
	Int		m_grades;													///< promotions; 0 is a side without a ladder, on EA's four levels
	Real	m_top;														///< experience to the top grade, in same-class kills (E)
	Int		m_pin[LEVEL_COUNT];								///< the grade each EA level starts on; m_pin[LEVEL_REGULAR] is 0
	Int		m_heroStart;											///< the insignia a hero wears at grade 0
	Real	m_statTop[RANK_STAT_COUNT];				///< each stat at the top of the ladder
	Real	m_auraRadius;											///< a leader's aura radius before RANK_PERK_AURA_RADIUS
	Real	m_perkAt[RANK_GRADES_MAX + 1][RANK_PERK_COUNT];	///< the perks a grade's own lines name, 0 for none
	Real	m_radiusAt[RANK_GRADES_MAX + 1][RANK_PERK_COUNT];	///< an aura line's own radius, 0 for the one before it, or m_auraRadius
	RankReach m_reachAt[RANK_GRADES_MAX + 1][RANK_PERK_COUNT];	///< an aura line's receivers

	// filled by finish()
	Real	m_ratio;
	Real	m_total[RANK_GRADES_MAX + 1];			///< experience to hold grade g, in E
	Real	m_stat[RANK_GRADES_MAX + 1][RANK_STAT_COUNT];	///< each stat at grade g
	Real	m_perk[RANK_GRADES_MAX + 1][RANK_PERK_COUNT];	///< each perk grade g holds, the strongest of its grade and those below
	Real	m_radius[RANK_GRADES_MAX + 1][RANK_PERK_COUNT];	///< the radius that came with it, carried up the same way
	RankReach m_reach[RANK_GRADES_MAX + 1][RANK_PERK_COUNT];	///< the receivers that came with it

	void clear();
	/** The ratio from m_grades and m_top, the totals, and the stats: worth (rate of fire times damage
		* times health) is 1 + worthGain x sqrt(experience / top) at each grade's experience, and every
		* stat is its top value raised to ln(worth) / ln(worth at the top), so the split holds all the way up. */
	void finish( Real worthGain );
	Int threshold( Int unitValue, Int grade ) const;						///< experience to hold the grade, rounded once
	Int gradeOf( Int unitValue, Int experience ) const;					///< the highest grade the experience holds
	VeterancyLevel levelOf( Int grade ) const;									///< the EA level whose pin the grade has passed
	/** A gain clipped one point short of the second threshold above the current grade, so one kill
		* lifts one grade at most. Experience past the next-to-last grade is not clipped. */
	Int clipGain( Int unitValue, Int experience, Int gain ) const;
	Int insigniaIndex( Int grade, Bool hero ) const;						///< a hero's insignia runs from m_heroStart to the top over its grades
	Real perk( Int grade, RankPerk perk ) const;								///< what the grade holds of the kind, 0 for nothing
	/// a Perk_<side> line's tokens after the =: grade kind [value [radius [reach]]]
	void addPerkLine( const char *const *tokens, Int count );
	/** The aura's own radius at the grade (RALLY's is m_auraRadius, and so is a line that gave none on
		* a side with no radius before it), times the AURA_RADIUS perk. */
	Real auraRadius( Int grade, RankAura aura ) const;
	RankReach auraReach( Int grade, RankAura aura ) const;			///< who the aura reaches; RALLY reaches units
	Real aura( Int grade, RankAura aura ) const;								///< what a leader of this grade gives; RALLY is its death rally
	/** What a DRILL aura capped at capGrade adds to a unit with the experience this second: rate times
		* its E, at least 1, never past the cap's threshold, 0 once there. */
	Int drillGain( Int unitValue, Int experience, Int capGrade, Real rate ) const;

	static Int unitValue( const ThingTemplate *thing );				///< E: the regular ExperienceValue, else half the Veteran threshold, at least 1
};

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

/// -team left a slot alone: its team comes from -teams, or none
enum { AUTO_SKIRMISH_TEAM_UNSET = -2 };

/** The mouse: Zero Hour 1.04's clicks, drags and edge scroll, or this fork's own (right button orders,
	* left drag selects with shift adding, the camera drag and the wider edge band).  Picked for each run
	* by -mouse, Classic without it.  Client only, and never read by GameLogic. */
enum { MOUSE_STYLE_CLASSIC = 0, MOUSE_STYLE_REFORGED = 1, MOUSE_STYLE_COUNT = 2 };

/** The language the game's words are shown in.  English is the string table EA shipped; every other
	* entry names a translation GameText lays over it.  Speech and video stay what the install has. */
enum TextLanguageType
{
	TEXT_LANGUAGE_ENGLISH	= 0,
	TEXT_LANGUAGE_TURKISH	= 1,
	TEXT_LANGUAGE_GERMAN	= 2,
	TEXT_LANGUAGE_UKRAINIAN	= 3,
	TEXT_LANGUAGE_RUSSIAN	= 4,
	TEXT_LANGUAGE_FRENCH	= 5,

	TEXT_LANGUAGE_COUNT		= 6,
};

/** The LightingStyle option.  Classic draws a map's lights as the map gives them; Reforged takes
	* the same lights and moves weight from the flat ambient and the fill lights to the sun, warms the
	* sun and cools the ambient (reforgeLighting), deepens the sun's shadow to match, and grades the
	* Direct3D 11 picture.  Client only: nothing in GameLogic reads a light. */
enum LightingStyleType
{
	LIGHTING_STYLE_CLASSIC	= 0,
	LIGHTING_STYLE_REFORGED	= 1,

	LIGHTING_STYLE_COUNT		= 2,
};

/** The AI profile knobs -aiknobsoff can switch off for a measurement. */
enum
{
	AIKNOB_ENGAGE_GATE	= 1,
	AIKNOB_ANSWER_ARMY	= 2,
	AIKNOB_MASS_UNIT		= 4,
	AIKNOB_BASE_WATCH		= 8,
	AIKNOB_STRIKE_DODGE	= 16,
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

	/// Whether the dynamic LOD follows the frame rate this run: the player's (or the preset's) setting,
	/// unless -noDynamicLOD turned it off.  Read this, not m_enableDynamicLOD, wherever the running game
	/// acts on it; m_enableDynamicLOD is the preference the options menu shows and saves.
	Bool isDynamicLODEnabled() const { return m_enableDynamicLOD && !m_noDynamicLODOverride; }
	/// The particle ceiling in force: -particlecap's, or the options slider's MaxParticleCount.
	Int getEffectiveParticleCap() const { return (m_particleCapOverride > 0) ? m_particleCapOverride : m_maxParticleCount; }
	/// The Direct3D 11 particle ring in force: -gpuparticles' (0 included, which is off), or the detail preset's.
	Int getEffectiveGpuParticleCapacity() const { return (m_gpuParticleCapacityOverride >= 0) ? m_gpuParticleCapacityOverride : m_gpuParticleCapacity; }

	static void parseGameDataDefinition( INI* ini );
	static void parseVeterancyDefinition( INI* ini );	///< the Veterancy block, m_veterancy*

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
	Int m_fullscreenScaling;	///< FullscreenScalingType: a fullscreen picture of another shape stretched or barred
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
	Bool m_noDynamicLODOverride;	// "-noDynamicLOD": off for this run, whatever the static preset or Options.ini set
	Bool m_enableStaticLOD;
	Int m_terrainLODTargetTimeMS;
	Bool m_useAlternateMouse;			///< EA's Alternate Mouse Setup, Options.ini UseAlternateMouse: Classic only, Reforged never reads it (client only)
	Bool m_clientRetaliationModeEnabled;
	Bool m_doubleClickAttackMove;
	Bool m_rightMouseAlwaysScrolls;
	Bool m_useCloudPlane;
	Bool m_useShadowVolumes;
	Bool m_useShadowVolumesForSkins;	// "UseShadowVolumesForSkins": also cast volume shadows off skinned meshes
	Bool m_useShadowDecals;
	Bool m_shadowsForProjectiles;	// "ShadowsForProjectiles": missiles and bombs that have no shadow of their own get a decal one
	Bool m_startAtMaxZoom;					// "OpenAtMaxZoom" in Options.ini: a game opens as far out as the wheel goes, not 300 over the ground
	Bool m_turboMode;								// "TurboMode" in Options.ini: a campaign or challenge mission runs at twice normal speed; a lobby has its own box
	Bool m_shadowsForProps;				// "ShadowsForProps": fences, rubbish, shrubs - scenery the art gave no shadow at all
	Bool m_shadowsForParticles;		// "ShadowsForParticles": big alpha-blended particle clouds drop a soft blob on the ground
	Bool m_volumetricSmokeShadows;	// "VolumetricSmokeShadows": smoke and dust shade the world and themselves through the sun's map (Direct3D 11)
	Bool m_smokeFireLighting;		// "SmokeFireLighting": smoke takes light from the fire beside it
	Bool m_smoothMotion;				// R1: on a display faster than the logic, models drawn between their last two logic states (W3DSmoothMotion.h); -nosmoothmotion turns it off
	Bool m_smoothMotionAnyDisplay;	// -smoothmotion: the same whatever the display's refresh rate
	Int m_waterStyle;					// "WaterStyle": 0 = Classic, 1 = Modern (SDL); applied between render frames
	Int m_cloudStyle;					// "CloudStyle": 0 = Classic (default), 1 = Volumetric (when supported)
	Bool m_explosionDebrisTrails;	// "DebrisTrails": blasts throw smoke-trailing fragments and burning shrapnel
	Int m_shadowQuality;				// "ShadowQuality": the sun's depth map at 1024, 2048 or 4096 texels a side (Direct3D 11)
	Int m_graphicsQuality;			// "GraphicsQuality": where the options menu's Performance / Quality slider stands; Custom takes the particle ring from the preset at or below it
	Bool m_classicGraphics;				// "ClassicGraphics": the game's own art, tile, shadows and picture; read at startup
	Int m_lightingStyle;					// "LightingStyle": LightingStyleType; the map's lights are rebalanced as each map loads
	Bool m_shadowMap;							// "-shadowmap": draw the casters into the sun's depth buffer as well
	Bool m_shadowMapReport;				// "-shadowmapreport": log what ended up in that buffer, once a second
	Bool m_shadowMapOldCasters;		// "-shadowmapoldcasters": every caster through the mesh renderer, none instanced
	Bool m_shadowMapOnly;					// "-shadowmaponly": the map's shadows with the stencil volumes' own darkening off
	Real m_shadowMapPenumbra;			// "-shadowtune": penumbra per world unit of gap under the caster, 0 for the built-in
	Real m_shadowMapSkyFill;			// how much of a wide shadow the sky fills back in
	Real m_shadowMapStrength;			// how dark a fully blocked pixel goes
	Bool m_contactShadows;					// "ContactShadows": the soft patch under a structure's footprint
	Real m_shadowMapWidest;				// how far the filter may open, in texels
	Bool m_particleGroundBounce;	// "-particlebounce": terrain collision on for every particle system
	Real m_smokeThickness;				// "-smoke": how much longer and thicker every smoke system runs, 0 for shipped behaviour
	Int  m_particleCapOverride;		// "-particlecap": stand in for the options slider's MaxParticleCount, 0 to use it
	Int  m_gpuParticleCapacity;		// spawn-record slots in the Direct3D 11 particle ring, the detail preset's GpuParticleCapacity; 0 keeps every particle on the CPU
	Int  m_gpuParticleCapacityOverride;	// "-gpuparticles": stand in for the preset's ring for one run, -1 to use it
	Bool m_gpuParticleCheck;			// "-gpuparticlecheck": run the CPU path beside the GPU one and log the closed form's largest error
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
	Int m_healthBarColors;				///< 0 the green-to-red health ramp, 1 the owner's player colour (client only)
	Int m_mouseStyle;							///< MOUSE_STYLE_CLASSIC or MOUSE_STYLE_REFORGED, set by -mouse only (client only, never GameLogic)
	Bool isClassicMouse() const { return m_mouseStyle == MOUSE_STYLE_CLASSIC; }
	/// which button gives a click on the ground its order.  Reforged mouse: the right.  Classic: the left,
	/// as 1.04 shipped, or the right under EA's Alternate Mouse Setup, where the left only selects
	Bool leftButtonOrders() const { return isClassicMouse() && !m_useAlternateMouse; }
	Bool rightButtonOrders() const { return !leftButtonOrders(); }
	Int m_playerColorScheme;			///< PlayerColorSchemeType: whose colour the client draws (client only)
	Int m_textLanguage;						///< TextLanguageType: the translation GameText lays over the CSF, read once at startup (client only)
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
	Bool m_snapBuildToNeighbour;		///< pull a structure flush against the edge of one already standing beside it
	Bool m_nudgeBuildPlacement;			///< slide a blocked structure to the nearest spot it does fit
	Int m_moneyPerMinute;				///< income every player gets once a minute regardless of supply lines; 0 = off
	Real m_buildPlacementOpacity;		///< how solid the structure riding the cursor is drawn, 0..1
	Bool m_buildPlacementShadows;		///< whether that structure casts a shadow while it rides the cursor
	Bool m_zoomToCursor;				///< the mouse wheel zooms toward whatever the cursor is over
	Int m_zoomSpeed;					///< "ZoomSpeed" in Options.ini: percent of the height one wheel notch moves the camera; 100 is the game's own
	Bool m_isometricCamera;				///< the tactical view from far off down a narrow cone, near enough orthographic
	Bool m_cameraShake;						///< "CameraShake": blasts and scripts may shake the view (client only)
	Int m_dragTolerance;				///< pixels the pointer may travel with a button held before the press is a drag; replaces Mouse.ini's DragTolerance
	Bool m_formationDrag;				///< with the move, attack move or guard key armed, a left drag spreads the selection along the line drawn
	Bool m_buildWithoutWorker;			///< with nothing selected the command bar shows a builder's buildings; off, an empty bar as 1.04 had it
	Bool m_chromaLighting;				///< put the state of the match on Razer hardware
	Int m_menuTransitionSpeed;			///< percent of the authored speed the menus slide and fade at; 100 = as drawn
	Int m_textureFilterMode;			///< 0 bilinear, 1 trilinear, 2 anisotropic
	Int m_anisotropyLevel;				///< samples anisotropic filtering may take; 0 = whatever the card offers
	Bool m_showNetBox;					///< the player's own switch for the top right info strip (InGameUI::drawHudOverlay)
	Bool m_showEmptyBuildingPips;		///< the row of empty slots over a building nobody is in
	Bool m_workersReturnToSupply;		///< a worker that finishes a build job goes back to the dock it left
	Bool m_archiveReplays;					///< keep a timestamped copy of every replay, instead of only the last one
	Int m_bloomIntensity;				///< Glow strength in percent, 0 = off; the renderer derives the rest
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
	Int m_aiKnobsOffParity;				///< -aiknobsoff even|odd|all <names>: those slots of a skirmish play with these profile knobs off; -1 none, 2 all
	Int m_aiKnobsOffMask;					///< ... which knobs: AIKNOB_* bits
	Int m_autoSkirmishTeams;				///< -teams <n>: split the auto-skirmish slots into n allied teams (0 or 1 = free-for-all)
	Int m_peaceTime;								///< -peacetime <n>: the lobby's peace time, in minutes, for an -autoskirmish run
	Bool m_unitLimit;								///< -unitlimit: the lobby's unit limit for an -autoskirmish run
	Int m_incomeSharing;						///< -incomesharing <n>: the lobby's income sharing, an IncomeSharing, for an -autoskirmish run
	Int m_techRespawn;							///< -techrespawn <n>: the lobby's tech building respawn, in minutes, for an -autoskirmish run
	Int m_supplyPileLimit;					///< -supplypilelimit <n>: the lobby's supply pile limit, in players a pile, for an -autoskirmish run
	Bool m_turboMatch;							///< -turbomode: the lobby's turbo mode, for an -autoskirmish or -netgame run
	Int m_superweapons;							///< -superweapons <n>: the lobby's superweapon rule, a SUPERWEAPONS_ mode, for an -autoskirmish run
	Int m_startingCash;							///< -startingcash <n>: the lobby's starting cash for an -autoskirmish run (0 = DefaultStartingCash)
	Int m_maxGameFrames;						///< -maxframes <n>: quit after n logic frames however the match is going (0 = no limit)
	Int m_screenShotFrame;					///< -screenshot <n>: save one picture when the run reaches logic frame n (0 = never)
	Int m_videoStartFrame;					///< -video <from> <to> [name]: the first logic frame recorded
	Int m_videoEndFrame;						///< -video: the last logic frame recorded (0 = no video)
	AsciiString m_videoName;				///< -video: the recording is Videos\<name>.mp4 next to the save games
	Int m_videoPictures;						///< -recordfps <n>: pictures recorded of each logic frame, n over LOGICFRAMES_PER_SECOND (1 = a picture a logic frame)
	Bool m_directorRecord;					///< -directorrecord [name]: the director films the whole match, split for two fights, and the run quits at its end
	AsciiString m_directorScoutFile;	///< -directorscout <file>: -directorrecord's first pass, headless, writes the match's fights and special powers here
	AsciiString m_directorTimelineFile;	///< -directortimeline <file>: what the first pass wrote, which the filming pass reads to arrive before each fight
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
	Int m_autoSkirmishTakeoverSlot;	///< -takeover <slot>: only that seat is driverless and the AI plays the rest; -1 for every seat
	AsciiString m_autoSkirmishSide[ MAX_PLAYER_COUNT ];	///< -side <slot> <faction>: name that slot's faction instead of drawing it from the seed
	Int m_autoSkirmishTeam[ MAX_PLAYER_COUNT ];	///< -team <slot> <n>: that slot's team, -1 for none; AUTO_SKIRMISH_TEAM_UNSET leaves it to -teams
	AsciiString m_autoSkirmishSeatName[ MAX_PLAYER_COUNT ];	///< -seatname <slot> <name>: what that seat is called instead of its difficulty
	Int m_autoSkirmishPersona[ MAX_PLAYER_COUNT ];	///< -aipersona <slot> <name>: that AI seat's AIPersonality, -1 leaves it to the two below
	Int m_autoSkirmishPersonaOdd;		///< -aipersona2 <name>: the odd seats' personality, -1 for the same as every seat's
	Int m_autoSkirmishPersonaAll;		///< -aipersonaall <name>: every AI seat's personality (0, Normal, by default)
	Int autoSkirmishPersona( Int slot ) const		///< what the three switches above give that seat
	{
		if (m_autoSkirmishPersona[ slot ] >= 0)
			return m_autoSkirmishPersona[ slot ];
		return ((slot & 1) && m_autoSkirmishPersonaOdd >= 0) ? m_autoSkirmishPersonaOdd : m_autoSkirmishPersonaAll;
	}
	AsciiString m_netGameHosts;				///< -netgame <ip>[,<ip>...]: the slot list of a LAN game started from the command line (empty = off)
	Bool m_netGameStarted;						///< that -netgame passed its checks and StartAutomatedGame ran, so every seat has this command line
	Int m_netGameLocalSlot;						///< -netslot <n>: which of those addresses this copy of the game is
	Int m_netGameAISlots;							///< -netai <n>: that many AI seats after the addresses, at the -aidiff rung
	AsciiString m_lanPlayerName;			///< -lanname <name>: the name this copy takes in the LAN lobby (empty = the one in the preferences)
	Bool m_lanLobbyOnStart;						///< -lanlobby: open the LAN lobby instead of stopping at the main menu
	Bool m_skirmishLobbyOnStart;			///< -skirmishlobby: open the skirmish staging room instead of stopping at the main menu
	Bool m_optionsMenuOnStart;				///< -optionsmenu: open the options screen over the main menu at startup
	Bool m_mapLight;								///< "MapLight": warm low sun and a still ground haze (Direct3D 11, client only)
	Bool m_teamGlow;								///< "TeamGlow": bright house colour glows (Direct3D 11, client only)
	Bool m_waterFoam;								///< "WaterFoam": foam, shore band and the sun's shadow on water (Direct3D 11, client only)
	Bool m_groundMarks;							///< "GroundMarks": craters, stains and longer treads that fade over minutes (Direct3D 11, client only)
	Bool m_firelight;								///< "Firelight": flames light the ground, walls and units around them (Direct3D 11, client only)
	Bool m_hemiAmbient;							///< "HemiAmbient": models take the ambient from the sky above and the ground below (Direct3D 11, client only)
	Bool m_ambientOcclusion;				///< "AmbientOcclusion": corners and the ground under what stands on it darken, half size in world units (Direct3D 11, client only)
	Bool m_weatherEffects;					///< "Weather": rain, blowing sand or snow by the map's ground, and a wind on trees and smoke (Direct3D 11, client only)
	Bool m_waterSplashes;						///< "WaterSplashes": a splash and rings where a blast or a falling body meets water (Direct3D 11, client only)
	Bool m_atmosphere;							///< "Atmosphere": the far ground fades toward the horizon and an empty frame takes a sky (Direct3D 11, client only)
	Bool m_structureCollapse;				///< "StructureCollapse": a building's last model goes into the ground (Direct3D 11, client only)
	Bool m_internalHdr;							///< "InternalHdr": what passes white in the half float scene keeps its colour on the way to eight bits; no HDR monitor output (Direct3D 11, client only)

	Int m_maxParticleCount;						///< maximum number of particles that can exist
	Int m_maxFieldParticleCount;			///< maximum number of field-type particles that can exist (roughly)
	WeaponBonusSet* m_weaponBonusSet;
	Real m_healthBonus[LEVEL_COUNT];			///< global bonuses to health for veterancy.

	/** The rank ladders, the Veterancy block in BalanceReforged.ini.  A unit whose template's Side starts
		* with America, China or GLA (so every general's copy too) climbs its side's ladder, and its stats
		* come from the grade instead of EA's VETERAN, ELITE and HERO lines and HealthBonus_*; Boss and
		* civilian units keep EA's.  A side's weapon table holds what the grade does not (China's HORDE)
		* and counts from the Veteran pin on. */
	RankLadder m_rankLadder[VETERANCY_SIDE_COUNT];
	Real m_rankWorthGain;																					///< worth at the top of every ladder is 1 + this
	Int m_rankCrateGrades;																				///< grades a veterancy crate gives per EA level it would have
	WeaponBonusSet* m_veterancyWeaponBonus[VETERANCY_SIDE_COUNT];	///< NULL: nothing beyond the grade
	UnsignedInt m_veterancySelfRepairDelay;												///< frames unhurt before self-repair starts
	Real m_rankAuraRateOfFireCap;																	///< the most a rate-of-fire aura adds
	Real m_rankDrillRate;																					///< what a drill aura adds a second, a share of the unit's E
	AsciiString m_rankSalvageCrate;																///< the crate a SALVAGE_CRATE perk leaves
	UnsignedInt m_rankRallyTime;																	///< frames a death rally lasts
	RadiusDecalTemplate m_rankAuraRing[RANK_AURA_COUNT];					///< the ground ring under every visible leader, one per aura kind (client)

	static VeterancySide veterancySideOf( const ThingTemplate *thing );
	const RankLadder *getRankLadder( const ThingTemplate *thing ) const;	///< NULL: the thing's side keeps EA's levels
	const WeaponBonusSet *getVeterancyWeaponBonus( const ThingTemplate *thing, VeterancyLevel level ) const;
	Real getRankStat( const ThingTemplate *thing, Int grade, RankStat stat ) const;	///< the grade's stat, or none without a ladder
	Real getRankHealthBonus( const ThingTemplate *thing, VeterancyLevel level, Int grade ) const;	///< the grade's on a ladder, else HealthBonus_*
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
	Int m_ambientVolume;							///< "AmbientVolume" in Options.ini: percent for looping world ambience, in place of the effects slider
	Int m_masterVolume;								///< "MasterVolume" in Options.ini: percent every other slider is multiplied by, music and speech included
	Bool m_dynamicMusic;							///< "DynamicMusic" in Options.ini: a skirmish's music follows the fight (MusicManager::update)
	Bool m_legacySound;								///< "LegacySound" in Options.ini: retail's mix, none of the v3.4 processing (MilesAudioManager::applyLegacySound)
	Bool m_muteInBackground;					///< "MuteInBackground" in Options.ini: silent while another window has the focus (WinMain's WM_ACTIVATE)
  Bool m_3DSoundPref;               ///< Whether user wants to use 3DSound or not

	Bool m_animateWindows;						///< Should we animate window transitions?

	Bool m_incrementalAGPBuf;
	
	UnsignedInt m_iniCRC;							///< CRC of important INI files
	UnsignedInt m_exeCRC;							///< CRC of the executable
	void computeExeCRC( void );				///< m_exeCRC from the build fingerprint, the version and the two scb as they resolve now
	UnsignedInt m_baseExeCRC;					///< m_exeCRC before any -mod mounted: the build without its mods, for the LAN mod check

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
	UnsignedInt m_networkSilentPlayerDropTime;	      	///< The number of milliseconds a player can go without sending anything before the disconnect screen drops them; replaces m_networkPlayerTimeoutTime there.
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
	std::vector<AsciiString> m_mods;	///< every -mod in command-line order, later wins: a folder ends in '\', anything else is a .big
	AsciiString m_modName;	///< UTF-8, each mod's "<name> <version>" from its mod.json (else the folder or archive name), joined by ", " in m_mods order; empty without -mod
	std::vector<AsciiString> m_modNames;	///< UTF-8, each mod's mod.json "name" (else the folder or archive name), in m_mods order
	std::vector<Bool> m_modIsPatch;	///< -modpatch: TRUE for each m_mods entry given as a patch; with no full -mod the patches stack as plain mods
	std::vector< std::vector<AsciiString> > m_modPatches;	///< -modfactions: the -modpatch folders over each m_mods[n], later wins
	std::vector<AsciiString> m_retailPatches;	///< -modfactions: the -modpatch folders given before any -mod, over retail's files
	Bool m_modFactions;	///< any full -mod given: every mod's factions load beside retail's under "M<n>~" names (ModNamespace).  "-modfactions" in a comment is this; the switch is gone

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

/** The Reforged lighting style applied to one time of day's lights, in place: the first light is
	* the sun, the other two the map's fills.  Derived from the values the map gave, so a dusk map
	* stays a dusk map; WorldHeightMap calls it on the tables it has just read, which is why it can
	* never be applied twice to the same numbers. */
extern void reforgeLighting( GlobalData::TerrainLighting lights[MAX_GLOBAL_LIGHTS] );

#endif
