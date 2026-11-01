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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: FXList.cpp ///////////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, December 2001
// Desc:   FXList descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/FXList.h"

#include "Common/DrawModule.h"
#include "Common/LogicThread.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"

#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/Drawable.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/PartitionManager.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

FXListStore *TheFXListStore = NULL;					///< the FXList store definition

//-------------------------------------------------------------------------------------------------
static void adjustVector(Coord3D *vec, const Matrix3D* mtx)
{
	if (mtx)
	{
		Vector3 vectmp;
		vectmp.X = vec->x;
		vectmp.Y = vec->y;
		vectmp.Z = vec->z;
		vectmp = mtx->Rotate_Vector(vectmp);
		vec->x = vectmp.X;
		vec->y = vectmp.Y;
		vec->z = vectmp.Z;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE CLASSES ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
void FXObjectView::take( const Object *obj )
{
	id = obj->getID();
	position = *obj->getPosition();
	transform = *obj->getTransformMatrix();
	const Player *player = obj->getControllingPlayer();
	playerIndex = player ? player->getPlayerIndex() : -1;
	boundingCircleRadius = obj->getGeometryInfo().getBoundingCircleRadius();
	alwaysClear = obj->isKindOf(KINDOF_ALWAYS_VISIBLE) || obj->friend_getPartitionData() == NULL;
	drawable = obj->getDrawable();
}

//-------------------------------------------------------------------------------------------------
void FXNugget::doFXObj(const FXObjectView* primary, const FXObjectView* secondary) const
{
	const Coord3D* p = primary ? &primary->position : NULL;
	const Matrix3D* mtx = primary ? &primary->transform : NULL;
	const Real speed = 0.0f;	// yes, that's right -- NOT the object's speed.
	const Coord3D* s = secondary ? &secondary->position : NULL;
	doFXPos(p, mtx, speed, s);
}

//-------------------------------------------------------------------------------------------------
class SoundFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(SoundFXNugget, "SoundFXNugget")		
	
public:

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/ ) const
	{
		AudioEventRTS sound(m_soundName);
		
		if (primary) 
		{
			sound.setPosition(primary);
		}

		TheAudio->addAudioEvent(&sound);
	}

	virtual void doFXObj(const FXObjectView* primary, const FXObjectView* secondary = NULL) const
	{
		AudioEventRTS sound(m_soundName);
		if (primary)
		{
			sound.setPlayerIndex(primary->playerIndex);
			sound.setPosition(&primary->position);
		}

		TheAudio->addAudioEvent(&sound);
	}


	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] = 
		{
			{ "Name",									INI::parseAsciiString,	NULL, offsetof( SoundFXNugget, m_soundName ) },
			{ 0, 0, 0, 0 }
		};

		SoundFXNugget* nugget = newInstance(SoundFXNugget);	
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	AsciiString		m_soundName;
};  
EMPTY_DTOR(SoundFXNugget)

//-------------------------------------------------------------------------------------------------
static Real calcDist(const Coord3D& src, const Coord3D& dst)
{
  Real dx = dst.x - src.x;
  Real dy = dst.y - src.y;
  Real dz = dst.z - src.z;
  return sqrt(dx*dx + dy*dy + dz*dz);
}

//-------------------------------------------------------------------------------------------------
class TracerFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TracerFXNugget, "TracerFXNugget")		
public:

	TracerFXNugget()
	{
		m_tracerName.set("GenericTracer");
    m_boneName.clear();
    m_speed = 0.0f; // means "use passed-in speed"
    m_decayAt = 1.0f;
		m_length = 10.0f;
		m_width = 1.0f;
		m_color.red = m_color.green = m_color.blue = 1.0f;
		m_probability = 1.0f;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* primaryMtx, const Real primarySpeed, const Coord3D *secondary, const Real /*overrideRadius*/ ) const
	{
		if (m_probability <= GameClientRandomValueReal(0, 1))
			return;

		if (primary && secondary)
		{
			Drawable *tracer = TheThingFactory->newDrawable(TheThingFactory->findTemplate(m_tracerName));
			if(!tracer)
				return;
			
			//Kris -- Redid this section Sept 18, 2002
			//Calculate tracer orientations to face from primary to secondary position. This
			//should be the direction that the projectile is being fired towards. It doesn't make
			//sense that the old stuff made use of the muzzle fx bone orientation (because it's a 
			//subobject). It had other problems because of elevation variations the tracers would
			//stay on the ground.
			//tracer->setTransformMatrix(primaryMtx);
			Matrix3D tracerMtx;
			Vector3 pos( primary->x, primary->y, primary->z );
			Vector3 dir( secondary->x - primary->x, secondary->y - primary->y, secondary->z - primary->z );
			dir.Normalize(); //This is fantastically crucial for calling buildTransformMatrix!!!!!
			tracerMtx.buildTransformMatrix( pos, dir );
			tracer->setTransformMatrix( &tracerMtx );
			tracer->setPosition(primary);

			Real speed = m_speed;
			if (speed == 0.0f)
			{
				speed = primarySpeed;
			}

			TracerDrawInterface* tdi = NULL;
			for (DrawModule** d = tracer->getDrawModules(); *d; ++d)
			{
				if ((tdi = (*d)->getTracerDrawInterface()) != NULL)
				{
					tdi->setTracerParms(speed, m_length, m_width, m_color, 1.0f);
				}
			}

			// estimate how long it will take us to get to the destination
			Real dist = calcDist(*primary, *secondary) - m_length;
			Real frames = (dist >= 0.0f && speed >= 0.0f) ? (dist / speed) : 1;
			Int framesAdjusted = REAL_TO_INT_CEIL(frames * m_decayAt);
			tracer->setExpirationDate(TheGameLogic->getFrame() + framesAdjusted);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary and secondary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] = 
		{
			{ "TracerName",			INI::parseAsciiString,			NULL, offsetof( TracerFXNugget, m_tracerName ) },
			{ "BoneName",				INI::parseAsciiString,			NULL, offsetof( TracerFXNugget, m_boneName ) },
      { "Speed",          INI::parseVelocityReal,     NULL, offsetof( TracerFXNugget, m_speed ) },
      { "DecayAt",        INI::parseReal,             NULL, offsetof( TracerFXNugget, m_decayAt ) },
      { "Length",					INI::parseReal,             NULL, offsetof( TracerFXNugget, m_length ) },
      { "Width",					INI::parseReal,             NULL, offsetof( TracerFXNugget, m_width ) },
      { "Color",					INI::parseRGBColor,					NULL, offsetof( TracerFXNugget, m_color ) },
      { "Probability",		INI::parseReal,             NULL, offsetof( TracerFXNugget, m_probability ) },
			{ 0, 0, 0, 0 }
		};

		TracerFXNugget* nugget = newInstance( TracerFXNugget );	
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	AsciiString			m_tracerName;
  AsciiString     m_boneName;
  Real            m_speed;
  Real            m_decayAt;
	Real						m_length;
	Real						m_width;
	RGBColor				m_color;
	Real						m_probability;
};  
EMPTY_DTOR(TracerFXNugget)

