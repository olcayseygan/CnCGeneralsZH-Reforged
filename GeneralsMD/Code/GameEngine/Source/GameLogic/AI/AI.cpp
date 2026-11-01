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

// AI.cpp
// The Artificial Intelligence system
// Author: Michael S. Booth, November 2000
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Common/CRCDebug.h"
#include "Common/GameState.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"

#include "GameLogic/AI.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIPlayer.h"		// for the per-frame AI profile the slow-frame report prints
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "Platform/MsvcFloatCasts.h"

extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE CLASS ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
void TAiData::addSideInfo(AISideInfo *infoToAdd) 
{
	infoToAdd->m_next = m_sideInfo;
	m_sideInfo = infoToAdd;
}

void TAiData::addFactionBuildList(AISideBuildList *buildList) 
{

	AISideBuildList *info = m_sideBuildLists;
	while (info) {
		if (buildList->m_side == info->m_side) {
			if (info->m_buildList)
				info->m_buildList->deleteInstance();
			info->m_buildList = buildList->m_buildList;
			buildList->m_buildList = NULL;
			buildList->m_next = NULL;
			buildList->deleteInstance();
			return;
		}
		info = info->m_next;
	}
	buildList->m_next = m_sideBuildLists;
	m_sideBuildLists = buildList;
}

const AISideInfo *TAiData::findSideInfo( const AsciiString &side, Int modIndex ) const
{
	const AsciiString own = AI::modSideKey( side, modIndex );
	for (const AISideInfo *info = m_sideInfo; info != NULL; info = info->m_next)
	{
		if (info->m_side == own)
			return info;
	}
	return modIndex >= 0 ? findSideInfo( side, -1 ) : NULL;
}

const AISideBuildList *TAiData::findBuildList( const AsciiString &side, Int modIndex ) const
{
	const AsciiString own = AI::modSideKey( side, modIndex );
	for (const AISideBuildList *build = m_sideBuildLists; build != NULL; build = build->m_next)
	{
		if (build->m_side == own)
			return build;
	}
	return modIndex >= 0 ? findBuildList( side, -1 ) : NULL;
}

const AISideInfo *TAiData::findSideInfo( const Player *player ) const
{
	// a map player whose faction is gone (an obsolete map) has no template, and plays retail's side
	const PlayerTemplate *pt = player->getPlayerTemplate();
	return findSideInfo( player->getSide(), pt != NULL ? pt->getModIndex() : -1 );
}

