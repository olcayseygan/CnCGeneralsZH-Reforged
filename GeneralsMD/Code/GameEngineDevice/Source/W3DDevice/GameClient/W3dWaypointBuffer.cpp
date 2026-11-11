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

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <assetmgr.h>
#include <texture.h>

#include "Common/GlobalData.h"
#include "GameLogic/AIPathfind.h"		// Path/PathNode, to draw a plain move order's route
#include "Common/RandomValue.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"

#include "GameClient/CinemaDirector.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

#include "GameLogic/Module/AIUpdate.h"

#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DSmoothMotion.h"

#include "Platform/SunShadow.h"

#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/segline.h"
#include "WW3D2/sortingrenderer.h"


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
	m_ringWidthScale = 1.0f;


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
void W3DWaypointBuffer::queueLine( const WaypointDraw& draw, Vector3 *points )
{
	if( m_linesUsed >= (Int)m_linePool.size() )
	{
		m_linePool.push_back( new SegmentedLineClass( *m_line ) );
		m_linePoolKinds.push_back( DRAW_LINE );
	}
	m_linePoolKinds[ m_linesUsed ] = draw.kind;
	SegmentedLineClass *line = m_linePool[ m_linesUsed++ ];
	setLineStyle( line, draw.kind );		// a pooled line may have been an outline last frame
	if( draw.kind == DRAW_RING || draw.kind == DRAW_RING_GLOW )
	{
		line->Set_Color( draw.color );
		line->Set_Width( draw.width );
		line->Set_Opacity( draw.opacity );
	}
	// a fading attack pulse has no glow to beat and would leave its full depth behind it, a ring the
	// occlusion and the haze still see after it faded; only a line at full strength writes depth
	if( draw.kind == DRAW_RING && draw.opacity >= 1.0f )
	{
		// The sorted list orders a line by its world height, triangle by triangle, so nothing says the
		// glow lands before the line. The line writes depth and the glow lies a little further from the
		// eye (recordRingLine): drawn after, the glow fails the depth test wherever the line is; drawn
		// before, the line covers it. Either way the same picture, and both stay late on the sorted
		// list, over the decals, the water and the occlusion
		ShaderClass ringShader = ShaderClass::_PresetAlphaShader;
		ringShader.Set_Depth_Mask( ShaderClass::DEPTH_WRITE_ENABLE );
		line->Set_Shader( ringShader );
	}
	if( draw.kind == DRAW_RING_GLOW )
	{
		// the order line's soft laser across the width, added
		line->Set_Shader( ShaderClass::_PresetAdditiveShader );
		line->Set_Texture( m_texture );
		line->Set_Texture_Mapping_Mode( SegLineRendererClass::TILED_TEXTURE_MAP );
	}
	line->Set_Points( draw.count, points );
}

void W3DWaypointBuffer::renderQueuedLines( RenderInfoClass &rinfo )
{
	// a selection ring is paint like a building's outline, and takes no light either
	auto isOutline = [this]( Int i ) { return m_linePoolKinds[ i ] == DRAW_OUTLINE_WHITE || m_linePoolKinds[ i ] == DRAW_OUTLINE_RED || m_linePoolKinds[ i ] == DRAW_RING || m_linePoolKinds[ i ] == DRAW_RING_GLOW; };

	for( Int i = 0; i < m_linesUsed; ++i )
		if( !isOutline( i ) )
			m_linePool[ i ]->Render( rinfo );

	// A building's outline is a blended draw, and a receiving program would shade it with the
	// building's shadow, the smoke and the headlights where -d3d9 does not: grey white and brick red
	// on the shadow side.  It is paint for the player and takes no light.  Each guard covers one
	// backend and the sorted node keeps both from the moment it is inserted: the emissive flag is
	// what the Direct3D 11 backend reads (Shadow_Receiving), the receiver exclusion what SDL3 reads,
	// and on Direct3D 11 the exclusion is a stub
	{
		SunShadow::ReceiverExclusion sunReceivers( DX8Wrapper::_Get_D3D_Device() );
		SortingRendererClass::_Set_Insert_Emissive( true );
		for( Int i = 0; i < m_linesUsed; ++i )
			if( isOutline( i ) )
				m_linePool[ i ]->Render( rinfo );
		SortingRendererClass::_Set_Insert_Emissive( false );
	}
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
	setLineStyle( m_line, DRAW_LINE );
}

