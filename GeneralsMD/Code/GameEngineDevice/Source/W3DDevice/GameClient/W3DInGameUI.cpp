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

// FILE: W3DInGameUI.cpp //////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Desct:	 In game user interface implementation for W3D
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include "Lib/Clock.h"

#include "Common/BuildAssistant.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/RandomMapGenerator.h"
#include "GameLogic/PolygonTrigger.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/LaneWarDirector.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
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
#include "GameClient/ObserverCamera.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DGUICallbacks.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DView.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/Common/W3DConvert.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/hanim.h"
#include "WW3D2/texture.h"
#include "WW3D2/dx8wrapper.h"
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
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/vertmaterial.h"
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

	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		m_moveHintRenderObj[ i ] = NULL;
		m_moveHintAnim[ i ] = NULL;
		m_moveHintShrouded[ i ] = FALSE;
	}

	m_buildGridCellX = 0;
	m_buildGridCellY = 0;
	memset( m_buildGridBlocked, 0, sizeof( m_buildGridBlocked ) );

}  // end W3DInGameUI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DInGameUI::~W3DInGameUI()
{
	REF_PTR_RELEASE( m_buildingPlacementAnchor );
	REF_PTR_RELEASE( m_buildingPlacementArrow );

	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		REF_PTR_RELEASE( m_moveHintRenderObj[ i ] );
		REF_PTR_RELEASE( m_moveHintAnim[ i ] );
	}

}  // end ~W3DInGameUI

// overlayGround ==============================================================
/** The ground a sheet laid point by point goes on: the tiles' own triangles, subdivided or not,
	cut on the diagonal they are drawn with, so neither a bulge between the samples nor the far half
	of a flipped cell swallows the depth tested paint (BaseHeightMapRenderObjClass::getDrawnHeight).
	Logic's plane always cuts the same diagonal. */
//=============================================================================
static Real overlayGround( Real x, Real y )
{
	if( TheTerrainRenderObject )
		return TheTerrainRenderObject->getDrawnHeight( x, y );
	return TheTerrainLogic->getGroundHeight( x, y );
}

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
			line = UnicodeString(u" ");
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
	// the recordings outlive their replays now (Display::replay); their strings go with Display::reset
	m_overWorldDraw.clear();
	m_dragBoxDraw.clear();
	m_postDrawCalls.clear();
	m_hudOverlayDraw.clear();
	m_guardMarkersFrame = ~0u;
	m_guardMarkerTops.clear();

}  // end reset

static void drawGroundRing( const Coord3D& center, Real radius, UnsignedInt color, Real width );

//-------------------------------------------------------------------------------------------------
/** LOGIC-THREAD.md phase 3.  The parts of draw that ask logic what to show (the superweapon clocks,
	* the guard markers, the world animations' fog, the production list) run here, at the sync point
	* before the render, and record what they draw; draw replays each list where the part used to run.
	* Under -cinema draw takes its own branch and none of this is drawn. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::recordDraw( void )
{
	m_overWorldDraw.clear();
	m_dragBoxDraw.clear();
	m_postDrawCalls.clear();
	m_hudOverlayDraw.clear();
	m_orderRingPoints.clear();
	m_orderRingColors.clear();
	m_orderRingBornMs.clear();
	m_orderMarkers.clear();
	takeMoveHintTransforms();
	if( CinemaDirector_hidesHud() )
		return;

	// the drag box is the pointer's, so a picture drawn again beside the tick keeps it where it is
	// while the rest of this list moves with a scrolled camera; it draws after the guard markers now
	TheDisplay->beginRecording( &m_dragBoxDraw );
	if( m_isDragSelecting )
		drawSelectionRegion();
	TheDisplay->endRecording();

	TheDisplay->beginRecording( &m_overWorldDraw );
	preDraw();
	takeBuildGrid();	// after preDraw, which puts the ghost where the patch is centred
	takeOrderRings();	// after preDraw, which places them (updateOrderHints)

	// draw the formation line if one is being dragged out
	if( m_isFormationDragging )
		drawFormationLine();

	// the attack circle, while the left button is still sweeping it out
	if( isAttackCircling() )
		drawAttackCircle();

	// which of the local player's units are holding a guard
	drawGuardMarkers();
	TheDisplay->endRecording();

	// While the map loads the display calls this every pass to paint the load screen's windows, and
	// the match is already "in game" with its peace time set: the truce plate, the corner readout and
	// the lists drew over the load screen.  Only the windows belong there.
	if( TheGlobalData->m_loadScreenRender )
		return;
	TheDisplay->beginRecording( &m_postDrawCalls );
	postDraw();
	TheDisplay->endRecording();
	TheDisplay->beginRecording( &m_hudOverlayDraw );
	drawHudOverlay();
	TheDisplay->endRecording();
}

//-------------------------------------------------------------------------------------------------
/** Draw member for the W3D implemenation of the game user interface */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::draw( void )
{
	// -cinema: none of the interface and none of the windows either.  Painting the window list let a
	// star banner slide in at the top right of an observer's footage (twice in trailer_chaos, frames
	// 930 and 1230), and nothing on the list belongs in a shot.  The letterbox is the display's own.
	if( CinemaDirector_hidesHud() )
	{
		// the console's 'hidehud showmap=true': the bar is hidden, so its radar window is put in the
		// corner by hand, where the radar's own pixel maths find it too, and painted.  -directorrecord
		// never asks for it: a map on the film was one more thing on the screen
		if( CinemaDirector_showsMap() && !TheObserverCamera.isDrawingSecond() )
		{
			GameWindow *radarWindow = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:LeftHUD" ) );
			// W3DLeftHUDDraw paints one pixel inside the window, and the radar keeps the map's shape
			// inside that, with black bars for the rest.  The window is cut to the map's own shape,
			// a pixel larger each side and one pixel over the screen's edges, so the map is flush
			enum { RADAR_BEZEL = 1 };
			ICoord2D size, ul, lr;
			radarWindow->winGetSize( &size.x, &size.y );
			TheRadar->findDrawPositions( 0, 0, size.x - 2 * RADAR_BEZEL, size.y - 2 * RADAR_BEZEL, &ul, &lr );
			IRegion2D corner;
			corner.lo.x = -RADAR_BEZEL;
			corner.hi.x = corner.lo.x + ( lr.x - ul.x ) + 2 * RADAR_BEZEL;
			corner.hi.y = TheDisplay->getHeight() + RADAR_BEZEL;
			corner.lo.y = corner.hi.y - ( lr.y - ul.y ) - 2 * RADAR_BEZEL;
			TheControlBar->placeWindowAt( radarWindow, corner );
			W3DLeftHUDDraw( radarWindow, NULL );
		}

		// -directorrecord's score bar and corner window over everything, in the main draw alone; the
		// replay pass's picture is the moment alone: the episode's cut lays its own band over it
		if( TheGlobalData->m_directorRecord && !TheObserverCamera.isReplaying() && !TheObserverCamera.isDrawingSecond() )
			drawDirectorBroadcast();
		return;
	}

	// preDraw, the formation line, the attack circle, the guard markers and the drag box, which
	// recordDraw ran at the sync point; the world's part moves with a camera scrolled since
	const ICoord2D shift = static_cast<W3DView *>( TheTacticalView )->overlayShift();
	TheDisplay->replayShifted( m_overWorldDraw, shift.x, shift.y );
	Display::replay( m_dragBoxDraw );

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
			// draw move hints
			drawMoveHints( view );

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
	tPostStart = Clock_Ticks();
#endif

	// postDraw, as recordDraw ran it
	Display::replay( m_postDrawCalls );

#ifdef DEBUG_LOGGING
	tPostEnd = Clock_Ticks();
#endif

	TheWindowManager->winRepaint();

#ifdef DEBUG_LOGGING
	tWinEnd = Clock_Ticks();
	freq = Clock_Ticks_Per_Second();
	if( freq > 0 )
	{
		TheUIPostDrawMS = (Real)((double)(tPostEnd - tPostStart) * 1000.0 / (double)freq);
		TheWindowRepaintMS = (Real)((double)(tWinEnd - tPostEnd) * 1000.0 / (double)freq);
	}
#endif

	//
	// The info strip in the corner, peace time included, goes on last, after every window has
	// painted: drawn before the window manager, a dialog, the menu or the control bar's own art
	// covered it.
	//
	Display::replay( m_hudOverlayDraw );

	// the online service's notice over every screen, menus and match alike; reads only the session, so it
	// is drawn here and not recorded at the sync point
	drawOnlineNotice();

#ifdef EXTENDED_STATS
	}
#endif

}  // end draw

//-------------------------------------------------------------------------------------------------
// The render state every sheet painted on the ground is drawn with: the build grid, the order rings
// and markers, and the wash inside an attack circle.  Alpha blended, untextured, never depth
// written, so everything drawn after the terrain - buildings, units, trees, the placement ghost
// itself - covers it, which is what makes it read as paint on the ground.
// The grid and the order marks are depth tested: drawn PASS_ALWAYS they showed through hills and
// over the bridge decks above them.  They lie on the ground, so each vertex is pulled a few units
// along its ray to the eye (GroundOverlayQuads::flush): that moves its depth and not its pixel, on
// every device alike, where a depth bias is a different number on each.  The wash stays
// PASS_ALWAYS: its quads are a sixth of the circle's radius across and would sink into any bump.
// This is the bibs' shader with texturing off (see W3DBibBuffer).
#define SC_GROUND_OVERLAY_DEPTH( depth ) ( SHADE_CNST(depth, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_DISABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )
#define SC_GROUND_OVERLAY			SC_GROUND_OVERLAY_DEPTH( ShaderClass::PASS_ALWAYS )
#define SC_GROUND_OVERLAY_TESTED	SC_GROUND_OVERLAY_DEPTH( ShaderClass::PASS_LEQUAL )

