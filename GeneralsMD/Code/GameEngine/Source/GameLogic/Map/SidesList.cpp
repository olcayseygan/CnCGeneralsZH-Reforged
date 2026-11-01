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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: SidesList.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: SidesList.cpp
//
// Created:   John Ahlquist, Nov 2001
//
// Desc:      Contains the information describing Sides (player, ai, neutral etc.)
//						in a scenario, including build lists for non-player sides.
//
//-----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <algorithm>

#include "Common/DataChunk.h"
#include "Common/FileSystem.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomMapGenerator.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"

static const Int K_SIDES_DATA_VERSION_1 = 1;
static const Int K_SIDES_DATA_VERSION_2 = 2;	// includes Team list.
static const Int K_SIDES_DATA_VERSION_3 = 3;	// includes Team list.

/* ********* SidesInfo class ****************************/
/**
 SidesInfo - Constructor.
*/
SidesInfo::SidesInfo(void) :
	m_pBuildList(NULL),
	m_scripts(NULL)
{
}

SidesInfo::SidesInfo(const SidesInfo& thatref) :
	m_pBuildList(NULL),
	m_scripts(NULL)
{
	*this = thatref;
}

/**
 SidesInfo - Destructor - 
*/
SidesInfo::~SidesInfo(void)
{
	clear();
}

void SidesInfo::init(const Dict* d)
{
	m_pBuildList->deleteInstance();
	m_pBuildList = NULL;
	m_dict.clear();
	if (m_scripts) 
		m_scripts->deleteInstance();
	m_scripts = NULL;
	if (d)
		m_dict = *d;
}

// ug, I hate having to overload stuff, but this makes it a lot easier to make copies safely
SidesInfo& SidesInfo::operator=(const SidesInfo& that)
{
	if (this != &that)
	{
		this->clear();
		this->m_dict = that.m_dict;

		BuildListInfo* thisBLTail = NULL;
		for (BuildListInfo* thatBL = that.m_pBuildList; thatBL; thatBL = thatBL->getNext())
		{
			BuildListInfo* thisBL = newInstance( BuildListInfo );	
			*thisBL = *thatBL;
			thisBL->setNextBuildList(NULL);

			if (thisBLTail)
				thisBLTail->setNextBuildList(thisBL);
			else
				this->m_pBuildList = thisBL;
				
			thisBLTail = thisBL;
		}

		if (that.m_scripts)
			this->m_scripts = that.m_scripts->duplicate();
		else
			this->m_scripts = NULL;
	}
	return *this;
}

/**
* SidesInfo::addToBuildList - Adds a build list entry as the nth entry.
*		
*/
void SidesInfo::addToBuildList(BuildListInfo *pBuildList, Int position)
{
	DEBUG_ASSERTLOG(pBuildList->getNext()==NULL, ("WARNING***Adding already linked element."));
	BuildListInfo *pCur = NULL;
	while (position) {
		position--;
		if (pCur==NULL) {
			pCur = m_pBuildList;
		} else {
			if (pCur->getNext()) {
				pCur = pCur->getNext();
			}	else {
				break; // at end of list.
			}
		}
	}
	if (pCur==NULL) {
		// add to front of list.
		pBuildList->setNextBuildList(m_pBuildList);
		m_pBuildList = pBuildList;
	}	else {
		pBuildList->setNextBuildList(pCur->getNext());
		pCur->setNextBuildList(pBuildList);
	}
}

/**
* SidesInfo::reorderInBuildList - Reorders a build list entry as the nth entry.
*		
*/
void SidesInfo::reorderInBuildList(BuildListInfo *pBuildList, Int newPosition)
{
	/*Int oldPos =*/ removeFromBuildList(pBuildList);
	addToBuildList(pBuildList, newPosition);
}

/**
* SidesInfo::removeFromBuildList - Removes a build list entry.
* Returns the position in the list that the item occupied.
*		
*/
Int SidesInfo::removeFromBuildList(BuildListInfo *pBuildList)
{
	DEBUG_ASSERTCRASH(pBuildList, ("Removing NULL list."));
	if (pBuildList==NULL) return 0;

	Int position = 0;

	if (pBuildList == m_pBuildList) {
		// First item in list, so update head.
		m_pBuildList = pBuildList->getNext();
	} else {
		position = 1;
		// Not the first item, so find the preceeding list element.
		BuildListInfo *pPrev = m_pBuildList;
		while (pPrev && (pPrev->getNext()!=pBuildList) ) {
			pPrev = pPrev->getNext();
			position++;
		}
		DEBUG_ASSERTCRASH(pPrev, ("Removing item not in list."));
		if (pPrev) {
			pPrev->setNextBuildList(pBuildList->getNext());
		}
	}
	pBuildList->setNextBuildList(NULL);
	return position;
}

/* ********* SidesList class ****************************/
/*extern*/ SidesList *TheSidesList = NULL;	 ///< singleton instance of SidesList
/**
 SidesList - Constructor.
*/
SidesList::SidesList(void) : m_numSides(0), m_numSkirmishSides(0), m_skirmishScriptsFromMap(false), m_forkAIMap(false)
{
}

/**
 SidesList - Destructor - 
*/
SidesList::~SidesList(void)
{
}

/**
 SidesList - reset - 
*/
void SidesList::reset(void)
{
	clear();
}

/**
 SidesList - clear - 
*/
void SidesList::clear(void)
{
	emptySides();
	emptyTeams();
	m_skirmishScriptsFromMap = false;
	m_forkAIMap = false;
}



