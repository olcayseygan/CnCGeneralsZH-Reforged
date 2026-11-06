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

// FILE: W3DWaypointBuffer.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   Command & Conquers: Generals
//
// File name: W3DWaypointBuffer.cpp
//
// Created:   Kris Morness, October 2002
//
// Desc:      Draw buffer to handle all the waypoints in the scene. Waypoints
//            are rendered after terrain, after roads & bridges, and after
//            global fog, but before structures, objects, units, trees, etc.
//            This way if we have two waypoints at the bottom of a hill but
//            going through the hill, the line won't get cut off. However, 
//            structures and units on top of paths will render above it. Waypoints
//            are only shown for selected units while in waypoint plotting mode.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------
#include "W3DDevice/GameClient/W3DWaypointBuffer.h"

#include <stdio.h>
#include <string.h>
#include <assetmgr.h>
#include <texture.h>

#include "Common/GlobalData.h"
#include "GameLogic/AIPathfind.h"		// Path/PathNode, to draw a plain move order's route
#include "Common/RandomValue.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"

#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"

#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

#include "GameLogic/Module/AIUpdate.h"

#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/HeightMap.h"

#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/segline.h"


#define MAX_DISPLAY_NODES 512



#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


//=============================================================================
// W3DWaypointBuffer::W3DWaypointBuffer
//=============================================================================
/** Constructor. Sets m_initialized to true if it finds the w3d models it needs
for the bibs. */
//=============================================================================
W3DWaypointBuffer::W3DWaypointBuffer(void)
{
	m_waypointNodeRobj = WW3DAssetManager::Get_Instance()->Create_Render_Obj( "SCMNode" );
	m_line = new SegmentedLineClass;

	m_texture = WW3DAssetManager::Get_Instance()->Get_Texture( "EXLaser.tga" );
	m_linesUsed = 0;


  setDefaultLineStyle();
}

//=============================================================================
// W3DWaypointBuffer::~W3DWaypointBuffer
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
W3DWaypointBuffer::~W3DWaypointBuffer(void)
{
	REF_PTR_RELEASE( m_waypointNodeRobj );
	REF_PTR_RELEASE( m_texture );
	REF_PTR_RELEASE( m_line );
	for( size_t i = 0; i < m_linePool.size(); ++i )
		REF_PTR_RELEASE( m_linePool[ i ] );
}

//=============================================================================
// A SegmentedLineClass renders through the static sort list (deferred to the end of the
// scene, drawn over the buildings with PASS_ALWAYS), but every WW3D::Render() of a puck
// flushes that list. Rendering one shared line per selected object therefore drew all but
// the last line during the terrain pass, where the building meshes then covered the piece
// coming out of the door. So each line gets its own object and they are rendered together
// after the last puck, so they all stay deferred.
//=============================================================================
void W3DWaypointBuffer::queueLine( Int numPoints, Vector3 *points )
{
	if( m_linesUsed >= (Int)m_linePool.size() )
		m_linePool.push_back( new SegmentedLineClass( *m_line ) );	// copies texture, shader, width, color
	m_linePool[ m_linesUsed++ ]->Set_Points( numPoints, points );
}

void W3DWaypointBuffer::renderQueuedLines( RenderInfoClass &rinfo )
{
	for( Int i = 0; i < m_linesUsed; ++i )
		m_linePool[ i ]->Render( rinfo );
	m_linesUsed = 0;
}

//=============================================================================
// W3DWaypointBuffer::freeBibBuffers
//=============================================================================
/** Frees the index and vertex buffers. */
//=============================================================================
void W3DWaypointBuffer::freeWaypointBuffers()
{
}


void W3DWaypointBuffer::setDefaultLineStyle( void )
{
	if( m_texture )
	{
		m_line->Set_Texture( m_texture );
	}
	ShaderClass lineShader=ShaderClass::_PresetAdditiveShader;
	lineShader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	m_line->Set_Shader( lineShader );	//pick the alpha blending mode you want - see shader.h for others.
	m_line->Set_Width( 1.5f );
	m_line->Set_Color( Vector3( 0.25f, 0.5f, 1.0f ) );
	m_line->Set_Texture_Mapping_Mode( SegLineRendererClass::TILED_TEXTURE_MAP );	//this tiles the texture across the line
}


