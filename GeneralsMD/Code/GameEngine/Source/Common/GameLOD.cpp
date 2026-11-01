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

// FILE: GameLOD.cpp ///////////////////////////////////////////////////////////
//
// Used to set detail levels of various game systems.
//
// Author: Mark Wilczynski, Sept 2002
//
//
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "zhio.h"

#include "Common/GameLOD.h"
#include "Common/OptionsCatalog.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/GameClient.h"
#include "Common/UserPreferences.h"

#define DEFINE_PARTICLE_SYSTEM_NAMES
#include "GameClient/ParticleSys.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define PROFILE_ERROR_LIMIT	0.94f	//fraction of profiled result needed to get a match.  Allows some room for error/fluctuation.

//Hack to get access to a static method on the W3DDevice side. -MW
extern Bool testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, Int *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex);

GameLODManager *TheGameLODManager=NULL;

static const FieldParse TheStaticGameLODFieldParseTable[] = 
{
	{ "MinimumFPS",						INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_minFPS)},
	{ "MinimumProcessorFps",			INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_minProcessorFPS)},
	{ "SampleCount2D",					INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_sampleCount2D ) },
	{ "SampleCount3D",					INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_sampleCount3D ) },
	{ "StreamCount",					INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_streamCount ) },
	{ "MaxParticleCount",				INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_maxParticleCount ) },
	{ "GpuParticleCapacity",			INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_gpuParticleCapacity ) },
	{ "UseShadowVolumes",				INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_useShadowVolumes ) },
	{ "UseShadowDecals",				INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_useShadowDecals ) },
	{ "UseCloudMap",					INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_useCloudMap ) },
	{ "UseLightMap",					INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_useLightMap ) },
	{ "ShowSoftWaterEdge",				INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_showSoftWaterEdge ) },
	{ "MaxTankTrackEdges",				INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_maxTankTrackEdges) },
	{ "MaxTankTrackOpaqueEdges",		INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_maxTankTrackOpaqueEdges) },
	{ "MaxTankTrackFadeDelay",			INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_maxTankTrackFadeDelay) },
	{ "UseBuildupScaffolds",			INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_useBuildupScaffolds ) },
	{ "UseTreeSway",					INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_useTreeSway ) },
	{ "UseEmissiveNightMaterials",		INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_useEmissiveNightMaterials ) },
	{ "UseHeatEffects",					INI::parseBool,					NULL,	offsetof( StaticGameLODInfo, m_useHeatEffects ) },
	{ "TextureReductionFactor",		INI::parseInt,					NULL,	offsetof( StaticGameLODInfo, m_textureReduction ) },
	{ NULL, NULL, NULL, 0 }
};

static const char *StaticGameLODNames[]=
{
	"Low",
	"Medium",
	"High",
	"Ultra",
	"Custom"
};

// The Direct3D 11 particle ring each level asks for, Low to Ultra.  Ultra's is the million slots the
// ring was measured at (GPU-PARTICLES.md) and each level below halves it.  The shipped GameLOD.ini
// has no GpuParticleCapacity line, so these stand unless a modder's copy names one.
static const Int TheGpuParticleCapacities[STATIC_GAME_LOD_CUSTOM] = { 1 << 17, 1 << 18, 1 << 19, 1 << 20 };

StaticGameLODInfo::StaticGameLODInfo(void)
{
	m_minFPS=0;
	m_minProcessorFPS=0;
	m_sampleCount2D=6;
	m_sampleCount3D=24;
	m_streamCount=2;
	m_maxParticleCount=2500;
	m_gpuParticleCapacity=TheGpuParticleCapacities[STATIC_GAME_LOD_HIGH];

	m_useShadowVolumes=TRUE;
	m_useShadowDecals=TRUE;
	m_useCloudMap=TRUE;
	m_useLightMap=TRUE;
	m_showSoftWaterEdge=TRUE;
	m_maxTankTrackEdges=100;
	m_maxTankTrackOpaqueEdges=25;
	m_maxTankTrackFadeDelay=300000;
	m_useBuildupScaffolds=TRUE;
	m_useTreeSway=TRUE;
	m_useEmissiveNightMaterials=TRUE;
	m_useHeatEffects=TRUE;
	m_textureReduction = 0;	//none
	m_useFpsLimit = TRUE;
	m_enableDynamicLOD = TRUE;
	m_useTrees = TRUE;
}

