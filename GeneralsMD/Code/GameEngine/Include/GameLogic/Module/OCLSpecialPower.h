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

// FILE: OCLSpecialPower.h ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2002
// Desc:   Special powers that are driven by object creation lists
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __OCLSPECIALPOWER_H_
#define __OCLSPECIALPOWER_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/SpecialPowerModule.h"
#include "Common/Science.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class ObjectCreationList;
class Player;
class ThingTemplate;

enum OCLCreateLocType
{
	CREATE_AT_EDGE_NEAR_SOURCE,
	CREATE_AT_EDGE_NEAR_TARGET,
	CREATE_AT_LOCATION,
	USE_OWNER_OBJECT,
	CREATE_ABOVE_LOCATION,
	CREATE_AT_EDGE_FARTHEST_FROM_TARGET,
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class OCLSpecialPowerModuleData : public SpecialPowerModuleData
{

public:

	/// UpgradeOCL = <science> [<science> ...] <ocl>: every science owned (parseSciencesThenValue)
	struct Upgrades
	{
		ScienceVec									m_sciences;
		const ObjectCreationList*		m_ocl;

		Upgrades() : m_ocl(NULL)
		{
		}
	};

	std::vector<Upgrades>			m_upgradeOCL;
	const ObjectCreationList*	m_defaultOCL;
	OCLCreateLocType					m_createLoc;
	Bool											m_isOCLAdjustPositionToPassable; ///< Adjust target to nearest Passable cell

	//We need to know what the final product is going to be for script placement calculations
	//for construction sites like the sneak attack.
	AsciiString				m_referenceThingName;

	// An airlift (fork): with AirliftPlaneSlots above 0 the power is fired with a load the player picked
	// (MSG_DO_AIRLIFT_ORDER, OCLSpecialPower::doAirliftOrder), not on its own.  The OCL is one plane
	// with no payload; a flight of them carries the load, paid for at the units' price and a fee a plane
	std::vector<AsciiString>	m_airliftUnits;					///< AirliftUnit: what the order may hold, in the order the panel lists it
	Int												m_airliftPlaneSlots;		///< what one plane holds
	Int												m_airliftInfantrySlots;	///< what a soldier takes of it
	Int												m_airliftVehicleSlots;	///< what anything else takes
	Int												m_airliftMaxPlanes;			///< the most planes one order may need
	Int												m_airliftFeePerPlane;
	Real											m_airliftAllyRadius;		///< an ally's unit or building this near the spot takes the load
	Real											m_airliftSpacing;				///< how far apart the planes fly
	AsciiString								m_airliftInfantryChute;
	AsciiString								m_airliftVehicleChute;

	OCLSpecialPowerModuleData( void );
	static void buildFieldParse(MultiIniFieldParse& p);

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class OCLSpecialPower : public SpecialPowerModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( OCLSpecialPower, "OCLSpecialPower" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( OCLSpecialPower, OCLSpecialPowerModuleData )

public:

	OCLSpecialPower( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool object

	virtual void doSpecialPower( UnsignedInt commandOptions );
	virtual void doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions );
	virtual void doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions );

	//If the special power launches a construction site, we need to know the final product for placement purposes.
	virtual const ThingTemplate* getReferenceThingTemplate() const;

	// ---- airlift (fork) --------------------------------------------------------------------------
	// The panel (GameClient) and the order's handler (GameLogic) ask the same questions here, so what
	// the panel greys and what the logic refuses cannot drift apart.

	struct AirliftLine
	{
		const ThingTemplate *thing;
		Int count;
	};
	typedef std::vector< AirliftLine > AirliftOrder;

	enum AirliftCheck
	{
		AIRLIFT_OK,
		AIRLIFT_EMPTY,							///< nothing ordered
		AIRLIFT_NOT_BUILDABLE,			///< not on the power's list, or the player could not build it now: factory, prerequisite or cap
		AIRLIFT_TOO_MANY_PLANES,
		AIRLIFT_NO_MONEY
	};

	/// the airlift module on `obj` for this power, or NULL
	static OCLSpecialPower *findAirlift( Object *obj, const SpecialPowerTemplate *power );

	Bool isAirlift() const { return getOCLSpecialPowerModuleData()->m_airliftPlaneSlots > 0; }
	const OCLSpecialPowerModuleData *getAirliftData() const { return getOCLSpecialPowerModuleData(); }
	Int airliftSlotsOf( const ThingTemplate *thing ) const;
	/// a working factory of the player's offers it and `count` more stay under its cap
	Bool canAirlift( Player *player, const ThingTemplate *thing, Int count ) const;
	/// planes, cost and what refuses the order; loads (may be NULL) gets each plane's units
	AirliftCheck checkAirlift( Player *player, const AirliftOrder &order, Int *planes, Int *cost,
														 std::vector< std::vector< const ThingTemplate * > > *loads ) const;
	/// the ally who gets a load dropped at `loc`: the owner of the nearest unit or building of a player
	/// allied both ways within AirliftAllyRadius, or the caster with none
	Player *findAirliftRecipient( const Coord3D *loc ) const;
	/// pay, start the countdown and send the planes; FALSE and nothing done when refused
	Bool doAirliftOrder( const Coord3D *loc, const AirliftOrder &order );

	/// the list this power runs now, upgrades counted, and where its planes come from: the client's
	/// ground marker reads both while the player aims (read only)
	const ObjectCreationList* findOCL() const;
	OCLCreateLocType getCreateLoc() const { return getOCLSpecialPowerModuleData()->m_createLoc; }
};

#endif  // end __OCLSPECIALPOWER_H_
