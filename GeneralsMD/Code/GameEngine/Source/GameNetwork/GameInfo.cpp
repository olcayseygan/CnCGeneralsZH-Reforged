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

// FILE: GameInfo.cpp //////////////////////////////////////////////////////
// game setup state info
// Author: Matthew D. Campbell, December 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/CRCDebug.h"
#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/GameState.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/Xfer.h"
#include "GameNetwork/FileTransfer.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/LANAPI.h"						// for testing packet size
#include "GameNetwork/LANAPICallbacks.h"	// for testing packet size
#include "strtok_r.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


GameInfo *TheGameInfo = NULL;

// GameSlot ----------------------------------------

GameSlot::GameSlot()
{
	reset();
}

void GameSlot::reset()
{
	m_state = SLOT_CLOSED; // decent default
	m_isAccepted = false;
	m_hasMap = true;
	m_color = -1;
	m_startPos = -1;
	m_playerTemplate = -1;
	m_teamNumber = -1;
	m_NATBehavior = FirewallHelperClass::FIREWALL_TYPE_SIMPLE;
	m_lastFrameInGame = 0;
	m_disconnected = FALSE;
	m_IP = 0;
	m_port = 0;
	m_isMuted = FALSE;
	m_hasSavedOriginalSetup = FALSE;
	m_origPlayerTemplate = -1;
	m_origStartPos = -1;
	m_origColor = -1;
}

void GameSlot::saveOriginalSetup( void )
{
	DEBUG_LOG(("GameSlot::saveOriginalSetup() - orig was color=%d, pos=%d, house=%d\n",
		m_origColor, m_origStartPos, m_origPlayerTemplate));
	m_origPlayerTemplate = m_playerTemplate;
	m_origStartPos = m_startPos;
	m_origColor = m_color;
	DEBUG_LOG(("GameSlot::saveOriginalSetup() - color=%d, pos=%d, house=%d\n",
		m_color, m_startPos, m_playerTemplate));

	m_hasSavedOriginalSetup = TRUE;
}

static Int getSlotIndex(const GameSlot *slot)
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (TheGameInfo->getConstSlot(i) == slot)
			return i;
	}
	return -1;
}

static Bool isSlotLocalAlly(const GameSlot *slot)
{
	Int slotIndex = getSlotIndex(slot);
	Int localIndex = TheGameInfo->getLocalSlotNum();
	const GameSlot *localSlot = TheGameInfo->getConstSlot(localIndex);

	// if either doesn't exist, not an ally
	if (slotIndex < 0 || localIndex < 0)
		return FALSE;

	// if slot is us, ally
	if (slotIndex == localIndex)
		return TRUE;

	// if slot is same team as us, ally
	if (slot->getTeamNumber() == localSlot->getTeamNumber() && slot->getTeamNumber() >= 0)
		return TRUE;

	// if we're an observer, we see all
	if (localSlot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
		return TRUE;

	// nope
	return FALSE;
}

UnicodeString GameSlot::getApparentPlayerTemplateDisplayName( void ) const
{
	if (TheMultiplayerSettings && TheMultiplayerSettings->showRandomPlayerTemplate() &&
		m_origPlayerTemplate == PLAYERTEMPLATE_RANDOM && !isSlotLocalAlly(this))
	{
		return TheGameText->fetch("GUI:Random");
	}
	else if (m_origPlayerTemplate == PLAYERTEMPLATE_OBSERVER)
	{
		return TheGameText->fetch("GUI:Observer");
	}
	DEBUG_LOG(("Fetching player template display name for player template %d (orig is %d)\n",
		m_playerTemplate, m_origPlayerTemplate));
	if (m_playerTemplate < 0)
	{
		return TheGameText->fetch("GUI:Random");
	}
	return ThePlayerTemplateStore->getNthPlayerTemplate(m_playerTemplate)->getDisplayName();
}

Int GameSlot::getApparentPlayerTemplate( void ) const
{
	if (TheMultiplayerSettings && TheMultiplayerSettings->showRandomPlayerTemplate() &&
		!isSlotLocalAlly(this))
	{
		return m_origPlayerTemplate;
	}
	return m_playerTemplate;
}

Int GameSlot::getApparentColor( void ) const
{
	if (TheMultiplayerSettings && m_origPlayerTemplate == PLAYERTEMPLATE_OBSERVER)
		return TheMultiplayerSettings->getColor(PLAYERTEMPLATE_OBSERVER)->getColor();

	if (TheMultiplayerSettings && TheMultiplayerSettings->showRandomColor() &&
		!isSlotLocalAlly(this))
	{
		return m_origColor;
	}
	return m_color;
}

Int GameSlot::getApparentStartPos( void ) const
{
	if (TheMultiplayerSettings && TheMultiplayerSettings->showRandomStartPos() &&
		!isSlotLocalAlly(this))
	{
		return m_origStartPos;
	}
	return m_startPos;
}


void GameSlot::unAccept( void )
{
	if (isHuman())
	{
		m_isAccepted = false;
	}
}

void GameSlot::setMapAvailability( Bool hasMap )
{
	if (isHuman())
	{
		m_hasMap = hasMap;
	}
}

/** Which slot states are an opponent seat.
	*
	* This used to be written out as a list in three places and no two of them agreed, which cost a
	* seat its name (it came back "Closed") and its start position (wiped on every menu refresh,
	* because the guard below did not recognise it as still being the same kind of seat).  One
	* function, three callers.
	*
	* SLOT_TAKEOVER counts here on purpose: to the lobby it is an ordinary opponent seat that the
	* host owns and that no network peer is expected to check in from.  It differs in one place only
	* - the player it creates is human (GameLogic::startNewGame), so no AI is ever attached. */
Bool IsAISlotState( SlotState state )
{
	switch( state )
	{
		case SLOT_EASY_AI:
		case SLOT_MED_AI:
		case SLOT_BRUTAL_AI:
		case SLOT_TAKEOVER:
			return TRUE;
		default:
			return FALSE;
	}
}

/** The one letter an AI seat is written as in the options string the host sends round.
	*
	* 'E', 'M' and 'H' are EA's own and keep their meaning - 'H' is Brutal, from when the menu called
	* that rung Hard.  The takeover seat took a free letter.  This is the whole table: a seat the
	* host cannot write a letter for used to go out as 'H', which handed everybody at the table a
	* Brutal opponent regardless of what the host had picked. */
static const struct { SlotState state; char code; } theAISlotCodes[] =
{
	{ SLOT_EASY_AI,      'E' },
	{ SLOT_MED_AI,       'M' },
	{ SLOT_BRUTAL_AI,    'H' },
	{ SLOT_TAKEOVER,     'P' },
};

char SlotStateToOptionsChar( SlotState state )
{
	for (Int i = 0; i < (Int)(sizeof(theAISlotCodes)/sizeof(theAISlotCodes[0])); ++i)
	{
		if (theAISlotCodes[i].state == state)
			return theAISlotCodes[i].code;
	}
	DEBUG_ASSERTCRASH(FALSE, ("No options string code for slot state %d", state));
	return 'H';
}

Bool OptionsCharToSlotState( char c, SlotState *state )
{
	for (Int i = 0; i < (Int)(sizeof(theAISlotCodes)/sizeof(theAISlotCodes[0])); ++i)
	{
		if (theAISlotCodes[i].code == c)
		{
			if (state)
				*state = theAISlotCodes[i].state;
			return TRUE;
		}
	}
	return FALSE;
}

/** What a seat is called wherever one is listed: the lobby's drop-down, the seat itself, the game
	* info panel, the online browser's tooltip.  Four switch statements used to answer this and no
	* two of them agreed, so the same rung read "Easy Army" in one list and "Easy AI" in the next,
	* and "GUI:HardAI" sat on the Brutal rung in one file and somewhere else in another.
	*
	* So the three rungs are named here, in one style, and none of them goes through the shipped
	* strings any more.  Open, Closed and the takeover seat are not rungs and keep their localised
	* strings. */
UnicodeString SlotStateName( SlotState state )
{
	switch( state )
	{
		case SLOT_OPEN:					return TheGameText->fetch("GUI:Open");
		case SLOT_TAKEOVER:			return TheGameText->fetch("GUI:HumanSlot");
		case SLOT_EASY_AI:			return UnicodeString( L"Easy AI" );
		case SLOT_MED_AI:				return UnicodeString( L"Medium AI" );
		case SLOT_BRUTAL_AI:		return UnicodeString( L"Hard AI" );
		case SLOT_CLOSED:
		default:								return TheGameText->fetch("GUI:Closed");
	}
}

void GameSlot::setState( SlotState state, UnicodeString name, UnsignedInt IP )
{
	// An opponent seat keeps its colour, faction, team and start spot while it stays an opponent
	// seat - SLOT_TAKEOVER included, or the menu's own refresh (which re-sends the state to the
	// slot) would wipe the faction and team the moment you picked them.
	if (!(isAI() && IsAISlotState( state )))
	{
		m_color = -1;
		m_startPos = -1;
		m_playerTemplate = -1;
		m_teamNumber = -1;

		if (state == SLOT_OPEN && TheGameSpyGame && TheGameSpyGame->getConstSlot(0) == this)
		{
			DEBUG_CRASH(("Game Is Hosed!\n"));
		}
	}
	if (state == SLOT_PLAYER)
	{
		reset();
		m_state = state;
		m_name = name;
	}// state == SLOT_PLAYER
	else
	{
		m_state = state;
		m_isAccepted = true;
		m_hasMap = true;
		m_name = SlotStateName( state );
	}

	m_IP = IP;
}

// Various tests
Bool GameSlot::isHuman( void ) const
{
	return m_state == SLOT_PLAYER;
}

Bool GameSlot::isOccupied( void ) const
{
	return m_state == SLOT_PLAYER || isAI();
}

Bool GameSlot::isAI( void ) const
{
	return IsAISlotState( m_state );
}

Bool GameSlot::isPlayer( AsciiString userName ) const
{
	UnicodeString uName;
	uName.translate(userName);
	return (m_state == SLOT_PLAYER && !m_name.compareNoCase(uName));
}

Bool GameSlot::isPlayer( UnicodeString userName ) const
{
	return (m_state == SLOT_PLAYER && !m_name.compareNoCase(userName));
}

Bool GameSlot::isPlayer( UnsignedInt ip ) const
{
	return (m_state == SLOT_PLAYER && m_IP == ip);
}

Bool GameSlot::isOpen( void ) const
{
	return m_state == SLOT_OPEN;
}

// GameInfo ----------------------------------------

GameInfo::GameInfo()
{
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		m_slot[i] = NULL;
	}
	reset();
}

