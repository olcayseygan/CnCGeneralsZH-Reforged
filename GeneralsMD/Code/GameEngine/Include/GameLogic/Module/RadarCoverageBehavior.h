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

// FILE: RadarCoverageBehavior.h /////////////////////////////////////////////////////////////////
//
// Turkiye's radar: a circle of Radius in which its own and its allies' radar-guided rockets keep
// their lock (WeaponTemplate RadarBlindHitChance, Weapon.cpp), and in which moving enemies hidden in
// the fog are tracked: painted on the ground for the player, open to beyond-vision artillery.
// A source may wait for an upgrade, grow with others, go dark while it drives (a mobile radar),
// cover only the enemy it aims at (a laser designator), or see standing units too (a recon drone).
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __RADAR_COVERAGE_BEHAVIOR_H_
#define __RADAR_COVERAGE_BEHAVIOR_H_

#include "GameLogic/Module/BehaviorModule.h"

class Player;
class UpgradeTemplate;

//-------------------------------------------------------------------------------------------------
class RadarCoverageBehaviorModuleData : public BehaviorModuleData
{
public:

	/// RadiusUpgrade = <upgrade> <scale>, repeatable: Radius times every scale whose upgrade the owner has
	struct RadiusUpgrade
	{
		AsciiString m_upgrade;
		Real m_scale;
	};

	Real m_radius;
	std::vector<RadiusUpgrade> m_radiusUpgrades;
	AsciiString m_requiredUpgrade;	///< covers nothing until the owner has it
	AsciiString m_classifyUpgrade;	///< its contacts carry a class glyph once the owner has it
	Bool m_onlyWhenStationary;			///< covers nothing while the source moves
	Bool m_paintsTargetOnly;				///< covers only its own enemy attack or goal target within Radius, not the circle
	Bool m_seesStationary;					///< also reports stationary enemies, always classified

	RadarCoverageBehaviorModuleData();

	static void buildFieldParse( MultiIniFieldParse& p );
};

//-------------------------------------------------------------------------------------------------
class RadarCoverageBehavior : public BehaviorModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( RadarCoverageBehavior, "RadarCoverageBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( RadarCoverageBehavior, RadarCoverageBehaviorModuleData );

public:

	RadarCoverageBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	/// a working radar of this player or an ally covers this point
	static Bool isCovered( const Player *player, const Coord3D *pos );

	/** The radar holds this unit for this player: stealth does not hide it, and it either moves in
		* the player's or an ally's coverage, stands in a SeesStationary source, or is a source's painted
		* target.  Says nothing of the fog or of whose side it is on; the caller asks those. */
	static Bool isTracked( const Player *player, const Object *obj );

	/** GameLogic::update, after the partition manager: every RADAR_SWEEP_FRAMES, hands the client
		* each player's tracked enemies standing in fog and the live SeesStationary areas
		* (GameClient/RadarContactMarks.h).  Keeps nothing. */
	static void sweep( void );

private:

	/// what a player's radars make of a point, or of the object standing there
	struct Survey
	{
		Bool covered;			///< an area reaches it, or it is a painted target
		Bool painted;			///< a PaintsTargetOnly source has it as its target
		Bool recon;				///< a SeesStationary source reaches it
		Bool classified;	///< a source that reaches it names its class
	};
	/// when survey may stop: at the first coverage, the first paint, the first paint or recon, or never
	enum SurveyNeed { SURVEY_COVER, SURVEY_PAINT, SURVEY_HOLD, SURVEY_ALL };
	static void survey( const Player *player, const Coord3D *pos, const Object *target, SurveyNeed need, Survey &out );

	Real reach( void ) const;										///< effective radius, 0 while it covers nothing
	const Object *paintedTarget( void ) const;	///< its enemy attack or goal target within reach, PaintsTargetOnly only
	Bool classifies( void ) const;

	/* Every radar in the game, in no order that matters: isCovered only asks whether any of them
		 reaches.  The constructor and destructor keep it, so a loaded game rebuilds it as its objects
		 are made and there is nothing to save. */
	static RadarCoverageBehavior *s_first;
	RadarCoverageBehavior *m_next;

	/* The module data's upgrade names, found once: the upgrades are parsed after the objects, so the
		 data can only hold names.  Derived, not saved. */
	std::vector<const UpgradeTemplate *> m_radiusUpgrades;
	const UpgradeTemplate *m_requiredUpgrade;
	const UpgradeTemplate *m_classifyUpgrade;
};

#endif	// __RADAR_COVERAGE_BEHAVIOR_H_
