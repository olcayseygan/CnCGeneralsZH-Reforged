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

// FILE: GlobalData.cpp ///////////////////////////////////////////////////////////////////////////
// The GameLogicData object
// Author: trolfs, Michael Booth, Colin Day, April 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

//#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_TERRAIN_LOD_NAMES
#define DEFINE_TIME_OF_DAY_NAMES
#define DEFINE_WEATHER_NAMES
#define DEFINE_BODYDAMAGETYPE_NAMES
#define DEFINE_PANNING_NAMES

#include "Common/CRC.h"
#include "Common/EarlyOptions.h"	// findUserDataDirectory
#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/INI.h"
#include "Common/Monitors.h"
#include "Common/OptionsCatalog.h"
#include "Common/registry.h"
#include "Common/UserPreferences.h"
#include "Common/Version.h"

#include "GameLogic/AI.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/BodyModule.h"

#include "GameClient/Color.h"
#include "GameClient/Drawable.h"	// PLACEMENT_SILHOUETTE_OPACITY, the default for BuildPlacementOpacity
#include "GameClient/PlayerColorScheme.h"	// PLAYER_COLORS_ORIGINAL, the default for PlayerColors
#include "GameClient/TerrainVisual.h"

#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/GameInfo.h" // for the SlotState an auto-started skirmish gives its AI slots

// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
GlobalData* TheWritableGlobalData = NULL;				///< The global data singleton

