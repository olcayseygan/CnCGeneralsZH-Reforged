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
 *                     $Archive:: /Commando/Code/ww3d2/sortingrenderer.cpp                    $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "Platform/SunShadow.h"
#include "sortingrenderer.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "vertmaterial.h"
#include "texture.h"
#include <d3d9.h>
#include "d3dx9math.h"
#include "statistics.h"
#include <wwprofile.h>
#include <algorithm>
#include "Lib/Clock.h"		// Clock_Ticks: QueryPerformanceCounter on Windows, a monotonic clock elsewhere
#include "dx11runtime.h"

#ifdef _INTERNAL
// for occasional debugging...
// #pragma optimize("", off)
// #pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

bool SortingRendererClass::_EnableTriangleDraw=true;
static unsigned DEFAULT_SORTING_POLY_COUNT = 16384;	// (count * 3) must be less than 65536
static unsigned DEFAULT_SORTING_VERTEX_COUNT = 32768;	// count must be less than 65536

void SortingRendererClass::SetMinVertexBufferSize( unsigned val )
{
	DEFAULT_SORTING_VERTEX_COUNT = val;
	DEFAULT_SORTING_POLY_COUNT = val/2;	//typically have 2:1 vertex:triangle ratio.
}

struct ShortVectorIStruct
{
	unsigned short i;
	unsigned short j;
	unsigned short k;
};

struct TempIndexStruct
{
	ShortVectorIStruct tri;
	unsigned short idx;
	float z;
};

// ----------------------------------------------------------------------------
// The pool's entries in depth order, nearest last, as three passes of eleven bits over each depth's
// sort key.  The quicksort that was here was a fifth of a hundred-thousand-particle frame on its
// own, two hundred thousand triangles compared several million times.  This touches each entry four
// times.  It is stable, so entries at exactly the same depth keep the order they were pooled in; the
// quicksort left those in whatever order partitioning did.  Returns whichever of the two arrays
// holds the result.

static const unsigned DEPTH_RADIX_BITS=11;
static const unsigned DEPTH_RADIX_BUCKETS=1<<DEPTH_RADIX_BITS;
static const unsigned DEPTH_RADIX_PASSES=3;

static TempIndexStruct* Sort_By_Depth(TempIndexStruct* items,TempIndexStruct* scratch,unsigned count)
{
	static unsigned histogram[DEPTH_RADIX_PASSES][DEPTH_RADIX_BUCKETS];
	memset(histogram,0,sizeof(histogram));
	for (unsigned i=0;i<count;++i) {
		const unsigned key=SortingRendererClass::_Depth_Sort_Key(items[i].z);
		for (unsigned pass=0;pass<DEPTH_RADIX_PASSES;++pass) {
			++histogram[pass][(key>>(pass*DEPTH_RADIX_BITS))&(DEPTH_RADIX_BUCKETS-1)];
		}
	}

	TempIndexStruct* from=items;
	TempIndexStruct* to=scratch;
	for (unsigned pass=0;pass<DEPTH_RADIX_PASSES;++pass) {
		unsigned* buckets=histogram[pass];
		unsigned offset=0;
		for (unsigned b=0;b<DEPTH_RADIX_BUCKETS;++b) {
			const unsigned in_bucket=buckets[b];
			buckets[b]=offset;
			offset+=in_bucket;
		}

		const unsigned shift=pass*DEPTH_RADIX_BITS;
		for (unsigned i=0;i<count;++i) {
			const unsigned key=SortingRendererClass::_Depth_Sort_Key(from[i].z);
			to[buckets[(key>>shift)&(DEPTH_RADIX_BUCKETS-1)]++]=from[i];
		}

		TempIndexStruct* swap=from;
		from=to;
		to=swap;
	}
	return from;
}

// ----------------------------------------------------------------------------

class SortingNodeStruct : public DLNodeClass<SortingNodeStruct>
{
	W3DMPO_GLUE(SortingNodeStruct)

public:
	RenderStateStruct sorting_state;
	bool excludeSunReceivers;
	SunShadow::CasterEffect casterEffect;

	SphereClass bounding_sphere;

	Vector3 transformed_center;
	unsigned short start_index;			// First index used in the ib
	unsigned short polygon_count;			// Polygon count to process (3 indices = one polygon)
	unsigned short min_vertex_index;		// First index used in the vb
	unsigned short vertex_count;			// Number of vertices used in vb
	bool quads;									// four vertices a quad, in order from min_vertex_index; the ib is not read
	bool glow;									// the normals hold a fire's glow (Insert_Quads)
	bool fire;									// a fire system's quads, age and seed in v2 (Insert_Quads)
	bool emissive;							// an Emissive system's quads: no shadow, no smoke shade (Insert_Quads)
	bool hard_edge;							// quads with no soft particle fade, a mine's flare (Insert_Quads)
	// The object's Direct3D 11 look (RTS3DScene::renderOneObject) as the mesh renderer had it set
	// when a sorted mesh queued its triangles; its draws come later, after the look is put back.
	unsigned draw_effect;
	float draw_effect_parameters[4];
};

// Whether two nodes are drawn in the same look; the numbers differ per object.
static bool Same_Draw_Effect(const SortingNodeStruct* a,const SortingNodeStruct* b)
{
	return a->draw_effect==b->draw_effect
		&& (a->draw_effect==0 || memcmp(a->draw_effect_parameters,b->draw_effect_parameters,sizeof(a->draw_effect_parameters))==0);
}

static void No_Draw_Effect(SortingNodeStruct* state)
{
	state->draw_effect=0;
	state->draw_effect_parameters[0]=state->draw_effect_parameters[1]=0.0f;
	state->draw_effect_parameters[2]=state->draw_effect_parameters[3]=0.0f;
}

static DLListClass<SortingNodeStruct> sorted_list;
// Nodes that arrive with no bounding sphere have nothing to sort by; they wait here and go into
// the sorted list in one piece at flush time, instead of each walking it looking for a place.
static DLListClass<SortingNodeStruct> unsorted_list;
static DLListClass<SortingNodeStruct> clean_list;
static unsigned total_sorting_vertices;

static SortingNodeStruct* Get_Sorting_Struct()
{

	SortingNodeStruct* state=clean_list.Head();
	if (state) {
		state->Remove();
		return state;
	}
	state=W3DNEW SortingNodeStruct();
	return state;
}

// ----------------------------------------------------------------------------
//
// Temporary arrays for the sorting system
//
// ----------------------------------------------------------------------------

static TempIndexStruct* temp_index_array;
static TempIndexStruct* temp_sort_scratch_array;	// the radix sort's second buffer, always the same size
static unsigned temp_index_array_count;