// how far the build grid is pulled to the eye: its lines run ten units between two samples, and a
// subdivided cell's middle rises up to a unit over them (TerrainSubdivide's CAP)
static const Real GRID_PULL = 3.0f;

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
	GroundOverlayQuads( void ) : m_quads( 0 ), m_pull( 0.0f ), m_shrouded( FALSE ) { }

	/** Set by setGroundOverlayState for the sheet that follows: how far each vertex goes towards
		* the eye, and the shroud whose texture it is multiplied by when no shroud pass is still to
		* come (its first texture coordinates, out of the terrain's own mapping: a border texel
		* first, W3DShaderManager). */
	void begin( const Vector3 &eye, Real pull, W3DShroud *shroud )
	{
		m_eye = eye;
		m_pull = pull;
		m_shrouded = shroud != NULL;
		if( shroud != NULL )
		{
			m_shroudOffsetX = shroud->getCellWidth() - shroud->getDrawOriginX();
			m_shroudOffsetY = shroud->getCellHeight() - shroud->getDrawOriginY();
			m_shroudScaleX = 1.0f / ( shroud->getCellWidth() * shroud->getTextureWidth() );
			m_shroudScaleY = 1.0f / ( shroud->getCellHeight() * shroud->getTextureHeight() );
		}
	}

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

	/// a different pull for the quads that follow; the ones already added keep theirs
	void setPull( Real pull )
	{
		flush();
		m_pull = pull;
	}

private:
	enum { MAX_QUADS = 512 };
	struct Vert
	{
		Vector3 pos;
		UnsignedInt diffuse;
	};
	Vert m_verts[ MAX_QUADS * 4 ];
	Int m_quads;
	Vector3 m_eye;
	Real m_pull;
	Bool m_shrouded;
	Real m_shroudOffsetX, m_shroudOffsetY, m_shroudScaleX, m_shroudScaleY;
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
				Vector3 pos = src.pos;
				if( m_pull > 0.0f )
				{
					// along the ray to the eye, so the pixel stays where it was and only the depth moves
					Vector3 toEye = m_eye - pos;
					const Real distance = toEye.Length();
					if( distance > 2.0f * m_pull )
						pos += toEye * ( m_pull / distance );
				}
				vb->x = pos.X;
				vb->y = pos.Y;
				vb->z = pos.Z;
				vb->nx = 0.0f;
				vb->ny = 0.0f;
				vb->nz = 1.0f;
				vb->diffuse = src.diffuse;
				vb->u1 = m_shrouded ? ( src.pos.X + m_shroudOffsetX ) * m_shroudScaleX : 0.0f;
				vb->v1 = m_shrouded ? ( src.pos.Y + m_shroudOffsetY ) * m_shroudScaleY : 0.0f;
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
	* before any quad is added.  pull above nought depth tests the sheet, its vertices that far
	* nearer the eye; shadeByShroud multiplies it by the shroud's texture, the one the ground took.
	* On Direct3D 11 outside Classic that bind is the eased, smoothed picture (Direct3D11_Set_Shroud),
	* so the sheet and the ground under it fade together. */
static void setGroundOverlayState( Real pull, Bool shadeByShroud )
{
	static ShaderClass overlayShader( SC_GROUND_OVERLAY );
	static ShaderClass testedShader( SC_GROUND_OVERLAY_TESTED );
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset( VertexMaterialClass::PRELIT_DIFFUSE );
	DX8Wrapper::Set_Material( material );
	REF_PTR_RELEASE( material );
	W3DShroud *shroud = ( shadeByShroud && TheTerrainRenderObject ) ? TheTerrainRenderObject->getShroud() : NULL;
	TextureClass *shroudTexture = ( shroud != NULL ) ? shroud->getShroudTexture() : NULL;
	if( shroudTexture == NULL )
		shroud = NULL;
	DX8Wrapper::Set_Texture( 0, shroudTexture );
	ShaderClass shader( pull > 0.0f ? testedShader : overlayShader );
	if( shroud != NULL )
		shader.Set_Texturing( ShaderClass::TEXTURING_ENABLE );
	DX8Wrapper::Set_Shader( shader );

	// the eye of the camera drawing this pass, out of the wrapper's own copy of the view (a rotation
	// and a translation, CameraClass::Apply): -R^T t
	Matrix4x4 view;
	DX8Wrapper::Get_Transform( D3DTS_VIEW, view );
	Vector3 eye;
	for( Int j = 0; j < 3; ++j )
		eye[ j ] = -( view[ 0 ][ j ] * view[ 0 ][ 3 ] + view[ 1 ][ j ] * view[ 1 ][ 3 ] + view[ 2 ][ j ] * view[ 2 ][ 3 ] );
	theGroundOverlayQuads.begin( eye, pull, shroud );
	// the quads are in world space; the terrain pass has rendered pucks and bridges since its own
	// identity was set, and the sheet used to inherit whichever of their matrices came last
	DX8Wrapper::Set_Transform( D3DTS_WORLD, Matrix3D( 1 ) );
	DX8Wrapper::Apply_Render_State_Changes();
}