//-------------------------------------------------------------------------------------------------
GlobalData* GlobalData::m_theOriginal = NULL;

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ const FieldParse GlobalData::s_GlobalDataFieldParseTable[] = 
{
	{ "Windowed",									INI::parseBool,				NULL,			offsetof( GlobalData, m_windowed ) },
	{ "XResolution",							INI::parseInt,				NULL,			offsetof( GlobalData, m_xResolution ) },
	{ "YResolution",							INI::parseInt,				NULL,			offsetof( GlobalData, m_yResolution ) },
	{ "MapName",									INI::parseAsciiString,NULL,			offsetof( GlobalData, m_mapName ) },
	{ "MoveHintName",							INI::parseAsciiString,NULL,			offsetof( GlobalData, m_moveHintName ) },
	{ "UseTrees",									INI::parseBool,				NULL,			offsetof( GlobalData, m_useTrees ) },
	{ "UseFPSLimit",							INI::parseBool,				NULL,			offsetof( GlobalData, m_useFpsLimit ) },
	{ "DumpAssetUsage",						INI::parseBool,				NULL,			offsetof( GlobalData, m_dumpAssetUsage ) },
	{ "FramesPerSecondLimit",			INI::parseInt,				NULL,			offsetof( GlobalData, m_framesPerSecondLimit ) },
	{ "ChipsetType",							INI::parseInt,				NULL,			offsetof( GlobalData, m_chipSetType ) },
	{ "MaxShellScreens",					INI::parseInt,				NULL,			offsetof( GlobalData, m_maxShellScreens ) },
	{ "UseCloudMap",							INI::parseBool,				NULL,			offsetof( GlobalData, m_useCloudMap ) },
	{ "UseLightMap",							INI::parseBool,				NULL,			offsetof( GlobalData, m_useLightMap ) },
	{ "BilinearTerrainTex",				INI::parseBool,				NULL,			offsetof( GlobalData, m_bilinearTerrainTex ) },
	{ "TrilinearTerrainTex",			INI::parseBool,				NULL,			offsetof( GlobalData, m_trilinearTerrainTex ) },
	{ "MultiPassTerrain",					INI::parseBool,				NULL,			offsetof( GlobalData, m_multiPassTerrain ) },
	{ "AdjustCliffTextures",			INI::parseBool,				NULL,			offsetof( GlobalData, m_adjustCliffTextures ) },
	{ "Use3WayTerrainBlends",			INI::parseInt,				NULL,			offsetof( GlobalData, m_use3WayTerrainBlends ) },
	{ "StretchTerrain",						INI::parseBool,				NULL,			offsetof( GlobalData, m_stretchTerrain ) },
	{ "UseHalfHeightMap",					INI::parseBool,				NULL,			offsetof( GlobalData, m_useHalfHeightMap ) },
	
	
	{ "DrawEntireTerrain",					INI::parseBool,				NULL,			offsetof( GlobalData, m_drawEntireTerrain ) },
	{ "TerrainLOD",									INI::parseIndexList,	TerrainLODNames,	offsetof( GlobalData, m_terrainLOD ) },
	{ "TerrainLODTargetTimeMS",			INI::parseInt,				NULL,			offsetof( GlobalData, m_terrainLODTargetTimeMS ) },
	{ "RightMouseAlwaysScrolls",		INI::parseBool,				NULL,			offsetof( GlobalData, m_rightMouseAlwaysScrolls ) },
	{ "UseWaterPlane",							INI::parseBool,				NULL,			offsetof( GlobalData, m_useWaterPlane ) },
	{ "UseCloudPlane",							INI::parseBool,				NULL,			offsetof( GlobalData, m_useCloudPlane ) },
	{ "DownwindAngle",							INI::parseReal,				NULL,			offsetof( GlobalData, m_downwindAngle ) },
	{ "UseShadowVolumes",						INI::parseBool,				NULL,			offsetof( GlobalData, m_useShadowVolumes ) },
	{ "UseShadowVolumesForSkins",		INI::parseBool,				NULL,			offsetof( GlobalData, m_useShadowVolumesForSkins ) },
	{ "UseShadowDecals",						INI::parseBool,				NULL,			offsetof( GlobalData, m_useShadowDecals ) },
	{ "ShadowsForProjectiles",						INI::parseBool,				NULL,			offsetof( GlobalData, m_shadowsForProjectiles ) },
	{ "StartAtMaxZoom",											INI::parseBool,				NULL,			offsetof( GlobalData, m_startAtMaxZoom ) },
	{ "ContactShadows",										INI::parseBool,				NULL,			offsetof( GlobalData, m_contactShadows ) },
	{ "ShadowsForProps",									INI::parseBool,				NULL,			offsetof( GlobalData, m_shadowsForProps ) },
	{ "ShadowsForParticles",						INI::parseBool,				NULL,			offsetof( GlobalData, m_shadowsForParticles ) },
	{ "TextureReductionFactor",			INI::parseInt,				NULL,			offsetof( GlobalData, m_textureReductionFactor ) },
	{ "UseBehindBuildingMarker",		INI::parseBool,				NULL,			offsetof( GlobalData, m_enableBehindBuildingMarkers ) },
	{ "WaterPositionX",							INI::parseReal,				NULL,			offsetof( GlobalData, m_waterPositionX ) },
	{ "WaterPositionY",							INI::parseReal,				NULL,			offsetof( GlobalData, m_waterPositionY ) },
	{ "WaterPositionZ",							INI::parseReal,				NULL,			offsetof( GlobalData, m_waterPositionZ ) },
	{ "WaterExtentX",								INI::parseReal,				NULL,			offsetof( GlobalData, m_waterExtentX ) },
	{ "WaterExtentY",								INI::parseReal,				NULL,			offsetof( GlobalData, m_waterExtentY ) },
	{ "WaterType",									INI::parseInt,				NULL,			offsetof( GlobalData, m_waterType ) },
	{ "FeatherWater",						  	INI::parseInt,				NULL,			offsetof( GlobalData, m_featherWater ) },
	{ "ShowSoftWaterEdge",					INI::parseBool,				NULL,			offsetof( GlobalData, m_showSoftWaterEdge ) },

	// nasty ick, we need to save this data with a map and not hard code INI values
	{ "VertexWaterAvailableMaps1",		INI::parseAsciiString,	NULL,		offsetof( GlobalData, m_vertexWaterAvailableMaps[ 0 ] ) },
	{ "VertexWaterHeightClampLow1",		INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterHeightClampLow[ 0 ] ) },
	{ "VertexWaterHeightClampHi1",		INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterHeightClampHi[ 0 ] ) },
	{ "VertexWaterAngle1",						INI::parseAngleReal,		NULL,		offsetof( GlobalData, m_vertexWaterAngle[ 0 ] ) },
	{ "VertexWaterXPosition1",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterXPosition[ 0 ] ) },
	{ "VertexWaterYPosition1",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterYPosition[ 0 ] ) },
	{ "VertexWaterZPosition1",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterZPosition[ 0 ] ) },
	{ "VertexWaterXGridCells1",				INI::parseInt,					NULL,		offsetof( GlobalData, m_vertexWaterXGridCells[ 0 ] ) },
	{ "VertexWaterYGridCells1",				INI::parseInt,					NULL,		offsetof( GlobalData, m_vertexWaterYGridCells[ 0 ] ) },
	{ "VertexWaterGridSize1",					INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterGridSize[ 0 ] ) },
	{ "VertexWaterAttenuationA1",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationA[ 0 ] ) },
	{ "VertexWaterAttenuationB1",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationB[ 0 ] ) },
	{ "VertexWaterAttenuationC1",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationC[ 0 ] ) },
	{ "VertexWaterAttenuationRange1",	INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationRange[ 0 ] ) },

	// nasty ick, we need to save this data with a map and not hard code INI values
	{ "VertexWaterAvailableMaps2",		INI::parseAsciiString,	NULL,		offsetof( GlobalData, m_vertexWaterAvailableMaps[ 1 ] ) },
	{ "VertexWaterHeightClampLow2",		INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterHeightClampLow[ 1 ] ) },
	{ "VertexWaterHeightClampHi2",		INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterHeightClampHi[ 1 ] ) },
	{ "VertexWaterAngle2",						INI::parseAngleReal,		NULL,		offsetof( GlobalData, m_vertexWaterAngle[ 1 ] ) },
	{ "VertexWaterXPosition2",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterXPosition[ 1 ] ) },
	{ "VertexWaterYPosition2",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterYPosition[ 1 ] ) },
	{ "VertexWaterZPosition2",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterZPosition[ 1 ] ) },
	{ "VertexWaterXGridCells2",				INI::parseInt,					NULL,		offsetof( GlobalData, m_vertexWaterXGridCells[ 1 ] ) },
	{ "VertexWaterYGridCells2",				INI::parseInt,					NULL,		offsetof( GlobalData, m_vertexWaterYGridCells[ 1 ] ) },
	{ "VertexWaterGridSize2",					INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterGridSize[ 1 ] ) },
	{ "VertexWaterAttenuationA2",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationA[ 1 ] ) },
	{ "VertexWaterAttenuationB2",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationB[ 1 ] ) },
	{ "VertexWaterAttenuationC2",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationC[ 1 ] ) },
	{ "VertexWaterAttenuationRange2",	INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationRange[ 1 ] ) },

	// nasty ick, we need to save this data with a map and not hard code INI values
	{ "VertexWaterAvailableMaps3",		INI::parseAsciiString,	NULL,		offsetof( GlobalData, m_vertexWaterAvailableMaps[ 2 ] ) },
	{ "VertexWaterHeightClampLow3",		INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterHeightClampLow[ 2 ] ) },
	{ "VertexWaterHeightClampHi3",		INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterHeightClampHi[ 2 ] ) },
	{ "VertexWaterAngle3",						INI::parseAngleReal,		NULL,		offsetof( GlobalData, m_vertexWaterAngle[ 2 ] ) },
	{ "VertexWaterXPosition3",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterXPosition[ 2 ] ) },
	{ "VertexWaterYPosition3",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterYPosition[ 2 ] ) },
	{ "VertexWaterZPosition3",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterZPosition[ 2 ] ) },
	{ "VertexWaterXGridCells3",				INI::parseInt,					NULL,		offsetof( GlobalData, m_vertexWaterXGridCells[ 2 ] ) },
	{ "VertexWaterYGridCells3",				INI::parseInt,					NULL,		offsetof( GlobalData, m_vertexWaterYGridCells[ 2 ] ) },
	{ "VertexWaterGridSize3",					INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterGridSize[ 2 ] ) },
	{ "VertexWaterAttenuationA3",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationA[ 2 ] ) },
	{ "VertexWaterAttenuationB3",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationB[ 2 ] ) },
	{ "VertexWaterAttenuationC3",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationC[ 2 ] ) },
	{ "VertexWaterAttenuationRange3",	INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationRange[ 2 ] ) },

	// nasty ick, we need to save this data with a map and not hard code INI values
	{ "VertexWaterAvailableMaps4",		INI::parseAsciiString,	NULL,		offsetof( GlobalData, m_vertexWaterAvailableMaps[ 3 ] ) },
	{ "VertexWaterHeightClampLow4",		INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterHeightClampLow[ 3 ] ) },
	{ "VertexWaterHeightClampHi4",		INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterHeightClampHi[ 3 ] ) },
	{ "VertexWaterAngle4",						INI::parseAngleReal,		NULL,		offsetof( GlobalData, m_vertexWaterAngle[ 3 ] ) },
	{ "VertexWaterXPosition4",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterXPosition[ 3 ] ) },
	{ "VertexWaterYPosition4",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterYPosition[ 3 ] ) },
	{ "VertexWaterZPosition4",				INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterZPosition[ 3 ] ) },
	{ "VertexWaterXGridCells4",				INI::parseInt,					NULL,		offsetof( GlobalData, m_vertexWaterXGridCells[ 3 ] ) },
	{ "VertexWaterYGridCells4",				INI::parseInt,					NULL,		offsetof( GlobalData, m_vertexWaterYGridCells[ 3 ] ) },
	{ "VertexWaterGridSize4",					INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterGridSize[ 3 ] ) },
	{ "VertexWaterAttenuationA4",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationA[ 3 ] ) },
	{ "VertexWaterAttenuationB4",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationB[ 3 ] ) },
	{ "VertexWaterAttenuationC4",			INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationC[ 3 ] ) },
	{ "VertexWaterAttenuationRange4",	INI::parseReal,					NULL,		offsetof( GlobalData, m_vertexWaterAttenuationRange[ 3 ] ) },

	{ "SkyBoxPositionZ",				INI::parseReal,				NULL,			offsetof( GlobalData, m_skyBoxPositionZ ) },
	{ "SkyBoxScale",				INI::parseReal,				NULL,			offsetof( GlobalData, m_skyBoxScale ) },
	{ "DrawSkyBox",				INI::parseBool,				NULL,			offsetof( GlobalData, m_drawSkyBox ) },
	{ "CameraPitch",								INI::parseReal,				NULL,			offsetof( GlobalData, m_cameraPitch ) },
	{ "CameraYaw",									INI::parseReal,				NULL,			offsetof( GlobalData, m_cameraYaw ) },
	{ "CameraHeight",								INI::parseReal,				NULL,			offsetof( GlobalData, m_cameraHeight ) },
	{ "MaxCameraHeight",						INI::parseReal,				NULL,			offsetof( GlobalData, m_maxCameraHeight ) },
	{ "UseCameraConstraints",				INI::parseBool,				NULL,			offsetof( GlobalData, m_useCameraConstraints ) },
	{ "CameraBoundaryMargin",				INI::parseInt,				NULL,			offsetof( GlobalData, m_cameraBoundaryMargin ) },
	{ "EdgeScrollInWindowedMode",	INI::parseBool,				NULL,			offsetof( GlobalData, m_edgeScrollInWindowedMode ) },
	{ "SnapBuildPlacementTo45",		INI::parseBool,				NULL,			offsetof( GlobalData, m_snapBuildPlacementTo45 ) },
	{ "SnapCameraRotateTo45",			INI::parseBool,				NULL,			offsetof( GlobalData, m_snapCameraRotateTo45 ) },
	{ "GridBuildPlacement",				INI::parseBool,				NULL,			offsetof( GlobalData, m_gridBuildPlacement ) },
	{ "NudgeBuildPlacement",			INI::parseBool,				NULL,			offsetof( GlobalData, m_nudgeBuildPlacement ) },
	{ "MoneyPerMinute",						INI::parseInt,				NULL,			offsetof( GlobalData, m_moneyPerMinute ) },
	{ "BuildPlacementOpacity",		INI::parseReal,				NULL,			offsetof( GlobalData, m_buildPlacementOpacity ) },
	{ "BuildPlacementShadows",		INI::parseBool,				NULL,			offsetof( GlobalData, m_buildPlacementShadows ) },
	{ "ZoomToCursor",							INI::parseBool,				NULL,			offsetof( GlobalData, m_zoomToCursor ) },
	{ "IsometricCamera",					INI::parseBool,				NULL,			offsetof( GlobalData, m_isometricCamera ) },
	{ "FormationDrag",						INI::parseBool,				NULL,			offsetof( GlobalData, m_formationDrag ) },
	{ "ShowAllyCursors",					INI::parseBool,				NULL,			offsetof( GlobalData, m_showAllyCursors ) },
	{ "ChromaLighting",						INI::parseBool,				NULL,			offsetof( GlobalData, m_chromaLighting ) },
	{ "ShowHudOverlay",						INI::parseBool,				NULL,			offsetof( GlobalData, m_showHudOverlay ) },
	{ "ShowPlacementRangeRing",		INI::parseBool,				NULL,			offsetof( GlobalData, m_showPlacementRangeRing ) },
	{ "WorkersReturnToSupply",		INI::parseBool,				NULL,			offsetof( GlobalData, m_workersReturnToSupply ) },
	{ "DetailedBuildTooltips",		INI::parseBool,				NULL,			offsetof( GlobalData, m_detailedBuildTooltips ) },
	{ "ArchiveReplays",						INI::parseBool,				NULL,			offsetof( GlobalData, m_archiveReplays ) },
	{ "Bloom",										INI::parseInt,				NULL,			offsetof( GlobalData, m_bloomIntensity ) },
	{ "BloomThreshold",						INI::parseInt,				NULL,			offsetof( GlobalData, m_bloomThreshold ) },
	{ "MinCameraHeight",						INI::parseReal,				NULL,			offsetof( GlobalData, m_minCameraHeight ) },
	{ "TerrainHeightAtEdgeOfMap",					INI::parseReal,				NULL,			offsetof( GlobalData, m_terrainHeightAtEdgeOfMap ) },
	{ "UnitDamagedThreshold",				INI::parseReal,				NULL,			offsetof( GlobalData, m_unitDamagedThresh ) },
	{ "UnitReallyDamagedThreshold",	INI::parseReal,				NULL,			offsetof( GlobalData, m_unitReallyDamagedThresh ) },
	{ "GroundStiffness",					INI::parseReal,				NULL,				offsetof( GlobalData, m_groundStiffness ) },
	{ "StructureStiffness",					INI::parseReal,				NULL,				offsetof( GlobalData, m_structureStiffness ) },
	{ "Gravity",									INI::parseAccelerationReal,				NULL,				offsetof( GlobalData, m_gravity ) },
	{ "StealthFriendlyOpacity",		INI::parsePercentToReal,				NULL,				offsetof( GlobalData, m_stealthFriendlyOpacity ) },
	{ "DefaultOcclusionDelay",				INI::parseDurationUnsignedInt,				NULL,			offsetof( GlobalData, m_defaultOcclusionDelay ) },
	
	{ "PartitionCellSize",				INI::parseReal,				NULL,			offsetof( GlobalData, m_partitionCellSize ) },

	{ "AmmoPipScaleFactor",				INI::parseReal,				NULL,			offsetof( GlobalData, m_ammoPipScaleFactor ) },
	{ "ContainerPipScaleFactor",	INI::parseReal,				NULL,			offsetof( GlobalData, m_containerPipScaleFactor ) },
	{ "AmmoPipWorldOffset",						INI::parseCoord3D,				NULL,			offsetof( GlobalData, m_ammoPipWorldOffset ) },
	{ "ContainerPipWorldOffset",				INI::parseCoord3D,				NULL,			offsetof( GlobalData, m_containerPipWorldOffset ) },
	{ "AmmoPipScreenOffset",						INI::parseCoord2D,				NULL,			offsetof( GlobalData, m_ammoPipScreenOffset ) },
	{ "ContainerPipScreenOffset",				INI::parseCoord2D,				NULL,			offsetof( GlobalData, m_containerPipScreenOffset ) },

	{ "HistoricDamageLimit",				INI::parseDurationUnsignedInt,				NULL,			offsetof( GlobalData, m_historicDamageLimit ) },

	{ "MaxTerrainTracks",					INI::parseInt,				NULL,			offsetof( GlobalData, m_maxTerrainTracks ) },
	{ "TimeOfDay",								INI::parseIndexList,	TimeOfDayNames,			offsetof( GlobalData, m_timeOfDay ) },
	{ "Weather",									INI::parseIndexList,	WeatherNames,			offsetof( GlobalData, m_weather ) },
	{ "MakeTrackMarks",						INI::parseBool,				NULL,			offsetof( GlobalData, m_makeTrackMarks ) },
	{ "HideGarrisonFlags",						INI::parseBool,				NULL,			offsetof( GlobalData, m_hideGarrisonFlags ) },
	{ "ForceModelsToFollowTimeOfDay",						INI::parseBool,				NULL,			offsetof( GlobalData, m_forceModelsToFollowTimeOfDay ) },
	{ "ForceModelsToFollowWeather",						INI::parseBool,				NULL,			offsetof( GlobalData, m_forceModelsToFollowWeather ) },

	{ "LevelGainAnimationName",		INI::parseAsciiString,	NULL,	offsetof( GlobalData, m_levelGainAnimationName ) },
	{ "LevelGainAnimationTime",		INI::parseReal,					NULL, offsetof( GlobalData, m_levelGainAnimationDisplayTimeInSeconds ) },
	{ "LevelGainAnimationZRise",	INI::parseReal,					NULL, offsetof( GlobalData, m_levelGainAnimationZRisePerSecond ) },

	{ "GetHealedAnimationName",		INI::parseAsciiString,	NULL,	offsetof( GlobalData, m_getHealedAnimationName ) },
	{ "GetHealedAnimationTime",		INI::parseReal,					NULL, offsetof( GlobalData, m_getHealedAnimationDisplayTimeInSeconds ) },
	{ "GetHealedAnimationZRise",	INI::parseReal,					NULL, offsetof( GlobalData, m_getHealedAnimationZRisePerSecond ) },

	{ "TerrainLightingMorningAmbient",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][0].ambient ) },
	{ "TerrainLightingMorningDiffuse",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][0].diffuse ) },
	{ "TerrainLightingMorningLightPos",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][0].lightPos ) },
	{ "TerrainLightingAfternoonAmbient",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][0].ambient ) },
	{ "TerrainLightingAfternoonDiffuse",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][0].diffuse ) },
	{ "TerrainLightingAfternoonLightPos",	INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][0].lightPos ) },
	{ "TerrainLightingEveningAmbient",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][0].ambient ) },
	{ "TerrainLightingEveningDiffuse",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][0].diffuse ) },
	{ "TerrainLightingEveningLightPos",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][0].lightPos ) },
	{ "TerrainLightingNightAmbient",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][0].ambient ) },
	{ "TerrainLightingNightDiffuse",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][0].diffuse ) },
	{ "TerrainLightingNightLightPos",			INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][0].lightPos ) },

	{ "TerrainObjectsLightingMorningAmbient",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][0].ambient ) },
	{ "TerrainObjectsLightingMorningDiffuse",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][0].diffuse ) },
	{ "TerrainObjectsLightingMorningLightPos",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][0].lightPos ) },
	{ "TerrainObjectsLightingAfternoonAmbient",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][0].ambient ) },
	{ "TerrainObjectsLightingAfternoonDiffuse",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][0].diffuse ) },
	{ "TerrainObjectsLightingAfternoonLightPos",	INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][0].lightPos ) },
	{ "TerrainObjectsLightingEveningAmbient",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][0].ambient ) },
	{ "TerrainObjectsLightingEveningDiffuse",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][0].diffuse ) },
	{ "TerrainObjectsLightingEveningLightPos",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][0].lightPos ) },
	{ "TerrainObjectsLightingNightAmbient",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][0].ambient ) },
	{ "TerrainObjectsLightingNightDiffuse",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][0].diffuse ) },
	{ "TerrainObjectsLightingNightLightPos",			INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][0].lightPos ) },

	//Secondary global light	
	{ "TerrainLightingMorningAmbient2",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][1].ambient ) },
	{ "TerrainLightingMorningDiffuse2",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][1].diffuse ) },
	{ "TerrainLightingMorningLightPos2",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][1].lightPos ) },
	{ "TerrainLightingAfternoonAmbient2",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][1].ambient ) },
	{ "TerrainLightingAfternoonDiffuse2",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][1].diffuse ) },
	{ "TerrainLightingAfternoonLightPos2",	INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][1].lightPos ) },
	{ "TerrainLightingEveningAmbient2",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][1].ambient ) },
	{ "TerrainLightingEveningDiffuse2",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][1].diffuse ) },
	{ "TerrainLightingEveningLightPos2",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][1].lightPos ) },
	{ "TerrainLightingNightAmbient2",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][1].ambient ) },
	{ "TerrainLightingNightDiffuse2",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][1].diffuse ) },
	{ "TerrainLightingNightLightPos2",			INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][1].lightPos ) },

	{ "TerrainObjectsLightingMorningAmbient2",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][1].ambient ) },
	{ "TerrainObjectsLightingMorningDiffuse2",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][1].diffuse ) },
	{ "TerrainObjectsLightingMorningLightPos2",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][1].lightPos ) },
	{ "TerrainObjectsLightingAfternoonAmbient2",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][1].ambient ) },
	{ "TerrainObjectsLightingAfternoonDiffuse2",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][1].diffuse ) },
	{ "TerrainObjectsLightingAfternoonLightPos2",	INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][1].lightPos ) },
	{ "TerrainObjectsLightingEveningAmbient2",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][1].ambient ) },
	{ "TerrainObjectsLightingEveningDiffuse2",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][1].diffuse ) },
	{ "TerrainObjectsLightingEveningLightPos2",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][1].lightPos ) },
	{ "TerrainObjectsLightingNightAmbient2",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][1].ambient ) },
	{ "TerrainObjectsLightingNightDiffuse2",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][1].diffuse ) },
	{ "TerrainObjectsLightingNightLightPos2",			INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][1].lightPos ) },

	//Third global light
	{ "TerrainLightingMorningAmbient3",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][2].ambient ) },
	{ "TerrainLightingMorningDiffuse3",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][2].diffuse ) },
	{ "TerrainLightingMorningLightPos3",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_MORNING ][2].lightPos ) },
	{ "TerrainLightingAfternoonAmbient3",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][2].ambient ) },
	{ "TerrainLightingAfternoonDiffuse3",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][2].diffuse ) },
	{ "TerrainLightingAfternoonLightPos3",	INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_AFTERNOON ][2].lightPos ) },
	{ "TerrainLightingEveningAmbient3",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][2].ambient ) },
	{ "TerrainLightingEveningDiffuse3",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][2].diffuse ) },
	{ "TerrainLightingEveningLightPos3",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_EVENING ][2].lightPos ) },
	{ "TerrainLightingNightAmbient3",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][2].ambient ) },
	{ "TerrainLightingNightDiffuse3",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][2].diffuse ) },
	{ "TerrainLightingNightLightPos3",			INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainLighting[ TIME_OF_DAY_NIGHT ][2].lightPos ) },

	{ "TerrainObjectsLightingMorningAmbient3",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][2].ambient ) },
	{ "TerrainObjectsLightingMorningDiffuse3",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][2].diffuse ) },
	{ "TerrainObjectsLightingMorningLightPos3",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_MORNING ][2].lightPos ) },
	{ "TerrainObjectsLightingAfternoonAmbient3",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][2].ambient ) },
	{ "TerrainObjectsLightingAfternoonDiffuse3",		INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][2].diffuse ) },
	{ "TerrainObjectsLightingAfternoonLightPos3",	INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_AFTERNOON ][2].lightPos ) },
	{ "TerrainObjectsLightingEveningAmbient3",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][2].ambient ) },
	{ "TerrainObjectsLightingEveningDiffuse3",			INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][2].diffuse ) },
	{ "TerrainObjectsLightingEveningLightPos3",		INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_EVENING ][2].lightPos ) },
	{ "TerrainObjectsLightingNightAmbient3",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][2].ambient ) },
	{ "TerrainObjectsLightingNightDiffuse3",				INI::parseRGBColor,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][2].diffuse ) },
	{ "TerrainObjectsLightingNightLightPos3",			INI::parseCoord3D,			NULL,			offsetof( GlobalData, m_terrainObjectsLighting[ TIME_OF_DAY_NIGHT ][2].lightPos ) },

	
	{ "NumberGlobalLights",								INI::parseInt,				NULL,			offsetof( GlobalData, m_numGlobalLights)},
	{ "InfantryLightMorningScale",				INI::parseReal,			NULL,			offsetof( GlobalData, m_infantryLightScale[TIME_OF_DAY_MORNING] ) },
	{ "InfantryLightAfternoonScale",				INI::parseReal,			NULL,			offsetof( GlobalData, m_infantryLightScale[TIME_OF_DAY_AFTERNOON] ) },
	{ "InfantryLightEveningScale",				INI::parseReal,			NULL,			offsetof( GlobalData, m_infantryLightScale[TIME_OF_DAY_EVENING] ) },
	{ "InfantryLightNightScale",				INI::parseReal,			NULL,			offsetof( GlobalData, m_infantryLightScale[TIME_OF_DAY_NIGHT] ) },

	{ "MaxTranslucentObjects",						INI::parseInt,				NULL,			offsetof( GlobalData, m_maxVisibleTranslucentObjects) },
	{ "OccludedColorLuminanceScale",				INI::parseReal,				NULL,			offsetof( GlobalData, m_occludedLuminanceScale) },

