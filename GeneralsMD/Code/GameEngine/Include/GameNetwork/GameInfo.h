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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GameInfo.h //////////////////////////////////////////////////////
// Generals game setup information
// Author: Matthew D. Campbell, December 2001

#pragma once

#ifndef __GAMEINFO_H__
#define __GAMEINFO_H__

#include "Common/Snapshot.h"
#include "Common/Money.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/FirewallHelper.h"
#include <vector>

enum SlotState
{
	SLOT_OPEN,
	SLOT_CLOSED,
	SLOT_EASY_AI,
	SLOT_MED_AI,
	SLOT_BRUTAL_AI,
	SLOT_PLAYER,
	// fork: an opponent seat with nothing behind it - no AI is ever created for it and you take
	// it over in game with Shift-Ctrl-T.  Appended on purpose: the values above are written into
	// save games and replays.
	SLOT_TAKEOVER
};

enum
{
	PLAYERTEMPLATE_RANDOM = -1,
	PLAYERTEMPLATE_OBSERVER = -2,
	PLAYERTEMPLATE_MIN = PLAYERTEMPLATE_OBSERVER
};

// A Random the player narrowed to a pool of factions.  The pool rides in the seat's faction number
// itself, PLAYERTEMPLATE_RANDOM_POOL - mask, so every slot string, LAN or online request, save and
// replay that carries a pick carries the pool too and every machine draws from the same list.  Bit n
// of the mask is the n-th template GetRandomPoolCandidates lists, the store's order, which the INI
// checksum already holds equal across machines.
enum
{
	PLAYERTEMPLATE_RANDOM_POOL = -16,
	RANDOM_POOL_BITS = 30		// ponytail: candidates past the 30th cannot be ticked into a pool; a wider mask needs a field of its own in the slot string
};

inline Bool IsRandomPlayerTemplate( Int pick ) { return pick == PLAYERTEMPLATE_RANDOM || pick < PLAYERTEMPLATE_RANDOM_POOL; }
/// a faction, Random, a Random pool or Observer, whatever the pick came from
Bool IsValidPlayerTemplatePick( Int pick );
/// the pick for a pool; no faction and the default pool are plain Random
Int EncodeRandomPool( UnsignedInt mask );
/// the pool a Random pick draws from; plain Random's is the default one
UnsignedInt DecodeRandomPool( Int pick );
/// the templates a Random may land on, in the store's order: playable, not a locked general
void GetRandomPoolCandidates( std::vector<Int> &candidates );
/// every candidate a template does not mark RandomExcluded
UnsignedInt GetDefaultRandomPool( void );
/// the templates a Random pick draws from in this game; a pool the old-factions rule empties draws as plain Random
void GetRandomDraw( Int pick, Bool oldFactionsOnly, std::vector<Int> &draw );
/// a pick as a preferences file keeps it, a Random pool by its armies' template names: the pool's bits
/// count candidates, and a faction added or dropped since the file was written renumbers them
AsciiString PlayerTemplatePreference( Int pick );
Int PlayerTemplateFromPreference( const AsciiString &text );
/// a pick as the slot string carries it: a Random pool as its mask in hex, the room's budget being 400 bytes
AsciiString PlayerTemplateSlotField( Int pick );
Int PlayerTemplateFromSlotField( const AsciiString &field );

// The lobby's handicap column, in percent of a unit's max health.
enum
{
	HANDICAP_FULL = 100,
	HANDICAP_STEP = 10
};

// The lobby's income sharing: which earnings are split evenly between a player and every ally
// still in the match.  The values travel in the options string and in saves, so only append.
enum IncomeSharing
{
	INCOME_SHARING_OFF,				// the retail game, everybody keeps what he earns
	INCOME_SHARING_TECH,			// captured tech buildings, the oil derricks
	INCOME_SHARING_ALL,				// every steady income: supply runs, hackers, derricks, black markets
	INCOME_SHARING_COUNT
};

/**
  * GameSlot class - maintains information about the contents of a
	* game slot.  This persists throughout the game.
	*/
