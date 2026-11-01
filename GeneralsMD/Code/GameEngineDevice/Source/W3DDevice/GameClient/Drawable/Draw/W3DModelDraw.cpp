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

// FILE: W3DModelDraw.cpp ///////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:   Default w3d draw module
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////

#define DEFINE_W3DANIMMODE_NAMES
#define DEFINE_WEAPONSLOTTYPE_NAMES

#define NO_DEBUG_CRC

#include "Common/crc.h"
#include "Common/CRCDebug.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/LatchRestore.h"
#include "Common/PerfTimer.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/GameLOD.h"
#include "Common/Xfer.h"
#include "Common/GameState.h"
#include "Common/QuickTrig.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/Shadow.h"
#include "GameLogic/GameLogic.h"		// for real-time frame
#include "GameLogic/GhostObject.h"
#include "GameLogic/PartitionManager.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/Object.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "WW3D2/hanim.h"
#include "WW3D2/hlod.h"
#include "WW3D2/htree.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/dx11runtime.h"
#include "WW3D2/ffshader.h"
#include "Common/BitFlagsIO.h"

#include <algorithm>

/// Models whose headlights were showing the last time the drawables were drawn.  A render pass
/// draws them only when the logic clock has moved (W3DView::update), which with the frame rate
/// uncapped is a few passes in ten, so the list is kept between draws and only a pass that drew
/// something takes off what it did not draw (W3DModelDraw::lightHeadlights).
static std::vector<W3DModelDraw*> s_headlightQueue;
static UnsignedInt s_headlightPass = 1;
static Bool s_headlightDrawn = FALSE;

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------
/** W3DModelDraw::buildRenderObjects of a drawable logic made in a tick, run after it. */
static Bool s_fillingLogicCaches = FALSE;