/* These are internal use only, they do not need file definitons 
	{ "TerrainAmbientRGB",				INI::parseRGBColor,		NULL,			offsetof( GlobalData, m_terrainAmbient ) },
	{ "TerrainDiffuseRGB",				INI::parseRGBColor,		NULL,			offsetof( GlobalData, m_terrainDiffuse ) },
	{ "TerrainLightPos",					INI::parseCoord3D,		NULL,			offsetof( GlobalData, m_terrainLightPos ) },
*/
	{ "MaxRoadSegments",						INI::parseInt,				NULL,			offsetof( GlobalData, m_maxRoadSegments ) },
	{ "MaxRoadVertex",							INI::parseInt,				NULL,			offsetof( GlobalData, m_maxRoadVertex ) },
	{ "MaxRoadIndex",								INI::parseInt,				NULL,			offsetof( GlobalData, m_maxRoadIndex ) },
	{ "MaxRoadTypes",								INI::parseInt,				NULL,			offsetof( GlobalData, m_maxRoadTypes ) },

	{ "ValuePerSupplyBox",					INI::parseInt,				NULL,			offsetof( GlobalData, m_baseValuePerSupplyBox ) },
	
	{ "AudioOn",										INI::parseBool,				NULL,			offsetof( GlobalData, m_audioOn ) },
	{ "MusicOn",										INI::parseBool,				NULL,			offsetof( GlobalData, m_musicOn ) },
	{ "SoundsOn",										INI::parseBool,				NULL,			offsetof( GlobalData, m_soundsOn ) },
	{ "Sounds3DOn",									INI::parseBool,				NULL,			offsetof( GlobalData, m_sounds3DOn ) },
	{ "SpeechOn",										INI::parseBool,				NULL,			offsetof( GlobalData, m_speechOn ) },
	{ "VideoOn",										INI::parseBool,				NULL,			offsetof( GlobalData, m_videoOn ) },
	{ "DisableCameraMovements",			INI::parseBool,				NULL,			offsetof( GlobalData, m_disableCameraMovement ) },