TAiData::~TAiData()
{
	AISideInfo *info = m_sideInfo;
	m_sideInfo = NULL;
	while (info) { 
		AISideInfo *cur = info;
		info = info->m_next;
		cur->deleteInstance();
	}

	AISideBuildList *build = m_sideBuildLists;
	m_sideBuildLists = NULL;
	while (build) { 
		AISideBuildList *cur = build;
		build = build->m_next;
		cur->deleteInstance();
	}

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE CLASS ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
AISideBuildList::AISideBuildList( AsciiString side ) : 
	m_side(side), 
	m_buildList(NULL), 
	m_next(NULL)
{
}

AISideBuildList::~AISideBuildList()
{
	if (m_buildList) {
		m_buildList->deleteInstance(); // note - deletes all in the list.
	}
	m_buildList = NULL;
}

void AISideBuildList::addInfo(BuildListInfo *info)
{
	// Add to the end of the list.
	if (m_buildList == NULL) {
		m_buildList = info;
	} else {
		BuildListInfo *cur = m_buildList;
		while (cur && cur->getNext()) {
			cur = cur->getNext();
		}
		DEBUG_ASSERTCRASH(cur && cur->getNext()==NULL, ("Logic error."));
		cur->setNextBuildList(info);
	}
	info->setNextBuildList(NULL); // should be at the end of the list.
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
static const FieldParse TheAIFieldParseTable[] = 
{
																	 
	{ "StructureSeconds",				INI::parseReal,NULL,		offsetof( TAiData, m_structureSeconds ) },
	{ "TeamSeconds",						INI::parseReal,NULL,		offsetof( TAiData, m_teamSeconds ) },
	{ "Wealthy",								INI::parseInt,NULL,			offsetof( TAiData, m_resourcesWealthy ) },
	{ "Poor",										INI::parseInt,NULL,		  offsetof( TAiData, m_resourcesPoor ) },
	{ "ForceIdleMSEC",					INI::parseDurationUnsignedInt,NULL,offsetof( TAiData, m_forceIdleFramesCount )	},
	{ "StructuresWealthyRate",	INI::parseReal,NULL,		offsetof( TAiData, m_structuresWealthyMod ) },
	{ "TeamsWealthyRate",				INI::parseReal,NULL,		offsetof( TAiData, m_teamWealthyMod ) },
	{ "StructuresPoorRate",			INI::parseReal,NULL,		offsetof( TAiData, m_structuresPoorMod ) },
	{ "TeamsPoorRate",					INI::parseReal,NULL,		offsetof( TAiData, m_teamPoorMod ) },
	{ "TeamResourcesToStart",		INI::parseReal,NULL,		offsetof( TAiData, m_teamResourcesToBuild ) },
	{ "GuardInnerModifierAI",		INI::parseReal,NULL,		offsetof( TAiData, m_guardInnerModifierAI ) },
	{ "GuardOuterModifierAI",		INI::parseReal,NULL,		offsetof( TAiData, m_guardOuterModifierAI ) },
	{ "GuardInnerModifierHuman",INI::parseReal,NULL,		offsetof( TAiData, m_guardInnerModifierHuman ) },
	{ "GuardOuterModifierHuman",INI::parseReal,NULL,		offsetof( TAiData, m_guardOuterModifierHuman ) },
	{ "GuardChaseUnitsDuration",				INI::parseDurationUnsignedInt,NULL,		offsetof( TAiData, m_guardChaseUnitFrames ) },
	{ "GuardEnemyScanRate",				INI::parseDurationUnsignedInt,NULL,		offsetof( TAiData, m_guardEnemyScanRate ) },
	{ "GuardEnemyReturnScanRate",				INI::parseDurationUnsignedInt,NULL,		offsetof( TAiData, m_guardEnemyReturnScanRate ) },
	{ "SkirmishGroupFudgeDistance",	INI::parseReal,NULL,		offsetof( TAiData, m_skirmishGroupFudgeValue ) },

	{ "RepulsedDistance",				INI::parseReal,NULL,		offsetof( TAiData, m_repulsedDistance ) },
	{ "EnableRepulsors",				INI::parseBool,NULL,		offsetof( TAiData, m_enableRepulsors ) },

	{	"AlertRangeModifier",			INI::parseReal,NULL,		offsetof( TAiData, m_alertRangeModifier)	},
	{	"AggressiveRangeModifier",INI::parseReal,NULL,		offsetof( TAiData, m_aggressiveRangeModifier)	},

	{ "ForceSkirmishAI",				INI::parseBool,NULL,		offsetof( TAiData, m_forceSkirmishAI ) },
	{ "RotateSkirmishBases",		INI::parseBool,NULL,		offsetof( TAiData, m_rotateSkirmishBases ) },

	{ "AttackUsesLineOfSight",	INI::parseBool,NULL,		offsetof( TAiData, m_attackUsesLineOfSight ) },
	{ "AttackIgnoreInsignificantBuildings",	INI::parseBool,NULL,		offsetof( TAiData, m_attackIgnoreInsignificantBuildings ) },

	
	{ "AttackPriorityDistanceModifier", INI::parseReal,NULL, offsetof( TAiData, m_attackPriorityDistanceModifier) },
 	{ "MaxRecruitRadius",				INI::parseReal,NULL,		offsetof( TAiData, m_maxRecruitDistance ) },
	{ "SkirmishBaseDefenseExtraDistance",	INI::parseReal,NULL,	offsetof( TAiData, m_skirmishBaseDefenseExtraDistance ) },

 	{ "WallHeight",							INI::parseReal,NULL,		offsetof( TAiData, m_wallHeight ) },

	{ "SideInfo",			AI::parseSideInfo,			NULL, NULL },

	{ "SkillLevel",		AI::parseSkillLevel,		NULL, NULL },


	{ "SkirmishBuildList",			AI::parseSkirmishBuildList,			NULL, NULL },


 	{ "MinInfantryForGroup",		INI::parseInt,NULL,			offsetof( TAiData, m_minInfantryForGroup ) },
 	{ "MinVehiclesForGroup",		INI::parseInt,NULL,			offsetof( TAiData, m_minVehiclesForGroup ) },

 	{ "MinDistanceForGroup",		INI::parseReal,NULL,			offsetof( TAiData, m_minDistanceForGroup ) },
 	{ "DistanceRequiresGroup",	INI::parseReal,NULL,			offsetof( TAiData, m_distanceRequiresGroup ) },
 	{ "MinClumpDensity",				INI::parseReal,NULL,			offsetof( TAiData, m_minClumpDensity ) },

 	{ "InfantryPathfindDiameter",		INI::parseInt,NULL,			offsetof( TAiData, m_infantryPathfindDiameter ) },
 	{ "VehiclePathfindDiameter",		INI::parseInt,NULL,			offsetof( TAiData, m_vehiclePathfindDiameter ) },
 	{ "RebuildDelayTimeSeconds",		INI::parseInt,NULL,			offsetof( TAiData, m_rebuildDelaySeconds ) },
 	{ "SupplyCenterSafeRadius",			INI::parseReal,NULL,			offsetof( TAiData, m_supplyCenterSafeRadius ) },

 	{ "AIDozerBoredRadiusModifier",	INI::parseReal,NULL,			offsetof( TAiData, m_aiDozerBoredRadiusModifier ) },
 	{ "AICrushesInfantry",	INI::parseBool,NULL,			offsetof( TAiData, m_aiCrushesInfantry ) },

 	{ "MaxRetaliationDistance",	INI::parseReal,NULL,			offsetof( TAiData, m_maxRetaliateDistance ) },
 	{ "RetaliationFriendsRadius",	INI::parseReal,NULL,			offsetof( TAiData, m_retaliateFriendsRadius ) },


	{ NULL,					NULL,						NULL,						0 }  // keep this last

};

void AI::parseSideInfo(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	const char* c = ini->getNextToken();
	AsciiString side(c);

	static const FieldParse myFieldParse[] = 
		{
			{ "ResourceGatherersEasy",				INI::parseInt,						NULL, offsetof( AISideInfo, m_easy ) },
			{ "ResourceGatherersNormal",			INI::parseInt,						NULL, offsetof( AISideInfo, m_normal ) },
      { "ResourceGatherersHard",				INI::parseInt,						NULL, offsetof( AISideInfo, m_hard ) },
      { "BaseDefenseStructure1",				INI::parseAsciiString,		NULL, offsetof( AISideInfo, m_baseDefenseStructure1 ) },
			{ "Spotter",											INI::parseAsciiString,		NULL, offsetof( AISideInfo, m_spotter ) },
			{ "Artillery",										INI::parseAsciiStringVector,	NULL, offsetof( AISideInfo, m_artillery ) },
			{ "SkillSet1",										AI::parseSkillSet,				NULL, offsetof( AISideInfo, m_skillSet1 ) },
			{ "SkillSet2",										AI::parseSkillSet,				NULL, offsetof( AISideInfo, m_skillSet2 ) },
			{ "SkillSet3",										AI::parseSkillSet,				NULL, offsetof( AISideInfo, m_skillSet3 ) },
			{ "SkillSet4",										AI::parseSkillSet,				NULL, offsetof( AISideInfo, m_skillSet4 ) },
			{ "SkillSet5",										AI::parseSkillSet,				NULL, offsetof( AISideInfo, m_skillSet5 ) },
			{ NULL,							NULL,											NULL, 0 }  // keep this last
		};

	AISideInfo *resourceInfo = ((TAiData*)instance)->m_sideInfo;
	while (resourceInfo) {
		if (side == resourceInfo->m_side) {
			break;
		}
		resourceInfo = resourceInfo->m_next;
	}
	if (resourceInfo==NULL) 
	{
		resourceInfo = newInstance(AISideInfo);
		((TAiData*)instance)->addSideInfo(resourceInfo);
	}
	resourceInfo->m_side = side;
	ini->initFromINI(resourceInfo, myFieldParse);

}

void AI::parseSkillSet(INI *ini, void *instance, void* store, const void* /*userData*/)
{
	static const FieldParse myFieldParse[] = 
		{
			{ "Science",											AI::parseScience,					NULL, NULL },
			{ NULL,							NULL,											NULL, 0 }  // keep this last
		};

	TSkillSet *skillset = ((TSkillSet*)store);
	skillset->m_numSkills = 0;
	ini->initFromINI(store, myFieldParse);
}

void AI::parseScience(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	TSkillSet *skillset = ((TSkillSet*)instance);
	if (skillset->m_numSkills>=MAX_AI_UPGRADES) {
#ifdef DEBUG_CRASHING
		const char* c = ini->getNextToken();
		DEBUG_CRASH(("Too many SCIENCE skills in skillset. Skill = %s, max is %d", c, MAX_AI_UPGRADES));
#endif
		return;
	}
	skillset->m_skills[skillset->m_numSkills] = SCIENCE_INVALID;
	INI::parseScience(ini, instance, skillset->m_skills+skillset->m_numSkills, NULL);
	ScienceType science = skillset->m_skills[skillset->m_numSkills];
	if (science != SCIENCE_INVALID) {
		if (TheScienceStore->getSciencePurchaseCost(science)==0) {
			DEBUG_CRASH(("Science %s is not purchaseable, can't be bought.", 
				TheScienceStore->getInternalNameForScience(science).str()));
			return;
		}
		skillset->m_numSkills++;
	}
}

void AI::parseSkirmishBuildList(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	const char* c = ini->getNextToken();
	AsciiString faction(c);

	static const FieldParse myFieldParse[] = 
		{
			{ "Structure",			BuildListInfo::parseStructure,			NULL, NULL },
			{ NULL,							NULL,											NULL, 0 }  // keep this last
		};

	AISideBuildList *build = newInstance(AISideBuildList)(faction);	
	ini->initFromINI(build, myFieldParse);
	((TAiData*)instance)->addFactionBuildList(build);

}

//--------------------------------------------------------------------------------------------------------

/// The AI system singleton
AI *TheAI = NULL;


/**
 * Constructor for the AI system
 */
AI::AI( void )
{
	m_aiData = NEW TAiData;
	m_pathfinder = NEW Pathfinder;
	m_nextFormationID = NO_FORMATION_ID;
	m_lastBuildingPlacementFrame = 0;
}

/**
 * Initialize the AI system
 */
void AI::init( void )
{
	m_nextGroupID = 0;
	m_sweptGroupID = 0;
}

#ifdef DEBUG_LOGGING
static Int theSweptGroups = 0;		///< temporary: groups sweepEmptyGroups destroyed this match
#endif

/**
 * Reset the AI system in preparation for a new map
 */
void AI::reset( void )
{
#ifdef DEBUG_LOGGING
	theSweptGroups = 0;
#endif
	m_pathfinder->reset();
	while (m_aiData && m_aiData->m_next) {
		TAiData *cur = m_aiData;
		m_aiData = m_aiData->m_next;
		delete cur;
	}
	while (m_groupList.size())
	{
		AIGroup *groupToRemove = m_groupList.front();
		if (groupToRemove)
		{
			destroyGroup(groupToRemove);
		}
		else
		{
			m_groupList.pop_front(); // NULL group, just kill from list.  Shouldn't really happen, but just in case.
		}
	}
	m_nextGroupID = 0;
	m_sweptGroupID = 0;
	m_nextFormationID = NO_FORMATION_ID;
	getNextFormationID(); // increment once past NO_FORMATION_ID.  jba.
	m_lastBuildingPlacementFrame = 0;
}

Bool AI::claimBuildingPlacement( UnsignedInt frame )
{
	if (m_lastBuildingPlacementFrame == frame && frame != 0)
		return FALSE;
	m_lastBuildingPlacementFrame = frame;
	return TRUE;
}

/**
 * Update the AI system
 */
Int AI::s_enemyScans = 0;
Real AI::s_lastPathfindMS = 0.0f;
Real AI::s_lastPlayerUpdateMS = 0.0f;

/** Milliseconds between two performance counter readings. */
static Real aiElapsedMS( const Int64 &from, const Int64 &to )
{
	static Int64 freq = 0;
	if( freq == 0 )
		freq = Clock_Ticks_Per_Second();
	if( freq == 0 )
		return 0.0f;
	return (Real)( (double)(to - from) * 1000.0 / (double)freq );
}

void AI::update( void )
{
	Int64 start, afterPathfind, end;
	start = Clock_Ticks();

	// Age the flow maps before anything reads them: the traffic left by last frame's jams decays,
	// and a clearance field made stale by a building going up is rebuilt at most once a second.
	m_pathfinder->decayTraffic();
	m_pathfinder->updateClearanceIfDirty();

	// Do pathfinding.
	m_pathfinder->processPathfindQueue();

	afterPathfind = Clock_Ticks();

	// run player updates
	{
		AIPlayer::resetFrameProfile();
		ThePlayerList->UPDATE();
	}

	end = Clock_Ticks();
	s_lastPathfindMS = aiElapsedMS( start, afterPathfind );
	s_lastPlayerUpdateMS = aiElapsedMS( afterPathfind, end );

	sweepEmptyGroups();
}

/**
 * Destroy the AI system
 */
AI::~AI()
{
	if (m_pathfinder) {
		delete m_pathfinder;
	}
	m_pathfinder = NULL;
	while (m_aiData) 
	{
		TAiData *cur = m_aiData;
		m_aiData = m_aiData->m_next;
		delete cur;
	}
}


void AI::newOverride(void)
{
	TAiData *cur = m_aiData;
	m_aiData = NEW TAiData;
	*m_aiData = *cur;
	m_aiData->m_sideInfo = NULL;
	AISideInfo *info = cur->m_sideInfo;
	while (info) {
		AISideInfo *newInfo = newInstance(AISideInfo);
		*newInfo = *info;
		newInfo->m_next = NULL;
		addSideInfo(newInfo);
		info = info->m_next;
	}									
	m_aiData->m_sideBuildLists = NULL;
	AISideBuildList *build = cur->m_sideBuildLists;
	while (build) {
		AISideBuildList *newbuild = newInstance(AISideBuildList)(build->m_side);
		newbuild->m_next = NULL;
		newbuild->m_buildList = build->m_buildList->duplicate();
		m_aiData->addFactionBuildList(newbuild);
		build = build->m_next;
	}
	m_aiData->m_next = cur;
}

void AI::addSideInfo(AISideInfo *infoToAdd)
{
	m_aiData->addSideInfo(infoToAdd);

}

//-------------------------------------------------------------------------------------------------
/** Parse GameData entry */
//-------------------------------------------------------------------------------------------------
void AI::parseAiDataDefinition( INI* ini )
{
	if( TheAI )
	{

		// 
		// if the type of loading we're doing creates override data, we need to
		// be loading into a new override item
		//
		if( ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES )
			TheAI->newOverride();

	}  // end if

	/* -modfactions: a mod's pass (INI::loadModPasses) takes only its sides' entries, filed under
		 "M<n>~<side>" with its names already prefixed, so its factions build and train its own things.
		 The tuning in the rest of the block stays the fork's, mods fit the fork. */
	const Int mod = TheModNamespace.getActive();
	if (mod >= 0)
	{
		TAiData *modData = NEW TAiData;
		// the mod's AIData.ini goes over its Default\AIData.ini: what the first filed seeds this parse, so a
		// SideInfo field the second leaves out keeps the first's value, as retail's two files merge
		const AsciiString prefix = modSideKey( AsciiString::TheEmptyString, mod );
		for (AISideInfo **link = &TheAI->m_aiData->m_sideInfo; *link != NULL; )
		{
			AISideInfo *info = *link;
			if (!info->m_side.startsWith( prefix ))
			{
				link = &info->m_next;
				continue;
			}
			*link = info->m_next;
			const AsciiString side( info->m_side.str() + prefix.getLength() );
			info->m_side = side;
			modData->addSideInfo( info );
		}
		ini->initFromINI( modData, TheAIFieldParseTable );
		Int sides = 0;
		while (modData->m_sideInfo != NULL)
		{
			AISideInfo *info = modData->m_sideInfo;
			modData->m_sideInfo = info->m_next;
			info->m_side = modSideKey( info->m_side, mod );
			TheAI->m_aiData->addSideInfo( info );
			++sides;
		}
		Int buildLists = 0;
		while (modData->m_sideBuildLists != NULL)
		{
			AISideBuildList *build = modData->m_sideBuildLists;
			modData->m_sideBuildLists = build->m_next;
			build->m_side = modSideKey( build->m_side, mod );
			TheAI->m_aiData->addFactionBuildList( build );
			++buildLists;
		}
		delete modData;
		DEBUG_LOG(( "MODNS M%d~ AIData from %s: %d sides, %d skirmish build lists\n", mod + 1, ini->getFilename().str(), sides, buildLists ));
		return;
	}

	// parse the ini weapon definition
	ini->initFromINI( TheAI->m_aiData, TheAIFieldParseTable );

}

AsciiString AI::modSideKey( const AsciiString &side, Int modIndex )
{
	if (modIndex < 0)
		return side;
	AsciiString key;
	key.format( "M%d~%s", modIndex + 1, side.str() );
	return key;
}




//--------------------------------------------------------------------------------------------------------
/**
 * Create a new AI Group
 */
AIGroup *AI::createGroup( void )
{
	// create a new instance
	AIGroup *group = newInstance(AIGroup);

	// add it to the list
//	DEBUG_LOG(("***AIGROUP %x is being added to m_groupList.\n", group ));
	m_groupList.push_back( group );
	m_groupIndex[ group ] = --m_groupList.end();

	return group;
}

/**
 * Destroy the given AI Group
 */
void AI::destroyGroup( AIGroup *group )
{
	std::unordered_map<AIGroup *, std::list<AIGroup *>::iterator>::iterator i = m_groupIndex.find( group );

	// make sure group is actually in the list: a caller can hold one that emptied and destroyed itself,
	// and the group's own destructor comes back here through remove()
	if (i == m_groupIndex.end())
		return;

	DEBUG_ASSERTCRASH(group != NULL, ("A NULL group made its way into the AIGroup list.. jkmcd"));

	// remove it
//	DEBUG_LOG(("***AIGROUP %x is being removed from m_groupList.\n", group ));
	m_groupList.erase( i->second );
	m_groupIndex.erase( i );

	// destroy group
	group->deleteInstance();
}

/**
 * Destroy every group made since the last sweep that is still empty.
 *
 * remove() destroys a group when its last member leaves, so a group that never had a member was
 * never destroyed at all: a sequential script or a script action asking a team with nobody left in
 * it whether it is idle (ScriptEngine::evaluateAndProgressAllSequentialScripts, the ScriptActions
 * team actions) left one behind every time, and each stayed in m_groupList, walked by the CRC every
 * frame, for the rest of the match.  Nothing keeps a group past the call that made it, so by the end
 * of the AI update an empty one is garbage.  Groups leave the list only by being destroyed and are
 * appended with rising IDs, so the ones made since the last sweep are its tail, and one that had a
 * member once is destroyed by remove() the moment it empties.
 */
void AI::sweepEmptyGroups( void )
{
	std::list<AIGroup *>::iterator it = m_groupList.end();
	while (it != m_groupList.begin())
	{
		std::list<AIGroup *>::iterator prev = it;
		--prev;
		if ((*prev)->getID() <= m_sweptGroupID)
			break;
		it = prev;
	}
	while (it != m_groupList.end())
	{
		AIGroup *group = *it;
		++it;
		if (group->isEmpty())
		{
			destroyGroup( group );
#ifdef DEBUG_LOGGING
			++theSweptGroups;
#endif
		}
	}
	m_sweptGroupID = m_nextGroupID;

#ifdef DEBUG_LOGGING
	// temporary: what the list holds once a minute, and how many groups it used to keep by now
	if (TheGameLogic->getFrame() % (60 * LOGICFRAMES_PER_SECOND) == 0)
		DEBUG_LOG(("AI GROUPS frame %d: %d listed, %d empty swept this match\n", TheGameLogic->getFrame(),
			(Int)m_groupList.size(), theSweptGroups));
#endif
}

/**
 * Given an ID, return the associated AIGroup
 */
AIGroup *AI::findGroup( UnsignedInt id )
{
	/// @todo Optimize this (MSB)
	std::list<AIGroup *>::iterator i;
	for( i=m_groupList.begin(); i!=m_groupList.end(); ++i )
		if ((*i)->getID() == id)
			return (*i);

	return NULL;
}

//--------------------------------------------------------------------------------------------------------
/**
 * Get the next formation id.
 */
FormationID AI::getNextFormationID(void )
{
	FormationID nextVal = m_nextFormationID;
	m_nextFormationID = (FormationID) (nextVal+1);
	return nextVal;
}

//-----------------------------------------------------------------------------
class PartitionFilterLiveMapEnemies : public PartitionFilter
{
private:
	const Object *m_obj;
public:
	PartitionFilterLiveMapEnemies(const Object *obj) : m_obj(obj) { }

	virtual Bool allow(Object *objOther)
	{
		// this is way fast (bit test) so do it first.
		if (objOther->isEffectivelyDead())
			return false;

		// this is also way fast (bit test) so do it next.
		if (objOther->isOffMap() != m_obj->isOffMap())
			return false;

		Relationship r = m_obj->getRelationship(objOther);
		if (r != ENEMIES)
			return false;

		return true;
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual const char* debugGetName() { return "PartitionFilterLiveMapEnemies"; }
#endif
};

//-----------------------------------------------------------------------------
class PartitionFilterWithinAttackRange : public PartitionFilter
{
private:
	const Object* m_obj;
public:
	PartitionFilterWithinAttackRange(const Object* obj) : m_obj(obj) { }

	virtual Bool allow(Object* objOther)
	{
		for (Int i = 0; i < WEAPONSLOT_COUNT;	i++ )
		{
			// ignore empty slots.
			const Weapon* w = m_obj->getWeaponInWeaponSlot((WeaponSlotType)i);
			if (w == NULL)
				continue;

			// a weapon that cannot target this kind of thing does not bring it into range
			if ((w->getAntiMask() & WeaponSet::getVictimAntiMask(objOther)) == 0)
				continue;

			// a garrisoned soldier idles at the middle of the building and shoots from its edge
			if (w->isWithinAttackRangeFromFirePoint(m_obj, objOther, objOther->getPosition()))
			{
				return true;
			}
		}
		return false;
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual const char* debugGetName() { return "PartitionFilterWithinAttackRange"; }
#endif
};


typedef struct 
{
	Int priority;
	const AttackPriorityInfo *info;
} TPriorityInfo;

static void priorityFunc(Object *obj, void *userData)
{
	TPriorityInfo *dp;
	dp = (TPriorityInfo*)userData;
	Int curPriority = dp->info->getPriority(obj->getTemplate());
	if (curPriority>dp->priority) {
		dp->priority = curPriority;
	}
}


//-----------------------------------------------------------------------------
/**
 * How much an enemy is worth shooting before the others, with no Object involved so
 * test_gameengine can link straight to it - the same trick as AIAttackMove_shouldRetaliate
 * in AIStates.cpp.
 *
 * Attack move used to hand out whatever the scan found first, which is the nearest thing:
 * a group ordered through an enemy base stopped for the first dozer it passed while the
 * artillery beside it kept firing. Ranking by threat fixes the order of business.
 *
 * 'ThreatValue' is the INI field the threat map already feeds on, so a mod that sets it
 * decides this outright; the shipped Zero Hour INI never sets it (it is absent from every
 * file in INIZH.big), so build cost stands in - EA's own price is the only per-unit measure
 * of dangerousness the shipped data has, and it tracks it well enough (an Overlord costs
 * five times a Humvee).
 *
 * Anything that can shoot back at *us* outranks anything that cannot, whatever it cost: a
 * Command Center is worth more than a Gattling tank and is still not what is killing us.
 * That is what AI_THREAT_ARMED_BONUS buys - it is larger than any build cost in the game, so
 * the two groups never interleave, and distance never moves a target out of its group either.
 *
 * Within a group distance does decide, because a threat we have to cross the field to reach is
 * not the one hurting us now: the worth of a target falls off with range, halving every
 * 'distanceModifier' world units away. That is the same knob the scripted attack priorities use
 * (AI INI's AttackPriorityDistanceModifier, 100.0 in the shipped data), so both notions of "worth
 * shooting" fade over the same distance. It is 0 until that INI is read - and the ctor default -
 * so a zero (or a negative, or a distance behind us) means no falloff at all rather than a
 * divide by zero.
 */
enum { AI_THREAT_ARMED_BONUS = 1000000 };		///< larger than any build cost in the game

Int AI_threatScore( Int templateThreatValue, Int buildCost, Bool threatensMe,
										Real dist, Real distanceModifier )
{
	Int score = (templateThreatValue > 0) ? templateThreatValue : buildCost;
	if (score < 1)
		score = 1;					// a free civilian car is still worth more than nothing at all

	if (distanceModifier > 0.0f && dist > 0.0f)
	{
		score = REAL_TO_INT( score * distanceModifier / (distanceModifier + dist) );
		if (score < 1)
			score = 1;				// however far off it is, it still beats finding nothing
	}

	if (threatensMe)
		score += AI_THREAT_ARMED_BONUS;

	return score;
}

//-----------------------------------------------------------------------------
/**
 * Whether a candidate of the scripted-priority search below takes the lead, Object-free so
 * test_gameengine can link to it.
 *
 * That search used to sort every enemy on the map near to far, a stable merge sort over the
 * gathered list, and let a candidate lead only on a higher priority after distance or an equal
 * one with a higher priority before it. So the pick was the best by those two, then the smaller
 * distance, then whichever the gathered list held first. One pass over the unsorted list that
 * compares on all of it and keeps the incumbent on a full tie picks the same Object. A hunting
 * unit asks for this over the whole map once a second, and the sort was 7% of its time.
 */
Bool AI_attackPriorityTakesLead( Int modPriority, Int curPriority, Real distSqr,
																 Int bestModPriority, Int bestCurPriority, Real bestDistSqr )
{
	if (modPriority != bestModPriority)
		return modPriority > bestModPriority;
	if (curPriority != bestCurPriority)
		return curPriority > bestCurPriority;
	return distSqr < bestDistSqr;
}

//-----------------------------------------------------------------------------
/**
 * Return the closest enemy, according to the qualifiers.
 */
static Bool AI_filtersAllow( PartitionFilter **filters, Object *candidate )
{
	for ( ; *filters; ++filters )
	{
		if (!(*filters)->allow(candidate))
			return false;
	}
	return true;
}

Object *AI::findClosestEnemy( const Object *me, Real range, UnsignedInt qualifiers,
														 const AttackPriorityInfo *info, PartitionFilter *optionalFilter)
{

	if ((qualifiers & CAN_ATTACK) && !me->isAbleToAttack())
	{
		/*
			PartitionFilterPossibleToAttack would filter out everything anyway,
			so just punt here.
		*/
		return NULL;
	}

	// counted, so the slow-frame log can say how many of the frame's range queries are this
	bumpEnemyScanCount();

	// only consider live, on-map enemies.
	// since this gets called a ton, I made a special custom filter to
	// combine several canned ones, in the name of speed (srj)
	PartitionFilterLiveMapEnemies filterObvious(me);

	PartitionFilterWithinAttackRange filterWithinAttackRange(me);

	// never target buildings (unless they can attack)
	PartitionFilterRejectBuildings filterBldgs(me);

	// and only stuff that isn't stealthed (and not detected)
	// (note that stealthed allies aren't hidden from us, but we're only looking for enemies here)
	// *** This doesn't cut it anymore. Bombtrucks can be disguised as an enemy member meaning we
	//     still want to acquire it -- however this old filter fails because the unit is stealthed.
	//PartitionFilterRejectByObjectStatus filterStealth(OBJECT_STATUS_STEALTHED, OBJECT_STATUS_DETECTED);
	// *** Use this new filter
	PartitionFilterStealthedAndUndetected filterStealth( me, false );

	// (optional) only stuff we can see.
	PartitionFilterLineOfSight	filterLOS(me);

	// (optional) only stuff we can attack
	PartitionFilterPossibleToAttack filterAttack(ATTACK_NEW_TARGET, me, CMD_FROM_AI);

	// (optional) only stuff that is significant
	PartitionFilterInsignificantBuildings filterInsignificant(true, false);
	
	// (optional) only stuff clear of fog
	PartitionFilterFreeOfFog filterFogged(me->getControllingPlayer()->getPlayerIndex());

	PartitionFilter *filters[16];
	Int numFilters = 0;

	// Important note: the filters are called in order, and once one rejects an object,
	// the remaining ones are not called, so you should endeavor to
	// arrange them such that the most-coarse (ie, the ones that will usually REJECT
	// the most objects) should come first.
	//
	// srj sez: I actually did profiling on USA04 (enabling FILTER_PROFILING in partition mgr)
	// to determine the order of these. some observations:
	//
	// -- filterTeam is BY FAR the best to put first (since most things near you tend to be nonenemies).
	// -- filterLOS tends to reject a very large number, but is computationally expensive
	// -- filterStealth is BY FAR the least common to be useful, so it goes last.
	// GS Fog check used to be inside can attack, so it feels right to be right after it

	filters[numFilters++] = &filterObvious;

	if( !(qualifiers & ATTACK_BUILDINGS) )
		filters[numFilters++] = &filterBldgs;

	if (qualifiers & WITHIN_ATTACK_RANGE)
		filters[numFilters++] = &filterWithinAttackRange;

	/* The two gathering searches below used to run every filter over every enemy in range and only
		 then pick one. A hunting unit searches the whole map, and the can-attack filter on a thousand
		 enemies was a single 30-47ms unit update in an eight-player match. They now gather with the
		 cheap filters above and ask the rest only of a candidate that would take the lead, which picks
		 the same enemy. */
	PartitionFilter *gatherFilters[16];
	const Int numGatherFilters = numFilters;
	for (Int f = 0; f < numGatherFilters; ++f)
		gatherFilters[f] = filters[f];
	gatherFilters[numGatherFilters] = NULL;

	if (qualifiers & CAN_SEE)
		filters[numFilters++] = &filterLOS;

	if (qualifiers & CAN_ATTACK)
		filters[numFilters++] = &filterAttack;

	if (qualifiers & UNFOGGED)
		filters[numFilters++] = &filterFogged;

	if (qualifiers & IGNORE_INSIGNIFICANT_BUILDINGS)
		filters[numFilters++] = &filterInsignificant;

	filters[numFilters++] = &filterStealth;

	if (optionalFilter) 
	{
		filters[numFilters++] = optionalFilter;
	}

	filters[numFilters] = NULL;

	// filterObvious goes first in both lists and lets only ENEMIES through, so a cell holding no
	// enemy player's Object cannot hold an answer and the searches below pass over it: the same
	// objects reach the filters in the same order, and no filter after the first ever sees the rest
	const PlayerMaskType enemyPlayers = me->getTeam()->getEnemyPlayerMask();

	if (info == NULL || info == TheScriptEngine->getDefaultAttackInfo())
	{
		if (qualifiers & PREFER_HIGH_THREAT)
		{
			// Biggest threat in range wins; the iteration is sorted near to far, so a strict
			// improvement keeps the nearest of equal threats - which is the whole priority
			// order the caller wants: what shoots us (its own check, before we are called),
			// then what is worth shooting, then what is closest.
			Real distanceModifier = TheAI->getAiData()->m_attackPriorityDistanceModifier;
			Object *bestThreat = NULL;
			Int bestScore = 0;
			ObjectIterator *threatIter = ThePartitionManager->iterateObjectsInRange(me, range, FROM_BOUNDINGSPHERE_2D, gatherFilters, ITER_SORTED_NEAR_TO_FAR, enemyPlayers);
			MemoryPoolObjectHolder threatHolder(threatIter);
			for (Object *theEnemy = threatIter->first(); theEnemy; theEnemy = threatIter->next())
			{
				Real dist = sqrt( ThePartitionManager->getDistanceSquared(me, theEnemy, FROM_BOUNDINGSPHERE_2D) );

				/* Score it as if it were armed first, which is the best this candidate can possibly
					 do: the armed bonus is the only thing getAbleToAttackSpecificObject decides here and
					 it only ever adds. If the best it can do does not beat what we already have, the
					 expensive question never has to be asked at all.

					 Same answer as asking every time, and most of the asking gone. This is where an
					 eight-player match spends its partition time - the list is every enemy in vision
					 range, sorted near to far, so the first candidate sets a high bar and the far ones
					 cannot clear it. */
				const Int armedScore = AI_threatScore( theEnemy->getTemplate()->getThreatValue(),
																		theEnemy->getTemplate()->calcCostToBuild( theEnemy->getControllingPlayer() ),
																		true, dist, distanceModifier );
				if (armedScore <= bestScore || !AI_filtersAllow(filters + numGatherFilters, theEnemy))
					continue;

				Bool threatensMe = theEnemy->isAbleToAttack();		// cheap bit test, and required before the call below
				if (threatensMe)
				{
					CanAttackResult r = theEnemy->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, me, CMD_FROM_AI );
					threatensMe = (r == ATTACKRESULT_POSSIBLE || r == ATTACKRESULT_POSSIBLE_AFTER_MOVING);
				}

				// the bonus is added last and nothing else in the score depends on it
				const Int score = threatensMe ? armedScore : (armedScore - AI_THREAT_ARMED_BONUS);
				if (score > bestScore)
				{
					bestScore = score;
					bestThreat = theEnemy;
				}
			}
			return bestThreat;
		}

		// No additional attack info, so just return the closest one.
		Object* o = ThePartitionManager->getClosestObject( me, range, FROM_BOUNDINGSPHERE_2D, filters, NULL, NULL, enemyPlayers );
		return o;
	}

	Object *bestEnemy = NULL;
	Int			effectivePriority=0;
	Int			actualPriority=0;
	Real		bestDistSqr=0.0f;
	// unsorted, see AI_attackPriorityTakesLead; each entry carries the distance the gather measured,
	// which is what getDistanceSquared would measure again
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(me, range, FROM_BOUNDINGSPHERE_2D, gatherFilters, ITER_FASTEST, enemyPlayers);
	MemoryPoolObjectHolder holder(iter);
	Real distSqr;
	for (Object *theEnemy = iter->firstWithNumeric(&distSqr); theEnemy; theEnemy = iter->nextWithNumeric(&distSqr))
	{
		Int curPriority = info->getPriority(theEnemy->getTemplate());
		if (curPriority == 0) 
			continue; // don't attack 0 priority targets.

		/* check for garrisoned buildings/vehicles & see if a higher priority unit is inside. */
		ContainModuleInterface* contain = theEnemy->getContain();
		if (contain) {
			TPriorityInfo priorityInfo;
			priorityInfo.priority = curPriority;
			priorityInfo.info = info;
			contain->iterateContained( priorityFunc, &priorityInfo, false ) ;
			if (priorityInfo.priority > curPriority) {
				curPriority = priorityInfo.priority;
			}
		}

		Real dist = sqrt(distSqr);
		Int modifier = floatToIntAsMsvc(dist/TheAI->getAiData()->m_attackPriorityDistanceModifier);
		Int modPriority = curPriority-modifier;
		if (modPriority < 1)
			modPriority = 1;
		if (AI_attackPriorityTakesLead(modPriority, curPriority, distSqr, effectivePriority, actualPriority, bestDistSqr)
			&& AI_filtersAllow(filters + numGatherFilters, theEnemy))
		{
			effectivePriority = modPriority;
			actualPriority = curPriority;
			bestDistSqr = distSqr;
			bestEnemy = theEnemy;
		}
	}	
	if (bestEnemy) {
		//DEBUG_LOG(("Find closest found %s, hunter %s, info %s\n", bestEnemy->getTemplate()->getName().str(), 
		//	me->getTemplate()->getName().str(), info->getName().str()));
	}
	return bestEnemy;
}




 /////////////////////////////
/**
 * Return the closest ally, according to the qualifiers.
 */
Object *AI::findClosestAlly( const Object *me, Real range, UnsignedInt qualifiers)
{
	// never target buildings (unless they can attack)
	PartitionFilterRejectBuildings		filterBldgs(me);

	// only consider allies.
	PartitionFilterRelationship	filterTeam(me, PartitionFilterRelationship::ALLOW_ALLIES);

	// and only stuff that is not dead
	PartitionFilterAlive filterAlive;

	// and on map (or not)
	PartitionFilterSameMapStatus filterMapStatus(me);

	// (optional) only stuff we can see.
	PartitionFilterLineOfSight	filterLOS(me);

	PartitionFilter *filters[16];
	Int numFilters = 0;

	filters[numFilters++] = &filterBldgs;
	filters[numFilters++] = &filterTeam;
	filters[numFilters++] = &filterAlive;
	filters[numFilters++] = &filterMapStatus;

	if (qualifiers & CAN_SEE)
		filters[numFilters++] = &filterLOS;

	filters[numFilters] = NULL;

	return ThePartitionManager->getClosestObject( me, range, FROM_BOUNDINGSPHERE_2D, filters );
}
/////////////////////////////



 /////////////////////////////
/**
 * Return the closest repulsor.
 */
Object *AI::findClosestRepulsor( const Object *me, Real range)
{

	if (!getAiData()->m_enableRepulsors) {
		return NULL;
	}

	// never target buildings (unless they can attack)
	PartitionFilterRepulsor		filter(me);

	// and only stuff that isn't stealthed (and not detected)
	// (note that stealthed allies aren't hidden from us, but that's ok. jba.)
	PartitionFilterRejectByObjectStatus filterStealth( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_STEALTHED ), 
																										 MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_DETECTED, OBJECT_STATUS_DISGUISED ) );

	PartitionFilter *filters[16];
	Int numFilters = 0;

	filters[numFilters++] = &filter;
	filters[numFilters++] = &filterStealth;
	filters[numFilters] = NULL;

	return ThePartitionManager->getClosestObject( me, range, FROM_BOUNDINGSPHERE_2D, filters );
}
/////////////////////////////

Real AI::getAdjustedVisionRangeForObject(const Object *object, Int factorsToConsider)
{
	Real originalRange = object->getVisionRange();
	const AIUpdateInterface *ai = object->getAI();
	const TAiData *aiData = TheAI->getAiData();

	if (!ai) 
	{
		DEBUG_CRASH(("Unit without AI ('%s') calling AI::getAdjustedVisionRangeForObject. Notify jkmcd.", object->getTemplate()->getName().str()));
		return 0.0f;
	}
	
	UnsignedInt moodMatrixVal = ai->getMoodMatrixValue();

	if (factorsToConsider & AI_VISIONFACTOR_OWNERTYPE) 
	{
		Bool playerIsHuman = (moodMatrixVal & MM_Controller_Player) != 0;

		if (playerIsHuman) 
		{
			if (factorsToConsider & AI_VISIONFACTOR_GUARDINNER) 
				originalRange *= aiData->m_guardInnerModifierHuman;
			else
				originalRange *= aiData->m_guardOuterModifierHuman;
		} 
		else 
		{
			if (factorsToConsider & AI_VISIONFACTOR_GUARDINNER)
				originalRange *= aiData->m_guardInnerModifierAI;
			else
				originalRange *= aiData->m_guardOuterModifierAI;
		}
	}

	if (object->getContainedBy() != NULL) 
	{
		originalRange = object->getLargestWeaponRange();
	} 
	else 
	{
		if ((factorsToConsider & AI_VISIONFACTOR_MOOD) && ((moodMatrixVal & MM_Controller_Player) == 0) ) 
		{
			switch(moodMatrixVal & MM_Mood_Bitmask)
			{
				case MM_Mood_Sleep:
					return 0.0f;

				case MM_Mood_Passive:
				case MM_Mood_Normal: 
					break;
				
				case MM_Mood_Alert: 
					originalRange *= TheAI->getAiData()->m_alertRangeModifier;
					break;

				case MM_Mood_Aggressive:
					originalRange *= TheAI->getAiData()->m_aggressiveRangeModifier;
					break;
			}
		}
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheGlobalData->m_debugVisibility) 
	{
		// ICK. This really nasty statement is used so that we only initialize this color once.
		// It should be exactly double the intensity of its targettable brother.
		static RGBColor theAdjustedVisionColor = {
			(TheGlobalData->m_debugVisibilityTargettableColor.red * 2 <= 1.0f ? 
				TheGlobalData->m_debugVisibilityTargettableColor.red * 2 :
				1.0f),
			(TheGlobalData->m_debugVisibilityTargettableColor.green * 2 <= 1.0f ? 
				TheGlobalData->m_debugVisibilityTargettableColor.green * 2 :
				1.0f),
			(TheGlobalData->m_debugVisibilityTargettableColor.blue * 2 <= 1.0f ? 
				TheGlobalData->m_debugVisibilityTargettableColor.blue * 2 :
				1.0f)
		};

		Vector3 pos(originalRange, 0, 0);
		for (int i = 0; i < TheGlobalData->m_debugVisibilityTileCount; ++i) 
		{
			pos.Rotate_Z(1.0f * i / TheGlobalData->m_debugVisibilityTileCount * 2 * PI);
			Coord3D coord = { pos.X + object->getPosition()->x, pos.Y + object->getPosition()->y, pos.Z + object->getPosition()->z };

			addIcon(&coord, TheGlobalData->m_debugVisibilityTileWidth, 
											TheGlobalData->m_debugVisibilityTileDuration, 
											theAdjustedVisionColor);
		}
	}
#endif
	return originalRange;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** The ladder, from AI-ROADMAP.md D6.  Three rungs, and each one is a sentence: Easy looks around
	* and answers late, Medium reacts in time and expands on its own, Brutal is the AI played as well
	* as it has been programmed to play.  A rung that plays wrong is read as a column here rather
	* than hunted through a switch(difficulty) somewhere.
	*
	* Brutal is the baseline, not a bonus: every number in its row is what the code does when nothing
	* is held back, and the two rungs below it are handicaps on the AI's *decisions*.
	*
	* Read the columns down, not across: nothing here is money, build speed, vision or unit stats.
	*
	*
	* Two columns were settled by measurement rather than by design, and both are worth reading
	* before touching them again:
	*
	*  - retreatTtkRatio started at the roadmap's 0.5-0.85.  That is a force quitting a fight it is
	*    very nearly winning, and it measured as one: turning retreat off entirely scored better
	*    than 0.85 did.  These are the "clearly losing" numbers, and at 0.5 retreat is worth about a
	*    win in twenty over having none.
	*
	*  - useInfluenceMapForAttackLane was FALSE everywhere after its first cut aimed at the enemy's
	*    most valuable visible cell: 7-9 over 32 matches against itself, twice the losses for half
	*    the kills, because aiming at the money means walking past the army.  It is on at the top
	*    again for the roadmap's actual proposal, AIPlayer::chooseApproachLabel: of Center, Flank and
	*    Backdoor, the approach with the least firepower this AI has seen along it.
	*
	* Easy and Medium were stepped toward that baseline on 2026-09-29, after Hard's extra buildings
	* opened a gap the two lower rungs could not cross.  Easy looks every 75 seconds and re-decides
	* every 7, and its trucks follow the piles; it still never retreats, never counters and never
	* hurries a spend.  Medium looks every 45 and re-decides every 3, counters at half weight, and
	* starts spending past 6000 instead of 10000.  Massing, team retreat, the attack lanes, defended
	* expansions, extra buildings and tactical fighting stay on Hard, and the two measured columns
	* above were left where the matches put them.
	*
	* gate, answer and massU came in on 2026-10-09 and none of them moved Hard against Hard: over 24
	* matches a condition on Twilight Flame, Winter Wolf and Tournament Desert, a side with all three
	* won 5 where the side without won 4, at the same value lost for value killed.  Against the
	* overlordrush scenario gate and answer together cut the sends into a fight the matchup loses
	* from 48% to 40% and stragglers from 17% to 14% of deaths; massU on top shortened the matches
	* and lost more for what it killed (0.70 against 0.64), so it stays off until it earns its place.
	*
	*
	* The last eight columns came in on 2026-10-10, on every rung.  bankS and floor are the bank each
	* rung keeps before it spends past its plan: Hard holds thirty seconds of its income, Medium a
	* minute, Easy two, and the floor is the whole target until an income is measured.  Below the
	* target the purchase queue saves for the best thing it cannot afford; above it nothing is saved
	* for and an upgrade needs half the payoff.  payoff and upgr are when an upgrade is worth its
	* price: what it improves has to be worth that many times the price, and the match has to be that
	* many seconds old.  nonc is what a building or an unarmed unit counts for there next to a fighting
	* unit.  share is how much of thirty seconds' income an idle factory may turn into extra units on
	* a pass with the bank over target, and maxF the most war factories, barracks and airfields the
	* rule puts up, one an income window while the bank is over target and not falling, or past fMult
	* times the target whatever it is doing (0 for never).
	*
	* The first cut counted buildings at full price, so Fortified Structure was every GLA's first
	* upgrade; spent every idle factory's worth of surplus on every rung, so Medium's peak army rose
	* from 67 to 99 and Hard against Medium went from 3-0 to 1-1; and sized production at a guessed 60
	* a second a factory, so a Hard AI earning 93 a second wanted the two factories it had.  The second
	* cut (share 0.10/0.25/1.00, maxF 3/4/8, the trend alone) took Fortified Structure off the top, but
	* Medium had idle factories, took 62 surplus units and four factories a match, peaked at 112 against
	* Hard's 112 and drew all six.  So Medium and Easy keep to their plan's two war factories and a
	* barracks (maxF 3, with the plan's unbuilt ones counted, so the rule adds none), and their share is
	* cut to 0.10 and 0.05: Medium's 62 surplus units at 0.25 become about 25, the 25 Easy took at 0.10
	* when its army was fine, and Easy's half of that keeps it under Medium.  Hard alone adds factories,
	* and also past twice its target whatever the trend (fMult).  With that Hard beat Medium 3-0 over
	* 45,000 frames and Medium beat Easy 4-0.
	*
	* Hard's hoard was never there in the mirror.  The batch read the bank as starting money plus earned
	* minus spent, and the score keeper's spent counts buildings and upgrades and never a unit: that sum
	* said 52,000 to 59,000 where the bank itself ended between 10 and 4,000, before this rule and after
	* it.  Against Medium it ended on 12,300 on average before and 1,600 after.  HEADLESS BANK prints the
	* bank itself now.
	*
	* Medium's own hoard was real.  On Twilight Flame its five supply centres earn 300 a second and more
	* by the middle of the match, and the plan's three factories with a tenth of it as surplus spent
	* little of that: six Medium China matches against Easy to frame 45,000 ended on 125,000 to 230,000
	* in the bank and Medium won one, the rest unfinished.  So Medium now takes a quarter of its surplus,
	* puts up factories to five once the bank is past three times its target, and its minimum wave is
	* 2,000 rather than 3,000.  The same six matches went 6-0 to Medium.  Hard against Medium stayed
	* mostly unfinished at 45,000 on both builds, 1-0 with 5 draws before and 0-1 with 5 after, so a
	* Hard that cannot close out Medium is still open.
	*
	* The last column, strikeE, came in on 2026-10-14: what a supply centre, black market, hacker,
	* drop zone, captured derrick, strategy centre or palace adds to a superweapon's or a general
	* power's score, in multiples of its price on top of the price.  Price alone and the threat weight
	* on armed units sent every strike at the army; a strike on the economy is the one a player feels
	* for the rest of the match.  Easy keeps price alone, Medium doubles it, Hard triples it.
	*
	* strikeT and swDef came in on 2026-10-20, from v3.1.0 players: the computer seldom built a
	* superweapon and spent the ones it built on their army.  strikeT is the threat weight that was a
	* flat 2 on every rung; Easy keeps it, Medium halves it, Hard drops it and looks at his army only when
	* none of his buildings is known.  strikeE now also covers factories, power plants and superweapons
	* (the last at the 1,000 floor, since a strike seldom kills one), and Medium's went from 1 to 2, Hard's
	* from 2 to 3.  swDef is the guns each superweapon asks for, 12 on every rung before: over twelve
	* four-player matches on Twilight Flame the median side peaked at 4 finished guns on Easy, 9 on Medium
	* and 18 on Hard, so Medium
	* built 3 superweapons in 48 sides over 25 minutes.  At 6 it built 36, and 29 of its 38 shots landed
	* mostly on buildings.  Hard's went from 14 of 33 shots mostly on buildings to 23 of 36, and against
	* Easy from 17 of 30 to 27 of 29.  Easy's 3 is unmeasured in play: its script clock still holds its
	* first superweapon past 25 minutes, so none was built either way.
	*
	* backlog, 2026-10-21: the window over which every factory of a kind has to be full (a barracks or
	* war factory busy, an airfield's parking taken while an aircraft waits for a place) on 80% of the
	* ten-second spend passes before another of that kind goes up, with no cap on how many stand.  It replaced a cap on production buildings (3, 5, 8)
	* and a bank rule, and a gate that waited for a unit queued behind the one being built, which the
	* scripts never queue: the computer stood one barracks, one war factory and one airfield all match.
	*
	* stall, lost, retry and stage, 2026-10-21: a building placed out at a dock in enemy ground stood
	* unbuilt all match, its money spent, every dozer sent to it killed on the way, and its spot taken from
	* every later expansion.  A site away from home that has not moved forward for stall seconds, or any
	* site whose builders died lost times with nothing built in between, is cancelled for its money back,
	* and its spot (and the dock it was for) left alone for retry seconds.  stage sends an idle dozer
	* ahead to the site the base builder is waiting on money for.  Easy gives up latest and does not stage.
	*
	* spot, 2026-10-22: Turkiye's Firtina and Sakarya see 100 and shoot 500 to 600, so they hit what
	* somebody else shows them.  America's scripts, which Turkiye runs, put a Tomahawk or a Paladin in a
	* team with no Pathfinder, and the guns walked out blind.  An attack team with no spotter now trains
	* this many of the side's (AISideInfo Spotter) the moment such a gun comes out for it, and waits for
	* them (AIPlayer::sendForSpotters): one for Easy and Medium, two for Hard, so that losing one does
	* not blind the gun.  Those scripts name a Tomahawk in no Easy team at all, in Medium's from the
	* third wave, about seventeen minutes in, and in Hard's from about seven, so arty, the share of the
	* attack teams with no gun that are given one of the side's own (AISideInfo Artillery), is all of
	* Easy's few, half of Medium's and a quarter of Hard's.  A side that names no Artillery or Spotter
	* draws nothing and plays as before.
	*
	* inflS to fireMem, 2026-10-30.  The owner: "bu ai lar artik su influence map i falan biraz ciddiye
	* alsinlar, hedef aliniyorlar hala devam ediyorlar ayni yerde."  Units shelled from past their sight
	* stood and took it on every rung, so every rung now reads the map and answers a hit (AIPlayer::
	* answerFire).  inflS is how old the map may be when read: Hard draws it on the clock every second as
	* before, and the rungs below draw it only when something asks, every two and every four seconds at
	* the most, so a quiet match costs them nothing.  fireLook is how often a rung looks at who is being hit:
	* Easy every three seconds.  A roll for the share of hits it noticed came out: hits come seconds apart, so
	* a miss threw away what may be the only look a tank gets, and every draw redrew the match.  word is how far the rest of the group goes with a unit hit: Easy's answer alone, one at a time, which is the
	* sloppy part, and Hard's whole group at once.  odds is what ours have to be worth against what covers
	* the gun before they go for it, with Hard the least willing to walk into a loss and Easy the most, as
	* with the odds at a known defence's edge.  fireMem is how long a gun it has lost sight of stays on its
	* map, and with it the ground it covers.
	*
	*                      scoutS maxSc decis  cntr  mass   ttk  indiv team infl focus save harv expand guard hoard econ micro  gate  answer massU  bankS  floor payoff upgr    nonc  share backlog push   minPow  siege  beam  strikeE strikeT swDef                     stall  lost retry  stage  spot arty    inflS fireLook word   odds   fireMem  watch watchOdds dodge */
static const AIDifficultyProfile s_defaultSkillLadder[ AISKILL_COUNT ] =
{
	/* Easy      */ { 75.0f, 1,  7.0f,  0.00f, FALSE, 0.00f, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,  FALSE, FALSE,     0, FALSE, FALSE, FALSE, FALSE, FALSE, 120.0f, 6000, 4.0f, 720.0f, 0.05f, 0.05f,  90.0f, 360.0f, 1500.0f,  80.0f, 0.00f, 0.0f, 2.0f, 3,                        300.0f, 3, 300.0f, FALSE, 1, 1.00f, 4.0f, 3.00f,   0.0f, 0.60f, 10.0f, 16.0f, 1.0f, 3.25f },
	/* Medium    */ { 45.0f, 1,  3.0f,  0.50f, FALSE, 0.35f, TRUE,  FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,  FALSE,  6000, FALSE, FALSE, FALSE, FALSE, FALSE,  60.0f, 5000, 2.5f, 420.0f, 0.05f, 0.25f,  75.0f, 300.0f, 2000.0f, 120.0f, 1.00f, 2.0f, 1.0f, 6,                        240.0f, 2, 240.0f, TRUE,  1, 0.50f, 2.0f, 1.00f, 150.0f, 0.80f, 20.0f,  6.0f, 1.5f, 1.50f },
	/* Hard      */ { 25.0f, 2,  1.5f,  1.00f, TRUE,  0.50f, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,   4000, TRUE,  TRUE,  TRUE,  TRUE,  FALSE,  30.0f, 4000, 1.5f, 180.0f, 0.05f, 1.00f,  45.0f, 240.0f, 4500.0f, 160.0f, 0.25f, 3.0f, 0.0f, DEFENSES_PER_SUPERWEAPON, 180.0f, 2, 180.0f, TRUE,  2, 0.25f, 1.0f, 0.33f, 300.0f, 1.00f, 40.0f,  2.0f, 2.0f, 0.33f }
};

//-------------------------------------------------------------------------------------------------
/** How the exchange is going: how long this force lasts, over how long it needs to finish what is
	* shooting at it.  Above 1 it is winning.
	*
	* The two degenerate cases are the ones that matter in practice.  Nothing is shooting at me: no
	* fight, never retreat, so a large number.  I cannot hurt what is shooting me: no exchange to
	* win at any health, so zero. */
//-------------------------------------------------------------------------------------------------
Real aiRetreatRatio( Real myHealth, Real myPower, Real enemyHealth, Real enemyPower )
{
	const Real NOT_A_FIGHT = 1000.0f;

	if( enemyPower <= 0.0f || enemyHealth <= 0.0f )
		return NOT_A_FIGHT;				// nothing that can hurt me
	if( myPower <= 0.0f || myHealth <= 0.0f )
		return 0.0f;							// nothing I can do about it

	const Real howLongILast = myHealth / enemyPower;
	const Real howLongTheyLast = enemyHealth / myPower;
	if( howLongTheyLast <= 0.0f )
		return NOT_A_FIGHT;

	Real ratio = howLongILast / howLongTheyLast;
	return (ratio > NOT_A_FIGHT) ? NOT_A_FIGHT : ratio;
}

static Bool gunsReach( Real x, Real y, const Real *guns, Int gunCount, const AIInfluenceMap *map )
{
	for( Int g = 0; g < gunCount; ++g )
	{
		const Real gx = x - guns[ 3 * g ];
		const Real gy = y - guns[ 3 * g + 1 ];
		if( gx * gx + gy * gy <= guns[ 3 * g + 2 ] * guns[ 3 * g + 2 ] )
			return TRUE;
	}
	return map != NULL && map->enemyAt( x, y ) > 0.0f;
}

//-------------------------------------------------------------------------------------------------
/** How far a force at (fromX, fromY) walks along (dirX, dirY), a unit vector, in steps, to the first
	* spot no gun reaches with one more step of clear ground past it, that next step included; -1 when it
	* is not out by maxWalk, or walks off the map (bounds: low x, low y, high x, high y) first. */
static Real walkOutOfReach( Real fromX, Real fromY, Real dirX, Real dirY, Real maxWalk, Real step,
														const Real *guns, Int gunCount, const Real *bounds, const AIInfluenceMap *map )
{
	for( Real along = step; along + step < maxWalk; along += step )
	{
		const Real nextX = fromX + dirX * (along + step);
		const Real nextY = fromY + dirY * (along + step);
		if( nextX < bounds[ 0 ] || nextY < bounds[ 1 ] || nextX > bounds[ 2 ] || nextY > bounds[ 3 ] )
			return -1.0f;
		if( !gunsReach( fromX + dirX * along, fromY + dirY * along, guns, gunCount, map ) &&
				!gunsReach( fromX + dirX * (along + step), fromY + dirY * (along + step), guns, gunCount, map ) )
			return along + step;
	}
	return -1.0f;
}

/** Where a force that is losing stops falling back: the first spot no gun in guns[] reaches with one
	* more step of clear ground past it, and that next step is where it stands, so the edge of a reach is
	* never the place.  guns holds x, y and reach for each of gunCount guns.  FALSE, with home in the
	* answer, when no way leaves their reach before it has walked as far as home is.
	*
	* The way out is the road home, or 45 or 90 degrees off it when that gets out of reach more than a
	* step sooner.  Home alone walked a force past the guns whenever they stood between it and home, the
	* Gatling Cannons by Tournament Desert's middle docks among them, and one in seven units a Hard China
	* pulled out of a fight there died to a building within fifteen seconds of the order.  A way that walks
	* off the map (bounds) is no way out; ground the pathfinder cannot stand on is its to move to the
	* nearest that it can, as with any move.  With a map, every enemy gun on it counts as well as the ones in
	* guns[]: those are the guns of the fight, and a spot clear of them stood under the next gun over. */
//-------------------------------------------------------------------------------------------------
Bool aiRetreatFallbackPoint( Real fromX, Real fromY, Real homeX, Real homeY, Real step,
														 const Real *guns, Int gunCount, const Real *bounds, Real *outX, Real *outY,
														 const AIInfluenceMap *map )
{
	*outX = homeX;
	*outY = homeY;
	const Real dx = homeX - fromX;
	const Real dy = homeY - fromY;
	const Real homeDist = (Real)sqrt( dx * dx + dy * dy );
	if( homeDist <= 0.0f )
		return FALSE;
	const Real hx = dx / homeDist;
	const Real hy = dy / homeDist;
	const Real HALF_ROOT_TWO = 0.70710678f;		// cos and sin of 45 degrees, no trig in logic
	const Real ways[ 5 ][ 2 ] =
	{
		{ hx, hy },
		{ HALF_ROOT_TWO * (hx - hy), HALF_ROOT_TWO * (hx + hy) },
		{ HALF_ROOT_TWO * (hx + hy), HALF_ROOT_TWO * (hy - hx) },
		{ -hy, hx },
		{ hy, -hx }
	};
	Int best = -1;
	Real bestWalk = 0.0f;
	for( Int w = 0; w < 5; ++w )
	{
		const Real walk = walkOutOfReach( fromX, fromY, ways[ w ][ 0 ], ways[ w ][ 1 ], homeDist, step, guns, gunCount, bounds, map );
		if( walk >= 0.0f && (best < 0 || walk + step < bestWalk) )
		{
			best = w;
			bestWalk = walk;
		}
	}
	if( best < 0 )
		return FALSE;
	*outX = fromX + ways[ best ][ 0 ] * bestWalk;
	*outY = fromY + ways[ best ][ 1 ] * bestWalk;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
AIFireAnswer aiAnswerFire( Bool onTheGun, Bool fightingAGun, Bool canShootNow, Bool canGetAtIt, Real ours, Real cover, Real odds, Bool mayLeave, Bool idle )
{
	if( onTheGun || fightingAGun )
		return AIFIRE_CARRY_ON;
	if( canShootNow )
		return AIFIRE_SHOOT_BACK;
	if( canGetAtIt && !aiOutgunned( ours, cover, odds ) )
		return AIFIRE_GO_KILL;
	if( mayLeave )
		return AIFIRE_LEAVE;
	if( !idle )
		return AIFIRE_CARRY_ON;
	return canGetAtIt ? AIFIRE_GO_KILL : AIFIRE_LEAVE;
}

//-------------------------------------------------------------------------------------------------
Real aiFireOddsNow( Real odds, Bool sentAtIt, Real margin )
{
	return sentAtIt ? odds / margin : odds;
}

//-------------------------------------------------------------------------------------------------
Bool aiFireMayGoBack( Bool sentAtIt )
{
	return !sentAtIt;
}

//-------------------------------------------------------------------------------------------------
Bool aiFireAnswerOver( Bool retreatHasIt, Bool steppedAway, UnsignedInt framesOnIt, UnsignedInt maxFrames )
{
	return retreatHasIt || steppedAway || framesOnIt >= maxFrames;
}

//-------------------------------------------------------------------------------------------------
Bool aiHolderLetGo( Bool retreatSeesIt, UnsignedInt heldFrames, Bool moving, UnsignedInt holdFrames, UnsignedInt staleFrames )
{
	if( heldFrames > staleFrames )
		return TRUE;
	return !retreatSeesIt && !moving && heldFrames > holdFrames;
}

//-------------------------------------------------------------------------------------------------
void aiStrikeDodgeSpot( Real x, Real y, Real clumpX, Real clumpY, Real reach, const Real *bounds, Int tie, Real *outX, Real *outY )
{
	Real dx = x - clumpX;
	Real dy = y - clumpY;
	Real length = (Real)sqrt( dx * dx + dy * dy );
	if( length < 1.0f )
	{
		dx = (tie & 1) ? 1.0f : -1.0f;
		dy = (tie & 2) ? 1.0f : -1.0f;
		length = (Real)sqrt( 2.0f );
	}
	dx *= reach / length;
	dy *= reach / length;
	if( x + dx < bounds[ 0 ] || x + dx > bounds[ 2 ] )
		dx = -dx;
	if( y + dy < bounds[ 1 ] || y + dy > bounds[ 3 ] )
		dy = -dy;
	*outX = x + dx;
	*outY = y + dy;
}

//-------------------------------------------------------------------------------------------------
Bool aiFireMayLeave( Bool unitAtHome, Bool gunAtHome, Bool rungRetreats, Bool gunKnown, Real gunDistance, Real retreatRadius )
{
	if( unitAtHome )
		return !gunAtHome;
	return !(rungRetreats && gunKnown && gunDistance <= retreatRadius);
}

//-------------------------------------------------------------------------------------------------
Bool aiOutgunned( Real ours, Real cover, Real odds )
{
	return cover > 0.0f && ours < odds * cover;
}

//-------------------------------------------------------------------------------------------------
Bool aiRecentlyHit( UnsignedInt hitFrame, UnsignedInt now, UnsignedInt window )
{
	return hitFrame <= now && now - hitFrame <= window;
}

//-------------------------------------------------------------------------------------------------
Bool aiWalkEntersReach( Real x, Real y, Real dirX, Real dirY, Real gunX, Real gunY, Real reach, Real edge, Real lookahead )
{
	const Real toX = gunX - x;
	const Real toY = gunY - y;
	const Real along = toX * dirX + toY * dirY;
	if( toX * toX + toY * toY <= reach * reach || along <= 0.0f )
		return FALSE;
	const Real ahead = along > lookahead ? lookahead : along;
	const Real nearX = x + dirX * ahead - gunX;
	const Real nearY = y + dirY * ahead - gunY;
	return nearX * nearX + nearY * nearY <= edge * edge;
}

//-------------------------------------------------------------------------------------------------
/** What a force that fell back does next.  It holds until it has stopped and minHold has passed;
	* then it goes back the moment the fight it left no longer reads as lost (ratioThere at or over
	* resumeRatio, which an empty fight always is), and gives up and goes home at maxHold. */
//-------------------------------------------------------------------------------------------------
AIFallbackDecision aiRetreatHoldDecision( Bool arrived, UnsignedInt heldFrames, Real ratioThere, Real resumeRatio,
																					UnsignedInt minHold, UnsignedInt maxHold )
{
	if( heldFrames < minHold || (!arrived && heldFrames < maxHold) )
		return AIFALLBACK_HOLD;
	if( ratioThere >= resumeRatio )
		return AIFALLBACK_RESUME;
	return heldFrames >= maxHold ? AIFALLBACK_GO_HOME : AIFALLBACK_HOLD;
}

//-------------------------------------------------------------------------------------------------
/** What a spot is still worth to aim a superweapon at, after one already aimed near it.
	*
	* Nothing outside the blast fades, and nothing older than the window does.  Inside both, how much
	* the two shots overlap decides: on top of the old aim the worth is the share of the window that
	* has passed, at the rim it is untouched, and it slides between the two.
	*
	* The grading is the part that does the work, and the flat version that came first is why.  A
	* fade that took the same amount off every point inside the blast left the ranking among them
	* exactly as it was, so the second shot picked the same winner as the first and only the
	* reported worth moved - measured, seventeen scans of one cell in one match.  Grading it makes
	* the next shot slide off the crater and cover ground the last one did not. */
//-------------------------------------------------------------------------------------------------
Real aiStrikeFade( Real distanceSqr, Real radiusSqr, UnsignedInt age, UnsignedInt window )
{
	if( window == 0 || age >= window )
		return 1.0f;
	if( radiusSqr <= 0.0f || distanceSqr >= radiusSqr )
		return 1.0f;

	const Real overlap = 1.0f - sqrt( distanceSqr / radiusSqr );		// 1 on top of the old aim, 0 at the rim
	const Real recovered = INT_TO_REAL( age ) / INT_TO_REAL( window );
	return 1.0f - overlap * (1.0f - recovered);
}

//-------------------------------------------------------------------------------------------------
Bool aiShouldMass( Real waitingThreat, Real enemyVisibleThreat, Real massFraction,
									 Bool timeExpired, Bool baseUnderAttack )
{
	if( timeExpired )
		return FALSE;					// the safety valve: never wait for ever
	if( baseUnderAttack )
		return FALSE;					// there is a fight at home; the wave is needed now, wherever it is
	if( enemyVisibleThreat <= 0.0f )
		return FALSE;					// nothing found to mass against - waiting would be waiting on nothing
	if( massFraction <= 0.0f )
		return FALSE;

	return waitingThreat < massFraction * enemyVisibleThreat;
}

//-------------------------------------------------------------------------------------------------
Bool aiWantsAnotherFactory( Int finished, Int constructing, UnsignedInt history, Int count, Int window )
{
	if( constructing > 0 )
		return FALSE;					// one is already going up
	if( finished <= 0 )
		return TRUE;					// the last one is gone, so put one back
	if( count < window )
		return FALSE;					// the window is not in yet
	Int full = 0;
	for( Int i = 0; i < window; ++i )
		if( history & ( 1u << i ) )
			++full;
	return full * 100 >= window * AI_FACTORY_FULL_PERCENT;
}

//-------------------------------------------------------------------------------------------------
AISiteVerdict aiWatchSite( AISiteWatch *watch, Real percent, ObjectID builder, Bool builderAlive,
													 UnsignedInt now, UnsignedInt stallFrames, Int maxLost )
{
	if( percent > watch->m_percent )
	{
		watch->m_percent = percent;
		watch->m_progressFrame = now;
		watch->m_buildersLost = 0;
	}
	if( !builderAlive && builder != INVALID_ID && builder != watch->m_lostBuilder )
	{
		watch->m_lostBuilder = builder;
		++watch->m_buildersLost;
	}
	if( maxLost > 0 && watch->m_buildersLost >= maxLost )
		return AISITE_BUILDERS_LOST;
	if( stallFrames > 0 && now - watch->m_progressFrame >= stallFrames )
		return AISITE_STALLED;
	return AISITE_KEEP;
}

//-------------------------------------------------------------------------------------------------
Bool aiSpotCooling( const std::vector<AICooledSpot> &spots, const Coord3D *pos, Real radius, UnsignedInt now )
{
	for( size_t i = 0; i < spots.size(); ++i )
	{
		if( spots[ i ].m_until <= now )
			continue;
		const Real dx = spots[ i ].m_pos.x - pos->x;
		const Real dy = spots[ i ].m_pos.y - pos->y;
		if( dx * dx + dy * dy <= radius * radius )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool aiWantsAnotherTechBuilding( Int standing, Int onTheWay )
{
	if( onTheWay > 0 )
		return FALSE;
	return standing < AI_TECH_BUILDING_COPIES;
}

//-------------------------------------------------------------------------------------------------
Int aiDefenseAllowance( Int army, UnsignedInt frame )
{
	const Int byArmy = army / AI_ARMY_PER_DEFENSE;
	Int byClock = (Int)( frame / ( AI_DEFENSE_PACE_SECONDS * LOGICFRAMES_PER_SECOND ) );
	if( byClock > 2 * DEFENSES_PER_SUPERWEAPON )
		byClock = 2 * DEFENSES_PER_SUPERWEAPON;
	return byArmy > byClock ? byArmy : byClock;
}

//-------------------------------------------------------------------------------------------------
Real aiFireSampleWeight( Real baseToThreat, Real sampleToThreat, Real reach, Bool outsideBase )
{
	Real weight = reach > 0.0f ? ( baseToThreat - sampleToThreat ) / reach : 0.0f;
	if( weight < 0.0f )
		weight = 0.0f;
	return outsideBase ? weight + 0.1f : weight;
}

//-------------------------------------------------------------------------------------------------
Int aiFindChoke( const UnsignedShort *fromBase, const UnsignedShort *fromThreat, Int width, Int cells, Int routeLength,
								 Int minDistance, Int maxDistance, Int *roadCells )
{
	*roadCells = 0;
	// whole bands only: a sliver at the far end holds few cells because it is thin, not narrow
	const Int bands = ( maxDistance - minDistance + 1 ) / AI_CHOKE_BAND;
	if( bands <= 0 )
		return -1;
	std::vector<Int> count( bands, 0 );
	std::vector<Real> sumX( bands, 0.0f );
	std::vector<Real> sumY( bands, 0.0f );
	std::vector<Int> bandOf( cells, -1 );
	for( Int i = 0; i < cells; ++i )
	{
		const Int b = fromBase[ i ];
		const Int t = fromThreat[ i ];
		if( b == AI_GROUND_UNREACHED || t == AI_GROUND_UNREACHED || b < minDistance || b > maxDistance || b > t )
			continue;
		if( ( b + t ) * 100 > routeLength * AI_CHOKE_SLACK_PERCENT )
			continue;
		const Int band = ( b - minDistance ) / AI_CHOKE_BAND;
		if( band >= bands )
			continue;
		bandOf[ i ] = band;
		++count[ band ];
		sumX[ band ] += INT_TO_REAL( i % width );
		sumY[ band ] += INT_TO_REAL( i / width );
	}

	Int best = -1;
	Real bestNarrow = 0.0f;
	for( Int band = 0; band < bands; ++band )
	{
		if( count[ band ] == 0 )
			continue;
		const Real middle = INT_TO_REAL( minDistance + band * AI_CHOKE_BAND + AI_CHOKE_BAND / 2 );
		const Real open = sqrt( middle * ( INT_TO_REAL( routeLength ) - middle ) );
		const Real narrow = INT_TO_REAL( count[ band ] ) / ( open > 1.0f ? open : 1.0f );
		if( best < 0 || narrow < bestNarrow )
		{
			best = band;
			bestNarrow = narrow;
		}
	}
	if( best < 0 )
		return -1;

	// ponytail: two roads through one band meet in the middle; the road cell nearest it stands for both
	const Real midX = sumX[ best ] / INT_TO_REAL( count[ best ] );
	const Real midY = sumY[ best ] / INT_TO_REAL( count[ best ] );
	Int choke = -1;
	Real chokeSqr = 0.0f;
	for( Int i = 0; i < cells; ++i )
	{
		if( bandOf[ i ] != best )
			continue;
		const Real d = sqr( INT_TO_REAL( i % width ) - midX ) + sqr( INT_TO_REAL( i / width ) - midY );
		if( choke < 0 || d < chokeSqr )
		{
			choke = i;
			chokeSqr = d;
		}
	}
	*roadCells = count[ best ];
	return choke;
}

//-------------------------------------------------------------------------------------------------
AIGroundFlood::AIGroundFlood( Int width, Int height, WalkableFunc walkable, void *userData ) :
	m_width( width ),
	m_height( height ),
	m_walkable( walkable ),
	m_userData( userData ),
	m_dist( width * height, AI_GROUND_UNREACHED ),
	m_walk( width * height, 0 ),
	m_at( 0 ),
	m_cursor( 0 ),
	m_pending( 0 ),
	m_max( AI_GROUND_UNREACHED - 1 )
{
}

//-------------------------------------------------------------------------------------------------
void AIGroundFlood::addSource( Int x, Int y )
{
	if( x < 0 || y < 0 || x >= m_width || y >= m_height )
		return;
	const Int index = y * m_width + x;
	if( m_dist[ index ] == 0 )
		return;
	m_dist[ index ] = 0;
	m_bucket[ 0 ].push_back( index );
	++m_pending;
}

//-------------------------------------------------------------------------------------------------
Bool AIGroundFlood::walkable( Int x, Int y, Int index )
{
	if( m_walk[ index ] == 0 )
		m_walk[ index ] = m_walkable( x, y, m_userData ) ? 1 : 2;
	return m_walk[ index ] == 1;
}

//-------------------------------------------------------------------------------------------------
Bool AIGroundFlood::next( Int *x, Int *y, Int *distance )
{
	static const Int STEPS[ 8 ][ 3 ] = {
		{ 1, 0, AI_GROUND_STEP }, { -1, 0, AI_GROUND_STEP }, { 0, 1, AI_GROUND_STEP }, { 0, -1, AI_GROUND_STEP },
		{ 1, 1, AI_GROUND_DIAGONAL }, { -1, 1, AI_GROUND_DIAGONAL }, { 1, -1, AI_GROUND_DIAGONAL }, { -1, -1, AI_GROUND_DIAGONAL } };

	while( m_pending > 0 )
	{
		std::vector<Int> &bucket = m_bucket[ m_at & 3 ];
		if( m_cursor >= (Int)bucket.size() )
		{
			m_pending -= (Int)bucket.size();
			bucket.clear();
			m_cursor = 0;
			++m_at;
			continue;
		}
		const Int index = bucket[ m_cursor++ ];
		if( m_dist[ index ] != m_at )
			continue;		// reached again by a shorter way, and handed out from that bucket

		const Int cx = index % m_width;
		const Int cy = index / m_width;
		for( Int n = 0; n < 8; ++n )
		{
			const Int nx = cx + STEPS[ n ][ 0 ];
			const Int ny = cy + STEPS[ n ][ 1 ];
			if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
				continue;
			const Int d = m_at + STEPS[ n ][ 2 ];
			const Int nIndex = ny * m_width + nx;
			if( d > m_max || d >= m_dist[ nIndex ] || !walkable( nx, ny, nIndex ) )
				continue;
			m_dist[ nIndex ] = (UnsignedShort)d;
			m_bucket[ d & 3 ].push_back( nIndex );
			++m_pending;
		}
		*x = cx;
		*y = cy;
		*distance = m_at;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Int aiPickBaseDefense( const Int *standing, const Bool *armed, Int count )
{
	Int best = -1;
	Int bestWeight = 0;
	for( Int i = 0; i < count; ++i )
	{
		const Int weight = standing[ i ] * ( armed[ i ] ? 1 : AI_SUPPORT_DEFENSE_WEIGHT );
		if( best < 0 || weight < bestWeight || ( weight == bestWeight && armed[ i ] && !armed[ best ] ) )
		{
			best = i;
			bestWeight = weight;
		}
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
Real aiScoutScore( UnsignedInt now, UnsignedInt lastSeenFrame, Real distance, UnsignedInt freshFrames )
{
	// 0 == never looked, and now - 0 is bigger than any real age, so those sort to the front by itself
	const UnsignedInt age = now - lastSeenFrame;

	if( lastSeenFrame != 0 && age < freshFrames )
		return 0.0f;					// the picture is still good; the walk would buy nothing

	if( distance < 1.0f )
		distance = 1.0f;			// already standing on it - the best place to look from, and no divide by zero

	return (Real)age / distance;
}

//-------------------------------------------------------------------------------------------------
Real aiStartOccupiedOdds( Int unlocatedEnemies, Int uncheckedPositions )
{
	if( unlocatedEnemies <= 0 || uncheckedPositions <= 0 )
		return 0.0f;											// nobody left to find, or nowhere left to look

	if( unlocatedEnemies >= uncheckedPositions )
		return 1.0f;											// nowhere else for them to be: this is deduced, not guessed

	return (Real)unlocatedEnemies / (Real)uncheckedPositions;
}

//-------------------------------------------------------------------------------------------------
Bool aiIsIncomeNotATarget( Bool isTechBuilding, Bool isCapturable, Bool isTechBaseDefense )
{
	return isTechBuilding && isCapturable && !isTechBaseDefense;
}

//-------------------------------------------------------------------------------------------------
Real aiRoleMassFraction( AIRole role )
{
	switch( role )
	{
		case AIROLE_AGGRESSIVE:		return 0.8f;		// presses, with less
		case AIROLE_DEFENSIVE:		return 1.3f;		// would rather absorb a hit and counter-attack
		case AIROLE_SUPPORTIVE:		return 1.0f;		// goes when the ally goes
		case AIROLE_ECONOMIST:		return 1.5f;		// fights only with a clear advantage
		case AIROLE_STEAMROLLER:	return 1.8f;		// one push, and it had better land
		default:									return 1.0f;
	}
}

//-------------------------------------------------------------------------------------------------
static const char *const s_personalityNames[ AIPERSONA_COUNT ] =
{
	"Normal", "Attacker", "Turtle", "Economist", "Air", "Rusher", "Raider", "Tech",
	"Infantry", "Armor", "Sneaky", "Opportunist", "Supporter", "Counter", "Random"
};

const char *aiPersonalityName( AIPersonality persona )
{
	return (persona >= 0 && persona < AIPERSONA_COUNT) ? s_personalityNames[ persona ] : s_personalityNames[ 0 ];
}

AIPersonality aiPersonalityFromName( const char *name )
{
	for( Int i = 0; i < AIPERSONA_COUNT; ++i )
		if( strcasecmp( name, s_personalityNames[ i ] ) == 0 )
			return (AIPersonality)i;
	return AIPERSONA_NORMAL;
}

//-------------------------------------------------------------------------------------------------
/** The personalities, one row each in AIPersonality's order.  The buy columns are aiPurchaseScore's
	* kinds: power, dozer, economy, base, superweapon, defence, army, upgrade, emergency.  The Rusher's
	* row is the one it plays by until its rush is out; after that it plays the Attacker's. */
//-------------------------------------------------------------------------------------------------
//                                  role                push  atkP  wave  hold  bank  hoard surpl retr  cntr  +cntr def   upgr   defT   air    inf    veh    stlth    pow dz  eco base sw   def  army upg em
static const AIPersonaWeights s_personaWeights[ AIPERSONA_COUNT ] =
{
	/* Normal      */ { -1,                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,   0.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,   0, 0,   0,   0,   0,   0, 0 } },
	/* Attacker    */ { AIROLE_AGGRESSIVE,  0.6f, 0.5f, 0.5f, 0.5f, 0.35f,0.5f, 2.0f, 0.8f, 1.0f, 0.0f, 0.6f, 1.0f,  -5.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,   0, 0,   0, -15,  20,   0, 0 } },
	/* Turtle      */ { AIROLE_DEFENSIVE,   1.6f, 1.5f, 1.8f, 1.6f, 1.0f, 1.0f, 1.0f, 1.6f, 1.0f, 0.0f, 1.8f, 1.0f,  15.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,   0, 0,   0,  40, -10,   0, 0 } },
	/* Economist   */ { -1,                 1.2f, 1.1f, 1.2f, 1.0f, 1.2f, 0.7f, 0.8f, 1.2f, 1.0f, 0.0f, 1.1f, 0.6f,   0.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,  30, 0,   0,   0,  -5,  15, 0 } },
	/* Air         */ { -1,                 1.0f, 1.0f, 1.0f, 1.0f, 0.6f, 1.0f, 1.5f, 1.0f, 1.0f, 0.0f, 0.6f, 1.0f,   0.0f, 25.0f, -5.0f, -5.0f,  0.0f, { 0, 0,   0, 10, -20, -15,  20,   0, 0 } },
	/* Rusher      */ { AIROLE_AGGRESSIVE,  1.0f, 1.0f, 1.0f, 1.0f, 0.25f,0.3f, 3.0f, 0.5f, 1.0f, 0.0f, 0.3f, 3.0f, -15.0f,-10.0f,  5.0f,  5.0f,  0.0f, { 0, 0,   0, 0, -30, -40,  35, -20, 0 } },
	/* Raider      */ { AIROLE_AGGRESSIVE,  0.9f, 0.8f, 0.8f, 0.8f, 0.8f, 1.0f, 1.2f, 1.2f, 1.0f, 0.0f, 0.8f, 1.0f,  -5.0f,  0.0f, -5.0f, 15.0f,  0.0f, { 0, 0,   0, 0,   0, -10,  10,   0, 0 } },
	/* Tech        */ { AIROLE_DEFENSIVE,   1.4f, 1.2f, 1.3f, 1.2f, 1.0f, 1.0f, 1.0f, 1.2f, 1.0f, 0.0f, 1.4f, 0.5f,   5.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,   5, 0,  40,  10, -10,  15, 0 } },
	/* Infantry    */ { -1,                 0.9f, 1.0f, 1.0f, 1.0f, 0.8f, 1.0f, 1.3f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,   0.0f, -5.0f, 25.0f,-10.0f,  0.0f, { 0, 0,   0, 0,   0,   0,  10,   0, 0 } },
	/* Armor       */ { -1,                 1.2f, 1.4f, 1.6f, 1.4f, 0.7f, 1.0f, 1.5f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,   0.0f, -5.0f,-10.0f, 25.0f,  0.0f, { 0, 0,   0, 0,   0,   0,   5,   0, 0 } },
	/* Sneaky      */ { -1,                 1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 1.0f, 1.5f, 1.1f, 1.0f, 0.0f, 1.0f, 1.0f,   0.0f,  0.0f,  0.0f,  0.0f, 25.0f, { 0, 0,   0, 0,   0,   0,   0,   0, 0 } },
	/* Opportunist */ { AIROLE_AGGRESSIVE,  0.9f, 0.9f, 0.8f, 0.8f, 0.9f, 1.0f, 1.0f, 1.2f, 1.0f, 0.0f, 1.0f, 1.0f,   0.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,   0, 0,   0,   0,   5,   0, 0 } },
	/* Supporter   */ { AIROLE_SUPPORTIVE,  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.2f, 1.0f,   5.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,   0, 0,   0,   5,   0,   0, 0 } },
	/* Counter     */ { -1,                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.6f, 0.3f, 1.0f, 1.0f,   0.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,   0, 0,   0,   0,   0,   0, 0 } },
	/* Random      */ { -1,                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,   0.0f,  0.0f,  0.0f,  0.0f,  0.0f, { 0, 0,   0, 0,   0,   0,   0,   0, 0 } }
};

const AIPersonaWeights &aiPersonaWeights( AIPersonality persona )
{
	return s_personaWeights[ (persona >= 0 && persona < AIPERSONA_COUNT) ? persona : AIPERSONA_NORMAL ];
}

Real aiPersonaTeamBias( const AIPersonaWeights &weights, Bool defenceTeam, Real airShare, Real infantryShare,
												Real vehicleShare, Real stealthShare )
{
	Real bias = weights.m_air * airShare + weights.m_infantry * infantryShare + weights.m_vehicle * vehicleShare +
							weights.m_stealth * stealthShare;
	if( defenceTeam )
		bias += weights.m_defenceTeams;
	return bias;
}

Real aiPersonaAirBias( Real airWeight, Real enemyAntiAirShare )
{
	Real taste = 1.0f - 1.5f * enemyAntiAirShare;
	if( taste < -0.5f )
		taste = -0.5f;
	return airWeight * taste;
}

Real aiPersonaEnemyCost( AIPersonality persona, Real cost, Real economyShare, Bool fightingSomeoneElse )
{
	if( persona != AIPERSONA_OPPORTUNIST )
		return cost;
	if( economyShare < 0.0f ) economyShare = 0.0f;
	if( economyShare > 1.0f ) economyShare = 1.0f;
	cost *= 0.4f + economyShare;			// the thinnest in sight costs under half of the fattest
	if( fightingSomeoneElse )
		cost *= 0.5f;
	return cost;
}

Int aiPersonaShareAmount( Int bank, Int target, Int cap )
{
	const Int over = bank - 2 * target;
	if( over <= 0 )
		return 0;
	Int amount = (over / 2) / 100 * 100;
	if( amount > cap )
		amount = cap;
	return amount < AI_SHARE_MIN ? 0 : amount;
}

Bool aiPersonaChatAllowed( UnsignedInt now, UnsignedInt lastChat )
{
	return lastChat == 0 || now - lastChat >= AI_CHAT_GAP_FRAMES;
}

//-------------------------------------------------------------------------------------------------
Real aiEnemyCost( Real distSqr, Bool crippled, Bool alreadyTargetedByAnotherAI,
									Bool isAttackingMe, Real economyShare, Bool joinAllysTarget )
{
	Real cost = distSqr;

	// A crippled enemy is an opportunity, not a distraction. EA multiplied his distance up; this
	// pulls it down. Only a genuinely finished one is skipped, and that is the caller's test.
	if( crippled )
		cost *= 0.4f;

	// The bigger his economy, the more of the match is decided by hitting him.
	if( economyShare < 0.0f ) economyShare = 0.0f;
	if( economyShare > 1.0f ) economyShare = 1.0f;
	cost *= (1.0f - 0.3f * economyShare);

	//
	// EA's two flat terms, kept: do not gang up, and gently prefer whoever is already on me.
	//
	// The first one is exactly backwards for a supportive AI - converging on the ally's chosen
	// enemy is the entire point of allied play - so that role flips its sign rather than paying it.
	//
	if( alreadyTargetedByAnotherAI )
		cost += joinAllysTarget ? -(250.0f * 250.0f) : (500.0f * 500.0f);
	if( isAttackingMe )
		cost -= 25.0f * 25.0f;

	return (cost < 0.0f) ? 0.0f : cost;
}

//-------------------------------------------------------------------------------------------------
Int aiHoardAdjustedDelay( Int frames, Int money, Int hoardAt )
{
	if( hoardAt <= 0 || money <= hoardAt || frames <= 1 )
		return frames;

	Real scale = INT_TO_REAL( hoardAt ) / INT_TO_REAL( money );		// 2x the cash -> half the wait
	if( scale < 0.25f )
		scale = 0.25f;																							// and no faster than four times

	Int out = REAL_TO_INT_CEIL( INT_TO_REAL( frames ) * scale );
	return (out < 1) ? 1 : out;
}

//-------------------------------------------------------------------------------------------------
Int aiHoardAllowedTeamInstances( Int maxInstances, Int money, Int hoardAt )
{
	const Int MAX_EXTRA_INSTANCES = 3;

	if( hoardAt <= 0 || maxInstances <= 0 )
		return maxInstances;

	Int extra = money / (2 * hoardAt);
	if( extra > MAX_EXTRA_INSTANCES )
		extra = MAX_EXTRA_INSTANCES;
	return maxInstances + extra;
}

//-------------------------------------------------------------------------------------------------
Int aiPurchaseScore( AIPurchaseKind kind, Bool earlyGame )
{
	switch( kind )
	{
		case AIBUY_EMERGENCY:		return 200;
		case AIBUY_POWER:				return 100;
		case AIBUY_DOZER:				return 90;
		case AIBUY_ECONOMY:			return earlyGame ? 85 : 50;
		case AIBUY_BASE:				return 70;
		case AIBUY_SUPERWEAPON:	return 60;
		case AIBUY_ARMY:				return earlyGame ? 30 : 55;
		case AIBUY_DEFENSE:			return 45;
		case AIBUY_UPGRADE:			return 35;
		default:								return 0;
	}
}

//-------------------------------------------------------------------------------------------------
AIPurchaseQueue::Lapse AIPurchaseQueue::expire( UnsignedInt frame )
{
	if( m_key == NONE )
		return KEPT;
	if( frame - m_since >= AI_PURCHASE_HOLD_FRAMES )
	{
		m_lapsedKey = m_key;
		m_lapsedUntil = frame + AI_PURCHASE_HOLD_FRAMES;
		m_key = NONE;
		return TIMED_OUT;
	}
	if( frame - m_seen >= AI_PURCHASE_STALE_FRAMES )
	{
		m_key = NONE;
		return STALE;
	}
	return KEPT;
}

//-------------------------------------------------------------------------------------------------
AIPurchaseQueue::Verdict AIPurchaseQueue::ask( Int key, Int cost, Int score, Bool startable, Int money, Int hoardAt, UnsignedInt frame )
{
	if( key == m_key )
	{
		m_seen = frame;
		m_cost = cost;
		return cost <= money ? BUY : SAVING;
	}

	const Bool outranks = m_key == NONE || score > m_score || (score == m_score && key < m_key);
	if( cost <= money )
	{
		const Int ceiling = hoardAt > m_cost ? hoardAt : m_cost;
		if( outranks || money - cost >= m_cost || money > ceiling )
			return BUY;
		++m_held;
		return HELD;
	}

	if( !startable || !outranks || money > hoardAt || (key == m_lapsedKey && frame < m_lapsedUntil) )
		return SHORT;
	m_key = key;
	m_cost = cost;
	m_score = score;
	m_since = frame;
	m_seen = frame;
	m_held = 0;
	return RESERVED;
}

//-------------------------------------------------------------------------------------------------
Int aiBankTarget( Real incomePerSecond, Real seconds, Int floor )
{
	const Int byIncome = REAL_TO_INT_FLOOR( incomePerSecond * seconds );
	return byIncome > floor ? byIncome : floor;
}


//-------------------------------------------------------------------------------------------------
Int aiSurplusBudget( Int bank, Int target, Real incomePerSecond, Real share )
{
	const Int excess = bank - target;
	if( excess <= 0 )
		return 0;
	const Int byIncome = REAL_TO_INT_FLOOR( share * incomePerSecond * AI_SURPLUS_INCOME_SECONDS );
	return byIncome < excess ? byIncome : excess;
}

//-------------------------------------------------------------------------------------------------
Int aiFortuneBudget( Int bank, Int keep, Int fortune, Real share )
{
	Int excess = bank - keep;
	if( excess > fortune )
		excess = fortune;
	if( excess <= 0 )
		return 0;
	return REAL_TO_INT_FLOOR( share * INT_TO_REAL( excess ) );
}

//-------------------------------------------------------------------------------------------------
Real aiCounterFit( Real matchup, Real weight )
{
	return 1.0f + weight * (2.0f * matchup - 1.0f);
}

//-------------------------------------------------------------------------------------------------
Real aiUpgradeWeight( Bool fights, Real fit, Real nonCombatWeight )
{
	return fights ? fit : nonCombatWeight;
}

//-------------------------------------------------------------------------------------------------
Int aiPickUpgrade( const Real *covered, const Int *cost, Int count, Real payoff, Bool overTarget )
{
	const Real needed = overTarget ? 0.5f * payoff : payoff;
	Int best = -1;
	Real bestTimes = 0.0f;
	for( Int i = 0; i < count; ++i )
	{
		if( cost[ i ] <= 0 || covered[ i ] <= 0.0f )
			continue;
		const Real times = covered[ i ] / INT_TO_REAL( cost[ i ] );
		if( times >= needed && (best < 0 || times > bestTimes) )
		{
			best = i;
			bestTimes = times;
		}
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
Int aiLeastDefendedLane( const Real *laneFirepower, Int laneCount, Int requestedLane )
{
	Int quietest = -1;
	for( Int i = 0; i < laneCount; ++i )
	{
		if( laneFirepower[ i ] < 0.0f )
			continue;
		if( quietest < 0 || laneFirepower[ i ] < laneFirepower[ quietest ] )
			quietest = i;
	}
	if( quietest < 0 )
		return requestedLane;

	const Bool requestedExists = requestedLane >= 0 && requestedLane < laneCount && laneFirepower[ requestedLane ] >= 0.0f;
	if( requestedExists && laneFirepower[ requestedLane ] <= laneFirepower[ quietest ] )
		return requestedLane;
	return quietest;
}

//-------------------------------------------------------------------------------------------------
Bool aiReleaseWave( Real heldPower, Real wavePower, UnsignedInt heldFrames, UnsignedInt maxHoldFrames )
{
	if( heldPower <= 0.0f )
		return FALSE;			// nothing parked that can fight, nothing to send
	return heldPower >= wavePower || heldFrames >= maxHoldFrames;
}

//-------------------------------------------------------------------------------------------------
Real aiWinChance( Real myPower, Real enemyPower )
{
	const Real PRIOR = 1500.0f;		// about two main battle tanks a side

	return (myPower + PRIOR) / (myPower + enemyPower + 2.0f * PRIOR);
}

//-------------------------------------------------------------------------------------------------
Real aiSideWinChance( Real myPower, Real alliedPower, Real enemyPower, Int enemyAllies, Real allyShare )
{
	return aiWinChance( myPower + alliedPower * allyShare, enemyPower * (1.0f + INT_TO_REAL( enemyAllies ) * allyShare) );
}

//-------------------------------------------------------------------------------------------------
AIPressure aiPressureLevel( Real chance, Bool enemyBaseInSight, AIPressure current )
{
	const Real FINISH_AT = 0.85f;
	const Real PRESS_AT = 0.65f;
	const Real DEFEND_AT = 0.35f;
	const Real KEEP = 0.10f;			// how far past its threshold a level is kept

	if( current == AIPRESSURE_FINISH ? chance >= FINISH_AT - KEEP : (enemyBaseInSight && chance >= FINISH_AT) )
		return AIPRESSURE_FINISH;
	if( current >= AIPRESSURE_PRESS ? chance >= PRESS_AT - KEEP : (enemyBaseInSight && chance >= PRESS_AT) )
		return AIPRESSURE_PRESS;
	if( chance <= (current == AIPRESSURE_DEFEND ? DEFEND_AT + KEEP : DEFEND_AT) )
		return AIPRESSURE_DEFEND;
	return AIPRESSURE_NORMAL;
}

//-------------------------------------------------------------------------------------------------
Bool aiReleaseWaveAt( AIPressure level, Real heldPower, Real wavePower, UnsignedInt heldFrames, UnsignedInt maxHoldFrames )
{
	if( level >= AIPRESSURE_PRESS )
		return heldPower > 0.0f;
	if( level == AIPRESSURE_DEFEND )
		return heldPower >= wavePower && aiReleaseWave( heldPower, 2.0f * wavePower, heldFrames, 2 * maxHoldFrames );
	return aiReleaseWave( heldPower, wavePower, heldFrames, maxHoldFrames );
}

//-------------------------------------------------------------------------------------------------
/** How well a team answers what the enemy is fielding.
	*
	* m_answer carries the matchups: how the team's own units fare, weapon against armour, against
	* every kind of unit it can see.  EA's data says a Crusader shell does a tenth of its damage to a
	* rifleman, and that is the kind of fact a KindOf flag could never tell the AI.  Stealth detection
	* keeps a term of its own, because no firepower answers a unit nobody can target. */
//-------------------------------------------------------------------------------------------------
Real aiCounterScore( const AIEnemyComposition &enemy, const AITeamCapability &team )
{
	Real score = team.m_answer;
	if( team.m_detectsStealth )
		score += enemy.m_stealth;

	if( score < 0.0f ) score = 0.0f;
	if( score > 1.0f ) score = 1.0f;
	return score;
}

//-------------------------------------------------------------------------------------------------
/** Frames until one weapon puts one target down.  The first shot pays one delay and every later
	* shot pays the wait before it - the reload when the shot before it emptied the clip.  Paying a
	* delay for the first shot is what keeps two one-shot kills from both reading as instant. */
//-------------------------------------------------------------------------------------------------
Real aiFramesToKill( Real targetHealth, const AIShotPattern &shots )
{
	const Real OVERKILL_TOLERANCE = 0.0001f;		// 480 health at 60 a shot is eight shots, not nine

	if( shots.m_damagePerShot <= 0.0f || targetHealth <= 0.0f )
		return AI_CANNOT_KILL;

	Int shotsNeeded = (Int)ceilf( targetHealth / shots.m_damagePerShot - OVERKILL_TOLERANCE );
	if( shotsNeeded < 1 )
		shotsNeeded = 1;
	const Int reloads = (shots.m_clipSize > 0) ? (shotsNeeded - 1) / shots.m_clipSize : 0;
	const Real firstShot = (shots.m_delayFrames > 1.0f) ? shots.m_delayFrames : 1.0f;

	return shots.m_openingFrames + firstShot
		+ INT_TO_REAL( shotsNeeded - 1 - reloads ) * shots.m_delayFrames
		+ INT_TO_REAL( reloads ) * shots.m_reloadFrames;
}

//-------------------------------------------------------------------------------------------------
/** log2 out of frexpf and a fixed series, all exact or plain IEEE arithmetic.  The runtime's log
	* takes an FMA3 path on a processor that has one, and a last bit that differs between two
	* machines is enough to flip the AI's pick between two scores that tie. */
//-------------------------------------------------------------------------------------------------
static Real deterministicLog2( Real value )
{
	const Real INVERSE_LN2 = 1.44269504f;

	Int exponent = 0;
	const Real mantissa = frexpf( value, &exponent );		// value = mantissa * 2^exponent, mantissa in [0.5, 1)

	// ln(m) = 2 atanh(t) with t = (m - 1) / (m + 1), and |t| <= 1/3 keeps five terms under 1e-6
	const Real t = (mantissa - 1.0f) / (mantissa + 1.0f);
	const Real t2 = t * t;
	const Real atanhT = t * (1.0f + t2 * (1.0f / 3.0f + t2 * (1.0f / 5.0f + t2 * (1.0f / 7.0f + t2 * (1.0f / 9.0f)))));

	return INT_TO_REAL( exponent ) + 2.0f * atanhT * INVERSE_LN2;
}

//-------------------------------------------------------------------------------------------------
/** One pairing, money for money.  On a log scale, so twice as good and half as good sit the same
	* distance either side of an even trade, and the two directions of one pairing always sum to 1. */
//-------------------------------------------------------------------------------------------------
Real aiMatchupScore( Real myFramesToKill, Real theirFramesToKill, Real myCost, Real theirCost )
{
	const Real SATURATING_DOUBLINGS = 4.0f;		// sixteen times better is as good as it gets
	const Real SATURATING_ADVANTAGE = 16.0f;	// 2 ^ SATURATING_DOUBLINGS

	if( myFramesToKill < 0.0f )
		return 0.0f;				// nothing I can do about it, whatever it can do to me
	if( theirFramesToKill < 0.0f )
		return 1.0f;				// it cannot touch me and I can kill it

	Real advantage = theirFramesToKill / myFramesToKill;
	if( myCost > 0.0f && theirCost > 0.0f )
		advantage *= theirCost / myCost;			// twice the price has to kill twice as fast to break even

	// the score saturates past sixteen either way, and the clamp keeps zero and infinity out of the log
	if( advantage > SATURATING_ADVANTAGE ) advantage = SATURATING_ADVANTAGE;
	if( advantage < 1.0f / SATURATING_ADVANTAGE ) advantage = 1.0f / SATURATING_ADVANTAGE;
	const Real doublings = deterministicLog2( advantage );
	Real score = 0.5f + 0.5f * doublings / SATURATING_DOUBLINGS;
	if( score < 0.0f ) score = 0.0f;
	if( score > 1.0f ) score = 1.0f;
	return score;
}

//-------------------------------------------------------------------------------------------------
/** The value-weighted mean of every pairing's score, turned back into an advantage as s / (1 - s):
	* even at 0.5, three at 0.75, the inverse when the sides swap.  That stays near the doublings the
	* score was made from (0.75 was four times better) with no exp() in logic. */
//-------------------------------------------------------------------------------------------------
Real aiArmyAdvantage( const Real *myValue, Int myKinds, const Real *theirValue, Int theirKinds, const Real *score )
{
	const Real MOST = 16.0f;

	Real weighed = 0.0f;
	Real scored = 0.0f;
	for( Int i = 0; i < myKinds; ++i )
	{
		for( Int j = 0; j < theirKinds; ++j )
		{
			const Real weight = myValue[ i ] * theirValue[ j ];
			weighed += weight;
			scored += weight * score[ i * theirKinds + j ];
		}
	}
	if( weighed <= 0.0f )
		return 1.0f;		// one side is empty: no exchange to weigh

	Real s = scored / weighed;
	const Real lowest = 1.0f / (MOST + 1.0f);
	if( s < lowest ) s = lowest;
	if( s > 1.0f - lowest ) s = 1.0f - lowest;
	return s / (1.0f - s);
}

//-------------------------------------------------------------------------------------------------
const AIDifficultyProfile *AI::getDifficultyProfile( AISkillLevel level ) const
{
	if( m_aiData == NULL )
		return &s_defaultSkillLadder[ AISKILL_BRUTAL ];
	if( level < 0 || level >= AISKILL_COUNT )
		level = AISKILL_BRUTAL;				// the baseline; a bad index must not read as a handicap
	return &m_aiData->m_skill[ level ];
}

//-------------------------------------------------------------------------------------------------
/** SkillLevel = Brutal ... End, in AI.ini: tuning the ladder without a recompile, which is what
	* the headless batch runner wants.  Anything the block does not mention keeps its default. */
//-------------------------------------------------------------------------------------------------
void AI::parseSkillLevel(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	static const FieldParse myFieldParse[] =
	{
		{ "ScoutIntervalSeconds",			INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_scoutIntervalSeconds ) },
		{ "MaxScouts",								INI::parseInt,  NULL, offsetof( AIDifficultyProfile, m_maxScouts ) },
		{ "DecisionIntervalSeconds",	INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_decisionIntervalSeconds ) },
		{ "CounterCompositionWeight",	INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_counterCompositionWeight ) },
		{ "MassBeforeAttacking",			INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_massBeforeAttacking ) },
		{ "RetreatTtkRatio",					INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_retreatTtkRatio ) },
		{ "RetreatIndividualUnits",		INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_retreatIndividualUnits ) },
		{ "RetreatTeams",							INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_retreatTeams ) },
		{ "UseInfluenceMapForAttackLane", INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_useInfluenceMapForAttackLane ) },
		{ "FocusFire",								INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_focusFire ) },
		{ "SavesSciencePoints",				INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_savesSciencePoints ) },
		{ "AdaptiveHarvesters",				INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_adaptiveHarvesters ) },
		{ "SelfTriggeredExpansion",		INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_selfTriggeredExpansion ) },
		{ "DefendExpansions",					INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_defendExpansions ) },
		{ "CashHoardThreshold",				INI::parseInt,  NULL, offsetof( AIDifficultyProfile, m_cashHoardThreshold ) },
		{ "EconomyBuildings",					INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_economyBuildings ) },
		{ "TacticalMicro",						INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_tacticalMicro ) },
		{ "EngageGate",								INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_engageGate ) },
		{ "AnswerArmy",								INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_answerArmy ) },
		{ "MassUnit",									INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_massUnit ) },
		{ "BankTargetSeconds",				INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_bankTargetSeconds ) },
		{ "BankTargetFloor",					INI::parseInt,  NULL, offsetof( AIDifficultyProfile, m_bankTargetFloor ) },
		{ "UpgradePayoff",						INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_upgradePayoff ) },
		{ "UpgradeStartSeconds",			INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_upgradeStartSeconds ) },
		{ "UpgradeNonCombatWeight",		INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_upgradeNonCombatWeight ) },
		{ "SurplusShare",							INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_surplusShare ) },
		{ "FactoryBacklogSeconds",		INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_factoryBacklogSeconds ) },
		{ "FirstPushSeconds",					INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_firstPushSeconds ) },
		{ "MinAttackPower",						INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_minAttackPower ) },
		{ "SiegeTrail",								INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_siegeTrail ) },
		{ "BeamSteerSeconds",					INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_beamSteerSeconds ) },
		{ "StrikeEconomyWeight",			INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_strikeEconomyWeight ) },
		{ "StrikeThreatWeight",				INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_strikeThreatWeight ) },
		{ "SuperweaponDefenses",			INI::parseInt,  NULL, offsetof( AIDifficultyProfile, m_superweaponDefenses ) },
		{ "SiteStallSeconds",					INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_siteStallSeconds ) },
		{ "SiteBuildersLost",					INI::parseInt,  NULL, offsetof( AIDifficultyProfile, m_siteBuildersLost ) },
		{ "SiteRetrySeconds",					INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_siteRetrySeconds ) },
		{ "StageDozers",							INI::parseBool, NULL, offsetof( AIDifficultyProfile, m_stageDozers ) },
		{ "ArtillerySpotters",				INI::parseInt,  NULL, offsetof( AIDifficultyProfile, m_artillerySpotters ) },
		{ "ArtilleryShare",						INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_artilleryShare ) },
		{ "InfluenceSeconds",					INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_influenceSeconds ) },
		{ "FireLookSeconds",					INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_fireLookSeconds ) },
		{ "FireWordRadius",						INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_fireWordRadius ) },
		{ "FireChargeOdds",						INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_fireChargeOdds ) },
		{ "FireMemorySeconds",				INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_fireMemorySeconds ) },
		{ "BaseWatchSeconds",					INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_baseWatchSeconds ) },
		{ "BaseWatchOdds",						INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_baseWatchOdds ) },
		{ "StrikeDodgeSeconds",				INI::parseReal, NULL, offsetof( AIDifficultyProfile, m_strikeDodgeSeconds ) },
		{ NULL, NULL, NULL, 0 }
	};

	static const char *names[ AISKILL_COUNT ] =
		{ "Easy", "Medium", "Brutal" };

	AsciiString name( ini->getNextToken() );
	Int which = -1;
	for( Int i = 0; i < AISKILL_COUNT; ++i )
		if( name.compareNoCase( names[ i ] ) == 0 )
			which = i;

	if( which < 0 )
	{
		DEBUG_LOG(("AI.ini SkillLevel '%s' is not one of Easy/Medium/Brutal\n", name.str()));
		which = AISKILL_BRUTAL;			// parse it somewhere rather than throw the block on the floor
	}

	ini->initFromINI( &((TAiData*)instance)->m_skill[ which ], myFieldParse );
}