void W3DWaypointBuffer::record( DrawKind kind, Bool orderHint, Int count, const Vector3 *points )
{
	WaypointDraw draw;
	draw.kind = kind;
	draw.orderHint = orderHint;
	draw.first = (Int)m_drawPoints.size();
	draw.count = count;
	m_draws.push_back( draw );
	m_drawPoints.insert( m_drawPoints.end(), points, points + count );
}

//=============================================================================
// W3DWaypointBuffer::drawWaypoints
//=============================================================================
/** Draws the pucks and lines takeReadsForRender took, in the order it took them. */
//=============================================================================
void W3DWaypointBuffer::drawWaypoints(RenderInfoClass &rinfo)
{
	setDefaultLineStyle();
	if( m_draws.empty() )
		return;

	//Create a default light environment with no lights and only full ambient.
	//@todo: Fix later by copying default scene light environement from W3DScene.cpp.
	LightEnvironmentClass lightEnv;
	lightEnv.Reset(Vector3(0,0,0), Vector3(1.0f,1.0f,1.0f));
	lightEnv.Pre_Render_Update(rinfo.Camera.Get_Transform());
	RenderInfoClass localRinfo(rinfo.Camera);
	localRinfo.light_environment=&lightEnv;

	// Not in the water's mirror pass: these are orders drawn for the player, even where Modern
	// reflects the ground, and a PASS_ALWAYS order line would lie on top of the water.
	const Bool mirror = ShaderClass::Is_Backface_Culling_Inverted();
	for( std::vector<WaypointDraw>::const_iterator it = m_draws.begin(); it != m_draws.end(); ++it )
	{
		if( it->orderHint && mirror )
			continue;
		Vector3 *points = &m_drawPoints[ it->first ];
		switch( it->kind )
		{
			case DRAW_PUCK:
				// an install whose W3D archives lack SCMNode.w3d (seen in a third-party repack) has no
				// puck; retail drew one only for a rally point, every order draws one now
				if( !m_waypointNodeRobj )
					break;
				m_waypointNodeRobj->Set_Position( points[ 0 ] );
				WW3D::Render( *m_waypointNodeRobj, localRinfo );		// the little hockey puck
				break;
			case DRAW_LINE:
				queueLine( it->count, points );
				break;
			case DRAW_OUTPOST_LINE:
				m_line->Set_Color( Vector3( 0.95f, 0.5f, 0.0f ) );
				m_line->Set_Width( 3.0f );
				m_line->Set_Points( it->count, points );
				m_line->Render( localRinfo );
				break;
		}
	}

	// every line goes out after the last puck: a puck's render flushes the static sort list (queueLine)
	if( m_linesUsed > 0 )
		renderQueuedLines( localRinfo );
}

//=============================================================================
// W3DWaypointBuffer::takeReadsForRender
//=============================================================================
/** LOGIC-THREAD.md phase 4: the selection's orders, rally points and exits read the objects and the
		ground, so they are read at the render's sync point and drawWaypoints draws what was taken. */