/* These are internal use only, they do not need file definitons 		
	/// @todo remove this hack
	{ "InGame",											INI::parseBool,				NULL,			offsetof( GlobalData, m_inGame ) },
*/

	{ "DebugAI",										INI::parseBool,				NULL,			offsetof( GlobalData, m_debugAI ) },
	{ "DebugAIObstacles",						INI::parseBool,				NULL,			offsetof( GlobalData, m_debugAIObstacles ) },
	{ "ShowClientPhysics",				INI::parseBool,				NULL,			offsetof( GlobalData, m_showClientPhysics ) },
	{ "ShowTerrainNormals",				INI::parseBool,				NULL,			offsetof( GlobalData, m_showTerrainNormals ) },
	{ "ShowObjectHealth",						INI::parseBool,				NULL,			offsetof( GlobalData, m_showObjectHealth ) },
	{ "HealthBars",									INI::parseInt,				NULL,			offsetof( GlobalData, m_healthBarMode ) },
	{ "PlayerColors",								INI::parseInt,				NULL,			offsetof( GlobalData, m_playerColorScheme ) },

	{ "ParticleScale",										INI::parseReal,					NULL,	 offsetof( GlobalData, m_particleScale ) },
	{ "AutoFireParticleSmallPrefix",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoFireParticleSmallPrefix ) },
	{ "AutoFireParticleSmallSystem",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoFireParticleSmallSystem ) },
	{ "AutoFireParticleSmallMax",					INI::parseInt,					NULL,	 offsetof( GlobalData, m_autoFireParticleSmallMax ) },
	{ "AutoFireParticleMediumPrefix",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoFireParticleMediumPrefix ) },
	{ "AutoFireParticleMediumSystem",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoFireParticleMediumSystem ) },
	{ "AutoFireParticleMediumMax",				INI::parseInt,					NULL,	 offsetof( GlobalData, m_autoFireParticleMediumMax ) },
	{ "AutoFireParticleLargePrefix",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoFireParticleLargePrefix ) },
	{ "AutoFireParticleLargeSystem",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoFireParticleLargeSystem ) },
	{ "AutoFireParticleLargeMax",					INI::parseInt,					NULL,	 offsetof( GlobalData, m_autoFireParticleLargeMax ) },
	{ "AutoSmokeParticleSmallPrefix",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoSmokeParticleSmallPrefix ) },
	{ "AutoSmokeParticleSmallSystem",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoSmokeParticleSmallSystem ) },
	{ "AutoSmokeParticleSmallMax",				INI::parseInt,					NULL,	 offsetof( GlobalData, m_autoSmokeParticleSmallMax ) },
	{ "AutoSmokeParticleMediumPrefix",		INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoSmokeParticleMediumPrefix ) },
	{ "AutoSmokeParticleMediumSystem",		INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoSmokeParticleMediumSystem ) },
	{ "AutoSmokeParticleMediumMax",				INI::parseInt,					NULL,	 offsetof( GlobalData, m_autoSmokeParticleMediumMax ) },
	{ "AutoSmokeParticleLargePrefix",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoSmokeParticleLargePrefix ) },
	{ "AutoSmokeParticleLargeSystem",			INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoSmokeParticleLargeSystem ) },
	{ "AutoSmokeParticleLargeMax",				INI::parseInt,					NULL,	 offsetof( GlobalData, m_autoSmokeParticleLargeMax ) },
	{ "AutoAflameParticlePrefix",					INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoAflameParticlePrefix ) },
	{ "AutoAflameParticleSystem",					INI::parseAsciiString,  NULL,  offsetof( GlobalData, m_autoAflameParticleSystem ) },
	{ "AutoAflameParticleMax",						INI::parseInt,					NULL,	 offsetof( GlobalData, m_autoAflameParticleMax ) },

	// Synthetic link conditions, for reproducing a slow or lossy line on one machine.  EA had these
	// commented out as "internal use only"; they are no more dangerous than the command-line options
	// that set the same fields, and an INI is the only way to set them for a run the launcher starts.
	{ "LatencyAverage",							INI::parseInt,				NULL,			offsetof( GlobalData, m_latencyAverage ) },
	{ "LatencyAmplitude",						INI::parseInt,				NULL,			offsetof( GlobalData, m_latencyAmplitude ) },
	{ "LatencyPeriod",							INI::parseInt,				NULL,			offsetof( GlobalData, m_latencyPeriod ) },
	{ "LatencyNoise",								INI::parseInt,				NULL,			offsetof( GlobalData, m_latencyNoise ) },
	{ "PacketLoss",									INI::parseInt,				NULL,			offsetof( GlobalData, m_packetLoss ) },

	{ "BuildSpeed",									INI::parseReal,				NULL,			offsetof( GlobalData, m_BuildSpeed ) },
	{ "MinDistFromEdgeOfMapForBuild",	 INI::parseReal,				NULL,			offsetof( GlobalData, m_MinDistFromEdgeOfMapForBuild ) },
	{ "SupplyBuildBorder",	 INI::parseReal,				NULL,			offsetof( GlobalData, m_SupplyBuildBorder ) },
	{ "AllowedHeightVariationForBuilding", INI::parseReal,NULL,			offsetof( GlobalData, m_allowedHeightVariationForBuilding ) },
	{ "MinLowEnergyProductionSpeed",INI::parseReal,				NULL,			offsetof( GlobalData, m_MinLowEnergyProductionSpeed ) },
	{ "MaxLowEnergyProductionSpeed",INI::parseReal,				NULL,			offsetof( GlobalData, m_MaxLowEnergyProductionSpeed ) },
	{ "LowEnergyPenaltyModifier",		INI::parseReal,				NULL,			offsetof( GlobalData, m_LowEnergyPenaltyModifier ) },
	{ "MultipleFactory",						INI::parseReal,				NULL,			offsetof( GlobalData, m_MultipleFactory ) },
	{ "RefundPercent",							INI::parsePercentToReal,			NULL,			offsetof( GlobalData, m_RefundPercent ) },

	{ "CommandCenterHealRange",			INI::parseReal,				NULL,			offsetof( GlobalData, m_commandCenterHealRange ) },
	{ "CommandCenterHealAmount",		INI::parseReal,				NULL,			offsetof( GlobalData, m_commandCenterHealAmount ) },

	{ "StandardMinefieldDensity",		INI::parseReal,				NULL,			offsetof( GlobalData, m_standardMinefieldDensity ) },
	{ "StandardMinefieldDistance",		INI::parseReal,				NULL,			offsetof( GlobalData, m_standardMinefieldDistance ) },

	{ "MaxLineBuildObjects",				INI::parseInt,				NULL,			offsetof( GlobalData, m_maxLineBuildObjects ) },
	{ "MaxTunnelCapacity",					INI::parseInt,				NULL,			offsetof( GlobalData, m_maxTunnelCapacity ) },

	{ "MaxParticleCount",						INI::parseInt,				NULL,			offsetof( GlobalData, m_maxParticleCount ) },
	{ "MaxFieldParticleCount",						INI::parseInt,				NULL,			offsetof( GlobalData, m_maxFieldParticleCount ) },
	{ "HorizontalScrollSpeedFactor",INI::parseReal,				NULL,			offsetof( GlobalData, m_horizontalScrollSpeedFactor ) },
	{ "VerticalScrollSpeedFactor",	INI::parseReal,				NULL,			offsetof( GlobalData, m_verticalScrollSpeedFactor ) },
	{ "ScrollAmountCutoff",					INI::parseReal,				NULL,			offsetof( GlobalData, m_scrollAmountCutoff ) },
	{ "CameraAdjustSpeed",					INI::parseReal,				NULL,			offsetof( GlobalData, m_cameraAdjustSpeed ) },
	{ "EnforceMaxCameraHeight",			INI::parseBool,				NULL,			offsetof( GlobalData, m_enforceMaxCameraHeight ) },
	{ "KeyboardScrollSpeedFactor",	INI::parseReal,				NULL,			offsetof( GlobalData, m_keyboardScrollFactor ) },
	{ "KeyboardDefaultScrollSpeedFactor",	INI::parseReal,				NULL,			offsetof( GlobalData, m_keyboardDefaultScrollFactor ) },
	{ "MovementPenaltyDamageState",	INI::parseIndexList,	TheBodyDamageTypeNames,	 offsetof( GlobalData, m_movementPenaltyDamageState ) },

// you cannot set this; it always has a value of 100%.
//{ "HealthBonus_Regular",				INI::parsePercentToReal, NULL,	offsetof( GlobalData, m_healthBonus[LEVEL_REGULAR]) },
	{ "HealthBonus_Veteran",				INI::parsePercentToReal, NULL,	offsetof( GlobalData, m_healthBonus[LEVEL_VETERAN]) },
	{ "HealthBonus_Elite",					INI::parsePercentToReal, NULL,	offsetof( GlobalData, m_healthBonus[LEVEL_ELITE]) },
	{ "HealthBonus_Heroic",					INI::parsePercentToReal, NULL,	offsetof( GlobalData, m_healthBonus[LEVEL_HEROIC]) },

	{ "HumanSoloPlayerHealthBonus_Easy",					INI::parsePercentToReal,			NULL,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_HUMAN][DIFFICULTY_EASY] ) },
	{ "HumanSoloPlayerHealthBonus_Normal",				INI::parsePercentToReal,			NULL,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_HUMAN][DIFFICULTY_NORMAL] ) },
	{ "HumanSoloPlayerHealthBonus_Hard",				INI::parsePercentToReal,			NULL,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_HUMAN][DIFFICULTY_HARD] ) },

	{ "AISoloPlayerHealthBonus_Easy",					INI::parsePercentToReal,			NULL,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_COMPUTER][DIFFICULTY_EASY] ) },
	{ "AISoloPlayerHealthBonus_Normal",				INI::parsePercentToReal,			NULL,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_COMPUTER][DIFFICULTY_NORMAL] ) },
	{ "AISoloPlayerHealthBonus_Hard",				INI::parsePercentToReal,			NULL,			offsetof( GlobalData, m_soloPlayerHealthBonusForDifficulty[PLAYER_COMPUTER][DIFFICULTY_HARD] ) },

	{ "WeaponBonus",								WeaponBonusSet::parseWeaponBonusSetPtr,	NULL,	offsetof( GlobalData, m_weaponBonusSet ) },

	{ "DefaultStructureRubbleHeight",	INI::parseReal,			NULL,			offsetof( GlobalData, m_defaultStructureRubbleHeight ) },

	{ "FixedSeed",									INI::parseInt,				NULL,			offsetof( GlobalData, m_fixedSeed ) },

	{ "ShellMapName",								INI::parseAsciiString,NULL,			offsetof( GlobalData, m_shellMapName ) },
	{ "ShellMapOn",									INI::parseBool,				NULL,			offsetof( GlobalData, m_shellMapOn ) },
	{	"PlayIntro",									INI::parseBool,				NULL,			offsetof( GlobalData, m_playIntro ) },

	{ "FirewallBehavior",						INI::parseInt,				NULL,			offsetof( GlobalData, m_firewallBehavior ) },
	{ "FirewallPortOverride",				INI::parseInt,				NULL,			offsetof( GlobalData, m_firewallPortOverride ) },
	{	"FirewallPortAllocationDelta",INI::parseInt,				NULL,			offsetof( GlobalData, m_firewallPortAllocationDelta) },

	{	"GroupSelectMinSelectSize",		INI::parseInt,				NULL,			offsetof( GlobalData, m_groupSelectMinSelectSize ) },
	{	"GroupSelectVolumeBase",			INI::parseReal,				NULL,			offsetof( GlobalData, m_groupSelectVolumeBase ) },
	{	"GroupSelectVolumeIncrement",	INI::parseReal,				NULL,			offsetof( GlobalData, m_groupSelectVolumeIncrement ) },
	{	"MaxUnitSelectSounds",				INI::parseInt,				NULL,			offsetof( GlobalData, m_maxUnitSelectSounds ) },

	{	"SelectionFlashSaturationFactor",	INI::parseReal,		NULL,			offsetof( GlobalData, m_selectionFlashSaturationFactor ) },
	{	"SelectionFlashHouseColor",	      INI::parseBool,		NULL,			offsetof( GlobalData, m_selectionFlashHouseColor ) },

	{	"CameraAudibleRadius",				INI::parseReal,				NULL,			offsetof( GlobalData, m_cameraAudibleRadius ) },
	{ "GroupMoveClickToGatherAreaFactor", INI::parseReal,	NULL,			offsetof( GlobalData, m_groupMoveClickToGatherFactor ) },
	{ "ShakeSubtleIntensity",				INI::parseReal,				NULL,			offsetof( GlobalData, m_shakeSubtleIntensity ) },
	{ "ShakeNormalIntensity",				INI::parseReal,				NULL,			offsetof( GlobalData, m_shakeNormalIntensity ) },
	{ "ShakeStrongIntensity",				INI::parseReal,				NULL,			offsetof( GlobalData, m_shakeStrongIntensity ) },
	{ "ShakeSevereIntensity",				INI::parseReal,				NULL,			offsetof( GlobalData, m_shakeSevereIntensity ) },
	{ "ShakeCineExtremeIntensity",	INI::parseReal,				NULL,			offsetof( GlobalData, m_shakeCineExtremeIntensity ) },
	{ "ShakeCineInsaneIntensity",		INI::parseReal,				NULL,			offsetof( GlobalData, m_shakeCineInsaneIntensity ) },
	{ "MaxShakeIntensity",					INI::parseReal,				NULL,			offsetof( GlobalData, m_maxShakeIntensity ) },
	{ "MaxShakeRange",							INI::parseReal,				NULL,			offsetof( GlobalData, m_maxShakeRange) },
	{ "SellPercentage",							INI::parsePercentToReal,	NULL,			offsetof( GlobalData, m_sellPercentage ) },
	{ "BaseRegenHealthPercentPerSecond", INI::parsePercentToReal, NULL,	offsetof( GlobalData, m_baseRegenHealthPercentPerSecond ) },
	{ "BaseRegenDelay",							INI::parseDurationUnsignedInt, NULL,offsetof( GlobalData, m_baseRegenDelay ) },

#ifdef ALLOW_SURRENDER
	{ "PrisonBountyMultiplier",			INI::parseReal,				NULL,			offsetof( GlobalData, m_prisonBountyMultiplier ) },
	{ "PrisonBountyTextColor",			INI::parseColorInt,		NULL,			offsetof( GlobalData, m_prisonBountyTextColor ) },
#endif

	{ "SpecialPowerViewObject",			INI::parseAsciiString,	NULL,			offsetof( GlobalData, m_specialPowerViewObjectName ) },

	{ "StandardPublicBone", INI::parseAsciiStringVectorAppend, NULL, offsetof(GlobalData, m_standardPublicBones) },
	{ "ShowMetrics",								INI::parseBool,				   NULL,		offsetof( GlobalData, m_showMetrics ) },
  { "DefaultStartingCash",				Money::parseMoneyAmount, NULL,		offsetof( GlobalData, m_defaultStartingCash ) },
	{ "PeaceTimeBaseRadius",				INI::parseReal, NULL,							offsetof( GlobalData, m_peaceTimeBaseRadius ) },
	{ "PeaceTimeBaseDamage",				INI::parseReal, NULL,							offsetof( GlobalData, m_peaceTimeBaseDamage ) },

