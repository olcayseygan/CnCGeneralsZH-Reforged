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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DTerrainVisual.cpp /////////////////////////////////////////////////////////////////////
// W3D implementation details for visual aspects of terrain
// Author: Colin Day, April 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "Common/GameState.h"
#include "Common/LogicThread.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "Common/TerrainTypes.h"
#include "Common/Xfer.h"
#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.		 
#include "Common/QuickTrig.h"

#include "GameClient/Drawable.h"
#include "GameClient/ClientRandomValue.h"

#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"

#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainVisual.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DDebugIcons.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DGranny.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/FlatHeightMap.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "WW3D2/light.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/coltype.h"
#include "WW3D2/coltest.h"
#include "WW3D2/assetmgr.h"



class TestSeismicFilter : public SeismicSimulationFilterBase
{

  virtual SeismicSimStatusCode filterCallback( WorldHeightMapInterfaceClass *heightMap, const SeismicSimulationNode *node )
  {


    Int life = node->m_life;

    if ( heightMap == NULL )
      return SEISMIC_STATUS_INVALID;


    if ( life == 0 )
      return SEISMIC_STATUS_ACTIVE;
    if ( life < 15 )
    {
      // ADD HEIGHT BECAUSE THE EXPLOSION IS PUSHING DIRT UP

      Real magnitude = node->m_magnitude;

      Real offsScalar =  magnitude / (Real)life; // real-life, get it?
      Int radius = node->m_radius;
      Int border = heightMap->getBorderSize();
      Int centerX = node->m_center.x + border ;
      Int centerY = node->m_center.y + border ;
  
      UnsignedInt workspaceWidth = radius*2;
      Real *workspace = NEW( Real[ sqr(workspaceWidth) ] );
      Real *workspaceEnd = workspace + sqr(workspaceWidth);


      for ( Real *t = workspace; t < workspaceEnd; ++t ) *t = 0.0f;// clear the workspace

      Int x;	// VC6 leaked it out to the copy-back loop below
      for (x = 0; x < radius; ++x)
      {
        for (Int y = 0; y < radius; ++y)
        {

          Real distance = sqrt( sqr(x) + sqr(y) );//Pythagoras
      
          if ( distance < radius )
          {
            Real distScalar = cos( ( distance / radius * (PI/2) ) );
            Real height = (offsScalar * distScalar); 

            workspace[ (radius + x) +  workspaceWidth * (radius + y) ] = height + heightMap->getBilinearSampleSeismicZVelocity( centerX + x,  centerY + y ) ;//kaleidoscope
            
            if ( x != 0 ) // non-zero test prevents cross-shaped double stamp 
            {
      			  workspace[ (radius - x) + workspaceWidth * (radius + y) ] = height + heightMap->getBilinearSampleSeismicZVelocity( centerX - x,  centerY + y ) ;
              if ( y != 0 )
                workspace[ (radius - x) + workspaceWidth * (radius - y) ] =  height + heightMap->getBilinearSampleSeismicZVelocity( centerX - x,  centerY - y ) ;
            }
            if ( y != 0 )
      			  workspace[ (radius + x) + workspaceWidth * (radius - y) ] = height + heightMap->getBilinearSampleSeismicZVelocity( centerX + x,  centerY - y ) ;
          }
        }
      }

      // stuff the values from the workspace into the heightmap's velocities
      for (x = 0; x < workspaceWidth; ++x)
        for (Int y = 0; y < workspaceWidth; ++y)
    			heightMap->setSeismicZVelocity( centerX - radius + x, centerY - radius + y,  workspace[  x + workspaceWidth * y ]  );

      delete [] workspace;

      return SEISMIC_STATUS_ACTIVE;
    }
    else
      return SEISMIC_STATUS_ZERO_ENERGY;
  }

  virtual Real applyGravityCallback( Real velocityIn )
  {
    Real velocityOut = velocityIn;
    velocityOut -= 1.5f;
    return velocityOut;
  }


};
static TestSeismicFilter testSeismicFilter;




