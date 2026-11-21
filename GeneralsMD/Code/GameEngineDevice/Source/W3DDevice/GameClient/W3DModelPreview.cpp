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

// W3DModelPreview.cpp: Display::drawModelPreview on W3D, one unit model drawn live inside a
// rectangle of a shell screen (the skins screen).
//
// The 3D cursor is the precedent (W3DMouse::draw): a scene of its own and a camera of its own,
// rendered after the interface with the depth cleared, which the interface does not use.  The
// rectangle is an off-centre view plane over the whole screen (ModelPreviewFrame.h), since the
// camera's viewport never reaches Direct3D 11.  There is no scissor, so a turning model is framed by
// the cylinder its body sweeps about its own up axis, which it cannot leave at any turn, and a still
// (a card) by its body's box corners at its one turn (ModelPreviewFrame::fitPoints).  Rotors are
// hidden either way: nothing could clip their tips, and framing them shrank a Chinook to two thirds.
//
// The model is what a Drawable of the template draws in its default state: every W3DModelDraw kind
// of draw module the Drawable would make (the same MinLODRequired filter), each made by the asset
// manager with the skin and the house colour as W3DModelDraw makes it, with what that module hides
// hidden: the state's Hide/ShowSubObject lines, its muzzle flashes and every headlight beam, and its
// state's first animation looping (a Chinook's rotors).  It stands on a low octagonal steel plate with
// a line in the house colour, drawn as 2D triangles projected through the same camera before the
// model, so the model always stands over it.
//
// No GameLogic: no Object, no Drawable, nothing from the logic frame or its random stream.  The
// asset manager and the module data's state lookup are the render's to call (the tick parks the
// render before it touches either).

#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/ModelPreviewFrame.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/ModuleFactory.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/scene.h"
#include "WW3D2/camera.h"
#include "WW3D2/light.h"
#include "WW3D2/hlod.h"
#include "WW3D2/hanim.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/render2d.h"
#include "WW3D2/dx11runtime.h"

#include <math.h>
#include <string.h>
#include <vector>

namespace {

const float PREVIEW_TAN_HALF = 0.36f;			// about 40 degrees across the rectangle's narrower side
const float PREVIEW_ELEVATION = 0.38f;		// the eye about 22 degrees above the plate
const float PREVIEW_STILL_ELEVATION = 0.50f;	// a still from about 29 degrees, so the roof shows with the side
const float PREVIEW_HELD_SHARE = 0.94f;		// the body inside this share of the rectangle's half spans
const float PREVIEW_DIM = 0.6f;					// the light a look the account lacks is drawn under: dark enough to
																					// read as locked beside an owned one, light enough that Bronze, Gold
																					// and Emerald still tell apart (0.32 made the truck's five one grey)
const float PREVIEW_YAW = -0.70f;					// from the front right of a model facing +X at turn 0
const size_t PREVIEW_MOST_MODELS = 16;		// models kept for a screen that draws several at once

/* The preview's own light, the same every time: retail GameData.ini's afternoon object lights (sun,
	 sky fill, back fill), not the table the last map wrote, which a night or snow match leaves behind. */
struct PreviewLight
{
	float diffuse[3];
	float direction[3];
};
const float PREVIEW_AMBIENT[3] = { 56.0f / 255.0f, 52.0f / 255.0f, 44.0f / 255.0f };
const PreviewLight PREVIEW_LIGHTS[] =
{
	{ { 1.0f, 1.0f, 1.0f },																	{ -0.81f, 0.38f, -0.45f } },
	{ { 60.0f / 255.0f, 60.0f / 255.0f, 80.0f / 255.0f },	{ 0.79f, 0.62f, 0.0f } },
	{ { 30.0f / 255.0f, 30.0f / 255.0f, 20.0f / 255.0f },	{ 0.81f, -0.48f, -0.34f } },
};
const Int PREVIEW_LIGHT_COUNT = sizeof(PREVIEW_LIGHTS) / sizeof(PREVIEW_LIGHTS[0]);

struct PreviewModel
{
	AsciiString templateName;
	AsciiString state;								// the model condition it is shown in; empty for its default
	Int skin;
	Color house;
	std::vector<RenderObjClass *> parts;	// one a draw module; empty when the template has no model
	Vector3 center;										// the middle of what it sweeps turning: on its up axis, its box's middle height
	float radius;											// the sphere round that holding all of it at any turn
	float footprint;									// how far from the axis it reaches sideways
	float body;												// the plate's size: its body round the axis, long guns left out
	Bool drawn;												// one draw went through whole (Direct3D 11 compiles a new program off the frame)
	std::vector<Vector3> corners;			// the visible parts' box corners at the origin pose, rotor discs left out
	std::vector<RenderObjClass *> rotors;	// the rotors' own parts, held, hidden in a still
	float sweep;											// how far the corners reach from the up axis
	float lowZ, highZ;								// and how low and high they go
};

SimpleSceneClass *s_scene = NULL;
CameraClass *s_camera = NULL;
LightClass *s_lights[PREVIEW_LIGHT_COUNT] = { NULL };
std::vector<PreviewModel> s_models;				// most recently used last

Bool isNoModel(const AsciiString &name)
{
	return name.isEmpty() || name.compareNoCase("None") == 0;
}

// hide every part on a bone, every level of detail, as WeaponBarrelInfo::setMuzzleFlashHidden does
void hidePartsOnBone(RenderObjClass *robj, Int bone)
{
	if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		HLodClass *hlod = (HLodClass *)robj;
		for (Int lod = 0; lod < hlod->Get_LOD_Count(); ++lod)
			for (Int i = 0; i < hlod->Get_Lod_Model_Count(lod); ++i)
				if (hlod->Get_Lod_Model_Bone(lod, i) == bone)
					hlod->Peek_Lod_Model(lod, i)->Set_Hidden(true);
		return;
	}
	RenderObjClass *child = robj->Get_Sub_Object_On_Bone(0, bone);
	if (child)
	{
		child->Set_Hidden(true);
		child->Release_Ref();
	}
}

