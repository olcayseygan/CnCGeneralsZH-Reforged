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

// FILE: OCLSpecialPower.cpp //////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2002
// Desc:   Special powers that are driven by object creation lists
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/BuildAssistant.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeliverPayloadAIUpdate.h"
#include "GameLogic/Module/OCLSpecialPower.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// MODULE DATA ////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

enum 
{
	CREATE_ABOVE_LOCATION_HEIGHT = 300,
	MAX_ADJUST_RADIUS = 500
};

static const char* TheOCLCreateLocTypeNames[] =
{
	"CREATE_AT_EDGE_NEAR_SOURCE",
	"CREATE_AT_EDGE_NEAR_TARGET",
	"CREATE_AT_LOCATION",
	"USE_OWNER_OBJECT",
	"CREATE_ABOVE_LOCATION",
	"CREATE_AT_EDGE_FARTHEST_FROM_TARGET",
	NULL
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
OCLSpecialPowerModuleData::OCLSpecialPowerModuleData( void )
{
	m_defaultOCL = NULL;
	m_upgradeOCL.clear();
	m_createLoc = CREATE_AT_EDGE_NEAR_SOURCE;
	m_isOCLAdjustPositionToPassable = FALSE;
	m_airliftPlaneSlots = 0;
	m_airliftInfantrySlots = 1;
	m_airliftVehicleSlots = 4;
	m_airliftMaxPlanes = 4;
	m_airliftFeePerPlane = 0;
	m_airliftAllyRadius = 200.0f;
	m_airliftSpacing = 60.0f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static void parseOCLUpgradePair( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	OCLSpecialPowerModuleData::Upgrades up;

	const AsciiString token = parseSciencesThenValue(ini, &up.m_sciences);
	up.m_ocl = TheObjectCreationListStore->findObjectCreationList(token.str());	// could be null!
	if (!up.m_ocl && token.compareNoCase("None") != 0 && ini->getLoadType() == INI_LOAD_MULTIFILE)
	{
		// as INI::parseObjectCreationList: a patch naming a list the loaded data lacks drops the module
		DEBUG_LOG(("[LINE: %d in '%s'] object creation list '%s' does not exist; the reference is skipped.\n", ini->getLineNum(), ini->getFilename().str(), token.str()));
		ini->setMissingReference( TRUE );
		return;
	}
	DEBUG_ASSERTCRASH(up.m_ocl || token.compareNoCase("None") == 0, ("ObjectCreationList %s not found!\n", token.str()));

	std::vector<OCLSpecialPowerModuleData::Upgrades>* s = (std::vector<OCLSpecialPowerModuleData::Upgrades>*)store;
	s->push_back(up);
} 

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/* static */ void OCLSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	SpecialPowerModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] = 
	{
		{ "UpgradeOCL", parseOCLUpgradePair, NULL, offsetof( OCLSpecialPowerModuleData, m_upgradeOCL ) },
		{ "OCL", INI::parseObjectCreationList, NULL, offsetof( OCLSpecialPowerModuleData, m_defaultOCL ) },
		{ "CreateLocation", INI::parseIndexList, TheOCLCreateLocTypeNames, offsetof( OCLSpecialPowerModuleData, m_createLoc ) },
		{ "ReferenceObject", INI::parseAsciiString, NULL, offsetof( OCLSpecialPowerModuleData, m_referenceThingName ) },
		{ "OCLAdjustPositionToPassable", INI::parseBool, NULL, offsetof( OCLSpecialPowerModuleData, m_isOCLAdjustPositionToPassable ) },
		{ "AirliftUnit", INI::parseAsciiStringVectorAppend, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftUnits ) },
		{ "AirliftPlaneSlots", INI::parseInt, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftPlaneSlots ) },
		{ "AirliftInfantrySlots", INI::parseInt, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftInfantrySlots ) },
		{ "AirliftVehicleSlots", INI::parseInt, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftVehicleSlots ) },
		{ "AirliftMaxPlanes", INI::parseInt, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftMaxPlanes ) },
		{ "AirliftFeePerPlane", INI::parseInt, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftFeePerPlane ) },
		{ "AirliftAllyRadius", INI::parseReal, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftAllyRadius ) },
		{ "AirliftSpacing", INI::parseReal, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftSpacing ) },
		{ "AirliftInfantryChute", INI::parseAsciiString, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftInfantryChute ) },
		{ "AirliftVehicleChute", INI::parseAsciiString, NULL, offsetof( OCLSpecialPowerModuleData, m_airliftVehicleChute ) },
		{ 0, 0, 0, 0 }
	};
	p.add(dataFieldParse);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// MODULE /////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
