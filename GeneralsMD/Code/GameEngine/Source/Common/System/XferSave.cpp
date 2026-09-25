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

// FILE: XferSave.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, February 2002
// Desc:   Xfer disk write implementation
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/XferSave.h"
#include "Common/Snapshot.h"
#include "Common/GameMemory.h"

// PRIVATE TYPES //////////////////////////////////////////////////////////////////////////////////
class XferBlockData : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(XferBlockData, "XferBlockData")		

public:

	XferFilePos filePos;			///< the file position of this block
	XferBlockData *next;			///< next block on the stack

};
EMPTY_DTOR(XferBlockData)

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC METHDOS /////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
XferSave::XferSave( void )
{

	m_xferMode = XFER_SAVE;
	m_fileFP = NULL;
	m_blockStack = NULL;

}  // end XferSave

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
XferSave::~XferSave( void )
{

	// warn the user if a file was left open
	if( m_fileFP != NULL )
	{

		DEBUG_CRASH(( "Warning: Xfer file '%s' was left open\n", m_identifier.str() ));
		close();

	}  // end if

	//
	// the block stack should be empty, if it's not that means we started blocks but never
	// called enough matching end blocks
	//
	if( m_blockStack != NULL )
	{

		// tell the user there is an error
		DEBUG_CRASH(( "Warning: XferSave::~XferSave - m_blockStack was not NULL!\n" ));

		// delete the block stack
		XferBlockData *next;
		while( m_blockStack )
		{

			next = m_blockStack->next;
			m_blockStack->deleteInstance();
			m_blockStack = next;

		}  // end while

	}  // end if

}  // end ~XferSave

//-------------------------------------------------------------------------------------------------
/** Open file 'identifier' for writing */
//-------------------------------------------------------------------------------------------------
void XferSave::open( AsciiString identifier )
{

	// sanity, check to see if we're already open
	if( m_fileFP != NULL )
	{

		DEBUG_CRASH(( "Cannot open file '%s' cause we've already got '%s' open\n",
									identifier.str(), m_identifier.str() ));
		throw XFER_FILE_ALREADY_OPEN;

	}  // end if

	// call base class
	Xfer::open( identifier );

	// open the file
	m_fileFP = fopen( identifier.str(), "w+b" );
	if( m_fileFP == NULL )
	{
		
		DEBUG_CRASH(( "File '%s' not found\n", identifier.str() ));
		throw XFER_FILE_NOT_FOUND;

	}  // end if

}  // end open

//-------------------------------------------------------------------------------------------------
/** Close our current file */
//-------------------------------------------------------------------------------------------------
void XferSave::close( void )
{

	// sanity, if we don't have an open file we can do nothing
	if( m_fileFP == NULL )
	{

		DEBUG_CRASH(( "Xfer close called, but no file was open\n" ));
		throw XFER_FILE_NOT_OPEN;

	}  // end if

	// write the file and close it
	const size_t size = m_data.size();
	const Bool written = size == 0 || fwrite( &m_data[ 0 ], size, 1, m_fileFP ) == 1;
	fclose( m_fileFP );
	m_fileFP = NULL;
	m_data.clear();
	if( !written )
	{
		DEBUG_LOG(( "XferSave - Error writing %u bytes to file '%s'\n", (UnsignedInt)size, m_identifier.str() ));
		throw XFER_WRITE_ERROR;
	}

	// erase the filename
	m_identifier.clear();

}  // end close

//-------------------------------------------------------------------------------------------------
/** Write a placeholder at the current location in the file and store this location
	* internally.  The next endBlock that is called will back up to the most recently stored
	* beginBlock and write the difference in file bytes from the endBlock call to the 
	* location of this beginBlock.  The current file position will then return to the location
	* at which endBlock was called */
//-------------------------------------------------------------------------------------------------
Int XferSave::beginBlock( void )
{

	// sanity
	DEBUG_ASSERTCRASH( m_fileFP != NULL, ("Xfer begin block - file pointer for '%s' is NULL\n",
										 m_identifier.str()) );

	// get the current file position so we can back up here for the next end block call
	XferFilePos filePos = (XferFilePos)m_data.size();

	// write a placeholder
	XferBlockSize blockSize = 0;
	xferImplementation( &blockSize, sizeof( XferBlockSize ) );

	// save this block position on the top of the "stack"
	XferBlockData *top = newInstance(XferBlockData);
// impossible -- exception will be thrown (srj)
//	if( top == NULL )
//	{
//
//		DEBUG_CRASH(( "XferSave - Begin block, out of memory - can't save block stack data\n" ));
//		return XFER_OUT_OF_MEMORY;
//
//	}  // end if

	top->filePos = filePos;
	top->next = m_blockStack;
	m_blockStack = top;

	return XFER_OK;

}  // end beginBlock

