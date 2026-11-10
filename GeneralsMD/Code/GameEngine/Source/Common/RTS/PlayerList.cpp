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

// FILE: PlayerList.cpp /////////////////////////////////////////////////////////
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
// File name: PlayerList.cpp
//
// Created:   Steven Johnson, October 2001
//
// Desc:      @todo
//
//-----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/WideCharFns.h"

#include "Common/Errors.h"
#include "Common/DataChunk.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/Team.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#ifdef _DEBUG
#include "GameLogic/Object.h"
#endif
#include "GameLogic/GameLogic.h"
#include "GameLogic/SidesList.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkDefs.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
/*extern*/ PlayerList *ThePlayerList = NULL;

//-----------------------------------------------------------------------------
PlayerList::PlayerList() :
	m_local(NULL),
	m_playerCount(0)
{
	// we only allocate a few of these, so don't bother pooling 'em
	for (Int i = 0; i < MAX_PLAYER_COUNT; i++)
		m_players[ i ] = NEW Player( i );
	init();
}

//-----------------------------------------------------------------------------
PlayerList::~PlayerList() 
{
	try {
		// the world is happier if we reinit things before destroying them,
		// to avoid debug warnings
		init();
	} catch (...) {
		// nothing
	}
	for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
		delete m_players[ i ];
}

//-----------------------------------------------------------------------------
Player *PlayerList::getNthPlayer(Int i)
{
	LOGIC_WINDOW_READ( WINDOW_PLAYER );
	if( !isValidPlayerIndex( i ) )
	{
//		DEBUG_CRASH( ("Illegal player index\n") );
		return NULL;
	}
	return m_players[i]; 
}

//-----------------------------------------------------------------------------
Player *PlayerList::findPlayerWithNameKey(NameKeyType key)
{
	for (Int i = 0; i < m_playerCount; i++)
	{
		if (m_players[i]->getPlayerNameKey() == key)
		{
			return m_players[i];
		}
	}
	return NULL;
}

//-----------------------------------------------------------------------------
void PlayerList::reset()
{
	TheTeamFactory->clear(); // cleans up energy, among other things
	init();
}