OCLSpecialPower::OCLSpecialPower( Thing *thing, const ModuleData *moduleData )
							 : SpecialPowerModule( thing, moduleData )
{

} 

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const ObjectCreationList* OCLSpecialPower::findOCL() const
{
	const OCLSpecialPowerModuleData* d = getOCLSpecialPowerModuleData();
	const OCLSpecialPowerModuleData::Upgrades* up = findFirstOwnedEntry(getObject()->getControllingPlayer(), d->m_upgradeOCL);
	return up ? up->m_ocl : d->m_defaultOCL;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
OCLSpecialPower::~OCLSpecialPower( void )
{

}

//-------------------------------------------------------------------------------------------------
/** Execute the power */
//-------------------------------------------------------------------------------------------------
void OCLSpecialPower::doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions )
{
	// the whole refusal, not just disabled: the base call below returns early on a paused power or a
	// Pro Rules refusal, but the OCL further down was created anyway, with no recharge started
	if (isRefused())
		return;

	// sanity
	if( loc == NULL )
		return;

	// an airlift flies only with a load the player picked (doAirliftOrder); a script's or the AI's
	// plain fire would send empty planes and start the countdown for nothing
	if( isAirlift() )
	{
		DEBUG_LOG(( "AIRLIFT: frame %u %s fired with no order, nothing sent\n", TheGameLogic->getFrame(), getPowerName().str() ));
		return;
	}

	// get the module data
	const OCLSpecialPowerModuleData *modData = getOCLSpecialPowerModuleData();

	Coord3D targetCoord = *loc;

	if( modData->m_isOCLAdjustPositionToPassable )
	{
		FindPositionOptions fpOptions;
		fpOptions.flags = FPF_CLEAR_CELLS_ONLY;
		fpOptions.maxRadius = MAX_ADJUST_RADIUS;
		if ( ! ThePartitionManager->findPositionAround(&targetCoord, &fpOptions, &targetCoord) )
    { // if findPosition() fails, then don't monkey with target Coord!
    	targetCoord = *loc;
    }
    

	}

	// call the base class action cause we are *EXTENDING* functionality
	SpecialPowerModule::doSpecialPowerAtLocation( &targetCoord, angle, commandOptions );

	const ObjectCreationList* ocl = findOCL();

	// at what point will the "deliverer" come in
	Coord3D creationCoord;
	switch (modData->m_createLoc)
	{
		case CREATE_AT_EDGE_NEAR_SOURCE:
			creationCoord = TheTerrainLogic->findClosestEdgePoint( getObject()->getPosition() );
			ObjectCreationList::create( ocl, getObject(), &creationCoord, &targetCoord, angle );
			break;
		case CREATE_AT_EDGE_NEAR_TARGET:
			creationCoord = TheTerrainLogic->findClosestEdgePoint(&targetCoord);
			ObjectCreationList::create( ocl, getObject(), &creationCoord, &targetCoord, angle );
			break;
		case CREATE_AT_EDGE_FARTHEST_FROM_TARGET:
			creationCoord = TheTerrainLogic->findFarthestEdgePoint(&targetCoord);
			creationCoord.z += CREATE_ABOVE_LOCATION_HEIGHT;
			ObjectCreationList::create( ocl, getObject(), &creationCoord, &targetCoord, angle );
			break;
		case CREATE_AT_LOCATION:
			// this is the case where the special power stuff originates at the location of the mouse click
			creationCoord = targetCoord;
			ObjectCreationList::create( ocl, getObject(), &creationCoord, &targetCoord, angle );
			break;
		case USE_OWNER_OBJECT:
			creationCoord.set( &targetCoord );
			// (Bool)FALSE picks the createOwner overload; with the Real angle beside it, false became
			// lifetimeFrames = 0 and a new transport was made after all
			ObjectCreationList::create( ocl, getObject(), &creationCoord, &targetCoord, (Bool)FALSE );
			break;
		case CREATE_ABOVE_LOCATION:
			// this is the case where the special power stuff originates above the location of the mouse click
			creationCoord = targetCoord;
			creationCoord.z += CREATE_ABOVE_LOCATION_HEIGHT;
			ObjectCreationList::create( ocl, getObject(), &creationCoord, &targetCoord, angle );
			break;
	}
}  

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void OCLSpecialPower::doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions )
{
	if (getObject()->isDisabled())
		return;

	// convert to a location
	if( !obj )
		return;
	doSpecialPowerAtLocation( obj->getPosition(), INVALID_ANGLE, commandOptions );
}  

