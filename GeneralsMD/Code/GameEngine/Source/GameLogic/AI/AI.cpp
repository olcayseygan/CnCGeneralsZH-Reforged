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

// AI.cpp
// The Artificial Intelligence system
// Author: Michael S. Booth, November 2000
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/CRCDebug.h"
#include "Common/GameState.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
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
#include "Lib/FixBoundary.h"

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
}

/**
 * Reset the AI system in preparation for a new map
 */
void AI::reset( void )
{
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
		QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
	if( freq == 0 )
		return 0.0f;
	return (Real)( (double)(to - from) * 1000.0 / (double)freq );
}

void AI::update( void )
{
	Int64 start, afterPathfind, end;
	QueryPerformanceCounter( (LARGE_INTEGER *)&start );

	// Age the flow maps before anything reads them: the traffic left by last frame's jams decays,
	// and a clearance field made stale by a building going up is rebuilt at most once a second.
	m_pathfinder->decayTraffic();
	m_pathfinder->updateClearanceIfDirty();

	// Do pathfinding.
	m_pathfinder->processPathfindQueue();

	QueryPerformanceCounter( (LARGE_INTEGER *)&afterPathfind );

	// run player updates
	{
		AIPlayer::resetFrameProfile();
		ThePlayerList->UPDATE();
	}

	QueryPerformanceCounter( (LARGE_INTEGER *)&end );
	s_lastPathfindMS = aiElapsedMS( start, afterPathfind );
	s_lastPlayerUpdateMS = aiElapsedMS( afterPathfind, end );
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

	// parse the ini weapon definition
	ini->initFromINI( TheAI->m_aiData, TheAIFieldParseTable );

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

	return group;
}

/**
 * Destroy the given AI Group
 */
void AI::destroyGroup( AIGroup *group )
{
	std::list<AIGroup *>::iterator i = std::find( m_groupList.begin(), m_groupList.end(), group );

	// make sure group is actually in the list
	if (i == m_groupList.end())
		return;

	DEBUG_ASSERTCRASH(group != NULL, ("A NULL group made its way into the AIGroup list.. jkmcd"));

	// remove it
//	DEBUG_LOG(("***AIGROUP %x is being removed from m_groupList.\n", group ));
	m_groupList.erase( i );

	// destroy group
	group->deleteInstance();
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
			const Coord3D victimPos = objOther->getPositionFix()->toCoord3D();	// P6
			if (w->isWithinAttackRangeFromFirePoint(m_obj, objOther, &victimPos))
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

	const Fix rangeFx = fixFromReal( range );	// P7: the caller's range is still AI/INI data

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
			ObjectIterator *threatIter = ThePartitionManager->iterateObjectsInRangeFix(me, rangeFx, FROM_BOUNDINGSPHERE_2D, gatherFilters, ITER_SORTED_NEAR_TO_FAR);
			MemoryPoolObjectHolder threatHolder(threatIter);
			for (Object *theEnemy = threatIter->first(); theEnemy; theEnemy = threatIter->next())
			{
				const Fix dist = fixSqrt( ThePartitionManager->getDistanceSquaredFix(me, theEnemy, FROM_BOUNDINGSPHERE_2D) );

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
																		true, fixToReal( dist ), distanceModifier );	// P7
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
		Object* o = ThePartitionManager->getClosestObjectFix( me, rangeFx, FROM_BOUNDINGSPHERE_2D, filters );
		return o;
	}

	Object *bestEnemy = NULL;
	Int			effectivePriority=0;
	Int			actualPriority=0;
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRangeFix(me, rangeFx, FROM_BOUNDINGSPHERE_2D, gatherFilters, ITER_SORTED_NEAR_TO_FAR);
	MemoryPoolObjectHolder holder(iter);
	for (Object *theEnemy = iter->first(); theEnemy; theEnemy = iter->next()) 
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

		const Fix dist = fixSqrt( ThePartitionManager->getDistanceSquaredFix(me, theEnemy, FROM_BOUNDINGSPHERE_2D) );
		Int modifier = fixToReal( dist )/TheAI->getAiData()->m_attackPriorityDistanceModifier;	// P7
		Int modPriority = curPriority-modifier;
		if (modPriority < 1)
			modPriority = 1;
		const Bool takesLead = modPriority > effectivePriority ||
			(modPriority == effectivePriority && curPriority > actualPriority);
		if (takesLead && AI_filtersAllow(filters + numGatherFilters, theEnemy))
		{
			effectivePriority = modPriority;
			actualPriority = curPriority;
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

	return ThePartitionManager->getClosestObjectFix( me, fixFromReal( range ), FROM_BOUNDINGSPHERE_2D, filters );	// P7: range
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

	return ThePartitionManager->getClosestObjectFix( me, fixFromReal( range ), FROM_BOUNDINGSPHERE_2D, filters );	// P7: range
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
			Coord3D coord = object->getPositionFix()->toCoord3D();	// client: a debug icon
			coord.x += pos.X;
			coord.y += pos.Y;
			coord.z += pos.Z;

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
	* and answers far too late, Medium reacts in time and expands on its own, Brutal is the AI played
	* as well as it has been programmed to play.  A rung that plays wrong is read as a column here
	* rather than hunted through a switch(difficulty) somewhere.
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
	*                      scoutS maxSc react decis   cntr  mass  ttk   indiv team  infl  focus  save   harv  expand guard hoard  econ */
static const AIDifficultyProfile s_defaultSkillLadder[ AISKILL_COUNT ] =
{
	/* Easy      */ { 90.0f, 1, 10.0f,  0.00f, FALSE, 0.00f, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,     0, FALSE, FALSE },
	/* Medium    */ { 60.0f, 1,  5.0f,  0.25f, FALSE, 0.35f, TRUE,  FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,  FALSE, 10000, FALSE, FALSE },
	/* Brutal    */ { 25.0f, 2,  1.5f,  1.00f, TRUE,  0.50f, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,   4000, TRUE,  TRUE }
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