/**
* SidesList::ParseSidesDataChunk - read a Sides chunk.
* Format is the newer CHUNKY format.
*	See SidesList::WriteSidesDataChunk for the writer.
*	Input: DataChunkInput 
*		
*/
Bool SidesList::ParseSidesDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	DEBUG_ASSERTCRASH(TheSidesList, ("TheSidesList is null"));

	if (TheSidesList==NULL) 
		return false;

	TheSidesList->clear();
	Int count = file.readInt();
	Int i, j;
	TheSidesList->emptySides();
	for (i=0; i<count; i++) 
	{
		if (i >= MAX_PLAYER_COUNT) break;
		Dict d =  file.readDict();
		TheSidesList->addSide(&d);
		BuildListInfo* pBuildList;
		Int count = file.readInt();
		for (j=0; j<count; j++) 
		{
			pBuildList = newInstance( BuildListInfo );
			pBuildList->setBuildingName(file.readAsciiString());
			pBuildList->setTemplateName(file.readAsciiString());
			Coord3D loc;
			loc.x = file.readReal();
			loc.y = file.readReal();
			loc.z = file.readReal();
			loc.z = 0; // force to ground level
			pBuildList->setLocation(loc);
			pBuildList->setAngle(file.readReal());
			pBuildList->setInitiallyBuilt(file.readByte());
			pBuildList->setNumRebuilds(file.readInt());
			if (info->version >= K_SIDES_DATA_VERSION_3)
			{
				pBuildList->setScript(file.readAsciiString());
				pBuildList->setHealth(file.readInt());
				pBuildList->setWhiner(file.readByte());
				pBuildList->setUnsellable(file.readByte());
				pBuildList->setRepairable(file.readByte());
			}
			TheSidesList->getSideInfo(i)->addToBuildList(pBuildList, j);
		}
	}	
	if (info->version >= K_SIDES_DATA_VERSION_2)
	{
		count = file.readInt();
		TheSidesList->emptyTeams();
		for (i=0; i<count; i++) 
		{
			Dict d =  file.readDict();
			TheSidesList->addTeam(&d);
		}
	}
	
	file.registerParser( AsciiString("PlayerScriptsList"), info->label, ScriptList::ParseScriptsDataChunk );
	if (!file.parse(NULL)) {
		throw(ERROR_CORRUPT_FILE_FORMAT);
	}
	ScriptList *scripts[MAX_PLAYER_COUNT];
	count = ScriptList::getReadScripts(scripts);
	for (i=0; i<count; i++) {
		if (i<TheSidesList->getNumSides()) {
			ScriptList *pSL = TheSidesList->getSideInfo(i)->getScriptList();
			pSL->deleteInstance();
			TheSidesList->getSideInfo(i)->setScriptList(scripts[i]);
			scripts[i] = NULL;
		} else {
			// Read in more players worth than we have.
			scripts[i]->deleteInstance();
			scripts[i] = NULL;
		}
	}
	TheSidesList->validateSides();

	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Incorrect data file length."));
	return true;
}


/**
* SidesList::WriteSidesDataChunk - Writes a Sides chunk.
* Format is the newer CHUNKY format.
*	See SidesList::ParseSidesDataChunk for the reader.
*	Input: DataChunkInput 
*		
*/
void SidesList::WriteSidesDataChunk(DataChunkOutput &chunkWriter)
{
	DEBUG_ASSERTCRASH(TheSidesList, ("TheSidesList is null"));
	if (TheSidesList==NULL) 
		return;
	/**********HEIGHT MAP DATA ***********************/
	chunkWriter.openDataChunk("SidesList", K_SIDES_DATA_VERSION_3);	
	
		chunkWriter.writeInt(TheSidesList->getNumSides());
		Int i;
		for (i=0; i<TheSidesList->getNumSides(); i++) {
			chunkWriter.writeDict(*TheSidesList->getSideInfo(i)->getDict());	
			BuildListInfo* pBuildList = TheSidesList->getSideInfo(i)->getBuildList();
			Int count = 0;
			while (pBuildList) {
				count++;
				pBuildList = pBuildList->getNext();
			}
			chunkWriter.writeInt(count);
			pBuildList = TheSidesList->getSideInfo(i)->getBuildList();
			while (pBuildList) {
				chunkWriter.writeAsciiString(pBuildList->getBuildingName());
				chunkWriter.writeAsciiString(pBuildList->getTemplateName());
				chunkWriter.writeReal(pBuildList->getLocation()->x);
				chunkWriter.writeReal(pBuildList->getLocation()->y);
				chunkWriter.writeReal(pBuildList->getLocation()->z);
				chunkWriter.writeReal(pBuildList->getAngle());
				chunkWriter.writeByte(pBuildList->isInitiallyBuilt());
				chunkWriter.writeInt(pBuildList->getNumRebuilds());
				// BEGIN stuff new to K_SIDES_DATA_VERSION_3
				chunkWriter.writeAsciiString(pBuildList->getScript());
				chunkWriter.writeInt(pBuildList->getHealth());
				chunkWriter.writeByte(pBuildList->getWhiner());
				chunkWriter.writeByte(pBuildList->getUnsellable());
				chunkWriter.writeByte(pBuildList->getRepairable());
				// END stuff new to K_SIDES_DATA_VERSION_3

				pBuildList = pBuildList->getNext();
			}
		}	
		
		// BEGIN stuff new to K_SIDES_DATA_VERSION_2
		chunkWriter.writeInt(TheSidesList->getNumTeams());
		for (i=0; i<TheSidesList->getNumTeams(); i++) {
			chunkWriter.writeDict(*TheSidesList->getTeamInfo(i)->getDict());	
		}
		// END stuff new to K_SIDES_DATA_VERSION_2

		ScriptList *scripts[MAX_PLAYER_COUNT];
		for (i=0; i<TheSidesList->getNumSides(); i++) {
			scripts[i] = TheSidesList->getSideInfo(i)->getScriptList();
		}
		ScriptList::WriteScriptsDataChunk(chunkWriter, scripts, TheSidesList->getNumSides());
	chunkWriter.closeDataChunk();

	Bool modified = TheSidesList->validateSides();
	DEBUG_ASSERTLOG(!modified, ("*** had to clean up sideslist on read"));
	modified = false;	// silence compiler warnings in release build
	
}

TeamsInfo *SidesList::findTeamInfo(AsciiString name, Int* index /*= NULL*/)
{
	return m_teamrec.findTeamInfo(name, index);
}

SidesInfo *SidesList::findSideInfo(AsciiString name, Int* index /*= NULL*/)
{
	for (int i = 0; i < m_numSides; i++) 
	{
		if (m_sides[i].getDict()->getAsciiString(TheKey_playerName) == name)
		{
			if (index)
				*index = i;
			return &m_sides[i];
		}
	}
	return NULL;
}

SidesInfo *SidesList::findSkirmishSideInfo(AsciiString name, Int* index /*= NULL*/)
{
	for (int i = 0; i < m_numSkirmishSides; i++) 
	{
		if (m_skirmishSides[i].getDict()->getAsciiString(TheKey_playerName) == name)
		{
			if (index)
				*index = i;
			return &m_skirmishSides[i];
		}
	}
	return NULL;
}

static AsciiString static_readPlayerNames[MAX_PLAYER_COUNT];
static Dict static_readPlayerDicts[MAX_PLAYER_COUNT];	///< beside the names, for a mod's scripts (loadModSkirmishScripts)
static TeamsInfoRec static_readModTeams;								///< a mod's ScriptTeams, before its players are renamed

