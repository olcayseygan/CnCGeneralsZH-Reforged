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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DGameClient.cpp /////////////////////////////////////////////////
//
// W3DImplementaion of the GameClient.  If there were a client/server
// architecture, this game interface could be thought of as the "client"
// that the user uses to interact with the logic of the game world which
// would be known as the "server"
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <stdlib.h>

// USER INCLUDES //////////////////////////////////////////////////////////////

#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ModuleFactory.h"
#include "Common/RandomValue.h"
#include "Common/GlobalData.h"
#include "Common/GameLOD.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/RayEffect.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/View.h"
#include "WW3D2/dx11runtime.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/GameClient/W3DStatusCircle.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/GroundMarks.h"
#include "W3DDevice/GameClient/StructureCollapse.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "WW3D2/hlod.h"
#include "WW3D2/part_emt.h"
#include "WW3D2/hanim.h"
#include "WW3D2/htree.h"
#include "WW3D2/animobj.h"  ///< @todo superhack for demo, remove!

extern void disableUVAnimations(RenderObjClass *robj);	// W3DGhostObject.cpp

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DGameClient::W3DGameClient()
{

}  // end W3DGameClient

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DGameClient::~W3DGameClient()
{

}  // end ~W3DGameClient

//-------------------------------------------------------------------------------------------------
/** Initialize resources for the w3d game client */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::init( void )
{

	// extending initialization routine
	GameClient::init();

}  // end init

//-------------------------------------------------------------------------------------------------
/** Per frame udpate, note we are extending functionality */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::update( void )
{

	// before the base, whose draw opens the window the tick runs beside
	StructureCollapse::update();

	// call base
	GameClient::update();

}  // end update