static const FieldParse TheDynamicGameLODFieldParseTable[] = 
{
	{ "MinimumFPS",						INI::parseInt,					NULL,	offsetof( DynamicGameLODInfo, m_minFPS)},
	{ "ParticleSkipMask",				INI::parseInt,					NULL,	offsetof( DynamicGameLODInfo, m_dynamicParticleSkipMask)},
	{ "DebrisSkipMask",					INI::parseInt,					NULL,	offsetof( DynamicGameLODInfo, m_dynamicDebrisSkipMask)},
	{ "SlowDeathScale",					INI::parseReal,					NULL,	offsetof( DynamicGameLODInfo, m_slowDeathScale)},
	{ "MinParticlePriority",			INI::parseIndexList, ParticlePriorityNames,	offsetof( DynamicGameLODInfo, m_minDynamicParticlePriority)},
	{ "MinParticleSkipPriority",		INI::parseIndexList, ParticlePriorityNames,	offsetof( DynamicGameLODInfo, m_minDynamicParticleSkipPriority)},
	{ NULL, NULL, NULL, 0 }
};

static const char *DynamicGameLODNames[]=
{
	"Low",
	"Medium",
	"High",
	"VeryHigh"
};

DynamicGameLODInfo::DynamicGameLODInfo(void)
{
	m_minFPS=0;
	m_dynamicParticleSkipMask=0;
	m_dynamicDebrisSkipMask=0;
	m_slowDeathScale=1.0f;
	m_minDynamicParticlePriority = PARTICLE_PRIORITY_LOWEST;
	m_minDynamicParticleSkipPriority = PARTICLE_PRIORITY_LOWEST;
};

//Keep this in sync with enum in GameLOD.h
static char *CPUNames[] = 
{
	"XX","P3", "P4","K7", NULL
};

//Keep this in sync with enum in GameLOD.h
static char *VideoNames[] = 
{
	"XX","V2","V3","V4","V5","TNT","TNT2","GF2","R100","PS11","GF3","GF4","PS14","R200","PS20","R300", NULL
};

void parseReallyLowMHz(INI* ini)
{
	Int mhz;
	INI::parseInt(ini,NULL,&mhz,NULL);
	if (TheGameLODManager)
	{
		TheGameLODManager->setReallyLowMHz(mhz);
	}
}

void INI::parseBenchProfile( INI* ini)
{
	if( TheGameLODManager )
	{
			BenchProfile *preset = TheGameLODManager->newBenchProfile();

			if (preset)
			{
				INI::parseIndexList(ini,NULL,&preset->m_cpuType,CPUNames);
				INI::parseInt(ini,NULL,&preset->m_mhz,NULL);
				INI::parseReal(ini,NULL,&preset->m_intBenchIndex,NULL);
				INI::parseReal(ini,NULL,&preset->m_floatBenchIndex,NULL);
				INI::parseReal(ini,NULL,&preset->m_memBenchIndex,NULL);
			}
	}
}

/**Parse a description of all the LOD settings for a given detail level*/
void INI::parseLODPreset( INI* ini )
{
	const char *c;
	AsciiString name;

	// read the name
	c = ini->getNextToken();
	name.set( c );	//name of detail level - low, medium, high

	if( TheGameLODManager )
	{
		StaticGameLODLevel index = (StaticGameLODLevel)TheGameLODManager->getStaticGameLODIndex(name);
		if (index != STATIC_GAME_LOD_UNKNOWN)
		{
			LODPresetInfo *preset = TheGameLODManager->newLODPreset(index);

			if (preset)
			{
				INI::parseIndexList(ini,NULL,&preset->m_cpuType,CPUNames);
				INI::parseInt(ini,NULL,&preset->m_mhz,NULL);
				INI::parseIndexList(ini,NULL,&preset->m_videoType,VideoNames);
				INI::parseInt(ini,NULL,&preset->m_memory,NULL);
			}
		}
	}
}