/**
* ParsePlayersDataChunk - read players names data chunk.
* Format is the newer CHUNKY format.
*	Input: DataChunkInput 
*		
*/
#define K_PLAYERS_NAMES_FOR_SCRIPTS_VERSION_1 1
#define K_PLAYERS_NAMES_FOR_SCRIPTS_VERSION_2 2

static Bool ParsePlayersDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	Int readDicts = 0;
	if (info->version >= K_PLAYERS_NAMES_FOR_SCRIPTS_VERSION_2) {
		readDicts = file.readInt();
	}
	Int numNames = file.readInt();
	Int i;
	for (i=0; i<numNames; i++) {
		if (i>=MAX_PLAYER_COUNT) break;
		static_readPlayerNames[i] = file.readAsciiString();
		static_readPlayerDicts[i].clear();
		if (readDicts) {
			static_readPlayerDicts[i] = file.readDict();
		}
	}
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	return true;
}

/// a SkirmishScripts.scb's ScriptTeams, kept whole until its players are known
/// (loadModSkirmishScripts, prepareForMP_or_Skirmish)
static Bool ParseModTeamsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	while (!file.atEndOfChunk()) {
		Dict teamDict = file.readDict();
		static_readModTeams.addTeam(&teamDict);
	}
	return true;
}

/** What a mod's skirmish scripts call its own: its kept players and their default teams under
	* "M<n>~", and what they build, upgrade, research, fire or press read as the mod's where it defines
	* one.  Script, flag and counter names stay, Player::initFromDict qualifies those per player. */
struct ModScriptNames
{
	Int mod;
	AsciiString prefix;
	std::vector<AsciiString> players;

	Bool isPlayer(const AsciiString& name) const
	{
		return std::find(players.begin(), players.end(), name) != players.end();
	}
	AsciiString player(const AsciiString& name) const
	{
		if (!isPlayer(name))
			return name;
		AsciiString renamed = prefix;
		renamed.concat(name);
		return renamed;
	}
	/// a player's default team is "team<player>" (Player::setDefaultTeam), so it follows the player
	AsciiString team(const AsciiString& name) const
	{
		if (!name.startsWith("team") || !isPlayer(AsciiString(name.str() + 4)))
			return name;
		AsciiString renamed = "team";
		renamed.concat(player(AsciiString(name.str() + 4)));
		return renamed;
	}
	void parameter(Parameter *parm) const
	{
		ModNameType type;
		switch (parm->getParameterType())
		{
			case Parameter::SIDE: parm->friend_setString(player(parm->getString())); return;
			case Parameter::TEAM: parm->friend_setString(team(parm->getString())); return;
			case Parameter::OBJECT_TYPE: type = MODNAME_OBJECT; break;
			case Parameter::UPGRADE: type = MODNAME_UPGRADE; break;
			case Parameter::SCIENCE: type = MODNAME_SCIENCE; break;
			case Parameter::SPECIAL_POWER: type = MODNAME_SPECIALPOWER; break;
			case Parameter::COMMAND_BUTTON:
			case Parameter::COMMANDBUTTON_ABILITY:
			case Parameter::COMMANDBUTTON_ALL_ABILITIES: type = MODNAME_COMMANDBUTTON; break;
			default: return;
		}
		const AsciiString modded = TheModNamespace.modName(mod, type, parm->getString());
		if (type != MODNAME_OBJECT || !namesMapObject(parm->getString(), modded))
			parm->friend_setString(modded);
	}
	/** Maps stay retail's under every mod, so what stands on them is retail's: a mod's script asking
		* after a SupplyDock means the map's, and M<n>~SupplyDock stands nowhere.  Contra's stash trucks
		* never left home for a pile. */
	Bool namesMapObject(const AsciiString& name, const AsciiString& modded) const
	{
		const ThingTemplate *modTemplate = TheThingFactory->findTemplate(modded, FALSE);
		if (modTemplate == NULL || modded == name || TheThingFactory->findTemplate(name, FALSE) == NULL)
			return FALSE;
		for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
		{
			const PlayerTemplate *faction = ThePlayerTemplateStore->getNthPlayerTemplate(i);
			if (faction->getModIndex() == mod && faction->getSide() == modTemplate->getDefaultOwningSide())
				return FALSE;
		}
		return TRUE;
	}
	void script(Script *s) const
	{
		for (OrCondition *orCondition = s->getOrCondition(); orCondition; orCondition = orCondition->getNextOrCondition())
		{
			for (Condition *condition = orCondition->getFirstAndCondition(); condition; condition = condition->getNext())
			{
				for (Int i = 0; i < condition->getNumParameters(); ++i)
					parameter(condition->getParameter(i));
			}
		}
		ScriptAction *const actions[] = { s->getAction(), s->getFalseAction() };
		for (Int list = 0; list < 2; ++list)
		{
			for (ScriptAction *action = actions[list]; action; action = action->getNext())
			{
				for (Int i = 0; i < action->getNumParameters(); ++i)
					parameter(action->getParameter(i));
			}
		}
	}
	void scripts(ScriptList *list) const
	{
		for (Script *s = list->getScript(); s; s = s->getNext())
			script(s);
		for (ScriptGroup *group = list->getScriptGroup(); group; group = group->getNext())
		{
			for (Script *s = group->getScript(); s; s = s->getNext())
				script(s);
		}
	}
};

/** -modfactions: each mod's own SkirmishScripts.scb, for its factions' computer players.  A player of
	* the file whose faction the mod defines (FactionAmerica read as M<n>~FactionAmerica) is kept with
	* its scripts and teams, renamed so it never meets retail's player of the same name. */
