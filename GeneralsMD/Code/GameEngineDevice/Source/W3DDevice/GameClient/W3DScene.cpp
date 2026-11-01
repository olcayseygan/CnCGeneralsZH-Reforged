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

// FILE: W3DScene.cpp /////////////////////////////////////////////////////////
//
// Scene manger for display using W3DDispaly.  A scene manager can customize
// the rendering process, culling, material passes ...
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include "Platform/SunShadow.h"
#include <stdlib.h>
#include "Lib/Clock.h"

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Lib/BaseType.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/Drawable.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Color.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/View.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/FireLights.h"
#include "W3DDevice/GameClient/W3DGranny.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DVolumetricShadow.h"
#include "W3DDevice/GameClient/W3DStatusCircle.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/sortingrenderer.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/light.h"
#include "WW3D2/matpass.h"
#include "WW3D2/shader.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/colorspace.h"

#include "WW3D2/shdlib.h"
#include "WW3D2/dx11runtime.h"
#include "WW3D2/ffshader.h"
#include "Platform/RenderTypes.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"

#include <algorithm>
#include <vector>

/* A hint that a cache line is about to be read, as in PartitionManager.cpp: it reads nothing the
	 program sees, so no result depends on it. */
#if defined(__GNUC__) || defined(__clang__)
	#define SCENE_PREFETCH(addr) __builtin_prefetch(addr)
#elif defined(_M_X64) || defined(_M_IX86)
	#include <xmmintrin.h>
	#define SCENE_PREFETCH(addr) _mm_prefetch((const char *)(addr), _MM_HINT_T0)
#elif defined(_M_ARM64)
	#include <intrin.h>
	#define SCENE_PREFETCH(addr) __prefetch(addr)
#else
	#define SCENE_PREFETCH(addr) ((void)0)
#endif

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

///////////////////////////////////////////////////////////////////////////////
// DEFINITIONS ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///@todo: Remove these globals since we no longer need W3D to call them for us.
extern void DoTrees(RenderInfoClass & rinfo);
extern void DoShadows(RenderInfoClass & rinfo, Bool stencilPass);
extern void DoParticles(RenderInfoClass & rinfo);

// The Direct3D 11 cloak goes by the stealthed state, not by the opacity, which a friendly stealth
// unit swings between about a half and one: drawn by the opacity it blinked on and off.  Such a
// unit always goes in the translucent pass, after the opaque world, because the cloak bends a copy
// of the scene taken at its first draw.  theTranslucentPass is up while that pass draws.
static Bool theTranslucentPass = FALSE;

static Bool wearsCloak( const Drawable *draw )
{
	if( !Direct3D11_Draw_Effects() )
		return FALSE;
	const StealthLookType look = draw->getStealthDrawnLook();
	const Real fade = draw->getStealthFade();
	if( look == STEALTHLOOK_VISIBLE_FRIENDLY || look == STEALTHLOOK_VISIBLE_FRIENDLY_DETECTED )
	{
		// mines force opacity 0 and must not become a lens on the ground
		if( draw->isKindOf( KINDOF_MINE ) )
			return FALSE;
		// a frame that cannot draw the cloak (a screen filter holds the target) draws him by opacity
		return Direct3D11_Takes_Draw_Effect( DRAW_EFFECT_CLOAK );
	}
	// revealed fades in the translucent pass so the heat can come up from nothing
	if( look == STEALTHLOOK_VISIBLE_DETECTED && fade < 0.99f )
		return Direct3D11_Takes_Draw_Effect( DRAW_EFFECT_REVEALED );
	return FALSE;
}

// No texturing, no zbuffer reading/writing, primary gradient, no
// blending, no fogging - mostly for use in solid-colored opaque objects.
#define SC_PLAYER_COLOR ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, \
	ShaderClass::SRCBLEND_ONE, ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )
static ShaderClass PlayerColorShader(SC_PLAYER_COLOR);

// The player's option, gated by the stencil buffer the markers need. Asked per frame and never
// written back: a device reset that loses the stencil for a moment must not clear the option.
static Bool behindBuildingMarkersEnabled()
{
	return TheGlobalData->m_enableBehindBuildingMarkers && DX8Wrapper::Has_Stencil();
}

//=============================================================================
// RTS3DScene::RTS3DScene
//=============================================================================
/** */
//=============================================================================
RTS3DScene::RTS3DScene()
{
	setName("RTS3DScene");
	m_drawTerrainOnly = false;
	m_numGlobalLights=0;
	Int i;	// VC6 for-scope leak
	for (i=0; i<LightEnvironmentClass::MAX_LIGHTS; i++)
	{	m_globalLight[i]=NULL;
		m_infantryLight[i]=NEW_REF( LightClass, (LightClass::DIRECTIONAL) );
	}

	m_scratchLight = NEW_REF( LightClass, (LightClass::DIRECTIONAL) );
//	REF_PTR_SET(m_globalLight[lightIndex], pLight);

#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheGlobalData->m_shroudOn)
		m_shroudMaterialPass = NEW_REF(W3DShroudMaterialPassClass,());
	else
		m_shroudMaterialPass = NULL;
#else
	m_shroudMaterialPass = NEW_REF(W3DShroudMaterialPassClass,());
#endif

	m_maskMaterialPass = NEW_REF(W3DMaskMaterialPassClass,());
	m_customPassMode = SCENE_PASS_DEFAULT;

	m_heatVisionMaterialPass = NEW_REF(MaterialPassClass,());
	m_heatVisionOnlyPass = NEW_REF(MaterialPassClass,());
	VertexMaterialClass *heatVisionMtl = NEW_REF(VertexMaterialClass,());
	heatVisionMtl->Set_Lighting(true);
	heatVisionMtl->Set_Ambient(0,0,0);
	heatVisionMtl->Set_Diffuse(0.02f,0.01f,0.00f);
	heatVisionMtl->Set_Emissive(0.5f,0.2f,0.0f);
	m_heatVisionMaterialPass->Set_Material(heatVisionMtl);
	ShaderClass heatVisionShader=ShaderClass::_PresetAdditiveSolidShader;
	heatVisionShader.Set_Depth_Compare(ShaderClass::PASS_EQUAL);
	m_heatVisionMaterialPass->Set_Shader(heatVisionShader);
	heatVisionMtl->Release_Ref();
	heatVisionShader.Set_Depth_Compare(ShaderClass::PASS_LEQUAL);
	heatVisionShader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
	m_heatVisionOnlyPass->Set_Material(heatVisionMtl);
	m_heatVisionOnlyPass->Set_Shader(heatVisionShader);


//	VertexMaterialClass *frenzyMtl = NEW_REF(VertexMaterialClass,());
//	frenzyMtl->Set_Lighting(TRUE);
//	frenzyMtl->Set_Ambient(  0, 0, 0 );
//	frenzyMtl->Set_Diffuse(  1.0f, 0.0f, 0.0f );
//	frenzyMtl->Set_Emissive( 1.0f, 0.0f, 0.0f );
//	m_frenzyMaterialPass = NEW_REF(MaterialPassClass,());
//	m_frenzyMaterialPass->Set_Material(frenzyMtl);
//	frenzyMtl->Release_Ref();
//	ShaderClass frenzyShader=ShaderClass::_PresetMultiplicativeShader;
//	frenzyShader.Set_Depth_Compare(ShaderClass::PASS_EQUAL);
//	frenzyShader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
//	m_frenzyMaterialPass->Set_Shader(frenzyShader);

  
	//Allocate memory to hold queue of visible renderobjects that need to be drawn last
	//because they are forced translucent.
	m_translucentObjectsCount = 0;
	if (TheGlobalData && TheGlobalData->m_maxVisibleTranslucentObjects)
		m_translucentObjectsBuffer = NEW RenderObjClass* [TheGlobalData->m_maxVisibleTranslucentObjects];
	else
		m_translucentObjectsBuffer = NULL;

	m_numPotentialOccluders=0;
	m_numPotentialOccludees=0;
	m_numNonOccluderOrOccludee=0;
	m_occludedObjectsCount=0;

	m_potentialOccluders=NULL;
	m_potentialOccludees=NULL;
	m_nonOccludersOrOccludees=NULL;

	//Modify the shader to make occlusion transparent
	ShaderClass shader = PlayerColorShader;
	shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);

    m_potentialOccluders = NEW RenderObjClass* [TheGlobalData->m_maxVisibleOccluderObjects];
	m_potentialOccludees = NEW RenderObjClass* [TheGlobalData->m_maxVisibleOccludeeObjects];
	m_nonOccludersOrOccludees = NEW RenderObjClass* [TheGlobalData->m_maxVisibleNonOccluderOrOccludeeObjects];

#ifdef USE_NON_STENCIL_OCCLUSION
	for (i=0; i<MAX_PLAYER_COUNT; i++)
	{	m_occludedMaterialPass[i]=NEW_REF(MaterialPassClass,());
		VertexMaterialClass * vmtl = NEW_REF(VertexMaterialClass,());
		vmtl->Set_Lighting(true);
		vmtl->Set_Ambient(0,0,0);	//we're only using emissive so kill all other lights.
		vmtl->Set_Diffuse(0,0,0);
		m_occludedMaterialPass[i]->Set_Material(vmtl);
		m_occludedMaterialPass[i]->Set_Shader(shader);
		vmtl->Release_Ref();	//material pass is holding the pointer so release ref.
	}
#else
	for (i=0; i<MAX_PLAYER_COUNT; i++)
		m_occludedMaterialPass[i]=NULL;
#endif

}  // end RTS3DScene

//=============================================================================
// RTS3DScene::~RTS3DScene
//=============================================================================
/** */
//=============================================================================
RTS3DScene::~RTS3DScene()
{
	Int i;	// VC6 for-scope leak
	for (i=0; i<LightEnvironmentClass::MAX_LIGHTS; i++)
	{
		REF_PTR_RELEASE(m_globalLight[i]);
		REF_PTR_RELEASE(m_infantryLight[i]);
	}

	REF_PTR_RELEASE(m_scratchLight);

	REF_PTR_RELEASE(m_shroudMaterialPass);

	REF_PTR_RELEASE(m_maskMaterialPass);

	REF_PTR_RELEASE(m_heatVisionMaterialPass);

	REF_PTR_RELEASE(m_heatVisionOnlyPass);

	if (m_translucentObjectsBuffer)
		delete [] m_translucentObjectsBuffer;

	if (m_nonOccludersOrOccludees)
		delete [] m_nonOccludersOrOccludees;

	if (m_potentialOccludees)
		delete [] m_potentialOccludees;

	if (m_potentialOccluders)
		delete [] m_potentialOccluders;

	for (i=0; i<MAX_PLAYER_COUNT; i++)
	{	REF_PTR_RELEASE(m_occludedMaterialPass[i]);
	}

	for (size_t spent = 0; spent < m_spentDynamicLights.size(); ++spent)
		m_spentDynamicLights[spent]->Release_Ref();

}  // end ~RTS3DScene


void	RTS3DScene::setGlobalLight(LightClass *pLight, Int lightIndex)
{
	if (m_numGlobalLights < (lightIndex+1))
		m_numGlobalLights=(lightIndex+1);
	REF_PTR_SET(m_globalLight[lightIndex], pLight);
}

/**Find all objects which need to be drawn in a special way because they are occluded by other
objects.
@todo:  Need some kind of scene subdivision or find way to use Partition manger to speed up the ray
intersection tests.  Maybe truncate the ray to terrain length before using it?
*/
void RTS3DScene::flagOccludedObjects(CameraClass * camera)
{
	Vector3 camPosition=camera->Get_Position();

	//Find which objects are actually occluded
	RenderObjClass **occludee=m_potentialOccludees;
	LineSegClass lineseg;
	CastResultStruct result;
	Bool hit=FALSE;
	Vector3 newEndPoint;
	result.ComputeContactPoint=false;
	RayCollisionTestClass raytest(lineseg,&result,COLL_TYPE_ALL,false,false);
	raytest.CollisionType=COLL_TYPE_ALL;

	m_occludedObjectsCount=0;

	for (Int i=0; i<m_numPotentialOccludees; i++,occludee++)
	{
		// Per occludee. This was declared once outside the loop and never cleared, so the first
		// object found to be behind a building made every object after it in the list "occluded"
		// too - whatever was actually in front of it, and with no occluder to point at. On a busy
		// screen that is most of the list drawn through the see-through path for nothing.
		hit=FALSE;
		result.StartBad = false;
		result.Fraction = 1.0f;

		raytest.Ray.Set(camPosition,(*occludee)->Get_Position());

		RenderObjClass **occluder=m_potentialOccluders;

		//Check this object against all other possible blocking objects
		for (Int j=0; j<m_numPotentialOccluders; j++,occluder++)
		{
			// Do a quick ray-sphere test (Graphics Gems I,  p388)
			RenderObjClass *robj=*occluder;

			const SphereClass *sphere = &robj->Get_Bounding_Sphere();

			// make a vector from the ray origin to the sphere center
			Vector3 sphere_vector(sphere->Center - raytest.Ray.Get_P0());
			
			// get the dot product between the sphere_vector and the ray vector
			Real Alpha = Vector3::Dot_Product(sphere_vector, raytest.Ray.Get_Dir());

			Real Beta = sphere->Radius * sphere->Radius - (Vector3::Dot_Product(sphere_vector, sphere_vector) - Alpha * Alpha);

			if(Beta < 0.0f)
				continue;	//no intersection
				
			//Do a more accurate test against object geometry
			if (robj->Cast_Ray(raytest))
			{
				//Found an object closer than last closest object
				//Adjust our results and refine search
				raytest.CollidedRenderObj = robj;
				hit=TRUE;
				//reset the result space for next test
				result.StartBad = false; result.Fraction = 1.0f;
				break;
			}
		}
		if (hit)
		{	//ocludee was blocked by something so flag it for custom rendering
			DrawableInfo *drawInfo=(DrawableInfo *)(*occludee)->Get_User_Data();
			drawInfo->m_flags |= DrawableInfo::ERF_IS_OCCLUDED;
			m_potentialOccludees[m_occludedObjectsCount++] = *occludee;
		}
	}
}