class GameSlot
{
public:
	GameSlot();
	virtual void reset();

	void setAccept( void ) { m_isAccepted = true; }		///< Accept the current options
	void unAccept( void );														///< Unaccept (options changed, etc)
	Bool isAccepted( void ) const { return m_isAccepted; }	///< Non-human slots are always accepted

	void setMapAvailability( Bool hasMap );						///< Set whether the slot has the map
	Bool hasMap( void ) const { return m_hasMap; }		///< Non-human slots always have the map

	void setState( SlotState state,
		UnicodeString name = UnicodeString::TheEmptyString,
		UnsignedInt IP = 0);														///< Set the slot's state (human, AI, open, etc)
	SlotState getState( void ) const { return m_state; }		///< Get the slot state

	void setColor( Int color ) { m_color = color; }
	Int getColor( void ) const { return m_color; }

	void setStartPos( Int startPos ) { m_startPos = startPos; }
	Int getStartPos( void ) const { return m_startPos; }

	void setPlayerTemplate( Int playerTemplate ) 
	{ m_playerTemplate = playerTemplate;
		if (playerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			m_startPos = -1;
			m_handicap = HANDICAP_FULL;
		}
	 }
	Int getPlayerTemplate( void ) const { return m_playerTemplate; }

	void setTeamNumber( Int teamNumber ) { m_teamNumber = teamNumber; }
	Int getTeamNumber( void ) const { return m_teamNumber; }

	void setAIPersonality( Int persona ) { m_aiPersonality = persona; }	///< an AIPersonality; RANDOM stays RANDOM until the match starts
	Int getAIPersonality( void ) const { return m_aiPersonality; }

	void setHandicap( Int handicap ) { m_handicap = handicap; }	///< percent of max health this seat's units have, see IsValidHandicap
	Int getHandicap( void ) const { return m_handicap; }

	inline void setName( UnicodeString name ) { m_name = name; }
	inline UnicodeString getName( void ) const { return m_name; }

	inline void setIP( UnsignedInt IP ) { m_IP = IP; }
	inline UnsignedInt getIP( void ) const { return m_IP; }

	inline void setPort( UnsignedShort port ) { m_port = port; }
	inline UnsignedShort getPort( void ) const { return m_port; }

	inline void setNATBehavior( FirewallHelperClass::FirewallBehaviorType NATBehavior) { m_NATBehavior = NATBehavior; }
	inline FirewallHelperClass::FirewallBehaviorType getNATBehavior() const { return m_NATBehavior; }
	
	void saveOriginalSetup( void );
	Bool hasSavedOriginalSetup( void ) const { return m_hasSavedOriginalSetup; }
	inline Int getOriginalPlayerTemplate( void ) const	{ return m_origPlayerTemplate; }
	inline Int getOriginalColor( void ) const						{ return m_origColor; }
	inline Int getOriginalStartPos( void ) const				{ return m_origStartPos; }
	Int getApparentPlayerTemplate( void ) const;
	Int getApparentColor( void ) const;
	Int getApparentStartPos( void ) const;
	UnicodeString getApparentPlayerTemplateDisplayName( void ) const;

	// Various tests
	Bool isHuman( void ) const;															///< Is this slot occupied by a human player?
	Bool isOccupied( void ) const;													///< Is this slot occupied (by a human or an AI)?
	Bool isAI( void ) const;																///< Is this slot occupied by an AI?
	Bool isTakeover( void ) const { return m_state == SLOT_TAKEOVER; }	///< An AI seat with no AI: the player drives it
	Bool isPlayer( AsciiString userName ) const;						///< Does this slot contain the given user?
	Bool isPlayer( UnicodeString userName ) const;					///< Does this slot contain the given user?
	Bool isPlayer( UnsignedInt ip ) const;									///< Is this slot at this IP?
	Bool isOpen( void ) const;

