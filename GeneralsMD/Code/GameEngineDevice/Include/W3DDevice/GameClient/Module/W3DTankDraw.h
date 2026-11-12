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

// FIEL: W3DTankDraw.h ////////////////////////////////////////////////////////////////////////////
// Draw a tank
// Author: Michael S. Booth, October 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _W3D_TANK_DRAW_H_
#define _W3D_TANK_DRAW_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/DrawModule.h"
#include "GameClient/ParticleSys.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "WW3D2/hanim.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/part_emt.h"

//-------------------------------------------------------------------------------------------------
class W3DTankDrawModuleData : public W3DModelDrawModuleData
{
public:
	AsciiString m_treadDebrisNameLeft;
	AsciiString m_treadDebrisNameRight;

	VelocityPerFrame m_treadAnimationRate;	///<amount of tread texture to scroll per sec at top speed.  1.0 == full width.
	Real m_treadPivotSpeedFraction;	///<no longer read: the treads follow the ground. Kept so the INI key parses.
	Real m_treadDriveSpeedFraction;	///<no longer read either.

	W3DTankDrawModuleData();
	~W3DTankDrawModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
class W3DTankDraw : public W3DModelDraw
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( W3DTankDraw, "W3DTankDraw" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( W3DTankDraw, W3DTankDrawModuleData )
		
public:

	W3DTankDraw( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void setHidden(Bool h);
	virtual void doDrawModule(const Matrix3D* transformMtx);
	virtual void buildRenderObjects( Bool madeInTick );
	virtual void setFullyObscuredByShroud(Bool fullyObscured);

protected:
	virtual void onRenderObjRecreated(void);

protected:

	/// debris emitters for when tank is moving
	ParticleSystem* m_treadDebrisLeft;
	ParticleSystem* m_treadDebrisRight;

	RenderObjClass *m_prevRenderObj;

	enum TreadType { TREAD_LEFT, TREAD_RIGHT, TREAD_MIDDLE };	//types of treads for different vehicles
	// four belts a level of detail, for up to four levels: every level's belts scroll, so the one drawn does
	enum {MAX_TREADS_PER_TANK=4, MAX_TREAD_LODS=4, MAX_TREAD_SLOTS=MAX_TREADS_PER_TANK*MAX_TREAD_LODS};

	struct TreadObjectInfo
	{
		RenderObjClass	*m_robj;	///<sub-object for tread
		TreadType	m_type;			///<kind of tread
		RenderObjClass::Material_Override m_materialSettings;	///<used to set current uv scroll amount.
	};

	TreadObjectInfo m_treads[MAX_TREAD_SLOTS];
	Int m_treadCount;

	/// road wheels on bones WHEEL_L01..L08 then WHEEL_R01..R08; a slot whose bone the model lacks has m_bone 0
	enum {WHEELS_PER_SIDE=8};
	struct WheelInfo
	{
		Int		m_bone;		///<bone index in the current render object, 0 if absent
		Real	m_radius;	///<from the meshes on the bone
		Real	m_angle;	///<about the axle, kept across a model swap
	};
	WheelInfo m_wheels[2*WHEELS_PER_SIDE];
	Bool m_hasWheels;

	void createEmitters( void );					///< Create particle effects.
	void tossEmitters( void );					///< Create particle effects.

	void startMoveDebris( void );												///< start creating debris from the tank treads
	void stopMoveDebris( void );												///< stop creating debris from the tank treads
	void updateTreadObjects(void);												///< update pointers to sub-objects like treads.
	void updateWheelBones(RenderObjClass *robj);								///< find the road wheel bones and their radii
};

#endif // _W3D_TANK_DRAW_H_