//-------------------------------------------------------------------------------------------------
/** Draw the pathfinder's own cell grid under the structure sitting on the cursor, and cross out
	* the cells it cannot go on.  The lines only under GridBuildPlacement; the washed cells always.
	* GridBuildPlacement snaps a footprint's edges to these very lines
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
	* "Cannot build" here is what the cell itself says, red for an existing structure or rubble,
	* striped for the ground: water, a cliff, plain impassable, the band along the map's edge.  It
	* deliberately does not run the full isLocationLegalToBuild for every cell - that is a
	* per-structure query (build radius, shroud, supply proximity) and a couple of hundred of them a
	* frame is not worth it.  The ghost's own red tint already answers that question for the one spot
	* the cursor is actually on, and the one ground rule no cell can carry alone, a footprint too
	* steep as a whole, stripes the cells under that ghost (takeBuildGrid).
	*
	* The cells are read at the sync point (takeBuildGrid), not here: this runs in the terrain pass,
	* beside the tick. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawBuildGrid( Bool shadeByShroud )
{
	if( m_pendingPlaceType == NULL )
		return;
	// the grid you see is the grid you snap to: with the snap off the lines would mean nothing, but
	// the cells nothing can stand on still do
	const Bool drawLines = gridPlacementOn();
	if( m_placeIcon == NULL || m_placeIcon[ 0 ] == NULL )
		return;
	if( TheTerrainLogic == NULL )
		return;

	// how many cells each way around the cursor we light up
	enum { GRID_RADIUS = BUILD_GRID_RADIUS };
	enum { GRID_CELLS = BUILD_GRID_CELLS, GRID_POINTS = GRID_CELLS + 1 };

	// half the width of a painted line, in world units - a cell is PLACEMENT_CELL (10) across
	const Real LINE_HALF_WIDTH = 0.3f;
	// the ground is sampled at the line, so a line running across a slope would sink into the hill
	// on one side; lifting the whole sheet a hair keeps it out of the dirt without floating
	const Real GRID_LIFT = 0.35f;

	// the pathfinder's own cell indexing, so the lines drawn are the lines it reasons about
	const Int cellX = m_buildGridCellX;
	const Int cellY = m_buildGridCellY;

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
			gz[ iy ][ ix ] = overlayGround( gx[ ix ], gy[ iy ] ) + GRID_LIFT;

			const Real dx = (Real)fabs( ix - mid );
			const Real dy = (Real)fabs( iy - mid );
			const Real d = ( dx > dy ) ? dx : dy;
			const Real f = 1.0f - d / mid;
			fade[ iy ][ ix ] = ( f > 0.0f ) ? f : 0.0f;
		}
	}

	setGroundOverlayState( GRID_PULL, shadeByShroud );

	GroundOverlayQuads &quads = theGroundOverlayQuads;

	// the lines themselves, one quad per cell edge so they follow the ground over every bump.  Faint
	// on purpose: players said the brighter grid hid the ground they were trying to read
	const Real LINE_ALPHA = 0x26;
	for( iy = 0; drawLines && iy < GRID_POINTS; ++iy )
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

	// and a wash over every cell a structure cannot stand on: red where something stands, the game's
	// own hazard stripes, amber on a dark wash, where the ground itself refuses (takeBuildGrid).  The
	// two read apart by pattern as well as by colour, so a player who cannot tell red from amber
	// still can, and the dark between the stripes keeps them on sand and snow, where amber alone
	// washes out.  Filling the cell reads at a glance where an X drawn in thin lines did not.  Four
	// triangles round the cell's middle, sampled too: one quad took a diagonal of its own, and on a
	// cliff cell, the kind most often washed, the other one's half sank metres into the rock under
	// the depth test.  The middle lies on whichever diagonal the ground took, so the fan follows
	// both halves.
	const Real BLOCKED_ALPHA = 0x30;
	const UnsignedInt BLOCKED_RGB = 0x00FF3030;
	const Real GROUND_ALPHA = 0x40;
	const UnsignedInt GROUND_RGB = 0x00101418;		// near black, a little steel in it, as the order rims
	const Real STRIPE_ALPHA = 0x70;
	const UnsignedInt STRIPE_RGB = 0x00FFB000;
	// where a stripe crosses a cell edge, measured along it: half of every diagonal of the cell is stripe
	const Real STRIPE_CUT = 2.5f;
	const Real stripeT = STRIPE_CUT / PATHFIND_CELL_SIZE_F;
	// The striped cells are mostly cliffs, and a grid line runs half a unit off the terrain's own
	// vertices, so across a crease it sags up to about a unit and a half under the drawn rock; at a
	// low camera the grid's pull did not lift that out of the depth test.  They go second, pulled
	// further.
	const Real GROUND_PULL = 5.0f;
	for( Int row = 0; row < 2 * GRID_CELLS; ++row )
	{
		// the patch's rows twice over: the red cells, then the striped ones
		const Int pass = row / GRID_CELLS;
		iy = row % GRID_CELLS;
		if( row == GRID_CELLS )
			quads.setPull( GROUND_PULL );
		for( ix = 0; ix < GRID_CELLS; ++ix )
		{
			const UnsignedByte wash = m_buildGridBlocked[ iy ][ ix ];
			if( wash == BUILD_CELL_CLEAR || ( wash == BUILD_CELL_GROUND ) != ( pass == 1 ) )
				continue;

			if( fade[ iy ][ ix ] <= 0.0f && fade[ iy + 1 ][ ix + 1 ] <= 0.0f )
				continue;

			const Vector3 corner[ 4 ] =
			{
				Vector3( gx[ ix ],     gy[ iy ],     gz[ iy ][ ix ] ),
				Vector3( gx[ ix + 1 ], gy[ iy ],     gz[ iy ][ ix + 1 ] ),
				Vector3( gx[ ix + 1 ], gy[ iy + 1 ], gz[ iy + 1 ][ ix + 1 ] ),
				Vector3( gx[ ix ],     gy[ iy + 1 ], gz[ iy + 1 ][ ix ] )
			};
			const Real cornerFade[ 4 ] = { fade[ iy ][ ix ], fade[ iy ][ ix + 1 ], fade[ iy + 1 ][ ix + 1 ], fade[ iy + 1 ][ ix ] };
			const Real midX = 0.5f * ( gx[ ix ] + gx[ ix + 1 ] ), midY = 0.5f * ( gy[ iy ] + gy[ iy + 1 ] );
			const Vector3 mid( midX, midY, overlayGround( midX, midY ) + GRID_LIFT );
			const Real midFade = 0.25f * ( cornerFade[ 0 ] + cornerFade[ 1 ] + cornerFade[ 2 ] + cornerFade[ 3 ] );
			const Bool ground = wash == BUILD_CELL_GROUND;
			const UnsignedInt rgb = ground ? GROUND_RGB : BLOCKED_RGB;
			const Real alpha = ground ? GROUND_ALPHA : BLOCKED_ALPHA;
			const UnsignedInt midColor = overlayColor( rgb, alpha * midFade );
			for( Int k = 0; k < 4; ++k )
			{
				const Int n = ( k + 1 ) & 3;
				quads.add( corner[ k ], corner[ n ], mid, mid,
									 overlayColor( rgb, alpha * cornerFade[ k ] ),
									 overlayColor( rgb, alpha * cornerFade[ n ] ), midColor, midColor );
			}
			if( !ground )
				continue;

			// The stripes run along x - y, the cell's diagonal from corner 0 to corner 2, so one leaving
			// a cell at a corner goes on in the cell diagonally past it; each cell also paints the tips
			// of its two neighbours' stripes that cross its other corners, or every stripe would pinch
			// to a point there.  Points on an edge go on the ground with its corners; the two either
			// side of the middle are sampled, as the middle is, being off the edges.
			auto onEdge = [&]( Int k, Int n, Real t ) -> Vector3
			{
				return corner[ k ] + ( corner[ n ] - corner[ k ] ) * t;
			};
			auto edgeColor = [&]( Int k, Int n, Real t ) -> UnsignedInt
			{
				return overlayColor( STRIPE_RGB, STRIPE_ALPHA * ( cornerFade[ k ] + ( cornerFade[ n ] - cornerFade[ k ] ) * t ) );
			};
			const Real half = 0.5f * STRIPE_CUT;
			const Vector3 right( midX + half, midY - half, overlayGround( midX + half, midY - half ) + GRID_LIFT );
			const Vector3 left( midX - half, midY + half, overlayGround( midX - half, midY + half ) + GRID_LIFT );
			const UnsignedInt c0 = edgeColor( 0, 0, 0.0f ), c2 = edgeColor( 2, 2, 0.0f );
			const UnsignedInt cMid = overlayColor( STRIPE_RGB, STRIPE_ALPHA * midFade );
			const Vector3 r0 = onEdge( 0, 1, stripeT ), r1 = onEdge( 1, 2, 1.0f - stripeT );
			const Vector3 l0 = onEdge( 0, 3, stripeT ), l1 = onEdge( 3, 2, 1.0f - stripeT );
			const UnsignedInt cr0 = edgeColor( 0, 1, stripeT ), cr1 = edgeColor( 1, 2, 1.0f - stripeT );
			const UnsignedInt cl0 = edgeColor( 0, 3, stripeT ), cl1 = edgeColor( 3, 2, 1.0f - stripeT );
			quads.add( corner[ 0 ], r0, right, mid, c0, cr0, cMid, cMid );
			quads.add( mid, right, r1, corner[ 2 ], cMid, cMid, cr1, c2 );
			quads.add( corner[ 0 ], mid, left, l0, c0, cMid, cMid, cl0 );
			quads.add( mid, corner[ 2 ], l1, left, cMid, c2, cl1, cMid );
			// the neighbours' tips, triangles at corners 1 and 3
			for( Int k = 1; k < 4; k += 2 )
			{
				const Int back = ( k + 3 ) & 3, on = ( k + 1 ) & 3;
				const UnsignedInt ck = edgeColor( k, k, 0.0f );
				const UnsignedInt cb = edgeColor( k, back, stripeT ), co = edgeColor( k, on, stripeT );
				quads.add( corner[ k ], onEdge( k, back, stripeT ), onEdge( k, on, stripeT ), corner[ k ],
									 ck, cb, co, ck );
			}
		}
	}

	quads.flush();

}  // end drawBuildGrid

/** The lowest and highest ground under one footprint's samples (BuildAssistant::iterateFootprint),
	* the two numbers isLocationLegalToBuild's flatness test compares. */
struct BuildGridHeightSpan
{
	Real loZ, hiZ;
};

static void buildGridSampleHeight( const Coord3D *at, void *userData )
{
	BuildGridHeightSpan *span = (BuildGridHeightSpan *)userData;
	if( at->z < span->loZ )
		span->loZ = at->z;
	if( at->z > span->hiZ )
		span->hiZ = at->z;
}