	void setLastFrameInGame( UnsignedInt frame ) { m_lastFrameInGame = frame; }
	void markAsDisconnected( void ) { m_disconnected = TRUE; }
	UnsignedInt lastFrameInGame( void ) const { return m_lastFrameInGame; }
	Bool disconnected( void ) const { return isHuman() && m_disconnected; }

	void mute( Bool isMuted ) { m_isMuted = isMuted; }
	Bool isMuted( void ) const { return m_isMuted; }
protected:
	SlotState m_state;
	Bool m_isAccepted;
	Bool m_hasMap;
	Bool m_isMuted;
	Bool m_hasSavedOriginalSetup;											///< TRUE once the pre-randomization setup below was captured
	Int m_color;																			///< color, or -1 for random
	Int m_startPos;																		///< start position, or -1 for random
	Int m_playerTemplate;															///< PlayerTemplate
	Int m_teamNumber;																	///< alliance, -1 for none
	Int m_aiPersonality;															///< AIPersonality of an AI seat, AIPERSONA_NORMAL otherwise
	Int m_handicap;																		///< percent of max health this seat's units have, HANDICAP_FULL for none
	Int m_origColor;																			///< color, or -1 for random
	Int m_origStartPos;																		///< start position, or -1 for random
	Int m_origPlayerTemplate;															///< PlayerTemplate
	UnicodeString m_name;															///< Only valid for human players
	UnsignedInt m_IP;																	///< Only valid for human players in LAN/WOL
	UnsignedShort m_port;															///< Only valid for human players in LAN/WOL
	FirewallHelperClass::FirewallBehaviorType m_NATBehavior;	///< The NAT behavior for this slot's player.
	UnsignedInt m_lastFrameInGame;	// only valid for human players
	Bool m_disconnected;						// only valid for human players
};

/**
  * GameInfo class - maintains information about the game setup and
	* the contents of its slot list hroughout the game.
	*/
class GameInfo
{
public:
	GameInfo();
	
	void init( void );
	virtual void reset( void );

	void clearSlotList( void );

	Int getNumPlayers( void ) const;									///< How many players (human and AI) are in the game?
	Int getNumNonObserverPlayers( void ) const;				///< How many non-observer players (human and AI) are in the game?
	Int getMaxPlayers( void ) const;									///< How many players (human and AI) can be in the game?

	void enterGame( void );														///< Mark us as having entered the game
	void leaveGame( void );														///< Mark us as having left the game
	virtual void startGame( Int gameID );											///< Mark our game as started, and record the game ID
	void endGame( void );															///< Mark us as out of game
	inline Int getGameID( void ) const;								///< Get the game ID of the current game or the last one if we're not in game

	inline void setInGame( void );										///< set the m_inGame flag
	inline Bool isInGame( void ) const;											///< Are we (in game or in game setup)?  As opposed to chatting, matching, etc
	inline Bool isGameInProgress( void ) const;							///< Is the game in progress?
	inline void setGameInProgress( Bool inProgress ); ///< Set whether the game is in progress or not.
	void setSlot( Int slotNum, GameSlot slotInfo );		///< Set the slot state (human, open, AI, etc)
	GameSlot* getSlot( Int slotNum );									///< Get the slot
	const GameSlot* getConstSlot( Int slotNum ) const;	///< Get the slot
	void handleOriginalSetups( void );								///< a restart puts back the pre-random slot setup, a first start saves it
	virtual Bool amIHost( void ) const;															///< Convenience function - is the local player the game host?
	virtual Int getLocalSlotNum( void ) const;				///< Get the local slot number, or -1 if we're not present
	Int getSlotNum( AsciiString userName ) const;			///< Get the slot number corresponding to a specific user, or -1 if he's not present

