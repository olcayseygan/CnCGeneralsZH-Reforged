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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8renderer.cpp                        $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 111                                                         $*
 *                                                                                             *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define ENABLE_CATEGORY_LOG
//#define ENABLE_STRIPING

#include "Platform/SunShadow.h"
#include "dx8renderer.h"
#include "dx8wrapper.h"
#include "dx8polygonrenderer.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8fvf.h"
#include "dx8caps.h"
#include "dx8rendererdebugger.h"
#include "dx11runtime.h"
#include "ffshader.h"
#include "wwdebug.h"
#include "wwprofile.h"
#include "wwmemlog.h"
#include "rinfo.h"
#include "statistics.h"
#include "meshmdl.h"
#include "vp.h"
#include "decalmsh.h"
#include "matpass.h"
#include "camera.h"
#include "stripoptimizer.h"
#include "meshgeometry.h"
#include "sortingrenderer.h"
#include "htree.h"

#include <algorithm>
#include <tuple>
#include <vector>

/*
** Global Instance of the DX8MeshRender
*/
DX8MeshRendererClass TheDX8MeshRenderer;
bool DX8TextureCategoryClass::m_gForceMultiply = false; // Forces opaque materials to use the multiply blend - pseudo transparent effect.  jba.
// ----------------------------------------------------------------------------

static DynamicVectorClass<Vector3>				_TempVertexBuffer;
static DynamicVectorClass<Vector3>				_TempNormalBuffer;
static DynamicVectorClass<MeshClass *>			_SkinFillMeshes;

static MultiListClass<MeshModelClass>			_RegisteredMeshList;
static TextureCategoryList							texture_category_delete_list;
static FVFCategoryList								fvf_category_container_delete_list;

// helper data structure
class PolyRemover : public MultiListObjectClass
{
public:
	DX8TextureCategoryClass *	src;
	DX8TextureCategoryClass *	dest;
	DX8PolygonRendererClass *  pr;
};

typedef MultiListClass<PolyRemover>			PolyRemoverList;
typedef MultiListIterator<PolyRemover>		PolyRemoverListIterator;

#define VERTEX_BUFFER_OVERFLOW	0xffff		//'Generals' flag to signal when a mesh didn't fit in streaming vertex buffer.

/**
** PolyRenderTaskClass
** This is a record of a polyrendere that needs to be rendered
** for this frame.  Since MeshClass instances can share meshmodels
** (and therefore their dx8 polygon renderers) this record contains
** a pointer to the polygon renderer and the MeshClass instance that
** it is being rendered for. 
*/
class PolyRenderTaskClass : public AutoPoolClass<PolyRenderTaskClass, 256>
{
public:
	PolyRenderTaskClass(DX8PolygonRendererClass * p_renderer,MeshClass * p_mesh) :
		Renderer(p_renderer),
		Mesh(p_mesh),
		NextVisible(NULL)
	{
		WWASSERT(Renderer != NULL);
		WWASSERT(Mesh != NULL);
		Mesh->Add_Ref();
	}

	~PolyRenderTaskClass(void)
	{
		Mesh->Release_Ref();
	}

	DX8PolygonRendererClass *	Peek_Polygon_Renderer(void)							{ return Renderer; }
	MeshClass *						Peek_Mesh(void)											{ return Mesh; }

	PolyRenderTaskClass *		Get_Next_Visible(void)									{ return NextVisible; }
	void								Set_Next_Visible(PolyRenderTaskClass * prtc)		{ NextVisible = prtc; }

protected:

	DX8PolygonRendererClass *	Renderer;
	MeshClass *						Mesh;
	PolyRenderTaskClass *		NextVisible;

};

DEFINE_AUTO_POOL(PolyRenderTaskClass, 256);

/**
** MatPassTaskClass
** This is the record of a material pass that needs to be rendered on
** a particular mesh.  These are linked into the FVF container which
** contains the mesh model.  They are also pooled to remove memory 
** allocation overhead.
*/
class MatPassTaskClass : public AutoPoolClass<MatPassTaskClass, 256>
{
public:
	MatPassTaskClass(MaterialPassClass * pass,MeshClass * mesh) :
		MaterialPass(pass),
		Mesh(mesh),
		NextVisible(NULL)
	{
		WWASSERT(MaterialPass != NULL);
		WWASSERT(Mesh != NULL);
		MaterialPass->Add_Ref();
		Mesh->Add_Ref();
	}

	~MatPassTaskClass(void)
	{
		MaterialPass->Release_Ref();
		Mesh->Release_Ref();
	}
	
	MaterialPassClass *	Peek_Material_Pass(void)							{ return MaterialPass; }
	MeshClass *				Peek_Mesh(void)										{ return Mesh; }
	
	MatPassTaskClass *	Get_Next_Visible(void)								{ return NextVisible; }
	void						Set_Next_Visible(MatPassTaskClass * mpr)		{ NextVisible = mpr; }

private:

	MaterialPassClass *	MaterialPass;
	MeshClass *				Mesh;
	MatPassTaskClass *	NextVisible;
};

DEFINE_AUTO_POOL(MatPassTaskClass, 256);


// ----------------------------------------------------------------------------


inline static bool Equal_Material(const VertexMaterialClass* mat1,const VertexMaterialClass* mat2)
{
	int crc0 = mat1 ? mat1->Get_CRC() : 0;
	int crc1 = mat2 ? mat2->Get_CRC() : 0;
	return (crc0 == crc1);
}


DX8TextureCategoryClass::DX8TextureCategoryClass(
	DX8FVFCategoryContainer* container_,
	TextureClass** texs,
	ShaderClass shd, 
	VertexMaterialClass* mat,
	int pass_)
	:
	pass(pass_),
	shader(shd),
	render_task_head(NULL),
	material(mat),
	container(container_)
{
	WWASSERT(pass>=0);
	WWASSERT(pass<DX8FVFCategoryContainer::MAX_PASSES);

	for (int a=0;a<MeshMatDescClass::MAX_TEX_STAGES;++a) 
	{
		textures[a]=NULL;
		REF_PTR_SET(textures[a],texs[a]);
	}

	if (material) material->Add_Ref();
}

DX8TextureCategoryClass::~DX8TextureCategoryClass()
{
	// Unregistering the mesh where polygon renderers are connected to kills all polygon renderers
	while (DX8PolygonRendererClass* p_renderer=PolygonRendererList.Get_Head()) {
		TheDX8MeshRenderer.Unregister_Mesh_Type(p_renderer->Get_Mesh_Model_Class());
	}
	for (int a=0;a<MeshMatDescClass::MAX_TEX_STAGES;++a) 
	{
		REF_PTR_RELEASE(textures[a]);
	}

	REF_PTR_RELEASE(material);
}

void DX8TextureCategoryClass::Add_Render_Task(DX8PolygonRendererClass * p_renderer,MeshClass * p_mesh)
{
	PolyRenderTaskClass * new_prt = new PolyRenderTaskClass(p_renderer,p_mesh);
	new_prt->Set_Next_Visible(render_task_head);
	render_task_head = new_prt;

	container->Add_Visible_Texture_Category(this,pass);
}

void DX8TextureCategoryClass::Add_Polygon_Renderer(DX8PolygonRendererClass* p_renderer,DX8PolygonRendererClass* add_after_this)
{
	WWASSERT(p_renderer!=NULL);
	WWASSERT(!PolygonRendererList.Contains(p_renderer));

	if (add_after_this != NULL) {
		bool res = PolygonRendererList.Add_After(p_renderer,add_after_this,false);
		WWASSERT(res != NULL);
	} else {
		PolygonRendererList.Add(p_renderer);
	}

	p_renderer->Set_Texture_Category(this);
}

void DX8TextureCategoryClass::Remove_Polygon_Renderer(DX8PolygonRendererClass* p_renderer)
{
	PolygonRendererList.Remove(p_renderer);
	p_renderer->Set_Texture_Category(NULL);
	if (PolygonRendererList.Peek_Head() == NULL) {
		container->Remove_Texture_Category(this);
		texture_category_delete_list.Add_Tail(this);
	}
}


void DX8FVFCategoryContainer::Remove_Texture_Category(DX8TextureCategoryClass* tex_category)
{
	unsigned pass;
	for (pass =0;pass<passes;++pass) {
		texture_category_list[pass].Remove(tex_category);
	}
	for (pass=0; pass<passes; pass++) {
		// If any of the texture category lists has anything in it, no need to delete this container
		if (texture_category_list[pass].Peek_Head() != NULL) return;
	}
	fvf_category_container_delete_list.Add_Tail(this);
}

void DX8FVFCategoryContainer::Add_Visible_Material_Pass(MaterialPassClass * pass,MeshClass * mesh)
{
	MatPassTaskClass * new_mpr = new MatPassTaskClass(pass,mesh);

	if (visible_matpass_head == NULL) {
		WWASSERT(visible_matpass_tail == NULL);
		visible_matpass_head = new_mpr;
	} else {
		WWASSERT(visible_matpass_tail != NULL);
		visible_matpass_tail->Set_Next_Visible(new_mpr);
	}

	visible_matpass_tail = new_mpr;
	AnythingToRender=true;
}

void DX8FVFCategoryContainer::Render_Procedural_Material_Passes(void)
{
	// additional passes
	MatPassTaskClass * mpr = visible_matpass_head;
	MatPassTaskClass * last_mpr = NULL;
   	bool renderTasksRemaining=false;

	while (mpr != NULL) {
		SNAPSHOT_SAY(("Render_Procedural_Material_Pass\n"));

   		MeshClass * mesh = mpr->Peek_Mesh();
   	
   		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)	//check if this mesh is valid
   		{	//skip this mesh so it gets rendered later after vertices are filled in.
	        last_mpr = mpr;
   			mpr = mpr->Get_Next_Visible();
   			renderTasksRemaining = true;
   			continue;
   		}
	
		mpr->Peek_Mesh()->Render_Material_Pass(mpr->Peek_Material_Pass(),index_buffer);
		MatPassTaskClass * next_mpr = mpr->Get_Next_Visible();
		
		// remove from list, then delete
		if (last_mpr == NULL) {
			visible_matpass_head = next_mpr;
		} else {
	       last_mpr->Set_Next_Visible(next_mpr);
	    }

		delete mpr;
		mpr = next_mpr;
	}

	visible_matpass_tail = renderTasksRemaining ? last_mpr : NULL;
}

void DX8RigidFVFCategoryContainer::Add_Delayed_Visible_Material_Pass(MaterialPassClass * pass, MeshClass * mesh)
{
	MatPassTaskClass * new_mpr = new MatPassTaskClass(pass,mesh);

	if (delayed_matpass_head == NULL) {
		WWASSERT(delayed_matpass_tail == NULL);
		delayed_matpass_head = new_mpr;
	} else {
		WWASSERT(delayed_matpass_tail != NULL);
		delayed_matpass_tail->Set_Next_Visible(new_mpr);
	}

	delayed_matpass_tail = new_mpr;
	AnyDelayedPassesToRender=true;
}

void DX8RigidFVFCategoryContainer::Render_Delayed_Procedural_Material_Passes(void)
{
	if (!Any_Delayed_Passes_To_Render()) return;
	AnyDelayedPassesToRender=false;

	DX8Wrapper::Set_Vertex_Buffer(vertex_buffer);
	DX8Wrapper::Set_Index_Buffer(index_buffer,0);

	SNAPSHOT_SAY(("DX8RigidFVFCategoryContainer::Render_Delayed_Procedural_Material_Passes()\n"));

	// additional passes
	MatPassTaskClass * mpr = delayed_matpass_head;
	while (mpr != NULL) {
	
		mpr->Peek_Mesh()->Render_Material_Pass(mpr->Peek_Material_Pass(),index_buffer);
		MatPassTaskClass * next_mpr = mpr->Get_Next_Visible();
		
		delete mpr;
		mpr = next_mpr;
	}

	delayed_matpass_head = delayed_matpass_tail = NULL;
}


void DX8TextureCategoryClass::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	StringClass work(255,true);
	work.Format("	DX8TextureCategoryClass\n");
	WWDEBUG_SAY((work));

	StringClass work2(255,true);
	for (int stage=0;stage<MeshMatDescClass::MAX_TEX_STAGES;++stage) {
		work2.Format("	texture[%d]: %x (%s)\n", stage, textures[stage], textures[stage] ? textures[stage]->Get_Name() : "-");
		work+=work2;
	}
	work2.Format("	material: %x (%s)\n	shader: %x\n", material, material ? material->Get_Name() : "-", shader);
	work+=work2;
	WWDEBUG_SAY((work));

	work.Format("	%8s %8s %6s %6s %6s %5s %s\n",
		"idx_cnt",
		"poly_cnt",
		"i_offs",
		"min_vi",
		"vi_rng",
		"ident",
		"name");
	WWDEBUG_SAY((work));

	DX8PolygonRendererListIterator it(&PolygonRendererList);
	while (!it.Is_Done()) {
	
		DX8PolygonRendererClass* p_renderer = it.Peek_Obj();

		PolyRenderTaskClass * prtc=render_task_head;
		while (prtc) {
			if (prtc->Peek_Polygon_Renderer()==p_renderer) break;
			prtc = prtc->Get_Next_Visible();
		}

		if (prtc != NULL) {
			WWDEBUG_SAY(("+"));
			p_renderer->Log();
		} else {
			if (!only_visible) {
				WWDEBUG_SAY(("-"));
				p_renderer->Log();
			}
		}
		it.Next();
	}
#endif
}

// ----------------------------------------------------------------------------