// ------------------------------------------------------------------------------------------------
void OCLSpecialPower::doSpecialPower( UnsignedInt commandOptions )
{
	if (isRefused() || isAirlift())
		return;

	Coord3D creationCoord;
	creationCoord.set( getObject()->getPosition() );
	
	// call the base class action cause we are *EXTENDING* functionality
	SpecialPowerModule::doSpecialPowerAtLocation( &creationCoord, INVALID_ANGLE, commandOptions );

	const ObjectCreationList* ocl = findOCL();
	ObjectCreationList::create( ocl, getObject(), &creationCoord, &creationCoord, false );
}

// ------------------------------------------------------------------------------------------------
// Airlift (fork): Turkiye's Ally Aid, and any power that sets AirliftPlaneSlots.  The player picks the
// load in a panel (GameClient/AirliftPanel.cpp), the order comes as MSG_DO_AIRLIFT_ORDER and is
// checked again here from logic state alone.  The planes are the OCL's, one each, flown in a line
// abreast from the map edge nearest the caster; the load is made for the caster, since a transport
// takes only its owner's units, and DeliverPayloadAIUpdate hands each chute to the recipient as it
// leaves the plane.  A plane shot down takes its load with it (TransportContain destroys riders who
// cannot get out), and nothing is refunded.
// ------------------------------------------------------------------------------------------------
OCLSpecialPower *OCLSpecialPower::findAirlift( Object *obj, const SpecialPowerTemplate *power )
{
	if( obj == NULL || power == NULL )
		return NULL;

	static const NameKeyType key_OCLSpecialPower = NAMEKEY( "OCLSpecialPower" );
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		if( (*m)->getModuleNameKey() != key_OCLSpecialPower )
			continue;
		OCLSpecialPower *sp = (OCLSpecialPower *)( *m );
		if( sp->isAirlift() && sp->getSpecialPowerTemplate() == power )
			return sp;
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------
Int OCLSpecialPower::airliftSlotsOf( const ThingTemplate *thing ) const
{
	const OCLSpecialPowerModuleData *d = getOCLSpecialPowerModuleData();
	const Int slots = thing->isKindOf( KINDOF_INFANTRY ) ? d->m_airliftInfantrySlots : d->m_airliftVehicleSlots;
	return max( 1, min( slots, d->m_airliftPlaneSlots ) );
}

// ------------------------------------------------------------------------------------------------
struct AirliftFactorySearch
{
	const ThingTemplate *thing;
	Bool found;
};

/// a finished, working factory whose bar offers the button; a full queue, no parking or too little
/// money for one more do not matter to a plane
static void findAirliftFactory( Object *obj, void *userData )
{
	AirliftFactorySearch *s = (AirliftFactorySearch *)userData;
	if( s->found || obj->isEffectivelyDead() || obj->getProductionUpdateInterface() == NULL )
		return;

	const CanMakeType can = TheBuildAssistant->canMakeUnit( obj, s->thing );
	if( can != CANMAKE_OK && can != CANMAKE_NO_MONEY && can != CANMAKE_QUEUE_FULL && can != CANMAKE_PARKING_PLACES_FULL )
		return;
	if( TheBuildAssistant->offersPlayerBuildButton( obj, s->thing ) )
		s->found = TRUE;
}

Bool OCLSpecialPower::canAirlift( Player *player, const ThingTemplate *thing, Int count ) const
{
	if( player == NULL || thing == NULL || count <= 0 )
		return FALSE;

	const std::vector< AsciiString > &units = getOCLSpecialPowerModuleData()->m_airliftUnits;
	Bool listed = FALSE;
	for( std::vector< AsciiString >::const_iterator it = units.begin(); it != units.end() && !listed; ++it )
		listed = ( thing->getName() == *it );
	if( !listed || !player->canBuildMoreOfType( thing, count ) )
		return FALSE;

	AirliftFactorySearch search;
	search.thing = thing;
	search.found = FALSE;
	player->iterateObjects( findAirliftFactory, &search );
	return search.found;
}

// ------------------------------------------------------------------------------------------------
OCLSpecialPower::AirliftCheck OCLSpecialPower::checkAirlift( Player *player, const AirliftOrder &order, Int *planes, Int *cost,
																														 std::vector< std::vector< const ThingTemplate * > > *loads ) const
{
	const OCLSpecialPowerModuleData *d = getOCLSpecialPowerModuleData();
	*planes = 0;
	*cost = 0;
	if( loads )
		loads->clear();

	// what it costs, and whether it fits at all before anything is packed
	Int slots = 0;
	for( AirliftOrder::const_iterator it = order.begin(); it != order.end(); ++it )
	{
		if( it->thing == NULL || it->count <= 0 )
			return AIRLIFT_EMPTY;
		if( !canAirlift( player, it->thing, it->count ) )
			return AIRLIFT_NOT_BUILDABLE;
		slots += airliftSlotsOf( it->thing ) * it->count;
		if( slots > d->m_airliftPlaneSlots * d->m_airliftMaxPlanes )
			return AIRLIFT_TOO_MANY_PLANES;
		*cost += it->thing->calcCostToBuild( player ) * it->count;
	}
	if( slots == 0 )
		return AIRLIFT_EMPTY;

	// first fit, the vehicles first so the soldiers fill the room they leave, in the order's own order
	std::vector< Int > room;
	std::vector< std::vector< const ThingTemplate * > > packed;
	for( Int pass = 0; pass < 2; ++pass )
	{
		for( AirliftOrder::const_iterator it = order.begin(); it != order.end(); ++it )
		{
			if( it->thing->isKindOf( KINDOF_INFANTRY ) != ( pass == 1 ) )
				continue;
			const Int each = airliftSlotsOf( it->thing );
			for( Int n = 0; n < it->count; ++n )
			{
				size_t plane = 0;
				while( plane < room.size() && room[ plane ] < each )
					++plane;
				if( plane == room.size() )
				{
					room.push_back( d->m_airliftPlaneSlots );
					packed.push_back( std::vector< const ThingTemplate * >() );
				}
				room[ plane ] -= each;
				packed[ plane ].push_back( it->thing );
			}
		}
	}

	*planes = (Int)room.size();
	if( *planes > d->m_airliftMaxPlanes )
		return AIRLIFT_TOO_MANY_PLANES;
	*cost += *planes * d->m_airliftFeePerPlane;
	if( loads )
		loads->swap( packed );
	if( (Int)player->getMoney()->countMoney() < *cost )
		return AIRLIFT_NO_MONEY;
	return AIRLIFT_OK;
}

// ------------------------------------------------------------------------------------------------
Player *OCLSpecialPower::findAirliftRecipient( const Coord3D *loc ) const
{
	Player *caster = getObject()->getControllingPlayer();
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &alive, NULL };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( loc, getOCLSpecialPowerModuleData()->m_airliftAllyRadius,
																																					 FROM_BOUNDINGSPHERE_2D, filters, ITER_SORTED_NEAR_TO_FAR );
	MemoryPoolObjectHolder hold( iter );
	for( Object *other = iter->first(); other; other = iter->next() )
	{
		Player *owner = other->getControllingPlayer();
		if( owner == NULL || owner == caster || !owner->isPlayableSide() || !owner->isPlayerActive() )
			continue;
		if( caster->getRelationship( owner->getDefaultTeam() ) != ALLIES || owner->getRelationship( caster->getDefaultTeam() ) != ALLIES )
			continue;
		return owner;
	}
	return caster;
}