	// Game options
	void setMap( AsciiString mapName );								///< Set the map to play on
	void setMapCRC( UnsignedInt mapCRC );							///< Set the map CRC
	void setMapSize( UnsignedInt mapSize );						///< Set the map size
	void setMapContentsMask( Int mask );							///< Set the map contents mask (1=map,2=preview,4=map.ini)
	inline AsciiString getMap( void ) const;								///< Get the game map
	inline UnsignedInt getMapCRC( void ) const;							///< Get the map CRC
	inline UnsignedInt getMapSize( void ) const;						///< Get the map size
	inline Int getMapContentsMask( void ) const;						///< Get the map contents mask
	void setSeed( Int seed );													///< Set the random seed for the game
	inline Int getSeed( void ) const;												///< Get the game seed
	inline Int getUseStats( void ) const;		///< Does this game count towards gamespy stats?
	inline void setUseStats( Int useStats );

  inline UnsignedShort getSuperweaponRestriction( void ) const; ///< Get any optional limits on superweapons
  void setSuperweaponRestriction( UnsignedShort restriction ); ///< Set the optional limits on superweapons
  inline const Money & getStartingCash(void) const; 
  void setStartingCash( const Money & startingCash );

	void setSlotPointer( Int index, GameSlot *slot );	///< Set the slot info pointer

	void setLocalIP( UnsignedInt ip ) { m_localIP =ip; }	///< Set the local IP
	UnsignedInt getLocalIP( void ) const { return m_localIP; }	///< Get the local IP

	Bool isColorTaken(Int colorIdx, Int slotToIgnore = -1 ) const;
	Bool isStartPositionTaken(Int positionIdx, Int slotToIgnore = -1 ) const;

	virtual void resetAccepted(void);															///< Reset the accepted flag on all players
	virtual void resetStartSpots(void);						///< reset the start spots for the new map.
	virtual void adjustSlotsForMap(void);					///< adjusts the slots to open and closed depending on the players in the game and the number of players the map can hold.

	virtual void closeOpenSlots(void);						///< close all slots that are currently unoccupied.

	// CRC checking hack
	// [1,100]: a lobby or replay string saying C=0 (or less) made GameLogic take m_frame % 0
	void setCRCInterval( Int val ) { m_crcInterval = (val<1)?1:((val<100)?val:100); }
	inline Int getCRCInterval( void ) const { return m_crcInterval; }
	
	Bool haveWeSurrendered(void) { return m_surrendered; }
	void markAsSurrendered(void) { m_surrendered = TRUE; }

	Bool isSkirmish(void); // TRUE if 1 human & 1+ AI are present && !isSandbox()
	Bool isMultiPlayer(void); // TRUE if 2+ human are present
	Bool isSandbox(void); // TRUE if everybody is on the same team
	
	Bool isPlayerPreorder(Int index);
	void markPlayerAsPreorder(Int index);

  inline Bool oldFactionsOnly(void) const;
  inline void setOldFactionsOnly( Bool oldFactionsOnly );

  // Minutes of peace at the head of the match: nobody can shoot anybody, and anything that
  // wanders up to somebody else's command center burns.  0 is the retail game.
  inline Int getPeaceTime( void ) const;
  void setPeaceTime( Int minutes );

  // The lobby's unit limit: UNIT_LIMIT_TOTAL units shared out evenly between the players.  Off is
  // the retail game.  UnitLimitPerPlayer in Player.h turns the player count into each one's share.
  inline Bool getUnitLimit( void ) const;
  void setUnitLimit( Bool unitLimit );

  // The lobby's Pro Rules check box (PRO-RULES.md, the list ProRulesBanThing and its neighbours in
  // Player.h hold).  On is the default for a lobby this build made; a host too old to send it is off.
  inline Bool getProRules( void ) const;
  void setProRules( Bool proRules );

  // The lobby's income sharing, an IncomeSharing from Player.h: what an ally's earnings are split
  // with the rest of his team.  Off is the retail game.
  inline Int getIncomeSharing( void ) const;
  void setIncomeSharing( Int incomeSharing );

  // Minutes a destroyed tech building lies in ruins before a neutral one stands on its spot again.
  // 0 is the retail game, where a destroyed tech building stays destroyed.
  inline Int getTechRespawn( void ) const;
  void setTechRespawn( Int minutes );

