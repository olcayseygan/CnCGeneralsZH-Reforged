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

// FILE: ExperienceTracker.h //////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, February 2002
// Desc:   Keeps track of experience points so Veterance levels can be gained
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef EXPERIENCE_TRACKER_H
#define EXPERIENCE_TRACKER_H

#include "Common/GameCommon.h"
#include "Common/GameType.h"
#include "Common/GameMemory.h"
#include "Common/Snapshot.h"

class Object;

/* A kill's experience is split by damage dealt (Object::scoreTheKill).  The victim remembers its
	 last few attackers; the killing blow takes a fixed cut and the rest goes by the share of health
	 each took off inside the window. */
enum
{
	KILL_XP_DAMAGER_SLOTS = 4,														///< attackers a victim remembers
	KILL_XP_KILLING_BLOW_PERCENT = 25,										///< the killing blow's fixed cut
	KILL_XP_WINDOW_FRAMES = 10 * LOGICFRAMES_PER_SECOND,	///< damage older than this earns nothing
};

struct KillXPDamager
{
	ObjectID		m_id;					///< INVALID_ID for an empty slot
	Int					m_damage;			///< health taken off, summed while the attacker keeps hitting
	UnsignedInt	m_frame;			///< frame of the last hit
};

/** Add a hit to the slots.  An attacker already there adds to its own slot; a new one takes the
		weakest slot (least live damage, then the oldest hit, then the lowest index), and an empty or
		expired slot counts as no damage. */
void KillXPRecordDamage( KillXPDamager *slots, ObjectID source, Int damage, UnsignedInt frame );

/** Split total experience: shares[i] is slot i's cut of the 75% by live damage, and the return is
		what is left for the killing blow, its 25% plus the rounding.  No live damage returns total. */
Int KillXPSplit( Int total, const KillXPDamager *slots, UnsignedInt frame, Int *shares );

/* Restoring an ally's health earns experience (Object::scoreTheHeal): HEAL_XP_PERCENT of what the
	 patient is worth as a kill, scaled by the share of its maximum health put back.  Heals come a
	 sliver a frame, so the healer carries the fraction of a point in HEAL_XP_SCALE units. */
enum
{
	HEAL_XP_PERCENT = 50,				///< a full heal is worth this much of a kill
	HEAL_XP_SCALE = 1000000,		///< carry units per experience point
};

/** Add one heal to *carry and return the whole points it now holds, leaving the remainder in it.
		restored and maxHealth are in the same units, restored no more than maxHealth. */
Int HealXPAccrue( Int *carry, Int value, Int restored, Int maxHealth );

class ExperienceTracker : public MemoryPoolObject, public Snapshot
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(ExperienceTracker, "ExperienceTrackerPool" )	
public:
	ExperienceTracker(Object *parent);

	VeterancyLevel getVeterancyLevel() const { return m_currentLevel; }			///< What level am I?
	Int getExperienceValue( const Object* killer ) const;										///< How much do give for being killed
	Int getCurrentExperience( void ) const { return m_currentExperience; };	///< How much experience do I have at the moment?
	Bool isTrainable() const;																						///< Can I gain experience?
	Bool isAcceptingExperiencePoints() const;														///< Either I am trainable, or I have a Sink set up

	void setVeterancyLevel( VeterancyLevel newLevel, Bool provideFeedback = TRUE );						///< Set Level to this
	void setMinVeterancyLevel( VeterancyLevel newLevel );					///< Set Level to AT LEAST this... if we are already >= this level, do nothing.
	void addExperiencePoints( Int experienceGain, Bool canScaleForBonus = TRUE );	///< Gain this many exp.
	Bool gainExpForLevel(Int levelsToGain, Bool canScaleForBonus = TRUE );			  ///< Gain enough exp to gain a level. return false if can't gain a level.
	Bool canGainExpForLevel(Int levelsToGain) const;															///< return same value as gainExpForLevel, but don't change anything
	void setExperienceAndLevel(Int experienceIn, Bool provideFeedback = TRUE );
	void setExperienceSink( ObjectID sink );											///< My experience actually goes to this person (loose couple)

	Real getExperienceScalar() const { return m_experienceScalar; }
	void setExperienceScalar( Real scalar ) { m_experienceScalar = scalar; }

	void recordDamage( ObjectID source, Int damage );											///< an enemy took this much health off me
	const KillXPDamager *getDamagers() const { return m_damagers; }				///< KILL_XP_DAMAGER_SLOTS of them
	ObjectID getExperienceSink() const { return m_experienceSink; }
	Int accrueHealExperience( Int value, Int restored, Int maxHealth ) { return HealXPAccrue( &m_healXPCarry, value, restored, maxHealth ); }

	// --------------- inherited from Snapshot interface --------------
	void crc( Xfer *xfer );
	void xfer( Xfer *xfer );
	void loadPostProcess( void );

private:
	Object*						m_parent;														///< Object I am owned by
	VeterancyLevel		m_currentLevel;											///< Level of experience
	Int								m_currentExperience;								///< Number of experience points
	ObjectID					m_experienceSink;										///< ID of object I have pledged my experience point gains to
	Real							m_experienceScalar;									///< Scales any experience gained by this multiplier.
	KillXPDamager			m_damagers[KILL_XP_DAMAGER_SLOTS];	///< recent attackers, for splitting the kill
	Int								m_healXPCarry;											///< healing experience short of a whole point, in HEAL_XP_SCALE units
};

#endif