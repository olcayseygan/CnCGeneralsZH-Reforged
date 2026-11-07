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

// FILE: W3DTankDraw.cpp //////////////////////////////////////////////////////////////////////////
// Draw turreted tanks
// Michael S. Booth, October 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>
#include <math.h>
#include "Common/Thing.h"
#include "Common/ThingFactory.h"
#include "Common/GameAudio.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameClient/Drawable.h"
#include "GameClient/ParticleSys.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/GameClient/Module/W3DTankDraw.h"
#include "W3DDevice/GameClient/TankWheelSpin.h"
#include "WW3D2/matinfo.h"
#include "WW3D2/hlod.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

class Matrix3D;

//-------------------------------------------------------------------------------------------------
W3DTankDrawModuleData::W3DTankDrawModuleData() :
	m_treadDebrisNameLeft("TrackDebrisDirtLeft"),
	m_treadDebrisNameRight("TrackDebrisDirtRight"),
	m_treadAnimationRate(0.0f),
	m_treadPivotSpeedFraction(0.6f),
	m_treadDriveSpeedFraction(0.3f)
{
}

//-------------------------------------------------------------------------------------------------
W3DTankDrawModuleData::~W3DTankDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
void W3DTankDrawModuleData::buildFieldParse(MultiIniFieldParse& p) 
{
  W3DModelDrawModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "TreadDebrisLeft", INI::parseAsciiString, NULL, offsetof(W3DTankDrawModuleData, m_treadDebrisNameLeft) },
		{ "TreadDebrisRight", INI::parseAsciiString, NULL, offsetof(W3DTankDrawModuleData, m_treadDebrisNameRight) },
		{ "TreadAnimationRate", INI::parseVelocityReal, NULL, offsetof(W3DTankDrawModuleData, m_treadAnimationRate) },
		{ "TreadPivotSpeedFraction", INI::parseReal, NULL, offsetof(W3DTankDrawModuleData, m_treadPivotSpeedFraction) },
		{ "TreadDriveSpeedFraction", INI::parseReal, NULL, offsetof(W3DTankDrawModuleData, m_treadDriveSpeedFraction) },
		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTankDraw::W3DTankDraw( Thing *thing, const ModuleData* moduleData )
: W3DModelDraw( thing, moduleData ),m_prevRenderObj(NULL), m_treadDebrisLeft(NULL), m_treadDebrisRight(NULL)
{
	m_treadDebrisLeft = NULL;
	m_treadDebrisRight = NULL;

	for (Int i=0; i<MAX_TREAD_SLOTS; i++)
		m_treads[i].m_robj = NULL;

	m_treadCount=0;

	for (Int i=0; i<2*WHEELS_PER_SIDE; i++)
	{
		m_wheels[i].m_bone = 0;
		m_wheels[i].m_radius = 0.0f;
		m_wheels[i].m_angle = 0.0f;
	}
	m_hasWheels = FALSE;
}