// NOTE: m_doubleClickTimeMS is still in use, but we disallow setting it from the GameData.ini file. It is now set in the constructor according to the windows parameter.
//	{ "DoubleClickTimeMS",									INI::parseUnsignedInt,			NULL, offsetof( GlobalData, m_doubleClickTimeMS ) },
	
	{ "ShroudColor",		INI::parseRGBColor,						NULL,	offsetof( GlobalData, m_shroudColor) },
	{ "ClearAlpha",			INI::parseUnsignedByte,				NULL,	offsetof( GlobalData, m_clearAlpha) },
	{ "FogAlpha",				INI::parseUnsignedByte,				NULL,	offsetof( GlobalData, m_fogAlpha) },
	{ "ShroudAlpha",		INI::parseUnsignedByte,				NULL,	offsetof( GlobalData, m_shroudAlpha) },

	{ "HotKeyTextColor",										INI::parseColorInt,					NULL,	offsetof( GlobalData, m_hotKeyTextColor ) },

	{ "PowerBarBase",												INI::parseInt,							NULL,	offsetof( GlobalData, m_powerBarBase) },
	{ "PowerBarIntervals",									INI::parseReal,							NULL,	offsetof( GlobalData, m_powerBarIntervals) },
	{ "PowerBarYellowRange",								INI::parseInt,							NULL,	offsetof( GlobalData, m_powerBarYellowRange) },
	{ "UnlookPersistDuration",							INI::parseDurationUnsignedInt, NULL, offsetof( GlobalData, m_unlookPersistDuration) },

	{ "NetworkFPSHistoryLength", INI::parseInt, NULL, offsetof(GlobalData, m_networkFPSHistoryLength) },
	{ "NetworkLatencyHistoryLength", INI::parseInt, NULL, offsetof(GlobalData, m_networkLatencyHistoryLength) },
	{ "NetworkRunAheadMetricsTime", INI::parseInt, NULL, offsetof(GlobalData, m_networkRunAheadMetricsTime) },
	{ "NetworkCushionHistoryLength", INI::parseInt, NULL, offsetof(GlobalData, m_networkCushionHistoryLength) },
	{ "NetworkRunAheadSlack", INI::parseInt, NULL, offsetof(GlobalData, m_networkRunAheadSlack) },
	{ "NetworkKeepAliveDelay", INI::parseInt, NULL, offsetof(GlobalData, m_networkKeepAliveDelay) },
	{ "NetworkDisconnectTime", INI::parseInt, NULL, offsetof(GlobalData, m_networkDisconnectTime) },
	{ "NetworkPlayerSilenceTime", INI::parseInt, NULL, offsetof(GlobalData, m_networkPlayerSilenceTime) },
	{ "NetworkStallCeilingTime", INI::parseInt, NULL, offsetof(GlobalData, m_networkStallCeilingTime) },
	{ "NetworkPlayerTimeoutTime", INI::parseInt, NULL, offsetof(GlobalData, m_networkPlayerTimeoutTime) },
	{ "NetworkDisconnectScreenNotifyTime", INI::parseInt, NULL, offsetof(GlobalData, m_networkDisconnectScreenNotifyTime) },
	
	{ "KeyboardCameraRotateSpeed", INI::parseReal, NULL, offsetof( GlobalData, m_keyboardCameraRotateSpeed ) },
	{ "PlayStats",									INI::parseInt,				NULL,			offsetof( GlobalData, m_playStats ) },

#if defined(_DEBUG) || defined(_INTERNAL)
	{ "DisableCameraFade",			INI::parseBool,				NULL,			offsetof( GlobalData, m_disableCameraFade ) },
	{ "DisableScriptedInputDisabling",			INI::parseBool,		NULL,			offsetof( GlobalData, m_disableScriptedInputDisabling ) },
	{ "DisableMilitaryCaption",			INI::parseBool,				NULL,			offsetof( GlobalData, m_disableMilitaryCaption ) },
	{ "BenchmarkTimer",			INI::parseInt,				NULL,			offsetof( GlobalData, m_benchmarkTimer ) },
	{ "CheckMemoryLeaks", INI::parseBool, NULL, offsetof(GlobalData, m_checkForLeaks) },
	{ "Wireframe",								INI::parseBool,				NULL,			offsetof( GlobalData, m_wireframe ) },
	{ "StateMachineDebug",				INI::parseBool,				NULL,			offsetof( GlobalData, m_stateMachineDebug ) },
	{ "ShroudOn",										INI::parseBool,				NULL,			offsetof( GlobalData, m_shroudOn ) },
	{ "FogOfWarOn",										INI::parseBool,				NULL,			offsetof( GlobalData, m_fogOfWarOn ) },
	{ "ShowCollisionExtents",				INI::parseBool,				NULL,			offsetof( GlobalData, m_showCollisionExtents ) },
  { "ShowAudioLocations",  				INI::parseBool,				NULL,			offsetof( GlobalData, m_showAudioLocations ) },
	{ "DebugProjectileTileWidth",		INI::parseReal,				NULL,			offsetof( GlobalData, m_debugProjectileTileWidth) },
	{ "DebugProjectileTileDuration",INI::parseInt,				NULL,			offsetof( GlobalData, m_debugProjectileTileDuration) },
	{ "DebugProjectileTileColor",		INI::parseRGBColor,		NULL,			offsetof( GlobalData, m_debugProjectileTileColor) },
	{ "DebugVisibilityTileCount",		INI::parseInt,				NULL,			offsetof( GlobalData, m_debugVisibilityTileCount) },
	{ "DebugVisibilityTileWidth",		INI::parseReal,				NULL,			offsetof( GlobalData, m_debugVisibilityTileWidth) },
	{ "DebugVisibilityTileDuration",INI::parseInt,				NULL,			offsetof( GlobalData, m_debugVisibilityTileDuration) },
	{ "DebugVisibilityTileTargettableColor",INI::parseRGBColor, NULL,	offsetof( GlobalData, m_debugVisibilityTargettableColor) },
	{ "DebugVisibilityTileDeshroudColor",		INI::parseRGBColor,	NULL,	offsetof( GlobalData, m_debugVisibilityDeshroudColor) },
	{ "DebugVisibilityTileGapColor",				INI::parseRGBColor,	NULL,	offsetof( GlobalData, m_debugVisibilityGapColor) },
	{ "DebugThreatMapTileDuration",					INI::parseInt,							NULL,	offsetof( GlobalData, m_debugThreatMapTileDuration) },
	{ "MaxDebugThreatMapValue",							INI::parseUnsignedInt,			NULL,	offsetof( GlobalData, m_maxDebugThreat) },
	{ "DebugCashValueMapTileDuration",			INI::parseInt,							NULL,	offsetof( GlobalData, m_debugCashValueMapTileDuration) },
	{ "MaxDebugCashValueMapValue",					INI::parseUnsignedInt,			NULL,	offsetof( GlobalData, m_maxDebugValue) },
	{ "VTune", INI::parseBool,	NULL,			offsetof( GlobalData, m_vTune ) },
	{ "SaveStats",									INI::parseBool,				NULL,			offsetof( GlobalData, m_saveStats ) },
	{ "UseLocalMOTD",								INI::parseBool,				NULL,			offsetof( GlobalData, m_useLocalMOTD ) },
	{ "BaseStatsDir",								INI::parseAsciiString,NULL,			offsetof( GlobalData, m_baseStatsDir ) },
	{ "LocalMOTDPath",							INI::parseAsciiString,NULL,			offsetof( GlobalData, m_MOTDPath ) },
	{ "ExtraLogging",								INI::parseBool,				NULL,			offsetof( GlobalData, m_extraLogging ) },
