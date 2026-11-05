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

/// -directorrecord's lines between panes: the brand's gold (zerohour.gg's
/// --gold, #f2c230) between two edges of its ground (--bg, #0c1220), so they read on snow and on
/// night maps alike
static const Color PANE_GOLD = GameMakeColor( 0xf2, 0xc2, 0x30, 255 );
static const Color PANE_EDGE = GameMakeColor( 0x0c, 0x12, 0x20, 255 );
/// the band under the lines is the site's text blue, --blue #8095ea, the brand's second colour: a
/// wide faint layer and a narrower stronger one, fading out along the ray
static const UnsignedByte PANE_BAND_RGB[ 3 ] = { 0x80, 0x95, 0xea };
static const Real PANE_BAND_OUTER_ALPHA = 0.3f;
static const Real PANE_BAND_INNER_ALPHA = 0.55f;
static const Real PANE_BAND_FAR_SHARE = 0.35f;
/// the light that runs along the gold: --gold most of the way to white, as wide as the edged line and
/// this share of its length, fading out to both ends.  At --gold half way to white and the gold's
/// own width nobody saw it at 720p
static const UnsignedByte PANE_SHIMMER_RGB[ 3 ] = { 0xff, 0xf6, 0xd8 };
static const Real PANE_SHIMMER_SHARE = 0.18f;

static Color paneShimmer( UnsignedByte alpha )
{
	return GameMakeColor( PANE_SHIMMER_RGB[ 0 ], PANE_SHIMMER_RGB[ 1 ], PANE_SHIMMER_RGB[ 2 ], alpha );
}

static Color paneBand( Real alpha )
{
	return GameMakeColor( PANE_BAND_RGB[ 0 ], PANE_BAND_RGB[ 1 ], PANE_BAND_RGB[ 2 ], (UnsignedByte)REAL_TO_INT( 255.0f * alpha ) );
}

/** The rays between the panes, from their meeting point out: the dark edge whole from the start, so
	* the seams read as the panes slide in, and the band and the gold as far as they have drawn out on
	* the settled panes.  Every ray's band first, then every edge, then the gold, so the gold runs on
	* unbroken where they meet.  Drawn, a light runs out along the gold now and then. */