//-------------------------------------------------------------------------------------------------
/** The slot grid of every Lane War build plot (LaneWarDirector), painted on the ground in the
	* terrain pass like the build grid, so a unit standing in a slot stands on top of it.  The plot wears
	* the island's own ground, so the lines take the lane map theme's colour (LaneGridColours), pale on
	* dark ground and a deep shade on light, and the plot's outline is stronger than the lines between
	* its slots.  Nothing else marks a plot.  The bounds come
	* from the director, which sets them at the map's load and never beside a tick. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawLanePlotGrids( Bool shadeByShroud )
{
	if( TheLaneWarDirector == NULL || !TheLaneWarDirector->isActive() || TheTerrainLogic == NULL )
		return;

	const Real SLOT = (Real)LANEWAR_SLOT_SIZE;
	const Real LINE_HALF_WIDTH = 0.6f, EDGE_HALF_WIDTH = 1.2f;
	const Real LIFT = 0.35f;	// as the build grid's

	// the theme's own line colour from the map's name, read once a map (black on anything else)
	static AsciiString s_colourMap;
	static UnsignedInt s_line = overlayColor( 0x00000000, 0x40 ), s_edge = overlayColor( 0x00000000, 0x70 );
	if( TheGlobalData != NULL && s_colourMap != TheGlobalData->m_mapName )
	{
		s_colourMap = TheGlobalData->m_mapName;
		if( !LaneGridColours( s_colourMap, &s_line, &s_edge ) )
		{
			s_line = overlayColor( 0x00000000, 0x40 );
			s_edge = overlayColor( 0x00000000, 0x70 );
		}
	}
	const UnsignedInt LINE = s_line, EDGE = s_edge;

	// depth tested and shaded by the shroud as the build grid is, so a hill or a deck covers it
	setGroundOverlayState( GRID_PULL, shadeByShroud );
	GroundOverlayQuads &quads = theGroundOverlayQuads;
	for( Int r = 0; r < LANEWAR_TEAMS * LANEWAR_SEATS; ++r )
	{
		{
			Coord2D lo, hi;
			if( !TheLaneWarDirector->getPlotBounds( r / LANEWAR_SEATS, r % LANEWAR_SEATS, &lo, &hi ) )
				continue;
			// lines every slot from the low corner; the director counts from the front left, which gives
			// the same lines while a plot is whole slots a side, as every generated one is
			const Int slotsX = REAL_TO_INT_FLOOR( ( hi.x - lo.x ) / SLOT + 0.5f );
			const Int slotsY = REAL_TO_INT_FLOOR( ( hi.y - lo.y ) / SLOT + 0.5f );

			// lines along y, one quad a slot so each follows the ground
			for( Int k = 0; k <= slotsX; ++k )
			{
				const Bool edge = k == 0 || k == slotsX;
				const Real x = min( lo.x + k * SLOT, hi.x ), w = edge ? EDGE_HALF_WIDTH : LINE_HALF_WIDTH;
				const UnsignedInt c = edge ? EDGE : LINE;
				for( Int j = 0; j < slotsY; ++j )
				{
					const Real y0 = lo.y + j * SLOT, y1 = min( y0 + SLOT, hi.y );
					const Real z0 = overlayGround( x, y0 ) + LIFT, z1 = overlayGround( x, y1 ) + LIFT;
					quads.add( Vector3( x - w, y0, z0 ), Vector3( x + w, y0, z0 ), Vector3( x + w, y1, z1 ), Vector3( x - w, y1, z1 ),
										 c, c, c, c );
				}
			}

			// and along x
			for( Int k = 0; k <= slotsY; ++k )
			{
				const Bool edge = k == 0 || k == slotsY;
				const Real y = min( lo.y + k * SLOT, hi.y ), w = edge ? EDGE_HALF_WIDTH : LINE_HALF_WIDTH;
				const UnsignedInt c = edge ? EDGE : LINE;
				for( Int j = 0; j < slotsX; ++j )
				{
					const Real x0 = lo.x + j * SLOT, x1 = min( x0 + SLOT, hi.x );
					const Real z0 = overlayGround( x0, y ) + LIFT, z1 = overlayGround( x1, y ) + LIFT;
					quads.add( Vector3( x0, y - w, z0 ), Vector3( x1, y - w, z1 ), Vector3( x1, y + w, z1 ), Vector3( x0, y + w, z0 ),
										 c, c, c, c );
				}
			}
		}
	}
	quads.flush();

}  // end drawLanePlotGrids

//-------------------------------------------------------------------------------------------------
/** Which cells of the patch round the ghost drawBuildGrid washes, and as what, read at the sync
	* point.  Red is something standing there.  The pathfinder marks every structure the moment it is
	* placed, the enemy's in the shroud included, so its cells alone drew enemy foundations into the
	* dark: a cell held by something the local player has no picture of stays clear here, and so does
	* rubble under the shroud, while a building he still sees standing in the fog after it died is
	* washed from its ghost.  Striped is the ground itself, the map's own and always shown: water,
	* cliffs, plain impassable, the band along the map's edge, and the ghost's own footprint when it is
	* too steep as a whole or sits on a bridge.  Nothing here is written anywhere; every query is one
	* the ghost's own legality check already makes on this side. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::takeBuildGrid( void )
{
	if( m_pendingPlaceType == NULL || m_placeIcon == NULL || m_placeIcon[ 0 ] == NULL )
		return;

	const Coord3D *center = m_placeIcon[ 0 ]->getPosition();
	m_buildGridCellX = REAL_TO_INT_FLOOR( (center->x + 0.5f) / PATHFIND_CELL_SIZE_F ) - BUILD_GRID_RADIUS;
	m_buildGridCellY = REAL_TO_INT_FLOOR( (center->y + 0.5f) / PATHFIND_CELL_SIZE_F ) - BUILD_GRID_RADIUS;

	Pathfinder *pathfinder = TheAI->pathfinder();
	const Int me = ThePlayerList->getLocalPlayer()->getPlayerIndex();

	// the map's own refusals, the ones checkSampleBuildLocation makes at every sample: off the
	// pathfinder's map, inside MinDistFromEdgeOfMapForBuild of the edge, and a cell the pathfinder
	// calls water, cliff or impassable.  A cell is in the band when either of its grid lines is,
	// which is exactly where a footprint snapped to that line puts its edge sample.
	Region3D pathfindExtent, mapExtent;
	TheTerrainLogic->getMaximumPathfindExtent( &pathfindExtent );
	TheTerrainLogic->getExtent( &mapExtent );
	const Real edge = TheGlobalData->m_MinDistFromEdgeOfMapForBuild;
	// the trainer's build anywhere leaves the ground rules out of the ghost's check, and so out of here
	const Bool groundRules = BitTest( placementCheckOptions(), BuildAssistant::TERRAIN_RESTRICTIONS );

	for( Int iy = 0; iy < BUILD_GRID_CELLS; ++iy )
	{
		for( Int ix = 0; ix < BUILD_GRID_CELLS; ++ix )
		{
			const Real loX = placementGridLine( m_buildGridCellX + ix ), loY = placementGridLine( m_buildGridCellY + iy );
			const Real hiX = loX + PATHFIND_CELL_SIZE_F, hiY = loY + PATHFIND_CELL_SIZE_F;
			Coord3D at;
			at.x = 0.5f * ( loX + hiX );
			at.y = 0.5f * ( loY + hiY );
			at.z = 0.0f;

			const PathfindCell *cell = pathfinder->getCell( LAYER_GROUND, m_buildGridCellX + ix, m_buildGridCellY + iy );
			UnsignedByte wash = BUILD_CELL_CLEAR;
			if( cell == NULL || !pathfindExtent.isInRegionNoZ( &at ) )
				wash = BUILD_CELL_GROUND;
			else if( edge > 0.0f && ( loX < mapExtent.lo.x + edge || hiX > mapExtent.hi.x - edge ||
																loY < mapExtent.lo.y + edge || hiY > mapExtent.hi.y - edge ) )
				wash = BUILD_CELL_GROUND;
			else if( cell->getType() == PathfindCell::CELL_OBSTACLE )
			{
				// the cell can still name an object the tick has already destroyed; nothing is left to
				// hide, and the pathfinder still refuses the cell, so it draws red
				const Object *obstacle = TheGameLogic->findObjectByID( cell->getObstacleID() );
				if( obstacle == NULL || !BuildAssistant_isHiddenFromLocalPlayer( obstacle ) )
					wash = BUILD_CELL_OBJECT;
			}
			else if( cell->getType() == PathfindCell::CELL_RUBBLE )
			{
				if( ThePartitionManager->getShroudStatusForPlayer( me, &at ) != CELLSHROUD_SHROUDED )
					wash = BUILD_CELL_OBJECT;
			}
			else if( cell->getType() != PathfindCell::CELL_CLEAR )
				wash = BUILD_CELL_GROUND;		// water, cliff, plain impassable

			m_buildGridBlocked[ iy ][ ix ] = ( wash == BUILD_CELL_GROUND && !groundRules ) ? BUILD_CELL_CLEAR : wash;
		}
	}

	// A building that died in his fog still stands there for him, drawn from its ghost, but the
	// pathfinder let go of its cells the moment it died: wash the footprint the ghost remembers, or
	// bare ground under the ghost tells him it is gone.
	enum { MAX_GHOSTS = 16 };
	const GhostObject *ghosts[ MAX_GHOSTS ];
	const Int ghostCount = BuildAssistant_findDeadGhostsInFog( center, BUILD_GRID_RADIUS * PATHFIND_CELL_SIZE_F,
																														 ghosts, MAX_GHOSTS );
	for( Int g = 0; g < ghostCount; ++g )
	{
		const Coord3D *at = ghosts[ g ]->getParentPosition();
		const Real reach = ghosts[ g ]->getGeometryMajorRadius() + ghosts[ g ]->getGeometryMinorRadius() +
											 PATHFIND_CELL_SIZE_F;	// a round one reaches four tenths of a cell past its radius
		const Int loX = max( 0, REAL_TO_INT_FLOOR( (at->x - reach) / PATHFIND_CELL_SIZE_F ) - m_buildGridCellX );
		const Int loY = max( 0, REAL_TO_INT_FLOOR( (at->y - reach) / PATHFIND_CELL_SIZE_F ) - m_buildGridCellY );
		const Int hiX = min( BUILD_GRID_CELLS - 1, REAL_TO_INT_FLOOR( (at->x + reach) / PATHFIND_CELL_SIZE_F ) - m_buildGridCellX );
		const Int hiY = min( BUILD_GRID_CELLS - 1, REAL_TO_INT_FLOOR( (at->y + reach) / PATHFIND_CELL_SIZE_F ) - m_buildGridCellY );

		for( Int iy = loY; iy <= hiY; ++iy )
		{
			for( Int ix = loX; ix <= hiX; ++ix )
			{
				if( BuildAssistant_ghostCoversCell( ghosts[ g ], m_buildGridCellX + ix, m_buildGridCellY + iy ) )
					m_buildGridBlocked[ iy ][ ix ] = BUILD_CELL_OBJECT;
			}
		}
	}

	// What the cells cannot say: a footprint too steep as a whole (AllowedHeightVariationForBuilding
	// over every sample of it, which a gentle slope fails for a big building and passes for a small
	// one) and a centre on a bridge's deck.  Judged on each placement ghost where it stands, with the
	// check's own sampler, so the cells under a ghost the check turned red for the ground are striped
	// and a green one never is.
	for( Int i = 0; groundRules && i < TheGlobalData->m_maxLineBuildObjects && m_placeIcon[ i ] != NULL; ++i )
	{
		Coord3D at = *m_placeIcon[ i ]->getPosition();
		const Real angle = m_placeIcon[ i ]->getOrientation();
		const GeometryInfo &geom = m_pendingPlaceType->getTemplateGeometryInfo();

		BuildGridHeightSpan span;
		span.loZ = mapExtent.hi.z;		// the check starts its two the wrong way round, as here
		span.hiZ = mapExtent.lo.z;
		TheBuildAssistant->iterateFootprint( m_pendingPlaceType, angle, &at, PATHFIND_CELL_SIZE_F,
																				 buildGridSampleHeight, &span );
		const Bool steep = span.hiZ - span.loZ > TheGlobalData->m_allowedHeightVariationForBuilding;
		if( !steep && TheTerrainLogic->getLayerForDestination( &at ) == LAYER_GROUND )
			continue;

		// stripe every clear cell whose middle the footprint covers, in iterateFootprint's frame: the
		// major radius along the ghost's own x
		const Bool box = geom.getGeomType() == GEOMETRY_BOX;
		const Real reach = geom.getBoundingCircleRadius();
		const Real halfX = box ? geom.getMajorRadius() : reach;
		const Real halfY = box ? geom.getMinorRadius() : reach;
		const Real c = (Real)cos( angle ), s = (Real)sin( angle );
		const Int loX = max( 0, REAL_TO_INT_FLOOR( (at.x - reach + 0.5f) / PATHFIND_CELL_SIZE_F ) - m_buildGridCellX );
		const Int loY = max( 0, REAL_TO_INT_FLOOR( (at.y - reach + 0.5f) / PATHFIND_CELL_SIZE_F ) - m_buildGridCellY );
		const Int hiX = min( BUILD_GRID_CELLS - 1, REAL_TO_INT_FLOOR( (at.x + reach + 0.5f) / PATHFIND_CELL_SIZE_F ) - m_buildGridCellX );
		const Int hiY = min( BUILD_GRID_CELLS - 1, REAL_TO_INT_FLOOR( (at.y + reach + 0.5f) / PATHFIND_CELL_SIZE_F ) - m_buildGridCellY );
		for( Int iy = loY; iy <= hiY; ++iy )
		{
			for( Int ix = loX; ix <= hiX; ++ix )
			{
				if( m_buildGridBlocked[ iy ][ ix ] != BUILD_CELL_CLEAR )
					continue;
				const Real dx = placementGridLine( m_buildGridCellX + ix ) + 0.5f * PATHFIND_CELL_SIZE_F - at.x;
				const Real dy = placementGridLine( m_buildGridCellY + iy ) + 0.5f * PATHFIND_CELL_SIZE_F - at.y;
				const Bool inside = box ? ( fabs( dx * c + dy * s ) <= halfX && fabs( -dx * s + dy * c ) <= halfY )
																: ( dx * dx + dy * dy <= reach * reach );
				if( inside )
					m_buildGridBlocked[ iy ][ ix ] = BUILD_CELL_GROUND;
			}
		}
	}

}  // end takeBuildGrid

//-------------------------------------------------------------------------------------------------
/** The wash inside the circle a left drag is sweeping targets out of.  The rim is a screen space
	* line drawn with everything else; this is the ground it encloses, laid down in the terrain pass
	* so it bends over every slope inside it and so the tanks standing in the circle are drawn on top
	* of the wash instead of under it.
	*
	* Rings rather than one fan from the centre: a fan spans a valley with a single flat triangle,
	* and the circles worth drawing are big enough to cross one. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAttackCircleFill( Bool shadeByShroud )
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
			ring[ r ][ s ].Set( x, y, overlayGround( x, y ) + FILL_LIFT );
		}
	}

	setGroundOverlayState( 0.0f, shadeByShroud );

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

enum { ORDER_RING_SEGMENTS = 16 };		// a dot a few pixels across

// Every order mark on the ground is its colour on a dark rim, so a green ring still reads on snow
// and a gold one on sand: the rim goes down first, every rim before every colour, so one unit's rim
// never cuts into its neighbour's ring
static const UnsignedInt ORDER_RIM_RGB = 0x00101418;	// near black, a little steel in it
static const Real ORDER_RIM_ALPHA = 0x78;
static const Real ORDER_MARK_LIFT = 0.35f;				// the build grid's: out of the dirt, not floating
static const Real ORDER_MARK_PULL = 1.5f;					// towards the eye for the depth test, times the zoom (setGroundOverlayState)
static const Real ORDER_MARK_MIN_ZOOM = 0.6f;			// widths follow the camera's zoom between these, so
static const Real ORDER_MARK_MAX_ZOOM = 4.5f;			// a mark keeps about its width on the screen, out to the
																									// wheel's ceiling near 3.9 (W3DWaypointBuffer's ORDER_MAX_ZOOM_SCALE)

// Each unit's own destination: a dot on its rim, a point and not a circle. Rings the size of the
// body drew a grid of ghost selection rings over a group's formation and drowned the click marker
static const Real ORDER_DOT_RADIUS = 1.1f;
static const Real ORDER_DOT_RIM = 0.4f;
static const Real ORDER_RING_ALPHA = 0xD0;

// the marker where an order lands: a ring closing on the point with four arrowheads on it pointing
// in, there and gone inside InGameUI::MOVE_MARKER_MS. The loudest mark on the ground: twice the
// destination dot's width of band, solid colour
static const Real MARKER_START_RADIUS = 14.0f;
static const Real MARKER_END_RADIUS = 3.2f;
static const Real MARKER_CLOSE_MS = 320.0f;				// to close on the point, easing in
static const Real MARKER_IN_MS = 50.0f;
static const Real MARKER_FADE_MS = 260.0f;				// the last of it
static const Real MARKER_WIDTH = 0.95f;
static const Real MARKER_RIM = 0.4f;
static const Real MARKER_ARROW_GAP = 0.7f;				// between the ring and an arrowhead's tip
static const Real MARKER_ARROW_ARM = 2.6f;
static const Real MARKER_ARROW_SPREAD = 0.62f;		// radians off the spoke, each arm
static const Real MARKER_DOT_RADIUS = 1.2f;				// on the point once the ring is there
static const Real MARKER_ALPHA = 0xFF;
enum { MARKER_SEGMENTS = 32, MARKER_ARROWS = 4, MARKER_DOT_SEGMENTS = 10 };

//-------------------------------------------------------------------------------------------------
/** The order rings' and markers' ground, at the sync point: the terrain heights are logic's, and
	* drawOrderRings runs in the terrain pass beside the tick.  Each destination dot is sampled at its
	* own vertices, so one on a slope lies on it; one on a bridge is flat at the deck's height.  A
	* marker moves every picture, so it takes a grid of heights over what it covers now: it only
	* closes from here, so a picture drawn later still lies inside the grid.
	* ponytail: every dot's ground is sampled again each frame of its fade, 68 heights a dot, and a
	* marker's 289; keep the points of a settled dot if a few hundred at once ever show */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::takeOrderRings( void )
{
	const UnsignedInt now = Clock_Milliseconds();
	m_orderMarkScale = MAX( ORDER_MARK_MIN_ZOOM, MIN( ORDER_MARK_MAX_ZOOM, TheTacticalView->getZoom() ) );
	const Real dot = ORDER_DOT_RADIUS * m_orderMarkScale;
	const Real rim = ORDER_DOT_RIM * m_orderMarkScale;

	const std::vector<OrderRing>& rings = getOrderRings();
	for( std::vector<OrderRing>::const_iterator ring = rings.begin(); ring != rings.end(); ++ring )
	{
		if( ring->bornMs == 0 || now - ring->bornMs >= (UnsignedInt)ORDER_RING_MS )
			continue;
		// paint in the terrain pass lies under the water, which tinted it; that dot goes after the water
		// (W3DWaypointBuffer::takeWaterMarks)
		if( isOrderMarkOnWater( ring->at.x, ring->at.y, NULL ) )
			continue;

		m_orderRingColors.push_back( orderKindColor( ring->kind ) );
		m_orderRingBornMs.push_back( ring->bornMs );

		// the same small dot under an infantryman and a tank: the rim's disc (0 to 3), the colour's
		// inside it (1 to 2)
		const Real radii[ 4 ] = { 0.0f, 0.0f, dot, dot + rim };
		for( Int s = 0; s <= ORDER_RING_SEGMENTS; ++s )
		{
			const Real angle = 2.0f * PI * (Real)s / (Real)ORDER_RING_SEGMENTS;
			const Real c = (Real)cos( angle );
			const Real n = (Real)sin( angle );
			for( Int k = 0; k < 4; ++k )
			{
				const Real x = ring->at.x + radii[ k ] * c, y = ring->at.y + radii[ k ] * n;
				const Real z = ring->onGround ? overlayGround( x, y ) : ring->at.z;
				m_orderRingPoints.push_back( Vector3( x, y, z + ORDER_MARK_LIFT ) );
			}
		}
	}

	// Classic draws EA's ring instead (drawMoveHints)
	if( TheGlobalData->m_classicGraphics )
		return;
	for( Int i = 0; i < MAX_MOVE_HINTS; ++i )
	{
		const MoveHintStruct& hint = m_moveHint[ i ];
		if( hint.bornMs == 0 || now - hint.bornMs >= (UnsignedInt)MOVE_MARKER_MS )
			continue;
		const Real x = hint.pos.x, y = hint.pos.y;
		if( isOrderMarkOnWater( x, y, NULL ) )
			continue;		// after the water, on the sorted list (W3DWaypointBuffer::takeWaterMarks)
		m_orderMarkers.push_back( OrderMarker() );
		OrderMarker &mark = m_orderMarkers.back();
		mark.rgb = orderKindColor( hint.kind );
		mark.bornMs = hint.bornMs;
		mark.reach = 0.0f;
		const Real ground = overlayGround( x, y );
		// a point well over the ground is a bridge's deck: flat there
		const Bool deck = hint.pos.z > ground + 2.0f;
		mark.at.Set( x, y, deck ? hint.pos.z : ground );
		if( !deck )
		{
			// the ring as wide as it is now, its arrowheads and rim outside it (drawOrderRings)
			const Real close = MIN( 1.0f, (Real)( now - hint.bornMs ) / MARKER_CLOSE_MS );
			const Real left = 1.0f - close;
			const Real radius = MARKER_END_RADIUS + ( MARKER_START_RADIUS - MARKER_END_RADIUS ) * left * left * left;
			mark.reach = ( radius + MARKER_ARROW_GAP + MARKER_WIDTH + MARKER_ARROW_ARM + 2.0f * MARKER_RIM ) * m_orderMarkScale + 1.0f;
			const Real step = 2.0f * mark.reach / (Real)( ORDER_MARKER_GRID - 1 );
			for( Int gy = 0; gy < ORDER_MARKER_GRID; ++gy )
				for( Int gx = 0; gx < ORDER_MARKER_GRID; ++gx )
					mark.ground[ gy * ORDER_MARKER_GRID + gx ] = overlayGround( x - mark.reach + gx * step, y - mark.reach + gy * step );
		}
	}
}

