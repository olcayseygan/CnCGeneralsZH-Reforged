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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DInGameUI.cpp //////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Desct:	 In game user interface implementation for W3D
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>

#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameClient/CinemaDirector.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Image.h"
#include "GameClient/Mouse.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DGUICallbacks.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/Common/W3DConvert.h"
#include "WW3D2/WW3D.h"
#include "WW3D2/HAnim.h"
#include "WW3D2/Texture.h"
#include "WW3D2/DX8Wrapper.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/shader.h"

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.		 

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


#ifdef _DEBUG
#include "W3DDevice/GameClient/HeightMap.h"
#include "WW3D2/DX8IndexBuffer.h"
#include "WW3D2/DX8VertexBuffer.h"
#include "WW3D2/VertMaterial.h"
class DebugHintObject : public RenderObjClass
{	

public:

	DebugHintObject(void);
	DebugHintObject(const DebugHintObject & src);
	DebugHintObject & operator = (const DebugHintObject &);
	~DebugHintObject(void);

	virtual RenderObjClass *	Clone(void) const;
	virtual int						Class_ID(void) const;
	virtual void					Render(RenderInfoClass & rinfo);
	virtual Bool					Cast_Ray(RayCollisionTestClass & raytest);

	virtual void					Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const;
  virtual void					Get_Obj_Space_Bounding_Box(AABoxClass & aabox) const;

	int updateBlock(void);
	void freeMapResources(void);
	void setLocAndColorAndSize(const Coord3D *loc, Int argb, Int size);

protected:

	Coord3D m_myLoc;
	Int m_myColor;	// argb
	Int m_mySize;

	DX8IndexBufferClass				*m_indexBuffer;
	ShaderClass								m_shaderClass; //shader or rendering state for heightmap
	VertexMaterialClass	  	  *m_vertexMaterialClass;
	DX8VertexBufferClass			*m_vertexBufferTile;	//First vertex buffer.

	void initData(void);
};

// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_ALPHA ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )


DebugHintObject::~DebugHintObject(void)
{
	freeMapResources();
}

DebugHintObject::DebugHintObject(void) :
	m_indexBuffer(NULL),
	m_vertexMaterialClass(NULL),
	m_vertexBufferTile(NULL),
	m_myColor(0),
	m_mySize(0)
{
	initData();
}

Bool DebugHintObject::Cast_Ray(RayCollisionTestClass & raytest)
{
	return false;	
}

DebugHintObject::DebugHintObject(const DebugHintObject & src)
{
	*this = src;
}

DebugHintObject & DebugHintObject::operator = (const DebugHintObject & that)
{
	DEBUG_CRASH(("oops"));
	return *this;
}

void DebugHintObject::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	Vector3	ObjSpaceCenter((float)1000*0.5f,(float)1000*0.5f,(float)0);
	float length = ObjSpaceCenter.Length();
	sphere.Init(ObjSpaceCenter, length);
}

void DebugHintObject::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	Vector3	minPt(0,0,0);
	Vector3	maxPt((float)1000,(float)1000,(float)1000);
	box.Init(minPt,maxPt);
}

Int DebugHintObject::Class_ID(void) const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

RenderObjClass * DebugHintObject::Clone(void) const
{
	DEBUG_CRASH(("oops"));
	return NEW DebugHintObject(*this);
}


void DebugHintObject::freeMapResources(void)
{
	REF_PTR_RELEASE(m_indexBuffer);
	REF_PTR_RELEASE(m_vertexBufferTile);
	REF_PTR_RELEASE(m_vertexMaterialClass);
}

//Allocate a heightmap of x by y vertices.
//data must be an array matching this size.
void DebugHintObject::initData(void)
{	
	freeMapResources();	//free old data and ib/vb

	m_indexBuffer = NEW_REF(DX8IndexBufferClass,(3));

	// Fill up the IB
	{
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		ib[0]=0;
		ib[1]=1;
		ib[2]=2;
	}

	m_vertexBufferTile = NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZDUV1,3,DX8VertexBufferClass::USAGE_DEFAULT));

	//go with a preset material for now.
	m_vertexMaterialClass = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);

	//use a multi-texture shader: (text1*diffuse)*text2.
	m_shaderClass = ShaderClass::ShaderClass(SC_ALPHA);
}

void DebugHintObject::setLocAndColorAndSize(const Coord3D *loc, Int argb, Int size)
{
	m_myLoc = *loc;
	m_myColor = argb;
	m_mySize = size;

	if (m_myLoc.z < 0 && TheTerrainRenderObject) 
	{
		m_myLoc.z = TheTerrainRenderObject->getHeightMapHeight(m_myLoc.x, m_myLoc.y, NULL);
	}

	if (m_vertexBufferTile)
	{
		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexBufferTile);
		VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();

		Real x1 = m_mySize * 0.866;	// cos(30)
		Real y1 = m_mySize * 0.5;		// sin(30)
		
		// note, pts must go in a counterclockwise order!
		vb[0].x = 0;
		vb[0].y = m_mySize;
		vb[0].z = 0;
		vb[0].diffuse = m_myColor;
		vb[0].u1 = 0;
		vb[0].v1 = 0;

		vb[1].x = -x1;
		vb[1].y = -y1;
		vb[1].z = 0;
		vb[1].diffuse = m_myColor;
		vb[1].u1 = 0;
		vb[1].v1 = 0;

		vb[2].x = x1;
		vb[2].y = -y1;
		vb[2].z = 0;
		vb[2].diffuse = m_myColor;
		vb[2].u1 = 0;
		vb[2].v1 = 0;
	}
}

void DebugHintObject::Render(RenderInfoClass & rinfo)
{
	SphereClass bounds(Vector3(m_myLoc.x, m_myLoc.y, m_myLoc.z), m_mySize); 
	if (!rinfo.Camera.Cull_Sphere(bounds)) 
	{
		DX8Wrapper::Set_Material(m_vertexMaterialClass);
		DX8Wrapper::Set_Shader(m_shaderClass);
		DX8Wrapper::Set_Texture(0, NULL);
		DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
		DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile);

		Matrix3D tm(Transform);
		Vector3 vec(m_myLoc.x, m_myLoc.y, m_myLoc.z);
		tm.Set_Translation(vec);
		DX8Wrapper::Set_Transform(D3DTS_WORLD, tm);

		DX8Wrapper::Draw_Triangles(	0, 1, 0, 3);
	}
}
#endif // _DEBUG


///////////////////////////////////////////////////////////////////////////////////////////////////
// DEFINITIONS
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DInGameUI::W3DInGameUI()
{
	m_buildingPlacementAnchor = NULL;
	m_buildingPlacementArrow = NULL;

	for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
		m_allyCursorNames[ i ] = NULL;

	for( Int i = 0; i < MAX_ORDER_STEP_NUMBERS; ++i )
		m_orderStepNumbers[ i ] = NULL;

}  // end W3DInGameUI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DInGameUI::~W3DInGameUI()
{
	REF_PTR_RELEASE( m_buildingPlacementAnchor );
	REF_PTR_RELEASE( m_buildingPlacementArrow );

	for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
		if( m_allyCursorNames[ i ] )
		{
			TheDisplayStringManager->freeDisplayString( m_allyCursorNames[ i ] );
			m_allyCursorNames[ i ] = NULL;
		}

	for( Int i = 0; i < MAX_ORDER_STEP_NUMBERS; ++i )
		if( m_orderStepNumbers[ i ] )
		{
			TheDisplayStringManager->freeDisplayString( m_orderStepNumbers[ i ] );
			m_orderStepNumbers[ i ] = NULL;
		}

}  // end ~W3DInGameUI

