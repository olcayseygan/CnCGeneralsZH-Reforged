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


#pragma once

#ifndef __NETCOMMANDREF_H
#define __NETCOMMANDREF_H

#include "GameNetwork/NetCommandMsg.h"
#include "Common/GameMemory.h"

#if defined(_INTERNAL) || defined(_DEBUG)
//	#define DEBUG_NETCOMMANDREF
#endif

#ifdef DEBUG_NETCOMMANDREF
#define NEW_NETCOMMANDREF(msg) newInstance(NetCommandRef)(msg, __FILE__, __LINE__)
#else
#define NEW_NETCOMMANDREF(msg) newInstance(NetCommandRef)(msg)
#endif
 

class NetCommandRef : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(NetCommandRef, "NetCommandRef")		
public:
#ifdef DEBUG_NETCOMMANDREF
	NetCommandRef(NetCommandMsg *msg, char *filename, int line);
#else
	NetCommandRef(NetCommandMsg *msg);
#endif
	//~NetCommandRef();

	NetCommandMsg *getCommand();
	NetCommandRef *getNext();
	NetCommandRef *getPrev();
	void setNext(NetCommandRef *next);
	void setPrev(NetCommandRef *prev);

	void setRelay(SlotMask relay);
	SlotMask getRelay() const;

	time_t getTimeLastSent() const;
	void setTimeLastSent(time_t timeLastSent);

	Int getNumTimesSent() const;
	void markSent(time_t when);

	time_t getTimeLastOnWire() const;
	Int getNumCopiesSent() const;
	void markCopied(time_t when);

protected:
	NetCommandMsg *m_msg;
	NetCommandRef *m_next;
	NetCommandRef *m_prev;
	SlotMask m_relay; ///< Need this in the command reference since the relay value will be different depending on where this particular reference is being sent.
	time_t m_timeLastSent;
	Int m_numTimesSent;		///< How many times this reference has gone out.  An ack for a command sent more than once cannot be timed (which send is it acking?), so it is not used as a latency sample - Karn's rule.
	time_t m_timeLastOnWire;	///< The last send or redundant copy.  Spaces the copies; the retry timer keeps m_timeLastSent.
	Int m_numCopiesSent;			///< Redundant copies since the last real send.  See CONNECTION_REDUNDANT_COPIES.

#ifdef DEBUG_NETCOMMANDREF
	UnsignedInt m_id;
#endif
};

/**
 * Return the command message.
 */
inline NetCommandMsg * NetCommandRef::getCommand() 
{
	return m_msg;
}

/**
 * Return the next command ref in the list.
 */
inline NetCommandRef * NetCommandRef::getNext() 
{
	return m_next;
}

/**
 * Return the previous command ref in the list.
 */
inline NetCommandRef * NetCommandRef::getPrev() 
{
	return m_prev;
}

/**
 * Set the next command ref in the list.
 */
inline void NetCommandRef::setNext(NetCommandRef *next) 
{
	m_next = next;
}

/**
 * Set the previous command ref in the list.
 */
inline void NetCommandRef::setPrev(NetCommandRef *prev) 
{
	m_prev = prev;
}

/**
 * Return the time for the last time this command was sent from this reference.
 */
inline time_t NetCommandRef::getTimeLastSent() const
{
	return m_timeLastSent;
}

/**
 * Set the time for the last time this command was sent from this reference.
 */
inline void NetCommandRef::setTimeLastSent(time_t timeLastSent) 
{
	m_timeLastSent = timeLastSent;
}

/**
 * How many times this reference has been put on the wire.  1 is a first transmission, more is a
 * retransmission and its ack is ambiguous.
 */
inline Int NetCommandRef::getNumTimesSent() const
{
	return m_numTimesSent;
}

/**
 * Record a send: stamp the time and count it.
 */
inline void NetCommandRef::markSent(time_t when)
{
	m_timeLastSent = when;
	m_timeLastOnWire = when;
	m_numCopiesSent = 0;
	++m_numTimesSent;
}

/**
 * The last time this reference went out at all, real send or redundant copy.  -1 before the first.
 */
inline time_t NetCommandRef::getTimeLastOnWire() const
{
	return m_timeLastOnWire;
}

/**
 * How many redundant copies have followed the last real send.
 */
inline Int NetCommandRef::getNumCopiesSent() const
{
	return m_numCopiesSent;
}

/**
 * Record a redundant copy.  Neither the retry timer nor Karn's count moves: the copy exists so the
 * retry is never needed, and the first ack still times the original send.
 */
inline void NetCommandRef::markCopied(time_t when)
{
	m_timeLastOnWire = when;
	++m_numCopiesSent;
}

/**
 * Set the send relay for this reference of the command.
 */
inline void NetCommandRef::setRelay(SlotMask relay)
{
	m_relay = relay;
}

/**
 * Return the send relay for this refreence of the command.
 */
inline SlotMask NetCommandRef::getRelay() const
{
	return m_relay;
}

#endif // #ifndef __NETCOMMANDREF_H