DX8FVFCategoryContainer::DX8FVFCategoryContainer(unsigned FVF_,bool sorting_)
	:
	FVF(FVF_),
	sorting(sorting_),
	visible_matpass_head(NULL),
	visible_matpass_tail(NULL),
	index_buffer(0),
	used_indices(0),
	passes(MAX_PASSES),
	uv_coordinate_channels(0),
	AnythingToRender(false),
	AnyDelayedPassesToRender(false)
{
	if ((FVF&D3DFVF_TEX1)==D3DFVF_TEX1) uv_coordinate_channels=1;
	if ((FVF&D3DFVF_TEX2)==D3DFVF_TEX2) uv_coordinate_channels=2;
	if ((FVF&D3DFVF_TEX3)==D3DFVF_TEX3) uv_coordinate_channels=3;
	if ((FVF&D3DFVF_TEX4)==D3DFVF_TEX4) uv_coordinate_channels=4;
	if ((FVF&D3DFVF_TEX5)==D3DFVF_TEX5) uv_coordinate_channels=5;
	if ((FVF&D3DFVF_TEX6)==D3DFVF_TEX6) uv_coordinate_channels=6;
	if ((FVF&D3DFVF_TEX7)==D3DFVF_TEX7) uv_coordinate_channels=7;
	if ((FVF&D3DFVF_TEX8)==D3DFVF_TEX8) uv_coordinate_channels=8;
}

// ----------------------------------------------------------------------------

DX8FVFCategoryContainer::~DX8FVFCategoryContainer()
{
	REF_PTR_RELEASE(index_buffer);

	for (unsigned p=0;p<passes;++p) {
		while (DX8TextureCategoryClass * tex = texture_category_list[p].Remove_Head()) {
			delete tex;
		}
	}
}

// ----------------------------------------------------------------------------

DX8TextureCategoryClass* DX8FVFCategoryContainer::Find_Matching_Texture_Category(
	TextureClass* texture,
	unsigned pass,
	unsigned stage,
	DX8TextureCategoryClass* ref_category)
{
	// Find texture category which matches ref_category's properties but has 'texture' on given pass and stage.
	DX8TextureCategoryClass* dest_tex_category=NULL;
	TextureCategoryListIterator dest_it(&texture_category_list[pass]);
	while (!dest_it.Is_Done()) {
		if (dest_it.Peek_Obj()->Peek_Texture(stage)==texture) {
			// Compare all stage's textures
			dest_tex_category=dest_it.Peek_Obj();
			bool all_textures_same = true;
			for (unsigned int s = 0; s < MeshMatDescClass::MAX_TEX_STAGES; s++) {
				if (stage!=s) {
					all_textures_same = all_textures_same && (dest_tex_category->Peek_Texture(s) == ref_category->Peek_Texture(s));
				}
			}
			if (all_textures_same &&
				Equal_Material(dest_tex_category->Peek_Material(),ref_category->Peek_Material()) &&
				dest_tex_category->Get_Shader()==ref_category->Get_Shader()) {
				return dest_tex_category;
			}
		}
		dest_it.Next();
	}
	return NULL;
}

DX8TextureCategoryClass* DX8FVFCategoryContainer::Find_Matching_Texture_Category(
		VertexMaterialClass* vmat,
		unsigned pass,		
		DX8TextureCategoryClass* ref_category)
{
	// Find texture category which matches ref_category's properties but has 'vmat' on given pass
	DX8TextureCategoryClass* dest_tex_category=NULL;
	TextureCategoryListIterator dest_it(&texture_category_list[pass]);
	while (!dest_it.Is_Done()) {
		if (Equal_Material(dest_it.Peek_Obj()->Peek_Material(),vmat)) {
			// Compare all stage's textures
			dest_tex_category=dest_it.Peek_Obj();
			bool all_textures_same = true;
			for (unsigned int s = 0; s < MeshMatDescClass::MAX_TEX_STAGES; s++)
				all_textures_same = all_textures_same && (dest_tex_category->Peek_Texture(s) == ref_category->Peek_Texture(s));			
			if (all_textures_same &&				
				dest_tex_category->Get_Shader()==ref_category->Get_Shader()) {
				return dest_tex_category;
			}
		}
		dest_it.Next();
	}
	return NULL;
}

void DX8FVFCategoryContainer::Change_Polygon_Renderer_Texture(
	DX8PolygonRendererList& polygon_renderer_list,
	TextureClass* texture,
	TextureClass* new_texture,
	unsigned pass,
	unsigned stage)
{
	WWASSERT(pass<passes);

	PolyRemoverList prl;

	bool foundtexture=false;

	if (texture==new_texture) return;

	// Find source texture category, then find all polygon renderers who belong to that category
	// and move them to destination category.
	TextureCategoryListIterator src_it(&texture_category_list[pass]);
	while (!src_it.Is_Done()) {
		DX8TextureCategoryClass* src_tex_category=src_it.Peek_Obj();		
		if (src_tex_category->Peek_Texture(stage)==texture) {
			foundtexture=true;
			DX8PolygonRendererListIterator poly_it(&polygon_renderer_list);
			while (!poly_it.Is_Done()) {
				// If source texture category contains polygon renderer, move to destination category
				DX8PolygonRendererClass* polygon_renderer=poly_it.Peek_Obj();
				DX8TextureCategoryClass *prc=polygon_renderer->Get_Texture_Category();

				if (prc==src_tex_category) {					
					DX8TextureCategoryClass* dest_tex_category=Find_Matching_Texture_Category(new_texture,pass,stage,src_tex_category);

					if (!dest_tex_category) {
						TextureClass * tmp_textures[MeshMatDescClass::MAX_TEX_STAGES];
						for (int s=0;s<MeshMatDescClass::MAX_TEX_STAGES;++s) {
							tmp_textures[s]=src_tex_category->Peek_Texture(s);
						}
						tmp_textures[stage]=new_texture;

						DX8TextureCategoryClass * new_tex_category=W3DNEW DX8TextureCategoryClass(
							this,
							tmp_textures,
							src_tex_category->Get_Shader(),
							const_cast<VertexMaterialClass*>(src_tex_category->Peek_Material()),
							pass);
		
						/*
						** Add the texture category object into the list, immediately after any existing
						** texture category object which uses the same texture.  This will result in
						** the list always having matching texture categories next to each other.
						*/
						bool found_similar_category = false;
						TextureCategoryListIterator tex_it(&texture_category_list[pass]);
						while (!tex_it.Is_Done()) {
							// Categorize according to first stage's texture for now
							if (tex_it.Peek_Obj()->Peek_Texture(0) == tmp_textures[0]) {
								texture_category_list[pass].Add_After(new_tex_category,tex_it.Peek_Obj());
								found_similar_category = true;
								break;
							}
							tex_it.Next();
						}

						if (!found_similar_category) {
							texture_category_list[pass].Add_Tail(new_tex_category);
						}
						dest_tex_category=new_tex_category;
					}
					PolyRemover *rem=W3DNEW PolyRemover;
					rem->src=src_tex_category;
					rem->dest=dest_tex_category;
					rem->pr=polygon_renderer;
					prl.Add(rem);					
				}
				poly_it.Next();
			} // while			
		} //if src_texture==texture
		else
			// quit loop if we've got a texture change
			if (foundtexture) break;
		src_it.Next();
	} // while

	PolyRemoverListIterator prli(&prl);

	while (!prli.Is_Done())
	{
		PolyRemover *rem=prli.Peek_Obj();
		rem->src->Remove_Polygon_Renderer(rem->pr);
		rem->dest->Add_Polygon_Renderer(rem->pr);		
		prli.Remove_Current_Object();
		delete rem;
	}
}

void DX8FVFCategoryContainer::Change_Polygon_Renderer_Material(
		DX8PolygonRendererList& polygon_renderer_list,
		VertexMaterialClass* vmat,
		VertexMaterialClass* new_vmat,
		unsigned pass)
{
	WWASSERT(pass<passes);

	PolyRemoverList prl;

	bool foundtexture=false;

	if (vmat==new_vmat) return;

	// Find source texture category, then find all polygon renderers who belong to that category
	// and move them to destination category.
	TextureCategoryListIterator src_it(&texture_category_list[pass]);
	while (!src_it.Is_Done()) {
		DX8TextureCategoryClass* src_tex_category=src_it.Peek_Obj();
		if (src_tex_category->Peek_Material()==vmat) {			
			DX8PolygonRendererListIterator poly_it(&polygon_renderer_list);
			while (!poly_it.Is_Done()) {
				// If source texture category contains polygon renderer, move to destination category
				DX8PolygonRendererClass* polygon_renderer=poly_it.Peek_Obj();
				DX8TextureCategoryClass *prc=polygon_renderer->Get_Texture_Category();
				if (prc==src_tex_category) {
					foundtexture=true;
					DX8TextureCategoryClass* dest_tex_category=Find_Matching_Texture_Category(new_vmat,pass,src_tex_category);

					if (!dest_tex_category) {
						TextureClass * tmp_textures[MeshMatDescClass::MAX_TEX_STAGES];
						for (int s=0;s<MeshMatDescClass::MAX_TEX_STAGES;++s) {
							tmp_textures[s]=src_tex_category->Peek_Texture(s);
						}						

						DX8TextureCategoryClass * new_tex_category=W3DNEW DX8TextureCategoryClass(
							this,
							tmp_textures,
							src_tex_category->Get_Shader(),
							const_cast<VertexMaterialClass*>(new_vmat),
							pass);
		
						/*
						** Add the texture category object into the list, immediately after any existing
						** texture category object which uses the same texture.  This will result in
						** the list always having matching texture categories next to each other.
						*/
						bool found_similar_category = false;
						TextureCategoryListIterator tex_it(&texture_category_list[pass]);
						while (!tex_it.Is_Done()) {
							// Categorize according to first stage's texture for now
							if (tex_it.Peek_Obj()->Peek_Texture(0) == tmp_textures[0]) {
								texture_category_list[pass].Add_After(new_tex_category,tex_it.Peek_Obj());
								found_similar_category = true;
								break;
							}
							tex_it.Next();
						}

						if (!found_similar_category) {
							texture_category_list[pass].Add_Tail(new_tex_category);
						}
						dest_tex_category=new_tex_category;
					}
					PolyRemover *rem=W3DNEW PolyRemover;
					rem->src=src_tex_category;
					rem->dest=dest_tex_category;
					rem->pr=polygon_renderer;
					prl.Add(rem);
				}
				poly_it.Next();
			} // while			
		} // if 
		else
			if (foundtexture) break;
		src_it.Next();
	} // while

	PolyRemoverListIterator prli(&prl);

	while (!prli.Is_Done())
	{
		PolyRemover *rem=prli.Peek_Obj();
		rem->src->Remove_Polygon_Renderer(rem->pr);
		rem->dest->Add_Polygon_Renderer(rem->pr);		
		prli.Remove_Current_Object();
		delete rem;
	}
}

// ----------------------------------------------------------------------------

unsigned DX8FVFCategoryContainer::Define_FVF(MeshModelClass* mmc,bool enable_lighting)
{
	if ((!!mmc->Get_Flag(MeshGeometryClass::SORT)) && WW3D::Is_Sorting_Enabled()) {
		return dynamic_fvf_type;
	}

	unsigned fvf=D3DFVF_XYZ;

	int tex_coord_count=mmc->Get_UV_Array_Count();

	if (mmc->Get_Color_Array(0,false)) {
		fvf|=D3DFVF_DIFFUSE;
	}
	if (mmc->Get_Color_Array(1,false)) {
		fvf|=D3DFVF_SPECULAR;
	}
	
	switch (tex_coord_count) {
	default:
	case 0:
		break;
	case 1: fvf|=D3DFVF_TEX1; break;
	case 2: fvf|=D3DFVF_TEX2; break;
	case 3: fvf|=D3DFVF_TEX3; break;
	case 4: fvf|=D3DFVF_TEX4; break;
	case 5: fvf|=D3DFVF_TEX5; break;
	case 6: fvf|=D3DFVF_TEX6; break;
	case 7: fvf|=D3DFVF_TEX7; break;
	case 8: fvf|=D3DFVF_TEX8; break;
	}

	if (!mmc->Needs_Vertex_Normals()) {  //enable_lighting || mmc->Get_Flag(MeshModelClass::PRELIT_MASK)) {
		return fvf;
	}

	fvf|=D3DFVF_NORMAL;	// Realtime-lit
	return fvf;
}

// ----------------------------------------------------------------------------

DX8RigidFVFCategoryContainer::DX8RigidFVFCategoryContainer(unsigned FVF,bool sorting_)
	:
	DX8FVFCategoryContainer(FVF,sorting_),
	vertex_buffer(0),
	used_vertices(0),
	delayed_matpass_head(NULL),
	delayed_matpass_tail(NULL)
{
}

// ----------------------------------------------------------------------------

DX8RigidFVFCategoryContainer::~DX8RigidFVFCategoryContainer()
{
	REF_PTR_RELEASE(vertex_buffer);
}

// ----------------------------------------------------------------------------

void DX8RigidFVFCategoryContainer::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	StringClass work(255,true);
	work.Format("DX8RigidFVFCategoryContainer --------------\n");
	WWDEBUG_SAY((work));
	if (vertex_buffer) {
		StringClass fvfname(255,true);
		vertex_buffer->FVF_Info().Get_FVF_Name(fvfname);
		work.Format("VB size (used/total): %d/%d FVF: %s\n",used_vertices,vertex_buffer->Get_Vertex_Count(),fvfname);
		WWDEBUG_SAY((work));
	}
	else {
		WWDEBUG_SAY(("EMPTY VB\n"));
	}
	if (index_buffer) {
		work.Format("IB size (used/total): %d/%d\n",used_indices,index_buffer->Get_Index_Count());
		WWDEBUG_SAY((work));
	}
	else {
		WWDEBUG_SAY(("EMPTY IB\n"));
	}

	for (unsigned p=0;p<passes;++p) {
		WWDEBUG_SAY(("Pass: %d\n",p));

		TextureCategoryListIterator it(&texture_category_list[p]);
		while (!it.Is_Done()) {
			it.Peek_Obj()->Log(only_visible);
			it.Next();
		}
	}
#endif
}

// ----------------------------------------------------------------------------
//
// Generic render function for rigid meshes
//
// ----------------------------------------------------------------------------

