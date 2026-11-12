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

// FILE: CRCSnapshotRing.h ////////////////////////////////////////////////////////////////////////
// Evidence for a mismatch, kept before anyone knows there will be one.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _CRC_SNAPSHOT_RING_H_
#define _CRC_SNAPSHOT_RING_H_

#include "Lib/BaseType.h"
#include "Common/GameCommon.h"	// LOGICFRAMES_PER_RETAIL_FRAME
#include <vector>

/* A mismatch is reported as one number: "the world CRC differs".  Worse, it is reported the
	 run-ahead's worth of frames after the frame it was computed for, by which time the world has
	 moved on and the divergent object is unrecoverable.  So keep the evidence while it is free to
	 keep: as the world CRC is computed, write down the running CRC after each object, into a ring
	 of the last few CRC frames.  Nothing reads the ring unless a mismatch is found, and then the
	 first object whose running CRC differs between two players' dumps is the object that diverged. */

#define CRC_SNAPSHOT_RING_SIZE (16 * LOGICFRAMES_PER_RETAIL_FRAME)	// 16 at the retail rate; the run-ahead counted in frames grows with the logic rate

struct CRCObjectEntry
{
	UnsignedInt m_id;								///< the ObjectID, as a plain int so this file needs no engine types
	UnsignedInt m_runningCRC;				///< world CRC after this object was folded in
	Real m_x, m_y, m_z;							///< where it was, to read the dump without cross-referencing
	Real m_health;
};

/* Every object's running CRC can agree and the world CRC still differ (the v3.2.0 reports 1127/1134,
	 1143/1148 and 1264/1265 did): then what split is something getCRC folds in after the objects.  So
	 keep the running CRC at the end of each part as well, in getCRC's order, and the first section
	 that differs between two dumps names the part. */
enum CRCSection
{
	CRC_SECTION_OBJECTS,			///< after the object walk
	CRC_SECTION_PARTITION,		///< after the logic random seed and the partition manager's shroud
	CRC_SECTION_PLAYERS,			///< after the player list: battle plans, skill and science points, money
	CRC_SECTION_AI,						///< after the pathfinder, the AI data and the AI groups
	CRC_SECTION_SCRIPTS,			///< after the script engine
	CRC_SECTION_COUNT
};

/// the player list's CRCed numbers, so a dump says which purse or which points moved
struct CRCPlayerEntry
{
	UnsignedInt m_money;
	Int m_skillPoints;
	Int m_sciencePurchasePoints;
};

/// one run of a player's commands of one type on one frame (see CRCSnapshot::m_commands)
struct CRCCommandEntry
{
	UnsignedInt m_frame;						///< the logic frame the commands executed on
	Int m_player;										///< player index
	Int m_type;											///< GameMessage::Type
	UnsignedInt m_count;						///< messages in the run
	UnsignedInt m_argHash;					///< every argument of every message in the run, in order
};

struct CRCSnapshot
{
	UnsignedInt m_frame;
	UnsignedInt m_totalCRC;
	UnsignedInt m_randomSeed;
	Bool m_valid;										///< FALSE until endSnapshot() closes it
	std::vector<CRCObjectEntry> m_objects;
	UnsignedInt m_sectionCRC[ CRC_SECTION_COUNT ];	///< world CRC at the end of each CRCSection
	std::vector<CRCPlayerEntry> m_players;					///< by player index
	/* The network commands logic executed after the previous snapshot and before this one, the inputs
		 that made this world out of the last.  Two dumps whose CRCs part on a frame with the same
		 digests ran the same orders differently (a logic split); different digests were given different
		 orders (an input split). */
	std::vector<CRCCommandEntry> m_commands;
};

//-------------------------------------------------------------------------------------------------
/** A fixed ring of per-object CRC snapshots.  Deliberately free of engine singletons so it can be
	* tested on its own. */
//-------------------------------------------------------------------------------------------------
class CRCSnapshotRing
{
public:
	CRCSnapshotRing( void );

	void clear( void );																					///< forget everything (new game)

	void beginSnapshot( UnsignedInt frame );										///< start filling the next slot
	void addObject( UnsignedInt id, UnsignedInt runningCRC,
									Real x, Real y, Real z, Real health );				///< record one object's running CRC
	void setSectionCRC( CRCSection section, UnsignedInt runningCRC );	///< record the world CRC at a section's end
	void addPlayer( UnsignedInt money, Int skillPoints, Int sciencePurchasePoints );	///< record the next player's numbers
	void endSnapshot( UnsignedInt totalCRC, UnsignedInt randomSeed );	///< close the slot being filled
	void addCommand( UnsignedInt frame, Int player, Int type, UnsignedInt argHash );	///< one executed command, for the next snapshot

	Int getNewestSlot( void ) const;														///< -1 when no snapshot is complete
	Int getNthNewestSlot( Int n ) const;												///< n==0 is the newest; -1 when out of range
	Int findSlotByCRC( UnsignedInt crc ) const;									///< newest first; -1 when no slot has that CRC
	Int findSlotByFrame( UnsignedInt frame ) const;							///< newest first; -1 when no slot is that frame
	const CRCSnapshot *getSlot( Int slot ) const;								///< NULL for an out of range or unfilled slot

	Int getNumSnapshots( void ) const { return m_numSnapshots; }

private:
	CRCSnapshot m_snapshots[ CRC_SNAPSHOT_RING_SIZE ];
	Int m_writeSlot;																						///< slot beginSnapshot() last opened, -1 before the first
	Int m_numSnapshots;																					///< how many slots hold real data
	std::vector<CRCCommandEntry> m_pendingCommands;							///< executed since the last beginSnapshot()
};

#endif // _CRC_SNAPSHOT_RING_H_