//-------------------------------------------------------------------------------------------------
/** Do the tail end as described in beginBlock above.  Back up to the last begin block,
	* write the file difference from current position to the last begin position, and put
	* current file position back to where it was */
//-------------------------------------------------------------------------------------------------
void XferSave::endBlock( void )
{

	// sanity
	DEBUG_ASSERTCRASH( m_fileFP != NULL, ("Xfer end block - file pointer for '%s' is NULL\n",
										 m_identifier.str()) );

	// sanity, make sure we have a block started
	if( m_blockStack == NULL )
	{

		DEBUG_CRASH(( "Xfer end block called, but no matching begin block was found\n" ));
		throw XFER_BEGIN_END_MISMATCH;

	}  // end if

	// save our current file position
	XferFilePos currentFilePos = (XferFilePos)m_data.size();

	// pop the block descriptor off the top of the block stack
	XferBlockData *top = m_blockStack;
	m_blockStack = m_blockStack->next;

	// write the size in bytes between the block position and what is our current file position
	// over the placeholder
	XferBlockSize blockSize = currentFilePos - top->filePos - sizeof( XferBlockSize );
	memcpy( &m_data[ top->filePos ], &blockSize, sizeof( XferBlockSize ) );

	// delete the block data as it's all used up now
	top->deleteInstance();

}  // end endBlock

//-------------------------------------------------------------------------------------------------
/** Skip forward 'dataSize' bytes in the file */
//-------------------------------------------------------------------------------------------------
void XferSave::skip( Int dataSize )
{

	// sanity
	DEBUG_ASSERTCRASH( m_fileFP != NULL, ("XferSave - file pointer for '%s' is NULL\n",
										 m_identifier.str()) );


	// skip forward dataSize bytes, which a file sought past its end fills with zeros
	m_data.resize( m_data.size() + dataSize, 0 );

}  // end skip

// ------------------------------------------------------------------------------------------------
/** Entry point for xfering a snapshot */
// ------------------------------------------------------------------------------------------------
void XferSave::xferSnapshot( Snapshot *snapshot )
{

	if( snapshot == NULL )
	{

		DEBUG_CRASH(( "XferSave::xferSnapshot - Invalid parameters\n" ));
		throw XFER_INVALID_PARAMETERS;

	}  // end if

	// run the xfer function of the snapshot
	snapshot->xfer( this );

}  // end xferSnapshot

// ------------------------------------------------------------------------------------------------
/** Save ascii string */
// ------------------------------------------------------------------------------------------------
void XferSave::xferAsciiString( AsciiString *asciiStringData )
{

	// sanity
	if( asciiStringData->getLength() > 255 )
	{

		DEBUG_CRASH(( "XferSave cannot save this unicode string because it's too long.  Change the size of the length header (but be sure to preserve save file compatability\n" ));
		throw XFER_STRING_ERROR;

	}  // end if
	
	// save length of string to follow
	UnsignedByte len = asciiStringData->getLength();
	xferUnsignedByte( &len );

	// save string data
	if( len > 0 )
		xferUser( (void *)asciiStringData->str(), sizeof( Byte ) * len );

}  // end xferAsciiString

// ------------------------------------------------------------------------------------------------
/** Save unicodee string */
// ------------------------------------------------------------------------------------------------
void XferSave::xferUnicodeString( UnicodeString *unicodeStringData )
{
	
	// sanity
	if( unicodeStringData->getLength() > 255 )
	{

		DEBUG_CRASH(( "XferSave cannot save this unicode string because it's too long.  Change the size of the length header (but be sure to preserve save file compatability\n" ));
		throw XFER_STRING_ERROR;

	}  // end if

	// save length of string to follow
	UnsignedByte len = unicodeStringData->getLength();
	xferUnsignedByte( &len );

	// save string data
	if( len > 0 )
		xferUser( (void *)unicodeStringData->str(), sizeof( WideChar ) * len );

}  // end xferUnicodeString

//-------------------------------------------------------------------------------------------------
/** Perform the write operation */
//-------------------------------------------------------------------------------------------------
void XferSave::xferImplementation( void *data, Int dataSize )
{

	// sanity
	DEBUG_ASSERTCRASH( m_fileFP != NULL, ("XferSave - file pointer for '%s' is NULL\n",
										 m_identifier.str()) );

	// the file is written out on close
	const char *bytes = (const char *)data;
	m_data.insert( m_data.end(), bytes, bytes + dataSize );

}  // end xferImplementation