//=============================================================================
/** One object of castRay's walk: a quick ray-sphere test (Graphics Gems I, p388), then the geometry.
	* A hit shortens the ray to end on it, so a later object has to be nearer to count. */
static Bool castRayAgainst(RenderObjClass *robj, RayCollisionTestClass &tempRayTest, RayCollisionTestClass &raytest)
{
	const SphereClass *sphere = &robj->Get_Bounding_Sphere();
	Vector3 sphere_vector(sphere->Center - tempRayTest.Ray.Get_P0());
	Real Alpha = Vector3::Dot_Product(sphere_vector, tempRayTest.Ray.Get_Dir());
	Real Beta = sphere->Radius * sphere->Radius - (Vector3::Dot_Product(sphere_vector, sphere_vector) - Alpha * Alpha);
	if (Beta < 0.0f || !robj->Cast_Ray(tempRayTest))
		return FALSE;

	raytest.CollidedRenderObj = robj;
	Vector3 newEndPoint;
	tempRayTest.Ray.Compute_Point(tempRayTest.Result->Fraction,&newEndPoint);
	tempRayTest.Ray.Set(raytest.Ray.Get_P0(),newEndPoint);
	//intersection point is at the end of shortened ray, so adjust fraction to 1.0
	tempRayTest.Result->Fraction=1.0f;
	return TRUE;
}

//=============================================================================
// RTS3DScene::castRay
//=============================================================================
/** Does a ray intersection test against objects in our scene.
	By default, it only tests objects determined to be visible to the user.
	Setting testAll forces it to test all objects in the scene.
	CollisionType is used as a mask to ignore certain types of objects.
 */
//=============================================================================
Bool RTS3DScene::castRay(RayCollisionTestClass & raytest, Bool testAll, Int collisionType)
{
// this shouldn't be necessary here, and would be an undesirable performance hit.
// if you ever add or modify code here, it MIGHT become necessary... so do so with caution. (srj)
//#ifdef DIRTY_CONDITION_FLAGS
//	StDrawableDirtyStuffLocker lockDirtyStuff;
//#endif

	//temporary results for each object tested
	CastResultStruct result;
	RayCollisionTestClass tempRayTest(raytest.Ray,&result);
	Bool hit=FALSE;

	tempRayTest.CollisionType = COLL_TYPE_ALL;
	//check if a mesh is translucent before colliding with it. Skips headlights, etc.
	tempRayTest.CheckTranslucent = true;

	if (testAll)
	{
		RefRenderObjListIterator it(&RenderList);
		for (it.First(); !it.Is_Done(); it.Next())
		{
			RenderObjClass *robj = it.Peek_Obj();
			if ((robj->Get_Collision_Type() & collisionType) && castRayAgainst(robj, tempRayTest, raytest))
				hit = TRUE;
		}
	}
	else
	{
		// The visible ones only (m_visibleObjects, see Add_Render_Object): about 145 of 900 render
		// objects late in a 4-player match.  The mouse-over pick runs once a client frame, undrawn
		// frames included, and the walk past the other 750 was two thirds of its time.
		for (size_t i = 0; i < m_visibleObjects.size(); ++i)
		{
			RenderObjClass *robj = m_visibleObjects[i];
			if (robj && (robj->Get_Collision_Type() & collisionType) && robj->Is_Really_Visible() && castRayAgainst(robj, tempRayTest, raytest))
				hit = TRUE;
		}
	}

	//Store results of ray intersection test including a clipped ray that ends on object.
	raytest.Ray=tempRayTest.Ray;
	return hit;
}

//=============================================================================
// RTS3DScene::Add_Render_Object / Remove_Render_Object / Remove_All_Render_Objects
//=============================================================================
/** m_visibleObjects holds every object in the scene whose visible bit is set, in RenderList order,
	* so a walk over it that asks Is_Really_Visible meets what a walk over RenderList would.  An
	* object joins the scene without the bit (DEFAULT_BITS) and only Visibility_Check sets it, so the
	* list is what that check left, plus an object that comes back still carrying the bit from an
	* earlier stay.  A removed object is nulled rather than erased, keeping the order, which is the
	* tie-break of castRay and the order the render loop queues in; the scene's reference is what
	* kept it alive. */
//=============================================================================
void RTS3DScene::Add_Render_Object(RenderObjClass * obj)
{
	const bool listed = RenderList.Contains(obj);
	SimpleSceneClass::Add_Render_Object(obj);
	if (!listed && obj->Is_Visible())
		m_visibleObjects.push_back(obj);
}

void RTS3DScene::Remove_Render_Object(RenderObjClass * obj)
{
	std::replace(m_visibleObjects.begin(), m_visibleObjects.end(), obj, (RenderObjClass *)NULL);
	SimpleSceneClass::Remove_Render_Object(obj);
}

void RTS3DScene::Remove_All_Render_Objects(void)
{
	m_visibleObjects.clear();
	SimpleSceneClass::Remove_All_Render_Objects();
}

//=============================================================================
// posedPartsInView
//=============================================================================
/** A model whose animation carries it out of its bind pose (W3DModelDraw's animLeavesBindPose) is
  * in view when its parts are, as they stand now. */
//=============================================================================
static Bool posedPartsInView(RenderObjClass *robj, CameraClass *camera)
{
	SphereClass posed;
	return W3DModelDraw::getPosedBoundingSphere(robj, posed) && !camera->Cull_Sphere(posed);
}

/** Visibility_Check's walk is a linked list of objects scattered over the heap, most of them off the
	* screen and done with in a few tests, so the walk is mostly waiting for each one to arrive.  This
	* asks for the one two ahead: its vtable pointer and bits at the start, its cached bounding sphere
	* about 100 bytes in.  A layout change only makes the second hint miss. */
static inline void prefetchAhead(RefRenderObjListIterator it)
{
	it.Next();
	if (it.Is_Done())
		return;
	it.Next();
	if (it.Is_Done())
		return;
	const char *obj = (const char *)it.Peek_Obj();
	SCENE_PREFETCH(obj);
	SCENE_PREFETCH(obj + 100);
}

//=============================================================================
// RTS3DScene::Visibility_Check
//=============================================================================
/** Custom visibility check method for the RTS3DScene, we can put optimized
  * culling methods in here */
//=============================================================================
void RTS3DScene::Visibility_Check(CameraClass * camera)
{
#ifdef DIRTY_CONDITION_FLAGS
	StDrawableDirtyStuffLocker lockDirtyStuff;
#endif

	RefRenderObjListIterator it(&RenderList);
	DrawableInfo *drawInfo = NULL;
	Drawable	*draw = NULL;
	RenderObjClass * robj;

	m_numPotentialOccluders=0;
	m_numPotentialOccludees=0;
	m_translucentObjectsCount=0;
	m_numNonOccluderOrOccludee=0;
	m_visibleObjects.clear();

	Int currentFrame=0;
	if (TheGameLogic) currentFrame = TheGameLogic->getFrame();
	if (currentFrame <= TheGlobalData->m_defaultOcclusionDelay)
		currentFrame = TheGlobalData->m_defaultOcclusionDelay+1;	//make sure occlusion is enabled when game starts (frame 0).


	if (ShaderClass::Is_Backface_Culling_Inverted()) 
	{	//we are rendering reflections
		///@todo: Have better flag to detect reflection pass

		// Loop over all top-level RenderObjects in this scene. If the bounding sphere is not in front
		// of all the frustum planes, it is invisible.
		for (it.First(); !it.Is_Done(); it.Next()) {

			robj = it.Peek_Obj();
			prefetchAhead(it);

			draw=NULL;
			drawInfo = (DrawableInfo *)robj->Get_User_Data();
			if (drawInfo)
				draw=drawInfo->m_drawable;

			if( draw )
			{
				// The flags are last frame's.  Customized_Render leaves an occluder, and any drawable
				// the behind-building markers delay, for the occlusion lists to draw, and this pass
				// fills no lists, so a stale flag keeps the object out of the mirror altogether.
				drawInfo->m_flags = DrawableInfo::ERF_IS_NORMAL;
				if (robj->Is_Force_Visible()) {
					robj->Set_Visible(true);
				} else {
					// The same test the main pass hides a drawable by, or a unit under the fog of war,
					// or a building standing in for its own ghost, shows up in the water and nowhere else.
					Bool hidden = robj->Is_Hidden() || draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud();
					robj->Set_Visible(!hidden && draw->getDrawsInMirror() && (!camera->Cull_Sphere(robj->Get_Bounding_Sphere()) || posedPartsInView(robj, camera)));
				}
			}
			else
			{	//perform normal culling on non-drawables
				if (robj->Is_Force_Visible()) {
					robj->Set_Visible(true);
				} else {
					robj->Set_Visible(!camera->Cull_Sphere(robj->Get_Bounding_Sphere()));
				}
			}
			if (robj->Is_Visible())
				m_visibleObjects.push_back(robj);
		}
	}
	else
	{

		// Loop over all top-level RenderObjects in this scene. If the bounding sphere is not in front
		// of all the frustum planes, it is invisible.
		for (it.First(); !it.Is_Done(); it.Next()) {

			robj = it.Peek_Obj();
			prefetchAhead(it);

			if (robj->Is_Force_Visible()) {
				robj->Set_Visible(true);
			} else if (robj->Is_Hidden()) {
				robj->Set_Visible(false);
			} else {

				bool isVisible=!camera->Cull_Sphere(robj->Get_Bounding_Sphere()) || posedPartsInView(robj, camera);

				if (isVisible)
				{	//need to keep track of occluders and ocludees for subsequent code.
					drawInfo = (DrawableInfo *)robj->Get_User_Data();
					if (drawInfo && (draw=drawInfo->m_drawable) != NULL)
					{
						if (draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud())
						{	
							isVisible = FALSE;
						  robj->Set_Visible(isVisible);
            }
						//assume normal rendering.
						drawInfo->m_flags = DrawableInfo::ERF_IS_NORMAL;	//clear any rendering flags that may be in effect.

            if ( ! isVisible )
              continue;

						if ((draw->getEffectiveOpacity() != 1.0f || wearsCloak(draw)) && m_translucentObjectsCount < TheGlobalData->m_maxVisibleTranslucentObjects)
						{	drawInfo->m_flags |= DrawableInfo::ERF_IS_TRANSLUCENT;	//object is translucent
							m_translucentObjectsBuffer[m_translucentObjectsCount++] = robj;
						}
						if (behindBuildingMarkersEnabled() && TheGameLogic->getShowBehindBuildingMarkers())
						{
							//visible drawable. Check if it's either an occluder or occludee
							if (draw->isKindOf(KINDOF_STRUCTURE) && m_numPotentialOccluders < TheGlobalData->m_maxVisibleOccluderObjects)
							{	//object which could occlude other objects that need to be visible.
								//Make sure this object is not translucent so it's not rendered twice (from m_potentialOccluders and m_translucentObjectsBuffer)
								// a bit test, not an exclusive or.  The two happen to agree today only
								// because m_flags is cleared to ERF_IS_NORMAL a few lines up and
								// ERF_IS_TRANSLUCENT is the one bit that can be set before this point;
								// the moment anything else sets a flag first, the xor says "not
								// translucent" for an object that is, and it gets drawn twice.
								if (!(drawInfo->m_flags & DrawableInfo::ERF_IS_TRANSLUCENT))
									m_potentialOccluders[m_numPotentialOccluders++]=robj;
								drawInfo->m_flags |= DrawableInfo::ERF_POTENTIAL_OCCLUDER;
							}
							else
							if (draw->getObjectForDraw().hasObject &&
									(draw->isKindOf(KINDOF_SCORE) || draw->isKindOf(KINDOF_SCORE_CREATE) || draw->isKindOf(KINDOF_SCORE_DESTROY) || draw->isKindOf(KINDOF_MP_COUNT_FOR_VICTORY)) &&
									draw->getObjectForDraw().safeOcclusionFrame <= currentFrame && m_numPotentialOccludees < TheGlobalData->m_maxVisibleOccludeeObjects)
							{	//object which could be occluded but still needs to be visible.
								//We process transucent units twice (also in m_translucentObjectsBuffer) because we need to see them when occluded.
								m_potentialOccludees[m_numPotentialOccludees++]=robj;
								drawInfo->m_flags |= DrawableInfo::ERF_POTENTIAL_OCCLUDEE;
							}
							else
							if (drawInfo->m_flags == DrawableInfo::ERF_IS_NORMAL && m_numNonOccluderOrOccludee < TheGlobalData->m_maxVisibleNonOccluderOrOccludeeObjects)
							{	//regular object with no custom effects but still needs to be delayed to get the occlusion feature to work correctly.
								//Make sure this object is not translucent so it's not rendered twice (from m_potentialOccluders and m_translucentObjectsBuffer)
								// a bit test, not an exclusive or.  The two happen to agree today only
								// because m_flags is cleared to ERF_IS_NORMAL a few lines up and
								// ERF_IS_TRANSLUCENT is the one bit that can be set before this point;
								// the moment anything else sets a flag first, the xor says "not
								// translucent" for an object that is, and it gets drawn twice.
								if (!(drawInfo->m_flags & DrawableInfo::ERF_IS_TRANSLUCENT))	//make sure not translucent
									m_nonOccludersOrOccludees[m_numNonOccluderOrOccludee++]=robj;
								drawInfo->m_flags |= DrawableInfo::ERF_IS_NON_OCCLUDER_OR_OCCLUDEE;
							}
						}
					}
				}

				robj->Set_Visible(isVisible);
			}
			if (robj->Is_Visible())
				m_visibleObjects.push_back(robj);

			///@todo: We're not using LOD yet so I disabled this code. MW
			// Also, should check how multiple passes (reflections) get along
			// with the LOD manager - we're rendering double the load it thinks we are.
			// Prepare visible objects for LOD:
			//	if (robj->Is_Really_Visible()) {
			//			robj->Prepare_LOD(*camera);
			//		}
		}
	}

   Visibility_Checked = true;
}