// what the real draw module hides on a new model: the state's own list, the muzzle flashes, the headlight beams
void hideLikeTheDrawModule(RenderObjClass *robj, const ModelConditionInfo *state)
{
	for (std::vector<ModelConditionInfo::HideShowSubObjInfo>::const_iterator it = state->m_hideShowVec.begin(); it != state->m_hideShowVec.end(); ++it)
		hideShowSubObject(robj, it->subObjName.str(), it->hide);

	// validateWeaponBarrelInfo's names (MUZZLEFX01 to 99, or the name alone), without its caches,
	// which are the tick's
	char buffer[256];
	for (Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot)
	{
		const AsciiString &flash = state->m_weaponMuzzleFlashName[slot];
		if (flash.isEmpty())
			continue;
		Int bone = robj->Get_Bone_Index(flash.str());
		if (bone > 0)
			hidePartsOnBone(robj, bone);
		for (Int i = 1; i <= 99; ++i)
		{
			snprintf(buffer, sizeof(buffer), "%s%02d", flash.str(), i);
			bone = robj->Get_Bone_Index(buffer);
			if (bone > 0)
				hidePartsOnBone(robj, bone);
		}
	}

	for (Int i = 0; i < robj->Get_Num_Sub_Objects(); ++i)
	{
		RenderObjClass *part = robj->Get_Sub_Object(i);
		if (strstr(part->Get_Name(), "HEADLIGHT"))
			part->Set_Hidden(true);
		part->Release_Ref();
	}
}

// one draw module's default state, made and dressed as W3DModelDraw makes it; NULL when it draws nothing
RenderObjClass *makePart(const ThingTemplate *tmpl, const ModelConditionInfo *state, Int skin, Color house)
{
	RenderObjClass *robj = W3DDisplay::m_assetManager->Create_Render_Obj(state->m_modelName.str(), tmpl->getAssetScale(), house, NULL, NULL, skin);
	if (robj == NULL)
		return NULL;

	hideLikeTheDrawModule(robj, state);

	// the state's first animation, looping: a soldier's stance, a Chinook's rotors
	if (!state->m_animations.empty())
	{
		HAnimClass *anim = W3DDisplay::m_assetManager->Get_HAnim(state->m_animations.front().getName().str());
		if (anim)
		{
			robj->Set_Animation(anim, 0, RenderObjClass::ANIM_MODE_LOOP);
			anim->Release_Ref();
		}
	}

	// the top level of detail always: a new HLodClass holds its coarsest
	if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD)
		((HLodClass *)robj)->Set_LOD_Level(((HLodClass *)robj)->Get_LOD_Count() - 1);
	robj->Set_Ignore_LOD_Cost(true);
	robj->Set_Force_Visible(true);
	return robj;
}