void DX8RigidFVFCategoryContainer::Render(void)
{
	if (!Anything_To_Render()) return;
	AnythingToRender=false;

	DX8Wrapper::Set_Vertex_Buffer(vertex_buffer);

	DX8Wrapper::Set_Index_Buffer(index_buffer,0);

	SNAPSHOT_SAY(("DX8RigidFVFCategoryContainer::Render()\n"));
	// The Z-biasing was causing more problems than they solved.
	// Disabling it for now HY.
	//int zbias=0;
	//DX8Wrapper::Set_DX8_ZBias(zbias);
	for (unsigned p=0;p<passes;++p) {
		SNAPSHOT_SAY(("Pass: %d\n",p));
		while (DX8TextureCategoryClass * tex = visible_texture_category_list[p].Remove_Head()) {
			tex->Render();
		}
		//zbias++;
		//if (zbias>15) zbias=15;
		//DX8Wrapper::Set_DX8_ZBias(zbias);
	}

	Render_Procedural_Material_Passes();

	//DX8Wrapper::Set_DX8_ZBias(0);
}

// ----------------------------------------------------------------------------

bool DX8RigidFVFCategoryContainer::Check_If_Mesh_Fits(MeshModelClass* mmc)
{
	if (!vertex_buffer) return true;	// No VB created - mesh will fit as a new vb will be created when inserting
	unsigned required_vertices=mmc->Get_Vertex_Count();
	unsigned available_vertices=vertex_buffer->Get_Vertex_Count()-used_vertices;
	unsigned required_polygons=mmc->Get_Polygon_Count();
	if (mmc->Get_Gap_Filler()) {
		required_polygons+=mmc->Get_Gap_Filler()->Get_Polygon_Count();
	} 
	unsigned required_indices=required_polygons*3*mmc->Get_Pass_Count();
	unsigned available_indices=index_buffer->Get_Index_Count()-used_indices;
	if (
		required_vertices<=available_vertices &&
		(required_indices)<=available_indices) {
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------------

class Vertex_Split_Table
{
	MeshModelClass* mmc;
	bool npatch_enable;
	unsigned polygon_count;
	TriIndex* polygon_array;

	bool allocated_polygon_array;

public:
	Vertex_Split_Table(MeshModelClass* mmc_)
		:
		mmc(mmc_),
		npatch_enable(false),
		allocated_polygon_array(false)
	{
		if (DX8Wrapper::Get_Current_Caps()->Support_NPatches() && mmc->Needs_Vertex_Normals()) {
			if (mmc->Get_Flag(MeshGeometryClass::ALLOW_NPATCHES)) {
				npatch_enable=true;
			}
		}

		const GapFillerClass* gap_filler=mmc->Get_Gap_Filler();
		polygon_count=mmc->Get_Polygon_Count();
		if (gap_filler) polygon_count+=gap_filler->Get_Polygon_Count();
//		if (mmc->Get_Gap_Filler_Polygon_Count()) {
			allocated_polygon_array=true;
			polygon_array=W3DNEWARRAY TriIndex[polygon_count];
			memcpy(
				polygon_array,
				mmc->Get_Polygon_Array(),
				mmc->Get_Polygon_Count()*sizeof(TriIndex));
			if (gap_filler) {
				memcpy(
					polygon_array+mmc->Get_Polygon_Count(),
					gap_filler->Get_Polygon_Array(),
					gap_filler->Get_Polygon_Count()*sizeof(TriIndex));
			}
//		}
//		else {
//			polygon_array=const_cast<TriIndex*>(mmc->Get_Polygon_Array());
//		}

	}

	~Vertex_Split_Table()
	{
		if (allocated_polygon_array) {
			delete[] polygon_array;
		}
	}

	const Vector3* Get_Vertex_Array() const
	{
		return mmc->Get_Vertex_Array();
	}

	const Vector3* Get_Vertex_Normal_Array() const
	{
		return mmc->Get_Vertex_Normal_Array();
	}

	const unsigned* Get_Color_Array(unsigned index) const
	{
		return mmc->Get_Color_Array(index,false);
	}

	const Vector2* Get_UV_Array(unsigned uv_array_index) const
	{
		return mmc->Get_UV_Array_By_Index(uv_array_index);
	}

	unsigned Get_Vertex_Count() const
	{
		return mmc->Get_Vertex_Count();
	}

	unsigned Get_Polygon_Count() const
	{
		return polygon_count;
	}

	unsigned Get_Pass_Count() const
	{
		return mmc->Get_Pass_Count();
	}

	TextureClass* Peek_Texture(unsigned idx,unsigned pass,unsigned stage)
	{
		if (mmc->Has_Texture_Array(pass,stage)) {
			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				return mmc->Get_Gap_Filler()->Get_Texture_Array(pass,stage)[idx-mmc->Get_Polygon_Count()];
			}
			return mmc->Peek_Texture(idx,pass,stage);
		}
		return mmc->Peek_Single_Texture(pass,stage);
	}

	VertexMaterialClass* Peek_Material(unsigned idx,unsigned pass)
	{
		if (mmc->Has_Material_Array(pass)) {
			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				return mmc->Get_Gap_Filler()->Get_Material_Array(pass)[idx-mmc->Get_Polygon_Count()];
			}
			return mmc->Peek_Material(mmc->Get_Polygon_Array()[idx][0],pass);
		}
		return mmc->Peek_Single_Material(pass);
	}

	ShaderClass Peek_Shader(unsigned idx,unsigned pass)
	{
		if (mmc->Has_Shader_Array(pass)) {
			ShaderClass shader;
			
			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				shader=mmc->Get_Gap_Filler()->Get_Shader_Array(pass)[idx-mmc->Get_Polygon_Count()];
			}
			else shader=mmc->Get_Shader(idx,pass);

			if (npatch_enable) {
				shader.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
			}

			return shader;
		}
		if (!npatch_enable) return mmc->Get_Single_Shader(pass);
		ShaderClass shader=mmc->Get_Single_Shader(pass);
		shader.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
		return shader;

	}

	MeshModelClass* Get_Mesh_Model_Class()
	{
		return mmc;
	}

	unsigned short* Get_Polygon_Array(unsigned pass)
	{
		return (unsigned short*)polygon_array;
	}
};

// ----------------------------------------------------------------------------

void DX8RigidFVFCategoryContainer::Add_Mesh(MeshModelClass* mmc_)
{
	WWASSERT(Check_If_Mesh_Fits(mmc_));

	Vertex_Split_Table split_table(mmc_);
	int needed_vertices=split_table.Get_Vertex_Count();

	/*
	** This FVFCategoryContainer doesn't have a vertex buffer yet so allocate one big
	** enough to contain this mesh.
	*/
	if (!vertex_buffer) {
		int vb_size=4000;
		if (vb_size<needed_vertices) vb_size=needed_vertices;
		if (sorting) {
			vertex_buffer=NEW_REF(SortingVertexBufferClass,(vb_size));
			WWASSERT(vertex_buffer->FVF_Info().Get_FVF()==FVF);	// Only one sorting FVF type!
		}
		else {
			vertex_buffer=NEW_REF(DX8VertexBufferClass,(
				FVF,
				vb_size,
				(DX8Wrapper::Get_Current_Caps()->Support_NPatches() && WW3D::Get_NPatches_Level()>1) ? DX8VertexBufferClass::USAGE_NPATCHES : DX8VertexBufferClass::USAGE_DEFAULT));
		}
	}

	/*
	** Append this mesh's vertices to the vertex buffer.
	*/

	VertexBufferClass::AppendLockClass l(vertex_buffer,used_vertices,split_table.Get_Vertex_Count());
	const FVFInfoClass fi=vertex_buffer->FVF_Info();
	unsigned char *vb=(unsigned char*) l.Get_Vertex_Array();
	unsigned int i;
	const Vector3 *locs=split_table.Get_Vertex_Array();
	const Vector3 *norms=split_table.Get_Vertex_Normal_Array();
	const unsigned *diffuse=split_table.Get_Color_Array(0);
	const unsigned *specular=split_table.Get_Color_Array(1);
	for (i=0; i<split_table.Get_Vertex_Count(); i++)
	{
		*(Vector3*)(vb+fi.Get_Location_Offset())=locs[i];
		
		if ((FVF&D3DFVF_NORMAL)==D3DFVF_NORMAL && norms) {
			*(Vector3*)(vb+fi.Get_Normal_Offset())=norms[i];
		}

		if ((FVF&D3DFVF_DIFFUSE)==D3DFVF_DIFFUSE) {
			if (diffuse) {
				*(unsigned int*)(vb+fi.Get_Diffuse_Offset())=diffuse[i];
			} else {
				*(unsigned int*)(vb+fi.Get_Diffuse_Offset()) = 0xFFFFFFFF;
			}
		}
		
		if ((FVF&D3DFVF_SPECULAR)==D3DFVF_SPECULAR) {
			if (specular) {
				*(unsigned int*)(vb+fi.Get_Specular_Offset())=specular[i];
			} else {
				*(unsigned int*)(vb+fi.Get_Specular_Offset()) = 0xFFFFFFFF;
			}
		}

		vb+=fi.Get_FVF_Size();
	}
	

	/*
	** Append the UV coordinates to the vertex buffer
	*/
	int uvcount = 0;
	if ((FVF&D3DFVF_TEX1) == D3DFVF_TEX1) {
		uvcount = 1;
	}
	if ((FVF&D3DFVF_TEX2) == D3DFVF_TEX2) {
		uvcount = 2;
	}
	if ((FVF&D3DFVF_TEX3) == D3DFVF_TEX3) {
		uvcount = 3;
	}
	if ((FVF&D3DFVF_TEX4) == D3DFVF_TEX4) {
		uvcount = 4;
	}
	if ((FVF&D3DFVF_TEX5) == D3DFVF_TEX5) {
		uvcount = 5;
	}
	if ((FVF&D3DFVF_TEX6) == D3DFVF_TEX6) {
		uvcount = 6;
	}
	if ((FVF&D3DFVF_TEX7) == D3DFVF_TEX7) {
		uvcount = 7;
	}
	if ((FVF&D3DFVF_TEX8) == D3DFVF_TEX8) {
		uvcount = 8;
	}
	
	for (int j=0; j<uvcount; j++) {
		unsigned char *vb=(unsigned char*) l.Get_Vertex_Array();
		const Vector2*uvs=split_table.Get_UV_Array(j);
		if (uvs) {
			for (i=0; i<split_table.Get_Vertex_Count(); i++)
			{
				*(Vector2*)(vb+fi.Get_Tex_Offset(j))=uvs[i];
				vb+=fi.Get_FVF_Size();
			}		
		}
	}

	Generate_Texture_Categories(split_table,used_vertices);

	used_vertices+=needed_vertices;//vertex_count;
}

void DX8FVFCategoryContainer::Insert_To_Texture_Category(
	Vertex_Split_Table& split_table,
	TextureClass** texs,
	VertexMaterialClass* mat,
	ShaderClass shader,
	int pass,
	unsigned vertex_offset)
{
	/*
	** Try to find a DX8TextureCategoryClass in this FVF container which matches the
	** given textures(one per stage), material and shader combination.
	*/
	bool fit_in_existing_category = false;
	TextureCategoryListIterator it(&texture_category_list[pass]);
	while (!it.Is_Done()) {
		DX8TextureCategoryClass * tex_category=it.Peek_Obj();
		// Compare all stage's textures
		bool all_textures_same = true;
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			all_textures_same = all_textures_same && (tex_category->Peek_Texture(stage) == texs[stage]);
		}
		if (all_textures_same && Equal_Material(tex_category->Peek_Material(),mat) && tex_category->Get_Shader()==shader) {
			used_indices+=tex_category->Add_Mesh(split_table,vertex_offset,used_indices,index_buffer,pass);
			fit_in_existing_category = true;
			break;
		}
		it.Next();
	}

	if (!fit_in_existing_category) {
		
		DX8TextureCategoryClass * new_tex_category=W3DNEW DX8TextureCategoryClass(this,texs,shader,mat,pass);
		used_indices+=new_tex_category->Add_Mesh(split_table,vertex_offset,used_indices,index_buffer,pass);
		
		/*
		** Add the texture category object into the list, immediately after any existing
		** texture category object which uses the same texture.  This will result in
		** the list always having matching texture categories next to each other.
		*/
		bool found_similar_category = false;
		TextureCategoryListIterator it(&texture_category_list[pass]);
		while (!it.Is_Done()) {
			// Categorize according to first stage's texture for now
			if (it.Peek_Obj()->Peek_Texture(0) == texs[0]) {
				texture_category_list[pass].Add_After(new_tex_category,it.Peek_Obj());
				found_similar_category = true;
				break;
			}
			it.Next();
		}

		if (!found_similar_category) {
			texture_category_list[pass].Add_Tail(new_tex_category);
		}
	}
}

const unsigned MAX_ADDED_TYPE_COUNT=64;
struct Textures_Material_And_Shader_Booking_Struct
{
	TextureClass* added_textures[MeshMatDescClass::MAX_TEX_STAGES][MAX_ADDED_TYPE_COUNT];
	VertexMaterialClass* added_materials[MAX_ADDED_TYPE_COUNT];
	ShaderClass added_shaders[MAX_ADDED_TYPE_COUNT];
	unsigned added_type_count;

	Textures_Material_And_Shader_Booking_Struct() : added_type_count(0) {}

	bool Add_Textures_Material_And_Shader(TextureClass** texs, VertexMaterialClass* mat, ShaderClass shd)
	{
		for (unsigned a=0;a<added_type_count;++a) {
			// Compare textures
			bool all_textures_same = true;
			for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
				all_textures_same = all_textures_same && (texs[stage] == added_textures[stage][a]);
			}
			if (all_textures_same && Equal_Material(mat,added_materials[a]) && shd==added_shaders[a]) {
				return false;
			}
		}
		WWASSERT(added_type_count<MAX_ADDED_TYPE_COUNT);
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			added_textures[stage][added_type_count]=texs[stage];
		}
		added_materials[added_type_count]=mat;
		added_shaders[added_type_count]=shd;
		added_type_count++;
		return true;
	}
};