#endif

	{ NULL,					NULL,						NULL,						0 }  // keep this last

};


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GlobalData::GlobalData()
{
	Int i, j;

	//
	// we have now instanced a global data instance, if theOriginal is NULL, this is
	// *the* very first instance and it shall be recorded.  This way, when we load 
	// overrides of the global data, we can revert to the common, original data
	// in m_theOriginal
	//
	if( m_theOriginal == NULL )
		m_theOriginal = this;
	m_next = NULL;

#if defined(_DEBUG) || defined(_INTERNAL) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	m_specialPowerUsesDelay = TRUE;
#endif
  m_TiVOFastMode = FALSE;

	m_latencyAverage = 0;
	m_latencyAmplitude = 0;
	m_latencyPeriod = 0;
	m_latencyNoise = 0;
	m_packetLoss = 0;

#if defined(_DEBUG) || defined(_INTERNAL)
	m_wireframe = 0;
	m_stateMachineDebug = FALSE;
	m_shroudOn = TRUE;
	m_fogOfWarOn = FALSE;
	m_jabberOn = FALSE;
	m_munkeeOn = FALSE;
	m_showCollisionExtents = FALSE;
  m_showAudioLocations = FALSE;
	m_debugCamera = FALSE;
	m_debugVisibility = FALSE;
	m_debugVisibilityTileCount = 32;	// default to 32.
	m_debugVisibilityTileDuration = LOGICFRAMES_PER_SECOND;
	m_debugProjectilePath = FALSE;
	m_debugProjectileTileWidth = 10;
	m_debugProjectileTileDuration = LOGICFRAMES_PER_SECOND;  // Changed By Sadullah Nader
	m_debugThreatMap = FALSE;
	m_maxDebugThreat = 5000;
	m_debugThreatMapTileDuration = LOGICFRAMES_PER_SECOND;  // Changed By Sadullah Nader
	m_debugCashValueMap = FALSE;
	m_maxDebugValue = 10000;
	m_debugCashValueMapTileDuration = LOGICFRAMES_PER_SECOND; // Changed By Sadullah Nader
	m_debugIgnoreAsserts = FALSE;
	m_debugIgnoreStackTrace = FALSE;
	m_vTune = false;
	m_checkForLeaks = TRUE;
	m_benchmarkTimer = -1;
  

	m_allowUnselectableSelection = FALSE;
	m_disableCameraFade = false;
	m_disableScriptedInputDisabling = false;
	m_disableMilitaryCaption = false;
	m_saveStats = FALSE;
	m_saveAllStats = FALSE;
	m_useLocalMOTD = FALSE;
	m_baseStatsDir = ".\\";
	m_MOTDPath = "MOTD.txt";
	m_extraLogging = FALSE;
#endif

	m_playStats = -1;
	m_incrementalAGPBuf = FALSE;
	m_mapName.clear();
	m_moveHintName.clear();
	m_useTrees = 0;
	m_useTreeSway = TRUE;
	m_useDrawModuleLOD = FALSE;
	m_useHeatEffects = TRUE;
	m_useFpsLimit = FALSE;
	m_dumpAssetUsage = FALSE;
	m_framesPerSecondLimit = 0;
	m_chipSetType = 0;
	m_windowed = 0;
	m_windowMode = WINDOW_MODE_FULLSCREEN;
	m_msaaLevel = 0;
	m_vsync = FALSE;
	m_direct3D11 = TRUE;
	m_direct3D11DumpPath.clear();
	// The Direct3D 11 frame gets every effect the backend has unless -dx11post names a chain of its
	// own; "-dx11post off" is the faithful 2003 picture that dx11-check.ps1 compares against.
	m_direct3D11PostChain = "bloom,ao,fxaa,sharpen";
	m_xResolution = 800;
	m_yResolution = 600;
	m_maxShellScreens = 0;
	m_useCloudMap = FALSE;
	m_use3WayTerrainBlends = 1;
	m_useLightMap = FALSE;
	m_bilinearTerrainTex = FALSE;
	m_trilinearTerrainTex = FALSE;
	m_multiPassTerrain = FALSE;
	m_adjustCliffTextures = FALSE;
	m_stretchTerrain = FALSE;
	m_useHalfHeightMap = FALSE;
	m_terrainLOD = TERRAIN_LOD_AUTOMATIC;
	m_terrainLODTargetTimeMS = 0;
	m_enableDynamicLOD = TRUE;
	m_enableStaticLOD = TRUE;
	m_rightMouseAlwaysScrolls = FALSE;
	m_useWaterPlane = FALSE;
	m_useCloudPlane = FALSE;
	m_downwindAngle = ( -0.785f );//Northeast!
	m_useShadowVolumes = FALSE;
	m_useShadowVolumesForSkins = TRUE;	//on by default: the shipped INI has no entry for it
	m_useShadowDecals = FALSE;
	m_shadowsForProjectiles = TRUE;	//on by default: the shipped INI has no entry for it
	m_startAtMaxZoom = TRUE;		//open a game framed as wide as the player could zoom by hand
	m_shadowsForProps = TRUE;				//likewise: scenery with no shadow of its own gets one
	m_shadowsForParticles = TRUE;	//on by default: the shipped INI has no entry for it
	m_classicGraphics = FALSE;
	m_shadowMap = TRUE;						//the sun's own shadows are what the game draws with now
	m_shadowMapOnly = TRUE;				//and they replace the stencil volumes rather than joining them
	m_shadowMapReport = FALSE;
	m_shadowMapPenumbra = 0.0f;		//zero anywhere here means the built-in value stands
	m_shadowMapSkyFill = 0.0f;
	m_shadowMapStrength = 0.0f;
	m_shadowMapWidest = 0.0f;
	m_contactShadows = TRUE;		//on by default: a building with nothing under it reads as laid on the ground
	m_textureReductionFactor = -1;
	m_enableBehindBuildingMarkers = TRUE;
	m_scriptDebug = FALSE;
	m_particleEdit = FALSE;
	m_displayDebug = FALSE;
	m_winCursors = TRUE;
	m_constantDebugUpdate = FALSE;
	m_showTeamDot = FALSE;
	m_fixedSeed = -1; // disabled
	m_autoSkirmishPlayers = 0; // no skirmish from the command line
	m_autoSkirmishAIState = SLOT_BRUTAL_AI;
	m_autoSkirmishAIStateOdd = 0;		// 0 = not set: every slot plays at -aidiff
	m_noTacticsSlotParity = -1;
	m_autoSkirmishTeams = 0;				// 0 = not set: every slot fights every other slot
	m_peaceTime = 0;								// no truce unless -peacetime asks for one
	m_unitLimit = FALSE;						// no unit limit unless -unitlimit asks for one
	m_incomeSharing = 0;						// INCOME_SHARING_OFF unless -incomesharing asks
	m_techRespawn = 0;							// a destroyed tech building stays destroyed unless -techrespawn asks
	m_autoSkirmishObserver = FALSE;
	m_headless = FALSE;
	m_turbo = FALSE;
	m_maxGameFrames = 0; // run until the match ends
	m_screenShotFrame = 0; // take no picture unless -screenshot asks for one
	m_videoStartFrame = 0;
	m_videoEndFrame = 0; // record nothing unless -video asks for a range
	m_videoName.clear();
	m_wavStartFrame = 0;
	m_wavEndFrame = 0; // record no sound unless -wav asks for a range
	m_wavName.clear();
	m_autoCameraSeconds = 0; // the camera stays where it was put
	m_cameraLookSet = FALSE; // -camera not given: the map decides where the view starts
	m_cameraLook.x = m_cameraLook.y = 0.0f;
	m_traceMoveID = 0; // no movement trace
	m_slowFrameMS = 20.0f; // a frame worth a line in the log; -slowframe lowers it for a hunt
	m_drawDelayMS = 0; // client passes run as fast as the machine does unless -drawdelay slows them
	m_drawDelayJitterMS = 0;
	m_showLanes = FALSE; // the lane overlay is a diagnostic, off unless -showlanes asks for it
	m_uiDrill = 0; // nobody presses the minimise button; -uidrill is how a script presses it
	m_resDrillFrame = 0; // the resolution stays where it started unless -resdrill changes it mid-match
	m_resDrillX = 0;
	m_resDrillY = 0;
	m_resDrillKeep = FALSE;
	m_noRenderDevice = FALSE; // -headless still takes a 100x100 device unless -nodevice says not to
	m_controlPort = 0; // nothing listens; -control opens the socket
	m_scenarioFile.clear(); // nothing is scripted; -scenario is a measuring tool and ruins the match it runs in
	m_cinemaScript.clear(); // the interface is on and the camera belongs to the player
	m_autoSkirmishTakeover = FALSE; // the AI plays the opponents unless -takeover empties their seats
	for( Int slot = 0; slot < MAX_PLAYER_COUNT; slot++ )
		m_autoSkirmishSide[ slot ].clear(); // every faction still comes out of the seed unless -side names one
	m_netGameHosts.clear(); // no network game from the command line
	m_netGameStarted = FALSE;
	m_netGameLocalSlot = 0;
	m_netGameAISlots = 0;
	m_lanPlayerName.clear(); // the lobby name comes out of the preferences unless -lanname says otherwise
	m_lanLobbyOnStart = FALSE;
	m_skirmishLobbyOnStart = FALSE;
	m_optionsMenuOnStart = FALSE;
	m_randomMapsInMenus = FALSE;
	m_horizontalScrollSpeedFactor = 1.0;
	m_verticalScrollSpeedFactor = 1.0;

	m_waterPositionX = 0.0f;
	m_waterPositionY = 0.0f;
	m_waterPositionZ = 0.0f;
	m_waterExtentX = 0.0f;	
	m_waterExtentY = 0.0f;
	m_waterType = 0;
	m_featherWater = FALSE;
	m_showSoftWaterEdge = TRUE;	//display soft water edge
	m_usingWaterTrackEditor = FALSE;
	m_isWorldBuilder = FALSE;

	m_showMetrics = false;

	// peace time's no-go ring around a command center; GameData.ini can move both
	m_peaceTimeBaseRadius = 250.0f;
	m_peaceTimeBaseDamage = 250.0f;

	for( i = 0; i < MAX_WATER_GRID_SETTINGS; i++ )
	{

		m_vertexWaterHeightClampLow[ i ] = 0.0f;
		m_vertexWaterHeightClampHi[ i ] = 0.0f;
		m_vertexWaterAngle[ i ] = 0.0f;
		m_vertexWaterXPosition[ i ] = 0.0f;
		m_vertexWaterYPosition[ i ] = 0.0f;
		m_vertexWaterZPosition[ i ] = 0.0f;
		m_vertexWaterXGridCells[ i ] = 0;
		m_vertexWaterYGridCells[ i ] = 0;
		m_vertexWaterGridSize[ i ] = 0.0f;
		m_vertexWaterAttenuationA[ i ] = 0.0f;
		m_vertexWaterAttenuationB[ i ] = 0.0f;
		m_vertexWaterAttenuationC[ i ] = 0.0f;
		m_vertexWaterAttenuationRange[ i ] = 0.0f;
		//Added By Sadullah Nader
		//Initializations missing and needed
		m_vertexWaterAvailableMaps[i].clear();
	}  // end for i

	m_skyBoxPositionZ = 0.0f;
	m_drawSkyBox = FALSE;
	m_skyBoxScale = 4.5f;

	m_historicDamageLimit = 0;
	m_maxTerrainTracks = 0;

	m_levelGainAnimationDisplayTimeInSeconds = 0.0f;
	m_levelGainAnimationZRisePerSecond = 0.0f;

	m_getHealedAnimationDisplayTimeInSeconds = 0.0f;
	m_getHealedAnimationZRisePerSecond = 0.0f;

	m_maxTankTrackEdges=100;
	m_maxTankTrackOpaqueEdges=25;
	m_maxTankTrackFadeDelay=300000;

	m_timeOfDay = TIME_OF_DAY_AFTERNOON;
	m_weather = WEATHER_NORMAL;
	m_makeTrackMarks = FALSE;
	m_hideGarrisonFlags = FALSE;
	m_forceModelsToFollowTimeOfDay = true;
	m_forceModelsToFollowWeather = true;

	m_partitionCellSize = 0.0f;
	m_ammoPipScaleFactor = 1.0f;
	m_containerPipScaleFactor = 1.0f;
	m_ammoPipWorldOffset.zero();
	m_containerPipWorldOffset.zero();
	m_ammoPipScreenOffset.x = m_ammoPipScreenOffset.y = 0;
	m_containerPipScreenOffset.x = m_containerPipScreenOffset.y = 0;

	for (i=0; i<MAX_GLOBAL_LIGHTS; i++)
	{
		m_terrainAmbient[i].red = 0.0f;
		m_terrainAmbient[i].green = 0.0f;
		m_terrainAmbient[i].blue = 0.0f;
		m_terrainDiffuse[i].red = 0.0f;
		m_terrainDiffuse[i].green = 0.0f;
		m_terrainDiffuse[i].blue = 0.0f;
		m_terrainLightPos[i].x = 0.0f;
		m_terrainLightPos[i].y = 0.0f;
		m_terrainLightPos[i].z = -1.0f;

		for (j=0; j<TIME_OF_DAY_COUNT; j++)
		{	m_terrainLighting[ j ][i].ambient.red=0;
			m_terrainLighting[ j ][i].ambient.green=0;
			m_terrainLighting[ j ][i].ambient.blue=0;
			m_terrainLighting[ j ][i].diffuse.red=0;
			m_terrainLighting[ j ][i].diffuse.green=0;
			m_terrainLighting[ j ][i].diffuse.blue=0;
			m_terrainLighting[ j ][i].lightPos.x=0;
			m_terrainLighting[ j ][i].lightPos.y=0;
			m_terrainLighting[ j ][i].lightPos.z=-1.0f;

			m_terrainObjectsLighting[ j ][i].ambient.red=0;
			m_terrainObjectsLighting[ j ][i].ambient.green=0;
			m_terrainObjectsLighting[ j ][i].ambient.blue=0;
			m_terrainObjectsLighting[ j ][i].diffuse.red=0;
			m_terrainObjectsLighting[ j ][i].diffuse.green=0;
			m_terrainObjectsLighting[ j ][i].diffuse.blue=0;
			m_terrainObjectsLighting[ j ][i].lightPos.x=0;
			m_terrainObjectsLighting[ j ][i].lightPos.y=0;
			m_terrainObjectsLighting[ j ][i].lightPos.z=-1.0f;
		}
	}

	for (j=TIME_OF_DAY_FIRST; j<TIME_OF_DAY_COUNT; j++)
		m_infantryLightScale[j] = 1.5f;
	
	m_scriptOverrideInfantryLightScale = -1.0f;

	m_numGlobalLights = 3;
	m_maxRoadSegments = 0;
	m_maxRoadVertex = 0;
	m_maxRoadIndex = 0;
	m_maxRoadTypes = 0;

	m_baseValuePerSupplyBox = 100;

	m_audioOn = TRUE;
	m_musicOn = TRUE;
	m_soundsOn = TRUE;
	m_sounds3DOn = TRUE;
	m_speechOn = TRUE;
	m_videoOn = TRUE;
	m_disableCameraMovement = FALSE;
	m_maxVisibleTranslucentObjects = 512;
	m_maxVisibleOccluderObjects = 512;
	m_maxVisibleOccludeeObjects = 512;
	m_maxVisibleNonOccluderOrOccludeeObjects = 512;
	m_occludedLuminanceScale = 0.5f;

	m_useFX = TRUE;

//	m_inGame = FALSE;	

	m_noDraw = 0;
	m_particleScale = 1.0f;

	m_autoFireParticleSmallMax = 0;
	m_autoFireParticleMediumMax = 0;
	m_autoFireParticleLargeMax = 0;
	m_autoSmokeParticleSmallMax = 0;
	m_autoSmokeParticleMediumMax = 0;
	m_autoSmokeParticleLargeMax = 0;
	m_autoAflameParticleMax = 0;
  
	// Added By Sadullah Nader
	// Initializations missing and needed
	m_autoFireParticleSmallPrefix.clear();
	m_autoFireParticleMediumPrefix.clear();
	m_autoFireParticleLargePrefix.clear();
	m_autoSmokeParticleSmallPrefix.clear();
	m_autoSmokeParticleMediumPrefix.clear();
	m_autoSmokeParticleLargePrefix.clear();
	m_autoAflameParticlePrefix.clear();

	m_autoFireParticleSmallSystem.clear();
	m_autoFireParticleMediumSystem.clear();
	m_autoFireParticleLargeSystem.clear();
	m_autoSmokeParticleSmallSystem.clear();
	m_autoSmokeParticleMediumSystem.clear();
	m_autoSmokeParticleLargeSystem.clear();
	m_autoAflameParticleSystem.clear();
	m_levelGainAnimationName.clear();
	m_getHealedAnimationName.clear();
	m_specialPowerViewObjectName.clear();

	m_drawEntireTerrain = FALSE;
	m_maxParticleCount = 0;
	m_particleGroundBounce = FALSE;
	m_smokeThickness = 0.0f;
	m_particleCapOverride = 0;
	m_maxFieldParticleCount = 30;
	
	// End Add

	m_debugAI = AI_DEBUG_NONE;
	m_debugSupplyCenterPlacement = FALSE;
	m_debugAIObstacles = FALSE;
	m_showClientPhysics = TRUE;
	m_showTerrainNormals = FALSE;
	m_showObjectHealth = FALSE;
	// what this fork has always done, so nobody's game changes until they say so
	m_healthBarMode = HEALTH_BAR_ALWAYS;
	// the lobby's own colours until somebody asks for something else
	m_playerColorScheme = PLAYER_COLORS_ORIGINAL;
	// the words the game shipped with until somebody picks a translation
	m_textLanguage = TEXT_LANGUAGE_ENGLISH;
	// the mouse and keys this fork plays with until somebody asks for the ones the game shipped with
	m_inputScheme = INPUT_SCHEME_MODERN;
	// W A S D stay on the grid and the unit keys until somebody asks for them on the camera
	m_wasdCamera = FALSE;
	// the lines have been on since they were added, so nobody loses them until they say so
	m_showOrderLines = TRUE;

	m_particleEdit = FALSE;

	m_cameraPitch = 0.0f;
	m_cameraYaw = 0.0f;
	m_cameraHeight = 0.0f;
	m_minCameraHeight = 100.0f;
	m_maxCameraHeight = 300.0f;
	m_terrainHeightAtEdgeOfMap = 0.0f;

	m_unitDamagedThresh = 0.5f;
	m_unitReallyDamagedThresh = 0.1f;
	m_groundStiffness = 0.5f;
	m_structureStiffness = 0.5f;
	m_gravity = -1.0f;
	m_stealthFriendlyOpacity = 0.5f;
	m_defaultOcclusionDelay = LOGICFRAMES_PER_SECOND * 3;	//default to 3 seconds

	m_preloadEverything = FALSE;
	m_preloadReport = FALSE;

	m_netMinPlayers = 1; // allowing sandbox mode

	m_defaultIP = 0;

	m_BuildSpeed = 0.0f;
	m_MinDistFromEdgeOfMapForBuild = 0.0f;
	m_SupplyBuildBorder = 0.0f;
	m_allowedHeightVariationForBuilding = 0.0f;
	m_MinLowEnergyProductionSpeed = 0.0f;
	m_MaxLowEnergyProductionSpeed = 0.0f;
	m_LowEnergyPenaltyModifier = 0.0f;
	m_MultipleFactory = 0.0f;
	m_RefundPercent = 0.0f;

	m_commandCenterHealRange = 0.0f;
	m_commandCenterHealAmount = 0.0f;
	m_maxTunnelCapacity = 0;
	m_maxLineBuildObjects = 0;

	m_standardMinefieldDensity = 0.01f;
	m_standardMinefieldDistance = 40.0f;
	
	m_groupSelectMinSelectSize = 5;
	m_groupSelectVolumeBase = 0.5f;
	m_groupSelectVolumeIncrement = 0.02f;
	m_maxUnitSelectSounds = 8;

	m_selectionFlashSaturationFactor = 0.5f; /// how colorful should the selection flash be? 0-4
	m_selectionFlashHouseColor = FALSE;  /// skip the house color and just use white.

	m_cameraAudibleRadius = 500.0;
	m_groupMoveClickToGatherFactor = 1.0f;

	m_shakeSubtleIntensity = 0.5f;
	m_shakeNormalIntensity = 1.0f;
	m_shakeStrongIntensity = 2.5f;
	m_shakeSevereIntensity = 5.0f;
	m_shakeCineExtremeIntensity = 8.0f;
	m_shakeCineInsaneIntensity = 12.0f;
	m_maxShakeIntensity = 10.0f;
	m_maxShakeRange = 150.f;

	m_sellPercentage = 1.0f;
	m_baseRegenHealthPercentPerSecond = 0.0f;
	m_baseRegenDelay = 0;

#ifdef ALLOW_SURRENDER
	m_prisonBountyMultiplier = 1.0f;
	m_prisonBountyTextColor = GameMakeColor( 255, 255, 255, 255 );
#endif

	m_hotKeyTextColor = GameMakeColor(255,255,0,255);

  // THis is put ON ice until later
  //  m_cheaterHasBeenSpiedIfMyLowestBitIsTrue = GameMakeColor(255,128,0,0);// orange, to the hacker's eye

	m_shroudColor.setFromInt( 0x00FFFFFF ) ;
	m_clearAlpha = 255;
	m_fogAlpha = 127;
	m_shroudAlpha = 0;

	m_powerBarBase = 7;
	m_powerBarIntervals = 3;
	m_powerBarYellowRange = 5;
	m_displayGamma = 1.0f;	//ramp that does nothing

	m_standardPublicBones.clear();

	m_antiAliasBoxValue = 0;

//	m_languageFilterPref = false;
	m_languageFilterPref = true;
	m_firewallBehavior = FirewallHelperClass::FIREWALL_TYPE_UNKNOWN;
	m_firewallSendDelay = FALSE;
	m_firewallPortOverride = 0;
	m_firewallPortAllocationDelta = 0;
	m_loadScreenDemo = FALSE;
	m_disableRender = false;
	
	m_saveCameraInReplay = FALSE;
	m_useCameraInReplay = FALSE;


	m_debugShowGraphicalFramerate = FALSE;

	// By default, show all asserts.

	m_unlookPersistDuration		= 30;

	//-----------------------------------------------------------------------------------------------

	// network timing values.  Having these default to 0 would be bad. - BGC
	m_networkFPSHistoryLength = 30;
	m_networkLatencyHistoryLength = 200;
	m_networkRunAheadMetricsTime = 500;
	m_networkCushionHistoryLength = 10;
	m_networkRunAheadSlack = 10;
	m_networkKeepAliveDelay = 20;
	/* 5000 was EA's.  With the stall judgement in DisconnectManager::update the screen no longer
		 comes up on duration alone, so this is now only the point at which we start asking whether
		 anybody has gone quiet - and 5s of stall is common on a loaded machine in an eight player
		 game.  8s costs nothing when the link really is dead, because the silence test decides. */
	m_networkDisconnectTime = 8000;
	/* Longer than the 8s keep-alive round: a player who is merely stalled still answers on that
		 schedule, so anything under it would call a slow player a disconnected one. */
	m_networkPlayerSilenceTime = 12000;
	m_networkStallCeilingTime = 20000;
	m_networkPlayerTimeoutTime = 60000;
	m_networkDisconnectScreenNotifyTime = 15000;

	m_isBreakableMovie = FALSE;
	m_breakTheMovie = FALSE;

	setTimeOfDay( m_timeOfDay );

	m_buildMapCache = FALSE;
	m_initialFile.clear();
	m_pendingFile.clear();

	for (i = LEVEL_FIRST; i <= LEVEL_LAST; ++i)
		m_healthBonus[i] = 1.0f;

	for (i = 0; i < PLAYERTYPE_COUNT; ++i)
	{
		for (j = 0; j < DIFFICULTY_COUNT; ++j)
		{
			m_soloPlayerHealthBonusForDifficulty[i][j] = 1.0f;
		}
	}

	m_defaultStructureRubbleHeight = 1.0f;
	m_weaponBonusSet = newInstance(WeaponBonusSet);

	m_shellMapName.set("Maps\\ShellMap1\\ShellMap1.map");
	m_shellMapOn =TRUE;
	m_playIntro = TRUE;
	m_playSizzle = TRUE;
	m_afterIntro = FALSE;
	m_allowExitOutOfMovies = FALSE;
	m_loadScreenRender = FALSE;
  m_musicVolumeFactor = 0.5f;
 	m_SFXVolumeFactor = 0.5f;
  m_voiceVolumeFactor = 0.5f;
  m_3DSoundPref = false;

	m_keyboardDefaultScrollFactor = m_keyboardScrollFactor = 0.5f;
	m_scrollAmountCutoff = 10.0f;
	m_cameraAdjustSpeed = 0.1f;
	m_enforceMaxCameraHeight = TRUE;

	//
	// The fork's own conveniences default ON. They were opt-in at first, which in practice meant
	// nobody saw them: an Options.ini that predates them simply has no such key, so every one of
	// them stayed off and the features looked like they had never been added.
	//
	// The first four are still catalog rows, so Options.ini can still turn them off by name even
	// though the menu no longer shows them. The rest are not: they left TheOptionCatalog with the
	// controls that used to set them, and what is written here is what every game gets. GameData.ini
	// remains the way to change one, because the field table above still names it.
	//
	m_useCameraConstraints = TRUE;
	m_cameraBoundaryMargin = 200;
	m_edgeScrollInWindowedMode = TRUE;
	m_snapCameraRotateTo45 = TRUE;
	m_zoomToCursor = TRUE;
	m_isometricCamera = FALSE;
	// the right button no longer scrolls, so a right-drag is free to mean something
	m_formationDrag = TRUE;
	m_showAllyCursors = TRUE;
	m_chromaLighting = TRUE;	//costs nothing on a machine with no Razer server: the handshake fails once
	m_menuTransitionSpeed = 100;
	m_textureFilterMode = 2;	// anisotropic; retail shipped bilinear on a 2003 fill-rate budget
	m_anisotropyLevel = 0;		// whatever the card offers, capped at 16 in _Init_Filters

	m_snapBuildPlacementTo45 = TRUE;
	m_gridBuildPlacement = TRUE;
	m_nudgeBuildPlacement = TRUE;
	m_moneyPerMinute = 0;
	m_buildPlacementOpacity = PLACEMENT_SILHOUETTE_OPACITY;
	m_buildPlacementShadows = TRUE;
	m_showHudOverlay = TRUE;
	m_showPlacementRangeRing = TRUE;
	m_showSkillStrip = TRUE;
	m_showSuperweaponStrip = TRUE;
	m_workersReturnToSupply = TRUE;
	m_detailedBuildTooltips = TRUE;
	m_archiveReplays = TRUE;

	// Bloom is the one that does NOT default on: it changes how the game looks rather than what it
	// can do, and the artwork was painted in 2003 for a screen with no glow at all.  Both fields are
	// percentages and GameData.ini sets them as such - the strength, and the brightness below which
	// nothing glows.  The options screen offers levels instead and stores one of those in
	// Options.ini; OptionsCatalog.cpp holds the percentage each level stands for, and 0 and 65 here
	// are two of them.
	m_bloomIntensity = 0;
	m_bloomThreshold = 65;
	
	m_animateWindows = TRUE;
	
	m_iniCRC = 0;
	m_exeCRC = 0;
	
	// lets CRC the executable!  Whee!
	const Int blockSize = 65536;
	Char buffer[ _MAX_PATH ];
	CRC exeCRC;
	GetModuleFileName( NULL, buffer, sizeof( buffer ) );
	File *fp = TheFileSystem->openFile(buffer, File::READ | File::BINARY);
	if (fp != NULL) {
		unsigned char crcBlock[blockSize];
		Int amtRead = 0;
		while ( (amtRead=fp->read(crcBlock, blockSize)) > 0 )
		{
			exeCRC.computeCRC(crcBlock, amtRead);
		}
		fp->close();
		fp = NULL;
	}
	if (TheVersion)
	{
		UnsignedInt version = TheVersion->getVersionNumber();
		exeCRC.computeCRC( &version, sizeof(UnsignedInt) );
	}
	// Add in MP scripts to the EXE CRC, since the game will go out of sync if they change
	fp = TheFileSystem->openFile("Data\\Scripts\\SkirmishScripts.scb", File::READ | File::BINARY);
	if (fp != NULL) {
		unsigned char crcBlock[blockSize];
		Int amtRead = 0;
		while ( (amtRead=fp->read(crcBlock, blockSize)) > 0 )
		{
			exeCRC.computeCRC(crcBlock, amtRead);
		}
		fp->close();
		fp = NULL;
	}
	fp = TheFileSystem->openFile("Data\\Scripts\\MultiplayerScripts.scb", File::READ | File::BINARY);
	if (fp != NULL) {
		unsigned char crcBlock[blockSize];
		Int amtRead = 0;
		while ( (amtRead=fp->read(crcBlock, blockSize)) > 0 )
		{
			exeCRC.computeCRC(crcBlock, amtRead);
		}
		fp->close();
		fp = NULL;
	}

	m_exeCRC = exeCRC.get();
	DEBUG_LOG(("EXE CRC: 0x%8.8X\n", m_exeCRC));
	
	m_movementPenaltyDamageState = BODY_REALLYDAMAGED;
	
	m_shouldUpdateTGAToDDS = FALSE;
	
	// Default DoubleClickTime to System double click time.
	m_doubleClickTimeMS = GetDoubleClickTime(); // Note: This is actual MS, not frames.
	
#ifdef DUMP_PERF_STATS
	m_dumpPerformanceStatistics = FALSE;
  m_dumpStatsAtInterval = FALSE;
  m_statsInterval = 30;      
#endif

	m_forceBenchmark = FALSE;	///<forces running of CPU detection benchmark, even on known cpu's.

	m_keyboardCameraRotateSpeed = 0.1f;

  // Documents plus the registry's leaf name, or the same leaf under %LOCALAPPDATA% when nothing can
  // be written under Documents - see findUserDataDirectory, which WinMain asks the same question of.
  // A redirected Documents folder can sit at a path longer than MAX_PATH, so the buffer is generous.
  char temp[1024];
  if (findUserDataDirectory(temp, sizeof(temp)))
  {
    m_userDataDir = temp;
    DEBUG_LOG(("User data folder: %s\n", temp));
  }
  else
  {
    //
    // Saves, replays, the options file and the crash log all live under this.  When the shell
    // cannot tell us where Documents is - a redirected folder whose path is longer than MAX_PATH is
    // the way it happens - the directory was simply left empty and every one of those writes went
    // somewhere else without a word.  Say so; the tail of this log is what a bug report carries.
    //
    DEBUG_LOG(("Could not find the Documents folder: saves, replays and settings have nowhere of their own to go.\n"));
  }
	
	//-allAdvice feature
	//m_allAdvice = FALSE;

	m_clientRetaliationModeEnabled = TRUE; //On by default.

}  // end GlobalData


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GlobalData::~GlobalData( void )
{
	DEBUG_ASSERTCRASH( TheWritableGlobalData->m_next == NULL, ("~GlobalData: theOriginal is not original\n") );

	if (m_weaponBonusSet)
		m_weaponBonusSet->deleteInstance();

	if( m_theOriginal == this )	{
		m_theOriginal = NULL;
		TheWritableGlobalData = NULL;
	}

}  // end ~GlobalData

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool GlobalData::setTimeOfDay( TimeOfDay tod )
{
	if( tod >= TIME_OF_DAY_COUNT || tod < TIME_OF_DAY_FIRST )
	{
		return FALSE;
	}

	m_timeOfDay = tod;
	for (Int i=0; i<MAX_GLOBAL_LIGHTS; i++)
	{	m_terrainAmbient[i] = m_terrainLighting[ tod ][i].ambient;
		m_terrainDiffuse[i] = m_terrainLighting[ tod ][i].diffuse;
		m_terrainLightPos[i] = m_terrainLighting[ tod ][i].lightPos;
	}

	return TRUE;

}