//============================================================================
// RTS3DScene::renderSingleDrawable
//=============================================================================
/** Renders a single drawable entity. */
//=============================================================================
void RTS3DScene::renderSpecificDrawables(RenderInfoClass &rinfo, Int numDrawable, Drawable **theDrawables)
{
#ifdef DIRTY_CONDITION_FLAGS
	StDrawableDirtyStuffLocker lockDirtyStuff;
#endif
	Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;
	RefRenderObjListIterator it(&UpdateList);	
	// loop through all render objects in the list:
	for (it.First(&RenderList); !it.Is_Done();) 
	{
		RenderObjClass *robj;
		// get the render object
		robj = it.Peek_Obj();

		it.Next();	//advance to next object in case this one gets deleted during renderOneObject().

		DrawableInfo *drawInfo = (DrawableInfo *)robj->Get_User_Data();
		Drawable *draw=NULL;
		if (drawInfo)
			draw = drawInfo->m_drawable;
		if (!draw) continue;
		Bool match = false;
		for (Int i = 0; i<numDrawable; i++) {
			if (theDrawables[i] == draw) {
				match = true;
				break;
			}
		}
		if (match) {
			renderOneObject(rinfo, robj, localPlayerIndex);
		}
	}
}

//=============================================================================
/** Which of the Direct3D 11 frame's own looks a drawable wears this frame, and its four numbers
	* (DRAW_EFFECT_SAMPLING in ffshader.h): seconds, strength, a seed from the drawable's id and the
	* drawable's opacity.  The object's half is the sync point's copy (Drawable::getObjectForDraw);
	* nothing here goes back into the logic.  None under -d3d9, -headless or Classic graphics, where the heat
	* vision pass and the disabled tint draw what they always drew.
	*
	* An enemy a detector found is revealed, and an own stealthed unit is cloaked whatever its opacity
	* (wearsCloak), with the detector's pulse in the strength of both.  The cloak only in the
	* translucent pass: the scene's copy it bends is taken at its first draw, and the behind-building
	* markers draw an occluded unit early, mid-way through the opaque world.  Mines keep the heat vision's old exception.
	* An EMP takes precedence over power: an EMP'd building is out of power as well. */
//=============================================================================
static unsigned chooseDrawEffect( const Drawable *draw, Bool translucentPass, float parameters[4] )
{
	if ( !Direct3D11_Draw_Effects() )
		return DRAW_EFFECT_NONE;

	unsigned effect = DRAW_EFFECT_NONE;
	const StealthLookType look = draw->getStealthDrawnLook();
	const Real pulse = draw->getSecondMaterialPassOpacity();
	const Real fade = draw->getStealthFade();
	Real opacity = draw->getStealthSolid();
	Real strength = 1.0f;

	if ( look == STEALTHLOOK_VISIBLE_DETECTED && !draw->isKindOf( KINDOF_MINE )
		&& ( translucentPass || fade >= 0.99f ) )
	{
		effect = DRAW_EFFECT_REVEALED;
		strength = pulse;
		opacity = fade;
	}
	else if ( translucentPass && wearsCloak( draw )
		&& look != STEALTHLOOK_VISIBLE_DETECTED )
	{
		// Detected, the amber stays on and the detector's sweep brightens it; on the pulse alone it
		// showed for a few frames a sweep and was never seen.
		effect = DRAW_EFFECT_CLOAK;
		strength = ( look == STEALTHLOOK_VISIBLE_FRIENDLY_DETECTED ) ? MAX( 0.6f, pulse ) : 0.0f;
	}
	else if ( draw->getObjectForDraw().hasObject && !draw->isObjectDeadForDraw() )
	{
		if ( draw->getObjectForDraw().empDisabled )
			effect = DRAW_EFFECT_EMP;
		else if ( draw->getObjectForDraw().outage )
			effect = DRAW_EFFECT_OUTAGE;
	}

	// A frame the backend would draw the look in nowhere (a screen filter or a mirror holds the
	// target, or the device made no copy for the cloak) keeps the heat vision and the tint instead.
	if ( effect != DRAW_EFFECT_NONE && !Direct3D11_Takes_Draw_Effect( effect ) )
		return DRAW_EFFECT_NONE;

	if ( effect != DRAW_EFFECT_NONE )
	{
		// an hour of seconds keeps the sines' arguments small enough for single precision
		parameters[0] = (float)( WW3D::Get_Sync_Time() % 3600000u ) * 0.001f;
		parameters[1] = strength;
		parameters[2] = (float)fmod( (double)draw->getID() * 0.6180339887, 1.0 );
		parameters[3] = opacity;
	}
	return effect;
}

//============================================================================
// RTS3DScene::renderOneObject
//=============================================================================
/** Renders a single drawable entity. */
//=============================================================================
void RTS3DScene::renderOneObject(RenderInfoClass &rinfo, RenderObjClass *robj, Int localPlayerIndex)
{

	Drawable *draw = NULL;
	DrawableInfo *drawInfo = NULL;
	Bool drawableHidden=FALSE;
	ObjectShroudStatus ss=OBJECTSHROUD_INVALID;
	Bool doExtraMaterialPop=FALSE;
	Bool doExtraFlagsPop=FALSE;
	LightClass **sceneLights=m_globalLight;



	if (robj->Class_ID() == RenderObjClass::CLASSID_IMAGE3D	)
	{	robj->Render(rinfo);	//notify decals system that this track is visible
		return;	//decals are not lit by this system yet so skip rest of lighting
	}

	LightEnvironmentClass lightEnv;
	SphereClass sph = robj->Get_Bounding_Sphere();
	drawInfo = (DrawableInfo *)robj->Get_User_Data();
	if (drawInfo)
	{	draw = drawInfo->m_drawable;
		//If we have a drawInfo but not drawable, we must be dealing with
		//a ghost object which is always fogged.
		if (!draw)
			ss = OBJECTSHROUD_FOGGED;
	}

	// all this ambient business no longer handles the tinting and flashing stuff,
	// but it does still light the drawable explicitly, and can be fudged like this
	// infantry test does, here...
	// the tint has been delegated to the getTint() stuff, below... MLorenzen
	Vector3 ambient = Get_Ambient_Light();
	if (draw && (drawableHidden=draw->isDrawableEffectivelyHidden()) != TRUE)
	{
#ifdef NOT_IN_USE
		const Vector3* drawAmbient = draw->getAmbientLight();
		if (drawAmbient)
			ambient.Add(ambient, *drawAmbient, &ambient);
#endif
		// the shroud as GameClient::update's pass asked it for this player, ahead of the draw
		// (LOGIC-THREAD.md phase 3: the draw does not call into the partition, nor into the object)
		if (draw->getObjectForDraw().hasObject) {
			ss = draw->getShroudForDraw();
			// For objects like planes, that pop out of the shroud, fire, then head back,
			// we keep drawing them for 2 seconds after they return to the fogged area,
			// so the player can see them and missiles chasing them.  jba.
			if (ss == OBJECTSHROUD_CLEAR) {
				draw->setShroudClearFrame(TheGameLogic->getFrame());
			}	else if (ss >= OBJECTSHROUD_FOGGED && draw->getShroudClearFrame()!=0) {
				UnsignedInt limit = 2*LOGICFRAMES_PER_SECOND;
				if (draw->isObjectDeadForDraw()) {
					limit += 3*LOGICFRAMES_PER_SECOND;
				}
				if (TheGameLogic->getFrame() < limit + draw->getShroudClearFrame()) {
					// It's been less than 2 seconds since we could see them clear, so keep showing them.
					ss = OBJECTSHROUD_PARTIAL_CLEAR;
				}
			}
		}
		else
		{	//drawable with no object so no way to know if it's shrouded.
			//Unless another unrelated object controls the shroud status
			//(Hack for prison camps which contain enemy prisoner drawables): we will assume that drawables
			//without objects are 'particle' like and therefore don't need drawing if fogged/shrouded.
			ss = draw->getShroudForDraw() >= OBJECTSHROUD_FOGGED ? OBJECTSHROUD_SHROUDED : OBJECTSHROUD_CLEAR;
		}

		if (draw->isKindOf(KINDOF_INFANTRY))
		{	//ambient = m_infantryAmbient;  //has no effect - see comment on m_infantryAmbient
			sceneLights = m_infantryLight;
		}

		lightEnv.Reset(sph.Center, ambient);


		// The Direct3D 11 frame's own look for this object, if it is in one of the four states that
		// have one.  The two that replace the disabled tint skip it: the dark grey under them only
		// muddied the cold grey they draw.
		rinfo.drawEffect = chooseDrawEffect( draw, theTranslucentPass, rinfo.drawEffectParameters );

		// HANDLE THE SPECIAL DRAWABLE-LEVEL COLORING SETTINGS FIRST

		const Vector3 *tintColor = NULL;
		const Vector3 *selectionColor = NULL;

		if ( rinfo.drawEffect != DRAW_EFFECT_OUTAGE && rinfo.drawEffect != DRAW_EFFECT_EMP )
			tintColor = draw->getTintColor();
		selectionColor = draw->getSelectionColor();

		if ( tintColor || selectionColor )
		{
			Vector3 sumTint, temp, restore;
			
			sumTint.Set(0,0,0); 
			
			if (tintColor)
				Vector3::Add(sumTint, *tintColor, &sumTint);
			if (selectionColor)
				Vector3::Add(sumTint, *selectionColor, &sumTint);

			for (Int globalLightIndex = 0; globalLightIndex < m_numGlobalLights; globalLightIndex++)
			{
				sceneLights[globalLightIndex]->Get_Diffuse( &temp );
				restore = temp;

				Vector3::Add(temp, sumTint, &temp);

				sceneLights[globalLightIndex]->Set_Diffuse( temp );
				lightEnv.Add_Light(*sceneLights[globalLightIndex]);
				sceneLights[globalLightIndex]->Set_Diffuse( restore );

			} // next light
			
			temp = lightEnv.Get_Equivalent_Ambient();
			Vector3::Add(sumTint, temp, &temp );

			lightEnv.Set_Output_Ambient( temp );

		}
		else // no funny coloring going on, so just add the lights normally
		{
			for (Int globalLightIndex = 0; globalLightIndex < m_numGlobalLights; globalLightIndex++)
			{
				lightEnv.Add_Light(*sceneLights[globalLightIndex]);
			}
		}

		//Apply custom render pass for any drawables with heatvision enabled.  The cloak and the
		//revealed look draw the detector's pulse themselves, over the unit's own paint.
		if (draw->getSecondMaterialPassOpacity() != 0
			&& rinfo.drawEffect != DRAW_EFFECT_CLOAK && rinfo.drawEffect != DRAW_EFFECT_REVEALED )
		{
			rinfo.materialPassEmissiveOverride = draw->getSecondMaterialPassOpacity();

      //if ( draw->testTintStatus( TINT_STATUS_FRENZY ) )
      //{
			//	rinfo.Push_Material_Pass(m_heatVisionMaterialPass);
      //}
			//else 
      if (draw->getStealthLook() == STEALTHLOOK_VISIBLE_DETECTED )
			{
			  rinfo.materialPassEmissiveOverride = draw->getSecondMaterialPassOpacity();
				// THIS WILL EXPLICITLY SKIP THE FIRST PASS SO THAT HEATVISION ONLY WILL RENDER
				rinfo.Push_Override_Flags(RenderInfoClass::RINFO_OVERRIDE_ADDITIONAL_PASSES_ONLY);
				rinfo.Push_Material_Pass(m_heatVisionOnlyPass);
        doExtraFlagsPop = TRUE;
			}
			else
			{
				//THIS CALLS FOR THE HEATVISION TO RENDER
			  rinfo.materialPassEmissiveOverride = draw->getSecondMaterialPassOpacity();
				rinfo.Push_Material_Pass(m_heatVisionMaterialPass);
			}

			doExtraMaterialPop = TRUE;
		}
	}
	else
	{	//either no drawable or it is hidden
		if (drawableHidden)
			return;	//don't bother with anything else

		//Render object without a drawable.  Must be either some fluff/debug object or a ghostObject.
		if (ss == OBJECTSHROUD_FOGGED)
		{	//Must be ghost object because we don't fog normal things.  Fogged objects always have a predefined
			//lighting environment applied which emulates the look of fog.
			rinfo.light_environment = &m_foggedLightEnv;
			robj->Render(rinfo);
			rinfo.light_environment = NULL;
			return;
		}
		else
		{	lightEnv.Reset(sph.Center, ambient);
			for (Int globalLightIndex = 0; globalLightIndex < m_numGlobalLights; globalLightIndex++)
				lightEnv.Add_Light(*m_globalLight[globalLightIndex]);
		}
	}

	if (!drawableHidden)
	{
		//standard scene lights
		RefRenderObjListIterator it2(&LightList);	
		for (it2.First(); !it2.Is_Done(); it2.Next())
		{	
			LightClass *pLight = (LightClass*)it2.Peek_Obj();
			SphereClass lSph = pLight->Get_Bounding_Sphere();
			Bool cull = (pLight->Get_Type() == LightClass::POINT && !Spheres_Intersect(sph, lSph));
			if (!cull) {
				lightEnv.Add_Light(*pLight);
			}
		}

    // On the Direct3D 11 frame the dynamic lights reach every pixel instead (handBlastLights), and
    // added here as well they would light the model twice.
    if( draw && draw->getReceivesDynamicLights() && !Direct3D11_Lights_Per_Pixel() )
    {
		  // dynamic lights
		  RefRenderObjListIterator dynaLightIt(&m_dynamicLightList);	
		  for (dynaLightIt.First(); !dynaLightIt.Is_Done(); dynaLightIt.Next())
		  {	
			  W3DDynamicLight* pDyna = (W3DDynamicLight*)dynaLightIt.Peek_Obj();
			  if (!pDyna->isEnabled()) {
				  continue;
			  }
			  SphereClass lSph = pDyna->Get_Bounding_Sphere();
			  if (pDyna->Get_Type() == LightClass::POINT && !Spheres_Intersect(sph, lSph)) {
				  continue;
			  }
			  lightEnv.Add_Light(*(LightClass*)dynaLightIt.Peek_Obj());
		  }
    }
		
		lightEnv.Pre_Render_Update(rinfo.Camera.Get_Transform());
		rinfo.light_environment = &lightEnv;

		if (drawInfo)
		{
#if defined(_DEBUG) || defined(_INTERNAL)
			if (!TheGlobalData->m_shroudOn)
				ss = OBJECTSHROUD_CLEAR;
#endif
			
			if (m_customPassMode == SCENE_PASS_DEFAULT)	
			{
				if (ss <= OBJECTSHROUD_CLEAR)
					robj->Render(rinfo);
				else
				{	
						rinfo.Push_Material_Pass(m_shroudMaterialPass);
						robj->Render(rinfo);
						rinfo.Pop_Material_Pass();
				}
			}
			else
			if (m_maskMaterialPass)
			{	rinfo.Push_Material_Pass(m_maskMaterialPass);
				rinfo.Push_Override_Flags(RenderInfoClass::RINFO_OVERRIDE_ADDITIONAL_PASSES_ONLY);
				robj->Render(rinfo);
				rinfo.Pop_Override_Flags();
				rinfo.Pop_Material_Pass();
			}
		}//drawInfo exists so rendering a drawable.
		else
		{
			robj->Render(rinfo);
		}
	}//drawable or robj is not hidden

	rinfo.light_environment = NULL;
	rinfo.drawEffect = DRAW_EFFECT_NONE;
	if (doExtraMaterialPop)	//check if there is an extra material on the stack from the add'l material effect.
		rinfo.Pop_Material_Pass();
	if (doExtraFlagsPop)
		rinfo.Pop_Override_Flags();	//flags used to disable base pass and only render custom heat vision pass.
}