void DX8FVFCategoryContainer::Generate_Texture_Categories(Vertex_Split_Table& split_table,unsigned vertex_offset)
{
	int polygon_count=split_table.Get_Polygon_Count();
	int index_count=polygon_count*3*split_table.Get_Pass_Count(); 

	/*
	** If we don't have an index buffer yet, allocate one.  Make it hold at least 12000 entries,
	** more if the mesh requires it.  
	*/
	if (!index_buffer) {
		int ib_size=12000;  
		if (ib_size<index_count) ib_size=index_count;
		if (sorting) {
			index_buffer=NEW_REF(SortingIndexBufferClass,(ib_size));
		}
		else {
			index_buffer=NEW_REF(DX8IndexBufferClass,(
				ib_size,
				(DX8Wrapper::Get_Current_Caps()->Support_NPatches() && WW3D::Get_NPatches_Level()>1) ? DX8IndexBufferClass::USAGE_NPATCHES : DX8IndexBufferClass::USAGE_DEFAULT));
		}
	}

	for (unsigned pass=0;pass<split_table.Get_Pass_Count();++pass) {
		Textures_Material_And_Shader_Booking_Struct textures_material_and_shader_booking;

		unsigned old_used_indices=used_indices;

		for (int i=0;i<polygon_count;++i) {
			TextureClass* textures[MeshMatDescClass::MAX_TEX_STAGES];
			// disabled this assert as MAX_TEXTURE_STAGES is now 8, but legacy MeshMat::MAX_TEX_STAGES is still 2
	//		WWASSERT(MAX_TEXTURE_STAGES==MeshMatDescClass::MAX_TEX_STAGES);
			for (int stage=0;stage<MeshMatDescClass::MAX_TEX_STAGES;stage++) 
			{
				textures[stage]=split_table.Peek_Texture(i,pass,stage);
			}
			VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
			ShaderClass shader=split_table.Peek_Shader(i,pass);
			if (!textures_material_and_shader_booking.Add_Textures_Material_And_Shader(textures,mat,shader)) continue;

			Insert_To_Texture_Category(split_table,textures,mat,shader,pass,vertex_offset);
		}

		int new_inds=used_indices-old_used_indices;
		WWASSERT(new_inds<=polygon_count*3);
	}
}

// ----------------------------------------------------------------------------

DX8SkinFVFCategoryContainer::DX8SkinFVFCategoryContainer(bool sorting)
	:
	DX8FVFCategoryContainer(DX8_FVF_XYZNUV1,sorting),
	VisibleVertexCount(0),
	VisibleSkinHead(NULL),
	VisibleSkinTail(NULL)
{
}

// ----------------------------------------------------------------------------

DX8SkinFVFCategoryContainer::~DX8SkinFVFCategoryContainer()
{
}

// ----------------------------------------------------------------------------

void DX8SkinFVFCategoryContainer::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	StringClass work(255,true);
	work.Format("DX8SkinFVFCategoryContainer --------------\n");
	WWDEBUG_SAY((work));

	if (index_buffer) {
		work.Format("IB size (used/total): %d/%d\n",used_indices,index_buffer->Get_Index_Count());
		WWDEBUG_SAY((work));
	}
	else {
		WWDEBUG_SAY(("EMPTY IB\n"));
	}

	for (unsigned pass=0;pass<passes;++pass) {
		TextureCategoryListIterator it(&texture_category_list[pass]);
		while (!it.Is_Done()) {
			it.Peek_Obj()->Log(only_visible);
			it.Next();
		}
	}
#endif
}

// ----------------------------------------------------------------------------

// Skins a thread claims at once; a skinned mesh is a few hundred vertices.
static const int SKINS_PER_CLAIM=2;

struct SkinFillJob
{
	MeshClass **meshes;
	VertexFormatXYZNDUV2 *dest_verts;
	Vector3 *loc;
	Vector3 *norm;
};

// One skin, deformed and written into its slice of the vertex buffer.  Runs on the job pool: it
// reads the mesh and its pose, writes only from its base vertex offset on, and allocates nothing,
// so the bytes are the ones the single-threaded fill wrote.
static void Fill_Skin(int index,void *context)
{
	SkinFillJob *job=(SkinFillJob *)context;
	MeshClass *mesh=job->meshes[index];
	MeshModelClass *mmc=mesh->Peek_Model();
	const int mesh_vertex_count=mmc->Get_Vertex_Count();
	const int vertex_offset=mesh->Get_Base_Vertex_Offset();

	Vector3* loc=job->loc+vertex_offset;
	Vector3* norm=job->norm+vertex_offset;
	const Vector2* uv0=mmc->Get_UV_Array_By_Index(0);
	const Vector2* uv1=mmc->Get_UV_Array_By_Index(1);
	const unsigned* diffuse=mmc->Get_Color_Array(0,false);

	VertexFormatXYZNDUV2* verts=job->dest_verts+vertex_offset;

	mesh->Get_Deformed_Vertices(loc,norm);

	for (int v=0;v<mesh_vertex_count;++v) {
		verts[v].x=(*loc)[0];
		verts[v].y=(*loc)[1];
		verts[v].z=(*loc)[2];
		verts[v].nx=(*norm)[0];
		verts[v].ny=(*norm)[1];
		verts[v].nz=(*norm)[2];
		if (diffuse) {
			verts[v].diffuse=*diffuse++;
		}
		else {
			verts[v].diffuse=0;
		}
		if (uv0) {
			verts[v].u1=(*uv0)[0];
			verts[v].v1=(*uv0)[1];
			uv0++;
		}
		else {
			verts[v].u1=0.0f;
			verts[v].v1=0.0f;
		}
		if (uv1) {
			verts[v].u2=(*uv1)[0];
			verts[v].v2=(*uv1)[1];
			uv1++;
		}
		else {
			verts[v].u2=0.0f;
			verts[v].v2=0.0f;
		}

		loc++;
		norm++;
	}
}

void DX8SkinFVFCategoryContainer::Render(void)
{
	SNAPSHOT_SAY(("DX8SkinFVFCategoryContainer::Render()\n"));
	if (!Anything_To_Render()) {
		SNAPSHOT_SAY(("Nothing to render\n"));
		return;
	}
	AnythingToRender=false;

	DX8Wrapper::Set_Vertex_Buffer(NULL);	// Free up the reference to the current vertex buffer
														// (in case it is the dynamic, which may have to be resized)

	//'Generals' customization to allow more than 65535 vertices
	unsigned int maxVertexCount=VisibleVertexCount;
	if (maxVertexCount > 65535)
	{	//clamp vertex count to maximum size that can be indexed by 16-bit index
		maxVertexCount = 65535;
	}

	DynamicVBAccessClass vb(
		sorting ? BUFFER_TYPE_DYNAMIC_SORTING : BUFFER_TYPE_DYNAMIC_DX8,
		dynamic_fvf_type,
		maxVertexCount);
	SNAPSHOT_SAY(("DynamicVBAccess - %s - %d vertices\n",sorting ? "sorting" : "non-sorting",VisibleVertexCount));

	unsigned int renderedVertexCount=0;

	// Every mesh deforms into its own slice of the scratch buffers, so the fill can run on the job
	// pool; sized here because a job must not allocate.
	if (_TempVertexBuffer.Length() < (int)maxVertexCount) _TempVertexBuffer.Resize(maxVertexCount);
	if (_TempNormalBuffer.Length() < (int)maxVertexCount) _TempNormalBuffer.Resize(maxVertexCount);

	MeshClass * mesh = VisibleSkinHead;
	MeshClass * remainingMesh = VisibleSkinHead;
	while (renderedVertexCount < VisibleVertexCount)
	{	mesh = remainingMesh;
		{	DynamicVBAccessClass::WriteLockClass l(&vb);
			VertexFormatXYZNDUV2 * dest_verts = l.Get_Formatted_Vertex_Array();
			unsigned vertex_offset=0;
			remainingMesh = NULL;
			_SkinFillMeshes.Reset_Active();

			while (mesh != NULL) {

				MeshModelClass * mmc = mesh->Peek_Model();
				int mesh_vertex_count=mmc->Get_Vertex_Count();
				//'Generals' mod to deal with cases where not all meshes fit in VB.
				if (vertex_offset+mesh_vertex_count > maxVertexCount || remainingMesh)
				{	//flag mesh so we know it didn't fit in the vertex buffer
					mesh->Set_Base_Vertex_Offset(VERTEX_BUFFER_OVERFLOW);
					if (remainingMesh == NULL)
						remainingMesh = mesh;	//start of meshes that didn't fit in buffer
					mesh = mesh->Peek_Next_Visible_Skin();	//skip rendering this mesh
					continue;
				}


		// If this assert hits, a skinned mesh has probably been added to the scene more than once.
		// Example: A skinned mesh was added to the scene then it was attached to a bone without being removed from the scene.
		WWASSERT((vertex_offset+mesh_vertex_count)<=VisibleVertexCount);
			DX8_RECORD_SKIN_RENDER(mesh->Get_Num_Polys(),mesh_vertex_count);

				mesh->Set_Base_Vertex_Offset(vertex_offset);
				_SkinFillMeshes.Add(mesh);
				vertex_offset+=mesh_vertex_count;
				renderedVertexCount += mesh_vertex_count;

				mesh = mesh->Peek_Next_Visible_Skin();
			}	//while

			SkinFillJob job;
			job.meshes=&_SkinFillMeshes[0];
			job.dest_verts=dest_verts;
			job.loc=&_TempVertexBuffer[0];
			job.norm=&_TempNormalBuffer[0];
			SortingRendererClass::Parallel_For(_SkinFillMeshes.Count(),SKINS_PER_CLAIM,Fill_Skin,&job);
		}//lock

		SNAPSHOT_SAY(("Set vb: %x ib: %x\n",vb,index_buffer));

		DX8Wrapper::Set_Vertex_Buffer(vb);
		DX8Wrapper::Set_Index_Buffer(index_buffer,0);

		//Flush the meshes which fit in the vertex buffer, applying all texture variations
		for (unsigned pass=0;pass<passes;++pass) {
			SNAPSHOT_SAY(("Pass: %d\n",pass));

			TextureCategoryListIterator it(&visible_texture_category_list[pass]);
			while (!it.Is_Done()) {
				it.Peek_Obj()->Render();
				it.Next();
			}
		}

		Render_Procedural_Material_Passes();
	}//while

	//remove all the rendered data from queues
	for (unsigned pass=0;pass<passes;++pass) {
		while (DX8TextureCategoryClass * tex = visible_texture_category_list[pass].Remove_Head()) {
		}
	}

	WWASSERT(renderedVertexCount==VisibleVertexCount);


	clearVisibleSkinList();
}

bool DX8SkinFVFCategoryContainer::Check_If_Mesh_Fits(MeshModelClass* mmc)
{
	if (!index_buffer) return true;	// No IB created - mesh will fit as a new ib will be created when inserting
	int required_polygons=mmc->Get_Polygon_Count();
	if (mmc->Get_Gap_Filler()) {
		required_polygons+=mmc->Get_Gap_Filler()->Get_Polygon_Count();
	}

	if ((required_polygons*3*mmc->Get_Pass_Count())<=index_buffer->Get_Index_Count()-used_indices) {
		return true;
	}
	return false;
}

void DX8SkinFVFCategoryContainer::clearVisibleSkinList() 
{
	while (VisibleSkinHead != NULL)
	{
		MeshClass* next = VisibleSkinHead->Peek_Next_Visible_Skin();
		VisibleSkinHead->Set_Next_Visible_Skin(NULL);
		VisibleSkinHead = next;
	}
	VisibleSkinHead = NULL;
	VisibleSkinTail = NULL;
	VisibleVertexCount = 0;
}
void DX8SkinFVFCategoryContainer::Add_Visible_Skin(MeshClass * mesh) 
{
	if (mesh->Peek_Next_Visible_Skin() != NULL || mesh == VisibleSkinTail)
	{
		DEBUG_CRASH(("Mesh %s is already a visible skin, and we tried to add it again... please notify Mark W or Steven J immediately!\n",mesh->Get_Name()));
		return;
	}
	if (VisibleSkinHead == NULL)
		VisibleSkinTail = mesh;
	mesh->Set_Next_Visible_Skin(VisibleSkinHead);
	VisibleSkinHead = mesh;
	VisibleVertexCount += mesh->Peek_Model()->Get_Vertex_Count();
}


// ----------------------------------------------------------------------------

void DX8SkinFVFCategoryContainer::Reset()
{
	clearVisibleSkinList();
	
	for (unsigned pass=0;pass<passes;++pass) {
		while (DX8TextureCategoryClass* texture_category=texture_category_list[pass].Peek_Head()) {
			delete texture_category;
		}
	}

	REF_PTR_RELEASE(index_buffer);
	used_indices=0;
}

// ----------------------------------------------------------------------------

void DX8SkinFVFCategoryContainer::Add_Mesh(MeshModelClass* mmc)
{
	Vertex_Split_Table split_table(mmc);

	Generate_Texture_Categories(split_table,0);
}

// ----------------------------------------------------------------------------

