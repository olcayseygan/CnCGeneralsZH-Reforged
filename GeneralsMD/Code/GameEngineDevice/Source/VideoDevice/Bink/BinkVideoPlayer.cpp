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
// Module:    VideoDevice
//
// File name: BinkDevice.cpp
//
// Created:   10/22/01	TR
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "Lib/BaseType.h"
#include "VideoDevice/Bink/BinkVideoPlayer.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AudioAffect.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/Registry.h"

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------


 
//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------
#define VIDEO_LANG_PATH_FORMAT "Data/%s/Movies/%s.%s"
#define VIDEO_PATH	"Data\\Movies"
#define VIDEO_EXT		"bik"



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
// BinkVideoPlayer::BinkVideoPlayer
//============================================================================

BinkVideoPlayer::BinkVideoPlayer()
{

}

//============================================================================
// BinkVideoPlayer::~BinkVideoPlayer
//============================================================================

BinkVideoPlayer::~BinkVideoPlayer()
{
	deinit();
}

//============================================================================
// BinkVideoPlayer::init
//============================================================================

void	BinkVideoPlayer::init( void )
{
	// Need to load the stuff from the ini file.
	VideoPlayer::init();

	initializeBinkWithMiles();
}

//============================================================================
// BinkVideoPlayer::deinit
//============================================================================

void BinkVideoPlayer::deinit( void )
{
	TheAudio->releaseHandleForBink();
	VideoPlayer::deinit();
}

//============================================================================
// BinkVideoPlayer::reset
//============================================================================

void	BinkVideoPlayer::reset( void )
{
	VideoPlayer::reset();
}

//============================================================================
// BinkVideoPlayer::update
//============================================================================

void	BinkVideoPlayer::update( void )
{
	VideoPlayer::update();

}

//============================================================================
// BinkVideoPlayer::loseFocus
//============================================================================

void	BinkVideoPlayer::loseFocus( void )
{
	VideoPlayer::loseFocus();
}

//============================================================================
// BinkVideoPlayer::regainFocus
//============================================================================

void	BinkVideoPlayer::regainFocus( void )
{
	VideoPlayer::regainFocus();
}

//============================================================================
// BinkVideoPlayer::createStream
//============================================================================

VideoStreamInterface* BinkVideoPlayer::createStream( HBINK handle )
{

	if ( handle == NULL )
	{
		return NULL;
	}

	BinkVideoStream *stream = NEW BinkVideoStream;

	if ( stream )
	{

		stream->m_handle = handle;
		stream->m_next = m_firstStream;
		stream->m_player = this;
		m_firstStream = stream;

		// Bink drops a volume set before its first BinkWait, so without this every movie played at
		// full level whatever the speech slider said.
		BinkWait( stream->m_handle );
		BinkSetVolume( stream->m_handle, 0, movieAudioVolume( TheAudio->getVolume(AudioAffect_Speech) ) );
	}

	return stream;
}

//============================================================================
// BinkVideoPlayer::movieAudioVolume
//============================================================================

Int BinkVideoPlayer::movieAudioVolume( Real volume )
{
	// never let volume go to 0, as Bink will interpret that as "play at full volume".
	Int mod = (Int) ((volume * 0.8f) * 100) + 1;
	return (32768*mod)/100;
}

//============================================================================
// BinkVideoPlayer::setVolume
//============================================================================

void BinkVideoPlayer::setVolume( Real volume )
{
	// Movie audio goes out through Bink, not through the Miles mixer, so a slider change made while
	// a movie is open has to be handed to each stream.
	Int binkVolume = movieAudioVolume( volume );
	for ( VideoStreamInterface* stream = firstStream(); stream != NULL; stream = stream->next() )
	{
		BinkSetVolume( static_cast<BinkVideoStream*>( stream )->m_handle, 0, binkVolume );
	}
}

//============================================================================
// BinkVideoPlayer::open
//============================================================================