DECLARE_PERF_TIMER(translucentRender)

#ifdef DEBUG_LOGGING
extern Real TheTranslucentMS;
extern UnsignedInt TheTranslucentDraws;
extern UnsignedInt TheSortingPolygonsRefused;
extern Real TheSortingSortMS;
extern Real TheSortingCopyMS;
extern Real TheSortingDrawMS;
extern UnsignedInt TheSortingEntries;

static Real sceneElapsedMS( const Int64 &from, const Int64 &to )
{
	Int64 freq;
	freq = Clock_Ticks_Per_Second();
	if( freq < 1 )
		return 0.0f;
	return (Real)((double)(to - from) * 1000.0 / (double)freq);
}
#endif

/**Draw everything that was submitted from this scene*/
void RTS3DScene::Flush(RenderInfoClass & rinfo)
{
	//don't draw shadows in this mode because they interfere with destination alpha or are invisible (wireframe)
	if (m_customPassMode == SCENE_PASS_DEFAULT && Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
		DoShadows(rinfo, false);	//draw all non-stencil shadows (decals) since they fall under other objects.
	TheDX8MeshRenderer.Flush();	//draw all non-translucent objects.

	//draw all non-translucent objects which were separated because they are hidden and need custom rendering.
#ifdef USE_NON_STENCIL_OCCLUSION
	flushOccludedObjects(rinfo);
#else
	if (DX8Wrapper::Has_Stencil())
		flushOccludedObjectsIntoStencil(rinfo);
#endif

	// (gth) CNC3 Flush the shader meshes	
	SHD_FLUSH;

	// Draw the trees last so they alpha blend onto everything correctly.
	DoTrees(rinfo);

	//don't draw shadows in this mode because they interfere with destination alpha
	if (m_customPassMode == SCENE_PASS_DEFAULT && Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
		DoShadows(rinfo, true);	//draw all stencil shadows

	// The Ambient Occlusion option goes on under the water and every translucent draw, none of which
	// writes depth: the water takes no corner's shade from its bed and only the bed's shows through it.
	if (TheGlobalData->m_ambientOcclusion && !TheGlobalData->m_classicGraphics && m_customPassMode == SCENE_PASS_DEFAULT && Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
		Direct3D11_Ambient_Occlusion();

	WW3D::Render_And_Clear_Static_Sort_Lists(rinfo);	//draws things like water

	if (m_customPassMode == SCENE_PASS_DEFAULT && Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
		flushTranslucentObjects(rinfo);	//draw all translucent meshes which don't need per-poly sorting.

	{
		USE_PERF_TIMER(translucentRender)
#ifdef DEBUG_LOGGING
		// Timed as one block: the heat smudges at the end of DoParticles flush the sorting pool
		// themselves, so the line between building the quads and drawing them is not here.
		// doParticles times its own system loop into TheParticleFillMS.
		Int64 tTranslucentStart, tTranslucentEnd;
		const unsigned drawsBefore = DX8Wrapper::Get_Draw_Calls();
		const unsigned refusedBefore = SortingRendererClass::Get_Refused_Polygon_Count();
		SortingRendererClass::Reset_Flush_Profile();
		tTranslucentStart = Clock_Ticks();
#endif

		//don't draw transparent in this mode because they interfere with destination alpha
		if (m_customPassMode == SCENE_PASS_DEFAULT && Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
		{
			// The Direct3D 11 chain's ambient occlusion goes on here, over the world with every
			// corner in its depth and under the fire and smoke, which write none: put on the
			// finished frame it shaded a fireball by the chimneys and trees behind it.
			Direct3D11_Occlude_Scene();
			DoParticles(rinfo);	//queue up particles for rendering.
		}

		SortingRendererClass::Flush();	//draw sorted translucent polys like particles.
#ifdef DEBUG_LOGGING
		tTranslucentEnd = Clock_Ticks();
		TheTranslucentMS += sceneElapsedMS( tTranslucentStart, tTranslucentEnd );
		TheTranslucentDraws += DX8Wrapper::Get_Draw_Calls() - drawsBefore;
		TheSortingPolygonsRefused += SortingRendererClass::Get_Refused_Polygon_Count() - refusedBefore;
		{
			unsigned entries = 0;
			float sortMS = 0.0f, copyMS = 0.0f, drawMS = 0.0f;
			SortingRendererClass::Get_Flush_Profile( entries, sortMS, copyMS, drawMS );
			TheSortingEntries += entries;
			TheSortingSortMS += sortMS;
			TheSortingCopyMS += copyMS;
			TheSortingDrawMS += drawMS;
		}
#endif
	}
	TheDX8MeshRenderer.Clear_Pending_Delete_Lists();
}

/**Generate a predefined light environment(s) that will be applied to many objects.  Useful for things like totally fogged
objects and most generaic map objects that are not lit by dynamic lights.*/
void RTS3DScene::updateFixedLightEnvironments(RenderInfoClass & rinfo)
{
	//Figure out how dimly lit fogged objects should be compared to fully lit.
	Real foggedLightFrac = (Real)TheGlobalData->m_fogAlpha/(Real)TheGlobalData->m_clearAlpha;
	Real infantryLightScale;
	if( TheGlobalData->m_scriptOverrideInfantryLightScale != -1.0f )
		infantryLightScale = TheGlobalData->m_scriptOverrideInfantryLightScale;
	else
		infantryLightScale = TheGlobalData->m_infantryLightScale[TheGlobalData->m_timeOfDay];
	
	//Generate the default light environment
	m_defaultLightEnv.Reset(Vector3(0,0,0), Get_Ambient_Light());
	m_foggedLightEnv.Reset(Vector3(0,0,0), Get_Ambient_Light()*foggedLightFrac);

	Vector3 oldDiffuse, oldAmbient;
	for (Int globalLightIndex = 0; globalLightIndex < m_numGlobalLights; globalLightIndex++)
	{	m_defaultLightEnv.Add_Light(*m_globalLight[globalLightIndex]);
		//copy default lighting for infantry so we can tweak it.
		*m_infantryLight[globalLightIndex]=*m_globalLight[globalLightIndex];
		m_infantryLight[globalLightIndex]->Set_Transform(m_globalLight[globalLightIndex]->Get_Transform());

		m_globalLight[globalLightIndex]->Get_Diffuse(&oldDiffuse);
		m_globalLight[globalLightIndex]->Get_Ambient(&oldAmbient);
    oldDiffuse *= infantryLightScale; 
    oldAmbient *= infantryLightScale; 
    static Vector3 id (1.0f, 1.0f, 1.0f);
    oldDiffuse.Cap_Absolute_To(id);
    oldAmbient.Cap_Absolute_To(id);
		m_infantryLight[globalLightIndex]->Set_Ambient(oldAmbient);//CLAMPED
		m_infantryLight[globalLightIndex]->Set_Diffuse(oldDiffuse);//CLAMPED

		//copy the normal light for fog so we can modify it
		m_scratchLight->Set_Transform(m_globalLight[globalLightIndex]->Get_Transform());
		//modify light with attenuated value to adjust for fog.
		m_globalLight[globalLightIndex]->Get_Diffuse(&oldDiffuse);
		m_scratchLight->Set_Diffuse(oldDiffuse*foggedLightFrac);
		m_globalLight[globalLightIndex]->Get_Ambient(&oldAmbient);
		m_scratchLight->Set_Ambient(oldAmbient*foggedLightFrac);
		m_foggedLightEnv.Add_Light(*m_scratchLight);
	}

	m_defaultLightEnv.Pre_Render_Update(rinfo.Camera.Get_Transform());
	m_foggedLightEnv.Pre_Render_Update(rinfo.Camera.Get_Transform());

	m_infantryAmbient = Get_Ambient_Light();// * infantryLightScale;	//for now don't adjust ambient so that we don't lose directional lighting.
}

/**Generate custom rendering passes for each potential player color.  This is currently only used
to render occluded objects using the color of the player*/
void RTS3DScene::updatePlayerColorPasses(void)
{
#ifdef USE_NON_STENCIL_OCCLUSION
	Vector3 hsv,rgb;

	if (behindBuildingMarkersEnabled() && TheGameLogic->getShowBehindBuildingMarkers())
	{
		Int numPlayers=ThePlayerList->getPlayerCount();

		for (Int i=0; i<numPlayers; i++)
		{	Player *player=ThePlayerList->getNthPlayer(i);
			Int playerIndex=player->getPlayerIndex();
			Real red,green,blue,alpha;
			GameGetColorComponentsReal(clientPlayerColor(player),&red,&green,&blue,&alpha);
			RGB_To_HSV(hsv,Vector3(red,green,blue));
			hsv.Z*=TheGlobalData->m_occludedLuminanceScale;
			HSV_To_RGB(rgb,hsv);
			VertexMaterialClass *vmat=m_occludedMaterialPass[playerIndex]->Peek_Material();
			vmat->Set_Emissive(rgb);
		}
	}
#endif
}

#define ZBias 0.0001f

DECLARE_PERF_TIMER(NonTerrainRender)
void RTS3DScene::Render(RenderInfoClass & rinfo)
{
	// Covers both submission and Flush, including sorted draws. Nested reflections and
	// custom passes cannot borrow the main view's map or its stencil ownership.
	SunShadow::SceneScope sunScene(DX8Wrapper::_Get_D3D_Device(),
		m_customPassMode == SCENE_PASS_DEFAULT && !ShaderClass::Is_Backface_Culling_Inverted()
		&& Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE);
	USE_PERF_TIMER(NonTerrainRender)
	DX8Wrapper::Set_Fog(FogEnabled, FogColor, FogStart, FogEnd);

	if (Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
	{
		if (m_customPassMode == SCENE_PASS_DEFAULT)
		{	//Regular rendering pass with no effects
			updatePlayerColorPasses();///@todo: this probably doesn't need to be done each frame.
			updateFixedLightEnvironments(rinfo);
			Customized_Render(rinfo);
			Flush(rinfo);
		}
		else
		if (m_customPassMode == SCENE_PASS_ALPHA_MASK)
		{
			//a projected alpha texture which will later be used to determine where
			//wireframe should be visible.
			///@todo: Clearing to black may not be needed if the scene already did the clear.
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);
			DX8Wrapper::Set_DX8_ZBias(0);
			//Since all objects will be rendered with same material, disable resetting until all are done.
			m_maskMaterialPass->setAllowUninstall(FALSE);

			Customized_Render(rinfo);	//render mask into alpha channel and fill z-buffer with depth values.
			Flush(rinfo);
			m_maskMaterialPass->setAllowUninstall(TRUE);
			m_maskMaterialPass->UnInstall_Materials();

			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED);

			ShaderClass::Invalidate();
		}
	}
	else
	{
		Bool old_enable=WW3D::Is_Texturing_Enabled();
		if (Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_CLEAR_LINE)
		{	//render scene with solid black color but have destination alpha store
			//a projected alpha texture which will later be used to determine where
			//wireframe should be visible.
			///@todo: Clearing to black may not be needed if the scene already did the clear.
			DX8Wrapper::Clear(true, false, Vector3(0.0f,0.0f,0.0f),1.0f);	// Clear color but not z
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);
			DX8Wrapper::Set_DX8_ZBias(0);
			
			//We're only filling the z-buffer so ignore normal textures and state changes to speed things up.
			m_customPassMode = SCENE_PASS_ALPHA_MASK;
			m_maskMaterialPass->setAllowUninstall(FALSE);

			Customized_Render(rinfo);	//render mask into alpha channel and fill z-buffer with depth values.
			Flush(rinfo);

			m_maskMaterialPass->setAllowUninstall(TRUE);
			m_maskMaterialPass->UnInstall_Materials();
			
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED);
			WW3D::Enable_Coloring(0xff008000);
			WW3D::Enable_Texturing(false);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);

			//Move maximum z-buffer value in a little to shift all z-values closer
			//and thus forcing line to appear on top of previous pass.
			Real nearZ,farZ;
			rinfo.Camera.Get_Zbuffer_Range(nearZ, farZ);
			rinfo.Camera.Set_Zbuffer_Range(nearZ, farZ-ZBias);
			rinfo.Camera.Apply();

//			DX8Wrapper::Set_DX8_Render_State (D3DRS_ZBIAS, 4);
			Customized_Render(rinfo);	//render wireframe where z-test passes
			Flush(rinfo);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);

			rinfo.Camera.Set_Zbuffer_Range(nearZ, farZ);
			rinfo.Camera.Apply();

//			DX8Wrapper::Set_DX8_ZBias(0);
			WW3D::Enable_Texturing(old_enable);
			WW3D::Enable_Coloring(0);

			ShaderClass::Invalidate();
		}
		else
		{	//old W3D custom rendering code.

			//Disable writes to color buffer to save memory bandwidth - we only need Z.
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,0);
			DX8Wrapper::Set_DX8_ZBias(0);
			Customized_Render(rinfo);
			Flush(rinfo);
			//Re-enable writes to color buffer.
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED);

			switch (Get_Extra_Pass_Polygon_Mode()) {
			case EXTRA_PASS_LINE:
				WW3D::Enable_Texturing(false);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
				DX8Wrapper::Set_DX8_ZBias(7);
				Customized_Render(rinfo);
				break;
			case EXTRA_PASS_CLEAR_LINE:
				DX8Wrapper::Clear(true, false, Vector3(0.0f,0.0f,0.0f), 0.0f);	// Clear color but not z
				WW3D::Enable_Texturing(false);
				WW3D::Enable_Coloring(0xff008000);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
				DX8Wrapper::Set_DX8_ZBias(7);
				Customized_Render(rinfo);
				break;
			}
			Flush(rinfo);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);
			DX8Wrapper::Set_DX8_ZBias(0);
			WW3D::Enable_Texturing(old_enable);
			WW3D::Enable_Coloring(0);
			ShaderClass::Invalidate();
		}
	}
}