unsigned DX8TextureCategoryClass::Add_Mesh(
	Vertex_Split_Table& split_table,
	unsigned vertex_offset,
	unsigned index_offset,
	IndexBufferClass* index_buffer,
	unsigned pass)
{
	int poly_count=split_table.Get_Polygon_Count();

	unsigned index_count=0;

	/*
	** Count the polygons in the given mesh in the given pass which match this texture category
	*/
	unsigned polygons=0;

	for (int i=0;i<poly_count;++i) {
		bool all_textures_same = true;
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass, stage) == textures[stage]);
		}
		VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
		ShaderClass shd=split_table.Peek_Shader(i,pass);

		if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
			polygons++;
		}
	}

	/*
	** Add the indices for the polygons that match into this renderer's dx8 index table.
	*/
	if (polygons) {

		index_count=polygons*3;
#ifndef ENABLE_STRIPING
		bool stripify=false;
#else
		bool stripify=true;
		if (index_buffer->Type()==BUFFER_TYPE_SORTING || index_buffer->Type()==BUFFER_TYPE_DYNAMIC_SORTING) {
			stripify=false;
		}
#endif;
		const TriIndex* src_indices=(const TriIndex*)split_table.Get_Polygon_Array(pass);//mmc->Get_Polygon_Array();

		if (stripify) {
			int* triangles=W3DNEWARRAY int[index_count];
			int triangle_index_count=0;
			for (int i=0;i<poly_count;++i) {
				bool all_textures_same = true;
				for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
					all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass, stage) == textures[stage]);
				}
				VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
				ShaderClass shd=split_table.Peek_Shader(i,pass);

				if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
					triangles[triangle_index_count++]=src_indices[i][0]+vertex_offset;
					triangles[triangle_index_count++]=src_indices[i][1]+vertex_offset;
					triangles[triangle_index_count++]=src_indices[i][2]+vertex_offset;
				}
			}

			int* strips=StripOptimizerClass::Stripify(triangles, triangle_index_count/3);
			delete[] triangles;
			int* strip=StripOptimizerClass::Combine_Strips(strips+1,strips[0]);
			delete[] strips;

			if (index_count<unsigned(strip[0])) {
				stripify=false;
			}
			else {
				index_count=strip[0];

				DX8PolygonRendererClass* p_renderer=W3DNEW DX8PolygonRendererClass(
					index_count,
					split_table.Get_Mesh_Model_Class(),
					this,
					vertex_offset,
					index_offset,
					true,
					pass);
				PolygonRendererList.Add_Tail(p_renderer);

				{
					IndexBufferClass::AppendLockClass l(index_buffer,index_offset,index_count);
					unsigned short* dst_indices=l.Get_Index_Array();

					unsigned short vmin=0xffff;
					unsigned short vmax=0;

					/*
					** Iterate over the polys for this pass, adding each one that matches this texture+material+shader
					*/
					for (unsigned i=0;i<index_count;++i) {
						unsigned short idx;

						idx=(unsigned short)(strip[i+1]);
						vmin=MIN(vmin,idx);
						vmax=MAX(vmax,idx);
						*dst_indices++=idx;
					}
					
					/*
					** Remember the min and max vertex indices that these polygons used (for optimization)
					*/
					p_renderer->Set_Vertex_Index_Range(vmin,vmax-vmin+1);
				}
			}
			delete[] strip;
		}

		// Need to check stripify again as it may be changed to false by the previous statement
		if (!stripify ) {
			DX8PolygonRendererClass* p_renderer=W3DNEW DX8PolygonRendererClass(
				index_count,
				split_table.Get_Mesh_Model_Class(),
				this,
				vertex_offset,
				index_offset,
				false,
				pass);
			PolygonRendererList.Add_Tail(p_renderer);

			IndexBufferClass::AppendLockClass l(index_buffer,index_offset,index_count);
			unsigned short* dst_indices=l.Get_Index_Array();

			unsigned short vmin=0xffff;
			unsigned short vmax=0;

			/*
			** Iterate over the polys for this pass, adding each one that matches this texture+material+shader
			*/
			for (int i=0;i<poly_count;++i) {
				bool all_textures_same = true;
				for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
					all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass, stage) == textures[stage]);
				}
				VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
				ShaderClass shd=split_table.Peek_Shader(i,pass);

				if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
					unsigned short idx;

					idx=(unsigned short)(src_indices[i][0]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d, ",idx));

					idx=(unsigned short)(src_indices[i][1]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d, ",idx));

					idx=(unsigned short)(src_indices[i][2]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d\n",idx));
				}
			}

			WWASSERT((vmax-vmin)<split_table.Get_Mesh_Model_Class()->Get_Vertex_Count());
			
			/*
			** Remember the min and max vertex indices that these polygons used (for optimization)
			*/
			p_renderer->Set_Vertex_Index_Range(vmin,vmax-vmin+1);
			WWASSERT(index_count<=unsigned(split_table.Get_Polygon_Count()*3));
		}
	}

	return index_count;
}

// ----------------------------------------------------------------------------

void DX8TextureCategoryClass::Render(void)
{
	#ifdef WWDEBUG
	if (!WW3D::Expose_Prelit()) {
	#endif

		for (unsigned i=0;i<MeshMatDescClass::MAX_TEX_STAGES;++i) 
		{
			SNAPSHOT_SAY(("Set_Texture(%d,%s)\n",i,Peek_Texture(i) ? Peek_Texture(i)->Get_Texture_Name() : "NULL"));
			DX8Wrapper::Set_Texture(i,Peek_Texture(i));
		}

	#ifdef WWDEBUG
	}
	#endif

	SNAPSHOT_SAY(("Set_Material(%s)\n",Peek_Material() ? Peek_Material()->Get_Name() : "NULL"));
	VertexMaterialClass *vmaterial=(VertexMaterialClass *)Peek_Material();	//ugly cast from const but we'll restore it after changes so okay. -MW
	DX8Wrapper::Set_Material(vmaterial);

	SNAPSHOT_SAY(("Set_Shader(%x)\n",Get_Shader()));
	ShaderClass theShader = Get_Shader();

	//Setup an alpha blend version of this shader just in case it's needed. -MW
	ShaderClass theAlphaShader = theShader;
	theAlphaShader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	theAlphaShader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	//if we want to allow other translucent polygons behind this mesh, we need to disable z-write but
	//this will cause sorting errors on this mesh.
	//theAlphaShader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);

	DX8Wrapper::Set_Shader(theShader);

	if (m_gForceMultiply && theShader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ZERO) {
		theShader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_COLOR);
		theShader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
		DX8Wrapper::Set_Shader(theShader);
		//VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		//DX8Wrapper::Set_Material(material);
		//REF_PTR_RELEASE(material);
		DX8Wrapper::Apply_Render_State_Changes();
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);
	}


	bool renderTasksRemaining=false;

	PolyRenderTaskClass * prt = render_task_head;
	PolyRenderTaskClass * last_prt = NULL;

	while (prt) {

		/*
		** Dig out the parameters for this render task
		*/
		DX8PolygonRendererClass * renderer = prt->Peek_Polygon_Renderer();
		MeshClass * mesh = prt->Peek_Mesh();
		
		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)	//check if this mesh is valid
		{	//skip this mesh so it gets rendered later after vertices are filled in.
			last_prt = prt;
			prt = prt->Get_Next_Visible();
			renderTasksRemaining = true;
			continue;
		}

		SNAPSHOT_SAY(("mesh = %s\n",mesh->Get_Name()));

		#ifdef WWDEBUG	
		// Debug rendering: if it exists, expose prelighting on this mesh by disabling all base textures.
		if (WW3D::Expose_Prelit()) {
			switch (mesh->Peek_Model()->Get_Flag (MeshGeometryClass::PRELIT_MASK)) {

				unsigned i;

				case MeshGeometryClass::PRELIT_VERTEX:
					
					// Disable texturing on all stages and passes.
					for (i = 0; i < MeshMatDescClass::MAX_TEX_STAGES; i++) 
					{
						DX8Wrapper::Set_Texture (i, NULL);
					}
					break;

				case MeshGeometryClass::PRELIT_LIGHTMAP_MULTI_PASS:
					
					// Disable texturing on all but the last pass.
					if (pass == mesh->Peek_Model()->Get_Pass_Count() - 1) {
						for (i = 0; i < MeshMatDescClass::MAX_TEX_STAGES; i++) 
						{
							DX8Wrapper::Set_Texture (i, Peek_Texture (i));
						}
					} else {
						for (i = 0; i < MAX_TEXTURE_STAGES; i++) {
							DX8Wrapper::Set_Texture (i, NULL);
						}
					}
					break;

				case MeshGeometryClass::PRELIT_LIGHTMAP_MULTI_TEXTURE:
					
					// Disable texturing on all but the zeroth stage of each pass.
					DX8Wrapper::Set_Texture (0, Peek_Texture (0));
					for (i = 1; i < MeshMatDescClass::MAX_TEX_STAGES; i++) 
					{
						DX8Wrapper::Set_Texture (i, NULL);
					}
					break;

				default:
					for (i = 0; i < MeshMatDescClass::MAX_TEX_STAGES; i++) 
					{
						DX8Wrapper::Set_Texture (i, Peek_Texture (i));
					}
					break;
			}
		}
		#endif

		/*
		** If the user is not installing LightEnvironmentClasses, we leave the lighting render
		** states untouched.  This way they can set a couple global lights that affect the entire scene.
		*/
		LightEnvironmentClass * lenv = mesh->Get_Lighting_Environment();
		if (lenv != NULL) {
			SNAPSHOT_SAY(("LightEnvironment, lights: %d\n",lenv->Get_Light_Count()));
			DX8Wrapper::Set_Light_Environment(lenv);
		}
		else {
			SNAPSHOT_SAY(("No light environment\n"));
		}

		/*
		** Support for ALIGNED and ORIENTED camera modes
		*/
		const Matrix3D* world_transform = &mesh->Get_Transform();
		bool identity=mesh->Is_Transform_Identity();
		Matrix3D tmp_world;

		if (mesh->Peek_Model()->Get_Flag(MeshModelClass::ALIGNED)) {
			SNAPSHOT_SAY(("Camera mode ALIGNED\n"));

			Vector3 mesh_position;
			Vector3 camera_z_vector;
			
			TheDX8MeshRenderer.Peek_Camera()->Get_Transform().Get_Z_Vector(&camera_z_vector);
			mesh->Get_Transform().Get_Translation(&mesh_position);

			tmp_world.Obj_Look_At(mesh_position,mesh_position + camera_z_vector,0.0f);
			world_transform = &tmp_world;

		} else if (mesh->Peek_Model()->Get_Flag(MeshModelClass::ORIENTED)) {
			SNAPSHOT_SAY(("Camera mode ORIENTED\n"));
		
			Vector3 mesh_position;
			Vector3 camera_position;

			TheDX8MeshRenderer.Peek_Camera()->Get_Transform().Get_Translation(&camera_position);
			mesh->Get_Transform().Get_Translation(&mesh_position);

			tmp_world.Obj_Look_At(mesh_position,camera_position,0.0f);
			world_transform = &tmp_world;
		
		} else if (mesh->Peek_Model()->Get_Flag(MeshModelClass::SKIN)) {
			SNAPSHOT_SAY(("Set world identity (for skin)\n"));
			
			tmp_world.Make_Identity();
			world_transform = &tmp_world;
			identity=true;
		}


		if (identity) {
			SNAPSHOT_SAY(("Set_World_Identity\n"));
			DX8Wrapper::Set_World_Identity();
		}
		else {
			SNAPSHOT_SAY(("Set_World_Transform\n"));
			DX8Wrapper::Set_Transform(D3DTS_WORLD,*world_transform);
		}

		
//--------------------------------------------------------------------
		if (mesh->Get_ObjectScale() != 1.0f)
			DX8Wrapper::Set_DX8_Render_State(D3DRS_NORMALIZENORMALS, true);