void W3DWaypointBuffer::setLineStyle( SegmentedLineClass *line, DrawKind kind )
{
	if( kind == DRAW_OUTLINE_WHITE || kind == DRAW_OUTLINE_RED || kind == DRAW_RING || kind == DRAW_RING_GLOW )
	{
		// A building's outline is solid paint, not the order line's additive laser: added to sand, red
		// came out a pale orange.  It is depth tested like anything else on the ground, so the building
		// hides the far side of its own outline, and it rides the terrain (traceStructureOutline) so a
		// hill does not swallow it.  Alpha blended to land on the sorted list, after the ground decals
		// round a building's base that would otherwise darken it
		line->Set_Texture( NULL );
		line->Set_Shader( ShaderClass::_PresetAlphaShader );
		line->Set_Width( 2.0f );
		line->Set_Color( ( kind == DRAW_OUTLINE_RED ) ? Vector3( 0.95f, 0.1f, 0.05f ) : Vector3( 1.0f, 1.0f, 1.0f ) );
		line->Set_Opacity( 0.9f );
		return;
	}

	line->Set_Texture( m_texture );
	ShaderClass lineShader=ShaderClass::_PresetAdditiveShader;
	lineShader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	line->Set_Shader( lineShader );	//pick the alpha blending mode you want - see shader.h for others.
	line->Set_Width( 1.5f );
	line->Set_Color( Vector3( 0.25f, 0.5f, 1.0f ) );
	line->Set_Opacity( 1.0f );
	line->Set_Texture_Mapping_Mode( SegLineRendererClass::TILED_TEXTURE_MAP );	//this tiles the texture across the line
}


void W3DWaypointBuffer::record( DrawKind kind, Bool orderHint, Int count, const Vector3 *points )
{
	WaypointDraw draw;
	draw.kind = kind;
	draw.orderHint = orderHint;
	draw.first = (Int)m_drawPoints.size();
	draw.count = count;
	draw.color.Set( 1.0f, 1.0f, 1.0f );
	draw.width = 2.0f;
	draw.opacity = 0.9f;
	m_draws.push_back( draw );
	m_drawPoints.insert( m_drawPoints.end(), points, points + count );
}

void W3DWaypointBuffer::recordRing( DrawKind kind, Int count, const Vector3 *points, const Vector3& color, Real width, Real opacity )
{
	record( kind, TRUE, count, points );
	m_draws.back().color = color;
	m_draws.back().width = width;
	m_draws.back().opacity = opacity;
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
			case DRAW_OUTLINE_WHITE:
			case DRAW_OUTLINE_RED:
			case DRAW_RING:
			case DRAW_RING_GLOW:
				queueLine( *it, points );
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

			// Order Lines off in the options takes away a plain one-point order's line and nothing else:
			// the puck at its end still shows where the order lands. A unit with more than one place to
			// go (a shift queue, a waypoint path; step is numbered then) keeps its threads, since that
			// list is the thing being planned
			if( TheGlobalData->m_showOrderLines || it->step > 0 || it->kind == InGameUI::ORDER_HINT_WAYPOINT )
				record( DRAW_LINE, TRUE, 2, points );

			record( DRAW_PUCK, TRUE, 1, &points[ 1 ] );		// the end wears the puck the joints between orders do
		}
		// the lines stay queued: the rally and placement pucks below would flush them into the
		// terrain pass, under the buildings (see queueLine)
	}

	takeRingView();

	// The outline round each selected building (InGameUI::updateStructureOutlines decides which): the
	// units' thin glowing line in its owner's colour, round its footprint with sharp corners
	// (recordStructureOutline). EA's red one round a building the selection is attacking is not drawn:
	// it only ever came of the local player's own attack orders, and the target's model flashing red
	// (Drawable_flashOrderTargets) says it now
	{
		const std::vector<InGameUI::StructureOutline>& outlines = TheInGameUI->getStructureOutlines();
		for( std::vector<InGameUI::StructureOutline>::const_iterator it = outlines.begin(); it != outlines.end(); ++it )
		{
			if( !it->selected )
				continue;
			const Object *obj = TheGameLogic->findObjectByID( it->id );
			if( obj != NULL )
				recordStructureOutline( obj );
		}
	}

	// the circle round every other selected unit, merged where they touch
	takeSelectionRings();

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

					// one just sent and not yet run by logic is drawn at once
					const Coord3D *rallyPoint = TheInGameUI->getPredictedRallyPoint( obj->getID() );
					if( rallyPoint == NULL )
						rallyPoint = exitInterface->getRallyPoint();
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