GameLODManager::GameLODManager(void)
{
	m_currentStaticLOD = STATIC_GAME_LOD_UNKNOWN;
	m_currentDynamicLOD = DYNAMIC_GAME_LOD_HIGH;
	m_numParticleGenerations=0;
	m_dynamicParticleSkipMask=0;
	m_numDebrisGenerations=0;
	m_dynamicDebrisSkipMask=0;
	m_videoPassed=false;
	m_cpuPassed=false;
	m_memPassed=false;
	m_slowDeathScale=1.0f;
	m_idealDetailLevel = STATIC_GAME_LOD_UNKNOWN;
	m_videoChipType = DC_MAX;
	m_cpuType = XX;
	m_numRAM=0;
	m_cpuFreq=0;
	m_intBenchIndex=0;
	m_floatBenchIndex=0;
	m_memBenchIndex=0;
	m_compositeBenchIndex=0;
	m_numBenchProfiles=0;
	m_currentTextureReduction=0;
	m_reallyLowMHz = 400;
	
	for (Int i=0; i<STATIC_GAME_LOD_CUSTOM; i++)
	{
		m_numLevelPresets[i]=0;
		m_staticGameLODInfo[i].m_gpuParticleCapacity=TheGpuParticleCapacities[i];	//before GameLOD.ini, which may name its own
	}
};

GameLODManager::~GameLODManager()
{

}

BenchProfile *GameLODManager::newBenchProfile(void)
{
	if (m_numBenchProfiles < MAX_BENCH_PROFILES)
	{	
		m_numBenchProfiles++;
		return &m_benchProfiles[m_numBenchProfiles-1];
	}

	DEBUG_CRASH(( "GameLODManager::newBenchProfile - Too many profiles defined\n"));
	return NULL;
}

LODPresetInfo *GameLODManager::newLODPreset(StaticGameLODLevel index)
{
	if (m_numLevelPresets[index] < MAX_LOD_PRESETS_PER_LEVEL)
	{	
		m_numLevelPresets[index]++;
		return &m_lodPresets[index][m_numLevelPresets[index]-1];
	}

	DEBUG_CRASH(( "GameLODManager::newLODPreset - Too many presets defined for '%s'\n", TheGameLODManager->getStaticGameLODLevelName(index)));
	return NULL;
}