//-------------------------------------------------------------------------------------------------
/** Create a new global data instance to override the existing data set.  The
	* initial values of the newly created instance will be a copy of the current
	* data (or the most recently created override) */
//-------------------------------------------------------------------------------------------------
GlobalData *GlobalData::newOverride( void )
{
	GlobalData *override = NEW GlobalData;

	// copy the data from the latest override (TheWritableGlobalData) to the newly created instance
	DEBUG_ASSERTCRASH( TheWritableGlobalData, ("GlobalData::newOverride() - no existing data\n") );
	*override = *TheWritableGlobalData;

	//
	// link the override to the previously created one, the link order is important here
	// for the reset function, if you change the way things are linked
	// for overrides make sure you update the reset function
	//
	override->m_next = TheWritableGlobalData;

	// set this new instance as the 'most current override' where we will access all data from
	TheWritableGlobalData = override;

	return override;

}  // end newOveride

//-------------------------------------------------------------------------------------------------
void GlobalData::init( void )
{
	// nothing
}

//-------------------------------------------------------------------------------------------------
/** Reset, remove any override data instances and return to just the initial one
	*/
//-------------------------------------------------------------------------------------------------
void GlobalData::reset( void )
{
	DEBUG_ASSERTCRASH(this == TheWritableGlobalData, ("calling reset on wrong GlobalData"));

	//
	// delete	any data instances that were loaded as an override and set the original
	// global data instance as the singleton TheWritableGlobalData once again
	//
	while (TheWritableGlobalData != GlobalData::m_theOriginal)
	{

		// get next instance
		GlobalData* next = TheWritableGlobalData->m_next;

		// delete the head of the global data list (the latest override)
		delete TheWritableGlobalData;

		// set next as top
		TheWritableGlobalData = next;

	}  // end while

	//
	// we now have the one single global data in TheWritableGlobalData singleton, lets sanity check
	// some of all that
	//
	DEBUG_ASSERTCRASH( TheWritableGlobalData->m_next == NULL, ("ResetGlobalData: theOriginal is not original\n") );
	DEBUG_ASSERTCRASH( TheWritableGlobalData == GlobalData::m_theOriginal, ("ResetGlobalData: oops\n") );

}  // end ResetGlobalData