//-------------------------------------------------------------------------------------------------
class RayEffectFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(RayEffectFXNugget, "RayEffectFXNugget")		
public:

	RayEffectFXNugget()
	{
		m_templateName.clear();
		m_primaryOffset.x = m_primaryOffset.y = m_primaryOffset.z = 0;
		m_secondaryOffset.x = m_secondaryOffset.y = m_secondaryOffset.z = 0;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * secondary, const Real /*overrideRadius*/ ) const
	{
		const ThingTemplate* tmpl = TheThingFactory->findTemplate(m_templateName);
		DEBUG_ASSERTCRASH(tmpl, ("RayEffect %s not found\n",m_templateName.str()));
		if (primary && secondary && tmpl)
		{
			Coord3D sourcePos = *primary;
			sourcePos.x += m_primaryOffset.x;
			sourcePos.y += m_primaryOffset.y;
			sourcePos.z += m_primaryOffset.z;

			Coord3D targetPos = *secondary;
			targetPos.x += m_secondaryOffset.x;
			targetPos.y += m_secondaryOffset.y;
			targetPos.z += m_secondaryOffset.z;

			TheGameClient->createRayEffectByTemplate(&sourcePos, &targetPos, tmpl);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary AND secondary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] = 
		{
			{ "Name",									INI::parseAsciiString,			NULL, offsetof( RayEffectFXNugget, m_templateName ) },
			{ "PrimaryOffset",				INI::parseCoord3D,					NULL, offsetof( RayEffectFXNugget, m_primaryOffset ) },
			{ "SecondaryOffset",			INI::parseCoord3D,					NULL, offsetof( RayEffectFXNugget, m_secondaryOffset ) },
			{ 0, 0, 0, 0 }
		};

		RayEffectFXNugget* nugget = newInstance( RayEffectFXNugget ); 
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	AsciiString			m_templateName;
	Coord3D					m_primaryOffset;
	Coord3D					m_secondaryOffset;
};  
EMPTY_DTOR(RayEffectFXNugget)