void GameLODManager::init(void)
{
	INI ini;
	//Get Presets for each LOD level.
	ini.load( AsciiString( "Data\\INI\\GameLOD.ini" ), INI_LOAD_OVERWRITE, NULL );

	//Get presets for each known hardware configuration
	ini.load( AsciiString( "Data\\INI\\GameLODPresets.ini"), INI_LOAD_OVERWRITE, NULL);

	// Ultra has no block in the shipped GameLOD.ini, so it is worked out from whatever High became.
	m_staticGameLODInfo[STATIC_GAME_LOD_ULTRA] = makeUltraStaticGameLOD( m_staticGameLODInfo[STATIC_GAME_LOD_HIGH] );

	// A preset is a step on the options menu's quality slider, and what it puts in the controls the
	// menu shows is that step's, not GameLOD.ini's, so the boxes a preset fills in are what it applies.
	for (Int level = STATIC_GAME_LOD_LOW; level < STATIC_GAME_LOD_CUSTOM; ++level)
		applyGraphicsQualityStep( m_staticGameLODInfo[level], TheGraphicsQualityAnchors[level] );

	//Get Presets for custom LOD level by pulling them out of initial globaldata (which should
	//have all settings already applied).
	refreshCustomStaticLODLevel();

	//Override with user preferences
	OptionPreferences optionPref;

	StaticGameLODLevel userSetDetail=(StaticGameLODLevel)optionPref.getStaticGameDetail();

	m_idealDetailLevel=(StaticGameLODLevel)optionPref.getIdealStaticGameDetail();

	//always get this data in case we need it later.
	testMinimumRequirements(NULL,&m_cpuType,&m_cpuFreq,&m_numRAM,NULL,NULL,NULL);

	if ((Real)(m_numRAM)/(Real)(256*1024*1024) >= PROFILE_ERROR_LIMIT)
		m_memPassed=TRUE;	//check if they have at least 256 MB

	if (m_idealDetailLevel == STATIC_GAME_LOD_UNKNOWN || TheGlobalData->m_forceBenchmark)
	{
		if (m_cpuType == XX || TheGlobalData->m_forceBenchmark)
		{
			//need to run the benchmark
			testMinimumRequirements(NULL,NULL,NULL,NULL,&m_intBenchIndex,&m_floatBenchIndex,&m_memBenchIndex);
			
			if (TheGlobalData->m_forceBenchmark)
			{	//we want to see the numbers.  So dump them to a logfile.
				FILE *fp=zh_fopen("Benchmark.txt","w");
				if (fp)
				{
					fprintf(fp,"BenchProfile = %s %d %f %f %f", CPUNames[m_cpuType], m_cpuFreq, m_intBenchIndex, m_floatBenchIndex, m_memBenchIndex);
					fclose(fp);
				}
			}

	 		m_compositeBenchIndex = m_intBenchIndex + m_floatBenchIndex;	///@todo: Need to scale these based on our apps usage of int/float/mem ops.

			StaticGameLODLevel currentLevel=STATIC_GAME_LOD_LOW;
			BenchProfile *prof=m_benchProfiles;
			m_cpuType = P3;	//assume lowest spec.
			m_cpuFreq = 1000;	//assume lowest spec.
			for (Int k=0; k<m_numBenchProfiles; k++)
			{
				//Check if we're within 5% of the performance of this cpu profile.
				if (m_intBenchIndex/prof->m_intBenchIndex >= PROFILE_ERROR_LIMIT && m_floatBenchIndex/prof->m_floatBenchIndex >= PROFILE_ERROR_LIMIT && m_memBenchIndex/prof->m_memBenchIndex >= PROFILE_ERROR_LIMIT)
				{	
					for (Int i=STATIC_GAME_LOD_HIGH; i >= STATIC_GAME_LOD_LOW; i--)
					{
						LODPresetInfo *preset=&m_lodPresets[i][0];	//pointer to first preset at this LOD level.
						for (Int j=0; j<m_numLevelPresets[i]; j++)
						{
							if(	prof->m_cpuType == preset->m_cpuType &&	((Real)prof->m_mhz/(Real)preset->m_mhz >= PROFILE_ERROR_LIMIT))
							{	currentLevel = (StaticGameLODLevel)i;
								m_cpuType = prof->m_cpuType;
								m_cpuFreq = prof->m_mhz;
								break;
							}
							preset++;	//skip to next preset
						}
						if (currentLevel >= i)
							break;	//we already found a higher level than the remaining presets so no need to keep searching.
					}
				}
				prof++;
			}
		}	//finding equivalent CPU to unkown cpu.
	}	//find data needed to determine m_idealDetailLevel

	if (userSetDetail == STATIC_GAME_LOD_CUSTOM)
	{
		TheWritableGlobalData->m_textureReductionFactor = optionPref.getTextureReduction();
		TheWritableGlobalData->m_useShadowVolumes = optionPref.get3DShadowsEnabled();
		TheWritableGlobalData->m_useShadowDecals = optionPref.get2DShadowsEnabled();
		TheWritableGlobalData->m_enableBehindBuildingMarkers = optionPref.getBuildingOcclusionEnabled();
		TheWritableGlobalData->m_maxParticleCount = optionPref.getParticleCap();
		TheWritableGlobalData->m_enableDynamicLOD = optionPref.getDynamicLODEnabled();
		TheWritableGlobalData->m_useFpsLimit = optionPref.getFPSLimitEnabled();
		TheWritableGlobalData->m_useLightMap = optionPref.getLightmapEnabled();
		TheWritableGlobalData->m_showSoftWaterEdge = optionPref.getSmoothWaterEnabled();
		TheWritableGlobalData->m_useHeatEffects = optionPref.getUseHeatEffects();
		TheWritableGlobalData->m_useDrawModuleLOD = optionPref.getExtraAnimationsDisabled();
		TheWritableGlobalData->m_useTreeSway = optionPref.getTreeSwayEnabled();
		TheWritableGlobalData->m_useTrees = optionPref.getTreesEnabled();
	}

	// Camera zoom-out is deliberately not settable here: how much of the battlefield you can see
	// is a competitive advantage, so it stays at the GameData.ini value for everyone.

	// TheOptionCatalog is not read here.  It is read in GlobalData::parseGameDataDefinition, which
	// runs while TheWritableGlobalData is being constructed and therefore before parseCommandLine;
	// this function runs after it, and anything it wrote would overrule the command line.

	setStaticLODLevel(userSetDetail);
}

/** The detail preset whose step on the quality slider is this one or the nearest one below it. */
static StaticGameLODLevel graphicsQualityLevelAtOrBelow(Int step)
{
	Int level = STATIC_GAME_LOD_ULTRA;
	while (TheGraphicsQualityAnchors[level] > step)
		--level;
	return (StaticGameLODLevel)level;
}