//=============================================================================
// RTS3DScene::Customized_Renderer
//=============================================================================
/** Custom render method for the RTS3DScene, custom render properties for our
  * particular game go here */
//=============================================================================
//-------------------------------------------------------------------------------------------------
/** The dynamic point lights (explosions, muzzle flashes, burning wrecks) for the Direct3D 11 frame
		to light per pixel (BLAST_LIGHT_SAMPLING), the BLAST_LIGHT_SLOTS nearest the camera.  The
		colours go over divided by the map's terrain light, the way the headlights' gain is worked out
		(W3DModelDraw::lightHeadlights), so the shader multiplies the lit pixel by one plus the sum.
		Lights the terrain relight would skip (a near reach under a tenth of a unit) are skipped here
		too.  Client only: the lights are what FX lists put up and nothing in logic reads them. */
static void handBlastLights(RefRenderObjListClass &dynamicLights, const Vector3 &camera)
{
	if (!Direct3D11_Lights_Per_Pixel())
		return;

	static std::vector< std::pair<Real, W3DDynamicLight*> > nearest;
	nearest.clear();
	RefRenderObjListIterator it(&dynamicLights);
	for (it.First(); !it.Is_Done(); it.Next())
	{
		W3DDynamicLight *light = (W3DDynamicLight*)it.Peek_Obj();
		if (!light->isEnabled() || light->Get_Type() != LightClass::POINT)
			continue;
		float nearReach, farReach;
		light->Get_Far_Attenuation_Range(nearReach, farReach);
		if (nearReach < 0.1f || farReach <= 0.0f)
			continue;
		const Vector3 d = light->Get_Position() - camera;
		nearest.push_back(std::make_pair(d.Length2(), light));
	}
	// ponytail: nearest the camera, not nearest the screen's middle; weigh by on-screen reach if a
	// barrage just off the edge ever steals slots from the fight in view.
	std::sort(nearest.begin(), nearest.end(),
		[](const std::pair<Real, W3DDynamicLight*> &a, const std::pair<Real, W3DDynamicLight*> &b)
		{ return a.first < b.first; });

	const RGBColor &ambient = TheGlobalData->m_terrainAmbient[0];
	const RGBColor &diffuse = TheGlobalData->m_terrainDiffuse[0];
	const Real map[3] = { ambient.red + diffuse.red, ambient.green + diffuse.green, ambient.blue + diffuse.blue };
	const Real mean = (map[0] + map[1] + map[2]) / 3.0f;
	Real base[3];
	for (Int c = 0; c < 3; ++c)
		base[c] = sqrtf((map[c] > 0.15f ? map[c] : 0.15f) * (mean > 0.15f ? mean : 0.15f));

	static float lights[BLAST_LIGHT_SLOTS][BLAST_LIGHT_FLOATS];
	unsigned count = 0;
	// The Firelight option's fires come first, so a barrage's muzzle flashes cannot take a burning base
	// off the ground for a frame and give it back the next; FireLights::MAX_LIGHTS leaves the pulses the rest.
	Int fireCount;
	const FireLights::Light *fires = getFirelights(&fireCount);
	for (Int i = 0; i < fireCount; ++i)
	{
		const FireLights::Light &fire = fires[i];
		float *slot = lights[count++];
		slot[0] = fire.x; slot[1] = fire.y; slot[2] = fire.z; slot[3] = fire.reach;
		slot[4] = fire.red / base[0]; slot[5] = fire.green / base[1]; slot[6] = fire.blue / base[2];
		slot[7] = fire.inner;
		slot[8] = slot[4] * FireLights::AMBIENT_SHARE; slot[9] = slot[5] * FireLights::AMBIENT_SHARE; slot[10] = slot[6] * FireLights::AMBIENT_SHARE;
		slot[11] = 0.0f;
	}
	for (size_t i = 0; i < nearest.size() && count < BLAST_LIGHT_SLOTS; ++i)
	{
		W3DDynamicLight *light = nearest[i].second;
		float nearReach, farReach;
		light->Get_Far_Attenuation_Range(nearReach, farReach);
		Vector3 position = light->Get_Position(), lightDiffuse, lightAmbient;
		light->Get_Diffuse(&lightDiffuse);
		light->Get_Ambient(&lightAmbient);
		float *slot = lights[count++];
		slot[0] = position.X; slot[1] = position.Y; slot[2] = position.Z; slot[3] = farReach;
		slot[4] = lightDiffuse.X / base[0]; slot[5] = lightDiffuse.Y / base[1]; slot[6] = lightDiffuse.Z / base[2];
		slot[7] = nearReach;
		slot[8] = lightAmbient.X / base[0]; slot[9] = lightAmbient.Y / base[1]; slot[10] = lightAmbient.Z / base[2];
		slot[11] = 0.0f;
	}
	Direct3D11_Set_Blast_Lights(&lights[0][0], count);
}