  // How many players may gather from one supply pile at the same time, allies counted one each.
  // 0 is the retail game, where a pile takes whoever drives up to it.
  inline Int getSupplyPileLimit( void ) const;
  void setSupplyPileLimit( Int players );

  // Turbo mode: the match runs twice as many logic frames a second, each one the same as at normal
  // speed.  The host's choice, so every machine and the replay agree on it.
  Bool getTurbo( void ) const { return m_turbo; }
  void setTurbo( Bool turbo ) { m_turbo = turbo; }

  Bool hasAIPlayers( void ) const;									///< is any slot held by a computer player?

protected:
	Int m_preorderMask;
	Int m_crcInterval;
	Bool m_inGame;
	Bool m_inProgress;
	Bool m_surrendered;
	Int m_gameID;
	GameSlot *m_slot[MAX_SLOTS];

	UnsignedInt m_localIP;

	// Game options
	AsciiString m_mapName;
	UnsignedInt m_mapCRC;
	UnsignedInt m_mapSize;
	Int m_mapMask;
	Int m_seed;
	Int m_useStats;
  Money         m_startingCash;
  UnsignedShort m_superweaponRestriction;
  Bool m_oldFactionsOnly; // Only USA, China, GLA -- not USA Air Force General, GLA Toxic General, et al
  Int m_peaceTime; // minutes of enforced peace at the start of the match, 0 = off
  Bool m_unitLimit; // the lobby's unit limit is on
  Bool m_proRules; // the lobby's Pro Rules are on
  Int m_incomeSharing; // an IncomeSharing, INCOME_SHARING_OFF = the retail game
  Int m_techRespawn; // minutes before a destroyed tech building comes back, 0 = never
  Int m_supplyPileLimit; // players who may gather from one supply pile at once, 0 = any number
  Bool m_turbo; // the lobby's turbo mode is on
};

extern GameInfo *TheGameInfo;

// Inline functions
Int					GameInfo::getGameID( void ) const								{ return m_gameID; }
AsciiString	GameInfo::getMap( void ) const									{ return m_mapName; }
UnsignedInt	GameInfo::getMapCRC( void ) const								{ return m_mapCRC; }
UnsignedInt	GameInfo::getMapSize( void ) const							{ return m_mapSize; }
Int					GameInfo::getMapContentsMask( void ) const			{ return m_mapMask; }
Int					GameInfo::getSeed( void ) const									{ return m_seed; }
Bool				GameInfo::isInGame( void ) const								{ return m_inGame; }
void				GameInfo::setInGame( void )											{ m_inGame = true; }
Bool				GameInfo::isGameInProgress( void ) const				{ return m_inProgress; }
void				GameInfo::setGameInProgress( Bool inProgress )	{ m_inProgress = inProgress; }
Int					GameInfo::getUseStats( void ) const             { return m_useStats; }
void				GameInfo::setUseStats( Int useStats )           { m_useStats = useStats; }
const Money&GameInfo::getStartingCash( void ) const         { return m_startingCash; }
UnsignedShort GameInfo::getSuperweaponRestriction( void ) const { return m_superweaponRestriction; }
Bool        GameInfo::oldFactionsOnly(void) const           { return m_oldFactionsOnly; }
void        GameInfo::setOldFactionsOnly( Bool oldFactionsOnly ) { m_oldFactionsOnly = oldFactionsOnly; }
/* A computer player does not honour a truce.  It stands still for five minutes and then plays the
	 same game it was going to play, so all peace time buys against a bot is a handicap for whoever
	 is not one.  A lobby with a bot in it therefore has no peace time whatever the host picked, and
	 the rule lives in the getter: the options string on the wire, the combo box and GameLogic all
	 read it here, so none of them has to remember.  The host's pick is kept, not cleared - take the
	 bot back out and it is still there. */
