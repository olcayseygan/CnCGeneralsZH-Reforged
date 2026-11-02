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

// OrderQueue.cpp /////////////////////////////////////////////////////////////////////////////////
// The shift queue, kept by the logic. See OrderQueue.h for what it is for.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameLogic/OrderQueue.h"

#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/SpecialPowerUpdateModule.h"

#include <algorithm>
#include <iterator>

// How long everyone in a chain has to have been off its order before the next one goes out.  A unit
// that has just been given an order can read as idle for a frame while it is still being set up, and
// an aircraft taxiing out holds its order rather than being on it; four retail frames is an eighth of
// a second, which is invisible at a corner and longer than either of those.
static const UnsignedInt STEP_SETTLE_FRAMES = 4 * LOGICFRAMES_PER_RETAIL_FRAME;

//-------------------------------------------------------------------------------------------------
QueuedOrder::QueuedOrder( void ) : m_type( GameMessage::MSG_INVALID )
{
}

//-------------------------------------------------------------------------------------------------
void QueuedOrder::copyFrom( const GameMessage *msg )
{
	m_type = msg->getType();
	m_argTypes.clear();
	m_args.clear();
	const Int count = msg->getArgumentCount();
	for( Int i = 0; i < count; i++ )
	{
		m_argTypes.push_back( msg->getArgumentDataType( i ) );
		m_args.push_back( *msg->getArgument( i ) );
	}
}

//-------------------------------------------------------------------------------------------------
GameMessage *QueuedOrder::makeMessage( Int playerIndex ) const
{
	GameMessage *msg = newInstance( GameMessage )( m_type );
	msg->friend_setPlayerIndex( playerIndex );

	const size_t count = m_args.size();
	for( size_t i = 0; i < count; i++ )
	{
		const GameMessageArgumentType& arg = m_args[ i ];
		switch( m_argTypes[ i ] )
		{
			case ARGUMENTDATATYPE_INTEGER:			msg->appendIntegerArgument( arg.integer ); break;
			case ARGUMENTDATATYPE_REAL:					msg->appendRealArgument( arg.real ); break;
			case ARGUMENTDATATYPE_BOOLEAN:			msg->appendBooleanArgument( arg.boolean ); break;
			case ARGUMENTDATATYPE_OBJECTID:			msg->appendObjectIDArgument( arg.objectID ); break;
			case ARGUMENTDATATYPE_DRAWABLEID:		msg->appendDrawableIDArgument( arg.drawableID ); break;
			case ARGUMENTDATATYPE_TEAMID:				msg->appendTeamIDArgument( arg.teamID ); break;
			case ARGUMENTDATATYPE_LOCATION:			msg->appendLocationArgument( arg.location ); break;
			case ARGUMENTDATATYPE_PIXEL:				msg->appendPixelArgument( arg.pixel ); break;
			case ARGUMENTDATATYPE_PIXELREGION:	msg->appendPixelRegionArgument( arg.pixelRegion ); break;
			case ARGUMENTDATATYPE_TIMESTAMP:		msg->appendTimestampArgument( arg.timestamp ); break;
			case ARGUMENTDATATYPE_WIDECHAR:			msg->appendWideCharArgument( arg.wChar ); break;
			default:
				DEBUG_CRASH(( "QueuedOrder: argument %d of %s has unknown type %d", (Int)i,
											GameMessage::getCommandTypeAsAsciiString( m_type ).str(), (Int)m_argTypes[ i ] ));
				break;
		}
	}

	return msg;
}

