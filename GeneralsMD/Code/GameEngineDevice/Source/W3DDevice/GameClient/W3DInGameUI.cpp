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
#include "GameLogic/Object.h"
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

	for( Int i = 0; i < MAX_PLAYER_COUNT; ++i )
		m_allyCursorNames[ i ] = NULL;

	for( Int i = 0; i < MAX_ORDER_STEP_NUMBERS; ++i )
		m_orderStepNumbers[ i ] = NULL;

	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		m_moveHintRenderObj[ i ] = NULL;
		m_moveHintAnim[ i ] = NULL;
	}

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

}  // end reset

static void drawGroundRing( const Coord3D& center, Real radius, UnsignedInt color, Real width );

/// -directorrecord's lines between panes and the radar's frame: the brand's gold (zerohour.gg's
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
/// the corner radar's dark edge is a pixel this many rows of the picture
static const Real CORNER_EDGE_ROWS_A_PIXEL = 720.0f;
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

/// a rectangle the given pixels larger than window on every side, filled
static void fillAround( const IRegion2D &window, Int outside, Color color )
{
	TheDisplay->drawFillRect( window.lo.x - outside, window.lo.y - outside,
		window.hi.x - window.lo.x + 2 * outside, window.hi.y - window.lo.y + 2 * outside, color );
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
		// corner by hand, where the radar's own pixel maths find it too, and painted.  Under
		// -directorrecord it sits in the middle of the bottom edge, slides down off the screen before
		// panes come and back up after them, and is not drawn while they are up: framed where the rays
		// met, its box for every pane was one more thing on a split screen the owner found too busy
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
			const Int mapWidth = corner.hi.x - corner.lo.x;
			const Int mapHeight = corner.hi.y - corner.lo.y;
			// -directorrecord frames it, half the rays' gold on a pixel of edge a 720 rows with the lines'
			// band as a halo, flush with the bottom so only the top and the sides show; the console's
			// showmap alone keeps it bare and flush with the corner
			if( TheGlobalData->m_directorRecord )
			{
				const Int frameGold = ObserverCamera_paneLineWidth( TheDisplay->getHeight() );
				const Int halo = max( ( ObserverCamera_paneBandWidth( TheDisplay->getHeight() ) - frameGold ) / 2 - OBSERVER_PANE_LINE_EDGE, 0 ) / 2;
				const Int gold = max( frameGold / 2, 1 );
				const Int outside = gold + max( REAL_TO_INT( TheDisplay->getHeight() / CORNER_EDGE_ROWS_A_PIXEL ), 1 );
				// slid down far enough to take the frame and its halo off the screen too
				const Int slide = REAL_TO_INT( TheObserverCamera.getCornerRadarSlide() * ( mapHeight + outside + halo ) );
				corner.lo.x = ( TheDisplay->getWidth() - mapWidth ) / 2;
				corner.hi.x = corner.lo.x + mapWidth;
				corner.lo.y += slide;
				corner.hi.y += slide;
				// filled rectangles under the radar, square at the corners: the halo, the edge, then the gold
				// up to the window, so the map sits on the gold with no gap
				fillAround( corner, outside + halo, paneBand( PANE_BAND_OUTER_ALPHA ) );
				fillAround( corner, outside + halo / 2, paneBand( PANE_BAND_INNER_ALPHA ) );
				fillAround( corner, outside, PANE_EDGE );
				fillAround( corner, gold, PANE_GOLD );
			}
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

	preDraw();

	// draw selection region if drag selecting
	if( m_isDragSelecting )
		drawSelectionRegion();

	// draw the formation line if one is being dragged out
	if( m_isFormationDragging )
		drawFormationLine();

	// Classic draws what the game as shipped drew: none of the threads, plan numbers, ally pointers
	// or guard shields below, and its waypoint path is W3DWaypointBuffer's.  A move order gets
	// EA's animated ground ring (drawMoveHints) in place of the order markers
	const Bool classic = TheGlobalData->isClassicUI();

	// and where everything selected is headed, drag or no drag.  Classic has EA's ring instead,
	// drawMoveHints below
	if( !classic )
		drawOrderHints();

	// and which of the plans each builder puts up next
	if( !classic )
		drawBuildPlanNumbers();

	// where the allies are pointing, which is the one thing on this screen somebody else is doing
	if( !classic )
		drawAllyCursors();

	// the attack circle, while the left button is still sweeping it out
	if( isAttackCircling() )
		drawAttackCircle();

	// which of the local player's units are holding a guard
	if( !classic )
		drawGuardMarkers();

	// the circle an armed guard will hold, under the cursor, at the size the wheel left it.  A drag
	// is drawing a guard line instead, where every unit holds its own station
	if( isAreaPicking() && !m_isFormationDragging )
	{
		Coord3D center;
		TheTacticalView->screenToTerrain( &TheMouse->getMouseStatus()->pos, &center );
		drawGroundRing( center, getAreaPickRadius(), 0xCC55CCFF, 2.0f );		// the guard blue the hints use
	}

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

	// While the map loads the display calls this every pass to paint the load screen's windows, and
	// the match is already "in game" with its peace time set: the truce plate, the corner readout and
	// the strips drew over the load screen.  Only the windows belong there.
	const Bool matchOnScreen = !TheGlobalData->m_loadScreenRender;
	if( matchOnScreen )
		postDraw();

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
	// The clock/rate plate goes on last, after every window has painted. In postDraw with the rest
	// of the world overlays it was drawn before the window manager, so anything the manager put on
	// top of it - a dialog, the menu, the control bar's own art at the top of the screen - covered
	// the one reading you want visible exactly when something is going wrong.
	//
	// the peace time clock across the top middle, and the clock plate in the corner beside it
	// Classic's screen is EA's, which had neither plate
	if( matchOnScreen && !TheGlobalData->isClassicUI() )
	{
		drawPeaceTimer();
		drawHudOverlay();
		drawWireframeNotice();
		drawScoreboard();
	}

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
	* that question for the one spot the cursor is actually on. */
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
	const Real LINE_HALF_WIDTH = 0.3f;
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
/** draw the line an armed left drag is spreading the selection along.  Where each unit will stand is
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
/** A circle on the ground, not on the screen, so it follows the terrain the way the units it is
	* about do */
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
	center.z = TheTerrainLogic->getGroundHeight( center.x, center.y );
	if( TheTacticalView->worldToScreenTriReturn( &center, &middle ) != View::WTS_INVALID )
		TheDisplay->drawLine( middle.x, middle.y, getAttackCircleCursor().x,
													getAttackCircleCursor().y, 1.0f, 0x66FF5555 );

}  // end drawAttackCircle