//=============================================================================
// Selection rings
//=============================================================================
// Every selected unit stands in a small circle on the ground, about its own body. Where circles
// overlap they are drawn as one outline, their union: each circle keeps only the arcs no other
// circle covers, cut where they cross, and the arcs are joined into closed loops, so five soldiers
// standing close wears one scalloped blob with its cusps pointing in, not five rings through each
// other.
//
// The line is the building outline's: a thin ribbon on the sorted list, depth tested so a unit's
// own body hides the part of its circle behind its feet, riding the terrain point by point. Its
// colour is the owner's pushed to full brightness (the brightest channel at one, the hue kept). A
// wider additive copy under it, through the order line's soft laser texture, is the glow; it lies a
// little behind the line, which writes depth, so the two read the same in either order (queueLine).
// The rim is sampled every world unit, fine enough that no facet shows close up. Widths and the lift follow the camera's
// height, so the line keeps about the same width on the screen at any zoom. A corner
// sharper than a right angle is bevelled, since the line mitres every corner without a limit, and a
// long loop is cut into pieces on straight stretches, since the line caps every 128 points.
// Buildings keep their rectangle outline.
//=============================================================================
static const Real RING_MARGIN = 1.0f;				///< ring radius over the unit's bounding circle
static const Real RING_MIN_RADIUS = 5.0f;
static const Real RING_MAX_RADIUS = 80.0f;
static const Real RING_STEP = 1.0f;					///< world units between two points of a ring
static const Int RING_MIN_SAMPLES = 48;
static const Real RING_LIFT = 1.0f;					///< over the ground at the default camera height, times the zoom scale
static const Real RING_MIN_LIFT = 0.6f;			///< never lower, or the ground pokes through between two points on a bump
static const Real RING_WIDTH = 0.3f;				///< the line at the default camera height: about two pixels
static const Real RING_OPACITY = 1.0f;
static const Real RING_GLOW_WIDTH = 1.4f;		///< the soft additive glow under it
static const Real RING_GLOW_STRENGTH = 0.5f;	///< the glow's colour, a share of the line's
static const Real RING_GLOW_BEHIND = 0.4f;	///< how far nearer the eye the line lies than its glow, times the zoom scale
static const Real RING_BEVEL_COS = -0.17f;	///< a corner turning further than this (about 100 degrees) is bevelled
static const Int RING_MAX_LINE_POINTS = 120;	///< under the line's 128-point chunk
static const Real RING_MIN_ZOOM_SCALE = 0.3f;	///< widths and the lift follow the camera's height, so a line keeps
static const Real RING_MAX_ZOOM_SCALE = 3.0f;	///< about the same width on the screen at any zoom, between these two
static const Real RING_JOIN_TOLERANCE = 0.25f;	///< two runs whose crossing points are this close are one corner of the outline

// an attack order's pulse: copies of the outline rising off the ground and fading (recordRingLoop)
static const Int PULSE_COUNT = 2;
static const Real PULSE_SECONDS = 0.5f;			///< one copy's rise
static const Real PULSE_GAP = 0.2f;					///< between two copies leaving the ground
static const Real PULSE_RISE = 14.0f;				///< world units a copy climbs
static const Real PULSE_OPACITY = 0.9f;			///< a copy's opacity as it leaves, falling to nothing

// a selected building's outline: its footprint grown by InGameUI's OUTLINE_MARGIN
static const Real STRUCTURE_MARGIN = 6.0f;

/** The selection lines' widths follow the camera's height, and the line sits nearer the eye than its
		glow; both read off the view at the sync point, before the building outlines and the rings. */
void W3DWaypointBuffer::takeRingView( void )
{
	m_ringWidthScale = MAX( RING_MIN_ZOOM_SCALE, MIN( RING_MAX_ZOOM_SCALE, TheTacticalView->getZoom() ) );
	const Coord3D& eye = TheTacticalView->get3DCameraPosition();
	m_ringCamera.Set( eye.x, eye.y, eye.z );
}

