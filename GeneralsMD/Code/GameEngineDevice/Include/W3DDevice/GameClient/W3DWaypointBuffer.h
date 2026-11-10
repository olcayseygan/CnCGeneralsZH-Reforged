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

// FILE: W3DWaypointBuffer.h ////////////////////////////////////////////////
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
// File name: W3DWaypointBuffer.h
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

#pragma once

#ifndef __W3D_WAYPOINT_BUFFER_H
#define __W3D_WAYPOINT_BUFFER_H

//-----------------------------------------------------------------------------
//           Includes                                                      
//-----------------------------------------------------------------------------
#include "always.h"
#include "rendobj.h"
#include "w3d_file.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "shader.h"
#include "vertmaterial.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include <vector>

class SegmentedLineClass;

class W3DWaypointBuffer 
{	
	friend class HeightMapRenderObjClass;
public:

	W3DWaypointBuffer(void);
	~W3DWaypointBuffer(void);

	void takeReadsForRender(void);	///< the order hints, rally lines and pucks, read at the render's sync point
	void drawWaypoints(RenderInfoClass &rinfo);	///< draws what takeReadsForRender took
	void freeWaypointBuffers();


private:
	enum DrawKind { DRAW_PUCK, DRAW_LINE, DRAW_OUTPOST_LINE, DRAW_OUTLINE_WHITE, DRAW_OUTLINE_RED };
	struct WaypointDraw
	{
		DrawKind kind;
		Bool orderHint;		///< not in the water's mirror pass
		Int first;
		Int count;
	};
	void record( DrawKind kind, Bool orderHint, Int count, const Vector3 *points );

	std::vector<WaypointDraw> m_draws;		///< this picture's pucks and lines, in the order they render
	std::vector<Vector3> m_drawPoints;

  void setDefaultLineStyle();
	void setLineStyle( SegmentedLineClass *line, DrawKind kind );	///< an order line's laser, or a building outline's solid colour
	void queueLine( Int numPoints, Vector3 *points, DrawKind kind );	///< hand the points to a pooled line object (not rendered yet)
	void renderQueuedLines( RenderInfoClass &rinfo );			///< render every queued line; all stay deferred on the static sort list together

	RenderObjClass *m_waypointNodeRobj;
	SegmentedLineClass *m_line;
	TextureClass *m_texture;
	std::vector<SegmentedLineClass *> m_linePool;				///< one line object per line drawn this frame, grown on demand
	std::vector<DrawKind> m_linePoolKinds;							///< what each pooled line is this frame; outlines render under the sun receiver exclusion
	std::vector<Coord3D> m_outlineTrace;								///< a building outline's points, reused frame to frame
	std::vector<Vector3> m_outlinePoints;
	Int m_linesUsed;
};

#endif  // end __W3D_WAYPOINT_BUFFER_H
