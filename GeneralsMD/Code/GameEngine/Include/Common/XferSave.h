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

// FILE: XferSave.h ///////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, February 2002
// Desc:   Xfer hard disk write implementation
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __XFER_SAVE_H_
#define __XFER_SAVE_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/Xfer.h"

#include <vector>

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class XferBlockData;
class Snapshot;

///////////////////////////////////////////////////////////////////////////////////////////////////
typedef long XferFilePos;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class XferSave : public Xfer
{

public:

	XferSave( void );
	virtual ~XferSave( void );

	// Xfer methods
	virtual void open( AsciiString identifier );		///< open file for writing
	virtual void close( void );											///< write the file and put it in place
	/// Drop what was written and leave any file already at the identifier as it was.  Close writes
	/// into "<identifier>.tmp" and renames that over the identifier only once every byte is down, so
	/// a full disk or a crash mid-save never costs the save that was there.
	void discard( void );
	/// No file at all: the bytes stay in memory for takeMemory, sized up front to reserveBytes.  A
	/// network match's rejoin checkpoint is written this way on the frame and put on the disk by a
	/// thread of its own (Network.cpp).
	void openInMemory( AsciiString identifier, size_t reserveBytes );
	void takeMemory( std::vector< char > &bytes );			///< what was written, and the xfer closed
	virtual Int beginBlock( void );									///< write placeholder block size
	virtual void endBlock( void );									///< backup to last begin block and write size
	virtual void skip( Int dataSize );							///< skipping during a write is a no-op

	virtual void xferSnapshot( Snapshot *snapshot );		///< entry point for xfering a snapshot

	// xfer methods
	virtual void xferAsciiString( AsciiString *asciiStringData );  ///< xfer ascii string (need our own)
	virtual void xferUnicodeString( UnicodeString *unicodeStringData );	///< xfer unicode string (need our own);

protected:

	virtual void xferImplementation( void *data, Int dataSize );		///< the xfer implementation

	FILE * m_fileFP;																			///< pointer to file
	XferBlockData *m_blockStack;													///< stack of block data
	/// The whole file, written out in one go on close.  Written straight to the file, every endBlock
	/// sought back to fill its size in and forward again, and each seek emptied the CRT's buffer: a
	/// replay checkpoint took 30 to 60ms that way and takes 6 to 13 from memory.
	std::vector< char > m_data;
	Bool m_inMemory;																			///< openInMemory: no file behind m_data

};

#endif // __XFER_SAVE_H_