void RTS3DScene::Customized_Render( RenderInfoClass &rinfo )
{
#ifdef DIRTY_CONDITION_FLAGS
	StDrawableDirtyStuffLocker lockDirtyStuff;
#endif

	RenderObjClass *terrainObject=NULL,*robj;
	m_translucentObjectsCount = 0;	//start of new frame so no translucent objects
	m_occludedObjectsCount = 0;

	/* The sun's depth pass runs before the frame rather than with the shadows, because it flushes
		 the mesh renderer and drains the sort lists: run in the middle of a frame it drew the
		 helicopters' rotor discs into the shadow map instead of onto the screen, and they went
		 missing from both.  Here nothing is queued yet.  SHADOW-MAP-PLAN.md phase 1.

		 "Here" is the frame's own pass and no other.  A map whose water reflects draws the scene a
		 second time into the mirror texture, from inside WaterRenderObjClass::Render, which is
		 itself inside the frame this function started - so that second Customized_Render reached
		 this line with the whole frame already queued, and the drain took the surf and the
		 helicopters off the screen and into a depth buffer.  The main menu's shell map is a water
		 map, which is where it showed.  The guards are the ones queueShadows uses further down. */
	if (TheW3DVolumetricShadowManager != NULL && m_customPassMode == SCENE_PASS_DEFAULT &&
		!ShaderClass::Is_Backface_Culling_Inverted() &&
		Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
	{
		TheW3DVolumetricShadowManager->renderShadowMap(rinfo.Camera);
		// Before the frame for the same reason: it has to lie under all of it.  The pass above has just
		// handed the backend the camera it needs.
		if (TheGlobalData->m_atmosphere)
			Direct3D11_Atmosphere_Sky();
	}

	Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;

#define USE_LIGHT_ENV 1

   if (!Visibility_Checked) {
      // set the visibility bit in all render objects in all layers.
	   Visibility_Check(&rinfo.Camera);
#ifdef USE_NON_STENCIL_OCCLUSION
	   flagOccludedObjects(&rinfo.Camera);
#endif
   }
   Visibility_Checked = false;

	if (m_customPassMode == SCENE_PASS_DEFAULT && !ShaderClass::Is_Backface_Culling_Inverted())
		parkSpentDynamicLights();

	RefRenderObjListIterator it(&UpdateList);
	// allow all objects in the update list to do their "every frame" processing
	for (it.First(); !it.Is_Done(); it.Next()) {
		RenderObjClass * robj = it.Peek_Obj();
		if (robj->Class_ID() == RenderObjClass::CLASSID_TILEMAP)
			terrainObject=robj;	//found terrain object, store for later.
		if (!ShaderClass::Is_Backface_Culling_Inverted()) {
			// If we are doing water mirror, we draw with backface culling inverted.  In this case, 
			// we only want to call On_Frame_Update if we aren't drawing water, as otherwise
			// we get 2 frame updates per frame, and it screws up the particle emitters.
			it.Peek_Obj()->On_Frame_Update();
		}
	}

	Direct3D11_Set_Hemi_Ambient(TheGlobalData->m_hemiAmbient);

	// After the lights' fade has stepped and before the terrain and the models are lit.
	if (m_customPassMode == SCENE_PASS_DEFAULT && !ShaderClass::Is_Backface_Culling_Inverted())
		handBlastLights(m_dynamicLightList, rinfo.Camera.Get_Position());

	//terrain needs to be rendered first
	if (terrainObject)	// Don't check visibility - terrain is always visible. jba.
	{		
		robj=terrainObject;
		rinfo.light_environment = NULL;		// Terrain is self lit.
		rinfo.Camera.Set_User_Data(this);	//pass the scene to terrain via user data.
		if (m_customPassMode == SCENE_PASS_DEFAULT && m_shroudMaterialPass)
		{
			rinfo.Push_Material_Pass(m_shroudMaterialPass);
			robj->Render(rinfo);
			rinfo.Pop_Material_Pass();
		}
		else
		if (m_customPassMode == SCENE_PASS_ALPHA_MASK && m_maskMaterialPass)
		{
			rinfo.Push_Material_Pass(m_maskMaterialPass);
			robj->Render(rinfo);
			rinfo.Pop_Material_Pass();
		}
		else
		robj->Render(rinfo);
	}

	if (m_drawTerrainOnly) {
		return;
	}
#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableObjects) { 
		return;
	}
#endif

	// loop through the visible render objects: m_visibleObjects is RenderList's visible ones in its
	// order (Add_Render_Object), and the ~85% of the list off the screen cost this walk most of its
	// time.  By index, so an object removed during renderOneObject() only leaves a null behind.
	for (size_t v = 0; v < m_visibleObjects.size(); ++v)
	{
		robj = m_visibleObjects[v];
		if (robj == NULL || robj->Class_ID() == RenderObjClass::CLASSID_TILEMAP)
			continue;	//we already rendered terrain

		if (robj->Is_Really_Visible()) {
			DrawableInfo *drawInfo = (DrawableInfo *)robj->Get_User_Data();
			Drawable *draw=NULL;
			if (drawInfo)
				draw = drawInfo->m_drawable;
#ifdef USE_NON_STENCIL_OCCLUSION
			if (!(draw && drawInfo->m_flags & DrawableInfo::ERF_DELAYED_RENDER))	//model rendering is delayed for some reason until end of normal scene
#else
			if (!(draw && drawInfo->m_flags & (DrawableInfo::ERF_DELAYED_RENDER|DrawableInfo::ERF_POTENTIAL_OCCLUDER|DrawableInfo::ERF_IS_NON_OCCLUDER_OR_OCCLUDEE)))	//in this mode we delay almost all objects in order to do correct sorting with stencil.
#endif
				renderOneObject(rinfo, robj, localPlayerIndex);
		}
	}

#ifdef	INCLUDE_GRANNY_IN_BUILD
	if (TheGrannyRenderObjSystem)
	{	//we only want to update granny animations once per frame, so only queue
		//them up if this is not a mirror pass.
		if (!ShaderClass::Is_Backface_Culling_Inverted())
			TheGrannyRenderObjSystem->queueUpdate();
		TheGrannyRenderObjSystem->Flush();
	}
#endif
	
	//Tell shadow manager to render shadows at the end of this frame
	//Don't draw shadows if there is no terrain present.
	if (TheW3DShadowManager && terrainObject && !ShaderClass::Is_Backface_Culling_Inverted() &&
		Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
		TheW3DShadowManager->queueShadows(TRUE);

	// only render particles once per frame
	if (terrainObject != NULL && TheParticleSystemManager != NULL &&
		Get_Extra_Pass_Polygon_Mode() == EXTRA_PASS_DISABLE)
	{	TheParticleSystemManager->queueParticleRender();
	}

}  // end Customized_Renderer

/**Convert a player index to a color index, we use this because color indices are
assigned in left-right binary flipped fashion so as not to occupy lower bits unless
necessary.  This is used because lower bits are free for use by stencil shadow
rendering*/
#define NUMBER_PLAYER_COLOR_BITS 4	//need 4 bits to encode 8 players because indices start at 1 (not 0).
Int playerIndexToColorIndex(Int playerIndex)
{
	Int tmp=playerIndex;
	Int result=0;
	Int flippedPosition;

	//Player index is stored in 4 bits
	for (Int i=0; i<NUMBER_PLAYER_COLOR_BITS; i++)
	{
		flippedPosition = NUMBER_PLAYER_COLOR_BITS-1-i;	//correct position of bit after it's flipped left/right

		if (flippedPosition > i)
		{	//shifting left
			result |= (tmp & (1<<i))<<(flippedPosition-i);
		}
		else
		{	//shifting right
			result |= (tmp & (1<<i))>>(i-flippedPosition);
		}
	}
	return result;
}

/**Utility function used to render a full screen quad with the specified color and
stencil mask*/
void renderStenciledPlayerColor( UnsignedInt color, UnsignedInt stencilRef, Bool clear=FALSE)
{
	struct _TRANSLITVERTEX {
	    Vector4 p;
		UnsignedInt color;   // diffuse color    
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

    v[0].p.Set(xpos+width, ypos+height, 0.0f, 1.0f );
    v[1].p.Set(xpos+width, 0, 0.0f, 1.0f );
    v[2].p.Set(xpos, ypos+height, 0.0f, 1.0f );
    v[3].p.Set(xpos,  0, 0.0f, 1.0f );
    v[0].color = color;
    v[1].color = color;
    v[2].color = color;
    v[3].color = color;

	DX8Wrapper::Set_Shader(PlayerColorShader);
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Apply_Render_State_Changes();	//force update all renderstates

	LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

	if (!m_pDev)
		return;	//need device to render anything.

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	// Through the wrapper rather than on the device: _Draw_DX8_Primitive_UP mirrors the strip into
	// the Direct3D 11 backend, which reads the format the wrapper last set and would otherwise
	// build the layout out of whatever the previous pass left behind.
	DX8Wrapper::Set_Vertex_Format(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

	// Set stencil states
	DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, TRUE );
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE );
	RenderUInt32	oldColorWriteEnable=0x12345678;
	if (clear)
	{	//we want to clear the stencil buffer to some known value whereever a player index is stored
		Int occludedMask=TheW3DShadowManager->getStencilShadowMask();
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILREF,      0x80808080 );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILMASK,     occludedMask );	//isolate bits containing occluder|playerIndex
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILWRITEMASK,0xffffffff );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFUNC,  D3DCMP_LESS );	//only draw to pixels that match the reference value
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_REPLACE );	
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILPASS,  D3DSTENCILOP_REPLACE );	//pixels which had occluded player colors, get MSB set.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFAIL,  D3DSTENCILOP_ZERO );	//pixels which had no occluded player colors are cleared.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_NEVER  );	//fail all access to the frame buffer to improve memory bandwidth

		//disable writes to color buffer
		if (DX8Wrapper::Get_Current_Caps()->Get_DX8_Caps().PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE)
		{	DX8Wrapper::_Get_D3D_Device()->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWriteEnable);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,0);
		}
		else
		{	//device does not support disabling writes to color buffer so fake it through alpha blending
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ZERO );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ONE );
		}
	}
	else
	{	DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILREF,      stencilRef );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILMASK,     0xffffffff );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILWRITEMASK,0xffffffff );	
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFUNC,  D3DCMP_EQUAL );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILPASS,  D3DSTENCILOP_KEEP );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFAIL,  D3DSTENCILOP_KEEP );

		//Make occluded pixels transparent
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
	}

	if (DX8Wrapper::_Is_Triangle_Draw_Enabled())
		DX8Wrapper::_Draw_DX8_Primitive_UP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANSLITVERTEX));

	// turn off the stencil buffer
	DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE );
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);	//restore shader state
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE );
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO );
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_ALWAYS);

	if (oldColorWriteEnable != 0x12345678)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,oldColorWriteEnable);

}  // end renderStencilShadows

