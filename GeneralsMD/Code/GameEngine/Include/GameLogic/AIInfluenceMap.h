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

// AIInfluenceMap.h
// What one computer player knows can shoot at each patch of ground, and what it has there itself.

#pragma once

#ifndef _AI_INFLUENCE_MAP_H_
#define _AI_INFLUENCE_MAP_H_

#include <vector>

class Xfer;

/** A grid over the map, one per computer player, rebuilt from what that player knows about.
	*
	* The engine's own per-cell threat layer is built from ThingTemplate::ThreatValue, which the shipped
	* data almost never sets, so every one of its cells reads zero.  This one stamps each armed thing's
	* combat power onto every cell its guns reach, the reach stretched by the high ground the way
	* Weapon_elevatedRange stretches it, so "how much can hit me here" is one lookup.  The friendly
	* layer is the same stamp for this player's own units and its allies', drawn for Hard's tactics alone.
	*
	* Everything here is plain arithmetic on logic state in the order the caller hands it over, so two
	* machines that stamp the same objects in the same order hold the same map. */
class AIInfluenceMap
{
public:
	AIInfluenceMap();

	/** Size the grid to a map and forget everything on it.  heights is filled by the caller through
		* setCellHeight, once per map, since the ground does not move. */
	void reset( Real originX, Real originY, Real width, Real height, Real cellSize );
	Bool isSized() const { return m_cols > 0; }
	void clear();

	Int getCols() const { return m_cols; }
	Int getRows() const { return m_rows; }
	void cellCenter( Int col, Int row, Real *x, Real *y ) const;
	void setCellHeight( Int col, Int row, Real z );

	/** Add power to every cell a weapon of this range, fired from (x, y, z), reaches. */
	void stampEnemy( Real x, Real y, Real z, Real range, Real power );
	void stampFriend( Real x, Real y, Real z, Real range, Real power );

	Real enemyAt( Real x, Real y ) const;
	Real friendAt( Real x, Real y ) const;

	/** The whole map, into a save and back: a loaded game reads the same map the uninterrupted one
		* holds until its next rebuild, which can be most of a second away. */
	void xfer( Xfer *xfer );

	void markBuilt( UnsignedInt frame ) { m_builtFrame = frame; m_built = TRUE; }
	void markUnbuilt() { m_built = FALSE; }
	Bool isBuilt() const { return m_built; }
	UnsignedInt getBuiltFrame() const { return m_builtFrame; }

private:
	void stamp( std::vector<Real> &layer, Real x, Real y, Real z, Real range, Real power );
	Int cellIndexAt( Real x, Real y ) const;

	Real m_originX, m_originY, m_cellSize;
	Int m_cols, m_rows;
	std::vector<Real> m_enemy;
	std::vector<Real> m_friend;
	std::vector<Real> m_height;
	Bool m_built;
	UnsignedInt m_builtFrame;
};

/** Where a unit that outranges what is closing on it should step to: on the line from the enemy
	* through the unit, at the far edge of its own reach, which is as far from the enemy's gun as it can
	* stand and still shoot.  FALSE when it has no such room, which is when its range is not longer than
	* the enemy's by twice the margin. */
Bool AIKite_standoffDistance( Real myRange, Real enemyRange, Real margin, Real *distance );

#endif
