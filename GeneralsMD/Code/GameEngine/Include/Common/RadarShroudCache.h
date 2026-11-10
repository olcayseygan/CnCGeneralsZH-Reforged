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

// FILE: RadarShroudCache.h ///////////////////////////////////////////////////////////////////////
// Desc:   The radar's shroud layer, kept in main memory and uploaded once a frame.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _RADAR_SHROUD_CACHE_H_
#define _RADAR_SHROUD_CACHE_H_

#include "Lib/BaseType.h"

/* The radar draws the shroud as a small texture - 128x128 - with black pixels whose alpha says how
	 hidden that part of the map is.  The simulation pokes at it one map cell at a time: every time a
	 unit's line of sight crosses a shroud cell boundary, the cell tells the radar the new state of
	 that one cell, which is one to four pixels.

	 That used to go straight at the texture, and the road it took was expensive beyond belief.  Each
	 call asked the texture for its surface (a Direct3D GetSurfaceLevel plus a wrapper object), then
	 called DrawPixel once per pixel - and DrawPixel is a GetDesc, a LockRect of a one-pixel
	 rectangle, one byte written, and an UnlockRect.  An army on the move crosses hundreds of shroud
	 cells a frame, and the first thing a match does is walk the whole map through
	 refreshShroudForLocalPlayer, one cell at a time.  EA left a note next to the radar's terrain
	 drawing saying they had tried locking once and it made "absolutely *no* performance difference";
	 that was about the terrain, which is drawn once per map, and it is not true of the shroud, which
	 is drawn all match long.

	 So the shroud layer lives here instead, one alpha byte per texture pixel, and the texture is
	 written at most once a frame, over the rectangle that actually changed.  Nothing here touches
	 Direct3D, which is also what makes it testable. */

class RadarShroudCache
{
public:

	RadarShroudCache();
	~RadarShroudCache();

	/** Give the cache the size of the texture it shadows.  Everything starts clear (alpha 0) and
			fully dirty, because the texture it shadows has just been created and holds nothing. */
	void setSize( Int width, Int height );

	/** Every pixel to one alpha, and the whole surface dirty. */
	void clear( UnsignedByte alpha );

	/** One pixel.  Points outside the texture are dropped, which is what the caller's
			legalRadarPoint test used to do; a write that changes nothing does not dirty anything. */
	void setAlpha( Int x, Int y, UnsignedByte alpha );

	UnsignedByte getAlpha( Int x, Int y ) const;

	Bool isDirty( void ) const { return m_dirty; }

	// the dirty rectangle, inclusive on both ends; only meaningful while isDirty()
	Int getDirtyMinX( void ) const { return m_dirtyMinX; }
	Int getDirtyMinY( void ) const { return m_dirtyMinY; }
	Int getDirtyMaxX( void ) const { return m_dirtyMaxX; }
	Int getDirtyMaxY( void ) const { return m_dirtyMaxY; }

	/** Write the dirty rectangle into a locked surface and mark the cache clean.  bytesPerPixel is
			the surface's, and the colour written is black with our alpha - GameMakeColor( 0, 0, 0, a )
			truncated to the pixel size, exactly what DrawPixel used to write.  Does nothing, and
			stays dirty, if there is no buffer to write into. */
	void flushTo( void *bits, Int pitch, Int bytesPerPixel );

	/** Forget the dirty rectangle, for a caller that wrote it into its texture its own way. */
	void markClean( void ) { m_dirty = FALSE; m_dirtyMinX = 0; m_dirtyMinY = 0; m_dirtyMaxX = -1; m_dirtyMaxY = -1; }

private:

	void markDirty( Int x, Int y );

	UnsignedByte *m_alpha;				///< one byte per texture pixel, row major, no padding
	Int m_width;
	Int m_height;

	Bool m_dirty;
	Int m_dirtyMinX;
	Int m_dirtyMinY;
	Int m_dirtyMaxX;
	Int m_dirtyMaxY;

	// no copying: it owns a buffer and nothing needs to copy one
	RadarShroudCache( const RadarShroudCache & );
	RadarShroudCache &operator=( const RadarShroudCache & );
};

#endif  // _RADAR_SHROUD_CACHE_H_