// loadText ===================================================================
/** Load text from the file */
//=============================================================================
static void loadText( char *filename, GameWindow *listboxText )
{
	if (!listboxText)
		return;
	GadgetListBoxReset(listboxText);

	FILE *fp;

	// open the file
	fp = fopen( filename, "r" );
	if( fp == NULL )
		return;

	char buffer[ 1024 ];
	UnicodeString line;
	Color color = GameMakeColor(255, 255, 255, 255);
	while( fgets( buffer, 1024, fp ) != NULL )
	{
		line.translate(buffer);
		line.trim();
		if (line.isEmpty())
			line = UnicodeString(L" ");
		GadgetListBoxAddEntryText(listboxText, line, color, -1, -1);
	}  // end while

	// close the file
	fclose( fp );

}  // end loadText

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::init( void )
{

	// extending functionality
	InGameUI::init();
	// for the beta, they didn't want the help menu showing up, but I left this as a bock
	// comment because we'll probably want to add this back in.
/*
		// create the MOTD
		GameWindow *motd = TheWindowManager->winCreateFromScript( AsciiString("MOTD.wnd") );
		if( motd )
		{
			NameKeyType listboxTextID = TheNameKeyGenerator->nameToKey( "MOTD.wnd:ListboxMOTD" );
			GameWindow *listboxText = TheWindowManager->winGetWindowFromId(motd, listboxTextID);
	
			loadText( "HelpScreen.txt", listboxText );
	
			// hide it for now
			motd->winHide( TRUE );
	
		}  // end if*/
	
		
}  // end init

//-------------------------------------------------------------------------------------------------
/** Update in game UI */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::update( void )
{

	// call base
	InGameUI::update();

}  // end update