void SidesList::loadModSkirmishScripts(void)
{
	m_modSkirmishSides.clear();
	m_modSkirmishTeamrec.clear();
	if (TheGlobalData == NULL || !TheGlobalData->m_modFactions)
		return;

	const AsciiString path = "data\\Scripts\\SkirmishScripts.scb";
	for (Int mod = 0; mod < (Int)TheGlobalData->m_mods.size(); ++mod)
	{
		// the stream reads the whole file as it opens, so the focus is back before the parse
		TheFileSystem->setFocus(mod);
		CachedFileInputStream theInputStream;
		const Bool opened = TheFileSystem->doesFileExist(path.str()) && theInputStream.open(path);
		TheFileSystem->setFocus(FILE_FOCUS_ALL);
		if (!opened)
			continue;

		DataChunkInput file(&theInputStream);
		file.registerParser( AsciiString("PlayerScriptsList"), AsciiString::TheEmptyString, ScriptList::ParseScriptsDataChunk );
		file.registerParser( AsciiString("ScriptsPlayers"), AsciiString::TheEmptyString, ParsePlayersDataChunk );
		file.registerParser( AsciiString("ScriptTeams"), AsciiString::TheEmptyString, ParseModTeamsDataChunk );
		const Bool parsed = file.parse(this);
		ScriptList *scripts[MAX_PLAYER_COUNT];
		const Int count = ScriptList::getReadScripts(scripts);

		ModScriptNames names;
		names.mod = mod;
		names.prefix.format("M%d~", mod + 1);
		const size_t firstSide = m_modSkirmishSides.size();
		for (Int i = 0; i < count; ++i)
		{
			// a file written without the players' dicts names its factions only through the map's player of that name
			AsciiString fileFaction = static_readPlayerDicts[i].getAsciiString(TheKey_playerFaction);
			SidesInfo *mapSide = findSkirmishSideInfo(static_readPlayerNames[i]);
			if (fileFaction.isEmpty() && mapSide != NULL)
				fileFaction = mapSide->getDict()->getAsciiString(TheKey_playerFaction);
			const AsciiString faction = TheModNamespace.modName(mod, MODNAME_PLAYERTEMPLATE, fileFaction);
			const PlayerTemplate *pt = ThePlayerTemplateStore->findPlayerTemplate(NAMEKEY(faction));
			if (!parsed || scripts[i] == NULL || pt == NULL || pt->getModIndex() != mod || !pt->isPlayableSide())
			{
				if (scripts[i])
					scripts[i]->deleteInstance();
				continue;
			}
			names.players.push_back(static_readPlayerNames[i]);
			Dict sideDict(static_readPlayerDicts[i]);
			sideDict.setAsciiString(TheKey_playerName, names.player(static_readPlayerNames[i]));
			sideDict.setAsciiString(TheKey_playerFaction, pt->getName());
			m_modSkirmishSides.push_back(SidesInfo());
			m_modSkirmishSides.back().init(&sideDict);
			m_modSkirmishSides.back().setScriptList(scripts[i]);
		}
		for (size_t s = firstSide; s < m_modSkirmishSides.size(); ++s)
			names.scripts(m_modSkirmishSides[s].getScriptList());

		const NameKeyType unitKeys[] = { TheKey_teamUnitType1, TheKey_teamUnitType2, TheKey_teamUnitType3,
			TheKey_teamUnitType4, TheKey_teamUnitType5, TheKey_teamUnitType6, TheKey_teamUnitType7, TheKey_teamTransport };
		Int teams = 0;
		for (Int t = 0; t < static_readModTeams.getNumTeams(); ++t)
		{
			Dict teamDict(*static_readModTeams.getTeamInfo(t)->getDict());
			const AsciiString owner = teamDict.getAsciiString(TheKey_teamOwner);
			if (!names.isPlayer(owner))
				continue;
			teamDict.setAsciiString(TheKey_teamOwner, names.player(owner));
			teamDict.setAsciiString(TheKey_teamName, names.team(teamDict.getAsciiString(TheKey_teamName)));
			for (size_t k = 0; k < ARRAY_SIZE(unitKeys); ++k)
			{
				Bool exists = FALSE;
				const AsciiString unit = teamDict.getAsciiString(unitKeys[k], &exists);
				if (exists)
					teamDict.setAsciiString(unitKeys[k], TheModNamespace.modName(mod, MODNAME_OBJECT, unit));
			}
			m_modSkirmishTeamrec.addTeam(&teamDict);
			++teams;
		}
		DEBUG_LOG(("MODNS M%d~ SkirmishScripts.scb: %d of %d players kept, %d teams%s\n", mod + 1,
			(Int)(m_modSkirmishSides.size() - firstSide), count, teams, parsed ? "" : ", unreadable"));

		static_readModTeams.clear();
		for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
		{
			static_readPlayerNames[i].clear();
			static_readPlayerDicts[i].clear();
		}
	}
}

Int SidesList::skirmishSideFit(const AsciiString& candidateSide, Int candidateMod, const AsciiString& side, Int mod)
{
	if (candidateSide != side)
		return 0;
	if (candidateMod == mod)
		return 2;
	return candidateMod < 0 ? 1 : 0;
}

SidesInfo *SidesList::findSkirmishSideFor(const PlayerTemplate *pt, TeamsInfoRec **teams)
{
	SidesInfo *found = findSkirmishSideNamed(pt->getSide(), pt->getModIndex(), teams);
	/* A faction the map's skirmish scripts were never written for (the fork's own, Turkiye) takes the
		 scripts of the original faction it is built on. */
	if (found == NULL)
	{
		const AsciiString oldSide = ThePlayerTemplateStore->getOldFactionSide(pt->getBaseSide());
		if (!oldSide.isEmpty() && oldSide != pt->getSide())
			found = findSkirmishSideNamed(oldSide, pt->getModIndex(), teams);
	}
	return found;
}

SidesInfo *SidesList::findSkirmishSideNamed(const AsciiString& side, Int mod, TeamsInfoRec **teams)
{
	SidesInfo *best = NULL;
	Int bestFit = 0;
	for (size_t i = 0; i < m_modSkirmishSides.size() && bestFit < 2; ++i)
	{
		const PlayerTemplate *spt = ThePlayerTemplateStore->findPlayerTemplate(NAMEKEY(m_modSkirmishSides[i].getDict()->getAsciiString(TheKey_playerFaction)));
		if (skirmishSideFit(spt->getSide(), spt->getModIndex(), side, mod) == 2)
		{
			best = &m_modSkirmishSides[i];
			bestFit = 2;
			if (teams)
				*teams = &m_modSkirmishTeamrec;
		}
	}
	for (Int i = 0; i < m_numSkirmishSides && bestFit < 2; ++i)
	{
		const PlayerTemplate *spt = ThePlayerTemplateStore->findPlayerTemplate(NAMEKEY(m_skirmishSides[i].getDict()->getAsciiString(TheKey_playerFaction)));
		const Int fit = spt ? skirmishSideFit(spt->getSide(), spt->getModIndex(), side, mod) : 0;
		if (fit > bestFit)
		{
			best = &m_skirmishSides[i];
			bestFit = fit;
			if (teams)
				*teams = &m_skirmishTeamrec;
		}
	}
	return best;
}