//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTerrainVisual::W3DTerrainVisual()
{

	m_terrainRenderObject = NULL;
	m_waterRenderObject = NULL;
	TheWaterRenderObj = NULL;
	
  m_logicHeightMap   = NULL;
  
#ifdef DO_SEISMIC_SIMULATIONS
  m_clientHeightMap = NULL;
#endif











}  // end W3DTerrainVisual

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTerrainVisual::~W3DTerrainVisual()
{
	// release our render object
	if (TheTerrainRenderObject == m_terrainRenderObject) {
		TheTerrainRenderObject = NULL;
	}

	if (TheTerrainTracksRenderObjClassSystem)
	{
		delete TheTerrainTracksRenderObjClassSystem;
		TheTerrainTracksRenderObjClassSystem=NULL;
	}

#ifdef	INCLUDE_GRANNY_IN_BUILD
	if (TheGrannyRenderObjSystem)
	{
		delete TheGrannyRenderObjSystem;
		TheGrannyRenderObjSystem=NULL;
	}
#endif

	if (TheW3DShadowManager)
	{	
		delete TheW3DShadowManager;
		TheW3DShadowManager=NULL;
	}

	if (TheSmudgeManager)
	{
		delete TheSmudgeManager;
		TheSmudgeManager=NULL;
	}

	REF_PTR_RELEASE( m_waterRenderObject );
	TheWaterRenderObj=NULL;
	REF_PTR_RELEASE( m_terrainRenderObject );
	REF_PTR_RELEASE( m_logicHeightMap );

#ifdef DO_SEISMIC_SIMULATIONS
  REF_PTR_RELEASE( m_clientHeightMap );
#endif
}  // end ~W3DTerrainVisual

//-------------------------------------------------------------------------------------------------
/** init */
//-------------------------------------------------------------------------------------------------
void W3DTerrainVisual::init( void )
{

	// extend
	TerrainVisual::init();
	// create a new render object for W3D
	m_terrainRenderObject = NEW_REF( HeightMapRenderObjClass, () );
	m_terrainRenderObject->Set_Collision_Type( PICK_TYPE_TERRAIN );
	TheTerrainRenderObject = m_terrainRenderObject;

	// initialize track drawing system
	TheTerrainTracksRenderObjClassSystem = NEW TerrainTracksRenderObjClassSystem;
	TheTerrainTracksRenderObjClassSystem->init(W3DDisplay::m_3DScene);

#ifdef	INCLUDE_GRANNY_IN_BUILD
	// initialize Granny model drawing system
	TheGrannyRenderObjSystem = NEW GrannyRenderObjSystem;
#endif

	// initialize object shadow drawing system
	TheW3DShadowManager = NEW W3DShadowManager;
 	TheW3DShadowManager->init();
	
	// create a water plane render object
	TheWaterRenderObj=m_waterRenderObject = NEW_REF( WaterRenderObjClass, () );
	m_waterRenderObject->init(TheGlobalData->m_waterPositionZ, TheGlobalData->m_waterExtentX, TheGlobalData->m_waterExtentY, W3DDisplay::m_3DScene, (WaterRenderObjClass::WaterType)TheGlobalData->m_waterType);	//create a water plane that's 128x128 units
	m_waterRenderObject->Set_Position(Vector3(TheGlobalData->m_waterPositionX,TheGlobalData->m_waterPositionY,TheGlobalData->m_waterPositionZ));	//place water in world

	// create smudge rendering system.
	TheSmudgeManager = NEW(W3DSmudgeManager);
	TheSmudgeManager->init();

#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("********************* WARNING- Doing UNIT TIMINGS. ")
#else 
		if (TheGlobalData->m_waterType == WaterRenderObjClass::WATER_TYPE_1_FB_REFLECTION)
		{	// add water render object to the pre-pass scene (to be rendered before main scene)
 			//W3DDisplay::m_prePass3DScene->Add_Render_Object( m_waterRenderObject);
		}
		else
		{	// add water render object to the post-pass scene (to be rendered after main scene)
			W3DDisplay::m_3DScene->Add_Render_Object( m_waterRenderObject);
		}
#endif
	if (TheGlobalData->m_useCloudPlane)
		m_waterRenderObject->toggleCloudLayer(true);
	else
		m_waterRenderObject->toggleCloudLayer(false);

	// the vertex animated water's index 0 settings, which were set here, are TerrainLogic::init's now:
	// the grid is logic's, and the render object takes its transform from it when it draws

#ifdef DO_SEISMIC_SIMULATIONS
  m_seismicSimulationList.clear();
#endif

}  // end init

//-------------------------------------------------------------------------------------------------
/** reset */
//-------------------------------------------------------------------------------------------------
void W3DTerrainVisual::reset( void )
{

	// extend
	TerrainVisual::reset();

	m_terrainRenderObject->reset();

	if (TheW3DShadowManager)
		TheW3DShadowManager->Reset();

	if (TheSmudgeManager)
		TheSmudgeManager->reset();

	if (TheTerrainTracksRenderObjClassSystem)
		TheTerrainTracksRenderObjClassSystem->Reset();

	// reset water render object if present
	if( m_waterRenderObject )
	{
		for (Int i=0; i<5; i++)
		{
			//check if this texture was ever changed from default
			if (m_currentSkyboxTexNames[i] != m_initialSkyboxTexNames[i])
			{
				m_waterRenderObject->replaceSkyboxTexture(m_currentSkyboxTexNames[i], m_initialSkyboxTexNames[i]);
				m_currentSkyboxTexNames[i]=m_initialSkyboxTexNames[i];	//update current state to new texture
			}
		}

		m_waterRenderObject->reset();
	}

#ifdef DO_SEISMIC_SIMULATIONS
  m_seismicSimulationList.clear();
#endif
  
}  // end reset