// ponytail: every circle is checked against every other once a frame to find its neighbours, O(n^2)
// over the selection; a few hundred units is a few ten thousand distance tests. A grid over the
// circles if a whole-army select ever shows up in a frame's cost.
void W3DWaypointBuffer::takeSelectionRings( void )
{
	m_circles.clear();
	if( CinemaDirector_hidesHud() )
		return;

	// A selected Angry Mob is drawn from its nexus: its members come from the sync point's copy whether
	// or not each is selected yet (a mob picked the frame it was made had none selected, and no rings),
	// and a member that is selected as well is left to it
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	m_selectedNexus.clear();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		if( (*it)->getObject() && (*it)->getObject()->isKindOf( KINDOF_MOB_NEXUS ) )
			m_selectedNexus.push_back( (*it)->getObject()->getID() );

	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		const Drawable *draw = *it;
		const Object *obj = draw->getObject();
		if( obj == NULL || obj->isEffectivelyDead() )
			continue;
		const Vector3 color = ringColor( obj->getControllingPlayer() );
		const Real pulse = Drawable_attackOrderSeconds( draw->getID() );		// an attack order's pulse
		// the nexus is invisible by design, so its own drawable's hidden flag says nothing: asked first,
		// it skipped the mob whose members were then left to it, and the mob wore no rings
		if( obj->isKindOf( KINDOF_MOB_NEXUS ) )
		{
			const MobMemberForDraw *members;
			const Int n = Drawable_mobMembersForDraw( obj->getID(), &members );
			for( Int m = 0; m < n; ++m )
			{
				const Real memberPulse = Drawable_attackOrderSeconds( members[ m ].drawable );
				const Real latest = ( pulse < 0.0f ) ? memberPulse : ( memberPulse < 0.0f ) ? pulse : MIN( pulse, memberPulse );
				addSelectionCircle( members[ m ].pos, members[ m ].radius, members[ m ].layer, color, latest );
			}
			continue;
		}
		if( obj->isContained() || draw->isDrawableEffectivelyHidden() )
			continue;
		// a building wears its outline; an aircraft in the air has the altitude line's ring under it
		if( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isAirborneTarget() )
			continue;
		if( std::find( m_selectedNexus.begin(), m_selectedNexus.end(), obj->getProducerID() ) != m_selectedNexus.end() )
			continue;

		// where the model is shown, which smooth motion puts a fraction of a tick behind the logic
		// (not on a picture a whole tick behind, which captured nothing: W3DView's smoothLock)
		Coord3D pos = *draw->getPosition();
		if( TheSmoothMotionActive && TheSmoothMotionAlpha < 1.0f )
			draw->getSmoothMotionPosition( TheSmoothMotionAlpha, &pos );
		addSelectionCircle( pos, obj->getGeometryInfo().getBoundingCircleRadius(), obj->getLayer(), color, pulse );
	}
	if( m_circles.empty() )
		return;

	const Int count = (Int)m_circles.size();
	m_circleNear.clear();
	m_circleNearFirst.resize( count + 1 );
	for( Int i = 0; i < count; ++i )
	{
		m_circleNearFirst[ i ] = (Int)m_circleNear.size();
		for( Int j = 0; j < count; ++j )
		{
			if( j == i || m_circles[ j ].layer != m_circles[ i ].layer )
				continue;		// a unit on a bridge and one on the ground under it keep their own circles
			const Real dx = m_circles[ j ].x - m_circles[ i ].x;
			const Real dy = m_circles[ j ].y - m_circles[ i ].y;
			const Real reach = m_circles[ i ].r + m_circles[ j ].r;
			if( dx * dx + dy * dy < reach * reach )
				m_circleNear.push_back( j );
		}
	}
	m_circleNearFirst[ count ] = (Int)m_circleNear.size();

	m_runPoints.clear();
	m_runFirst.clear();
	m_runCircle.clear();

	for( Int i = 0; i < count; ++i )
	{
		const SelectionCircle& c = m_circles[ i ];
		const Int samples = MAX( RING_MIN_SAMPLES, (Int)ceil( 2.0f * PI * c.r / RING_STEP ) );
		const Real step = 2.0f * PI / samples;
		auto ringPoint = [&]( Int k ) -> Vector3
		{
			const Real x = c.x + c.r * (Real)Cos( step * k );
			const Real y = c.y + c.r * (Real)Sin( step * k );
			return Vector3( x, y, ringGroundZ( c, x, y ) );
		};

		Int firstInside = -1;
		m_ringInside.resize( samples );
		for( Int k = 0; k < samples; ++k )
		{
			const Real a = step * k;
			m_ringInside[ k ] = insideOtherCircle( i, c.x + c.r * (Real)Cos( a ), c.y + c.r * (Real)Sin( a ) );
			if( m_ringInside[ k ] && firstInside < 0 )
				firstInside = k;
		}

		if( firstInside < 0 )
		{
			// nothing covers any of it: the whole circle is a loop of its own
			m_ringPoints.clear();
			for( Int k = 0; k < samples; ++k )
				m_ringPoints.push_back( ringPoint( k ) );
			recordRingLoop( c.color, c.pulse );
		}
		else
		{
			// Walk once round from a covered sample, so every uncovered run is met whole. A run starts
			// and ends where this circle crosses into another, found by halving the step between the
			// samples either side of it, so the arcs of two circles meet in one point with no gap. The
			// runs are joined into the outline's loops below.
			for( Int t = 1; t <= samples; ++t )
			{
				const Int k = ( firstInside + t ) % samples;
				const Int prev = ( firstInside + t - 1 ) % samples;
				const Real a = step * ( firstInside + t );
				if( m_ringInside[ k ] == m_ringInside[ prev ] )
				{
					if( !m_ringInside[ k ] )
						m_runPoints.push_back( ringPoint( firstInside + t ) );
					continue;
				}

				Real inA = a - step, outA = a;		// the covered end and the open end of this step
				if( m_ringInside[ k ] )
				{
					inA = a;
					outA = a - step;
				}
				for( Int n = 0; n < 8; ++n )
				{
					const Real mid = ( inA + outA ) * 0.5f;
					if( insideOtherCircle( i, c.x + c.r * (Real)Cos( mid ), c.y + c.r * (Real)Sin( mid ) ) )
						inA = mid;
					else
						outA = mid;
				}
				const Real x = c.x + c.r * (Real)Cos( outA );
				const Real y = c.y + c.r * (Real)Sin( outA );
				if( !m_ringInside[ k ] )
				{
					// out of another circle: a run starts here
					m_runFirst.push_back( (Int)m_runPoints.size() );
					m_runCircle.push_back( i );
				}
				m_runPoints.push_back( Vector3( x, y, ringGroundZ( c, x, y ) ) );
				if( !m_ringInside[ k ] )
					m_runPoints.push_back( ringPoint( firstInside + t ) );	// and goes on to this sample
				// into another circle: the run ends on the crossing just pushed
			}
		}
	}

	// Join the runs into the outline's loops. Every run is walked the same way round its circle, so
	// where one ends going into a circle the next one along the outline starts coming out of it, at
	// the same crossing: one loop through every cusp, mitred there by the line instead of two butt
	// ends overshooting each other.
	const Int runs = (Int)m_runCircle.size();
	m_runFirst.push_back( (Int)m_runPoints.size() );
	m_runUsed.assign( runs, FALSE );
	const Real joinSq = RING_JOIN_TOLERANCE * RING_JOIN_TOLERANCE;
	auto apartSq = []( const Vector3& a, const Vector3& b ) { return ( a.X - b.X ) * ( a.X - b.X ) + ( a.Y - b.Y ) * ( a.Y - b.Y ); };
	for( Int r = 0; r < runs; ++r )
	{
		if( m_runUsed[ r ] )
			continue;
		m_runUsed[ r ] = TRUE;
		m_ringPoints.assign( m_runPoints.begin() + m_runFirst[ r ], m_runPoints.begin() + m_runFirst[ r + 1 ] );
		Real loopPulse = m_circles[ m_runCircle[ r ] ].pulse;		// the most recent order of any unit on the loop
		Bool closed = FALSE;
		for( ;; )
		{
			// another run going on from here comes first: closing on the start the moment it is near
			// left a sliver between three circles as a loop of its own, a bright dot
			const Vector3 end = m_ringPoints.back();
			Int next = -1;
			Real best = joinSq;
			for( Int s = 0; s < runs; ++s )
			{
				if( m_runUsed[ s ] )
					continue;
				const Real d = apartSq( end, m_runPoints[ m_runFirst[ s ] ] );
				if( d < best )
				{
					best = d;
					next = s;
				}
			}
			if( next < 0 )
			{
				closed = m_ringPoints.size() > 2 && apartSq( end, m_ringPoints.front() ) < joinSq;
				break;
			}
			m_runUsed[ next ] = TRUE;
			const Real nextPulse = m_circles[ m_runCircle[ next ] ].pulse;
			if( nextPulse >= 0.0f && ( loopPulse < 0.0f || nextPulse < loopPulse ) )
				loopPulse = nextPulse;
			m_ringPoints.insert( m_ringPoints.end(), m_runPoints.begin() + m_runFirst[ next ] + 1, m_runPoints.begin() + m_runFirst[ next + 1 ] );
		}
		if( closed )
		{
			m_ringPoints.pop_back();		// the crossing it started on
			recordRingLoop( m_circles[ m_runCircle[ r ] ].color, loopPulse );
		}
		else if( m_ringPoints.size() >= 2 )
			recordRingLine( (Int)m_ringPoints.size(), &m_ringPoints[ 0 ], m_circles[ m_runCircle[ r ] ].color );
	}
}