void GameLODManager::refreshCustomStaticLODLevel(void)
{
	StaticGameLODInfo *lodInfo=&m_staticGameLODInfo[STATIC_GAME_LOD_CUSTOM];

	lodInfo->m_maxParticleCount=TheGlobalData->m_maxParticleCount;
	// the ring has no control on the pages, so a Custom mix takes it from the preset at or below the quality slider's step
	lodInfo->m_gpuParticleCapacity=m_staticGameLODInfo[graphicsQualityLevelAtOrBelow(TheGlobalData->m_graphicsQuality)].m_gpuParticleCapacity;
	lodInfo->m_useShadowVolumes=TheGlobalData->m_useShadowVolumes;
	lodInfo->m_useShadowDecals=TheGlobalData->m_useShadowDecals;
	lodInfo->m_useCloudMap=TheGlobalData->m_useCloudMap;
	lodInfo->m_useLightMap=TheGlobalData->m_useLightMap;
	lodInfo->m_showSoftWaterEdge=TheGlobalData->m_showSoftWaterEdge;
	lodInfo->m_maxTankTrackEdges=TheGlobalData->m_maxTankTrackEdges;
	lodInfo->m_maxTankTrackOpaqueEdges=TheGlobalData->m_maxTankTrackOpaqueEdges;
	lodInfo->m_maxTankTrackFadeDelay=TheGlobalData->m_maxTankTrackFadeDelay;
	lodInfo->m_useBuildupScaffolds=!TheGlobalData->m_useDrawModuleLOD;
	lodInfo->m_useHeatEffects = TheGlobalData->m_useHeatEffects;
	lodInfo->m_useTreeSway=TheGlobalData->m_useTreeSway;
	lodInfo->m_textureReduction=TheGlobalData->m_textureReductionFactor;
	lodInfo->m_useFpsLimit = TheGlobalData->m_useFpsLimit;
	lodInfo->m_enableDynamicLOD=TheGlobalData->m_enableDynamicLOD;
	lodInfo->m_useTrees = TheGlobalData->m_useTrees;

}

/**Convert LOD name to an index*/
Int GameLODManager::getStaticGameLODIndex(AsciiString name)
{
	for (Int i=0; i<STATIC_GAME_LOD_COUNT; ++i)
	{
		if (name.compareNoCase(StaticGameLODNames[i]) == 0)
			return i;
	}

	DEBUG_CRASH(( "GameLODManager::getGameLODIndex - Invalid LOD name '%s'\n", name.str() ));
	return STATIC_GAME_LOD_UNKNOWN;
}

/**Parse a description of all the LOD settings for a given detail level*/
void INI::parseStaticGameLODDefinition( INI* ini )
{
	const char *c;
	AsciiString name;

	// read the name
	c = ini->getNextToken();
	name.set( c );	

	if( TheGameLODManager )
	{
		Int index = TheGameLODManager->getStaticGameLODIndex(name);
		if (index != STATIC_GAME_LOD_UNKNOWN)
		{
			StaticGameLODInfo *lodInfo = &(TheGameLODManager->m_staticGameLODInfo[index]);

			// parse the ini definition
			ini->initFromINI( lodInfo, TheStaticGameLODFieldParseTable );
		}
	}
}

/**Parse an LOD level*/
void INI::parseStaticGameLODLevel( INI* ini, void * , void *store, const void*)
{
	const char *tok=ini->getNextToken();
	for (Int i=0; i<STATIC_GAME_LOD_COUNT; i++)
		if( strcasecmp(tok, StaticGameLODNames[i]) == 0 )
		{	*(StaticGameLODLevel*)store = (StaticGameLODLevel)i;
			return;
		}

	DEBUG_CRASH(("invalid GameLODLevel token %s -- expected LOW/MEDIUM/HIGH\n",tok));
	throw INI_INVALID_DATA;
}

const char *GameLODManager::getStaticGameLODLevelName(StaticGameLODLevel level)
{
	return StaticGameLODNames[level];
}