/*static*/ Bool SidesList::isForkAIMapPath(const AsciiString& mapPath)
{
	AsciiString path = mapPath;
	// a loaded save plays its own copy of the map from the save folder; the map it was started on decides
	if (TheGameState && TheGameState->isInSaveDirectory(path))
		path = TheGameState->getPristineMapName();
	if (path.isEmpty())
		return false;

	// The generator's maps sit in the player's own Maps folder, but the game made them.  Told by the
	// leaf name alone ("rmg_v17_..."), the way the lobby and the replay carry it: isGeneratedMapPath
	// also checks a template's CRC against this machine's copy of it, a local file.
	const char *back = path.reverseFind('\\');
	const char *forward = path.reverseFind('/');
	const char *sep = (forward != NULL && (back == NULL || forward > back)) ? forward : back;
	AsciiString leaf = sep ? sep + 1 : path.str();
	leaf.toLower();
	Int version = 0, seed = 0, players = 0, cells = 0;
	if (sscanf(leaf.str(), "rmg_v%d_%d_%dp_%dc", &version, &seed, &players, &cells) == 4 &&
			version == RANDOM_MAP_GENERATOR_VERSION)
		return true;

	// The player's own Maps folder first: with no user data folder it would be "Maps" itself, the
	// game's, and nothing tells the two apart, so the game's wins as it does for the lobby.  Then the
	// game's folder and its archives.  Anything else (a save's copy with no pristine name) is not ours.
	const AsciiString userData = TheGlobalData->getPath_UserData();
	if (!userData.isEmpty())
	{
		AsciiString userMaps = userData;
		userMaps.concat("Maps\\");
		if (path.startsWithNoCase(userMaps.str()))
			return false;
	}
	return path.startsWithNoCase("maps\\") || path.startsWithNoCase("maps/");
}

void SidesList::prepareForMP_or_Skirmish(void)
{
	m_forkAIMap = isForkAIMapPath(TheGlobalData->m_mapName);
	m_skirmishTeamrec.clear();
	Int i;
	for (i = 0; i < getNumTeams(); i++)
	{
		Dict *tdict = getTeamInfo(i)->getDict();
		m_skirmishTeamrec.addTeam(tdict);
	}
	m_teamrec.clear();

	for (i = 0; i < MAX_PLAYER_COUNT; i++) {
		m_skirmishSides[i].clear();
	}
	m_numSkirmishSides = 0;

	for (i = 0; i < m_numSides; i++) 
	{
		m_skirmishSides[m_numSkirmishSides] = m_sides[i];
		m_numSkirmishSides++;
		if (m_sides[i].getDict()->getAsciiString(TheKey_playerFaction) == "FactionCivilian") {
			// Don't remove FactionCivilian.
			continue;
		}
		if (m_numSides == 1) break;	// can't remove the last side.
		removeSide(i);
		i--;
	}
	Bool gotScripts = false;
	for (i=0; i<m_numSkirmishSides; i++) {
		if (m_skirmishSides[i].getDict()->getAsciiString(TheKey_playerFaction) == "FactionCivilian") {
			// Don't consider FactionCivilian.
			continue;
		}
		if (m_skirmishSides[i].getScriptList()==NULL) continue;
		if (m_skirmishSides[i].getScriptList()->getScript() != NULL || 
			m_skirmishSides[i].getScriptList()->getScriptGroup()!=NULL) {
			gotScripts = true;
		}
	}
	m_skirmishScriptsFromMap = gotScripts;
	if (gotScripts)
		DEBUG_LOG(("Skirmish map brings its own scripts: fork AI off, map %s\n", TheGlobalData->m_mapName.str()));
	loadModSkirmishScripts();
	if (!gotScripts) {
		AsciiString path = "data\\Scripts\\SkirmishScripts.scb";
		// A player's own map gets EA's scripts as EA shipped them: the fork's file carries its own
		// openings and its Hard without the free cash.  A -mod that ships its own SkirmishScripts.scb
		// keeps it, since that is the file that resolves; a mod with none (art only) changes nothing.
		// Either way the exe CRC hashes the resolved file and this one (GlobalData::computeExeCRC), so
		// two machines that disagree here never share a match.
		const char *retail = "data\\Scripts\\SkirmishScriptsRetail.scb";
		Bool inModArchive = FALSE;
		const Bool modScripts = TheFileSystem->modLooseFile(path, &inModArchive).isNotEmpty() || inModArchive;
		if (!m_forkAIMap && !modScripts && TheFileSystem->doesFileExist(retail))
			path = retail;
		DEBUG_LOG(("Skirmish map using standard scripts %s: fork AI %s, map %s\n", path.str(),
							 m_forkAIMap ? "on" : "off", TheGlobalData->m_mapName.str()));
		m_skirmishTeamrec.clear();
		CachedFileInputStream theInputStream;
		if (theInputStream.open(path)) { 
				ChunkInputStream *pStrm = &theInputStream;
				DataChunkInput file( pStrm );
				file.registerParser( AsciiString("PlayerScriptsList"), AsciiString::TheEmptyString, ScriptList::ParseScriptsDataChunk );
				file.registerParser( AsciiString("ScriptsPlayers"), AsciiString::TheEmptyString, ParsePlayersDataChunk );
				file.registerParser( AsciiString("ScriptTeams"), AsciiString::TheEmptyString, ParseModTeamsDataChunk );
				if (!file.parse(this)) {
					DEBUG_LOG(("ERROR - Unable to read in skirmish scripts.\n"));
					static_readModTeams.clear();
					return;
				}
				ScriptList *scripts[MAX_PLAYER_COUNT];
				Int count = ScriptList::getReadScripts(scripts);
				Int i;
				for (i=0; i<count; i++) {
					Int curSide = -1;
					Int j;
					for (j=0; j<m_numSkirmishSides; j++) {
 						AsciiString name = getSkirmishSideInfo(j)->getDict()->getAsciiString(TheKey_playerName);
						if (name == static_readPlayerNames[i]) {
							curSide = j;
							break;
						}
					}
					if (curSide == -1) {
						/* A map saved before a faction existed has no skirmish player for it: every retail map
							 lacks ShockWave's Armor, Special Weapons and Salvage generals.  Player::initFromDict then
							 found no scripts, made the seat human with no AI, and the base stood idle - and the
							 takeover click handed it to whoever clicked it.  The file's own player for that side
							 stands in, kept beside the map's, which nearly fill m_skirmishSides. */
						const PlayerTemplate *pt = ThePlayerTemplateStore->findPlayerTemplate(NAMEKEY(static_readPlayerDicts[i].getAsciiString(TheKey_playerFaction)));
						if (scripts[i] && pt && pt->isPlayableSide() && findSkirmishSideNamed(pt->getSide(), pt->getModIndex(), NULL) == NULL) {
							DEBUG_LOG(("Skirmish map has no player for %s, using %s from the standard scripts\n", pt->getSide().str(), static_readPlayerNames[i].str()));
							m_modSkirmishSides.push_back(SidesInfo());
							m_modSkirmishSides.back().init(&static_readPlayerDicts[i]);
							m_modSkirmishSides.back().setScriptList(scripts[i]);
							scripts[i] = NULL;
						}
						if (scripts[i])
							scripts[i]->deleteInstance();
						continue;
					}

					ScriptList *pSL = getSkirmishSideInfo(curSide)->getScriptList();
					getSkirmishSideInfo(curSide)->setScriptList(scripts[i]);
					scripts[i] = NULL;
					if (pSL)
						pSL->deleteInstance();
					scripts[i] = NULL;
				}
				for (Int t = 0; t < static_readModTeams.getNumTeams(); ++t) {
					const Dict *teamDict = static_readModTeams.getTeamInfo(t)->getDict();
					const AsciiString owner = teamDict->getAsciiString(TheKey_teamOwner);
					if (findSkirmishSideInfo(owner)) {
						addSkirmishTeam(teamDict);
						continue;
					}
					for (size_t s = 0; s < m_modSkirmishSides.size(); ++s) {
						if (m_modSkirmishSides[s].getDict()->getAsciiString(TheKey_playerName) == owner) {
							m_modSkirmishTeamrec.addTeam(teamDict);
							break;
						}
					}
				}
				static_readModTeams.clear();
				for (i=0; i<MAX_PLAYER_COUNT; i++) {
					static_readPlayerNames[i].clear();
					static_readPlayerDicts[i].clear();
				}
		}


	}
}