/** m_ringPoints as a closed loop. A line has two ends whatever its points say, and a loop closed on
		a point left a crack where the two ends' caps met at an angle. So it starts and ends halfway
		along its first stretch, where both caps square off the same straight piece and meet flush
		(traceStructureOutline closes a building's outline the same way). */
void W3DWaypointBuffer::recordRingLoop( const Vector3& color, Real pulse, Bool withGlow, Bool withLine )
{
	const Int n = (Int)m_ringPoints.size();
	if( n < 3 )
		return;
	const Vector3 mid = ( m_ringPoints[ 0 ] + m_ringPoints[ 1 ] ) * 0.5f;
	m_loopPoints.clear();
	m_loopPoints.push_back( mid );
	m_loopPoints.insert( m_loopPoints.end(), m_ringPoints.begin() + 1, m_ringPoints.end() );
	m_loopPoints.push_back( m_ringPoints[ 0 ] );
	m_loopPoints.push_back( mid );
	recordRingLine( (Int)m_loopPoints.size(), &m_loopPoints[ 0 ], color, 1.0f, withGlow, withLine );

	// An attack order: copies of the outline rise off the ground and fade, PULSE_COUNT of them
	// PULSE_GAP apart, so the order reads on the units it went to. Line only, easing out as it climbs
	if( pulse < 0.0f )
		return;
	for( Int p = 0; p < PULSE_COUNT; ++p )
	{
		const Real t = ( pulse - p * PULSE_GAP ) / PULSE_SECONDS;
		if( t < 0.0f || t >= 1.0f )
			continue;
		const Real rise = PULSE_RISE * ( 1.0f - ( 1.0f - t ) * ( 1.0f - t ) );
		m_pulsePoints = m_loopPoints;
		for( size_t k = 0; k < m_pulsePoints.size(); ++k )
			m_pulsePoints[ k ].Z += rise;
		recordRingLine( (Int)m_pulsePoints.size(), &m_pulsePoints[ 0 ], color, PULSE_OPACITY * ( 1.0f - t ), FALSE, TRUE );
	}
}