//-------------------------------------------------------------------------------------------------
void W3DTankDraw::buildRenderObjects( Bool madeInTick )
{
	W3DModelDraw::buildRenderObjects( madeInTick );
	createEmitters();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTankDraw::tossEmitters( void )
{
	if (m_treadDebrisLeft)
	{
		m_treadDebrisLeft->attachToObject(NULL);
		m_treadDebrisLeft->destroy();
		m_treadDebrisLeft = NULL;
	}
	if (m_treadDebrisRight)
	{
		m_treadDebrisRight->attachToObject(NULL);
		m_treadDebrisRight->destroy();
		m_treadDebrisRight = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DTankDraw::createEmitters( void )
{
	if (!m_treadDebrisLeft)
	{
		const ParticleSystemTemplate *sysTemplate;
		sysTemplate = TheParticleSystemManager->findTemplate(getW3DTankDrawModuleData()->m_treadDebrisNameLeft);
		if (sysTemplate)
		{
			m_treadDebrisLeft = TheParticleSystemManager->createParticleSystem( sysTemplate );
			m_treadDebrisLeft->attachToDrawable(getDrawable());
			// important: mark it as do-not-save, since we'll just re-create it when we reload.
			m_treadDebrisLeft->setSaveable(FALSE);
			// they come into being stopped.
	  	m_treadDebrisLeft->stop();
		}
	}
	if (!m_treadDebrisRight)
	{
		const ParticleSystemTemplate *sysTemplate;
		sysTemplate = TheParticleSystemManager->findTemplate(getW3DTankDrawModuleData()->m_treadDebrisNameRight);
		if (sysTemplate)
		{
			m_treadDebrisRight = TheParticleSystemManager->createParticleSystem( sysTemplate );
			m_treadDebrisRight->attachToDrawable(getDrawable());
			// important: mark it as do-not-save, since we'll just re-create it when we reload.
			m_treadDebrisRight->setSaveable(FALSE);
			// they come into being stopped.
	  	m_treadDebrisRight->stop();
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DTankDraw::~W3DTankDraw()
{
	for (Int i=0; i<MAX_TREAD_SLOTS; i++)
		if (m_treads[i].m_robj)
			REF_PTR_RELEASE(m_treads[i].m_robj);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**

 * Start creating debris from the tank treads
 */
void W3DTankDraw::startMoveDebris( void )
{
	if (getDrawable()->isDrawableEffectivelyHidden())
		return;
	if (m_treadDebrisLeft)
    m_treadDebrisLeft->start();
	if (m_treadDebrisRight)
    m_treadDebrisRight->start();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
 * Stop creating debris from the tank treads
 */
void W3DTankDraw::stopMoveDebris( void )
{
  if (m_treadDebrisLeft)
  	m_treadDebrisLeft->stop();
  if (m_treadDebrisRight)
  	m_treadDebrisRight->stop();
}

//-------------------------------------------------------------------------------------------------
void W3DTankDraw::setHidden(Bool h)
{
	W3DModelDraw::setHidden(h);
	if (h)
	{
		stopMoveDebris();
	}
}

//-------------------------------------------------------------------------------------------------
void W3DTankDraw::setFullyObscuredByShroud(Bool fullyObscured)
{
	if (fullyObscured != getFullyObscuredByShroud())
	{
		if (fullyObscured)
			stopMoveDebris();
	}
	W3DModelDraw::setFullyObscuredByShroud(fullyObscured);
}

/**Grab pointers to the sub-meshes for each tread*/ 
void W3DTankDraw::updateTreadObjects(void)
{
	RenderObjClass *robj=getRenderObject();

	//clear all previous tread pointers
	for (Int i=0; i<m_treadCount; i++)
		REF_PTR_RELEASE(m_treads[i].m_robj);
	m_treadCount = 0;

	//Make sure this object has defined a speed for tread scrolling.
	if (getW3DTankDrawModuleData() && getW3DTankDrawModuleData()->m_treadAnimationRate && robj)
	{
		// four belts a level, so a model with levels of detail scrolls the full model's belts as well
		// as the coarse ones the walk meets first; a model of one level, every retail one, keeps four
		Int levels = robj->Class_ID() == RenderObjClass::CLASSID_HLOD ? ((HLodClass *)robj)->Get_LOD_Count() : 1;
		levels = levels < 1 ? 1 : (levels > MAX_TREAD_LODS ? MAX_TREAD_LODS : levels);
		const Int slots = MAX_TREADS_PER_TANK * levels;
		for (Int i=0; i < robj->Get_Num_Sub_Objects() && m_treadCount < slots; i++)
		{
			RenderObjClass *subObj=robj->Get_Sub_Object(i);
			const char *meshName;
			//Check if subobject name starts with "TREADS".
			if (subObj && subObj->Class_ID() == RenderObjClass::CLASSID_MESH && subObj->Get_Name()
				&& ( (meshName=strchr(subObj->Get_Name(),'.') ) != 0 && *(meshName++))
				&&strncasecmp(meshName,"TREADS", 6) == 0)
			{	//check if sub-object has the correct material to do texture scrolling.
				MaterialInfoClass *mat=subObj->Get_Material_Info();
				if (mat)
				{	for (Int j=0; j<mat->Vertex_Material_Count() && m_treadCount < slots; j++)
					{
						VertexMaterialClass *vmaterial=mat->Peek_Vertex_Material(j);
						LinearOffsetTextureMapperClass *mapper=(LinearOffsetTextureMapperClass *)vmaterial->Peek_Mapper();
						if (mapper && mapper->Mapper_ID() == TextureMapperClass::MAPPER_ID_LINEAR_OFFSET)
						{	mapper->Set_UV_Offset_Delta(Vector2(0,0));	//disable automatic scrolling
							subObj->Add_Ref();	//increase reference since we're storing the pointer
							m_treads[m_treadCount].m_robj=subObj;
							m_treads[m_treadCount].m_type = TREAD_MIDDLE;	//default type
							subObj->Set_User_Data(&m_treads[m_treadCount].m_materialSettings);	//tell W3D about custom material settings
							m_treads[m_treadCount].m_materialSettings.customUVOffset=Vector2(0,0);
							switch (meshName[6])	//check next character after 'TREADS'
							{
								case 'L':
								case 'l':	m_treads[m_treadCount].m_type = TREAD_LEFT;
										break;
								case 'R':
								case 'r':	m_treads[m_treadCount].m_type = TREAD_RIGHT;
										break;
							}
							m_treadCount++;
						}
					}
					REF_PTR_RELEASE(mat);
				}
			}
			REF_PTR_RELEASE(subObj);
		}
	}

	updateWheelBones(robj);
	m_prevRenderObj = robj;
}

//-------------------------------------------------------------------------------------------------
/** Road wheels on bones WHEEL_L01..L08 (the hull's +Y side) and WHEEL_R01..R08 (-Y), any of them,
	* each bone at its wheel's hub with local +Y toward the hull's +Y on both sides, so a positive
	* Rotate_Y rolls every wheel forward, as on a truck's tyres. A wheel's
	* radius is the larger of its meshes' X and Z half extents, every LOD's mesh on that bone counted,
	* so a sprocket and a road wheel each turn at their own rate. No retail model has these bones. */
//-------------------------------------------------------------------------------------------------
void W3DTankDraw::updateWheelBones(RenderObjClass *robj)
{
	m_hasWheels = FALSE;
	for (Int i=0; i<2*WHEELS_PER_SIDE; i++)
	{
		WheelInfo &wheel = m_wheels[i];
		wheel.m_bone = 0;
		if (robj == NULL)
			continue;
		char name[16];
		sprintf(name, "WHEEL_%c%02d", i < WHEELS_PER_SIDE ? 'L' : 'R', i % WHEELS_PER_SIDE + 1);
		Int bone = robj->Get_Bone_Index(name);
		if (bone <= 0)
			continue;
		Real radius = 0.0f;
		Int count = robj->Get_Num_Sub_Objects_On_Bone(bone);
		for (Int j=0; j<count; j++)
		{
			RenderObjClass *mesh = robj->Get_Sub_Object_On_Bone(j, bone);
			if (mesh)
			{
				AABoxClass box;
				mesh->Get_Obj_Space_Bounding_Box(box);
				radius = WWMath::Max(radius, WWMath::Max(box.Extent.X, box.Extent.Z));
				REF_PTR_RELEASE(mesh);
			}
		}
		if (radius > 0.0f)
		{
			wheel.m_bone = bone;
			wheel.m_radius = radius;
			m_hasWheels = TRUE;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DTankDraw::onRenderObjRecreated(void)
{
	updateTreadObjects();
}

//-------------------------------------------------------------------------------------------------
/** Map behavior states into W3D animations. */
//-------------------------------------------------------------------------------------------------
void W3DTankDraw::doDrawModule(const Matrix3D* transformMtx)
{
	const Real DEBRIS_THRESHOLD = ConvertRetailPerFrameSquared(0.00001f);

 	Bool frozen = TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished();
 	frozen = frozen || TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript();
	if (frozen)
		return;
	if (getRenderObject()==NULL) return;
	if (getRenderObject() != m_prevRenderObj) {
		updateTreadObjects();
	}

	// get object from logic
	Object *obj = getDrawable()->getObject();
	if (obj == NULL)
		return;

	// get object physics state
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == NULL)
		return;

	const Coord3D *vel = physics->getVelocity();

	// if tank is moving, kick up dust and debris
	Real velMag = vel->x*vel->x + vel->y*vel->y;		// only care about moving on the ground

	if (velMag > DEBRIS_THRESHOLD && !getDrawable()->isDrawableEffectivelyHidden() && !getFullyObscuredByShroud())
		startMoveDebris();
	else
		stopMoveDebris();

	// kick debris higher the faster we move (the multipliers were written for distance per 30 Hz frame)
	Coord3D velMult;
	velMag = (Real)sqrt( velMag ) * LOGICFRAMES_PER_RETAIL_FRAME;

	velMult.x = 0.5f * velMag + 0.1f;
	if (velMult.x > 1.0f)
		velMult.x = 1.0f;

	velMult.y = velMult.x;

	velMult.z = velMag + 0.1f;
	if (velMult.z > 1.0f)
		velMult.z = 1.0f;

	m_treadDebrisLeft->setVelocityMultiplier( &velMult );
	m_treadDebrisRight->setVelocityMultiplier( &velMult );

	m_treadDebrisLeft->setBurstCountMultiplier( velMult.z );
	m_treadDebrisRight->setBurstCountMultiplier( velMult.z );

	/* EA scrolled the treads by TreadAnimationRate every drawn frame once the tank passed a share of
		 its top speed, which at 120 pictures a second ran them four times too fast, never backwards, and
		 both at once in a turn. Each tread now runs over exactly the ground it covered: the distance
		 driven, less or plus the turn times half the track width, at TreadAnimationRate per top speed's
		 worth of distance. A pivot runs them against each other, a reverse runs them backwards. */
	Real forward, turn;
	stepGroundMotion(forward, turn);
	// undamaged top speed: a damaged tank is slower, and its treads still have to match the ground.
	// A thing with no AI has no locomotor and its treads stand still. No retail object pairs this
	// module with no AI, but data that draws a hulk with it does, and EA only asked for the AI when
	// the model had treads; v2.4.0 asked every frame and crashed (crash report 134).
	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	const Locomotor *loco = ai ? ai->getCurLocomotor() : NULL;
	Real maxSpeed = loco ? loco->getMaxSpeedForCondition(BODY_PRISTINE) : 0.0f;
	if (m_treadCount && maxSpeed > 0.0f)
	{
		Real uvPerDistance = getW3DTankDrawModuleData()->m_treadAnimationRate / maxSpeed;
		Real sideways = turn * obj->getGeometryInfo().getMinorRadius();
		TreadObjectInfo *pTread=m_treads;
		for (Int i=0; i<m_treadCount; i++, pTread++)
		{
			Real distance = forward;
			if (pTread->m_type == TREAD_LEFT)
				distance -= sideways;
			else if (pTread->m_type == TREAD_RIGHT)
				distance += sideways;
			Real offset_u = pTread->m_materialSettings.customUVOffset.X - uvPerDistance * distance;
			// ensure coordinates of offset are in [0, 1] range:
			offset_u = offset_u - WWMath::Floor(offset_u);
			pTread->m_materialSettings.customUVOffset.Set(offset_u,0);
		}
	}

	// road wheels roll over the ground their side's tread covered, at their own radius
	if (m_hasWheels)
	{
		RenderObjClass *robj = getRenderObject();
		Real halfTrack = obj->getGeometryInfo().getMinorRadius();
		Matrix3D wheelXfrm(1);
		for (Int i=0; i<2*WHEELS_PER_SIDE; i++)
		{
			WheelInfo &wheel = m_wheels[i];
			if (wheel.m_bone == 0)
				continue;
			wheel.m_angle = TankWheelAngle(wheel.m_angle, forward, turn, halfTrack, i < WHEELS_PER_SIDE, wheel.m_radius);
			wheelXfrm.Make_Identity();
			wheelXfrm.Rotate_Y(wheel.m_angle);
			robj->Capture_Bone(wheel.m_bone);
			robj->Control_Bone(wheel.m_bone, wheelXfrm);
		}
	}

	W3DModelDraw::doDrawModule(transformMtx);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DTankDraw::crc( Xfer *xfer )
{

	// extend base class
	W3DModelDraw::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DTankDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	W3DModelDraw::xfer( xfer );

	// John A and Mark W say there is no data to save here

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DTankDraw::loadPostProcess( void )
{

	// extend base class
	W3DModelDraw::loadPostProcess();

	// toss any existing ones and re-create 'em (since this module expects 'em to always be around)
	tossEmitters();
	createEmitters();

}  // end loadPostProcess