//-------------------------------------------------------------------------------------------------
class LightPulseFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(LightPulseFXNugget, "LightPulseFXNugget")		
public:

	LightPulseFXNugget() : m_radius(0), m_increaseFrames(0), m_decreaseFrames(0), m_boundingCirclePct(0)
	{
		m_color.red = m_color.green = m_color.blue = 0;
	}

	virtual void doFXObj(const FXObjectView* primary, const FXObjectView* /*secondary*/) const
	{
		if (primary)
		{
			Real radius = m_radius;

			if (m_boundingCirclePct > 0)
				radius = (primary->boundingCircleRadius * m_boundingCirclePct);

			TheDisplay->createLightPulse(&primary->position, &m_color, 1, radius, m_increaseFrames, m_decreaseFrames);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/ ) const
	{
		if (primary)
		{
			TheDisplay->createLightPulse(primary, &m_color, 1, m_radius, m_increaseFrames, m_decreaseFrames);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] = 
		{
			{ "Color",						INI::parseRGBColor,								NULL, offsetof( LightPulseFXNugget, m_color ) },
			{ "Radius",						INI::parseReal,										NULL, offsetof( LightPulseFXNugget, m_radius ) },
			{ "RadiusAsPercentOfObjectSize",		INI::parsePercentToReal,	NULL, offsetof( LightPulseFXNugget, m_boundingCirclePct ) },
			{ "IncreaseTime",			INI::parseDurationUnsignedInt,	NULL, offsetof( LightPulseFXNugget, m_increaseFrames ) },
			{ "DecreaseTime",			INI::parseDurationUnsignedInt,	NULL, offsetof( LightPulseFXNugget, m_decreaseFrames ) },
			{ 0, 0, 0, 0 }
		};

		LightPulseFXNugget* nugget = newInstance( LightPulseFXNugget );	
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	RGBColor			m_color;
	Real					m_radius;
	Real					m_boundingCirclePct;
	UnsignedInt		m_increaseFrames;
	UnsignedInt		m_decreaseFrames;
};  
EMPTY_DTOR(LightPulseFXNugget)

//-------------------------------------------------------------------------------------------------
class ViewShakeFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(ViewShakeFXNugget, "ViewShakeFXNugget")		
public:

	ViewShakeFXNugget() : m_shake(View::SHAKE_NORMAL)
	{
	}

	virtual Real blastMarkRadius() const
	{
		switch (m_shake)
		{
			case View::SHAKE_SUBTLE:	return 10.0f;
			case View::SHAKE_NORMAL:	return 12.0f;
			case View::SHAKE_STRONG:	return 18.0f;
			default:									return 30.0f;
		}
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/ ) const
	{
		if (primary)
		{
			if (TheTacticalView)
				TheTacticalView->shake(primary, m_shake);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] = 
		{
			{ "Type",				parseShakeType,								NULL, offsetof( ViewShakeFXNugget, m_shake ) },
			{ 0, 0, 0, 0 }
		};

		ViewShakeFXNugget* nugget = newInstance( ViewShakeFXNugget );	
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

protected:
	static void parseShakeType( INI* ini, void *instance, void *store, const void* /*userData*/ )
	{
		static const LookupListRec shakeTypeNames[] = 
		{
			{ "SUBTLE", View::SHAKE_SUBTLE },
			{ "NORMAL", View::SHAKE_NORMAL },
			{ "STRONG", View::SHAKE_STRONG },
			{ "SEVERE", View::SHAKE_SEVERE },
			{ "CINE_EXTREME", View::SHAKE_CINE_EXTREME },
			{ "CINE_INSANE",  View::SHAKE_CINE_INSANE },
			{ 0, 0 }
		};
		*(Int *)store = INI::scanLookupList(ini->getNextToken(), shakeTypeNames);
	}

private:
	View::CameraShakeType m_shake;

};  
EMPTY_DTOR(ViewShakeFXNugget)

//-------------------------------------------------------------------------------------------------
/** The picture drawn in toward the blast and then pushed out by a ring, for a nuke.  A client-only
	* picture: it goes to the tactical view, which hands it to the Direct3D 11 post chain, and does
	* nothing on a device without one. */
//-------------------------------------------------------------------------------------------------
class ScreenDistortionFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(ScreenDistortionFXNugget, "ScreenDistortionFXNugget")
public:

	ScreenDistortionFXNugget()
	{
		m_info.m_radius = 0.0f;
		m_info.m_pullFrames = 0;
		m_info.m_waveFrames = 0;
		m_info.m_pullStrength = 0.0f;
		m_info.m_waveStrength = 0.0f;
		m_info.m_waveWidth = 0.0f;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/ ) const
	{
		if (TheTacticalView)
			TheTacticalView->addScreenDistortion(primary, m_info);
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "Radius",					INI::parseReal,									NULL, offsetof( ScreenDistortionFXNugget, m_info.m_radius ) },
			{ "PullTime",				INI::parseDurationUnsignedInt,	NULL, offsetof( ScreenDistortionFXNugget, m_info.m_pullFrames ) },
			{ "WaveTime",				INI::parseDurationUnsignedInt,	NULL, offsetof( ScreenDistortionFXNugget, m_info.m_waveFrames ) },
			{ "PullStrength",		INI::parsePercentToReal,				NULL, offsetof( ScreenDistortionFXNugget, m_info.m_pullStrength ) },
			{ "WaveStrength",		INI::parsePercentToReal,				NULL, offsetof( ScreenDistortionFXNugget, m_info.m_waveStrength ) },
			{ "WaveWidth",			INI::parsePercentToReal,				NULL, offsetof( ScreenDistortionFXNugget, m_info.m_waveWidth ) },
			{ 0, 0, 0, 0 }
		};

		ScreenDistortionFXNugget* nugget = newInstance( ScreenDistortionFXNugget );
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	ScreenDistortionInfo m_info;
};
EMPTY_DTOR(ScreenDistortionFXNugget)