//-------------------------------------------------------------------------------------------------
/** Reset this device client system.  Note we are extending reset functionality from
	* the device independent client */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::reset( void )
{

	StructureCollapse::clear();

	// call base class
	GameClient::reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** allocate a new drawable using the thing template for initialization.
	* if we want to have the thing manager actually contain the pools of
	* object and drawable storage it seems OK to have it be friends with the
	* GameLogic/Client for those purposes, or we could put the allocation pools
	* in the GameLogic and GameClient themselves */
//-------------------------------------------------------------------------------------------------
Drawable *W3DGameClient::friend_createDrawable( const ThingTemplate *tmplate,
																								DrawableStatus statusBits )
{
	Drawable *draw = NULL;

	// sanity
	if( tmplate == NULL )
		return NULL;
	
	draw = newInstance(Drawable)( tmplate, statusBits );

	return draw;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DGameClient::addScorch(const Coord3D *pos, Real radius, Scorches type)
{
	const Coord3D at = *pos;
	LOGIC_AFTER_TICK( [=]() { addScorch( &at, radius, type ); } );
	LOGIC_CENSUS( CENSUS_GAMECLIENT );
	if (TheTerrainRenderObject)
	{
		Vector3 loc(pos->x, pos->y, pos->z);
		TheTerrainRenderObject->addScorch(loc, radius, type);
	}
}

//-------------------------------------------------------------------------------------------------
void W3DGameClient::addGroundMark(const Coord3D *pos, Real radius, Int type)
{
	const Coord3D at = *pos;
	LOGIC_AFTER_TICK( [=]() { addGroundMark( &at, radius, type ); } );
	if (TheTerrainRenderObject)
		TheTerrainRenderObject->addGroundMark(Vector3(pos->x, pos->y, pos->z), radius, type);
}

//-------------------------------------------------------------------------------------------------
/** A building is down, in cells this screen sees: a soft stain with cracks in it.  Called from the
	* body's change of state inside logic's pass, and played after it. */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::structureFell(const Coord3D *pos, Real radius)
{
	const Coord3D at = *pos;
	LOGIC_AFTER_TICK( [=]() { structureFell( &at, radius ); } );
	// a landmark bridge counts as a structure at over 1000 units: no round stain fits that
	const Real MAX_STAINED_RADIUS = 150.0f;
	// under this the cracks are smaller than a terrain cell's reach and their edge runs into the next art
	const Real MIN_STAINED_RADIUS = 12.5f;
	if (radius > MAX_STAINED_RADIUS || radius < MIN_STAINED_RADIUS)
		return;
	if (ThePartitionManager->getShroudStatusForPlayer(TheObserverCamera.getShroudPlayerIndex(), pos) != CELLSHROUD_CLEAR)
		return;
	addGroundMark(pos, radius * 1.1f, GroundMarks::STAIN);
	addGroundMark(pos, radius * 0.8f, GroundMarks::CRACKS);

	// a big one jolts the camera on top of what its FX shakes, a step under a nuke's kick; a barracks or
	// a tower does not.  Not under Classic or Direct3D 9, which keep the old shake.
	const Real JOLT_FROM_RADIUS = 40.0f;
	if (radius >= JOLT_FROM_RADIUS && TheTacticalView && !TheGlobalData->m_classicGraphics && Direct3D11_Present_Is_Enabled())
		TheTacticalView->shake(pos, View::SHAKE_STRONG);
}

//-------------------------------------------------------------------------------------------------
/** The Water Splashes option: a blast or a falling body came down at pos.  In water it throws a splash
	* and, where the water has depth, foam and rings, sized by radius, from the surface.
	* ParticleSystemReforged.ini holds the systems, outside the multiplayer checksum.  Classic Graphics
	* and Direct3D 9 keep EA's water, and the Modern water style its own foam. */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::splashInWater(const Coord3D *pos, Real radius)
{
	if (!TheGlobalData->m_waterSplashes || TheGlobalData->m_classicGraphics || TheGlobalData->m_waterStyle != 0 || !Direct3D11_Present_Is_Enabled())
		return;
	const Coord3D at = *pos;
	LOGIC_AFTER_TICK( [=]() { splashInWater( &at, radius ); } );

	// any higher is a blast in the air; at or under the surface is what lands, since a shell comes down on
	// the bed.  The surface, not the bed, is where the water shows.
	const Real MAX_HEIGHT_OVER_WATER = 10.0f;
	Real waterZ, bedZ;
	if (!TheTerrainLogic->isUnderwater(pos->x, pos->y, &waterZ, &bedZ) || pos->z > waterZ + MAX_HEIGHT_OVER_WATER)
		return;
	if (pos->z > waterZ && TheTerrainLogic->findBridgeAt(pos) != NULL)
		return;
	if (ThePartitionManager->getShroudStatusForPlayer(TheObserverCamera.getShroudPlayerIndex(), pos) != CELLSHROUD_CLEAR)
		return;

	// the systems' colours are a day's: a night's splash takes how bright the map's light is, not its
	// colour, which on a night map is deep blue
	const GlobalData::TerrainLighting &light = TheGlobalData->m_terrainLighting[TheGlobalData->m_timeOfDay][0];
	const Real bright = min(0.3f * (light.ambient.red + light.diffuse.red)
		+ 0.59f * (light.ambient.green + light.diffuse.green)
		+ 0.11f * (light.ambient.blue + light.diffuse.blue), 1.0f);

	// Flat layers want a real surface under them: on the sand at the water's edge they read as glass laid
	// on a beach.  A channel's open water is only 2 deep on the maps measured (Flash Effect, Dark Night)
	// and that edge 0.1.  A piece of debris or the shell itself coming down, radius 1, throws spray alone.
	const Real MIN_DEPTH_FOR_RINGS = 1.0f;
	const Real MIN_RADIUS_FOR_RINGS = 3.0f;
	static const char *const layers[] = { "WaterSplashThrow", "WaterSplashFoam", "WaterSplashRing", "WaterSplashRingLate" };
	const size_t layerCount = waterZ - bedZ < MIN_DEPTH_FOR_RINGS || radius < MIN_RADIUS_FOR_RINGS ? 1 : ARRAY_SIZE(layers);

	const Real scale = WWMath::Clamp(radius / 15.0f, 0.25f, 4.0f);
	const Coord3D surface = { pos->x, pos->y, waterZ };
	const Coord3D velocity = { scale, scale, scale };
	for (size_t i = 0; i < layerCount; ++i)
	{
		ParticleSystem *sys = TheParticleSystemManager->createParticleSystem(TheParticleSystemManager->findTemplate(layers[i]));
		if (sys == NULL)
			continue;
		sys->setPosition(&surface);
		sys->setSizeMultiplier(scale);
		sys->setVelocityMultiplier(&velocity);
		// every key, the first too: tintAllColors leaves that one, and a particle overshoots its rate to the next
		for (Int key = 0; key < MAX_KEYFRAMES; ++key)
		{
			sys->m_colorKey[key].color.red *= bright;
			sys->m_colorKey[key].color.green *= bright;
			sys->m_colorKey[key].color.blue *= bright;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The Collapse option, with the model the building had a frame before rubble: the copy that goes
	* into the ground.  Runs among the drawable's own work after the tick, and the drawable may be gone
	* by the next frame.  The animation stops where it stands, as a ghost object's does. */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::structureCollapsing(Drawable *draw, Real height)
{
	if (TheGlobalData->m_classicGraphics || !Direct3D11_Present_Is_Enabled() || W3DDisplay::m_3DScene == NULL)
		return;
	// a slab of foundation or a mine's stub has nothing to go down
	const Real MIN_HEIGHT = 8.0f;
	if (height < MIN_HEIGHT || draw->isDrawableEffectivelyHidden())
		return;
	for (DrawModule **dm = draw->getDrawModules(); *dm; ++dm)
	{
		ObjectDrawInterface *di = (*dm)->getObjectDrawInterface();
		RenderObjClass *robj = di ? ((W3DModelDraw *)di)->getRenderObject() : NULL;
		if (robj == NULL || robj->Is_Hidden())
			continue;
		RenderObjClass *copy = robj->Clone();
		copy->Set_ObjectColor(robj->Get_ObjectColor());
		copy->Set_Transform(robj->Get_Transform());
		copy->Set_Collision_Type(0);	// a click goes through to what stands behind it
		if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD)
		{
			float frame, mult;
			int mode, numFrames;
			HAnimClass *anim = ((HLodClass *)robj)->Peek_Animation_And_Info(frame, numFrames, mode, mult);
			copy->Set_Animation(anim, frame);
			disableUVAnimations(copy);
		}
		StructureCollapse::add(copy, height);
	}
}

//-------------------------------------------------------------------------------------------------
/** create an effect that requires a start and end location */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::createRayEffectByTemplate( const Coord3D *start, 
																		 const Coord3D *end, 
																		 const ThingTemplate* tmpl )
{
	Drawable *draw = TheThingFactory->newDrawable(tmpl);

	if( draw )
	{
		Coord3D pos;

		// add to world, the location of the drawable is at the midpoint of laser
		pos.x = (end->x - start->x) * 0.5f + start->x;
		pos.y = (end->y - start->y) * 0.5f + start->y;
		pos.z = (end->z - start->z) * 0.5f + start->z;
		draw->setPosition( &pos );

		// add this ray effect to the list of ray effects
		TheRayEffects->addRayEffect( draw, start, end );
			
	}  // end if

}  // end createRayEffectByTemplate

//-------------------------------------------------------------------------------------------------
/**  Tell all the drawables what time of day it is now */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::setTimeOfDay( TimeOfDay tod )
{

	GameClient::setTimeOfDay(tod);

	//tell cloud/water plane to update its lighting/texture
	if (TheWaterRenderObj)
		TheWaterRenderObj->setTimeOfDay(tod);
	if (TheW3DShadowManager)
		TheW3DShadowManager->setTimeOfDay(tod);

	//tell the display to update its lighting
	TheDisplay->setTimeOfDay( tod );

}  // end setTimeOfDay


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DGameClient::setTeamColor(Int red, Int green, Int blue)
{

	W3DStatusCircle::setColor(red, green, blue);

}  // end setTeamColor

//-------------------------------------------------------------------------------------------------
/** temporary entry point for adjusting LOD for development testing. */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::adjustLOD( Int adj ) 
{
	if (TheGlobalData == NULL) 
		return;

	TheWritableGlobalData->m_textureReductionFactor += adj;

	if (TheWritableGlobalData->m_textureReductionFactor > 4)
		TheWritableGlobalData->m_textureReductionFactor = 4;	//16x less resolution is probably enough.
	if (TheWritableGlobalData->m_textureReductionFactor < 0)
		TheWritableGlobalData->m_textureReductionFactor = 0;

	if (WW3D::Get_Texture_Reduction() != TheWritableGlobalData->m_textureReductionFactor)
	{	WW3D::Set_Texture_Reduction(TheWritableGlobalData->m_textureReductionFactor,32);
		TheGameLODManager->setCurrentTextureReduction(TheWritableGlobalData->m_textureReductionFactor);
		if( TheTerrainRenderObject ) 
  			TheTerrainRenderObject->setTextureLOD( TheWritableGlobalData->m_textureReductionFactor );
	}

}  // end adjustLOD

//-------------------------------------------------------------------------------------------------
/**  Tell the terrain that an object moved, so it can knock down trees or crush grass
		or whatever is appropriate. jba. */
//-------------------------------------------------------------------------------------------------
void W3DGameClient::notifyTerrainObjectMoved(Object *obj)
{
	// LOGIC-THREAD.md: drawTrees reads the trees beside the tick, so a push or a topple waits for its
	// end and takes the unit as it stands then.  A unit the tick destroyed pushes nothing.
	const ObjectID id = obj->getID();
	LOGIC_AFTER_TICK( [this, id]() {
		Object *mover = TheGameLogic->findObjectByID( id );
		if( mover )
			notifyTerrainObjectMoved( mover );
	} );
	if (TheTerrainRenderObject) {
		TheTerrainRenderObject->unitMoved(obj);
	}

}  // end setTimeOfDay