//-------------------------------------------------------------------------------------------------
/** Reset the in game ui */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::reset( void )
{

	// call base
	InGameUI::reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Draw member for the W3D implemenation of the game user interface */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::draw( void )
{
	// -cinema: none of the interface and none of the windows either.  Painting the window list let a
	// star banner slide in at the top right of an observer's footage (twice in trailer_chaos, frames
	// 930 and 1230), and nothing on the list belongs in a shot.  The letterbox is the display's own.
	if( CinemaDirector_hidesHud() )
		return;

	preDraw();

	// draw selection region if drag selecting
	if( m_isDragSelecting )
		drawSelectionRegion();

	// draw the formation line if one is being dragged out
	if( m_isFormationDragging )
		drawFormationLine();

	// and where everything selected is headed, drag or no drag
	drawOrderHints();

	// where the allies are pointing, which is the one thing on this screen somebody else is doing
	drawAllyCursors();

	// the attack circle, while the left button is still sweeping it out
	if( isAttackCircling() )
		drawAttackCircle();

	// for each view draw hints
	/// @todo should the UI be iterating through views like this?
	if( TheDisplay )
	{
		View *view;

		for( view = TheDisplay->getFirstView();
				 view;
				 view = TheDisplay->getNextView( view ) )
		{

			// draw attack hints
			drawAttackHints( view );

			// draw placement angle selection if needed
			drawPlaceAngle( view );

		}  // end for view

	}  // end if

	// repaint all our windows

#ifdef EXTENDED_STATS
	if (!DX8Wrapper::stats.m_disableConsole) {
#endif

#ifdef DO_UNIT_TIMINGS	 
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
	extern Bool g_UT_startTiming;
	if (!g_UT_startTiming)
#endif

#ifdef DEBUG_LOGGING
	//
	// The interface's own half of a frame, split again: the overlays and strips this class draws
	// over the world, and the window system's repaint of the control bar and every dialog.
	//
	extern Real TheUIPostDrawMS;
	extern Real TheWindowRepaintMS;
	Int64 tPostStart, tPostEnd, tWinEnd, freq;
	QueryPerformanceCounter( (LARGE_INTEGER *)&tPostStart );
#endif

	postDraw();

#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&tPostEnd );
#endif

	TheWindowManager->winRepaint();

#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&tWinEnd );
	QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
	if( freq > 0 )
	{
		TheUIPostDrawMS = (Real)((double)(tPostEnd - tPostStart) * 1000.0 / (double)freq);
		TheWindowRepaintMS = (Real)((double)(tWinEnd - tPostEnd) * 1000.0 / (double)freq);
	}
#endif

	//
	// The clock/rate plate goes on last, after every window has painted. In postDraw with the rest
	// of the world overlays it was drawn before the window manager, so anything the manager put on
	// top of it - a dialog, the menu, the control bar's own art at the top of the screen - covered
	// the one reading you want visible exactly when something is going wrong.
	//
	// the peace time clock across the top middle, and the clock plate in the corner beside it
	drawPeaceTimer();
	drawHudOverlay();
	drawScoreboard();

#ifdef EXTENDED_STATS
	}
#endif

}  // end draw

//-------------------------------------------------------------------------------------------------
// The render state every sheet painted on the ground is drawn with: the build grid, and the wash
// inside an attack circle.  Alpha blended, untextured, depth tested but never depth written, and
// PASS_ALWAYS: the sheet lies exactly on the terrain, so anything that compared depth against the
// terrain would z-fight with it.  Drawing it in the terrain pass and never writing depth means
// everything drawn after the terrain - buildings, units, trees, the placement ghost itself - covers
// it, which is what makes it read as paint on the ground.
// This is the bibs' shader with texturing off (see W3DBibBuffer).
#define SC_GROUND_OVERLAY ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_DISABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

/** rgb plus an alpha given as a float 0..255, clamped - these sheets are all one colour at
	* a per-vertex strength. */
static UnsignedInt overlayColor( UnsignedInt rgb, Real alpha )
{
	Int a = REAL_TO_INT( alpha );
	if( a < 0 )
		a = 0;
	else if( a > 255 )
		a = 255;
	return rgb | ((UnsignedInt)a << 24);
}

/** Batches a ground sheet's quads through the dynamic vertex buffer.  The build grid alone is ~1700
	* line quads plus a fill for each blocked cell, which is more than one dynamic lock wants to hold,
	* so it flushes in fixed chunks - the draw state is set once by the caller and holds across the
	* flushes. */
class GroundOverlayQuads
{
public:
	GroundOverlayQuads( void ) : m_quads( 0 ) { }

	void add( const Vector3 &p0, const Vector3 &p1, const Vector3 &p2, const Vector3 &p3,
						UnsignedInt c0, UnsignedInt c1, UnsignedInt c2, UnsignedInt c3 )
	{
		if( m_quads >= MAX_QUADS )
			flush();

		Vert *v = &m_verts[ m_quads * 4 ];
		v[ 0 ].pos = p0;  v[ 0 ].diffuse = c0;
		v[ 1 ].pos = p1;  v[ 1 ].diffuse = c1;
		v[ 2 ].pos = p2;  v[ 2 ].diffuse = c2;
		v[ 3 ].pos = p3;  v[ 3 ].diffuse = c3;
		++m_quads;
	}

	void flush( void );

private:
	enum { MAX_QUADS = 512 };
	struct Vert
	{
		Vector3 pos;
		UnsignedInt diffuse;
	};
	Vert m_verts[ MAX_QUADS * 4 ];
	Int m_quads;
};

void GroundOverlayQuads::flush( void )
{
	if( m_quads == 0 )
		return;

	const Int quads = m_quads;
	m_quads = 0;		// whatever happens below, this batch is spent

	DynamicVBAccessClass vbAccess( BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, quads * 4 );
	DynamicIBAccessClass ibAccess( BUFFER_TYPE_DYNAMIC_DX8, quads * 6 );
	{
		DynamicVBAccessClass::WriteLockClass vbLock( &vbAccess );
		DynamicIBAccessClass::WriteLockClass ibLock( &ibAccess );
		VertexFormatXYZNDUV2 *vb = vbLock.Get_Formatted_Vertex_Array();
		UnsignedShort *ib = ibLock.Get_Index_Array();
		if( vb == NULL || ib == NULL )
			return;

		for( Int q = 0; q < quads; ++q )
		{
			for( Int i = 0; i < 4; ++i, ++vb )
			{
				const Vert &src = m_verts[ q * 4 + i ];
				vb->x = src.pos.X;
				vb->y = src.pos.Y;
				vb->z = src.pos.Z;
				vb->nx = 0.0f;
				vb->ny = 0.0f;
				vb->nz = 1.0f;
				vb->diffuse = src.diffuse;
				vb->u1 = 0.0f;
				vb->v1 = 0.0f;
				vb->u2 = 0.0f;
				vb->v2 = 0.0f;
			}

			*ib++ = (UnsignedShort)(q * 4);
			*ib++ = (UnsignedShort)(q * 4 + 1);
			*ib++ = (UnsignedShort)(q * 4 + 2);
			*ib++ = (UnsignedShort)(q * 4);
			*ib++ = (UnsignedShort)(q * 4 + 2);
			*ib++ = (UnsignedShort)(q * 4 + 3);
		}
	}

	DX8Wrapper::Set_Index_Buffer( ibAccess, 0 );
	DX8Wrapper::Set_Vertex_Buffer( vbAccess );
	DX8Wrapper::Draw_Triangles( 0, quads * 2, 0, quads * 4 );
}

// 32k of vertices, shared by every sheet drawn on the ground.  They all run on the render thread
// and each one flushes before it returns, so there is only ever one batch in flight.
static GroundOverlayQuads theGroundOverlayQuads;

/** The prelit, untextured, alpha blended state a ground sheet is drawn with.  Set once per sheet,
	* before any quad is added. */
static void setGroundOverlayState( void )
{
	static ShaderClass overlayShader( SC_GROUND_OVERLAY );
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset( VertexMaterialClass::PRELIT_DIFFUSE );
	DX8Wrapper::Set_Material( material );
	REF_PTR_RELEASE( material );
	DX8Wrapper::Set_Texture( 0, NULL );
	DX8Wrapper::Set_Shader( overlayShader );
	DX8Wrapper::Apply_Render_State_Changes();
}

//-------------------------------------------------------------------------------------------------
/** Draw the pathfinder's own cell grid under the structure sitting on the cursor, and cross out
	* the cells it cannot go on.  GridBuildPlacement snaps a footprint's edges to these very lines
	* (see snapPlacementToGrid), so being able to see them is the difference between guessing at a
	* flush row of buildings and laying one out.
	*
	* Only the cells around the cursor are drawn.  The whole map's worth would be a wall of lines,
	* and the ones being aimed at are the ones worth seeing - so the lines also fade out towards the
	* edge of the patch instead of ending on a hard square.
	*
	* It is drawn as quads lying on the terrain, from inside the terrain pass
	* (HeightMapRenderObjClass::Render calls it right after the bibs), not as a screen space
	* overlay: painted on the ground it is read at a glance, and everything drawn after the terrain
	* covers it, so a building never has grid lines crawling over its roof.
	*
	* "Cannot build" here is the pathfinder cell's own type: water, a cliff, rubble, an existing
	* structure, plain impassable.  It deliberately does not run the full isLocationLegalToBuild for
	* every cell - that is a per-structure query (build radius, shroud, supply proximity) and a
	* couple of hundred of them a frame is not worth it.  The ghost's own red tint already answers
	* that question for the one spot the cursor is actually on. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawBuildGrid( void )
{
	// the grid you see is the grid you snap to: with the snap off it would mean nothing
	if( m_pendingPlaceType == NULL || TheGlobalData->m_gridBuildPlacement == FALSE )
		return;
	if( m_placeIcon == NULL || m_placeIcon[ 0 ] == NULL )
		return;
	if( TheTerrainLogic == NULL || TheAI == NULL )
		return;

	Pathfinder *pathfinder = TheAI->pathfinder();
	if( pathfinder == NULL )
		return;

	const Coord3D *center = m_placeIcon[ 0 ]->getPosition();
	if( center == NULL )
		return;

	// how many cells each way around the cursor we light up
	enum { GRID_RADIUS = 28 };
	enum { GRID_CELLS = GRID_RADIUS * 2 + 1, GRID_POINTS = GRID_CELLS + 1 };

	// half the width of a painted line, in world units - a cell is PLACEMENT_CELL (10) across
	const Real LINE_HALF_WIDTH = 0.45f;
	// the ground is sampled at the line, so a line running across a slope would sink into the hill
	// on one side; lifting the whole sheet a hair keeps it out of the dirt without floating
	const Real GRID_LIFT = 0.35f;

	// the pathfinder's own cell indexing, so the lines drawn are the lines it reasons about
	const Int cellX = REAL_TO_INT_FLOOR( (center->x + 0.5f) / PATHFIND_CELL_SIZE_F ) - GRID_RADIUS;
	const Int cellY = REAL_TO_INT_FLOOR( (center->y + 0.5f) / PATHFIND_CELL_SIZE_F ) - GRID_RADIUS;

	// sample the terrain once per grid corner, rather than once per quad corner.  Static, not
	// automatic: at this radius the two square tables are ~27k together, which is more than a
	// render-path stack frame should be carrying (the quad batch below is static for the same
	// reason).
	Real gx[ GRID_POINTS ], gy[ GRID_POINTS ];
	static Real gz[ GRID_POINTS ][ GRID_POINTS ];
	static Real fade[ GRID_POINTS ][ GRID_POINTS ];
	Int ix, iy;

	for( ix = 0; ix < GRID_POINTS; ++ix )
	{
		gx[ ix ] = placementGridLine( cellX + ix );
		gy[ ix ] = placementGridLine( cellY + ix );
	}

	// the patch fades out to nothing at its edge, measured from the cursor's own cell, so it ends
	// on a soft edge instead of a hard square
	const Real mid = (Real)GRID_RADIUS + 0.5f;
	for( iy = 0; iy < GRID_POINTS; ++iy )
	{
		for( ix = 0; ix < GRID_POINTS; ++ix )
		{
			gz[ iy ][ ix ] = TheTerrainLogic->getGroundHeight( gx[ ix ], gy[ iy ] ) + GRID_LIFT;

			const Real dx = (Real)fabs( ix - mid );
			const Real dy = (Real)fabs( iy - mid );
			const Real d = ( dx > dy ) ? dx : dy;
			const Real f = 1.0f - d / mid;
			fade[ iy ][ ix ] = ( f > 0.0f ) ? f : 0.0f;
		}
	}

	setGroundOverlayState();

	GroundOverlayQuads &quads = theGroundOverlayQuads;

	// the lines themselves, one quad per cell edge so they follow the ground over every bump
	const Real LINE_ALPHA = 0x58;
	for( iy = 0; iy < GRID_POINTS; ++iy )
	{
		for( ix = 0; ix < GRID_POINTS; ++ix )
		{
			if( ix + 1 < GRID_POINTS && ( fade[ iy ][ ix ] > 0.0f || fade[ iy ][ ix + 1 ] > 0.0f ) )
			{
				const UnsignedInt c0 = overlayColor( 0x00FFFFFF, LINE_ALPHA * fade[ iy ][ ix ] );
				const UnsignedInt c1 = overlayColor( 0x00FFFFFF, LINE_ALPHA * fade[ iy ][ ix + 1 ] );
				quads.add( Vector3( gx[ ix ],     gy[ iy ] - LINE_HALF_WIDTH, gz[ iy ][ ix ] ),
									 Vector3( gx[ ix + 1 ], gy[ iy ] - LINE_HALF_WIDTH, gz[ iy ][ ix + 1 ] ),
									 Vector3( gx[ ix + 1 ], gy[ iy ] + LINE_HALF_WIDTH, gz[ iy ][ ix + 1 ] ),
									 Vector3( gx[ ix ],     gy[ iy ] + LINE_HALF_WIDTH, gz[ iy ][ ix ] ),
									 c0, c1, c1, c0 );
			}

			if( iy + 1 < GRID_POINTS && ( fade[ iy ][ ix ] > 0.0f || fade[ iy + 1 ][ ix ] > 0.0f ) )
			{
				const UnsignedInt c0 = overlayColor( 0x00FFFFFF, LINE_ALPHA * fade[ iy ][ ix ] );
				const UnsignedInt c1 = overlayColor( 0x00FFFFFF, LINE_ALPHA * fade[ iy + 1 ][ ix ] );
				quads.add( Vector3( gx[ ix ] - LINE_HALF_WIDTH, gy[ iy ],     gz[ iy ][ ix ] ),
									 Vector3( gx[ ix ] + LINE_HALF_WIDTH, gy[ iy ],     gz[ iy ][ ix ] ),
									 Vector3( gx[ ix ] + LINE_HALF_WIDTH, gy[ iy + 1 ], gz[ iy + 1 ][ ix ] ),
									 Vector3( gx[ ix ] - LINE_HALF_WIDTH, gy[ iy + 1 ], gz[ iy + 1 ][ ix ] ),
									 c0, c0, c1, c1 );
			}
		}
	}

	// and a red wash over every cell a structure cannot stand on.  Filling the cell reads at a
	// glance where an X drawn in thin lines did not.
	const Real BLOCKED_ALPHA = 0x44;
	for( iy = 0; iy < GRID_CELLS; ++iy )
	{
		for( ix = 0; ix < GRID_CELLS; ++ix )
		{
			PathfindCell *cell = pathfinder->getCell( LAYER_GROUND, cellX + ix, cellY + iy );
			if( cell == NULL || cell->getType() == PathfindCell::CELL_CLEAR )
				continue;

			if( fade[ iy ][ ix ] <= 0.0f && fade[ iy + 1 ][ ix + 1 ] <= 0.0f )
				continue;

			quads.add( Vector3( gx[ ix ],     gy[ iy ],     gz[ iy ][ ix ] ),
								 Vector3( gx[ ix + 1 ], gy[ iy ],     gz[ iy ][ ix + 1 ] ),
								 Vector3( gx[ ix + 1 ], gy[ iy + 1 ], gz[ iy + 1 ][ ix + 1 ] ),
								 Vector3( gx[ ix ],     gy[ iy + 1 ], gz[ iy + 1 ][ ix ] ),
								 overlayColor( 0x00FF3030, BLOCKED_ALPHA * fade[ iy ][ ix ] ),
								 overlayColor( 0x00FF3030, BLOCKED_ALPHA * fade[ iy ][ ix + 1 ] ),
								 overlayColor( 0x00FF3030, BLOCKED_ALPHA * fade[ iy + 1 ][ ix + 1 ] ),
								 overlayColor( 0x00FF3030, BLOCKED_ALPHA * fade[ iy + 1 ][ ix ] ) );
		}
	}

	quads.flush();

}  // end drawBuildGrid

//-------------------------------------------------------------------------------------------------
/** The wash inside the circle a left drag is sweeping targets out of.  The rim is a screen space
	* line drawn with everything else; this is the ground it encloses, laid down in the terrain pass
	* so it bends over every slope inside it and so the tanks standing in the circle are drawn on top
	* of the wash instead of under it.
	*
	* Rings rather than one fan from the centre: a fan spans a valley with a single flat triangle,
	* and the circles worth drawing are big enough to cross one. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAttackCircleFill( void )
{
	Coord3D center;
	Real radius;
	if( !getAttackCircleGround( center, radius ) )
		return;

	enum { FILL_SEGMENTS = 48, FILL_RINGS = 6 };

	// the ground is sampled at the vertex, so a sheet across a slope would sink into the hill
	// between two samples; lifting it a hair keeps it out of the dirt without floating
	const Real FILL_LIFT = 0.35f;
	// faint on purpose.  It says which ground is inside the circle, it does not hide what is on it
	const Real FILL_ALPHA = 0x2C;
	const UnsignedInt FILL_RGB = 0x00FF5555;		// the attack red the rim and the hints use

	Vector3 ring[ FILL_RINGS + 1 ][ FILL_SEGMENTS + 1 ];
	Int r, s;
	for( r = 0; r <= FILL_RINGS; ++r )
	{
		const Real ringRadius = radius * (Real)r / (Real)FILL_RINGS;
		for( s = 0; s <= FILL_SEGMENTS; ++s )
		{
			const Real angle = 2.0f * PI * (Real)s / (Real)FILL_SEGMENTS;
			const Real x = center.x + ringRadius * (Real)cos( angle );
			const Real y = center.y + ringRadius * (Real)sin( angle );
			ring[ r ][ s ].Set( x, y, TheTerrainLogic->getGroundHeight( x, y ) + FILL_LIFT );
		}
	}

	setGroundOverlayState();

	GroundOverlayQuads &quads = theGroundOverlayQuads;
	const UnsignedInt fill = overlayColor( FILL_RGB, FILL_ALPHA );
	// the innermost band is a fan around the centre point, two segments to a quad.  Running it through
	// the same loop as the rest pins both inner corners to that one point, and every second triangle
	// of the band comes out with no area at all
	for( s = 0; s < FILL_SEGMENTS; s += 2 )
	{
		quads.add( ring[ 0 ][ 0 ], ring[ 1 ][ s ], ring[ 1 ][ s + 1 ], ring[ 1 ][ s + 2 ],
							 fill, fill, fill, fill );
	}

	for( r = 1; r < FILL_RINGS; ++r )
	{
		for( s = 0; s < FILL_SEGMENTS; ++s )
		{
			quads.add( ring[ r ][ s ], ring[ r + 1 ][ s ], ring[ r + 1 ][ s + 1 ], ring[ r ][ s + 1 ],
								 fill, fill, fill, fill );
		}
	}

	quads.flush();

}  // end drawAttackCircleFill

//-------------------------------------------------------------------------------------------------
/** draw 2d selection region on screen */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawSelectionRegion( void )
{
	Real width = 2.0f;
	UnsignedInt color = 0x9933FF33;  //0xAARRGGBB

	TheDisplay->drawOpenRect( m_dragSelectRegion.lo.x,
														m_dragSelectRegion.lo.y,
														m_dragSelectRegion.hi.x - m_dragSelectRegion.lo.x,
														m_dragSelectRegion.hi.y - m_dragSelectRegion.lo.y,
														width,
														color );

}  // end drawSelectionRegion

//-------------------------------------------------------------------------------------------------
/** draw the line a right drag is spreading the selection along.  Where each unit will stand is
	* said by the cursor sitting there, not by a mark on the line */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawFormationLine( void )
{
	const Real width = 2.0f;
	// the line says which order it is about to be, in the colours the order hints already use
	const UnsignedInt color = isInAttackMoveToMode() ? 0xCCFF66CC
											: isForceAttackArmed() ? 0xCCFF5555
											: isGuardArmed() ? 0xCC55CCFF
											: 0xCC33FF33;  //0xAARRGGBB
	const std::vector<ICoord2D>& curve = m_formationDragPoints;
	const Int points = (Int)curve.size();
	if( points < 2 )
		return;

	for( Int i = 1; i < points; ++i )
		TheDisplay->drawLine( curve[ i - 1 ].x, curve[ i - 1 ].y, curve[ i ].x, curve[ i ].y,
													width, color );

	// no ticks along it any more.  Every station already carries the marker of the order it is about
	// to receive, drawn by drawOrderHints off the same hints, and a tick beside a marker was the
	// same fact said twice

}  // end drawFormationLine

//-------------------------------------------------------------------------------------------------
/** draw the circle a left drag is sweeping targets out of.  It is a circle on the ground, not on
	* the screen, so it follows the terrain the way the selection it is about to make does */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAttackCircle( void )
{
	Coord3D center;
	Real radius;
	if( !getAttackCircleGround( center, radius ) )
		return;

	const Int segments = 48;
	const UnsignedInt color = 0xCCFF5555;  //0xAARRGGBB, the attack red the hints use
	const Real width = 2.0f;

	ICoord2D previous;
	Bool havePrevious = FALSE;
	for( Int i = 0; i <= segments; ++i )
	{
		const Real angle = 2.0f * PI * (Real)i / (Real)segments;
		Coord3D world;
		world.x = center.x + radius * (Real)cos( angle );
		world.y = center.y + radius * (Real)sin( angle );
		world.z = TheTerrainLogic->getGroundHeight( world.x, world.y );

		ICoord2D screen;
		if( TheTacticalView->worldToScreenTriReturn( &world, &screen ) == View::WTS_INVALID )
		{
			havePrevious = FALSE;
			continue;
		}

		if( havePrevious )
			TheDisplay->drawLine( previous.x, previous.y, screen.x, screen.y, width, color );

		previous = screen;
		havePrevious = TRUE;
	}

	// and the radius itself, so the drag reads as a radius rather than a rubber band
	ICoord2D middle;
	center.z = TheTerrainLogic->getGroundHeight( center.x, center.y );
	if( TheTacticalView->worldToScreenTriReturn( &center, &middle ) != View::WTS_INVALID )
		TheDisplay->drawLine( middle.x, middle.y, getAttackCircleCursor().x,
													getAttackCircleCursor().y, 1.0f, 0x66FF5555 );

}  // end drawAttackCircle

//-------------------------------------------------------------------------------------------------
/** The thread is coloured by what it is for: anything that ends in a shot is red, an attack move
	* is pink, a post to be held is blue, everything else is green.  The marker on the end of it is
	* the plain pointer in the same colour - one shape for every order, so the colour is the whole
	* message.  A dot and a ring were tried in its place and players wanted the pointer back. */
//-------------------------------------------------------------------------------------------------
static UnsignedInt orderHintLineColor( InGameUI::OrderHintKind kind )
{
	switch( kind )
	{
		case InGameUI::ORDER_HINT_ATTACK_MOVE:
			return 0x66FF66CC;
		case InGameUI::ORDER_HINT_ATTACK:
		case InGameUI::ORDER_HINT_FORCE_ATTACK:
		case InGameUI::ORDER_HINT_ATTACK_GROUND:
			return 0x66FF5555;
		case InGameUI::ORDER_HINT_GUARD:
			return 0x6655CCFF;
		default:
			return 0x6655FF55;
	}
}

static UnsignedInt orderHintMarkerColor( InGameUI::OrderHintKind kind )
{
	return orderHintLineColor( kind ) | 0xFF000000;
}

struct OrderCursorArt
{
	const Image *image;
	ICoord2D hotSpot;
	Bool tried;
};
static OrderCursorArt s_orderCursorArt[ Mouse::NUM_MOUSE_CURSORS ];

//-------------------------------------------------------------------------------------------------
/** Turn one Windows cursor into something the 2D renderer can draw.  Mouse.ini's Image entries
	* name mapped images that do not exist in the shipped data, and the Texture entries name .ANI
	* files rather than textures - so the art the player is actually holding only exists as an HCURSOR.
	* Pull the first frame's bits out of it and keep them as a texture. */
//-------------------------------------------------------------------------------------------------
static const Image *loadOrderCursorImage( Mouse::MouseCursor cursor, ICoord2D *hotSpot )
{
	const AsciiString& name = TheMouse->m_cursorInfo[ cursor ].textureName;
	if( name.isEmpty() )
		return NULL;

	char path[ 256 ];
	snprintf( path, ARRAY_SIZE(path), "data\\cursors\\%s.ANI", name.str() );

	HCURSOR hcursor = LoadCursorFromFile( path );
	if( hcursor == NULL )
		return NULL;

	ICONINFO info;
	if( GetIconInfo( hcursor, &info ) == FALSE )
		return NULL;

	const Image *result = NULL;

	BITMAP bm;
	if( info.hbmColor && GetObject( info.hbmColor, sizeof( BITMAP ), &bm ) )
	{
		const Int w = bm.bmWidth;
		const Int h = bm.bmHeight;

		BITMAPINFO bi;
		memset( &bi, 0, sizeof( bi ) );
		bi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
		bi.bmiHeader.biWidth = w;
		bi.bmiHeader.biHeight = -h;			// negative means top down, which is the order a texture wants
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		UnsignedInt *color = NEW UnsignedInt[ w * h ];
		UnsignedInt *mask = NEW UnsignedInt[ w * h ];

		HDC dc = GetDC( NULL );
		GetDIBits( dc, info.hbmColor, 0, h, color, &bi, DIB_RGB_COLORS );
		GetDIBits( dc, info.hbmMask, 0, h, mask, &bi, DIB_RGB_COLORS );
		ReleaseDC( NULL, dc );

		//
		// a 32-bit cursor carries its own alpha; an older one leaves it zero and says what is
		// transparent in the AND mask instead, where a white pixel is a hole
		//
		Bool hasAlpha = FALSE;
		for( Int i = 0; i < w * h; ++i )
			if( color[ i ] & 0xFF000000 )
			{
				hasAlpha = TRUE;
				break;
			}

		if( hasAlpha == FALSE )
			for( Int i = 0; i < w * h; ++i )
				color[ i ] |= (mask[ i ] & 0x00FFFFFF) ? 0x00000000 : 0xFF000000;

		TextureClass *texture = MSGNEW("TextureClass") TextureClass( w, h, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1 );
		SurfaceClass *surface = texture->Get_Surface_Level();
		Int pitch;
		UnsignedByte *bits = (UnsignedByte *)surface->Lock( &pitch );
		for( Int row = 0; row < h; ++row )
			memcpy( bits + row * pitch, color + row * w, w * sizeof( UnsignedInt ) );
		surface->Unlock();
		REF_PTR_RELEASE( surface );

		delete [] color;
		delete [] mask;

		Image *image = newInstance(Image);
		Region2D uv;
		uv.lo.x = 0.0f;
		uv.lo.y = 0.0f;
		uv.hi.x = 1.0f;
		uv.hi.y = 1.0f;
		image->setStatus( IMAGE_STATUS_RAW_TEXTURE );
		image->setRawTextureData( texture );
		image->setUV( &uv );
		image->setTextureWidth( w );
		image->setTextureHeight( h );
		ICoord2D size;
		size.x = w;
		size.y = h;
		image->setImageSize( &size );

		hotSpot->x = info.xHotspot;
		hotSpot->y = info.yHotspot;
		result = image;
	}

	if( info.hbmColor )
		DeleteObject( info.hbmColor );
	if( info.hbmMask )
		DeleteObject( info.hbmMask );

	return result;
}

//-------------------------------------------------------------------------------------------------
/** The marker on a destination is the cursor the player would be holding if they were pointing at
	* it.  Built once. */
//-------------------------------------------------------------------------------------------------
static const Image *orderCursorImage( Mouse::MouseCursor cursor, ICoord2D *hotSpot )
{
	OrderCursorArt& art = s_orderCursorArt[ cursor ];
	if( art.tried == FALSE )
	{
		art.image = loadOrderCursorImage( cursor, &art.hotSpot );
		art.tried = TRUE;
	}

	*hotSpot = art.hotSpot;
	return art.image;
}

//-------------------------------------------------------------------------------------------------
/** The cursor a kind of order is given with, for the kinds whose colour is the plain green and so
	* says nothing on its own.  Mouse::NONE for the rest. */
//-------------------------------------------------------------------------------------------------
static Mouse::MouseCursor orderHintCursor( InGameUI::OrderHintKind kind )
{
	switch( kind )
	{
		case InGameUI::ORDER_HINT_ENTER:					return Mouse::ENTER_FRIENDLY;
		case InGameUI::ORDER_HINT_DOCK:						return Mouse::DOCK;
		case InGameUI::ORDER_HINT_GET_REPAIRED:		return Mouse::GET_REPAIRED;
		case InGameUI::ORDER_HINT_GET_HEALED:			return Mouse::GET_HEALED;
		case InGameUI::ORDER_HINT_DO_REPAIR:			return Mouse::DO_REPAIR;
		case InGameUI::ORDER_HINT_CAPTURE:				return Mouse::CAPTUREBUILDING;
		case InGameUI::ORDER_HINT_HACK:						return Mouse::HACK;
		default:																	return Mouse::NONE;
	}
}

static const Int ORDER_STEP_POINT_SIZE = 11;

//-------------------------------------------------------------------------------------------------
/** How much bigger the step numbers are drawn than at 800x600.  Their font grows with the screen,
	* so everything laid out round them grows by the same factor or the number runs into the pointer
	* at 1440p and into the marker beside it at 4K. */
//-------------------------------------------------------------------------------------------------
static Real orderStepScale( void )
{
	return (Real)TheGlobalLanguageData->adjustFontSize( ORDER_STEP_POINT_SIZE ) / (Real)ORDER_STEP_POINT_SIZE;
}

//-------------------------------------------------------------------------------------------------
/** A step of a shift list carries its number, and beside it what the step is: the upgrade's own
	* button art, or the cursor of an order the colour cannot tell apart from a move.  The row sits
	* above and to the right of the pointer's tip, clear of the pointer itself, which hangs below it. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawOrderStep( const OrderHint& hint, const ICoord2D& tip, UnsignedInt color )
{
	const Real NUMBER_OFFSET_X = 10.0f;
	const Real ROW_HEIGHT = 22.0f;
	const Real ICON_GAP = 2.0f;

	const Real scale = orderStepScale();
	const Int rowHeight = REAL_TO_INT( ROW_HEIGHT * scale );
	const UnsignedInt alpha = color & 0xFF000000;
	Int x = tip.x + REAL_TO_INT( NUMBER_OFFSET_X * scale );
	const Int top = tip.y - rowHeight;

	if( hint.step > 0 && hint.step <= MAX_ORDER_STEP_NUMBERS )
	{
		DisplayString *&number = m_orderStepNumbers[ hint.step - 1 ];
		if( number == NULL )
		{
			number = TheDisplayStringManager->newDisplayString();
			number->setFont( TheWindowManager->winFindFont( AsciiString( "Arial" ),
																TheGlobalLanguageData->adjustFontSize( ORDER_STEP_POINT_SIZE ), TRUE ) );
			UnicodeString text;
			text.format( L"%d", hint.step );
			number->setText( text );
		}

		Int width = 0, height = 0;
		number->getSize( &width, &height );
		number->draw( x, top + ( rowHeight - height ) / 2, (Color)color, (Color)alpha );
		x += width + REAL_TO_INT( ICON_GAP * scale );
	}

	const Image *icon = hint.icon;
	if( icon == NULL )
	{
		ICoord2D hotSpot;
		icon = orderCursorImage( orderHintCursor( hint.kind ), &hotSpot );
	}
	if( icon == NULL )
		return;

	// the row's height, and the width the art's own proportions give it: a button cameo is wider
	// than it is tall, and squeezed into a square it reads as a different picture
	const Int iconWidth = rowHeight * icon->getImageWidth() / max( icon->getImageHeight(), 1 );
	TheDisplay->drawImage( icon, x, top, x + iconWidth, top + rowHeight, alpha | 0x00FFFFFF );
}

//-------------------------------------------------------------------------------------------------
/** One faint line per bunch of selected units going the same way, from where they stand to where
	* they are going, with the order's own cursor sitting on the destination.  Green for a move, pink
	* for an attack-move, red for an attack.  The goals are read off the units every frame, so the
	* lines last as long as the orders do and go when the units arrive or the selection changes. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawOrderHints( void )
{
	const std::vector<OrderHint>& hints = getOrderHints();
	if( hints.empty() )
		return;

	const Real width = 1.0f;

	// how far right of the spot an upgrade or an ability stands: past the pointer and the number of
	// the step that ends there, both of which grow with the screen
	const Real IN_PLACE_MARKER_OFFSET = 44.0f;
	const Int inPlaceOffset = REAL_TO_INT( IN_PLACE_MARKER_OFFSET * orderStepScale() );

	// A new marker slides up out of the bottom right and fades in over this long, so an order that
	// has just been given announces itself instead of appearing fully formed.  Wall clock rather
	// than frames: the picture is uncapped, so a frame count would run at the frame rate.
	const UnsignedInt MARKER_SLIDE_MS = 130;
	const Real MARKER_SLIDE_PIXELS = 13.0f;

	const UnsignedInt nowMs = timeGetTime();

	for( std::vector<OrderHint>::const_iterator it = hints.begin(); it != hints.end(); ++it )
	{
		const UnsignedInt lineColor = orderHintLineColor( it->kind );

		const UnsignedInt ageMs = nowMs - it->bornMs;
		Real arrival = 1.0f;
		if( ageMs < MARKER_SLIDE_MS )
			arrival = (Real)ageMs / (Real)MARKER_SLIDE_MS;

		// eased out, so it comes in fast and settles rather than sliding at one speed and stopping
		const Real remaining = 1.0f - arrival;
		const Real eased = 1.0f - remaining * remaining * remaining;

		// a unit off the edge of the screen still has a destination worth seeing, and the line to it
		// says which way it went.  WTS_OUTSIDE_FRUSTUM still gives usable pixels, so only points
		// behind the camera are dropped
		ICoord2D from, to;
		if( TheTacticalView->worldToScreenTriReturn( &it->from, &from ) == View::WTS_INVALID )
			continue;
		if( TheTacticalView->worldToScreenTriReturn( &it->to, &to ) == View::WTS_INVALID )
			continue;

		// an upgrade or an ability is used on the spot the step before it ends on, whose marker is
		// already there, so this one stands beside it rather than on top of it and draws no thread
		const Bool inPlace = it->kind == ORDER_HINT_UPGRADE || it->kind == ORDER_HINT_ABILITY;
		if( inPlace )
			to.x += inPlaceOffset;

		// Order Lines off in the options takes the lines away and leaves the markers: where a unit is
		// going is still worth a glance when the thread across the map is not
		if( TheGlobalData->m_showOrderLines && !inPlace )
			TheDisplay->drawLine( from.x, from.y, to.x, to.y, width, lineColor );

		// the marker is the plain pointer, tinted: its white body takes the order colour and the
		// dark outline stays.  The hot spot is the pixel the player aims with, so that is the pixel
		// that goes on the destination - a pointer hung by its top left corner points at the wrong
		// ground
		ICoord2D hotSpot;
		const Image *image = orderCursorImage( Mouse::ARROW, &hotSpot );
		if( image )
		{
			const Int w = image->getImageWidth();
			const Int h = image->getImageHeight();
			const Int slide = REAL_TO_INT_FLOOR( ( 1.0f - eased ) * MARKER_SLIDE_PIXELS );
			const Int x = to.x - hotSpot.x + slide;
			const Int y = to.y - hotSpot.y + slide;

			// the tint carries the fade as well as the order's colour
			const UnsignedInt markerColor = ( orderHintMarkerColor( it->kind ) & 0x00FFFFFF )
																			| ( (UnsignedInt)REAL_TO_INT( 255.0f * eased ) << 24 );
			TheDisplay->drawImage( image, x, y, x + w, y + h, markerColor );

			if( it->step > 0 || it->icon )
			{
				ICoord2D tip;
				tip.x = to.x + slide;
				tip.y = to.y + slide;
				drawOrderStep( *it, tip, markerColor );
			}
		}
	}

}  // end drawOrderHints

//-------------------------------------------------------------------------------------------------
/** A patch of the ally's own colour on the ground under their cursor.  It is a fan of rings whose
	* alpha falls to nothing at the rim, drawn in the terrain pass like the attack circle's wash, so
	* units and buildings stand on top of it and it reads as light on the map rather than as a disc
	* hanging over it.
	*
	* Faint on purpose: seven of these at full strength would be seven holes in the map. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAllyCursorLights( void )
{
	if( !TheGlobalData->m_showAllyCursors )
		return;

	enum { LIGHT_SEGMENTS = 20, LIGHT_RINGS = 5 };
	const Real LIGHT_RADIUS = 55.0f;		// about three tanks across, so it reads at the zoom people play at
	const Real LIGHT_LIFT = 0.35f;			// the same hair off the dirt the attack circle's wash takes
	const Real LIGHT_CENTRE_ALPHA = 0x78;

	Bool stateSet = FALSE;

	for( Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex )
	{
		const Real fade = getAllyCursorFade( playerIndex );
		if( fade <= 0.0f )
			continue;

		// a cursor is only ever known for a player who was found and vouched for on the way in
		Player *player = ThePlayerList->getNthPlayer( playerIndex );

		const Coord3D& centre = getAllyCursor( playerIndex ).shown;
		const UnsignedInt rgb = (UnsignedInt)clientPlayerColor( player ) & 0x00FFFFFF;

		Vector3 ring[ LIGHT_RINGS + 1 ][ LIGHT_SEGMENTS + 1 ];
		Int r, s;
		for( r = 0; r <= LIGHT_RINGS; ++r )
		{
			const Real ringRadius = LIGHT_RADIUS * (Real)r / (Real)LIGHT_RINGS;
			for( s = 0; s <= LIGHT_SEGMENTS; ++s )
			{
				const Real angle = 2.0f * PI * (Real)s / (Real)LIGHT_SEGMENTS;
				const Real x = centre.x + ringRadius * (Real)cos( angle );
				const Real y = centre.y + ringRadius * (Real)sin( angle );
				ring[ r ][ s ].Set( x, y, TheTerrainLogic->getGroundHeight( x, y ) + LIGHT_LIFT );
			}
		}

		// the alpha of ring r, so the patch is brightest under the pointer and gone at the rim.  The
		// falloff holds its strength through the middle and then drops, which is what a pool of light
		// looks like; a straight ramp reads as a flat disc with a hard edge
		UnsignedInt ringColor[ LIGHT_RINGS + 1 ];
		for( r = 0; r <= LIGHT_RINGS; ++r )
		{
			const Real across = (Real)r / (Real)LIGHT_RINGS;
			const Real remaining = 1.0f - across;
			ringColor[ r ] = overlayColor( rgb, LIGHT_CENTRE_ALPHA * fade * remaining * remaining * ( 3.0f - 2.0f * remaining ) );
		}

		// one render state for all of them, and only if there turned out to be something to draw
		if( stateSet == FALSE )
		{
			setGroundOverlayState();
			stateSet = TRUE;
		}

		GroundOverlayQuads &quads = theGroundOverlayQuads;

		// the innermost band is a fan around the centre point, two segments to a quad - running it
		// through the ring loop below would give every second triangle no area at all
		for( s = 0; s < LIGHT_SEGMENTS; s += 2 )
		{
			quads.add( ring[ 0 ][ 0 ], ring[ 1 ][ s ], ring[ 1 ][ s + 1 ], ring[ 1 ][ s + 2 ],
								 ringColor[ 0 ], ringColor[ 1 ], ringColor[ 1 ], ringColor[ 1 ] );
		}

		for( r = 1; r < LIGHT_RINGS; ++r )
		{
			for( s = 0; s < LIGHT_SEGMENTS; ++s )
			{
				quads.add( ring[ r ][ s ], ring[ r + 1 ][ s ], ring[ r + 1 ][ s + 1 ], ring[ r ][ s + 1 ],
									 ringColor[ r ], ringColor[ r + 1 ], ringColor[ r + 1 ], ringColor[ r ] );
			}
		}
	}

	if( stateSet )
		theGroundOverlayQuads.flush();

}  // end drawAllyCursorLights

//-------------------------------------------------------------------------------------------------
/** Each ally's mouse, drawn as their name in their own colour sitting in the middle of the pool of
	* light on the ground under it.  No pointer: a second arrow on screen is read as your own for the
	* half second it takes to notice it is not, and the name is the part that says whose it is.
	*
	* An ally who stops sending - alt-tabbed, or dropped - fades out rather than vanishing, which is
	* the difference between "they went away" and "the network hiccuped". */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAllyCursors( void )
{
	if( !TheGlobalData->m_showAllyCursors )
		return;

	const Int NAME_POINT_SIZE = 10;

	for( Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex )
	{
		const Real fade = getAllyCursorFade( playerIndex );
		if( fade <= 0.0f )
			continue;

		// a cursor is only ever known for a player who was found and vouched for on the way in
		Player *player = ThePlayerList->getNthPlayer( playerIndex );

		// a cursor off the side of the screen still has a usable pixel; only one behind the camera
		// has not, which is the one case worldToScreenTriReturn calls invalid
		ICoord2D screen;
		if( TheTacticalView->worldToScreenTriReturn( &getAllyCursor( playerIndex ).shown, &screen ) == View::WTS_INVALID )
			continue;

		const UnsignedInt alpha = (UnsignedInt)REAL_TO_INT( 255.0f * fade );
		const UnsignedInt rgb = (UnsignedInt)clientPlayerColor( player ) & 0x00FFFFFF;
		const Color tint = (Color)( rgb | ( alpha << 24 ) );
		const Color dropColor = GameMakeColor( 0, 0, 0, (UnsignedByte)alpha );

		if( m_allyCursorNames[ playerIndex ] == NULL )
		{
			m_allyCursorNames[ playerIndex ] = TheDisplayStringManager->newDisplayString();
			m_allyCursorNames[ playerIndex ]->setFont( TheWindowManager->winFindFont(
																AsciiString( "Arial" ),
																TheGlobalLanguageData->adjustFontSize( NAME_POINT_SIZE ), TRUE ) );
		}

		// set every frame: a player who is destroyed or renamed mid-match should not keep a stale plate
		m_allyCursorNames[ playerIndex ]->setText( player->getPlayerDisplayName() );

		// centred on the reported spot in both directions, so the name sits in the middle of its own
		// pool of light and the two together are the marker
		Int nameWidth = 0, nameHeight = 0;
		m_allyCursorNames[ playerIndex ]->getSize( &nameWidth, &nameHeight );
		m_allyCursorNames[ playerIndex ]->draw( screen.x - nameWidth / 2,
																						screen.y - nameHeight / 2,
																						tint, dropColor );
	}

}  // end drawAllyCursors

//-------------------------------------------------------------------------------------------------
/** Draw visual back for clicking to attack a unit in the world */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAttackHints( View *view )
{

}  // end drawAttackHints

//-------------------------------------------------------------------------------------------------
/** Draw the angle selection for placing building if needed */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawPlaceAngle( View *view )
{
//	Coord2D v, p, o;
	//Real size = 15.0f;

	//Create the anchor & arrow if not already created!
	if( !m_buildingPlacementAnchor )
	{
		m_buildingPlacementAnchor = W3DDisplay::m_assetManager->Create_Render_Obj( "Locater01" );

		// sanity
		if( !m_buildingPlacementAnchor )
		{
			DEBUG_CRASH( ("Unable to create BuildingPlacementAnchor (Locator01.w3d) -- cursor for placing buildings") );
			return;
		}
	}
	if( !m_buildingPlacementArrow )
	{
		m_buildingPlacementArrow = W3DDisplay::m_assetManager->Create_Render_Obj( "Locater02" );

		// sanity
		if( !m_buildingPlacementArrow )
		{
			DEBUG_CRASH( ("Unable to create BuildingPlacementArrow (Locator02.w3d) -- cursor for placing buildings") );
			return;
		}
	}

	Bool anchorInScene = m_buildingPlacementAnchor->Peek_Scene() != NULL;
	Bool arrowInScene	 = m_buildingPlacementArrow->Peek_Scene() != NULL;

	// get out of here if this display isn't up anyway, and with shift held it is not: that drag
	// lays a row and turns nothing, so an anchor and an arrow would promise a turn it will not make
	if( isPlacementAnchored() == FALSE || placesRow() )
	{
		if( anchorInScene )
		{
			//If our anchor is in the scene, remove it from the scene but don't delete it.
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementAnchor );
		}
		if( arrowInScene )
		{
			//If our arrow is in the scene, remove it from the scene but don't delete it.
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementArrow );
		}
		return;
	}

	// get the anchor points
	ICoord2D start, end;
	getPlacementPoints( &start, &end );




	Coord3D vector;
	vector.x = end.x - start.x;
	vector.y = end.y - start.y;
	vector.z = 0.0f;
	Real length = vector.length();

	Bool showArrow = length >= 5.0f;

	if( showArrow )
	{
		if( anchorInScene )
		{
			//We're switching to the arrow!
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementAnchor );
		}
		if( !arrowInScene )
		{
			W3DDisplay::m_3DScene->Add_Render_Object( m_buildingPlacementArrow );
			arrowInScene = TRUE;
		}
	}
	else
	{
		if( arrowInScene )
		{
			//We're switching to the anchor!
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementArrow );
		}
		if( !anchorInScene )
		{
			W3DDisplay::m_3DScene->Add_Render_Object( m_buildingPlacementAnchor );
			anchorInScene = TRUE;
		}
	}

	//The proper way to orient the placement arrow is to copy the matrix from the m_placeIcon[0]!
	if( anchorInScene )
	{
		if ( m_placeIcon[ 0 ] )
			m_buildingPlacementAnchor->Set_Transform( *m_placeIcon[ 0 ]->getTransformMatrix() );
	}
	else if( arrowInScene )
	{
		if ( m_placeIcon[ 0 ] )
			m_buildingPlacementArrow->Set_Transform( *m_placeIcon[ 0 ]->getTransformMatrix() );
	}

	
	//m_buildingPlacementArrow->Set_Transform(

	// draw a little box at the start to show the "anchor" point
	//Real rectSize = 4.0f;
	//TheDisplay->drawFillRect( start.x - rectSize / 2, start.y - rectSize / 2,
	//													rectSize, rectSize, color );

	// compute vector for line
	//v.x = end.x - start.x;
	//v.y = end.y - start.y;
	//v.normalize();

	// compute opposite vector
	//o.x = -v.x;
	//o.y = -v.y;

	// compute perpendicular vector one way
	//p.x = -v.y;
	//p.y = v.x;

	// draw the line
	//start.x = o.x * size + p.x * (size/2.0f) + end.x;
	//start.y = o.y * size + p.y * (size/2.0f) + end.y;
	//TheDisplay->drawLine( start.x, start.y, end.x, end.y, width, color );

	// compute perpendicular vector other way
	//p.x = v.y;
	//p.y = -v.x;

	// draw the line
	//start.x = o.x * size + p.x * (size/2.0f) + end.x;
	//start.y = o.y * size + p.y * (size/2.0f) + end.y;
	//TheDisplay->drawLine( start.x, start.y, end.x, end.y, width, color );

}  // end drawPlaceAngle