/**Function which calculates the recommended LOD level for current hardware
configuration.*/
StaticGameLODLevel GameLODManager::findStaticLODLevel(void)
{
	//Check if we have never done the test on current system
	if (m_idealDetailLevel == STATIC_GAME_LOD_UNKNOWN)
	{
		//search all our presets for matching hardware
		m_idealDetailLevel = STATIC_GAME_LOD_LOW;

		//get system configuration - only need vide chip type, got rest in ::init().
		testMinimumRequirements(&m_videoChipType,NULL,NULL,NULL,NULL,NULL,NULL);
		if (m_videoChipType == DC_UNKNOWN)
			m_videoChipType = DC_TNT2;	//presume it's at least TNT2 level

		Int numMBRam=m_numRAM/(1024*1024);

		for (Int i=STATIC_GAME_LOD_HIGH; i >= STATIC_GAME_LOD_LOW; i--)
		{
				LODPresetInfo *preset=&m_lodPresets[i][0];	//pointer to first preset at this LOD level.
				for (Int j=0; j<m_numLevelPresets[i]; j++)
				{

					if(	m_cpuType == preset->m_cpuType &&
							((Real)m_cpuFreq/(Real)preset->m_mhz >= PROFILE_ERROR_LIMIT) &&//make sure we're within 5% or higher
							m_videoChipType >= preset->m_videoType &&
							((Real)numMBRam/(Real)preset->m_memory >= PROFILE_ERROR_LIMIT)
						)
					{	m_idealDetailLevel = (StaticGameLODLevel)i;
						break;
					}

					preset++;	//skip to next preset

				}
				if (m_idealDetailLevel >= i)
					break;	//we already found a higher level than the remaining presets so no need to keep searching.
		}
		//Save ideal detail level for future usage
		OptionPreferences optionPref;
		optionPref["IdealStaticGameLOD"] = getStaticGameLODLevelName(m_idealDetailLevel);
		if (getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)	//save for future usage.
			optionPref["StaticGameLOD"] = getStaticGameLODLevelName(m_idealDetailLevel);
		optionPref.write();
	}

	return m_idealDetailLevel;
}

/**Set all game systems to match the desired LOD level.*/
Bool GameLODManager::setStaticLODLevel(StaticGameLODLevel level)
{
	if (!TheGlobalData->m_enableStaticLOD)
	{	m_currentStaticLOD = STATIC_GAME_LOD_CUSTOM; 
		return FALSE;
	}

	if (level == STATIC_GAME_LOD_UNKNOWN || (level != STATIC_GAME_LOD_CUSTOM && m_currentStaticLOD == level))
		return FALSE;	//level is already applied.  Custom levels are always applied since random options could change.

	applyStaticLODLevel(level);
	m_currentStaticLOD = level;

	return TRUE;
}

