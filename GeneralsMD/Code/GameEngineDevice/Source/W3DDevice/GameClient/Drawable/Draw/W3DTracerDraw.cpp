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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DTracerDraw.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, December 2001
// Desc:   Tracer drawing
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>

#include "Common/GlobalData.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/Module/W3DTracerDraw.h"
#include "WW3D2/line3d.h"
#include "WW3D2/segline.h"
#include "WW3D2/surfaceclass.h"
#include "WW3D2/texture.h"
#include "W3DDevice/GameClient/W3DScene.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// GLOWING TRACERS ////////////////////////////////////////////////////////////////////////////////
// With the Glow option on, EA's tracer is drawn as it always was and a halo goes round it, the way
// C&C 3 draws its tracers: a strip turned to face the camera, a soft falloff across it and a fade
// along it, brightest at the head. The halo is added to what is behind it, in the tracer's INI
// colour run toward orange, and opted into the Direct3D 11 glow gain. Inside EA's box it adds
// nothing, since the box is opaque and nearer the eye than the strip through its axis.
// EA's streak is the floor. The try before this (2026-11-06) replaced the box with a blended core
// and a faint halo, both shaped by the texture: across, the falloff averages 0.43 of its peak, along,
// a fade to the power 1.5 averages 0.4, so the two together carried 0.17 of what the constants said,
// and a core two to four pixels wide sampled the shoulders of its profile, not its peak. Shot on a
// desert map the tracer was gone, the ground lifted by about 16 for two logic frames. Glow at Off,
// or Classic Graphics, draws EA's box alone. Draw side only: nothing here is read by GameLogic or
// parsed from INI.
static const Real TRACER_HALO_WIDTH_SCALE	= 6.0f;		// over the INI width, 0.7 on Reforged's guns
static const Real TRACER_HALO_INTENSITY		= 0.5f;		// the halo's peak, at the head
static const Real TRACER_HALO_TAIL				= 0.3f;		// share of that left at the tail, the fade linear between
static const Vector3 TRACER_HALO_TINT( 1.0f, 0.6f, 0.25f );	// times the INI colour, most of which are white or cream
static const Real TRACER_HEAD_CAP				= 0.06f;	// share of the length that rounds off the head
static const Int TRACER_TEXTURE_SIZE			= 64;

// ponytail: made by the first glowing tracer and kept for the process (64x64, 16 KB), since bursts
// come and go several times a second; a release at display shutdown if a leak report ever names it
static TextureClass *s_tracerTexture = NULL;

// one at the middle (0), zero at the edge (1)
static Real tracerFalloff( Real distance )
{
	const Real edge = expf( -4.0f );
	return MAX( ( expf( -4.0f * distance * distance ) - edge ) / ( 1.0f - edge ), 0.0f );
}

// across runs -1..1 over the strip's width, along 0 at the tail to 1 at the head
static Real tracerTexel( Real across, Real along )
{
	Real cap = ( along - ( 1.0f - TRACER_HEAD_CAP ) ) / TRACER_HEAD_CAP;	// 0 where the head's cap starts, 1 at its tip
	Real fade = TRACER_HALO_TAIL + ( 1.0f - TRACER_HALO_TAIL ) * along;
	return tracerFalloff( across ) * fade * tracerFalloff( MAX( cap, 0.0f ) );
}