/** The ground under a marker at x, y: its grid's heights, bilinear between the samples, which are
	* closer than a terrain cell until the camera is pulled most of the way out. */
static Real markerGround( const Vector3 &at, Real reach, const Real *heights, Int grid, Real x, Real y )
{
	if( reach <= 0.0f )
		return at.Z;
	const Real step = 2.0f * reach / (Real)( grid - 1 );
	Real fx = ( x - ( at.X - reach ) ) / step;
	Real fy = ( y - ( at.Y - reach ) ) / step;
	fx = MAX( 0.0f, MIN( (Real)( grid - 1 ), fx ) );
	fy = MAX( 0.0f, MIN( (Real)( grid - 1 ), fy ) );
	const Int x0 = MIN( grid - 2, REAL_TO_INT_FLOOR( fx ) );
	const Int y0 = MIN( grid - 2, REAL_TO_INT_FLOOR( fy ) );
	const Real s = fx - (Real)x0, t = fy - (Real)y0;
	const Real *row0 = heights + y0 * grid;
	const Real *row1 = row0 + grid;
	const Real top = row0[ x0 ] + s * ( row0[ x0 + 1 ] - row0[ x0 ] );
	const Real bottom = row1[ x0 ] + s * ( row1[ x0 + 1 ] - row1[ x0 ] );
	return top + t * ( bottom - top );
}