//=============================================================================
void W3DWaypointBuffer::takeReadsForRender(void)
{
	m_draws.clear();
	m_drawPoints.clear();

	// Retail drew its own white nodes-and-lines here for every selected unit's goal path while
	// isInWaypointMode() (shift held) was true. The shift queue now has its own per-order hint
	// threads (InGameUI::updateOrderHints), which cover the same ground and stay up after the key is
	// let go, so the white duplicate is gone.
	//
	// Every order the selection is on is drawn the way a building's rally point is drawn: this
	// line and a puck on each of its points, on the ground or on any unit or building it is aimed at.
	// The hints are rebuilt off the units every frame, so a puck on a target follows it.
	{
		const std::vector<InGameUI::OrderHint>& hints = TheInGameUI->getOrderHints();
		for( std::vector<InGameUI::OrderHint>::const_iterator it = hints.begin(); it != hints.end(); ++it )
		{
			if( it->mark == InGameUI::ORDER_MARK_NONE )
				continue;		// an upgrade or ability bought where the step before ends: no line, no point

			// an order's point can carry a zero height; nothing goes under the ground, and an aircraft
			// at either end keeps its own height
			Vector3 points[ 2 ];
			points[ 0 ].Set( it->from.x, it->from.y, max( it->from.z, TheTerrainLogic->getGroundHeight( it->from.x, it->from.y ) ) );
			points[ 1 ].Set( it->to.x, it->to.y, max( it->to.z, TheTerrainLogic->getGroundHeight( it->to.x, it->to.y ) ) );

			// Order Lines off in the options takes away a plain one-point order's line and the puck at
			// its end, on ground or on the unit it is aimed at. A unit with more than one place to go
			// (a shift queue, a waypoint path; step is numbered then) keeps its threads and pucks,
			// since that list is the thing being planned
			if( !TheGlobalData->m_showOrderLines && it->step == 0 && it->kind != InGameUI::ORDER_HINT_WAYPOINT )
				continue;

			record( DRAW_LINE, TRUE, 2, points );
			record( DRAW_PUCK, TRUE, 1, &points[ 1 ] );		// the end wears the puck the joints between orders do
		}
		// the lines stay queued: the rally and placement pucks below would flush them into the
		// terrain pass, under the buildings (see queueLine)
	}

	// Rally points, drawn whenever we are not actively laying down waypoints. This used to be the
	// else of the block above, which is why turning that block on full time hid every rally line.
	if( !TheInGameUI->isInWaypointMode() )
	{
		Vector3 points[ MAX_DISPLAY_NODES + 1 ]; //Lines have nodes + 1 points.

		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		Drawable *draw;
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			draw = *it;
			Object *obj = draw->getObject();

			Int numPoints = 0;
			if( obj )
			{
				if ( ! obj->isLocallyControlled())
					continue;



        // WAIT! before we go browsing the drawable list for buildings that want to draw their rally points
        // lets test for that very special case of having a listeningoutpost selected, and some enemy drawable moused-over
        if ( obj->isKindOf( KINDOF_REVEALS_ENEMY_PATHS ) )
        {
 
          DrawableID enemyID = TheInGameUI->getMousedOverDrawableID();
          Drawable *enemyDraw = TheGameClient->findDrawableByID( enemyID );
          if ( enemyDraw )
          {
            Object *enemy = enemyDraw->getObject();
            if ( enemy )
            {
              if ( enemy->getRelationship( obj ) == ENEMIES )
              {
                
                Coord3D delta = *obj->getPosition();
                delta.sub( enemy->getPosition() );
                if ( delta.length() <= obj->getVisionRange() ) // is listening outpost close enough to do this?
                {


                  //////////////////////////////////////////////////////////////////////                
                  AIUpdateInterface *ai = enemy->getAI();
				          Int goalSize = ai ? ai->friend_getWaypointGoalPathSize() : 0;
				          Int gpIdx = ai ? ai->friend_getCurrentGoalPathIndex() : 0;
                  if( ai ) 
                  {
                    Bool lineExists = FALSE;

					          const Coord3D *pos = enemy->getPosition();
					          points[ numPoints++ ].Set( Vector3( pos->x, pos->y, pos->z ) );

                    if ( gpIdx >= 0 && gpIdx < goalSize )// Ooh, the enemy is in waypoint mode
				            {

					            for( int i = gpIdx; i < goalSize; i++ )
					            {
						            const Coord3D *waypoint = ai->friend_getGoalPathPosition( i );
						            if( waypoint )
						            {
							            //Render line from previous point to current node.

							            if( numPoints < MAX_DISPLAY_NODES + 1 )
							            {
								            points[ numPoints++ ].Set( Vector3( waypoint->x, waypoint->y, waypoint->z ) );
							            }

							            Vector3 puck( waypoint->x, waypoint->y, waypoint->z );
							            record( DRAW_PUCK, FALSE, 1, &puck );
                          lineExists = TRUE;
						            }
					            }
                    }
                    else // then enemy may be moving to a goal position
                    {
                      const Coord3D *destinationPoint = ai->getGoalPosition();
                      if ( destinationPoint->length() > 1.0f )
                      {
								        points[ numPoints++ ].Set( Vector3( destinationPoint->x, destinationPoint->y, destinationPoint->z ) );
							          record( DRAW_PUCK, FALSE, 1, &points[ numPoints - 1 ] );
                        lineExists = TRUE;
                      }
                    }

                    if ( lineExists )
                    {
					            //Now render the lines in one pass!
					            record( DRAW_OUTPOST_LINE, FALSE, numPoints, points );
                    }
                  }
                  //////////////////////////////////////////////////////////////////////                




                }
              }
            }
          }

          break;// dont even bother with the rest, since this one listening outpost satisfies the single path-line limit
        }




				ExitInterface *exitInterface = obj->getObjectExitInterface();
				if( exitInterface )
				{

					Coord3D exitPoint;
					if ( ! exitInterface->getExitPosition(exitPoint))
						exitPoint = *obj->getPosition();
						
					points[ numPoints ].Set( Vector3( exitPoint.x, exitPoint.y, exitPoint.z ) );
					numPoints++;

					Bool boxWrap = TRUE;
					Coord3D naturalRallyPoint;
					if (exitInterface->getNaturalRallyPoint(naturalRallyPoint, FALSE))//FALSE means "without the extra offset" 
					{
						if( !naturalRallyPoint.equals( exitPoint ) )
						{
							points[ numPoints ].Set( Vector3( naturalRallyPoint.x, naturalRallyPoint.y, naturalRallyPoint.z ) );
							numPoints++;
						}
						else
						{
							//Helipad rally point -- so don't use box wrapping.
							boxWrap = FALSE;
						}
					}
					else 
						continue; //next drawable

					const Coord3D *rallyPoint = exitInterface->getRallyPoint();
					if( rallyPoint )
					{
						if( boxWrap )
						{

							//test to se whether the rally point flanks the side of the natural rally point
							//to do this we find the two corners of the geometry extents that are nearest the natural rally point
							// these define the natural rally point edge
							// an intermediate point should be inserted at the corner nearest the rally point
							const GeometryInfo& geom = obj->getGeometryInfo();
							const Coord3D *ctr = obj->getPosition();
							Coord3D NRPDelta;
							NRPDelta.x = naturalRallyPoint.x - exitPoint.x;
							NRPDelta.y = naturalRallyPoint.y - exitPoint.y;
							NRPDelta.z = 0.0f;


							//This is a quick idiot test to se whether the rally line needs to wrap the box at all
							Coord3D wayOutPoint = NRPDelta;
							wayOutPoint.normalize();
							wayOutPoint.scale( 99999.9f );
							Real wayOutLength = wayOutPoint.length();
							wayOutPoint.add(&naturalRallyPoint);


							//if the rallypoint is closer to the wayoutpoint than it is to the natural rally point then we definitely do not wrap
							Coord3D rallyToWayOutDelta = wayOutPoint;
							rallyToWayOutDelta.sub(rallyPoint);
							if ( (100.0f + rallyToWayOutDelta.length()) > wayOutLength) 
							{

								//if we passed the above idiot test, now lets be sure by testing the dotproduct of the rp against the wayoutpoint
								wayOutPoint.normalize();// a normal shooting straight out the door
								//next comes the delta between the NRP and the RP
								Coord3D NRPToRPDelta = naturalRallyPoint;
								NRPToRPDelta.sub(rallyPoint);
								NRPToRPDelta.normalize();
								Real dot = NRPToRPDelta.x * wayOutPoint.x + NRPToRPDelta.y * wayOutPoint.y;
								if (dot > 0)
								{

									Real angle = obj->getOrientation();
									Real c = (Real)cos(angle);
									Real s = (Real)sin(angle); 

									Coord3D NRPToCtrDelta;
									NRPToCtrDelta.x = naturalRallyPoint.x - ctr->x;
									NRPToCtrDelta.y = naturalRallyPoint.y - ctr->y;
									NRPToCtrDelta.x = 0.0f;

									Real exc = geom.getMajorRadius() * c;
									Real eyc = geom.getMinorRadius() * c;
									Real exs = geom.getMajorRadius() * s;
									Real eys = geom.getMinorRadius() * s;

									Coord2D corners[ 4 ];

									corners[0].x = ctr->x - exc - eys;
									corners[0].y = ctr->y + eyc - exs;
									corners[1].x = ctr->x + exc - eys;
									corners[1].y = ctr->y + eyc + exs;
									corners[2].x = ctr->x + exc + eys;
									corners[2].y = ctr->y - eyc + exs;
									corners[3].x = ctr->x - exc + eys;
									corners[3].y = ctr->y - eyc - exs;

									Coord2D *pNearElbow = NULL;//find the closest corner to the rallyPoint same end as door
									Coord2D *pFarElbow = NULL; //find the closest corner to the rallypoint away from door
									Coord2D *nearCandidate = NULL;
									Coord3D cornerToRPDelta, cornerToExitDelta;
									cornerToRPDelta.z = 0.0f;
									cornerToExitDelta.z = 0.0f;
									Real elbowDistanceNear = 99999.9f;
									Real elbowDistanceFar = 99999.9f;
									
									for (UnsignedInt cornerIndex = 0; cornerIndex < 4; ++ cornerIndex)
									{
										nearCandidate = &corners[cornerIndex];//for quicker array access
										cornerToExitDelta.x = exitPoint.x - nearCandidate->x;
										cornerToExitDelta.y = exitPoint.y - nearCandidate->y;
										cornerToExitDelta.normalize();

										dot = cornerToExitDelta.x * wayOutPoint.x + cornerToExitDelta.y * wayOutPoint.y;
										if ( dot < 0.0f )
										{
											cornerToRPDelta.x = rallyPoint->x - nearCandidate->x;
											cornerToRPDelta.y = rallyPoint->y - nearCandidate->y;
											if (cornerToRPDelta.length() < elbowDistanceNear)
											{
												elbowDistanceNear = cornerToRPDelta.length();
												pNearElbow = nearCandidate;
											}
										}
										else
										{
											cornerToRPDelta.x = rallyPoint->x - nearCandidate->x;
											cornerToRPDelta.y = rallyPoint->y - nearCandidate->y;
											if (cornerToRPDelta.length() < elbowDistanceFar)
											{
												elbowDistanceFar = cornerToRPDelta.length();
												pFarElbow = nearCandidate;
											}
										}



									}

									if (pNearElbow)//did we find a nearest corner?
									{
										points[ numPoints ].Set( Vector3( pNearElbow->x, pNearElbow->y, ctr->z ) );
										numPoints++;


										//and for that matter did we find a far side coner?
										if (pFarElbow)//did we find a nearest corner?
										{
											// but let's test the dot of the first elbow against the rally point to find out
											//whethet the rally point wraps around this one, too
											Coord3D firstElbowDelta;
											firstElbowDelta.x = naturalRallyPoint.x - pNearElbow->x;
											firstElbowDelta.y = naturalRallyPoint.y - pNearElbow->y;
											firstElbowDelta.z = 0.0f;
											firstElbowDelta.normalize();

											Coord3D firstToRPDelta;
											firstToRPDelta.x = pNearElbow->x - rallyPoint->x;
											firstToRPDelta.y = pNearElbow->y - rallyPoint->y;
											firstToRPDelta.z = 0.0f;
											firstToRPDelta.normalize();

											dot = firstToRPDelta.x * firstElbowDelta.x + firstToRPDelta.y * firstElbowDelta.y;
											if (dot < 0)// we have a second elbow
											{
												points[ numPoints ].Set( Vector3( pFarElbow->x, pFarElbow->y, ctr->z ) );
												numPoints++;
											}
										}

									}

								}

							}
						}

						// Finally draw the line out to the RallyPoint
						points[ numPoints ].Set( Vector3( rallyPoint->x, rallyPoint->y, rallyPoint->z ) );
						numPoints++;

					}
					else
						continue;

					record( DRAW_LINE, FALSE, numPoints, points );

				}// end if exit interface
				
			}
		}

	}

	//
	// While a structure rides the cursor waiting to be placed, show where its units will come out.
	// Siting a war factory is a decision about which way the tanks face when they roll off the
	// line, and until now that was invisible until the building was finished and selected. The two
	// points come off the template's production exit module - there is no Object yet, so there is no
	// ExitInterface to ask - and the ghost's own transform carries them into world space, so the
	// line swings around with the building as the player wheels it.
	//
	const Drawable *placeIcon = TheInGameUI->getPendingPlaceDrawable();
	if( placeIcon )
	{
		Coord3D createPoint, naturalRallyPoint;
		if( TheInGameUI->getPendingPlaceType()->getProductionExitPointsInModelSpace( createPoint, naturalRallyPoint ) )
		{
			const Matrix3D *transform = placeIcon->getTransformMatrix();

			Vector3 exitLoc( createPoint.x, createPoint.y, createPoint.z );
			transform->Transform_Vector( *transform, exitLoc, &exitLoc );

			Vector3 rallyLoc( naturalRallyPoint.x, naturalRallyPoint.y, naturalRallyPoint.z );
			transform->Transform_Vector( *transform, rallyLoc, &rallyLoc );

			// a helipad puts both points in the same place; one puck, no line
			if( !(exitLoc == rallyLoc) )
			{
				record( DRAW_PUCK, FALSE, 1, &rallyLoc );	//The little hockey puck

				Vector3 points[ 2 ];
				points[ 0 ] = exitLoc;
				points[ 1 ] = rallyLoc;
				record( DRAW_LINE, FALSE, 2, points );
			}
		}
	}
}


