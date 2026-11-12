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

// FILE: SpecialPower.h ///////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2002
// Desc:   Special power templates and the system that holds them
// Edited: Kris Morness -- July 2002 (added BitFlag system)
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SPECIALPOWER_H_
#define __SPECIALPOWER_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "Common/GameMemory.h"
#include "Common/SubsystemInterface.h"
#include "Lib/BaseType.h"
#include "Common/BitFlags.h"
#include "Common/Overridable.h"
#include "Common/Override.h"
#include "Common/Science.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class ObjectCreationList;
class Object;
class INI;
class Player;
struct FieldParse;
enum AcademyClassificationType : Int;

//-------------------------------------------------------------------------------------------------
/** A science-keyed power field (UpgradeOCL, UpgradeTargetCount, UpgradeRadiusCursorRadius) reads
	"<science> [<science> ...] <value>": every token but the last is a science and the player must own
	all of them.  The entries are tried in file order and the first owned one wins, so a talent line
	keyed on the talent and tier 3 sits above the one keyed on the talent and tier 2.  Fills sciences
	and returns the value token; a science the loaded data lacks stays as SCIENCE_INVALID, which no
	player owns, so that entry never matches, as a one-science line did. */
AsciiString parseSciencesThenValue( INI *ini, ScienceVec *sciences );

/// owns every science in the list; an empty list is owned by nobody
template <class Owner>
inline Bool ownsAllSciences( const Owner *owner, const ScienceVec &sciences )
{
	if( owner == NULL || sciences.empty() )
		return FALSE;
	for( ScienceVec::const_iterator it = sciences.begin(); it != sciences.end(); ++it )
		if( !owner->hasScience( *it ) )
			return FALSE;
	return TRUE;
}

/// the first entry (with an m_sciences list) whose sciences the owner all has, or NULL
template <class Owner, class Entry>
inline const Entry *findFirstOwnedEntry( const Owner *owner, const std::vector<Entry> &entries )
{
	for( typename std::vector<Entry>::const_iterator it = entries.begin(); it != entries.end(); ++it )
		if( ownsAllSciences( owner, it->m_sciences ) )
			return &(*it);
	return NULL;
}

// For SpecialPowerType and SpecialPowerMaskType::s_bitNameList. Part of detangling.
#include "Common/SpecialPowerType.h"

// For SpecialPowerMaskType. Part of detangling.
#include "Common/SpecialPowerMaskType.h"

#define MAKE_SPECIALPOWER_MASK(k) SpecialPowerMaskType(SpecialPowerMaskType::kInit, (k))

inline Bool TEST_SPECIALPOWERMASK(const SpecialPowerMaskType& m, SpecialPowerType t) 
{ 
	return m.test(t); 
}
inline Bool TEST_SPECIALPOWERMASK_ANY(const SpecialPowerMaskType& m, const SpecialPowerMaskType& mask) 
{ 
	return m.anyIntersectionWith(mask);
}
inline Bool TEST_SPECIALPOWERMASK_MULTI(const SpecialPowerMaskType& m, const SpecialPowerMaskType& mustBeSet, const SpecialPowerMaskType& mustBeClear)
{
	return m.testSetAndClear(mustBeSet, mustBeClear);
}
inline Bool SPECIALPOWERMASK_ANY_SET(const SpecialPowerMaskType& m) 
{ 
	return m.any(); 
}
inline void CLEAR_SPECIALPOWERMASK(SpecialPowerMaskType& m) 
{ 
	m.clear(); 
}
inline void SET_SPECIALPOWERMASK( SpecialPowerMaskType& m, SpecialPowerType t, Int val = 1 )
{
	m.set( t, val );
}
inline void SET_ALL_SPECIALPOWERMASK_BITS(SpecialPowerMaskType& m)
{
	m.clear();
	m.flip();
}
inline void FLIP_SPECIALPOWERMASK(SpecialPowerMaskType& m)
{
	m.flip();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SpecialPowerTemplate : public Overridable
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SpecialPowerTemplate, "SpecialPowerTemplate" )