//-----------------------------------------------------------------------------
void PlayerList::newGame()
{
	Int i;

	DEBUG_ASSERTCRASH(this != NULL, ("null this"));
	
	TheTeamFactory->clear(); // cleans up energy, among other things

	// first, re-init ourselves.
	init();

	// ok, now create the rest of players we need.
	Bool setLocal = false;
	for( i = 0; i < TheSidesList->getNumSides(); i++)
	{
		Dict *d = TheSidesList->getSideInfo(i)->getDict();
		AsciiString pname = d->getAsciiString(TheKey_playerName);
		if (pname.isEmpty())
			continue;	// it's neutral, which we've already done, so skip it.

		/// @todo The Player class should have a reset() method, instead of directly calling initFromDict() (MSB)
		Player* p = m_players[m_playerCount++];
		p->initFromDict(d);

		// Multiplayer override
		Bool exists;	// throwaway, since we don't care if it exists
		if (d->getBool(TheKey_multiplayerIsLocal, &exists))
		{
			DEBUG_LOG(("Player %s is multiplayer local\n", pname.str()));
			setLocalPlayer(p);
			setLocal = true;
		}

		if (!setLocal && !TheNetwork && d->getBool(TheKey_playerIsHuman))
		{
			setLocalPlayer(p);
			setLocal = true;
		}

		// Set the build list.
		p->setBuildList(TheSidesList->getSideInfo(i)->getBuildList());
		// Build list is attached to player now, so release it from the side info.
		TheSidesList->getSideInfo(i)->releaseBuildList();
	}

	if (!setLocal)
	{
		DEBUG_ASSERTCRASH(TheNetwork, ("*** Map has no human player... picking first nonneutral player for control\n"));
		for( i = 0; i < TheSidesList->getNumSides(); i++)
		{
			Player* p = getNthPlayer(i);
			if (p != getNeutralPlayer())
			{
				p->setPlayerType(PLAYER_HUMAN, false);
				setLocalPlayer(p);
				setLocal = true;
				break;
			}
		}
	}

	// must reset teams *after* creating players.
	TheTeamFactory->initFromSides(TheSidesList);

	for( i = 0; i < TheSidesList->getNumSides(); i++)
	{
		Dict *d = TheSidesList->getSideInfo(i)->getDict();
		Player* p = findPlayerWithNameKey(NAMEKEY(d->getAsciiString(TheKey_playerName)));

		AsciiString tok;

		AsciiString enemies = d->getAsciiString(TheKey_playerEnemies);
		while (enemies.nextToken(&tok))
		{
			Player *p2 = findPlayerWithNameKey(NAMEKEY(tok));
			if (p2)
			{
				p->setPlayerRelationship(p2, ENEMIES);
			}
			else
			{
				DEBUG_LOG(("unknown enemy %s\n",tok.str()));
			}
		}

		AsciiString allies = d->getAsciiString(TheKey_playerAllies);
		while (allies.nextToken(&tok))
		{
			Player *p2 = findPlayerWithNameKey(NAMEKEY(tok));
			if (p2)
			{
				p->setPlayerRelationship(p2, ALLIES);
			}
			else
			{
				DEBUG_LOG(("unknown ally %s\n",tok.str()));
			}
		}

		// finally, make sure self & neutral are correct.
		p->setPlayerRelationship(p, ALLIES);
		if (p != getNeutralPlayer())
			p->setPlayerRelationship(getNeutralPlayer(), NEUTRAL);
	}

	/* Every player gets its default team, walked by player rather than looked up by side name: a map
		 with two sides of one name made two players, the lookup above found the first twice, and the
		 second was left with no default team for the first enemy search to read. */
	for( i = 0; i < m_playerCount; i++ )
		m_players[i]->setDefaultTeam();

	/* Now that the players exist and are named, write down which network slot each one came from.
		 The two numbers are not the same and never were: player 0 is the neutral player, so a human
		 in slot 0 is player 1.  Everything in GameNetwork is indexed by the slot. */
	assignSlotIndices( TheGameInfo );

	/* The local player chosen above is the one the recording had, in playback too: the replay's game
		 info carries the recorded local slot, and the switch to the ReplayObserver comes later. */
	const Int originalMode = (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
													 ? TheRecorder->getGameMode() : TheGameLogic->getGameMode();
	m_keyboardPlayer = (originalMode == GAME_LAN || originalMode == GAME_INTERNET) ? NULL : m_local;
}

//-----------------------------------------------------------------------------
void PlayerList::init()
{
	m_playerCount = 1;
	m_players[0]->init(NULL);

	for (int i = 1; i < MAX_PLAYER_COUNT; i++)
		m_players[i]->init(NULL);

	for (int j = 0; j < MAX_PLAYER_COUNT; j++)
		m_slotIndices[j] = -1;

	m_keyboardPlayer = NULL;

	// call setLocalPlayer so that becomingLocalPlayer() gets called appropriately
	setLocalPlayer(m_players[0]);

}

//-----------------------------------------------------------------------------
void PlayerList::update()
{
	// update all players
	for( Int i = 0; i < MAX_PLAYER_COUNT; i++ )
	{
		m_players[i]->update();
	}  // end for i

}

//-----------------------------------------------------------------------------
void PlayerList::newMap()
{
	// update all players
	for( Int i = 0; i < MAX_PLAYER_COUNT; i++ )
	{
		m_players[i]->newMap();
	}  // end for i

}

// ------------------------------------------------------------------------
void PlayerList::teamAboutToBeDeleted(Team* team)
{
	for( Int i = 0; i < MAX_PLAYER_COUNT; i++ )
	{
		m_players[i]->removeTeamRelationship(team);
	}
}

//=============================================================================
void PlayerList::updateTeamStates(void) 
{
	// Clear team flags for all players.
	for( Int i = 0; i < MAX_PLAYER_COUNT; i++ )
	{
		m_players[i]->updateTeamStates();
	}  // end for i
}

//-----------------------------------------------------------------------------
Team *PlayerList::validateTeam( AsciiString owner )
{
	// owner could be a player or team. first, check team names.
	Team *t = TheTeamFactory->findTeam(owner);
	if (t)
	{
		//DEBUG_LOG(("assigned obj %08lx to team %s\n",obj,owner.str()));
	}	
	else
	{
		DEBUG_CRASH(("no team or player named %s could be found!\n", owner.str()));
		t = getNeutralPlayer()->getDefaultTeam();
	}
	return t;
}

//-----------------------------------------------------------------------------
void PlayerList::setLocalPlayer(Player *player)
{
	// can't set local player to null -- if you try, you get neutral.
	if (player == NULL)
	{
		DEBUG_CRASH(("local player may not be null"));
		player = getNeutralPlayer();
	}

	if (player != m_local)
	{
		// m_local can be null the very first time we call this.
		if (m_local)
			m_local->becomingLocalPlayer(false);
		m_local = player;
		player->becomingLocalPlayer(true);
	}

#ifdef INTENSE_DEBUG
	if (player)
	{
		DEBUG_LOG(("\n----------\n"));
		// did you know? you can use "%ls" to print a doublebyte string, even in a single-byte printf...
		DEBUG_LOG(("Switching local players. The new player is named '%s' (%s) and owns the following objects:\n",
			WideCharAsUtf8( player->getPlayerDisplayName().str() ).str(),
			TheNameKeyGenerator->keyToName(player->getPlayerNameKey()).str()
		));
		for (Object *obj = player->getFirstOwnedObject(); obj; obj = obj->getNextOwnedObject())
		{
			DEBUG_LOG(("Obj %08lx is of type %s",obj,obj->getTemplate()->getName().str()));
			if (!player->canBuild(obj->getTemplate()))
			{
				DEBUG_LOG((" (NOT BUILDABLE)"));
			}
			DEBUG_LOG(("\n"));
		}
		DEBUG_LOG(("\n----------\n"));
	}
#endif

}

//-----------------------------------------------------------------------------
Player *PlayerList::getPlayerFromMask( PlayerMaskType mask )
{
	Player *player = NULL;
	Int i;

	for( i = 0; i < MAX_PLAYER_COUNT; i++ )
	{
		
		player = getNthPlayer( i );
		if( player && player->getPlayerMask() == mask )
			return player;

	}  // end for i

	DEBUG_CRASH( ("Player does not exist for mask\n") );
	return NULL; // mask not found

}  // end getPlayerFromMask

//-----------------------------------------------------------------------------
Player *PlayerList::getEachPlayerFromMask( PlayerMaskType& maskToAdjust )
{
	Player *player = NULL;
	Int i;

	for( i = 0; i < MAX_PLAYER_COUNT; i++ )
	{
		
		player = getNthPlayer( i );
		if ( player && BitTest(player->getPlayerMask(), maskToAdjust ))
		{
			maskToAdjust &= (~player->getPlayerMask());
			return player;
		}
	}  // end for i

	DEBUG_CRASH( ("No players found that contain any matching masks.\n") );
	maskToAdjust = 0;
	return NULL; // mask not found
}


//-------------------------------------------------------------------------------------------------
PlayerMaskType PlayerList::getPlayersWithRelationship( Int srcPlayerIndex, UnsignedInt allowedRelationships )
{
	PlayerMaskType retVal = 0;

	if (allowedRelationships == 0)
		return retVal;

	Player *srcPlayer = getNthPlayer(srcPlayerIndex);
	if (!srcPlayer)
		return retVal;

	if (BitTest(allowedRelationships, ALLOW_SAME_PLAYER))
		BitSet(retVal, srcPlayer->getPlayerMask());

	for ( Int i = 0; i < getPlayerCount(); ++i )
	{
		Player *player = getNthPlayer(i);
		if (!player)
			continue;

		if (player == srcPlayer)
			continue;

		switch (srcPlayer->getRelationship(player->getDefaultTeam()))
		{
			case ENEMIES:
				if (BitTest(allowedRelationships, ALLOW_ENEMIES))
					BitSet(retVal, player->getPlayerMask());
				break;
			case ALLIES:
				if (BitTest(allowedRelationships, ALLOW_ALLIES))
					BitSet(retVal, player->getPlayerMask());
				break;
			case NEUTRAL:
				if (BitTest(allowedRelationships, ALLOW_NEUTRAL))
					BitSet(retVal, player->getPlayerMask());
				break;
		}
	}

	return retVal;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PlayerList::crc( Xfer *xfer )
{
	xfer->xferInt( &m_playerCount );

	for( Int i = 0; i < m_playerCount; ++i )
		xfer->xferSnapshot( m_players[ i ] );
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void PlayerList::xfer( Xfer *xfer )
{

	// version
	// 2: the keyboard seat, which newGame would otherwise reset to the local player and so undo a
	//    Shift-Ctrl-T for the shared-look hook
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// xfer the player count
	Int playerCount = m_playerCount;
	xfer->xferInt( &playerCount );

	//
	// sanity, the player count read from the file should match our player count that
	// was setup from the bare bones map load since that data can't change during run time
	//
	if( playerCount != m_playerCount )
	{

		DEBUG_CRASH(( "Invalid player count '%d', should be '%d'\n", playerCount, m_playerCount ));
		throw SC_INVALID_DATA;

	}  // end if

	// xfer each of the player data
	for( Int i = 0; i < playerCount; ++i )
		xfer->xferSnapshot( m_players[ i ] );

	if( version >= 2 )
	{
		Int keyboardIndex = m_keyboardPlayer ? m_keyboardPlayer->getPlayerIndex() : -1;
		xfer->xferInt( &keyboardIndex );
		m_keyboardPlayer = keyboardIndex >= 0 ? getNthPlayer( keyboardIndex ) : NULL;
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PlayerList::loadPostProcess( void )
{

}  // end postProcessLoad

//-----------------------------------------------------------------------------
void PlayerList::setSlotIndex( Int playerIndex, Int slotIndex )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT)
		m_slotIndices[playerIndex] = slotIndex;
}

//-----------------------------------------------------------------------------
Int PlayerList::getSlotIndex( Int playerIndex ) const
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT)
		return m_slotIndices[playerIndex];

	return -1;
}

//-----------------------------------------------------------------------------
/** Map every occupied network slot onto the player index that was built from it.  The slots are
    named "player0".."playerN" when the game is set up, which is the only link between the two. */
// a full lobby still leaves seats for the neutral player, the civilians and the replay observer
static_assert( MAX_SLOTS + 3 <= MAX_PLAYER_COUNT, "MAX_PLAYER_COUNT holds every slot and the map's own players" );
void PlayerList::assignSlotIndices( const GameInfo *gameInfo )
{
	for (int i = 0; i < MAX_PLAYER_COUNT; i++)
		m_slotIndices[i] = -1;

	if (gameInfo == NULL)
		return;

	AsciiString playerName;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const GameSlot *gameSlot = gameInfo->getConstSlot( slot );
		if (gameSlot == NULL || !gameSlot->isOccupied())
			continue;

		playerName.format( "player%d", slot );
		Player *player = findPlayerWithNameKey( TheNameKeyGenerator->nameToKey( playerName ) );
		if (player)
			setSlotIndex( player->getPlayerIndex(), slot );
	}
}