void GameInfo::init( void )
{
	reset();
}

void GameInfo::reset( void )
{
	m_crcInterval = NET_CRC_INTERVAL;
	m_inGame = false;
	m_inProgress = false;
	m_gameID = 0;
	m_mapName = AsciiString("NOMAP");
	m_mapMask = 0;
	m_seed = GetTickCount(); //GameClientRandomValue(0, INT_MAX - 1);
	m_useStats = TRUE;
	m_surrendered = FALSE;
  m_oldFactionsOnly = FALSE;
	// Added By Sadullah Nader
	// Initializations missing and needed
//	m_localIP = 0; // BGC - actually we don't want this to be reset since the m_localIP is 
										// set properly in the constructor of LANGameInfo which uses this as a base class.
	m_mapCRC = 0;
	m_mapSize = 0;
  m_superweaponRestriction = 0;
  m_startingCash = TheGlobalData->m_defaultStartingCash;
  m_peaceTime = 0;
  m_unitLimit = FALSE;
  m_proRules = TRUE;
  m_incomeSharing = INCOME_SHARING_OFF;
  m_techRespawn = 0;
  m_supplyPileLimit = 0;
  m_ruleset = RULESET_NONE;

	//

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_slot[i])
			m_slot[i]->reset();
	}

	m_preorderMask = 0;
}

Bool GameInfo::isPlayerPreorder(Int index)
{
	if (index >= 0 && index < MAX_SLOTS)
		return ((m_preorderMask & (1 << index)) != 0);
	return FALSE;
}

void GameInfo::markPlayerAsPreorder(Int index)
{
	if (index >= 0 && index < MAX_SLOTS)
		m_preorderMask |= 1 << index;
}


void GameInfo::clearSlotList( void )
{
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_slot[i])
			m_slot[i]->setState(SLOT_CLOSED);
	}
}

Int GameInfo::getNumPlayers( void ) const
{
	Int numPlayers = 0;
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_slot[i] && m_slot[i]->isOccupied())
			numPlayers++;
	}
	return numPlayers;
}

Int GameInfo::getNumNonObserverPlayers( void ) const
{
	Int numPlayers = 0;
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_slot[i] && m_slot[i]->isOccupied() && m_slot[i]->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
			numPlayers++;
	}
	return numPlayers;
}

Int GameInfo::getMaxPlayers( void ) const
{
	if (!TheMapCache)
		return -1;

	AsciiString lowerMap = m_mapName;
	lowerMap.toLower();
	MapCache::iterator it = TheMapCache->find(lowerMap);
	if (it == TheMapCache->end())
		return -1;
	MapMetaData data = it->second;
	return data.m_numPlayers;
}

void GameInfo::enterGame( void )
{
	DEBUG_ASSERTCRASH(!m_inGame && !m_inProgress, ("Entering game at a bad time!"));
	reset();
	m_inGame = true;
	m_inProgress = false;
}

void GameInfo::leaveGame( void )
{
	DEBUG_ASSERTCRASH(m_inGame && !m_inProgress, ("Leaving game at a bad time!"));
	reset();
}

void GameInfo::startGame( Int gameID )
{
	DEBUG_ASSERTCRASH(m_inGame && !m_inProgress, ("Starting game at a bad time!"));
	m_gameID = gameID;
	closeOpenSlots();
	m_inProgress = true;
}

void GameInfo::endGame( void )
{
	DEBUG_ASSERTCRASH(m_inGame && m_inProgress, ("Ending game without playing one!"));
	m_inGame = false;
	m_inProgress = false;
}