PreviewModel makeModel(const AsciiString &templateName, const AsciiString &stateName, Int skin, Color house)
{
	PreviewModel m;
	m.templateName = templateName;
	m.state = stateName;
	m.skin = skin;
	m.house = house;
	m.center.Set(0, 0, 0);
	m.radius = 1.0f;
	m.footprint = 1.0f;
	m.body = 1.0f;
	m.drawn = FALSE;
	m.sweep = 1.0f;
	m.lowZ = 0.0f;
	m.highZ = 1.0f;

	const ThingTemplate *tmpl = TheThingFactory ? TheThingFactory->findTemplate(templateName, FALSE) : NULL;
	if (tmpl == NULL)
	{
		DEBUG_LOG(("MODELPREVIEW: no template '%s'\n", templateName.str()));
		return m;
	}

	// every draw module a Drawable of it would make (Drawable's constructor), the W3DModelDraw kinds drawn
	Vector3 lo(0, 0, 0), hi(0, 0, 0);
	std::vector<AABoxClass> shown;		// every visible part's own box
	std::vector<RenderObjClass *> shownParts;	// and the part, held; NULL for a model taken whole
	const ModuleInfo &mi = tmpl->getDrawModuleInfo();
	for (Int i = 0; i < mi.getCount(); ++i)
	{
		const ModuleData *mdd = mi.getNthData(i);
		if (mdd == NULL)
			continue;
		if (TheGlobalData->m_useDrawModuleLOD && TheGameLODManager
				&& mdd->getMinimumRequiredGameLOD() > TheGameLODManager->getStaticLODLevel())
			continue;
		const W3DModelDrawModuleData *md = mdd->getAsW3DModelDrawModuleData();
		if (md == NULL)
			continue;
		ModelConditionFlags flags;
		if (stateName.isNotEmpty() && !flags.setBitByName(stateName.str()))
			DEBUG_LOG(("MODELPREVIEW: no model condition '%s'\n", stateName.str()));
		const ModelConditionInfo *state = md->findBestInfo(flags);
		if (state == NULL || isNoModel(state->m_modelName))
			continue;
		RenderObjClass *robj = makePart(tmpl, state, skin, house);
		if (robj == NULL)
		{
			DEBUG_LOG(("MODELPREVIEW: '%s' has no model '%s'\n", templateName.str(), state->m_modelName.str()));
			continue;
		}

		// what shows, not the whole art: the model's box takes in its hidden parts too (the supply
		// truck's reaches 85 units forward, three times its body), so the visible parts' own boxes,
		// posed at the origin
		Vector3 partLo(0, 0, 0), partHi(0, 0, 0);
		Bool any = FALSE;
		robj->Set_Transform(Matrix3D(true));
		robj->Update_Sub_Object_Transforms();
		Bool skinned = FALSE;
		for (Int s = 0; s <= robj->Get_Num_Sub_Objects(); ++s)
		{
			RenderObjClass *sub = NULL;
			AABoxClass box;
			if (s < robj->Get_Num_Sub_Objects())
			{
				sub = robj->Get_Sub_Object(s);
				// a collision box (the worker's PICKBOX, 17 units across) is never drawn
				if (sub->Class_ID() == RenderObjClass::CLASSID_AABOX || sub->Class_ID() == RenderObjClass::CLASSID_OBBOX)
				{
					sub->Release_Ref();
					continue;
				}
				// A skin's box is its rest pose, wider than the stance it is drawn in (the worker's took 18
				// units across a man 4 wide), so a skin is framed by the bones below instead.
				if (sub->Class_ID() == RenderObjClass::CLASSID_MESH)
				{
					MeshModelClass *model = ((MeshClass *)sub)->Get_Model();
					const Bool skin = model && model->Get_Flag(MeshGeometryClass::SKIN);
					REF_PTR_RELEASE(model);
					if (skin)
					{
						skinned |= !sub->Is_Hidden();
						sub->Release_Ref();
						continue;
					}
				}
				box = sub->Get_Bounding_Box();
			}
			else if (skinned)
			{
				// the posed skeleton: every bone above the ground at the state's first frame, padded by a
				// limb's and a head's thickness, standing on the ground
				Vector3 bLo(0, 0, 0), bHi(0, 0, 0);
				Bool bones = FALSE;
				for (Int b = 1; b < robj->Get_Num_Bones(); ++b)
				{
					const Vector3 p = robj->Get_Bone_Transform(b).Get_Translation();
					if (p.Z < -200.0f)
						continue;		// a parked bone, as below
					if (!bones)
						bLo = bHi = p;
					bLo.Update_Min(p);
					bHi.Update_Max(p);
					bones = TRUE;
				}
				if (!bones)
					continue;
				bLo -= Vector3(1.2f, 1.2f, 0.0f);
				bHi += Vector3(1.2f, 1.2f, 1.5f);
				bLo.Z = WWMath::Min(bLo.Z, 0.0f);
				box.Init_Min_Max(bLo, bHi);
			}
			else
				continue;
			// a part whose animation buries it, as an infantryman's stance puts the crate he carries and
			// his mine detector 1392 units under the ground: drawn out of sight there, and framing it
			// shrank the worker to a speck.  Only a part parked that far down: one that hangs a little under
			// the origin is real and stays in the frame.
			// ponytail: a fixed depth, ten times a tall unit's height; a model with real parts that deep needs a relative test
			const Bool buried = box.Center.Z + box.Extent.Z < -200.0f;
			if (buried && sub && !sub->Is_Hidden())
				DEBUG_LOG(("MODELPREVIEW: '%s' part '%s' buried at %.1f, left out of the frame\n", templateName.str(),
					sub->Get_Name(), box.Center.Z + box.Extent.Z));
			if (!(sub && sub->Is_Hidden()) && !buried)
			{
				shown.push_back(box);
				if (sub)
					sub->Add_Ref();
				shownParts.push_back(sub);
				if (!any)
				{
					partLo = box.Center - box.Extent;
					partHi = box.Center + box.Extent;
					any = TRUE;
				}
				else
				{
					partLo.Update_Min(box.Center - box.Extent);
					partHi.Update_Max(box.Center + box.Extent);
				}
			}
			if (sub)
				sub->Release_Ref();
		}
		if (!any)
		{
			AABoxClass box;
			robj->Get_Obj_Space_Bounding_Box(box);
			partLo = box.Center - box.Extent;
			partHi = box.Center + box.Extent;
			shown.push_back(box);
			shownParts.push_back(NULL);
		}
		if (m.parts.empty())
		{
			lo = partLo;
			hi = partHi;
		}
		else
		{
			lo.Update_Min(partLo);
			hi.Update_Max(partHi);
		}
		m.parts.push_back(robj);
		DEBUG_LOG(("MODELPREVIEW: '%s' skin %d part '%s' box (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n", templateName.str(), skin,
			state->m_modelName.str(), partLo.X, partLo.Y, partLo.Z, partHi.X, partHi.Y, partHi.Z));
	}
	if (m.parts.empty())
	{
		DEBUG_LOG(("MODELPREVIEW: nothing to draw for '%s'\n", templateName.str()));
		return m;
	}

	// It turns about its own origin, the upright the game turns it about, so what it sweeps is a
	// cylinder round that axis: its box's farthest corner sideways, its box's height.  The bounding
	// sphere's centre sits well off the axis on a long barrel (a Crusader's 15 units forward), and
	// turning about that centre left the hull off its plate.
	const float farX = WWMath::Max(fabsf(lo.X), fabsf(hi.X));
	const float farY = WWMath::Max(fabsf(lo.Y), fabsf(hi.Y));
	m.footprint = WWMath::Max(sqrtf(farX * farX + farY * farY), 0.5f);
	const float halfHeight = 0.5f * (hi.Z - lo.Z);
	m.center.Set(0.0f, 0.0f, 0.5f * (hi.Z + lo.Z));
	m.radius = sqrtf(m.footprint * m.footprint + halfHeight * halfHeight);

	// The frame holds the body; a rotor is hidden in the preview: a part over twelve times wider than
	// it is tall and at least a quarter of the model's width is a disc (the Chinook's blades reach 20
	// units sideways of a narrow hull; a tank's track is about seven times wider than tall).
	// ponytail: a shape rule, not a name list; a flat wide part of a new model counts as a disc
	float widest = 0.0f;
	for (size_t i = 0; i < shown.size(); ++i)
		widest = WWMath::Max(widest, WWMath::Max(shown[i].Extent.X, shown[i].Extent.Y));
	Vector3 bodyLo(0, 0, 0), bodyHi(0, 0, 0);
	std::vector<size_t> thin;
	for (Int pass = 0; pass < 2 && m.corners.empty(); ++pass)
	{
		for (size_t i = 0; i < shown.size(); ++i)
		{
			const AABoxClass &box = shown[i];
			const float across = WWMath::Max(box.Extent.X, box.Extent.Y);
			if (pass == 0 && box.Extent.Z < 0.08f * across && across > 0.25f * widest)
			{
				thin.push_back(i);
				continue;
			}
			for (Int c = 0; c < 8; ++c)
			{
				const Vector3 corner(box.Center.X + ((c & 1) ? box.Extent.X : -box.Extent.X),
					box.Center.Y + ((c & 2) ? box.Extent.Y : -box.Extent.Y),
					box.Center.Z + ((c & 4) ? box.Extent.Z : -box.Extent.Z));
				if (m.corners.empty())
					bodyLo = bodyHi = corner;
				bodyLo.Update_Min(corner);
				bodyHi.Update_Max(corner);
				m.corners.push_back(corner);
			}
		}
		if (m.corners.empty())
			thin.clear();		// nothing but flat parts: frame them as the body
	}
	// a flat part in the body's top quarter is a rotor (the Chinook's two at 13.8 and 16.0 of 16.8);
	// one lower down (a plate on the Crusader at 6.2 of 11.3) stays out of the frame altogether
	for (size_t t = 0; t < thin.size(); ++t)
	{
		const AABoxClass &box = shown[thin[t]];
		const Bool rotor = box.Center.Z > bodyLo.Z + 0.75f * (bodyHi.Z - bodyLo.Z);
		DEBUG_LOG(("MODELPREVIEW: '%s' flat part at (%.1f,%.1f,%.1f) reach %.1f by %.1f, %s\n", templateName.str(),
			box.Center.X, box.Center.Y, box.Center.Z, box.Extent.X, box.Extent.Y, rotor ? "a rotor" : "left out"));
		if (!rotor)
			continue;
		if (shownParts[thin[t]])
		{
			shownParts[thin[t]]->Add_Ref();
			m.rotors.push_back(shownParts[thin[t]]);
		}
	}
	for (size_t i = 0; i < shownParts.size(); ++i)
		REF_PTR_RELEASE(shownParts[i]);
	m.sweep = 0.5f;
	for (size_t i = 0; i < m.corners.size(); ++i)
		m.sweep = WWMath::Max(m.sweep, sqrtf(m.corners[i].X * m.corners[i].X + m.corners[i].Y * m.corners[i].Y));
	m.lowZ = bodyLo.Z;
	m.highZ = bodyHi.Z;

	// the body round the axis: the box's nearer end each way, mirrored, which leaves out a gun
	// reaching far forward (the Crusader's box runs -15 to +45 on X, its hull about 15 each way)
	const float nearX = WWMath::Max(WWMath::Min(fabsf(bodyLo.X), fabsf(bodyHi.X)), 0.0f);
	const float nearY = WWMath::Max(WWMath::Min(fabsf(bodyLo.Y), fabsf(bodyHi.Y)), 0.0f);
	m.body = WWMath::Max(sqrtf(nearX * nearX + nearY * nearY), 0.25f * m.sweep);

	DEBUG_LOG(("MODELPREVIEW: '%s' skin %d, %d parts, swept radius %.1f, body %.1f, body sweep %.1f, body box (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f), %d discs\n",
		templateName.str(), skin, (Int)m.parts.size(), m.footprint, m.body, m.sweep,
		bodyLo.X, bodyLo.Y, bodyLo.Z, bodyHi.X, bodyHi.Y, bodyHi.Z, (Int)m.rotors.size()));
	return m;
}