//-------------------------------------------------------------------------------------------------
/** update */
//-------------------------------------------------------------------------------------------------
void W3DTerrainVisual::update( void )
{

	// extend
	TerrainVisual::update();

#ifdef DO_SEISMIC_SIMULATIONS
  handleSeismicSimulations();
#endif
	// if we have a water render object, it has an update method
	if( m_waterRenderObject )
		m_waterRenderObject->update();


}  // end update


#ifdef DO_SEISMIC_SIMULATIONS

void W3DTerrainVisual::addSeismicSimulation( const SeismicSimulationNode& sim )
{
    // HERE WOULD BE A GREAT PLACE FOR AN IDIOT TEST:
  // REJECT SIMULATION NODES THAT ARE OFF SCREEN!!!!!!!!!!
    // HERE WOULD BE A GREAT PLACE FOR AN IDIOT TEST:
  // REJECT SIMULATION NODES THAT ARE OFF SCREEN!!!!!!!!!!

	LOGIC_AFTER_TICK( [this, sim]() { addSeismicSimulation( sim ); } );
	LOGIC_CENSUS( CENSUS_TERRAINVISUAL );
  m_seismicSimulationList.push_back( sim );
}



void W3DTerrainVisual::handleSeismicSimulations( void )
{
  if ( ! m_clientHeightMap || ! m_logicHeightMap || ! m_terrainRenderObject )
    return;


  if ( ! m_seismicSimulationList.empty() )
  {
    SeismicSimulationListIt it = m_seismicSimulationList.begin();
    
    
    m_clientHeightMap->clearSeismicUpdateFlags();


    while ( it != m_seismicSimulationList.end() )
	  {
      SeismicSimulationNode *ssn = &*it;

      if ( ssn )
      {
        SeismicSimulationFilterBase::SeismicSimStatusCode code = ssn->handleFilterCallback( m_clientHeightMap );
        DEBUG_ASSERTCRASH( code != SeismicSimulationFilterBase::SEISMIC_STATUS_INVALID, ("Trouble in the Seismic simulator.") );
      
        switch ( code ) 
        {
          case SeismicSimulationFilterBase::SEISMIC_STATUS_ACTIVE:
          {
        	  break;
          }
          case SeismicSimulationFilterBase::SEISMIC_STATUS_ZERO_ENERGY:
          {
        	  break;
          }
        }

        Int border = m_clientHeightMap->getBorderSizeInline();

        // Now we apply some gravity to the dirt, so it falls back to its "original" height
        UnsignedInt fallCount = 0;
        for (Int x = border+ssn->m_region.lo.x; x < border+ssn->m_region.hi.x; ++x)
        {
          for (Int y = border+ssn->m_region.lo.y; y < border+ssn->m_region.hi.y; ++y)
          {
            if ( ! m_clientHeightMap->getSeismicUpdateFlag( x, y ) )
            {
              UnsignedByte heightOfOriginal = m_logicHeightMap->getHeight( x, y ); // LOGIC, YES DEFINITELY THE LOGIC

              Real oldSpeed = m_clientHeightMap->getSeismicZVelocity( x, y );
              Real newSpeed = ssn->applyGravity( oldSpeed );// - 0.5f;
              
              m_clientHeightMap->setSeismicZVelocity( x, y, newSpeed );

              Int heightToUse = m_clientHeightMap->getHeight( x, y ) + newSpeed ;


              if (heightToUse <= heightOfOriginal)
              {
                heightToUse = heightOfOriginal;
                m_clientHeightMap->setSeismicZVelocity( x, y, 0.0f ); //poof! the dirt hit ground level so stop "falling"
              }
              else
              {
                ++fallCount;
                
                if ( heightToUse > 255 )
                  heightToUse = 255;

              }
    			    m_clientHeightMap->setRawHeight( x, y, heightToUse ); 
              m_clientHeightMap->setSeismicUpdateFlag( x, y, TRUE );
            }
            
          }
        }

        if ( fallCount == 0 )
          ssn->m_clean = TRUE;

      }

      ++it;

	  }
  }





}

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void W3DTerrainVisual::updateSeismicSimulations( void )
{

	if (m_logicHeightMap==NULL) 
		return;

	if (m_clientHeightMap==NULL) 
		return;

  if (m_terrainRenderObject==NULL)
    return;

  if ( ! m_seismicSimulationList.empty() )
  {
    SeismicSimulationListIt it = m_seismicSimulationList.begin();

    // First we run through the list and do our business for each region

	  while ( it != m_seismicSimulationList.end() )
	  {
      SeismicSimulationNode *hur = &*it;
      if ( hur )
      {
        Int border = m_clientHeightMap->getBorderSizeInline();

		    TheTerrainRenderObject->updateBlock(
          hur->m_region.lo.x + border, 
          hur->m_region.lo.y + border, 
          hur->m_region.hi.x + border, 
          hur->m_region.hi.y + border, 
          m_clientHeightMap, 
          0);
      }

      ++it;

	  }
    // Then we check to see if these need to get erased from the list
    it = m_seismicSimulationList.begin();
	  while ( it != m_seismicSimulationList.end() )
	  {
      SeismicSimulationNode *hur = &*it;
      if ( hur->m_clean )
      {
		    it = m_seismicSimulationList.erase( it );
      }
      else
        ++it; 

	  }

  }
}


