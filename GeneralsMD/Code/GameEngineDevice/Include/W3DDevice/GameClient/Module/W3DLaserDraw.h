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

// FILE: W3DLaserDraw.h ///////////////////////////////////////////////////////////////////////////
// Author: 
// Desc:   
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DLASERDRAW_H_
#define __W3DLASERDRAW_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/DrawModule.h"
//#include "WW3D2/Line3D.h"
#include "GameClient/Color.h"
#include "WWMath/vector3.h"

#include <vector>

class SegmentedLineClass;
class TextureClass;

/// -heathaze: a glowing beam on show, for the air round it to bend (gatherHeatHaze)
struct LaserHeat
{
	Vector3 start;
	Vector3 end;
	Real glowWidth;		///< the halo's whole width, world units
	Real coreRadius;	///< half the body's width: the beam a viewer sees, which must not move
	Real light;				///< the glow's brightness with the fade, 0 to 1
};

class W3DLaserDrawModuleData : public ModuleData
{
public:

  Color m_innerColor;
  Color m_outerColor;
	Real m_innerBeamWidth;
	Real m_outerBeamWidth;
	Real m_scrollRate;
	Bool m_tile;
  UnsignedInt m_numBeams;
  UnsignedInt m_maxIntensityFrames;
  UnsignedInt m_fadeFrames;
	AsciiString m_textureName;
	UnsignedInt m_segments;
	Real m_arcHeight;
	Real m_segmentOverlapRatio;
	Real m_tilingScalar;

	W3DLaserDrawModuleData();
	~W3DLaserDrawModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

	virtual Bool getLaserTemplateWidth( Real &width ) const { width = m_outerBeamWidth * 0.5f; return TRUE; }
};

//-------------------------------------------------------------------------------------------------
/** W3D laser draw */
//-------------------------------------------------------------------------------------------------
class W3DLaserDraw : public DrawModule, public LaserDrawInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( W3DLaserDraw, "W3DLaserDraw" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( W3DLaserDraw, W3DLaserDrawModuleData )
		
public:

	W3DLaserDraw( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void doDrawModule(const Matrix3D* transformMtx);
	virtual void smoothMotionApply(Real alpha);	///< every -recordfps picture: the head and the flicker move on W3D's clock
	virtual void buildRenderObjects( Bool madeInTick );
	virtual void releaseShadows(void) {};	///< we don't care about preserving temporary shadows.	
	virtual void allocateShadows(void) {};	///< we don't care about preserving temporary shadows.
	virtual void setShadowsEnabled(Bool enable) { }
	virtual void setFullyObscuredByShroud(Bool fullyObscured) { };
	virtual void reactToTransformChange(const Matrix3D* oldMtx, const Coord3D* oldPos, Real oldAngle) { }
	virtual void reactToGeometryChange() { }
	virtual Bool isLaser() const { return true; }
	Real getLaserTemplateWidth() const;
	
	virtual LaserDrawInterface* getLaserDrawInterface() { return this; }
	virtual const LaserDrawInterface* getLaserDrawInterface() const { return this; }

	static void gatherHeat( std::vector<LaserHeat> &beams );	///< every glowing beam on show, appended

protected:

	void drawGlow( Real widthScale, Real sinceArrivalMs );

	SegmentedLineClass **m_line3D;  ///< line 3D for effect
	SegmentedLineClass **m_glow3D;  ///< halo, body and hot core per segment, NULL for beams that do not glow
	SegmentedLineClass *m_flare3D[ 4 ];	///< muzzle glow and core, impact glow and core; made beside m_glow3D
	Vector3 m_beamStart;						///< the first segment's start and the last one's end, for the flares
	Vector3 m_beamEnd;
	Real m_glowWidth;								///< the halo's and the body's widths as last placed, for gatherHeat
	Real m_bodyWidth;
	Real m_light;										///< the glow's brightness on the last draw, for gatherHeat
	UnsignedInt m_birthFrame;				///< logic frame the lines were built on, for the fade in and out
	UnsignedInt m_lifetimeFrames;		///< the template's LifetimeUpdate, 0 when it has none (no fade out)
	UnsignedInt m_birthMs;					///< picture clock of the first draw, for the travel
	Bool m_started;									///< drawn once, so m_birthMs and m_travels are set
	Bool m_travels;									///< travels out from the muzzle; false when it carries on from a beam just drawn
	Bool m_travelDrawn;							///< the last draw had the head on its way, so this one places the whole beam
	TextureClass *m_texture;
	Real m_textureAspectRatio;			///< aspect ratio of texture
	Bool m_selfDirty;								// not saved

};

#endif // __W3DLASERDRAW_H_