//--------------------------------------------------------------------
		/*
		** Render mesh using either sorting or immediate pipeline
		*/
		//(gth) this if statement's contents are not tabbed to avoid perforce merge problems...
		if (!DX8RendererDebugger::Is_Enabled() || !mesh->Is_Disabled_By_Debugger()) {

		// The object's look on the Direct3D 11 frame (RTS3DScene::renderOneObject picks it), for this
		// mesh's draws only.  A sorted mesh draws later, from the sorting pool, which keeps the look
		// with its triangles (SortingRendererClass::Insert_Triangles), and on Linux its sun shadow
		// coverage too.  The cloak writes alpha 1 (effect_cloak), so a mesh that blends by its own
		// alpha (the Stealth General's yard emblem, a campfire card, a well's water) would come out
		// as a solid quad; it draws without the look, as under -d3d9.  The whole mesh goes: one that
		// mixes opaque polygons with a blended card loses the glass on its opaque ones too.
		unsigned draw_effect = mesh->Get_Draw_Effect();
		if (draw_effect == DRAW_EFFECT_CLOAK && mesh->Is_Alpha()) {
			draw_effect = DRAW_EFFECT_NONE;
		}
		if (draw_effect != 0) {
			SunShadow::SetCasterEffect(DX8Wrapper::_Get_D3D_Device(),draw_effect, mesh->Get_Draw_Effect_Parameters());
		}
		// A house colour mesh under the Team Glow option.  Immediate draws only: a sorted one draws later,
		// after this has been put back, and goes without.
		const bool team_glow = mesh->Peek_Model()->Get_Flag(MeshGeometryClass::TEAM_GLOW) != 0;
		if (team_glow) {
			Direct3D11_Set_Team_Glow(true);
		}

		if ((!!mesh->Peek_Model()->Get_Flag(MeshGeometryClass::SORT)) && WW3D::Is_Sorting_Enabled()) {
			renderer->Render_Sorted(mesh->Get_Base_Vertex_Offset(),mesh->Get_Bounding_Sphere());
		} else {
			//non-transparent mesh that will be rendered immediately.  Okay to adjust the shader/material
			//if necessary
			if (mesh->Get_Alpha_Override() != 1.0 || (mesh->Get_User_Data() && *(int *)mesh->Get_User_Data() == RenderObjClass::USER_DATA_MATERIAL_OVERRIDE))
			{	//mesh has material override of some kind
				//adjust the opacity of this model
				float oldOpacity=vmaterial->Get_Opacity();
				Vector3 oldDiffuse;
				Vector2 oldUVOffset;
				unsigned int oldUVOffsetSyncTime;
				vmaterial->Get_Diffuse(&oldDiffuse);
				LinearOffsetTextureMapperClass *oldMapper=(LinearOffsetTextureMapperClass *)vmaterial->Peek_Mapper();
				if ( mesh->Get_User_Data() && *(int *)mesh->Get_User_Data() == RenderObjClass::USER_DATA_MATERIAL_OVERRIDE && oldMapper && oldMapper->Mapper_ID() == TextureMapperClass::MAPPER_ID_LINEAR_OFFSET)
				{	RenderObjClass::Material_Override *matOverride=(RenderObjClass::Material_Override *)mesh->Get_User_Data();
					oldUVOffsetSyncTime = oldMapper->Get_LastUsedSyncTime();
					oldMapper->Set_LastUsedSyncTime(WW3D::Get_Sync_Time());	//make sure zero time passes for the mapper.
					oldMapper->Get_Current_UV_Offset(oldUVOffset);
					oldMapper->Set_Current_UV_Offset(matOverride->customUVOffset);
				}
				else
					oldMapper=NULL;
				if (mesh->Get_Alpha_Override() != 1.0)
				{
					if (mesh->Is_Additive())
					{	//additvie blended mesh can't switch to alpha or we will get a black outline.
						//so adjust diffuse color instead.
						vmaterial->Set_Diffuse(mesh->Get_Alpha_Override(),mesh->Get_Alpha_Override(),mesh->Get_Alpha_Override());
						theAlphaShader = theShader;	//keep using additive blending.
					}
					vmaterial->Set_Opacity(mesh->Get_Alpha_Override());
					DX8Wrapper::Set_Shader(theAlphaShader);
					DX8Wrapper::Apply_Render_State_Changes();
					DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF,(int)((float)0x60*mesh->Get_Alpha_Override()));
					renderer->Render(mesh->Get_Base_Vertex_Offset());
					DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHAREF,0x60);
					vmaterial->Set_Opacity(oldOpacity);	//restore previous value
					vmaterial->Set_Diffuse(oldDiffuse.X,oldDiffuse.Y,oldDiffuse.Z);
					DX8Wrapper::Set_Shader(theShader);	//restore previous value
				}
				else
					renderer->Render(mesh->Get_Base_Vertex_Offset());

				if (oldMapper)	//did we override the uv offset?
				{	oldMapper->Set_LastUsedSyncTime(oldUVOffsetSyncTime);
					oldMapper->Set_Current_UV_Offset(oldUVOffset);
				}
				DX8Wrapper::Set_Material(NULL);	//force a reset of vertex material since we secretly changed opacity
				DX8Wrapper::Set_Material(vmaterial);	//restore previous material.
			} 
			else
				renderer->Render(mesh->Get_Base_Vertex_Offset());
		}
		if (draw_effect != 0) {
			static const float no_effect[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			SunShadow::SetCasterEffect(DX8Wrapper::_Get_D3D_Device(),0, no_effect);
		}
		if (team_glow) {
			Direct3D11_Set_Team_Glow(false);
		}
//--------------------------------------------------------------------
		if (mesh->Get_ObjectScale() != 1.0f)
			DX8Wrapper::Set_DX8_Render_State(D3DRS_NORMALIZENORMALS, false);
//--------------------------------------------------------------------




        } // (gth) non-tabbed to aviod per-force merge problems...

		/*
		** Move to the next render task.  Note that the delete should be fast because prt's are pooled
		*/
		PolyRenderTaskClass * next_prt = prt->Get_Next_Visible();

		// remove from list, then delete
		if (last_prt == NULL) {
		   render_task_head = next_prt;
		} else {
		  last_prt->Set_Next_Visible(next_prt);
		}

		delete prt;
		prt = next_prt;
	}

	if (!renderTasksRemaining)
	{	WWASSERT(!render_task_head);
		Clear_Render_List();
	}
}


DX8MeshRendererClass::DX8MeshRendererClass()
	:
	camera(NULL),
	enable_lighting(true),
	texture_category_container_list_skin(NULL),
	visible_decal_meshes(NULL)
{
}

DX8MeshRendererClass::~DX8MeshRendererClass()
{
	Invalidate(true);
	Clear_Pending_Delete_Lists();
	if (texture_category_container_list_skin != NULL) {
		delete texture_category_container_list_skin;
	}
}

void DX8MeshRendererClass::Init(void)
{
	// DMS - Only allocate one if we havent already (leak fix)
	if(!texture_category_container_list_skin)
		texture_category_container_list_skin = W3DNEW FVFCategoryList;
}

void DX8MeshRendererClass::Shutdown(void)
{
	Invalidate(true);
	Clear_Pending_Delete_Lists();
	_TempVertexBuffer.Clear();	//free memory
	_TempNormalBuffer.Clear();
	_SkinFillMeshes.Clear();
}

// ----------------------------------------------------------------------------

void DX8MeshRendererClass::Clear_Pending_Delete_Lists()
{
	while (DX8TextureCategoryClass* category=texture_category_delete_list.Remove_Head()) {
		delete category;
	}
	while (DX8FVFCategoryContainer* container=fvf_category_container_delete_list.Remove_Head()) {
		delete container;
	}
}

// ----------------------------------------------------------------------------

static void Add_Rigid_Mesh_To_Container(FVFCategoryList* container_list,unsigned fvf,MeshModelClass* mmc)
{
	WWASSERT(container_list);
	DX8FVFCategoryContainer * container = NULL;
	bool sorting=((!!mmc->Get_Flag(MeshModelClass::SORT)) && WW3D::Is_Sorting_Enabled() && (mmc->Get_Sort_Level() == SORT_LEVEL_NONE));

	FVFCategoryListIterator it(container_list);
	while (!it.Is_Done()) {
		container = it.Peek_Obj();
		if (sorting==container->Is_Sorting() && container->Check_If_Mesh_Fits(mmc)) {
			container->Add_Mesh(mmc);
			return;
		}
		it.Next();
	}

	container=W3DNEW DX8RigidFVFCategoryContainer(fvf,sorting);
	container_list->Add_Tail(container);
	container->Add_Mesh(mmc);
}

// ----------------------------------------------------------------------------

void DX8MeshRendererClass::Unregister_Mesh_Type(MeshModelClass* mmc)
{
	while (DX8PolygonRendererClass* n=mmc->PolygonRendererList.Remove_Head()) {
		delete n;
	}
	_RegisteredMeshList.Remove(mmc);

	// Also remove the gap filler!
	if (mmc->GapFiller) {
		GapFillerClass* gf=mmc->GapFiller;
		mmc->GapFiller=NULL;
		delete gf;
	}

}


void DX8MeshRendererClass::Register_Mesh_Type(MeshModelClass* mmc)
{
	WWMEMLOG(MEM_GEOMETRY);
#ifdef ENABLE_CATEGORY_LOG
	WWDEBUG_SAY(("Registering mesh: %s (%d polys, %d verts + %d gap polygons)\n",mmc->Get_Name(),mmc->Get_Polygon_Count(),mmc->Get_Vertex_Count(),mmc->Get_Gap_Filler_Polygon_Count()));
#endif
	bool skin=(mmc->Get_Flag(MeshModelClass::SKIN) && mmc->VertexBoneLink);
	bool sorting=((!!mmc->Get_Flag(MeshModelClass::SORT)) && WW3D::Is_Sorting_Enabled() && (mmc->Get_Sort_Level() == SORT_LEVEL_NONE));

	if (skin) {

		/*
		** This mesh is a skin.  Add it to a DX8SkinFVFCategoryContainer.
		*/
		WWASSERT(texture_category_container_list_skin);

		FVFCategoryListIterator it(texture_category_container_list_skin);
		while (!it.Is_Done()) {
			DX8FVFCategoryContainer * container = it.Peek_Obj();
			if (sorting==container->Is_Sorting() && container->Check_If_Mesh_Fits(mmc)) {
				container->Add_Mesh(mmc);
				return;
			}
			it.Next();
		}

		DX8FVFCategoryContainer * new_container=W3DNEW DX8SkinFVFCategoryContainer(sorting);
		texture_category_container_list_skin->Add_Tail(new_container);
		new_container->Add_Mesh(mmc);
	
	} else {

		/*
		** We should never try to add the same mesh model to the system twice.
		*/
		WWASSERT_PRINT(_RegisteredMeshList.Contains(mmc) == false,("Mesh name: %s",mmc->Get_Name()));

		/*
		** If the previous step didn't add the mesh, then we have to actually process this mesh
		*/
		if (!_RegisteredMeshList.Contains(mmc)) {

			unsigned fvf=DX8FVFCategoryContainer::Define_FVF(mmc,enable_lighting);

			/*
			** Search for an existing FVF Category Container that matches this mesh
			*/
			int i;
			for (i =0;i<texture_category_container_lists_rigid.Count();++i) {
				FVFCategoryList * list=texture_category_container_lists_rigid[i];
				WWASSERT(list);
				DX8FVFCategoryContainer * container=list->Peek_Head();
				if (container && container->Get_FVF()!=fvf) continue;

				Add_Rigid_Mesh_To_Container(list,fvf,mmc);
				break;
			}

			if (i==texture_category_container_lists_rigid.Count()) {

				/*
				** We couldn't find an existing FVF category container so we have to add one.  Future
				** meshes that use this FVF will also be able to use this container.
				*/
				FVFCategoryList * new_fvf_category = W3DNEW FVFCategoryList();
				texture_category_container_lists_rigid.Add(new_fvf_category);
				Add_Rigid_Mesh_To_Container(new_fvf_category,fvf,mmc);
			}

			/*
			** Done processing the mesh, add its polygon renderers to the global registered mesh list
			*/
			if (mmc->PolygonRendererList.Is_Empty() == false) {
				_RegisteredMeshList.Add_Tail(mmc);
			}
			else {
				WWDEBUG_SAY(("Error: Register_Mesh_Type failed! file: %s line: %d\r\n",__FILE__,__LINE__));
			}
		}
	}

	return;
}

static unsigned statistics_requested=0;

void DX8MeshRendererClass::Request_Log_Statistics()
{
	statistics_requested=WW3D::Get_Frame_Count();
}


// ---------------------------------------------------------------------------
//
// Render all meshes that are added to visible lists
//
// ---------------------------------------------------------------------------

static void Render_FVF_Category_Container_List(FVFCategoryList& list)
{
	FVFCategoryListIterator it(&list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Render();
		it.Next();
	}
}

static void Render_FVF_Category_Container_List_Delayed_Passes(FVFCategoryList& list)
{
	FVFCategoryListIterator it(&list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Render_Delayed_Procedural_Material_Passes();
		it.Next();
	}
}

// ---------------------------------------------------------------------------
//
// The sun's depth pass, instanced.  A texture category's walk sets a texture, a material, lights,
// a world matrix and a shader for every piece of every caster, and the backend then builds and
// compares a constant block and resolves a pipeline for each, all for a draw whose only output is
// depth.  An opaque rigid caster's depth is its triangles through world, view and projection and
// nothing else, so those are drawn here, one draw per piece of a model whatever the number of
// copies in the sun's box.  An alpha tested one adds its texture's alpha test, and a skin is
// skinned in the vertex program from its bind pose, uploaded once, and the pass's bones, so its
// vertices are not deformed on the CPU for the pass at all.
//
// ---------------------------------------------------------------------------

int DX8MeshRendererClass::Shadow_Lean_Draws = 0;
int DX8MeshRendererClass::Shadow_Lean_Instances = 0;
int DX8MeshRendererClass::Shadow_Lean_Alpha_Tested = 0;
int DX8MeshRendererClass::Shadow_Lean_Skinned = 0;
int DX8MeshRendererClass::Shadow_Old_Meshes[DX8MeshRendererClass::SHADOW_OLD_REASON_COUNT] = { 0 };

void DX8MeshRendererClass::Reset_Shadow_Caster_Counts()
{
	Shadow_Lean_Draws = 0;
	Shadow_Lean_Instances = 0;
	Shadow_Lean_Alpha_Tested = 0;
	Shadow_Lean_Skinned = 0;
	memset(Shadow_Old_Meshes, 0, sizeof(Shadow_Old_Meshes));
}

const char * DX8MeshRendererClass::Shadow_Old_Reason_Name(int reason)
{
	static const char * const names[SHADOW_OLD_REASON_COUNT] = {
		"sorted", "camera-facing", "draw-effect", "fading", "debug", "streaming", "skin-passes", "blended",
		"alpha-test", "skin-alpha-test", "format", "no-base-pass", "refused"
	};
	return names[reason];
}

/* A pass groups the casters by model: the first instance of a model this pass works its pieces
	 out (Describe_Shadow_Caster_Piece, its base pass pieces as runs) and every instance after it
	 only joins the model's group.  Each piece of each model is then one run over the group's
	 instances, which a counting sort by group lays out one after another. */
struct ShadowCasterPiece
{
	TextureClass * Texture;				// stage 0 of an alpha tested piece, NULL otherwise
	DX11ShadowCasterKind Kind;
	DX11BufferTwinClass * Vertices;		// NULL for a skin, which reads the skin source
	unsigned Stride;
	unsigned TextureOffset;
	DX11BufferTwinClass * Indices;
	unsigned StartIndex;
	unsigned IndexCount;
	bool Strip;
	bool Culled;
	unsigned long DepthFunction;
	DX8PolygonRendererClass * Renderer;
};

struct ShadowCasterGroup
{
	MeshModelClass * Model;
	unsigned FirstPiece;
	unsigned PieceCount;
	unsigned InstanceCount;
	unsigned FirstInstance;
	unsigned BaseVertex;
};

struct ShadowCasterInstance
{
	MeshClass * Mesh;					// a reference each, as a render task holds
	unsigned Group;
	const float * World;				// the Matrix3D's rows, NULL for an identity world
	unsigned FirstBone;
};

struct ShadowCasterRunEntry
{
	TextureClass * Texture;
	unsigned Piece;
	DX11ShadowCasterRun Run;
};

