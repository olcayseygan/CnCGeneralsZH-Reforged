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

// FILE: CRCSnapshotRing.cpp //////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameLogic/CRCSnapshotRing.h"

//-------------------------------------------------------------------------------------------------
CRCSnapshotRing::CRCSnapshotRing( void )
{
	clear();
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::clear( void )
{
	for( Int i = 0; i < CRC_SNAPSHOT_RING_SIZE; ++i )
	{
		m_snapshots[i].m_frame = 0;
		m_snapshots[i].m_totalCRC = 0;
		m_snapshots[i].m_randomSeed = 0;
		m_snapshots[i].m_valid = FALSE;
		m_snapshots[i].m_objects.clear();
		for( Int s = 0; s < CRC_SECTION_COUNT; ++s )
			m_snapshots[i].m_sectionCRC[s] = 0;
		m_snapshots[i].m_players.clear();
		m_snapshots[i].m_commands.clear();
	}
	m_writeSlot = -1;
	m_numSnapshots = 0;
	m_pendingCommands.clear();
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::beginSnapshot( UnsignedInt frame )
{
	m_writeSlot = (m_writeSlot + 1) % CRC_SNAPSHOT_RING_SIZE;

	CRCSnapshot &snap = m_snapshots[ m_writeSlot ];

	/* The oldest slot is being reused; if it held a complete snapshot it no longer counts as one
		 until this snapshot closes. */
	if( snap.m_valid )
		--m_numSnapshots;

	snap.m_frame = frame;
	snap.m_totalCRC = 0;
	snap.m_randomSeed = 0;
	snap.m_valid = FALSE;
	// keep the vector's capacity - the object count barely changes from one CRC frame to the next
	snap.m_objects.clear();
	for( Int s = 0; s < CRC_SECTION_COUNT; ++s )
		snap.m_sectionCRC[s] = 0;
	snap.m_players.clear();
	// the commands since the last snapshot belong to this one; swap so both vectors keep their capacity
	snap.m_commands.swap( m_pendingCommands );
	m_pendingCommands.clear();
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::addObject( UnsignedInt id, UnsignedInt runningCRC,
																 Real x, Real y, Real z, Real health )
{
	if( m_writeSlot < 0 )
		return;

	CRCObjectEntry entry;
	entry.m_id = id;
	entry.m_runningCRC = runningCRC;
	entry.m_x = x;
	entry.m_y = y;
	entry.m_z = z;
	entry.m_health = health;
	entry.m_vx = entry.m_vy = entry.m_vz = 0.0f;
	entry.m_payloadState = -1;

	m_snapshots[ m_writeSlot ].m_objects.push_back( entry );
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::setLastObjectMotion( Real vx, Real vy, Real vz, Int payloadState )
{
	if( m_writeSlot < 0 || m_snapshots[ m_writeSlot ].m_objects.empty() )
		return;

	CRCObjectEntry &entry = m_snapshots[ m_writeSlot ].m_objects.back();
	entry.m_vx = vx;
	entry.m_vy = vy;
	entry.m_vz = vz;
	entry.m_payloadState = payloadState;
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::setSectionCRC( CRCSection section, UnsignedInt runningCRC )
{
	if( m_writeSlot < 0 )
		return;

	m_snapshots[ m_writeSlot ].m_sectionCRC[ section ] = runningCRC;
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::addPlayer( UnsignedInt money, Int skillPoints, Int sciencePurchasePoints )
{
	if( m_writeSlot < 0 )
		return;

	CRCPlayerEntry entry;
	entry.m_money = money;
	entry.m_skillPoints = skillPoints;
	entry.m_sciencePurchasePoints = sciencePurchasePoints;

	m_snapshots[ m_writeSlot ].m_players.push_back( entry );
}

//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::endSnapshot( UnsignedInt totalCRC, UnsignedInt randomSeed )
{
	if( m_writeSlot < 0 )
		return;

	CRCSnapshot &snap = m_snapshots[ m_writeSlot ];
	snap.m_totalCRC = totalCRC;
	snap.m_randomSeed = randomSeed;
	snap.m_valid = TRUE;
	++m_numSnapshots;
}

//-------------------------------------------------------------------------------------------------
/** A run of one player's commands of one type on one frame folds into one entry, so a selection
	* change and its order cost two entries, not one per message. */
//-------------------------------------------------------------------------------------------------
void CRCSnapshotRing::addCommand( UnsignedInt frame, Int player, Int type, UnsignedInt argHash )
{
	if( !m_pendingCommands.empty() )
	{
		CRCCommandEntry &last = m_pendingCommands.back();
		if( last.m_frame == frame && last.m_player == player && last.m_type == type )
		{
			++last.m_count;
			last.m_argHash = last.m_argHash * 33 + argHash;
			return;
		}
	}

	CRCCommandEntry entry;
	entry.m_frame = frame;
	entry.m_player = player;
	entry.m_type = type;
	entry.m_count = 1;
	entry.m_argHash = argHash;
	m_pendingCommands.push_back( entry );
}

//-------------------------------------------------------------------------------------------------
Int CRCSnapshotRing::getNewestSlot( void ) const
{
	if( m_writeSlot < 0 )
		return -1;

	// only the slot being filled can be incomplete, so at worst we step back over that one
	for( Int k = 0; k < CRC_SNAPSHOT_RING_SIZE; ++k )
	{
		Int slot = m_writeSlot - k;
		while( slot < 0 )
			slot += CRC_SNAPSHOT_RING_SIZE;

		if( m_snapshots[ slot ].m_valid )
			return slot;
	}

	return -1;
}

//-------------------------------------------------------------------------------------------------
Int CRCSnapshotRing::getNthNewestSlot( Int n ) const
{
	Int newest = getNewestSlot();
	if( newest < 0 || n < 0 || n >= m_numSnapshots )
		return -1;

	Int slot = newest - n;
	while( slot < 0 )
		slot += CRC_SNAPSHOT_RING_SIZE;

	return m_snapshots[ slot ].m_valid ? slot : -1;
}

//-------------------------------------------------------------------------------------------------
/** Our own CRC for the mismatching frame is one of the values that were just compared, so the slot
	* carrying it names the frame the mismatch happened on - which is several frames behind the frame
	* it was detected on. */
//-------------------------------------------------------------------------------------------------
Int CRCSnapshotRing::findSlotByCRC( UnsignedInt crc ) const
{
	for( Int n = 0; n < m_numSnapshots; ++n )
	{
		Int slot = getNthNewestSlot( n );
		if( slot >= 0 && m_snapshots[ slot ].m_totalCRC == crc )
			return slot;
	}

	return -1;
}

//-------------------------------------------------------------------------------------------------
/** CRC messages carry the frame they were computed for, so the frame that mismatched is known
	* exactly and needs no lookup by value. */
//-------------------------------------------------------------------------------------------------
Int CRCSnapshotRing::findSlotByFrame( UnsignedInt frame ) const
{
	for( Int n = 0; n < m_numSnapshots; ++n )
	{
		Int slot = getNthNewestSlot( n );
		if( slot >= 0 && m_snapshots[ slot ].m_frame == frame )
			return slot;
	}

	return -1;
}

//-------------------------------------------------------------------------------------------------
const CRCSnapshot *CRCSnapshotRing::getSlot( Int slot ) const
{
	if( slot < 0 || slot >= CRC_SNAPSHOT_RING_SIZE || !m_snapshots[ slot ].m_valid )
		return NULL;

	return &m_snapshots[ slot ];
}