void GameInfo::setSlot( Int slotNum, GameSlot slotInfo )
{
	DEBUG_ASSERTCRASH( slotNum >= 0 && slotNum < MAX_SLOTS, ("GameInfo::setSlot - Invalid slot number"));
	if (slotNum < 0 || slotNum >= MAX_SLOTS)
		return;

	DEBUG_ASSERTCRASH( m_slot[slotNum], ("NULL slot pointer"));
	if (!m_slot[slotNum])
		return;

//	Bool isHuman = slotInfo.isHuman();
//	Bool wasHuman = m_slot[slotNum]->isHuman();

	if (slotNum == 0)
	{
		slotInfo.setAccept();
		slotInfo.setMapAvailability(true);
	}
	*m_slot[slotNum] = slotInfo;

#ifdef DEBUG_LOGGING
	UnsignedInt ip = slotInfo.getIP();
#endif

	DEBUG_LOG(("GameInfo::setSlot - setting slot %d to be player %ls with IP %d.%d.%d.%d\n", slotNum, slotInfo.getName().str(),
							ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff));
}

GameSlot* GameInfo::getSlot( Int slotNum )
{
	DEBUG_ASSERTCRASH( slotNum >= 0 && slotNum < MAX_SLOTS, ("GameInfo::getSlot - Invalid slot number"));
	if (slotNum < 0 || slotNum >= MAX_SLOTS)
		return NULL;

	return m_slot[slotNum];
}

const GameSlot* GameInfo::getConstSlot( Int slotNum ) const
{
	DEBUG_ASSERTCRASH( slotNum >= 0 && slotNum < MAX_SLOTS, ("GameInfo::getSlot - Invalid slot number"));
	if (slotNum < 0 || slotNum >= MAX_SLOTS)
		return NULL;

	return m_slot[slotNum];
}

Int GameInfo::getLocalSlotNum( void ) const
{
	DEBUG_ASSERTCRASH(m_inGame, ("Looking for local game slot while not in game"));
	if (!m_inGame)
		return -1;

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot == NULL) {
			continue;
		}
		if (slot->isPlayer(m_localIP))
			return i;
	}
	return -1;
}

Int GameInfo::getSlotNum( AsciiString userName ) const
{
	DEBUG_ASSERTCRASH(m_inGame, ("Looking for game slot while not in game"));
	if (!m_inGame)
		return -1;

	UnicodeString uName;
	uName.translate(userName);
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot->isPlayer( uName ))
			return i;
	}
	return -1;
}

Bool GameInfo::amIHost( void ) const
{
	DEBUG_ASSERTCRASH(m_inGame, ("Looking for game slot while not in game"));
	if (!m_inGame)
		return false;

	return getConstSlot(0)->isPlayer(m_localIP);
}

void GameInfo::setMap( AsciiString mapName )
{
	m_mapName = mapName;
	if (m_inGame && amIHost())
	{
		const MapMetaData *mapData = TheMapCache->findMap( mapName );
		if (mapData)
		{
			m_mapMask = 1;
			AsciiString path = mapName;
			path.removeLastChar();
			path.removeLastChar();
			path.removeLastChar();
			path.concat("tga");
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'\n", path.str()));
			File *fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 2;
				fp->close();
				fp = NULL;
			}

			AsciiString newMapName;
			if (mapName.getLength() > 0)
			{
				AsciiString token;
				mapName.nextToken(&token, "\\/");
				// add all the tokens except the last one.
				// that way we don't add the filename, just the
				// directory name, we can do this since the filename
				// is just the directory name with the file extention
				// added onto it.
				while (mapName.find('\\') != NULL)
				{
					if (newMapName.getLength() > 0)
					{
						newMapName.concat('/');
					}
					newMapName.concat(token);
					mapName.nextToken(&token, "\\/");
				}
			}
			newMapName.concat("/map.ini");
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'\n", newMapName.str()));
			fp = TheFileSystem->openFile(newMapName.str());
			if (fp)
			{
				m_mapMask |= 4;
				fp->close();
				fp = NULL;
			}

			path = GetStrFileFromMap(m_mapName);
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'\n", path.str()));
			fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 8;
				fp->close();
				fp = NULL;
			}

			path = GetSoloINIFromMap(m_mapName);
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'\n", path.str()));
			fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 16;
				fp->close();
				fp = NULL;
			}

			path = GetAssetUsageFromMap(m_mapName);
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'\n", path.str()));
			fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 32;
				fp->close();
				fp = NULL;
			}

			path = GetReadmeFromMap(m_mapName);
			DEBUG_LOG(("GameInfo::setMap() - Looking for '%s'\n", path.str()));
			fp = TheFileSystem->openFile(path.str());
			if (fp)
			{
				m_mapMask |= 64;
				fp->close();
				fp = NULL;
			}
		}
		else
		{
			m_mapMask = 0;
		}
	}
}

void GameInfo::setMapContentsMask( Int mask )
{
	m_mapMask = mask;
}

void GameInfo::setMapCRC( UnsignedInt mapCRC )
{
	m_mapCRC = mapCRC;
	if (!TheMapCache)
		return;

	// check the map cache
	if (m_inGame && getLocalSlotNum() >= 0)
	{
		//TheMapCache->updateCache();
		AsciiString lowerMap = m_mapName;
		lowerMap.toLower();
		//DEBUG_LOG(("GameInfo::setMapCRC - looking for map file \"%s\" in the map cache\n", lowerMap.str()));
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it == TheMapCache->end())
		{
			/*
			DEBUG_LOG(("GameInfo::setMapCRC - could not find map file.\n"));
			it = TheMapCache->begin();
			while (it != TheMapCache->end())
			{
				DEBUG_LOG(("\t\"%s\"\n", it->first.str()));
				++it;
			}
			*/
			getSlot(getLocalSlotNum())->setMapAvailability(false);
		}
		else if (m_mapCRC != it->second.m_CRC)
		{
			DEBUG_LOG(("GameInfo::setMapCRC - map CRC's do not match (%X/%X).\n", m_mapCRC, it->second.m_CRC));
			getSlot(getLocalSlotNum())->setMapAvailability(false);
		}
		else
		{
			//DEBUG_LOG(("GameInfo::setMapCRC - map CRC's match.\n"));
			getSlot(getLocalSlotNum())->setMapAvailability(true);
		}
	}
}

void GameInfo::setMapSize( UnsignedInt mapSize )
{
	m_mapSize = mapSize;
	if (!TheMapCache)
		return;

	// check the map cache
	if (m_inGame && getLocalSlotNum() >= 0)
	{
		//TheMapCache->updateCache();
		AsciiString lowerMap = m_mapName;
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it == TheMapCache->end())
		{
			DEBUG_LOG(("GameInfo::setMapSize - could not find map file.\n"));
			getSlot(getLocalSlotNum())->setMapAvailability(false);
		}
		else if (m_mapCRC != it->second.m_CRC)
		{
			DEBUG_LOG(("GameInfo::setMapSize - map CRC's do not match.\n"));
			getSlot(getLocalSlotNum())->setMapAvailability(false);
		}
		else
		{
			//DEBUG_LOG(("GameInfo::setMapSize - map CRC's match.\n"));
			getSlot(getLocalSlotNum())->setMapAvailability(true);
		}
	}
}

