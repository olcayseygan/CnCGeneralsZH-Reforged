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

// FILE: PieceDamageBehavior.h ///////////////////////////////////////////////////////////////////
//
// A structure that comes apart where it is hit.  Each Piece is a sub-object of the model with a bone
// at its centre and health of its own.  A weapon hit (DamageInfoInput::m_hasImpact) shares the damage
// the body took among the standing pieces: the one nearest the impact takes all of it, and the rest
// less the further they stand beyond that one, nothing at Reach past it, Reach being the weapon's
// damage radius times BlastReachScale or MinimumReach, whichever is larger.  So a bigger blast
// reaches and breaks more; BlastReachScale below 1 keeps a blast wider than the whole structure
// (the Nuke Cannon's 60 on a 22 wide tower) from hitting every piece nearly as hard as the nearest.  A
// piece whose health is gone breaks, and so does one that rests on a broken piece: its sub-object is
// hidden, its broken stand-in (the same name with _B) shown and BreakFX played at its bone.  The
// object's own health and death are untouched.  The draw module has to make each bone public
// (ExtraPublicBone), or the logic cannot find it.
//
// A piece named in DisablesWeapons takes the object's guns with it: once one breaks, the object gets
// OBJECT_STATUS_NO_ATTACK for good, and stands on as a target with the health it has left.  The
// pieces' health is in Object::crc, since it now decides play.
//
//   Behavior = PieceDamageBehavior ModuleTag_Pieces
//     MinimumReach    = 12
//     BlastReachScale = 0.5
//     BreakFX         = FX_StructureDamagedNoShake
//     Piece        = TWR_P01 PIECE01 150              ; sub-object, bone, health
//     Piece        = TWR_P05 PIECE05 150 TWR_P01      ; ...and the earlier pieces it rests on
//     DisablesWeapons = TWR_P14 TWR_P15               ; after the Piece lines that name them
//   End
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __PIECE_DAMAGE_BEHAVIOR_H_
#define __PIECE_DAMAGE_BEHAVIOR_H_

#include "GameLogic/Module/DamageModule.h"

class FXList;
class Xfer;

//-------------------------------------------------------------------------------------------------
/** One breakable part. */
struct PieceDamagePiece
{
	AsciiString		m_subObject;		///< drawn while it stands; m_subObject + "_B" is drawn once it breaks
	AsciiString		m_bone;					///< at the piece's centre
	Real					m_health;
	UnsignedInt		m_restsOn;			///< a bit for each earlier piece that holds this one up
};

//-------------------------------------------------------------------------------------------------
class PieceDamageBehaviorModuleData : public DamageModuleData
{
public:
	std::vector<PieceDamagePiece>	m_pieces;
	Real							m_minimumReach;
	Real							m_blastReachScale;
	UnsignedInt				m_disablesWeapons;		///< a bit for each piece whose break stops the object firing
	const FXList*			m_breakFX;

	PieceDamageBehaviorModuleData();

	static void buildFieldParse( MultiIniFieldParse& p );
};

//-------------------------------------------------------------------------------------------------
/** The pieces' health, apart from the Object so a test can drive it. */
class PieceDamageState
{
public:
	enum { MAX_PIECES = 32 };

	void reset( const std::vector<PieceDamagePiece>& pieces );

	/** One hit: amount shared out from impact over reach among the standing pieces, whose centres are
		* at[i].  Returns a bit for each piece it broke, by its own health or by what it rests on. */
	UnsignedInt hit( const std::vector<PieceDamagePiece>& pieces, const Coord3D *at,
									 const Coord3D& impact, Real reach, Real amount );

	UnsignedInt getBrokenMask() const;
	Int getCount() const { return (Int)m_health.size(); }
	Real getHealth( Int i ) const { return m_health[ i ]; }

	void xfer( Xfer *xfer );

	/** The share of a hit a piece dist from the impact takes, the nearest standing piece being nearest away. */
	static Real share( Real dist, Real nearest, Real reach );

private:
	std::vector<Real> m_health;
};

//-------------------------------------------------------------------------------------------------
class PieceDamageBehavior : public DamageModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( PieceDamageBehavior, "PieceDamageBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( PieceDamageBehavior, PieceDamageBehaviorModuleData )

public:

	PieceDamageBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void onDamage( DamageInfo *damageInfo );
	// ponytail: a repair heals the body, not the pieces; give onHealing a share when a pieced building can be repaired
	virtual void onHealing( DamageInfo *damageInfo ) { }
	virtual void onBodyDamageStateChange( const DamageInfo* damageInfo, BodyDamageType oldState, BodyDamageType newState ) { }

	const PieceDamageState& getState() const { return m_state; }

private:

	void showBroken( UnsignedInt pieces ) const;

	PieceDamageState m_state;
};

#endif
