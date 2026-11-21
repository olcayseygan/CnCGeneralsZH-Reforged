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

// FILE: W3DFractureDraw.h
// A structure drawn as the pre-fractured copy of its model once it is hit (the Lane War fortress and
// watch tower).  It rides beside the object's W3DModelDraw, which keeps the turret, the bones, the
// condition states and the picking: until the first hit nothing changes, then that model is hidden
// and its fragments are drawn in its place, posed on its bones.  A hit knocks out the fragments where
// it landed (FractureCore.h) and throws them as debris; at zero health the whole structure comes down
// by its baked collapse, or by a plain fall when it has none, and the fragments lie as its ruin.
// Client only: the logic says where a hit landed (Drawable::reactToImpact) and nothing goes back.
// The pick follows the fragments' boxes, a save keeps the holes (the drawable's xfer) and the intact
// model's stencil shadow goes once the fragments stand in for it.

#pragma once

#ifndef __W3D_FRACTURE_DRAW_H_
#define __W3D_FRACTURE_DRAW_H_

#include "Common/DrawModule.h"

#include <vector>

class RenderInfoClass;
class FractureRenderObjClass;

//-------------------------------------------------------------------------------------------------
class W3DFractureDrawModuleData : public ModuleData
{
public:
	AsciiString m_fractureFile;						///< Art/W3D/<name>.lwfrag, its .lwcollapse and the <name>.w3d it was cut from
	std::vector<AsciiString> m_gunBones;	///< fragments on these bones are never knocked out by a hit

	W3DFractureDrawModuleData() { }
	static void buildFieldParse( MultiIniFieldParse &p );
};

//-------------------------------------------------------------------------------------------------
class W3DFractureDraw : public DrawModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( W3DFractureDraw, "W3DFractureDraw" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( W3DFractureDraw, W3DFractureDrawModuleData )

public:

	W3DFractureDraw( Thing *thing, const ModuleData *moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void doDrawModule( const Matrix3D *transformMtx );
	virtual void setShadowsEnabled( Bool enable ) { }
	virtual void releaseShadows( void ) { }
	virtual void allocateShadows( void ) { }
	virtual void setFullyObscuredByShroud( Bool fullyObscured );
	virtual void reactToTransformChange( const Matrix3D *oldMtx, const Coord3D *oldPos, Real oldAngle ) { }
	virtual void reactToGeometryChange( void ) { }
	virtual void reactToImpact( const Coord3D &pos, Real radius, const Coord3D *source );

	/// Once a frame before the views draw (W3DDisplay::draw): what falls, falls on the screen or off it.
	static void updateAll( void );
	/// Every fractured structure's fragments into the sun's depth map (W3DVolumetricShadowManager).
	static void renderShadowCasters( RenderInfoClass &sunInfo );
	/// TRUE for a drawable whose fall this module draws, which the Collapse option then leaves alone.
	static Bool stagesCollapse( const Drawable *draw );

private:

	Bool makeFracture( void );

	FractureRenderObjClass *m_fracture;						///< made at the first hit or the death
	std::vector<unsigned char> m_loadedStanding;	///< a load's holes, until the fragments are made
	Int m_loadedWay;															///< and the baked way it fell, -1 for none
	Bool m_loaded;																///< dead in a loaded save: the ruin it shows fell before it

};

#endif // __W3D_FRACTURE_DRAW_H_
