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

/** What a gun is a threat to: the enemy layer holds one figure for each, since a tank, a rifleman and a
	* helicopter fear different guns. */
enum AIThreatClass
{
	AITHREAT_VEHICLE,
	AITHREAT_INFANTRY,
	AITHREAT_AIR,
	AITHREAT_COUNT
};

/** How a reader weighs the three: what it moves, by share.  The weights sum to one. */
struct AIThreatMix
{
	Real w[ AITHREAT_COUNT ];
};

static const AIThreatMix AITHREAT_GROUND_MIX = { { 0.5f, 0.5f, 0.0f } };	///< a reader that does not say what it moves
static const AIThreatMix AITHREAT_COST_MIX = { { 1.0f, 0.0f, 0.0f } };		///< the vehicle slot alone, which the cost map (-aiknobsoff ... costmap) fills
static const AIThreatMix AITHREAT_INFANTRY_MIX = { { 0.0f, 1.0f, 0.0f } };
static const AIThreatMix AITHREAT_AIR_MIX = { { 0.0f, 0.0f, 1.0f } };

/** A grid over the map, one per computer player, rebuilt from what that player knows about.
	*
	* The engine's own per-cell threat layer is built from ThingTemplate::ThreatValue, which the shipped
	* data almost never sets, so every one of its cells reads zero.  This one stamps each armed thing's
	* threat to vehicles, to infantry and to aircraft onto every cell its guns for each reach, the reach
	* stretched by the high ground the way Weapon_elevatedRange stretches it, so "how much can hit me here"
	* is one lookup weighted by what is asking.  The friendly layer holds the same three figures for this
	* player's and its allies' guns, drawn for Hard's tactics alone.
	*
	* With the map layers (-aiknobsoff ... maplayers takes them off) four more go beside them: the share of
	* the enemy layer that stands still (defences, and the guns firing out of a building), where the enemy's
	* worth is (economy, production, tech), the ground that hurts whoever stands on it (radiation, toxin,
	* fire fields and known mines), and a gun's stamp takes its shape: nothing inside its minimum range,
	* half its figure over the outer quarter of its reach.  A seat without them stamps none of the four, and
	* every read of an empty layer adds an exact zero.
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

	/** Add power[ AITHREAT_VEHICLE ] and power[ AITHREAT_INFANTRY ] to every cell a gun of groundRange,
		* fired from (x, y, z), reaches, and power[ AITHREAT_AIR ] to every cell one of airRange reaches. */
	void stampEnemy( Real x, Real y, Real z, Real groundRange, Real airRange, const Real *power );

	/** The enemy layer read with the reader's weights.  AITHREAT_COST_MIX reads the vehicle slot alone, exactly. */
	Real enemyAt( Real x, Real y, const AIThreatMix &mix ) const;
	/** ... the part of it that stands still, read the same way. */
	Real staticAt( Real x, Real y, const AIThreatMix &mix ) const;
	/** What a walk over (x, y) should fear: the enemy layer, its still guns counted twice (they are still there
		* when the walker arrives, where an army may have moved on), and the hazards by the share of the reader
		* that walks.  With the three extra layers empty it is enemyAt to the bit. */
	Real routeAt( Real x, Real y, const AIThreatMix &mix ) const;
	/** The friendly layer: this side's guns against each class, read with the weights of what it fights. */
	Real friendAt( Real x, Real y, const AIThreatMix &mix ) const;
	Real friendAt( Real x, Real y ) const { return friendAt( x, y, AITHREAT_GROUND_MIX ); }
	/** Our side holds (x, y): nothing the enemy has there outweighs what we have, friendly read with friendMix
		* (what the enemy fields), the enemy with enemyMix (what we move).  Ground nobody covers is held. */
	Bool heldAt( Real x, Real y, const AIThreatMix &friendMix, const AIThreatMix &enemyMix ) const;
	Real valueAt( Real x, Real y ) const;
	Real hazardAt( Real x, Real y ) const;

	/** The whole map, into a save and back: a loaded game reads the same map the uninterrupted one
		* holds until its next rebuild, which can be most of a second away.  version is the owner's: below
		* 37 a cell held one enemy figure, which goes into all three slots; below 40 one friendly figure, which
		* goes into the vehicle and infantry slots, and no still, worth or hazard layer. */
	void xfer( Xfer *xfer, UnsignedByte version );

	void markBuilt( UnsignedInt frame ) { m_builtFrame = frame; m_built = TRUE; }
	void markUnbuilt() { m_built = FALSE; }
	Bool isBuilt() const { return m_built; }
	UnsignedInt getBuiltFrame() const { return m_builtFrame; }

	/** A stamp in two halves, for many maps of one grid stamping the same gun: the cells it reaches, appended
		* in the order a stamp adds to them, then the adding.  The same sums in the same order as stampEnemy and
		* stampEnemy, on any map of the same grid: the heights come off the one terrain. */
	void cellsReached( Real x, Real y, Real z, Real range, std::vector<Int> &cells ) const;
	/** ... a gun's shape: the cells from minRange out appended to cells, the ones past edgeShare of the reach
		* there (the high ground's stretch included) after the rest, *edgeCount of them.  scratch holds the edge
		* cells meanwhile.  minRange 0 and edgeShare 1 are cellsReached, cell for cell. */
	void cellsBanded( Real x, Real y, Real z, Real range, Real minRange, Real edgeShare,
		std::vector<Int> &cells, std::vector<Int> &scratch, Int *edgeCount ) const;
	/** Every cell whose centre is within radius of (x, y), flat: a worth or a hazard, not a gun. */
	void cellsInDisc( Real x, Real y, Real radius, std::vector<Int> &cells ) const;
	void addEnemy( const Int *cells, Int count, Real vehicle, Real infantry );
	void addEnemyAir( const Int *cells, Int count, Real air );
	void addStatic( const Int *cells, Int count, Real vehicle, Real infantry );
	void addStaticAir( const Int *cells, Int count, Real air );
	void addFriend( const Int *cells, Int count, Real vehicle, Real infantry );
	void addFriendAir( const Int *cells, Int count, Real air );
	void addValue( const Int *cells, Int count, Real value );
	void addHazard( const Int *cells, Int count, Real hazard );
	Bool sameGrid( Int cols, Int rows, Real originX, Real originY, Real cellSize ) const;
	Real getOriginX() const { return m_originX; }
	Real getOriginY() const { return m_originY; }
	Real getCellSize() const { return m_cellSize; }

