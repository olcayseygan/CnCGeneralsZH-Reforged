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

// FILE: Radar.cpp ////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, January 2002
// Desc:   Radar logic implementation
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameAudio.h"
#include "Common/LogicThread.h"
#include "Common/GameState.h"
#include "Common/MiscAudio.h"
#include "Common/Radar.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/GlobalData.h"
#include "Common/Xfer.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/StealthUpdate.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// GLOBALS ////////////////////////////////////////////////////////////////////////////////////////
Radar *TheRadar = NULL;  ///< the radar global singleton

// PRIVATE ////////////////////////////////////////////////////////////////////////////////////////
#define RADAR_QUEUE_TERRAIN_REFRESH_DELAY (LOGICFRAMES_PER_SECOND * 3.0f)

//-------------------------------------------------------------------------------------------------
/** Delete list resources used by the radar and return them to the memory pools */
//-------------------------------------------------------------------------------------------------
void Radar::deleteListResources( void )
{
	RadarObject *nextObject;

	// delete entries from the local object list
	while( m_localObjectList )
	{

		// get next object
		nextObject = m_localObjectList->friend_getNext();

		// remove radar data from object; none when a tick's remove was still queued (removeObject)
		if( m_localObjectList->friend_getObject() )
			m_localObjectList->friend_getObject()->friend_setRadarData( NULL );

		// delete the head of the list
		m_localObjectList->deleteInstance();

		// set head of the list to the next object
		m_localObjectList = nextObject;

	}  // end while

	// delete entries from the regular object list
	while( m_objectList )
	{

		// get next object
		nextObject = m_objectList->friend_getNext();

		// remove radar data from object; none when a tick's remove was still queued (removeObject)
		if( m_objectList->friend_getObject() )
			m_objectList->friend_getObject()->friend_setRadarData( NULL );

		// delete the head of the list
		m_objectList->deleteInstance();

		// set head of the list to the next object
		m_objectList = nextObject;

	}  // end while

	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{

		DEBUG_ASSERTCRASH( obj->friend_getRadarData() == NULL, ("oops") );

	}

}  // end deleteListResources