void releaseModel(PreviewModel &m)
{
	for (size_t i = 0; i < m.rotors.size(); ++i)
		REF_PTR_RELEASE(m.rotors[i]);
	m.rotors.clear();
	for (size_t i = 0; i < m.parts.size(); ++i)
		REF_PTR_RELEASE(m.parts[i]);
	m.parts.clear();
}

PreviewModel *findModel(const AsciiString &templateName, const AsciiString &state, Int skin, Color house)
{
	for (size_t i = 0; i < s_models.size(); ++i)
	{
		if (s_models[i].skin == skin && s_models[i].house == house && s_models[i].templateName == templateName
			&& s_models[i].state == state)
		{
			if (i + 1 < s_models.size())
			{
				PreviewModel used = s_models[i];
				s_models.erase(s_models.begin() + i);
				s_models.push_back(used);
			}
			return &s_models.back();
		}
	}
	// ponytail: an evicted look's munged prototype and recoloured sheets stay until freeModelPreviews
	// (or the next reset) releases unused assets; bounded by the screen's looks in one house colour
	if (s_models.size() >= PREVIEW_MOST_MODELS)
	{
		releaseModel(s_models.front());
		s_models.erase(s_models.begin());
	}
	s_models.push_back(makeModel(templateName, state, skin, house));
	return &s_models.back();
}