Bool SidesList::isPlayerDefaultTeam(TeamsInfo *t)
{
	// if our name is "teamfoo" and there is a player named "foo", we are a player-default team.
	AsciiString tname = t->getDict()->getAsciiString(TheKey_teamName);
	if (tname.startsWith("team"))
	{
		const char* rest = tname.str() + 4;
		for (int j = 0; j < m_numSides; j++)
		{
			AsciiString pname = m_sides[j].getDict()->getAsciiString(TheKey_playerName);
			if (strcmp(pname.str(), rest) == 0)
			{
				return true;
			}
		}
	}
	return false;
}

void SidesList::emptySides() 
{ 
	Int i;

	m_numSides = 0;
	m_numSkirmishSides = 0;
	for (i = 0; i < MAX_PLAYER_COUNT; i++) {
		m_sides[i].clear();
		m_skirmishSides[i].clear();
	}
	m_modSkirmishSides.clear();
}

void SidesList::emptyTeams()
{
	m_teamrec.clear();
	m_skirmishTeamrec.clear();
	m_modSkirmishTeamrec.clear();
}

void SidesList::addSide(const Dict* d)
{
	DEBUG_ASSERTCRASH(m_numSides < MAX_PLAYER_COUNT, ("too many players"));
	if (m_numSides < MAX_PLAYER_COUNT)
		m_sides[m_numSides++].init(d);
}

void SidesList::addTeam(const Dict* d)
{
	m_teamrec.addTeam(d);
}

void SidesList::addSkirmishTeam(const Dict* d)
{
	m_skirmishTeamrec.addTeam(d);
}

void SidesList::removeSide(Int i)
{
	if (i < 0 || i >= m_numSides || m_numSides <= 1)
		return;

	for ( ; i < m_numSides-1; i++)
		m_sides[i] = m_sides[i+1];

	for ( ; i < MAX_PLAYER_COUNT; i++)
		m_sides[i].clear();

	--m_numSides;
}

void SidesList::removeTeam(Int i)
{
	m_teamrec.removeTeam(i);
}

Bool SidesList::validateAllyEnemyList(const AsciiString& tname, AsciiString& allies)
{
		// owners/allies/enemies must be players.

	Bool modified = false;

	AsciiString str, newstr, token;
	
	str = allies;
	newstr.clear();
	while (str.nextToken(&token))
	{
		if (token == tname)
		{
			modified = true;
			continue;	// no allies/enemies with self
		}

		SidesInfo *si = findSideInfo(token);
		if (!si)
		{
			modified = true;
			continue;	// player not found.
		}

		if (!newstr.isEmpty())
			newstr.concat(" ");
		newstr.concat(token);
	}

	allies = newstr;
	return modified;
}

void SidesList::addPlayerByTemplate(AsciiString playerTemplateName)
{
	AsciiString playerName;
	UnicodeString playerDisplayName;
	Bool isHuman = false;

	if (playerTemplateName.isEmpty())
	{
		playerName.set("");	// magic code for "neutral"
		playerDisplayName = u"Neutral";
		isHuman = false;
	}
	else
	{
		playerName.set("Plyr");
		if (playerTemplateName.startsWith("Faction"))
		{
			playerName.concat(playerTemplateName.str() + 7);
		}
		else
		{
			playerName.concat(playerTemplateName);
		}
		playerDisplayName.translate(playerName);
		isHuman = true;
		// special-case "civilian"...
		if (playerName == "PlyrCivilian")
			isHuman = false;
	}

	Dict d;

	d.clear();
	d.setAsciiString(TheKey_playerName, playerName);	
	d.setBool(TheKey_playerIsHuman, isHuman);
	d.setUnicodeString(TheKey_playerDisplayName, playerDisplayName);
	d.setAsciiString(TheKey_playerFaction, playerTemplateName);
	d.setAsciiString(TheKey_playerAllies, AsciiString::TheEmptyString);
	d.setAsciiString(TheKey_playerEnemies, AsciiString::TheEmptyString);

	addSide(&d);

	AsciiString playerTeamName;
	playerTeamName.set("team");
	playerTeamName.concat(playerName);

	d.clear();
	d.setAsciiString(TheKey_teamName, playerTeamName);
	d.setAsciiString(TheKey_teamOwner, playerName);
	d.setBool(TheKey_teamIsSingleton, true);
	addTeam(&d);
}