//-------------------------------------------------------------------------------------------------
/** Parse GameData entry */
//-------------------------------------------------------------------------------------------------
void GlobalData::parseGameDataDefinition( INI* ini )
{
	if( TheWritableGlobalData && ini->getLoadType() != INI_LOAD_MULTIFILE)
	{

		// 
		// if the type of loading we're doing creates override data, we need to
		// be loading into a new override item
		//
		if( ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES )
			TheWritableGlobalData->newOverride();

	}  // end if
	else if (!TheWritableGlobalData)
	{

		// we don't have any global data instance at all yet, create one
		TheWritableGlobalData = NEW GlobalData;

	}  // end else
	// If we're multifile, then continue loading stuff into the Global Data as normal.

	// parse the ini weapon definition
	ini->initFromINI( TheWritableGlobalData, s_GlobalDataFieldParseTable );

	// GameData.ini knows only the old boolean.  Seed the three-way mode from it, so a data file that
	// asks for a window still gets one, and let Options.ini and then the command line refine it.
	TheWritableGlobalData->m_windowMode = TheWritableGlobalData->m_windowed
			? WINDOW_MODE_WINDOWED : WINDOW_MODE_FULLSCREEN;


	// override INI values with user preferences
	OptionPreferences optionPref;
	TheWritableGlobalData->m_clientRetaliationModeEnabled = optionPref.getRetaliationModeEnabled();
	TheWritableGlobalData->m_doubleClickAttackMove = optionPref.getDoubleClickAttackMoveEnabled();
	TheWritableGlobalData->m_keyboardScrollFactor = optionPref.getScrollFactor();
	TheWritableGlobalData->m_defaultIP = optionPref.getLANIPAddress();
	TheWritableGlobalData->m_firewallSendDelay = optionPref.getSendDelay();
	TheWritableGlobalData->m_firewallBehavior = optionPref.getFirewallBehavior();
	TheWritableGlobalData->m_firewallPortAllocationDelta = optionPref.getFirewallPortAllocationDelta();
	TheWritableGlobalData->m_firewallPortOverride = optionPref.getFirewallPortOverride();
	
	TheWritableGlobalData->m_saveCameraInReplay = optionPref.saveCameraInReplays();
	TheWritableGlobalData->m_useCameraInReplay = optionPref.useCameraInReplays();
	
	Int val=optionPref.getGammaValue();
	//generate a value between 0.6 and 2.0.
	if (val < 50)
	{	//darker gamma
		if (val <= 0)
			TheWritableGlobalData->m_displayGamma = 0.6f;
		else
			TheWritableGlobalData->m_displayGamma=1.0f-(0.4f) * (Real)(50-val)/50.0f;
	}
	else
	if (val > 50)
		TheWritableGlobalData->m_displayGamma=1.0f+(1.0f) * (Real)(val-50)/50.0f;

	Int xres,yres;
	optionPref.getResolution(&xres, &yres);

	TheWritableGlobalData->m_xResolution = xres;
	TheWritableGlobalData->m_yResolution = yres;
	TheWritableGlobalData->m_monitor = optionPref["Monitor"];

	// Everything in TheOptionCatalog, in one pass, and last: a row is allowed to overwrite what the
	// hand-written block above just read.  This is also why the catalog is read here and not in
	// GameLODManager::setOptionPreferences, where it started - this function runs while
	// TheWritableGlobalData is being constructed, which is before parseCommandLine, so the command
	// line still wins over the preferences file.  setOptionPreferences runs after it and would not.
	loadOptionsFromPreferences( optionPref );

	// Classic graphics takes the stencil shadows and the unfiltered picture back here, before the
	// command line, so -dx11post still names a chain for the one run it is given.  The rest of the
	// setting is read where the device starts and where the archives mount.
	if (TheWritableGlobalData->m_classicGraphics)
	{
		TheWritableGlobalData->m_shadowMap = FALSE;
		TheWritableGlobalData->m_direct3D11PostChain = "off";
	}

	applyWindowMode();
}

//-------------------------------------------------------------------------------------------------
/** Work out what m_windowMode means for the rest of the engine. */
//-------------------------------------------------------------------------------------------------
void applyWindowMode( void )
{
	const Int mode = TheWritableGlobalData->m_windowMode;

	// Both windowed modes ask the device for a windowed swap chain; what separates them is the style
	// WinMain gave the window, which was decided before any of this ran.
	TheWritableGlobalData->m_windowed = (mode != WINDOW_MODE_FULLSCREEN);

	if( mode == WINDOW_MODE_BORDERLESS )
	{
		const MonitorEntry monitor = findMonitor( TheWritableGlobalData->m_monitor.str() );
		TheWritableGlobalData->m_xResolution = monitor.rect.right - monitor.rect.left;
		TheWritableGlobalData->m_yResolution = monitor.rect.bottom - monitor.rect.top;

		// there is no desktop left around a borderless window to park the cursor on, so scrolling at
		// the edge is the only way the mouse moves the camera
		TheWritableGlobalData->m_edgeScrollInWindowedMode = TRUE;
	}
}