static TempIndexStruct* Get_Temp_Index_Array(unsigned count)
{
	if (count < DEFAULT_SORTING_POLY_COUNT)
		count = DEFAULT_SORTING_POLY_COUNT;
	if (count>temp_index_array_count) {
		delete[] temp_index_array;
		delete[] temp_sort_scratch_array;
		temp_index_array=W3DNEWARRAY TempIndexStruct[count];
		temp_sort_scratch_array=W3DNEWARRAY TempIndexStruct[count];
		temp_index_array_count=count;
	}
	return temp_index_array;
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	const SphereClass& bounding_sphere,
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	SNAPSHOT_SAY(("SortingRenderer::Insert(start_i: %d, polygons : %d, min_vi: %d, vertex_count: %d)\n",
		start_index,polygon_count,min_vertex_index,vertex_count));


	DX8_RECORD_SORTING_RENDER(polygon_count,vertex_count);

	SortingNodeStruct* state=Get_Sorting_Struct();

	DX8Wrapper::Get_Render_State(state->sorting_state);
	state->excludeSunReceivers = SunShadow::ReceiversExcluded(DX8Wrapper::_Get_D3D_Device());
	state->casterEffect = SunShadow::GetCasterEffect(DX8Wrapper::_Get_D3D_Device());

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));


	state->bounding_sphere=bounding_sphere;
	state->start_index=start_index;
	state->polygon_count=polygon_count;
	state->min_vertex_index=min_vertex_index;
	state->vertex_count=vertex_count;
	state->quads=false;
	state->glow=false;
	state->fire=false;
	state->emissive=false;
	state->hard_edge=false;
	state->draw_effect=Direct3D11_Requested_Draw_Effect(state->draw_effect_parameters);

	SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	if (state->bounding_sphere.Radius <= 0.0f) {
		// Nothing to sort by, so do not pay the walk down the list looking for a place.
		state->transformed_center=Vector3(0.0f,0.0f,0.0f);
		unsorted_list.Add_Tail(state);
	}
	else {
	D3DXMATRIX mtx=(D3DXMATRIX&)state->sorting_state.world*(D3DXMATRIX&)state->sorting_state.view;
	D3DXVECTOR3 vec=(D3DXVECTOR3&)state->bounding_sphere.Center;
	D3DXVECTOR4 transformed_vec;
	D3DXVec3Transform(
		&transformed_vec,
		&vec,
		&mtx);
	state->transformed_center=Vector3(transformed_vec[0],transformed_vec[1],transformed_vec[2]);


	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	SortingNodeStruct* node=sorted_list.Head();
	while (node) {
		if (state->transformed_center.Z>node->transformed_center.Z) {
			if (sorted_list.Head()==sorted_list.Tail())
				sorted_list.Add_Head(state);
			else
				state->Insert_Before(node);
			break;
		}
		node=node->Succ();
	}
	if (!node) sorted_list.Add_Tail(state);
	}

#ifdef WWDEBUG
	unsigned short* indices=NULL;
	SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
	WWASSERT(index_buffer);
	indices=index_buffer->index_buffer;
	WWASSERT(indices);
	indices+=state->start_index;
	indices+=state->sorting_state.iba_offset;

	for (int i=0;i<state->polygon_count;++i) {
		unsigned short idx1=indices[i*3]-state->min_vertex_index;
		unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
		unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
		WWASSERT(idx1<state->vertex_count);
		WWASSERT(idx2<state->vertex_count);
		WWASSERT(idx3<state->vertex_count);
	}
#endif // WWDEBUG
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system, with no bounding information.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	SphereClass sphere(Vector3(0.0f,0.0f,0.0f),0.0f);
	Insert_Triangles(sphere,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
// Insert quads laid out four vertices each, in order, from min_vertex_index in the vertex buffer
// that is set.  The pool sorts and writes a quad as one piece: a billboard's four corners share one
// depth, so its two triangles always had the same key, and a quad is four vertices and six indices
// where two loose triangles were six and six.  No bounding information, like the call above.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Quads(
	unsigned short quad_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count,
	bool glow,
	bool fire,
	bool emissive,
	bool hard_edge)
{
	const unsigned short polygon_count=quad_count*2;
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(0,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	DX8_RECORD_SORTING_RENDER(polygon_count,vertex_count);

	SortingNodeStruct* state=Get_Sorting_Struct();
	DX8Wrapper::Get_Render_State(state->sorting_state);
	state->excludeSunReceivers = SunShadow::ReceiversExcluded(DX8Wrapper::_Get_D3D_Device());
	state->casterEffect = SunShadow::GetCasterEffect(DX8Wrapper::_Get_D3D_Device());

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));
	WWASSERT(vertex_count==quad_count*4);

	state->bounding_sphere=SphereClass(Vector3(0.0f,0.0f,0.0f),0.0f);
	state->start_index=0;
	state->polygon_count=polygon_count;
	state->min_vertex_index=min_vertex_index;
	state->vertex_count=vertex_count;
	state->quads=true;
	state->glow=glow;
	state->fire=fire;
	state->emissive=emissive;
	state->hard_edge=hard_edge;
	No_Draw_Effect(state);
	state->transformed_center=Vector3(0.0f,0.0f,0.0f);
	unsorted_list.Add_Tail(state);
}

// ----------------------------------------------------------------------------
//
// Flush all sorting polygons.
//
// ----------------------------------------------------------------------------