Bool SidesList::validateSides()
{
	Bool modified = false;

	// ensure we have at least one player, and at least one neutral player.
	Int i;
	Int neutral = -1;
	Int num = getNumSides();
	for (i = 0; i < num; i++)
	{
		if (getSideInfo(i)->getDict()->getAsciiString(TheKey_playerName).isEmpty())
		{
			neutral = i;
			break;
		}
	}
	if (neutral == -1)
	{
		addPlayerByTemplate(AsciiString::TheEmptyString);
		modified = true;
	}

	// now ensure that every player has a proper "default team"
	for (i = 0; i < getNumSides(); i++) 
	{
		Dict *pdict = getSideInfo(i)->getDict();
		AsciiString pname = pdict->getAsciiString(TheKey_playerName);
		AsciiString tname("team");
		tname.concat(pname);
		TeamsInfo *ti = findTeamInfo(tname);
		if (ti)
		{
			// make sure the team owner points back to the player.
			if (ti->getDict()->getAsciiString(TheKey_teamOwner) != pname)
			{
				DEBUG_CRASH(("hmm, team owner mismatch (%s) (%s), this should not normally be possible",ti->getDict()->getAsciiString(TheKey_teamOwner).str(), pname.str()));
				ti->getDict()->setAsciiString(TheKey_teamOwner, pname);
				modified = true;
			}
			// default teams are always singletons.
			if (!ti->getDict()->getBool(TheKey_teamIsSingleton))
			{
				DEBUG_CRASH(("hmm, this should not normally be possible"));
				ti->getDict()->setBool(TheKey_teamIsSingleton, true);
				modified = true;
			}
		}
		else
		{
			DEBUG_LOG(("*** default team for player %s missing (should not be possible), adding it...\n",tname.str()));
			Dict d;
			d.setAsciiString(TheKey_teamName, tname);
			d.setAsciiString(TheKey_teamOwner, pname);
			d.setBool(TheKey_teamIsSingleton, true);
			addTeam(&d);
			modified = true;
		}

		AsciiString allies = pdict->getAsciiString(TheKey_playerAllies);
		AsciiString enemies = pdict->getAsciiString(TheKey_playerEnemies);

		// ensure all teams have valid allies & enemies.
		// (note that owners can be teams or players, but allies/enemies can only be teams.)
		if (validateAllyEnemyList(pname, allies))
		{
			DEBUG_LOG(("bad allies...\n"));
			pdict->setAsciiString(TheKey_playerAllies, allies);
			modified = true;
		}

		if (validateAllyEnemyList(pname, enemies))
		{
			DEBUG_LOG(("bad enemies...\n"));
			pdict->setAsciiString(TheKey_playerEnemies, enemies);
			modified = true;
		}
	}

	// ensure there's no overlap between team names and player names.
	// (if there is, the player wins and the team is whacked.)
validate_team_names:
	for (i = 0; i < getNumTeams(); i++)
	{
		Dict *tdict = getTeamInfo(i)->getDict();
		AsciiString tname = tdict->getAsciiString(TheKey_teamName);
		if (findSideInfo(tname))
		{
			DEBUG_CRASH(("name %s is duplicate between player and team, removing...\n",tname.str()));
			removeTeam(i);
			modified = true;
			goto validate_team_names;
		}
	}

	for (i = 0; i < getNumTeams(); i++)
	{
		Dict *tdict = getTeamInfo(i)->getDict();
		AsciiString tname = tdict->getAsciiString(TheKey_teamName);
		AsciiString towner = tdict->getAsciiString(TheKey_teamOwner);
		SidesInfo* si = findSideInfo(towner);
		if (si == NULL || towner == tname)
		{
			DEBUG_LOG(("bad owner %s; reparenting to neutral...\n",towner.str()));
			tdict->setAsciiString(TheKey_teamOwner, AsciiString::TheEmptyString);
			modified = true;
		}
//		if (tdict->getType(NAMEKEY(AsciiString("teamAllies"))) != Dict::DICT_NONE)
//			tdict->remove(NAMEKEY(AsciiString("teamAllies")));
//		if (tdict->getType(NAMEKEY(AsciiString("teamEnemies"))) != Dict::DICT_NONE)
//			tdict->remove(NAMEKEY(AsciiString("teamEnemies")));

	}

	return modified;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SidesList::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method 
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SidesList::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// xfer num sides
	Int sideCount = getNumSides();
	xfer->xferInt( &sideCount );
	if( sideCount != getNumSides() )
	{

		DEBUG_CRASH(( "SidesList::xfer - The sides list size has changed, this was not supposed to happen, you must version this method and figure out how to translate between old and new versions now\n" ));
		throw SC_INVALID_DATA;

	}  // end if

	// side data
	ScriptList *scriptList;
	Bool scriptListPresent;
	for( Int i = 0; i < sideCount; ++i )
	{

		// xfer script list data that can change
		scriptList = getSideInfo( i )->getScriptList();
		scriptListPresent = scriptList ? TRUE : FALSE;
		xfer->xferBool( &scriptListPresent );
		if( (scriptList == NULL && scriptListPresent == TRUE) ||
				(scriptList != NULL && scriptListPresent == FALSE) )
		{

			DEBUG_CRASH(( "SidesList::xfer - script list missing/present mismatch\n" ));
			throw SC_INVALID_DATA;

		}  // end if
		if( scriptListPresent )
			xfer->xferSnapshot( scriptList );

	}  // end for i

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SidesList::loadPostProcess( void )
{

}  // end loadPostProcess

/* ********* BuildListInfo class ****************************/
/**
 BuildListInfo - Constructor.
*/
BuildListInfo::BuildListInfo(void) :
m_nextBuildList(NULL),
m_renderObj(NULL),
m_shadowObj(NULL),
m_isInitiallyBuilt(false),
m_numRebuilds(0),
m_angle(0),
m_script(AsciiString::TheEmptyString),
m_health(100),
m_whiner(true),
m_unsellable(false),
m_repairable(true),
m_objectID(INVALID_ID),
m_objectTimestamp(0),
m_underConstruction(false),
m_isSupplyBuilding(false),
m_desiredGatherers(0),
m_currentGatherers(0),
m_automaticallyBuild(true),
m_priorityBuild(false),
m_buildingName(AsciiString::TheEmptyString)
{
	// Added by Sadullah Nader
	// these initialized values are necessary!!!
	m_location.zero();
	m_rallyPointOffset.x = 0.0f;
	m_rallyPointOffset.y = 0.0f;
	m_selected = FALSE;

	Int i;
	for (i=0; i<MAX_RESOURCE_GATHERERS; i++) 
	{
		m_resourceGatherers[i] = INVALID_ID;
	}
}


/**
 BuildListInfo - Destructor - note - if linked, deletes linked items.
*/
BuildListInfo::~BuildListInfo(void)
{
	if (m_nextBuildList) {
		BuildListInfo *cur = m_nextBuildList;
		BuildListInfo *next;
		while (cur) {
			next = cur->getNext();
			cur->setNextBuildList(NULL); // prevents recursion. 
			cur->deleteInstance();
			cur = next; 
		}
	}
}

void BuildListInfo::parseStructure(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	const char* c = ini->getNextToken();
	AsciiString tTemplateName(c);

	static const FieldParse myFieldParse[] = 
		{
			{ "Name",				INI::parseAsciiString,		NULL, offsetof( BuildListInfo, m_buildingName	 ) },
			{ "Location",		INI::parseCoord2D,				NULL, offsetof( BuildListInfo, m_location ) },
      { "Rebuilds",		INI::parseInt,						NULL, offsetof( BuildListInfo, m_numRebuilds ) },
      { "Angle",			INI::parseAngleReal,			NULL, offsetof( BuildListInfo, m_angle ) },
      { "InitiallyBuilt",			INI::parseBool,		NULL, offsetof( BuildListInfo, m_isInitiallyBuilt ) },
      { "RallyPointOffset",			INI::parseCoord2D,		NULL, offsetof( BuildListInfo, m_rallyPointOffset ) },
      { "AutomaticallyBuild",			INI::parseBool,	NULL, offsetof( BuildListInfo, m_automaticallyBuild ) },
			{ NULL,							NULL,											NULL, 0 }  // keep this last
		};

	BuildListInfo *buildInfo = newInstance( BuildListInfo );
	buildInfo->setTemplateName(tTemplateName);
	ini->initFromINI(buildInfo, myFieldParse);
	((AISideBuildList*)instance)->addInfo(buildInfo);
}


/**
 BuildListInfo - Duplicate - note - if linked, duplicates linked items.
*/
BuildListInfo *BuildListInfo::duplicate(void)
{
	BuildListInfo *first = newInstance( BuildListInfo );
	*first = *this;
	first->m_nextBuildList = NULL;
	BuildListInfo *next = this->m_nextBuildList;
	BuildListInfo *cur = first;
	while (next) {
		BuildListInfo *link = newInstance( BuildListInfo );
		*link = *next;
		link->m_nextBuildList = NULL;
		cur->m_nextBuildList = link;
		cur = link;
		next = next->m_nextBuildList;
	}
	return first;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void BuildListInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void BuildListInfo::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	xfer->xferAsciiString( &m_buildingName );
	xfer->xferAsciiString( &m_templateName );
	xfer->xferCoord3D( &m_location );
	xfer->xferCoord2D( &m_rallyPointOffset );
	xfer->xferReal( &m_angle );
	xfer->xferBool( &m_isInitiallyBuilt );
	xfer->xferUnsignedInt( &m_numRebuilds );
	xfer->xferAsciiString( &m_script );
	xfer->xferInt( &m_health );
	xfer->xferBool( &m_whiner );
	xfer->xferBool( &m_unsellable );
	xfer->xferBool( &m_repairable );
	xfer->xferBool( &m_automaticallyBuild );
	// m_renderObj we don't need to xfer this, its for the editor only
	// m_shadowObj we don't need to xfer this, its for the editor only
	// m_selected we don't need to xfer this, its for the editor only
	xfer->xferObjectID( &m_objectID );
	xfer->xferUnsignedInt( &m_objectTimestamp );
	xfer->xferBool( &m_underConstruction );
	xfer->xferUser( m_resourceGatherers, sizeof( ObjectID ) * MAX_RESOURCE_GATHERERS );
	xfer->xferBool( &m_isSupplyBuilding );
	xfer->xferInt( &m_desiredGatherers );
	xfer->xferBool( &m_priorityBuild );
	if (version>=2) {
		xfer->xferInt(&m_currentGatherers);
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void BuildListInfo::loadPostProcess( void )
{

}  // end loadPostProcess

/* ********* TeamsInfoRec class ****************************/
TeamsInfoRec::TeamsInfoRec() : 
	m_numTeams(0), m_numTeamsAllocated(0), m_teams(NULL)
{
}

TeamsInfoRec::TeamsInfoRec(const TeamsInfoRec& thatref) : 
	m_numTeams(0), m_numTeamsAllocated(0), m_teams(NULL)
{
	*this = thatref;
}

TeamsInfoRec::~TeamsInfoRec() 
{
	clear();
}

// ug, I hate having to overload stuff, but this makes it a lot easier to make copies safely
TeamsInfoRec& TeamsInfoRec::operator=(const TeamsInfoRec& thatref)
{
	const TeamsInfoRec* that = &thatref;
	if (this != that)
	{
		this->clear();
		for (int i = 0; i < that->m_numTeams; i++) 
		{
			this->addTeam(that->m_teams[i].getDict());
		}
	}
	return *this;
}

void TeamsInfoRec::clear() 
{ 
	Int i;

	for (i = 0; i < m_numTeamsAllocated; i++) 
		m_teams[i].clear(); 

	m_numTeams = 0; 
	m_numTeamsAllocated = 0;
	delete [] m_teams;
	m_teams = NULL;
}

TeamsInfo *TeamsInfoRec::findTeamInfo(AsciiString name, Int* index /*= NULL*/)
{
	for (int i = 0; i < m_numTeams; i++) 
	{
		if (m_teams[i].getDict()->getAsciiString(TheKey_teamName) == name)
		{
			if (index)
				*index = i;
			return &m_teams[i];
		}
	}
	return NULL;
}

void TeamsInfoRec::addTeam(const Dict* d)
{
	enum
	{
		TEAM_ALLOC_CHUNK = 8	///< how many teams to alloc at a time
	};

	DEBUG_ASSERTCRASH(m_numTeams < 2048, ("%d teams have been allocated (so far). This seems excessive.", m_numTeams ));
	if (m_numTeams >= m_numTeamsAllocated)
	{
	// pool[]ify
		TeamsInfo* nti = NEW TeamsInfo[m_numTeamsAllocated + TEAM_ALLOC_CHUNK];	// throws on failure
		
		Int i;

		for (i = 0; i < m_numTeams; i++)
			nti[i] = m_teams[i];

		for ( ; i < m_numTeamsAllocated + TEAM_ALLOC_CHUNK; i++) 
			nti[i].clear(); 
		
		delete [] m_teams;

		m_teams = nti;
		m_numTeamsAllocated += TEAM_ALLOC_CHUNK;
	}

	m_teams[m_numTeams++].init(d);
}

void TeamsInfoRec::removeTeam(Int i)
{
	if (i < 0 || i >= m_numTeams || m_numTeams <= 1)
		return;

	for ( ; i < m_numTeams-1; i++)
		m_teams[i] = m_teams[i+1];

	for ( ; i < m_numTeamsAllocated; i++)
		m_teams[i].clear();

	--m_numTeams;
}