TAiData::TAiData() : 
m_next(NULL), 
m_sideInfo(NULL), 
m_attackIgnoreInsignificantBuildings(false),
m_skirmishGroupFudgeValue(0.0f),
m_structureSeconds(0), 
m_teamSeconds(0), 
m_resourcesWealthy(0), 
m_resourcesPoor(0), 
m_forceIdleFramesCount(1),
m_structuresWealthyMod(1.0f),	// divisor: never default to 0
m_teamPoorMod(1.0f),			// divisor: never default to 0
m_teamResourcesToBuild(0),
m_guardInnerModifierAI(0),
m_guardOuterModifierAI(0),
m_guardInnerModifierHuman(0),
m_guardOuterModifierHuman(0),
m_guardChaseUnitFrames(0),
m_guardEnemyScanRate(LOGICFRAMES_PER_SECOND/2),
m_guardEnemyReturnScanRate(LOGICFRAMES_PER_SECOND),
m_wallHeight(0),
m_alertRangeModifier(0),
m_aggressiveRangeModifier(0),
m_attackPriorityDistanceModifier(0),
m_maxRecruitDistance(0),
m_skirmishBaseDefenseExtraDistance(0),
m_repulsedDistance(0),
m_enableRepulsors(false),
m_forceSkirmishAI(false),
m_rotateSkirmishBases(false),
m_attackUsesLineOfSight(true),
m_minInfantryForGroup(3),
m_minVehiclesForGroup(4),
m_minDistanceForGroup(100),
m_minClumpDensity(0.5f),
m_infantryPathfindDiameter(6),
m_vehiclePathfindDiameter(6),
m_supplyCenterSafeRadius(250),
m_rebuildDelaySeconds(10),
//Added By Sadullah Nader
//Initialization(s) inserted
m_distanceRequiresGroup(0.0f),
m_sideBuildLists(NULL),
m_structuresPoorMod(1.0f),		// divisor: never default to 0
m_teamWealthyMod(1.0f),			// divisor: never default to 0
m_aiDozerBoredRadiusModifier(2.0),
m_aiCrushesInfantry(true), 
m_maxRetaliateDistance(210.0f), 
m_retaliateFriendsRadius(120.0f)
//
{
	// the ladder starts at its shipped defaults; an AI.ini SkillLevel block overrides one rung
	for (Int i = 0; i < AISKILL_COUNT; ++i)
		m_skill[i] = s_defaultSkillLadder[i];
}