Int         GameInfo::getPeaceTime( void ) const            { return hasAIPlayers() ? 0 : m_peaceTime; }
Bool        GameInfo::getUnitLimit( void ) const            { return m_unitLimit; }
Bool        GameInfo::getProRules( void ) const             { return m_proRules; }
Int         GameInfo::getIncomeSharing( void ) const        { return m_incomeSharing; }
Int         GameInfo::getTechRespawn( void ) const          { return m_techRespawn; }
Int         GameInfo::getSupplyPileLimit( void ) const      { return m_supplyPileLimit; }

AsciiString GameInfoToAsciiString( const GameInfo *game );
Bool ParseAsciiStringToGameInfo( GameInfo *game, AsciiString options );

// The same with no length to fit and the map named in full, for the replay header the launcher reads.
AsciiString GameInfoToReplayString( const GameInfo *game );

// GameInfoToAsciiString with the map's portable path given and the length the string has to fit
// (0: any, and the map field in full), shortening the human players' names to fit; empty when even
// one letter a name is too long.  The tests build the string through this, with no TheGameState or
// TheLAN.
AsciiString GameInfoOptionsString( const GameInfo *game, const AsciiString& portableMapPath, Int maxLength );

// The options string's map field and back.  The field is the portable path's folders,
// "maps/twilight flame", "userdata/maps/rmg_v17_7_8p_672c".  The lobby's field, which has to leave
// the eight seats' names room in 400 bytes, gives a user map's folder alone, "my map", and a
// generated one in its wire form (generatedMapWireName).  Every form reads back to the path.
AsciiString GameInfoMapField( const AsciiString& portableMapPath );
AsciiString GameInfoLobbyMapField( const AsciiString& portableMapPath );
AsciiString GameInfoMapPathFromField( const AsciiString& field );

// An AI seat's personality is an optional sixth field on its slot entry, C<rung>,color,tmpl,start,team[,persona].
// AIPersonaSlotField is what goes after the team: nothing for Normal, so every Normal options string,
// replay header and Skirmish.ini stays the one it was, and ",<n>" otherwise.  AIPersonaFromSlotField
// reads that field back; absent (NULL or empty, every older string) or a value this build does not
// know is Normal, so a bad field costs the seat its personality and never the whole parse.
AsciiString AIPersonaSlotField( Int persona );
Int AIPersonaFromSlotField( const char *field );

// A seat's handicap is the last field of its slot entry, human or AI, written only when it is not
// HANDICAP_FULL, so every string without a pick stays the one it was.  An AI seat writes its persona
// as ",0" when a handicap has to follow it.  Absent, or a value the lobby does not offer, is full health.
Bool IsValidHandicap( Int handicap );
AsciiString HandicapSlotField( Int handicap );
Int HandicapFromSlotField( const char *field );

// A guest's "Handicap=<n>" request reaching the host.  TRUE when the slot list has to go round again,
// which it does for a pick that does not fit in the options string as well: the guest's box has to
// come back to what the host kept.
Bool ApplyHandicapRequest( GameInfo *game, GameSlot *slot, Int handicap );

//
// Three things every lobby - skirmish, LAN and online - has to agree on about a slot state, kept
// here so that appending a rung to the enum above is one edit rather than a hunt through the menus.
// IsAISlotState() is what GameSlot::isAI() answers with; the two char functions are the AI seat's
// one-letter code in the options string the host sends every client.
//
Bool IsAISlotState( SlotState state );
char SlotStateToOptionsChar( SlotState state );
Bool OptionsCharToSlotState( char c, SlotState *state );
UnicodeString SlotStateName( SlotState state );


/**
  * The SkirmishGameInfo class holds information about the skirmish game and
	* the contents of its slot list.
	*/

class SkirmishGameInfo : public GameInfo, public Snapshot
{
private:
	GameSlot m_skirmishSlot[MAX_SLOTS];

protected:
	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

public:
	SkirmishGameInfo()
	{
		for (Int i = 0; i< MAX_SLOTS; ++i)
			setSlotPointer(i, &m_skirmishSlot[i]);
	}
};

extern SkirmishGameInfo *TheSkirmishGameInfo;
extern SkirmishGameInfo *TheChallengeGameInfo;

#endif // __GAMEINFO_H__