static unsigned _ShadowCasterPass = 1;
static std::vector<ShadowCasterPiece> _ShadowCasterPieces;
static std::vector<ShadowCasterGroup> _ShadowCasterGroups;
static std::vector<ShadowCasterInstance> _ShadowCasterInstances;
static std::vector<MeshClass *> _ShadowCasterOrdered;
static std::vector<const float *> _ShadowCasterWorlds;
static std::vector<unsigned> _ShadowCasterFirstBones;
static std::vector<const float *> _ShadowCasterBones;
static std::vector<DX11ShadowCasterRun> _ShadowCasterRuns;
static std::vector<unsigned> _ShadowCasterRunPieces;
static std::vector<ShadowCasterRunEntry> _ShadowCasterRunEntries;
// The skeleton whose bones went in last, which the next skin of the same model reuses.
static const HTreeClass * _ShadowCasterLastTree = NULL;
static unsigned _ShadowCasterLastFirstBone = 0;
static std::vector<DX11ShadowSkinVertex> _ShadowSkinVertices;

static DX11BufferTwinClass * Shadow_Caster_Vertex_Twin(VertexBufferClass * buffer)
{
	if (buffer == NULL || buffer->Type() != BUFFER_TYPE_DX8) {
		return NULL;
	}
	DX8VertexBufferClass * vertices = static_cast<DX8VertexBufferClass *>(buffer);
	return (vertices->Get_DX8_Vertex_Buffer() != NULL) ? vertices->Get_DX11_Twin() : NULL;
}

static DX11BufferTwinClass * Shadow_Caster_Index_Twin(IndexBufferClass * buffer)
{
	if (buffer == NULL || buffer->Type() != BUFFER_TYPE_DX8) {
		return NULL;
	}
	DX8IndexBufferClass * indices = static_cast<DX8IndexBufferClass *>(buffer);
	return (indices->Get_DX8_Index_Buffer() != NULL) ? indices->Get_DX11_Twin() : NULL;
}

/* Whether an alpha tested piece's test is stage 0's texel alpha alone, which is all the lean pixel
	 program reads: a plain texture through the material's own coordinates (no mapper), an alpha the
	 stages take from the texture and the diffuse colour only, and a diffuse alpha of one, which is
	 the material's opacity when it is lit from the material and a vertex colour's alpha otherwise,
	 so a vertex colour has to stay out of it. */
static bool Shadow_Caster_Alpha_Is_Texture(DX8TextureCategoryClass * category, const ShaderClass & shader,
	unsigned fvf, unsigned & uv_set)
{
	VertexMaterialClass * material = const_cast<VertexMaterialClass *>(category->Peek_Material());
	const ShaderClass::PriGradientType gradient = shader.Get_Primary_Gradient();
	if (category->Peek_Texture(0) == NULL || shader.Get_Texturing() != ShaderClass::TEXTURING_ENABLE
		|| (gradient != ShaderClass::GRADIENT_DISABLE && gradient != ShaderClass::GRADIENT_MODULATE
			&& gradient != ShaderClass::GRADIENT_ADD && gradient != ShaderClass::GRADIENT_MODULATE2X)
		|| shader.Get_Post_Detail_Alpha_Func() != ShaderClass::DETAILALPHA_DISABLE
		|| (material != NULL && material->Peek_Mapper(0) != NULL)) {
		return false;
	}
	// Lit, the diffuse alpha is the material's whenever the vertex has no colour to take it from,
	// whatever source the material names (ffvertex.cpp falls back to MaterialDiffuse).
	const bool lit = material != NULL && material->Get_Lighting() && !WW3D::Is_Coloring_Enabled();
	const bool lit_from_material = lit && material->Get_Diffuse_Color_Source() == VertexMaterialClass::MATERIAL;
	if ((lit && material->Get_Opacity() != 1.0f)
		|| ((fvf & D3DFVF_DIFFUSE) != 0 && !lit_from_material)) {
		return false;
	}
	uv_set = (material != NULL) ? (unsigned)material->Get_UV_Source(0) : 0;
	const unsigned uv_sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	const unsigned uv_format = (fvf >> (16 + uv_set * 2)) & 3;
	return uv_set < uv_sets && uv_format == D3DFVF_TEXTUREFORMAT2;
}

/* One base pass piece of a model as a run, or the reason the model goes the old way. */
static int Describe_Shadow_Caster_Piece(DX8PolygonRendererClass * renderer, bool skin,
	ShadowCasterPiece & record)
{
	DX8TextureCategoryClass * category = renderer->Get_Texture_Category();
	const ShaderClass shader = category->Get_Shader();
	DX8FVFCategoryContainer * container = category->Get_Container();
	if (shader.Get_Src_Blend_Func() != ShaderClass::SRCBLEND_ONE
		|| shader.Get_Dst_Blend_Func() != ShaderClass::DSTBLEND_ZERO) {
		return DX8MeshRendererClass::SHADOW_OLD_BLENDED;
	}
	const bool alpha_tested = shader.Get_Alpha_Test() != ShaderClass::ALPHATEST_DISABLE;
	if (alpha_tested && skin) {
		return DX8MeshRendererClass::SHADOW_OLD_SKIN_ALPHA_TEST;
	}
	record.Indices = Shadow_Caster_Index_Twin(container->Peek_Index_Buffer());
	record.Texture = NULL;
	record.TextureOffset = 0;
	if (skin) {
		// The skin source's vertices, which Flush_Shadow_Casters places; the indices are the
		// model's own, the container having put every skin at vertex offset 0 (Add_Mesh).
		record.Kind = DX11_SHADOW_CASTER_SKINNED;
		record.Vertices = NULL;
		record.Stride = 0;
		if (record.Indices == NULL) {
			return DX8MeshRendererClass::SHADOW_OLD_FORMAT;
		}
	} else {
		VertexBufferClass * buffer = static_cast<DX8RigidFVFCategoryContainer *>(container)->Peek_Vertex_Buffer();
		record.Kind = DX11_SHADOW_CASTER_RIGID;
		record.Vertices = Shadow_Caster_Vertex_Twin(buffer);
		if ((container->Get_FVF() & D3DFVF_POSITION_MASK) != D3DFVF_XYZ || record.Vertices == NULL
			|| record.Indices == NULL) {
			return DX8MeshRendererClass::SHADOW_OLD_FORMAT;
		}
		record.Stride = buffer->FVF_Info().Get_FVF_Size();
		if (alpha_tested) {
			unsigned uv_set = 0;
			if (!Shadow_Caster_Alpha_Is_Texture(category, shader, container->Get_FVF(), uv_set)) {
				return DX8MeshRendererClass::SHADOW_OLD_ALPHA_TEST;
			}
			record.Kind = DX11_SHADOW_CASTER_ALPHA_TESTED;
			record.Texture = category->Peek_Texture(0);
			record.TextureOffset = buffer->FVF_Info().Get_Tex_Offset(uv_set);
		}
	}

	record.StartIndex = (unsigned short)renderer->Get_Index_Offset();
	record.Strip = renderer->Is_Strip();
	record.IndexCount = record.Strip
		? (unsigned)(unsigned short)(renderer->Get_Index_Count() - 2) + 2
		: (unsigned)(unsigned short)(renderer->Get_Index_Count() / 3) * 3;
	// ShaderClass::Apply's cull and depth test; which way the cull faces is read when the run is made.
	record.Culled = shader.Get_Cull_Mode() != 0;
	record.DepthFunction = (unsigned long)(int(shader.Get_Depth_Compare()) + 1);
	record.Renderer = renderer;
	return -1;
}

/* A model's pieces from the given one on, merged where they can be one draw: a model's base pass
	 pieces sit one after another in the container's index buffer, a texture category each
	 (Generate_Texture_Categories hands the indices out in order), and the depth pass draws two
	 opaque pieces in the same buffers with the same cull and test alike.  Only an order-free test
	 (less or less-equal, so the nearest surface wins whichever is drawn first) is merged, and only
	 lists. */
static void Merge_Shadow_Caster_Pieces(size_t first)
{
	std::sort(_ShadowCasterPieces.begin() + first, _ShadowCasterPieces.end(),
		[](const ShadowCasterPiece & left, const ShadowCasterPiece & right) { return left.StartIndex < right.StartIndex; });
	size_t kept = first;
	for (size_t index = first + 1; index < _ShadowCasterPieces.size(); ++index) {
		ShadowCasterPiece & last = _ShadowCasterPieces[kept];
		const ShadowCasterPiece & next = _ShadowCasterPieces[index];
		const bool order_free = last.DepthFunction == D3DCMP_LESSEQUAL || last.DepthFunction == D3DCMP_LESS;
		if (last.Kind != DX11_SHADOW_CASTER_ALPHA_TESTED && next.Kind == last.Kind && !last.Strip && !next.Strip
			&& next.Vertices == last.Vertices && next.Stride == last.Stride && next.Indices == last.Indices
			&& next.Culled == last.Culled && next.DepthFunction == last.DepthFunction && order_free
			&& last.StartIndex + last.IndexCount == next.StartIndex) {
			last.IndexCount += next.IndexCount;
		} else {
			_ShadowCasterPieces[++kept] = next;
		}
	}
	_ShadowCasterPieces.resize(kept + 1);
}

// The pass's records gone, their storage kept for the next pass, and a new pass begun.
static void Clear_Shadow_Casters()
{
	_ShadowCasterInstances.clear();
	_ShadowCasterPieces.clear();
	_ShadowCasterGroups.clear();
	_ShadowCasterBones.clear();
	_ShadowCasterLastTree = NULL;
	++_ShadowCasterPass;
}

bool DX8MeshRendererClass::Add_Shadow_Caster(MeshClass * mesh, bool has_material_passes)
{
	/* What the texture category's walk would do differently for this mesh, each a reason to leave
		 it there: a sorted or camera facing model, a look (the cloak's shadow), a fading opacity, a
		 mesh the debugger hides, the forced multiply that blends opaque
		 materials, a rigid model still waiting for room in a streaming buffer (a skin's offset is the
		 last fill's and means nothing here), and a skin whose material passes need its vertices
		 deformed into the container anyway. */
	MeshModelClass * model = mesh->Peek_Model();
	const bool skin = model->Get_Flag(MeshGeometryClass::SKIN) != 0;
	/* A material override (a tread's scrolling coordinates) only moves the coordinates of a stage 0
		 linear offset mapper, and no piece taken reads those: an opaque piece writes depth alone and an
		 alpha tested one is taken only without a mapper.  Fading swaps in a blending shader, which the
		 pass turns into its own alpha test on the faded alpha, and stays the old way. */
	int reason = -1;
	if (mesh->Get_Draw_Effect() != 0) {
		reason = SHADOW_OLD_DRAW_EFFECT;
	} else if (mesh->Get_Alpha_Override() != 1.0f) {
		reason = SHADOW_OLD_FADING;
	} else if (DX8RendererDebugger::Is_Enabled() || DX8TextureCategoryClass::Is_Force_Multiply()) {
		reason = SHADOW_OLD_DEBUG;
	} else if (!skin && mesh->Get_Base_Vertex_Offset() != 0) {
		// A rigid mesh's offset is nought (only a skin's fill sets one) unless it is waiting for room.
		reason = SHADOW_OLD_STREAMING;
	} else if (skin && has_material_passes) {
		reason = SHADOW_OLD_SKIN_PASSES;
	}
	if (reason >= 0) {
		++Shadow_Old_Meshes[reason];
		return false;
	}

	if (model->ShadowCasterPass != _ShadowCasterPass) {
		/* The model's first instance this pass.  Every base pass piece has to be taken, or the model
			 goes the old way whole.  Only pass 0 is drawn: a later pass is the same triangles again and
			 adds no depth the first did not write. */
		model->ShadowCasterPass = _ShadowCasterPass;
		if (model->Get_Flag(MeshGeometryClass::SORT)) {
			reason = SHADOW_OLD_SORTED;
		} else if (model->Get_Flag(MeshModelClass::ALIGNED) || model->Get_Flag(MeshModelClass::ORIENTED)) {
			reason = SHADOW_OLD_CAMERA_FACING;
		}
		const size_t first = _ShadowCasterPieces.size();
		DX8PolygonRendererListIterator it(&(model->PolygonRendererList));
		for (; reason < 0 && !it.Is_Done(); it.Next()) {
			DX8PolygonRendererClass * renderer = it.Peek_Obj();
			if (renderer->Get_Pass() != 0) {
				continue;
			}
			ShadowCasterPiece piece;
			reason = Describe_Shadow_Caster_Piece(renderer, skin, piece);
			if (reason < 0) {
				_ShadowCasterPieces.push_back(piece);
			}
		}
		if (reason < 0 && _ShadowCasterPieces.size() == first) {
			reason = SHADOW_OLD_NO_BASE_PASS;
		}
		if (reason >= 0) {
			_ShadowCasterPieces.resize(first);
			model->ShadowCasterGroup = -1 - reason;
		} else {
			Merge_Shadow_Caster_Pieces(first);
			ShadowCasterGroup group;
			group.Model = model;
			group.FirstPiece = (unsigned)first;
			group.PieceCount = (unsigned)(_ShadowCasterPieces.size() - first);
			group.InstanceCount = 0;
			group.FirstInstance = 0;
			group.BaseVertex = 0;
			model->ShadowCasterGroup = (int)_ShadowCasterGroups.size();
			_ShadowCasterGroups.push_back(group);
		}
	}
	if (model->ShadowCasterGroup < 0) {
		++Shadow_Old_Meshes[-1 - model->ShadowCasterGroup];
		return false;
	}

	/* Each instance's world, as the rows of its Matrix3D, NULL where the texture category's walk sets
		 an identity world (an identity transform, and every skin); each skeleton's bones in the pose
		 the skin container's fill would read them in (MeshClass::Get_Deformed_Vertices), and each
		 skin's first bone among them.  A model's skins are rendered one after another, so they share
		 one copy of the skeleton; a skeleton met again later only costs a second copy. */
	mesh->Add_Ref();
	ShadowCasterInstance instance;
	instance.Mesh = mesh;
	instance.Group = (unsigned)model->ShadowCasterGroup;
	instance.World = NULL;
	instance.FirstBone = 0;
	if (!skin) {
		if (!mesh->Is_Transform_Identity()) {
			instance.World = reinterpret_cast<const float *>(&mesh->Get_Transform());
		}
	} else {
		const HTreeClass * tree = mesh->Get_Container()->Get_HTree();
		if (tree != _ShadowCasterLastTree) {
			_ShadowCasterLastTree = tree;
			_ShadowCasterLastFirstBone = (unsigned)_ShadowCasterBones.size();
			const int pivots = tree->Num_Pivots();
			for (int pivot = 0; pivot < pivots; ++pivot) {
				_ShadowCasterBones.push_back(reinterpret_cast<const float *>(&tree->Get_Transform(pivot)));
			}
		}
		instance.FirstBone = _ShadowCasterLastFirstBone;
	}
	_ShadowCasterInstances.push_back(instance);
	++_ShadowCasterGroups[instance.Group].InstanceCount;
	return true;
}