//-------------------------------------------------------------------------------------------------
void TAiData::crc( Xfer *xfer )
{
	xfer->xferReal( &m_structureSeconds );
	xfer->xferReal( &m_teamSeconds );
	xfer->xferInt( &m_resourcesWealthy );
	xfer->xferInt( &m_resourcesPoor );
	xfer->xferUnsignedInt( &m_forceIdleFramesCount );
	xfer->xferReal( &m_structuresWealthyMod );
	xfer->xferReal( &m_teamWealthyMod );
	xfer->xferReal( &m_structuresPoorMod );
	xfer->xferReal( &m_teamPoorMod );
	xfer->xferReal( &m_teamResourcesToBuild );
	xfer->xferReal( &m_guardInnerModifierAI );
	xfer->xferReal( &m_guardOuterModifierAI );
	xfer->xferReal( &m_guardInnerModifierHuman );
	xfer->xferReal( &m_guardOuterModifierHuman );
	xfer->xferUnsignedInt( &m_guardChaseUnitFrames );
	xfer->xferUnsignedInt( &m_guardEnemyScanRate );
	xfer->xferUnsignedInt( &m_guardEnemyReturnScanRate );
	xfer->xferReal( &m_alertRangeModifier );
	xfer->xferReal( &m_aggressiveRangeModifier );
	xfer->xferReal( &m_attackPriorityDistanceModifier );
	xfer->xferReal( &m_maxRecruitDistance );
	xfer->xferReal( &m_skirmishBaseDefenseExtraDistance );
	xfer->xferReal( &m_repulsedDistance );
	xfer->xferBool( &m_enableRepulsors );
	CRCGEN_LOG(("CRC after AI TAiData for frame %d is 0x%8.8X\n", TheGameLogic->getFrame(), ((XferCRC *)xfer)->getCRC()));

}  // end crc