static TextureClass *makeTracerTexture( void )
{
	TextureClass *texture = MSGNEW("TextureClass") TextureClass( TRACER_TEXTURE_SIZE, TRACER_TEXTURE_SIZE, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1 );
	SurfaceClass *surf = texture->Get_Surface_Level();
	Int pitch;
	UnsignedByte *pData = (UnsignedByte*)surf->Lock( &pitch );
	for( Int y = 0; y < TRACER_TEXTURE_SIZE; y++ )
	{
		Real along = ( y + 0.5f ) / TRACER_TEXTURE_SIZE;
		UnsignedInt *row = (UnsignedInt*)( pData + y * pitch );
		for( Int x = 0; x < TRACER_TEXTURE_SIZE; x++ )
		{
			Real across = ( x + 0.5f ) / TRACER_TEXTURE_SIZE * 2.0f - 1.0f;
			UnsignedInt c = (UnsignedInt)( tracerTexel( across, along ) * 255.0f + 0.5f );
			row[ x ] = ( c << 24 ) | ( c << 16 ) | ( c << 8 ) | c;
		}
	}
	surf->Unlock();
	REF_PTR_RELEASE( surf );
	texture->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	texture->Get_Filter().Set_V_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	return texture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTracerDraw::W3DTracerDraw( Thing *thing, const ModuleData* moduleData ) : DrawModule( thing, moduleData )
{

	// set opacity
	m_opacity = 1.0f;
	m_length = 20.0f;
	m_width = 0.5f;
	m_color.red = 0.9f;
	m_color.green = 0.8f;
	m_color.blue = 0.7f;
	m_speedInDistPerFrame = 1.0f;
	m_theTracer = NULL;
	m_glow = NULL;

}  // end W3DTracerDraw

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::setTracerParms(Real speed, Real length, Real width, const RGBColor& color, Real initialOpacity)
{ 
	m_speedInDistPerFrame = speed; 
	m_length = length;
	m_width = width;
	m_color = color;
	m_opacity = initialOpacity;
	if( m_theTracer )
	{
		shapeTracer();
		// these calls nuke the internal transform, so re-set it here
		placeTracer( *getDrawable()->getTransformMatrix() );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTracerDraw::~W3DTracerDraw( void )
{
	// remove tracer from the scene and delete
	if( m_theTracer )
	{
		W3DDisplay::m_3DScene->Remove_Render_Object( m_theTracer );
		REF_PTR_RELEASE( m_theTracer );
	}
	if( m_glow )
	{
		W3DDisplay::m_3DScene->Remove_Render_Object( m_glow );
		REF_PTR_RELEASE( m_glow );
	}
}

//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::reactToTransformChange( const Matrix3D *oldMtx,
																							 const Coord3D *oldPos,
																							 Real oldAngle )
{
	placeTracer( *getDrawable()->getTransformMatrix() );
}

//-------------------------------------------------------------------------------------------------
// The Glow option is read once a tracer, since one lives a few frames.
//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::createTracer( const Matrix3D &mtx )
{
	Vector3 start( 0.0f, 0.0f, 0.0f );
	Vector3 stop( m_length, 0.0f, 0.0f );

	// create tracer render object for us to manipulate
	// poolify
	m_theTracer = NEW Line3DClass( start,
																 stop,
																 m_width,  // width
																 m_color.red,  // red
																 m_color.green,  // green
																 m_color.blue,  // blue
																 m_opacity );  // transparency
	W3DDisplay::m_3DScene->Add_Render_Object( m_theTracer );

	if( !TheGlobalData->m_classicGraphics && TheGlobalData->m_bloomIntensity > 0 )
	{
		if( !s_tracerTexture )
			s_tracerTexture = makeTracerTexture();
		ShaderClass halo = ShaderClass::_PresetAdditiveShader;
		halo.Set_Glow( ShaderClass::GLOW_ENABLE );
		m_glow = NEW SegmentedLineClass;
		m_glow->Set_Shader( halo );
		m_glow->Set_Texture( s_tracerTexture );
		m_glow->Set_Texture_Mapping_Mode( SegLineRendererClass::TILED_TEXTURE_MAP );
		m_glow->Set_Texture_Tile_Factor( 1.0f );	// v 0 at the first point, the tail, and 1 at the head
		W3DDisplay::m_3DScene->Add_Render_Object( m_glow );
		shapeTracer();
	}

	// set the transform for the tracer to that of the drawable
	placeTracer( mtx );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::shapeTracer( void )
{
	Vector3 start( 0.0f, 0.0f, 0.0f );
	Vector3 stop( m_length, 0.0f, 0.0f );
	m_theTracer->Reset( start, stop, m_width );
	m_theTracer->Re_Color( m_color.red, m_color.green, m_color.blue );
	if( m_glow )
	{
		Vector3 points[ 2 ] = { start, stop };	// the tail, then the head
		m_glow->Set_Points( 2, points );
		m_glow->Set_Width( m_width * TRACER_HALO_WIDTH_SCALE );
	}
	lightTracer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::lightTracer( void )
{
	m_theTracer->Set_Opacity( m_opacity );
	if( m_glow )
	{
		// an additive draw has no alpha, so the halo fades by its colour
		const Real halo = TRACER_HALO_INTENSITY * MAX( m_opacity, 0.0f );
		m_glow->Set_Color( Vector3( m_color.red * TRACER_HALO_TINT.X * halo, m_color.green * TRACER_HALO_TINT.Y * halo, m_color.blue * TRACER_HALO_TINT.Z * halo ) );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::placeTracer( const Matrix3D &mtx )
{
	if( m_theTracer )
		m_theTracer->Set_Transform( mtx );
	if( m_glow )
		m_glow->Set_Transform( mtx );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTracerDraw::doDrawModule(const Matrix3D* transformMtx)
{

	// create tracer
	if( m_theTracer == NULL )
		createTracer( *transformMtx );

	UnsignedInt expDate = getDrawable()->getExpirationDate();
	if (expDate != 0)
	{
		Real decay = m_opacity / (expDate - TheGameLogic->getFrame());
		m_opacity -= decay;
		lightTracer();
	}

	// set the position for the tracer
	if (m_speedInDistPerFrame != 0.0f)
	{
		Matrix3D pos = m_theTracer->Get_Transform();
		pos.Translate( Vector3( m_speedInDistPerFrame, 0.0f, 0.0 ) );
		placeTracer( pos );
	}

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DTracerDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DTracerDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// no data to save here, nobody will ever notice

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DTracerDraw::loadPostProcess( void )
{

	// extend base class
	DrawModule::loadPostProcess();

}  // end loadPostProcess