void Release_Refs(SortingNodeStruct* state)
{
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(state->sorting_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(state->sorting_state.index_buffer);
	REF_PTR_RELEASE(state->sorting_state.material);
	for (i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i) 
	{
		REF_PTR_RELEASE(state->sorting_state.Textures[i]);
	}
}

static unsigned overlapping_node_count;
static unsigned overlapping_entry_count;	// sort entries: a triangle each, or a quad each for a quad node
static const unsigned MAX_OVERLAPPING_NODES=4096;
// One batch of the flush: a 16 bit index names 65535 vertices, and the index buffer holds 65535.
static const unsigned MAX_BATCH_VERTICES=65535;
static const unsigned MAX_BATCH_INDICES=65535;
// A sort entry whose third index is this is a quad; its first index is the quad's first vertex.
static const unsigned short QUAD_ENTRY=0xFFFF;
static SortingNodeStruct* overlapping_nodes[MAX_OVERLAPPING_NODES];
// Where each pooled node's first vertex sits, worked out once in the sort pass for the copy pass.
static VertexFormatXYZNDUV2* overlapping_node_vertices[MAX_OVERLAPPING_NODES];
// Where each pooled node's sort entries begin, worked out in order before the nodes fill them in parallel.
static unsigned overlapping_node_entry_offsets[MAX_OVERLAPPING_NODES];
static unsigned refused_polygon_count;

unsigned SortingRendererClass::Get_Refused_Polygon_Count()
{
	return refused_polygon_count;
}

static unsigned flush_profile_entries;
static float flush_profile_sort_ms;
static float flush_profile_copy_ms;
static float flush_profile_draw_ms;

static float flushProfileElapsedMS(long long from, long long to)
{
	static long long freq = 0;
	if (freq == 0)
		freq = Clock_Ticks_Per_Second();
	if (freq < 1)
		return 0.0f;
	return (float)((double)(to - from) * 1000.0 / (double)freq);
}

void SortingRendererClass::Reset_Flush_Profile()
{
	flush_profile_entries = 0;
	flush_profile_sort_ms = 0.0f;
	flush_profile_copy_ms = 0.0f;
	flush_profile_draw_ms = 0.0f;
}

void SortingRendererClass::Get_Flush_Profile(unsigned &entries, float &sort_ms, float &copy_ms, float &draw_ms)
{
	entries = flush_profile_entries;
	sort_ms = flush_profile_sort_ms;
	copy_ms = flush_profile_copy_ms;
	draw_ms = flush_profile_draw_ms;
}

static SortingRendererClass::ParallelForFunc parallel_for_hook=NULL;

void SortingRendererClass::Set_Parallel_For(ParallelForFunc parallel_for)
{
	parallel_for_hook=parallel_for;
}

static void Run_Parallel(int count,int granularity,void (*work)(int,void*),void* context)
{
	if (count<=0) return;
	if (parallel_for_hook) {
		parallel_for_hook(count,granularity,work,context);
		return;
	}
	for (int i=0;i<count;++i) {
		work(i,context);
	}
}

void SortingRendererClass::Parallel_For(int count,int granularity,void (*work)(int,void*),void* context)
{
	Run_Parallel(count,granularity,work,context);
}

// nodes a thread claims at once in the depth pass; a particle node is a few hundred quads
static const int NODES_PER_CLAIM=4;
// entries one job copies into a batch at once
static const unsigned ENTRIES_PER_COPY_CHUNK=2048;

// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_To_Sorting_Pool(SortingNodeStruct* state)
{
	if (overlapping_node_count>=MAX_OVERLAPPING_NODES) {
		refused_polygon_count+=state->polygon_count;
		Release_Refs(state);
		WWASSERT(0);
		return;
	}

	overlapping_nodes[overlapping_node_count]=state;
	overlapping_entry_count+=state->quads ? state->polygon_count/2 : state->polygon_count;
	overlapping_node_count++;
}

// ----------------------------------------------------------------------------
//static unsigned prevLight = 0xffffffff;

static void Apply_Render_State(RenderStateStruct& render_state)
{



	DX8Wrapper::Set_Shader(render_state.shader);

	DX8Wrapper::Set_Material(render_state.material);

	for (int i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i) 
	{
		DX8Wrapper::Set_Texture(i,render_state.Textures[i]);
	}

	DX8Wrapper::_Set_DX8_Transform(D3DTS_WORLD,render_state.world);
	DX8Wrapper::_Set_DX8_Transform(D3DTS_VIEW,render_state.view);



  if (!render_state.material->Get_Lighting())
    return;
  //prevLight = render_state.lightsHash;

	if (render_state.LightEnable[0]) 
  {
    
    DX8Wrapper::Set_DX8_Light(0,&render_state.Lights[0]);
		if (render_state.LightEnable[1]) 
    {
			DX8Wrapper::Set_DX8_Light(1,&render_state.Lights[1]);
			if (render_state.LightEnable[2]) 
      {
				DX8Wrapper::Set_DX8_Light(2,&render_state.Lights[2]);
				if (render_state.LightEnable[3]) 
					DX8Wrapper::Set_DX8_Light(3,&render_state.Lights[3]);
				else 
					DX8Wrapper::Set_DX8_Light(3,NULL);
			}
			else 
				DX8Wrapper::Set_DX8_Light(2,NULL);
		}
		else 
			DX8Wrapper::Set_DX8_Light(1,NULL);
	}
	else 
		DX8Wrapper::Set_DX8_Light(0,NULL);


}

// ----------------------------------------------------------------------------
// Would Apply_Render_State leave the device exactly as it is?  Byte compares on the lights and the
// matrices can only say no to two states that are really the same, which costs a draw and nothing
// else.

static bool Same_Draw_State(const RenderStateStruct& a, const RenderStateStruct& b)
{
	if (a.shader.Get_Bits()!=b.shader.Get_Bits() || a.material!=b.material) return false;
	for (int i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i) {
		if (a.Textures[i]!=b.Textures[i]) return false;
	}
	if (memcmp(&a.world,&b.world,sizeof(a.world))!=0 || memcmp(&a.view,&b.view,sizeof(a.view))!=0) return false;
	if (a.material && a.material->Get_Lighting()) {
		if (memcmp(a.LightEnable,b.LightEnable,sizeof(a.LightEnable))!=0) return false;
		if (memcmp(a.Lights,b.Lights,sizeof(a.Lights))!=0) return false;
	}
	return true;
}

// Whether two nodes draw in one run: the device state above and every look the flush sets per run.
// A billboard run fades into the depth behind it and a mesh run does not, so the two never share one.
static bool Same_Run_State(const SortingNodeStruct* a,const SortingNodeStruct* b)
{
	return a->glow==b->glow && a->fire==b->fire && a->emissive==b->emissive && a->quads==b->quads
		&& Same_Draw_Effect(a,b) && a->excludeSunReceivers==b->excludeSunReceivers
		&& a->casterEffect==b->casterEffect && Same_Draw_State(a->sorting_state,b->sorting_state);
}

// ----------------------------------------------------------------------------
// Each pooled node's run state, named by the first node in pool order that has it, so a run break
// is one compare and not Same_Run_State's matrices and lights.  The hash covers only fields that
// Same_Run_State compares byte for byte, so equal states always hash alike.

static unsigned short overlapping_node_class[MAX_OVERLAPPING_NODES];
static const unsigned CLASS_TABLE_SIZE=2*MAX_OVERLAPPING_NODES;
static unsigned short class_table[CLASS_TABLE_SIZE];	// a class's node plus one; nought is empty
static unsigned class_table_hash[CLASS_TABLE_SIZE];

static unsigned Hash_Words(unsigned hash,const void* bytes,unsigned byte_count)
{
	const unsigned char* at=(const unsigned char*)bytes;
	for (unsigned offset=0;offset+sizeof(unsigned)<=byte_count;offset+=sizeof(unsigned)) {
		unsigned word;
		memcpy(&word,at+offset,sizeof(word));
		hash=(hash^word)*16777619u;
	}
	return hash;
}

static unsigned Run_State_Hash(const SortingNodeStruct* node,int textures)
{
	const RenderStateStruct& state=node->sorting_state;
	const unsigned fields[3]={
		state.shader.Get_Bits(),
		node->draw_effect,
		(node->glow?1u:0u)|(node->fire?2u:0u)|(node->emissive?4u:0u)|(node->quads?8u:0u)|(node->excludeSunReceivers?16u:0u) };
	unsigned hash=Hash_Words(2166136261u,fields,sizeof(fields));
	hash=Hash_Words(hash,&state.material,sizeof(state.material));
	hash=Hash_Words(hash,state.Textures,sizeof(state.Textures[0])*textures);
	hash=Hash_Words(hash,&state.world,sizeof(state.world));
	return Hash_Words(hash,&state.view,sizeof(state.view));
}

// A node nothing may pass, nor it anything: a look that reads the scene's copy, a sun caster
// effect, or a texture in the default pool, which is where every render target lives and so where a
// draw sampling the picture under it would find it.
static bool Run_Stays_In_Order(const SortingNodeStruct* node,int textures)
{
	if (node->draw_effect!=0 || node->casterEffect.Effect!=0) return true;
	for (int i=0;i<textures;++i) {
		const TextureBaseClass* texture=node->sorting_state.Textures[i];
		if (texture && texture->Get_Pool()==TextureBaseClass::POOL_DEFAULT) return true;
	}
	return false;
}

static void Classify_Nodes(unsigned node_count,unsigned short* states)
{
	const int textures=DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();
	memset(class_table,0,sizeof(class_table));
	for (unsigned node=0;node<node_count;++node) {
		const SortingNodeStruct* state=overlapping_nodes[node];
		const unsigned hash=Run_State_Hash(state,textures);
		unsigned slot=hash&(CLASS_TABLE_SIZE-1);
		while (class_table[slot]!=0
			&& (class_table_hash[slot]!=hash || !Same_Run_State(overlapping_nodes[class_table[slot]-1],state))) {
			slot=(slot+1)&(CLASS_TABLE_SIZE-1);
		}
		if (class_table[slot]==0) {
			class_table[slot]=(unsigned short)(node+1);
			class_table_hash[slot]=hash;
		}
		overlapping_node_class[node]=(unsigned short)(class_table[slot]-1);
		states[node]=overlapping_node_class[node]
			| (Run_Stays_In_Order(state,textures) ? (unsigned short)SortingRendererClass::SORT_STATE_FIXED : 0);
	}
}

// ----------------------------------------------------------------------------

static inline bool Entry_Is_Quad(const TempIndexStruct& entry)
{
	return entry.tri.k==QUAD_ENTRY;
}

static inline unsigned Entry_Vertex_Count(const TempIndexStruct& entry)
{
	return Entry_Is_Quad(entry) ? 4 : 3;
}

static inline unsigned Entry_Index_Count(const TempIndexStruct& entry)
{
	return Entry_Is_Quad(entry) ? 6 : 3;
}

// Where each entry of a batch writes its vertices and indices.  Worked out in order on the render
// thread so the copy can be split across threads; the array only grows, and only there.
struct BatchEntryOffset
{
	unsigned vertex;
	unsigned index;
};

static BatchEntryOffset* batch_offsets;
static unsigned batch_offsets_count;

static const BatchEntryOffset* Compute_Batch_Offsets(const TempIndexStruct* entries,unsigned entry_count)
{
	if (entry_count>batch_offsets_count) {
		delete[] batch_offsets;
		batch_offsets=W3DNEWARRAY BatchEntryOffset[entry_count];
		batch_offsets_count=entry_count;
	}
	unsigned vertex=0;
	unsigned index=0;
	for (unsigned e=0;e<entry_count;++e) {
		batch_offsets[e].vertex=vertex;
		batch_offsets[e].index=index;
		vertex+=Entry_Vertex_Count(entries[e]);
		index+=Entry_Index_Count(entries[e]);
	}
	return batch_offsets;
}

struct BatchCopyJob
{
	const TempIndexStruct* entries;
	const BatchEntryOffset* offsets;
	unsigned entry_count;
	VertexFormatXYZNDUV2* vertices;
	unsigned short* indices;
};

static void Copy_Batch_Vertices(int chunk,void* context)
{
	const BatchCopyJob* job=(const BatchCopyJob*)context;
	const unsigned first=(unsigned)chunk*ENTRIES_PER_COPY_CHUNK;
	const unsigned last=MIN(first+ENTRIES_PER_COPY_CHUNK,job->entry_count);
	for (unsigned e=first;e<last;++e) {
		const TempIndexStruct& entry=job->entries[e];
		const VertexFormatXYZNDUV2* src_verts=overlapping_node_vertices[entry.idx];
		VertexFormatXYZNDUV2* dest_verts=job->vertices+job->offsets[e].vertex;
		if (Entry_Is_Quad(entry)) {
			const VertexFormatXYZNDUV2* quad=src_verts+entry.tri.i;
			memcpy(dest_verts,quad,sizeof(VertexFormatXYZNDUV2)*4);
			// The billboard's half width, the longest its soft fade may take (SOFT_PARTICLE_SAMPLING):
			// the first two corners are one side apart.  A billboard reads one coordinate set.
			const float dx=quad[1].x-quad[0].x;
			const float dy=quad[1].y-quad[0].y;
			const float dz=quad[1].z-quad[0].z;
			// A hard edged node's quads (a mine's flare on the ground) fade over a thousandth of a
			// unit, which is the plain depth test -d3d9 draws them with.
			const float half_width=overlapping_nodes[entry.idx]->hard_edge
				? 0.001f : 0.5f*sqrtf(dx*dx+dy*dy+dz*dz);
			dest_verts[0].u2=half_width;
			dest_verts[1].u2=half_width;
			dest_verts[2].u2=half_width;
			dest_verts[3].u2=half_width;
		}
		else {
			dest_verts[0]=src_verts[entry.tri.i];
			dest_verts[1]=src_verts[entry.tri.j];
			dest_verts[2]=src_verts[entry.tri.k];
		}
	}
}

static void Write_Batch_Indices(int chunk,void* context)
{
	const BatchCopyJob* job=(const BatchCopyJob*)context;
	const unsigned first=(unsigned)chunk*ENTRIES_PER_COPY_CHUNK;
	const unsigned last=MIN(first+ENTRIES_PER_COPY_CHUNK,job->entry_count);
	for (unsigned e=first;e<last;++e) {
		const unsigned short vertex=(unsigned short)job->offsets[e].vertex;
		unsigned short* index_array=job->indices+job->offsets[e].index;
		if (Entry_Is_Quad(job->entries[e])) {
			index_array[0]=vertex;
			index_array[1]=vertex+1;
			index_array[2]=vertex+2;
			index_array[3]=vertex+2;
			index_array[4]=vertex+3;
			index_array[5]=vertex;
		}
		else {
			index_array[0]=vertex;
			index_array[1]=vertex+1;
			index_array[2]=vertex+2;
		}
	}
}

// ----------------------------------------------------------------------------
// Runs.  Nearly every sorted entry was a draw of its own in a big battle: two fires a screen apart
// burn at the same depths, the sort interleaves their quads, and the state flipped at every one.
// Blending orders only the draws that land on one pixel, so an entry may move back past entries
// that share no pixel with it and join the last run of its own state; every pixel still blends the
// same draws in the same order, so the picture is the same to the bit.  Where an entry can reach is
// worked out on SORT_TILE_GRID by SORT_TILE_GRID tiles of the viewport: an entry's tiles hold every
// pixel it can touch, found from its corners through the projection with a margin, and anything
// whose corners cannot be put on the screen that way covers all of it.

static const unsigned TILE_GRID=SortingRendererClass::SORT_TILE_GRID;
// How many runs back an entry looks for its own state.
static const unsigned RUN_LOOK_BACK=64;
// Past the rasterizer's snapping (a 256th of a pixel) and the CPU's rounding of the corners against
// the GPU's (a millionth of the width), in the viewport's own units, two across: 1.3 pixels at 1280.
static const float TILE_MARGIN=0.002f;
// A corner this close to the eye, or behind it, puts the entry on the whole screen.
static const float TILE_NEAREST_W=1.0e-3f;

// Each pooled node's world, view and projection in one, for its entries' tiles.
static D3DXMATRIX overlapping_node_clip[MAX_OVERLAPPING_NODES];
static D3DXMATRIX sort_projection;
static unsigned short overlapping_node_state[MAX_OVERLAPPING_NODES];

// The arrays only grow, and only on the render thread.
static unsigned short* entry_states;
static SortingRendererClass::SortTiles* entry_tiles;
static unsigned* entry_order;
static unsigned entry_arrays_count;
static unsigned* group_entry_run;
static unsigned* group_run_start;
static unsigned group_arrays_count;

static void Reserve_Entry_Arrays(unsigned count)
{
	if (count<=entry_arrays_count) return;
	delete[] entry_states;
	delete[] entry_tiles;
	delete[] entry_order;
	entry_states=W3DNEWARRAY unsigned short[count];
	entry_tiles=W3DNEWARRAY SortingRendererClass::SortTiles[count];
	entry_order=W3DNEWARRAY unsigned[count];
	entry_arrays_count=count;
}

static void Release_Entry_Arrays()
{
	delete[] entry_states;
	delete[] entry_tiles;
	delete[] entry_order;
	delete[] group_entry_run;
	delete[] group_run_start;
	entry_states=NULL;
	entry_tiles=NULL;
	entry_order=NULL;
	group_entry_run=NULL;
	group_run_start=NULL;
	entry_arrays_count=0;
	group_arrays_count=0;
}

// NaN goes to the screen's edge on either side, so a broken corner widens the range.
static inline unsigned char Low_Tile(float ndc)
{
	const float tile=(ndc-TILE_MARGIN+1.0f)*(0.5f*TILE_GRID);
	if (!(tile>0.0f)) return 0;
	return (tile<(float)(TILE_GRID-1)) ? (unsigned char)tile : (unsigned char)(TILE_GRID-1);
}

static inline unsigned char High_Tile(float ndc)
{
	const float tile=(ndc+TILE_MARGIN+1.0f)*(0.5f*TILE_GRID);
	if (!(tile<(float)(TILE_GRID-1))) return (unsigned char)(TILE_GRID-1);
	return (tile>0.0f) ? (unsigned char)tile : 0;
}

static SortingRendererClass::SortTiles Whole_Grid()
{
	SortingRendererClass::SortTiles tiles;
	tiles.x0=0;
	tiles.x1=(unsigned char)(TILE_GRID-1);
	tiles.y0=0;
	tiles.y1=(unsigned char)(TILE_GRID-1);
	return tiles;
}

// The corners go through the same three matrices the draw's vertex program multiplies them by.  With
// every corner in front of the eye a triangle's picture is the triangle of its corners' pictures, and
// clipping only takes from it.
SortingRendererClass::SortTiles SortingRendererClass::_Entry_Tiles(const float* const* corners,
	unsigned corner_count,const float* clip)
{
	float min_x=1.0e30f,max_x=-1.0e30f,min_y=1.0e30f,max_y=-1.0e30f;
	for (unsigned c=0;c<corner_count;++c) {
		const float x=corners[c][0],y=corners[c][1],z=corners[c][2];
		const float w=x*clip[3]+y*clip[7]+z*clip[11]+clip[15];
		if (!(w>TILE_NEAREST_W)) return Whole_Grid();
		const float screen_x=(x*clip[0]+y*clip[4]+z*clip[8]+clip[12])/w;
		const float screen_y=(x*clip[1]+y*clip[5]+z*clip[9]+clip[13])/w;
		if (!(screen_x==screen_x) || !(screen_y==screen_y)) return Whole_Grid();
		min_x=MIN(min_x,screen_x);
		max_x=MAX(max_x,screen_x);
		min_y=MIN(min_y,screen_y);
		max_y=MAX(max_y,screen_y);
	}
	SortTiles tiles;
	tiles.x0=Low_Tile(min_x);
	tiles.x1=High_Tile(max_x);
	tiles.y0=Low_Tile(min_y);
	tiles.y1=High_Tile(max_y);
	return tiles;
}

struct EntryTileJob
{
	const TempIndexStruct* entries;
	unsigned entry_count;
};

static void Find_Entry_Tiles(int chunk,void* context)
{
	const EntryTileJob* job=(const EntryTileJob*)context;
	const unsigned first=(unsigned)chunk*ENTRIES_PER_COPY_CHUNK;
	const unsigned last=MIN(first+ENTRIES_PER_COPY_CHUNK,job->entry_count);
	for (unsigned e=first;e<last;++e) {
		const TempIndexStruct& entry=job->entries[e];
		const VertexFormatXYZNDUV2* src_verts=overlapping_node_vertices[entry.idx];
		const float* corners[4];
		unsigned corner_count=3;
		if (Entry_Is_Quad(entry)) {
			for (unsigned c=0;c<4;++c) {
				corners[c]=&src_verts[entry.tri.i+c].x;
			}
			corner_count=4;
		}
		else {
			corners[0]=&src_verts[entry.tri.i].x;
			corners[1]=&src_verts[entry.tri.j].x;
			corners[2]=&src_verts[entry.tri.k].x;
		}
		entry_tiles[e]=SortingRendererClass::_Entry_Tiles(corners,corner_count,&overlapping_node_clip[entry.idx]._11);
		entry_states[e]=overlapping_node_state[entry.idx];
	}
}

struct OpenRun
{
	unsigned short state;
	unsigned long long rows[TILE_GRID];	// the tiles its entries cover, a bit a column
};

unsigned SortingRendererClass::_Group_Runs(const unsigned short* states,const SortTiles* tiles,
	unsigned count,unsigned* order)
{
	if (count>group_arrays_count) {
		delete[] group_entry_run;
		delete[] group_run_start;
		group_entry_run=W3DNEWARRAY unsigned[count];
		group_run_start=W3DNEWARRAY unsigned[count];
		group_arrays_count=count;
	}
	static OpenRun open_runs[RUN_LOOK_BACK];
	unsigned runs=0;
	for (unsigned e=0;e<count;++e) {
		const unsigned short state=states[e];
		const SortTiles& at=tiles[e];
		const unsigned long long columns=(2ull<<at.x1)-(1ull<<at.x0);
		unsigned run=runs;
		const unsigned oldest=(runs>RUN_LOOK_BACK) ? runs-RUN_LOOK_BACK : 0;
		for (unsigned r=runs;r-->oldest;) {
			const OpenRun& open=open_runs[r%RUN_LOOK_BACK];
			if (open.state==state) {
				run=r;
				break;
			}
			if ((state|open.state)&SORT_STATE_FIXED) break;
			bool shared=false;
			for (unsigned y=at.y0;y<=at.y1 && !shared;++y) {
				shared=(open.rows[y]&columns)!=0;
			}
			if (shared) break;
		}
		OpenRun& target=open_runs[run%RUN_LOOK_BACK];
		if (run==runs) {
			target.state=state;
			memset(target.rows,0,sizeof(target.rows));
			group_run_start[run]=0;
			++runs;
		}
		for (unsigned y=at.y0;y<=at.y1;++y) {
			target.rows[y]|=columns;
		}
		group_entry_run[e]=run;
		++group_run_start[run];
	}

	// Each run's entries in the order the sort gave them, the runs in the order they opened.
	unsigned start=0;
	for (unsigned r=0;r<runs;++r) {
		const unsigned entries_in_run=group_run_start[r];
		group_run_start[r]=start;
		start+=entries_in_run;
	}
	for (unsigned e=0;e<count;++e) {
		order[group_run_start[group_entry_run[e]]++]=e;
	}
	return runs;
}

// The run's state is any of its nodes': they are all one class.
static void Draw_Run(SortingNodeStruct* state,unsigned first_index,unsigned triangles,
	unsigned first_vertex,unsigned vertex_count)
{
	Apply_Render_State(state->sorting_state);
	SunShadow::SetSmokeGlow(DX8Wrapper::_Get_D3D_Device(),state->glow);
	Direct3D11_Set_Fire_Look(state->fire);
	Direct3D11_Set_Emissive(state->emissive);
	Direct3D11_Set_Soft_Particles(state->quads);
	Direct3D11_Set_Draw_Effect(state->draw_effect,state->draw_effect_parameters);
	SunShadow::ReceiverExclusion sunReceivers(DX8Wrapper::_Get_D3D_Device(), state->excludeSunReceivers);
	SunShadow::CasterEffectScope casterEffect(DX8Wrapper::_Get_D3D_Device(), state->casterEffect);
	DX8Wrapper::Draw_Triangles(first_index,triangles,first_vertex,vertex_count);
}

// Draw one batch of the entries, in the order _Group_Runs gave them.
//
// Each entry's vertices are copied out of its node on their own - three for a triangle, four for a
// quad - so a run of entries covers one contiguous stretch of the batch's vertices and indices.  A
// quad's indices are the pattern PointGroupClass's own quad index buffers use.
//
// A run carries on across nodes of one class and ends where the class changes.  Thirty inferno
// cannons each with its own fire system on the same texture, interleaved in depth, used to be a
// draw every few triangles; _Group_Runs has already moved each entry back past the entries in
// other tiles to the last run of its class, so every pixel blends the same draws in the depth
// sort's order and the picture does not change.
static void Flush_Sorting_Batch(const TempIndexStruct* entries,unsigned entry_count,unsigned vertex_count,unsigned index_count)
{
	BatchCopyJob job;
	job.entries=entries;
	job.offsets=Compute_Batch_Offsets(entries,entry_count);
	job.entry_count=entry_count;
	job.vertices=NULL;
	job.indices=NULL;
	const int chunks=(int)((entry_count+ENTRIES_PER_COPY_CHUNK-1)/ENTRIES_PER_COPY_CHUNK);

	long long tCopyStart, tCopyEnd, tDrawEnd;
	tCopyStart = Clock_Ticks();

	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,(unsigned short)vertex_count);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);

		// If you have a crash in here and "dest_verts" points to illegal memory area,
		// it is because D3D is in illegal state, and the only known cure is rebooting.
		// This illegal state is usually caused by Quake3-engine powered games such as MOHAA.
		job.vertices=(VertexFormatXYZNDUV2 *)lock.Get_Formatted_Vertex_Array();
		Run_Parallel(chunks,1,Copy_Batch_Vertices,&job);
	}

	DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DX8,(unsigned short)index_count);
	{
		DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
		job.indices=lock.Get_Index_Array();

		try {
		Run_Parallel(chunks,1,Write_Batch_Indices,&job);
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	DX8Wrapper::Set_Index_Buffer(dyn_ib_access,0); // Override with this buffer (do something to prevent need for this!)
	DX8Wrapper::Set_Vertex_Buffer(dyn_vb_access); // Override with this buffer (do something to prevent need for this!)

	tCopyEnd = Clock_Ticks();
	flush_profile_copy_ms += flushProfileElapsedMS(tCopyStart, tCopyEnd);

	DX8Wrapper::Apply_Render_State_Changes();

	unsigned run_first_index=0;
	unsigned run_first_vertex=0;
	unsigned run_triangles=0;
	unsigned index_cursor=0;
	unsigned vertex_cursor=0;
	unsigned run_class=overlapping_node_class[entries[0].idx];
	for (unsigned e=0;e<entry_count;++e) {
		const unsigned entry_class=overlapping_node_class[entries[e].idx];
		if (entry_class!=run_class) {
			Draw_Run(overlapping_nodes[run_class],run_first_index,run_triangles,run_first_vertex,vertex_cursor-run_first_vertex);
			run_first_index=index_cursor;
			run_first_vertex=vertex_cursor;
			run_triangles=0;
			run_class=entry_class;
		}
		run_triangles+=Entry_Is_Quad(entries[e]) ? 2 : 1;
		index_cursor+=Entry_Index_Count(entries[e]);
		vertex_cursor+=Entry_Vertex_Count(entries[e]);
	}
	Draw_Run(overlapping_nodes[run_class],run_first_index,run_triangles,run_first_vertex,vertex_cursor-run_first_vertex);
	SunShadow::SetSmokeGlow(DX8Wrapper::_Get_D3D_Device(),false);
	Direct3D11_Set_Fire_Look(false);
	Direct3D11_Set_Emissive(false);
	Direct3D11_Set_Soft_Particles(false);
	static const float no_effect[4]={0.0f,0.0f,0.0f,0.0f};
	Direct3D11_Set_Draw_Effect(0,no_effect);

	tDrawEnd = Clock_Ticks();
	flush_profile_draw_ms += flushProfileElapsedMS(tCopyEnd, tDrawEnd);
}

// ----------------------------------------------------------------------------
//
// The pool used to go out as one dynamic vertex buffer and one 16 bit index buffer, so a node that
// would have taken either past 65535 was refused and not drawn: a screen with a hundred thousand
// particles on it drew about sixteen thousand of them.  EA had left the batching as a @todo right
// here.  Every triangle is still sorted against every other, back to front, and only then written out
// in batches, in that order.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Flush_Sorting_Pool()
{
	if (!overlapping_node_count) return;

	SNAPSHOT_SAY(("SortingSystem - Flush \n"));

	// The draws' projection, which no run sets, for the entries' tiles.
	Matrix4x4 projection;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION,projection);
	sort_projection=(D3DXMATRIX&)projection;

	// Before any run resolves its program: the soft particles fade into this copy of the depth, and
	// a sorted mesh writing depth between two of their runs costs no second one.  A pool with no
	// billboards in it takes none.
	for (unsigned node=0;node<overlapping_node_count;++node) {
		if (overlapping_nodes[node]->quads) {
			Direct3D11_Take_Scene_Depth();
			break;
		}
	}

	flush_profile_entries += overlapping_entry_count;
	long long tSortStart, tSortEnd;
	tSortStart = Clock_Ticks();

	TempIndexStruct* tis=Get_Temp_Index_Array(overlapping_entry_count);

	// Every entry's depth, with its indices kept relative to its own node's first vertex.  Each node
	// writes only its own entries, so once the offsets are known the nodes fill them in parallel.
	struct NodeDepthJob {
		static void Run(int node_id,void* context) {
		TempIndexStruct* tis=(TempIndexStruct*)context;
		SortingNodeStruct* state=overlapping_nodes[node_id];
		const VertexFormatXYZNDUV2* src_verts=overlapping_node_vertices[node_id];
		const unsigned polygon_array_offset=overlapping_node_entry_offsets[node_id];

		D3DXMATRIX d3d_mtx=(D3DXMATRIX&)state->sorting_state.world*(D3DXMATRIX&)state->sorting_state.view;
		const Matrix4x4& mtx=(const Matrix4x4&)d3d_mtx;
		const bool camera_space=(mtx[0][2] == 0.0f && mtx[1][2] == 0.0f && mtx[3][2] == 0.0f && mtx[2][2] == 1.0f);
		overlapping_node_clip[node_id]=d3d_mtx*sort_projection;

		if (state->quads) {
			const int quad_count=state->polygon_count/2;
			for (int q=0;q<quad_count;++q) {
				const VertexFormatXYZNDUV2 *v = src_verts + q*4;
				TempIndexStruct *tis_ptr = tis + polygon_array_offset + q;
				tis_ptr->tri.i = (unsigned short)(q*4);
				tis_ptr->tri.j = 0;
				tis_ptr->tri.k = QUAD_ENTRY;
				tis_ptr->idx = node_id;
				if (camera_space) {
					tis_ptr->z = (v[0].z + v[1].z + v[2].z + v[3].z)*0.25f;
				} else {
					tis_ptr->z = (mtx[0][2]*(v[0].x + v[1].x + v[2].x + v[3].x) +
												mtx[1][2]*(v[0].y + v[1].y + v[2].y + v[3].y) +
												mtx[2][2]*(v[0].z + v[1].z + v[2].z + v[3].z))*0.25f + mtx[3][2];
				}
				DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Quad has invalid center"));
			}
			return;
		}

		unsigned short* indices=NULL;
		SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
		WWASSERT(index_buffer);
		indices=index_buffer->index_buffer;
		WWASSERT(indices);
		indices+=state->start_index;
		indices+=state->sorting_state.iba_offset;

		if (camera_space) {
			// The common case for particle systems.
			for (int i=0;i<state->polygon_count;++i) {
				unsigned short idx1=indices[i*3]-state->min_vertex_index;
				unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
				unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
				WWASSERT(idx1<state->vertex_count);
				WWASSERT(idx2<state->vertex_count);
				WWASSERT(idx3<state->vertex_count);
				const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
				const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
				const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
				unsigned array_index=i+polygon_array_offset;
				WWASSERT(array_index<overlapping_entry_count);
				TempIndexStruct *tis_ptr = tis + array_index;
				tis_ptr->tri.i = idx1;
				tis_ptr->tri.j = idx2;
				tis_ptr->tri.k = idx3;
				tis_ptr->idx = node_id;
				tis_ptr->z = (v1->z + v2->z + v3->z)/3.0f;
				DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
			}
		} else {
			for (int i=0;i<state->polygon_count;++i) {
				unsigned short idx1=indices[i*3]-state->min_vertex_index;
				unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
				unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
				WWASSERT(idx1<state->vertex_count);
				WWASSERT(idx2<state->vertex_count);
				WWASSERT(idx3<state->vertex_count);
				const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
				const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
				const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
				unsigned array_index=i+polygon_array_offset;
				WWASSERT(array_index<overlapping_entry_count);
				TempIndexStruct *tis_ptr = tis + array_index;
				tis_ptr->tri.i = idx1;
				tis_ptr->tri.j = idx2;
				tis_ptr->tri.k = idx3;
				tis_ptr->idx = node_id;
				tis_ptr->z = (mtx[0][2]*(v1->x + v2->x + v3->x) +
											mtx[1][2]*(v1->y + v2->y + v3->y) +
											mtx[2][2]*(v1->z + v2->z + v3->z))/3.0f + mtx[3][2];
				DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
			}
		}
		}
	};

	unsigned entry_offset=0;
	for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
		WWASSERT(vertex_buffer);
		VertexFormatXYZNDUV2* src_verts=vertex_buffer->VertexBuffer;
		WWASSERT(src_verts);
		src_verts+=state->sorting_state.vba_offset;
		src_verts+=state->sorting_state.index_base_offset;
		src_verts+=state->min_vertex_index;
		overlapping_node_vertices[node_id]=src_verts;
		overlapping_node_entry_offsets[node_id]=entry_offset;
		entry_offset+=state->quads ? state->polygon_count/2 : state->polygon_count;
	}
	Run_Parallel((int)overlapping_node_count,NODES_PER_CLAIM,NodeDepthJob::Run,tis);

	const TempIndexStruct* sorted=Sort_By_Depth(tis,temp_sort_scratch_array,overlapping_entry_count);

	// The runs, drawn from the radix sort's other buffer.
	Classify_Nodes(overlapping_node_count,overlapping_node_state);
	Reserve_Entry_Arrays(overlapping_entry_count);
	EntryTileJob tile_job;
	tile_job.entries=sorted;
	tile_job.entry_count=overlapping_entry_count;
	Run_Parallel((int)((overlapping_entry_count+ENTRIES_PER_COPY_CHUNK-1)/ENTRIES_PER_COPY_CHUNK),1,Find_Entry_Tiles,&tile_job);
	_Group_Runs(entry_states,entry_tiles,overlapping_entry_count,entry_order);
	TempIndexStruct* grouped=(sorted==temp_index_array) ? temp_sort_scratch_array : temp_index_array;
	for (unsigned e=0;e<overlapping_entry_count;++e) {
		grouped[e]=sorted[entry_order[e]];
	}

	tSortEnd = Clock_Ticks();
	flush_profile_sort_ms += flushProfileElapsedMS(tSortStart, tSortEnd);

	unsigned batch_start=0;
	while (batch_start<overlapping_entry_count) {
		unsigned batch_end=batch_start;
		unsigned batch_vertices=0;
		unsigned batch_indices=0;
		while (batch_end<overlapping_entry_count) {
			const unsigned vertices=Entry_Vertex_Count(grouped[batch_end]);
			const unsigned indices=Entry_Index_Count(grouped[batch_end]);
			if (batch_vertices+vertices>MAX_BATCH_VERTICES || batch_indices+indices>MAX_BATCH_INDICES) {
				break;
			}
			batch_vertices+=vertices;
			batch_indices+=indices;
			++batch_end;
		}
		Flush_Sorting_Batch(grouped+batch_start,batch_end-batch_start,batch_vertices,batch_indices);
		batch_start=batch_end;
	}

	// Release all references and return nodes back to the clean list for the frame...
	for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		Release_Refs(state);
		clean_list.Add_Head(state);
	}
	overlapping_node_count=0;
	overlapping_entry_count=0;

	SNAPSHOT_SAY(("SortingSystem - Done flushing\n"));

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush()
{
	WWPROFILE("SortingRenderer::Flush");
	Matrix4x4 old_view;
	Matrix4x4 old_world;
	DX8Wrapper::Get_Transform(D3DTS_VIEW,old_view);
	DX8Wrapper::Get_Transform(D3DTS_WORLD,old_world);

	// Put the unsorted nodes in where the sorted ones cross behind the camera, in the order they
	// arrived.  Insert_Before takes care of the head, so the boundary node can be the head.
	if (unsorted_list.Head()) {
		SortingNodeStruct* at=sorted_list.Head();
		while (at && at->transformed_center.Z > 0.0f)
			at=at->Succ();

		while (SortingNodeStruct* state=unsorted_list.Head()) {
			state->Remove();
			if (at)
				state->Insert_Before(at);
			else
				sorted_list.Add_Tail(state);
		}
	}

	while (SortingNodeStruct* state=sorted_list.Head()) {
		state->Remove();
		
		if ((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
			(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)) {
			Insert_To_Sorting_Pool(state);
		}
		else {
			DX8Wrapper::Set_Render_State(state->sorting_state);
			SunShadow::ReceiverExclusion sunReceivers(DX8Wrapper::_Get_D3D_Device(), state->excludeSunReceivers);
			SunShadow::CasterEffectScope casterEffect(DX8Wrapper::_Get_D3D_Device(), state->casterEffect);
			DX8Wrapper::Draw_Triangles(state->start_index,state->polygon_count,state->min_vertex_index,state->vertex_count);
			DX8Wrapper::Release_Render_State();
			Release_Refs(state);
			clean_list.Add_Head(state);
		}
	}

	bool old_enable=DX8Wrapper::_Is_Triangle_Draw_Enabled();
	DX8Wrapper::_Enable_Triangle_Draw(_EnableTriangleDraw);
	Flush_Sorting_Pool();
	DX8Wrapper::_Enable_Triangle_Draw(old_enable);

	DX8Wrapper::Set_Index_Buffer(0,0);
	DX8Wrapper::Set_Vertex_Buffer(0);
	total_sorting_vertices=0;

	DynamicIBAccessClass::_Reset(false);
	DynamicVBAccessClass::_Reset(false);


	DX8Wrapper::Set_Transform(D3DTS_VIEW,old_view);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,old_world);

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Deinit()
{
	SortingNodeStruct *head = NULL;

	//
	//	Flush the sorted list
	//
	while ((head = sorted_list.Head ()) != NULL) {
		sorted_list.Remove_Head ();
		delete head;
	}

	//
	//	Flush the unsorted list
	//
	while ((head = unsorted_list.Head ()) != NULL) {
		unsorted_list.Remove_Head ();
		delete head;
	}

	//
	//	Flush the clean list
	//
	while ((head = clean_list.Head ()) != NULL) {
		clean_list.Remove_Head ();
		delete head;
	}

	delete[] temp_index_array;
	temp_index_array=NULL;
	delete[] temp_sort_scratch_array;
	temp_sort_scratch_array=NULL;
	temp_index_array_count=0;
	delete[] batch_offsets;
	batch_offsets=NULL;
	batch_offsets_count=0;
	Release_Entry_Arrays();
}


// ----------------------------------------------------------------------------
//
// Insert a VolumeParticle triangle into the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_VolumeParticle(
	const SphereClass& bounding_sphere,
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count,
	unsigned short layerCount)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	//FOR VOLUME_PARTICLE LOGIC:
	// WE MUST MULTIPLY THE VERTCOUNT AND POLYCOUNT BY THE VOLUME_PARTICLE DEPTH
	DX8_RECORD_SORTING_RENDER( polygon_count * layerCount,vertex_count * layerCount);//THIS IS VOLUME_PARTICLE SPECIFIC

	SortingNodeStruct* state=Get_Sorting_Struct();
	DX8Wrapper::Get_Render_State(state->sorting_state);
	state->excludeSunReceivers = SunShadow::ReceiversExcluded(DX8Wrapper::_Get_D3D_Device());
	state->casterEffect = SunShadow::GetCasterEffect(DX8Wrapper::_Get_D3D_Device());

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));

	state->bounding_sphere=bounding_sphere;
	state->start_index=start_index;
	state->min_vertex_index=min_vertex_index;
	state->polygon_count=polygon_count * layerCount;//THIS IS VOLUME_PARTICLE SPECIFIC
	state->vertex_count=vertex_count * layerCount;//THIS IS VOLUME_PARTICLE SPECIFIC
	state->quads=false;
	state->glow=false;
	state->fire=false;
	state->emissive=false;
	state->hard_edge=false;
	No_Draw_Effect(state);

	SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	// Transform the center point to view space for sorting

	D3DXMATRIX mtx=(D3DXMATRIX&)state->sorting_state.world*(D3DXMATRIX&)state->sorting_state.view;
	D3DXVECTOR3 vec=(D3DXVECTOR3&)state->bounding_sphere.Center;
	D3DXVECTOR4 transformed_vec;
	D3DXVec3Transform(
		&transformed_vec,
		&vec,
		&mtx); 
	state->transformed_center=Vector3(transformed_vec[0],transformed_vec[1],transformed_vec[2]);


	// BUT WHAT IS THE DEAL WITH THE VERTCOUNT AND POLYCOUNT BEING N BUT TRANSFORMED CENTER COUNT == 1

	//THE TRANSFORMED CENTER[2] IS THE ZBUFFER DEPTH
	
	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	SortingNodeStruct* node=sorted_list.Head();
	while (node) {
		if (state->transformed_center.Z>node->transformed_center.Z) {
			if (sorted_list.Head()==sorted_list.Tail())
				sorted_list.Add_Head(state);
			else
				state->Insert_Before(node);
			break;
		}
		node=node->Succ();
	}
	if (!node) sorted_list.Add_Tail(state);
}