void makeScene()
{
	if (s_scene)
		return;
	s_scene = NEW_REF(SimpleSceneClass, ());
	s_camera = NEW_REF(CameraClass, ());

	s_scene->Set_Ambient_Light(Vector3(PREVIEW_AMBIENT[0], PREVIEW_AMBIENT[1], PREVIEW_AMBIENT[2]));
	for (Int i = 0; i < PREVIEW_LIGHT_COUNT; ++i)
	{
		const PreviewLight &l = PREVIEW_LIGHTS[i];
		s_lights[i] = NEW_REF(LightClass, (LightClass::DIRECTIONAL));
		s_lights[i]->Set_Ambient(Vector3(0, 0, 0));
		s_lights[i]->Set_Diffuse(Vector3(l.diffuse[0], l.diffuse[1], l.diffuse[2]));
		s_lights[i]->Set_Specular(Vector3(0, 0, 0));
		// as W3DDisplay::setTimeOfDay places the world's: the light's Z axis is its direction
		Matrix3D mtx;
		mtx.Set(Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(l.direction[0], l.direction[1], l.direction[2]), Vector3(0, 0, 0));
		s_lights[i]->Set_Transform(mtx);
		s_scene->Add_Render_Object(s_lights[i]);
	}
}

// the preview's own light at share of its strength, ambient included
void setPreviewLight(float share)
{
	s_scene->Set_Ambient_Light(Vector3(PREVIEW_AMBIENT[0], PREVIEW_AMBIENT[1], PREVIEW_AMBIENT[2]) * share);
	for (Int i = 0; i < PREVIEW_LIGHT_COUNT; ++i)
	{
		const PreviewLight &l = PREVIEW_LIGHTS[i];
		s_lights[i]->Set_Diffuse(Vector3(l.diffuse[0], l.diffuse[1], l.diffuse[2]) * share);
	}
}

