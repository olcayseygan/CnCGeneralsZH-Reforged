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

// OrderQueue.h ///////////////////////////////////////////////////////////////////////////////////
// The orders a player has lined up with shift, kept by the logic and handed to the units one at a
// time as they finish the one in front.
//
// The queue used to live on the client, which sent each next order over the network once it saw the
// last one was over.  Every step therefore waited a round trip, which at the 200-300 ms most players
// sit at is a visible pause at every corner, and the list belonged to the selection: clicking
// anything else threw it away.  Here the list belongs to the units it was given to, and it moves on
// the same logic frame on every machine.
//
// A queued order is an ordinary order message with MSG_QUEUE_NEXT_ORDER in front of it.  The logic
// keeps a copy of the message and dispatches it again, to the units that still hold the list, when
// its turn comes, so every kind of order the dispatcher knows can be queued without a line of its own
// here.
//
// The units that were told together form a chain and go through it together: nobody starts the next
// order until everyone left alive is done with this one, so an army does not string out across the
// map behind its fastest unit.  Selecting part of a chain and adding to it splits that part off with
// a copy of the list.  An order given without shift ends the list for the units it went to.  An
// upgrade bought with shift is a step as well, paid for when the chain reaches it.  A structure placed
// with shift is the other way round: it goes on the ground and is paid for at once, and the builder's
// step is to go and put it up when its list gets there.
//
// Everything here is logic state: it is saved with the player, and a replay rebuilds it from the
// recorded messages.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#ifndef __ORDERQUEUE_H__
#define __ORDERQUEUE_H__

#include "Common/GameCommon.h"
#include "Common/MessageStream.h"

#include <list>
#include <vector>

class AIGroup;
class Object;
class Player;
class Xfer;

/// The argument of MSG_QUEUE_NEXT_ORDER.  It arrives from another machine, so it is range-checked.
enum OrderQueueMode
{
	ORDER_QUEUE_NONE = -1,		///< the next order is not queued
	ORDER_QUEUE_APPEND = 0,		///< the next order goes after whatever its units are already doing
	ORDER_QUEUE_FRESH,				///< the next order starts a new list, dropping the old one

	ORDER_QUEUE_MODE_COUNT
};

//-------------------------------------------------------------------------------------------------
/** A copy of one order message, kept until its turn comes. */
//-------------------------------------------------------------------------------------------------
class QueuedOrder
{
public:
	QueuedOrder( void );

	void copyFrom( const GameMessage *msg );
	GameMessage *makeMessage( Int playerIndex ) const;		///< the caller deletes it

	GameMessage::Type getType( void ) const { return m_type; }
	Bool isStanding( void ) const { return m_type == GameMessage::MSG_INVALID; }
	ObjectID getTargetID( void ) const;											///< the object the order names, INVALID_ID for none
	Bool getDestination( Coord3D *pos ) const;							///< the first location it carries, FALSE for none
	const GameMessageArgumentType& getArgument( Int index ) const { return m_args[ index ]; }

	void xfer( Xfer *xfer );

private:
	GameMessage::Type											m_type;				///< MSG_INVALID: whatever the units were doing when the list began
	std::vector<GameMessageArgumentDataType>	m_argTypes;
	std::vector<GameMessageArgumentType>			m_args;
};

//-------------------------------------------------------------------------------------------------
/** One group of units and the orders they still owe. */
//-------------------------------------------------------------------------------------------------
struct OrderChain
{
	OrderChain( void );

	std::vector<ObjectID>		m_members;					///< sorted, so a split and a lookup agree on every machine
	QueuedOrder							m_active;						///< the order the chain is on now
	std::vector<QueuedOrder>	m_pending;					///< the rest, in the order they were given
	UnsignedInt							m_settledFrames;		///< how many frames in a row nobody has been working on m_active
	Bool										m_waitingForRearm;	///< the order stands, but its aircraft have gone home for ammo
};

typedef std::list<OrderChain> OrderChainList;

//-------------------------------------------------------------------------------------------------
/** Every chain one player has running. */
//-------------------------------------------------------------------------------------------------
class OrderQueue
{
public:
	OrderQueue( void );

	void reset( void );

	/// MSG_QUEUE_NEXT_ORDER: the next message from this player is queued in this way
	void setNextOrderMode( Int mode );

	/**
		Called for every message the player sends.  Returns TRUE when the message was a queued order and
		has been dealt with here, in which case selected has been destroyed and the dispatcher must not
		carry the order out itself.  An order given without shift ends the list for the units it names
		and returns FALSE, leaving selected alone.
	*/
	Bool takeMessage( GameMessage *msg, AIGroup *selected, Player *owner );

	/// A MSG_QUEUE_NEXT_ORDER with no order behind it in the same frame is forgotten, not carried over.
	void forgetNextOrderMode( void ) { m_nextOrderMode = ORDER_QUEUE_NONE; }

	/// The mark the last MSG_QUEUE_NEXT_ORDER left, used up.  A placement reads it for itself.
	Int takeNextOrderMode( void ) { const Int mode = m_nextOrderMode; m_nextOrderMode = ORDER_QUEUE_NONE; return mode; }

	/// A placement made with shift: site goes up when builder's list gets to it
	void queueConstruction( const Object *site, Object *builder, Player *owner );

	void update( Player *owner );		///< once a logic frame: hand out the next order to every chain that is done

	const OrderChainList& getChains( void ) const { return m_chains; }
	const OrderChain *findChain( ObjectID id ) const;

	void xfer( Xfer *xfer );

	static Bool isOrder( GameMessage::Type type );			///< a hand-given order to the selected units
	static Bool isQueueable( GameMessage::Type type );	///< an order that can wait its turn in a chain
	static Bool isTerminal( GameMessage::Type type );		///< an order that never finishes: a post, and nothing can be queued behind it

private:
	void queueOrder( GameMessage *msg, const std::vector<ObjectID>& selected, Player *owner );
	Bool queueUpgrade( GameMessage *msg, AIGroup *selected, Player *owner );		///< FALSE: bought now, not queued
	void startChain( GameMessage *msg, const std::vector<ObjectID>& members, Player *owner );
	void addChain( GameMessage *msg, const std::vector<ObjectID>& members );
	void appendOrder( OrderChain& chain, GameMessage *msg, Player *owner );
	void releaseUnits( const std::vector<ObjectID>& ids );
	Bool isStepOver( OrderChain& chain, Player *owner );
	Bool advance( OrderChain& chain, Player *owner );		///< FALSE when the chain has nothing left

	OrderChainList	m_chains;
	Int							m_nextOrderMode;		///< an OrderQueueMode, set by MSG_QUEUE_NEXT_ORDER for the message after it
};

#endif // __ORDERQUEUE_H__