void GameInfo::setSeed( Int seed )
{
	m_seed = seed;
}

void GameInfo::setSlotPointer( Int index, GameSlot *slot )
{
	if (index < 0 || index >= MAX_SLOTS)
		return;

	m_slot[index] = slot;
}

void GameInfo::setSuperweaponRestriction( UnsignedShort restriction )
{
  m_superweaponRestriction = restriction;
}

void GameInfo::setStartingCash( const Money & startingCash )
{
  m_startingCash = startingCash;
}

Bool GameInfo::hasAIPlayers( void ) const
{
  for (Int i = 0; i < MAX_SLOTS; ++i)
  {
    const GameSlot *slot = getConstSlot(i);
    if (slot && slot->isAI())
      return TRUE;
  }
  return FALSE;
}

void GameInfo::setPeaceTime( Int minutes )
{
  // the options string carries this across the wire, so a client sending nonsense must not be able
  // to freeze the match: an hour is more than any lobby offers and still finite
  if (minutes < 0)
    minutes = 0;
  if (minutes > 60)
    minutes = 60;
  m_peaceTime = minutes;
}

void GameInfo::setUnitLimit( Bool unitLimit )
{
  m_unitLimit = unitLimit;
}

void GameInfo::setProRules( Bool proRules )
{
  m_proRules = proRules;
}

// a value from a newer build or a hand-made options string reads as off
void GameInfo::setIncomeSharing( Int incomeSharing )
{
  m_incomeSharing = ( incomeSharing > INCOME_SHARING_OFF && incomeSharing < INCOME_SHARING_COUNT )
                    ? incomeSharing : INCOME_SHARING_OFF;
}

// clamped like peace time: the value arrives over the wire
void GameInfo::setTechRespawn( Int minutes )
{
  if (minutes < 0)
    minutes = 0;
  if (minutes > 60)
    minutes = 60;
  m_techRespawn = minutes;
}

// clamped the same way; a lobby has MAX_SLOTS players, so nothing above that says anything more
void GameInfo::setSupplyPileLimit( Int players )
{
  if (players < 0)
    players = 0;
  if (players > MAX_SLOTS)
    players = MAX_SLOTS;
  m_supplyPileLimit = players;
}

// a value from a newer build or a hand-made options string reads as the retail game
void GameInfo::setRuleset( Int ruleset )
{
  m_ruleset = ( ruleset > RULESET_NONE && ruleset < RULESET_COUNT ) ? ruleset : RULESET_NONE;
}

Bool GameInfo::isColorTaken(Int colorIdx, Int slotToIgnore ) const
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot && slot->getColor() == colorIdx && i != slotToIgnore)
			return true;
	}
	return false;
}

Bool GameInfo::isStartPositionTaken(Int positionIdx, Int slotToIgnore ) const
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot && slot->getStartPos() == positionIdx && i != slotToIgnore)
			return true;
	}
	return false;
}

void GameInfo::resetAccepted( void )
{
	GameSlot *slot = getSlot(0);
	if (slot)
		slot->setAccept();
	for(int i = 1; i< MAX_SLOTS; i++)
	{
		slot = getSlot(i);
		if (slot)
			slot->unAccept();
	}
}

void GameInfo::resetStartSpots()
{
	GameSlot *slot = NULL;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		slot = getSlot(i);
		if (slot != NULL)
		{
			slot->setStartPos(-1);
		}
	}
}

// adjust the slots in the game to open or closed
// depending on the players in there now and the number of
// players the map can hold.
void GameInfo::adjustSlotsForMap()
{
	const MapMetaData *md = TheMapCache->findMap(m_mapName);
	if (md != NULL)
	{
		// get the number of players allowed from the map.
		Int numPlayers = md->m_numPlayers;
		Int numPlayerSlots = 0;

		// first get the number of occupied slots.
		// i is used after the loop; VC6 for-scope let it escape.
		Int i;
		for (i = 0; i < MAX_SLOTS; ++i)
		{
			GameSlot *tempSlot = getSlot(i);
			if (tempSlot->isOccupied())
			{
				++numPlayerSlots;
			}
		}

		// now go through and close the appropriate number of slots.
		// note that no players are kicked in this process, we leave
		// that up to the user.
		for (i = 0; i < MAX_SLOTS; ++i)
		{
			// we have room for more players, if this slot is unoccupied, set it to open.
			GameSlot *slot = getSlot(i);
			if (numPlayers > numPlayerSlots)
			{
				if (!(slot->isOccupied()))
				{
					GameSlot newSlot;
					newSlot.setState(SLOT_OPEN);
					setSlot(i, newSlot);
					++numPlayerSlots;
				}
			}
			else
			{
				if (!(slot->isOccupied()))
				{
					// we don't have any more room, set this slot to closed.
					GameSlot newSlot;
					newSlot.setState(SLOT_CLOSED);
					setSlot(i, newSlot);
				}
			}
		}
	}
}

void GameInfo::closeOpenSlots()
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = getSlot(i);
		if (!(slot->isOccupied()))
		{
			GameSlot newSlot;
			newSlot.setState(SLOT_CLOSED);
			setSlot(i, newSlot);
		}
	}
}

static Bool isSlotLocalAlly(GameInfo *game, const GameSlot *slot)
{
	const GameSlot *localSlot = game->getConstSlot(game->getLocalSlotNum());
	if (!localSlot)
		return TRUE;

	if (slot == localSlot)
		return TRUE;

	if (slot->getTeamNumber() < 0)
		return FALSE;

	return slot->getTeamNumber() == localSlot->getTeamNumber();
}

Bool GameInfo::isSkirmish(void)
{
	Bool sawAI = FALSE;

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (i == getLocalSlotNum())
			continue;

		if (getConstSlot(i)->isHuman())
			return FALSE;

		if (getConstSlot(i)->isAI())
		{
			if (isSlotLocalAlly(getConstSlot(i)))
				return FALSE;
			sawAI = TRUE;
		}
	}
	return sawAI;
}

Bool GameInfo::isMultiPlayer(void)
{
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (i == getLocalSlotNum())
			continue;

		if (getConstSlot(i)->isHuman())
			return TRUE;
	}

	return FALSE;
}

Bool GameInfo::isSandbox(void)
{
	Int localSlotNum = getLocalSlotNum();
	Int localTeam = getConstSlot(localSlotNum)->getTeamNumber();
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (i == localSlotNum)
			continue;

		const GameSlot *slot = getConstSlot(i);
		if (slot->isOccupied() && (slot->getTeamNumber() < 0 || slot->getTeamNumber() != localTeam))
			return FALSE;
	}
	return TRUE;
}


// Convenience Functions ----------------------------------------

static const char slotListID		= 'S';