static void drawPaneRays( void )
{
	const Real gold = (Real)ObserverCamera_paneLineWidth( TheDisplay->getHeight() );
	const Real edged = gold + 2 * OBSERVER_PANE_LINE_EDGE;
	const Real band = (Real)ObserverCamera_paneBandWidth( TheDisplay->getHeight() );
	const Coord2D origin = TheObserverCamera.getPaneOrigin();
	const Real *rays = TheObserverCamera.getPaneRays();
	const Int count = TheObserverCamera.getDrawnPaneCount();
	const Real progress = TheObserverCamera.getLineProgress();
	const Real bandShown = ObserverCamera_bandShown( progress );
	const Real drawn = ObserverCamera_lineDrawn( progress );
	// the far end is past the farthest corner from the meeting point, wherever that has slid to
	const Real width = (Real)TheDisplay->getWidth();
	const Real height = (Real)TheDisplay->getHeight();
	const Real farX = max( fabsf( origin.x ), fabsf( width - origin.x ) );
	const Real farY = max( fabsf( origin.y ), fabsf( height - origin.y ) );
	const Real whole = sqrtf( farX * farX + farY * farY );
	const Real length = whole * drawn;
	const Int fromX = REAL_TO_INT( origin.x );
	const Int fromY = REAL_TO_INT( origin.y );
	for( Int pass = 0; pass < 4; pass++ )
	{
		for( Int ray = 0; ray < count; ray++ )
		{
			const Real angle = rays[ ray ] * PI / 180.0f;
			const Real reach = pass == 2 ? whole : length;
			const Int toX = REAL_TO_INT( origin.x + cosf( angle ) * reach );
			const Int toY = REAL_TO_INT( origin.y - sinf( angle ) * reach );
			if( pass == 0 && bandShown > 0.0f )
				TheDisplay->drawLine( fromX, fromY, toX, toY, band, paneBand( PANE_BAND_OUTER_ALPHA * bandShown ),
					paneBand( PANE_BAND_OUTER_ALPHA * bandShown * PANE_BAND_FAR_SHARE ) );
			else if( pass == 1 && bandShown > 0.0f )
				TheDisplay->drawLine( fromX, fromY, toX, toY, ( band + edged ) * 0.5f, paneBand( PANE_BAND_INNER_ALPHA * bandShown ),
					paneBand( PANE_BAND_INNER_ALPHA * bandShown * PANE_BAND_FAR_SHARE ) );
			else if( pass == 2 )
				TheDisplay->drawLine( fromX, fromY, toX, toY, edged, PANE_EDGE );
			else if( pass == 3 && drawn > 0.0f )
				TheDisplay->drawLine( fromX, fromY, toX, toY, gold, PANE_GOLD );
		}
	}

	const Real shimmer = ObserverCamera_shimmerAt( GameEngine_pictureFrame() );
	if( drawn < 1.0f || shimmer < 0.0f )
		return;
	const Real half = length * PANE_SHIMMER_SHARE * 0.5f;
	const Real middle = length * shimmer;
	for( Int ray = 0; ray < count; ray++ )
	{
		const Real angle = rays[ ray ] * PI / 180.0f;
		const Real alongX = cosf( angle );
		const Real alongY = -sinf( angle );
		const Real start = max( middle - half, 0.0f );
		const Real end = min( middle + half, length );
		const Int middleX = REAL_TO_INT( origin.x + alongX * middle );
		const Int middleY = REAL_TO_INT( origin.y + alongY * middle );
		TheDisplay->drawLine( REAL_TO_INT( origin.x + alongX * start ), REAL_TO_INT( origin.y + alongY * start ), middleX, middleY, edged,
			paneShimmer( 0 ), paneShimmer( 255 ) );
		TheDisplay->drawLine( middleX, middleY, REAL_TO_INT( origin.x + alongX * end ), REAL_TO_INT( origin.y + alongY * end ), edged,
			paneShimmer( 255 ), paneShimmer( 0 ) );
	}
}

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
		const Bool framed = TheObserverCamera.isRadarFramed();
		IRegion2D noFrame;
		noFrame.lo.x = noFrame.lo.y = noFrame.hi.x = noFrame.hi.y = 0;
		TheObserverCamera.setRadarFrame( noFrame );

		// -directorrecord's panes: the rays between them, under the radar, drawn in every pane's draw so
		// each pane's half of a line's band lies over its own picture.  Kept from pane 0 alone, the band
		// showed pane 0's ground on the far side of the seam, the black past its map in an eight-pane
		// opening
		if( framed )
			drawPaneRays();

		// the console's 'hidehud showmap=true': the bar is hidden, so its radar window is put in the
		// corner by hand, where the radar's own pixel maths find it too, and painted.  -directorrecord
		// never asks for it: a map on the film was one more thing on the screen
		if( CinemaDirector_showsMap() && !framed && !TheObserverCamera.isDrawingSecond() )
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

		// -directorrecord's opening plates, each pane's own, then the score bar and a split's labels over
		// everything and pane 0's alone like the lines
		if( TheGlobalData->m_directorRecord )
		{
			drawDirectorIntroPlate();
			if( !TheObserverCamera.isDrawingSecond() )
				drawDirectorBroadcast();
		}
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
	// the quads are in world space; the terrain pass has rendered pucks and bridges since its own
	// identity was set, and the sheet used to inherit whichever of their matrices came last
	DX8Wrapper::Set_Transform( D3DTS_WORLD, Matrix3D( 1 ) );
	DX8Wrapper::Apply_Render_State_Changes();
}