void GameLODManager::applyStaticLODLevel(StaticGameLODLevel level)
{
///@todo: Still need to implement these settings:
//	m_sampleCount2D=6;
//	m_sampleCount3D=24;
//	m_streamCount=2;
//	m_useEmissiveNightMaterials=TRUE;

	//save previous info for this level since it may be overwritten by refreshCustomStaticLODLevel().
	StaticGameLODInfo prevLodBackup;
	if (m_currentStaticLOD != STATIC_GAME_LOD_UNKNOWN)
		prevLodBackup=m_staticGameLODInfo[m_currentStaticLOD];

	if (level == STATIC_GAME_LOD_CUSTOM)
		refreshCustomStaticLODLevel();	//store current settings into custom preset

	StaticGameLODInfo *lodInfo=&m_staticGameLODInfo[level];
	StaticGameLODInfo *prevLodInfo=&prevLodBackup;

	Int requestedTextureReduction = 0;
	Bool requestedTrees = m_memPassed;	//only use trees if memory requirement passed.
	if (level == STATIC_GAME_LOD_CUSTOM)
	{	requestedTextureReduction = lodInfo->m_textureReduction;
		requestedTrees = lodInfo->m_useTrees;
	}
	else
	if (level >= STATIC_GAME_LOD_LOW)
	{
		// The level the player picked decides the texture resolution.  It used to come from the
		// machine benchmark instead, and that benchmark answers "low" on any modern machine - so
		// choosing High still gave low resolution textures and nothing in the menu could raise
		// them.  Under 256 MB is still forced down, which is what the memory test is for.
		requestedTextureReduction = m_memPassed
			? getLevelTextureReduction(level)
			: getLevelTextureReduction(STATIC_GAME_LOD_LOW);
	}

	if (TheGlobalData)
	{
		TheWritableGlobalData->m_maxParticleCount=lodInfo->m_maxParticleCount;
		TheWritableGlobalData->m_gpuParticleCapacity=lodInfo->m_gpuParticleCapacity;
		TheWritableGlobalData->m_useShadowVolumes=lodInfo->m_useShadowVolumes;
		TheWritableGlobalData->m_useShadowDecals=lodInfo->m_useShadowDecals;

		//Check if texture resolution changed.  No need to apply when current is unknown because display will do it
		if (requestedTextureReduction != m_currentTextureReduction)
		{
				TheWritableGlobalData->m_textureReductionFactor = requestedTextureReduction;
				if (TheGameClient)
					TheGameClient->adjustLOD(0);	//apply the new setting stored in globaldata
		}

		//Check if shadow state changed
		if (m_currentStaticLOD == STATIC_GAME_LOD_UNKNOWN	||
			lodInfo->m_useShadowVolumes != prevLodInfo->m_useShadowVolumes ||
			lodInfo->m_useShadowDecals != prevLodInfo->m_useShadowDecals)
		{
			if (TheGameClient)
			{
				TheGameClient->releaseShadows();	//free all shadows
				TheGameClient->allocateShadows();	//allocate those shadows that are enabled.
			}
		}

		// m_useCloudMap is not the preset's: cloud shadows are a catalog option of their own (OptionsCatalog.cpp)
		TheWritableGlobalData->m_useLightMap=lodInfo->m_useLightMap;
		TheWritableGlobalData->m_showSoftWaterEdge=lodInfo->m_showSoftWaterEdge;
		//Check if shoreline blending mode has changed
		if (m_currentStaticLOD == STATIC_GAME_LOD_UNKNOWN || lodInfo->m_showSoftWaterEdge != prevLodInfo->m_showSoftWaterEdge)
		{
			if (TheTerrainVisual)
				TheTerrainVisual->setShoreLineDetail();
		}

		TheWritableGlobalData->m_maxTankTrackEdges=lodInfo->m_maxTankTrackEdges;
		TheWritableGlobalData->m_maxTankTrackOpaqueEdges=lodInfo->m_maxTankTrackOpaqueEdges;
		TheWritableGlobalData->m_maxTankTrackFadeDelay=lodInfo->m_maxTankTrackFadeDelay;
		TheWritableGlobalData->m_useTreeSway=lodInfo->m_useTreeSway;
		TheWritableGlobalData->m_useDrawModuleLOD=!lodInfo->m_useBuildupScaffolds;
		TheWritableGlobalData->m_useHeatEffects=lodInfo->m_useHeatEffects;
		TheWritableGlobalData->m_enableDynamicLOD = lodInfo->m_enableDynamicLOD;
		TheWritableGlobalData->m_useFpsLimit = lodInfo->m_useFpsLimit;
		TheWritableGlobalData->m_useTrees = requestedTrees;
	}
	if (!m_memPassed || isReallyLowMHz()) {
		TheWritableGlobalData->m_shellMapOn = false;
	}
	if (TheTerrainVisual)
		TheTerrainVisual->setTerrainTracksDetail();

}

/**Parse a description of all the LOD settings for a given detail level*/
void INI::parseDynamicGameLODDefinition( INI* ini )
{
	const char *c;
	AsciiString name;

	// read the name
	c = ini->getNextToken();
	name.set( c );	

	if( TheGameLODManager )
	{
		Int index = TheGameLODManager->getDynamicGameLODIndex(name);
		if (index != DYNAMIC_GAME_LOD_UNKNOWN)
		{
			DynamicGameLODInfo *lodInfo = &(TheGameLODManager->m_dynamicGameLODInfo[index]);

			// parse the ini weapon definition
			ini->initFromINI( lodInfo, TheDynamicGameLODFieldParseTable );
		}
	}
}

/**Parse an LOD level*/
void INI::parseDynamicGameLODLevel( INI* ini, void * , void *store, const void*)
{
	const char *tok=ini->getNextToken();
	for (Int i=0; i<DYNAMIC_GAME_LOD_COUNT; i++)
		if( strcasecmp(tok, DynamicGameLODNames[i]) == 0 )
		{	*(DynamicGameLODLevel*)store = (DynamicGameLODLevel)i;
			return;
		}

	DEBUG_CRASH(("invalid GameLODLevel token %s -- expected LOW/MEDIUM/HIGH\n",tok));
	throw INI_INVALID_DATA;
}

/**Convert LOD name to an index*/
Int GameLODManager::getDynamicGameLODIndex(AsciiString name)
{
	for (Int i=0; i<DYNAMIC_GAME_LOD_COUNT; ++i)
	{
		if (name.compareNoCase(DynamicGameLODNames[i]) == 0)
			return i;
	}

	DEBUG_CRASH(( "GameLODManager::getGameLODIndex - Invalid LOD name '%s'\n", name.str() ));
	return STATIC_GAME_LOD_UNKNOWN;
}