//-----------------------------------------------------------------------------
void TAiData::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

}  // end xfer

//-----------------------------------------------------------------------------
void TAiData::loadPostProcess( void )
{

}  // end loadPostProcess

//-----------------------------------------------------------------------------
void AI::crc( Xfer *xfer )
{

	xfer->xferSnapshot( m_pathfinder );
	CRCGEN_LOG(("CRC after AI pathfinder for frame %d is 0x%8.8X\n", TheGameLogic->getFrame(), ((XferCRC *)xfer)->getCRC()));

	AsciiString marker;
	TAiData *aiData = m_aiData;
	while (aiData)
	{
		marker = "MARKER:TAiData";
		xfer->xferAsciiString(&marker);
		xfer->xferSnapshot( aiData );
		aiData = aiData->m_next;
	}

	for (std::list<AIGroup *>::iterator groupIt = m_groupList.begin(); groupIt != m_groupList.end(); ++groupIt)
	{
		if (*groupIt)
		{
			marker = "MARKER:AIGroup";
			xfer->xferAsciiString(&marker);
			xfer->xferSnapshot( (*groupIt) );
		}
	}

}  // end crc

//-----------------------------------------------------------------------------
void AI::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

}  // end xfer

//-----------------------------------------------------------------------------
void AI::loadPostProcess( void )
{

}  // end loadPostProcess