/** Under another selection circle: strictly inside it, so two circles that only touch both keep
		their edge where they meet. */
Bool W3DWaypointBuffer::insideOtherCircle( Int self, Real x, Real y ) const
{
	for( Int n = m_circleNearFirst[ self ]; n < m_circleNearFirst[ self + 1 ]; ++n )
	{
		const SelectionCircle& o = m_circles[ m_circleNear[ n ] ];
		const Real dx = x - o.x;
		const Real dy = y - o.y;
		const Real r = o.r - 0.01f;
		if( dx * dx + dy * dy < r * r )
			return TRUE;
	}
	return FALSE;
}

/** The height a ring point lies at: the unit's own layer (a bridge's deck, or the ground), or the
		water's surface when the unit stands in water, lifted clear of it. A deck is not clipped to its
		edge, so a ring hanging over the side stays at deck height instead of dropping to the riverbed;
		nor does the water under a bridge pull it down or up. */
Real W3DWaypointBuffer::ringGroundZ( const SelectionCircle& c, Real x, Real y ) const
{
	const Real lift = MAX( RING_MIN_LIFT, RING_LIFT * m_ringWidthScale );
	if( c.layer != LAYER_GROUND )
		return TheTerrainLogic->getLayerHeight( x, y, c.layer, NULL, FALSE ) + lift;
	return MAX( TheTerrainLogic->getLayerHeight( x, y, c.layer ), c.waterZ ) + lift;
}

/** A circle round one selected unit, at the position it is drawn. A second circle on the same spot
		and size is dropped: two near-coincident circles each left an open arc and a doubled glow. */
void W3DWaypointBuffer::addSelectionCircle( const Coord3D& pos, Real radius, PathfindLayerEnum layer, const Vector3& color, Real pulse )
{
	SelectionCircle c;
	c.x = pos.x;
	c.y = pos.y;
	c.r = MAX( RING_MIN_RADIUS, MIN( RING_MAX_RADIUS, radius + RING_MARGIN ) );
	c.layer = layer;
	c.color = color;
	c.pulse = pulse;
	for( size_t i = 0; i < m_circles.size(); ++i )
	{
		const SelectionCircle& o = m_circles[ i ];
		if( fabs( o.x - c.x ) < 0.1f && fabs( o.y - c.y ) < 0.1f && fabs( o.r - c.r ) < 0.1f && o.layer == c.layer )
			return;
	}
	Real waterZ = 0.0f;
	c.waterZ = TheTerrainLogic->isUnderwater( c.x, c.y, &waterZ ) ? waterZ : -1.0e9f;
	m_circles.push_back( c );
}