#define MAX_VISIBLE_OCCLUDED_PLAYER_OBJECTS	512 //maximum number of occluded objects permitted per player
void RTS3DScene::flushOccludedObjectsIntoStencil(RenderInfoClass & rinfo)
{
	RenderObjClass *robj;
	Drawable *draw;
	RenderObjClass *playerObjects[MAX_PLAYER_COUNT][MAX_VISIBLE_OCCLUDED_PLAYER_OBJECTS];
	RenderObjClass **lastPlayerObject[MAX_PLAYER_COUNT];
	Int playerColorIndex[MAX_PLAYER_COUNT];
	Int visiblePlayerColors[MAX_PLAYER_COUNT];	///<color assigned to each of the visible players
	Int numObjects;
	Vector3 hsv,rgb;

	Int usedPlayerColorIndex=1;
	Int numVisiblePlayerColors=0;

	//Clear pointers into temporary arrays where each player's objects will be stored.
	//We do this so that all objects are sorted by color which reduces the number of
	//state changes needed when drawing them.
	Int i;	// VC6 leaked it out to every loop below
	for (i=0; i<MAX_PLAYER_COUNT; i++)
	{	lastPlayerObject[i]=&playerObjects[i][0];
		playerColorIndex[i]=-1;
	}

	//Assume no player colors are visible and all stencil bits are free for use by shadows.
	TheW3DShadowManager->setStencilShadowMask(0);

	Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;

	if (m_numPotentialOccludees && m_numPotentialOccluders)
	{
		//bucket sort all possibly occluded objects by player index/color.
		for (Int i=0; i<m_numPotentialOccludees; i++)
		{
			robj=m_potentialOccludees[i];

			draw = ((DrawableInfo *)robj->Get_User_Data())->m_drawable;
			Int index=draw->getObjectForDraw().playerIndex;

			if ((lastPlayerObject[index]-&playerObjects[index][0]) >= MAX_VISIBLE_OCCLUDED_PLAYER_OBJECTS)
			{
				DEBUG_ASSERTCRASH(FALSE,("Exceeded Maximum Number of potentially occluded models"));
				continue;
			}

			*lastPlayerObject[index] = robj;
			lastPlayerObject[index]++;	//increment to next object
		}

		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, TRUE );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILMASK, 0xffffffff);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILWRITEMASK, 0xffffffff);
		//Always store player index into stencil unless it is occluded by another
		//player's potentially occluded objects.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFUNC,  D3DCMP_ALWAYS );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFAIL,  D3DSTENCILOP_KEEP );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILPASS,  D3DSTENCILOP_REPLACE );

		//Find out which player indices are actually used and remap them to
		//a color index.  Render all objects using the same color index at once.
		//We render potential occludees first because this allows them to z-sort correctly
		//when they are behind an occluder.
		for (i=0; i<MAX_PLAYER_COUNT; i++)
		{
			if ((numObjects=lastPlayerObject[i]-&playerObjects[i][0]) != 0)
			{	
				//this player has some objects so draw them using his color index.
				if (playerColorIndex[i]==-1)	//color index not assigned yet?
				{	//assign a new color index to this player
					playerColorIndex[i]=playerIndexToColorIndex(usedPlayerColorIndex++);
					//assign a color to this index by copying it from the controlling player
					//of all objects in this list.
					draw = ((DrawableInfo *)playerObjects[i][0]->Get_User_Data())->m_drawable;
					Int color=draw->getObjectForDraw().playerColor;
					RGB_To_HSV(hsv,Vector3(((color>>16)&0xff)/255.0f,((color>>8)&0xff)/255.0f,(color &0xff)/255.0f));
					hsv.Z*=TheGlobalData->m_occludedLuminanceScale;
					HSV_To_RGB(rgb,hsv);
					visiblePlayerColors[numVisiblePlayerColors++]=DX8Wrapper::Convert_Color(rgb,0.5f);
				}

				Int thisPlayerColorIndex=playerColorIndex[i];

				//Store this object's color index into bits 3-6 of stencil buffer
				DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILREF, thisPlayerColorIndex<<3);

				//Render all of this player's objects for which we care when they are occluded.
				RenderObjClass **renderList=&playerObjects[i][0];
				for (Int j=0; j<numObjects; j++)
				{
					DrawableInfo *drawInfo=((DrawableInfo *)(*renderList)->Get_User_Data());
					if (drawInfo->m_flags & DrawableInfo::ERF_IS_TRANSLUCENT)
					{
						TheDX8MeshRenderer.Flush();	//render all the submitted meshes using current stencil function
						SHD_FLUSH;
						//Disable writing to color buffer since translucent objects are rendered at end of frame.
						DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFUNC,  D3DCMP_NEVER );	//never allow frame buffer writes.
						DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFAIL,  D3DSTENCILOP_REPLACE );	//always replace existing stencil value
						renderOneObject(rinfo, (*renderList), localPlayerIndex);
						TheDX8MeshRenderer.Flush();	//render all the submitted meshes using current stencil function
						SHD_FLUSH;
						DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFAIL,  D3DSTENCILOP_KEEP );
						DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFUNC,  D3DCMP_ALWAYS );
					}
					else
						renderOneObject(rinfo, (*renderList), localPlayerIndex);
					renderList++;	//advance to next object
				}

				TheDX8MeshRenderer.Flush();	//render all the submitted meshes using current stencil function
			}
		}
		//Stencil buffer is now filled with color indices of potentially occluded objects.  We now draw
		//non-occluder or occludee objects such as small rocks, shrubs, etc. which we don't care about
		//but need to render here so that they don't interfere with building occlusion.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE );	//these objects are not stored in stencil
		RenderObjClass **nonOccluderOrOccludeeList=m_nonOccludersOrOccludees;
		for (i=0; i<m_numNonOccluderOrOccludee; i++)
		{
			renderOneObject(rinfo, (*nonOccluderOrOccludeeList), localPlayerIndex);
			nonOccluderOrOccludeeList++;	//advance to next one
		}
		TheDX8MeshRenderer.Flush();	//render all the submitted meshes using current stencil function

		//Stencil buffer is now filled with color indices of potentially occluded objects.  We now draw
		//occluder objects so they cover up and modify stencil MSB wherever they are in front of other objects.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, TRUE );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILREF, 0xffffffff);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILMASK, 0xffffffff);	//isolate lowest player color
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILWRITEMASK, 0x80);	//only write to MSB
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFUNC,  D3DCMP_ALWAYS );	//check if player colors stored in pixel
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFAIL,  D3DSTENCILOP_KEEP );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILPASS,  D3DSTENCILOP_REPLACE );

		//Render all potential occluders on top of already rendered potential occludees.
		RenderObjClass **occluderList=m_potentialOccluders;
		for (i=0; i<m_numPotentialOccluders; i++)
		{
			renderOneObject(rinfo, (*occluderList), localPlayerIndex);
			occluderList++;	//advance to next one
		}

		TheDX8MeshRenderer.Flush();	//render all the submitted meshes using current stencil function

		//We now have a stencil buffer where pixels that are occluded have a bit pattern of 1INDX000.
		//INDX contains the occluded player's color index.  We walk through all the player colors and
		//draw them wherever the stencil matches the color's index.
		Int usedPlayerColorBits=0;
		for (i=0; i<numVisiblePlayerColors; i++)
		{
			Int color=visiblePlayerColors[i];
			Int stencilRef=(playerIndexToColorIndex(i+1)<<3)|0x80;
			renderStenciledPlayerColor(color,stencilRef);
			usedPlayerColorBits |= stencilRef;	//keep track of all bits used for occlusion/player colors.
		}

		TheW3DShadowManager->setStencilShadowMask(usedPlayerColorBits);
		if (numVisiblePlayerColors >= 8 && TheGlobalData->m_useShadowVolumes)
		{	//for cases where we have 8 or more visible players, we're only left with 3 bits to store
			//stencil shadows.  That's probably not enough since it will only allow 7 overlapping shadows.
			//So we clear the stencil buffer, leaving only the MSB set on any occluded player pixels so that
			//shadow code knows not to overwrite these pixels.
			renderStenciledPlayerColor(0,0, TRUE);
			TheW3DShadowManager->setStencilShadowMask(0x80808080);	//msb indicates occluded player pixels so ignore it when filling screen with shadow
		}

		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE );
	}
	else
	if (m_numNonOccluderOrOccludee || m_numPotentialOccluders || m_numPotentialOccludees)
	{	//no occluded objects so don't need to render anything special.  Just draw the queued up
		//objects like normal because they were skipped in the main scene traversal.

		// A translucent occludee is drawn by the translucent pass; drawn here as well it came out
		// solid, a stealthed unit under its own see-through copy, whenever no building was in view.
		RenderObjClass **occludeeList=m_potentialOccludees;
		for (i=0; i<m_numPotentialOccludees; i++)
		{
			const DrawableInfo *occludeeInfo = (const DrawableInfo *)(*occludeeList)->Get_User_Data();
			if (!(occludeeInfo->m_flags & DrawableInfo::ERF_IS_TRANSLUCENT))
				renderOneObject(rinfo, (*occludeeList), localPlayerIndex);
			occludeeList++;	//advance to next one
		}

		RenderObjClass **occluderList=m_potentialOccluders;
		for (i=0; i<m_numPotentialOccluders; i++)
		{
			renderOneObject(rinfo, (*occluderList), localPlayerIndex);
			occluderList++;	//advance to next one
		}

		RenderObjClass **nonOccluderOrOccludeeList=m_nonOccludersOrOccludees;
		for (i=0; i<m_numNonOccluderOrOccludee; i++)
		{
			renderOneObject(rinfo, (*nonOccluderOrOccludeeList), localPlayerIndex);
			nonOccluderOrOccludeeList++;	//advance to next one
		}
		TheDX8MeshRenderer.Flush();	//render all the submitted meshes using current stencil function
	}

	//Reset scene ambient because we sometimes mess around with it to make objects
	//glow, etc. when processing drawables.  This is a good place to do it because this
	//function gets called right after we flush regular render objects.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,DX8Wrapper::Convert_Color(this->Get_Ambient_Light(),0.0f));
}

/*Version which does not require stencil buffer*/
void RTS3DScene::flushOccludedObjects(RenderInfoClass & rinfo)
{
	RenderObjClass *robj;
	Drawable *draw;

	TheW3DShadowManager->setStencilShadowMask(0);

	if (m_occludedObjectsCount)
	{
		Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;

		if (DX8Wrapper::Has_Stencil())	//just in case we have shadows, disable them over occluded pixels.
		{
			//Set all stencil pixels of potentially occluded objects to 128.
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, TRUE );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILREF,      128 );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILMASK,     0xffffffff );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILWRITEMASK,0xffffffff );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFAIL,  D3DSTENCILOP_KEEP );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILPASS,  D3DSTENCILOP_REPLACE );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILFUNC,  D3DCMP_ALWAYS );
		}

		//First draw all the solid colored models
		///@todo: Optimize this so that the extra passes don't actually install the material since it's all the same.
		rinfo.Push_Override_Flags(RenderInfoClass::RINFO_OVERRIDE_ADDITIONAL_PASSES_ONLY);	//disable textures
		Int i;	// VC6 leaked it out to the second pass below
		for (i=0; i<m_occludedObjectsCount; i++)
		{
			robj=m_potentialOccludees[i];

			draw = ((DrawableInfo *)robj->Get_User_Data())->m_drawable;
			Int index=draw->getObjectForDraw().playerIndex;

			rinfo.Push_Material_Pass(m_occludedMaterialPass[index]);
			robj->Render(rinfo);
			rinfo.Pop_Material_Pass();
		}
		rinfo.Pop_Override_Flags();
		TheDX8MeshRenderer.Flush();

		//Now draw the normal models so they cover up the colored models on any pixels that

		//Now draw the normal models so they cover up the colored models on any pixels that
		//Normal models will clear stencil value from 128 back to 0 where the object pixels are
		//not occluded but will leave  128 in stencil where still occluded.
		if (DX8Wrapper::Has_Stencil())
			DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILREF,      0 );

		for (i=0; i<m_occludedObjectsCount; i++)
		{
			robj=m_potentialOccludees[i];
			renderOneObject(rinfo, robj, localPlayerIndex);//WW3D::Render(*robj,rinfo);
		}

		//Flush all the submitted translucent objects.
		TheDX8MeshRenderer.Flush();
		m_occludedObjectsCount = 0;
		DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE );
		TheW3DShadowManager->setStencilShadowMask(0x80808080);	//upper MSB always contains flag indicating occluded player color.
	}

	//Reset scene ambient because we sometimes mess around with it to make objects
	//glow, etc. when processing drawables.  This is a good place to do it because this
	//function gets called right after we flush regular render objects.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,DX8Wrapper::Convert_Color(this->Get_Ambient_Light(),0.0f));
}

void RTS3DScene::flushTranslucentObjects(RenderInfoClass & rinfo)
{
	RenderObjClass *robj;
	Drawable *draw;

	if (m_translucentObjectsCount)
	{
		Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;

		for (Int i=0; i<m_translucentObjectsCount; i++)
		{
			robj=m_translucentObjectsBuffer[i];
			draw = ((DrawableInfo *)robj->Get_User_Data())->m_drawable;

			rinfo.alphaOverride = draw->getEffectiveOpacity();

			theTranslucentPass = TRUE;
			renderOneObject(rinfo, robj, localPlayerIndex);//WW3D::Render(*robj,rinfo);
			theTranslucentPass = FALSE;
		}

		//Flush all the submitted translucent objects.
		TheDX8MeshRenderer.Flush();
		WW3D::Render_And_Clear_Static_Sort_Lists(rinfo);	//draws things like water
		rinfo.alphaOverride = 1.0f;	//disable forced alpha
		m_translucentObjectsCount = 0;
	}

	//Reset scene ambient because we sometimes mess around with it to make objects
	//glow, etc. when processing drawables.  This is a good place to do it because this
	//function gets called right after we flush regular render objects.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,DX8Wrapper::Convert_Color(this->Get_Ambient_Light(),0.0f));
}

//=============================================================================
// RTS3DScene::createLightsIterator
//=============================================================================
/** Returns an iterator of the lights in the scene. */
//=============================================================================
RefRenderObjListIterator * RTS3DScene::createLightsIterator(void)
{
	RefRenderObjListIterator * it = NEW RefRenderObjListIterator(&LightList);	// poolify
	return it;
}


//=============================================================================
// RTS3DScene::destroyLightsIterator
//=============================================================================
/** Destroys the iterator returned by createLightsIterator. */
//=============================================================================
void RTS3DScene::destroyLightsIterator(RefRenderObjListIterator * it)
{
	delete it;
}


//=============================================================================
// RTS3DScene::addDynamicLight
//=============================================================================
/** Adds a dynamic light. */
//=============================================================================
void RTS3DScene::addDynamicLight(W3DDynamicLight * obj)
{
	m_dynamicLightList.Add(obj);
	UpdateList.Add(obj);
}

//=============================================================================
// RTS3DScene::addDynamicLight
//=============================================================================
/** Adds a dynamic light. */
//=============================================================================
W3DDynamicLight * RTS3DScene::getADynamicLight(void)
{
		RefRenderObjListIterator dynaLightIt(&m_dynamicLightList);
		W3DDynamicLight *pLight;
		for (dynaLightIt.First(); !dynaLightIt.Is_Done(); dynaLightIt.Next())
		{		
			pLight = (W3DDynamicLight*)dynaLightIt.Peek_Obj();
			if (!pLight->isEnabled()) {
				pLight->setEnabled(true);
				return(pLight);
			}
		}
		if (m_spentDynamicLights.empty())
			pLight = NEW_REF(W3DDynamicLight, ());
		else
		{
			pLight = m_spentDynamicLights.back();
			m_spentDynamicLights.pop_back();
		}
		addDynamicLight( pLight );
		pLight->Release_Ref();
		pLight->setEnabled(true);
		return(pLight);
}

//=============================================================================
// RTS3DScene::parkSpentDynamicLights
//=============================================================================
/** Takes the lights that are off and out of the terrain's tiles out of both lists.  The pool used
	* to keep every light the busiest moment of the match had needed, mostly off, and the five walks
	* over it a frame (the fade, the terrain, the blast lights, the smoke's pulses, a model's own
	* lights under -d3d9) read each one: a late match had thousands to step over, all of it cache
	* misses the logic thread's own walks then had to share the memory with. */
