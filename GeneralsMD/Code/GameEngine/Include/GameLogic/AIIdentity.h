// AIIdentity.h
// The general's own style for a skirmish computer player: always on, under the lobby personality.
// Author: Reforged, 2026-11-27

#pragma once

#ifndef _AI_IDENTITY_H_
#define _AI_IDENTITY_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

class AIPlayer;
class AIGroup;
class Object;
class SpecialPowerTemplate;
class Team;
class TeamPrototype;
class ThingTemplate;
class Xfer;
enum ScienceType : Int;

/** Every general a skirmish seat can play.  Written in saves through AIIdentity::xfer; append only. */
enum AIGeneral
{
	AIGENERAL_NONE = 0,				///< a side no identity knows (Boss, civilians, a mod's faction): the shared AI alone
	AIGENERAL_USA,
	AIGENERAL_USA_LASER,
	AIGENERAL_USA_SUPERWEAPON,
	AIGENERAL_USA_AIR_FORCE,
	AIGENERAL_CHINA,
	AIGENERAL_CHINA_TANK,
	AIGENERAL_CHINA_INFANTRY,
	AIGENERAL_CHINA_NUKE,
	AIGENERAL_GLA,
	AIGENERAL_GLA_TOXIN,
	AIGENERAL_GLA_DEMOLITION,
	AIGENERAL_GLA_STEALTH,
	AIGENERAL_TURKIYE,
	AIGENERAL_COUNT
};

/** The general's identity: the hooks the shared AI (AIPlayer, AISkirmishPlayer) calls at its decision points.
	*
	* Every hook's default leaves the shared decision as it is, to the bit, so a general with no moves plays the
	* shared AI exactly.  A side group overrides them in its own file (AIIdentityUSA.cpp, AIIdentityChina.cpp,
	* AIIdentityGLA.cpp, AIIdentityTurkiye.cpp), whose class AIPlayer and AISkirmishPlayer name as a friend, so it
	* may read and order through their members without touching their files.  The hooks run only while
	* AIPlayer::identityOn() holds: a seat with -aiknobsoff ... identity (skirmish only, never a network game) calls
	* none of them, onStart included, so it draws nothing from the logic stream either.
	*
	* GameLogic: what a hook decides must be the same on every machine.  The logic random stream only
	* (GameLogicRandomValue), no client state, fixed iteration order, and anything kept across frames saved in
	* xfer under the class's own version. */
class AIIdentity
{
public:
	AIIdentity( AIPlayer *ai, AIGeneral general ) : m_ai( ai ), m_general( general ), m_moves( 0 ) {}
	virtual ~AIIdentity() {}

	AIGeneral getGeneral( void ) const { return m_general; }
	/// this match's moves (the side file's own bits), drawn in onStart
	Bool hasMove( UnsignedInt move ) const { return ( m_moves & move ) != 0; }

	// ---- the hooks, in the order a match meets them ----
	/// the seat's first turn: draw the moves (drawMove)
	virtual void onStart( void ) {}
	/// every AIPlayer::update, after the powers that fire when ready: the general's standing orders
	virtual void onTick( void ) {}
	/// build plan: the purchase score of a base building the plan weighs (AISkirmishPlayer's build list pick)
	virtual Int buildingScore( const ThingTemplate *building, Int shared ) const { return shared; }
	/// unit pick: the score of one of the scripts' teams when the AI picks the next to build (selectTeamToBuild)
	virtual Real teamScore( const TeamPrototype *team, Real shared ) const { return shared; }
	/// defence placement: TRUE when the identity placed this base defence itself, and the shared placement is skipped.
	/// Asked first by AIPlayer::placeDefense (flank FALSE), and again by buildAIBaseDefenseStructure when the shared
	/// placement failed, so one defence can be asked twice in a turn: keep it free of draws and counters.  Never call
	/// AIPlayer::placeDefense or buildAIBaseDefenseStructure from here: both ask this hook first and would recurse
	/// for ever; place through placeDefenseAround or placeNear
	virtual Bool placeDefence( const ThingTemplate *defence, Bool flank ) { return FALSE; }
	/// wave size: the power a parked wave waits for (AIPlayer::wavePower)
	virtual Real wavePower( Real shared ) const { return shared; }
	/// wave timing: the longest a parked wave waits (AIPlayer::waveMaxHoldFrames)
	virtual UnsignedInt waveMaxHoldFrames( UnsignedInt shared ) const { return shared; }
	/// a wave of this power went (AIPlayer::sendWave), before it walks
	virtual void onWaveSent( AIGroup *wave, Real power ) {}
	/// power and superweapon target: TRUE when target was set here, and the shared aim is skipped (computeSuperweaponTarget)
	virtual Bool powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target ) { return FALSE; }
	/// unit retreat: the health share under which a dear unit is pulled out to be mended, 0 for never (pullOutRanked)
	virtual Real pullHealth( const Object *unit, Real shared ) const { return shared; }
	/// unit retreat: the health share under which a ranked unit is pulled out on its own health (pullOutRanked; shared
	/// is the rank rule's, 35% a veteran, 55% a heroic).  Only a higher share changes anything: the rule still pulls
	/// at its own
	virtual Real rankPullHealth( const Object *unit, Real shared ) const { return shared; }
	/// wave building: TRUE for a script team the general leaves to its script, never parked for a wave
	/// (AIPlayer::holdTeamForWave, after the base and perimeter defence teams).  Keep it pure
	virtual Bool keepsScript( const Team *team ) const { return FALSE; }
	/// science pick: the science the skill set buyer weighs at this place of its list (AIPlayer::update's SKILLS walk).
	/// Return another to put it first: one the seat cannot afford yet stops the walk there and saves the points, as
	/// the shared buyer does for its own list.  Asked once per place per walk, so keep it pure
	virtual ScienceType sciencePick( ScienceType shared ) const { return shared; }
	/// attack road: the score of a lane a wave weighs, lower taken first (chooseApproachLabel: the known firepower
	/// along it, never negative for a road that exists; sneakyLane: 0 for the side road its turn takes, 1 for the
	/// other).  Keep it pure and never negative
	virtual Real laneScore( const AsciiString &lane, const Coord3D *from, Real shared ) const { return shared; }

	/// the identity's own state; a side class calls this first, then saves its own under its own version
	virtual void xfer( Xfer *xfer );

	/// the identity of the general this side string plays, never NULL for a seat that runs its own AI
	static AIIdentity *create( AIPlayer *ai, const AsciiString &side );
	static AIGeneral generalOfSide( const AsciiString &side );

protected:
	/// one move of this match, on with chance percent: 100 or more draws nothing, under it one logic-stream draw.
	/// Draw only in onStart or onTick.  Never call a hook, or draw, inside look() or the scan jobs: they run on a
	/// parallel_for (AIPlayer.cpp, the lookAllSeats jobs), and a draw there splits the logic stream.  The score
	/// hooks (buildingScore, teamScore, wavePower, waveMaxHoldFrames, pullHealth) stay const and pure.
	void drawMove( UnsignedInt move, Int chance );

	AIPlayer		*m_ai;
	AIGeneral		m_general;
	UnsignedInt	m_moves;
};

// each side group's factory, in its own file
AIIdentity *createUSAIdentity( AIPlayer *ai, AIGeneral general );
AIIdentity *createChinaIdentity( AIPlayer *ai, AIGeneral general );
AIIdentity *createGLAIdentity( AIPlayer *ai, AIGeneral general );
AIIdentity *createTurkiyeIdentity( AIPlayer *ai, AIGeneral general );

#endif