/** The owner's colour at full brightness: the brightest channel at one, the hue kept. A grey seat
		comes out white, a navy one bright blue. */
Vector3 W3DWaypointBuffer::ringColor( const Player *owner )
{
	Real red = 1.0f, green = 1.0f, blue = 1.0f, alpha = 1.0f;
	if( owner )
		GameGetColorComponentsReal( clientPlayerColor( owner ), &red, &green, &blue, &alpha );
	const Real brightest = MAX( red, MAX( green, blue ) );
	if( brightest <= 0.01f )
		return Vector3( 1.0f, 1.0f, 1.0f );
	return Vector3( red / brightest, green / brightest, blue / brightest );
}

void W3DWaypointBuffer::recordRingLine( Int count, const Vector3 *points, const Vector3& color, Real alpha, Bool withGlow, Bool withLine )
{
	// A crossing found right on a sample comes out as the same point twice, and the line nudges a
	// zero-length piece in a direction of its own; one of them goes
	m_cleanPoints.clear();
	for( Int k = 0; k < count; ++k )
	{
		if( !m_cleanPoints.empty() )
		{
			const Vector3& last = m_cleanPoints.back();
			if( fabs( points[ k ].X - last.X ) < 0.01f && fabs( points[ k ].Y - last.Y ) < 0.01f )
				continue;
		}
		m_cleanPoints.push_back( points[ k ] );
	}
	if( m_cleanPoints.size() < 2 )
		return;

	// The glow is bevelled for its own width, and the line, which writes depth (queueLine), comes
	// RING_GLOW_BEHIND nearer the eye than it. The line moves rather than the glow: pushed away, the
	// glow's wide lower edge sank into ground rising toward the camera
	if( withGlow )
		recordRingPieces( DRAW_RING_GLOW, color * ( RING_GLOW_STRENGTH * alpha ), RING_GLOW_WIDTH * m_ringWidthScale, 1.0f, 0.0f );
	if( withLine )
		recordRingPieces( DRAW_RING, color, RING_WIDTH * m_ringWidthScale, RING_OPACITY * alpha, -RING_GLOW_BEHIND * m_ringWidthScale );
}

/** m_cleanPoints as one kind of ribbon, bevelled at sharp corners and cut to the line's length. */
void W3DWaypointBuffer::recordRingPieces( DrawKind kind, const Vector3& color, Real width, Real opacity, Real behind )
{
	const Int count = (Int)m_cleanPoints.size();
	const Vector3 *points = &m_cleanPoints[ 0 ];

	// Bevel every corner turning further than RING_BEVEL_COS, where two circles meet: the point
	// becomes two, a little way back along each side, so the turn is taken in two halves and neither
	// mitre runs out into a spike. Half the ribbon's width, never more than a third of either side.
	const Real bevel = 0.5f * width;
	m_bevelPoints.clear();
	m_bevelPoints.push_back( points[ 0 ] );
	for( Int k = 1; k + 1 < count; ++k )
	{
		Vector3 in = points[ k ] - points[ k - 1 ];
		Vector3 out = points[ k + 1 ] - points[ k ];
		const Real inLength = in.Length();
		const Real outLength = out.Length();
		if( inLength > 0.001f && outLength > 0.001f )
		{
			const Real turnCos = ( in.X * out.X + in.Y * out.Y ) / ( (Real)sqrt( in.X * in.X + in.Y * in.Y ) * (Real)sqrt( out.X * out.X + out.Y * out.Y ) + 0.0001f );
			if( turnCos < RING_BEVEL_COS )
			{
				m_bevelPoints.push_back( points[ k ] - in * ( MIN( bevel, inLength / 3.0f ) / inLength ) );
				m_bevelPoints.push_back( points[ k ] + out * ( MIN( bevel, outLength / 3.0f ) / outLength ) );
				continue;
			}
		}
		m_bevelPoints.push_back( points[ k ] );
	}
	m_bevelPoints.push_back( points[ count - 1 ] );

	// moved along the eye ray, away for a positive behind and toward for a negative: the same pixels
	if( behind != 0.0f )
	{
		for( size_t k = 0; k < m_bevelPoints.size(); ++k )
		{
			Vector3 away = m_bevelPoints[ k ] - m_ringCamera;
			const Real length = away.Length();
			if( length > 0.0f )
				m_bevelPoints[ k ] += away * ( behind / length );
		}
	}

	// Cut a long line into pieces of at most RING_MAX_LINE_POINTS, each ending and the next starting
	// halfway along the same stretch, so their caps meet flush (recordRingLoop's seam, again)
	const Int total = (Int)m_bevelPoints.size();
	Int first = 0;
	while( first < total - 1 )
	{
		Int last = MIN( total - 1, first + RING_MAX_LINE_POINTS - 2 );
		m_piecePoints.clear();
		if( first > 0 )
			m_piecePoints.push_back( ( m_bevelPoints[ first - 1 ] + m_bevelPoints[ first ] ) * 0.5f );
		for( Int k = first; k < last; ++k )
			m_piecePoints.push_back( m_bevelPoints[ k ] );
		if( last == total - 1 )
			m_piecePoints.push_back( m_bevelPoints[ last ] );
		else
			m_piecePoints.push_back( ( m_bevelPoints[ last - 1 ] + m_bevelPoints[ last ] ) * 0.5f );
		recordRing( kind, (Int)m_piecePoints.size(), &m_piecePoints[ 0 ], color, width, opacity );
		first = last;
	}
}