//=============================================================================
void RTS3DScene::parkSpentDynamicLights(void)
{
	RefRenderObjListIterator dynaLightIt(&m_dynamicLightList);
	for (dynaLightIt.First(); !dynaLightIt.Is_Done(); )
	{
		W3DDynamicLight *pLight = (W3DDynamicLight*)dynaLightIt.Peek_Obj();
		dynaLightIt.Next();
		if (!pLight->isSpent())
			continue;
		pLight->Add_Ref();
		m_spentDynamicLights.push_back(pLight);
		UpdateList.Remove(pLight);
		m_dynamicLightList.Remove(pLight);
	}
}

//=============================================================================
// RTS3DScene::stepDynamicLights
//=============================================================================
/** The lights' half of Customized_Render, in its order, for a pass whose draw is skipped: -headless,
	* the undrawn stretch of -jumpToFrame, a minimised window, a lost device, a replay's fast forward and
	* seek (29 passes in 30 skip the draw).  Only the render stepped a light's fade and parked it, so
	* those runs kept every light pulse of the match switched on and getADynamicLight walked all of
	* them for each new one: 28 to 37% of the main thread late in a
	* headless match, and a -jumpToFrame's first drawn frame lit by every muzzle flash of the jump.  The
	* fade counts logic frames and is gated on the client frame, so a later render steps nothing twice,
	* and a light the terrain still holds is not spent and waits for it. */
//=============================================================================
void RTS3DScene::stepDynamicLights(void)
{
	parkSpentDynamicLights();
	RefRenderObjListIterator dynaLightIt(&m_dynamicLightList);
	for (dynaLightIt.First(); !dynaLightIt.Is_Done(); dynaLightIt.Next())
		dynaLightIt.Peek_Obj()->On_Frame_Update();
}

//=============================================================================
// RTS3DScene::removeDynamicLight
//=============================================================================
/** Removes a dynamic light. */
//=============================================================================
void RTS3DScene::removeDynamicLight(W3DDynamicLight * obj)
{
	m_dynamicLightList.Remove(obj);
}

//=============================================================================
// RTS3DScene::doRender
//=============================================================================
/** Render the scene */
//=============================================================================
void RTS3DScene::doRender( CameraClass * cam )
{
	m_camera = cam;
	DRAW();
	m_camera = NULL;

}  // end Customized_Render

//=============================================================================
// RTS3DScene::draw
//=============================================================================
/** Customized render for the 2d scene management */
//=============================================================================
void RTS3DScene::draw( )
{

	if (m_camera == NULL) {
		DEBUG_CRASH(("Null m_camera in RTS3DScene::draw"));
		return;
	}
	WW3D::Render( this, m_camera );


}  // end Customized_Render


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//=============================================================================
// RTS2DScene::RTS2DScene
//=============================================================================
/** */
//=============================================================================
RTS2DScene::RTS2DScene()
{
	setName("RTS2DScene");
	m_status = NEW_REF( W3DStatusCircle, () );
	Add_Render_Object( m_status );
}  // end RTS2DScene

//=============================================================================
// RTS2DScene::~RTS2DScene
//=============================================================================
/** */
//=============================================================================
RTS2DScene::~RTS2DScene()
{
	this->Remove_Render_Object(m_status);
	REF_PTR_RELEASE(m_status);
}  // end ~RTS2DScene

//=============================================================================
// RTS2DScene::Custimized_Render
//=============================================================================
/** Customized render for the 2d scene management */
//=============================================================================
void RTS2DScene::Customized_Render( RenderInfoClass &rinfo )
{

	// call simple scene class renderer
	SimpleSceneClass::Customized_Render( rinfo );

}  // end Customized_Render

//=============================================================================
// RTS2DScene::doRender
//=============================================================================
/** Render the scene */
//=============================================================================
void RTS2DScene::doRender( CameraClass * cam )
{

	m_camera = cam;
	DRAW();
	m_camera = NULL;

}  // end Customized_Render

//=============================================================================
// RTS2DScene::draw
//=============================================================================
/** Customized render for the 2d scene management */
//=============================================================================
void RTS2DScene::draw( )
{

	if (m_camera == NULL) {
		DEBUG_CRASH(("Null m_camera in RTS2DScene::draw"));
		return;
	}
	WW3D::Render( this, m_camera );


}  // end Customized_Render



///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//=============================================================================
// RTS3DInterfaceScene::RTS3DInterfaceScene
//=============================================================================
/** */
//=============================================================================
RTS3DInterfaceScene::RTS3DInterfaceScene()
{
}  // end RTS3DInterfaceScene

//=============================================================================
// RTS3DInterfaceScene::~RTS3DInterfaceScene
//=============================================================================
/** */
//=============================================================================
RTS3DInterfaceScene::~RTS3DInterfaceScene()
{
}  // end ~RTS3DInterfaceScene

//=============================================================================
// RTS3DInterfaceScene::Custimized_Render
//=============================================================================
/** Customized render for the 3d interface scene management */
//=============================================================================
void RTS3DInterfaceScene::Customized_Render( RenderInfoClass &rinfo )
{

	// call simple scene class renderer
	SimpleSceneClass::Customized_Render( rinfo );

}  // end Customized_Render



/// The following is an :archive" of a partial attempt at detecting the mapshroud hack
/*
 *
	
void RTS3DScene::Visibility_Check(CameraClass * camera)
{
#ifdef DIRTY_CONDITION_FLAGS
	StDrawableDirtyStuffLocker lockDirtyStuff;
#endif

	RefRenderObjListIterator it(&RenderList);
	DrawableInfo *drawInfo = NULL;
	Drawable	*draw = NULL;
	RenderObjClass * robj;

	m_numPotentialOccluders=0;
	m_numPotentialOccludees=0;
	m_translucentObjectsCount=0;
	m_numNonOccluderOrOccludee=0;

	Int currentFrame=0;
	if (TheGameLogic) currentFrame = TheGameLogic->getFrame();
	if (currentFrame <= TheGlobalData->m_defaultOcclusionDelay)
		currentFrame = TheGlobalData->m_defaultOcclusionDelay+1;	//make sure occlusion is enabled when game starts (frame 0).


	if (ShaderClass::Is_Backface_Culling_Inverted()) 
	{	//we are rendering reflections
		///@todo: Have better flag to detect reflection pass

		// Loop over all top-level RenderObjects in this scene. If the bounding sphere is not in front
		// of all the frustum planes, it is invisible.
		for (it.First(); !it.Is_Done(); it.Next()) {

			robj = it.Peek_Obj();

			draw=NULL;
			drawInfo = (DrawableInfo *)robj->Get_User_Data();
			if (drawInfo)
				draw=drawInfo->m_drawable;

			if( draw )
			{
				if (robj->Is_Force_Visible()) {
					robj->Set_Visible(true);
				} else {
					robj->Set_Visible(draw->getDrawsInMirror() && !camera->Cull_Sphere(robj->Get_Bounding_Sphere()));
				}
			}
			else
			{	//perform normal culling on non-drawables
				if (robj->Is_Force_Visible()) {
					robj->Set_Visible(true);
				} else {
					robj->Set_Visible(!camera->Cull_Sphere(robj->Get_Bounding_Sphere()));
				}
			}
		}
	}
	else
	{

		// Loop over all top-level RenderObjects in this scene. If the bounding sphere is not in front
		// of all the frustum planes, it is invisible.
		for (it.First(); !it.Is_Done(); it.Next()) {

			robj = it.Peek_Obj();

			if (robj->Is_Force_Visible()) {
				robj->Set_Visible(true);
			} else if (robj->Is_Hidden()) {
				robj->Set_Visible(false);
			} else {

				UnsignedByte isVisible = 0;
        

        //Cheater Foil
        isVisible |= (UnsignedByte)(camera->Cull_Sphere(robj->Get_Bounding_Sphere()) == FALSE);
        isVisible |= (UnsignedByte)(draw->isDrawableEffectivelyHidden());
        isVisible |= (draw->getFullyObscuredByShroudWithCheatSpy());
				robj->Set_VisibleWithCheatSpy(isVisible);
				if (robj->Is_VisibleWithCheatSpy())//this will clear for the bit set above
          
				{	//need to keep track of occluders and ocludees for subsequent code.
					drawInfo = (DrawableInfo *)robj->Get_User_Data();
					if (drawInfo && (draw=drawInfo->m_drawable) != NULL)
					{

//            now handled above in the cheater foil <<<<<<<<<<<<
//						if (draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud())
//						{	robj->Set_Visible(false);
//							continue;
//						}
						//assume normal rendering.
						drawInfo->m_flags = DrawableInfo::ERF_IS_NORMAL;	//clear any rendering flags that may be in effect.


						if ((draw->getEffectiveOpacity() != 1.0f || wearsCloak(draw)) && m_translucentObjectsCount < TheGlobalData->m_maxVisibleTranslucentObjects)
						{	drawInfo->m_flags |= DrawableInfo::ERF_IS_TRANSLUCENT;	//object is translucent
							m_translucentObjectsBuffer[m_translucentObjectsCount++] = robj;
						}
						if (behindBuildingMarkersEnabled() && TheGameLogic->getShowBehindBuildingMarkers())
						{
							//visible drawable. Check if it's either an occluder or occludee
							if (draw->isKindOf(KINDOF_STRUCTURE) && m_numPotentialOccluders < TheGlobalData->m_maxVisibleOccluderObjects)
							{	//object which could occlude other objects that need to be visible.
								//Make sure this object is not translucent so it's not rendered twice (from m_potentialOccluders and m_translucentObjectsBuffer)
								// a bit test, not an exclusive or.  The two happen to agree today only
								// because m_flags is cleared to ERF_IS_NORMAL a few lines up and
								// ERF_IS_TRANSLUCENT is the one bit that can be set before this point;
								// the moment anything else sets a flag first, the xor says "not
								// translucent" for an object that is, and it gets drawn twice.
								if (!(drawInfo->m_flags & DrawableInfo::ERF_IS_TRANSLUCENT))
									m_potentialOccluders[m_numPotentialOccluders++]=robj;
								drawInfo->m_flags |= DrawableInfo::ERF_POTENTIAL_OCCLUDER;
							}
							else
							if (draw->getObjectForDraw().hasObject &&
									(draw->isKindOf(KINDOF_SCORE) || draw->isKindOf(KINDOF_SCORE_CREATE) || draw->isKindOf(KINDOF_SCORE_DESTROY) || draw->isKindOf(KINDOF_MP_COUNT_FOR_VICTORY)) &&
									draw->getObjectForDraw().safeOcclusionFrame <= currentFrame && m_numPotentialOccludees < TheGlobalData->m_maxVisibleOccludeeObjects)
							{	//object which could be occluded but still needs to be visible.
								//We process transucent units twice (also in m_translucentObjectsBuffer) because we need to see them when occluded.
								m_potentialOccludees[m_numPotentialOccludees++]=robj;
								drawInfo->m_flags |= DrawableInfo::ERF_POTENTIAL_OCCLUDEE;
							}
							else
							if (drawInfo->m_flags == DrawableInfo::ERF_IS_NORMAL && m_numNonOccluderOrOccludee < TheGlobalData->m_maxVisibleNonOccluderOrOccludeeObjects)
							{	//regular object with no custom effects but still needs to be delayed to get the occlusion feature to work correctly.
								//Make sure this object is not translucent so it's not rendered twice (from m_potentialOccluders and m_translucentObjectsBuffer)
								// a bit test, not an exclusive or.  The two happen to agree today only
								// because m_flags is cleared to ERF_IS_NORMAL a few lines up and
								// ERF_IS_TRANSLUCENT is the one bit that can be set before this point;
								// the moment anything else sets a flag first, the xor says "not
								// translucent" for an object that is, and it gets drawn twice.
								if (!(drawInfo->m_flags & DrawableInfo::ERF_IS_TRANSLUCENT))	//make sure not translucent
									m_nonOccludersOrOccludees[m_numNonOccluderOrOccludee++]=robj;
								drawInfo->m_flags |= DrawableInfo::ERF_IS_NON_OCCLUDER_OR_OCCLUDEE;
							}
						}


					}
				}

//				robj->Set_Visible(isVisible); //now handled above in the cheater foil
			}

			///@todo: We're not using LOD yet so I disabled this code. MW
			// Also, should check how multiple passes (reflections) get along
			// with the LOD manager - we're rendering double the load it thinks we are.
			// Prepare visible objects for LOD:
			//	if (robj->Is_Really_Visible()) {
			//			robj->Prepare_LOD(*camera);
			//		}
		}
	}

   Visibility_Checked = true;
}


 *
 */