VideoStreamInterface*	BinkVideoPlayer::open( AsciiString movieTitle )
{
	VideoStreamInterface*	stream = NULL;

	//
	// VideoOn is what -headless and -novideo turn off, and nothing had ever read it: a run with no
	// picture still opened the file, decoded every frame of it and played its audio.  Every caller
	// already copes with a NULL stream - that is what a missing movie file gives them - so this is
	// the one place the switch has to be honoured.
	//
	if (TheGlobalData && !TheGlobalData->m_videoOn)
		return NULL;

	const Video* pVideo = getVideo(movieTitle);
	if (pVideo) {
		DEBUG_LOG(("BinkVideoPlayer::createStream() - About to open bink file\n"));

		char localizedFilePath[ _MAX_PATH ];
		sprintf( localizedFilePath, VIDEO_LANG_PATH_FORMAT, GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );
		HBINK handle = BinkOpen(localizedFilePath , BINKPRELOADALL );
		DEBUG_ASSERTLOG(!handle, ("opened localized bink file %s\n", localizedFilePath));
		if (!handle)
		{
			char filePath[ _MAX_PATH ];
			snprintf( filePath, ARRAY_SIZE(filePath), "%s\\%s.%s", VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );
			handle = BinkOpen(filePath , BINKPRELOADALL );
			DEBUG_ASSERTLOG(!handle, ("opened bink file %s\n", localizedFilePath));
		}

		// the original Generals campaign movies stay loose in the base game's own folder (Steam's
		// ZH_Generals\Data\Movies), which none of the paths above reach
		const AsciiString &baseDir = TheArchiveFileSystem->m_baseGameDirectory;
		if (!handle && baseDir.isNotEmpty())
		{
			char filePath[ _MAX_PATH ];
			snprintf( filePath, ARRAY_SIZE(filePath), "%s" VIDEO_LANG_PATH_FORMAT, baseDir.str(), GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );
			handle = BinkOpen(filePath , BINKPRELOADALL );
			if (!handle)
			{
				snprintf( filePath, ARRAY_SIZE(filePath), "%s%s\\%s.%s", baseDir.str(), VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );
				handle = BinkOpen(filePath , BINKPRELOADALL );
			}
			DEBUG_ASSERTLOG(!handle, ("opened bink file %s\n", filePath));
		}

		DEBUG_LOG(("BinkVideoPlayer::createStream() - About to create stream\n"));
		stream = createStream( handle );
	}

	return stream;	
}

//============================================================================
// BinkVideoPlayer::load
//============================================================================

VideoStreamInterface*	BinkVideoPlayer::load( AsciiString movieTitle )
{
	return open(movieTitle); // load() used to have the same body as open(), so I'm combining them.  Munkee.
}

//============================================================================
//============================================================================
void BinkVideoPlayer::notifyVideoPlayerOfNewProvider( Bool nowHasValid )
{
	if (!nowHasValid) {
		TheAudio->releaseHandleForBink();
		BinkSetSoundTrack(0, 0);
	} else {
		initializeBinkWithMiles();
	}
}

//============================================================================
//============================================================================
void BinkVideoPlayer::initializeBinkWithMiles()
{
	Int retVal = 0;
	void *driver = TheAudio->getHandleForBink();	
	
	if ( driver )
	{
		retVal = BinkSoundUseDirectSound(driver);
	}
	if( !driver || retVal == 0)
	{
		BinkSetSoundTrack ( 0,0 );
	}
}

//============================================================================
// BinkVideoStream::BinkVideoStream
//============================================================================

BinkVideoStream::BinkVideoStream()
: m_handle(NULL)
{

}

//============================================================================
// BinkVideoStream::~BinkVideoStream
//============================================================================

BinkVideoStream::~BinkVideoStream()
{
	if ( m_handle != NULL )
	{
		BinkClose( m_handle );
		m_handle = NULL;
	}
}

//============================================================================
// BinkVideoStream::update
//============================================================================

void BinkVideoStream::update( void )
{
	BinkWait( m_handle );
}

//============================================================================
// BinkVideoStream::isFrameReady
//============================================================================

Bool BinkVideoStream::isFrameReady( void )
{
	return !BinkWait( m_handle );
}

//============================================================================
// BinkVideoStream::frameDecompress
//============================================================================

void BinkVideoStream::frameDecompress( void )
{
		BinkDoFrame( m_handle );
}

//============================================================================
// BinkVideoStream::frameRender
//============================================================================

void BinkVideoStream::frameRender( VideoBuffer *buffer )
{
	if ( buffer )
	{
		void *mem = buffer->lock();

		u32 flags;

		switch ( buffer->format())
		{
			case VideoBuffer::TYPE_X8R8G8B8:
				flags = BINKSURFACE32;
				break;

			case VideoBuffer::TYPE_R8G8B8:
				flags = BINKSURFACE24;
				break;

			case VideoBuffer::TYPE_R5G6B5:
				flags = BINKSURFACE565;
				break;

			case VideoBuffer::TYPE_X1R5G5B5:
				flags = BINKSURFACE555;
				break;

			default:
				return;
		}
		
		if ( mem != NULL )
		{

			BinkCopyToBuffer ( m_handle, mem, buffer->pitch(), buffer->height(),
													buffer->xPos(), buffer->yPos(), flags );
			buffer->unlock();
		}
	}

}

//============================================================================
// BinkVideoStream::frameNext
//============================================================================

void BinkVideoStream::frameNext( void )
{
	BinkNextFrame( m_handle );
}

//============================================================================
// BinkVideoStream::frameIndex
//============================================================================

Int BinkVideoStream::frameIndex( void )
{
	return m_handle->FrameNum - 1;
}

//============================================================================
// BinkVideoStream::totalFrames
//============================================================================

Int	BinkVideoStream::frameCount( void )
{
	return m_handle->Frames;
}

//============================================================================
// BinkVideoStream::frameGoto
//============================================================================

void BinkVideoStream::frameGoto( Int index )
{
	BinkGoto(m_handle, index, NULL );
}

//============================================================================
// VideoStream::height
//============================================================================

Int		BinkVideoStream::height( void )
{
	return m_handle->Height;
}

//============================================================================
// VideoStream::width
//============================================================================

Int		BinkVideoStream::width( void )
{
	return m_handle->Width;
}