/** A selected building's outline: the units' thin glowing line in its owner's colour round its
		footprint grown by STRUCTURE_MARGIN, a rectangle for a box and a circle for a round one, a point
		every RING_STEP so it rides the ground. A rectangle's corners turn a right angle, under
		RING_BEVEL_COS, so they are mitred sharp and never bevelled. */
void W3DWaypointBuffer::recordStructureOutline( const Object *obj )
{
	const GeometryInfo& geom = obj->getGeometryInfo();
	const Coord3D *ctr = obj->getPosition();

	SelectionCircle ground;		// for the ground's height under each point
	ground.x = ctr->x;
	ground.y = ctr->y;
	ground.r = 0.0f;
	ground.layer = obj->getLayer();
	ground.waterZ = -1.0e9f;
	ground.pulse = -1.0f;
	ground.color = ringColor( obj->getControllingPlayer() );
	auto push = [&]( Real x, Real y )
	{
		m_ringPoints.push_back( Vector3( x, y, ringGroundZ( ground, x, y ) ) );
	};

	m_ringPoints.clear();
	if( geom.getGeomType() == GEOMETRY_BOX )
	{
		const Real c = (Real)Cos( obj->getOrientation() );
		const Real s = (Real)Sin( obj->getOrientation() );
		const Real a = geom.getMajorRadius() + STRUCTURE_MARGIN;
		const Real b = geom.getMinorRadius() + STRUCTURE_MARGIN;
		const Real signX[ 4 ] = { -1.0f, 1.0f, 1.0f, -1.0f };
		const Real signY[ 4 ] = { 1.0f, 1.0f, -1.0f, -1.0f };
		Coord2D corners[ 4 ];
		for( Int i = 0; i < 4; ++i )
		{
			corners[ i ].x = ctr->x + signX[ i ] * a * c - signY[ i ] * b * s;
			corners[ i ].y = ctr->y + signX[ i ] * a * s + signY[ i ] * b * c;
		}
		for( Int i = 0; i < 4; ++i )
		{
			const Coord2D& p = corners[ i ];
			const Coord2D& q = corners[ ( i + 1 ) % 4 ];
			const Real ex = q.x - p.x, ey = q.y - p.y;
			const Int pieces = MAX( 1, (Int)ceil( (Real)sqrt( ex * ex + ey * ey ) / RING_STEP ) );
			for( Int k = 0; k < pieces; ++k )
				push( p.x + ex * k / pieces, p.y + ey * k / pieces );
		}
	}
	else
	{
		const Real r = geom.getMajorRadius() + STRUCTURE_MARGIN;
		const Int samples = MAX( RING_MIN_SAMPLES, (Int)ceil( 2.0f * PI * r / RING_STEP ) );
		for( Int k = 0; k < samples; ++k )
			push( ctr->x + r * (Real)Cos( 2.0f * PI * k / samples ), ctr->y + r * (Real)Sin( 2.0f * PI * k / samples ) );
	}
	recordRingLoop( ground.color );
}