//-------------------------------------------------------------------------------------------------
/** Draw the pathfinder's own cell grid under the structure sitting on the cursor, and cross out
	* the cells it cannot go on.  The lines only under GridBuildPlacement; the red cells always.
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
	* "Cannot build" here is the pathfinder cell's own type: water, a cliff, rubble, an existing
	* structure, plain impassable.  It deliberately does not run the full isLocationLegalToBuild for
	* every cell - that is a per-structure query (build radius, shroud, supply proximity) and a
	* couple of hundred of them a frame is not worth it.  The ghost's own red tint already answers
	* that question for the one spot the cursor is actually on.
	*
	* The cells are read at the sync point (takeBuildGrid), not here: this runs in the terrain pass,
	* beside the tick. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawBuildGrid( void )
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

	// and a red wash over every cell a structure cannot stand on.  Filling the cell reads at a
	// glance where an X drawn in thin lines did not.
	const Real BLOCKED_ALPHA = 0x30;
	for( iy = 0; iy < GRID_CELLS; ++iy )
	{
		for( ix = 0; ix < GRID_CELLS; ++ix )
		{
			if( !m_buildGridBlocked[ iy ][ ix ] )
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
/** Which cells of the patch round the ghost drawBuildGrid washes red, read from the pathfinder at
	* the sync point.  The pathfinder marks every structure the moment it is placed, the enemy's in the
	* shroud included, so its cells alone drew enemy foundations into the dark: a cell held by
	* something the local player has no picture of stays clear here, and so does rubble under the
	* shroud, while a building he still sees standing in the fog after it died is washed from its
	* ghost.  Water, cliffs and plain impassable ground are the map's own and always show. */
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

	for( Int iy = 0; iy < BUILD_GRID_CELLS; ++iy )
	{
		for( Int ix = 0; ix < BUILD_GRID_CELLS; ++ix )
		{
			const PathfindCell *cell = pathfinder->getCell( LAYER_GROUND, m_buildGridCellX + ix, m_buildGridCellY + iy );
			Bool blocked = cell != NULL && cell->getType() != PathfindCell::CELL_CLEAR;

			if( blocked && cell->getType() == PathfindCell::CELL_OBSTACLE )
			{
				// the cell can still name an object the tick has already destroyed; nothing is left to
				// hide, and the pathfinder still refuses the cell, so it draws red
				const Object *obstacle = TheGameLogic->findObjectByID( cell->getObstacleID() );
				blocked = obstacle == NULL || !BuildAssistant_isHiddenFromLocalPlayer( obstacle );
			}
			else if( blocked && cell->getType() == PathfindCell::CELL_RUBBLE )
			{
				Coord3D at;
				at.x = ( (Real)( m_buildGridCellX + ix ) + 0.5f ) * PATHFIND_CELL_SIZE_F;
				at.y = ( (Real)( m_buildGridCellY + iy ) + 0.5f ) * PATHFIND_CELL_SIZE_F;
				at.z = 0.0f;
				blocked = ThePartitionManager->getShroudStatusForPlayer( me, &at ) != CELLSHROUD_SHROUDED;
			}

			m_buildGridBlocked[ iy ][ ix ] = blocked;
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
					m_buildGridBlocked[ iy ][ ix ] = TRUE;
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
/** draw the line an armed left drag is spreading the selection along.  Where each unit will stand is
	* said by the cursor sitting there, not by a mark on the line */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawFormationLine( void )
{
	const Real width = 2.0f;
	// the line says which order it is about to be, in the colours the order hints already use
	const UnsignedInt color = isInAttackMoveToMode() ? 0xCCFF66CC
											: ( isForceAttackArmed() || isForceLineHeld() ) ? 0xCCFF5555
											: isGuardArmed() ? 0xCC55CCFF
											: 0xCC33FF33;  //0xAARRGGBB
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
/** One screen row from `left` to `right`, the two end pixels at the share of them the span covers,
	* so a slanted edge fades across a pixel instead of stepping. */
//-------------------------------------------------------------------------------------------------
static void drawCoveredSpan( Real left, Real right, Int y, UnsignedInt color )
{
	if( right <= left )
		return;
	const UnsignedInt rgb = color & 0x00FFFFFF;
	const Real alpha = (Real)( color >> 24 );
	const Int first = (Int)floorf( left );
	const Int last = (Int)floorf( right );
	if( first == last )
	{
		TheDisplay->drawFillRect( first, y, 1, 1, rgb | ( REAL_TO_INT( alpha * ( right - left ) ) << 24 ) );
		return;
	}
	TheDisplay->drawFillRect( first, y, 1, 1, rgb | ( REAL_TO_INT( alpha * ( first + 1 - left ) ) << 24 ) );
	if( last > first + 1 )
		TheDisplay->drawFillRect( first + 1, y, last - first - 1, 1, color );
	if( right > last )
		TheDisplay->drawFillRect( last, y, 1, 1, rgb | ( REAL_TO_INT( alpha * ( right - last ) ) << 24 ) );
}

//-------------------------------------------------------------------------------------------------
/** A heater shield centred on `centreX`, filled one screen row at a time: straight sides for the
	* upper part, then a curve closing to the point at the bottom.  The left half takes `leftColor`
	* and the right half `rightColor`, which with a light and a darker steel reads as a raised
	* centre ridge with the light from the left. */
//-------------------------------------------------------------------------------------------------
static void drawShieldShape( Real centreX, Int top, Real width, Int height, UnsignedInt leftColor, UnsignedInt rightColor )
{
	const Real SHOULDER = 0.4f;		// share of the height above the taper
	for( Int row = 0; row < height; ++row )
	{
		const Real t = ( row + 0.5f ) / height;
		Real half = width * 0.5f;
		if( t > SHOULDER )
			half *= sqrtf( ( 1.0f - t ) / ( 1.0f - SHOULDER ) );
		drawCoveredSpan( centreX - half, centreX, top + row, leftColor );
		drawCoveredSpan( centreX, centreX + half, top + row, rightColor );
	}
}

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
	const UnsignedInt SHADOW_COLOR = 0x60000000;		// lifts it off snow and sand alike
	const UnsignedInt EDGE_COLOR = 0xE00C1014;
	const UnsignedInt LIT_COLOR = 0xF0E8EEF4;		// the command bar's steel, the side facing the light
	const UnsignedInt SHADED_COLOR = 0xF0939FAC;

	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	const Real uiScale = TheUIScale();
	const Real size = MARKER_HEIGHT * uiScale / TheTacticalView->getZoom();
	const Int height = REAL_TO_INT( max( MARKER_MIN_HEIGHT * uiScale, min( MARKER_MAX_HEIGHT * uiScale, size ) ) );
	const Real width = height * 0.8f;
	const Int lift = height / 2;		// clear of the health bar's line over the model's top
	const Real rim = max( 1.0f, height / 10.0f );

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

		const Real centreX = spot.x + 0.5f;
		const Int y = spot.y - lift - height;
		const Int inset = REAL_TO_INT( rim );
		drawShieldShape( centreX + rim, y + inset, width, height, SHADOW_COLOR, SHADOW_COLOR );
		drawShieldShape( centreX, y, width, height, EDGE_COLOR, EDGE_COLOR );
		drawShieldShape( centreX, y + inset, width - 2.0f * rim, height - 3 * inset, LIT_COLOR, SHADED_COLOR );
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
	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		Int elapsed = TheGameClient->getFrame() - m_moveHint[i].frame;
		if( m_moveHint[ i ].frame == 0 || elapsed > ConvertRetailFrames( 40 ) )
			continue;

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
	* code, deleted by fec052cc and brought back (InGameUI::createMoveHint fills it). */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawMoveHints( View *view )
{
	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		Int elapsed = TheGameClient->getFrame() - m_moveHint[i].frame;

		if( m_moveHint[ i ].frame != 0 && elapsed <= ConvertRetailFrames( 40 ) )
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