// ------------------------------------------------------------------------------------------------
/** A spot an airlifted unit may land on: inside the map, on clear ground (no cliff, water, structure
	* footprint or anything standing there now), and for a vehicle clear a radius out on all four sides */
static Bool isAirliftDropClear( const Coord3D &pos, Real radius )
{
	const Real EDGE_MARGIN = 30.0f;
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	if( pos.x < extent.lo.x + radius + EDGE_MARGIN || pos.x > extent.hi.x - radius - EDGE_MARGIN ||
			pos.y < extent.lo.y + radius + EDGE_MARGIN || pos.y > extent.hi.y - radius - EDGE_MARGIN )
		return FALSE;

	// a search of one point: minRadius and maxRadius 0 test the centre only, and a fixed start angle
	// keeps it off the logic random stream
	FindPositionOptions fpOptions;
	fpOptions.flags = FPF_CLEAR_CELLS_ONLY;
	fpOptions.startAngle = 0.0f;
	static const Real SIDES[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	for( Int i = 0; i < ( radius > 0.0f ? 5 : 1 ); ++i )
	{
		Coord3D probe = pos, found;
		probe.x += SIDES[ i ][ 0 ] * radius;
		probe.y += SIDES[ i ][ 1 ] * radius;
		probe.z = TheTerrainLogic->getGroundHeight( probe.x, probe.y );
		if( !ThePartitionManager->findPositionAround( &probe, &fpOptions, &found ) )
			return FALSE;
	}
	return TRUE;
}

struct AirliftDrop
{
	Coord3D pos;
	Real radius;
	Bool vehicle;
};

// ------------------------------------------------------------------------------------------------
/** Where one airlifted unit lands: the first clear point on a sunflower spiral round its plane's
	* spot that keeps its distance from every landing picked before it, a vehicle's wide of the other
	* vehicles.  The spiral packs the load tight and even, so the zone grows with the order. */
static Coord3D pickAirliftDrop( const Coord3D &around, const ThingTemplate *tmpl, std::vector< AirliftDrop > &taken )
{
	const Bool vehicle = !tmpl->isKindOf( KINDOF_INFANTRY );
	const Real radius = tmpl->getTemplateGeometryInfo().getBoundingCircleRadius();
	const Real VEHICLE_GAP = 25.0f, GAP = 8.0f, STEP = 8.0f, GOLDEN_ANGLE = 2.3999632f;
	const Int MAX_POINTS = 800;	// out to 8 * sqrt(800), about 226
	for( Int k = 0; k < MAX_POINTS; ++k )
	{
		const Real r = STEP * sqrt( (Real)k );
		const Real angle = fmod( GOLDEN_ANGLE * (Real)k, TWO_PI );
		Coord3D p = around;
		p.x += r * Cos( angle );
		p.y += r * Sin( angle );

		Bool apart = TRUE;
		for( std::vector< AirliftDrop >::const_iterator t = taken.begin(); t != taken.end() && apart; ++t )
		{
			const Real need = radius + t->radius + ( vehicle && t->vehicle ? VEHICLE_GAP : GAP );
			const Real dx = p.x - t->pos.x, dy = p.y - t->pos.y;
			apart = dx * dx + dy * dy >= need * need;
		}
		if( !apart || !isAirliftDropClear( p, vehicle ? radius : 0.0f ) )
			continue;

		p.z = TheTerrainLogic->getGroundHeight( p.x, p.y );
		AirliftDrop drop = { p, radius, vehicle };
		taken.push_back( drop );
		return p;
	}
	// nowhere clear near the spot: the plane's spot itself, as a paradrop lands
	AirliftDrop drop = { around, radius, vehicle };
	taken.push_back( drop );
	return around;
}

// ------------------------------------------------------------------------------------------------
Bool OCLSpecialPower::doAirliftOrder( const Coord3D *loc, const AirliftOrder &order )
{
	const OCLSpecialPowerModuleData *d = getOCLSpecialPowerModuleData();
	Object *me = getObject();
	Player *player = me->getControllingPlayer();
	if( !isAirlift() || isRefused() || me->isDisabled() || loc == NULL || player == NULL )
		return FALSE;

	Int planes = 0, cost = 0;
	std::vector< std::vector< const ThingTemplate * > > loads;
	const AirliftCheck check = checkAirlift( player, order, &planes, &cost, &loads );
	if( check != AIRLIFT_OK )
	{
		static const char *const WHY[] = { "ok", "nothing ordered", "a unit not on the list or not buildable now", "too many planes", "too little money" };
		DEBUG_LOG(( "AIRLIFT: frame %u player %d order refused: %s, %d planes, cost %d, money %u\n", TheGameLogic->getFrame(),
								player->getPlayerIndex(), WHY[ check ], planes, cost, player->getMoney()->countMoney() ));
		return FALSE;
	}

	Coord3D spot = *loc;
	if( d->m_isOCLAdjustPositionToPassable )
	{
		FindPositionOptions fpOptions;
		fpOptions.flags = FPF_CLEAR_CELLS_ONLY;
		fpOptions.maxRadius = MAX_ADJUST_RADIUS;
		if( !ThePartitionManager->findPositionAround( loc, &fpOptions, &spot ) )
			spot = *loc;
	}

	Player *recipient = findAirliftRecipient( loc );
	const UnsignedInt before = player->getMoney()->countMoney();
	player->getMoney()->withdraw( (UnsignedInt)cost );

	// the base call starts the countdown, the sounds and the radar event
	SpecialPowerModule::doSpecialPowerAtLocation( &spot, INVALID_ANGLE, 0 );

	DEBUG_LOG(( "AIRLIFT: frame %u player %d sends %d planes to (%.0f,%.0f) for player %d, cost %d (fee %d), money %u -> %u\n",
							TheGameLogic->getFrame(), player->getPlayerIndex(), planes, spot.x, spot.y, recipient->getPlayerIndex(),
							cost, planes * d->m_airliftFeePerPlane, before, player->getMoney()->countMoney() ));

	// in a line abreast, square to the line from the edge to the spot: 0, then one each side, then two
	const ObjectCreationList *ocl = findOCL();
	const Coord3D edge = TheTerrainLogic->findClosestEdgePoint( me->getPosition() );
	Real dx = spot.x - edge.x, dy = spot.y - edge.y;
	Real length = sqrt( dx * dx + dy * dy );
	if( length < 1.0f )
	{
		dx = 1.0f;
		dy = 0.0f;
		length = 1.0f;
	}
	const Real acrossX = -dy / length, acrossY = dx / length;
	const ThingTemplate *infantryChute = TheThingFactory->findTemplate( d->m_airliftInfantryChute );
	const ThingTemplate *vehicleChute = TheThingFactory->findTemplate( d->m_airliftVehicleChute );
	Team *team = player->getDefaultTeam();
	static const NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY( "DeliverPayloadAIUpdate" );

	std::vector< Coord3D > starts( planes ), targets( planes );
	for( Int i = 0; i < planes; ++i )
	{
		const Real side = (Real)( ( i + 1 ) / 2 ) * ( ( i % 2 ) ? 1.0f : -1.0f ) * d->m_airliftSpacing;
		starts[ i ] = edge;
		targets[ i ] = spot;
		starts[ i ].x += acrossX * side;
		starts[ i ].y += acrossY * side;
		targets[ i ].x += acrossX * side;
		targets[ i ].y += acrossY * side;
	}

	// each unit's own landing spot, the vehicles' first so they get the room: every chute steers onto
	// its own (the OCL leaves ParachuteDirectly off), where one shared spot a plane stacked its two tanks
	std::vector< std::vector< Coord3D > > drops( planes );
	std::vector< AirliftDrop > taken;
	for( Int pass = 0; pass < 2; ++pass )
		for( Int i = 0; i < planes; ++i )
		{
			drops[ i ].resize( loads[ i ].size(), targets[ i ] );
			std::vector< size_t > mine;
			std::vector< Coord3D > picked;
			for( size_t j = 0; j < loads[ i ].size(); ++j )
				if( loads[ i ][ j ]->isKindOf( KINDOF_INFANTRY ) == ( pass == 1 ) )
				{
					mine.push_back( j );
					picked.push_back( pickAirliftDrop( targets[ i ], loads[ i ][ j ], taken ) );
				}
			// the plane drops in load order as it flies, so the first out gets the spot nearest the way
			// in and the last the far side: no chute has to steer back against the flight
			for( size_t a = 1; a < picked.size(); ++a )
				for( size_t b = a; b > 0; --b )
				{
					const Real alongB = picked[ b ].x * dx + picked[ b ].y * dy, alongA = picked[ b - 1 ].x * dx + picked[ b - 1 ].y * dy;
					if( alongB >= alongA )
						break;
					std::swap( picked[ b ], picked[ b - 1 ] );
				}
			for( size_t m = 0; m < mine.size(); ++m )
				drops[ i ][ mine[ m ] ] = picked[ m ];
		}

	for( Int i = 0; i < planes; ++i )
	{
		Coord3D start = starts[ i ], target = targets[ i ];
		Object *plane = ObjectCreationList::create( ocl, me, &start, &target, INVALID_ANGLE );
		ContainModuleInterface *hold = plane ? plane->getContain() : NULL;
		if( hold == NULL )
		{
			DEBUG_LOG(( "AIRLIFT: plane %d was not made, its load is lost\n", i ));
			continue;
		}
		DeliverPayloadAIUpdate *ai = (DeliverPayloadAIUpdate *)plane->findUpdateModule( key_DeliverPayloadAIUpdate );
		if( ai && recipient != player )
			ai->setPayloadRecipient( recipient->getPlayerIndex() );

		Int carried = 0;
		for( std::vector< const ThingTemplate * >::const_iterator it = loads[ i ].begin(); it != loads[ i ].end(); ++it )
		{
			const Coord3D &landing = drops[ i ][ it - loads[ i ].begin() ];
			Object *unit = TheThingFactory->newObject( *it, team );
			unit->setPosition( plane->getPosition() );
			unit->setProducer( plane );
			Object *payload = unit;

			const ThingTemplate *chuteTmpl = (*it)->isKindOf( KINDOF_INFANTRY ) ? infantryChute : vehicleChute;
			if( chuteTmpl )
			{
				Object *chute = TheThingFactory->newObject( chuteTmpl, team );
				chute->setPosition( plane->getPosition() );
				chute->setProducer( plane );
				if( chute->getContain() && chute->getContain()->isValidContainerFor( unit, true ) )
				{
					chute->getContain()->addToContain( unit );
					chute->getContain()->setOverrideDestination( &landing );
					payload = chute;
				}
				else
					TheGameLogic->destroyObject( chute );
			}

			if( hold->isValidContainerFor( payload, true ) )
			{
				hold->addToContain( payload );
				++carried;
			}
			else
			{
				DEBUG_LOG(( "AIRLIFT: plane %u cannot carry '%s'\n", plane->getID(), (*it)->getName().str() ));
				if( payload != unit )
					TheGameLogic->destroyObject( payload );
				TheGameLogic->destroyObject( unit );
			}
		}
		DEBUG_LOG(( "AIRLIFT: plane %d id %u carries %d units\n", i, plane->getID(), carried ));
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
const ThingTemplate* OCLSpecialPower::getReferenceThingTemplate() const
{
	return TheThingFactory->findTemplate( getOCLSpecialPowerModuleData()->m_referenceThingName );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void OCLSpecialPower::crc( Xfer *xfer )
{

	// extend base class
	SpecialPowerModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void OCLSpecialPower::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SpecialPowerModule::xfer( xfer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void OCLSpecialPower::loadPostProcess( void )
{

	// extend base class
	SpecialPowerModule::loadPostProcess();

}  // end loadPostProcess