//-------------------------------------------------------------------------------------------------
/** One dot under each unit where its order really ends, and the marker where an order was given,
	* painted on the ground in the terrain pass like the attack circle's wash, so the unit standing in
	* it is drawn over it.  Rims first, every one of them, then the colours.  Depth tested, so a hill or
	* a bridge deck in front of the point hides it.  Both terrains draw it after their shroud and ask
	* for shadeByShroud, the shroud's texture multiplied in as the ground took it: soft on Direct3D 11
	* outside Classic, a texel a cell elsewhere, black on black shroud. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawOrderRings( Bool shadeByShroud )
{
	if( m_orderRingColors.empty() && m_orderMarkers.empty() )
		return;
	// not in the water's mirror pass: they would lie on the reflection, as the order lines would
	// (W3DWaypointBuffer::drawWaypoints)
	if( ShaderClass::Is_Backface_Culling_Inverted() )
		return;

	setGroundOverlayState( ORDER_MARK_PULL * m_orderMarkScale, shadeByShroud );

	// the fade and the marker's closing are worked out here, from this picture's clock: a picture
	// drawn again beside the tick (phase 4) takes nothing new at the sync point, and anything baked
	// in there froze, then jumped
	const UnsignedInt now = Clock_Milliseconds();
	GroundOverlayQuads &quads = theGroundOverlayQuads;
	const Int fours = ORDER_RING_SEGMENTS + 1;
	const Real scale = m_orderMarkScale;

	for( Int pass = 0; pass < 2; ++pass )
	{
		const Bool rimPass = ( pass == 0 );
		for( size_t r = 0; r < m_orderRingColors.size(); ++r )
		{
			const UnsignedInt age = now - m_orderRingBornMs[ r ];
			if( age >= (UnsignedInt)ORDER_RING_MS )
				continue;
			const Real fade = 1.0f - (Real)age / (Real)ORDER_RING_MS;
			const UnsignedInt color = rimPass ? overlayColor( ORDER_RIM_RGB, ORDER_RIM_ALPHA * fade )
																				: overlayColor( m_orderRingColors[ r ], ORDER_RING_ALPHA * fade );
			const Int from = rimPass ? 0 : 1;		// the rim spans all four radii, the colour the middle two
			const Int to = rimPass ? 3 : 2;
			const Vector3 *p = &m_orderRingPoints[ r * fours * 4 ];
			for( Int s = 0; s < ORDER_RING_SEGMENTS; ++s )
			{
				const Vector3 *a = p + s * 4;
				const Vector3 *b = p + ( s + 1 ) * 4;
				quads.add( a[ from ], a[ to ], b[ to ], b[ from ], color, color, color, color );
			}
		}

		for( size_t m = 0; m < m_orderMarkers.size(); ++m )
		{
			const OrderMarker& mark = m_orderMarkers[ m ];
			const Real age = (Real)( now - mark.bornMs );
			if( age >= (Real)MOVE_MARKER_MS )
				continue;
			const Real fade = MIN( 1.0f, age / MARKER_IN_MS ) * MIN( 1.0f, ( (Real)MOVE_MARKER_MS - age ) / MARKER_FADE_MS );
			const UnsignedInt color = rimPass ? overlayColor( ORDER_RIM_RGB, ORDER_RIM_ALPHA * fade )
																				: overlayColor( mark.rgb, MARKER_ALPHA * fade );
			const Real grow = rimPass ? MARKER_RIM * scale : 0.0f;		// the rim is the same shape, this much fatter
			auto ground = [&]( Real x, Real y ) -> Vector3
			{
				return Vector3( x, y, markerGround( mark.at, mark.reach, mark.ground, ORDER_MARKER_GRID, x, y ) + ORDER_MARK_LIFT );
			};
			// a straight band from a to b, half as wide as given, grown at both ends by its rim
			auto band = [&]( Real ax, Real ay, Real bx, Real by, Real half )
			{
				Real dx = bx - ax, dy = by - ay;
				const Real length = (Real)sqrt( dx * dx + dy * dy );
				if( length < 0.001f )
					return;
				dx /= length;
				dy /= length;
				ax -= dx * grow;  ay -= dy * grow;
				bx += dx * grow;  by += dy * grow;
				const Real nx = -dy * half, ny = dx * half;
				quads.add( ground( ax + nx, ay + ny ), ground( bx + nx, by + ny ), ground( bx - nx, by - ny ), ground( ax - nx, ay - ny ),
									 color, color, color, color );
			};

			// closing on the point, fast and then slower, and resting there for the rest of its life
			const Real close = MIN( 1.0f, age / MARKER_CLOSE_MS );
			const Real left = 1.0f - close;
			const Real radius = ( MARKER_END_RADIUS + ( MARKER_START_RADIUS - MARKER_END_RADIUS ) * left * left * left ) * scale;
			const Real half = 0.5f * MARKER_WIDTH * scale + grow;
			for( Int s = 0; s < MARKER_SEGMENTS; ++s )
			{
				const Real a0 = 2.0f * PI * (Real)s / (Real)MARKER_SEGMENTS;
				const Real a1 = 2.0f * PI * (Real)( s + 1 ) / (Real)MARKER_SEGMENTS;
				const Real c0 = (Real)cos( a0 ), n0 = (Real)sin( a0 );
				const Real c1 = (Real)cos( a1 ), n1 = (Real)sin( a1 );
				const Real in = radius - half, out = radius + half;
				quads.add( ground( mark.at.X + in * c0, mark.at.Y + in * n0 ), ground( mark.at.X + out * c0, mark.at.Y + out * n0 ),
									 ground( mark.at.X + out * c1, mark.at.Y + out * n1 ), ground( mark.at.X + in * c1, mark.at.Y + in * n1 ),
									 color, color, color, color );
			}

			// the arrowheads, outside the ring on the diagonals, their tips at it
			const Real armHalf = 0.5f * MARKER_WIDTH * scale + grow;
			for( Int k = 0; k < MARKER_ARROWS; ++k )
			{
				const Real spoke = 0.25f * PI + 2.0f * PI * (Real)k / (Real)MARKER_ARROWS;
				const Real tip = radius + MARKER_ARROW_GAP * scale + 0.5f * MARKER_WIDTH * scale;
				const Real tx = mark.at.X + tip * (Real)cos( spoke );
				const Real ty = mark.at.Y + tip * (Real)sin( spoke );
				for( Int side = -1; side <= 1; side += 2 )
				{
					const Real arm = spoke + side * MARKER_ARROW_SPREAD;
					band( tx, ty, tx + MARKER_ARROW_ARM * scale * (Real)cos( arm ), ty + MARKER_ARROW_ARM * scale * (Real)sin( arm ), armHalf );
				}
			}

			// and once it is there, a dot on the point itself
			if( close >= 1.0f )
			{
				const Real dot = ( MARKER_DOT_RADIUS * scale ) + grow;
				const Vector3 centre = ground( mark.at.X, mark.at.Y );
				for( Int s = 0; s < MARKER_DOT_SEGMENTS; ++s )
				{
					const Real a0 = 2.0f * PI * (Real)s / (Real)MARKER_DOT_SEGMENTS;
					const Real a1 = 2.0f * PI * (Real)( s + 1 ) / (Real)MARKER_DOT_SEGMENTS;
					quads.add( centre, ground( mark.at.X + dot * (Real)cos( a0 ), mark.at.Y + dot * (Real)sin( a0 ) ),
										 ground( mark.at.X + dot * (Real)cos( a1 ), mark.at.Y + dot * (Real)sin( a1 ) ), centre,
										 color, color, color, color );
				}
			}
		}
	}

	quads.flush();

}  // end drawOrderRings

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
/** draw the line an armed left drag is spreading the selection along.  Where each unit will stand is
	* said by the cursor sitting there, not by a mark on the line */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawFormationLine( void )
{
	const Real width = 2.0f;
	// the line says which order it is about to be, in the colours the order lines and markers use
	const OrderHintKind kind = isInAttackMoveToMode() ? ORDER_HINT_ATTACK_MOVE
											: ( isForceAttackArmed() || isForceLineHeld() ) ? ORDER_HINT_FORCE_ATTACK
											: isGuardArmed() ? ORDER_HINT_GUARD
											: ORDER_HINT_MOVE;
	const UnsignedInt color = 0xCC000000 | orderKindColor( kind );  //0xAARRGGBB
	const std::vector<ICoord2D>& curve = m_formationDragPoints;
	const Int points = (Int)curve.size();
	if( points < 2 )
		return;

	// the kept points are a dozen pixels apart or more, and joined straight they read as a jagged
	// polyline.  A Catmull-Rom spline through them draws the same curve smooth and still passes
	// through every point the order carries, so the picture and the order agree where it matters.
	// Centripetal (knots spaced by the square root of the distance): the spacing is uneven, since the
	// last point rides the cursor and the spacing doubles when the curve is thinned, and the uniform
	// spline looped at the cursor and bulged out of sharp corners on that
	const Int STEPS = 8;
	const Real MIN_KNOT = 0.01f;		// two points on the same pixel would make a zero knot interval
	TheDisplay->beginBatch2D();
	Real lastX = (Real)curve[ 0 ].x;
	Real lastY = (Real)curve[ 0 ].y;
	for( Int i = 1; i < points; ++i )
	{
		Real px[ 4 ], py[ 4 ];
		px[ 1 ] = (Real)curve[ i - 1 ].x;		py[ 1 ] = (Real)curve[ i - 1 ].y;
		px[ 2 ] = (Real)curve[ i ].x;				py[ 2 ] = (Real)curve[ i ].y;
		// the ends get a point mirrored through them, so the curve leaves and arrives straight
		if( i > 1 )			{ px[ 0 ] = (Real)curve[ i - 2 ].x;	py[ 0 ] = (Real)curve[ i - 2 ].y; }
		else						{ px[ 0 ] = 2.0f * px[ 1 ] - px[ 2 ];	py[ 0 ] = 2.0f * py[ 1 ] - py[ 2 ]; }
		if( i + 1 < points ) { px[ 3 ] = (Real)curve[ i + 1 ].x;	py[ 3 ] = (Real)curve[ i + 1 ].y; }
		else						{ px[ 3 ] = 2.0f * px[ 2 ] - px[ 1 ];	py[ 3 ] = 2.0f * py[ 2 ] - py[ 1 ]; }

		Real knot[ 4 ];
		knot[ 0 ] = 0.0f;
		for( Int k = 1; k < 4; ++k )
		{
			const Real dx = px[ k ] - px[ k - 1 ];
			const Real dy = py[ k ] - py[ k - 1 ];
			knot[ k ] = knot[ k - 1 ] + max( MIN_KNOT, (Real)sqrt( sqrt( dx * dx + dy * dy ) ) );
		}

		for( Int s = 1; s <= STEPS; ++s )
		{
			// Barry and Goldman's pyramid, which is the spline for any knot spacing
			const Real t = knot[ 1 ] + ( knot[ 2 ] - knot[ 1 ] ) * (Real)s / (Real)STEPS;
			Real ax[ 3 ], ay[ 3 ];
			for( Int k = 0; k < 3; ++k )
			{
				const Real w = ( t - knot[ k ] ) / ( knot[ k + 1 ] - knot[ k ] );
				ax[ k ] = px[ k ] + ( px[ k + 1 ] - px[ k ] ) * w;
				ay[ k ] = py[ k ] + ( py[ k + 1 ] - py[ k ] ) * w;
			}
			Real bx[ 2 ], by[ 2 ];
			for( Int k = 0; k < 2; ++k )
			{
				const Real w = ( t - knot[ k ] ) / ( knot[ k + 2 ] - knot[ k ] );
				bx[ k ] = ax[ k ] + ( ax[ k + 1 ] - ax[ k ] ) * w;
				by[ k ] = ay[ k ] + ( ay[ k + 1 ] - ay[ k ] ) * w;
			}
			const Real w = ( t - knot[ 1 ] ) / ( knot[ 2 ] - knot[ 1 ] );
			const Real x = bx[ 0 ] + ( bx[ 1 ] - bx[ 0 ] ) * w;
			const Real y = by[ 0 ] + ( by[ 1 ] - by[ 0 ] ) * w;
			TheDisplay->drawLine( (Int)floorf( lastX + 0.5f ), (Int)floorf( lastY + 0.5f ),
														(Int)floorf( x + 0.5f ), (Int)floorf( y + 0.5f ), width, color );
			lastX = x;
			lastY = y;
		}
	}
	TheDisplay->endBatch2D();

}  // end drawFormationLine