//-------------------------------------------------------------------------------------------------
ObjectID QueuedOrder::getTargetID( void ) const
{
	// the object an order at a place carries is whatever stood on that spot, not what it is for
	if( m_type == GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION || m_type == GameMessage::MSG_DO_WEAPON_AT_LOCATION )
		return INVALID_ID;

	// MSG_ENTER names the selection first, as INVALID_ID, and what to climb into second
	for( size_t i = 0; i < m_args.size(); i++ )
	{
		if( m_argTypes[ i ] == ARGUMENTDATATYPE_OBJECTID && m_args[ i ].objectID != INVALID_ID )
			return m_args[ i ].objectID;
	}
	return INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
Bool QueuedOrder::getDestination( Coord3D *pos ) const
{
	for( size_t i = 0; i < m_args.size(); i++ )
	{
		if( m_argTypes[ i ] == ARGUMENTDATATYPE_LOCATION )
		{
			*pos = m_args[ i ].location;
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Version Info:
	* 1: Initial version */
//-------------------------------------------------------------------------------------------------
void QueuedOrder::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	Int type = (Int)m_type;
	xfer->xferInt( &type );
	m_type = (GameMessage::Type)type;

	UnsignedByte count = (UnsignedByte)m_args.size();
	xfer->xferUnsignedByte( &count );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_argTypes.resize( count );
		m_args.resize( count );
	}

	for( UnsignedByte i = 0; i < count; i++ )
	{
		Int argType = (Int)m_argTypes[ i ];
		xfer->xferInt( &argType );
		m_argTypes[ i ] = (GameMessageArgumentDataType)argType;

		GameMessageArgumentType& arg = m_args[ i ];
		switch( m_argTypes[ i ] )
		{
			case ARGUMENTDATATYPE_INTEGER:			xfer->xferInt( &arg.integer ); break;
			case ARGUMENTDATATYPE_REAL:					xfer->xferReal( &arg.real ); break;
			case ARGUMENTDATATYPE_BOOLEAN:			xfer->xferBool( &arg.boolean ); break;
			case ARGUMENTDATATYPE_OBJECTID:			xfer->xferObjectID( &arg.objectID ); break;
			case ARGUMENTDATATYPE_DRAWABLEID:		xfer->xferDrawableID( &arg.drawableID ); break;
			case ARGUMENTDATATYPE_TEAMID:				xfer->xferUnsignedInt( &arg.teamID ); break;
			case ARGUMENTDATATYPE_LOCATION:			xfer->xferCoord3D( &arg.location ); break;
			case ARGUMENTDATATYPE_PIXEL:				xfer->xferICoord2D( &arg.pixel ); break;
			case ARGUMENTDATATYPE_PIXELREGION:	xfer->xferIRegion2D( &arg.pixelRegion ); break;
			case ARGUMENTDATATYPE_TIMESTAMP:		xfer->xferUnsignedInt( &arg.timestamp ); break;
			case ARGUMENTDATATYPE_WIDECHAR:			xfer->xferUser( &arg.wChar, sizeof( arg.wChar ) ); break;
			default:
				throw XFER_INVALID_PARAMETERS;
		}
	}
}

//-------------------------------------------------------------------------------------------------
OrderChain::OrderChain( void ) : m_settledFrames( 0 ), m_waitingForRearm( FALSE )
{
}

//-------------------------------------------------------------------------------------------------
/** Is this unit still one of the chain's?  A unit that has died, changed hands or climbed into
	* something has left it.  A unit in the tunnels on its way somewhere is still walking the order it
	* was given. */
//-------------------------------------------------------------------------------------------------
static Bool OrderQueue_holdsChain( const Object *obj, const Player *owner )
{
	if( obj == NULL || obj->isEffectivelyDead() || obj->getControllingPlayer() != owner )
		return FALSE;

	if( obj->getContainedBy() == NULL )
		return TRUE;

	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	return ai != NULL && ai->hasTunnelTrip();
}

//-------------------------------------------------------------------------------------------------
/** A capture, a charge being planted, a hack: the ability walks the unit up to its target with orders
	* of its own, so while it runs the unit reads as idle or as told by the AI. */
//-------------------------------------------------------------------------------------------------
static Bool OrderQueue_isUsingAbility( const Object *obj )
{
	for( BehaviorModule **module = obj->getBehaviorModules(); *module; ++module )
	{
		const SpecialPowerUpdateInterface *power = (*module)->getSpecialPowerUpdateInterface();
		if( power != NULL && power->isSpecialAbility() && power->isActive() )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Is this unit busy with an order the player gave?  A fight it picked for itself while standing
	* about is not one: the group is done, and the next order takes it off that fight.  Nor is a
	* guard, which never ends.  A fight an attack move stopped for is the order itself, though it
	* marks itself the AI's while it lasts: without this a queued attack move ended the moment it met
	* anybody. */
//-------------------------------------------------------------------------------------------------
static Bool OrderQueue_isWorking( const Object *obj, const AIUpdateInterface *ai )
{
	if( OrderQueue_isUsingAbility( obj ) )
		return TRUE;

	if( ai->isIdle() )
		return FALSE;

	// a unit still driving out of the structure that built it holds the player's orders until it is
	// out (AIUpdateInterface::isAllowedToRespondToAiCommands); a queued one handed over now would be
	// held with them and replace the order in front of it
	if( ai->isExitingProducer() )
		return TRUE;

	if( ai->getCurrentStateID() == AI_ATTACK_MOVE_TO )
		return TRUE;

	if( ai->getLastCommandSource() != CMD_FROM_PLAYER )
		return FALSE;

	switch( ai->getCurrentStateID() )
	{
		case AI_GUARD:
		case AI_GUARD_RETALIATE:
		case AI_GUARD_TUNNEL_NETWORK:
			return FALSE;

		default:
			return TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
/** Is this unit walking a plain move, which a queued move can be added to the end of? */
//-------------------------------------------------------------------------------------------------
static Bool OrderQueue_isWalking( const AIUpdateInterface *ai )
{
	switch( ai->getCurrentStateID() )
	{
		case AI_MOVE_TO:
		case AI_FOLLOW_PATH:
		case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS:
		case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM:
			return TRUE;

		default:
			return FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
/** A special power that names the object firing it - a superweapon off the shortcut bar - goes to
	* that object whatever is selected, so it is no order to the selection at all. */
//-------------------------------------------------------------------------------------------------
static Bool OrderQueue_namesItsOwnSource( const GameMessage *msg )
{
	Int sourceArg;
	switch( msg->getType() )
	{
		case GameMessage::MSG_DO_SPECIAL_POWER:							sourceArg = 2; break;
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:		sourceArg = 3; break;
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:	sourceArg = 5; break;
		default:																						return FALSE;
	}
	return msg->getArgument( sourceArg )->objectID != INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
/** One line per thing a chain did.  The queue is driven by a player's clicks, so no unattended run
	* reproduces a fault in it and this line is the whole instrument.  Never one per frame. */
//-------------------------------------------------------------------------------------------------
static void OrderQueue_log( const char *what, const OrderChain& chain, const Player *owner )
{
	DEBUG_LOG(( "order queue: frame %d, player %d, %d units, %s, on %s, %d owed\n",
							TheGameLogic->getFrame(), owner->getPlayerIndex(), (Int)chain.m_members.size(), what,
							GameMessage::getCommandTypeAsAsciiString( chain.m_active.getType() ).str(),
							(Int)chain.m_pending.size() ));
}

//-------------------------------------------------------------------------------------------------
/** Hand an order to the units, as if the player had selected exactly them and given it. */
//-------------------------------------------------------------------------------------------------
static void OrderQueue_dispatch( GameMessage *msg, const std::vector<ObjectID>& members )
{
	AIGroup *group = TheAI->createGroup();
	for( std::vector<ObjectID>::const_iterator it = members.begin(); it != members.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
			group->add( obj );
	}

	// the dispatcher destroys the group when it is done with it
	TheGameLogic->logicMessageDispatcher( msg, group );
}

//-------------------------------------------------------------------------------------------------
static void OrderQueue_dispatch( const QueuedOrder& order, const std::vector<ObjectID>& members, Player *owner )
{
	GameMessage *msg = order.makeMessage( owner->getPlayerIndex() );
	OrderQueue_dispatch( msg, members );
	msg->deleteInstance();
}

//-------------------------------------------------------------------------------------------------
/** A queued move while the units are already walking one goes on the end of the path they are
	* walking, the way a shift move always did, rather than waiting for them to stop first. */
//-------------------------------------------------------------------------------------------------
static void OrderQueue_extendWalk( const Coord3D& dest, const std::vector<ObjectID>& members, Player *owner )
{
	GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_ADD_WAYPOINT );
	msg->friend_setPlayerIndex( owner->getPlayerIndex() );
	msg->appendLocationArgument( dest );
	OrderQueue_dispatch( msg, members );
	msg->deleteInstance();
}

//-------------------------------------------------------------------------------------------------
OrderQueue::OrderQueue( void ) : m_nextOrderMode( ORDER_QUEUE_NONE )
{
}

//-------------------------------------------------------------------------------------------------
void OrderQueue::reset( void )
{
	m_chains.clear();
	m_nextOrderMode = ORDER_QUEUE_NONE;
}

//-------------------------------------------------------------------------------------------------
void OrderQueue::setNextOrderMode( Int mode )
{
	if( mode < ORDER_QUEUE_APPEND || mode >= ORDER_QUEUE_MODE_COUNT )
	{
		DEBUG_CRASH(( "MSG_QUEUE_NEXT_ORDER: mode %d is not an OrderQueueMode", mode ));
		m_nextOrderMode = ORDER_QUEUE_NONE;
		return;
	}
	m_nextOrderMode = mode;
}

//-------------------------------------------------------------------------------------------------
Bool OrderQueue::isOrder( GameMessage::Type type )
{
	switch( type )
	{
		case GameMessage::MSG_DO_MOVETO:
		case GameMessage::MSG_DO_ATTACKMOVETO:
		case GameMessage::MSG_DO_FORCEMOVETO:
		case GameMessage::MSG_ADD_WAYPOINT:
		case GameMessage::MSG_DO_SALVAGE:
		case GameMessage::MSG_DO_ATTACK_OBJECT:
		case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
		case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
		case GameMessage::MSG_DO_GUARD_POSITION:
		case GameMessage::MSG_DO_GUARD_OBJECT:
		case GameMessage::MSG_DO_HOLD_POSITION:
		case GameMessage::MSG_DO_AUTO_GARRISON:
		case GameMessage::MSG_DO_AUTO_REPAIR:
		case GameMessage::MSG_DO_STOP:
		case GameMessage::MSG_DO_SCATTER:
		case GameMessage::MSG_DO_FORMATION_MOVETO:
		case GameMessage::MSG_DO_FORMATION_ATTACKMOVETO:
		case GameMessage::MSG_DO_FORMATION_FORCEATTACK:
		case GameMessage::MSG_DO_FORMATION_GUARD:
		case GameMessage::MSG_ENTER:
		case GameMessage::MSG_DOCK:
		case GameMessage::MSG_GET_REPAIRED:
		case GameMessage::MSG_GET_HEALED:
		case GameMessage::MSG_DO_REPAIR:
		case GameMessage::MSG_DO_WEAPON_AT_OBJECT:
		case GameMessage::MSG_DO_WEAPON_AT_LOCATION:
		case GameMessage::MSG_COMBATDROP_AT_OBJECT:
		case GameMessage::MSG_COMBATDROP_AT_LOCATION:
		case GameMessage::MSG_DO_SPECIAL_POWER:
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:
			return TRUE;

		default:
			return FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
Bool OrderQueue::isQueueable( GameMessage::Type type )
{
	switch( type )
	{
		case GameMessage::MSG_DO_MOVETO:
		case GameMessage::MSG_DO_ATTACKMOVETO:
		case GameMessage::MSG_DO_FORCEMOVETO:
		case GameMessage::MSG_DO_SALVAGE:
		case GameMessage::MSG_DO_ATTACK_OBJECT:
		case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
		case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
		case GameMessage::MSG_DO_GUARD_POSITION:
		case GameMessage::MSG_DO_GUARD_OBJECT:
		case GameMessage::MSG_DO_HOLD_POSITION:
		case GameMessage::MSG_DO_FORMATION_MOVETO:
		case GameMessage::MSG_DO_FORMATION_ATTACKMOVETO:
		case GameMessage::MSG_DO_FORMATION_FORCEATTACK:
		case GameMessage::MSG_DO_FORMATION_GUARD:
		case GameMessage::MSG_ENTER:
		case GameMessage::MSG_DOCK:
		case GameMessage::MSG_GET_REPAIRED:
		case GameMessage::MSG_GET_HEALED:
		case GameMessage::MSG_DO_REPAIR:
		case GameMessage::MSG_DO_WEAPON_AT_OBJECT:
		case GameMessage::MSG_DO_WEAPON_AT_LOCATION:
		case GameMessage::MSG_COMBATDROP_AT_OBJECT:
		case GameMessage::MSG_COMBATDROP_AT_LOCATION:
		case GameMessage::MSG_DO_SPECIAL_POWER:
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:
			return TRUE;

		default:
			return FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
Bool OrderQueue::isTerminal( GameMessage::Type type )
{
	switch( type )
	{
		case GameMessage::MSG_DO_GUARD_POSITION:
		case GameMessage::MSG_DO_GUARD_OBJECT:
		case GameMessage::MSG_DO_HOLD_POSITION:
		case GameMessage::MSG_DO_FORMATION_GUARD:
		case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
		case GameMessage::MSG_DO_FORMATION_FORCEATTACK:
			return TRUE;

		default:
			return FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
const OrderChain *OrderQueue::findChain( ObjectID id ) const
{
	for( OrderChainList::const_iterator it = m_chains.begin(); it != m_chains.end(); ++it )
	{
		if( std::binary_search( it->m_members.begin(), it->m_members.end(), id ) )
			return &(*it);
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------
Bool OrderQueue::takeMessage( GameMessage *msg, AIGroup *selected, Player *owner )
{
	const Int mode = m_nextOrderMode;
	m_nextOrderMode = ORDER_QUEUE_NONE;

	// buying one is no order to the units, so without shift it leaves their list alone
	if( msg->getType() == GameMessage::MSG_QUEUE_UPGRADE )
		return mode != ORDER_QUEUE_NONE && queueUpgrade( msg, selected, owner );

	if( selected == NULL || !isOrder( msg->getType() ) || OrderQueue_namesItsOwnSource( msg ) )
		return FALSE;

	// An attack circle names the one unit a shot is for, as the argument after the target. The chain
	// is that unit's alone, or the whole selection would still share every target. A one-argument
	// attack is the click it always was, and it still goes to everyone selected. A named unit that
	// has already died, or that this player does not own, is not a reason to fire the rest of the
	// selection at that target either.
	std::vector<ObjectID> ids;
	if( msg->getType() == GameMessage::MSG_DO_ATTACK_OBJECT && msg->getArgumentCount() >= 2 )
	{
		const ObjectID attackerID = msg->getArgument( 1 )->objectID;
		const Object *attacker = TheGameLogic->findObjectByID( attackerID );
		if( attacker != NULL && attacker->getControllingPlayer() != owner )
		{
			DEBUG_CRASH(( "MSG_DO_ATTACK_OBJECT: player %d named object %d, which it does not control",
										owner->getPlayerIndex(), (Int)attackerID ));
		}
		if( attacker == NULL || attacker->getControllingPlayer() != owner )
		{
			TheAI->destroyGroup( selected );
			return TRUE;
		}
		ids.push_back( attackerID );
	}
	else
	{
		ids = selected->getAllIDs();
		std::sort( ids.begin(), ids.end() );
	}

	if( mode == ORDER_QUEUE_NONE || !isQueueable( msg->getType() ) )
	{
		releaseUnits( ids );

		// a guard, a hold or a shot at the ground given without shift is a post all the same, and a
		// shift click behind it is turned away like one behind a queued guard
		if( isTerminal( msg->getType() ) )
			addChain( msg, ids );
		return FALSE;
	}

	// gone before anything is dispatched: every order handed out below puts its units into a group
	// of its own, and a unit joining one leaves this one, which destroys it when the last one goes
	TheAI->destroyGroup( selected );

	if( mode == ORDER_QUEUE_FRESH )
	{
		releaseUnits( ids );
		startChain( msg, ids, owner );
		return TRUE;
	}

	queueOrder( msg, ids, owner );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Shift: every chain the selection touches gets the order on its end.  A chain only part of which
	* is selected splits, and the selected part carries on with its own copy of the list; units in no
	* chain start one, behind whatever they are doing now. */
//-------------------------------------------------------------------------------------------------
void OrderQueue::queueOrder( GameMessage *msg, const std::vector<ObjectID>& selected, Player *owner )
{
	// who in the selection belongs to which chain, found before any chain is split or started, so the
	// new chains are not visited as though they were old ones
	std::vector<OrderChainList::iterator> touched;
	std::vector< std::vector<ObjectID> > parts;
	std::vector<ObjectID> loose = selected;

	for( OrderChainList::iterator it = m_chains.begin(); it != m_chains.end(); ++it )
	{
		std::vector<ObjectID> part;
		std::set_intersection( it->m_members.begin(), it->m_members.end(),
													 selected.begin(), selected.end(), std::back_inserter( part ) );
		if( part.empty() )
			continue;

		std::vector<ObjectID> rest;
		std::set_difference( loose.begin(), loose.end(), part.begin(), part.end(), std::back_inserter( rest ) );
		loose.swap( rest );

		// units on a post have no next place to be, so the order is not theirs
		if( isTerminal( it->m_active.getType() ) )
			continue;

		touched.push_back( it );
		parts.push_back( part );
	}

	for( size_t i = 0; i < touched.size(); i++ )
	{
		OrderChain& chain = *touched[ i ];
		if( parts[ i ].size() == chain.m_members.size() )
		{
			appendOrder( chain, msg, owner );
			continue;
		}

		OrderChain split = chain;
		split.m_members = parts[ i ];

		std::vector<ObjectID> rest;
		std::set_difference( chain.m_members.begin(), chain.m_members.end(),
												 parts[ i ].begin(), parts[ i ].end(), std::back_inserter( rest ) );
		chain.m_members.swap( rest );

		m_chains.push_back( split );
		appendOrder( m_chains.back(), msg, owner );
	}

	if( loose.empty() )
		return;

	Bool anyWorking = FALSE;
	Bool allWalking = TRUE;
	for( std::vector<ObjectID>::const_iterator it = loose.begin(); it != loose.end(); ++it )
	{
		const Object *obj = TheGameLogic->findObjectByID( *it );
		const AIUpdateInterface *ai = obj ? obj->getAIUpdateInterface() : NULL;
		if( ai == NULL || !OrderQueue_isWorking( obj, ai ) )
			continue;
		anyWorking = TRUE;
		if( !OrderQueue_isWalking( ai ) )
			allWalking = FALSE;
	}

	if( !anyWorking )
	{
		startChain( msg, loose, owner );
		return;
	}

	// they are busy with an order that was given without shift, so the list starts behind it
	OrderChain chain;
	chain.m_members = loose;
	m_chains.push_back( chain );
	OrderChain& added = m_chains.back();

	if( msg->getType() == GameMessage::MSG_DO_MOVETO && allWalking )
	{
		OrderQueue_extendWalk( msg->getArgument( 0 )->location, added.m_members, owner );
		added.m_active.copyFrom( msg );
		return;
	}

	added.m_pending.push_back( QueuedOrder() );
	added.m_pending.back().copyFrom( msg );
}

//-------------------------------------------------------------------------------------------------
/** Shift on an upgrade button: the unit buys it when its list gets there, and pays then; if the money
	* is not there by that time the upgrade is passed over.  It becomes a step of the chain the unit is
	* in, which does not split for it, since the upgrade names its unit and the rest of the chain has
	* nothing to do but wait a moment.  A unit busy with an order given without shift starts a list
	* behind it, and one with nothing to do buys it now. */
//-------------------------------------------------------------------------------------------------
Bool OrderQueue::queueUpgrade( GameMessage *msg, AIGroup *selected, Player *owner )
{
	const ObjectID producerID = msg->getArgument( 0 )->objectID;
	const Object *producer = TheGameLogic->findObjectByID( producerID );
	if( producer == NULL || producer->getControllingPlayer() != owner )
		return FALSE;

	OrderChainList::iterator chain = m_chains.begin();
	while( chain != m_chains.end() && !std::binary_search( chain->m_members.begin(), chain->m_members.end(), producerID ) )
		++chain;

	// a unit on a post has nothing to wait for, so it buys it now
	if( chain != m_chains.end() && isTerminal( chain->m_active.getType() ) )
		return FALSE;

	if( chain == m_chains.end() )
	{
		const AIUpdateInterface *ai = producer->getAIUpdateInterface();
		if( ai == NULL || !OrderQueue_isWorking( producer, ai ) )
			return FALSE;

		m_chains.push_back( OrderChain() );
		chain = --m_chains.end();
		chain->m_members.push_back( producerID );
	}

	if( selected )
		TheAI->destroyGroup( selected );
	appendOrder( *chain, msg, owner );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A new chain whose first order goes out now. */
//-------------------------------------------------------------------------------------------------
void OrderQueue::startChain( GameMessage *msg, const std::vector<ObjectID>& members, Player *owner )
{
	OrderQueue_dispatch( msg, members );
	addChain( msg, members );
}

//-------------------------------------------------------------------------------------------------
/** A chain on the order msg, which the caller has seen carried out.  On a guard, a hold or a shot at
	* the ground it is a post: it holds nothing, it never moves on, and it is there only so a shift
	* click behind it can be turned away, until an order without shift takes the units off it. */
//-------------------------------------------------------------------------------------------------
void OrderQueue::addChain( GameMessage *msg, const std::vector<ObjectID>& members )
{
	OrderChain chain;
	chain.m_members = members;
	chain.m_active.copyFrom( msg );
	m_chains.push_back( chain );
}

//-------------------------------------------------------------------------------------------------
void OrderQueue::appendOrder( OrderChain& chain, GameMessage *msg, Player *owner )
{
	// nothing after a guard would ever come round
	if( isTerminal( chain.m_active.getType() )
			|| ( !chain.m_pending.empty() && isTerminal( chain.m_pending.back().getType() ) ) )
		return;

	if( msg->getType() == GameMessage::MSG_DO_MOVETO && chain.m_pending.empty()
			&& chain.m_active.getType() == GameMessage::MSG_DO_MOVETO )
	{
		OrderQueue_extendWalk( msg->getArgument( 0 )->location, chain.m_members, owner );
		return;
	}

	chain.m_pending.push_back( QueuedOrder() );
	chain.m_pending.back().copyFrom( msg );
}

//-------------------------------------------------------------------------------------------------
/** An order given without shift: those units are out of whatever chain they were in. */
//-------------------------------------------------------------------------------------------------
void OrderQueue::releaseUnits( const std::vector<ObjectID>& ids )
{
	for( OrderChainList::iterator it = m_chains.begin(); it != m_chains.end(); )
	{
		std::vector<ObjectID> rest;
		std::set_difference( it->m_members.begin(), it->m_members.end(),
												 ids.begin(), ids.end(), std::back_inserter( rest ) );
		it->m_members.swap( rest );

		if( it->m_members.empty() )
			it = m_chains.erase( it );
		else
			++it;
	}
}

//-------------------------------------------------------------------------------------------------
void OrderQueue::update( Player *owner )
{
	for( OrderChainList::iterator it = m_chains.begin(); it != m_chains.end(); )
	{
		OrderChain& chain = *it;

		std::vector<ObjectID> alive;
		for( std::vector<ObjectID>::const_iterator id = chain.m_members.begin(); id != chain.m_members.end(); ++id )
		{
			if( OrderQueue_holdsChain( TheGameLogic->findObjectByID( *id ), owner ) )
				alive.push_back( *id );
		}
		chain.m_members.swap( alive );

		if( chain.m_members.empty() || ( isStepOver( chain, owner ) && !advance( chain, owner ) ) )
			it = m_chains.erase( it );
		else
			++it;
	}
}

//-------------------------------------------------------------------------------------------------
/** The order in front is over when its target is gone, or once nobody in the chain has been working
	* on it for a few frames.  An aircraft gone home for ammo is not done with it: the chain waits, and
	* sends the order again when the flight is back in the air with a full load, since an ordered
	* attack on one target does not survive a reload. */
//-------------------------------------------------------------------------------------------------
Bool OrderQueue::isStepOver( OrderChain& chain, Player *owner )
{
	const ObjectID targetID = chain.m_active.getTargetID();
	if( targetID != INVALID_ID )
	{
		const Object *target = TheGameLogic->findObjectByID( targetID );
		if( target == NULL || target->isEffectivelyDead() )
			return TRUE;
	}

	// a post never ends by itself; the object it guards dying is the one way off it, above
	if( isTerminal( chain.m_active.getType() ) )
		return FALSE;

	Bool anyWorking = FALSE;
	Bool anyRearming = FALSE;
	Bool anyReadyToGoAgain = FALSE;

	for( std::vector<ObjectID>::const_iterator id = chain.m_members.begin(); id != chain.m_members.end(); ++id )
	{
		const Object *obj = TheGameLogic->findObjectByID( *id );
		const AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if( ai == NULL )
			continue;

		const JetAIUpdate *jet = ai->getJetAIUpdate();
		if( jet != NULL )
		{
			if( jet->friend_isRearming() || obj->isOutOfAmmo() )
			{
				anyRearming = TRUE;
				continue;
			}

			// a parked aircraft carries its order in its pocket until the wheels are up
			ObjectID heldTarget = INVALID_ID;
			Coord3D heldPos;
			if( jet->friend_getHeldOrder( heldTarget, heldPos ) != AICMD_NO_COMMAND )
			{
				anyWorking = TRUE;
				continue;
			}
		}

		if( OrderQueue_isWorking( obj, ai ) || ai->hasTunnelTrip() )
			anyWorking = TRUE;
		else if( jet != NULL )
			anyReadyToGoAgain = TRUE;
	}

	if( anyWorking )
	{
		chain.m_settledFrames = 0;
		return FALSE;
	}

	if( anyRearming )
	{
		if( !chain.m_waitingForRearm )
			OrderQueue_log( "holding, gone home for ammo", chain, owner );
		chain.m_waitingForRearm = TRUE;
		chain.m_settledFrames = 0;
		return FALSE;
	}

	if( chain.m_waitingForRearm && anyReadyToGoAgain && !chain.m_active.isStanding() )
	{
		chain.m_waitingForRearm = FALSE;
		chain.m_settledFrames = 0;
		OrderQueue_log( "rearmed, sending the order again", chain, owner );
		OrderQueue_dispatch( chain.m_active, chain.m_members, owner );
		return FALSE;
	}

	chain.m_settledFrames++;
	return chain.m_settledFrames >= STEP_SETTLE_FRAMES;
}

//-------------------------------------------------------------------------------------------------
/** Send the next order that still makes sense.  A queued attack on something that has died since
	* is skipped, and a run of moves goes out as one path so the units do not stop at every point. */
//-------------------------------------------------------------------------------------------------
Bool OrderQueue::advance( OrderChain& chain, Player *owner )
{
	while( !chain.m_pending.empty() )
	{
		const QueuedOrder next = chain.m_pending.front();
		chain.m_pending.erase( chain.m_pending.begin() );

		const ObjectID targetID = next.getTargetID();
		if( targetID != INVALID_ID )
		{
			const Object *target = TheGameLogic->findObjectByID( targetID );
			if( target == NULL || target->isEffectivelyDead() )
				continue;

			// the list was copied when the chain split, and the upgrade belongs to whichever half has the unit
			if( next.getType() == GameMessage::MSG_QUEUE_UPGRADE
					&& !std::binary_search( chain.m_members.begin(), chain.m_members.end(), targetID ) )
				continue;
		}

		OrderQueue_dispatch( next, chain.m_members, owner );

		chain.m_active = next;
		chain.m_settledFrames = 0;
		chain.m_waitingForRearm = FALSE;
		OrderQueue_log( isTerminal( next.getType() ) ? "last order out, a post" : "next order out", chain, owner );

		Coord3D dest;
		while( next.getType() == GameMessage::MSG_DO_MOVETO && !chain.m_pending.empty()
					 && chain.m_pending.front().getType() == GameMessage::MSG_DO_MOVETO
					 && chain.m_pending.front().getDestination( &dest ) )
		{
			OrderQueue_extendWalk( dest, chain.m_members, owner );
			chain.m_pending.erase( chain.m_pending.begin() );
		}

		return TRUE;
	}

	OrderQueue_log( "list is done", chain, owner );
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Version Info:
	* 1: Initial version */
//-------------------------------------------------------------------------------------------------
void OrderQueue::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	UnsignedShort chainCount = (UnsignedShort)m_chains.size();
	xfer->xferUnsignedShort( &chainCount );
	if( xfer->getXferMode() == XFER_LOAD )
		m_chains.resize( chainCount );

	for( OrderChainList::iterator it = m_chains.begin(); it != m_chains.end(); ++it )
	{
		xfer->xferSTLObjectIDVector( &it->m_members );
		it->m_active.xfer( xfer );

		UnsignedShort pendingCount = (UnsignedShort)it->m_pending.size();
		xfer->xferUnsignedShort( &pendingCount );
		if( xfer->getXferMode() == XFER_LOAD )
			it->m_pending.resize( pendingCount );
		for( std::vector<QueuedOrder>::iterator order = it->m_pending.begin(); order != it->m_pending.end(); ++order )
			order->xfer( xfer );

		xfer->xferUnsignedInt( &it->m_settledFrames );
		xfer->xferBool( &it->m_waitingForRearm );
	}
}