// The longest prefix of a UTF-8 name, at most maxBytes long, that does not cut a character in half:
// a continuation byte (10xxxxxx) at the cut belongs to the character before it.
static Int utf8PrefixLength( const AsciiString& name, Int maxBytes )
{
	const Int len = name.getLength();
	if (len <= maxBytes)
		return len;

	Int cut = maxBytes;
	while (cut > 0 && (((unsigned char)name.str()[cut]) & 0xC0) == 0x80)
		--cut;
	return cut;
}

static void truncatePlayerName( AsciiString& name, Int maxBytes )
{
	const Int cut = utf8PrefixLength(name, maxBytes);
	while (name.getLength() > cut)
		name.removeLastChar();
}

// Bytes of the first whole character of the name, 0 when there is none.
static Int getMinPlayerNameLength( const AsciiString& name )
{
	for (Int maxBytes = 1; maxBytes <= name.getLength(); ++maxBytes)
	{
		const Int cut = utf8PrefixLength(name, maxBytes);
		if (cut > 0)
			return cut;
	}
	return 0;
}

// Share maxPlayerNamesLength bytes between the human players' names, every one keeping at least its
// first character, and a name shorter than its share handing what it does not use to the ones after.
static Bool truncatePlayerNames( const GameInfo *game, AsciiString playerNames[MAX_SLOTS], Int maxPlayerNamesLength )
{
	Int minLengths[MAX_SLOTS] = { 0 };
	Int minTotalLength = 0;
	Int playerCount = 0;
	Int i;

	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (slot && slot->isHuman())
		{
			minLengths[i] = getMinPlayerNameLength(playerNames[i]);
			if (minLengths[i] == 0)
				return FALSE;
			minTotalLength += minLengths[i];
			++playerCount;
		}
	}

	if (playerCount == 0 || maxPlayerNamesLength < minTotalLength)
		return FALSE;

	Int remainingLength = maxPlayerNamesLength;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (slot && slot->isHuman())
		{
			const Int extraLength = (remainingLength - minTotalLength) / playerCount;
			truncatePlayerName(playerNames[i], minLengths[i] + extraLength);
			remainingLength -= playerNames[i].getLength();
			minTotalLength -= minLengths[i];
			--playerCount;
		}
	}

	return TRUE;
}

static AsciiString buildGameInfoAsciiString( const GameInfo *game, const AsciiString playerNames[MAX_SLOTS] );

//
// The LAN packet has room for m_lanMaxOptionsLength bytes of this.  EA shortened each name while it
// appended the slots, sharing what was left between the slots still to come; once the fixed fields
// had used it all the share went negative, the name emptied, and removeLastChar on an empty string
// looped forever - the host of a room with long names hung.  The string is built with whole names
// first and only rebuilt with shortened ones when it does not fit.
//
AsciiString GameInfoToAsciiString( const GameInfo *game )
{
	if (!game)
		return AsciiString::TheEmptyString;

	AsciiString playerNames[MAX_SLOTS];
	Int playerNamesLength = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (slot && slot->isHuman())
		{
			playerNames[i] = WideCharStringToMultiByte(slot->getName().str()).c_str();
			playerNamesLength += playerNames[i].getLength();
		}
	}

	AsciiString optionsString = buildGameInfoAsciiString(game, playerNames);
	Bool optionsFit = TheLAN == NULL || optionsString.getLength() <= m_lanMaxOptionsLength;
	if (!optionsFit)
	{
		const Int fixedLength = optionsString.getLength() - playerNamesLength;
		if (truncatePlayerNames(game, playerNames, m_lanMaxOptionsLength - fixedLength))
		{
			optionsString = buildGameInfoAsciiString(game, playerNames);
			optionsFit = optionsString.getLength() <= m_lanMaxOptionsLength;
		}
	}

	if (!optionsFit)
	{
		DEBUG_CRASH(("WARNING: options string cannot fit within the expected length!  Length is %d, but max is %d!\n",
			optionsString.getLength(), m_lanMaxOptionsLength));
		return AsciiString::TheEmptyString;
	}

	return optionsString;
}

static AsciiString buildGameInfoAsciiString( const GameInfo *game, const AsciiString playerNames[MAX_SLOTS] )
{
	AsciiString mapName = game->getMap();
	mapName = TheGameState->realMapPathToPortableMapPath(mapName);
	AsciiString newMapName;
	if (mapName.getLength() > 0)
	{
		AsciiString token;
		mapName.nextToken(&token, "\\/");
		// add all the tokens except the last one.
		// that way we don't add the filename, just the
		// directory name, we can do this since the filename
		// is just the directory name with the file extention
		// added onto it.
		while (mapName.find('\\') != NULL)
		{
			if (newMapName.getLength() > 0)
			{
				newMapName.concat('/');
			}
			newMapName.concat(token);
			mapName.nextToken(&token, "\\/");
		}
		DEBUG_LOG(("Map name is %s\n", mapName.str()));
	}

	AsciiString optionsString;
	optionsString.format("US=%d;M=%2.2x%s;MC=%X;MS=%d;SD=%d;C=%d;SR=%u;SC=%u;O=%c;PT=%d;UL=%d;PR=%d;IS=%d;TR=%d;PL=%d;", game->getUseStats(), game->getMapContentsMask(), newMapName.str(),
		game->getMapCRC(), game->getMapSize(), game->getSeed(), game->getCRCInterval(), game->getSuperweaponRestriction(),
		game->getStartingCash().countMoney(), game->oldFactionsOnly() ? 'Y' : 'N', game->getPeaceTime(),
		game->getUnitLimit() ? 1 : 0, game->getProRules() ? 1 : 0, game->getIncomeSharing(), game->getTechRespawn(),
		game->getSupplyPileLimit() );

	// only when one is set: an older build refuses a key it does not know, and the retail string stays
	// the string it always was
	if (game->getRuleset() != RULESET_NONE)
	{
		AsciiString ruleset;
		ruleset.format("RS=%d;", game->getRuleset());
		optionsString.concat(ruleset);
	}

	//add player info for each slot
	optionsString.concat(slotListID);
	optionsString.concat('=');
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);

		AsciiString str;
		if (slot && slot->isHuman())
		{
			AsciiString tmp;  //all this data goes after name
			tmp.format( ",%X,%d,%c%c,%d,%d,%d,%d,%d:",
				slot->getIP(), slot->getPort(),
				(slot->isAccepted()?'T':'F'),
				(slot->hasMap()?'T':'F'),
				slot->getColor(), slot->getPlayerTemplate(),
				slot->getStartPos(), slot->getTeamNumber(),
				slot->getNATBehavior() );

			str.format( "H%s%s", playerNames[i].str(), tmp.str() );
		}
		else if (slot && slot->isAI())
		{
			str.format("C%c,%d,%d,%d,%d:", SlotStateToOptionsChar(slot->getState()),
				slot->getColor(), slot->getPlayerTemplate(),
				slot->getStartPos(), slot->getTeamNumber());
		}
		else if (slot && slot->getState() == SLOT_OPEN)
		{
			str = "O:";
		}
		else if (slot && slot->getState() == SLOT_CLOSED)
		{
			str = "X:";
		}
		else
		{
			DEBUG_ASSERTCRASH(false, ("Bad slot type"));
			str = "X:";
		}
		optionsString.concat(str);
	}
	optionsString.concat(';');

	return optionsString;
}