private:
	template <class Visit> void visitReach( Real x, Real y, Real z, Real range, Real minRange, Real edgeShare, Visit &visit ) const;
	Int cellIndexAt( Real x, Real y ) const;

	Real m_originX, m_originY, m_cellSize;
	Int m_cols, m_rows;
	std::vector<Real> m_enemy;		///< AITHREAT_COUNT a cell, in AIThreatClass order
	std::vector<Real> m_static;		///< ... the part of it from guns that stand still
	std::vector<Real> m_friend;		///< AITHREAT_COUNT a cell
	std::vector<Real> m_value;		///< one a cell: the enemy's worth near it
	std::vector<Real> m_hazard;		///< one a cell
	std::vector<Real> m_height;
	Bool m_built;
	UnsignedInt m_builtFrame;
};

/** What a gun on the move stamps from: its place plus leadFrames of its velocity, the lead no longer than
	* maxLead, so the map shows where a column is about to be and not where it was. */
void AIInfluence_projectedCentre( Real x, Real y, Real velX, Real velY, Real leadFrames, Real maxLead, Real *outX, Real *outY );

/** How much of a remembered enemy's figure is left age frames after it fell out of sight: all of it at 0,
	* falling evenly to none at fadeFrames. */
Real AIInfluence_memoryShare( UnsignedInt age, UnsignedInt fadeFrames );

/** Where a unit that outranges what is closing on it should step to: on the line from the enemy
	* through the unit, at the far edge of its own reach, which is as far from the enemy's gun as it can
	* stand and still shoot.  FALSE when it has no such room, which is when its range is not longer than
	* the enemy's by twice the margin. */
Bool AIKite_standoffDistance( Real myRange, Real enemyRange, Real margin, Real *distance );

#endif