//-------------------------------------------------------------------------------------------------
class TerrainScorchFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TerrainScorchFXNugget, "TerrainScorchFXNugget")		
public:

	TerrainScorchFXNugget() : m_scorch(-1), m_radius(0)
	{
	}

	virtual Real blastMarkRadius() const { return -max(m_radius, 1.0f); }

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/ ) const
	{
		if (primary)
		{
			Int scorch = m_scorch;
			if (scorch < 0)
			{
				scorch = GameClientRandomValue( SCORCH_1, SCORCH_4 );
			}
			TheGameClient->addScorch(primary, m_radius, (Scorches)scorch);

			// Battle Marks: a nuke, a MOAB or a Daisy Cutter burns a ring round its scorch and cracks the
			// ground under it, out of the same texture, so no new art.
			const Real RING_FROM_RADIUS = 100.0f;
			if (TheGlobalData->m_groundMarks && m_radius >= RING_FROM_RADIUS)
			{
				const Int RING_MARKS = 12;
				for (Int i = 0; i < RING_MARKS; ++i)
				{
					const Real angle = 2.0f * PI * (Real)i / (Real)RING_MARKS;
					Coord3D at = *primary;
					at.x += 1.1f * m_radius * (Real)cos(angle);
					at.y += 1.1f * m_radius * (Real)sin(angle);
					TheGameClient->addGroundMark(&at, 0.28f * m_radius, SCORCH_1 + i % (SCORCH_4 - SCORCH_1 + 1));
				}
				TheGameClient->addGroundMark(primary, 0.7f * m_radius, SCORCH_CRACKS);
			}
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] = 
		{
			{ "Type",				parseScorchType,			NULL, offsetof( TerrainScorchFXNugget, m_scorch ) },
			{ "Radius",			INI::parseReal,				NULL, offsetof( TerrainScorchFXNugget, m_radius ) },
			{ 0, 0, 0, 0 }
		};

		TerrainScorchFXNugget* nugget = newInstance( TerrainScorchFXNugget );	
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

protected:

	static void parseScorchType( INI* ini, void *instance, void *store, const void* /*userData*/ )
	{
		static const LookupListRec scorchTypeNames[] = 
		{
			{ "SCORCH_1",				SCORCH_1 },
			{ "SCORCH_2",				SCORCH_2 },
			{ "SCORCH_3",				SCORCH_3 },
			{ "SCORCH_4",				SCORCH_4 },
			{ "SHADOW_SCORCH",	SHADOW_SCORCH },
			{ "RANDOM",					-1 },
			{ 0, 0 }
		};
		*(Int *)store = INI::scanLookupList(ini->getNextToken(), scorchTypeNames);
	}

private:
	Int		m_scorch;
	Real	m_radius;
};  
EMPTY_DTOR(TerrainScorchFXNugget)