//-------------------------------------------------------------------------------------------------
/** A circle in the plane of the press, not rayed onto the terrain: a hill under the rim used to
	* jump the radius every frame. */
//-------------------------------------------------------------------------------------------------
static void drawGroundRing( const Coord3D& center, Real radius, UnsignedInt color, Real width )
{
	const Int segments = 48;

	ICoord2D previous;
	Bool havePrevious = FALSE;
	for( Int i = 0; i <= segments; ++i )
	{
		const Real angle = 2.0f * PI * (Real)i / (Real)segments;
		Coord3D world;
		world.x = center.x + radius * (Real)cos( angle );
		world.y = center.y + radius * (Real)sin( angle );
		world.z = center.z;

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
}

//-------------------------------------------------------------------------------------------------
/** draw the circle a left drag is sweeping targets out of */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAttackCircle( void )
{
	Coord3D center;
	Real radius;
	if( !getAttackCircleGround( center, radius ) )
		return;

	drawGroundRing( center, radius, 0xCCFF5555, 2.0f );		// the attack red the hints use

	// and the radius itself, so the drag reads as a radius rather than a rubber band
	ICoord2D middle;
	if( TheTacticalView->worldToScreenTriReturn( &center, &middle ) != View::WTS_INVALID )
		TheDisplay->drawLine( middle.x, middle.y, getAttackCircleCursor().x,
													getAttackCircleCursor().y, 1.0f, 0x66FF5555 );

}  // end drawAttackCircle

//-------------------------------------------------------------------------------------------------
/** Player::iterateObjects callback: the spot over the head of one of the local player's guards, into
	* the vector of them. */
//-------------------------------------------------------------------------------------------------
static void addGuardMarker( Object *obj, void *tops )
{
	if( !obj->isLocallyControlled() || obj->isEffectivelyDead() || obj->getContainedBy() )
		return;
	const AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || ai->getCurrentStateID() != AI_GUARD )
		return;
	const Drawable *draw = obj->getDrawable();
	if( draw == NULL || draw->isDrawableEffectivelyHidden() )
		return;

	Coord3D top = *obj->getPosition();
	top.z += obj->getGeometryInfo().getMaxHeightAbovePosition();
	( (std::vector<Coord3D> *)tops )->push_back( top );
}