UnsignedInt scaleColor(UnsignedInt argb, float k)
{
	UnsignedInt r = (UnsignedInt)((float)((argb >> 16) & 0xff) * k);
	UnsignedInt g = (UnsignedInt)((float)((argb >> 8) & 0xff) * k);
	UnsignedInt b = (UnsignedInt)((float)(argb & 0xff) * k);
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (argb & 0xff000000) | (r << 16) | (g << 8) | b;
}

// draws Direct3D 11 has left out so far while a program compiled off the frame; nought on Direct3D 9
unsigned long long drawsWaited()
{
	unsigned shipped = 0, held = 0, offFrame = 0;
	double longest = 0.0;
	unsigned long long waited = 0;
	Direct3D11_Program_Statistics(shipped, held, offFrame, longest, waited);
	return waited;
}

}	// namespace

// W3DDisplay::drawModelPreview ===============================================
//=============================================================================
void W3DDisplay::drawModelPreview( const AsciiString &templateName, Int skin, Color house, Int x, Int y, Int w, Int h, Real turns, Bool dim, Bool still, const char *state )
{
	if( isRecording() ) { record( [=]() { drawModelPreview( templateName, skin, house, x, y, w, h, turns, dim, still, state ); } ); return; }
	if( w <= 0 || h <= 0 || m_assetManager == NULL || templateName.isEmpty() )
		return;

	makeScene();
	PreviewModel *model = findModel( templateName, AsciiString( state ? state : "" ), skin, house );

	// what the 2D layer is holding goes down first, under the model
	flush2D();

	if( !model->parts.empty() )
	{
		const float W = (float)getWidth();
		const float H = (float)getHeight();
		const ModelPreviewFrame::ViewPlane plane = ModelPreviewFrame::forPanel( W, H, (float)x, (float)y, (float)w, (float)h, PREVIEW_TAN_HALF );

		// the plate: an octagon round the model's body, a little wider, never past what the body sweeps
		// (a barrel past the edge reads better than a hull on a plate twice its size); none for a still
		const float plateRadius = still ? 0.0f : WWMath::Min( model->body * 1.25f, model->sweep );
		const float plateThick = plateRadius * 0.10f;
		const float angle = (float)turns * 2.0f * WWMATH_PI;
		const float elevation = still ? PREVIEW_STILL_ELEVATION : PREVIEW_ELEVATION;
		const Vector3 toEye( cosf( PREVIEW_YAW ) * cosf( elevation ), sinf( PREVIEW_YAW ) * cosf( elevation ), sinf( elevation ) );

		// What the frame holds.  A still: the body's box corners at this turn, tight.  Turning: the
		// cylinder the body sweeps and the plate under it, the same at every turn so the model never
		// leaves the rectangle and the frame never breathes.  Rotors are hidden (below), so the body
		// alone sets the size.
		std::vector<Vector3> points;
		const float c = cosf( angle ), s = sinf( angle );
		if( still )
		{
			for( size_t i = 0; i < model->corners.size(); ++i )
			{
				const Vector3 &p = model->corners[ i ];
				points.push_back( Vector3( c * p.X - s * p.Y, s * p.X + c * p.Y, p.Z ) );
			}
		}
		else
		{
			for( Int k = 0; k < 16; ++k )
			{
				const float a = (float)k * WWMATH_PI / 8.0f;
				points.push_back( Vector3( model->sweep * cosf( a ), model->sweep * sinf( a ), model->highZ ) );
				points.push_back( Vector3( model->sweep * cosf( a ), model->sweep * sinf( a ), WWMath::Min( model->lowZ, -plateThick ) ) );
			}
		}

		// across the view (right, up) and toward the eye, then the tightest eye for this rectangle
		Vector3 right( -toEye.Y, toEye.X, 0.0f );
		right.Normalize();
		const Vector3 up( toEye.Y * right.Z - toEye.Z * right.Y, toEye.Z * right.X - toEye.X * right.Z, toEye.X * right.Y - toEye.Y * right.X );
		std::vector<float> across( points.size() ), upward( points.size() ), depth( points.size() );
		float nearest = 0.0f, farthest = 0.0f;
		for( size_t i = 0; i < points.size(); ++i )
		{
			across[ i ] = Vector3::Dot_Product( points[ i ], right );
			upward[ i ] = Vector3::Dot_Product( points[ i ], up );
			depth[ i ] = Vector3::Dot_Product( points[ i ], toEye );
			nearest = i == 0 ? depth[ i ] : WWMath::Max( nearest, depth[ i ] );
			farthest = i == 0 ? depth[ i ] : WWMath::Min( farthest, depth[ i ] );
		}
		const float narrow = (float)WWMath::Min( w, h );
		const ModelPreviewFrame::Fit fit = ModelPreviewFrame::fitPoints( &across[0], &upward[0], &depth[0], (int)points.size(), (int)points.size(),
			PREVIEW_TAN_HALF * (float)w / narrow, PREVIEW_TAN_HALF * (float)h / narrow, PREVIEW_HELD_SHARE, PREVIEW_HELD_SHARE );
		const Vector3 target = fit.centerX * right + fit.centerY * up;
		const float distance = WWMath::Max( fit.distance, nearest + 1.0f );
		const Vector3 eye = target + distance * toEye;

		Matrix3D cameraTransform;
		cameraTransform.Look_At( eye, target, 0.0f );
		s_camera->Set_Transform( cameraTransform );
		s_camera->Set_View_Plane( Vector2( plane.minX, plane.minY ), Vector2( plane.maxX, plane.maxY ) );
		s_camera->Set_Clip_Planes( WWMath::Max( distance - nearest - 2.0f, 0.5f ), distance - farthest + 2.0f );
		Matrix3D modelTransform( true );
		modelTransform.Rotate_Z( angle );
		for( size_t i = 0; i < model->parts.size(); ++i )
			model->parts[ i ]->Set_Transform( modelTransform );

		// Until the model has drawn whole once, no plate and no dim: on Direct3D 11 a new program is
		// compiled off the frame and its draws are left out meanwhile, and a plate standing empty for
		// those frames looks broken.  The model pops in, the plate with it on the next frame.
		const float shade = dim ? PREVIEW_DIM : 1.0f;
		if( model->drawn && !still )
		{
			// the plate, through the camera; the 2D renderer's triangles are drawn in the order added, so
			// the sides first and the top over them hides the far ones from this height
			const UnsignedInt steel = scaleColor( 0xFF3B4148, shade );
			const UnsignedInt line = scaleColor( house != 0 ? ( (UnsignedInt)house | 0xFF000000 ) : 0xFF8A929C, shade );
			Vector2 top[8], bottom[8], inner[8];
			for( Int i = 0; i < 8; ++i )
			{
				const float a = angle + ( (float)i + 0.5f ) * WWMATH_PI / 4.0f;
				Vector3 ndc;
				s_camera->Project( ndc, Vector3( plateRadius * cosf( a ), plateRadius * sinf( a ), 0.0f ) );
				ModelPreviewFrame::ndcToPixel( ndc.X, ndc.Y, W, H, &top[i].X, &top[i].Y );
				s_camera->Project( ndc, Vector3( plateRadius * cosf( a ), plateRadius * sinf( a ), -plateThick ) );
				ModelPreviewFrame::ndcToPixel( ndc.X, ndc.Y, W, H, &bottom[i].X, &bottom[i].Y );
				s_camera->Project( ndc, Vector3( 0.84f * plateRadius * cosf( a ), 0.84f * plateRadius * sinf( a ), 0.0f ) );
				ModelPreviewFrame::ndcToPixel( ndc.X, ndc.Y, W, H, &inner[i].X, &inner[i].Y );
			}
			Vector3 ndc;
			Vector2 middle;
			s_camera->Project( ndc, Vector3( 0.0f, 0.0f, 0.0f ) );
			ModelPreviewFrame::ndcToPixel( ndc.X, ndc.Y, W, H, &middle.X, &middle.Y );

			const Vector2 noUV( 0.0f, 0.0f );
			if( setup2D( NULL, FALSE ) )
				m_2DRender->Enable_Texturing( FALSE );
			for( Int i = 0; i < 8; ++i )
			{
				// a side lit by how far it faces the eye's way round
				const float a = angle + ( (float)i + 1.0f ) * WWMATH_PI / 4.0f;
				const float facing = cosf( a - PREVIEW_YAW );
				const UnsignedInt side = scaleColor( steel, 0.55f + 0.35f * ( facing > 0.0f ? facing : 0.0f ) );
				const Int j = ( i + 1 ) % 8;
				m_2DRender->Add_Tri( top[i], top[j], bottom[j], noUV, noUV, noUV, side );
				m_2DRender->Add_Tri( top[i], bottom[j], bottom[i], noUV, noUV, noUV, side );
			}
			for( Int i = 0; i < 8; ++i )
				m_2DRender->Add_Tri( middle, top[i], top[( i + 1 ) % 8], noUV, noUV, noUV, steel );
			for( Int i = 0; i < 8; ++i )
			{
				m_2DRender->Add_Line( top[i], top[( i + 1 ) % 8], 1.0f, scaleColor( 0xFF6C747E, shade ) );
				m_2DRender->Add_Line( inner[i], inner[( i + 1 ) % 8], 2.0f, line );
			}
			finish2D();
			flush2D();
		}

		// on Direct3D 11 the world's shadow receiving stays on after it, and this perspective draw
		// would sample the shell map's sun map
		Direct3D11_Clear_Shadow_Parameters();

		// a look the account lacks: the same model under a fraction of the light, so the model goes
		// dark and nothing round it does
		setPreviewLight( shade );
		// the rotors stay out: their discs reach half again past a Chinook's hull, framing them shrank
		// the body to two thirds, and there is no scissor to clip their tips at the rectangle
		// ponytail: hidden rather than clipped; a viewport or scissor seam in both backends would let them turn
		for( size_t i = 0; i < model->rotors.size(); ++i )
			model->rotors[ i ]->Set_Hidden( true );
		const unsigned long long waitedBefore = drawsWaited();
		for( size_t i = 0; i < model->parts.size(); ++i )
			s_scene->Add_Render_Object( model->parts[ i ] );
		WW3D::Render( s_scene, s_camera, false, true );
		for( size_t i = 0; i < model->parts.size(); ++i )
			s_scene->Remove_Render_Object( model->parts[ i ] );
		if( drawsWaited() == waitedBefore )
			model->drawn = TRUE;
	}

}  // end drawModelPreview

// W3DDisplay::freeModelPreviews ==============================================
/** the preview's models, lights, camera and scene, for a screen that closes and for shutdown; and the
	* skinned, recoloured prototypes and sheets the models made, which nothing else holds, so nothing of
	* the previews stays behind for a match that follows */
//=============================================================================
void W3DDisplay::freeModelPreviews( void )
{
	const Bool hadModels = !s_models.empty();
	for( size_t i = 0; i < s_models.size(); ++i )
		releaseModel( s_models[i] );
	s_models.clear();
	if( s_scene )
		s_scene->Remove_All_Render_Objects();
	for( Int i = 0; i < PREVIEW_LIGHT_COUNT; ++i )
		REF_PTR_RELEASE( s_lights[i] );
	REF_PTR_RELEASE( s_camera );
	REF_PTR_RELEASE( s_scene );

	if( hadModels && m_assetManager )
		m_assetManager->Release_Unused_Assets();

}  // end freeModelPreviews