public:

  SpecialPowerTemplate();
	// virtual destructor prototype provided by MemoryPoolObject

	static const FieldParse* getFieldParse( void ) { return m_specialPowerFieldParse; }

	void friend_setNameAndID(const AsciiString& name, UnsignedInt id)
	{
		m_name = name;
		m_id = id;
	}

	AsciiString getName( void ) const { return getFO()->m_name; }
	UnsignedInt getID( void ) const { return getFO()->m_id; }
	SpecialPowerType getSpecialPowerType( void ) const { return getFO()->m_type; }
	UnsignedInt getReloadTime( void ) const { return getFO()->m_reloadTime; }
	ScienceType getRequiredScience( void ) const { return getFO()->m_requiredScience; }
	const AudioEventRTS *getInitiateSound( void ) const { return &getFO()->m_initiateSound; }
	const AudioEventRTS *getInitiateAtTargetSound( void ) const { return &getFO()->m_initiateAtLocationSound; }
	Bool hasPublicTimer( void ) const { return getFO()->m_publicTimer; }
	Bool isSharedNSync( void ) const { return getFO()->m_sharedNSync; }
	UnsignedInt getDetectionTime( void ) const { return getFO()->m_detectionTime; }
	UnsignedInt getViewObjectDuration( void ) const { return getFO()->m_viewObjectDuration; }
	Real getViewObjectRange( void ) const { return getFO()->m_viewObjectRange; }
	/// the targeting circle, widened by the first UpgradeRadiusCursorRadius entry the player owns
	Real getRadiusCursorRadius( const Player *player ) const;
	Bool isShortcutPower() const { return getFO()->m_shortcutPower; }
	Bool isFiredByAIWhenReady() const { return getFO()->m_aiFiresWhenReady; }
	AcademyClassificationType getAcademyClassificationType() const { return m_academyClassificationType; }

private: 

	const SpecialPowerTemplate* getFO() const { return (const SpecialPowerTemplate*)friend_getFinalOverride(); }

	AsciiString				m_name;								///< name
	UnsignedInt				m_id;									///< unique identifier
	SpecialPowerType	m_type;								///< enum allowing for fast type checking for ability processing.
	DurationFrames				m_reloadTime;					///< (frames) after using special power, how long it takes to use again
	ScienceType				m_requiredScience;		///< science required (if any) to actually execute this power
	AudioEventRTS			m_initiateSound;			///< sound to play when initiated
	AudioEventRTS			m_initiateAtLocationSound;		///< sound to play at target location (if any)
	AcademyClassificationType m_academyClassificationType; ///< A value used by the academy to evaluate advice based on what players do.
	DurationFrames				m_detectionTime;			///< (frames) after using infiltration power (defection, etc.), 
																					///< how long it takes for ex comrades to realize it on their own
	DurationFrames				m_viewObjectDuration;	///< Lifetime of a looking object we slap down so you can watch the effect
	Real							m_viewObjectRange;		///< And how far that object can see.
	Real							m_radiusCursorRadius;	///< size of radius cursor, if any
public:
	struct UpgradeRadius
	{
		ScienceVec m_sciences;
		Real m_radius;
	};
private:
	std::vector<UpgradeRadius> m_upgradeRadiusCursorRadius;	///< UpgradeRadiusCursorRadius = <science> [<science> ...] <radius>
	Bool							m_publicTimer;				///< display a countdown timer for this special power for all to see
	Bool							m_sharedNSync;				///< If true, this is a special that is shared between all of a player's command centers
	Bool							m_shortcutPower;		///< Is this shortcut power capable of being fired by the side panel?
	Bool							m_aiFiresWhenReady;	///< the skirmish AI fires it on its own (AIPlayer::doReadySpecialPowers); no script names it

	static const FieldParse m_specialPowerFieldParse[];		///< the parse table

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SpecialPowerStore : public SubsystemInterface
{

public:

	SpecialPowerStore( void );
	~SpecialPowerStore( void );

	virtual void init( void ) { };
	virtual void update( void ) { };
	virtual void reset( void );

	const SpecialPowerTemplate *findSpecialPowerTemplate( AsciiString name ) { return findSpecialPowerTemplatePrivate(name); }
	const SpecialPowerTemplate *findSpecialPowerTemplateByID( UnsignedInt id );
	const SpecialPowerTemplate *getSpecialPowerTemplateByIndex( UnsignedInt index ); // for WorldBuilder

	/// does the object (and therefore the player) meet all the requirements to use this power
	Bool canUseSpecialPower( Object *obj, const SpecialPowerTemplate *specialPowerTemplate );

	Int getNumSpecialPowers( void ); // for WorldBuilder

	static void parseSpecialPowerDefinition( INI *ini );

private:

protected:
	SpecialPowerTemplate *findSpecialPowerTemplatePrivate( AsciiString name );

	typedef std::vector<SpecialPowerTemplate *> SpecialPowerTemplatePtrVector;
	SpecialPowerTemplatePtrVector m_specialPowerTemplates;			///< the special power templates

	UnsignedInt m_nextSpecialPowerID;

};

// EXTERNAL ///////////////////////////////////////////////////////////////////////////////////////
extern SpecialPowerStore *TheSpecialPowerStore;

#endif  // end __SPECIALPOWER_H_
