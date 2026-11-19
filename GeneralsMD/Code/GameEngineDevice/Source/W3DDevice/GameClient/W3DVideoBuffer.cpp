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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   Generals
//
// Module:    Video
//
// File name: W3DDevice/GameClient/W3DVideoBuffer.cpp
//
// Created:   10/23/01 TR
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "Common/GameMemory.h"
#include "WW3D2/texture.h"
#include "WW3D2/textureloader.h"
#include "W3DDevice/GameClient/W3DVideobuffer.h"

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------


//============================================================================
// W3DVideoBuffer::W3DVideoBuffer
//============================================================================

W3DVideoBuffer::W3DVideoBuffer( VideoBuffer::Type format )
: VideoBuffer(format),
	m_texture(NULL),
	m_surface(NULL)
{

}


//============================================================================
// W3DVideoBuffer::SetBuffer
//============================================================================

Bool W3DVideoBuffer::allocate( UnsignedInt width, UnsignedInt height )
{
	free();

	m_width = width;
	m_height = height;
	m_textureWidth = width;;
	m_textureHeight = height;;
	unsigned int temp_depth=1;
	TextureLoader::Validate_Texture_Size( m_textureWidth, m_textureHeight, temp_depth);

	WW3DFormat w3dFormat = TypeToW3DFormat(  m_format );
 
	if ( w3dFormat == WW3D_FORMAT_UNKNOWN )
	{
		return NULL;	
	}

	m_texture  = MSGNEW("TextureClass") TextureClass ( m_textureWidth, m_textureHeight, w3dFormat, MIP_LEVELS_1 );

	if ( m_texture == NULL )
	{
		return FALSE;
	}

	if ( lock() == NULL )
	{
		free();
		return FALSE;
	}

	unlock();


	return TRUE;
}

//============================================================================
// W3DVideoBuffer::~W3DVideoBuffer
//============================================================================

W3DVideoBuffer::~W3DVideoBuffer()
{
	free();
}

//============================================================================
// W3DVideoBuffer::lock
//============================================================================

void*		W3DVideoBuffer::lock( void )
{
	void *mem = NULL;

	if ( m_surface != NULL )
	{
		unlock();
	}

	m_surface = m_texture->Get_Surface_Level();

	if ( m_surface )
	{
		mem = m_surface->Lock( (Int*) &m_pitch );
	}

	// BinkCopyToBuffer writes m_height rows of m_width pixels at m_pitch, trusting the surface to
	// hold them.  Report 3866 (v4.0.0, Intel UHD 730) locked the EA logo's texture with a pitch of
	// 16: the 486 rows of 2880 bytes went 10 KB past the pixels in opaque black and over the
	// surface's own object.  A level too small for the movie is refused, so the frame is skipped
	// instead of written over the heap.
	if ( mem != NULL )
	{
		SurfaceClass::SurfaceDescription desc;
		m_surface->Get_Description( desc );
		const UnsignedInt rowBytes = ( m_xPos + m_width ) * bytesPerPixel( m_format );
		if ( rowBytes == 0 || m_pitch < rowBytes || desc.Height < m_yPos + m_height )
		{
			static Bool reported = FALSE;
			if ( !reported )
			{
				reported = TRUE;
				DEBUG_LOG(( "W3DVideoBuffer::lock - refused a %ux%u format %d level locked at pitch %u for a %ux%u movie needing %u bytes a row\n",
					desc.Width, desc.Height, (Int)desc.Format, m_pitch, m_width, m_height, rowBytes ));
			}
			unlock();
			mem = NULL;
		}
	}

	return mem;
}

//============================================================================
// W3DVideoBuffer::bytesPerPixel
//============================================================================

UnsignedInt W3DVideoBuffer::bytesPerPixel( VideoBuffer::Type format )
{
	switch ( format )
	{
		case TYPE_X8R8G8B8:	return 4;
		case TYPE_R8G8B8:		return 3;
		case TYPE_R5G6B5:
		case TYPE_X1R5G5B5:	return 2;
	}
	return 0;
}

//============================================================================
// W3DVideoBuffer::unlock
//============================================================================

void		W3DVideoBuffer::unlock( void )
{
	if ( m_surface != NULL )
	{
		m_surface->Unlock();
		m_surface->Release_Ref();
		m_surface = NULL;
	}
}

//============================================================================
// W3DVideoBuffer::valid
//============================================================================

Bool		W3DVideoBuffer::valid( void )
{
	return m_texture != NULL;
}

//============================================================================
// W3DVideoBuffer::reset
//============================================================================

void	W3DVideoBuffer::free( void )
{
	unlock();

	if ( m_texture )
	{
		unlock();
		m_texture->Release_Ref();
		m_texture = NULL;
	}
	m_surface = NULL;

	VideoBuffer::free();
}


//============================================================================
// W3DVideoBuffer::TypeToW3DFormat
//============================================================================

WW3DFormat W3DVideoBuffer::TypeToW3DFormat( VideoBuffer::Type format )
{
	WW3DFormat w3dFormat = WW3D_FORMAT_UNKNOWN;
	switch ( format )
	{
		case TYPE_X8R8G8B8:
			w3dFormat = WW3D_FORMAT_X8R8G8B8;
			break;

 		case TYPE_R8G8B8:
			w3dFormat = WW3D_FORMAT_R8G8B8;
			break;

 		case TYPE_R5G6B5:
			w3dFormat = WW3D_FORMAT_R5G6B5;
			break;

 		case TYPE_X1R5G5B5:
			w3dFormat = WW3D_FORMAT_X1R5G5B5;
			break;
	}

	return w3dFormat;
}

//============================================================================
// W3DFormatToType
//============================================================================

VideoBuffer::Type W3DVideoBuffer::W3DFormatToType( WW3DFormat w3dFormat )
{
	Type format = TYPE_UNKNOWN;
	switch ( w3dFormat )
	{
		case WW3D_FORMAT_X8R8G8B8:
				format = VideoBuffer::TYPE_X8R8G8B8;
				break;
		case WW3D_FORMAT_R8G8B8:
				format = VideoBuffer::TYPE_R8G8B8;
				break;
		case WW3D_FORMAT_R5G6B5:
				format = VideoBuffer::TYPE_R5G6B5;
				break;
		case WW3D_FORMAT_X1R5G5B5:
				format = VideoBuffer::TYPE_X1R5G5B5;
				break;
	}

	return format;
}