static Int grabHexInt(const char *s)
{
	char tmp[5] = "0xff";
	tmp[2] = s[0];
	tmp[3] = s[1];
	Int b = strtol(tmp, NULL, 16);
	return b;
}
Bool ParseAsciiStringToGameInfo(GameInfo *game, AsciiString options)
{
	// Parse game options
	char *buf = strdup(options.str());
	char *bufPtr = buf;
	char *strPos, *keyValPair;
	GameSlot newSlot[MAX_SLOTS];
	Bool optionsOk = true;
	AsciiString mapName;
	Int mapContentsMask;
	UnsignedInt mapCRC, mapSize;
	Int seed = 0;
	Int crc = 100;
	Bool sawCRC = FALSE;
  Bool oldFactionsOnly = FALSE;
	Int useStats = TRUE;
  Money startingCash = TheGlobalData->m_defaultStartingCash;
  UnsignedShort restriction = 0; // Always the default
  Int peaceTime = 0; // absent from the string = off, so an older host is still joinable
  Bool unitLimit = FALSE; // the same
  Bool proRules = FALSE; // the same
  Int incomeSharing = INCOME_SHARING_OFF; // the same
  Int techRespawn = 0; // the same
  Int supplyPileLimit = 0; // the same
  Int ruleset = RULESET_NONE; // the same

	Bool sawMap, sawMapCRC, sawMapSize, sawSeed, sawSlotlist, sawUseStats, sawSuperweaponRestriction, sawStartingCash, sawOldFactions;
	sawMap = sawMapCRC = sawMapSize = sawSeed = sawSlotlist = sawUseStats = sawSuperweaponRestriction = sawStartingCash = sawOldFactions = FALSE;

	//DEBUG_LOG(("Saw options of %s\n", options.str()));
	DEBUG_LOG(("ParseAsciiStringToGameInfo - parsing [%s]\n", options.str()));


	while ( (keyValPair = strtok_r(bufPtr, ";", &strPos)) != NULL )
	{
		bufPtr = NULL; // strtok within the same string

		AsciiString key, val;
		char *pos = NULL;
		char *keyPtr, *valPtr;
		keyPtr = (strtok_r(keyValPair, "=", &pos));
		valPtr = (strtok_r(NULL, "\n", &pos));
		if (keyPtr)
			key = keyPtr;
		if (valPtr)
			val = valPtr;

		if (val.isEmpty())
		{
			optionsOk = false;
			DEBUG_LOG(("ParseAsciiStringToGameInfo - saw empty value, quitting\n"));
			break;
		}

		if (key.compare("US") == 0)
		{
			useStats = atoi(val.str());
			sawUseStats = true;
		}
		else
		if (key.compare("M") == 0)
		{
			if (val.getLength() < 3)
			{
				optionsOk = FALSE;
				DEBUG_LOG(("ParseAsciiStringToGameInfo - saw bogus map; quitting\n"));
				break;
			}
			mapContentsMask = grabHexInt(val.str());
			AsciiString tempstr;
			AsciiString token;
			tempstr = val.str()+2;
			tempstr.nextToken(&token, "\\/");
			while (tempstr.getLength() > 0)
			{
				mapName.concat(token);
				mapName.concat('\\');
				tempstr.nextToken(&token, "\\/");
			}
			mapName.concat(token);
			mapName.concat('\\');
			mapName.concat(token);
			mapName.concat('.');
			mapName.concat(TheMapCache->getMapExtension());
			mapName = TheGameState->portableMapPathToRealMapPath(mapName);
			sawMap = true;
			DEBUG_LOG(("ParseAsciiStringToGameInfo - map name is %s\n", mapName.str()));
		}
		else if (key.compare("MC") == 0)
		{
			mapCRC = 0;
			sscanf(val.str(), "%X", &mapCRC);
			sawMapCRC = true;
		}
		else if (key.compare("MS") == 0)
		{
			mapSize = atoi(val.str());
			sawMapSize = true;
		}
		else if (key.compare("SD") == 0)
		{
			seed = atoi(val.str());
			sawSeed = true;
//			DEBUG_LOG(("ParseAsciiStringToGameInfo - random seed is %d\n", seed));
		}
		else if (key.compare("C") == 0)
		{
			crc = atoi(val.str());
			sawCRC = TRUE;
		}
    else if (key.compare("SR") == 0 )
    {
      restriction = (UnsignedShort)atoi(val.str());
      sawSuperweaponRestriction = TRUE;
    }
    else if (key.compare("SC") == 0 )
    {
      UnsignedInt startingCashAmount = strtoul( val.str(), NULL, 10 );
      startingCash.init();
      startingCash.deposit( startingCashAmount, FALSE );
      sawStartingCash = TRUE;
    }
    else if (key.compare("O") == 0 )
    {
      oldFactionsOnly = ( val.compareNoCase( "Y" ) == 0 );
      sawOldFactions = TRUE;
    }
    else if (key.compare("PT") == 0 )
    {
      peaceTime = atoi(val.str());
    }
    else if (key.compare("UL") == 0 )
    {
      unitLimit = atoi(val.str()) != 0;
    }
    else if (key.compare("PR") == 0 )
    {
      proRules = atoi(val.str()) != 0;
    }
    else if (key.compare("IS") == 0 )
    {
      incomeSharing = atoi(val.str());
    }
    else if (key.compare("TR") == 0 )
    {
      techRespawn = atoi(val.str());
    }
    else if (key.compare("PL") == 0 )
    {
      supplyPileLimit = atoi(val.str());
    }
    else if (key.compare("RS") == 0 )
    {
      ruleset = atoi(val.str());
    }
		else if (key.getLength() == 1 && *key.str() == slotListID)
		{
			sawSlotlist = true;
			/// @TODO: Need to read in all the slot info... big mess right now.
			char *rawSlotBuf = strdup(val.str());
			char *freeMe = NULL;
			AsciiString rawSlot;
//			Bool slotsOk = true;	//flag that lets us know whether or not the slot list is good.

//			DEBUG_LOG(("ParseAsciiStringToGameInfo - Parsing slot list\n"));
			for (int i=0; i<MAX_SLOTS; ++i)
				{
					rawSlot = strtok_r(rawSlotBuf,":",&pos);
					if( rawSlotBuf )
						freeMe = rawSlotBuf;
					rawSlotBuf = NULL;
					switch (*rawSlot.str())
					{
						case 'H':
						{
//							DEBUG_LOG(("ParseAsciiStringToGameInfo - Human player\n"));
							char *slotPos = NULL;
							//Parse out the Name																
							AsciiString slotValue(strtok_r((char *)rawSlot.str(),",",&slotPos));
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue name is empty, quitting\n"));
								break;
							}
							UnicodeString name;
              				name.set(MultiByteToWideCharSingleLine(slotValue.str() +1).c_str());

							//DEBUG_LOG(("ParseAsciiStringToGameInfo - name is %s\n", slotValue.str()+1));
							
							//Parse out the IP
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue IP address is empty, quitting\n"));
								break;
							}
							UnsignedInt playerIP = 0;
							sscanf(slotValue.str(),"%x", &playerIP);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - IP address is %x\n", playerIP));
							
							//set the state of the slot
							newSlot[i].setState(SLOT_PLAYER, name, playerIP);

							// parse out the port
							slotValue = strtok_r(NULL, ",", &slotPos);
							if (slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue port is empty, quitting\n"));
								break;
							}
							UnsignedInt playerPort = 0;
							sscanf(slotValue.str(), "%d", &playerPort);
							newSlot[i].setPort(playerPort);
							DEBUG_LOG(("ParseAsciiStringToGameInfo - port is %d\n", playerPort));

							//Read if it's accepted or not
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.getLength() != 2)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue accepted is mis-sized, quitting\n"));
								break;
							}
							const char *svs = slotValue.str();
							if(*svs == 'T') {
								newSlot[i].setAccept();
								//DEBUG_LOG(("ParseAsciiStringToGameInfo - player has accepted\n"));
							} else if (*svs == 'F') {
								newSlot[i].unAccept();
								//DEBUG_LOG(("ParseAsciiStringToGameInfo - player has not accepted\n"));
							}
							++svs;
							if(*svs == 'T') {
								newSlot[i].setMapAvailability(TRUE);
								//DEBUG_LOG(("ParseAsciiStringToGameInfo - player has map\n"));
							} else {
								newSlot[i].setMapAvailability(FALSE);
								//DEBUG_LOG(("ParseAsciiStringToGameInfo - player does not have map\n"));
							}

							//Read color index
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue color is empty, quitting\n"));
								break;
							}
							Int color = atoi(slotValue.str());
							if (color < -1 || color >= TheMultiplayerSettings->getNumColors())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player color was invalid, quitting\n"));
								break;
							}
							newSlot[i].setColor(color);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player color set to %d\n", color));

							//Read playerTemplate index
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue player template is empty, quitting\n"));
								break;
							}
							Int playerTemplate = atoi(slotValue.str());
							if (playerTemplate < PLAYERTEMPLATE_MIN || playerTemplate >= ThePlayerTemplateStore->getPlayerTemplateCount())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player template value is invalid, quitting\n"));
								break;
							}
							newSlot[i].setPlayerTemplate(playerTemplate);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player template is %d\n", playerTemplate));

							//Read start position index
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue start position is empty, quitting\n"));
								break;
							}
							Int startPos = atoi(slotValue.str());
							if (startPos < -1 || startPos >= MAX_SLOTS)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player start position is invalid, quitting\n"));
								break;
							}
							newSlot[i].setStartPos(startPos);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player start position is %d\n", startPos));

							//Read team index
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue team number is empty, quitting\n"));
								break;
							}
							Int team = atoi(slotValue.str());
							if (team < -1 || team >= MAX_SLOTS/2)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - team number is invalid, quitting\n"));
								break;
							}
							newSlot[i].setTeamNumber(team);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - team number is %d\n", team));

							// Read the NAT behavior
							slotValue = strtok_r(NULL, ",",&slotPos);
							if (slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - NAT behavior is empty, quitting\n"));
								break;
							}
							FirewallHelperClass::FirewallBehaviorType NATType = (FirewallHelperClass::FirewallBehaviorType)atoi(slotValue.str());
							if ((NATType < FirewallHelperClass::FIREWALL_MIN) ||
									(NATType > FirewallHelperClass::FIREWALL_MAX)) {
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - NAT behavior is invalid, quitting\n"));
								break;
							}
							newSlot[i].setNATBehavior(NATType);
							DEBUG_LOG(("ParseAsciiStringToGameInfo - NAT behavior is %X\n", NATType));
						}// case 'H':
						break;
						case 'C':
						{
            	DEBUG_LOG(("ParseAsciiStringToGameInfo - AI player\n"));
							char *slotPos = NULL;
							//Parse out the Name																
							AsciiString slotValue(strtok_r((char *)rawSlot.str(),",",&slotPos));
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue AI Type is empty, quitting\n"));
								break;
							}
              
							SlotState aiState;
							if (OptionsCharToSlotState(*(slotValue.str() + 1), &aiState))
							{
								newSlot[i].setState(aiState);
							}
							else
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - Unknown AI, quitting\n"));
							}
              
							//Read color index
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue color is empty, quitting\n"));
								break;
							}
							Int color = atoi(slotValue.str());
							if (color < -1 || color >= TheMultiplayerSettings->getNumColors())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player color was invalid, quitting\n"));
								break;
							}
							newSlot[i].setColor(color);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player color set to %d\n", color));

							//Read playerTemplate index
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue player template is empty, quitting\n"));
								break;
							}
							Int playerTemplate = atoi(slotValue.str());
							if (playerTemplate < PLAYERTEMPLATE_MIN || playerTemplate >= ThePlayerTemplateStore->getPlayerTemplateCount())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - player template value is invalid, quitting\n"));
								break;
							}
							newSlot[i].setPlayerTemplate(playerTemplate);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - player template is %d\n", playerTemplate));

							//Read start pos
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue start pos is empty, quitting\n"));
								break;
							}
							Int startPos = atoi(slotValue.str());
							Bool isStartPosBad = FALSE;
							if (startPos < -1 || startPos >= MAX_SLOTS)
							{
								isStartPosBad = TRUE;
							}
							for (Int j=0; j<i; ++j)
							{
								if (startPos >= 0 && startPos == newSlot[i].getStartPos())
								{
									isStartPosBad = TRUE; // can't have multiple people using the same start pos
								}
							}
							if (isStartPosBad)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - start pos is invalid, quitting\n"));
								break;
							}
							newSlot[i].setStartPos(startPos);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - start spot is %d\n", startPos));

							//Read team index
							slotValue = strtok_r(NULL,",",&slotPos);
							if(slotValue.isEmpty())
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - slotValue team number is empty, quitting\n"));
								break;
							}
							Int team = atoi(slotValue.str());
							if (team < -1 || team >= MAX_SLOTS/2)
							{
								optionsOk = false;
								DEBUG_LOG(("ParseAsciiStringToGameInfo - team number is invalid, quitting\n"));
								break;
							}
							newSlot[i].setTeamNumber(team);
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - team number is %d\n", team));

						}//case 'C':
						break;
						case 'O':
						{
							newSlot[i].setState( SLOT_OPEN );
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - Slot is open\n"));
						}// case 'O':
						break;
						case 'X':
						{
							newSlot[i].setState( SLOT_CLOSED );
							//DEBUG_LOG(("ParseAsciiStringToGameInfo - Slot is closed\n"));
						}// case 'X':
						break;
						default:
						{								
							optionsOk = false;
							DEBUG_LOG(("ParseAsciiStringToGameInfo - unrecognized slot entry, quitting\n"));
						}
						break;
					}
				}
		if(freeMe)
			free(freeMe);
		}
		else
		{
			optionsOk = false;
			break;
		}
	}
	if( buf )
		free(buf);

	//DEBUG_LOG(("Options were ok == %d\n", optionsOk));
	if (optionsOk && sawMap && sawMapCRC && sawMapSize && sawSeed && sawSlotlist && sawCRC && sawUseStats && sawSuperweaponRestriction && sawStartingCash && sawOldFactions )
	{
		// We were setting the Global Data directly here, but Instead, I'm now 
		// first setting the data in game.  We'll set the global data when
		// we start a game.
		if (!game)
			return true;

		//DEBUG_LOG(("ParseAsciiStringToGameInfo - game options all good, setting info\n"));

		for(Int i = 0; i<MAX_SLOTS; i++)
			game->setSlot(i,newSlot[i]);

		game->setMap(mapName);
		game->setMapCRC(mapCRC);
		game->setMapSize(mapSize);
		game->setMapContentsMask(mapContentsMask);
		game->setSeed(seed);
		game->setCRCInterval(crc);
		game->setUseStats(useStats);
    game->setSuperweaponRestriction(restriction);
    game->setStartingCash( startingCash );
    game->setOldFactionsOnly( oldFactionsOnly );
    game->setPeaceTime( peaceTime );
    game->setUnitLimit( unitLimit );
    game->setProRules( proRules );
    game->setIncomeSharing( incomeSharing );
    game->setTechRespawn( techRespawn );
    game->setSupplyPileLimit( supplyPileLimit );
    game->setRuleset( ruleset );

		return true;
	}

	DEBUG_LOG(("ParseAsciiStringToGameInfo - game options messed up\n"));
	return false;
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//------------------------- SkirmishGameInfo ---------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SkirmishGameInfo::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void SkirmishGameInfo::xfer( Xfer *xfer )
{
	const XferVersion currentVersion = 11;	// 5 adds m_peaceTime, 6 m_unitLimit, 7 m_proRules, 8 m_incomeSharing, 9 m_techRespawn, 10 m_supplyPileLimit, 11 m_ruleset
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );


	xfer->xferInt(&m_preorderMask);
	xfer->xferInt(&m_crcInterval);
	xfer->xferBool(&m_inGame);
	xfer->xferBool(&m_inProgress);
	xfer->xferBool(&m_surrendered);
	xfer->xferInt(&m_gameID);

	Int slot = MAX_SLOTS;
	xfer->xferInt(&slot);
	DEBUG_ASSERTCRASH(slot==MAX_SLOTS, ("MAX_SLOTS changed, need to change version. jba."));

	for (slot = 0; slot < MAX_SLOTS; slot++) 
	{
		Int state = m_slot[slot]->getState();
		xfer->xferInt(&state);

		UnicodeString name=m_slot[slot]->getName();	
		if (version >= 2)
		{
			xfer->xferUnicodeString(&name);
		}

		Bool isAccepted=m_slot[slot]->isAccepted();
		xfer->xferBool(&isAccepted);

		Bool isMuted=m_slot[slot]->isMuted();
		xfer->xferBool(&isMuted);
		m_slot[slot]->mute(isMuted);

		Int color=m_slot[slot]->getColor();
		xfer->xferInt(&color);

		Int startPos=m_slot[slot]->getStartPos();
		xfer->xferInt(&startPos);

		Int playerTemplate=m_slot[slot]->getPlayerTemplate();
		xfer->xferInt(&playerTemplate);

		Int teamNumber=m_slot[slot]->getTeamNumber();
		xfer->xferInt(&teamNumber);

		Int origColor=m_slot[slot]->getOriginalColor();
		xfer->xferInt(&origColor);

		Int origStartPos=m_slot[slot]->getOriginalStartPos();
		xfer->xferInt(&origStartPos);

 		Int origPlayerTemplate=m_slot[slot]->getOriginalPlayerTemplate();
		xfer->xferInt(&origPlayerTemplate);

		if( xfer->getXferMode() == XFER_LOAD ) {
			m_slot[slot]->setState((SlotState)state, name);
			if (isAccepted) m_slot[slot]->setAccept();

			m_slot[slot]->setPlayerTemplate(origPlayerTemplate);
			m_slot[slot]->setStartPos(origStartPos);
			m_slot[slot]->setColor(origColor);
			m_slot[slot]->saveOriginalSetup();

			m_slot[slot]->setTeamNumber(teamNumber);
			m_slot[slot]->setColor(color);
			m_slot[slot]->setStartPos(startPos);
			m_slot[slot]->setPlayerTemplate(playerTemplate);
		}
	}

	xfer->xferUnsignedInt(&m_localIP);

	xfer->xferMapName(&m_mapName);
	xfer->xferUnsignedInt(&m_mapCRC);
	xfer->xferUnsignedInt(&m_mapSize);
	xfer->xferInt(&m_mapMask);
	xfer->xferInt(&m_seed);

  if ( version >= 3 )
  {
    xfer->xferUnsignedShort( &m_superweaponRestriction );
    
    if ( version == 3 )
    {
      // Version 3 had a bool which is now gone
      Bool obsoleteBool;
      xfer->xferBool( &obsoleteBool );
    }

    xfer->xferSnapshot( &m_startingCash );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_superweaponRestriction = 0;
    m_startingCash = TheGlobalData->m_defaultStartingCash;
  }

  if ( version >= 5 )
  {
    xfer->xferInt( &m_peaceTime );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_peaceTime = 0;
  }

  if ( version >= 6 )
  {
    xfer->xferBool( &m_unitLimit );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_unitLimit = FALSE;
  }

  if ( version >= 7 )
  {
    xfer->xferBool( &m_proRules );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_proRules = FALSE;
  }

  if ( version >= 8 )
  {
    xfer->xferInt( &m_incomeSharing );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_incomeSharing = INCOME_SHARING_OFF;
  }

  if ( version >= 9 )
  {
    xfer->xferInt( &m_techRespawn );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_techRespawn = 0;
  }

  if ( version >= 10 )
  {
    xfer->xferInt( &m_supplyPileLimit );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_supplyPileLimit = 0;
  }

  if ( version >= 11 )
  {
    xfer->xferInt( &m_ruleset );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_ruleset = RULESET_NONE;
  }

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SkirmishGameInfo::loadPostProcess( void )
{
}  // end loadPostProcess

//
// Random color, position and faction are drawn off the logical seed.  A restarted game puts the
// pre-draw values back so the same draws happen again.  The recorder writes the slot list before
// GameLogic::tryStartNewGame runs, so both call this: done only in tryStartNewGame, the replay of a
// restarted skirmish recorded the first game's drawn values and every playback of it mismatched.
//
void GameInfo::handleOriginalSetups( void )
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = getSlot(i);
		if (slot->hasSavedOriginalSetup())
		{
			slot->setColor(slot->getOriginalColor());
			slot->setStartPos(slot->getOriginalStartPos());
			slot->setPlayerTemplate(slot->getOriginalPlayerTemplate());
		}
		else
		{
			slot->saveOriginalSetup();
		}
	}
}