#endif //#defined DO_SEISMIC_SIMULATIONS
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



//-------------------------------------------------------------------------------------------------
/** load method for W3D visual terrain */
//-------------------------------------------------------------------------------------------------
Bool W3DTerrainVisual::load( AsciiString filename )
{
	
#if 0	
	// (gth) Testing exclusion list asset releasing
	DynamicVectorClass<StringClass> exclusion_list(8000);
	
	WW3DAssetManager::Get_Instance()->Create_Asset_List(exclusion_list);

	exclusion_list.Add(StringClass("avcomanche"));
	exclusion_list.Add(StringClass("avcomanche_d"));
	exclusion_list.Add(StringClass("ptdogwood08"));
	exclusion_list.Add(StringClass("ptdogwood01_b"));
	exclusion_list.Add(StringClass("ptpalm01"));
	exclusion_list.Add(StringClass("ptpalm01_b"));
	exclusion_list.Add(StringClass("avhummer"));
	exclusion_list.Add(StringClass("avhummer_d"));
	exclusion_list.Add(StringClass("avleopard"));
	exclusion_list.Add(StringClass("avleopard_d"));

	WW3DAssetManager::Get_Instance()->Free_Assets_With_Exclusion_List(exclusion_list);
#endif

	// enhancing functionality specific for W3D terrain
	if( TerrainVisual::load( filename ) == FALSE )
		return FALSE;  // failed

	// open the terrain file
	CachedFileInputStream fileStrm;
	if( !fileStrm.open(filename) )
	{

		REF_PTR_RELEASE( m_terrainRenderObject );
		return FALSE;

	}  // end if

	if( m_terrainRenderObject == NULL )
		return FALSE;


  ChunkInputStream *pStrm = &fileStrm;

  // allocate new height map data to read from file
  REF_PTR_RELEASE( m_logicHeightMap );
	m_logicHeightMap = NEW WorldHeightMap(pStrm);




#ifdef DO_SEISMIC_SIMULATIONS

  fileStrm.close();
  fileStrm.open(filename);
  pStrm = &fileStrm;

	REF_PTR_RELEASE( m_clientHeightMap );
  m_clientHeightMap = NEW WorldHeightMap( pStrm );

#endif








	// Add any lights loaded by map.
	MapObject *pMapObj = MapObject::getFirstMapObject();
	while (pMapObj) 
	{
		Dict *d = pMapObj->getProperties();
		if (pMapObj->isLight()) 
		{ 
			Coord3D loc = *pMapObj->getLocation();
			if (loc.z < 0) {
				Vector3 vec;
				loc.z = m_terrainRenderObject->getHeightMapHeight(loc.x, loc.y, NULL);
				loc.z += d->getReal(TheKey_lightHeightAboveTerrain);
			}
			// It is a light, and handled at the device level.  jba.
			LightClass* lightP = NEW_REF(LightClass, (LightClass::POINT));

			RGBColor c;
			c.setFromInt(d->getInt(TheKey_lightAmbientColor));
			lightP->Set_Ambient( Vector3( c.red, c.green, c.blue ) );

			c.setFromInt(d->getInt(TheKey_lightDiffuseColor));
			lightP->Set_Diffuse( Vector3(  c.red, c.green, c.blue) );

			lightP->Set_Position(Vector3(loc.x, loc.y, loc.z));

			lightP->Set_Far_Attenuation_Range(d->getReal(TheKey_lightInnerRadius), d->getReal(TheKey_lightOuterRadius));
 			W3DDisplay::m_3DScene->Add_Render_Object(lightP);
			REF_PTR_RELEASE( lightP );
		}
		pMapObj = pMapObj->getNext();
	}


	RefRenderObjListIterator *it = W3DDisplay::m_3DScene->createLightsIterator();
	// apply the heightmap to the terrain render object

#ifdef DO_SEISMIC_SIMULATIONS
	m_terrainRenderObject->initHeightData( m_clientHeightMap->getDrawWidth(), 
																				 m_clientHeightMap->getDrawHeight(),
																				 m_clientHeightMap,
																				 it);
#else
	m_terrainRenderObject->initHeightData( m_logicHeightMap->getDrawWidth(), 
																				 m_logicHeightMap->getDrawHeight(),
																				 m_logicHeightMap,
																				 it);
#endif


	if (it) {
	 W3DDisplay::m_3DScene->destroyLightsIterator(it);
	 it = NULL;
	}
	// add our terrain render object to the scene
	W3DDisplay::m_3DScene->Add_Render_Object( m_terrainRenderObject );

#if defined _DEBUG || defined _INTERNAL
	// Icon drawing utility object for pathfinding.
	W3DDebugIcons *icons = NEW W3DDebugIcons;
 	W3DDisplay::m_3DScene->Add_Render_Object( icons );
	icons->Release_Ref(); // belongs to scene.
#endif

#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("********************* WARNING- Doing UNIT TIMINGS. ")
#else
	if (m_waterRenderObject && W3DDisplay::m_3DScene)
	{
		W3DDisplay::m_3DScene->Add_Render_Object( m_waterRenderObject);
		m_waterRenderObject->enableWaterGrid(false);
		m_waterRenderObject->updateMapOverrides();
	}
#endif

	pMapObj = MapObject::getFirstMapObject();
	while (pMapObj) 
	{
		Dict *d = pMapObj->getProperties();
		if (pMapObj->isScorch()) {
			const Coord3D *pos = pMapObj->getLocation();
			Vector3 loc(pos->x, pos->y, pos->z);
			Real radius = d->getReal(TheKey_objectRadius);
			Scorches type = (Scorches)d->getInt(TheKey_scorchType);
			m_terrainRenderObject->addScorch(loc, radius, type);
		}
		pMapObj = pMapObj->getNext();
	}

	// Everything on the scorch list came out of the map file. Say so: the list has a fixed size and
	// makes room by dropping its oldest entry, and the oldest entries are these - so a long match
	// used to rub out the map's own craters one at a time.
	m_terrainRenderObject->markScorchesStatic();

	// reset water render object if present
	if( m_waterRenderObject )
	{
		m_waterRenderObject->load();
	}

	return TRUE;  // success

}  // end load