//-------------------------------------------------------------------------------------------------
/** An order's colour: anything that ends in a shot is red, an attack move is pink, a post to be
	* held is blue, a building to be taken is gold, everything else is green.  The line itself is
	* EA's rally line now; the colour is left on the guard circle and the step numbers. */
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
		case InGameUI::ORDER_HINT_CAPTURE:
			return 0x66FFCC33;
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
#if defined(_WIN32)	// the .ANI cursor through Win32's cursor API; off Windows the cursor is C3's
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
#else
	(void)cursor;
	(void)hotSpot;
	return NULL;
#endif
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
	* says nothing on its own, and for a capture, whose gold is new enough to want saying twice.
	* Mouse::NONE for the rest. */
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
			text.format( u"%d", hint.step );
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
/** The screen-space part of the order hints: a guard's circle and the numbers of a shift list.  The
	* goals are read off the units every frame, so both last as long as the orders do. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawOrderHints( void )
{
	const std::vector<OrderHint>& hints = getOrderHints();
	if( hints.empty() )
		return;

	const Real width = 1.0f;

	// how far right of the spot an upgrade or an ability stands: past the number of the step that
	// ends there, which grows with the screen
	const Real IN_PLACE_MARKER_OFFSET = 44.0f;
	const Int inPlaceOffset = REAL_TO_INT( IN_PLACE_MARKER_OFFSET * orderStepScale() );

	// a group on one guard order is one circle, not one per unit stacked into an opaque band
	std::vector<const OrderHint *> ringsDrawn;

	for( std::vector<OrderHint>::const_iterator it = hints.begin(); it != hints.end(); ++it )
	{
		const UnsignedInt lineColor = orderHintLineColor( it->kind );

		if( it->radius > 0.0f )
		{
			Bool drawn = FALSE;
			for( size_t r = 0; r < ringsDrawn.size() && !drawn; ++r )
				drawn = ringsDrawn[ r ]->radius == it->radius && ringsDrawn[ r ]->to.x == it->to.x && ringsDrawn[ r ]->to.y == it->to.y;
			if( !drawn )
			{
				drawGroundRing( it->to, it->radius, lineColor, width );
				ringsDrawn.push_back( &(*it) );
			}
		}

		// The line, the joint and the flag are EA's rally point art in the 3D scene
		// (W3DWaypointBuffer::drawWaypoints and InGameUI::updateOrderFlags).  What is left here is
		// the step number and the art of a step whose kind the line cannot tell apart from a move.
		// WTS_OUTSIDE_FRUSTUM still gives usable pixels, so only points behind the camera are dropped
		ICoord2D to;
		if( TheTacticalView->worldToScreenTriReturn( &it->to, &to ) == View::WTS_INVALID )
			continue;

		// an upgrade or an ability is used on the spot the step before it ends on, whose number is
		// already there, so this one stands beside it rather than on top of it
		if( it->kind == ORDER_HINT_UPGRADE || it->kind == ORDER_HINT_ABILITY )
			to.x += inPlaceOffset;

		// a capture carries its cursor even alone: a lone one is the case that looked like a walk
		if( it->step > 0 || it->icon || it->kind == ORDER_HINT_CAPTURE )
			drawOrderStep( *it, to, orderHintMarkerColor( it->kind ) );
	}

}  // end drawOrderHints

//-------------------------------------------------------------------------------------------------
/** The local player's plans still waiting for a builder carry the number of their turn, so a
	* string of shift-placed buildings reads in the order its dozer will put them up.  Read off the
	* objects every frame; nothing here goes back to the logic. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawBuildPlanNumbers( void )
{
	// ponytail: walks every object each frame; keep a list of plans if a big map shows it in a profile
	std::vector<BuildPlanNumber> plans;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !obj->isLocallyControlled() || obj->testStatus( OBJECT_STATUS_SOLD ) || obj->isEffectivelyDead() ||
				!Object_isAwaitingBuilder( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ), obj->getConstructionPercent() ) )
			continue;

		// a plan whose builder is gone is nobody's queue
		Object *builder = TheGameLogic->findObjectByID( obj->getBuilderID() );
		if( builder == NULL || builder->isEffectivelyDead() || builder->getAIUpdateInterface() == NULL )
			continue;
		DozerAIInterface *dozerAI = builder->getAIUpdateInterface()->getDozerAIInterface();
		if( dozerAI == NULL )
			continue;

		BuildPlanNumber plan;
		plan.plan = obj->getID();
		plan.builder = builder->getID();
		plan.current = dozerAI->isTaskPending( DOZER_TASK_BUILD ) && dozerAI->getTaskTarget( DOZER_TASK_BUILD ) == obj->getID();
		plan.spot = *obj->getPosition();
		plan.spot.z += obj->getGeometryInfo().getMaxHeightAbovePosition();
		plan.step = 0;
		plans.push_back( plan );
	}

	InGameUI_numberBuildPlans( plans );

	for( std::vector<BuildPlanNumber>::const_iterator it = plans.begin(); it != plans.end(); ++it )
	{
		ICoord2D tip;
		if( it->step == 0 || !TheTacticalView->worldToScreen( &it->spot, &tip ) )
			continue;

		OrderHint hint;
		hint.step = it->step;
		drawOrderStep( hint, tip, orderHintMarkerColor( ORDER_HINT_MOVE ) );
	}

}  // end drawBuildPlanNumbers

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
/** A guarding unit looks like an idle one until something walks into its circle, so each of the
	* local player's guards wears a small shield over its head, selected or not.  Two groups on
	* overlapping posts can then be told apart from the ones simply standing about.
	*
	* It follows the zoom like the health bar does, so it stays in proportion to the unit, between a
	* floor that still reads as a shield and a ceiling that does not cover a close-up infantryman. */
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

	const Real uiScale = TheUIScale();
	const Real size = MARKER_HEIGHT * uiScale / TheTacticalView->getZoom();
	const Int height = REAL_TO_INT( max( MARKER_MIN_HEIGHT * uiScale, min( MARKER_MAX_HEIGHT * uiScale, size ) ) );
	const Real width = height * 0.8f;
	const Int lift = height / 2;		// clear of the health bar's line over the model's top
	const Real rim = max( 1.0f, height / 10.0f );

	// ponytail: walks every object each frame, like drawBuildPlanNumbers; share one walk if it shows
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !obj->isLocallyControlled() || obj->isEffectivelyDead() || obj->getContainedBy() )
			continue;
		const AIUpdateInterface *ai = obj->getAI();
		if( ai == NULL || ai->getCurrentStateID() != AI_GUARD )
			continue;
		const Drawable *draw = obj->getDrawable();
		if( draw == NULL || draw->isDrawableEffectivelyHidden() )
			continue;

		Coord3D top = *obj->getPosition();
		top.z += obj->getGeometryInfo().getMaxHeightAbovePosition();
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
/** A patch of the ally's own colour on the ground under their cursor.  It is a fan of rings whose
	* alpha falls to nothing at the rim, drawn in the terrain pass like the attack circle's wash, so
	* units and buildings stand on top of it and it reads as light on the map rather than as a disc
	* hanging over it.
	*
	* Faint on purpose: seven of these at full strength would be seven holes in the map. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAllyCursorLights( void )
{
	if( !TheGlobalData->m_showAllyCursors || TheGlobalData->isClassicUI() )
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
/** Classic: EA's animated ring on the ground where a move order landed, 40 client frames each.
	* EA's own code, deleted by fec052cc, back for Classic only (InGameUI::createMoveHint fills it). */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawMoveHints( View *view )
{
	const Bool classic = TheGlobalData->isClassicUI();
	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		Int elapsed = TheGameClient->getFrame() - m_moveHint[i].frame;

		if( classic && m_moveHint[ i ].frame != 0 && elapsed <= 40 )
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

			// move this hint render object to the position and align with terrain
			Matrix3D transform;
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

			m_moveHintRenderObj[ i ]->Set_Transform( transform );
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

