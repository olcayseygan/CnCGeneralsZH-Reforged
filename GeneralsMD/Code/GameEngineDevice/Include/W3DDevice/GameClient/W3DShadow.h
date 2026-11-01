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


#pragma once

#ifndef __W3DSHADOW_H_
#define __W3DSHADOW_H_

#include "matrix4.h"
#include "GameClient/Shadow.h"
#include "WW3D2/dx8wrapper.h"

class Drawable;	//forward reference

/** Copy one range into a streaming buffer and its Direct3D 11 twin.  While Direct3D 11 presents,
		Direct3D 9 draws nothing indexed, so its buffer is left alone and the twin is written in place; a
		twin the device refused keeps the Direct3D 9 copy. */
template <class D3D9_BUFFER>
inline Bool uploadStreamingRange(D3D9_BUFFER *d3d9Buffer, DX11BufferTwinClass *twin, const void *source,
	UnsignedInt byteOffset, UnsignedInt byteCount, UnsignedInt lockFlags)
{
	const Bool d3d9Draws = !Direct3D11_Present_Is_Enabled() || twin == NULL;
	void *d3d9Memory = NULL;
	if (d3d9Draws && d3d9Buffer->Lock(byteOffset, byteCount, &d3d9Memory, lockFlags) != D3D_OK)
		return FALSE;	//a lost device refuses the lock
	DX11BufferLockClass lock;
	void *target = lock.Begin(twin, d3d9Memory, byteOffset, byteCount, lockFlags, d3d9Draws);
	memcpy(target != NULL ? target : d3d9Memory, source, byteCount);
	lock.End();
	if (d3d9Draws)
		d3d9Buffer->Unlock();
	return TRUE;
}

// ShadowManager -------------------------------------------------------------
class W3DShadowManager
{

public:
	
	W3DShadowManager( void );
	~W3DShadowManager( void );
	Bool init( void );	///<initialize resources used by manager, must have valid D3D device.
	void queueShadows(Bool state) {m_isShadowScene=state;}	///<flags system to process shadows on next render call.

	// shadow list management
	void Reset( void );
	Shadow* addShadow( RenderObjClass *robj,Shadow::ShadowTypeInfo *shadowInfo=NULL, Drawable *draw=NULL);	///< adds shadow caster to rendering system.
	void removeShadow(Shadow *shadow);	///< removed shadow from rendering system and frees its resources.
	void removeAllShadows(void); ///< Remove all shadows.
	void setShadowColor(UnsignedInt color) { m_shadowColor=color;}	///<sets the shadow color and alpha, value in ARGB format.
	UnsignedInt getShadowColor() { return m_shadowColor;}	///<gets the shadow color and alpha, value in ARGB format.
	void setLightPosition(Int lightIndex, Real x, Real y, Real z);	///<sets the position of a specific light source.
	void setTimeOfDay(TimeOfDay tod);
	void invalidateCachedLightPositions(void);	///<forces shadow volumes to update regardless of last lightposition
	Vector3 &getLightPosWorld(Int lightIndex);	///<returns the position of specified light source.
	Bool	isShadowScene(void)	{return m_isShadowScene;}
	inline void setStencilShadowMask(int mask) {m_stencilShadowMask=mask;}	///<mask used to mask out stencil bits used for storing occlusion/playerColor
	inline Int getStencilShadowMask(void)	{return m_stencilShadowMask;}

	// rendering
	void RenderShadows( void );
	void ReleaseResources(void);
	Bool ReAcquireResources(void);

protected:

		Bool	m_isShadowScene;	///<flag if current scene needs shadows.  No shadows on pre-pass and 2D.
		UnsignedInt m_shadowColor;	///<color and alpha for all shadows in scene.
		Int m_stencilShadowMask;
};  // end class W3DShadowManager

extern W3DShadowManager *TheW3DShadowManager;

class FrustumClass;
class SphereClass;
Bool shadowCanReachView( const FrustumClass &view, const SphereClass &body, Real groundZ, Real runX, Real runY );	///< whether a body's shadow, running runX, runY sideways per unit of drop down to groundZ, can reach the view
Bool volumeShadowCanReachView( const FrustumClass &view, const SphereClass &body, Real groundZ );	///< the same along the ray from the shadow light's position

#endif	//__W3DSHADOW_H_