//-------------------------------------------------------------------------------------------------
/** A guarding unit looks like an idle one until something walks into its circle, so each of the
	* local player's guards wears a small shield over its head, selected or not.  It follows the zoom
	* like the health bar does, between a floor that still reads as a shield and a ceiling that does
	* not cover a close-up infantryman.  The one-interface change took it out with the Reforged
	* interface it was drawn under; it belongs to guard, not to that interface. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawGuardMarkers( void )
{
	const Real MARKER_HEIGHT = 10.0f;		// at 800x600 and zoom 1, a little closer than the opening camera
	const Real MARKER_MIN_HEIGHT = 6.0f;
	const Real MARKER_MAX_HEIGHT = 10.0f;
	const Int TALLEST_SHIELD = 40;		// GUARD_HEIGHTS' last, 10 x 4K's UI scale; taller ones stretch it

	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	const Real uiScale = TheUIScale();
	const Real size = MARKER_HEIGHT * uiScale / TheTacticalView->getZoom();
	const Int height = REAL_TO_INT( max( MARKER_MIN_HEIGHT * uiScale, min( MARKER_MAX_HEIGHT * uiScale, size ) ) );
	const Int lift = height / 2;		// clear of the health bar's line over the model's top

	// One picture a guard, GuardShield<h> (ReforgedGuardShield, Tools/hud_command_bar.py guard_shield): the
	// heater shield, its dark edge, lit and shaded halves and drop shadow exactly as the rows of covered spans
	// drew them, which cost 60 to 180 rectangles a guard every picture (GitHub #165).  It is drawn texel for
	// pixel, 2K + 1 wide round the shield's pixel column (K = ceil(rim + width / 2)) and h + inset high.
	const Int drawn = min( height, TALLEST_SHIELD );
	AsciiString name;
	name.format( "GuardShield%d", drawn );
	const Image *shield = TheMappedImageCollection->findImageByName( name );
	const Real rim = max( 1.0f, drawn / 10.0f );
	const Real stretch = (Real)height / drawn;
	const Int imageWidth = REAL_TO_INT( ( 2 * (Int)ceilf( rim + drawn * 0.4f ) + 1 ) * stretch );
	const Int imageHeight = REAL_TO_INT( ( drawn + (Int)rim ) * stretch );

	// Who stands guard, and where, moves only with a tick, so the walk runs once a logic frame; it ran
	// on every pass and was most of what this method cost.  Only the camera is this pass's, except under
	// frozen time, whose scripts move units with no new frame.
	// ponytail: two guards or more walk every object for the order; keep the list order if it shows
	const UnsignedInt frame = TheGameLogic->getFrame();
	const Player *local = ThePlayerList->getLocalPlayer();
	if( frame != m_guardMarkersFrame || local != m_guardMarkersPlayer || isTimeFrozenWithoutTick() )
	{
		m_guardMarkersFrame = frame;
		m_guardMarkersPlayer = local;
		m_guardMarkerTops.clear();
		// Only the local player's own objects wear one, and an observer owns none, so his objects are
		// asked first.  Two shields can overlap, and they go down in the order of the list of every
		// object as they always did, so with two or more that list is walked for the order.
		local->iterateObjects( addGuardMarker, &m_guardMarkerTops );
		if( m_guardMarkerTops.size() > 1 )
		{
			m_guardMarkerTops.clear();
			for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
				addGuardMarker( obj, &m_guardMarkerTops );
		}
	}

	for( size_t i = 0; i < m_guardMarkerTops.size(); ++i )
	{
		Coord3D top = m_guardMarkerTops[ i ];
		ICoord2D spot;
		if( !TheTacticalView->worldToScreen( &top, &spot ) )
			continue;

		const Int left = spot.x - imageWidth / 2;
		const Int y = spot.y - lift - height;
		TheDisplay->drawImage( shield, left, y, left + imageWidth, y + imageHeight );
	}

}  // end drawGuardMarkers

//-------------------------------------------------------------------------------------------------
/** Models which share the world scene with units, but describe the player's orders and cursor. */
//-------------------------------------------------------------------------------------------------
Bool W3DInGameUI::isWorldSpaceUI( const RenderObjClass *robj, const Drawable *draw ) const
{
	// These live in the world scene so the ground can cover them, but are feedback for the player.
	// A row of placement previews owns more than the single drawable under the cursor.
	if( robj == m_buildingPlacementAnchor || robj == m_buildingPlacementArrow )
		return TRUE;
	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
		if( robj == m_moveHintRenderObj[ i ] )
			return TRUE;
	if( draw && m_placeIcon )
		for( Int i = 0; i < TheGlobalData->m_maxLineBuildObjects; i++ )
			if( draw == m_placeIcon[ i ] )
				return TRUE;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Each live move ring aligned on logic's terrain, at the sync point: the ground and water heights
	* are logic's, and drawMoveHints runs inside the overlap window. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::takeMoveHintTransforms( void )
{
	if( !TheGlobalData->m_classicGraphics )
		return;		// the coloured marker instead (takeOrderRings)
	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		Int elapsed = TheGameClient->getFrame() - m_moveHint[i].frame;
		if( m_moveHint[ i ].frame == 0 || elapsed > ConvertRetailFrames( 40 ) )
			continue;

		// EA's ring is a model, lit by nothing the shroud can reach: under black shroud it drew bright
		// on black ground, so it is left out there.  In fog it keeps EA's look
		W3DShroud *shroud = TheTerrainRenderObject ? TheTerrainRenderObject->getShroud() : NULL;
		m_moveHintShrouded[ i ] = FALSE;
		if( shroud != NULL && shroud->getShroudTexture() != NULL && shroud->getCellWidth() > 0.0f && shroud->getCellHeight() > 0.0f )
		{
			const Int cellX = REAL_TO_INT_FLOOR( ( m_moveHint[ i ].pos.x - shroud->getDrawOriginX() ) / shroud->getCellWidth() );
			const Int cellY = REAL_TO_INT_FLOOR( ( m_moveHint[ i ].pos.y - shroud->getDrawOriginY() ) / shroud->getCellHeight() );
			if( cellX >= 0 && cellY >= 0 && cellX < shroud->getNumShroudCellsX() && cellY < shroud->getNumShroudCellsY() )
				m_moveHintShrouded[ i ] = shroud->getShroudLevel( cellX, cellY ) < 16;
		}

		Matrix3D &transform = m_moveHintTransform[ i ];
		PathfindLayerEnum layer = TheTerrainLogic->alignOnTerrain( 0, m_moveHint[ i ].pos, true, transform );

		Real waterZ;
		if (layer == LAYER_GROUND && TheTerrainLogic->isUnderwater(m_moveHint[ i ].pos.x, m_moveHint[ i ].pos.y, &waterZ))
		{
			Coord3D tmp = m_moveHint[ i ].pos;
			tmp.z = waterZ;
			Coord3D normal;
			normal.x = 0;
			normal.y = 0;
			normal.z = 1;
			makeAlignToNormalMatrix(0, tmp, normal, transform);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** EA's animated ring on the ground where a move order landed, 40 client frames each.  EA's own
	* code, deleted by fec052cc and brought back (InGameUI::createMoveHint fills it).  Classic
	* graphics only; otherwise drawOrderRings draws the order's coloured marker. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawMoveHints( View *view )
{
	if( !TheGlobalData->m_classicGraphics )
		return;
	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		Int elapsed = TheGameClient->getFrame() - m_moveHint[i].frame;

		if( m_moveHint[ i ].frame != 0 && elapsed <= ConvertRetailFrames( 40 ) && !m_moveHintShrouded[ i ] )
		{
			// create render object and add to scene of needed
			if( m_moveHintRenderObj[ i ] == NULL )
			{
				RenderObjClass *hint = W3DDisplay::m_assetManager->Create_Render_Obj(TheGlobalData->m_moveHintName.str());

				AsciiString animName;
				animName.format("%s.%s", TheGlobalData->m_moveHintName.str(), TheGlobalData->m_moveHintName.str());
				HAnimClass *anim = W3DDisplay::m_assetManager->Get_HAnim(animName.str());

				if( hint == NULL )
				{
					REF_PTR_RELEASE( anim );
					return;
				}

				// a fresh render object comes back visible, and the add below only runs for a hidden
				// one: start it hidden so it reaches the scene
				hint->Set_Hidden( 1 );
				m_moveHintRenderObj[ i ] = hint;
				// 'anim' comes back from Get_HAnim with an AddRef already
				REF_PTR_RELEASE(m_moveHintAnim[i]);
				m_moveHintAnim[i] = anim;
			}

			// show the render object if hidden
			if( m_moveHintRenderObj[ i ]->Is_Hidden() == 1 ) {
				m_moveHintRenderObj[ i ]->Set_Hidden( 0 );
				W3DDisplay::m_3DScene->Add_Render_Object( m_moveHintRenderObj[ i ] );
				if (m_moveHintAnim[i])
					m_moveHintRenderObj[i]->Set_Animation(m_moveHintAnim[i], 0, RenderObjClass::ANIM_MODE_ONCE);
			}

			// move this hint render object to where takeMoveHintTransforms laid it on the terrain
			m_moveHintRenderObj[ i ]->Set_Transform( m_moveHintTransform[ i ] );
		}
		else if( m_moveHintRenderObj[ i ] && m_moveHintRenderObj[ i ]->Is_Hidden() == 0 )
		{
			// hide hint marker
			m_moveHintRenderObj[ i ]->Set_Hidden( 1 );
			W3DDisplay::m_3DScene->Remove_Render_Object( m_moveHintRenderObj[ i ] );
		}
	}
}

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

	// get out of here if this display isn't up anyway, and with shift or alt held it is not: that
	// drag lays a row or a grid and turns nothing, so an anchor and an arrow would promise a turn
	// it will not make
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