const char *GameLODManager::getDynamicGameLODLevelName(DynamicGameLODLevel level)
{
	return DynamicGameLODNames[level];
}

/**Given an average fps, return the optimal dynamic LOD level that matches this fps.*/
DynamicGameLODLevel GameLODManager::findDynamicLODLevel(Real averageFPS)
{
	Int ifps=(Int)(averageFPS);	//convert to integer.

	for (Int i=DYNAMIC_GAME_LOD_VERY_HIGH; i>=DYNAMIC_GAME_LOD_LOW; i--)
	{	//check which of the LOD levels matches our fps
		if (m_dynamicGameLODInfo[i].m_minFPS < ifps)
			return (DynamicGameLODLevel)i;
	}
	return DYNAMIC_GAME_LOD_LOW;	//none of the low levels were slow enough so pick the lowest.
}

/**Set all game systems to match the desired LOD level.*/
Bool GameLODManager::setDynamicLODLevel(DynamicGameLODLevel level)
{
	if (level == DYNAMIC_GAME_LOD_UNKNOWN || m_currentDynamicLOD == level)
		return FALSE;

	m_currentDynamicLOD = level;

	applyDynamicLODLevel(level);

	return TRUE;
}

void GameLODManager::applyDynamicLODLevel(DynamicGameLODLevel level)
{
	m_numParticleGenerations=0;
	m_dynamicParticleSkipMask=m_dynamicGameLODInfo[level].m_dynamicParticleSkipMask;

	/* DebrisSkipMask and SlowDeathScale are still parsed and no longer applied.  The dynamic level
		 follows this machine's frame rate, and logic reads both: the one decides whether a debris object
		 is created, the other how long a death takes.  The stock GameLOD.ini sets them to no effect at
		 every level, and the file is outside the multiplayer checksum, so a changed one split the match. */
	m_minDynamicParticlePriority=m_dynamicGameLODInfo[level].m_minDynamicParticlePriority;
	m_minDynamicParticleSkipPriority=m_dynamicGameLODInfo[level].m_minDynamicParticleSkipPriority;
}

Int GameLODManager::getRecommendedTextureReduction(void)
{
	if (m_idealDetailLevel == STATIC_GAME_LOD_UNKNOWN)
		findStaticLODLevel();	//it was never tested, so test now.

	if (!m_memPassed)	//if they have < 256 MB, force them to low res textures.
		return m_staticGameLODInfo[STATIC_GAME_LOD_LOW].m_textureReduction;

	return m_staticGameLODInfo[m_idealDetailLevel].m_textureReduction;
}

Int GameLODManager::getLevelTextureReduction(StaticGameLODLevel level)
{
	return m_staticGameLODInfo[level].m_textureReduction;
}

StaticGameLODInfo makeUltraStaticGameLOD( const StaticGameLODInfo &high )
{
	StaticGameLODInfo ultra = high;
	ultra.m_gpuParticleCapacity = TheGpuParticleCapacities[STATIC_GAME_LOD_ULTRA];
	return ultra;
}

static Int graphicsQualityValue( const char *control, Int step )
{
	return findGraphicsQualityRow( control )->values[ step ];
}

void applyGraphicsQualityStep( StaticGameLODInfo &info, Int step )
{
	// the texture slider runs low to high, the field counts halvings
	info.m_textureReduction = 2 - graphicsQualityValue( "LowResSlider", step );
	info.m_maxParticleCount = graphicsQualityValue( "ParticleCapSlider", step );
	info.m_useShadowVolumes = graphicsQualityValue( "Check3DShadows", step ) != 0;
	info.m_useShadowDecals = graphicsQualityValue( "Check2DShadows", step ) != 0;
	info.m_useLightMap = graphicsQualityValue( "CheckGroundLighting", step ) != 0;
	info.m_showSoftWaterEdge = graphicsQualityValue( "CheckSmoothWater", step ) != 0;
	info.m_useBuildupScaffolds = graphicsQualityValue( "CheckExtraAnimations", step ) != 0;
	info.m_useTreeSway = graphicsQualityValue( "CheckTreeSway", step ) != 0;
	info.m_useHeatEffects = graphicsQualityValue( "CheckHeatEffects", step ) != 0;
	// the box is "never lower detail", which is dynamic LOD off
	info.m_enableDynamicLOD = graphicsQualityValue( "CheckNoDynamicLOD", step ) == 0;
}

Bool GameLODManager::didMemPass( void )
{ 
	return m_memPassed;	
}