// Final terrain and authored/saved water are available at this hook.
void W3DTerrainVisual::prewarmMapRenderData( void )
{
	if (!TheGlobalData || TheGlobalData->m_noRenderDevice || !m_waterRenderObject ||
		!TheGameLogic || TheGameLogic->isInShellGame()) return;
	m_waterRenderObject->prewarmStaticMapData();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::enableWaterGrid( Bool enable )
{

	// the flag getWaterGridHeight answered to is TerrainLogic's WaterGrid's now; the render object
	// still has buffers and river flags to see to
	if( m_waterRenderObject )
		m_waterRenderObject->enableWaterGrid( enable );

}  // end enableWaterGrid

//-------------------------------------------------------------------------------------------------
/** intersect the ray with the terrain, if a hit occurs TRUE is returned
	* and the result point on the terrain is returned in "result" */
//-------------------------------------------------------------------------------------------------
Bool W3DTerrainVisual::intersectTerrain( Coord3D *rayStart, 
																				 Coord3D *rayEnd, 
																				 Coord3D *result )
{
	Bool hit = FALSE;

	// sanity
	if( rayStart == NULL || rayEnd == NULL )
		return hit;

	if( m_terrainRenderObject )
	{
		CastResultStruct res;
		LineSegClass lineSeg( Vector3( rayStart->x, rayStart->y, rayStart->z ),
													Vector3( rayEnd->x, rayEnd->y, rayEnd->z ) );
		RayCollisionTestClass rayTest( lineSeg, &res );

		hit = m_terrainRenderObject->Cast_Ray( rayTest );
		if( hit && result )
		{
			Vector3 point = rayTest.Result->ContactPoint;

			result->x = point.X;
			result->y = point.Y;
			result->z = point.Z;	

		}  // end if
		
	}  // end if

	// return hit result
	return hit;

}  // end intersectTerrain

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTerrainVisual::getTerrainColorAt( Real x, Real y, RGBColor *pColor )
{

#ifdef DO_SEISMIC_SIMULATIONS
  if( m_clientHeightMap )
		m_clientHeightMap->getTerrainColorAt( x, y, pColor );
#else
  if( m_logicHeightMap )
		m_logicHeightMap->getTerrainColorAt( x, y, pColor );
#endif

}  // end getTerrainColorAt

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
TerrainType *W3DTerrainVisual::getTerrainTile( Real x, Real y )
{
	TerrainType *tile = NULL;


#ifdef DO_SEISMIC_SIMULATIONS
	if( m_clientHeightMap )
	{
		AsciiString tileName = m_clientHeightMap->getTerrainNameAt( x, y );
		tile = TheTerrainTypes->findTerrain( tileName );
	}  // end if
#else
  if( m_logicHeightMap )
	{
		AsciiString tileName = m_logicHeightMap->getTerrainNameAt( x, y );
		tile = TheTerrainTypes->findTerrain( tileName );
	}  // end if
#endif




	return tile;

}  // end getTerrainTile

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setRawMapHeight(const ICoord2D *gridPos, Int height)
{
  // This method writes to the m_logicHeightMap member,
  // since m_logicHeightMap is the true, golden standard to which m_clientHeightMap
  // interpolates during or after its Seismic simulation displaces it..
  // THIS IS TRUE ONLY WHEN DO_SEISMIC_SIMULATIONS is defined M Lorenzen, 8/23/03

	if (m_logicHeightMap) 
  {
		Int x = gridPos->x+m_logicHeightMap->getBorderSizeInline();
		Int y = gridPos->y+m_logicHeightMap->getBorderSizeInline();
 		//if (m_logicHeightMap->getHeight(x,y) != height) //ML changed to prevent scissoring with roads
 		if (m_terrainRenderObject->getLogicRawHeight(x,y) > height)
		{
			// logic's own height, read back by getGroundHeight, so now; the drawn map after the tick, which
			// the render reads beside it (LOGIC-THREAD.md phase 3, LogicHeightMapData)
			m_terrainRenderObject->setLogicRawHeight(x, y, height);
			LogicThread::callAfterTick( [this, x, y, height]() {
				m_logicHeightMap->setRawHeight(x, y, height);
				/* This used to be staticLightingChanged(), which rebuilds and re-lights every drawn
					 vertex on the map - 61 ms, measured, a visible stutter - because one cell moved.  EA
					 had noticed: "OOH! this could benefit from the new Seismic update code".  It gets a
					 partial rebuild of the cell's neighbourhood instead. */
				m_terrainRenderObject->terrainHeightChanged(x, y);

#ifdef DO_SEISMIC_SIMULATIONS
				if ( m_clientHeightMap )
				{
					if ( height < m_clientHeightMap->getHeight( x,y ) )
						m_clientHeightMap->setRawHeight( x, y, height ); // if the client map is heigher than this height, it will fall down to it anyway!
				}
#endif
			} );
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Int W3DTerrainVisual::getRawMapHeight(const ICoord2D *gridPos)
{
	if (m_logicHeightMap) 
  {
		Int x = gridPos->x+m_logicHeightMap->getBorderSizeInline();
		Int y = gridPos->y+m_logicHeightMap->getBorderSizeInline();
 		//if (m_logicHeightMap->getHeight(x,y) != height) //ML changed to prevent scissoring with roads
    return m_terrainRenderObject->getLogicRawHeight(x,y) ;
	}
  return 0;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::addFactionBibDrawable(Drawable *factionBuilding, Bool highlight, Real extra)
{
#ifdef DO_SEISMIC_SIMULATIONS 
	if (m_clientHeightMap)
#else
	if (m_logicHeightMap)
#endif
  {
		const Matrix3D * mtx = factionBuilding->getTransformMatrix();
		Vector3 corners[4];
		Coord3D pos;
		pos.set(0,0,0);
		Real exitWidth = factionBuilding->getTemplate()->getFactoryExitWidth();
		Real extraWidth = factionBuilding->getTemplate()->getFactoryExtraBibWidth() + extra;
		const GeometryInfo info = factionBuilding->getTemplate()->getTemplateGeometryInfo();
		Real sizeX = info.getMajorRadius();
		Real sizeY = info.getMinorRadius();
		if (info.getGeomType() != GEOMETRY_BOX) {
			sizeY = sizeX;
		}
		corners[0].Set(pos.x, pos.y, pos.z);
		corners[0].X -= sizeX+extraWidth;
		corners[0].Y -= sizeY+extraWidth;
		corners[1].Set(pos.x, pos.y, pos.z);
		corners[1].X += sizeX+exitWidth+extraWidth;
		corners[1].Y -= sizeY+extraWidth;
		corners[2].Set(pos.x, pos.y, pos.z);
		corners[2].X += sizeX+exitWidth+extraWidth;
		corners[2].Y += sizeY+extraWidth;
		corners[3].Set(pos.x, pos.y, pos.z);
		corners[3].X -= sizeX+extraWidth;
		corners[3].Y += sizeY+extraWidth;
		mtx->Transform_Vector(*mtx, corners[0], &corners[0]);
		mtx->Transform_Vector(*mtx, corners[1], &corners[1]);
		mtx->Transform_Vector(*mtx, corners[2], &corners[2]);
		mtx->Transform_Vector(*mtx, corners[3], &corners[3]);
		m_terrainRenderObject->addTerrainBibDrawable(corners, factionBuilding->getID(), highlight);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::addFactionBib(Object *factionBuilding, Bool highlight, Real extra)
{
	if( LogicThread::defersEffects() )
	{
		// the AI's placement tests: by ID, since the tick may destroy the building first
		const ObjectID id = factionBuilding->getID();
		LogicThread::callAfterTick( [this, id, highlight, extra]() {
			Object *live = TheGameLogic->findObjectByID( id );
			if( live )
				addFactionBib( live, highlight, extra );
		} );
		return;
	}
	LOGIC_CENSUS( CENSUS_TERRAINVISUAL );
#ifdef DO_SEISMIC_SIMULATIONS
	if (m_clientHeightMap) 
#else
	if (m_logicHeightMap) 
#endif
  {
		const Matrix3D * mtx = factionBuilding->getTransformMatrix();
		Vector3 corners[4];
		Coord3D pos;
		pos.set(0,0,0);
		Real exitWidth = factionBuilding->getTemplate()->getFactoryExitWidth();
		Real extraWidth = factionBuilding->getTemplate()->getFactoryExtraBibWidth() + extra;
		const GeometryInfo info = factionBuilding->getGeometryInfo();
		Real sizeX = info.getMajorRadius();
		Real sizeY = info.getMinorRadius();
		if (info.getGeomType() != GEOMETRY_BOX) {
			sizeY = sizeX;
		}
		corners[0].Set(pos.x, pos.y, pos.z);
		corners[0].X -= sizeX+extraWidth;
		corners[0].Y -= sizeY+extraWidth;
		corners[1].Set(pos.x, pos.y, pos.z);
		corners[1].X += sizeX+exitWidth+extraWidth;
		corners[1].Y -= sizeY+extraWidth;
		corners[2].Set(pos.x, pos.y, pos.z);
		corners[2].X += sizeX+exitWidth+extraWidth;
		corners[2].Y += sizeY+extraWidth;
		corners[3].Set(pos.x, pos.y, pos.z);
		corners[3].X -= sizeX+extraWidth;
		corners[3].Y += sizeY+extraWidth;
		mtx->Transform_Vector(*mtx, corners[0], &corners[0]);
		mtx->Transform_Vector(*mtx, corners[1], &corners[1]);
		mtx->Transform_Vector(*mtx, corners[2], &corners[2]);
		mtx->Transform_Vector(*mtx, corners[3], &corners[3]);
		m_terrainRenderObject->addTerrainBib(corners, factionBuilding->getID(), highlight);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeFactionBibDrawable(Drawable *factionBuilding)
{
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeTerrainBibDrawable(factionBuilding->getID());
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeFactionBib(Object *factionBuilding)
{
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeTerrainBib(factionBuilding->getID());
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeAllBibs(void)
{
	LOGIC_AFTER_TICK( [this]() { removeAllBibs(); } );
	LOGIC_CENSUS( CENSUS_TERRAINVISUAL );
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeAllTerrainBibs();
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeBibHighlighting(void)
{
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeTerrainBibHighlighting();
	}
}
 
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::removeTreesAndPropsForConstruction(const Coord3D* pos, 
																				const GeometryInfo& geom,
																				Real angle)
{
	if( LogicThread::defersEffects() )
	{
		const Coord3D at = *pos;
		LogicThread::callAfterTick( [this, at, geom, angle]() { removeTreesAndPropsForConstruction( &at, geom, angle ); } );
		return;
	}
	LOGIC_CENSUS( CENSUS_TERRAINVISUAL );
	if (m_terrainRenderObject) {
		m_terrainRenderObject->removeTreesAndPropsForConstruction(pos, geom, angle);
	}
}
 
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::addProp(const ThingTemplate *tTemplate, const Coord3D *pos, Real angle)
{
	ModelConditionFlags state;
	state.clear();
	if (TheGlobalData->m_weather == WEATHER_SNOWY)
	{
		state.set(MODELCONDITION_SNOW);
	}
	if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
	{
		state.set(MODELCONDITION_NIGHT);
	}
	AsciiString modelName;
	Real scale = tTemplate->getAssetScale();
	const ModuleInfo& mi = tTemplate->getDrawModuleInfo();
	if (mi.getCount() > 0)
	{
		const ModuleData* mdd = mi.getNthData(0);
		const W3DModelDrawModuleData* md = mdd ? mdd->getAsW3DModelDrawModuleData() : NULL;
		if (md)
		{
			modelName = md->getBestModelNameForWB(state);
		}
	}
	if (m_terrainRenderObject && modelName.isNotEmpty()) {
		m_terrainRenderObject->addProp(1, *pos, angle, scale, modelName);
	}
}
 
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setTerrainTracksDetail(void)
{
	if (TheTerrainTracksRenderObjClassSystem)
		TheTerrainTracksRenderObjClassSystem->setDetail();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::setShoreLineDetail(void)
{
	if (m_terrainRenderObject) 
		m_terrainRenderObject->setShoreLineDetail();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/// Replace the skybox texture
void W3DTerrainVisual::replaceSkyboxTextures(const AsciiString *oldTexName[5], const AsciiString *newTexName[5])
{
	if (m_waterRenderObject)
	{
		for (Int i=0; i<5; i++)
		{
			//check if this texture was never changed before and is still using the default art.
			if (m_initialSkyboxTexNames[i].isEmpty())
			{	m_initialSkyboxTexNames[i]=*oldTexName[i];
				m_currentSkyboxTexNames[i]=*oldTexName[i];
			}

			if (m_currentSkyboxTexNames[i] != *newTexName[i])
			{	m_waterRenderObject->replaceSkyboxTexture(m_currentSkyboxTexNames[i], *newTexName[i]);
				m_currentSkyboxTexNames[i]=*newTexName[i];	//update current state to new texture
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::crc( Xfer *xfer )
{

	// extend base class
	TerrainVisual::crc( xfer );

}  // end CRC

// ------------------------------------------------------------------------------------------------
/** Xfer
	* Version Info:
	* 1: Initial version 
	* 2: Add height map heights.
	* 3: Add client side trees & props. jba.
*/
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	TerrainVisual::xfer( xfer );

	// flag for whether or not the water grid is enabled; the flag and the grid are TerrainLogic's
	// now, written here, in this block and in these bytes, as they were from the render object
	WaterGrid *waterGrid = TheTerrainLogic->getWaterGrid();
	Bool gridEnabled = waterGrid->isHeightEnabled();
	xfer->xferBool( &gridEnabled );
	if( gridEnabled != waterGrid->isHeightEnabled() )
	{

		DEBUG_CRASH(( "W3DTerrainVisual::xfer - m_isWaterGridRenderingEnabled mismatch\n" ));
		throw SC_INVALID_DATA;

	}  // end if

	// xfer grid data if enabled
	if( gridEnabled )
		xfer->xferSnapshot( waterGrid );

/*
	{

		// grid width and height
		Int width = getGridWidth();
		Int height = getGridheight();
		xfer->xferInt( &width );
		xfer->xferInt( &height );
		if( width != getGridWidth() )
		{

			DEBUG_CRASH(( "W3DTerainVisual::xfer - grid width mismatch '%d' should be '%d'\n",
										width, getGridWidth() ));
			throw SC_INVALID_DATA;

		}  // end if
		if( height != getGridHeight() )
		{

			DEBUG_CRASH(( "W3DTerainVisual::xfer - grid height mismatch '%d' should be '%d'\n",
										height, getGridHeight() ));
			throw SC_INVALID_DATA;

		}  // end if

		// write data for each grid

	}  // end if
*/

	// Write out the terrain height data.
	if (version >= 2) {
		UnsignedByte *data = m_logicHeightMap->getDataPtr();
		Int len = m_logicHeightMap->getXExtent()*m_logicHeightMap->getYExtent();
		Int xferLen = len;
		xfer->xferInt(&xferLen);
		if (len!=xferLen) {
			DEBUG_CRASH(("Bad height map length."));
			if (len>xferLen) {
				len = xferLen;
			}
		}
		xfer->xferUser(data, len);	
		if (xfer->getXferMode() == XFER_LOAD)	
    {	
			// Update the display height map.
			m_terrainRenderObject->heightsLoaded();
		}
	}

	if (version >= 3) {
		xfer->xferSnapshot(m_terrainRenderObject);
	}



         // XFER //
         // X  R //
         // X  R //
         // XFER //
 /////   /USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP
//   //  /USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP
//    ////USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP----------------------------
//   //  /USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP                            ^
 /////   /USE CLIENT HEIGHT MAPCLIENT HEIGHT MAP                            0
         // XFER //
         // X  R //
         // X  R //
         // XFER //





}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DTerrainVisual::loadPostProcess( void )
{

	// extend base class
	TerrainVisual::loadPostProcess();

}  // end loadPostProcess