//-------------------------------------------------------------------------------------------------
static inline Bool isValidTimeToCalcLogicStuff()
{
	return (TheGameLogic && TheGameLogic->isInGameLogicUpdate()) ||
		(TheGameState && TheGameState->isInLoadGame()) || s_fillingLogicCaches;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#if defined(DEBUG_CRC) && (defined(_DEBUG) || defined(_INTERNAL))
#include <cstdarg>
class LogClass
{
public:
	LogClass(const char *fname);
	~LogClass();
	void log(const char *fmt, ...);
	void dumpMatrix3D(const Matrix3D *m, AsciiString name, AsciiString fname, Int line);
	void dumpReal(Real r, AsciiString name, AsciiString fname, Int line);

protected:
	FILE *m_fp;
};

LogClass::LogClass(const char *fname)
{
	char buffer[ _MAX_PATH ];
	GetModuleFileName( NULL, buffer, sizeof( buffer ) );
	char *pEnd = buffer + strlen( buffer );
	while( pEnd != buffer )
	{
		if( *pEnd == '\\' )
		{
			*pEnd = 0;
			break;
		}
		pEnd--;
	}
	AsciiString fullPath;
	fullPath.format("%s\\%s", buffer, fname);
	m_fp = fopen(fullPath.str(), "wt");
}

LogClass::~LogClass()
{
	if (m_fp)
	{
		fclose(m_fp);
	}
}

void LogClass::log(const char *fmt, ...)
{
	DEBUG_ASSERTCRASH(isValidTimeToCalcLogicStuff(), ("Calc'ing logic bone pos in client!!!"));
	if (!m_fp /*|| !isValidTimeToCalcLogicStuff()*/)
		return;
	static char buf[1024];
	static Int lastFrame = 0;
	static Int lastIndex = 0;
	if (lastFrame != TheGameLogic->getFrame())
	{
		lastFrame = TheGameLogic->getFrame();
		lastIndex = 0;
	}

	va_list va;
	va_start( va, fmt );
	vsnprintf(buf, 1024, fmt, va );
	buf[1023] = 0;
	va_end( va );

	char *tmp = buf;
	while (tmp && *tmp)
	{
		if (*tmp == '\r' || *tmp == '\n')
		{
			*tmp = ' ';
		}
		++tmp;
	}

	fprintf(m_fp, "%d:%d %s\n", lastFrame, lastIndex++, buf);
	fflush(m_fp);
}

void LogClass::dumpMatrix3D(const Matrix3D *m, AsciiString name, AsciiString fname, Int line)
{
	fname.toLower();
	fname = fname.reverseFind('\\') + 1;
	const Real *matrix = (const Real *)m;
	log("dumpMatrix3D() %s:%d %s\n",
		fname.str(), line, name.str());
	for (Int i=0; i<3; ++i)
		log("      0x%08X 0x%08X 0x%08X 0x%08X\n",
			AS_INT(matrix[(i<<2)+0]), AS_INT(matrix[(i<<2)+1]), AS_INT(matrix[(i<<2)+2]), AS_INT(matrix[(i<<2)+3]));
}

void LogClass::dumpReal(Real r, AsciiString name, AsciiString fname, Int line)
{
	if (!m_fp || !isValidTimeToCalcLogicStuff())
		return;
	fname.toLower();
	fname = fname.reverseFind('\\') + 1;
	log("dumpReal() %s:%d %s %8.8X (%f)\n",
		fname.str(), line, name.str(), AS_INT(r), r);
}

LogClass BonePosLog("bonePositions.txt");

#define BONEPOS_LOG(x) BonePosLog.log x
#define BONEPOS_DUMPMATRIX3D(x) BONEPOS_DUMPMATRIX3DNAMED(x, #x)
#define BONEPOS_DUMPMATRIX3DNAMED(x, y) BonePosLog.dumpMatrix3D(x, y, __FILE__, __LINE__)
#define BONEPOS_DUMPREAL(x) BONEPOS_DUMPREALNAMED(x, #x)
#define BONEPOS_DUMPREALNAMED(x, y) BonePosLog.dumpReal(x, y, __FILE__, __LINE__)

#else // DEBUG_CRC

#define BONEPOS_LOG(x) {}
#define BONEPOS_DUMPMATRIX3D(x) {}
#define BONEPOS_DUMPMATRIX3DNAMED(x, y) {}
#define BONEPOS_DUMPREAL(x) {}
#define BONEPOS_DUMPREALNAMED(x, y) {}

#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

#if defined(_DEBUG) || defined(_INTERNAL)
extern AsciiString TheThingTemplateBeingParsedName;
extern Real TheSkateDistOverride;
#endif

// flags that aren't read directly from INI, but set in response to various other situations
enum INIReadFlagsType
{
	ANIMS_COPIED_FROM_DEFAULT_STATE = 0,
	GOT_NONIDLE_ANIMS,
	GOT_IDLE_ANIMS,
};

enum ACBits
{
	RANDOMIZE_START_FRAME = 0,
	START_FRAME_FIRST,
	START_FRAME_LAST,
	ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT,
	PRISTINE_BONE_POS_IN_FINAL_FRAME,
	MAINTAIN_FRAME_ACROSS_STATES,
	RESTART_ANIM_WHEN_COMPLETE,
	MAINTAIN_FRAME_ACROSS_STATES2,
	MAINTAIN_FRAME_ACROSS_STATES3,
	MAINTAIN_FRAME_ACROSS_STATES4,
};

static const char *ACBitsNames[] =
{
	"RANDOMSTART",
	"START_FRAME_FIRST",
	"START_FRAME_LAST",
	"ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT",
	"PRISTINE_BONE_POS_IN_FINAL_FRAME",
	"MAINTAIN_FRAME_ACROSS_STATES",
	"RESTART_ANIM_WHEN_COMPLETE",
	"MAINTAIN_FRAME_ACROSS_STATES2",
	"MAINTAIN_FRAME_ACROSS_STATES3",
	"MAINTAIN_FRAME_ACROSS_STATES4",
	
	NULL
};

static const Int ALL_MAINTAIN_FRAME_FLAGS =
	(1<<MAINTAIN_FRAME_ACROSS_STATES) |
	(1<<MAINTAIN_FRAME_ACROSS_STATES2) |
	(1<<MAINTAIN_FRAME_ACROSS_STATES3) |
	(1<<MAINTAIN_FRAME_ACROSS_STATES4);

inline Bool isAnyMaintainFrameFlagSet(Int flags)
{
	return (flags & ALL_MAINTAIN_FRAME_FLAGS) != 0;
}

inline Bool isCommonMaintainFrameFlagSet(Int a, Int b)
{
	a &= ALL_MAINTAIN_FRAME_FLAGS;
	b &= ALL_MAINTAIN_FRAME_FLAGS;
	return (a & b) != 0;
}


/** @todo: Move this to some kind of INI file*/
//
// Note: these values are saved in save files, so you MUST NOT REMOVE OR CHANGE
// existing values!
//
static char *TerrainDecalTextureName[TERRAIN_DECAL_MAX]=
{
#ifdef ALLOW_DEMORALIZE
	"DM_RING",//demoralized
#else
	"TERRAIN_DECAL_DEMORALIZED_OBSOLETE",
#endif
	"EXHorde",//enthusiastic
	"EXHorde_UP", //enthusiastic with nationalism 
	"EXHordeB",//enthusiastic vehicle
	"EXHordeB_UP", //enthusiastic vehicle with nationalism
	"EXJunkCrate",//Marks a crate as special
	"EXHordeC_UP", //enthusiastic with fanaticism 
	"EXChemSuit", //Marks a unit as having chemical suit on
	"",	//dummy entry for TERRAIN_DECAL_NONE
	"" //dummy entry for TERRAIN_DECAL_SHADOW_TEXTURE
};

const UnsignedInt NO_NEXT_DURATION = 0xffffffff;

//-------------------------------------------------------------------------------------------------
W3DAnimationInfo::W3DAnimationInfo(const AsciiString& name, Bool isIdle, Real distanceCovered) : 
#ifdef RETAIN_ANIM_HANDLES
	m_handle(NULL), 
	m_naturalDurationInMsec(0),
#else
	m_naturalDurationInMsec(-1),
#endif
	m_name(name),
	m_isIdleAnim(isIdle), 
	m_distanceCovered(distanceCovered)
{ 
}

//-------------------------------------------------------------------------------------------------
W3DAnimationInfo::W3DAnimationInfo( const W3DAnimationInfo &r ) : 
	m_name(r.m_name),
#ifdef RETAIN_ANIM_HANDLES
	m_handle(r.m_handle),
#endif
	m_distanceCovered(r.m_distanceCovered),
	m_isIdleAnim(r.m_isIdleAnim),
	m_naturalDurationInMsec(r.m_naturalDurationInMsec)
{
#ifdef RETAIN_ANIM_HANDLES
	if (m_handle)
		m_handle->Add_Ref();
#endif
}

//-------------------------------------------------------------------------------------------------
W3DAnimationInfo& W3DAnimationInfo::operator=(const W3DAnimationInfo &r)
{
	m_name = r.m_name;
	m_distanceCovered = r.m_distanceCovered;
	m_naturalDurationInMsec = r.m_naturalDurationInMsec;
	m_isIdleAnim = r.m_isIdleAnim;

#ifdef RETAIN_ANIM_HANDLES
	REF_PTR_RELEASE(m_handle);
	m_handle = r.m_handle;
	if (m_handle)
		m_handle->Add_Ref();
#endif

	return (*this);
}


//-------------------------------------------------------------------------------------------------
// note that this now returns an ADDREFED handle, which must be released by the caller!
HAnimClass* W3DAnimationInfo::getAnimHandle() const
{ 
#ifdef RETAIN_ANIM_HANDLES
	if (m_handle == NULL)
	{
		LOGIC_CENSUS( CENSUS_ASSET_LOAD );
		// Get_HAnim addrefs it, so we'll have to release it in our dtor.
		m_handle = W3DDisplay::m_assetManager->Get_HAnim(m_name.str());
		DEBUG_ASSERTCRASH(m_handle, ("*** ASSET ERROR: animation %s not found\n",m_name.str()));
		if (m_handle)
		{
			m_naturalDurationInMsec = m_handle->Get_Num_Frames() * 1000.0f / m_handle->Get_Frame_Rate();
		}
	}
	// since we have it locally, must addref.
	if (m_handle)
		m_handle->Add_Ref();
	return m_handle;
#else
	LogicThread::parkRender();
	LOGIC_CENSUS( CENSUS_ASSET_LOAD );
	HAnimClass* handle = W3DDisplay::m_assetManager->Get_HAnim(m_name.str());
	DEBUG_ASSERTCRASH(handle, ("*** ASSET ERROR: animation %s not found\n",m_name.str()));
	if (handle != NULL && m_naturalDurationInMsec < 0)
	{
		m_naturalDurationInMsec = handle->Get_Num_Frames() * 1000.0f / handle->Get_Frame_Rate();
	}
	// since Get_HAnim() returns an addrefed handle, we must NOT addref here.
	//if (handle)
	//	handle->Add_Ref();
	return handle;
#endif
}

//-------------------------------------------------------------------------------------------------
W3DAnimationInfo::~W3DAnimationInfo() 
{ 
#ifdef RETAIN_ANIM_HANDLES
	REF_PTR_RELEASE(m_handle); 
	m_handle = NULL; 
#endif
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::preloadAssets( TimeOfDay timeOfDay, Real scale )
{
	// load this asset
	if( m_modelName.isEmpty() == FALSE )
	{
		TheDisplay->preloadModelAssets( m_modelName );
	}
	
	// this can be called from the client, which is problematic
//	validateStuff(NULL, getDrawable()->getScale());
	//validateCachedBones(NULL, scale);
	//validateTurretInfo();
	//validateWeaponBarrelInfo();
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::addPublicBone(const AsciiString& boneName) const
{
	if (boneName.isEmpty() || boneName.isNone())
		return;

	AsciiString tmp = boneName;
	tmp.toLower();
	if (std::find(m_publicBones.begin(), m_publicBones.end(), tmp) == m_publicBones.end())
	{
		m_publicBones.push_back(tmp);
	}
}

//-------------------------------------------------------------------------------------------------
Bool ModelConditionInfo::matchesMode(Bool night, Bool snowy) const
{
	for (std::vector<ModelConditionFlags>::const_iterator it = m_conditionsYesVec.begin(); 
				it != m_conditionsYesVec.end(); 
				++it)
	{
		if (it->test(MODELCONDITION_NIGHT) == (night) &&
				it->test(MODELCONDITION_SNOW) == (snowy))
		{
			return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
inline Bool testFlagBit(Int flags, Int bit)
{
	return (flags & (1<<bit)) != 0;
}

//-------------------------------------------------------------------------------------------------
static Bool findSingleBone(RenderObjClass* robj, const AsciiString& boneName, Matrix3D& mtx, Int& boneIndex)
{
	if (boneName.isNone() || boneName.isEmpty())
		return false;

	boneIndex = robj->Get_Bone_Index(boneName.str());
	if (boneIndex != 0)
	{
		mtx = robj->Get_Bone_Transform(boneIndex);
		return true;
	}
	else
	{
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
static Bool findSingleSubObj(RenderObjClass* robj, const AsciiString& boneName, Matrix3D& mtx, Int& boneIndex)
{
	if (boneName.isNone() || boneName.isEmpty())
		return false;

	RenderObjClass* childObject = robj->Get_Sub_Object_By_Name(boneName.str());
	if (childObject)
	{
		mtx = childObject->Get_Transform();

		// you'd think this would work, but, it does not. do it the hard way.
		// boneIndex = childObject->Get_Sub_Object_Bone_Index(childObject);

		for (Int subObj = 0; subObj < robj->Get_Num_Sub_Objects(); subObj++)
		{	
			RenderObjClass* test = robj->Get_Sub_Object(subObj);
			if (test == childObject)
			{
				boneIndex = robj->Get_Sub_Object_Bone_Index(0, subObj);
#if defined(_DEBUG) || defined(_INTERNAL)
				test->Release_Ref();
				test = robj->Get_Sub_Object_On_Bone(0, boneIndex);
				DEBUG_ASSERTCRASH(test != NULL && test == childObject, ("*** ASSET ERROR: Hmm, bone problem"));
#endif
			}
			if (test) test->Release_Ref();
		}

		childObject->Release_Ref();

		return true;
	}
	else
	{
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
static Bool doSingleBoneName(RenderObjClass* robj, const AsciiString& boneName, PristineBoneInfoMap& map)
{
	Bool foundAsBone = false;
	Bool foundAsSubObj = false;

	PristineBoneInfo info;
	AsciiString tmp;

	AsciiString boneNameTmp = boneName;
	boneNameTmp.toLower();	// convert to all-lowercase to avoid case sens issues later

	setFPMode();

	if (findSingleBone(robj, boneNameTmp, info.mtx, info.boneIndex))
	{
//DEBUG_LOG(("added bone %s\n",boneNameTmp.str()));
		BONEPOS_LOG(("Caching bone %s (index %d)\n", boneNameTmp.str(), info.boneIndex));
		BONEPOS_DUMPMATRIX3D(&(info.mtx));

		map[NAMEKEY(boneNameTmp)] = info;
		foundAsBone = true;
	}

	for (Int i = 1; i <= 99; ++i)
	{
		tmp.format("%s%02d", boneNameTmp.str(), i);
		if (findSingleBone(robj, tmp, info.mtx, info.boneIndex))
		{
//DEBUG_LOG(("added bone %s\n",tmp.str()));
			BONEPOS_LOG(("Caching bone %s (index %d)\n", tmp.str(), info.boneIndex));
			BONEPOS_DUMPMATRIX3D(&(info.mtx));
			map[NAMEKEY(tmp)] = info;
			foundAsBone = true;
		}
		else
		{
			break;
		}
	}

	if (!foundAsBone)
	{
		if (findSingleSubObj(robj, boneNameTmp, info.mtx, info.boneIndex))
		{
//DEBUG_LOG(("added subobj %s\n",boneNameTmp.str()));
			BONEPOS_LOG(("Caching bone from subobject %s (index %d)\n", boneNameTmp.str(), info.boneIndex));
			BONEPOS_DUMPMATRIX3D(&(info.mtx));
			map[NAMEKEY(boneNameTmp)] = info;
			foundAsSubObj = true;
		}

		for (Int i = 1; i <= 99; ++i)
		{
			tmp.format("%s%02d", boneNameTmp.str(), i);
			if (findSingleSubObj(robj, tmp, info.mtx, info.boneIndex))
			{
//DEBUG_LOG(("added subobj %s\n",tmp.str()));
				BONEPOS_LOG(("Caching bone from subobject %s (index %d)\n", tmp.str(), info.boneIndex));
				BONEPOS_DUMPMATRIX3D(&(info.mtx));
				map[NAMEKEY(tmp)] = info;
				foundAsSubObj = true;
			}
			else
			{
				break;
			}
		}
	}

	return foundAsBone || foundAsSubObj;
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::validateStuff(RenderObjClass* robj, Real scale, const std::vector<AsciiString>& extraPublicBones) const
{
// srj sez: hm, this doesn't make sense; I think we really do need to validate transition states.
//	if (m_transition != NO_TRANSITION)
//		return;
	
	loadAnimations();
	if (!(m_validStuff & PUBLIC_BONES_VALID) && isValidTimeToCalcLogicStuff())
	{
		for (std::vector<AsciiString>::const_iterator bone_it = extraPublicBones.begin(); bone_it != extraPublicBones.end(); ++bone_it)
		{
			addPublicBone(*bone_it);
		}
		m_validStuff |= PUBLIC_BONES_VALID;
	}
	validateCachedBones(robj, scale);
	validateTurretInfo();
	validateWeaponBarrelInfo();
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::validateCachedBones(RenderObjClass* robj, Real scale) const
{
	//DEBUG_ASSERTCRASH(isValidTimeToCalcLogicStuff(), ("calling validateCachedBones() from in GameClient!"));
	if (m_validStuff & PRISTINE_BONES_VALID)
		return;

	// the render's call leaves the cache alone: the flag is already clear, so the map is not read, and
	// beside a tick clearing it here raced the tick's fill
	if (!isValidTimeToCalcLogicStuff())
		return;

	// R3: this cache and the asset manager below are the render's too, so a tick beside it parks it
	// before it writes either
	LogicThread::parkRender();
	setFPMode();

	BONEPOS_LOG(("Validating bones for %s: %s", m_modelName.str(), getDescription().str()));
	//BONEPOS_LOG(("Passing in valid render obj: %d\n", (robj != 0)));
	BONEPOS_DUMPREAL(scale);

	m_pristineBones.clear();

	// go ahead and set this here, in case we return early.
	m_validStuff |= PRISTINE_BONES_VALID;

	Bool tossRobj = false;
	if (robj == NULL)
	{
		// the asset manager's built-in NULL object has no bones either, so it is not worth a trip there
		// from the tick
		if (m_modelName.isEmpty() || m_modelName.compareNoCase("NULL") == 0)
		{
			//BONEPOS_LOG(("Bailing: model name is empty\n"));
			return;
		}

		LOGIC_CENSUS( CENSUS_ASSET_LOAD );
		robj =W3DDisplay::m_assetManager->Create_Render_Obj(m_modelName.str(), scale, 0);
		DEBUG_ASSERTCRASH(robj, ("*** ASSET ERROR: Model %s not found!\n",m_modelName.str()));
		if (!robj)
		{
			//BONEPOS_LOG(("Bailing: could not load render object\n"));
			return;
		}
		tossRobj = true;
	}

	Matrix3D			originalTransform = robj->Get_Transform();	// save the transform
	HLodClass*		hlod = NULL;
	HAnimClass*		curAnim = NULL;
	int						numFrames = 0;
	float					frame = 0.0f;
	int						mode = 0;
	float					mult = 1.0f;

	if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		hlod = (HLodClass*)robj;
		curAnim = hlod->Peek_Animation_And_Info(frame, numFrames, mode, mult);
	}

	// if we have any animations in this state, always choose the first, since the animations
	// vary on a per-client basis.
	HAnimClass* animToUse;
	if (m_animations.size() > 0)
	{
		animToUse = m_animations.front().getAnimHandle();	// return an AddRef'ed handle
	}
	else
	{
		animToUse = curAnim;	// Peek_Animation_And_Info does not addref, so we must do so here
		if (animToUse)
			animToUse->Add_Ref();
	}
	if (animToUse != NULL)
	{
		// make sure we're in frame zero.
		Int whichFrame = testFlagBit(m_flags, PRISTINE_BONE_POS_IN_FINAL_FRAME) ? animToUse->Get_Num_Frames()-1 : 0;
		robj->Set_Animation(animToUse, whichFrame, RenderObjClass::ANIM_MODE_MANUAL);
		// must balance the addref, above
		REF_PTR_RELEASE(animToUse);
		animToUse = NULL;
	}

	Matrix3D tmp(true);
	tmp.Scale(scale);
	robj->Set_Transform(tmp);					// set to identity transform

	if (TheGlobalData)
	{
		for (std::vector<AsciiString>::const_iterator it = TheGlobalData->m_standardPublicBones.begin(); it != TheGlobalData->m_standardPublicBones.end(); ++it)
		{
			if (!doSingleBoneName(robj, *it, m_pristineBones))
			{
				// don't crash here, since these are catch-all global bones and won't be present in most models.
				//DEBUG_CRASH(("public bone %s (and variations thereof) not found in model %s!\n",it->str(),m_modelName.str()));
			}
			//else
			//{
			//	DEBUG_LOG(("global bone %s (or variations thereof) found in model %s\n",it->str(),m_modelName.str()));
			//}
		}
	}
	for (std::vector<AsciiString>::const_iterator it = m_publicBones.begin(); it != m_publicBones.end(); ++it)
	{
		if (!doSingleBoneName(robj, *it, m_pristineBones))
		{
			// DO crash here, since we specifically requested this bone for this model
			DEBUG_CRASH(("*** ASSET ERROR: public bone '%s' (and variations thereof) not found in model %s!\n",it->str(),m_modelName.str()));
		}
		//else
		//{
		//	DEBUG_LOG(("extra bone %s (or variations thereof) found in model %s\n",it->str(),m_modelName.str()));
		//}
	}

	robj->Set_Transform(originalTransform);			// restore previous transform
	if (curAnim != NULL)
	{
		robj->Set_Animation(curAnim, frame, mode);
		hlod->Set_Animation_Frame_Rate_Multiplier(mult);
	}

	if (tossRobj)
	{
		REF_PTR_RELEASE(robj);
	}
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::validateWeaponBarrelInfo() const
{
	//DEBUG_ASSERTCRASH(isValidTimeToCalcLogicStuff(), ("calling validateWeaponBarrelInfo() from in GameClient!"));
	if (m_validStuff & BARRELS_VALID)
		return;

	if (!isValidTimeToCalcLogicStuff())
	{
		return;
	}

	// R3: a state the render switches to inside the window reads these
	LogicThread::parkRender();
	setFPMode();

	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
	{
		m_weaponBarrelInfoVec[wslot].clear();
		m_hasRecoilBonesOrMuzzleFlashes[wslot] = false;

		const AsciiString& fxBoneName = m_weaponFireFXBoneName[wslot];
		const AsciiString& recoilBoneName = m_weaponRecoilBoneName[wslot];
		const AsciiString& mfName = m_weaponMuzzleFlashName[wslot];
		const AsciiString& plbName = m_weaponProjectileLaunchBoneName[wslot];
    
// a useful tool for finding missing INI entries in the weapon-bone block
// since this class has no idea which object it refers to, it must assume that the projectilelaunchbonename
// is required if the fxBoneName is specified
// this is not always true, since some weapons have no launched projectiles, hence the caveat in the assert message
//#if defined(_DEBUG) || defined(_INTERNAL)
//    if (
//          ( m_modelName.startsWith("UV") || m_modelName.startsWith("NV") || m_modelName.startsWith("AV") ||
//            m_modelName.startsWith("uv") || m_modelName.startsWith("nv") || m_modelName.startsWith("av")
//          )
//
//          && fxBoneName.isNotEmpty()
//          
//      )
//    DEBUG_ASSERTCRASH( plbName.isNotEmpty(), ("You appear to have a missing projectilelaunchbonename. \n Promptly ignore this assert if this model is used by a non-projectile-weapon-bearing unit\nModel name = %s.", m_modelName.str()) );
//#endif

		if (fxBoneName.isNotEmpty() || recoilBoneName.isNotEmpty() || mfName.isNotEmpty() || plbName.isNotEmpty())
		{
			Int prevFxBone = 0;
			char buffer[256];
			for (Int i = 1; i <= 99; ++i)
			{
				WeaponBarrelInfo info;
				info.m_projectileOffsetMtx.Make_Identity();

				if (!recoilBoneName.isEmpty())
				{
					snprintf(buffer, ARRAY_SIZE(buffer), "%s%02d",recoilBoneName.str(), i);
					findPristineBone(NAMEKEY(buffer), &info.m_recoilBone);
				}
				if (!mfName.isEmpty())
				{
					snprintf(buffer, ARRAY_SIZE(buffer), "%s%02d",mfName.str(), i);
					findPristineBone(NAMEKEY(buffer), &info.m_muzzleFlashBone);
#if defined(_DEBUG) || defined(_INTERNAL)
					if (info.m_muzzleFlashBone)
						info.m_muzzleFlashBoneName = buffer;
#endif
				}
				if (!fxBoneName.isEmpty())
				{
					snprintf(buffer, ARRAY_SIZE(buffer), "%s%02d",fxBoneName.str(), i);
					findPristineBone(NAMEKEY(buffer), &info.m_fxBone);
					// special case: if we have multiple muzzleflashes, but only one fxbone, use that fxbone for everything.
					if (info.m_fxBone == 0 && info.m_muzzleFlashBone != 0)
						info.m_fxBone = prevFxBone;
				}

				Int plbBoneIndex = 0;
				if (!plbName.isEmpty())
				{
					snprintf(buffer, ARRAY_SIZE(buffer), "%s%02d",plbName.str(), i);
					const Matrix3D* mtx = findPristineBone(NAMEKEY(buffer), &plbBoneIndex);
					if (mtx != NULL)
					{
						info.m_projectileOffsetMtx = *mtx;
						info.m_hasProjectileLaunchBone = true;
					}
				}

				if (info.m_fxBone == 0 && info.m_recoilBone == 0 && info.m_muzzleFlashBone == 0 && plbBoneIndex == 0)
					break;

				CRCDEBUG_LOG(("validateWeaponBarrelInfo() - model name %s wslot %d\n", m_modelName.str(), wslot));
				DUMPMATRIX3D(&(info.m_projectileOffsetMtx));
				BONEPOS_LOG(("validateWeaponBarrelInfo() - model name %s wslot %d\n", m_modelName.str(), wslot));
				BONEPOS_DUMPMATRIX3D(&(info.m_projectileOffsetMtx));
				m_weaponBarrelInfoVec[wslot].push_back(info);

				if (info.m_recoilBone != 0 || info.m_muzzleFlashBone != 0)
					m_hasRecoilBonesOrMuzzleFlashes[wslot] = true;

				prevFxBone = info.m_fxBone;
			}
			
			if (m_weaponBarrelInfoVec[wslot].empty())
			{
				// try the unadorned names
				WeaponBarrelInfo info;
				if (!recoilBoneName.isEmpty())
					findPristineBone(NAMEKEY(recoilBoneName), &info.m_recoilBone);

				if (!mfName.isEmpty())
					findPristineBone(NAMEKEY(mfName), &info.m_muzzleFlashBone);

#if defined(_DEBUG) || defined(_INTERNAL)
				if (info.m_muzzleFlashBone)
					info.m_muzzleFlashBoneName = mfName;
#endif

				const Matrix3D* plbMtx = plbName.isEmpty() ? NULL : findPristineBone(NAMEKEY(plbName), NULL);
				if (plbMtx != NULL)
				{
					info.m_projectileOffsetMtx = *plbMtx;
					info.m_hasProjectileLaunchBone = true;
				}
				else
					info.m_projectileOffsetMtx.Make_Identity();

				if (!fxBoneName.isEmpty())
					findPristineBone(NAMEKEY(fxBoneName), &info.m_fxBone);
				
				if (info.m_fxBone != 0 || info.m_recoilBone != 0 || info.m_muzzleFlashBone != 0 || plbMtx != NULL)
				{
					CRCDEBUG_LOG(("validateWeaponBarrelInfo() - model name %s (unadorned) wslot %d\n", m_modelName.str(), wslot));
					DUMPMATRIX3D(&(info.m_projectileOffsetMtx));
					BONEPOS_LOG(("validateWeaponBarrelInfo() - model name %s (unadorned) wslot %d\n", m_modelName.str(), wslot));
					BONEPOS_DUMPMATRIX3D(&(info.m_projectileOffsetMtx));
					m_weaponBarrelInfoVec[wslot].push_back(info);
					if (info.m_recoilBone != 0 || info.m_muzzleFlashBone != 0)
						m_hasRecoilBonesOrMuzzleFlashes[wslot] = true;
				}
				else
				{
					CRCDEBUG_LOG(("validateWeaponBarrelInfo() - model name %s (unadorned) found nothing\n", m_modelName.str()));
					BONEPOS_LOG(("validateWeaponBarrelInfo() - model name %s (unadorned) found nothing\n", m_modelName.str()));
				}
			}	// if empty

			DEBUG_ASSERTCRASH(!(m_modelName.isNotEmpty() && m_weaponBarrelInfoVec[wslot].empty()), ("*** ASSET ERROR: No fx bone named '%s' found in model %s!\n",fxBoneName.str(),m_modelName.str()));
		}
	}
	m_validStuff |= BARRELS_VALID;
} 

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::validateTurretInfo() const
{
	//DEBUG_ASSERTCRASH(isValidTimeToCalcLogicStuff(), ("calling validateTurretInfo() from in GameClient!"));
	if (m_validStuff & TURRETS_VALID)
		return;

	// the render's call leaves the bones alone: they are 0 until the flag is set, and beside a tick
	// zeroing them here wiped the bones the tick had just found
	if (!isValidTimeToCalcLogicStuff())
		return;

	// R3: as validateWeaponBarrelInfo
	LogicThread::parkRender();
	setFPMode();

	for (int tslot = 0; tslot < MAX_TURRETS; ++tslot)
	{
		TurretInfo& tur = m_turrets[tslot];
		if (m_modelName.isEmpty())
		{
			tur.m_turretAngleBone = 0;
			tur.m_turretPitchBone = 0;
			continue;
		}

		if (tur.m_turretAngleNameKey != NAMEKEY_INVALID)
		{
			if (findPristineBone(tur.m_turretAngleNameKey, &tur.m_turretAngleBone) == NULL)
			{
				DEBUG_CRASH(("*** ASSET ERROR: TurretBone %s not found! (%s)\n",KEYNAME(tur.m_turretAngleNameKey).str(),m_modelName.str()));
				tur.m_turretAngleBone = 0;
			}
		}
		else
		{
			tur.m_turretAngleBone = 0;
		}	

		if (tur.m_turretPitchNameKey != NAMEKEY_INVALID)
		{
			if (findPristineBone(tur.m_turretPitchNameKey, &tur.m_turretPitchBone) == NULL)
			{
				DEBUG_CRASH(("*** ASSET ERROR: TurretBone %s not found! (%s)\n",KEYNAME(tur.m_turretPitchNameKey).str(),m_modelName.str()));
				tur.m_turretPitchBone = 0;
			}
		}
		else
		{
			tur.m_turretPitchBone = 0;
		}	
	}

	m_validStuff |= TURRETS_VALID;
}

//-------------------------------------------------------------------------------------------------
const Matrix3D* ModelConditionInfo::findPristineBone(NameKeyType boneName, Int* boneIndex) const
{
	DEBUG_ASSERTCRASH((m_validStuff & PRISTINE_BONES_VALID), ("*** ASSET ERROR: bones are not valid"));
	if (!(m_validStuff & PRISTINE_BONES_VALID))
	{
		// set it to zero, some callers rely on this
		if (boneIndex)
			*boneIndex = 0;
		return NULL;
	}

	if (boneName == NAMEKEY_INVALID)
	{
		// set it to zero, some callers rely on this
		if (boneIndex)
			*boneIndex = 0;
		return NULL;
	}

	PristineBoneInfoMap::const_iterator it = m_pristineBones.find(boneName);
	if (it != m_pristineBones.end())
	{
		if (boneIndex)
			*boneIndex = it->second.boneIndex;
		return &it->second.mtx;
	}
	else
	{
		// set it to zero -- some callers rely on this!
		if (boneIndex)
			*boneIndex = 0;
		return NULL;
	}
}

//-------------------------------------------------------------------------------------------------
Bool ModelConditionInfo::findPristineBonePos(NameKeyType boneName, Coord3D& pos) const
{
	const Matrix3D* mtx = findPristineBone(boneName, NULL);
	if (mtx)
	{
		Vector3 v = mtx->Get_Translation();
		pos.x = v.X;
		pos.y = v.Y;
		pos.z = v.Z;
		return true;
	}
	else
	{
		pos.zero();
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::loadAnimations() const
{ 
#ifdef RETAIN_ANIM_HANDLES
	for (W3DAnimationVector::const_iterator it2 = m_animations.begin(); it2 != m_animations.end(); ++it2)
	{
		HAnimClass* h = it2->getAnimHandle();	// just force it to get loaded
		REF_PTR_RELEASE(h);	
		h = NULL;
	}
#else
	// srj sez: I think there is no real reason to preload these all anymore. things that need the anims
	// (for bones) will force an implicit load anyway, but there's no point in forcibly loading the anims 
	// that don't contain interesting logical bones.
#endif
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::clear()
{ 
	int i;
#if defined(_DEBUG) || defined(_INTERNAL)
	m_description.clear();
#endif
	m_conditionsYesVec.clear();
	m_modelName.clear();
	for (i = 0; i < MAX_TURRETS; ++i)
	{
		m_turrets[i].clear();
	}
	m_hideShowVec.clear();
	for (i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		m_weaponFireFXBoneName[i].clear();
		m_weaponRecoilBoneName[i].clear();
		m_weaponMuzzleFlashName[i].clear();
		m_weaponProjectileLaunchBoneName[i].clear();
		m_weaponBarrelInfoVec[i].clear();
		m_hasRecoilBonesOrMuzzleFlashes[i] = false;
	}
	m_particleSysBones.clear();
	m_animations.clear();
	m_flags = 0;
	m_transitionKey = NAMEKEY_INVALID;
	m_allowToFinishKey = NAMEKEY_INVALID;
	m_iniReadFlags = 0;
	m_mode = RenderObjClass::ANIM_MODE_ONCE;
	m_transitionSig = NO_TRANSITION;
	m_animMinSpeedFactor = 1.0f;
	m_animMaxSpeedFactor = 1.0f;
	m_pristineBones.clear();
	m_validStuff = 0;
}

//-------------------------------------------------------------------------------------------------
W3DModelDrawModuleData::W3DModelDrawModuleData() : 
	m_validated(0),
	m_okToChangeModelColor(false),
  m_particlesAttachedToAnimatedBones(false),
	m_animationsRequirePower(true),
	m_stabilizedTurretPitch(false),
	m_barrelArtPitch(0.0f),
#ifdef CACHE_ATTACH_BONE
	m_attachToDrawableBoneOffsetValid(false),
#endif
	m_minLODRequired(STATIC_GAME_LOD_LOW),
	m_defaultState(-1)
{
	const Real MAX_SHIFT = 3.0f;			
	const Real INITIAL_RECOIL_RATE = 2.0f;	
	const Real RECOIL_DAMPING = 0.4f;	
	const Real SETTLE_RATE = 0.065f;		

	m_projectileBoneFeedbackEnabledSlots = 0;
	m_initialRecoil = ConvertRetailPerFrameRate(INITIAL_RECOIL_RATE);
	m_maxRecoil = MAX_SHIFT;
	m_recoilDamping = ConvertRetailPerFrameFactor(RECOIL_DAMPING);
	m_recoilSettle = ConvertRetailPerFrameRate(SETTLE_RATE);


  m_receivesDynamicLights = TRUE;

	// m_ignoreConditionStates defaults to all zero, which is what we want
}

//-------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::validateStuffForTimeAndWeather(const Drawable* draw, Bool night, Bool snowy) const
{
	if (!isValidTimeToCalcLogicStuff())
		return;

	enum
	{
		NORMAL			= 0x0001,
		NIGHT				= 0x0002,
		SNOWY				= 0x0004,
		NIGHT_SNOWY	= 0x0008
	};

	Int mode;
	if (night)
	{
		mode = (snowy) ? NIGHT_SNOWY : NIGHT;
	}
	else
	{
		mode = (snowy) ? SNOWY : NORMAL;
	}

	if (m_validated & mode)
		return;

	m_validated |= mode;

	ModelConditionVector::iterator c_it;	// VC6 leaked it out to the transition loop below
	for (c_it = m_conditionStates.begin(); c_it != m_conditionStates.end(); ++c_it)
	{
		// a template with no state for both night and snow falls back on its night or its snow state,
		// and logic asks that one's bones; validated here it is not loaded from inside a tick
		if (!c_it->matchesMode(false, false) && !c_it->matchesMode(night, false) &&
				!c_it->matchesMode(false, snowy) && !c_it->matchesMode(night, snowy))
			continue;

		c_it->validateStuff(NULL, draw->getScale(), m_extraPublicBones);
	}

	for (TransitionMap::iterator t_it = m_transitionMap.begin(); t_it != m_transitionMap.end(); ++t_it)
	{
		// here's the tricky part: only want to load the anims for this one if there is at least one
		// source AND at least one dest state that matches the current mode
		NameKeyType src = recoverSrcState(t_it->first);
		NameKeyType dst = recoverDstState(t_it->first);

		Bool a = false;
		Bool b = false;
		for (c_it = m_conditionStates.begin(); c_it != m_conditionStates.end(); ++c_it)
		{

			if (!a && c_it->m_transitionKey == src && c_it->matchesMode(night, snowy))
				a = true;

			if (!b && c_it->m_transitionKey == dst && c_it->matchesMode(night, snowy))
				b = true;

		}
		if (a && b)
		{
			t_it->second.loadAnimations();
			// nope -- transition states don't get public bones.
			//it->addPublicBone(m_extraPublicBones);

		// srj sez: hm, this doesn't make sense; I think we really do need to validate transition states.
			t_it->second.validateStuff(NULL, draw->getScale(), m_extraPublicBones);
		}
	}
}

//-------------------------------------------------------------------------------------------------
W3DModelDrawModuleData::~W3DModelDrawModuleData()
{
	m_conditionStateMap.clear();
}

//-------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::preloadAssets( TimeOfDay timeOfDay, Real scale ) const
{

	for( ModelConditionVector::iterator it = m_conditionStates.begin(); 
			 it != m_conditionStates.end(); 
			 ++it )
	{

		it->preloadAssets( timeOfDay, scale );

	}

}

//-------------------------------------------------------------------------------------------------
AsciiString W3DModelDrawModuleData::getBestModelNameForWB(const ModelConditionFlags& c) const
{
	const ModelConditionInfo* info = findBestInfo(c);
	if (info)
		return info->m_modelName;
	return AsciiString::TheEmptyString;
}

#ifdef CACHE_ATTACH_BONE
//-------------------------------------------------------------------------------------------------
const Vector3* W3DModelDrawModuleData::getAttachToDrawableBoneOffset(const Drawable* draw) const
{
	if (m_attachToDrawableBone.isEmpty())
	{
		return NULL;
	}
	else
	{
		if (!m_attachToDrawableBoneOffsetValid)
		{
		// must use pristine bone here since the result is used by logic
			Matrix3D boneMtx;
			if (draw->getPristineBonePositions(m_attachToDrawableBone.str(), 0, NULL, &boneMtx, 1) == 1)
			{
				m_attachToDrawableBoneOffset = boneMtx.Get_Translation();
			}
			else
			{
				m_attachToDrawableBoneOffset.X = 0;
				m_attachToDrawableBoneOffset.Y = 0;
				m_attachToDrawableBoneOffset.Z = 0;
			}
			m_attachToDrawableBoneOffsetValid = true;
		}
		return &m_attachToDrawableBoneOffset;
	}
}
#endif

//-------------------------------------------------------------------------------------------------
enum ParseCondStateType
{
	PARSE_NORMAL,
	PARSE_DEFAULT,
	PARSE_TRANSITION,
	PARSE_ALIAS
};

//-------------------------------------------------------------------------------------------------
static void parseAsciiStringLC( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	AsciiString* asciiString = (AsciiString *)store;
	*asciiString = ini->getNextAsciiString();
	asciiString->toLower();
}

//-------------------------------------------------------------------------------------------------
/** ReplaceTexture = <old> <new>: draw every model of this module with <new> wherever it names <old>. */
void W3DModelDrawModuleData::parseReplaceTexture( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	W3DModelDrawModuleData* self = (W3DModelDrawModuleData*)instance;
	self->m_replaceTextureOld = ini->getNextAsciiString();
	self->m_replaceTextureNew = ini->getNextAsciiString();
}

//-------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "InitialRecoilSpeed",	INI::parseVelocityReal, NULL, offsetof(W3DModelDrawModuleData, m_initialRecoil) },
		{ "MaxRecoilDistance",	INI::parseReal, NULL, offsetof(W3DModelDrawModuleData, m_maxRecoil) },
		{ "RecoilDamping",	INI::parseRetailPerFrameFactorReal, NULL, offsetof(W3DModelDrawModuleData, m_recoilDamping) },
		{ "RecoilSettleSpeed",	INI::parseVelocityReal, NULL, offsetof(W3DModelDrawModuleData, m_recoilSettle) },
		{ "OkToChangeModelColor",	INI::parseBool, NULL, offsetof(W3DModelDrawModuleData, m_okToChangeModelColor) },
		{ "AnimationsRequirePower",	INI::parseBool, NULL, offsetof(W3DModelDrawModuleData, m_animationsRequirePower) },
		{ "StabilizedTurretPitch",	INI::parseBool, NULL, offsetof(W3DModelDrawModuleData, m_stabilizedTurretPitch) },
		{ "BarrelArtPitch",	INI::parseAngleReal, NULL, offsetof(W3DModelDrawModuleData, m_barrelArtPitch) },
		{ "ParticlesAttachedToAnimatedBones",	INI::parseBool, NULL, offsetof(W3DModelDrawModuleData, m_particlesAttachedToAnimatedBones) },
		{ "MinLODRequired",		INI::parseStaticGameLODLevel,	NULL,	offsetof(W3DModelDrawModuleData, m_minLODRequired) },
		{ "ProjectileBoneFeedbackEnabledSlots", INI::parseBitString32, TheWeaponSlotTypeNames, offsetof(W3DModelDrawModuleData, m_projectileBoneFeedbackEnabledSlots) },
		{ "DefaultConditionState", W3DModelDrawModuleData::parseConditionState, (void*)PARSE_DEFAULT, 0 },
		{ "ConditionState", W3DModelDrawModuleData::parseConditionState, (void*)PARSE_NORMAL, 0 },
		{ "AliasConditionState", W3DModelDrawModuleData::parseConditionState, (void*)PARSE_ALIAS, 0 },
		{ "TransitionState", W3DModelDrawModuleData::parseConditionState, (void*)PARSE_TRANSITION, 0 },
		{ "TrackMarks", parseAsciiStringLC, NULL, offsetof(W3DModelDrawModuleData, m_trackFile) },
		{ "ExtraPublicBone", INI::parseAsciiStringVectorAppend, NULL, offsetof(W3DModelDrawModuleData, m_extraPublicBones) },
		{ "AttachToBoneInAnotherModule", parseAsciiStringLC, NULL, offsetof(W3DModelDrawModuleData, m_attachToDrawableBone) },
		{ "IgnoreConditionStates", ModelConditionFlags::parseFromINI, NULL, offsetof(W3DModelDrawModuleData, m_ignoreConditionStates) },
		{ "ReceivesDynamicLights", INI::parseBool, NULL, offsetof(W3DModelDrawModuleData, m_receivesDynamicLights) },
		{ "ReplaceTexture", W3DModelDrawModuleData::parseReplaceTexture, NULL, 0 },
    { 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);

}

//-------------------------------------------------------------------------------------------------
enum AnimParseType
{
	ANIM_NORMAL,
	ANIM_IDLE
};

//-------------------------------------------------------------------------------------------------
static void parseAnimation(INI* ini, void *instance, void * /*store*/, const void* userData)
{
	AnimParseType animType = (AnimParseType)(UnsignedInt)(uintptr_t)userData;	// an enum kept in a pointer

	AsciiString animName = ini->getNextAsciiString();
	animName.toLower();

	const char* distanceCoveredToken = ini->getNextTokenOrNull();
	Real distanceCovered = distanceCoveredToken ? INI::scanReal(distanceCoveredToken) : 0;
	DEBUG_ASSERTCRASH(!(animType == ANIM_IDLE && distanceCovered != 0), ("You should not specify nonzero DistanceCovered values for Idle Anims"));

	const char* timesToRepeatToken = ini->getNextTokenOrNull();
	Int timesToRepeat = timesToRepeatToken ? INI::scanInt(timesToRepeatToken) : 1;
	if (timesToRepeat < 1) timesToRepeat = 1;

	W3DAnimationInfo animInfo(animName, (animType == ANIM_IDLE), distanceCovered);

	ModelConditionInfo* self = (ModelConditionInfo*)instance;
	if (self->m_iniReadFlags & (1<<ANIMS_COPIED_FROM_DEFAULT_STATE))
	{
		self->m_iniReadFlags &= ~((1<<ANIMS_COPIED_FROM_DEFAULT_STATE)|(1<<GOT_IDLE_ANIMS)|(1<<GOT_NONIDLE_ANIMS));
		self->m_animations.clear();
	}

	if (animInfo.isIdleAnim())
		self->m_iniReadFlags |= (1<<GOT_IDLE_ANIMS);
	else
		self->m_iniReadFlags |= (1<<GOT_NONIDLE_ANIMS);

	if (!animName.isEmpty() && !animName.isNone())
	{
		while (timesToRepeat--)
			self->m_animations.push_back(animInfo);
	}
}	

//-------------------------------------------------------------------------------------------------
static void parseShowHideSubObject(INI* ini, void *instance, void *store, const void* userData)
{
	std::vector<ModelConditionInfo::HideShowSubObjInfo>* vec = (std::vector<ModelConditionInfo::HideShowSubObjInfo>*)store;
	AsciiString subObjName = ini->getNextAsciiString();
	subObjName.toLower();

	// handle "None"
	if (subObjName.isNone())
	{
		vec->clear();
		return;
	}

	while (subObjName.isNotEmpty())
	{
		Bool found = false;
		for (std::vector<ModelConditionInfo::HideShowSubObjInfo>::iterator it = vec->begin(); it != vec->end(); ++it)
		{
			if (strcasecmp(it->subObjName.str(), subObjName.str()) == 0)
			{
				it->hide = (userData != NULL);
				found = true;
			}
		}
		
		if (!found)
		{
			ModelConditionInfo::HideShowSubObjInfo info;
			info.subObjName = subObjName;
			info.hide = (userData != NULL);
			vec->push_back(info);
		}
		subObjName = ini->getNextAsciiString();
		subObjName.toLower();
	}
}	

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::showSubObject( const AsciiString& name, Bool show )
{
	if( name.isNotEmpty() )
	{
		Bool found = false;
		for( std::vector<ModelConditionInfo::HideShowSubObjInfo>::iterator it = m_subObjectVec.begin(); it != m_subObjectVec.end(); ++it )
		{
			if( strcasecmp( it->subObjName.str(), name.str() ) == 0 )
			{
				it->hide = !show;
				found = true;
			}
		}

		if( !found )
		{
			ModelConditionInfo::HideShowSubObjInfo info;
			info.subObjName = name;
			info.hide = !show;
			m_subObjectVec.push_back( info );
		}
	}
}



//-------------------------------------------------------------------------------------------------
static void parseWeaponBoneName(INI* ini, void *instance, void * store, const void* /*userData*/)
{
	ModelConditionInfo* self = (ModelConditionInfo*)instance;
	AsciiString* arr = (AsciiString*)store;
	
	WeaponSlotType wslot = (WeaponSlotType)INI::scanIndexList(ini->getNextToken(), TheWeaponSlotTypeNames);
	arr[wslot] = ini->getNextAsciiString();
	arr[wslot].toLower();
	if (arr[wslot].isNone())
		arr[wslot].clear();

	if (self)
		self->addPublicBone(arr[wslot]);
}

//-------------------------------------------------------------------------------------------------
static void parseParticleSysBone(INI* ini, void *instance, void * store, const void * /*userData*/)
{
	ParticleSysBoneInfo info;
	info.boneName = ini->getNextAsciiString();
	info.boneName.toLower();
	ini->parseParticleSystemTemplate(ini, instance, &(info.particleSystemTemplate), NULL);
	ModelConditionInfo *self = (ModelConditionInfo *)instance;
	self->m_particleSysBones.push_back(info);
}

//-------------------------------------------------------------------------------------------------
static void parseRealRange( INI *ini, void *instance, void *store, const void* /*userData*/ )
{
	ModelConditionInfo *self = (ModelConditionInfo *)instance;

	const char *token = ini->getNextToken();
	self->m_animMinSpeedFactor = ini->scanReal( token );
	token = ini->getNextToken();
	self->m_animMaxSpeedFactor = ini->scanReal( token );
}

//-------------------------------------------------------------------------------------------------
static void parseLowercaseNameKey(INI* ini, void *instance, void * store, const void * /*userData*/)
{
	NameKeyType* key = (NameKeyType*)store;

	AsciiString tmp = ini->getNextToken();
	tmp.toLower();

	*key = NAMEKEY(tmp.str());
}

//-------------------------------------------------------------------------------------------------
static void parseBoneNameKey(INI* ini, void *instance, void * store, const void * /*userData*/)
{
	ModelConditionInfo* self = (ModelConditionInfo*)instance;
	NameKeyType* key = (NameKeyType*)store;

	AsciiString tmp = ini->getNextToken();
	tmp.toLower();

	if (self)
		self->addPublicBone(tmp);

	if (tmp.isEmpty() || tmp.isNone())
		*key = NAMEKEY_INVALID;
	else
		*key = NAMEKEY(tmp.str());
}

//-------------------------------------------------------------------------------------------------
static Bool doesStateExist(const ModelConditionVector& v, const ModelConditionFlags& f)
{
	for (ModelConditionVector::const_iterator it = v.begin(); it != v.end(); ++it)
	{
		for (Int i = it->getConditionsYesCount()-1; i >= 0; --i)
		{
			if (f == it->getNthConditionsYes(i))
				return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::parseConditionState(INI* ini, void *instance, void * /*store*/, const void* userData)
{
	static const FieldParse myFieldParse[] = 
	{
		{ "Model",	parseAsciiStringLC, NULL, offsetof(ModelConditionInfo, m_modelName) },
		{ "Turret",	parseBoneNameKey, NULL, offsetof(ModelConditionInfo, m_turrets[0].m_turretAngleNameKey) },
		{ "TurretArtAngle", INI::parseAngleReal, NULL, offsetof(ModelConditionInfo, m_turrets[0].m_turretArtAngle) },
		{ "TurretPitch",	parseBoneNameKey, NULL, offsetof(ModelConditionInfo, m_turrets[0].m_turretPitchNameKey) },
		{ "TurretArtPitch", INI::parseAngleReal, NULL, offsetof(ModelConditionInfo, m_turrets[0].m_turretArtPitch) },
		{ "AltTurret",	parseBoneNameKey, NULL, offsetof(ModelConditionInfo, m_turrets[1].m_turretAngleNameKey) },
		{ "AltTurretArtAngle", INI::parseAngleReal, NULL, offsetof(ModelConditionInfo, m_turrets[1].m_turretArtAngle) },
		{ "AltTurretPitch",	parseBoneNameKey, NULL, offsetof(ModelConditionInfo, m_turrets[1].m_turretPitchNameKey) },
		{ "AltTurretArtPitch", INI::parseAngleReal, NULL, offsetof(ModelConditionInfo, m_turrets[1].m_turretArtPitch) },
		{ "ShowSubObject", parseShowHideSubObject, (void*)0, offsetof(ModelConditionInfo, m_hideShowVec) },
		{ "HideSubObject", parseShowHideSubObject, (void*)1, offsetof(ModelConditionInfo, m_hideShowVec) },
		{ "WeaponFireFXBone", parseWeaponBoneName, NULL, offsetof(ModelConditionInfo, m_weaponFireFXBoneName[0]) },
		{ "WeaponRecoilBone", parseWeaponBoneName, NULL, offsetof(ModelConditionInfo, m_weaponRecoilBoneName[0]) },
		{ "WeaponMuzzleFlash", parseWeaponBoneName, NULL, offsetof(ModelConditionInfo, m_weaponMuzzleFlashName[0]) },
		{ "WeaponLaunchBone", parseWeaponBoneName, NULL, offsetof(ModelConditionInfo, m_weaponProjectileLaunchBoneName[0]) },
		{ "WeaponHideShowBone", parseWeaponBoneName, NULL, offsetof(ModelConditionInfo, m_weaponProjectileHideShowName[0]) },
		{ "Animation", parseAnimation, (void*)ANIM_NORMAL, offsetof(ModelConditionInfo, m_animations) },
		{ "IdleAnimation", parseAnimation, (void*)ANIM_IDLE, offsetof(ModelConditionInfo, m_animations) },
		{ "AnimationMode", INI::parseIndexList, TheAnimModeNames, offsetof(ModelConditionInfo, m_mode) },
		{ "TransitionKey", parseLowercaseNameKey, NULL, offsetof(ModelConditionInfo, m_transitionKey) },
		{ "WaitForStateToFinishIfPossible", parseLowercaseNameKey, NULL, offsetof(ModelConditionInfo, m_allowToFinishKey) },
		{ "Flags", INI::parseBitString32, ACBitsNames, offsetof(ModelConditionInfo, m_flags) },
		{ "ParticleSysBone", parseParticleSysBone, NULL, 0 },
		{ "AnimationSpeedFactorRange", parseRealRange, NULL, 0 },
		{ 0, 0, 0, 0 }
	};

	ModelConditionInfo info;
	W3DModelDrawModuleData* self = (W3DModelDrawModuleData*)instance;
	ParseCondStateType cst = (ParseCondStateType)(UnsignedInt)(uintptr_t)userData;	// an enum kept in a pointer
	switch (cst)
	{
		case PARSE_DEFAULT:
		{
			if (self->m_defaultState >= 0)
			{
				DEBUG_CRASH(("*** ASSET ERROR: you may have only one default state!\n"));
				throw INI_INVALID_DATA;
			}
			else if (ini->getNextTokenOrNull())
			{
				DEBUG_CRASH(("*** ASSET ERROR: unknown keyword\n"));
				throw INI_INVALID_DATA;
			}
			else
			{
				if (!self->m_conditionStates.empty())
				{
					DEBUG_CRASH(("*** ASSET ERROR: when using DefaultConditionState, it must be the first state listed (%s)\n",TheThingTemplateBeingParsedName.str()));
					throw INI_INVALID_DATA;
				}

				// note, this is size(), not size()-1, since we haven't actually modified the list yet
				self->m_defaultState = self->m_conditionStates.size();
				//DEBUG_LOG(("set default state to %d\n",self->m_defaultState));

				// add an empty conditionstateflag set
				ModelConditionFlags blankConditions;
				info.m_conditionsYesVec.clear();
				info.m_conditionsYesVec.push_back(blankConditions);

	#if defined(_DEBUG) || defined(_INTERNAL)
				info.m_description.clear();
				info.m_description.concat(TheThingTemplateBeingParsedName);
				info.m_description.concat(" DEFAULT");
	#endif
			}
		}
		break;

		case PARSE_TRANSITION:
		{
		  AsciiString firstNm = ini->getNextToken(); firstNm.toLower();
		  AsciiString secondNm = ini->getNextToken(); secondNm.toLower();
			NameKeyType firstKey = NAMEKEY(firstNm);
			NameKeyType secondKey = NAMEKEY(secondNm);

			if (firstKey == secondKey)
			{
				DEBUG_CRASH(("*** ASSET ERROR: You may not declare a transition between two identical states\n"));
				throw INI_INVALID_DATA;
			}

			if (self->m_defaultState >= 0)
			{
				info = self->m_conditionStates.at(self->m_defaultState);
				info.m_iniReadFlags |= (1<<ANIMS_COPIED_FROM_DEFAULT_STATE);
				info.m_transitionKey = NAMEKEY_INVALID;
				info.m_allowToFinishKey = NAMEKEY_INVALID;
			}

			info.m_transitionSig = buildTransitionSig(firstKey, secondKey);
	#if defined(_DEBUG) || defined(_INTERNAL)
			info.m_description.clear();
			info.m_description.concat(TheThingTemplateBeingParsedName);
			info.m_description.concat(" TRANSITION: ");
			info.m_description.concat(firstNm);
			info.m_description.concat(" ");
			info.m_description.concat(secondNm);
	#endif

		}
		break;

		case PARSE_ALIAS:
		{
			if (self->m_conditionStates.empty())
			{
				DEBUG_CRASH(("*** ASSET ERROR: AliasConditionState must refer to the previous state!\n"));
				throw INI_INVALID_DATA;
			}
			
			ModelConditionInfo& prevState = self->m_conditionStates.at(self->m_conditionStates.size()-1);
			
			ModelConditionFlags conditionsYes;

	#if defined(_DEBUG) || defined(_INTERNAL)
			AsciiString description;
			conditionsYes.parse(ini, &description);

			prevState.m_description.concat("\nAKA: ");
			prevState.m_description.concat(description);
	#else
			conditionsYes.parse(ini, NULL);
	#endif
			
			if (conditionsYes.anyIntersectionWith(self->m_ignoreConditionStates))
			{
				DEBUG_CRASH(("You should not specify bits in a state once they are used in IgnoreConditionStates (%s)\n", TheThingTemplateBeingParsedName.str()));
				throw INI_INVALID_DATA;
			}

			if (doesStateExist(self->m_conditionStates, conditionsYes))
			{
				DEBUG_CRASH(("*** ASSET ERROR: duplicate condition states are not currently allowed"));
				throw INI_INVALID_DATA;
			}


			if (!conditionsYes.any() && self->m_defaultState >= 0)
			{
				DEBUG_CRASH(("*** ASSET ERROR: you may not specify both a Default state and a Conditions=None state"));
				throw INI_INVALID_DATA;
			}

			prevState.m_conditionsYesVec.push_back(conditionsYes);
			
			// yes, return, NOT break!
			return;
		}

		case PARSE_NORMAL:
		{
			if (self->m_defaultState >= 0 && cst != PARSE_ALIAS)
			{
				info = self->m_conditionStates.at(self->m_defaultState);
				info.m_iniReadFlags |= (1<<ANIMS_COPIED_FROM_DEFAULT_STATE);
				info.m_conditionsYesVec.clear();
			}
	// no, we do not currently require a default state, cuz it would break the exiting INI
	// files too badly. maybe someday.
	//	else
	//	{
	//		DEBUG_CRASH(("*** ASSET ERROR: you must specify a default state\n"));
	//		throw INI_INVALID_DATA;
	//	}
			
			ModelConditionFlags conditionsYes;
	#if defined(_DEBUG) || defined(_INTERNAL)
			AsciiString description;
			conditionsYes.parse(ini, &description);

			info.m_description.clear();
			info.m_description.concat(TheThingTemplateBeingParsedName);
			info.m_description.concat("\n         ");
			info.m_description.concat(description);
	#else
			conditionsYes.parse(ini, NULL);
	#endif

			if (conditionsYes.anyIntersectionWith(self->m_ignoreConditionStates))
			{
				DEBUG_CRASH(("You should not specify bits in a state once they are used in IgnoreConditionStates (%s)\n", TheThingTemplateBeingParsedName.str()));
				throw INI_INVALID_DATA;
			}

			if (self->m_defaultState < 0 && self->m_conditionStates.empty() && conditionsYes.any())
			{
				// it doesn't actually NEED to be first, but it does need to be present, and this is the simplest way to enforce...
				DEBUG_CRASH(("*** ASSET ERROR: when not using DefaultConditionState, the first ConditionState must be for NONE (%s)\n",TheThingTemplateBeingParsedName.str()));
				throw INI_INVALID_DATA;
			}

			if (!conditionsYes.any() && self->m_defaultState >= 0)
			{
				DEBUG_CRASH(("*** ASSET ERROR: you may not specify both a Default state and a Conditions=None state"));
				throw INI_INVALID_DATA;
			}

			if (doesStateExist(self->m_conditionStates, conditionsYes))
			{
				DEBUG_CRASH(("*** ASSET ERROR: duplicate condition states are not currently allowed (%s)",info.m_description.str()));
				throw INI_INVALID_DATA;
			}
			
			DEBUG_ASSERTCRASH(info.m_conditionsYesVec.size() == 0, ("*** ASSET ERROR: nonempty m_conditionsYesVec.size(), see srj"));
			info.m_conditionsYesVec.clear();
			info.m_conditionsYesVec.push_back(conditionsYes);
		}
		break;
	}

	ini->initFromINI(&info, myFieldParse);

	if (info.m_modelName.isEmpty())
	{
		DEBUG_CRASH(("*** ASSET ERROR: you must specify a model name"));
		throw INI_INVALID_DATA;
	}
	else if (info.m_modelName.isNone())
	{
		info.m_modelName.clear();
	}

	if ((info.m_iniReadFlags & (1<<GOT_IDLE_ANIMS)) && (info.m_iniReadFlags & (1<<GOT_NONIDLE_ANIMS)))
	{
		DEBUG_CRASH(("*** ASSET ERROR: you should not specify both Animations and IdleAnimations for the same state"));
		throw INI_INVALID_DATA;
	}

	if ((info.m_iniReadFlags & (1<<GOT_IDLE_ANIMS)) && (info.m_mode != RenderObjClass::ANIM_MODE_ONCE && info.m_mode != RenderObjClass::ANIM_MODE_ONCE_BACKWARDS))
	{
		DEBUG_CRASH(("*** ASSET ERROR: Idle Anims should always use ONCE or ONCE_BACKWARDS (%s)\n",TheThingTemplateBeingParsedName.str()));
		throw INI_INVALID_DATA;
	}

	info.m_validStuff &= ~ModelConditionInfo::HAS_PROJECTILE_BONES;
	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
	{
		if (info.m_weaponProjectileLaunchBoneName[wslot].isNotEmpty())
		{
			info.m_validStuff |= ModelConditionInfo::HAS_PROJECTILE_BONES;
			break;
		}
	}

	if (cst == PARSE_TRANSITION)
	{
		if (info.m_iniReadFlags & (1<<GOT_IDLE_ANIMS))
		{
			DEBUG_CRASH(("*** ASSET ERROR: Transition States should not specify Idle anims"));
			throw INI_INVALID_DATA;
		}

		if (info.m_mode != RenderObjClass::ANIM_MODE_ONCE && info.m_mode != RenderObjClass::ANIM_MODE_ONCE_BACKWARDS)
		{
			DEBUG_CRASH(("*** ASSET ERROR: Transition States should always use ONCE or ONCE_BACKWARDS"));
			throw INI_INVALID_DATA;
		}

		if (info.m_transitionKey != NAMEKEY_INVALID || info.m_allowToFinishKey != NAMEKEY_INVALID)
		{
			DEBUG_CRASH(("*** ASSET ERROR: Transition States must not have transition keys or m_allowToFinishKey"));
			throw INI_INVALID_DATA;
		}

		self->m_transitionMap[info.m_transitionSig] = info;
	}
	else
	{
		self->m_conditionStates.push_back(info);
	}
}

//-------------------------------------------------------------------------------------------------
static Int countOnBits(UnsignedInt val)
{
	Int count = 0;
	for (Int i = 0; i < 32; ++i)
	{
		if (val & 1) 
			++count;
		val >>= 1;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
const ModelConditionInfo* W3DModelDrawModuleData::findBestInfo(const ModelConditionFlags& c) const
{
	ModelConditionFlags bits = c;
	bits.clear(m_ignoreConditionStates);
	return m_conditionStateMap.findBestInfo(m_conditionStates, bits);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DModelDraw::W3DModelDraw(Thing *thing, const ModuleData* moduleData) : DrawModule(thing, moduleData)
{
	int i;
	m_animationMode = RenderObjClass::ANIM_MODE_LOOP;
	m_hideHeadlights = true;
	m_hasHeadlights = false;
	m_headlightQueued = false;
	m_headlightPass = 0;
	m_pauseAnimation = false;
	m_curState = NULL;
	m_hexColor = 0;
	m_renderObject = NULL;
	m_shadow = NULL;
	m_contactShadow = NULL;
	m_shadowEnabled = TRUE;
	m_hasModelShadow = FALSE;
	m_terrainDecal = NULL;
	m_trackRenderObject = NULL;
	m_whichAnimInCurState = -1;
	m_nextState = NULL;
	m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
	for (i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		m_weaponRecoilInfoVec[i].clear();
	}
	for (i = 0; i < MAX_TURRETS; ++i)
		m_drawnTurretPitch[i] = 0.0f;
	m_needRecalcBoneParticleSystems = false;
	m_fullyObscuredByShroud = false;
	m_groundMotionPos.zero();
	m_groundMotionAngle = 0.0f;
	m_groundMotionFrame = 0;

	ModelConditionFlags emptyFlags;
	const ModelConditionInfo* info = findBestInfo(emptyFlags);
	if (!info)
	{
		DEBUG_CRASH(("*** ASSET ERROR: all draw modules must have an IDLE state\n"));
		throw INI_INVALID_DATA;
	}

	Drawable* draw = getDrawable();

  if ( draw )
  {
	  Object* obj = draw->getObject();
	  if (obj)
	  {	
		  // this is the one path that reads the indicator colour straight off the object rather
		  // than being handed it by Drawable::setIndicatorColor, so the scheme is applied here too
		  if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
			  m_hexColor = clientColor( obj->getNightIndicatorColor() );
		  else
			  m_hexColor = clientColor( obj->getIndicatorColor() );
	  }

    // THE VAST MAJORITY OF THESE SHOULD BE TRUE
    if ( ! getW3DModelDrawModuleData()->m_receivesDynamicLights)
    {
      draw->setReceivesDynamicLights( FALSE );
		  DEBUG_LOG(("setReceivesDynamicLights = FALSE: %s\n", draw->getTemplate()->getName().str()));
    }
  }
}

//-------------------------------------------------------------------------------------------------
/** The pristine bone, turret and barrel caches are logic's, and filling them loads every state's
	model.  A drawable logic makes in a tick fills them here after the tick, and one the map load makes
	fills them here too, in a window where the caches count as logic's (isValidTimeToCalcLogicStuff),
	so they take the same values from the same unanimated models as they did in the constructor.
	Without that, the first fill could come from an on-demand setModelState inside a later tick with
	this machine's live render object, or from a bone query loading the model inside the tick: a
	campaign map loads outside GameLogic::update, and its garrisons and spawn points asked on frame 1.
	A map object can also set its own night and snow flags once it is made (Object.cpp, objectTime
	and objectWeather), so the map load validates the night and snow states as well. */
void W3DModelDraw::buildRenderObjects( Bool madeInTick )
{
	{
		const Bool loadingMap = TheGameLogic->isLoadingMap();
		LatchRestore<Bool> forLogic( s_fillingLogicCaches, s_fillingLogicCaches || madeInTick || loadingMap );
		// only validate the current time-of-day and weather conditions by default.
		getW3DModelDrawModuleData()->validateStuffForTimeAndWeather(getDrawable(),
												TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT,
												TheGlobalData->m_weather == WEATHER_SNOWY);
		// ponytail: loads every map object's night and snow models once; validate per object override if memory bites
		if (loadingMap)
			getW3DModelDrawModuleData()->validateStuffForTimeAndWeather(getDrawable(), TRUE, TRUE);
	}

	ModelConditionFlags emptyFlags;
	setModelState(findBestInfo(emptyFlags));
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DModelDraw::onDrawableBoundToObject(void)
{
	getW3DModelDrawModuleData()->validateStuffForTimeAndWeather(getDrawable(), 
											TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT, 
											TheGlobalData->m_weather == WEATHER_SNOWY);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DModelDraw::~W3DModelDraw(void)
{
	if (m_headlightQueued)
	{
		std::vector<W3DModelDraw*>::iterator it = std::find(s_headlightQueue.begin(), s_headlightQueue.end(), this);
		if (it != s_headlightQueue.end())
			s_headlightQueue.erase(it);
	}

	if (m_trackRenderObject && TheTerrainTracksRenderObjClassSystem)
	{	
		TheTerrainTracksRenderObjClassSystem->unbindTrack(m_trackRenderObject);
		m_trackRenderObject = NULL;
	}

	nukeCurrentRender(NULL);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::doStartOrStopParticleSys()
{
	Bool hidden = getDrawable()->isDrawableEffectivelyHidden() || m_fullyObscuredByShroud;

	for (std::vector<ParticleSysTrackerType>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
	//for (std::vector<ParticleSystemID>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
	{
		ParticleSystem *sys = TheParticleSystemManager->findParticleSystem((*it).id);
		if (sys != NULL) {
			// this can be NULL
			if (hidden) {
				sys->stop();
			} else {
				sys->start();
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setHidden(Bool hidden)
{
	if (m_renderObject)
		m_renderObject->Set_Hidden(hidden);

	if (m_shadow)
		m_shadow->enableShadowRender(!hidden);

	m_shadowEnabled = hidden;

	if (m_terrainDecal)
		m_terrainDecal->enableShadowRender(!hidden);
	
	if (m_trackRenderObject && hidden)
	{	const Coord3D* pos = getDrawable()->getPosition();
		m_trackRenderObject->addCapEdgeToTrack(pos->x,pos->y);
	}
	
	doStartOrStopParticleSys();
}

/**Free all data used by this model's shadow.  This is used to dynamically enable/disable shadows by the options screen*/
void W3DModelDraw::releaseShadows(void)	///< frees all shadow resources used by this module - used by Options screen.
{
	unregisterModelShadow();
	if (m_shadow)
		m_shadow->release();
	m_shadow = NULL;
	if (m_contactShadow)
		m_contactShadow->release();
	m_contactShadow = NULL;
}

/** Does this model contain any skinned (bone-deformed) geometry?  Infantry and other animated
characters do; vehicles and structures are built from rigid sub-meshes.  Only LOD 0 is checked - that
is the LOD the shadow system fetches its live meshes from. */
static Bool hasSkinnedGeometry(RenderObjClass *robj)
{
	if (robj == NULL || robj->Class_ID() != RenderObjClass::CLASSID_HLOD)
		return FALSE;

	HLodClass *hlod=(HLodClass *)robj;

	for (Int i=0; i<hlod->Get_Lod_Model_Count(0); i++)
	{
		RenderObjClass *lodModel=hlod->Peek_Lod_Model(0,i);

		if (lodModel && lodModel->Class_ID() == RenderObjClass::CLASSID_MESH &&
				((MeshClass *)lodModel)->Peek_Model()->Get_Flag(MeshGeometryClass::SKIN))
			return TRUE;
	}

	return FALSE;
}

/** Infantry ship with a flat blob decal because the old shadow system refused to build a volume out
of a skinned mesh.  It can now, so give them a real cast shadow that follows the pose.  Returns TRUE
when the type was changed, so the caller can fall back if no volume geometry could be built. */
static Bool promoteSkinShadowToVolume(RenderObjClass *robj, Shadow::ShadowTypeInfo *shadowInfo)
{
	if (shadowInfo->m_type != SHADOW_DECAL ||
			!TheGlobalData->m_useShadowVolumes || !TheGlobalData->m_useShadowVolumesForSkins ||
			!hasSkinnedGeometry(robj))
		return FALSE;

	shadowInfo->m_type = SHADOW_VOLUME;
	//m_sizeX is the decal's width for a decal but the sun's elevation angle for a volume -
	//feeding the decal size through would clamp the shadow to a nonsense length.
	shadowInfo->m_sizeX = 0.0f;
	shadowInfo->m_sizeY = 0.0f;
	return TRUE;
}

/** Fill in the shadow this template asks for.  A template that asks for nothing still gets one if it
is a projectile: a missile or a bomb with no shadow slides over the ground with nothing tying it to
the terrain.  There can be dozens in the air at once, so it is always the cheap projected blob and
never a volume; a zero decal size makes addShadow take the size from the model's own bounding box.
Returns FALSE when this drawable gets no shadow at all. */
static Bool fillShadowInfoFromTemplate(const ThingTemplate *tmplate, Shadow::ShadowTypeInfo *shadowInfo, Bool allowPropVolume = TRUE)
{
	if (tmplate->getShadowType() == SHADOW_NONE)
	{
		//a prop the art gave no shadow at all - a fence, a rubbish pile, a shrub - gets a real volume
		//one: it never moves, so the shadow is built once and then costs nothing to keep.  Clearing
		//allowPropVolume is the retry for a model with no usable shadow geometry: it drops to the blob.
		const Bool prop = TheGlobalData->m_shadowsForProps && tmplate->isKindOf(KINDOF_IMMOBILE) &&
												!tmplate->isKindOf(KINDOF_PROJECTILE);

		if (prop && allowPropVolume && TheGlobalData->m_useShadowVolumes)
		{
			shadowInfo->m_ShadowName[0]	= 0;
			shadowInfo->allowUpdates		= FALSE;
			shadowInfo->allowWorldAlign	= TRUE;
			shadowInfo->m_type					= SHADOW_VOLUME;
			shadowInfo->m_sizeX					= 0.0f;
			shadowInfo->m_sizeY					= 0.0f;
			shadowInfo->m_offsetX				= 0.0f;
			shadowInfo->m_offsetY				= 0.0f;
			return TRUE;
		}

		if (!prop && (!TheGlobalData->m_shadowsForProjectiles || !tmplate->isKindOf(KINDOF_PROJECTILE)))
			return FALSE;

		//most of these name no texture of their own, so ask for the round blob a sphere gets
		strcpy(shadowInfo->m_ShadowName, tmplate->getShadowTextureName().isEmpty() ? "shadow" : tmplate->getShadowTextureName().str());
		shadowInfo->allowUpdates		= FALSE;
		shadowInfo->allowWorldAlign	= TRUE;
		shadowInfo->m_type					= SHADOW_DECAL;
		shadowInfo->m_sizeX					= 0.0f;
		shadowInfo->m_sizeY					= 0.0f;
		shadowInfo->m_offsetX				= 0.0f;
		shadowInfo->m_offsetY				= 0.0f;
		return TRUE;
	}

	strcpy(shadowInfo->m_ShadowName, tmplate->getShadowTextureName().str());
	DEBUG_ASSERTCRASH(shadowInfo->m_ShadowName[0] != '\0', ("this should be validated in ThingTemplate now"));
	shadowInfo->allowUpdates		= FALSE;		//shadow image will never update
	shadowInfo->allowWorldAlign	= TRUE;	//shadow image will wrap around world objects
	shadowInfo->m_type					= (ShadowType)tmplate->getShadowType();
	shadowInfo->m_sizeX					= tmplate->getShadowSizeX();
	shadowInfo->m_sizeY					= tmplate->getShadowSizeY();
	shadowInfo->m_offsetX				= tmplate->getShadowOffsetX();
	shadowInfo->m_offsetY				= tmplate->getShadowOffsetY();
	return TRUE;
}

/** Is this one of the trees the map placed as a real object?

	Every palm in the game is one, and there are 278 of them on Golden Oasis alone.  They are drawn
	from their own render object and never reach W3DTreeBuffer, so the pass that lays a batched
	tree's triangles on the ground has nothing of theirs to lay, and all they ever had was the round
	decal their template asks for.  The tree buffer takes their meshes over instead.

	The test is the template's own words: shrubbery, fixed in place, asking for a decal.  Classic
	graphics keeps the decal, which is what a palm stood on in 2003. */
static Bool wantsModelShadow(const ThingTemplate *tmplate)
{
	return tmplate->getShadowType() == SHADOW_DECAL &&
					!TheGlobalData->m_classicGraphics &&
					TheGlobalData->m_shadowsForProps &&
					(TheGlobalData->m_useShadowDecals || TheGlobalData->m_useShadowVolumes) &&
					tmplate->isKindOf(KINDOF_SHRUBBERY) &&
					tmplate->isKindOf(KINDOF_IMMOBILE);
}

/** Hand this model's shadow to the tree buffer, and say whether it took it. */
Bool W3DModelDraw::registerModelShadow(void)
{
	unregisterModelShadow();

	Drawable *draw = getDrawable();
	if (draw == NULL || m_renderObject == NULL || TheTerrainRenderObject == NULL)
		return FALSE;

	const ThingTemplate *tmplate = draw->getTemplate();
	if (tmplate == NULL || !wantsModelShadow(tmplate))
		return FALSE;

	m_hasModelShadow = TheTerrainRenderObject->addModelShadow(draw->getID(), m_renderObject);
	return m_hasModelShadow;
}

/** Take it back: the model is changing, or the drawable is going away. */
void W3DModelDraw::unregisterModelShadow(void)
{
	if (!m_hasModelShadow)
		return;
	m_hasModelShadow = FALSE;
	Drawable *draw = getDrawable();
	if (draw && TheTerrainRenderObject)
		TheTerrainRenderObject->removeModelShadow(draw->getID());
}

/** Create shadow resources if not already present. This is used to dynamically enable/disable shadows by the options screen*/
void W3DModelDraw::allocateShadows(void)
{
	const ThingTemplate *tmplate=getDrawable()->getTemplate();

	//Check if we don't already have a shadow but need one for this type of model.
	//A tree the buffer casts for us wants no decal of its own under it.
	Shadow::ShadowTypeInfo shadowInfo;
	if (!registerModelShadow() &&
			m_shadow == NULL && m_renderObject && TheW3DShadowManager && fillShadowInfoFromTemplate(tmplate, &shadowInfo))
	{
		//a projectile's decal is ours rather than the template's - never trade it for a volume
		Bool promotedToVolume = tmplate->getShadowType() != SHADOW_NONE &&
														promoteSkinShadowToVolume(m_renderObject, &shadowInfo);
		//a prop we handed a volume the template never asked for falls back the same way
		if (tmplate->getShadowType() == SHADOW_NONE && shadowInfo.m_type == SHADOW_VOLUME)
			promotedToVolume = TRUE;

  		m_shadow = TheW3DShadowManager->addShadow(m_renderObject, &shadowInfo);

		if (m_shadow == NULL && promotedToVolume)
		{	//no usable shadow geometry in this model - fall back to the decal the template asked for
			fillShadowInfoFromTemplate(tmplate, &shadowInfo, FALSE);
			m_shadow = TheW3DShadowManager->addShadow(m_renderObject, &shadowInfo);
		}

		if (m_shadow)
		{	m_shadow->enableShadowInvisible(isShadowHiddenByShroud());
			if (m_renderObject->Is_Hidden() || !m_shadowEnabled)
				m_shadow->enableShadowRender(FALSE);
		}
	}

	allocateContactShadow();
}

// How far the patch reaches past the building's own box, and how dark its middle is.  A patch the
// size of the footprint stops exactly at the wall and reads as a second slab; a little past it, and
// soft, is what a corner traps in daylight.
#define CONTACT_SHADOW_SPREAD 1.25f
#define CONTACT_SHADOW_OPACITY 110

/** The patch under a structure's footprint.

		A building's own shadow falls to one side and leaves the ground where it stands as bright as
		the field around it, so the concrete slab reads as a sticker laid over the terrain rather than
		as something standing on it.  This is the darkening that a corner traps in daylight: a soft
		blob, the size of the building, sitting under it and kept there.  It is not the building's
		shadow and it does not move with the sun. */
void W3DModelDraw::allocateContactShadow(void)
{
	// The soft patch is for things that stand still and meet the ground over an area.  A tank has a
	// shadow and a contact patch would follow it around looking like a stain.
	const ThingTemplate *tmplate = getDrawable() ? getDrawable()->getTemplate() : NULL;
	if (m_contactShadow != NULL || m_renderObject == NULL || tmplate == NULL
			|| TheW3DShadowManager == NULL || !TheGlobalData->m_contactShadows || TheGlobalData->m_classicGraphics
			|| !tmplate->isKindOf( KINDOF_STRUCTURE ))
		return;

	AABoxClass box;
	m_renderObject->Get_Obj_Space_Bounding_Box( box );
	const Real width = 2.0f * box.Extent.X * CONTACT_SHADOW_SPREAD;
	const Real depth = 2.0f * box.Extent.Y * CONTACT_SHADOW_SPREAD;
	if (width < 1.0f || depth < 1.0f)
		return;

	Shadow::ShadowTypeInfo contactInfo;
	strcpy( contactInfo.m_ShadowName, "shadow" );
	contactInfo.allowUpdates		= FALSE;
	contactInfo.allowWorldAlign	= TRUE;
	contactInfo.m_type					= SHADOW_ALPHA_DECAL;
	contactInfo.m_sizeX					= width;
	contactInfo.m_sizeY					= depth;
	contactInfo.m_offsetX				= 0.0f;
	contactInfo.m_offsetY				= 0.0f;

	m_contactShadow = TheW3DShadowManager->addShadow( m_renderObject, &contactInfo );
	if (m_contactShadow)
	{
		m_contactShadow->setOpacity( CONTACT_SHADOW_OPACITY );
		m_contactShadow->enableShadowInvisible( isShadowHiddenByShroud() );
		if (m_renderObject->Is_Hidden() || !m_shadowEnabled)
			m_contactShadow->enableShadowRender( FALSE );
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setShadowsEnabled(Bool enable)
{
	if (m_shadow)
		m_shadow->enableShadowRender(enable);
	m_shadowEnabled = enable;
}

/**collect some stats about the rendering cost of this draw module */
#if defined(_DEBUG) || defined(_INTERNAL)	
void W3DModelDraw::getRenderCost(RenderCost & rc) const
{
	getRenderCostRecursive(rc,m_renderObject);
	if (m_shadow)
		m_shadow->getRenderCost(rc);
}
#endif //_DEBUG || _INTERNAL


/**recurse through sub-objs to collect stats about the rendering cost of this draw module */
#if defined(_DEBUG) || defined(_INTERNAL)	
void W3DModelDraw::getRenderCostRecursive(RenderCost & rc,RenderObjClass * robj) const 
{
	if (robj == NULL) return;

	// recurse through sub-objects
	for (int i=0; i<robj->Get_Num_Sub_Objects(); i++) {
		RenderObjClass * sub_obj = robj->Get_Sub_Object(i);
		getRenderCostRecursive(rc,sub_obj);
		REF_PTR_RELEASE(sub_obj);
	}

	// Only consider visible sub-objects.  Some vehicles have hidden headlight cones for example
	// that are not normally rendered.
	if (robj->Is_Not_Hidden_At_All()) {
		
		// collect stats from meshes
		if (robj->Class_ID() == RenderObjClass::CLASSID_MESH) {
			MeshClass * mesh = (MeshClass*)robj;
			MeshModelClass * model = mesh->Peek_Model();
			if (model != NULL)
			{	if (model->Get_Flag(MeshGeometryClass::SORT))
					rc.addSortedMeshes(1);
				if (model->Get_Flag(MeshGeometryClass::SKIN))
					rc.addSkinMeshes(1);
			}

			rc.addDrawCalls(mesh->Get_Draw_Call_Count());
		}
		
		// collect bone stats.
		const HTreeClass * htree = robj->Get_HTree();
		if (htree != NULL) {
			rc.addBones(htree->Num_Pivots());
		}
	}
}
#endif //_DEBUG || _INTERNAL

//-------------------------------------------------------------------------------------------------
/** Does this thing keep its shadow when the fog closes over it?

	Anything fixed in place does.  A building you have already walked past is still standing there and
	still standing in the same light, and the fog draws it from the snapshot the ghost took: leaving
	the shadow out gave a town in the fog rows of buildings sitting on flat ground with nothing under
	them.  Trees, walls and the rest of the scenery are the same case.

	Anything that moves does not, and this is not a detail: a shadow crawling across fogged ground
	is the position of a unit you are not allowed to see.

	What decides it is whether the snapshot is standing there for whoever's fog is drawn, not the
	shroud status.  Never-seen ground has no snapshot, and neither has a building an observer's
	newly picked player saw before his fog was being remembered: there the building is not drawn,
	and a kept shadow was a building-shaped hole in the light on empty ground. */
static Bool keepsShadowInFog(const Drawable *draw)
{
	if (draw == NULL || TheGlobalData->m_classicGraphics)
		return FALSE;

	const Object *obj = draw->getObject();
	if (obj == NULL)
		return FALSE;

	if (!obj->isKindOf(KINDOF_IMMOBILE) || obj->isKindOf(KINDOF_PROJECTILE))
		return FALSE;

	const PartitionData *partition = obj->friend_getPartitionData();
	if (partition == NULL || partition->getGhostObject() == NULL)
		return FALSE;

	const Int viewerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;
	return partition->getGhostObject()->hasSnapShot(viewerIndex);
}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::isShadowHiddenByShroud(void) const
{
	return m_fullyObscuredByShroud && !keepsShadowInFog(getDrawable());
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setFullyObscuredByShroud(Bool fullyObscured)
{
	const Bool changed = m_fullyObscuredByShroud != fullyObscured;
	m_fullyObscuredByShroud = fullyObscured;

	const Bool hideShadow = isShadowHiddenByShroud();
	if (m_shadow)
		m_shadow->enableShadowInvisible(hideShadow);
	//the patch under a building used to keep the value it was made with, so a building the
	//fog's owner has never seen still darkened the ground where it stands
	if (m_contactShadow)
		m_contactShadow->enableShadowInvisible(hideShadow);

	if (changed)
	{
		//the terrain decal is a marker, not a shadow: it goes with the thing it marks
		if (m_terrainDecal)
			m_terrainDecal->enableShadowInvisible(m_fullyObscuredByShroud);

		doStartOrStopParticleSys();
	}
}

//-------------------------------------------------------------------------------------------------
static Bool isAnimationComplete(RenderObjClass* r)
{
	if (r && r->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		HLodClass *hlod = (HLodClass*)r;
		return hlod->Is_Animation_Complete();
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::adjustAnimSpeedToMovementSpeed()
{
	// if the cur animation has a "distance covered" number, try to throttle
	// the animation rate to match our actual speed.
	Real dist = getCurAnimDistanceCovered();
	if (dist > 0.0f)
	{
		const Object *obj = getDrawable()->getObject();
		if (obj)
		{
			const PhysicsBehavior* physics = obj->getPhysics();
			if (physics)
			{
				Real speed = physics->getVelocityMagnitude();	// in dist/frame
				if (speed > 0.0f)
				{
					// if distance-covered is specified for this anim, adjust the frame rate
					// so it appears to sync with our unit speed
					Real desiredDurationInMsec = dist / speed * MSEC_PER_LOGICFRAME_REAL;
					setCurAnimDurationInMsec(desiredDurationInMsec);
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
Real W3DModelDraw::getHullHeadingLag() const
{
	return getDrawable()->getDrawnHeadingLag();
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::adjustTransformMtx(Matrix3D& mtx) const
{
	const W3DModelDrawModuleData* d = getW3DModelDrawModuleData();

#ifdef CACHE_ATTACH_BONE
	const Vector3* offset = d->getAttachToDrawableBoneOffset(getDrawable());
	if (offset)
	{
		Vector3 tmp = mtx.Rotate_Vector(*offset);
		mtx.Adjust_X_Translation(tmp.X);
		mtx.Adjust_Y_Translation(tmp.Y);
		mtx.Adjust_Z_Translation(tmp.Z);
	}
#else
	if (d->m_attachToDrawableBone.isNotEmpty())
	{
		// override the mtx in this case. yes, call drawable, since the bone in question is 
		// likely to be in another module!
		Matrix3D boneMtx;
		if (getDrawable()->getCurrentWorldspaceClientBonePositions(d->m_attachToDrawableBone.str(), boneMtx))
		{
			mtx = boneMtx;
		}
		else
		{
			DEBUG_LOG(("m_attachToDrawableBone %s not found\n",getW3DModelDrawModuleData()->m_attachToDrawableBone.str()));
		}
	}
#endif

	if (m_curState->m_flags & (1<<ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT))
	{
		const Object *obj = getDrawable()->getObject();
		if (obj)
		{
			Real pct = obj->getConstructionPercent();
			if (pct >= 0.0f)
			{
				Real height = obj->getGeometryInfo().getMaxHeightAbovePosition();
				mtx.Translate_Z(-height + (height * pct / 100.0f));
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::doDrawModule(const Matrix3D* transformMtx)
{
	// update whether or not we should be animating.
	setPauseAnimation( !getDrawable()->getShouldAnimate(getW3DModelDrawModuleData()->m_animationsRequirePower) );

	Matrix3D scaledTransform;
	if (getDrawable()->getInstanceScale() != 1.0f)
	{	
		// do custom scaling of the W3D model.
		scaledTransform = *transformMtx;
		scaledTransform.Scale(getDrawable()->getInstanceScale());
		transformMtx = &scaledTransform;
		if (m_renderObject)
			m_renderObject->Set_ObjectScale(getDrawable()->getInstanceScale());
	}

	if (isAnimationComplete(m_renderObject))
	{
		if (m_curState != NULL && m_nextState != NULL)
		{
			//DEBUG_LOG(("transition %s is complete\n",m_curState->m_description.str()));
			const ModelConditionInfo* nextState = m_nextState;
			UnsignedInt nextDuration = m_nextStateAnimLoopDuration;
			m_nextState = NULL;
			m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
			setModelState(nextState);
			if (nextDuration != NO_NEXT_DURATION)
			{
				//DEBUG_LOG(("restoring pending duration of %d frames\n",nextDuration));
				setAnimationLoopDuration(nextDuration);
			}
		}
		
		if (m_renderObject &&
					m_curState != NULL &&
					m_whichAnimInCurState != -1)
		{
			if (m_curState->m_animations[m_whichAnimInCurState].isIdleAnim())
			{
				//DEBUG_LOG(("randomly switching to new idle state!\n"));

				// state hasn't changed, if it's been awhile, switch the idle anim
				// (yes, that's right: pass curState for prevState)
				adjustAnimation(m_curState, -1.0);
			}
			else if (testFlagBit(m_curState->m_flags, RESTART_ANIM_WHEN_COMPLETE))
			{
				adjustAnimation(m_curState, -1.0);
			}
		}
	}

	adjustAnimSpeedToMovementSpeed();

	// set our position in the render object to our position of the drawable
	if (m_renderObject)
	{
		Matrix3D mtx = *transformMtx;
		adjustTransformMtx(mtx);
		m_renderObject->Set_Transform(mtx);
	}

	handleClientTurretPositioning();
	recalcBonesForClientParticleSystems();
  
  const W3DModelDrawModuleData *modData = getW3DModelDrawModuleData();
  if ( modData->m_particlesAttachedToAnimatedBones )
    updateBonesForClientParticleSystems();// LORENZEN ADDED THIS
                                          // IT REPOSITIONS PARTICLESYSTEMS TO TSTAY IN SYNC WITH ANIMATED BONES

  handleClientRecoil();

	// A vehicle on a night map lights what is in front of it (lightHeadlights).  Drawable::draw has
	// already returned for anything hidden, stealthed or under the shroud, so an enemy's lamps never
	// show where the enemy does not.  Rooftop lamps on buildings and aircraft lights stay beams only.
	s_headlightDrawn = TRUE;
	if (m_hasHeadlights && !m_hideHeadlights && m_renderObject && !m_renderObject->Is_Hidden()
		&& Direct3D11_Is_Active() && !TheGlobalData->m_classicGraphics)
	{
		const Drawable* draw = getDrawable();
		const Object* obj = draw->getObject();
		if (!draw->isKindOf(KINDOF_STRUCTURE) && !draw->isKindOf(KINDOF_AIRCRAFT)
			&& (obj == NULL || !obj->isEffectivelyDead()))
		{
			m_headlightPass = s_headlightPass;
			if (!m_headlightQueued)
			{
				m_headlightQueued = true;
				s_headlightQueue.push_back(this);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
const ModelConditionInfo* W3DModelDraw::findTransitionForSig(TransitionSig sig) const
{
	const TransitionMap& transitionMap = getW3DModelDrawModuleData()->m_transitionMap;
	TransitionMap::const_iterator it = transitionMap.find(sig);
	if (it != transitionMap.end())
	{
		return &(*it).second;
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------
Real W3DModelDraw::getCurrentAnimFraction() const
{
	if (m_curState != NULL
			&& isAnyMaintainFrameFlagSet(m_curState->m_flags)
			&& m_renderObject != NULL
			&& m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		float framenum, dummy;
		int mode, numFrames;

		HLodClass* hlod = (HLodClass*)m_renderObject;
		/*HAnimClass* anim =*/ hlod->Peek_Animation_And_Info(framenum, numFrames, mode, dummy);
		if (framenum < 0.0)
			return 0.0;
		else if (framenum >= numFrames)
			return 1.0;
		else
			return (Real)framenum / ((Real)numFrames - 1);
	}
	else
	{
		return -1.0;
	}
}

//-------------------------------------------------------------------------------------------------
/** Whether an animation moves a bone further than the model's bind pose reaches.  W3D culls a model
	* by its bind pose, and the cinematic transports fly their whole route in their bones: the plane
	* GLA01 ends on (CINE_CPlane) climbs 1400 units away from the runway spot it stands on, and dropped
	* out of the picture mid-air the moment the camera stopped looking at that spot. */
//-------------------------------------------------------------------------------------------------
static Bool animLeavesBindPose(HAnimClass *anim, RenderObjClass *robj)
{
	// A channel's translation is the bone's offset from its bind pose, and what this is for starts
	// there and flies hundreds of units off: a bone hung straight off the root, at its bind pose on
	// the first frame and past the sphere and the floor later.  Infantry park the props they are not
	// holding 2000 units away from the first frame on, and a building's doors and dishes swing a
	// little past its sphere; neither needs anything but the bind pose
	const HTreeClass *tree = robj->Get_HTree();
	if (tree == NULL)
		return FALSE;
	const Real MIN_FLIGHT = 200.0f;
	const Real reach = max(robj->Get_Bounding_Sphere().Radius, MIN_FLIGHT);
	const Int lastFrame = anim->Get_Num_Frames() - 1;
	// an animation can carry more pivots than the model it plays on: the Bomb Truck's UVBMBTRUK has 42
	// and its damaged model UVBMBTRUK_D 41
	const Int pivots = min(anim->Get_Num_Pivots(), tree->Num_Pivots());
	for (Int pivot = 1; pivot < pivots; ++pivot)
	{
		if (tree->Get_Parent_Index(pivot) != 0 || !anim->Is_Node_Motion_Present(pivot))
			continue;
		Vector3 first, middle, last;
		anim->Get_Translation(first, pivot, 0.0f);
		anim->Get_Translation(middle, pivot, lastFrame * 0.5f);
		anim->Get_Translation(last, pivot, (Real)lastFrame);
		if (first.Length() <= reach && (middle.Length() > reach || last.Length() > reach))
			return TRUE;
	}
	return FALSE;
}

// The render objects animLeavesBindPose said yes for: a handful at most, the cinematic transports.
// Kept per render object, since one drawable can carry several models
static std::vector<const RenderObjClass *> s_posedCullModels;

static void setPosedCull(const RenderObjClass *robj, Bool posed)
{
	std::vector<const RenderObjClass *>::iterator it = std::find(s_posedCullModels.begin(), s_posedCullModels.end(), robj);
	if (posed && it == s_posedCullModels.end())
		s_posedCullModels.push_back(robj);
	else if (!posed && it != s_posedCullModels.end())
		s_posedCullModels.erase(it);
}

//-------------------------------------------------------------------------------------------------
/** A culled model is not drawn, and drawing is what moves its animation on, so setting its transform
	* again (which marks the hierarchy stale) and asking for a bone poses it at the current frame
	* first. */
//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::getPosedBoundingSphere(RenderObjClass *robj, SphereClass &sphere)
{
	if (std::find(s_posedCullModels.begin(), s_posedCullModels.end(), robj) == s_posedCullModels.end())
		return FALSE;

	const Int parts = robj->Get_Num_Sub_Objects();
	if (parts == 0)
		return FALSE;

	const Matrix3D transform = robj->Get_Transform();
	robj->Set_Transform(transform);
	robj->Get_Bone_Transform(0);
	for (Int i = 0; i < parts; ++i)
	{
		RenderObjClass *part = robj->Get_Sub_Object(i);
		if (i == 0)
			sphere = part->Get_Bounding_Sphere();
		else
			sphere.Add_Sphere(part->Get_Bounding_Sphere());
		part->Release_Ref();
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::adjustAnimation(const ModelConditionInfo* prevState, Real prevAnimFraction)
{
	if (!m_curState)
		return;

	// a state without an animation, or one that moves nothing far, culls by the bind pose again
	if (m_renderObject)
		setPosedCull(m_renderObject, FALSE);

	// if the current state has m_animations associated, do the right thing
	Int numAnims = m_curState->m_animations.size();
	if (numAnims > 0)
	{
		if (numAnims == 1)
		{
			m_whichAnimInCurState = 0;
		}
		else if (prevState == m_curState)
		{
			// select an idle anim different than the currently playing one
			Int animToAvoid = m_whichAnimInCurState;
			while (m_whichAnimInCurState == animToAvoid)
				m_whichAnimInCurState = GameClientRandomValue(0, numAnims-1);
		}
		else
		{
			m_whichAnimInCurState = GameClientRandomValue(0, numAnims-1);
		}
		
		const W3DAnimationInfo& animInfo = m_curState->m_animations[m_whichAnimInCurState];
		
		HAnimClass* animHandle = animInfo.getAnimHandle();	// note that this now returns an ADDREFED handle, which must be released by the caller!
		if (m_renderObject && animHandle)
		{
			Int startFrame = 0;
			if (m_curState->m_mode == RenderObjClass::ANIM_MODE_ONCE_BACKWARDS ||
					m_curState->m_mode == RenderObjClass::ANIM_MODE_LOOP_BACKWARDS)
			{
				startFrame = animHandle->Get_Num_Frames()-1;
			}

			if (testFlagBit(m_curState->m_flags, RANDOMIZE_START_FRAME))
			{
				startFrame = GameClientRandomValue(0, animHandle->Get_Num_Frames()-1);
			}
			else if (testFlagBit(m_curState->m_flags, START_FRAME_FIRST))
			{
				startFrame = 0;
			}
			else if (testFlagBit(m_curState->m_flags, START_FRAME_LAST))
			{
				startFrame = animHandle->Get_Num_Frames()-1;
			}
			// order is important here: MAINTAIN_FRAME_ACROSS_STATES is overridden by the other bits, above.
			else if (isAnyMaintainFrameFlagSet(m_curState->m_flags) &&
					prevState &&
					prevState != m_curState &&
					isAnyMaintainFrameFlagSet(prevState->m_flags) &&
					isCommonMaintainFrameFlagSet(m_curState->m_flags, prevState->m_flags) &&
					prevAnimFraction >= 0.0)
			{
				// getCurrentAnimFraction is frame / (frames - 1), so this is its inverse. EA wrote
				// fraction * frames - 1, which put every carried-over animation a frame back (upstream #157)
				// rounded, since frame 5 of 11 comes back as 4.9999995
				startFrame = REAL_TO_INT(prevAnimFraction * (animHandle->Get_Num_Frames()-1) + 0.5f);
			}

			m_renderObject->Set_Animation(animHandle, startFrame, m_curState->m_mode);
			setPosedCull(m_renderObject, animLeavesBindPose(animHandle, m_renderObject));
			REF_PTR_RELEASE(animHandle);
			animHandle = NULL;

			if (m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD)
			{
				HLodClass *hlod = (HLodClass*)m_renderObject;
				Real factor = GameClientRandomValueReal( m_curState->m_animMinSpeedFactor, m_curState->m_animMaxSpeedFactor );
				hlod->Set_Animation_Frame_Rate_Multiplier( factor );
			}
		}

	}
	else
	{
		m_whichAnimInCurState = -1;
	}

}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::setCurAnimDurationInMsec(Real desiredDurationInMsec)
{
	if (m_renderObject && m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		HLodClass* hlod = (HLodClass*)m_renderObject;
		HAnimClass* anim = hlod->Peek_Animation();
		if (anim)
		{
			Real naturalDurationInMsec = anim->Get_Num_Frames() * 1000.0f / anim->Get_Frame_Rate();
			if (naturalDurationInMsec > 0.0f && desiredDurationInMsec > 0.0f)
			{
				Real multiplier = naturalDurationInMsec / desiredDurationInMsec;
				hlod->Set_Animation_Frame_Rate_Multiplier(multiplier);
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
/** How far the unit has driven along its own heading, and how far it has turned, since the last
	* call, read off the position and heading the logic left. A wheel or a tread turned by these
	* amounts keeps pace with the ground however many pictures a logic frame gets, and runs backwards
	* when the unit backs up. Returns how many logic frames went by, 0 between two of them. */
//-------------------------------------------------------------------------------------------------
UnsignedInt W3DModelDraw::stepGroundMotion(Real& forward, Real& turn)
{
	const Coord3D *pos = getDrawable()->getPosition();
	Real angle = getDrawable()->getOrientation();
	UnsignedInt now = TheGameLogic->getFrame();
	if (m_groundMotionFrame == 0)
	{
		m_groundMotionPos = *pos;
		m_groundMotionAngle = angle;
		m_groundMotionFrame = now;
	}

	forward = (pos->x - m_groundMotionPos.x) * Cos(angle) + (pos->y - m_groundMotionPos.y) * Sin(angle);
	turn = stdAngleDiff(angle, m_groundMotionAngle);
	UnsignedInt frames = now - m_groundMotionFrame;

	m_groundMotionPos = *pos;
	m_groundMotionAngle = angle;
	m_groundMotionFrame = now;
	return frames;
}

//-------------------------------------------------------------------------------------------------
Real W3DModelDraw::getCurAnimDistanceCovered() const
{
	if (m_curState != NULL && m_whichAnimInCurState >= 0)
	{
		const W3DAnimationInfo& animInfo = m_curState->m_animations[m_whichAnimInCurState];
	#if defined(_DEBUG) || defined(_INTERNAL)
		if (TheSkateDistOverride != 0.0f)
			return TheSkateDistOverride;
	#endif
		return animInfo.getDistanceCovered();
	}
	return 0.0f;
}

//-------------------------------------------------------------------------------------------------
/**
	Utility function to make it easier to recursively hide all objects connected to a certain bone.
	We will hide all objects connected to bones which are children of boneIdx
*/
static void doHideShowBoneSubObjs(Bool state, Int numSubObjects, Int boneIdx, RenderObjClass *fullObject, const HTreeClass *htree)
{
#if 1	//(gth) fixed and tested this version
	for (Int i=0; i < numSubObjects; i++) 
	{
		bool is_child = false;
		Int parentBoneIndex = fullObject->Get_Sub_Object_Bone_Index(0, i);
		
		while (parentBoneIndex != 0) 
		{
			parentBoneIndex = htree->Get_Parent_Index(parentBoneIndex);

			if (parentBoneIndex == boneIdx) 
			{
				is_child = true;
				break;
			}
		}

		if (is_child) 
		{
			RenderObjClass* childObject = fullObject->Get_Sub_Object(i);
			childObject->Set_Hidden(state);
			childObject->Release_Ref();
		}
	}
#endif
#if 0	//old slow version
	
  for (Int i=0; i < numSubObjects; i++)
  {	
  	Int childBoneIndex = fullObject->Get_Sub_Object_Bone_Index(0, i);
  	Int parentIndex = htree->Get_Parent_Index(childBoneIndex);
  	if (childBoneIndex == parentIndex)
  		continue;

  	if (parentIndex == boneIdx)	// this object has our subobject as parent so copy hide state
  	{	
  		RenderObjClass* childObject = fullObject->Get_Sub_Object(i);
  		// recurse down the hierarchy to hide all sub-children
  		doHideShowBoneSubObjs(state, numSubObjects, childBoneIndex, fullObject, htree);
		childObject->Set_Hidden(state);
  		childObject->Release_Ref();
  	}
  } 
#endif
}

//-------------------------------------------------------------------------------------------------
void ModelConditionInfo::WeaponBarrelInfo::setMuzzleFlashHidden(RenderObjClass *fullObject, Bool hide) const
{
	if (fullObject)
	{
		RenderObjClass* childObject = fullObject->Get_Sub_Object_On_Bone(0, m_muzzleFlashBone);
		if (childObject)
		{
			childObject->Set_Hidden(hide);
			childObject->Release_Ref();
		}
		else
		{
			DEBUG_CRASH(("*** ASSET ERROR: childObject %s not found in setMuzzleFlashHidden()\n",m_muzzleFlashBoneName.str()));
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::doHideShowSubObjs(const std::vector<ModelConditionInfo::HideShowSubObjInfo>* vec)
{
	if (!m_renderObject)
		return;

	if (!vec->empty())
	{
		for (std::vector<ModelConditionInfo::HideShowSubObjInfo>::const_iterator it = vec->begin(); it != vec->end(); ++it)
		{
			Int objIndex;
			RenderObjClass* subObj;

			if ((subObj = m_renderObject->Get_Sub_Object_By_Name(it->subObjName.str(), &objIndex)) != NULL)
			{
				subObj->Set_Hidden(it->hide);

				const HTreeClass *htree = m_renderObject->Get_HTree();
				if (htree)
				{	
					//get the bone of this subobject so we can hide all other child objects that use this bone
					//as a parent.
					Int boneIdx = m_renderObject->Get_Sub_Object_Bone_Index(0, objIndex);
					doHideShowBoneSubObjs(it->hide, m_renderObject->Get_Num_Sub_Objects(), boneIdx, m_renderObject, htree);
				}
				subObj->Release_Ref();
			}
			else
			{
				DEBUG_CRASH(("*** ASSET ERROR: SubObject %s not found (%s)!\n",it->subObjName.str(),getDrawable()->getTemplate()->getName().str()));
			}
		}
	}

	//Kris (added Aug 2002) 
	//This is really important as it allows a person to override modelcondition show/hide objects. Used by the A10 strike
	//September 2002 -- now used by the UpgradeSubObject system.
	if( !m_subObjectVec.empty() )
	{
		updateSubObjects();
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::stopClientParticleSystems() 
{
	for (std::vector<ParticleSysTrackerType>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
	//for (std::vector<ParticleSystemID>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
	{
		ParticleSystem *sys = TheParticleSystemManager->findParticleSystem((*it).id);
		if (sys != NULL) 
		{
			// this can be NULL
			sys->destroy();
		}
	}
	m_particleSystemIDs.clear();
}

//-------------------------------------------------------------------------------------------------
/*
	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME

	This function must not EVER do ANYTHING which can in any way, shape, or form
	affect GameLogic in any way; if it does, net desyncs will occur and we
	will all lose our jobs. This must remain pure-client-only, and ensure
	that nothing it does can be detected, even in read-only form, by GameLogic!

	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME
*/
void W3DModelDraw::handleClientTurretPositioning()
{
	if (!m_curState || !(m_curState->m_validStuff & ModelConditionInfo::TURRETS_VALID))
		return;

	for (int tslot = 0; tslot < MAX_TURRETS; ++tslot)
	{
		const ModelConditionInfo::TurretInfo& tur = m_curState->m_turrets[tslot];
		Real turretAngle = 0;
		Real turretPitch = 0;
		if (tur.m_turretAngleBone || tur.m_turretPitchBone)
		{
			const Object *obj = getDrawable()->getObject();
			if (obj)
			{
				const AIUpdateInterface* ai = obj->getAIUpdateInterface();
				// a turret aims from the logic heading, which the drawn hull of a tank or car trails
				if (ai && ai->getTurretRotAndPitch((WhichTurretType)tslot, &turretAngle, &turretPitch))
					turretAngle -= getHullHeadingLag();
			}

			// do turret, if any
			if (tur.m_turretAngleBone != 0)
			{
				if (m_curState)
					turretAngle += tur.m_turretArtAngle;
				Matrix3D turretXfrm(1);
				turretXfrm.Rotate_Z(turretAngle);
				if (m_renderObject)
				{
					m_renderObject->Capture_Bone( tur.m_turretAngleBone );
					m_renderObject->Control_Bone( tur.m_turretAngleBone, turretXfrm );	
				}
			}

			// do turret pitch, if any.  The pitch the fork's data gave a stabilized gun is the logic's aim:
			// under Classic Graphics the barrel stays as modelled, as EA's tanks had it
			if (tur.m_turretPitchBone != 0 && !(TheGlobalData->m_classicGraphics && getW3DModelDrawModuleData()->m_stabilizedTurretPitch))
			{
				if (m_curState)
					turretPitch += tur.m_turretArtPitch;
				// The logic aims from a hull that never tilts; the drawn one lies on the ground and rocks when
				// it fires. A stabilized gun takes that tilt along its own heading off, and the angle the barrel
				// is modelled at (BarrelArtPitch: a Crusader's mesh rises 4.4 degrees from its bone), so the
				// drawn barrel stands at the logic's pitch against level ground, down the line its shell flies.
				const W3DModelDrawModuleData* md = getW3DModelDrawModuleData();
				if (md->m_stabilizedTurretPitch && m_renderObject && obj)
				{
					Vector3 heading(cosf(turretAngle), sinf(turretAngle), 0.0f);
					Vector3 drawn = m_renderObject->Get_Transform().Rotate_Vector(heading);
					Vector3 logic = obj->getTransformMatrix()->Rotate_Vector(heading);
					turretPitch -= atan2f(drawn.Z, sqrtf(drawn.X * drawn.X + drawn.Y * drawn.Y))
						- atan2f(logic.Z, sqrtf(logic.X * logic.X + logic.Y * logic.Y));
					turretPitch -= md->m_barrelArtPitch;
				}
				m_drawnTurretPitch[tslot] = turretPitch;
				Matrix3D turretPitchXfrm(1);
				turretPitchXfrm.Rotate_Y(-turretPitch);
				if (m_renderObject)
				{
					m_renderObject->Capture_Bone( tur.m_turretPitchBone );
					m_renderObject->Control_Bone( tur.m_turretPitchBone, turretPitchXfrm );	
				}
			}
		}
	} // next tslot
}


//void W3DModelDraw::handleClientFlagPositioning()
//{
//	const ModelConditionInfo::TurretInfo& tur = m_curState->m_turrets[tslot];
//	Real turretAngle = TheGlobalData->m_downwindAngle;
//	if (tur.m_flagAngleBone )
//	{
//		Matrix3D turretXfrm(1);
//		turretXfrm.Rotate_Z(turretAngle);
//		if (m_renderObject)
//		{
//			m_renderObject->Capture_Bone( tur.m_turretAngleBone );
//			m_renderObject->Control_Bone( tur.m_turretAngleBone, turretXfrm );	
//		}
//	}
//}

//-------------------------------------------------------------------------------------------------
/*
	Note that, strictly speaking this code is WRONG, since it assumes it will get called every frame,
	but in fact, will only get called every frame that it is visible. In practice, this isn't a big problem,
	but...

	@todo fix me someday (srj)
*/
void W3DModelDraw::handleClientRecoil()
{
	if (!m_curState || !(m_curState->m_validStuff & ModelConditionInfo::BARRELS_VALID))
	{
		return;
	}

	const W3DModelDrawModuleData* d = getW3DModelDrawModuleData();

	// do recoil, if any
	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
	{
		if (!m_curState->m_hasRecoilBonesOrMuzzleFlashes[wslot])
			continue;

		const ModelConditionInfo::WeaponBarrelInfoVec& barrels = m_curState->m_weaponBarrelInfoVec[wslot];
		WeaponRecoilInfoVec& recoils = m_weaponRecoilInfoVec[wslot];
		Int count = barrels.size();
		Int recoilCount = recoils.size();
		DEBUG_ASSERTCRASH(count == recoilCount, ("Barrel count != recoil count!"));
		count = (count>recoilCount)?recoilCount:count;
		for (Int i = 0; i < count; ++i)
		{
			if (barrels[i].m_muzzleFlashBone != 0)
			{
				Bool hidden = recoils[i].m_state != WeaponRecoilInfo::RECOIL_START;
				//DEBUG_LOG(("adjust muzzleflash %08lx for Draw %08lx state %s to %d at frame %d\n",subObjToHide,this,m_curState->m_description.str(),hidden?1:0,TheGameLogic->getFrame()));
				barrels[i].setMuzzleFlashHidden(m_renderObject, hidden);
			}

			const Real TINY_RECOIL = ConvertRetailPerFrameRate(0.01f);
			if (barrels[i].m_recoilBone != 0)
			{
				switch (recoils[i].m_state )
				{
					case WeaponRecoilInfo::IDLE:
						// nothing
						break;

					case WeaponRecoilInfo::RECOIL_START:
					case WeaponRecoilInfo::RECOIL:
						recoils[i].m_shift += recoils[i].m_recoilRate;
						recoils[i].m_recoilRate *= d->m_recoilDamping;			// recoil decelerates
						if (recoils[i].m_shift >= d->m_maxRecoil)
						{
							recoils[i].m_shift = d->m_maxRecoil;
							recoils[i].m_state = WeaponRecoilInfo::SETTLE;
						}
						else if (fabs(recoils[i].m_recoilRate) < TINY_RECOIL)
						{
							recoils[i].m_state = WeaponRecoilInfo::SETTLE;
						}
						break;

					case WeaponRecoilInfo::SETTLE:
						recoils[i].m_shift -= d->m_recoilSettle;
						if (recoils[i].m_shift <= 0.0f)
						{
							recoils[i].m_shift = 0.0f;
							recoils[i].m_state = WeaponRecoilInfo::IDLE;
						}
						break;
				}

				Matrix3D gunXfrm;
				gunXfrm.Make_Identity();
				// A barrel that is also its turret's pitch bone (a Crusader's, a Battlemaster's: the art has
				// no other bone to pitch) keeps the pitch while it recoils, back along the pitched barrel.
				// This matrix replaces the bone's whole control, so without it every shot snapped it level.
				for (Int tslot = 0; tslot < MAX_TURRETS; ++tslot)
				{
					if (m_curState->m_turrets[tslot].m_turretPitchBone == barrels[i].m_recoilBone)
					{
						gunXfrm.Rotate_Y( -m_drawnTurretPitch[tslot] );
						break;
					}
				}
				gunXfrm.Translate_X( -recoils[i].m_shift );
				//DEBUG_ASSERTLOG(recoils[i].m_shift==0.0f,("adjust bone %d by %f\n",recoils[i].m_recoilBone,recoils[i].m_shift));

				if (m_renderObject)
				{
					m_renderObject->Capture_Bone( barrels[i].m_recoilBone );
					m_renderObject->Control_Bone( barrels[i].m_recoilBone, gunXfrm );
				}
			}
			else
			{
				recoils[i].m_state = WeaponRecoilInfo::IDLE;
				//DEBUG_LOG(("reset Draw %08lx state %08lx\n",this,m_curState));
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/*
	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME

	This function must not EVER do ANYTHING which can in any way, shape, or form
	affect GameLogic in any way; if it does, net desyncs will occur and we
	will all lose our jobs. This must remain pure-client-only, and ensure
	that nothing it does can be detected, even in read-only form, by GameLogic!

	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME
*/
void W3DModelDraw::recalcBonesForClientParticleSystems()
{
	if (m_needRecalcBoneParticleSystems)
	{
		const Drawable* drawable = getDrawable();
		if (drawable != NULL )
		{
		
			if( m_curState != NULL && drawable->testDrawableStatus( DRAWABLE_STATUS_NO_STATE_PARTICLES ) == FALSE ) 
			{
				for (std::vector<ParticleSysBoneInfo>::const_iterator it = m_curState->m_particleSysBones.begin(); it != m_curState->m_particleSysBones.end(); ++it)
				{
					ParticleSystem *sys = TheParticleSystemManager->createParticleSystem(it->particleSystemTemplate);
					if (sys != NULL) 
					{
						Coord3D pos;
						pos.zero();
						Real rotation = 0.0f;

						Int boneIndex = m_renderObject ? m_renderObject->Get_Bone_Index(it->boneName.str()) : 0;
						if (boneIndex != 0)
						{
							// ugh... kill the mtx so we get it in modelspace, not world space
							Matrix3D originalTransform = m_renderObject->Get_Transform();	// save the transform
							Matrix3D tmp(true);
							tmp.Scale(getDrawable()->getScale());
							m_renderObject->Set_Transform(tmp);					// set to identity transform

							const Matrix3D boneTransform = m_renderObject->Get_Bone_Transform(boneIndex);
							Vector3 vpos = boneTransform.Get_Translation();
							rotation = boneTransform.Get_Z_Rotation();

							m_renderObject->Set_Transform(originalTransform);					// restore it

							pos.x = vpos.X;
							pos.y = vpos.Y;
							pos.z = vpos.Z;
						}

						// got the bone position...
						sys->setPosition(&pos);

						// and the direction, so that the system is oriented like the bone is
						sys->rotateLocalTransformZ(rotation);

						// Attatch it to the object...
						sys->attachToDrawable(drawable);

						// important: mark it as do-not-save, since we'll just re-create it when we reload.
						sys->setSaveable(FALSE);

						if (drawable->isDrawableEffectivelyHidden() || m_fullyObscuredByShroud) 
						{
							sys->stop(); // don't start the systems for drawables that are hidden.
						}

						// store the particle system id so we can kill it later.

						ParticleSysTrackerType tracker;
						tracker.id = sys->getSystemID();
						tracker.boneIndex = boneIndex;

						m_particleSystemIDs.push_back( tracker );
					}
				}
			}
		}
		m_needRecalcBoneParticleSystems = false;
	}
}



//-------------------------------------------------------------------------------------------------
/*
	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME

	This function must not EVER do ANYTHING which can in any way, shape, or form
	affect GameLogic in any way; if it does, net desyncs will occur and we
	will all lose our jobs. This must remain pure-client-only, and ensure
	that nothing it does can be detected, even in read-only form, by GameLogic!

	DANGER WARNING READ ME
	DANGER WARNING READ ME
	DANGER WARNING READ ME
*/
Bool W3DModelDraw::updateBonesForClientParticleSystems()
{
	const Drawable* drawable = getDrawable();
	if (drawable != NULL && m_curState != NULL && m_renderObject != NULL ) 
	{

//		Matrix3D originalTransform = m_renderObject->Get_Transform();
//		Matrix3D tmp = originalTransform;
 //   Vector3 zeroTranslation(0,0,0);
  //  tmp.Set_Translation( zeroTranslation );
	//	tmp.Scale(drawable->getScale());
//		m_renderObject->Set_Transform(tmp);					
				
    

		for (std::vector<ParticleSysTrackerType>::const_iterator it = m_particleSystemIDs.begin(); it != m_particleSystemIDs.end(); ++it)
		{
			ParticleSystem *sys = TheParticleSystemManager->findParticleSystem((*it).id);
			Int boneIndex = (*it).boneIndex;
			if ( (sys != NULL) && (boneIndex != 0)  ) 
			{
    		const Matrix3D boneTransform = m_renderObject->Get_Bone_Transform(boneIndex);// just a little worried about state changes
        
        Vector3 vpos = boneTransform.Get_Translation();

        Coord3D pos;
				pos.x = vpos.X;
				pos.y = vpos.Y;
				pos.z = vpos.Z;

				sys->setPosition(&pos);

        Real orientation = boneTransform.Get_Z_Rotation();
				sys->rotateLocalTransformZ(orientation);
        
        sys->setLocalTransform(&boneTransform);
        sys->setSkipParentXfrm(true);

			}
		}// next praticle system


//  	m_renderObject->Set_Transform(originalTransform);	

	}// end if Drawable

	return TRUE;

}




//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setTerrainDecal(TerrainDecalType type)
{
	if (m_terrainDecal)
		m_terrainDecal->release();

	m_terrainDecal = NULL;

	if (type == TERRAIN_DECAL_NONE || type >= TERRAIN_DECAL_MAX)
		//turning off decals on this object. (or bad value.)
		return;

	const ThingTemplate *tmplate=getDrawable()->getTemplate();

	//create a new terrain decal
	Shadow::ShadowTypeInfo decalInfo;
	decalInfo.allowUpdates = FALSE;	//shadow image will never update
	decalInfo.allowWorldAlign = TRUE;	//shadow image will wrap around world objects
	decalInfo.m_type = SHADOW_ALPHA_DECAL;

	//decalInfo.m_type = SHADOW_ADDITIVE_DECAL;//temporary kluge to test graphics

	if (type == TERRAIN_DECAL_SHADOW_TEXTURE)
		strcpy(decalInfo.m_ShadowName,tmplate->getShadowTextureName().str());
	else
		strcpy(decalInfo.m_ShadowName,TerrainDecalTextureName[type]);
	decalInfo.m_sizeX = tmplate->getShadowSizeX();
	decalInfo.m_sizeY = tmplate->getShadowSizeY();
	decalInfo.m_offsetX = tmplate->getShadowOffsetX();
	decalInfo.m_offsetY = tmplate->getShadowOffsetY();
	if (TheProjectedShadowManager)
		m_terrainDecal = TheProjectedShadowManager->addDecal(m_renderObject,&decalInfo);
	if (m_terrainDecal)
	{	m_terrainDecal->enableShadowInvisible(m_fullyObscuredByShroud);
		m_terrainDecal->enableShadowRender(m_shadowEnabled);
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setTerrainDecalSize(Real x, Real y)
{
	if (m_terrainDecal)
	{
		m_terrainDecal->setSize(x,y);
	}
}
//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setTerrainDecalOpacity(Real o)
{
	if (m_terrainDecal)
	{
		m_terrainDecal->setOpacity((Int)(255.0f * o));
	}
}


//-------------------------------------------------------------------------------------------------
void W3DModelDraw::nukeCurrentRender(Matrix3D* xform)
{
	// this needs to be "dirtied" so that the new pausing of the animation will be triggered.
	m_pauseAnimation = false;

	// changing geometry, so we need to remove shadow if present
	unregisterModelShadow();
	if (m_shadow)
		m_shadow->release();
	m_shadow = NULL;
	if (m_contactShadow)
		m_contactShadow->release();
	m_contactShadow = NULL;

	if(m_terrainDecal)
		m_terrainDecal->release();
	m_terrainDecal = NULL;

	// remove existing render object from the scene
	if (m_renderObject)
	{
		// save the transform for the new model
		if (xform)
			*xform = m_renderObject->Get_Transform();
		W3DDisplay::m_3DScene->Remove_Render_Object(m_renderObject);
		setPosedCull(m_renderObject, FALSE);
		REF_PTR_RELEASE(m_renderObject);
		m_renderObject = NULL;
	}
	else
	{
		if (xform)
		{	
			*xform = *getDrawable()->getTransformMatrix();
		}
	}
}

//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)	//art wants to see buildings without flags as a test.
void W3DModelDraw::hideGarrisonFlags(Bool hide)
{
	if (!m_renderObject)
		return;

	Int objIndex;
	RenderObjClass* subObj;

	if ((subObj = m_renderObject->Get_Sub_Object_By_Name("POLE", &objIndex)) != NULL)
	{
		subObj->Set_Hidden(hide);

		const HTreeClass *htree = m_renderObject->Get_HTree();
		if (htree)
		{	
			//get the bone of this subobject so we can hide all other child objects that use this bone
			//as a parent.
			Int boneIdx = m_renderObject->Get_Sub_Object_Bone_Index(0, objIndex);
			doHideShowBoneSubObjs(hide, m_renderObject->Get_Num_Sub_Objects(), boneIdx, m_renderObject, htree);
		}
		subObj->Release_Ref();
	}
}
#endif

//-------------------------------------------------------------------------------------------------
/** Hides all subobjects which are headlights.  Used to disable lights on models during the day.*/
void W3DModelDraw::hideAllHeadlights(Bool hide)
{
	m_hasHeadlights = false;
	if (m_renderObject)
	{
		for (Int subObj = 0; subObj < m_renderObject->Get_Num_Sub_Objects(); subObj++)
		{
			RenderObjClass* test = m_renderObject->Get_Sub_Object(subObj);
			if (strstr(test->Get_Name(),"HEADLIGHT"))
			{
				test->Set_Hidden(hide);
				m_hasHeadlights = true;
			}
			test->Release_Ref();
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Where this model's headlights shine from and which way, out of its visible HEADLIGHT beam meshes:
	* the beam starts at the mesh's pivot and runs along whichever of its own axes reaches furthest
	* from it, +Y on 84 of the 95 beams in W3DZH.big and +X on the Avenger and the train.  The origin
	* is the middle of the mesh across that axis, so a mesh holding both lamps puts one light between
	* them.  Several meshes are averaged into one light.  FALSE when no beam is showing. */
Bool W3DModelDraw::headlightBeam(Vector3& origin, Vector3& direction, Real& reach) const
{
	origin.Set(0.0f, 0.0f, 0.0f);
	direction.Set(0.0f, 0.0f, 0.0f);
	reach = 0.0f;
	if (m_renderObject == NULL)
		return FALSE;
	Int beams = 0;
	for (Int subObj = 0; subObj < m_renderObject->Get_Num_Sub_Objects(); subObj++)
	{
		RenderObjClass* test = m_renderObject->Get_Sub_Object(subObj);
		if (!test->Is_Hidden() && strstr(test->Get_Name(), "HEADLIGHT"))
		{
			AABoxClass box;
			test->Get_Obj_Space_Bounding_Box(box);
			const Vector3 lo = box.Center - box.Extent;
			const Vector3 hi = box.Center + box.Extent;
			Int axis = 0;
			Real length = 0.0f;
			Real sign = 1.0f;
			for (Int a = 0; a < 3; ++a)
			{
				if (hi[a] > length) { length = hi[a]; axis = a; sign = 1.0f; }
				if (-lo[a] > length) { length = -lo[a]; axis = a; sign = -1.0f; }
			}
			// a lamp lens a unit long (the Thunderbolt's) lights nothing
			if (length > 5.0f)
			{
				Vector3 start = box.Center;
				start[axis] = (sign > 0.0f) ? lo[axis] : hi[axis];
				Vector3 local(0.0f, 0.0f, 0.0f);
				local[axis] = sign;
				const Matrix3D& tm = test->Get_Transform();
				Vector3 worldStart, worldWay;
				Matrix3D::Transform_Vector(tm, start, &worldStart);
				Matrix3D::Rotate_Vector(tm, local, &worldWay);
				origin += worldStart;
				direction += worldWay;
				if (length > reach)
					reach = length;
				++beams;
			}
		}
		test->Release_Ref();
	}
	if (beams == 0 || direction.Length2() < 1e-6f)
		return FALSE;
	origin /= (Real)beams;
	direction.Normalize();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// ponytail: one spot light per vehicle and a fixed slot count; per-lamp lights or a tiled light
// list if a convoy close to the camera ever needs more than HEADLIGHT_SLOTS.
void W3DModelDraw::lightHeadlights(const Vector3& cameraPosition)
{
	// a pass that drew the drawables drops whatever it did not find lit
	if (s_headlightDrawn)
	{
		size_t kept = 0;
		for (size_t i = 0; i < s_headlightQueue.size(); ++i)
		{
			W3DModelDraw* draw = s_headlightQueue[i];
			if (draw->m_headlightPass == s_headlightPass)
				s_headlightQueue[kept++] = draw;
			else
				draw->m_headlightQueued = false;
		}
		s_headlightQueue.resize(kept);
		s_headlightDrawn = FALSE;
		++s_headlightPass;
	}

	// Nearest the camera first, and only those the slots can hold get their beams worked out.  The
	// light only reaches the pixels of the Direct3D 11 frame's shadow-receiving programs, so the
	// Classic graphics setting (no sun's map) and -d3d9 keep the beam meshes alone.
	static std::vector< std::pair<Real, W3DModelDraw*> > nearest;
	nearest.clear();
	for (size_t i = 0; i < s_headlightQueue.size(); ++i)
	{
		W3DModelDraw* draw = s_headlightQueue[i];
		const Coord3D* pos = draw->getDrawable()->getPositionForDraw();
		const Real dx = pos->x - cameraPosition.X, dy = pos->y - cameraPosition.Y, dz = pos->z - cameraPosition.Z;
		nearest.push_back(std::make_pair(dx * dx + dy * dy + dz * dz, draw));
	}
	std::sort(nearest.begin(), nearest.end());

	// The light reaches two and a half beam meshes (the Humvee's is 39 units, so about 100), and its
	// cone opens 32 degrees either side of the beam, which takes in both lamps' spread.  The beam
	// meshes do not agree on their pitch (the Humvee's leans up), so the light keeps only their
	// heading and dips a fixed 11 degrees from it, onto the ground in front.
	const Real CONE_EDGE_COS = 0.85f;
	const Real REACH_PER_BEAM = 2.5f;
	const Real TILT_DOWN = 0.2f;

	float lights[HEADLIGHT_SLOTS][8];
	unsigned count = 0;
	// a model whose beams give no light does not take a slot from the next one out
	for (size_t i = 0; i < nearest.size() && count < HEADLIGHT_SLOTS; ++i)
	{
		Vector3 origin, direction;
		Real beam;
		if (!nearest[i].second->headlightBeam(origin, direction, beam))
			continue;
		direction.Z = 0.0f;
		if (direction.Length2() < 1e-4f)
			continue;	// a beam pointing straight up or down has no heading
		direction.Normalize();
		direction.Z = -TILT_DOWN;
		direction.Normalize();
		float* light = lights[count++];
		light[0] = origin.X; light[1] = origin.Y; light[2] = origin.Z;
		light[3] = WWMath::Clamp(beam * REACH_PER_BEAM, 60.0f, 200.0f);
		light[4] = direction.X; light[5] = direction.Y; light[6] = direction.Z;
		light[7] = CONE_EDGE_COS;
	}
	// A warm white lamp over the light the map's terrain is lit by: what a pixel the moon lit gains
	// per unit of the lamp's light, channel by channel.  Divided by each channel of the moon alone,
	// Dark Night's blue one (0.29, 0.39, 0.87 summed) turned the lit ground orange, so each channel
	// is taken halfway towards the moon's mean.  The floor keeps a near black map from turning a
	// lamp into a floodlight.
	const Real LAMP[3] = { 1.6f, 1.48f, 1.25f };
	const RGBColor& ambient = TheGlobalData->m_terrainAmbient[0];
	const RGBColor& diffuse = TheGlobalData->m_terrainDiffuse[0];
	const Real map[3] = { ambient.red + diffuse.red, ambient.green + diffuse.green, ambient.blue + diffuse.blue };
	const Real mean = (map[0] + map[1] + map[2]) / 3.0f;
	float gain[3];
	for (Int c = 0; c < 3; ++c)
	{
		const Real moon = sqrtf((map[c] > 0.15f ? map[c] : 0.15f) * (mean > 0.15f ? mean : 0.15f));
		gain[c] = LAMP[c] / moon;
	}
	Direct3D11_Set_Headlights(&lights[0][0], count, gain);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::hideAllMuzzleFlashes(const ModelConditionInfo* state, RenderObjClass* renderObject)
{
	if (!state || !renderObject)
		return;

	if (!(state->m_validStuff & ModelConditionInfo::BARRELS_VALID))
	{
		return;
	}

	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
	{
		if (!state->m_hasRecoilBonesOrMuzzleFlashes[wslot])
			continue;

		const ModelConditionInfo::WeaponBarrelInfoVec& barrels = state->m_weaponBarrelInfoVec[wslot];
		for (ModelConditionInfo::WeaponBarrelInfoVec::const_iterator it = barrels.begin(); it != barrels.end(); ++it)
		{
			if (it->m_muzzleFlashBone != 0)
			{
				it->setMuzzleFlashHidden(renderObject, true);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
static Bool turretNamesDiffer(const ModelConditionInfo* a, const ModelConditionInfo* b)
{
	if (!(a->m_validStuff & ModelConditionInfo::TURRETS_VALID) || !(b->m_validStuff & ModelConditionInfo::TURRETS_VALID))
	{
		return true;
	}
	for (int i = 0; i < MAX_TURRETS; ++i)
		if (a->m_turrets[i].m_turretAngleNameKey != b->m_turrets[i].m_turretAngleNameKey || 
				a->m_turrets[i].m_turretPitchNameKey != b->m_turrets[i].m_turretPitchNameKey)
			return true;

	return false;
}

//-------------------------------------------------------------------------------------------------
/** Change the model for this drawable. If color is non-zero, it will also apply team color to the
	model */
//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setModelState(const ModelConditionInfo* newState)
{
	DEBUG_ASSERTCRASH(newState, ("invalid state in W3DModelDraw::setModelState\n")); 

#ifdef DEBUG_OBJECT_ID_EXISTS
	if (getDrawable() && getDrawable()->getObject() && getDrawable()->getObject()->getID() == TheObjectIDToDebug)
	{
		DEBUG_LOG(("REQUEST switching to state %s for obj %s %d\n",newState->m_description.str(),getDrawable()->getObject()->getTemplate()->getName().str(),getDrawable()->getObject()->getID()));
	}
#endif
	const ModelConditionInfo* nextState = NULL;
	if (m_curState != NULL && newState != NULL)
	{ 
		// if the requested state is the current state (and nothing is pending),
		// or if the requested state is pending, just punt.
		//
		// note that if the requested state matches the current state AND
		// we also have a pending state, then we nuke both of 'em. This sounds
		// weird, but is deliberate: nextState and newState will always be "real" states (or null),
		// leaving us these possibilities:
		//
		// -- curState is a transition state
		//		--in this case, new state should take precedence (unless there's a wait-to-finish marking, 
		//		which would be handled by the else clause)
		//
		// -- curState is a real state, nextState is a real state with a wait-to-finish clause
		//		-- if curState == newState, presume that we should restart curState, thus punting thepending nextState
		//		-- if curState != newState, presume we should switch to it, thus punting curState and nextState
		// 
		// -- curState is a real state, nextState is a real state without a wait-to-finish clause
		//		-- should be impossible!

		if ((m_curState == newState && m_nextState == NULL) ||
				(m_curState != NULL && m_nextState == newState))
		{
#ifdef DEBUG_OBJECT_ID_EXISTS
			if (getDrawable() && getDrawable()->getObject() && getDrawable()->getObject()->getID() == TheObjectIDToDebug)
			{
				DEBUG_LOG(("IGNORE duplicate state %s for obj %s %d\n",newState->m_description.str(),getDrawable()->getObject()->getTemplate()->getName().str(),getDrawable()->getObject()->getID()));
			}
#endif
			// I don't think he'll be interested...
			// he's already got one, you see
			return;
		} 
		// check for allow-to-finish implicit transitions
		else if (newState != m_curState &&
							newState->m_allowToFinishKey != NAMEKEY_INVALID &&
							newState->m_allowToFinishKey == m_curState->m_transitionKey &&
							m_renderObject &&
							!isAnimationComplete(m_renderObject))
		{
#ifdef DEBUG_OBJECT_ID_EXISTS
			if (getDrawable() && getDrawable()->getObject() && getDrawable()->getObject()->getID() == TheObjectIDToDebug)
			{
				DEBUG_LOG(("ALLOW_TO_FINISH state %s for obj %s %d\n",newState->m_description.str(),getDrawable()->getObject()->getTemplate()->getName().str(),getDrawable()->getObject()->getID()));
			}
#endif
			m_nextState = newState;
			m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
			return;
		}
		// check for a normal->normal state transition
		else if (newState != m_curState && 
					m_curState->m_transitionKey != NAMEKEY_INVALID &&
					newState->m_transitionKey != NAMEKEY_INVALID)
		{
			TransitionSig sig = buildTransitionSig(m_curState->m_transitionKey, newState->m_transitionKey);
			const ModelConditionInfo* transState = findTransitionForSig(sig);
			if (transState != NULL)
			{
#ifdef DEBUG_OBJECT_ID_EXISTS
				if (getDrawable() && getDrawable()->getObject() && getDrawable()->getObject()->getID() == TheObjectIDToDebug)
				{
					DEBUG_LOG(("using TRANSITION state %s before requested state %s for obj %s %d\n",transState->m_description.str(),newState->m_description.str(),getDrawable()->getObject()->getTemplate()->getName().str(),getDrawable()->getObject()->getID()));
				}
#endif
				nextState = newState;
				newState = transState;
			}
		}
	}
	
	// get this here, before we change anything... we'll need it to pass to adjustAnimation (srj)
	Real prevAnimFraction = getCurrentAnimFraction();

	//
	// Set up any particle effects for the new model.  Also stop the old ones, note that we
	// will never allow the placement of new ones if the status bit is set that tells us
	// we should not automatically create particle systems for the model condition states
	//
	if( getDrawable()->testDrawableStatus( DRAWABLE_STATUS_NO_STATE_PARTICLES ) == FALSE )
		m_needRecalcBoneParticleSystems = true;

	// always stop particle systems now
	stopClientParticleSystems();

	// and always hide muzzle flashes, in case modelstate and model have crossed at the same time
	hideAllMuzzleFlashes(newState, m_renderObject);

	// note that different states might use the same model; for these, don't go thru the
	// expense of creating a new render-object. (exception: if color is changing, or subobjs are changing,
	// or a few other things...)
	if (m_curState == NULL || 
			newState->m_modelName != m_curState->m_modelName || 
			turretNamesDiffer(newState, m_curState)
			// srj sez: I'm not sure why we want to do the "hard stuff" if we have projectile bones; I think
			// it is a holdover from days gone by when bones were handled quite differently, rather than being cached.
			// but doing this hard stuff is a lot more work, and I think it's unnecessary, so let's remove this.
			//|| (newState->m_validStuff & ModelConditionInfo::HAS_PROJECTILE_BONES)
		)
	{
		Matrix3D transform;
		nukeCurrentRender(&transform);
		Drawable* draw = getDrawable();

		// create a new render object and set into drawable
		if (newState->m_modelName.isEmpty())
		{
			m_renderObject = NULL;
		}
		else
		{
			const W3DModelDrawModuleData* data = getW3DModelDrawModuleData();
			Bool swapsTexture = !data->m_replaceTextureOld.isEmpty();
			LOGIC_CENSUS( CENSUS_ASSET_LOAD );
			m_renderObject = W3DDisplay::m_assetManager->Create_Render_Obj(newState->m_modelName.str(), draw->getScale(), m_hexColor,
				swapsTexture ? data->m_replaceTextureOld.str() : NULL, swapsTexture ? data->m_replaceTextureNew.str() : NULL);
			DEBUG_ASSERTCRASH(m_renderObject, ("*** ASSET ERROR: Model %s not found!\n",newState->m_modelName.str()));
		}

		//BONEPOS_LOG(("validateStuff() from within W3DModelDraw::setModelState()\n"));
		//BONEPOS_DUMPREAL(draw->getScale());

		// inside a tick never the live model, as getPristineBonePositionsForConditionState asks: the render
		// may be posing it beside the tick, and its captured turret bones would go into the shared cache
		newState->validateStuff(TheGameLogic->isInGameLogicUpdate() ? NULL : m_renderObject, draw->getScale(),
			getW3DModelDrawModuleData()->m_extraPublicBones);
		// ensure that any muzzle flashes from the *new* state, start out hidden...
//		hideAllMuzzleFlashes(newState, m_renderObject);//moved to above
		rebuildWeaponRecoilInfo(newState);
		doHideShowSubObjs(&newState->m_hideShowVec);

#if defined(_DEBUG) || defined(_INTERNAL)	//art wants to see buildings without flags as a test.
		if (TheGlobalData->m_hideGarrisonFlags && draw->isKindOf(KINDOF_STRUCTURE))
			hideGarrisonFlags(TRUE);
#endif

		const ThingTemplate *tmplate = draw->getTemplate();

		// set up tracks, if not already set.
		if (m_renderObject &&
				TheGlobalData->m_makeTrackMarks &&  
				!m_trackRenderObject && 
				TheTerrainTracksRenderObjClassSystem != NULL && 
				!getW3DModelDrawModuleData()->m_trackFile.isEmpty())
		{
			m_trackRenderObject = TheTerrainTracksRenderObjClassSystem->bindTrack(m_renderObject, 1.0f*MAP_XY_FACTOR, getW3DModelDrawModuleData()->m_trackFile.str());
			if (draw && m_trackRenderObject)
				m_trackRenderObject->setOwnerDrawable(draw);
		}

		// set up shadows
		Shadow::ShadowTypeInfo shadowInfo;
		//a tree the buffer casts for us wants no decal of its own under it
		if (!registerModelShadow() &&
				m_renderObject && TheW3DShadowManager && fillShadowInfoFromTemplate(tmplate, &shadowInfo))
		{
			//a projectile's decal is ours rather than the template's - never trade it for a volume
			Bool promotedToVolume = tmplate->getShadowType() != SHADOW_NONE &&
															promoteSkinShadowToVolume(m_renderObject, &shadowInfo);
			//a prop we handed a volume the template never asked for falls back the same way
			if (tmplate->getShadowType() == SHADOW_NONE && shadowInfo.m_type == SHADOW_VOLUME)
				promotedToVolume = TRUE;

  			m_shadow = TheW3DShadowManager->addShadow(m_renderObject, &shadowInfo, draw);

			if (m_shadow == NULL && promotedToVolume)
			{	//no usable shadow geometry in this model - fall back to the decal the template asked for
				fillShadowInfoFromTemplate(tmplate, &shadowInfo, FALSE);
				m_shadow = TheW3DShadowManager->addShadow(m_renderObject, &shadowInfo, draw);
			}

			if (m_shadow)
			{	m_shadow->enableShadowInvisible(isShadowHiddenByShroud());
				m_shadow->enableShadowRender(m_shadowEnabled);
			}
		}

		if( m_renderObject )
		{
			// set collision type for render object.  Used by WW3D2 collision code.
			if (tmplate->isKindOf(KINDOF_SELECTABLE))  
			{
				m_renderObject->Set_Collision_Type( PICK_TYPE_SELECTABLE );
			}

			if( tmplate->isKindOf( KINDOF_SHRUBBERY ))
			{
				m_renderObject->Set_Collision_Type( PICK_TYPE_SHRUBBERY );
			}
			if( tmplate->isKindOf( KINDOF_MINE ))
			{
				m_renderObject->Set_Collision_Type( PICK_TYPE_MINES );
			}
			if( tmplate->isKindOf( KINDOF_FORCEATTACKABLE ))
			{
				m_renderObject->Set_Collision_Type( PICK_TYPE_FORCEATTACKABLE );
			}
			if( tmplate->isKindOf( KINDOF_CLICK_THROUGH ))
			{
				m_renderObject->Set_Collision_Type( 0 );
			}
			
			Object *obj = draw->getObject();
 			if( obj )
   		{

				// for non bridge objects we adjust some collision types
				if( obj->isKindOf( KINDOF_BRIDGE ) == FALSE &&
						obj->isKindOf( KINDOF_BRIDGE_TOWER ) == FALSE )
				{

					if( obj->isKindOf( KINDOF_STRUCTURE ) && draw->getModelConditionFlags().test( MODELCONDITION_RUBBLE ) )
					{
	 					//A dead building, -- don't allow the user to click on rubble! Treat it as a location instead.
	   				m_renderObject->Set_Collision_Type( 0 );
					}
					else if( obj->isEffectivelyDead() )
					{
 						//A dead object, -- don't allow the user to click on rubble/hulks! Treat it as a location instead.
	   				m_renderObject->Set_Collision_Type( 0 );
					}
				}
   		}

			// add render object to our scene
			W3DDisplay::m_3DScene->Add_Render_Object(m_renderObject);

			// tie in our drawable as the user data pointer in the render object
			m_renderObject->Set_User_Data(draw->getDrawableInfo());
	
			setTerrainDecal(draw->getTerrainDecalType());

			//We created a new render object so we need to preserve the visibility state
			//of the previous render object.
			if (draw->isDrawableEffectivelyHidden())
			{
				m_renderObject->Set_Hidden(TRUE);
				if (m_shadow)
					m_shadow->enableShadowRender(FALSE);
				m_shadowEnabled = FALSE;
			}
			
			//
			// set the transform for the new model to that we saved before, we do this so that the
			// model transition is smooth and will immediately appear at the same orientation and location
			// as the previous one
			//
			m_renderObject->Set_Transform(transform);
			onRenderObjRecreated(); 
		}
	} 
	else 
	{

		//BONEPOS_LOG(("validateStuff() from within W3DModelDraw::setModelState()\n"));
		//BONEPOS_DUMPREAL(getDrawable()->getScale());

		newState->validateStuff(TheGameLogic->isInGameLogicUpdate() ? NULL : m_renderObject, getDrawable()->getScale(),
			getW3DModelDrawModuleData()->m_extraPublicBones);	// the live model only outside a tick, as above
		rebuildWeaponRecoilInfo(newState);

		// ensure that any muzzle flashes from the *previous* state, are hidden...
//		hideAllMuzzleFlashes(m_curState, m_renderObject);// moved to above
		
		doHideShowSubObjs(&newState->m_hideShowVec);
	}
	hideAllHeadlights(m_hideHeadlights);

	const ModelConditionInfo* prevState = m_curState;	// save, to pass to adjustAnimation (could be null)
	m_curState = newState;
	m_nextState = nextState;
	m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
	adjustAnimation(prevState, prevAnimFraction);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::replaceModelConditionState(const ModelConditionFlags& c)
{
	m_hideHeadlights = c.test(MODELCONDITION_NIGHT) ? false : true;

	const ModelConditionInfo* info = findBestInfo(c);
	if (info)
		setModelState(info);

	hideAllHeadlights(m_hideHeadlights);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setSelectable(Bool selectable)
{
	// set collision type for render object.  Used by WW3D2 collision code.
	if( m_renderObject )
	{
		int current = m_renderObject->Get_Collision_Type();
		if (selectable) {
			current |= PICK_TYPE_SELECTABLE;
		} else {
			current &= ~PICK_TYPE_SELECTABLE;
		}
		m_renderObject->Set_Collision_Type(current);
	}  // end if
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::replaceIndicatorColor(Color color)
{
	if (!getW3DModelDrawModuleData()->m_okToChangeModelColor)
		return;

	if (getRenderObject())
	{	
		Int newColor = (color == 0) ? 0 : (color | 0xFF000000);
		if (newColor != m_hexColor)
		{
			m_hexColor = newColor;

			// set these to NULL to force a regen of everything in setModelState.
			const ModelConditionInfo* tmp = m_curState;
			m_curState = NULL;
			m_nextState = NULL;
			m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
			setModelState(tmp);
		}
	}
}

//-------------------------------------------------------------------------------------------------
// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
Bool W3DModelDraw::clientOnly_getRenderObjInfo(Coord3D* pos, Real* boundingSphereRadius, Matrix3D* transform) const
{
	if (!m_renderObject)
		return false;

	Vector3 objPos = m_renderObject->Get_Position();	//get position of object
	pos->x = objPos.X;
	pos->y = objPos.Y;
	pos->z = objPos.Z;

	*transform = m_renderObject->Get_Transform();
	*boundingSphereRadius = m_renderObject->Get_Bounding_Sphere().Radius;

	return true;
}

static ModelConditionFlags logicConditionFlags(const Drawable* draw, const ModelConditionFlags& c);

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::getProjectileLaunchOffset(
	const ModelConditionFlags& condition, 
	WeaponSlotType wslot, 
	Int specificBarrelToUse, 
	Matrix3D* launchPos, 
	WhichTurretType tur, 
	Coord3D* turretRotPos, 
	Coord3D* turretPitchPos
) const
{
	/*
		Note, this recalcs the state every time, rather than using m_curState,
		because m_curState could be a transition state, and we really need
		to get the pristine bone(s) for the state that logic believes to be current,
		not the one the client might currently be using...
	*/
	const ModelConditionInfo* stateToUse = findBestInfo(logicConditionFlags(getDrawable(), condition));
	if (!stateToUse)
	{
		CRCDEBUG_LOG(("can't find best info\n"));
		//BONEPOS_LOG(("can't find best info\n"));
		return false;
	}
#if defined(_DEBUG) || defined(_INTERNAL)
	CRCDEBUG_LOG(("W3DModelDraw::getProjectileLaunchOffset() for %s\n",
		stateToUse->getDescription().str()));
#endif

#ifdef INTENSE_DEBUG
	AsciiString flags;
	for (Int i=0; i<condition.size(); ++i)
	{
		if (condition.test(i))
		{
			if (flags.isNotEmpty())
				flags.concat(',');
			flags.concat(condition.getNameFromSingleBit(i));
		}
	}
#endif // INTENSE_DEBUG

	const W3DModelDrawModuleData* d = getW3DModelDrawModuleData();
	//CRCDEBUG_LOG(("validateStuffs() from within W3DModelDraw::getProjectileLaunchOffset()\n"));
	//DUMPREAL(getDrawable()->getScale());
	//BONEPOS_LOG(("validateStuffs() from within W3DModelDraw::getProjectileLaunchOffset()\n"));
	//BONEPOS_DUMPREAL(getDrawable()->getScale());
	stateToUse->validateStuff(NULL, getDrawable()->getScale(), d->m_extraPublicBones);

	DEBUG_ASSERTCRASH(stateToUse->m_transitionSig == NO_TRANSITION, 
		("It is never legal to getProjectileLaunchOffset from a Transition state (they vary on a per-client basis)... however, we can fix this (see srj)\n"));

	DEBUG_ASSERTCRASH(specificBarrelToUse >= 0, ("specificBarrelToUse should now always be explicit"));

#ifdef CACHE_ATTACH_BONE
#else
	Coord3D techOffset;
	techOffset.zero();
	// must use pristine bone here since the result is used by logic
	Matrix3D pivot;
	if (d->m_attachToDrawableBone.isNotEmpty() &&
			getDrawable()->getPristineBonePositions( d->m_attachToDrawableBone.str(), 0, NULL, &pivot, 1 ) == 1)
	{
		pivot.Pre_Rotate_Z(getDrawable()->getOrientation());
		techOffset.x = pivot.Get_X_Translation();
		techOffset.y = pivot.Get_Y_Translation();
		techOffset.z = pivot.Get_Z_Translation();
	}
#endif

	CRCDEBUG_LOG(("wslot = %d\n", wslot));
	const ModelConditionInfo::WeaponBarrelInfoVec& wbvec = stateToUse->m_weaponBarrelInfoVec[wslot];	
	if( wbvec.empty() )
	{
		// Can't find the launch pos, but they might still want the other info they asked for
		CRCDEBUG_LOG(("empty wbvec\n"));
		//BONEPOS_LOG(("empty wbvec\n"));
		launchPos = NULL;
	}
	else
	{
		if (specificBarrelToUse < 0 || specificBarrelToUse >= wbvec.size())
			specificBarrelToUse = 0;

		if (launchPos)
		{
			CRCDEBUG_LOG(("specificBarrelToUse = %d\n", specificBarrelToUse));
			*launchPos = wbvec[specificBarrelToUse].m_projectileOffsetMtx;

			// The art angle turns a launch bone, posed the way the turret's art faces, to the front. With
			// no launch bone the matrix is the object's own, facing the front already, and turned by the
			// art it faced backward, so the turret's pitch tipped it down: the battleship's front guns
			// fired 12 degrees under barrels drawn 12 degrees up. The bone turns about the turret's pivot,
			// as the drawn turret does; turned about the model's origin, a turret off the centre threw it
			// across: a technical's cannon shell left 17 in front of its gun.
			if (tur != TURRET_INVALID && wbvec[specificBarrelToUse].m_hasProjectileLaunchBone)
			{
				const ModelConditionInfo::TurretInfo& artTurret = stateToUse->m_turrets[tur];
				Coord3D artPivot;
				artPivot.zero();
				if (artTurret.m_turretAngleNameKey != NAMEKEY_INVALID)
					stateToUse->findPristineBonePos(artTurret.m_turretAngleNameKey, artPivot);
				launchPos->Adjust_Translation(Vector3(-artPivot.x, -artPivot.y, 0.0f));
				launchPos->Pre_Rotate_Z(artTurret.m_turretArtAngle);
				launchPos->Adjust_Translation(Vector3(artPivot.x, artPivot.y, 0.0f));
				launchPos->Pre_Rotate_Y(-artTurret.m_turretArtPitch);
			}
#ifdef CACHE_ATTACH_BONE
#else
			launchPos->Adjust_Translation( Vector3( techOffset.x, techOffset.y, techOffset.z ) );
#endif

			DUMPMATRIX3D(launchPos);
		}
	}

	if (turretRotPos) 
		turretRotPos->zero();
	if (turretPitchPos) 
		turretPitchPos->zero();

	if (tur != TURRET_INVALID)
	{
#ifdef CACHE_ATTACH_BONE
		const Vector3* offset = d->getAttachToDrawableBoneOffset(getDrawable());
#endif
		const ModelConditionInfo::TurretInfo& turInfo = stateToUse->m_turrets[tur]; 
		if (turretRotPos)
		{
			if (turInfo.m_turretAngleNameKey != NAMEKEY_INVALID &&
					!stateToUse->findPristineBonePos(turInfo.m_turretAngleNameKey, *turretRotPos))
			{
				DEBUG_CRASH(("*** ASSET ERROR: TurretBone %s not found!\n",KEYNAME(turInfo.m_turretAngleNameKey).str()));
			}
#ifdef CACHE_ATTACH_BONE
			if (offset)
			{
				turretRotPos->x += offset->X;
				turretRotPos->y += offset->Y;
				turretRotPos->z += offset->Z;
			}
#endif
		}
		if (turretPitchPos)
		{
			if (turInfo.m_turretPitchNameKey != NAMEKEY_INVALID &&
					!stateToUse->findPristineBonePos(turInfo.m_turretPitchNameKey, *turretPitchPos))
			{
				DEBUG_CRASH(("*** ASSET ERROR: TurretBone %s not found!\n",KEYNAME(turInfo.m_turretPitchNameKey).str()));
			}
#ifdef CACHE_ATTACH_BONE
			if (offset)
			{
				turretPitchPos->x += offset->X;
				turretPitchPos->y += offset->Y;
				turretPitchPos->z += offset->Z;
			}
#endif
		}
	}
	return launchPos != NULL;// return if LaunchPos is valid or not
}

//-------------------------------------------------------------------------------------------------
Int W3DModelDraw::getPristineBonePositionsForConditionState(
	const ModelConditionFlags& condition,
	const char* boneNamePrefix, 
	Int startIndex, 
	Coord3D* positions, 
	Matrix3D* transforms, 
	Int maxBones
) const
{
	/*
		Note, this recalcs the state every time, rather than using m_curState,
		because m_curState could be a transition state, and we really need
		to get the pristine bone(s) for the state that logic believes to be current,
		not the one the client might currently be using...
	*/
	const ModelConditionInfo* stateToUse = findBestInfo(logicConditionFlags(getDrawable(), condition));
	if (!stateToUse)
		return 0;

//	if (isValidTimeToCalcLogicStuff())
//	{
//		CRCDEBUG_LOG(("W3DModelDraw::getPristineBonePositionsForConditionState() - state = '%s'\n",
//			stateToUse->getDescription().str()));
//		//CRCDEBUG_LOG(("renderObject == NULL: %d\n", (stateToUse==m_curState)?(m_renderObject == NULL):1));
//	}

	//BONEPOS_LOG(("validateStuff() from within W3DModelDraw::getPristineBonePositionsForConditionState()\n"));
	//BONEPOS_DUMPREAL(getDrawable()->getScale());
	// we must call this every time we set m_nextState, to ensure cached bones are happy
	stateToUse->validateStuff(
		// never the current render object, the way getProjectileLaunchOffset and getBarrelCount ask:
		// whether m_curState has caught up with the logic's state inside a tick depends on this
		// machine's view (on-demand flushes behind visibility and ghosts), and a render object kept
		// from the state before can carry that state's animation into the cached pristine bones
		NULL,
		getDrawable()->getScale(),
		getW3DModelDrawModuleData()->m_extraPublicBones);

	const int MAX_BONE_GET = 64;
	Matrix3D tmpMtx[MAX_BONE_GET];

	if (maxBones > MAX_BONE_GET)
		maxBones = MAX_BONE_GET;

	if (transforms == NULL)
		transforms = tmpMtx;

	Int posCount = 0;
	Int endIndex = (startIndex == 0) ? 0 : 99;	
	char buffer[256];
	Int i;	// VC6 leaked it out to the copy-out loop below
	for (i = startIndex; i <= endIndex; ++i)
	{
		if (i == 0)
			strcpy(buffer, boneNamePrefix);
		else
			snprintf(buffer, ARRAY_SIZE(buffer), "%s%02d", boneNamePrefix, i);
		
		for (char *c = buffer; c && *c; ++c)
		{
			// convert to all-lowercase since that's how we filled in the map
			*c = tolower(*c);
		}

		const Matrix3D* mtx = stateToUse->findPristineBone(NAMEKEY(buffer), NULL);
		if (mtx)
		{
			transforms[posCount] = *mtx;
//			if (isValidTimeToCalcLogicStuff())
//			{
//				DUMPMATRIX3D(mtx);
//			}
		}
		else
		{
			//DEBUG_CRASH(("*** ASSET ERROR: Bone %s not found!\n",buffer));
			const Object *obj = getDrawable()->getObject();
			if (obj)
				transforms[posCount] = *obj->getTransformMatrix();
			else
				transforms[posCount].Make_Identity();
			break;
		}

		++posCount;
		if (posCount >= maxBones)
			break;
	}
	
	if (positions && transforms)
	{
		for (i = 0; i < posCount; ++i)
		{
			Vector3 pos = transforms[i].Get_Translation();
			positions[i].x = pos.X;
			positions[i].y = pos.Y;
			positions[i].z = pos.Z;
//			if (isValidTimeToCalcLogicStuff())
//			{
//				DUMPCOORD3D(&(positions[i]));
//			}
		}
	}

//	if (isValidTimeToCalcLogicStuff())
//	{
//		CRCDEBUG_LOG(("end of W3DModelDraw::getPristineBonePositionsForConditionState()\n"));
//	}

	return posCount;
}


//-------------------------------------------------------------------------------------------------
// (gth) C&C3 Added this accessor for the bounding box of a render object in a W3DModelDraw module
// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
Bool W3DModelDraw::clientOnly_getRenderObjBoundBox(OBBoxClass * boundbox) const
{
	if (!m_renderObject)
		return false;

	AABoxClass aabox;
	m_renderObject->Get_Obj_Space_Bounding_Box(aabox);

	Matrix3D tm = m_renderObject->Get_Transform();

	// build an OBB for this AAB,transform
	OBBoxClass box0(aabox.Center,aabox.Extent);
	OBBoxClass::Transform(tm,box0,boundbox);

	return true;
}


//-------------------------------------------------------------------------------------------------
// (gth) C&C3 Added this accessor for a bone transform in the render object
// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
Bool W3DModelDraw::clientOnly_getRenderObjBoneTransform(const AsciiString & boneName,Matrix3D * set_tm) const
{
	if (!m_renderObject) {
		return false;
	}

	int idx = m_renderObject->Get_Bone_Index(boneName.str());
	if (idx == 0) {
		set_tm->Make_Identity();
		return false;
	} else {
		*set_tm = m_renderObject->Get_Bone_Transform(idx);
		return true;
	}
}


//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::getCurrentWorldspaceClientBonePositions(const char* boneName, Matrix3D& transform) const
{
	if (!m_renderObject)
		return false;

	Int boneIndex = m_renderObject->Get_Bone_Index(boneName);
	if (boneIndex == 0)
		return false;

	transform = m_renderObject->Get_Bone_Transform(boneIndex);
	return true;
}

//-------------------------------------------------------------------------------------------------
Int W3DModelDraw::getCurrentBonePositions(
	const char* boneNamePrefix, 
	Int startIndex, 
	Coord3D* positions, 
	Matrix3D* transforms, 
	Int maxBones
) const
{
	const int MAX_BONE_GET = 64;
	Matrix3D tmpMtx[MAX_BONE_GET];

	if (maxBones > MAX_BONE_GET)
		maxBones = MAX_BONE_GET;

	if (transforms == NULL)
		transforms = tmpMtx;

	if( !m_renderObject )
		return 0;

	// ugh... kill the mtx so we get it in modelspace, not world space
	Matrix3D originalTransform = m_renderObject->Get_Transform();	// save the transform
// which is faster: slamming the xform (and invalidating the objects xforms)
// or inverting it ourself? gotta profile to see. (srj)
#define DO_INV
#ifdef DO_INV
	Matrix3D inverse;
	originalTransform.Get_Orthogonal_Inverse(inverse);
	inverse.Scale(getDrawable()->getScale());
#else
	Matrix3D tmp(true);
	tmp.Scale(getDrawable()->getScale());
	m_renderObject->Set_Transform(tmp);					// set to identity transform
#endif

	Int posCount = 0;
	Int endIndex = (startIndex == 0) ? 0 : 99;	
	char buffer[256];
	Int i;	// VC6 leaked it out to the copy-out loop below
	for (i = startIndex; i <= endIndex; ++i)
	{
		if (i == 0)
			strcpy(buffer, boneNamePrefix);
		else
			snprintf(buffer, ARRAY_SIZE(buffer), "%s%02d", boneNamePrefix, i);
		
		Int boneIndex = m_renderObject->Get_Bone_Index(buffer);
		if (boneIndex == 0)
			break;

		transforms[posCount] = m_renderObject->Get_Bone_Transform(boneIndex);
#ifdef DO_INV
		transforms[posCount].preMul(inverse);
#endif
		//DEBUG_ASSERTCRASH(!m_renderObject->Is_Bone_Captured(boneIndex), ("bone is captured!"));

		++posCount;
		if (posCount >= maxBones)
			break;
	}
	
	if (positions && transforms)
	{
		for (i = 0; i < posCount; ++i)
		{
			Vector3 pos = transforms[i].Get_Translation();
			positions[i].x = pos.X;
			positions[i].y = pos.Y;
			positions[i].z = pos.Z;
		}
	}

#ifdef DO_INV
#else
	m_renderObject->Set_Transform(originalTransform);					// restore it
#endif

	return posCount;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::reactToTransformChange( const Matrix3D* oldMtx, 
																					 const Coord3D* oldPos, 
																					 Real oldAngle )
{

	// set the position of our render object
	if( m_renderObject )
	{
		Matrix3D mtx = *getDrawable()->getTransformMatrix();
		adjustTransformMtx(mtx);
		m_renderObject->Set_Transform(mtx);
	}

	if (m_trackRenderObject) 
	{
		Object *obj = getDrawable()->getObject();
		const Coord3D* pos = getDrawable()->getPosition();

		// no object for a tank the tick destroyed: its last move reaches the track after the tick, as a
		// cap, since whether it was stealthed is gone with the object
		if ( m_fullyObscuredByShroud || obj == NULL || obj->testStatus( OBJECT_STATUS_STEALTHED ) == TRUE )
		{
				m_trackRenderObject->addCapEdgeToTrack(pos->x, pos->y);
		}
		else
		{
			if (obj && obj->isSignificantlyAboveTerrain())
			{
				m_trackRenderObject->setAirborne();
			} 
			m_trackRenderObject->addEdgeToTrack(pos->x, pos->y);
		}
	}
} 

//-------------------------------------------------------------------------------------------------
/** R1, smooth motion (W3DSmoothMotion.h): on the first render pass after a logic tick, the transform the
	render object holds now (doDrawModule's, or reactToTransformChange's for a model out of view) becomes
	the current one, and the tick's verdict - blend, or show it as it is - is decided and counted. */
void W3DModelDraw::smoothMotionCapture(UnsignedInt clientFrame, Bool marked)
{
	if (m_renderObject == NULL)
		return;
	m_smoothMotion.capture(m_renderObject->Get_Transform(), m_renderObject, m_renderObject->Is_Hidden() != 0, clientFrame,
		marked != FALSE);
}

//-------------------------------------------------------------------------------------------------
/** R1: just before the scene renders, show the render object `alpha` of the way from its previous logic
	transform to its current one, when this tick's verdict allows it. */
void W3DModelDraw::smoothMotionApply(Real alpha)
{
	if (m_renderObject == NULL || m_smoothMotion.Model != m_renderObject || m_smoothMotion.Snap != SMOOTH_BLENDED)
		return;
	// an unmoved model already holds its one transform: shown as blended, nothing set
	if (m_smoothMotion.Still)
	{
		m_smoothMotion.Applied = true;
		return;
	}
	// a whole tick on (a paused game, a pass that fell behind) is the current transform, which it holds
	if (alpha >= 1.0f)
		return;
	// the parts were taken at capture, so a picture only mixes them
	Matrix3D shown;
	SmoothMotion_Pose(m_smoothMotion.Prev, m_smoothMotion.Cur, m_smoothMotion.Parts, alpha, shown);
	m_renderObject->Set_Transform(shown);
	m_smoothMotion.Applied = true;
}

//-------------------------------------------------------------------------------------------------
/** R1: straight after the render, the logic transform goes back, so picking and anything the logic reads
	from the render object (ParticleUplinkCannonUpdate's bones) see exactly what they did before. */
void W3DModelDraw::smoothMotionRestore()
{
	if (!m_smoothMotion.Applied)
		return;
	m_smoothMotion.Applied = false;
	if (m_renderObject != NULL && m_smoothMotion.Model == m_renderObject && !m_smoothMotion.Still)
		m_renderObject->Set_Transform(m_smoothMotion.Cur);
}

//-------------------------------------------------------------------------------------------------
const ModelConditionInfo* W3DModelDraw::findBestInfo(const ModelConditionFlags& c) const
{
	return getW3DModelDrawModuleData()->findBestInfo(c);
}

//-------------------------------------------------------------------------------------------------
/** The flags a question from the logic is answered under.  GARRISONED is set per viewer:
	* GarrisonContain keeps it off a building full of undetected stealth units on every machine but
	* its owner's and allies'.  Bones, barrels and launch points picked under it came out different
	* per machine, so logic asks with the owner's answer, which is whether anyone is inside. */
//-------------------------------------------------------------------------------------------------
static ModelConditionFlags logicConditionFlags(const Drawable* draw, const ModelConditionFlags& c)
{
	ModelConditionFlags flags = c;
	const Object* obj = draw->getObject();
	const ContainModuleInterface* contain = obj ? obj->getContain() : NULL;
	if (contain && contain->isGarrisonable())
		flags.set(MODELCONDITION_GARRISONED, contain->getContainCount() > 0 ? 1 : 0);
	return flags;
}

//-------------------------------------------------------------------------------------------------
Int W3DModelDraw::getBarrelCount(WeaponSlotType wslot) const
{
	/*
		The weapon asks this, so it has to answer for the state logic believes to be current, the way
		getProjectileLaunchOffset does.  m_curState leaves a transition only when this drawable is drawn,
		and whether it is drawn is this machine's camera and fog.
	*/
	const ModelConditionInfo* stateToUse = findBestInfo(logicConditionFlags(getDrawable(), getDrawable()->getModelConditionFlags()));
	if (!stateToUse)
		return 0;
	stateToUse->validateStuff(NULL, getDrawable()->getScale(), getW3DModelDrawModuleData()->m_extraPublicBones);
	return (stateToUse->m_validStuff & ModelConditionInfo::BARRELS_VALID) ?
		stateToUse->m_weaponBarrelInfoVec[wslot].size() : 0;
}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::handleWeaponFireFX(WeaponSlotType wslot, Int specificBarrelToUse, const FXList* fxl, Real weaponSpeed, const Coord3D* victimPos, Real damageRadius)
{
	DEBUG_ASSERTCRASH(specificBarrelToUse >= 0, ("specificBarrelToUse should now always be explicit"));

	if (!m_curState || !(m_curState->m_validStuff & ModelConditionInfo::BARRELS_VALID))
		return false;

	const ModelConditionInfo::WeaponBarrelInfoVec& wbvec = m_curState->m_weaponBarrelInfoVec[wslot];
	if (wbvec.empty())
	{
		// don't do this... some other module of our drawable may have handled it.
		// just return false and let the caller sort it out.
		// FXList::doFXPos(fxl, getDrawable()->getPosition(), getDrawable()->getTransformMatrix(), weaponSpeed, victimPos);
		return false;
	}
	
	Bool handled = false;

	if (specificBarrelToUse < 0 || specificBarrelToUse > wbvec.size())
		specificBarrelToUse = 0;

	const ModelConditionInfo::WeaponBarrelInfo& info = wbvec[specificBarrelToUse];

	if (fxl)
	{
		if (info.m_fxBone && m_renderObject)
		{ 
			const Object *logicObject = getDrawable()->getObject();// This is slow, so store it
			if( ! m_renderObject->Is_Hidden() || (logicObject == NULL) )
			{
				// I can ask the drawable's bone position if I am not hidden (if I have no object I have no choice)
				Matrix3D mtx = m_renderObject->Get_Bone_Transform(info.m_fxBone);
				Coord3D pos;
				pos.x = mtx.Get_X_Translation();
				pos.y = mtx.Get_Y_Translation();
				pos.z = mtx.Get_Z_Translation();
				FXList::doFXPos(fxl, &pos, &mtx, weaponSpeed, victimPos, damageRadius);
			}
			else
			{
				// Else, I should just use my logic position for the effect placement.
				// Things in transports regularly fire from inside (hidden), so this is not weird.
				const Matrix3D *mtx;
				Coord3D pos;
				mtx = logicObject->getTransformMatrix();
				pos = *(logicObject->getPosition());
				/** @todo Once Firepoint bones are actually implemented, this matrix will become correct.
				// Unless of course they decide to not have the tracers come out of the windows, but rather go towards the target.
				// In that case, tracers will have to be rewritten to be "point towards secondary" instead of 
				// "point straight ahead" which assumes we are facing the target.
				*/
				FXList::doFXPos(fxl, &pos, mtx, weaponSpeed, victimPos, damageRadius);
			}

			handled = true;
		}
		else
		{
			DEBUG_LOG(("*** no FXBone found for a non-null FXL\n"));
		}
	}

	if (info.m_recoilBone || info.m_muzzleFlashBone)
	{
		//DEBUG_LOG(("START muzzleflash %08lx for Draw %08lx state %s at frame %d\n",info.m_muzzleFlashBone,this,m_curState->m_description.str(),TheGameLogic->getFrame()));
		WeaponRecoilInfo& recoil = m_weaponRecoilInfoVec[wslot][specificBarrelToUse];
		recoil.m_state = WeaponRecoilInfo::RECOIL_START;
		recoil.m_recoilRate = getW3DModelDrawModuleData()->m_initialRecoil;
		if (info.m_muzzleFlashBone != 0)
			info.setMuzzleFlashHidden(m_renderObject, false);
	}

	return handled;
} 

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setAnimationLoopDuration(UnsignedInt numFrames)
{
// this is never defined -- srj
#ifdef NO_DURATIONS_ON_TRANSITIONS
	if (m_curState != NULL && m_curState->m_transition != NO_TRANSITION && 
			m_nextState != NULL && m_nextState->m_transition == NO_TRANSITION)
	{
		DEBUG_LOG(("deferring pending duration of %d frames\n",numFrames));
		m_nextStateAnimLoopDuration = numFrames;
		return;
	}

	m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
	DEBUG_ASSERTCRASH(m_curState != NULL && m_curState->m_transition == NO_TRANSITION, ("Hmm, setAnimationLoopDuration on a transition state is probably not right... see srj"));
#else
	m_nextStateAnimLoopDuration = NO_NEXT_DURATION;
#endif
	Real desiredDurationInMsec = ceilf(numFrames * MSEC_PER_LOGICFRAME_REAL);
	setCurAnimDurationInMsec(desiredDurationInMsec);
}

//-------------------------------------------------------------------------------------------------
/**
	similar to the above, but assumes that the current state is a "ONCE",
	and is smart about transition states... if there is a transition state 
	"inbetween", it is included in the completion time.
*/
void W3DModelDraw::setAnimationCompletionTime(UnsignedInt numFrames)
{
	if (m_curState != NULL && m_curState->m_transitionSig != NO_TRANSITION && m_curState->m_animations.size() > 0 &&
			m_nextState != NULL && m_nextState->m_transitionSig == NO_TRANSITION && m_nextState->m_animations.size() > 0)
	{
		// we have a transition; split up the time suitably.
		// note that this is just a guess, and assumes that the states
		// have only one anim each (or, if multiple, that they are
		// all pretty close in natural duration)
		const Real t1 = m_curState->m_animations.front().getNaturalDurationInMsec();
		const Real t2 = m_nextState->m_animations.front().getNaturalDurationInMsec();
		UnsignedInt transTime = REAL_TO_INT_FLOOR((numFrames * t1) / (t1 + t2));
		setAnimationLoopDuration(transTime);
		m_nextStateAnimLoopDuration = numFrames - transTime;
	}
	else
	{
		setAnimationLoopDuration(numFrames);
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setAnimationFrame( int frame )
{
	if( m_renderObject && m_whichAnimInCurState >= 0 )
	{
		const W3DAnimationInfo& animInfo = m_curState->m_animations[ m_whichAnimInCurState ];
		HAnimClass* animHandle = animInfo.getAnimHandle();	// note that this now returns an ADDREFED handle, which must be released by the caller!
		m_renderObject->Set_Animation( animHandle, frame );
		REF_PTR_RELEASE(animHandle);
	}
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::setPauseAnimation(Bool pauseAnim)
{
	if (m_pauseAnimation == pauseAnim) 
	{
		return;
	}

	m_pauseAnimation = pauseAnim;

	if (m_renderObject && m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		float framenum, dummy;
		int mode, numFrames;

		HLodClass* hlod = (HLodClass*)m_renderObject;
		HAnimClass* anim = hlod->Peek_Animation_And_Info(framenum, numFrames, mode, dummy);
		if (anim)
		{
			if (m_pauseAnimation) 
			{
				m_animationMode = mode;
				hlod->Set_Animation(anim, framenum, RenderObjClass::ANIM_MODE_MANUAL);
			} 
			else 
			{
				hlod->Set_Animation(anim, framenum, m_animationMode);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
#ifdef ALLOW_ANIM_INQUIRIES
// srj sez: not sure if this is a good idea, for net sync reasons...
Real W3DModelDraw::getAnimationScrubScalar( void ) const
{
	return getCurAnimDistanceCovered();
}
#endif

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::rebuildWeaponRecoilInfo(const ModelConditionInfo* state) 
{
	Int wslot;

	if (state == NULL)
	{
		for (wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
		{
			m_weaponRecoilInfoVec[wslot].clear();
		}
	}
	else
	{
		for (wslot = 0; wslot < WEAPONSLOT_COUNT; ++wslot)
		{
			Int ncount = state->m_weaponBarrelInfoVec[wslot].size();
			if (m_weaponRecoilInfoVec[wslot].size() != ncount)
			{
				WeaponRecoilInfo tmp;
				m_weaponRecoilInfoVec[wslot].resize(ncount, tmp);
			}

			for (WeaponRecoilInfoVec::iterator it = m_weaponRecoilInfoVec[wslot].begin(); it != m_weaponRecoilInfoVec[wslot].end(); ++it)
			{
				it->clear();
			}
		}
	}
} 

//-------------------------------------------------------------------------------------------------
/** Preload any assets for the time of day requested */
//-------------------------------------------------------------------------------------------------
void W3DModelDraw::preloadAssets( TimeOfDay timeOfDay )
{
	const W3DModelDrawModuleData *modData = getW3DModelDrawModuleData();

	if( modData )
		modData->preloadAssets( timeOfDay, getDrawable()->getScale() );
}

//-------------------------------------------------------------------------------------------------
Bool W3DModelDraw::isVisible() const
{
	return (m_renderObject && m_renderObject->Is_Really_Visible());
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::updateProjectileClipStatus( UnsignedInt shotsRemaining, UnsignedInt maxShots, WeaponSlotType slot )
{
	if ((getW3DModelDrawModuleData()->m_projectileBoneFeedbackEnabledSlots & (1 << slot)) == 0)
		return; // Only if specified.

	doHideShowProjectileObjects( shotsRemaining, maxShots, slot );// Means effectively, show m of n.
}

//-------------------------------------------------------------------------------------------------
Int W3DModelDraw::getProjectileClipFeedbackSlots() const
{
	return getW3DModelDrawModuleData()->m_projectileBoneFeedbackEnabledSlots;
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::updateDrawModuleSupplyStatus( Int , Int )
{
	// This level only cares if you have anything or not: the CARRYING condition, which
	// Drawable::updateDrawableSupplyStatus sets at the call so that a logic tick sees it at once
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::doHideShowProjectileObjects( UnsignedInt showCount, UnsignedInt maxCount, WeaponSlotType slot )
{
	// Loop through the projectile bones, and start hiding off the front.  That is, 8,9 means to hide the first only.
	if (maxCount < showCount)
	{
		DEBUG_CRASH(("Someone is trying to show more projectiles than they have.") );
		return;
	}
	Int hideCount = maxCount - showCount;

	std::vector<ModelConditionInfo::HideShowSubObjInfo> showHideVector;
	ModelConditionInfo::HideShowSubObjInfo oneEntry;
	if (m_curState->m_weaponProjectileHideShowName[slot].isEmpty())
	{
		for( Int projectileIndex = 0; projectileIndex < maxCount; projectileIndex++ )
		{
			oneEntry.subObjName.format("%s%02d", m_curState->m_weaponProjectileLaunchBoneName[slot].str(), (projectileIndex + 1));
			oneEntry.hide = ((projectileIndex + 1) <= hideCount);
			showHideVector.push_back( oneEntry );
		}
	}
	else
	{
		oneEntry.subObjName = m_curState->m_weaponProjectileHideShowName[slot];
		oneEntry.hide = (0 < hideCount);
		showHideVector.push_back( oneEntry );
	}

	// Rather than duplicate recursive utility stuff, I will build a vector like those used by the main ShowHide setting
	doHideShowSubObjs(&showHideVector);
}

//-------------------------------------------------------------------------------------------------
void W3DModelDraw::updateSubObjects()
{
	if (!m_renderObject)
		return;

	if (!m_subObjectVec.empty())
	{
		for (std::vector<ModelConditionInfo::HideShowSubObjInfo>::const_iterator it = m_subObjectVec.begin(); it != m_subObjectVec.end(); ++it)
		{
			Int objIndex;
			RenderObjClass* subObj;

			if ((subObj = m_renderObject->Get_Sub_Object_By_Name(it->subObjName.str(), &objIndex)) != NULL)
			{
				subObj->Set_Hidden(it->hide);

				const HTreeClass *htree = m_renderObject->Get_HTree();
				if (htree)
				{	
					//get the bone of this subobject so we can hide all other child objects that use this bone
					//as a parent.
					Int boneIdx = m_renderObject->Get_Sub_Object_Bone_Index(0, objIndex);
					doHideShowBoneSubObjs( it->hide, m_renderObject->Get_Num_Sub_Objects(), boneIdx, m_renderObject, htree);
				}
				subObj->Release_Ref();
			}
			else
			{
				DEBUG_CRASH(("*** ASSET ERROR: SubObject %s not found (%s)!\n",it->subObjName.str(),getDrawable()->getTemplate()->getName().str()));
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DModelDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added animation frame (CBD)
	*/
// ------------------------------------------------------------------------------------------------
void W3DModelDraw::xfer( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// weapon recoil info vectors
	UnsignedByte recoilInfoCount;
	WeaponRecoilInfo weaponRecoilInfo;
	for( Int i = 0; i < WEAPONSLOT_COUNT; ++i )
	{

		// count of data here
		recoilInfoCount = m_weaponRecoilInfoVec[ i ].size();
		xfer->xferUnsignedByte( &recoilInfoCount );
		if( xfer->getXferMode() == XFER_SAVE )
		{

			WeaponRecoilInfoVec::const_iterator it;
			for( it = m_weaponRecoilInfoVec[ i ].begin(); it != m_weaponRecoilInfoVec[ i ].end(); ++it )
			{

				// state
				weaponRecoilInfo.m_state = (*it).m_state;
				xfer->xferUser( &weaponRecoilInfo.m_state, sizeof( WeaponRecoilInfo::RecoilState ) );

				// shift
				weaponRecoilInfo.m_shift = (*it).m_shift;
				xfer->xferReal( &weaponRecoilInfo.m_shift );

				// recoil rate
				weaponRecoilInfo.m_recoilRate = (*it).m_recoilRate;
				xfer->xferReal( &weaponRecoilInfo.m_recoilRate );

			}  // end for, it
			
		}  // end if, save
		else
		{

			// clear this list before loading
			m_weaponRecoilInfoVec[ i ].clear();

			// read each data item
			for( Int j = 0; j < recoilInfoCount; ++j )
			{

				// read state
				xfer->xferUser( &weaponRecoilInfo.m_state, sizeof( WeaponRecoilInfo::RecoilState ) );

				// read shift
				xfer->xferReal( &weaponRecoilInfo.m_shift );

				// read recoil rate
				xfer->xferReal( &weaponRecoilInfo.m_recoilRate );

				// stuff it in the vector
				m_weaponRecoilInfoVec[ i ].push_back( weaponRecoilInfo );

			}  // end for, j

		}  // end else, load

	}  // end for, i

	// sub object vector
	UnsignedByte subObjectCount = m_subObjectVec.size();
	xfer->xferUnsignedByte( &subObjectCount );
	ModelConditionInfo::HideShowSubObjInfo hideShowSubObjInfo;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		std::vector<ModelConditionInfo::HideShowSubObjInfo>::const_iterator it;
		for( it = m_subObjectVec.begin(); it != m_subObjectVec.end(); ++it )
		{

			// sub object name
			hideShowSubObjInfo.subObjName = (*it).subObjName;
			xfer->xferAsciiString( &hideShowSubObjInfo.subObjName );

			// hide
			hideShowSubObjInfo.hide = (*it).hide;
			xfer->xferBool( &hideShowSubObjInfo.hide );

		}  // end for, it

	}  // end if, save
	else
	{

		// the vector must be emtpy
		m_subObjectVec.clear();

		// read each data item
		for( UnsignedByte i = 0; i < subObjectCount; ++i )
		{

			// name
			xfer->xferAsciiString( &hideShowSubObjInfo.subObjName );

			// hide
			xfer->xferBool( &hideShowSubObjInfo.hide );

			// stuff in vector
			m_subObjectVec.push_back( hideShowSubObjInfo );

		}  // end for, i

	}  // end else, load

	// animation
	if( version >= 2 )
	{
		
		if( xfer->getXferMode() == XFER_SAVE )
		{
				// srj sez: don't save info for transition states, since we can't really
				// restore them effectively.
			if ( m_renderObject 
					&& m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD
					&& m_curState
					&& m_curState->m_transitionSig == NO_TRANSITION )
			{
				
				// cast to HLod
				HLodClass *hlod = (HLodClass*)m_renderObject;

				// get animation info
				Int mode, numFrames;
				Real frame, dummy;
				HAnimClass *anim = hlod->Peek_Animation_And_Info( frame, numFrames, mode, dummy );

				// animation data is present
				Bool present = anim ? TRUE : FALSE;
				xfer->xferBool( &present );

				// if animation data is present
				if( anim )
				{

					// xfer mode
					xfer->xferInt( &mode );

					//
					// xfer frame as a fraction (this will allow the animations to change
					// in future patches but will still be mostly correct)
					//
					Real percent = frame / INT_TO_REAL( anim->Get_Num_Frames()-1 );
					xfer->xferReal( &percent );

				}  // end if, anim

			}  // end if
			else
			{

				// animation data is *NOT* present
				Bool present = FALSE;
				xfer->xferBool( &present );

			}  // end else

		}  // end if, save
		else
		{

			// read animation present flag
			Bool present;
			xfer->xferBool( &present );

			if( present )
			{
				
				{
					// read mode
					Int mode;
					xfer->xferInt( &mode );		// note, this will be ignored
				}

				// read percent
				Real percent;
				xfer->xferReal( &percent );

				//
				// cast render object to HLod, if this is no longer possible we have read the
				// data already and can just ignore it
				//
				if( m_renderObject && m_renderObject->Class_ID() == RenderObjClass::CLASSID_HLOD )
				{
					HLodClass *hlod = (HLodClass *)m_renderObject;

					// get anim
					HAnimClass *anim = hlod->Peek_Animation();

					// set animation data
					if( anim )
					{

						// figure out frame number given percent written in file and total frames on anim
						Real frame = percent * INT_TO_REAL( anim->Get_Num_Frames()-1 );

						float dummy1, dummy2;
						int curMode, dummy3;
						hlod->Peek_Animation_And_Info(dummy1, dummy3, curMode, dummy2);
						
						// srj sez: do not change the animation mode. it's too risky, since if you change (say) a nonlooping
						// to a looping, something might break since it could rely on that anim terminating.

						// set animation data
						hlod->Set_Animation( anim, frame, curMode );

					}  // end if, anim

				}  // end if

			}  // end if

		}  // end else, load

	}  // end if, version with animation info

	// when loading, update the sub objects if we have any
	if( xfer->getXferMode() == XFER_LOAD && m_subObjectVec.empty() == FALSE )
		updateSubObjects();

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DModelDraw::loadPostProcess( void )
{

	// extend base class
	DrawModule::loadPostProcess();

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::crc( Xfer *x )
{
	xfer(x);
}

// ------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::xfer( Xfer *x )
{

	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	x->xferVersion( &version, currentVersion );

	for (ModelConditionVector::iterator it = m_conditionStates.begin(); it != m_conditionStates.end(); ++it)
	{
		ModelConditionInfo *info = &(*it);
		x->xferByte(&(info->m_validStuff));
#if defined(_DEBUG) || defined(_INTERNAL)
		x->xferAsciiString(&(info->m_description));
#endif
		if (info->m_validStuff)
		{
			for (PristineBoneInfoMap::iterator bit = info->m_pristineBones.begin(); bit != info->m_pristineBones.end(); ++bit)
			{
				PristineBoneInfo *bone = &(bit->second);
				x->xferInt(&(bone->boneIndex));
				x->xferUser(&(bone->mtx), sizeof(Matrix3D));
			}
			Int i;	// VC6 leaked it out to the weapon-slot loop below
			for (i=0; i<MAX_TURRETS; ++i)
			{
				x->xferInt(&(info->m_turrets[i].m_turretAngleBone));
				x->xferInt(&(info->m_turrets[i].m_turretPitchBone));
			}
			for (i=0; i<WEAPONSLOT_COUNT; ++i)
			{
				for (ModelConditionInfo::WeaponBarrelInfoVec::iterator wit = info->m_weaponBarrelInfoVec[i].begin(); wit != info->m_weaponBarrelInfoVec[i].end(); ++wit)
				{
					x->xferUser(&(wit->m_projectileOffsetMtx), sizeof(Matrix3D));
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void W3DModelDrawModuleData::loadPostProcess( void )
{
}