void DX8MeshRendererClass::Flush_Shadow_Casters()
{
	if (_ShadowCasterInstances.empty()) {
		Clear_Shadow_Casters();
		return;
	}
	WWPROFILE("DX8MeshRenderer::Flush_Shadow_Casters");

	// The sun's view is set but waits in the wrapper for the next draw; the backend reads it here.
	DX8Wrapper::Apply_Render_State_Changes();

	/* The skins' bind poses into the skin source, a model the first time it is seen under the
		 source's generation.  A model that does not fit starts a new source, which every model uploaded
		 before it has to go into again, this pass's included, so the walk starts over. */
	bool uploaded = true;
	bool placed = false;
	while (uploaded && !placed) {
		placed = true;
		const unsigned generation = Direct3D11_Shadow_Skin_Generation();
		for (size_t index = 0; index < _ShadowCasterGroups.size() && placed && uploaded; ++index) {
			ShadowCasterGroup & group = _ShadowCasterGroups[index];
			if (_ShadowCasterPieces[group.FirstPiece].Kind != DX11_SHADOW_CASTER_SKINNED) {
				continue;
			}
			MeshModelClass * model = group.Model;
			if (model->ShadowSkinSourceOffset < 0 || model->ShadowSkinSourceGeneration != generation) {
				const int vertex_count = model->Get_Vertex_Count();
				const Vector3 * positions = model->Get_Vertex_Array();
				const uint16 * bones = model->Get_Vertex_Bone_Links();
				_ShadowSkinVertices.resize(vertex_count);
				for (int vertex = 0; vertex < vertex_count; ++vertex) {
					_ShadowSkinVertices[vertex].Position[0] = positions[vertex].X;
					_ShadowSkinVertices[vertex].Position[1] = positions[vertex].Y;
					_ShadowSkinVertices[vertex].Position[2] = positions[vertex].Z;
					_ShadowSkinVertices[vertex].Bone = bones[vertex];
				}
				unsigned first_vertex = 0;
				uploaded = Direct3D11_Upload_Shadow_Skin(&_ShadowSkinVertices[0], (unsigned)vertex_count, first_vertex);
				model->ShadowSkinSourceOffset = uploaded ? (int)first_vertex : -1;
				model->ShadowSkinSourceGeneration = Direct3D11_Shadow_Skin_Generation();
				placed = model->ShadowSkinSourceGeneration == generation;
			}
			group.BaseVertex = (unsigned)model->ShadowSkinSourceOffset;
		}
	}

	// The instances one group after another, a counting sort by group.
	const unsigned count = (unsigned)_ShadowCasterInstances.size();
	unsigned next = 0;
	for (size_t index = 0; index < _ShadowCasterGroups.size(); ++index) {
		ShadowCasterGroup & group = _ShadowCasterGroups[index];
		group.FirstInstance = next;
		next += group.InstanceCount;
		group.InstanceCount = 0;
	}
	_ShadowCasterOrdered.resize(count);
	_ShadowCasterWorlds.resize(count);
	_ShadowCasterFirstBones.resize(count);
	for (unsigned index = 0; index < count; ++index) {
		const ShadowCasterInstance & instance = _ShadowCasterInstances[index];
		ShadowCasterGroup & group = _ShadowCasterGroups[instance.Group];
		const unsigned slot = group.FirstInstance + group.InstanceCount++;
		_ShadowCasterOrdered[slot] = instance.Mesh;
		_ShadowCasterWorlds[slot] = instance.World;
		_ShadowCasterFirstBones[slot] = instance.FirstBone;
	}

	/* A run a piece of a model over all its instances, ordered by the texture an alpha tested one
		 tests against (none first), then by kind, cull, depth test and vertex buffer so the binds and
		 states change least. */
	const unsigned long cull = ShaderClass::Is_Backface_Culling_Inverted() ? D3DCULL_CCW : D3DCULL_CW;
	_ShadowCasterRunEntries.clear();
	for (size_t index = 0; index < _ShadowCasterGroups.size(); ++index) {
		const ShadowCasterGroup & group = _ShadowCasterGroups[index];
		for (unsigned piece_index = group.FirstPiece; piece_index < group.FirstPiece + group.PieceCount; ++piece_index) {
			const ShadowCasterPiece & piece = _ShadowCasterPieces[piece_index];
			ShadowCasterRunEntry entry;
			entry.Texture = piece.Texture;
			entry.Piece = piece_index;
			DX11ShadowCasterRun & run = entry.Run;
			run.Kind = piece.Kind;
			run.Vertices = piece.Vertices;
			run.Stride = piece.Stride;
			run.TextureOffset = piece.TextureOffset;
			run.Indices = piece.Indices;
			run.StartIndex = piece.StartIndex;
			run.IndexCount = piece.IndexCount;
			run.BaseVertex = group.BaseVertex;
			run.Strip = piece.Strip;
			run.CullMode = piece.Culled ? cull : D3DCULL_NONE;
			run.DepthFunction = piece.DepthFunction;
			run.FirstInstance = group.FirstInstance;
			run.InstanceCount = group.InstanceCount;
			_ShadowCasterRunEntries.push_back(entry);
		}
	}
	std::sort(_ShadowCasterRunEntries.begin(), _ShadowCasterRunEntries.end(),
		[](const ShadowCasterRunEntry & left, const ShadowCasterRunEntry & right) {
			return std::make_tuple(left.Texture, (int)left.Run.Kind, left.Run.CullMode, left.Run.DepthFunction, left.Run.Vertices)
				< std::make_tuple(right.Texture, (int)right.Run.Kind, right.Run.CullMode, right.Run.DepthFunction, right.Run.Vertices);
		});
	_ShadowCasterRuns.resize(_ShadowCasterRunEntries.size());
	_ShadowCasterRunPieces.resize(_ShadowCasterRunEntries.size());
	for (size_t index = 0; index < _ShadowCasterRunEntries.size(); ++index) {
		_ShadowCasterRuns[index] = _ShadowCasterRunEntries[index].Run;
		_ShadowCasterRunPieces[index] = _ShadowCasterRunEntries[index].Piece;
	}

	if (uploaded && Direct3D11_Upload_Shadow_Casters(&_ShadowCasterWorlds[0], &_ShadowCasterFirstBones[0], count,
			_ShadowCasterBones.empty() ? NULL : &_ShadowCasterBones[0], (unsigned)_ShadowCasterBones.size())) {
		// A texture's runs at a time, the opaque and skinned ones (no texture) first: an alpha tested
		// run's texture goes on stage 0 through the wrapper, as the texture category's walk sets it,
		// so the backend samples it with the filter and addressing that walk would have given it.
		size_t begin = 0;
		while (begin < _ShadowCasterRuns.size()) {
			TextureClass * texture = _ShadowCasterRunEntries[begin].Texture;
			size_t end = begin + 1;
			while (end < _ShadowCasterRuns.size() && _ShadowCasterRunEntries[end].Texture == texture) {
				++end;
			}
			if (texture != NULL) {
				DX8Wrapper::Set_Texture(0, texture);
				DX8Wrapper::Apply_Render_State_Changes();
			}
			if (Direct3D11_Draw_Shadow_Casters(&_ShadowCasterRuns[begin], (unsigned)(end - begin))) {
				for (size_t run = begin; run < end; ++run) {
					const DX11ShadowCasterRun & drawn = _ShadowCasterRuns[run];
					++Shadow_Lean_Draws;
					Shadow_Lean_Instances += (int)drawn.InstanceCount;
					Shadow_Lean_Alpha_Tested += (drawn.Kind == DX11_SHADOW_CASTER_ALPHA_TESTED) ? (int)drawn.InstanceCount : 0;
					Shadow_Lean_Skinned += (drawn.Kind == DX11_SHADOW_CASTER_SKINNED) ? (int)drawn.InstanceCount : 0;
				}
			} else {
				// No texture to test against on stage 0: those pieces go to their texture category.
				for (size_t run = begin; run < end; ++run) {
					const DX11ShadowCasterRun & refused = _ShadowCasterRuns[run];
					DX8PolygonRendererClass * renderer = _ShadowCasterPieces[_ShadowCasterRunPieces[run]].Renderer;
					for (unsigned instance = 0; instance < refused.InstanceCount; ++instance) {
						renderer->Get_Texture_Category()->Add_Render_Task(renderer,
							_ShadowCasterOrdered[refused.FirstInstance + instance]);
						++Shadow_Old_Meshes[SHADOW_OLD_REFUSED];
					}
				}
			}
			begin = end;
		}
	} else {
		// The backend would not draw them (a program or a buffer refused): every piece of every mesh
		// taken goes to its texture category, and a skin to its container's fill, as it would have
		// without this.
		for (size_t index = 0; index < _ShadowCasterInstances.size(); ++index) {
			MeshClass * mesh = _ShadowCasterInstances[index].Mesh;
			MeshModelClass * model = mesh->Peek_Model();
			DX8PolygonRendererListIterator it(&(model->PolygonRendererList));
			for (; !it.Is_Done(); it.Next()) {
				it.Peek_Obj()->Get_Texture_Category()->Add_Render_Task(it.Peek_Obj(), mesh);
			}
			if (model->Get_Flag(MeshGeometryClass::SKIN)) {
				DX8FVFCategoryContainer * container = model->PolygonRendererList.Peek_Head()->Get_Texture_Category()->Get_Container();
				static_cast<DX8SkinFVFCategoryContainer *>(container)->Add_Visible_Skin(mesh);
			}
			++Shadow_Old_Meshes[SHADOW_OLD_REFUSED];
		}
	}

	for (size_t index = 0; index < _ShadowCasterInstances.size(); ++index) {
		_ShadowCasterInstances[index].Mesh->Release_Ref();
	}
	Clear_Shadow_Casters();
}

void DX8MeshRendererClass::Flush(void)
{
	int i;

	WWPROFILE("DX8MeshRenderer::Flush");
	if (!camera) return;
	Log_Statistics_String(true);	

	/*
	** Render the FVF categories.  Note that it is critical that skins be 
	** rendered *after* the rigid meshes.  This is caused by the fact that an object may
	** have its base passes disabled and a translucent procedural material pass rendered
	** instead.  In this case, technically we have to delay rendering of the material pass but
	** for skins we just render these passes as we go because we can assume that the
	** bulk of the meshes have already been drawn (there would be extra overhead involved
	** in solving this for skins)
	*/
	for (i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Render_FVF_Category_Container_List(*texture_category_container_lists_rigid[i]);
	}

	Render_FVF_Category_Container_List(*texture_category_container_list_skin);

	Render_Decal_Meshes();

	/*
	** Render the translucent procedural material passes that were applied to meshes that
	** had their base passes disabled. 
	*/
	for (i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Render_FVF_Category_Container_List_Delayed_Passes(*texture_category_container_lists_rigid[i]);
	}

	DX8Wrapper::Set_Vertex_Buffer(NULL);
	DX8Wrapper::Set_Index_Buffer(NULL,0);
}


void DX8MeshRendererClass::Add_To_Render_List(DecalMeshClass * decalmesh)
{
	WWASSERT(decalmesh != NULL);
	decalmesh->Set_Next_Visible(visible_decal_meshes);
	visible_decal_meshes = decalmesh;
}

void DX8MeshRendererClass::Render_Decal_Meshes(void)
{
	DecalMeshClass * decal_mesh = visible_decal_meshes;
	if (!decal_mesh) return;

	DX8Wrapper::Set_DX8_ZBias(8);
	
	while (decal_mesh != NULL) {
		decal_mesh->Render();
		decal_mesh = decal_mesh->Peek_Next_Visible();
	}
	visible_decal_meshes = NULL;

	DX8Wrapper::Set_DX8_ZBias(0);
}

// ----------------------------------------------------------------------------

static void Log_Container_List(FVFCategoryList& container_list,bool only_visible)
{
	FVFCategoryListIterator it(&container_list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Log(only_visible);
		it.Next();
	}
}

void DX8MeshRendererClass::Log_Statistics_String(bool only_visible)
{
	if (statistics_requested!=WW3D::Get_Frame_Count()) return;

	for (int i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Log_Container_List(*texture_category_container_lists_rigid[i],only_visible);
	}
	Log_Container_List(*texture_category_container_list_skin,only_visible);

}

static void Invalidate_FVF_Category_Container_List(FVFCategoryList& list)
{
	while (DX8FVFCategoryContainer* fvf_category=list.Remove_Head()) {
		delete fvf_category;
	}
}

void DX8MeshRendererClass::Invalidate( bool shutdown)
{
	WWMEMLOG(MEM_RENDERER);
	_RegisteredMeshList.Reset_List();

	for (int i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Invalidate_FVF_Category_Container_List(*texture_category_container_lists_rigid[i]);
		delete texture_category_container_lists_rigid[i];
	}
	if (texture_category_container_list_skin) {
		Invalidate_FVF_Category_Container_List(*texture_category_container_list_skin);
		delete texture_category_container_list_skin;
		texture_category_container_list_skin=NULL;
	}

	if (!shutdown)
		texture_category_container_list_skin = W3DNEW FVFCategoryList;

	texture_category_container_lists_rigid.Delete_All();
}