//-------------------------------------------------------------------------------------------------
class ParticleSystemFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(ParticleSystemFXNugget, "ParticleSystemFXNugget")		
public:

	ParticleSystemFXNugget()
	{
		m_name.clear();
		m_count = 1;
		m_radius.setRange(0, 0, GameClientRandomVariable::CONSTANT);
		m_height.setRange(0, 0, GameClientRandomVariable::CONSTANT);
		// -1 means "don't mess with it, just accept the particle-system's defaults"
		m_delay.setRange(-1, -1, GameClientRandomVariable::CONSTANT);
		m_offset.x = m_offset.y = m_offset.z = 0;
		m_orientToObject = false;
		m_attachToObject = false;
		m_createAtGroundHeight = FALSE;
		m_useCallersRadius = FALSE;
		m_atTarget = FALSE;
		m_rotateX = m_rotateY = m_rotateZ = 0;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* primaryMtx, const Real /*primarySpeed*/, const Coord3D *secondary, const Real overrideRadius ) const
	{
		// AtTarget plays the system where the shot lands rather than at the muzzle: a FireFX is
		// played with the victim's position as its secondary, the same point a Tracer flies to,
		// and an instant-hit gun has no projectile to carry a detonation FX there.
		if (m_atTarget)
		{
			DEBUG_ASSERTCRASH(secondary, ("ParticleSystem %s is AtTarget but the FX has no target\n", m_name.str()));
			reallyDoFX(secondary, NULL, INVALID_ID, overrideRadius);
		}
		else if (primary)
		{
			reallyDoFX(primary, primaryMtx, INVALID_ID, overrideRadius);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	virtual void doFXObj(const FXObjectView* primary, const FXObjectView* secondary) const
	{
		if (primary)
		{

			if (m_ricochet && secondary)
			{
				// HERE WE MUST BUILD A MATRIX WHICH WILL ORIENT THE NEW PARTICLE SYSTEM TO FACE AWAY FROM THE SECONDARY OBJECT
				// THE RESULT SHOULD LOOK LIKE THE DIRECTION OF THE "ATTACK" IS CARRIED THROUGH LIKE A RICOCHET
				Real deltaX = primary->position.x - secondary->position.x;
				Real deltaY = primary->position.y - secondary->position.y;
				Real aimingAngle = atan2(deltaY, deltaX);
				Matrix3D aimingMatrix(1);
				aimingMatrix.Rotate_Z( aimingAngle );

				reallyDoFX(&primary->position, &aimingMatrix, primary->id, 0.0f);
			}
			else
				// if we have an object, then adjust the offset and direction by the object's transformation
				// matrix, so that (say) an offset of +10 in the z axis "follows" the orientation of the object.
				reallyDoFX(&primary->position, &primary->transform, primary->id, 0.0f);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] = 
		{
			{ "Name",									INI::parseAsciiString,			NULL, offsetof( ParticleSystemFXNugget, m_name ) },
			{ "Count",								INI::parseInt,							NULL, offsetof( ParticleSystemFXNugget, m_count ) },
			{ "Offset",								INI::parseCoord3D,					NULL, offsetof( ParticleSystemFXNugget, m_offset ) },
			{ "Radius",								INI::parseGameClientRandomVariable,		NULL, offsetof( ParticleSystemFXNugget, m_radius ) },
			{ "Height",								INI::parseGameClientRandomVariable,		NULL, offsetof( ParticleSystemFXNugget, m_height ) },
			{ "InitialDelay",					INI::parseGameClientRandomVariable,		NULL, offsetof( ParticleSystemFXNugget, m_delay ) },
			{ "RotateX",							INI::parseAngleReal,				NULL, offsetof( ParticleSystemFXNugget, m_rotateX ) },
			{ "RotateY",							INI::parseAngleReal,				NULL, offsetof( ParticleSystemFXNugget, m_rotateY ) },
			{ "RotateZ",							INI::parseAngleReal,				NULL, offsetof( ParticleSystemFXNugget, m_rotateZ ) },
			{ "OrientToObject",				INI::parseBool,							NULL, offsetof( ParticleSystemFXNugget, m_orientToObject ) },
			{ "Ricochet",				      INI::parseBool,							NULL, offsetof( ParticleSystemFXNugget, m_ricochet ) },
			{ "AttachToObject",				INI::parseBool,							NULL, offsetof( ParticleSystemFXNugget, m_attachToObject ) },
			{ "CreateAtGroundHeight",	INI::parseBool,							NULL, offsetof( ParticleSystemFXNugget, m_createAtGroundHeight ) },
			{ "UseCallersRadius",			INI::parseBool,							NULL, offsetof( ParticleSystemFXNugget, m_useCallersRadius ) },
			{ "AtTarget",							INI::parseBool,							NULL, offsetof( ParticleSystemFXNugget, m_atTarget ) },
			{ 0, 0, 0, 0 }
		};

		ParticleSystemFXNugget* nugget = newInstance( ParticleSystemFXNugget );	
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

protected:

	void reallyDoFX(const Coord3D *primary, const Matrix3D* mtx, ObjectID thingToAttachTo, Real overrideRadius ) const
	{
		Coord3D offset = m_offset;
		if (mtx)
		{
			adjustVector(&offset, mtx);
		}

		const ParticleSystemTemplate *tmp = TheParticleSystemManager->findTemplate(m_name);
		DEBUG_ASSERTCRASH(tmp, ("ParticleSystem %s not found\n",m_name.str()));
		if (tmp)
		{
			for (Int i = 0; i < m_count; i++ )
			{
				ParticleSystem *sys = TheParticleSystemManager->createParticleSystem(tmp);
				if (sys)
				{
					Coord3D newPos;
					Real radius = m_radius.getValue();
					Real angle = GameClientRandomValueReal( 0.0f, 2.0f * PI );

					newPos.x = primary->x + offset.x + radius * cos(angle);
					newPos.y = primary->y + offset.y + radius * sin(angle);
					newPos.z = primary->z + offset.z;
					if( m_createAtGroundHeight && TheTerrainLogic )
					{
						//old way:
						//newPos.z = TheTerrainLogic->getGrsoundHeight( newPos.x, newPos.y ) + 1;// The plus one prevents scissoring with terrain
						
						//new way: now we allow bridges in the GroundHeight.
						PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(&newPos);
						newPos.z = TheTerrainLogic->getLayerHeight( newPos.x, newPos.y, layer );
					}
					else
						newPos.z += m_height.getValue();


					if (m_orientToObject && mtx)
					{
						sys->setLocalTransform(mtx);
					}
					if (m_rotateX != 0.0f)
						sys->rotateLocalTransformX(m_rotateX);
					if (m_rotateY != 0.0f)
						sys->rotateLocalTransformY(m_rotateY);
					if (m_rotateZ != 0.0f)
						sys->rotateLocalTransformZ(m_rotateZ);

					// by ID: an object the tick destroyed after asking for this is gone now, and the
					// system lets go of it on its first update as it did of a dying one
					if (m_attachToObject && thingToAttachTo != INVALID_ID)
						sys->attachToObjectID(thingToAttachTo);
					else
						sys->setPosition( &newPos );

					Real delayInMsec = m_delay.getValue();
					if (delayInMsec >= 0.0f)
					{
						UnsignedInt delayInFrames = REAL_TO_INT_CEIL(ConvertDurationFromMsecsToFrames(delayInMsec));
						sys->setInitialDelay(delayInFrames);
					}

					if( m_useCallersRadius  &&  overrideRadius )
					{
						ParticleSystemInfo::EmissionVolumeType type = sys->getEmisionVolumeType();

						if( type == ParticleSystemInfo::EmissionVolumeType::SPHERE )
							sys->setEmissionVolumeSphereRadius( overrideRadius );
						else if( type == ParticleSystemInfo::EmissionVolumeType::CYLINDER )
							sys->setEmissionVolumeCylinderRadius( overrideRadius );
					}
				}
			}
		}
	}


private:
	AsciiString			m_name;
	Int							m_count;
	Coord3D					m_offset;
	GameClientRandomVariable	m_radius;
	GameClientRandomVariable	m_height;
	GameClientRandomVariable	m_delay;
	Real						m_rotateX, m_rotateY, m_rotateZ;
	Bool						m_orientToObject;
	Bool						m_attachToObject;
	Bool						m_createAtGroundHeight;
	Bool						m_useCallersRadius;
	Bool						m_atTarget;
	Bool						m_ricochet;
};  
EMPTY_DTOR(ParticleSystemFXNugget)

//-------------------------------------------------------------------------------------------------
class FXListAtBonePosFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(FXListAtBonePosFXNugget, "FXListAtBonePosFXNugget")		
public:

	FXListAtBonePosFXNugget()
	{
		m_fx = NULL;
    m_boneName.clear();
    m_orientToBone = true;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* primaryMtx, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/ ) const
	{
		DEBUG_CRASH(("You must use the object form for this effect"));
	}

	virtual void doFXObj(const FXObjectView* primary, const FXObjectView* /*secondary*/) const
	{
		if (primary)
		{
      // first, try the unadorned name.
			doFxAtBones(primary, 0);

      // then, try the 01,02,03...etc names.
			doFxAtBones(primary, 1);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] = 
		{
			{ "FX",								  	INI::parseFXList,			    NULL, offsetof( FXListAtBonePosFXNugget, m_fx ) },
			{ "BoneName",							INI::parseAsciiString,		NULL, offsetof( FXListAtBonePosFXNugget, m_boneName ) },
			{ "OrientToBone",					INI::parseBool,					  NULL, offsetof( FXListAtBonePosFXNugget, m_orientToBone ) },
			{ 0, 0, 0, 0 }
		};

		FXListAtBonePosFXNugget* nugget = newInstance( FXListAtBonePosFXNugget );	
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

protected:

	void doFxAtBones(const FXObjectView* obj, Int start) const
	{
    Coord3D bonePos[MAX_BONE_POINTS];
    Matrix3D boneMtx[MAX_BONE_POINTS];

    const Drawable* draw = obj->drawable;
		if (draw)
		{
			// yes, BONEPOS_CURRENT_CLIENT_ONLY -- this is client-only, so should be safe to do.
			Int count = draw->getCurrentClientBonePositions(m_boneName.str(), start, bonePos, boneMtx, MAX_BONE_POINTS);
			for (Int i = 0; i < count; ++i)
			{
				// Thing::convertBonePosToWorldPos, on the transform the object had at the call
				Coord3D p;
				Matrix3D m;
				m.mul(obj->transform, boneMtx[i]);
				Vector3 v(bonePos[i].x, bonePos[i].y, bonePos[i].z);
				Matrix3D::Transform_Vector(obj->transform, v, &v);
				p.x = v.X;
				p.y = v.Y;
				p.z = v.Z;
				FXList::doFXPos(m_fx, &p, &m, 0.0f, NULL, 0.0f);
			}
		}
	}

private:

	enum { MAX_BONE_POINTS = 40 };

	const FXList*		m_fx;
  AsciiString     m_boneName;
  Bool            m_orientToBone;
};  
EMPTY_DTOR(FXListAtBonePosFXNugget)

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

static const FieldParse TheFXListFieldParse[] =
{
	{ "Sound",											SoundFXNugget::parse, 0, 0},		
	{ "RayEffect",									RayEffectFXNugget::parse, 0, 0},		
	{ "Tracer",											TracerFXNugget::parse, 0, 0},		
	{ "LightPulse",									LightPulseFXNugget::parse, 0, 0},		
	{ "ViewShake",									ViewShakeFXNugget::parse, 0, 0},
	{ "ScreenDistortion",						ScreenDistortionFXNugget::parse, 0, 0},
	{ "TerrainScorch",							TerrainScorchFXNugget::parse, 0, 0},		
	{ "ParticleSystem",							ParticleSystemFXNugget::parse, 0, 0},		
	{ "FXListAtBonePos",						FXListAtBonePosFXNugget::parse, 0, 0},		
	{ NULL, NULL, 0, 0 }  // keep this last
};

//-------------------------------------------------------------------------------------------------
FXList::FXList() : m_markRadius(0.0f), m_splashRadius(0.0f)
{
}

//-------------------------------------------------------------------------------------------------
FXList::~FXList()
{
	clear();
}

//-------------------------------------------------------------------------------------------------
void FXList::clear()
{
	for (FXNuggetList::iterator it = m_nuggets.begin(); it != m_nuggets.end(); ++it)
	{
		if (*it)
			(*it)->deleteInstance();
	}
	m_nuggets.clear();
	m_markRadius = 0.0f;
	m_splashRadius = 0.0f;
}

//-------------------------------------------------------------------------------------------------
/** Which lists are a blast that marks the ground goes by name, there being nothing else that says
	* so: a muzzle flash shakes the view as hard as a shell landing, and only fifty lists scorch on
	* their own, none of them a tank shell or a missile.  Client only, read once at parse, and no INI
	* line changes.  A list that scorches already keeps that and takes no burn on top.  Water Splashes
	* takes every blast the name says and every list that scorches: the names a burn leaves out still
	* throw water up, and a nuke goes by no blast's name.  How high it lands is asked where it lands. */
//-------------------------------------------------------------------------------------------------
void FXList::noteBlastByName(const char *name)
{
	static const char *const blasts[] = { "Detonation", "Explosion", "Explode", "Impact", "HitsGround", "HitGround", "Blast", "Crash" };
	static const char *const notBurns[] = { "Toxin", "Anthrax", "EMP", "Bio", "Cleanup", "Garrison", "Disguise", "FlashBang", "Patriot", "Stinger", "Spill" };

	Bool isBlast = FALSE;
	for (size_t i = 0; i < ARRAY_SIZE(blasts); ++i)
		isBlast = isBlast || strstr(name, blasts[i]) != NULL;
	Bool burns = isBlast;
	for (size_t i = 0; i < ARRAY_SIZE(notBurns); ++i)
		burns = burns && strstr(name, notBurns[i]) == NULL;

	Real radius = 10.0f;
	Bool scorches = FALSE;
	for (FXNuggetList::const_iterator it = m_nuggets.begin(); it != m_nuggets.end(); ++it)
	{
		const Real asked = (*it)->blastMarkRadius();
		scorches = scorches || asked < 0.0f;
		radius = max(radius, (Real)fabs(asked));
	}
	m_markRadius = burns && !scorches ? radius : 0.0f;
	m_splashRadius = isBlast || scorches ? radius : 0.0f;
}

//-------------------------------------------------------------------------------------------------
void FXList::leaveBlastMark(const Coord3D *at) const
{
	if (m_markRadius <= 0.0f || !TheGlobalData->m_groundMarks)
		return;
	// any higher is a blast in the air or on a roof, and nothing of it lands
	const Real MAX_HEIGHT_OVER_GROUND = 15.0f;
	if (at->z - TheTerrainLogic->getGroundHeight(at->x, at->y) > MAX_HEIGHT_OVER_GROUND)
		return;
	// which burn by where it fell, not the client's random stream: a run with the option on plays
	// every other client-random effect as a run with it off does
	TheGameClient->addGroundMark(at, m_markRadius, SCORCH_1 + ((REAL_TO_INT(at->x) ^ REAL_TO_INT(at->y)) & 3));
}

//-------------------------------------------------------------------------------------------------
void FXList::doFXPos(const Coord3D *primary, const Matrix3D* primaryMtx, const Real primarySpeed, const Coord3D *secondary, const Real overrideRadius ) const
{
	if (!LogicThread::defersEffects())
	{
		doFXPosNow(primary, primaryMtx, primarySpeed, secondary, overrideRadius);
		return;
	}

	// LOGIC-THREAD.md phase 2, M5: after the tick, in order with its other effects, at the points the
	// call named.  Every nugget dereferences the primary, so it is always there.
	const Coord3D p = *primary;
	const Bool hasMtx = primaryMtx != NULL;
	const Matrix3D mtx = hasMtx ? *primaryMtx : Matrix3D(1);
	const Bool hasSecondary = secondary != NULL;
	const Coord3D s = hasSecondary ? *secondary : p;
	LogicThread::callAfterTick( [this, p, hasMtx, mtx, primarySpeed, hasSecondary, s, overrideRadius]() {
		doFXPosNow(&p, hasMtx ? &mtx : NULL, primarySpeed, hasSecondary ? &s : NULL, overrideRadius);
	} );
}

//-------------------------------------------------------------------------------------------------
void FXList::doFXPosNow(const Coord3D *primary, const Matrix3D* primaryMtx, const Real primarySpeed, const Coord3D *secondary, const Real overrideRadius ) const
{
	LOGIC_CENSUS( CENSUS_FXLIST );
	if (ThePartitionManager->getShroudStatusForPlayer(TheObserverCamera.getShroudPlayerIndex(), primary) != CELLSHROUD_CLEAR)
		return;

	for (FXNuggetList::const_iterator it = m_nuggets.begin(); it != m_nuggets.end(); ++it)
	{
		(*it)->doFXPos(primary, primaryMtx, primarySpeed, secondary, overrideRadius);
	}
	leaveBlastMark(primary);
	if (m_splashRadius > 0.0f)
		TheGameClient->splashInWater(primary, m_splashRadius);
}

//-------------------------------------------------------------------------------------------------
void FXList::doFXObj(const Object* primary, const Object* secondary) const
{
	FXObjectView p = {}, s = {};
	if (primary)
		p.take(primary);
	if (secondary)
		s.take(secondary);

	// LOGIC-THREAD.md phase 2, M5: inside a tick the FX plays after it, as the objects stood here
	if (!LogicThread::defersEffects())
	{
		doFXObjNow(primary ? &p : NULL, secondary ? &s : NULL, primary);
		return;
	}
	const Bool hasPrimary = primary != NULL;
	const Bool hasSecondary = secondary != NULL;
	LogicThread::callAfterTick( [this, p, s, hasPrimary, hasSecondary]() {
		doFXObjNow(hasPrimary ? &p : NULL, hasSecondary ? &s : NULL, hasPrimary ? TheGameLogic->findObjectByID(p.id) : NULL);
	} );
}

//-------------------------------------------------------------------------------------------------
void FXList::doFXObjNow(const FXObjectView* primary, const FXObjectView* secondary, const Object* primaryObject) const
{
	LOGIC_CENSUS( CENSUS_FXLIST );
	if (primary)
	{
		// The shroud is asked here and not at the call: the object's answer is a cache that can take a
		// ghost snapshot into the scene.  An object the tick destroyed after asking is gone; the cells
		// under its centre and its edge answer instead, any one clear being the object's partial clear.
		const Int shroudPlayer = TheObserverCamera.getShroudPlayerIndex();
		Bool hidden;
		if (primaryObject)
			hidden = primaryObject->getShroudedStatus(shroudPlayer) > OBJECTSHROUD_PARTIAL_CLEAR;
		else if (primary->alwaysClear)
			hidden = FALSE;
		else
		{
			const Real r = primary->boundingCircleRadius;
			const Coord3D &c = primary->position;
			const Coord3D footprint[] = { c, { c.x + r, c.y, c.z }, { c.x - r, c.y, c.z }, { c.x, c.y + r, c.z }, { c.x, c.y - r, c.z } };
			hidden = TRUE;
			for (size_t i = 0; i < ARRAY_SIZE(footprint) && hidden; ++i)
				hidden = ThePartitionManager->getShroudStatusForPlayer(shroudPlayer, &footprint[i]) != CELLSHROUD_CLEAR;
		}
		// An enemy's stealth this screen has not found plays nothing either, or the sparks, flare and
		// light pulse of a hit mark the empty spot.  Its drawable's look is the screen's answer, the
		// one its attached fire and smoke follow.  A dying one plays, as StealthUpdate shows it.
		if (!hidden && primaryObject && !primaryObject->isEffectivelyDead())
		{
			const Drawable *draw = primaryObject->getDrawable();
			hidden = draw && draw->getStealthLook() == STEALTHLOOK_INVISIBLE;
		}
		if (hidden)
			return;	//the primary object is fogged, shrouded or unseen stealth so don't bother with the effect.
	}

	for (FXNuggetList::const_iterator it = m_nuggets.begin(); it != m_nuggets.end(); ++it)
	{

		// HERE THE PRIMARY IS THE GUY RECEIVING THE FX, AND SECONDARY MIGHT BE THE GUY DEALING IT
		(*it)->doFXObj(primary, secondary);
	}
	if (primary)
	{
		leaveBlastMark(&primary->position);
		if (m_splashRadius > 0.0f)
			TheGameClient->splashInWater(&primary->position, m_splashRadius);
	}
}



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
FXListStore::FXListStore()
{
}

//-------------------------------------------------------------------------------------------------
FXListStore::~FXListStore()
{
	m_fxmap.clear();
}

//-------------------------------------------------------------------------------------------------
const FXList *FXListStore::findFXList(const char* name) const
{
	if (strcasecmp(name, "None") == 0)
		return NULL;

  FXListMap::const_iterator it = m_fxmap.find(NAMEKEY(name));
  if (it != m_fxmap.end()) 
	{
		return &(*it).second;
	}
	// a Superweapon General's copy the fork's data names, which Classic dropped: EA's plain list is what he had
	if (m_classicDroppedGeneralCopies && strncmp(name, "SupW_", 5) == 0)
		return findFXList(name + 5);
	AsciiString retailName;
	return TheModNamespace.findRetailName(AsciiString(name), MODNAME_FXLIST, &retailName) ? findFXList(retailName.str()) : NULL;
}

//-------------------------------------------------------------------------------------------------
/*static */ void FXListStore::parseFXListDefinition(INI *ini)
{
	// read the FXList name
	const char *c = ini->getNextToken();
	const AsciiString name = c;	// the token's buffer goes on to the next line
	NameKeyType key = TheNameKeyGenerator->nameToKey(c);
	if (TheFXListStore->m_classicDropsBlocks)
	{
		// EA's list stands and a name only the fork has plays nothing.  An FXList is the client's
		// business, so a Classic and a Reforged player can share a match over it.
		if (strncmp(name.str(), "SupW_", 5) == 0)
			TheFXListStore->m_classicDroppedGeneralCopies = TRUE;
		else
			TheFXListStore->m_fxmap[key];
		FXList dropped;
		ini->initFromINI(&dropped, TheFXListFieldParse);
		return;
	}
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		FXListStore *store = TheFXListStore;
		if (store->m_fxmap.find(key) == store->m_fxmap.end())
			store->m_addedByMap.push_back(key);
		else if (store->m_beforeMapOverrides.find(key) == store->m_beforeMapOverrides.end()
			&& std::find(store->m_addedByMap.begin(), store->m_addedByMap.end(), key) == store->m_addedByMap.end())
			store->m_beforeMapOverrides[key].swap(store->m_fxmap[key]);	// the game's own list, kept whole
	}
	FXList& fxl = TheFXListStore->m_fxmap[key];
	fxl.clear();
	ini->initFromINI(&fxl, TheFXListFieldParse);
	fxl.noteBlastByName(name.str());
}

//-------------------------------------------------------------------------------------------------
/** A map.ini rewrote FXLists in place and the next match played with them.  The game's own lists
	* go back.  A list the map brought is emptied rather than removed: a weapon whose effect name
	* found nothing at startup was pointed at it when the map loaded (WeaponStore::postProcessLoad),
	* and an empty list draws what the missing one did, nothing. */
//-------------------------------------------------------------------------------------------------
void FXListStore::reset()
{
	for (FXListMap::iterator it = m_beforeMapOverrides.begin(); it != m_beforeMapOverrides.end(); ++it)
	{
		FXList &live = m_fxmap[it->first];
		live.clear();
		live.swap(it->second);
	}
	m_beforeMapOverrides.clear();

	for (size_t i = 0; i < m_addedByMap.size(); ++i)
		m_fxmap[m_addedByMap[i]].clear();
	m_addedByMap.clear();
}

//-------------------------------------------------------------------------------------------------
/*static*/ void INI::parseFXListDefinition(INI *ini)
{
	FXListStore::parseFXListDefinition(ini);
}