// PUBLIC METHODS /////////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
RadarObject::RadarObject( void )
{

	m_object = NULL;
	m_next = NULL;
	m_color = GameMakeColor( 255, 255, 255, 255 );
	m_priority = 0;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
RadarObject::~RadarObject( void )
{

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool RadarObject::isTemporarilyHidden() const
{
	Drawable* draw = m_object->getDrawable();
	if (draw->getStealthLook() == STEALTHLOOK_INVISIBLE || draw->isDrawableEffectivelyHidden())
		return true;

	return false;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void RadarObject::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void RadarObject::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// object id
	ObjectID objectID = m_object ? m_object->getID() : INVALID_ID;
	xfer->xferObjectID( &objectID );
	if( xfer->getXferMode() == XFER_LOAD )
	{

		// find the object and save
		m_object = TheGameLogic->findObjectByID( objectID );
		if( m_object == NULL )
		{

			DEBUG_CRASH(( "RadarObject::xfer - Unable to find object for radar data\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// tell the object we now have some radar data
		m_object->friend_setRadarData( this );
		m_priority = m_object->getRadarPriority();

	}  // end if

	// color
	xfer->xferColor( &m_color );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void RadarObject::loadPostProcess( void )
{

}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Radar::Radar( void )
{

	m_radarWindow = NULL;
	m_objectList = NULL;
	m_localObjectList = NULL;
	m_radarHidden = false;
	m_radarForceOn = false;
	m_simpleColors = false;
	m_terrainAverageZ = 0.0f;
	m_waterAverageZ = 0.0f;
	m_xSample = 0.0f;
	m_ySample = 0.0f;
	m_mapExtent.lo.x = 0.0f;
	m_mapExtent.lo.y = 0.0f;
	m_mapExtent.lo.z = 0.0f;
	m_mapExtent.hi.x = 0.0f;
	m_mapExtent.hi.y = 0.0f;
	m_mapExtent.hi.z = 0.0f;
	m_queueTerrainRefreshFrame = 0;
	m_zoomView.reset();
	m_zoomStep = 0;
	m_zoomFollow.x = m_zoomFollow.y = -1.0e9f;

	// clear the radar events
	clearAllEvents();

}  // end Radar

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Radar::~Radar( void )
{

	// delete list resources
	deleteListResources();

}  // end ~Radar

//-------------------------------------------------------------------------------------------------
/** Clear all radar events */
//-------------------------------------------------------------------------------------------------
void Radar::clearAllEvents( void )
{

	// set next free index to the first one
	m_nextFreeRadarEvent = 0;
	m_lastRadarEvent = -1;

	// zero out all data
	for( Int i = 0; i < MAX_RADAR_EVENTS; ++i )
	{

		m_event[ i ].type = RADAR_EVENT_INVALID;
		m_event[ i ].active = FALSE;
		m_event[ i ].createFrame = 0;
		m_event[ i ].dieFrame = 0;
		m_event[ i ].fadeFrame = 0;
		m_event[ i ].color1.red = 0;
		m_event[ i ].color1.green = 0;
		m_event[ i ].color1.blue = 0;
		m_event[ i ].color2.red = 0;
		m_event[ i ].color2.green = 0;
		m_event[ i ].color2.blue = 0;
		m_event[ i ].worldLoc.x = 0.0f;
		m_event[ i ].worldLoc.y = 0.0f;
		m_event[ i ].worldLoc.z = 0.0f;
		m_event[ i ].radarLoc.x = 0;
		m_event[ i ].radarLoc.y = 0;
		m_event[ i ].soundPlayed = FALSE;
		m_event[ i ].mark = RADAR_MARK_UNIT;

	}  // end for i

	// the spacebar starts again from the last event
	m_cycleCount = 0;
	m_cycleAnchor = -1;
	m_cycleFrame = 0;

}  // end clearAllEvents

//-------------------------------------------------------------------------------------------------
/** Reset radar data */
//-------------------------------------------------------------------------------------------------
void Radar::reset( void )
{

	// delete list resources
	deleteListResources();

	// clear all events
	clearAllEvents();

	// stop forcing the radar on
	m_radarForceOn = false;

	// a new match opens on the whole map
	m_zoomView.reset();
	m_zoomStep = 0;
	m_zoomFollow.x = m_zoomFollow.y = -1.0e9f;

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Radar per frame update */
//-------------------------------------------------------------------------------------------------
void Radar::update( void )
{
	Int i;
	UnsignedInt thisFrame = TheGameLogic->getFrame();

	//
	// traverse the radar event list, if an event has a creationFrame it means that it
	// exists ... check to see if it's time for that event to die and if so, just clear
	// out the active status
	//
	for( i = 0; i < MAX_RADAR_EVENTS; i++ )
	{

		if( m_event[ i ].active == TRUE && m_event[ i ].createFrame && 
				thisFrame > m_event[ i ].dieFrame )
			m_event[ i ].active = FALSE;
				
	}  // end for i

	// see if we should refresh the terrain
	if( m_queueTerrainRefreshFrame != 0 && 
			TheGameLogic->getFrame() - m_queueTerrainRefreshFrame > RADAR_QUEUE_TERRAIN_REFRESH_DELAY )
	{

		// refresh the terrain
		refreshTerrain( TheTerrainLogic );

	}  // end if

	// A zoomed minimap follows the camera, but only once the camera moves: a wheel notch away from
	// it keeps what it zoomed to until then.  The look point is the client's camera; logic never
	// reads any of this.
	if( TheTacticalView && m_mapExtent.width() > 0.0f && m_mapExtent.height() > 0.0f )
	{
		Coord3D look;
		TheTacticalView->getPosition( &look );
		if( look.x != m_zoomFollow.x || look.y != m_zoomFollow.y )
		{
			m_zoomFollow.x = look.x;
			m_zoomFollow.y = look.y;
			m_zoomView.follow( look.x, look.y, m_mapExtent.width(), m_mapExtent.height() );
		}
	}

}  // end update

//-------------------------------------------------------------------------------------------------
/** Reset the radar for the new map data being given to it */
//-------------------------------------------------------------------------------------------------
void Radar::newMap( TerrainLogic *terrain )
{

	// keep a pointer for our radar window
	Int id = NAMEKEY( "ControlBar.wnd:LeftHUD" );
	m_radarWindow = TheWindowManager->winGetWindowFromId( NULL, id );
	DEBUG_ASSERTCRASH( m_radarWindow, ("Radar::newMap - Unable to find radar game window\n") );

	// reset all the data in the radar
	reset();

	// get the extents of the new map
	terrain->getExtent( &m_mapExtent );

	// we will sample at these intervals across the map
	m_xSample = m_mapExtent.width() / RADAR_CELL_WIDTH;
	m_ySample = m_mapExtent.height() / RADAR_CELL_HEIGHT;

	// find the "middle" height for the terrain (most used value) and water table
	Int x, y;
	Int terrainSamples = 0, waterSamples = 0;

	m_terrainAverageZ = 0.0f;
	m_waterAverageZ = 0.0f;
	Coord3D worldPoint;
  
  // since we're averaging let's skip every second sample...
  worldPoint.y=0;
	for( y = 0; y < RADAR_CELL_HEIGHT; y+=2, worldPoint.y+=2.0*m_ySample )
  {
    worldPoint.x=0;
    for( x = 0; x < RADAR_CELL_WIDTH; x+=2, worldPoint.x+=2.0*m_xSample )
		{
			// don't use this, we don't really need the 
      // Z position by this function... radarToWorld( &radarPoint, &worldPoint );
			// and this is done by isUnderwater anyway: z = terrain->getGroundHeight( worldPoint.x, worldPoint.y );
			Real z,waterZ;
			if( terrain->isUnderwater( worldPoint.x, worldPoint.y, &waterZ, &z ) )
			{
				m_waterAverageZ += z;
				waterSamples++;
			}
			else
			{
				m_terrainAverageZ += z;
				terrainSamples++;
			}

		}  // end for x
  }

	// avoid divide by zeros
	if( terrainSamples == 0 )
		terrainSamples = 1;
	if( waterSamples == 0 )
		waterSamples = 1;

	// compute averages
	m_terrainAverageZ = m_terrainAverageZ / INT_TO_REAL( terrainSamples );
	m_waterAverageZ = m_waterAverageZ / INT_TO_REAL( waterSamples );

}  // end newMap

//-------------------------------------------------------------------------------------------------
/** Add an object to the radar list.  The object will be sorted in the list to be grouped
	* using it's radar priority */
//-------------------------------------------------------------------------------------------------
void Radar::addObject( Object *obj )
{
	/* LOGIC-THREAD.md phase 2, M6.  The tick's adds and removes wait for its end, in order.  By ID,
		 since the tick may destroy the object first; then it never goes on, and a remove queued after
		 the add finds it on and takes it off again. */
	if( LogicThread::defersEffects() )
	{
		const ObjectID id = obj->getID();
		LogicThread::callAfterTick( [this, id]() {
			Object *live = TheGameLogic->findObjectByID( id );
			if( live && live->friend_getRadarData() == NULL )
				addObject( live );
		} );
		return;
	}
	LOGIC_CENSUS( CENSUS_RADAR );

	// get the radar priority for this object
	RadarPriorityType newPriority = obj->getRadarPriority();
	if( isPriorityVisible( newPriority ) == FALSE )
		return;

	// if this object is on the radar, remove it in favor of the new add
	RadarObject **list;
	RadarObject *newObj;

	// sanity
	DEBUG_ASSERTCRASH( obj->friend_getRadarData() == NULL,
										 ("Radar: addObject - non NULL radar data for '%s'\n", 
										 obj->getTemplate()->getName().str()) );

	// allocate a new object
	newObj = newInstance(RadarObject);

	// set the object data
	newObj->friend_setObject( obj );

	// set color for this object on the radar
	const Player *player = obj->getControllingPlayer();
	Bool useIndicatorColor = true;

	if( obj->isKindOf( KINDOF_DISGUISER ) )
	{
		//Because we have support for disguised units pretending to be units from another
		//team, we need to intercept it here and make sure it's rendered appropriately
		//based on which client is rendering it.
    StealthUpdate *update = obj->getStealth();
		if( update )
		{
			if( update->isDisguised() )
			{
				Player *clientPlayer = ThePlayerList->getLocalPlayer();
				Player *disguisedPlayer = ThePlayerList->getNthPlayer( update->getDisguisedPlayerIndex() );
				if( player->getRelationship( clientPlayer->getDefaultTeam() ) != ALLIES && clientPlayer->isPlayerActive() )
				{
					//Neutrals and enemies will see this disguised unit as the team it's disguised as.
					player = disguisedPlayer;
					if( player )
						useIndicatorColor = false;
				}
				//Otherwise, the color will show up as the team it really belongs to (already set above).
			}
		}
	}
	
	if( obj->getContain() )
	{
		// To handle Stealth garrison, ask containers what color they are drawing with to the local player.
		// Local is okay because radar display is not synced.
		player = obj->getContain()->getApparentControllingPlayer( ThePlayerList->getLocalPlayer() );
		if( player )
			useIndicatorColor = false;
	}

	// the colour the match agreed on, not the one this screen draws.  A blip is created once and
	// read every frame after that, so the scheme is applied where it is drawn: otherwise a unit
	// built before the setting was changed keeps the old colour until it dies.
	if( useIndicatorColor || (player == NULL) )
	{
		newObj->setColor( obj->getIndicatorColor() );
	}
	else
	{
		newObj->setColor( player->getPlayerColor() );
	}

	// set a chunk of radar data in the object
	obj->friend_setRadarData( newObj );
	newObj->friend_setPriority( newPriority );

	//
	// we will put this on either the local object list for objects that belong to the
	// local player, or on the regular object list for all other objects
	//
	if( obj->isLocallyControlled() )
		list = &m_localObjectList;
	else
		list = &m_objectList;

	// link object to master list at the head of it's priority section
	if( *list == NULL )
		*list = newObj;  // trivial case, an empty list
	else
	{
		RadarPriorityType prevPriority, currPriority;
		RadarObject *currObject, *prevObject, *nextObject;

		prevObject = NULL;
		prevPriority = RADAR_PRIORITY_INVALID;
		for( currObject = *list; currObject; currObject = nextObject )
		{

			// get the next object
			nextObject = currObject->friend_getNext();

			// get the priority of this entry in the list (currPriority)
			currPriority = (RadarPriorityType)currObject->friend_getPriority();

			//
			// if there is no previous object, or the previous priority is less than the
			// our new priority, and the current object in the list has a priority 
			// higher than our equal to our own we need to be inserted here
			//
			if( (prevObject == NULL || prevPriority < newPriority ) &&
					(currPriority >= newPriority) )
			{

				// insert into the list just ahead of currObject
				if( prevObject )
				{

					// the new entry next points to what the previous one used to point to
					newObj->friend_setNext( prevObject->friend_getNext() );

					// the previous one next now points to the new entry
					prevObject->friend_setNext( newObj );

				}  // end if
				else
				{

					// the new object next points to the current object
					newObj->friend_setNext( currObject );

					// new list head is now newObj
					*list = newObj;

				}  // end else

				break;  // exit for, stop the insert

			}  // end if
			else if( nextObject == NULL )
			{

				// at the end of the list, put object here
				currObject->friend_setNext( newObj );

			}  // end else if

			// our current object is now the previous object
			prevObject = currObject;
			prevPriority = currPriority;

		}  // end if

	}  // end else

}  // end addObject

//-------------------------------------------------------------------------------------------------
/** Try to delete an object from a specific list */
//-------------------------------------------------------------------------------------------------
Bool Radar::deleteFromList( Object *obj, RadarObject **list )
{
	RadarObject *radarObject, *prevObject = NULL;
					
	// find the object in list
	for( radarObject = *list; radarObject; radarObject = radarObject->friend_getNext() )
	{
		
		if( radarObject->friend_getObject() == obj )
		{

			// unlink the object from list
			if( prevObject == NULL )
				*list = radarObject->friend_getNext();  // removing head of list
			else
				prevObject->friend_setNext( radarObject->friend_getNext() );

			// set the object radar data to NULL
			obj->friend_setRadarData( NULL );

			// delete the object instance
			radarObject->deleteInstance();

			// all done, object found and deleted
			return TRUE;

		}  // end if

		// save this object as previous one encountered in the list
		prevObject = radarObject;

	}  // end for, radarObject

	// object was not found in this list
	return FALSE;

}  // end deleteFromList

//-------------------------------------------------------------------------------------------------
/** Take `entry` off `list` by its own address, never reading its object. TRUE if it was there. */
//-------------------------------------------------------------------------------------------------
Bool Radar::unlinkEntry( RadarObject *entry, RadarObject **list )
{
	RadarObject *prev = NULL;
	for( RadarObject *at = *list; at; prev = at, at = at->friend_getNext() )
	{
		if( at != entry )
			continue;
		if( prev == NULL )
			*list = at->friend_getNext();
		else
			prev->friend_setNext( at->friend_getNext() );
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Remove an object from the radar, the object may reside in any list */
//-------------------------------------------------------------------------------------------------
void Radar::removeObject( Object *obj )
{
	if( LogicThread::defersEffects() )
	{
		// an entry the object has is taken from it now and off the list after the tick, without a look
		// at the object, which the tick may free; one it has not got yet may be an add still queued
		RadarObject *entry = obj->friend_getRadarData();
		if( entry )
		{
			// a list torn down before the flush (the game cleared) then frees the entry without its object
			obj->friend_setRadarData( NULL );
			entry->friend_setObject( NULL );
			LogicThread::callAfterTick( [this, entry]() {
				if( !unlinkEntry( entry, &m_localObjectList ) )
					unlinkEntry( entry, &m_objectList );
				entry->deleteInstance();
			} );
		}
		else
		{
			const ObjectID id = obj->getID();
			LogicThread::callAfterTick( [this, id]() {
				Object *live = TheGameLogic->findObjectByID( id );
				if( live && live->friend_getRadarData() )
					removeObject( live );
			} );
		}
		return;
	}
	LOGIC_CENSUS( CENSUS_RADAR );

	// sanity
	if( obj->friend_getRadarData() == NULL )
		return;

	if( deleteFromList( obj, &m_localObjectList ) == TRUE )
		return;
	else if( deleteFromList( obj, &m_objectList ) == TRUE )
		return;
	else
	{

		// sanity
		DEBUG_ASSERTCRASH( 0, ("Radar: Tried to remove object '%s' which was not found\n",
											 obj->getTemplate()->getName().str()) );

	}  // end else

}  // end removeObject

//-------------------------------------------------------------------------------------------------
/** Translate a 2D spot on the radar (from (0,0) to (RADAR_CELL_WIDTH,RADAR_CELL_HEIGHT)
	* to a 3D spot in the world. Does not determine Z value!
	* Return TRUE if the radar points translates to a valid world position
	* Return FALSE if the radar point is not a valid world position */
//-------------------------------------------------------------------------------------------------		
Bool Radar::radarToWorld2D( const ICoord2D *radar, Coord3D *world )
{
	Int x, y;

	// sanity
	if( radar == NULL || world == NULL )
		return FALSE;

	// get the coords
	x = radar->x;
	y = radar->y;

	// more sanity
	if( x < 0 )
		x = 0;
	if( x >= RADAR_CELL_WIDTH )
		x = RADAR_CELL_WIDTH - 1;
	if( y < 0 )
		y = 0;
	if( y >= RADAR_CELL_HEIGHT )
		y = RADAR_CELL_HEIGHT - 1;

	// translate to world
	world->x = x * m_xSample;
	world->y = y * m_ySample;
  return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Translate a 2D spot on the radar (from (0,0) to (RADAR_CELL_WIDTH,RADAR_CELL_HEIGHT)
	* to a 3D spot in the world on the terrain
	* Return TRUE if the radar points translates to a valid world position
	* Return FALSE if the radar point is not a valid world position */
//-------------------------------------------------------------------------------------------------		
Bool Radar::radarToWorld( const ICoord2D *radar, Coord3D *world )
{
  if (!radarToWorld2D(radar,world))
    return FALSE;

	// find the terrain height here
	world->z = TheTerrainLogic->getGroundHeight( world->x, world->y );
	
	return TRUE;  // valid translation

}  // end radarToWorld

//-------------------------------------------------------------------------------------------------
/** Translate a point in the world to the 2D radar (x,y)
	* Return TRUE if the world point successfully translates to a radar point
	* Return FALSE if world point is a bogus off the map position */
//-------------------------------------------------------------------------------------------------
Bool Radar::worldToRadar( const Coord3D *world, ICoord2D *radar )
{

	// sanity
	if( world == NULL || radar == NULL )
		return FALSE;

	// sanity check the world position
//	if( world->x < m_mapExtent.lo.x || world->x > m_mapExtent.hi.x ||
//			world->y < m_mapExtent.lo.y || world->y > m_mapExtent.hi.y )
//		return FALSE;
	// This is actually an insanity check.  Nobody uses the return value, so this just leaves garbage in the 
	// return pointer.  The reason the question gets asked is there are 60 partition cells to 128 radar cells
	// (for example), and the radar wants to draw a horizontal line.  This line ends up three pixels long
	// at the right side, so the radar gives up and doesn't draw the middle one.
	// We bind to on radar anyway, and we only ask if we are on radar, so don't intentionally add edge weirdness.

	// convert
	radar->x = world->x / m_xSample;
	radar->y = world->y / m_ySample;

	// keep it in bounds
	if( radar->x < 0 )
		radar->x = 0;
	if( radar->x >= RADAR_CELL_WIDTH )
		radar->x = RADAR_CELL_WIDTH - 1;
	if( radar->y < 0 )
		radar->y = 0;
	if( radar->y >= RADAR_CELL_HEIGHT )
		radar->y = RADAR_CELL_HEIGHT - 1;

	return TRUE;  // valid translation

}  // end worldToRadar

// ------------------------------------------------------------------------------------------------
/** Translate an actual pixel location (relative pixel with (0,0) being the top left of
	* the radar area) to the "logical" radar coords that would cover the entire area of display
	* on the screen.  This is needed because some maps are "long" or "tall" and need a translation
	* to any radar image that has been scaled to preserve the map aspect ratio */
// ------------------------------------------------------------------------------------------------
Bool Radar::localPixelToRadar( const ICoord2D *pixel, ICoord2D *radar )
{

	// sanity
	if( pixel == NULL || radar == NULL )
		return FALSE;

	// if we have no radar window can't do the conversion (m_radarWindow is NULL between maps and
	// in the shell; the sibling screenPixelToWorld below has always checked this, this one did not)
	if( m_radarWindow == NULL )
		return FALSE;

	// get window size of the radar
	ICoord2D size;
	m_radarWindow->winGetSize( &size.x, &size.y );

	//
	// act like we're going to draw and find the aspect ratio adjusted points of the 
	// terrain radar positions
	//
	ICoord2D start = { 0, 0 };
	ICoord2D ul, lr;
	findDrawPositions( start.x, start.y, size.x, size.y, &ul, &lr );

	// get the scaled width and height
	Int scaledWidth = lr.x - ul.x;
	Int scaledHeight = lr.y - ul.y;

	// if the pixel is outsize of the adjusted radar area there are no logical coords.
	// the upper bound is exclusive: at pixel->x == lr.x the conversion below yields exactly
	// RADAR_CELL_WIDTH, one cell past the grid, and the caller maps it outside the map.
	if( pixel->x < ul.x || pixel->x >= lr.x ||
			pixel->y < ul.y || pixel->y >= lr.y )
		return FALSE;

	if( scaledWidth >= scaledHeight )
	{

		// just normal conversion from full stretched to radar cells
		radar->x = (pixel->x - ul.x)* RADAR_CELL_WIDTH / scaledWidth;

		// conversion for scaled Y direction in map
		radar->y = REAL_TO_INT( ((pixel->y - ul.y) / INT_TO_REAL( scaledHeight )) * size.y );
		
		//
		// radar->y now refers to a point that was "as if" the map was square, translate to radar
		// note that y is inverted to have the radar align with the world (+x = right, -y = down)
		//
		radar->y = (size.y - radar->y) * RADAR_CELL_HEIGHT / size.y;


	}  // end if
	else
	{

		// conversion for scaled Y direction in map
		radar->x = REAL_TO_INT( ((pixel->x - ul.x) / INT_TO_REAL( scaledWidth )) * size.x );
		
		// radar->x now refers to a point that was "as if" the map was square, translate to radar
		radar->x = radar->x * RADAR_CELL_WIDTH / size.x;

		//
		// just normal conversion from full stretched to radar cells, note that y is inverted
		// to have the radar align with the world (+x = right, -y = down)
		//
		radar->y = (size.y - pixel->y) * RADAR_CELL_HEIGHT / size.y;

	}  // end else

	return TRUE;

}  // end localPixelToRadar

// ------------------------------------------------------------------------------------------------
/** The map's picture in the radar window's own pixels, where W3DLeftHUDDraw draws it: a pixel
	* inside the window each side.  FALSE with no window or no map. */
// ------------------------------------------------------------------------------------------------
Bool Radar::findPicture( ICoord2D *ul, ICoord2D *lr )
{
	if( m_radarWindow == NULL || m_mapExtent.width() <= 0.0f || m_mapExtent.height() <= 0.0f )
		return FALSE;

	ICoord2D size;
	m_radarWindow->winGetSize( &size.x, &size.y );
	findDrawPositions( 1, 1, size.x - 2, size.y - 2, ul, lr );
	return lr->x > ul->x && lr->y > ul->y;
}

// ------------------------------------------------------------------------------------------------
/** A pixel of the radar window (upper left (0,0)) to the spot on the terrain the minimap draws
	* there, zoom and all.  FALSE off the map's picture, by the same bounds localPixelToRadar has
	* always used; the spot itself comes from the picture draw lays out, so a click lands on what is
	* under it at any zoom, to the pixel. */
// ------------------------------------------------------------------------------------------------
Bool Radar::localPixelToWorld( const ICoord2D *pixel, Coord3D *world )
{
	ICoord2D radar, ul, lr;
	if( pixel == NULL || world == NULL || localPixelToRadar( pixel, &radar ) == FALSE || findPicture( &ul, &lr ) == FALSE )
		return FALSE;

	RadarPictureTransform transform;
	transform.set( m_zoomView, m_mapExtent.width(), m_mapExtent.height(), ul, lr );
	transform.toWorld( *pixel, &world->x, &world->y );
	world->x = max( 0.0f, min( world->x, m_mapExtent.width() ) );
	world->y = max( 0.0f, min( world->y, m_mapExtent.height() ) );
	world->z = TheTerrainLogic->getGroundHeight( world->x, world->y );
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** A wheel notch over a pixel of the radar window: one step closer or further, the spot under the
	* pixel staying under it.  A pixel on the black bands zooms about the nearest edge of the map. */
// ------------------------------------------------------------------------------------------------
void Radar::zoomAtPixel( const ICoord2D *pixel, Bool closer )
{
	static const Real STEPS[ RADAR_ZOOM_STEPS ] = { 1.0f, 1.41421356f, 2.0f, 2.82842712f, 4.0f };

	ICoord2D ul, lr;
	if( pixel == NULL || findPicture( &ul, &lr ) == FALSE )
		return;

	const Int step = max( 0, min( (Int)RADAR_ZOOM_STEPS - 1, m_zoomStep + (closer ? 1 : -1) ) );
	if( step == m_zoomStep )
		return;

	ICoord2D at;
	at.x = max( ul.x, min( pixel->x, lr.x - 1 ) );
	at.y = max( ul.y, min( pixel->y, lr.y - 1 ) );
	RadarPictureTransform transform;
	transform.set( m_zoomView, m_mapExtent.width(), m_mapExtent.height(), ul, lr );
	Real x, y;
	transform.toWorld( at, &x, &y );

	m_zoomStep = step;
	m_zoomView.zoomAround( STEPS[ step ], x, y, m_mapExtent.width(), m_mapExtent.height() );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RadarZoomView::clampTo( Real mapWidth, Real mapHeight )
{
	zoom = max( 1.0f, min( zoom, 4.0f ) );
	x = max( 0.0f, min( x, mapWidth - mapWidth / zoom ) );
	y = max( 0.0f, min( y, mapHeight - mapHeight / zoom ) );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RadarZoomView::zoomAround( Real newZoom, Real anchorX, Real anchorY, Real mapWidth, Real mapHeight )
{
	newZoom = max( 1.0f, min( newZoom, 4.0f ) );
	// the anchor's share of the way across the shown part stays what it was
	x = anchorX - (anchorX - x) * zoom / newZoom;
	y = anchorY - (anchorY - y) * zoom / newZoom;
	zoom = newZoom;
	clampTo( mapWidth, mapHeight );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RadarZoomView::follow( Real pointX, Real pointY, Real mapWidth, Real mapHeight )
{
	const Real spanX = mapWidth / zoom;
	const Real spanY = mapHeight / zoom;
	const Real marginX = spanX * 0.2f;
	const Real marginY = spanY * 0.2f;
	if( pointX < x + marginX )
		x = pointX - marginX;
	else if( pointX > x + spanX - marginX )
		x = pointX - spanX + marginX;
	if( pointY < y + marginY )
		y = pointY - marginY;
	else if( pointY > y + spanY - marginY )
		y = pointY - spanY + marginY;
	clampTo( mapWidth, mapHeight );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RadarPictureTransform::set( const RadarZoomView &view, Real mapWidth, Real mapHeight, const ICoord2D &ul, const ICoord2D &lr )
{
	scaleX = mapWidth > 0.0f ? (lr.x - ul.x) * view.zoom / mapWidth : 0.0f;
	scaleY = mapHeight > 0.0f ? (lr.y - ul.y) * view.zoom / mapHeight : 0.0f;
	originX = ul.x - view.x * scaleX;
	originY = lr.y + view.y * scaleY;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RadarPictureTransform::toWorld( const ICoord2D &pixel, Real *worldX, Real *worldY ) const
{
	*worldX = scaleX > 0.0f ? (pixel.x + 0.5f - originX) / scaleX : 0.0f;
	*worldY = scaleY > 0.0f ? (originY - pixel.y - 0.5f) / scaleY : 0.0f;
}

// ------------------------------------------------------------------------------------------------
/** Translate a screen mouse position to world coords if the screen position is within
	* the radar window and that spot in the radar corresponds to a point in the world */
// ------------------------------------------------------------------------------------------------
Bool Radar::screenPixelToWorld( const ICoord2D *pixel, Coord3D *world )
{

	// sanity
	if( pixel == NULL || world == NULL )
		return FALSE;

	// if we have no radar window can't do the conversion
	if( m_radarWindow == NULL )
		return FALSE;

	// translate pixel coords to local pixel coords relative to the radar window
	ICoord2D localPixel;
	ICoord2D screenPos;
	m_radarWindow->winGetScreenPosition( &screenPos.x, &screenPos.y );
	localPixel.x = pixel->x - screenPos.x;
	localPixel.y = pixel->y - screenPos.y;

	return localPixelToWorld( &localPixel, world );

}  // end screenPixelToWorld

// ------------------------------------------------------------------------------------------------
/** Given the pixel coordinates, see if there is an object that is exactly in this
	* spot represented on the radar */
// ------------------------------------------------------------------------------------------------
Object *Radar::objectUnderRadarPixel( const ICoord2D *pixel )
{

	// sanity
	if( pixel == NULL )
		return NULL;

	// convert pixel location to radar logical radar location, through the zoom
	ICoord2D radar;
	Coord3D world;
	if( localPixelToWorld( pixel, &world ) == FALSE || worldToRadar( &world, &radar ) == FALSE )
		return NULL;

	// object we will return
	Object *obj = NULL;

	//
	// scan the objects on the radar list and return any object that maps its world location
	// to the radar location
	//
	
	// search the local object list
	obj = searchListForRadarLocationMatch( m_localObjectList, &radar );

	// search all other objects if not found
	if( obj == NULL )
		obj = searchListForRadarLocationMatch( m_objectList, &radar );

	// return the object found (if any)
	return obj;

}  // end objectUnderRadarPixel

// ------------------------------------------------------------------------------------------------
/** Search the object list for an object that maps to the given logical radar coords */
// ------------------------------------------------------------------------------------------------
Object *Radar::searchListForRadarLocationMatch( RadarObject *listHead, ICoord2D *radarMatch )
{

	// sanity
	if( listHead == NULL || radarMatch == NULL )
		return NULL;

	// scan the list
	RadarObject *radarObject;
	ICoord2D radar;
	for( radarObject = listHead; radarObject; radarObject = radarObject->friend_getNext() )
	{

		// get object
		Object *obj = radarObject->friend_getObject();
		
		// sanity
		if( obj == NULL )
		{

			DEBUG_CRASH(( "Radar::searchListForRadarLocationMatch - NULL object encountered in list\n" ));
			continue;

		}  // end if

		// convert object position to logical radar
		worldToRadar( obj->getPosition(), &radar );

		// see if this matches our match radar location
		if( radar.x >= radarMatch->x - 1 &&
				radar.x <= radarMatch->x + 1 && 
				radar.y >= radarMatch->y - 1 &&
				radar.y <= radarMatch->y + 1 )
			return obj;

	}  // end for, radarObject

	// no match found
	return NULL;

}  // end searchListForRadarLocationMatch

// ------------------------------------------------------------------------------------------------
/** Given the RELATIVE SCREEN start X and Y, the width and height of the area to draw the whole
	* radar in, compute what the upper left (ul) and lower right (lr) local coordinates are
	* that represent the actual terrain image part of the radar that will preserve the
	* aspect ratio of the map */
// ------------------------------------------------------------------------------------------------
void Radar::findDrawPositions( Int startX, Int startY, Int width, Int height, 
															 ICoord2D *ul, ICoord2D *lr )
{

	Real ratioWidth;
	Real ratioHeight;
	Coord2D radar;
	ratioWidth = m_mapExtent.width()/(width * 1.0f);
	ratioHeight = m_mapExtent.height()/(height* 1.0f);
	
	if( ratioWidth >= ratioHeight)
	{
		radar.x = m_mapExtent.width() / ratioWidth;
		radar.y = m_mapExtent.height()/ ratioWidth;
		ul->x = 0;
		ul->y = (height - radar.y) / 2.0f;
		lr->x = radar.x;
		lr->y = height - ul->y;
	}
	else
	{
		radar.x = m_mapExtent.width() / ratioHeight;
		radar.y = m_mapExtent.height()/ ratioHeight;
		ul->x = (width - radar.x ) / 2.0f;
		ul->y = 0;
		lr->x = width - ul->x;
		lr->y = radar.y;
	}
/*

	if( m_mapExtent.width() > m_mapExtent.height() )
	{

		//
		// +---------------+
		// |               |
		// |               |
		// +---------------+
		// |   map area    |
		// +---------------+
		// |               |
		// |               |
		// +---------------+
		//
		ul->x = 0;
		ul->y = (height - (m_mapExtent.height() / m_mapExtent.width() * height)) / 2.0f;
		lr->x = width;
		lr->y = height - ul->y;

	}  // end if
	else if(  m_mapExtent.height() > m_mapExtent.width() )
	{

		// +-----+---+-----+
		// |     | m |     |
		// |     | a |     |
		// |     | p |     |
		// |     |   |     |
		// |     | a |     |
		// |     | r |     |
		// |     | e |     |
		// |     | a |     |
		// +-----+---+-----+
		//

		ul->x = (width - (m_mapExtent.width() / m_mapExtent.height() * width)) / 2.0f;
		ul->y = 0;
		lr->x = width - ul->x;
		lr->y = height;

	}  // end else
	else
	{

		ul->x = 0;
		ul->y = 0;
		lr->x = width;
		lr->y = height;

	}  // end else
*/

	// make them pixel positions
	ul->x += startX;
	ul->y += startY;
	lr->x += startX;
	lr->y += startY;

}  // end findDrawPositions

//-------------------------------------------------------------------------------------------------
/** Radar color lookup table */
//-------------------------------------------------------------------------------------------------
struct RadarColorLookup
{
	RadarEventType event;
	RGBAColorInt color1;
	RGBAColorInt color2;
};
static RadarColorLookup radarColorLookupTable[] = 
{
	/*      Radar Event													Color 1									 Color 2       */
	{ RADAR_EVENT_CONSTRUCTION,					{ 128, 128, 255, 255 },  {  128, 255, 255, 255 } },
	// the light pink first: the tick is drawn in color 1 on a dark keyline, where EA's dark purple vanished
	{ RADAR_EVENT_UPGRADE,							{ 255, 185, 220, 255 },  {  128,   0,  64, 255 } },
	{ RADAR_EVENT_UNDER_ATTACK,					{ 255,   0,   0, 255 },  {  255, 128, 128, 255 } },
	{ RADAR_EVENT_INFORMATION,					{ 255, 255,   0, 255 },  {  255, 255, 128, 255 } },
	{ RADAR_EVENT_BEACON_PULSE,					{ 255, 255,   0, 255 },  {  255, 255, 128, 255 } },
	{ RADAR_EVENT_INFILTRATION,					{   0, 255, 255, 255 },  {  128, 255, 255, 255 } },
	{ RADAR_EVENT_BATTLE_PLAN,					{ 255, 255, 255, 255 },	 {  255, 255, 255, 255 } },
	{ RADAR_EVENT_STEALTH_DISCOVERED,		{   0, 255,   0, 255 },	 {    0, 128,   0, 255 } },
	{ RADAR_EVENT_STEALTH_NEUTRALIZED,	{   0, 255,   0, 255 },	 {    0, 128,   0, 255 } },
	{ RADAR_EVENT_FAKE,									{   0,	 0,   0,	 0 },	 {    0,	 0,   0,   0 } },
	{ RADAR_EVENT_STRUCTURE_LOST,				{ 255, 112,  32, 255 },	 {  120,   0,   0, 255 } },
	{ RADAR_EVENT_INVALID,							{   0,   0,   0,   0 },  {    0,   0,   0,   0 } }
};

//-------------------------------------------------------------------------------------------------
/** Create a new radar event */
//-------------------------------------------------------------------------------------------------
void Radar::createEvent( const Coord3D *world, RadarEventType type, Real secondsToLive, RadarEventMark mark )
{
	// sanity
	if( world == NULL )
		return;

	if( LogicThread::defersEffects() )
	{
		const Coord3D at = *world;
		LogicThread::callAfterTick( [=]() { createEvent( &at, type, secondsToLive, mark ); } );
		return;
	}
	LOGIC_CENSUS( CENSUS_RADAR );

	// lookup the colors we are to used based on the event 
	RGBAColorInt color[ 2 ];
	// i is read after the loop to tell "found" from "ran off the end", so it
	// outlives the for-scope VC6 gave it.
	Int i;
	for( i = 0; radarColorLookupTable[ i ].event != RADAR_EVENT_INVALID; ++i )
	{

		if( radarColorLookupTable[ i ].event == type )
		{

			color[ 0 ] = radarColorLookupTable[ i ].color1;
			color[ 1 ] = radarColorLookupTable[ i ].color2;
			break;

		}  // end if

	}  // end while

	// check for no match found in color table
	if( radarColorLookupTable[ i ].event == RADAR_EVENT_INVALID )
	{
		static RGBAColorInt color1 = { 255, 255, 255, 255 };
		static RGBAColorInt color2 = { 255, 255, 255, 255 };

		DEBUG_CRASH(( "Radar::createEvent - Event not found in color table, using default colors\n" ));
		color[ 0 ] = color1;
		color[ 1 ] = color2;

	}  // end if

	// call the internal method to create the event with these colors
	internalCreateEvent( world, type, secondsToLive, &color[ 0 ], &color[ 1 ], mark );

}  // end createEvent

// ------------------------------------------------------------------------------------------------
/** Create radar event using a specific colors from the player */
// ------------------------------------------------------------------------------------------------
void Radar::createPlayerEvent( Player *player, const Coord3D *world, 
															 RadarEventType type, Real secondsToLive )
{

	// sanity
	if( player == NULL || world == NULL )
		return;

	if( LogicThread::defersEffects() )
	{
		const Coord3D at = *world;
		LogicThread::callAfterTick( [=]() { createPlayerEvent( player, &at, type, secondsToLive ); } );
		return;
	}
	LOGIC_CENSUS( CENSUS_RADAR );

	// figure out the two colors we should use
	Color c;
	UnsignedByte r, g, b, a;
	RGBAColorInt color[ 2 ];

	// color 1
	c = clientPlayerColor( player );
	GameGetColorComponents( c, &r, &g, &b, &a );
	color[ 0 ].red = r;
	color[ 0 ].green = g;
	color[ 0 ].blue = b;
	color[ 0 ].alpha = a;

	// the second will be a darker color 1
	Real darkScale = 0.75f;
	color[ 1 ] = color[ 0 ];
	color[ 1 ].red -= REAL_TO_INT( color[ 0 ].red * darkScale );
	if( color[ 1 ].red < 0 )
		color[ 1 ].red = 0;
	color[ 1 ].green -= REAL_TO_INT( color[ 0 ].green * darkScale );
	if( color[ 1 ].green < 0 )
		color[ 1 ].green = 0;
	color[ 1 ].blue -= REAL_TO_INT( color[ 0 ].blue * darkScale );
	if( color[ 1 ].blue < 0 )
		color[ 1 ].blue = 0;

	// create the events using these colors
	internalCreateEvent( world, type, secondsToLive, &color[ 0 ], &color[ 1 ] );

}  // end createPlayerEvent

//-------------------------------------------------------------------------------------------------
/** Create a new radar event */
//-------------------------------------------------------------------------------------------------
void Radar::internalCreateEvent( const Coord3D *world, RadarEventType type, Real secondsToLive,
																 const RGBAColorInt *color1, const RGBAColorInt *color2,
																 RadarEventMark mark )
{
	static Real secondsBeforeDieToFade = 0.5f;  ///< this many seconds before we hit the die frame we start to fade away
		
	// sanity
	if( world == NULL || color1 == NULL || color2 == NULL )
		return;

	// translate the world coord to radar coords
	ICoord2D radar;
	worldToRadar( world, &radar );

	// add to the list of radar events
	m_event[ m_nextFreeRadarEvent ].type = type;
	m_event[ m_nextFreeRadarEvent ].active = TRUE;
	m_event[ m_nextFreeRadarEvent ].createFrame = TheGameLogic->getFrame();
	m_event[ m_nextFreeRadarEvent ].dieFrame = TheGameLogic->getFrame() + LOGICFRAMES_PER_SECOND * secondsToLive;
	m_event[ m_nextFreeRadarEvent ].fadeFrame = m_event[ m_nextFreeRadarEvent ].dieFrame - LOGICFRAMES_PER_SECOND * secondsBeforeDieToFade;
	m_event[ m_nextFreeRadarEvent ].color1 = *color1;
	m_event[ m_nextFreeRadarEvent ].color2 = *color2;
	m_event[ m_nextFreeRadarEvent ].worldLoc = *world;
	m_event[ m_nextFreeRadarEvent ].radarLoc = radar;
	m_event[ m_nextFreeRadarEvent ].soundPlayed = FALSE;
	m_event[ m_nextFreeRadarEvent ].mark = (UnsignedByte)( mark == RADAR_MARK_BY_TYPE ? markForType( type ) : mark );

	// record the index of this, our "last" radar event.
	if ( type != RADAR_EVENT_BEACON_PULSE )
		m_lastRadarEvent = m_nextFreeRadarEvent;

	//
	// increment the next radar event index, wrapping to the beginning.  If we ever have so many
	// events that they fill up the buffer the oldest ones will just drop off, eh ... should be fine.
	//
	m_nextFreeRadarEvent++;
	if( m_nextFreeRadarEvent >= MAX_RADAR_EVENTS )
		m_nextFreeRadarEvent = 0;

}  // end createEvent

//-------------------------------------------------------------------------------------------------
/** Get the last event position, if any.
	* Returns TRUE if event was found
	* Returns FALSE if there is no "last event" */
//-------------------------------------------------------------------------------------------------
Bool Radar::getLastEventLoc( Coord3D *eventPos )
{

	// if we have an index for the last event, one was present
	if( m_lastRadarEvent != -1 )
	{

		if( eventPos )
			*eventPos = m_event[ m_lastRadarEvent ].worldLoc;
		return TRUE;

	}  // end if

	return FALSE;  // no last event

}  // end getLastEventLoc

// two alarms of a type nearer than this are one place: the throttle refreshes the older, and the
// spacebar does not stop at it twice
static const Real RADAR_EVENT_SAME_PLACE = 250.0f;

// presses further apart than this start the spacebar again at the last event
static const UnsignedInt RADAR_CYCLE_FRAMES = LOGICFRAMES_PER_SECOND * 3;

//-------------------------------------------------------------------------------------------------
/** How big an event of this type draws when its maker does not say: a building's news at a
	* building's size, a unit's at a unit's */
//-------------------------------------------------------------------------------------------------
RadarEventMark Radar::markForType( RadarEventType type )
{
	switch( type )
	{
		case RADAR_EVENT_CONSTRUCTION:
		case RADAR_EVENT_UPGRADE:
		case RADAR_EVENT_INFORMATION:
		case RADAR_EVENT_BATTLE_PLAN:
		case RADAR_EVENT_STRUCTURE_LOST:
			return RADAR_MARK_STRUCTURE;
		default:
			return RADAR_MARK_UNIT;
	}
}

//-------------------------------------------------------------------------------------------------
/** The event a new one of this type at pos is held back by: the same type, the same place and made
	* under ten seconds ago.  -1 when there is none. */
//-------------------------------------------------------------------------------------------------
Int Radar::findThrottlingEvent( const RadarEvent *events, RadarEventType type, const Coord3D *pos, UnsignedInt frame )
{
	const UnsignedInt framesBetweenEvents = LOGICFRAMES_PER_SECOND * 10;
	for( Int i = 0; i < MAX_RADAR_EVENTS; ++i )
	{
		if( events[ i ].type != type )
			continue;

		//
		// Distance from the new event to this one, in 2D.  There were no brackets here at all,
		// so what it worked out was x - (px*x) - px + y - (py*y) - py: not a distance, not even
		// a positive number most of the time.  The test below it therefore answered at random,
		// which is why two attacks on opposite ends of the map could suppress each other's
		// warning while two on the same building both got one.
		//
		const Real dx = events[ i ].worldLoc.x - pos->x;
		const Real dy = events[ i ].worldLoc.y - pos->y;
		if( dx * dx + dy * dy <= RADAR_EVENT_SAME_PLACE * RADAR_EVENT_SAME_PLACE &&
				frame - events[ i ].createFrame < framesBetweenEvents )
			return i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
/** A hit the throttle held back keeps the alarm it fell under alive, drawn again if it had gone
	* out: it dies secondsToLive after the latest hit, not the first.  It grows to the bigger mark
	* of the two, so a unit's ring next to a building that is then hit becomes the building's. */
//-------------------------------------------------------------------------------------------------
void Radar::refreshEventLife( RadarEvent *event, UnsignedInt frame, Real secondsToLive, RadarEventMark mark )
{
	static const Real secondsBeforeDieToFade = 0.5f;		// as internalCreateEvent
	const UnsignedInt dieFrame = frame + (UnsignedInt)( LOGICFRAMES_PER_SECOND * secondsToLive );
	if( event->active == FALSE || dieFrame > event->dieFrame )
	{
		event->dieFrame = dieFrame;
		event->fadeFrame = dieFrame - (UnsignedInt)( LOGICFRAMES_PER_SECOND * secondsBeforeDieToFade );
	}
	event->active = TRUE;
	if( mark != RADAR_MARK_BY_TYPE && mark != RADAR_MARK_SIGNAL && event->mark != RADAR_MARK_SIGNAL && (UnsignedByte)mark > event->mark )
		event->mark = (UnsignedByte)mark;
}

//-------------------------------------------------------------------------------------------------
/** The next event back from `from` in the ring, counting from `newest`, that the spacebar may go
	* to: not an empty slot, not a beacon's pulse, and not the same place as one this run of presses
	* has already shown.  -1 when there is none left. */
//-------------------------------------------------------------------------------------------------
Int Radar::olderEventIndex( const RadarEvent *events, Int newest, Int from, const Int *visited, Int visitedCount )
{
	if( newest < 0 || newest >= MAX_RADAR_EVENTS || from < 0 || from >= MAX_RADAR_EVENTS )
		return -1;

	// how far back from the newest `from` is; the oldest the ring holds is MAX_RADAR_EVENTS - 1 back
	const Int fromAge = ( newest - from + MAX_RADAR_EVENTS ) % MAX_RADAR_EVENTS;
	for( Int age = fromAge + 1; age < MAX_RADAR_EVENTS; ++age )
	{
		const Int i = ( newest - age + MAX_RADAR_EVENTS ) % MAX_RADAR_EVENTS;
		if( events[ i ].type == RADAR_EVENT_INVALID || events[ i ].type == RADAR_EVENT_BEACON_PULSE )
			continue;

		Bool seen = FALSE;
		for( Int v = 0; v < visitedCount && !seen; ++v )
		{
			const Coord3D &there = events[ visited[ v ] ].worldLoc;
			const Real dx = events[ i ].worldLoc.x - there.x;
			const Real dy = events[ i ].worldLoc.y - there.y;
			seen = dx * dx + dy * dy <= RADAR_EVENT_SAME_PLACE * RADAR_EVENT_SAME_PLACE;
		}
		if( !seen )
			return i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
/** The spacebar.  The first press goes to the last event as it always did; each press within three
	* seconds of the one before goes one alarm further back, up to RADAR_CYCLE_DEPTH places, then
	* starts again at the last.  A new event starts it again too. */
//-------------------------------------------------------------------------------------------------
Bool Radar::cycleEventLoc( Coord3D *eventPos )
{
	if( m_lastRadarEvent == -1 )
		return FALSE;

	const UnsignedInt frame = TheGameLogic->getFrame();
	Int next = -1;
	if( m_cycleCount > 0 && m_cycleCount < RADAR_CYCLE_DEPTH && m_cycleAnchor == m_lastRadarEvent &&
			frame >= m_cycleFrame && frame - m_cycleFrame <= RADAR_CYCLE_FRAMES )
		next = olderEventIndex( m_event, m_lastRadarEvent, m_cycleVisited[ m_cycleCount - 1 ], m_cycleVisited, m_cycleCount );

	if( next == -1 )
	{
		// from the top
		m_cycleCount = 0;
		m_cycleAnchor = m_lastRadarEvent;
		next = m_lastRadarEvent;
	}

	m_cycleVisited[ m_cycleCount++ ] = next;
	m_cycleFrame = frame;
	if( eventPos )
		*eventPos = m_event[ next ].worldLoc;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** What the slot draws, for the pointers at the view's edge: its type, where, its colour and how
	* far it has faded, 1 solid.  FALSE when the slot shows nothing. */
//-------------------------------------------------------------------------------------------------
Bool Radar::getShownEvent( Int slot, RadarEventType *type, Coord3D *world, RGBAColorInt *color, Real *alpha ) const
{
	if( slot < 0 || slot >= MAX_RADAR_EVENTS )
		return FALSE;
	const RadarEvent &event = m_event[ slot ];
	if( event.active == FALSE || event.type == RADAR_EVENT_FAKE || event.type == RADAR_EVENT_BEACON_PULSE ||
			event.type == RADAR_EVENT_INVALID )
		return FALSE;

	const UnsignedInt frame = TheGameLogic->getFrame();
	Real fade = 1.0f;
	if( frame > event.fadeFrame && event.dieFrame > event.fadeFrame )
		fade = 1.0f - (Real)( frame - event.fadeFrame ) / (Real)( event.dieFrame - event.fadeFrame );
	if( fade <= 0.0f )
		return FALSE;

	if( type )
		*type = event.type;
	if( world )
		*world = event.worldLoc;
	if( color )
		*color = event.color1;
	if( alpha )
		*alpha = fade;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** Try to create a radar event for "we're under attack".  This will be called every time
	* actual damage is dealt to an object that the player owns that shows up on the radar.
	* We don't want to create under attack events every time we are damaged and we also want
	* to limit them based on time and local area of other recent attack events */
// ------------------------------------------------------------------------------------------------
void Radar::tryUnderAttackEvent( const Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	//
	// Some things are shot at constantly and raising the alarm for each of them buries the one that
	// matters: a wall segment, a mine, a piece of scenery a stray shell clipped.  NO_ATTACK_WARNING
	// in an object's KindOf says damage to it is not news - no ping, no glow, no EVA line.
	//
	if( obj->isKindOf( KINDOF_NO_ATTACK_WARNING ) )
		return;

	/* LOGIC-THREAD.md phase 2, M6.  What the warning says is read from the object at the call, since the
		 hit may kill it; whether it is given at all, and everything it shows and plays, waits for the end
		 of the tick (callAfterTick runs it at once outside one). */
	enum { WARN_HARVESTER, WARN_UNIT, WARN_BASE, WARN_OTHER } kind = WARN_OTHER;
	if( obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE ) )
		kind = obj->isKindOf( KINDOF_HARVESTER ) ? WARN_HARVESTER : WARN_UNIT;
	else if( obj->isKindOf( KINDOF_STRUCTURE ) && obj->isKindOf( KINDOF_MP_COUNT_FOR_VICTORY ) )
		kind = WARN_BASE;
	const Bool ours = obj->getControllingPlayer()->isLocalPlayer();
	const Bool allied = ThePlayerList->getLocalPlayer()->getRelationship( obj->getTeam() ) == ALLIES;
	const Coord3D at = *obj->getPosition();

	LogicThread::callAfterTick( [this, kind, ours, allied, at]()
	{
		// try to create the event; one held back keeps the alarm already there alive.  The ring is as
		// big as what was hit: the base's buildings, then a harvester, then any other unit
		const RadarEventMark mark = kind == WARN_BASE ? RADAR_MARK_STRUCTURE
																: kind == WARN_UNIT ? RADAR_MARK_UNIT : RADAR_MARK_HARVESTER;
		if( !tryEvent( RADAR_EVENT_UNDER_ATTACK, &at, mark ) )
			return;

		// the lamp beside the radar
		if( TheControlBar )
			TheControlBar->triggerRadarAttackGlow();
		//
		///@todo Should make an INI data driven table for radar event strings, and audio events
		//
		// UI feedback for being under attack (note that we display these messages and audio
		// queues even if we don't have a radar)
		//
		Player *player = ThePlayerList->getLocalPlayer();

		// create a message for the attack event
		if( kind == WARN_HARVESTER || kind == WARN_UNIT )
		{
			AudioEventRTS unitAttackSound;
			if( kind == WARN_HARVESTER )
			{
				TheInGameUI->message( "RADAR:HarvesterUnderAttack" );
				unitAttackSound = TheAudio->getMiscAudio()->m_radarHarvesterUnderAttackSound;
			}
			else
			{
				TheInGameUI->message( "RADAR:UnitUnderAttack" );
				// EA played the structure's here; the unit's own is the same warning, quieter
				unitAttackSound = TheAudio->getMiscAudio()->m_radarUnitUnderAttackSound;
			}
			unitAttackSound.setPlayerIndex(player->getPlayerIndex());
			TheAudio->addAudioEvent( &unitAttackSound );
		}
		else if( kind == WARN_BASE )
		{
			// play EVA. If its our object, play Base under attack.
			if (ours)
				TheEva->setShouldPlay(EVA_BaseUnderAttack);
			else if (allied)
				TheEva->setShouldPlay(EVA_AllyUnderAttack);

			TheInGameUI->message( "RADAR:StructureUnderAttack" );

			static AudioEventRTS structureAttackSound = TheAudio->getMiscAudio()->m_radarStructureUnderAttackSound;
			structureAttackSound.setPlayerIndex(player->getPlayerIndex());
			TheAudio->addAudioEvent( &structureAttackSound );
		}
		else
		{
			TheInGameUI->message( "RADAR:UnderAttack" );

			static AudioEventRTS underAttackSound = TheAudio->getMiscAudio()->m_radarUnderAttackSound;
			underAttackSound.setPlayerIndex(player->getPlayerIndex());
			TheAudio->addAudioEvent( &underAttackSound );
		}
	} );

}  // end tryUnderAttackEvent

// ------------------------------------------------------------------------------------------------
/** Try to create a radar event for "infiltration".
		This happens whenever a unit is hijacked, defected, converted to carbomb, hacked, or 
		otherwise snuck into */ 
// ------------------------------------------------------------------------------------------------
void Radar::tryInfiltrationEvent( const Object *obj )
{
	// the event, the message and the sound below each wait for the end of a tick themselves

	//Sanity!
	if( !obj )
	{
		return;
	}

	// We should only be warned against infiltrations that are taking place against us.
	if( obj->getControllingPlayer() != ThePlayerList->getLocalPlayer() )
		return;

	// create the radar event, a building's at a building's size
	createEvent( obj->getPosition(), RADAR_EVENT_INFILTRATION, 4.0f,
							 obj->isKindOf( KINDOF_STRUCTURE ) ? RADAR_MARK_STRUCTURE : RADAR_MARK_UNIT );

	//
	///@todo Should make an INI data driven table for radar event strings, and audio events
	//
	// UI feedback for being under attack (note that we display these messages and audio
	// queues even if we don't have a radar)
	//
	Player *player = ThePlayerList->getLocalPlayer();

	// display message
	TheInGameUI->message( "RADAR:Infiltration" );

	// play audio event
	static AudioEventRTS infiltrationWarningSound = TheAudio->getMiscAudio()->m_radarInfiltrationSound;
	infiltrationWarningSound.setPlayerIndex(player->getPlayerIndex());
	TheAudio->addAudioEvent( &infiltrationWarningSound );

}  // end tryInfiltrationEvent

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool Radar::tryEvent( RadarEventType event, const Coord3D *pos, RadarEventMark mark )
{
	// inside a tick the try waits for its end and the answer is FALSE: a caller that acts on the answer
	// asks from callAfterTick (StealthDetectorUpdate, tryUnderAttackEvent)
	if( LogicThread::defersEffects() && pos != NULL )
	{
		const Coord3D at = *pos;
		LogicThread::callAfterTick( [=]() { tryEvent( event, &at, mark ); } );
		return FALSE;
	}
	LOGIC_CENSUS( CENSUS_RADAR );

	// sanity
	if( event <= RADAR_EVENT_INVALID || event >= RADAR_EVENT_NUM_EVENTS || pos == NULL )
		return FALSE;

	//
	// An event of this type at the same place under ten seconds ago holds a new one back: no second
	// alarm, no second message or sound.  The hit is not lost, though: it keeps that alarm alive, so a
	// fight's ring stays up while the fight goes on and goes out a few seconds after it stops.
	//
	const UnsignedInt currentFrame = TheGameLogic->getFrame();
	const Int held = findThrottlingEvent( m_event, event, pos, currentFrame );
	if( held != -1 )
	{
		refreshEventLife( &m_event[ held ], currentFrame, 4.0f, mark );
		return FALSE;
	}

	// a long fight warns again every ten seconds; its new alarm takes over from the old one there
	for( Int i = 0; i < MAX_RADAR_EVENTS; ++i )
	{
		if( m_event[ i ].type != event || m_event[ i ].active == FALSE )
			continue;
		const Real dx = m_event[ i ].worldLoc.x - pos->x;
		const Real dy = m_event[ i ].worldLoc.y - pos->y;
		if( dx * dx + dy * dy <= RADAR_EVENT_SAME_PLACE * RADAR_EVENT_SAME_PLACE )
			m_event[ i ].active = FALSE;
	}

	// if we got here then we want to create a new event
	createEvent( pos, event, 4.0f, mark );

	// return TRUE for successfully created event
	return TRUE;

}  // end tryEvent


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Radar::refreshTerrain( TerrainLogic *terrain )
{

	// no future queue is valid now
	m_queueTerrainRefreshFrame = 0;

}  // end refreshTerrain

// ------------------------------------------------------------------------------------------------
/** Queue a refresh of the radar terrain, we have this so that if there is code that
	* rapidly needs to refresh the radar, it should use this so we aren't continually
	* rebuilding the radar graphic because that process is slow.  If you need to update
	* the terrain on the radar immediately use refreshTerrain() */
// ------------------------------------------------------------------------------------------------
void Radar::queueTerrainRefresh( void )
{

	//
	// we just simply overwrite the frame we have recorded for a radar refresh.  If there was
	// already one there, it's simply just forgotten and whatever changes we wanted to see
	// with that refresh will have to wait until enough time has passed to show these
	// changes as well.  why you ask ... well, because if we're calling this in close enough
	// proximity for us to overwrite something, we're changing the terrain features 
	// quite often and can't afford the expense of rebuilding the radar visual
	//
	const UnsignedInt frame = TheGameLogic->getFrame();
	LogicThread::callAfterTick( [this, frame]() { m_queueTerrainRefreshFrame = frame; } );

}  // end queueTerrainRefresh

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Radar::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer a radar object list given the head pointer as a parameter
	* Version Info;
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
static void xferRadarObjectList( Xfer *xfer, RadarObject **head )
{
	RadarObject *radarObject;

	// sanity
	DEBUG_ASSERTCRASH( head != NULL, ("xferRadarObjectList - Invalid parameters\n" ));

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// write could of objects in list
	UnsignedShort count = 0;
	for( radarObject = *head; radarObject; radarObject = radarObject->friend_getNext() )
		count++;
	xfer->xferUnsignedShort( &count );

	// xfer the list data
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// save each object
		for( radarObject = *head; radarObject; radarObject = radarObject->friend_getNext() )
		{

			// save this object
			xfer->xferSnapshot( radarObject );

		}  // end for, radarObject

	}  // end if, save
	else
	{

		// the list should be empty at this point as we are loading it as a whole
		if( *head != NULL )
		{
#if 1
			// srj sez: yeah, it SHOULD be, but a few rogue things come into existence (via their ctor) preloaded
			// with stuff (eg, "CabooseFullOfTerrorists"). we immediately destroy 'em, but they don't go away just yet.
			// so just ignore 'em if possible.
			for( radarObject = *head; radarObject; radarObject = radarObject->friend_getNext() )
			{
				if (!radarObject->friend_getObject()->isDestroyed())
				{
					DEBUG_CRASH(( "xferRadarObjectList - List head should be NULL, or contain only destroyed objects\n" ));
					throw SC_INVALID_DATA;
				}
			}
#else
			DEBUG_CRASH(( "xferRadarObjectList - List head should be NULL, but isn't\n" ));
			throw SC_INVALID_DATA;
#endif
		}  // end if

		// read each element
		for( UnsignedShort i = 0; i < count; ++i )
		{

			// alloate a new radar object 
			radarObject = newInstance(RadarObject);

			// link to the end of the list
			if( *head == NULL )
				*head = radarObject;
			else
			{

				RadarObject *other;
				for( other = *head; other->friend_getNext() != NULL; other = other->friend_getNext() )
				{
				}  // end for, other

				// set the end of the list to point to the new object
				other->friend_setNext( radarObject );

			}  // end else

			// load the data
			xfer->xferSnapshot( radarObject );
						
		}  // end for i

	}  // end else, load

}  // end xferRadarObjectList

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version
	* 2: each event's mark, its size on the radar */
// ------------------------------------------------------------------------------------------------
void Radar::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// radar hidden
	xfer->xferBool( &m_radarHidden );

	// radar force on
	xfer->xferBool( &m_radarForceOn );

	// save our local object list
	xferRadarObjectList( xfer, &m_localObjectList );

	// save the regular object list
	xferRadarObjectList( xfer, &m_objectList );

	// save the radar event count and data
	UnsignedShort eventCountVerify = MAX_RADAR_EVENTS;
	UnsignedShort eventCount = eventCountVerify;
	xfer->xferUnsignedShort( &eventCount );
	if( eventCount != eventCountVerify )
	{

		DEBUG_CRASH(( "Radar::xfer - size of MAX_RADAR_EVENTS has changed, you must version this xfer method to accomodate the new array size.  Was '%d' and is now '%d'\n",
									eventCount, eventCountVerify ));
		throw SC_INVALID_DATA;

	}  // end if
	for( UnsignedShort i = 0; i < eventCount; ++i )
	{

		// xfer event data
		xfer->xferUser( &m_event[ i ].type, sizeof( RadarEventType ) );
		xfer->xferBool( &m_event[ i ].active );
		xfer->xferUnsignedInt( &m_event[ i ].createFrame );
		xfer->xferUnsignedInt( &m_event[ i ].dieFrame );
		xfer->xferUnsignedInt( &m_event[ i ].fadeFrame );
		xfer->xferRGBAColorInt( &m_event[ i ].color1 );
		xfer->xferRGBAColorInt( &m_event[ i ].color2 );
		xfer->xferCoord3D( &m_event[ i ].worldLoc );
		xfer->xferICoord2D( &m_event[ i ].radarLoc );
		xfer->xferBool( &m_event[ i ].soundPlayed );
		if( version >= 2 )
			xfer->xferUnsignedByte( &m_event[ i ].mark );
		else
			m_event[ i ].mark = (UnsignedByte)markForType( m_event[ i ].type );

	}  // end for i

	// next event index
	xfer->xferInt( &m_nextFreeRadarEvent );

	// last event index
	xfer->xferInt( &m_lastRadarEvent );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Radar::loadPostProcess( void )
{

	//
	// refresh the radar texture now that all the objects (specifically bridges) have
	// been loaded with their correct damage states from save game file
	//
	refreshTerrain( TheTerrainLogic );

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** Is the priority type passed in a "visible" one that can show up on the radar */
// ------------------------------------------------------------------------------------------------
Bool Radar::isPriorityVisible( RadarPriorityType priority ) const 
{

	switch( priority )
	{

		case RADAR_PRIORITY_INVALID:
		case RADAR_PRIORITY_NOT_ON_RADAR:
			return FALSE;

		default:
			return TRUE;

	}  // end switch

}  // end isPriorityVisible
