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

// FILE: W3DVolumetricShadow.cpp ///////////////////////////////////////////////////////////
//
// Real time shadow volume representations
//
// Author: Colin Day, January 2001
// Adapted for W3D: Mark Wilczynski October 2001
//
//
///////////////////////////////////////////////////////////////////////////////
///@todo: Must cap shadow volumes if we ever allow camera inside the volumes.
///@todo: Find better way to determine when shadow volumes need updating - lights move, objects move.

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <assert.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "always.h"
#include "GameClient/View.h"
#include "WW3D2/camera.h"
#include "WW3D2/light.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/hlod.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "Lib/BaseType.h"
#include "W3DDevice/GameClient/W3DGranny.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DBridgeBuffer.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "GameLogic/PolygonTrigger.h"
#include "d3dx9math.h"
#include "Common/GlobalData.h"
#include "Common/DrawModule.h"
#include "W3DDevice/GameClient/W3DVolumetricShadow.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "WW3D2/statistics.h"
#include "Common/PerfTimer.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GameLogic.h"
#include "WW3D2/dx8caps.h"
#include "GameClient/Drawable.h"
#include "wwshade/shdmesh.h"
#include "wwshade/shdsubmesh.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/dx11runtime.h"
#include "WW3D2/sortingrenderer.h"
#include "GameClient/View.h"
#include "Platform/RenderTypes.h"
#include "Lib/Clock.h"		// Clock_Ticks: QueryPerformanceCounter on Windows, a monotonic clock elsewhere
#include "GameClient/ParticleSys.h"
#include <algorithm>
#include <vector>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// Global Variables and Functions /////////////////////////////////////////////

W3DVolumetricShadowManager	*TheW3DVolumetricShadowManager=NULL;
extern const FrustumClass *shadowCameraFrustum;	//defined in W3DShadow.

///////////////////////////////////////////////////////////////////////////////
// DEFINITIONS ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// when the change in angle from the object to the light source
// (represented in degrees in the first number below)
// is large enough the shadow info will be reconstructed
const Real cosAngleToCare = cos ((0.2 * PI) / 180.0);	//1.5 degree difference
#define MAX_SILHOUETTE_EDGES	1024	//maximum number of shadov volume sides or edges in silhoutte
#define	SHADOW_EXTRUSION_BUFFER	0.1f		//amount to extend shadow volume beyond what's required to hit ground.
#define AIRBORNE_UNIT_GROUND_DELTA 2.0f
#define MAX_SHADOW_LENGTH_SCALE_FACTOR	1.0f //amount shadow can extend beyond the objects normal bounding sphere
#define MAX_SHADOW_LENGTH_EXTRA_AIRBORNE_SCALE_FACTOR 1.5f	//scales MAX_SHADOW_LENGTH_SCALE_FACTOR a little more for flying units
#define MAX_EXTRUSION_LENGTH (512.0f*MAP_XY_FACTOR)	//maximum length of a shadow extrusion - assumed as 512x512 cell map for now.
#define MAX_SHADOW_EXTRUSION_UNDER_OBJECT_BEFORE_CLAMP	5.0f		//maximum amount that shadow can reach below object base (z-position) before we clamp it's length to reduce artifacts.
#define SHADOW_SAMPLING_INTERVAL (MAP_XY_FACTOR * 2.0f)				//stepsize along ray used to find lowest point on terrain within shadow's reach.
#define OVERHANGING_OBJECT_CLAMP_ANGLE	(80.0f/180.0f*PI)				//for objects that are right on a cliff edge, clamp light angle to cast a nearly vertical shadow.

// The sun's depth buffer and the box it covers.  2048 texels over 1800 world units is about one
// texel to the metre at the game's own scale, which is finer than the stencil edge it replaces.
// The box is centred on what the tactical camera looks at and is never narrower than that; it
// opens to the ground the camera sees once a zoom shows more (shadowMapHalfWidth), because at
// full zoom-out the top of the screen is about 1100 units past the look point and the ground
// there had no shadow at all.  The widest it goes is the cap below, 2.3 units a texel.
#define SHADOW_MAP_TEXELS 2048
#define SHADOW_MAP_HALF_WIDTH 900.0f
#define SHADOW_MAP_WIDEST_HALF_WIDTH 2400.0f
// The box grows in these steps, so the texel grid holds still while the zoom does.
#define SHADOW_MAP_HALF_WIDTH_STEP 64.0f
#define SHADOW_MAP_SUN_DISTANCE 2000.0f
#define SHADOW_MAP_NEAR_CLIP 10.0f
#define SHADOW_MAP_FAR_CLIP 4000.0f
// The console's freecam can look across the whole map at once, so its box is the whole map rather
// than the ground around a look point, and the map is four times as wide to keep the texels about
// the size they are at the usual zoom: a 6000 unit map is a box about 8500 across, a unit a texel.
// 8192 square at 32 bits is 256 MB of video memory while the freecam flies; a device that refuses
// it gets the usual 2048 over the same box, coarser.  Aircraft fly up to this far above the highest
// ground and still have to be inside the box's depth.
#define SHADOW_MAP_WHOLE_MAP_TEXELS 8192
#define SHADOW_MAP_WHOLE_MAP_HEADROOM 600.0f
#define SHADOW_MAP_WHOLE_MAP_DEPTH_MARGIN 200.0f
// How far a surface has to be behind what the map holds before it counts as shadowed, how dark a
// fully blocked pixel goes, and how far the filter reaches in texels.  The first is the one that
// decides between a surface shadowing itself in stripes and a shadow lifting off its own caster.
#define SHADOW_MAP_DEPTH_BIAS 0.0015f
// How far a bridge deck is pushed back in the map, in multiples of its own depth slope across one
// texel.  The decks are the one caster drawn with both faces, so this is their only guard against
// shadowing themselves.
#define SHADOW_MAP_BRIDGE_SLOPE_BIAS 2.0f
// 0.55 was picked on a frame with one base in it; over fourteen buildings a wide shadow on the
// ground read at a quarter of the sunlit sand, black in front of every wall, and the owner took
// 0.45 from three panels of the same base on 2026-09-21.
#define SHADOW_MAP_STRENGTH 0.45f
// How wide the filter may open and how narrow it stays on the ground, in texels of the map, and how
// much penumbra a unit of gap between a caster and what its shadow falls on is worth.  The last is
// the number the whole picture turns on: a tank's tracks are on the ground and keep a hard edge, a
// helicopter twenty metres up spreads.  It is not the sun's own half degree, which at this scale
// would be under a pixel; it is what the sky filling a shadow back in looks like.  It was 0.04,
// chosen on a frame of tanks, and a Comanche hovering 150 units up then cast nothing at all: the
// filter spread a fuselage four units wide over fourteen.  At 0.01 its blades read as a star.
#define SHADOW_MAP_WIDEST_TEXELS 9.0f
#define SHADOW_MAP_NARROWEST_TEXELS 0.9f
#define SHADOW_MAP_PENUMBRA_PER_UNIT 0.01f
#define SHADOW_MAP_SKY_FILL 0.12f
// The smoke in the sun's light.  The strength is how dark the thickest cloud leaves the ground and
// the smoke behind it, under the solid casters' 0.45 because the sky still lights the ground under a
// cloud and the smoke itself scatters some of the sun on; it held up on Golden Oasis on 2026-10-03.
// How thick each particle is to the sun is particleSunMapOpticalDepth's, and how a particle shades
// itself is the backend's (SMOKE_SELF_SHADOW_* in dx11backend.cpp).
#define SMOKE_SHADOW_STRENGTH 0.4f

// Whether the sun's map took this frame.  The volumes read it to know whether to stand down, and it
// is false on a machine with no Direct3D 11 device, which is what keeps that machine's shadows.
static Bool theShadowMapHoldsTheFrame = FALSE;

//#define SV_DEBUG
//#define SV_DEBUG_BOUNDS

struct SHADOW_STATIC_VOLUME_VERTEX	//vertex structure passed to D3D
{
		float x,y,z;
}; 
#define SHADOW_STATIC_VOLUME_FVF	D3DFVF_XYZ

#ifdef SV_DEBUG	//in debug mode, dynamic shadows are rendered with random diffuse color
	struct SHADOW_DYNAMIC_VOLUME_VERTEX	//vertex structure passed to D3D
	{
			float x,y,z;
			UnsignedInt diffuse;
	}; 
	#define SHADOW_DYNAMIC_VOLUME_FVF	D3DFVF_XYZ|D3DFVF_DIFFUSE
#else
	typedef struct SHADOW_STATIC_VOLUME_VERTEX	SHADOW_DYNAMIC_VOLUME_VERTEX;
	#define SHADOW_DYNAMIC_VOLUME_FVF	D3DFVF_XYZ
#endif

LPDIRECT3DVERTEXBUFFER9 shadowVertexBufferD3D=NULL;		///<D3D vertex buffer
LPDIRECT3DINDEXBUFFER9	shadowIndexBufferD3D=NULL;	///<D3D index buffer

//The Direct3D 11 copies of those two, null on a run without -dx11.  These buffers are made with
//CreateVertexBuffer straight on the device rather than through DX8VertexBufferClass, so they get
//none of the mirroring that class does and the twins are held here instead.  A write-only D3D9
//buffer cannot be read back, so the lock is redirected into the twin's own heap block and the
//unlock writes it into both - which is what DX11BufferLockClass is for.
DX11BufferTwinClass *shadowVertexTwin=NULL;
DX11BufferTwinClass *shadowIndexTwin=NULL;
int nShadowVertsInBuf=0;	//model vetices in vertex buffer
int nShadowStartBatchVertex=0;
int nShadowIndicesInBuf=0;	//model vetices in vertex buffer
int nShadowStartBatchIndex=0;
int SHADOW_VERTEX_SIZE=4096;
int SHADOW_INDEX_SIZE=8192;

//Rough bounding box around visible portion of the terrain
//useful for quick culling
static Real bcX;
static Real bcY;
static Real bcZ;
static Real beX;
static Real beY;
static Real beZ;

static LPDIRECT3DVERTEXBUFFER9 lastActiveVertexBuffer=NULL;

/** A simple structure to hold random geometry (vertices, polygons, etc.).  We'll use this
* to store shadow volumes. */
struct Geometry
{
	enum VisibleState {
		STATE_UNKNOWN = CollisionMath::BOTH,
		STATE_VISIBLE = CollisionMath::INSIDE,
		STATE_INVISIBLE = CollisionMath::OUTSIDE,
	};

	Geometry(void) : m_verts(NULL),m_indices(NULL),m_numPolygon(0),m_numVertex(0),m_flags(0) {}
	~Geometry(void) { Release();}

	Int Create( Int numVertices, Int numPolygons )
	{
		if (numVertices)
			if((m_verts=NEW Vector3[numVertices]) == 0)
				return FALSE;
		if (numPolygons)
			if((m_indices=NEW UnsignedShort[numPolygons*3]) == 0)
				return FALSE;
		m_numPolygon=numPolygons;
		m_numVertex=numVertices;
		m_numActivePolygon=0;
		m_numActiveVertex=0;
		return TRUE;
	}
	void Release(void)
	{	if (m_verts)
		{	delete [] m_verts;
			m_verts=NULL;
		}
		if (m_indices)
		{	delete [] m_indices;
			m_indices=NULL;
		}
		m_numActivePolygon=m_numPolygon=0;
		m_numActiveVertex=m_numVertex=0;
	}
	Int GetFlags (void) { return m_flags;}
	void SetFlags (Int flags) { m_flags = flags;}
	Int GetNumPolygon (void) { return m_numPolygon;}
	Int GetNumVertex (void)	{ return m_numVertex;}
	Int GetNumActivePolygon (void) { return m_numActivePolygon;}
	Int GetNumActiveVertex (void)	{ return m_numActiveVertex;}
	Int SetNumActivePolygon (Int numPolygons) { return m_numActivePolygon=numPolygons;}
	Int SetNumActiveVertex (Int numVertices)	{ return m_numActiveVertex=numVertices;}
	UnsignedShort *GetPolygonIndex (long dwPolyId, short *psIndexList) const
	{
		*psIndexList++ = m_indices[dwPolyId*3];
		*psIndexList++ = m_indices[dwPolyId*3+1];
		*psIndexList++ = m_indices[dwPolyId*3+2];
		return &m_indices[dwPolyId];
	}
	Int SetPolygonIndex (long dwPolyId, short *psIndexList)
	{
		m_indices[dwPolyId*3]=psIndexList[0];
		m_indices[dwPolyId*3+1]=psIndexList[1];
		m_indices[dwPolyId*3+2]=psIndexList[2];
		return 3;
	}
	Vector3 *GetVertex (int dwVertId)
	{
		return &m_verts[dwVertId];
	}
	const Vector3 *SetVertex (int dwVertId, const Vector3 *pvVertex)
	{
		m_verts[dwVertId]=*pvVertex;
		return 	pvVertex;
	}
	///Find a vertex within given range
	Int	FindVertexInRange (Int start, Int end, Vector3 *pvVertex)
	{
		for (Int i=start; i<end; i++)
		{
			if ((m_verts[i]-*pvVertex).Length2() == 0)
				return i;
		}
		return -1;
	}

	AABoxClass &getBoundingBox(void) {return m_boundingBox;}
	void	setBoundingBox(const AABoxClass &box)	{m_boundingBox=box;}
	void	setBoundingSphere(const SphereClass &sphere) {m_boundingSphere=sphere;}
	SphereClass &getBoundingSphere(void) {return m_boundingSphere;}
	void	setVisibleState(VisibleState state)	{m_visibleState=state;}
	VisibleState	getVisibleState(void) {return m_visibleState;}

private:
	Vector3	*m_verts;
	UnsignedShort *m_indices;
	Int m_numPolygon;
	Int m_numVertex;
	Int m_numActivePolygon;	///<number of polygons filled with valid data
	Int m_numActiveVertex;		///<number of vertices filled with valid data
	Int	m_flags;				///<geometry attribute flags - static vs. dynamic, etc.
	AABoxClass m_boundingBox;	///<object space bounding box of shadow volume
	SphereClass m_boundingSphere;	///<object space bounding sphere of shadow volume
	VisibleState	m_visibleState;		///<flag if this geometry was visible in this frame.
};

// CONST //////////////////////////////////////////////////////////////////////
const Int MAX_POLYGON_NEIGHBORS = 3;  // we use nothing but triangles for 
																			// geometry polygons so we have at 
																			// most 3 neighbors
const Int NO_NEIGHBOR = -1;  // entry value for neighbor when there isn't one

const Byte POLY_VISIBLE	  = 0x01;  // polygon is visible from light
const Byte POLY_PROCESSED = 0x02;  // this poly has been processed

// STRUCT /////////////////////////////////////////////////////////////////////

// NeighborEdge ---------------------------------------------------------------
typedef struct _NeighborEdge
{

	Short neighborIndex;  // index of polygon who is our neighbor, if there is
												// not a neighbor it contains NO_NEIGHBOR
	Short neighborEdgeIndex[ 2 ];  // the two vertex indices that represent the
																 // shared edge

} NeighborEdge;

// PolygonNeighbor ------------------------------------------------------------
struct  PolyNeighbor
{

	Short myIndex;  // our polygon index so we know who we are
	Byte status;  // status flags used when processing neighbors
	NeighborEdge neighbor[ MAX_POLYGON_NEIGHBORS ];

};

/**This class holds original mesh specific data and geometry.  The meshes stored in this
class have been cleaned to remove replicated vertices and also cache mesh data needed for
faster silhouette computation.  A model can contain many meshes for which we need to store
separate data so they can move relative to each other.*/
class W3DShadowGeometryMesh
{
	//for the sake of speed, give direct access to classes that need this data.
	friend class W3DShadowGeometry;
	friend class W3DVolumetricShadow;
	
public:
	W3DShadowGeometryMesh( void );
#ifdef DO_TERRAIN_SHADOW_VOLUMES
	virtual
#endif
	~W3DShadowGeometryMesh( void );

	/// @todo: Cache/Store face normals someplace so they are not recomputed when lights move.
	const Vector3& GetPolygonNormal(long dwPolyNormId) const
	{
		if (m_posedNormals)
			return m_posedNormals[dwPolyNormId];	//skin: normals rebuilt from this frame's pose
		WWASSERT(m_polygonNormals);
		return m_polygonNormals[dwPolyNormId];
	}
	int GetNumPolygon (void) const {return m_numPolygons;}
	/// given loaded geometry this builds the polygon neighbor information
	void buildPolygonNeighbors( void );
	void buildPolygonNormals(void)
	{
		if (!m_polygonNormals)
		{	//need to allocate storage
			Vector3 *tempVec = NEW Vector3[m_numPolygons];
			for (int i=0; i<m_numPolygons; i++)
			{
				buildPolygonNormal(i,&tempVec[i]);
			}
			m_polygonNormals = tempVec;
		}
	}
protected:
	Vector3 *buildPolygonNormal (long dwPolyNormId, Vector3 *pvNorm) const
	{
		if (m_polygonNormals)
			return &(*pvNorm=m_polygonNormals[dwPolyNormId]);
		short indexList[3];
//		Vector3 vertexList[3];
		//get vertex indices for this polygon
		GetPolygonIndex(dwPolyNormId,indexList);
		//get the vertices	
//		GetVertex(indexList[0],&vertexList[0]);
//		GetVertex(indexList[1],&vertexList[1]);
//		GetVertex(indexList[2],&vertexList[2]);
		const Vector3& v0=GetVertex(indexList[0]);
		const Vector3& v1=GetVertex(indexList[1]);
		const Vector3& v2=GetVertex(indexList[2]);

		//compute triangle normal by crossing 2 edges
		Vector3 edge1=v1-v0;
		Vector3 edge2=v1-v2;
#ifdef ALLOW_TEMPORARIES
		*pvNorm=Vector3::Cross_Product(edge2,edge1);
		pvNorm->Normalize();
#else
		Vector3::Normalized_Cross_Product(edge2,edge1, pvNorm);
#endif
		return pvNorm;
	}

	/// creating and deleting storage for the polygon neighbors
	Bool allocateNeighbors( Int numPolys );
	void deleteNeighbors( void );

	// geometry shadow data access
	PolyNeighbor *GetPolyNeighbor( Int polyIndex );
	int GetNumVertex (void)	const {	return m_numVerts;}
	///Get indices to the 3 vertices of this face.
#ifdef DO_TERRAIN_SHADOW_VOLUMES
	virtual
#endif
	void GetPolygonIndex (long dwPolyId, short *psIndexList) const
	{	const TriIndex *polyi=&m_polygons[dwPolyId];
		*psIndexList++ = m_parentVerts[polyi->I];
		*psIndexList++ = m_parentVerts[polyi->J];
		*psIndexList++ = m_parentVerts[polyi->K];
	}
#ifdef DO_TERRAIN_SHADOW_VOLUMES
	virtual
#endif
	const Vector3& GetVertex (int dwVertId) const
	{
		if (m_posedVerts)
			return m_posedVerts[dwVertId];	//skin: this frame's deformed vertices
		return m_verts[dwVertId];
	}

	Bool isSkin (void) const { return m_isSkin;}
	Int getNumSourceVerts (void) const { return m_numSourceVerts;}
	/**Point the vertex and face normal accessors at externally owned, per-frame data.  Used while a
	skinned mesh's silhouette and shadow volume are built; cleared again right after.*/
	void setPosedData (const Vector3 *verts, const Vector3 *normals)
	{	m_posedVerts = verts;
		m_posedNormals = normals;
	}

	MeshClass *m_mesh;	///< W3D mesh for this geometry
	Int m_meshRobjIndex;	///<index of this mesh within hlod robj
	const Vector3	*m_verts;		///<array of vertices
	Vector3	*m_polygonNormals;	///<array of face normals
	Int m_numVerts;	 ///< number of actual vertices after duplicates are removed.
	Int m_numPolygons; ///<number of polygons in source geometry
	const TriIndex	*m_polygons;	///<array of 3 vertex indices per face
	UnsignedShort *m_parentVerts;	///<array of parent vertex indices for each vertex.
	/// the neighbor info indexed by polygon id
	PolyNeighbor *m_polyNeighbors;
	Int m_numPolyNeighbors;  // length of m_polyNeighbors and the number of polygons
							 // in our current geometry.
	W3DShadowGeometry *m_parentGeometry; // mesh hierarchy containing this mesh.
	Bool m_isSkin;	///<mesh deforms with the skeleton, so its silhouette must be rebuilt from the current pose.
	Int m_numSourceVerts;	///<vertex count before duplicates were merged - the range m_parentVerts indexes into.
	const Vector3 *m_posedVerts;	///<this frame's deformed vertices, NULL unless a skin is being updated.
	const Vector3 *m_posedNormals;	///<face normals matching m_posedVerts, NULL unless a skin is being updated.

};	//end of meshInfo

#ifdef DO_TERRAIN_SHADOW_VOLUMES

//Custom version of W3DShadowGeometryMesh for meshes stored as heightmap
class W3DShadowGeometryHeightmapMesh : public W3DShadowGeometryMesh
{

public:
	virtual int GetPolygonIndex (long dwPolyId, short *psIndexList) const;
	virtual Vector3 *GetVertex (int dwVertId, Vector3 *pvVertex);
	W3DShadowGeometryHeightmapMesh(void) : m_patchOriginX(0),m_patchOriginY(0) { }
	void setPatchOrigin(Int x, Int y) {m_patchOriginX=x; m_patchOriginY=y;}
	void getPatchOrigin(Int *x, Int *y) {*x=m_patchOriginX; *y=m_patchOriginY;}
	void setPatchSize(Int size)	{m_width=size; m_numPolygons=(size-1)*(size-1)*2;}
	Int getPatchSize(void)	{return m_width;}

	protected:

		Int m_heightmapPitch;	///<width of full heightmap of which this mesh is a sub-rectangle
		Int m_width;			///<patch width
		Int m_patchOriginX;		///<location of patch within parent heightmap
		Int m_patchOriginY;		///<location of patch within parent heightmap
};

int W3DShadowGeometryHeightmapMesh::GetPolygonIndex (long dwPolyId, short *psIndexList) const
{
	//Find top left vertex of cell containing polygon
	WorldHeightMap *map=NULL;
	if (TheTerrainRenderObject)
		map=TheTerrainRenderObject->getMap();
	if (!map)
		return 0;

	Int row=dwPolyId/((m_width-1)<<1);
	Int column=(dwPolyId>>1)-row*((m_width-1));

#ifdef FLIP_TRIANGLES
	UnsignedByte alpha[4];
	float UA[4], VA[4];
	Bool flipForBlend;
	map->getAlphaUVData(column+m_patchOriginX, row+m_patchOriginY, UA, VA, alpha, &flipForBlend, false);
	if (flipForBlend)
	{
		if (dwPolyId &1)
		{	psIndexList[0]=row*m_width+column+1;
			psIndexList[1]=(row+1)*m_width+column+1;
			psIndexList[2]=(row+1)*m_width+column;
		}
		else
		{	psIndexList[0]=row*m_width+column;
			psIndexList[1]=row*m_width+column+1;
			psIndexList[2]=(row+1)*m_width+column;
		}
	}
	else
#endif
	{	if (dwPolyId &1)
		{	psIndexList[0]=row*m_width+column;
			psIndexList[1]=row*m_width+column+1;
			psIndexList[2]=(row+1)*m_width+column+1;
		}
		else
		{	psIndexList[0]=row*m_width+column;
			psIndexList[1]=(row+1)*m_width+column+1;
			psIndexList[2]=(row+1)*m_width+column;
		}
	}

	return 3;
}

Vector3 *W3DShadowGeometryHeightmapMesh::GetVertex (int dwVertId, Vector3 *pvVertex)
{
	WorldHeightMap *map=NULL;

	if (TheTerrainRenderObject)
		map=TheTerrainRenderObject->getMap();

	if (!map)
		return NULL;

	Int row=dwVertId/m_width;
	Int column=dwVertId-row*m_width;

	UnsignedByte *data=map->getDataPtr();
	pvVertex->X=(m_patchOriginX+column)*MAP_XY_FACTOR;
	pvVertex->Y=(m_patchOriginY+row)*MAP_XY_FACTOR;
	pvVertex->Z=(Real)data[(m_patchOriginX+column)+(m_patchOriginY+row)*map->getXExtent()]*MAP_HEIGHT_SCALE;

	return pvVertex;
}

Bool isPatchShadowed(W3DShadowGeometryHeightmapMesh	*hm_mesh)
{
	WorldHeightMap *map=NULL;
	Short poly[ 3 ];
	Vector3 vertex;
	Vector3 normal,lightVector;
	Int firstVisible=0;
	Int testVisible;

	if (TheTerrainRenderObject)
		map=TheTerrainRenderObject->getMap();

	if (!map)
		return NULL;

	hm_mesh->buildPolygonNormal( 0, &normal );

	// get the vertex indices at this polygon
	hm_mesh->GetPolygonIndex( 0, poly, 3 );

	//
	// find out "lightVector" to this polygon
	//
	// since our light source could be very close to the object and that
	// would change the shadow we are going to say that the light vector
	// is from the light position to one of the vertices in the polygon.
	// To be more correct we should use the center of the polygon but
	// this is a good approximation ... an ever broader approximation that
	// we could use would be the object center
	//
	hm_mesh->GetVertex( poly[ 0 ], &vertex );
	lightVector= vertex - LightPosWorld[0];

	//
	// dot the light vector with the normal of the polygon to see if the
	// poly is visible from this location
	//
	if( Vector3::Dot_Product( lightVector, normal ) < 0.0f )
		firstVisible=1;

	for (Int i=1; i<hm_mesh->GetNumPolygon(); i++)
	{
		hm_mesh->buildPolygonNormal( i, &normal );

		// get the vertex indices at this polygon
		hm_mesh->GetPolygonIndex( i, poly, 3 );

		//
		// find out "lightVector" to this polygon
		//
		// since our light source could be very close to the object and that
		// would change the shadow we are going to say that the light vector
		// is from the light position to one of the vertices in the polygon.
		// To be more correct we should use the center of the polygon but
		// this is a good approximation ... an ever broader approximation that
		// we could use would be the object center
		//
		hm_mesh->GetVertex( poly[ 0 ], &vertex );
		lightVector= vertex - LightPosWorld[0];

		//
		// dot the light vector with the normal of the polygon to see if the
		// poly is visible from this location
		//
		testVisible=0;
		if( Vector3::Dot_Product( lightVector, normal ) < 0.0f )
			testVisible=1;

//		if (testVisible ^ firstVisible)
//			return TRUE;	//found polys facing different directions to sun, will cast shadow
		if (!testVisible)
			return TRUE;	//some part of mesh not facing light, so it could cast a shadow
	}
	return FALSE;
}

#define SV_MAX_TERRAIN_MESHES	16

static W3DShadowGeometryHeightmapMesh terrainMeshes[SV_MAX_TERRAIN_MESHES];
static Int numTerrainMeshes=0;

void W3DVolumetricShadowManager::loadTerrainShadows(void)
{
	WorldHeightMap *map=NULL;
	Int patchSize=3;

	if (TheTerrainRenderObject)
		map=TheTerrainRenderObject->getMap();

	if (!map)
		return;

	for (Int y=0; y<map->getYExtent(); y += patchSize-1)
	{
		for (Int x=0; x<map->getXExtent(); x += patchSize-1)
		{
			W3DShadowGeometryHeightmapMesh	*hm_mesh=&terrainMeshes[numTerrainMeshes];
			hm_mesh->setPatchOrigin(x,y);
			hm_mesh->setPatchSize(patchSize);

			if(isPatchShadowed(hm_mesh))
			{	//some polygons in this patch cast shadows, need to generate a mesh
//				hm_mesh->buildPolygonNeighbors();
				numTerrainMeshes++;
			}
		}
	}

/*	W3DShadowGeometryHeightmapMesh	hm_mesh;
	short indexList[3];
	Vector3 vertexList[3];
*/
/*	W3DShadow *shadow = NEW W3DShadow;
	// add to our shadow list through the shadow next links
	shadow->m_next = m_shadowList;
	m_shadowList = shadow;	
*/
}

#endif //DO_TERRAIN_SHADOW_VOLUMES

/** This class will wrap any shadow casting geometry with additional
data needed for efficient shadow volume generation.  The W3DVolumetricShadowManager
will allocate these structures and hash them for quick re-use on other
models sharing the same geometry.*/
class W3DShadowGeometry : public RefCountClass, public	HashableClass
{

	public:

		W3DShadowGeometry( void ) { };
		~W3DShadowGeometry( void ) { };

		virtual	const char * Get_Key( void )	{ return m_namebuf;	}

		Int init (RenderObjClass *robj);
		Int initFromHLOD (RenderObjClass *robj);	///<initialize the geometry from a W3D HLOD object.
		Int initFromMesh (RenderObjClass *robj);///<initialize the geometry from a W3D Mesh object.

		const char *		Get_Name(void) const	{ return m_namebuf;}
		void				Set_Name(const char *name)
		{	memset(m_namebuf,0,sizeof(m_namebuf));	//pad with zero so always ends with null character.
			strncpy(m_namebuf,name,sizeof(m_namebuf)-1);
		}
		Int					getMeshCount(void)	{ return m_meshCount;}
		W3DShadowGeometryMesh	*getMesh(Int index)	{ return &m_meshList[index];}

		
		int GetNumTotalVertex (void)	{	return m_numTotalsVerts;}	///<total number of vertices in all meshes of this geometry

	private:

		char m_namebuf[2*W3D_NAME_LEN];	///<name of model hierarchy

		W3DShadowGeometryMesh m_meshList[MAX_SHADOW_CASTER_MESHES]; ///<collection of meshes for this geometry.
		Int m_meshCount;							///<number of meshes in hierarchy
		Int m_numTotalsVerts;						///<number of verts in entire hierarchy
};
  
#define MAX_SHADOW_VOLUME_VERTS 16384

Int W3DShadowGeometry::initFromHLOD(RenderObjClass *robj)
{
	HLodClass *hlod=(HLodClass *)robj;
	//locations of parent vertices inside the vertex array after duplicate
	//vertices are removed.
	UnsignedShort vertParent[MAX_SHADOW_VOLUME_VERTS];

	Int i,j,k,newVertexCount;

	Int top = hlod->Get_LOD_Count()-1;
	W3DShadowGeometryMesh *geomMesh=&m_meshList[m_meshCount];

	m_numTotalsVerts=0;

	for (i = 0; i < hlod->Get_Lod_Model_Count(top); i++)
	{
		if (hlod->Peek_Lod_Model(top,i) && hlod->Peek_Lod_Model(top,i)->Class_ID() == RenderObjClass::CLASSID_MESH)
		{
			DEBUG_ASSERTCRASH(m_meshCount < MAX_SHADOW_CASTER_MESHES, ("Too many shadow sub-meshes"));

			geomMesh->m_mesh = (MeshClass *)hlod->Peek_Lod_Model(top,i);
			geomMesh->m_meshRobjIndex=i;

//			if (!geomMesh->m_mesh->Peek_Model()->Get_Flag(MeshGeometryClass::CAST_SHADOW))
//				continue; // CNC3 (gth) Only cast shadows from meshes with the shadow flag ENABLED!

			if ((geomMesh->m_mesh->Is_Alpha() || geomMesh->m_mesh->Is_Translucent()) && !geomMesh->m_mesh->Peek_Model()->Get_Flag(MeshGeometryClass::CAST_SHADOW))
				continue; //transparent meshes that don't have forced shadows will not cast volumetric shadows
			// CNC3 (gth) skin meshes should never cast a volumetric shadow
			if (geomMesh->m_mesh->Peek_Model()->Get_Flag(MeshGeometryClass::SKIN)) 
				continue;

			MeshModelClass *mm = geomMesh->m_mesh->Peek_Model();
			geomMesh->m_numVerts=mm->Get_Vertex_Count();
			geomMesh->m_numSourceVerts=geomMesh->m_numVerts;
			geomMesh->m_verts=mm->Get_Vertex_Array();
			geomMesh->m_numPolygons=mm->Get_Polygon_Count();
			geomMesh->m_polygons=mm->Get_Polygon_Array();

			if (geomMesh->m_numVerts > MAX_SHADOW_VOLUME_VERTS)
				return FALSE;	//too many vertices to process

			//reset index of all vertices
			memset(vertParent,0xffffffff,sizeof(vertParent));
			newVertexCount=geomMesh->m_numVerts;
			//Find all duplicated vertices.
			for (j=0; j<geomMesh->m_numVerts; j++)
			{
				if (vertParent[j] != 0xffff)
					continue;	//this vertex has already been processed

				const Vector3 *v_curr=&geomMesh->m_verts[j];

				for (k=j+1; k<geomMesh->m_numVerts; k++)
				{
					Vector3 len(*v_curr - geomMesh->m_verts[k]);
					if (len.Length2() == 0)
					{	//found duplicate vertex
						vertParent[k]=j;
						newVertexCount--;	//decrease total vertices since duplicate found.
					}
				}
				vertParent[j]=j;	//first instance of new vertex
			}
			geomMesh->m_parentVerts = NEW UnsignedShort[geomMesh->m_numVerts];
			memcpy(geomMesh->m_parentVerts,vertParent,sizeof(UnsignedShort)*geomMesh->m_numVerts);
			geomMesh->m_numVerts=newVertexCount;	//adjust actual vertex count to ignore duplicates
			m_numTotalsVerts += newVertexCount;
			geomMesh->m_parentGeometry = this;

			// build our neighboring polygon information
//			geomMesh->buildPolygonNeighbors();
			
			geomMesh++;
			m_meshCount++;
		}

		
// ponytail: off, because wwshade is not built (no .dsp ever linked wwshade.lib
// either, and its bump shaders need the missing shdpp/NVASM tools).  ShdMeshClass
// is the only thing that returns CLASSID_SHDMESH, so with wwshade absent this test
// can never be true - keeping it on only costs two unresolved externals at link.
// Turn it back to 1 the day wwshade builds.
#if (0) //(cnc3)(gth) Support for ShaderMeshes!
// I'm coding this as a completely independent block rather than re-factoring the code above
// because it will probably save us pain in future merges.
		if (hlod->Peek_Lod_Model(top,i) && hlod->Peek_Lod_Model(top,i)->Class_ID() == RenderObjClass::CLASSID_SHDMESH)
		{
			DEBUG_ASSERTCRASH(m_meshCount < MAX_SHADOW_CASTER_MESHES, ("Too many shadow sub-meshes"));

			ShdMeshClass * shd_mesh = (ShdMeshClass *)hlod->Peek_Lod_Model(top,i);

			for (int sub_mesh_index=0; sub_mesh_index < shd_mesh->Get_Sub_Mesh_Count(); sub_mesh_index++) {
				ShdSubMeshClass * sub_mesh = shd_mesh->Peek_Sub_Mesh(sub_mesh_index);

				if (!sub_mesh->Get_Flag(MeshGeometryClass::CAST_SHADOW))
					continue; // CNC3 (gth) Only cast shadows from meshes with the shadow flag ENABLED!

				//transparent meshes that don't have forced shadows will not cast volumetric shadows
				if (shd_mesh->Is_Translucent() && !sub_mesh->Get_Flag(MeshGeometryClass::CAST_SHADOW))
					continue; 

				// skin meshes should never cast a volumetric shadow
				if (sub_mesh->Get_Flag(MeshGeometryClass::SKIN)) 
					continue;

				geomMesh->m_mesh = NULL; //hope this doesn't cause problems!
				geomMesh->m_meshRobjIndex=i;

				// Count the polygons and vertices 
				geomMesh->m_numVerts = sub_mesh->Get_Vertex_Count();
				geomMesh->m_numPolygons = sub_mesh->Get_Polygon_Count();

				geomMesh->m_verts=sub_mesh->Get_Vertex_Array();
				geomMesh->m_polygons=sub_mesh->Get_Polygon_Array();

				if (geomMesh->m_numVerts > MAX_SHADOW_VOLUME_VERTS)
					return FALSE;	//too many vertices to process

				//reset index of all vertices
				memset(vertParent,0xffffffff,sizeof(vertParent));
				newVertexCount=geomMesh->m_numVerts;
				//Find all duplicated vertices.
				for (j=0; j<geomMesh->m_numVerts; j++)
				{
					if (vertParent[j] != 0xffff)
						continue;	//this vertex has already been processed

					const Vector3 *v_curr=&geomMesh->m_verts[j];

					for (k=j+1; k<geomMesh->m_numVerts; k++)
					{
						Vector3 len(*v_curr - geomMesh->m_verts[k]);
						if (len.Length2() == 0)
						{	//found duplicate vertex
							vertParent[k]=j;
							newVertexCount--;	//decrease total vertices since duplicate found.
						}
					}
					vertParent[j]=j;	//first instance of new vertex
				}
				geomMesh->m_parentVerts = new UnsignedShort[geomMesh->m_numVerts];
				memcpy(geomMesh->m_parentVerts,vertParent,sizeof(UnsignedShort)*geomMesh->m_numVerts);
				geomMesh->m_numVerts=newVertexCount;	//adjust actual vertex count to ignore duplicates
				m_numTotalsVerts += newVertexCount;
				geomMesh->m_parentGeometry = this;

				// build our neighboring polygon information
//				geomMesh->buildPolygonNeighbors();
				
				geomMesh++;
				m_meshCount++;

			}
		}
#endif //(cnc3)(gth) Support for ShaderMeshes!
	
	}
	
	//Second pass: the skinned meshes the loop above deliberately skipped.  Their vertices move with
	//the skeleton, so the silhouette has to be rebuilt from the posed vertices every frame instead of
	//once here - but the topology (polygons, neighbors, welded vertex indices) never changes, so it is
	//shared exactly like a rigid mesh's.  These are taken from LOD 0 because that is the LOD
	//updateVolumes() and RenderVolume() fetch the live mesh from, and Get_Deformed_Vertices() must be
	//called on the same mesh the indices were built from.
	if (TheGlobalData && TheGlobalData->m_useShadowVolumesForSkins)
	{
		for (i = 0; i < hlod->Get_Lod_Model_Count(0); i++)
		{
			RenderObjClass *lodModel=hlod->Peek_Lod_Model(0,i);

			if (!lodModel || lodModel->Class_ID() != RenderObjClass::CLASSID_MESH)
				continue;

			MeshClass *skinMesh=(MeshClass *)lodModel;

			if (!skinMesh->Peek_Model()->Get_Flag(MeshGeometryClass::SKIN))
				continue;	//rigid meshes were handled above

			if ((skinMesh->Is_Alpha() || skinMesh->Is_Translucent()) && !skinMesh->Peek_Model()->Get_Flag(MeshGeometryClass::CAST_SHADOW))
				continue;	//transparent meshes that don't have forced shadows will not cast volumetric shadows

			if (m_meshCount >= MAX_SHADOW_CASTER_MESHES)
			{	DEBUG_ASSERTCRASH(m_meshCount < MAX_SHADOW_CASTER_MESHES, ("Too many shadow sub-meshes"));
				break;
			}

			geomMesh->m_mesh = skinMesh;
			geomMesh->m_meshRobjIndex = i;
			geomMesh->m_isSkin = TRUE;

			MeshModelClass *mm = skinMesh->Peek_Model();
			geomMesh->m_numVerts=mm->Get_Vertex_Count();
			geomMesh->m_numSourceVerts=geomMesh->m_numVerts;
			geomMesh->m_verts=mm->Get_Vertex_Array();	//bind pose, only used to weld duplicates below
			geomMesh->m_numPolygons=mm->Get_Polygon_Count();
			geomMesh->m_polygons=mm->Get_Polygon_Array();

			if (geomMesh->m_numVerts > MAX_SHADOW_VOLUME_VERTS)
				return FALSE;	//too many vertices to process

			//reset index of all vertices
			memset(vertParent,0xffffffff,sizeof(vertParent));
			newVertexCount=geomMesh->m_numVerts;
			//Find all duplicated vertices.  A skin's duplicates are duplicated in the bind pose and stay
			//welded in every pose, since both copies are driven by the same bone weights.
			for (j=0; j<geomMesh->m_numVerts; j++)
			{
				if (vertParent[j] != 0xffff)
					continue;	//this vertex has already been processed

				const Vector3 *v_curr=&geomMesh->m_verts[j];

				for (k=j+1; k<geomMesh->m_numVerts; k++)
				{
					Vector3 len(*v_curr - geomMesh->m_verts[k]);
					if (len.Length2() == 0)
					{	//found duplicate vertex
						vertParent[k]=j;
						newVertexCount--;	//decrease total vertices since duplicate found.
					}
				}
				vertParent[j]=j;	//first instance of new vertex
			}
			geomMesh->m_parentVerts = NEW UnsignedShort[geomMesh->m_numVerts];
			memcpy(geomMesh->m_parentVerts,vertParent,sizeof(UnsignedShort)*geomMesh->m_numVerts);
			geomMesh->m_numVerts=newVertexCount;	//adjust actual vertex count to ignore duplicates
			m_numTotalsVerts += newVertexCount;
			geomMesh->m_parentGeometry = this;

			geomMesh++;
			m_meshCount++;
		}
	}

//	for (i = 0; i < AdditionalModels.Count(); i++) {
//		res |= AdditionalModels[i].Model->Cast_Ray(raytest);
//	}

	return m_meshCount != 0;
}

Int W3DShadowGeometry::initFromMesh(RenderObjClass *robj)
{
	//locations of parent vertices inside the vertex array after duplicate
	//vertices are removed.
	UnsignedShort vertParent[MAX_SHADOW_VOLUME_VERTS];

	Int j,k,newVertexCount;
	W3DShadowGeometryMesh *geomMesh=&m_meshList[m_meshCount];

	assert (m_meshCount < MAX_SHADOW_CASTER_MESHES);

	geomMesh->m_mesh = (MeshClass *)robj;
	geomMesh->m_meshRobjIndex = -1;	//robj is the mesh so no index needed.
	if (((geomMesh->m_mesh->Is_Alpha() || geomMesh->m_mesh->Is_Translucent()) && !geomMesh->m_mesh->Peek_Model()->Get_Flag(MeshGeometryClass::CAST_SHADOW)))
		return FALSE; //transparent meshes that don't have forced shadows will not cast volumetric shadows

	MeshModelClass *mm = geomMesh->m_mesh->Peek_Model();
	geomMesh->m_numVerts=mm->Get_Vertex_Count();
	geomMesh->m_numSourceVerts=geomMesh->m_numVerts;
	geomMesh->m_isSkin=mm->Get_Flag(MeshGeometryClass::SKIN);
	geomMesh->m_verts=mm->Get_Vertex_Array();
	geomMesh->m_numPolygons=mm->Get_Polygon_Count();
	geomMesh->m_polygons=mm->Get_Polygon_Array();

	if (geomMesh->m_numVerts > MAX_SHADOW_VOLUME_VERTS)
		return FALSE;	//too many vertices to process

	//reset index of all vertices
	memset(vertParent,0xffffffff,sizeof(vertParent));
	newVertexCount=geomMesh->m_numVerts;
	//Find all duplicated vertices.
	for (j=0; j<geomMesh->m_numVerts; j++)
	{
		if (vertParent[j] != 0xffff)
			continue;	//this vertex has already been processed

		const Vector3 *v_curr=&geomMesh->m_verts[j];

		for (k=j+1; k<geomMesh->m_numVerts; k++)
		{
			Vector3 len(*v_curr - geomMesh->m_verts[k]);
			if (len.Length2() == 0)
			{	//found duplicate vertex
				vertParent[k]=j;
				newVertexCount--;	//decrease total vertices since duplicate found.
			}
		}
		vertParent[j]=j;	//first instance of new vertex
	}

	geomMesh->m_parentVerts = NEW UnsignedShort[geomMesh->m_numVerts];
	memcpy(geomMesh->m_parentVerts,vertParent,sizeof(UnsignedShort)*geomMesh->m_numVerts);
	geomMesh->m_numVerts=newVertexCount;	//adjust actual vertex count to ignore duplicates
	geomMesh->m_parentGeometry = this;

	m_meshCount=1;
	m_numTotalsVerts=newVertexCount;

	// build our neighboring polygon information
//	geomMesh->buildPolygonNeighbors();

	return TRUE;
}

Int W3DShadowGeometry::init(RenderObjClass *robj)
{
	return TRUE;
//	m_robj=robj;
/*	//code to deal with granny - don't think we'll use shadow volumes on these!?
	granny_file *fileInfo=robj->getPrototype().m_file;

	for (Int modelIndex=0; modelIndex<fileInfo->ModelCount; modelIndex++)
	{
		granny_model *sourceModel =  fileInfo->Models[modelIndex];
		if (strcasecmp(sourceModel->Name,"AABOX") == 0)
		{	//found a collision box, copy out data
			int MeshCount = sourceModel->MeshBindingCount;
			if (MeshCount==1)
			{
				granny_mesh *sourceMesh = sourceModel->MeshBindings[0].Mesh;
				granny_pn33_vertex *Vertices = (granny_pn33_vertex *)sourceMesh->PrimaryVertexData->Vertices;
				Vector3 points[24];

				assert (sourceMesh->PrimaryVertexData->VertexCount <= 24);

				for (Int boxVertex=0; boxVertex<sourceMesh->PrimaryVertexData->VertexCount; boxVertex++)
				{	points[boxVertex].Set(Vertices[boxVertex].Position[0],
										  Vertices[boxVertex].Position[1],
										  Vertices[boxVertex].Position[2]);
				}
				box.Init(points,sourceMesh->PrimaryVertexData->VertexCount);
			}
		}
		else
		{	//mesh is part of model
			int meshCount = sourceModel->MeshBindingCount;
			for (Int meshIndex=0; meshIndex<meshCount; meshIndex++)
			{
				granny_mesh *sourceMesh = sourceModel->MeshBindings[meshIndex].Mesh;
				if (sourceMesh->PrimaryVertexData)
					vertexCount+=sourceMesh->PrimaryVertexData->VertexCount;
			}
		}
	}*/
}

///////////////////////////////////////////////////////////////////////////////
// MEMBER DEFINITIONS /////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// W3DShadowGeometry =============================================================
// ============================================================================
W3DShadowGeometryMesh::W3DShadowGeometryMesh( void )
{	
	// init polygon neighbor information
	m_polyNeighbors = NULL;
	m_numPolyNeighbors = 0;
	m_parentVerts = NULL;
	m_polygonNormals = NULL;
	m_mesh = NULL;
	m_meshRobjIndex = -1;
	m_verts = NULL;
	m_polygons = NULL;
	m_numVerts = 0;
	m_numPolygons = 0;
	m_parentGeometry = NULL;
	m_isSkin = FALSE;
	m_numSourceVerts = 0;
	m_posedVerts = NULL;
	m_posedNormals = NULL;
}  // end W3DShadowGeometry

// ~W3DShadowGeometry ============================================================
// ============================================================================
W3DShadowGeometryMesh::~W3DShadowGeometryMesh( void )
{
	// remove our neighbor list information allocated
	deleteNeighbors();
	if (m_parentVerts) {
		delete [] m_parentVerts;
	}
	if (m_polygonNormals)
		delete [] m_polygonNormals;

}  // end ~W3DShadowGeometry

// GetPolyNeighbor ============================================================
// Return the poly neighbor structure at the given index
// ============================================================================
PolyNeighbor *W3DShadowGeometryMesh::GetPolyNeighbor( Int polyIndex )
{
if (!m_polyNeighbors) {
	buildPolygonNeighbors();
}

	// sanity
	if( polyIndex < 0 || polyIndex >= m_numPolyNeighbors )
	{

//		DBGPRINTF(( "Invalid neighbor index '%d'\n", polyIndex ));
		assert( 0 );
		return NULL;

	}  // en dif

	return &m_polyNeighbors[ polyIndex ];

}  // end GetPolyNeighbor

// buildPolygonNeighbors ======================================================
// Whenever we set a new geometry we want to build some information about
// the faces in the new geometry so that we can efficienty traverse across
// the surface to neighboring polygons
// ============================================================================
void W3DShadowGeometryMesh::buildPolygonNeighbors( void )
{
	Int numPolys;
	Int i, j;
	// Jani: Make sure we have polygon normals BEFORE we need them...
	buildPolygonNormals();

	// how many polygons are in our geometry
	numPolys = GetNumPolygon();

	//
	// if there are no polygons for this geometry then we should have no
	// neighbor information
	//
	if( numPolys == 0 )
	{

		//
		// if our geometry has somehow deformed and we used to have polygon
		// neighbor information we should delete it before we bail
		//
		if( m_numPolyNeighbors != 0 )
			deleteNeighbors();

		return;  // nothing to see here people, move along

	}  // end if

	//
	// in the event that this geometry can deform on the fly or we are
	// building our neighbor information for the very first time ...
	// if our current geometry has a different number of polygons than
	// we had previously calculated we need to delete and reallocate a
	// new storate space for the neighbor information
	//
	if( numPolys != m_numPolyNeighbors )
	{

		// delete the old neighbor storage
		deleteNeighbors();

		// allocate a new pool for neighbor information
		if( allocateNeighbors( numPolys ) == FALSE )
			return;

	}  // end if

	//
	// initialize all polygon neighbor information to none and assign our
	// own reference to myIndex so we know who we are
	//
	for( i = 0; i < m_numPolyNeighbors; i++ )
	{

		// assign our own identification
		m_polyNeighbors[ i ].myIndex = i;

		// assign our neighbors to none
		for( j = 0; j < MAX_POLYGON_NEIGHBORS; j++ )
			m_polyNeighbors[ i ].neighbor[ j ].neighborIndex = NO_NEIGHBOR;

	}  // end for i

	// assign polygon data for each of our polygons
	for( i = 0; i < m_numPolyNeighbors; i++ )
	{
		Short poly[ 3 ];  // vertex indices for this polygon
		Short otherPoly[ 3 ];  // vertex indices for other polygon

		// get the indices of the three triangle points for this polygon
		GetPolygonIndex( i, poly );
		const Vector3& vNorm=GetPolygonNormal(i);

		// find the neighbors of this polygon
		for( j = 0; j < m_numPolyNeighbors; j++ )
		{
			Int a, b;
			Int index1, index2;
			Int index1Pos[2]; //positions of shared edge vertices in triangle list. (0,1 or 2)
			Int diff1,diff2;

			// ignore our own polygon
			if( i == j )
				continue;

			// get the vertex index information for this other polygon
			GetPolygonIndex( j, otherPoly );

			//
			// if 2 of the 3 vertex indices are the same then these polygons
			// are neighbors
			//
			//Also check if winding order of vertices on edge is opposite.
			//If vertices are in same order as our polygon, then it's
			//not a valid edge because the neighbor is flipped.

			index1 = -1;
			index2 = -1;
			for( a = 0; a < 3; a++ )
				for( b = 0; b < 3; b++ )
					if( poly[ a ] == otherPoly[ b ] )
					{

						if( index1 == -1 )
						{	index1 = poly[ a ];  // record matching index1
							index1Pos[0]=a;
							index1Pos[1]=b;
						}
						else if( index2 == -1 )
						{	//Check direction of edge in each polygon.  If they are same direction skip it.
							diff1 = a-index1Pos[0];
							diff2 = b-index1Pos[1];
							if ( ((diff1&0x80000000)^((abs(diff1)&2)<<30)) != ((diff2&0x80000000)^((abs(diff2)&2)<<30)))
							{

								const Vector3& vOtherNorm=GetPolygonNormal(j);
								
								//Check if the 2 polygons face in exactly opposite directions - don't allow this type of neighbor.
								if (fabs(Vector3::Dot_Product(vOtherNorm,vNorm) + 1.0f) <= 0.01f)
									continue;

								index2 = poly[ a ];  // record matching index2
							}
							else
								continue;
						}
						else
						{//This is the same poly facing opposite direction.	//assert( 0 );  // should never match 3 vertices!
							index1=index2=-1;
							continue; 
						}
					}  // end if
			if( index1 != -1 && index2 != -1  )
			{
				//
				// the polygon j is a neighbor of our polygon i, put the j
				// index into the first free neighbor slot for polygon i
				//
				for( a = 0; a < MAX_POLYGON_NEIGHBORS; a++ )
					if( m_polyNeighbors[ i ].neighbor[ a ].neighborIndex == NO_NEIGHBOR )
					{

						// record the neighbor index
						m_polyNeighbors[ i ].neighbor[ a ].neighborIndex = j;

						// record the sharded edge vertex indices
						m_polyNeighbors[ i ].neighbor[ a ].neighborEdgeIndex[ 0 ] = index1;
						m_polyNeighbors[ i ].neighbor[ a ].neighborEdgeIndex[ 1 ] = index2;

						break;  // exit for a

					}  // end if

				//
				// error condition, if our counter a is at the max number
				// of neighbors, which is 3 for a triangle mesh, we did something
				// wrong here because that would mean we found a 4th match!
				//
				if (a == MAX_POLYGON_NEIGHBORS)
				{
//					Vector3 pv[3];
//					char errorText[255];

//					GetVertex (poly[0], &pv[0]);
//					GetVertex (poly[1], &pv[1]);
//					GetVertex (poly[2], &pv[2]);

//					pv[0] = pv[0] + pv[1] + pv[2];
//					pv[0] /= 3.0f;	//find center of polygon

//					sprintf(errorText,"%s: Shadow Polygon with too many neighbors at %f,%f,%f",m_parentGeometry->Get_Name(),pv[0].X,pv[0].Y,pv[0].Z);
//					DEBUG_LOG(("****%s Shadow Polygon with too many neighbors at %f,%f,%f\n",m_parentGeometry->Get_Name(),pv[0].X,pv[0].Y,pv[0].Z));
//					DEBUG_ASSERTCRASH(a != MAX_POLYGON_NEIGHBORS,(errorText));
				}

			}  // end if

		}  // end for j

	}  // end for i

}  // end buildPolygonNeighbors

// allocateNeighbors ==========================================================
// Allocate storage for the polygon neighbors and record its size
// ============================================================================
Bool W3DShadowGeometryMesh::allocateNeighbors( Int numPolys )
{

	// assure we're not re-allocating without deleting
	assert( m_numPolyNeighbors == 0 );
	assert( m_polyNeighbors == NULL );

	// allocate the list
	m_polyNeighbors = NEW PolyNeighbor[ numPolys ];
	if( m_polyNeighbors == NULL )
	{

//		DBGPRINTF(( "Unable to allocate polygon neighbors\n" ));
		assert( 0 );
		return FALSE;

	}  // end if

	// list is now acutally allocated
	m_numPolyNeighbors = numPolys;

	return TRUE;  // success!

}  // end allocateNeighbors

// deleteNeighbors ============================================================
// Delete all polygon neighbor storage and information
// ============================================================================
void W3DShadowGeometryMesh::deleteNeighbors( void )
{

	// delete list
	if( m_polyNeighbors )
	{

		delete [] m_polyNeighbors;
		m_polyNeighbors = NULL;
		m_numPolyNeighbors = 0;

	}  // end if

	// sanity error checking
	assert( m_numPolyNeighbors == 0 );
	assert( m_polyNeighbors == NULL );

}  // end deleteNeighbors

//#include "Common/ThingTemplate.h"

// updateOptimalExtrusionPadding ==============================================
// Use raycasting to figure out a shadow extrusion length that guarantees that
// the highest point of the object is extruded long enough to hit some ground.
// This is a very slow operation so only do once for static non-moving objects.
// ============================================================================
void W3DVolumetricShadow::updateOptimalExtrusionPadding(void)
{
	if (m_robj)
	{
//		DrawableInfo *drawInfo=(DrawableInfo *)m_robj->Get_User_Data();
//		Drawable *draw = drawInfo->m_drawable;

//		if (strstr(draw->getTemplate()->getName().str(),"Right02") != 0)
//			draw = draw;	//debug code for China06 wacky bridge shadow

		// get the light
		Vector3 lightPosWorld=TheW3DShadowManager->getLightPosWorld(0);

		// check if object has a limit/clamp on shadow length and adjust light
		// position of necessary.
		if (m_shadowLengthScale)
		{	//Find light's distance from origin in xy plane
			Real lightXYDistance = sqrt(lightPosWorld.X*lightPosWorld.X + lightPosWorld.Y * lightPosWorld.Y);
			Real newZ=lightXYDistance*m_shadowLengthScale;

			if (newZ > lightPosWorld.Z)
			{	//clamped z component is higher than actual light position allows so adjust it.
				lightPosWorld.Z = newZ;
			}
		}

		// find maximum shadow length which will not cause any corners of the object's bounding box
		// to cast shadows that drop significantly below the object's base.  This will help avoid
		// artifacts when we have an object on a cliff/hill casting shadows onto the ground below.
		// We need this hack because the terrain does not cast shadows and looks weird when objects
		//	sitting on an incline cast shadows down below.
		Vector3 objPos=m_robj->Get_Position();
		Vector3 lastValidTerrainPoint = objPos;
		Real baseGroundHeight=objPos.Z;
		const AABoxClass &box=m_robj->Get_Bounding_Box();
		Vector3 lightRay,shadowRay;
		LineSegClass lineseg;
		CastResultStruct result;
		Vector3 Corners[4];

		RayCollisionTestClass raytest(lineseg,&result);

		//Get vertices of top of bounding box since they will generate the longest shadow
		Corners[0]=box.Center+box.Extent;	//top right corner
		Corners[1]=Corners[0];
		Corners[1].X -= 2.0f*box.Extent.X;		//top left corner
		Corners[2]=Corners[1];
		Corners[2].Y -= 2.0f*box.Extent.Y;		//bottom left corner
		Corners[3]=Corners[2];
		Corners[3].X += 2.0f*box.Extent.X;		//bottom right corner

		//find the corner that causes the longest shadow projection
		//and clamp light position to make sure it falls on even ground about
		//the same height as the object's base.
		for (Int i=0; i<4; i++)
		{
			//Cast ray from top volume corners onto ground plane
			lightRay = Corners[i] - lightPosWorld;	//vector light to corner
			lightRay.Normalize();

			raytest.Ray.Set(Corners[i],Corners[i]+lightRay*MAX_EXTRUSION_LENGTH);
			result.Reset();

			//find out where this ray intersects terrain.
			if (TheTerrainRenderObject->Cast_Ray(raytest) && !raytest.Result->StartBad)
			{	//Found intersection point where shadow has its maximum length.  Do a quick
				//search to see if terrain falls significantly below the height of the object
				//anywhere between the base and the intersection point.  If so, we either need
				//to extend shadow extrusion or make the light angle more vertical.

				shadowRay.Set(result.ContactPoint-Corners[i]);	//vector from object corner to terrain intersection.
				shadowRay.Z = 0;	//remove z-component since we'll be sampling along the xy plane.

				//walk along the shadow/light direction vector looking for large dips - indicating object
				//is on hill or cliff.
				Real len=shadowRay.Length();
				Int  numSteps=REAL_TO_INT_CEIL(len/SHADOW_SAMPLING_INTERVAL);
				Real stepSize = 1.0f/(Real)numSteps;
				Vector3 terrainPoint;

				Real t=stepSize;
				for (Int j=0; j<numSteps; j++)
				{
					terrainPoint = Corners[i] + shadowRay*t;
					terrainPoint.Z=0;	//ignore height
					
					Real terrainHeight=TheTerrainRenderObject->getHeightMapHeight(terrainPoint.X,terrainPoint.Y,NULL);
					if (terrainHeight < (objPos.Z - MAX_SHADOW_EXTRUSION_UNDER_OBJECT_BEFORE_CLAMP))	//check if terrain dips more than 10 units under object.
					{	
						if (j == 0)	//this is the initial point so object must be right on the edge of a cliff.
						{	baseGroundHeight = terrainHeight;	//force extrusion all the way down cliff.
							Real tanAngle=tan(OVERHANGING_OBJECT_CLAMP_ANGLE);	//clamp to about 89 degrees or close to vertical lightpos.
							setShadowLengthScale(tanAngle);	//update the clamp angle to shorted shadow enough so this corner on flat ground.
							break;
						}

						//Find ray from last valid terrain contact point to object box corner.
						Vector3 clampRay(Corners[i]-lastValidTerrainPoint);
						Real clampAngle=asin(clampRay.Z/clampRay.Length());
						if (clampAngle >= (PI/2.0f) || clampAngle <= 0)
							clampAngle = OVERHANGING_OBJECT_CLAMP_ANGLE;	//clamp to about 89 degrees or close to vertical lightpos.
						Real tanAngle=tan(clampAngle);
						if (tanAngle > m_shadowLengthScale)
							setShadowLengthScale(tanAngle);	//update the clamp angle to shorted shadow enough so this corner on flat ground.
						break;
					}

					if (terrainHeight < baseGroundHeight)
					{	baseGroundHeight = terrainHeight;	//point was below object but within safety margin so record it's position.
						lastValidTerrainPoint = terrainPoint;
						lastValidTerrainPoint.Z = baseGroundHeight;
					}

					t+=stepSize;
				}
			}
		}


		m_extraExtrusionPadding = objPos.Z - baseGroundHeight + SHADOW_EXTRUSION_BUFFER;

		DEBUG_ASSERTCRASH(m_extraExtrusionPadding <= (255.0f*MAP_HEIGHT_SCALE),("Warning: Volumetric Shadow UpdateOptimalExtrusionPadding too large"));
	}
}

// getRenderCost ============================================================
// Returns number of draw calls for this shadow.
// ============================================================================
#if defined(_DEBUG) || defined(_INTERNAL)	
void W3DVolumetricShadow::getRenderCost(RenderCost & rc) const
{
	Int drawCount = 0;

	if (m_geometry && m_isEnabled && !m_isInvisibleEnabled && TheGlobalData->m_useShadowVolumes)
	{
		Int i,j;

		HLodClass *hlod=(HLodClass *)m_robj;
		MeshClass *mesh;
		Int meshIndex;

		for( i = 0; i < MAX_SHADOW_LIGHTS; i++ )
		{
			for (j=0; j<m_geometry->getMeshCount(); j++)
			{
				meshIndex=m_geometry->getMesh(j)->m_meshRobjIndex;

				if (meshIndex >= 0)
					mesh = (MeshClass *)hlod->Peek_Lod_Model(0,meshIndex);
				else
					mesh = (MeshClass *)m_robj;

				if (mesh && mesh->Is_Not_Hidden_At_All())
						drawCount++;
			}
		}
	}

	rc.addShadowDrawCalls(drawCount*2);
}
#endif

/************************************ New Buffered Rendering Code ************************/
void W3DVolumetricShadow::RenderVolume(Int meshIndex, Int lightIndex)
{
	HLodClass *hlod=(HLodClass *)m_robj;
	MeshClass *mesh=NULL;

	//RenderDynamicMeshVolume and RenderMeshVolumeBounds lock these, and ReAcquireResources leaves
	//them null when the device refuses the allocation after a reset (it logs SHADOW VOLUME BUFFERS).
	//The projected decals locked their own null buffer that way after a resolution change in v2.4.0.
	if (shadowVertexBufferD3D == NULL || shadowIndexBufferD3D == NULL)
		return;

	Int meshRobjIndex=m_geometry->getMesh(meshIndex)->m_meshRobjIndex;

	if (meshRobjIndex >= 0)
		mesh = (MeshClass *)hlod->Peek_Lod_Model(0,meshRobjIndex);
	else
		mesh = (MeshClass *)m_robj;

	if (mesh)
	{
			//Skinned volumes were built in world space (see updateVolumes), so they render with an
			//identity world transform just like the skinned mesh itself.
			static const Matrix3D identityXform(1);
			const Matrix3D *meshXform = m_geometry->getMesh(meshIndex)->isSkin() ? &identityXform : &mesh->Get_Transform();

#ifdef SV_DEBUG_BOUNDS
			RenderMeshVolumeBounds(meshIndex,lightIndex, meshXform);
#endif
			if (m_shadowVolume[0][ meshIndex ]->GetFlags() & SHADOW_DYNAMIC)
				RenderDynamicMeshVolume(meshIndex,lightIndex,meshXform);
			else
				RenderMeshVolume(meshIndex,lightIndex,meshXform);
	}
}

void W3DVolumetricShadow::RenderMeshVolume(Int meshIndex, Int lightIndex, const Matrix3D *meshXform)
{
	Geometry *geometry;
	Int numVerts, numPolys, numIndex;

	//Get D3D Device used by W3D for quicker access.
	LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

	if (!m_pDev)
		return;

	geometry = m_shadowVolume[lightIndex][ meshIndex ];

	//
	// if our count is out of sync with our geometry data something
	// is wrong here
	//
	assert( geometry );

	// get geometry requirements
	numVerts = geometry->GetNumActiveVertex();
	numPolys = geometry->GetNumActivePolygon();
	numIndex = numPolys * 3;

	// reject shadows with no data
	if( numVerts == 0 || numPolys == 0 )
		return;

	Matrix4x4 mWorld(*meshXform);

	///@todo: W3D always does transpose on all of matrix sets.  Slow???  Better to hack view matrix.
	//
	// Set on the device rather than through DX8Wrapper on purpose.  The wrapper's setter writes its
	// own DX8Transforms copy, which Apply_Render_State_Changes and _Get_DX8_Transform both read,
	// and this pass sets a world matrix per mesh and never puts the old one back.  The Direct3D 11
	// backend is told separately, which is all it needs.
	Matrix4x4 mWorldTransposed = mWorld.Transpose();
	m_pDev->SetTransform(D3DTS_WORLD,(D3DMATRIX *)&mWorldTransposed);
	Direct3D11_Mirror_Transform(D3DTS_WORLD,(const float *)&mWorldTransposed);

	W3DBufferManager::W3DVertexBufferSlot *vbSlot=m_shadowVolumeVB[lightIndex][ meshIndex ];
	if (!vbSlot)
		return;
	const UnsignedInt vertexSize = vbSlot->m_VB->m_DX8VertexBuffer->FVF_Info().Get_FVF_Size();
	if (vbSlot->m_VB->m_DX8VertexBuffer->Get_DX8_Vertex_Buffer() != lastActiveVertexBuffer)
	{	lastActiveVertexBuffer=vbSlot->m_VB->m_DX8VertexBuffer->Get_DX8_Vertex_Buffer();
		m_pDev->SetStreamSource(0,lastActiveVertexBuffer,0,vertexSize);	//12 bytes per vertex.
	}

	// The stream is set behind DX8Wrapper's back above, so the Direct3D 11 backend is told about it
	// here.  Unconditionally, not only when the buffer changes: the backend's stream is set by
	// everything else that draws between two shadow volumes.
	Direct3D11_Mirror_Stream_Source(vbSlot->m_VB->m_DX8VertexBuffer->Get_DX11_Twin(), vertexSize, 0);

	DEBUG_ASSERTCRASH(vbSlot->m_size >= numVerts,("Overflowing Shadow Vertex Buffer Slot"));

	W3DBufferManager::W3DIndexBufferSlot *ibSlot=m_shadowVolumeIB[lightIndex][ meshIndex ];
	if (!ibSlot)
		return;

	DEBUG_ASSERTCRASH(ibSlot->m_size >= numIndex,("Overflowing Shadow Index Buffer Slot"));

	// D3D9 takes the base vertex index on the draw call, not here.
	m_pDev->SetIndices(ibSlot->m_IB->m_DX8IndexBuffer->Get_DX8_Index_Buffer());
	Direct3D11_Mirror_Indices(ibSlot->m_IB->m_DX8IndexBuffer->Get_DX11_Twin());

	if (DX8Wrapper::_Is_Triangle_Draw_Enabled())
	{
		Debug_Statistics::Record_DX8_Polys_And_Vertices(numPolys,numVerts,ShaderClass::_PresetOpaqueShader);
		if (!Direct3D11_Present_Is_Enabled())
			m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,vbSlot->m_start,0,numVerts,ibSlot->m_start,numPolys);
		Direct3D11_Draw_Indexed_Triangles(numIndex,ibSlot->m_start,vbSlot->m_start);
	}

}

void W3DVolumetricShadow::RenderDynamicMeshVolume(Int meshIndex, Int lightIndex, const Matrix3D *meshXform)
{
	Geometry *geometry;
	Int numVerts, numPolys, numIndex;
	SHADOW_DYNAMIC_VOLUME_VERTEX* pvVertices;
	UnsignedShort *pvIndices;

	//Get D3D Device used by W3D for quicker access.
	LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

	if (!m_pDev)
		return;


	geometry = m_shadowVolume[lightIndex][ meshIndex ];

	//
	// if our count is out of sync with our geometry data something
	// is wrong here
	//
	assert( geometry );

	// get geometry requirements
	numVerts = geometry->GetNumActiveVertex();
	numPolys = geometry->GetNumActivePolygon();
	numIndex = numPolys * 3;

	// reject shadows with no data
	if( numVerts == 0 || numPolys == 0 )
		return;


	DX11BufferLockClass vertexLock;
	const UnsignedInt vertexStride = sizeof(SHADOW_DYNAMIC_VOLUME_VERTEX);
	UnsignedInt vertexOffset;
	UnsignedInt vertexFlags;
	if (nShadowVertsInBuf > (SHADOW_VERTEX_SIZE-numVerts))	//check if room for model verts
	{	//flush the buffer by drawing the contents and re-locking again
		vertexOffset=0;
		vertexFlags=D3DLOCK_DISCARD;
		if (shadowVertexBufferD3D->Lock(0,numVerts*vertexStride,(void**)&pvVertices,D3DLOCK_DISCARD) != D3D_OK)
			return;
		nShadowVertsInBuf=0;
		nShadowStartBatchVertex=0;
	}
	else
	{	vertexOffset=nShadowVertsInBuf*vertexStride;
		vertexFlags=D3DLOCK_NOOVERWRITE;
		if (shadowVertexBufferD3D->Lock(vertexOffset,numVerts*vertexStride, (void**)&pvVertices,D3DLOCK_NOOVERWRITE) != D3D_OK)
			return;
	}

	// From here the fill writes into the twin's own block instead of the D3D9 pointer, and the
	// unlock below copies that range into both.  Without a twin Begin answers null and the fill
	// carries on writing where it always did.
	void *vertexTarget = vertexLock.Begin(shadowVertexTwin, pvVertices, vertexOffset,
		numVerts*vertexStride, vertexFlags);
	if (vertexTarget != NULL)
		pvVertices = (SHADOW_DYNAMIC_VOLUME_VERTEX *)vertexTarget;
#ifdef SV_DEBUG
	srand(0x1345465);
#endif
	if(pvVertices)
	{
#ifdef SV_DEBUG
		for (Int i=0; i<numVerts; i++)
		{
			(*((Vector3 *)pvVertices))=*geometry->GetVertex(i);	//cast is valid since both start with xyz
			pvVertices->diffuse=(rand()%255) | ((rand()%255)<<8) | ((rand()%255)<<16);
			pvVertices++;
		}
#else
		memcpy(pvVertices,geometry->GetVertex(0),numVerts*sizeof(SHADOW_DYNAMIC_VOLUME_VERTEX));
#endif
	}

	vertexLock.End();
	shadowVertexBufferD3D->Unlock();

	DX11BufferLockClass indexLock;
	UnsignedInt indexOffset;
	UnsignedInt indexFlags;
	if (nShadowIndicesInBuf > (SHADOW_INDEX_SIZE-numIndex))	//check if room for model verts
	{	//flush the buffer by drawing the contents and re-locking again
		indexOffset=0;
		indexFlags=D3DLOCK_DISCARD;
		if (shadowIndexBufferD3D->Lock(0,numIndex*sizeof(short),(void**)&pvIndices,D3DLOCK_DISCARD) != D3D_OK)
			return;
		nShadowIndicesInBuf=0;
		nShadowStartBatchIndex=0;
	}
	else
	{	indexOffset=nShadowIndicesInBuf*sizeof(short);
		indexFlags=D3DLOCK_NOOVERWRITE;
		if (shadowIndexBufferD3D->Lock(indexOffset,numIndex*sizeof(short), (void**)&pvIndices,D3DLOCK_NOOVERWRITE) != D3D_OK)
			return;
	}

	void *indexTarget = indexLock.Begin(shadowIndexTwin, pvIndices, indexOffset,
		numIndex*sizeof(short), indexFlags);
	if (indexTarget != NULL)
		pvIndices = (UnsignedShort *)indexTarget;

	try {
	if(pvIndices)
	{
		memcpy(pvIndices,geometry->GetPolygonIndex(0,(short *)pvIndices),numPolys*3*sizeof(short));
	}
	IndexBufferExceptionFunc();
	} catch(...) {
		IndexBufferExceptionFunc();
	}

	indexLock.End();
	shadowIndexBufferD3D->Unlock();

	// D3D9 takes the base vertex index on the draw call, not here.
	m_pDev->SetIndices(shadowIndexBufferD3D);
	Direct3D11_Mirror_Indices(shadowIndexTwin);

	Matrix4x4 mWorld(*meshXform);
	Matrix4x4 mWorldTransposed = mWorld.Transpose();

	m_pDev->SetTransform(D3DTS_WORLD,(D3DMATRIX *)&mWorldTransposed);
	Direct3D11_Mirror_Transform(D3DTS_WORLD,(const float *)&mWorldTransposed);

	if (shadowVertexBufferD3D != lastActiveVertexBuffer)
	{	m_pDev->SetStreamSource(0,shadowVertexBufferD3D,0,vertexStride);
		lastActiveVertexBuffer = shadowVertexBufferD3D;
	}
	Direct3D11_Mirror_Stream_Source(shadowVertexTwin, vertexStride, 0);

	if (DX8Wrapper::_Is_Triangle_Draw_Enabled())
	{
		Debug_Statistics::Record_DX8_Polys_And_Vertices(numPolys,numVerts,ShaderClass::_PresetOpaqueShader);
		if (!Direct3D11_Present_Is_Enabled())
			m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,nShadowStartBatchVertex,0,numVerts,nShadowStartBatchIndex,numPolys);
		Direct3D11_Draw_Indexed_Triangles(numIndex,nShadowStartBatchIndex,nShadowStartBatchVertex);
	}

	nShadowVertsInBuf += numVerts;
	nShadowStartBatchVertex=nShadowVertsInBuf;

	nShadowIndicesInBuf += numIndex;
	nShadowStartBatchIndex=nShadowIndicesInBuf;
}

/** Debug function to draw bounding boxes around shadow volumes */
void W3DVolumetricShadow::RenderMeshVolumeBounds(Int meshIndex, Int lightIndex, const Matrix3D *meshXform)
{
	Geometry *geometry;
	Int numVerts, numPolys, numIndex;
	SHADOW_DYNAMIC_VOLUME_VERTEX* pvVertices;
	UnsignedShort *pvIndices;
	// Vertex Positions as a function of the box extents
	static Vector3						_BoxVerts[8] = 
	{
		Vector3(  1.0f, 1.0f, 1.0f ),		// +z ring of 4 verts
		Vector3( -1.0f, 1.0f, 1.0f ),
		Vector3( -1.0f,-1.0f, 1.0f ),
		Vector3(  1.0f,-1.0f, 1.0f ),

		Vector3(  1.0f, 1.0f,-1.0f ),		// -z ring of 4 verts;
		Vector3( -1.0f, 1.0f,-1.0f ),
		Vector3( -1.0f,-1.0f,-1.0f ),
		Vector3(  1.0f,-1.0f,-1.0f ),
	};
	// Face Connectivity
	static Vector3i					_BoxFaces[12] = 
	{
		Vector3i( 0,1,2 ),		// +z faces
		Vector3i( 0,2,3 ),		
		Vector3i( 4,7,6 ),		// -z faces
		Vector3i( 4,6,5 ),
		Vector3i( 0,3,7 ),		// +x faces
		Vector3i( 0,7,4 ),
		Vector3i( 1,5,6 ),		// -x faces
		Vector3i( 1,6,2 ),
		Vector3i( 4,5,1 ),		// +y faces
		Vector3i( 4,1,0 ),
		Vector3i( 3,2,6 ),		// -y faces
		Vector3i( 3,6,7 )
	};

	static Vector3 verts[8];

	//Get D3D Device used by W3D for quicker access.
	LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

	if (!m_pDev)
		return;

	Vector3 meshPosition;
	meshXform->Get_Translation(&meshPosition);	//current mesh position

	geometry = m_shadowVolume[lightIndex][ meshIndex ];
	AABoxClass &aab=geometry->getBoundingBox();

	// compute the vertex positions
	meshPosition += aab.Center;	//get world space position of bounding box

	for (int ivert=0; ivert<8; ivert++)
	{
		verts[ivert].X = meshPosition.X + _BoxVerts[ivert][0] * aab.Extent.X;
		verts[ivert].Y = meshPosition.Y + _BoxVerts[ivert][1] * aab.Extent.Y;
		verts[ivert].Z = meshPosition.Z + _BoxVerts[ivert][2] * aab.Extent.Z;
	}

	//
	// if our count is out of sync with our geometry data something
	// is wrong here
	//
	assert( geometry );

	// get geometry requirements
	numVerts = 8;
	numPolys = 12;
	numIndex = numPolys * 3;

	// reject shadows with no data
	if( numVerts == 0 || numPolys == 0 )
		return;


	if (nShadowVertsInBuf > (SHADOW_VERTEX_SIZE-numVerts))	//check if room for model verts
	{	//flush the buffer by drawing the contents and re-locking again
		if (shadowVertexBufferD3D->Lock(0,numVerts*sizeof(SHADOW_DYNAMIC_VOLUME_VERTEX),(void**)&pvVertices,D3DLOCK_DISCARD) != D3D_OK)
			return;
		nShadowVertsInBuf=0;
		nShadowStartBatchVertex=0;
	}
	else
	{	if (shadowVertexBufferD3D->Lock(nShadowVertsInBuf*sizeof(SHADOW_DYNAMIC_VOLUME_VERTEX),numVerts*sizeof(SHADOW_DYNAMIC_VOLUME_VERTEX), (void**)&pvVertices,D3DLOCK_NOOVERWRITE) != D3D_OK)
			return;
	}
	srand(0x1345465);
	if(pvVertices)
	{	for (Int i=0; i<8; i++)
		{
			pvVertices->x=verts[i][0];
			pvVertices->y=verts[i][1];
			pvVertices->z=verts[i][2];
#ifdef SV_DEBUG
			pvVertices->diffuse=(rand()%255) | ((rand()%255)<<8) | ((rand()%255)<<16);
#endif
			pvVertices++;
		}
	}

	shadowVertexBufferD3D->Unlock();

	if (nShadowIndicesInBuf > (SHADOW_INDEX_SIZE-numIndex))	//check if room for model verts
	{	//flush the buffer by drawing the contents and re-locking again
		if (shadowIndexBufferD3D->Lock(0,numIndex*sizeof(short),(void**)&pvIndices,D3DLOCK_DISCARD) != D3D_OK)
			return;;
		nShadowIndicesInBuf=0;
		nShadowStartBatchIndex=0;
	}
	else
	{	if (shadowIndexBufferD3D->Lock(nShadowIndicesInBuf*sizeof(short),numIndex*sizeof(short), (void**)&pvIndices,D3DLOCK_NOOVERWRITE) != D3D_OK)
			return;
	}


	if(pvIndices)
	{
		for (Int i=0; i<numPolys; i++,pvIndices+=3)
		{
			pvIndices[0] = _BoxFaces[i][0];
			pvIndices[1] = _BoxFaces[i][1];
			pvIndices[2] = _BoxFaces[i][2];
		}
	}

	shadowIndexBufferD3D->Unlock();

	// D3D9 takes the base vertex index on the draw call, not here.
	m_pDev->SetIndices(shadowIndexBufferD3D);


	//todo: replace this with mesh transform
	Matrix4x4 mWorld(1);	//identity since boxes are pre-transformed to world space.

	Matrix4x4 mWorldT = mWorld.Transpose();	// a named copy: ISO C++ takes no address of a temporary
	m_pDev->SetTransform(D3DTS_WORLD,(D3DMATRIX *)&mWorldT);
	
	m_pDev->SetStreamSource(0,shadowVertexBufferD3D,0,sizeof(SHADOW_DYNAMIC_VOLUME_VERTEX));
	m_pDev->SetFVF(SHADOW_DYNAMIC_VOLUME_FVF);

	if (!Direct3D11_Present_Is_Enabled())
		m_pDev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,nShadowStartBatchVertex,0,numVerts,nShadowStartBatchIndex,numPolys);

	nShadowVertsInBuf += numVerts;
	nShadowStartBatchVertex=nShadowVertsInBuf;

	nShadowIndicesInBuf += numIndex;
	nShadowStartBatchIndex=nShadowIndicesInBuf;
}

// Shadow =====================================================================
// Shadow default constructor
// ============================================================================
W3DVolumetricShadow::W3DVolumetricShadow( void )
{
	Int i,j;

	m_next = NULL;
	m_geometry = NULL;
	m_shadowLengthScale = 0.0f;
	m_extraExtrusionPadding = 0.0f;
	m_robj = NULL;
	m_isEnabled = TRUE;
	m_isInvisibleEnabled = FALSE;

	for (j=0; j < MAX_SHADOW_CASTER_MESHES; j++)
	{	m_numSilhouetteIndices[j] = 0;
		m_maxSilhouetteEntries[j] = 0;
		m_silhouetteIndex[j] = NULL;
		m_shadowVolumeCount[j] = 0;
	}

	for( i = 0; i < MAX_SHADOW_LIGHTS; i++ )
	{
		for (j=0; j < MAX_SHADOW_CASTER_MESHES; j++)
		{
			m_shadowVolume[ i ][j] = NULL;
			m_shadowVolumeVB[i][j] = NULL;
			m_shadowVolumeIB[i][j] = NULL;
			m_shadowVolumeRenderTask[i][j].m_parentShadow = this;
			m_shadowVolumeRenderTask[i][j].m_meshIndex = (UnsignedByte)j;
			m_shadowVolumeRenderTask[i][j].m_lightIndex = (UnsignedByte)i;
			m_objectXformHistory[ i ][j].Make_Identity();
			m_lightPosHistory[ i ][j] = Vector3(0,0,0);
			m_skinRebuiltOnFrame[ i ][j] = 0;
		}
	}  // end for i

}  // end W3DVolumetricShadow

// ~W3DVolumetricShadow ====================================================================
// W3DVolumetricShadow destructor
// ============================================================================
W3DVolumetricShadow::~W3DVolumetricShadow( void )
{
	Int i,j;

	// we must free any silhouette data allocated
	for (j = 0; j < MAX_SHADOW_CASTER_MESHES; j++)
		deleteSilhouette(j);

	// free any shadow volume data
	for( i = 0; i < MAX_SHADOW_LIGHTS; i++ )
	{	for (j = 0; j < MAX_SHADOW_CASTER_MESHES; j++)
		{	if( m_shadowVolume[ i ][j] )
				delete m_shadowVolume[ i ][j];
			if( m_shadowVolumeVB[i][j])
				TheW3DBufferManager->releaseSlot(m_shadowVolumeVB[i][j]);
			if( m_shadowVolumeIB[i][j])
				TheW3DBufferManager->releaseSlot(m_shadowVolumeIB[i][j]);
		}
	}

	if (m_geometry)
		REF_PTR_RELEASE(m_geometry);

	m_geometry=NULL;
	m_robj=NULL;

}  // end ~W3DVolumetricShadow

void W3DVolumetricShadow::SetGeometry( W3DShadowGeometry *geometry )
{

	Short numPrevVertices = 0;
	Short numNewVertices = 0;

	//
	// our geometry has changed, we need to allocate enough memory for the
	// silhouette data.  If silhouette data is present it must be reallocated
	// to accomoddate the new size if smaller
	//

	// if we had previous geometry how many vertices did it have

	for (Int i=0; i<MAX_SHADOW_CASTER_MESHES; i++)
	{
		if( m_geometry )
			numPrevVertices = m_geometry->getMesh(i)->GetNumVertex();

		// now many vertices does our new geometry have
		if( geometry )
			numNewVertices = geometry->getMesh(i)->GetNumVertex();

		//
		// TODO: Colin, may want to change this in the future
		// if our new geometry requires more memory allocate it, if it requires
		// less we'll leave it around for future switches in geometry
		//
		if( numNewVertices > numPrevVertices )
		{

			deleteSilhouette(i);
			if( allocateSilhouette(i, numNewVertices ) == FALSE )
				return;

		}  // end if
	}

	// assign the new geometry, possible over an old geometry
	m_geometry = geometry;

}  // end SetGeometry

/**Called once per frame for each object, when necessary it will reconstruct
 the shadow volume for this shadow from the silhouette of the geometry
 and any light sources
*/
void W3DVolumetricShadow::Update()
{
	static Int currentTime, lastTime, delay = 0;
	// OBJECT_PILE
	// static Vector3 originCompareVector(0,0,9999);
	static Vector3 originCompareVector(0,0,0);
	Vector3 pos;

	// sanity
	if( m_geometry == NULL)
		return;

	//
	// for now we will just rebuild a shadow volume every so often, this
	// should be changed to be built only when the light angle sufficiently
	// changes or when the object rotation sufficiently changes
	//

//	currentTime = timeGetTime();
//	if( currentTime - lastTime >= delay )
	{
		pos=m_robj->Get_Position();
		if (pos == originCompareVector)
		{	//the transform on this object was never set so we can't make any determination
			//if it's visible or what the shadow looks like.
			return;
		}
		//Check if this is a "flying" unit.  Flying units are defined as anything that moves more than
		//AIRBORNE_UNIT_GROUND_DELTA world units above ground.  This will allow "jumping" units like rocket
		//buggies from being flagged as "flying".  We minimize the number of flying units because their shadow
		//volumes are extruded longer to reach the lowest point on the map.  Regular shadows only extend far
		//enough to reach the base of the model (presumes base is already touching the ground).
		Real groundHeight; 
		if (TheTerrainLogic)
			groundHeight=TheTerrainLogic->getGroundHeight(pos.X,pos.Y);	//logic knows about bridges so use if available.
		else
			groundHeight=TheTerrainRenderObject->getHeightMapHeight(pos.X,pos.Y, NULL);
   		if (fabs(pos.Z - groundHeight) >= AIRBORNE_UNIT_GROUND_DELTA)
   		{	
 			Real extent = MAX_SHADOW_LENGTH_EXTRA_AIRBORNE_SCALE_FACTOR * m_robjExtent;

			//a shadow cast by the real sun lands off to one side of the aircraft, as far out as its
			//height asks for, so an aircraft just off screen can still have its shadow on it
			const Vector3 &lightPos = TheW3DShadowManager->getLightPosWorld(0);
			const Real lightXY = sqrt(lightPos.X*lightPos.X + lightPos.Y*lightPos.Y);
			Real lightZ = lightPos.Z;
			if (lightXY * m_shadowLengthScale > lightZ)
				lightZ = lightXY * m_shadowLengthScale;
			if (lightZ > 0.0f)
				extent += fabs(pos.Z - groundHeight) * lightXY / lightZ;
 			if (WWMath::Fabs(pos.X - bcX) > (beX + extent) ||
 				WWMath::Fabs(pos.Y - bcY) > (beY + extent) ||
 				WWMath::Fabs(pos.Z - bcZ) > (beZ + extent))
			{
				if (!volumeShadowCanReachView( *shadowCameraFrustum, m_robj->Get_Bounding_Sphere(), TheTerrainRenderObject->getMinHeight() ))
					return;	//shadow can't be visible so no point in updating.
			}

			//this unit is above ground, extend shadow volume to reach lowest point on the terrain plus extra bit to make
			//sure shadow goes under ground.
   			updateVolumes(fabs(pos.Z - TheTerrainRenderObject->getMinHeight()) + SHADOW_EXTRUSION_BUFFER);
   		}
   		else
 		{	//normal object that is not floating above ground so we don't need to extend the shadow lower than the object's
			//base since it should be sitting directly at ground level.

			/* The box around the visible terrain plus the caster's own radius is quick, and it is not
				 enough: a building's shadow under a low sun runs further than the building is wide, and one
				 standing just past that box laid its shadow in view and lost it all at once.  What fails
				 the box gets the slower question, whether its shadow can reach the view at all. */
 			if (WWMath::Fabs(pos.X - bcX) > (beX + m_robjExtent) ||
 				WWMath::Fabs(pos.Y - bcY) > (beY + m_robjExtent) ||
 				WWMath::Fabs(pos.Z - bcZ) > (beZ + m_robjExtent))
			{
				if (!volumeShadowCanReachView( *shadowCameraFrustum, m_robj->Get_Bounding_Sphere(), TheTerrainRenderObject->getMinHeight() ))
					return;	//shadow can't be visible so no point in updating.
			}
 
				//check if this object has never had it's extrusion length updated.  Will only be true for
				//immobile objects because finding an optimal extrusion length is expensive.
				if (!m_extraExtrusionPadding)
					updateOptimalExtrusionPadding();

   			updateVolumes(m_extraExtrusionPadding);
 		}


	//	floorZ = 2.0f;	//lower slightly so shadows go under ground.

	
		// update delay time
		lastTime = currentTime;
	}  // end if

}  // end Update

/** Update shadow volumes belonging to all meshes of this shadow caster.
*	Use zoffset to extend shadows below object's base by given amount.
*/
void W3DVolumetricShadow::updateVolumes(Real zoffset)
{
	Int i,j;

	HLodClass *hlod=(HLodClass *)m_robj;
	MeshClass *mesh;
	static AABoxClass aaBox;
	static SphereClass sphere;
	Int meshIndex;
	//A skinned mesh is drawn with an identity world transform - the skinning already put its
	//vertices in world space - so its shadow volume is built and rendered in world space too.
	static const Matrix3D identityXform(1);

	DEBUG_ASSERTCRASH(hlod != NULL,("updateVolumes : hlod is NULL!"));

	Bool parentVis=m_robj->Is_Really_Visible();

	for( i = 0; i < MAX_SHADOW_LIGHTS; i++ )
	{
		for (j=0; j<m_geometry->getMeshCount(); j++)
		{
			meshIndex=m_geometry->getMesh(j)->m_meshRobjIndex;

			if (meshIndex >= 0)
				mesh = (MeshClass *)hlod->Peek_Lod_Model(0,meshIndex);
			else
				mesh = (MeshClass *)m_robj;

			if (mesh) 
			{	
				if (!mesh->Is_Not_Hidden_At_All())
					continue;

				const Matrix3D *meshXform = m_geometry->getMesh(j)->isSkin() ? &identityXform : &mesh->Get_Transform();

				/**@todo: Getting the transform of the mesh may be forcing a full hierarchy evaluation.
					Expensive for off-screen models... do we really need this?	*/
				//Extend floor of model by 'zoffset' to compensate for flying units.
				updateMeshVolume(j, i, mesh, meshXform, mesh->Get_Bounding_Box(),m_robj->Get_Position().Z - zoffset);
				//update visibility if not set yet
				if (m_shadowVolume[i][j])
				{
					if(m_shadowVolume[i][j]->getVisibleState() ==	Geometry::STATE_UNKNOWN)
					{	//Updating the mesh volume didn't update the visibility, must do it here.
						//First check against bounding sphere
						if (parentVis)
 						{ //parent is visible so most likely all sub_objects are also visible so probably all shadows also visible.
 						  //skip additional visibility tests
 							m_shadowVolume[i][j]->setVisibleState(Geometry::STATE_VISIBLE);
 						}
 						else
						{	sphere=m_shadowVolume[i][j]->getBoundingSphere();
							sphere.Center += meshXform->Get_Translation();
							CollisionMath::OverlapType result=CollisionMath::Overlap_Test(*shadowCameraFrustum,sphere);
							if (result == CollisionMath::OVERLAPPED)
							{	//do a more accurate test against bounding box.
								aaBox=m_shadowVolume[i][j]->getBoundingBox();
								aaBox.Translate(meshXform->Get_Translation());	//translate bounding box to world space.
								if (CollisionMath::Overlap_Test(*shadowCameraFrustum,aaBox) != CollisionMath::OUTSIDE)
									m_shadowVolume[i][j]->setVisibleState(Geometry::STATE_VISIBLE);
								else
									m_shadowVolume[i][j]->setVisibleState(Geometry::STATE_INVISIBLE);
							}
							else
								m_shadowVolume[i][j]->setVisibleState((Geometry::VisibleState)result);
						}
					}
					if (m_shadowVolume[i][j]->getVisibleState() ==	Geometry::STATE_VISIBLE)
					{	//shadow volume is visible.  Add it to list of rendertasks.
						W3DBufferManager::W3DVertexBufferSlot *vbSlot=m_shadowVolumeVB[i][j];
						if (vbSlot)
						{	//add to static mesh volume list.
							W3DBufferManager::W3DRenderTask *oldTask=vbSlot->m_VB->m_renderTaskList;
							vbSlot->m_VB->m_renderTaskList=&m_shadowVolumeRenderTask[i][j];
							vbSlot->m_VB->m_renderTaskList->m_nextTask=oldTask;
						}
						else
						{
							TheW3DVolumetricShadowManager->addDynamicShadowTask(&m_shadowVolumeRenderTask[i][j]);
						}
					}
				}
			}
		}	// end for j
	}  // end for, i
}

//Scratch used while a skinned mesh's silhouette is rebuilt.  It is grown on demand and shared by
//every skinned caster: only one mesh is ever posed at a time, and the data is handed straight to the
//silhouette/volume builders and dropped again before the next mesh is touched.  It deliberately does
//not live in W3DShadowGeometryMesh - that object is cached by model name and shared by every unit
//using the model, so per-instance pose data must never be stored in it.
static Vector3 *skinPosedVerts = NULL;
static Vector3 *skinPosedNormals = NULL;
static Int skinPosedVertsSize = 0;
static Int skinPosedNormalsSize = 0;

void W3DVolumetricShadow::releaseSkinScratch(void)
{
	delete [] skinPosedVerts;
	delete [] skinPosedNormals;
	skinPosedVerts = NULL;
	skinPosedNormals = NULL;
	skinPosedVertsSize = 0;
	skinPosedNormalsSize = 0;
}

/**Pose a skinned mesh into the shared scratch buffers and point the geometry's vertex/normal
accessors at them.  Get_Deformed_Vertices() hands back world-space positions - the same ones the
renderer draws with an identity world transform - so the silhouette and the extruded volume come out
in world space.  Returns FALSE if the scratch could not be grown, in which case nothing was posed.*/
Bool W3DVolumetricShadow::poseSkinMesh(W3DShadowGeometryMesh *geomMesh, MeshClass *mesh)
{
	Int numVerts = geomMesh->getNumSourceVerts();
	Int numPolys = geomMesh->GetNumPolygon();

	if (numVerts <= 0 || numPolys <= 0 || mesh == NULL)
		return FALSE;

	//The polygon indices were welded against this exact vertex array.  If the mesh we are being asked
	//to pose is a different one (a different LOD, say) the indices would run off the end of the
	//scratch, so bail instead of corrupting memory.
	if (mesh->Peek_Model() == NULL || mesh->Peek_Model()->Get_Vertex_Count() != numVerts)
	{	DEBUG_ASSERTCRASH(0, ("shadow skin vertex count changed under us"));
		return FALSE;
	}

	if (numVerts > skinPosedVertsSize)
	{	delete [] skinPosedVerts;
		skinPosedVerts = NEW Vector3[numVerts];
		skinPosedVertsSize = numVerts;
	}

	if (numPolys > skinPosedNormalsSize)
	{	delete [] skinPosedNormals;
		skinPosedNormals = NEW Vector3[numPolys];
		skinPosedNormalsSize = numPolys;
	}

	//world-space positions for the current animation frame
	mesh->Get_Deformed_Vertices(skinPosedVerts);

	//the accessors must already see the posed vertices while the face normals are computed
	geomMesh->setPosedData(skinPosedVerts, NULL);

	for (Int i=0; i<numPolys; i++)
	{
		short indexList[3];
		geomMesh->GetPolygonIndex(i,indexList);

		const Vector3& v0=geomMesh->GetVertex(indexList[0]);
		const Vector3& v1=geomMesh->GetVertex(indexList[1]);
		const Vector3& v2=geomMesh->GetVertex(indexList[2]);

		//same winding convention as buildPolygonNormal()
		Vector3 edge1=v1-v0;
		Vector3 edge2=v1-v2;
		Vector3::Normalized_Cross_Product(edge2,edge1,&skinPosedNormals[i]);
	}

	geomMesh->setPosedData(skinPosedVerts, skinPosedNormals);

	return TRUE;
}

/*floorZ is the assumed ground height below the model.  The code will try to extrude shadows just long enough to hit this point in order
to reduce fill rate usage.*/
void W3DVolumetricShadow::updateMeshVolume(Int meshIndex, Int lightIndex, MeshClass *mesh, const Matrix3D *meshXform, const AABoxClass &meshBox, float floorZ )
{
	Vector3 lightPosObject;
	Matrix4x4 worldToObject;
	Vector3 objectCenter;
	Vector3 toLight;
	Vector3 toPrevLight;
	Vector3 prevPosToLight;
	Vector3 lightPosWorld;
	Vector3 prevObjPosition;
	//Figuring out if mesh has rotated is cheaper (no normalization) than figuring out if light angle has changed.
	//So we divide the 2 tests.  Also, our light (sun) almost never moves so no second test needed at all.
	Bool isMeshRotating = false;	//flag if mesh has rotated since last update. Translation doesn't matter for infinite light source.
	Bool isLightMoving = false;	//flag if light has moved since last update.

	Matrix4x4 objectToWorld(*meshXform);
	Matrix4x4 *prevXForm=&m_objectXformHistory[ lightIndex ][meshIndex];
	W3DShadowGeometryMesh *geomMesh=m_geometry->getMesh(meshIndex);
	Bool isSkin=geomMesh->isSkin();

	//
	// build the shadow silhouette and construct shadow volume from
	// this light location.  The for loop wrapped around this is 
	// theoretical code for future enhancements of multiple lights that
	// cast shadows
	//

#ifdef CNC3 //(gth) numerical error requires that the axis vectors be normalized...

	//When dealing with infinite light sources, we can assume that the shadow doesn't
	//change much based on object position.  Only the orientation to light matters.
	Vector3 va = (Vector3 &)(*prevXForm)[0];
	Vector3 vb = (Vector3 &)objectToWorld[0];
	va.Normalize();
	vb.Normalize();
	Real cosAngle = WWMath::Fabs(Vector3::Dot_Product(va,vb));

	if (cosAngle >= cosAngleToCare)
	{	
		
		va = (Vector3 &)(*prevXForm)[1];
		vb = (Vector3 &)objectToWorld[1];
		va.Normalize();
		vb.Normalize();
		cosAngle = WWMath::Fabs(Vector3::Dot_Product(va,vb));

		if (cosAngle >= cosAngleToCare)
		{
			va = (Vector3 &)(*prevXForm)[2];
			vb = (Vector3 &)objectToWorld[2];
			va.Normalize();
			vb.Normalize();
			cosAngle = WWMath::Fabs(Vector3::Dot_Product(va,vb));
			if (cosAngle < cosAngleToCare)
				isMeshRotating=true;
		}
		else
			isMeshRotating =true;
	}
	else
		isMeshRotating =true;


#else // CNC3 (old generals code)

#ifdef ASSUME_NEAR_LIGHTSOURCE
	if (memcmp(&objectToWorld,prevXForm,sizeof(objectToWorld)))
		isMeshRotating = true; //mesh transform has not changed since last update.
#else
	//When dealing with infinite light sources, we can assume that the shadow doesn't
	//change much based on object position.  Only the orientation to light matters.
	Real cosAngle = fabs (Vector3::Dot_Product((Vector3 &)(prevXForm->operator [](0)),(Vector3 &)(objectToWorld.operator [](0))));
	if (cosAngle >= cosAngleToCare)
	{	cosAngle = fabs (Vector3::Dot_Product((Vector3 &)(prevXForm->operator [](1)),(Vector3 &)(objectToWorld.operator [](1))));
		if (cosAngle >= cosAngleToCare)
		{
			cosAngle = fabs (Vector3::Dot_Product((Vector3 &)(prevXForm->operator [](2)),(Vector3 &)(objectToWorld.operator [](2))));
			if (cosAngle < cosAngleToCare)
				isMeshRotating=true;
		}
		else
			isMeshRotating =true;
	}
	else
		isMeshRotating =true;
#endif	//near light source
#endif // CNC3

	// get the light
	lightPosWorld = TheW3DShadowManager->getLightPosWorld(lightIndex);

	// get the object
	meshXform->Get_Translation(&objectCenter);	//current mesh position

	// check if object has a limit/clamp on shadow length and adjust light
	// position of necessary.
	if (m_shadowLengthScale)
	{	//Find light's distance from origin in xy plane
		Real lightXYDistance = sqrt(lightPosWorld.X*lightPosWorld.X + lightPosWorld.Y * lightPosWorld.Y);
		Real newZ=lightXYDistance*m_shadowLengthScale;

		if (newZ > lightPosWorld.Z)
		{	//clamped z component is higher than actual light position allows so adjust it.
			lightPosWorld.Z = newZ;
		}
	}

	if (lightPosWorld != m_lightPosHistory[ lightIndex ][meshIndex])
	{	//Light position has moved, see if enough to matter

		// compute vector from the light to the current object position
		toLight = objectCenter - lightPosWorld;
		toLight.Normalize();

		// compute vector from the previous light to the object position
		toPrevLight = objectCenter - m_lightPosHistory[ lightIndex ][meshIndex];
		toPrevLight.Normalize();

		Real cosAngle = fabs (Vector3::Dot_Product(toLight,toPrevLight));
		if (cosAngle < cosAngleToCare)	//less than 45 degree change
			isLightMoving =true;
	}
	else
	///@todo: Find a better way to deal with this - use maximum extrusion once!  Also avoid hit for units climbing hills.
	if (fabs(objectCenter.Z - prevXForm->operator [](2).W) > SHADOW_EXTRUSION_BUFFER)
		isLightMoving = true;	//treat model rising just like rotation since volume needs update for longer extrusion.

	//A skinned mesh's vertices move with the skeleton while its transform stays put, so none of the
	//tests above can see the change - the silhouette has to be rebuilt every time.
	if (isSkin)
		isMeshRotating = true;

	/* That rebuild is the most expensive thing a shadow does, and an army of infantry asks for it
		 once per soldier per frame: 420 Rangers on screen were a tenth of the frame in shadows alone.
		 So a frame rebuilds at most SKIN_SHADOW_REBUILDS_PER_FRAME skinned volumes, and any past that
		 keep last frame's pose, but never one older than SKIN_SHADOW_MAX_AGE_FRAMES: a stale one is
		 rebuilt whatever the budget says, which bounds how far a shadow's arms can lag its body. The
		 volume still moves with the unit either way; only the pose waits. Drawing only, so the logic
		 never sees it. */
	if (isSkin && !isLightMoving && m_shadowVolume[ lightIndex ][meshIndex])
	{
		enum { SKIN_SHADOW_REBUILDS_PER_FRAME = 64, SKIN_SHADOW_MAX_AGE_FRAMES = 6 };
		static UnsignedInt s_budgetFrame = 0;
		static Int s_rebuildsThisFrame = 0;
		const UnsignedInt frame = WW3D::Get_Frame_Count();
		if (frame != s_budgetFrame)
		{
			s_budgetFrame = frame;
			s_rebuildsThisFrame = 0;
		}
		const Bool isStale = frame - m_skinRebuiltOnFrame[ lightIndex ][meshIndex] >= SKIN_SHADOW_MAX_AGE_FRAMES;
		if (!isStale && s_rebuildsThisFrame >= SKIN_SHADOW_REBUILDS_PER_FRAME)
			isMeshRotating = false;
		else
		{
			++s_rebuildsThisFrame;
			m_skinRebuiltOnFrame[ lightIndex ][meshIndex] = frame;
		}
	}

	// reconstruct if needed
	if (isLightMoving || isMeshRotating)
	{
		//
		// transform the light in the world to object space, we
		// care only about the rotation of components for the coordinate
		// system change, not the translations
		//
		Real det;
		D3DXMatrixInverse((D3DXMATRIX*)&worldToObject, &det, (D3DXMATRIX*)&objectToWorld);

		// find out light position in object space
		Matrix4x4::Transform_Vector(worldToObject,lightPosWorld,&lightPosObject);

		//Updating shadow volumes is expensive, so verify that this volume is even visible.

		//Generate bounding box around shadow volume by extruding AABB corners
		AABoxClass box(meshBox);	//copy current mesh bounding box (will be smaller than shadow box).
		SphereClass sphere;			//rough bounding sphere of shadow volume - based on box.
		Vector3 Corners[8];
		Vector3 lightRay;
		Real vectorScale,vectorScaleTemp, vectorScaleMax;
		Real length;

		//Get vertices of top of bounding box
		Corners[0]=box.Center+box.Extent;	//top right corner
		Corners[1]=Corners[0];
		Corners[1].X -= 2.0f*box.Extent.X;		//top left corner
		Corners[2]=Corners[1];
		Corners[2].Y -= 2.0f*box.Extent.Y;		//bottom left corner
		Corners[3]=Corners[2];
		Corners[3].X += 2.0f*box.Extent.X;		//bottom right corner

		//Project top volume corners onto ground plane
		lightRay = Corners[0] - lightPosWorld;	//vector light to corner
		length= 1.0f/lightRay.Length();
		lightRay *= length;
		vectorScaleMax=vectorScale=(Real)fabs((Corners[0].Z-floorZ)/lightRay.Z);	//length of vector from top corner to ground.
		Corners[4]=Corners[0]+lightRay*vectorScale;
		vectorScaleMax *= length;

		lightRay = Corners[1] - lightPosWorld;	//vector light to corner
		length= 1.0f/lightRay.Length();
		lightRay *= length;
		vectorScaleTemp=(Real)fabs((Corners[1].Z-floorZ)/lightRay.Z);	//length of vector from top corner to ground.
		Corners[5]=Corners[1]+lightRay*vectorScaleTemp;
		vectorScaleTemp *= length;

		if (vectorScaleTemp > vectorScaleMax)
			vectorScaleMax=vectorScaleTemp;	//keep track of maximum required extrusion length.

		lightRay = Corners[2] - lightPosWorld;	//vector light to corner
		length= 1.0f/lightRay.Length();
		lightRay *= length;
		vectorScale=(Real)fabs((Corners[2].Z-floorZ)/lightRay.Z);	//length of vector from top corner to ground.
		Corners[6]=Corners[2]+lightRay*vectorScale;
		vectorScale *= length;

		if (vectorScale > vectorScaleMax)
			vectorScaleMax=vectorScale;	//keep track of maximum required extrusion length.

		lightRay = Corners[3] - lightPosWorld;	//vector light to corner
		length= 1.0f/lightRay.Length();
		lightRay *= length;
		vectorScaleTemp=(Real)fabs((Corners[3].Z-floorZ)/lightRay.Z);	//length of vector from top corner to ground.
		Corners[7]=Corners[3]+lightRay*vectorScaleTemp;
		vectorScaleTemp *= length;

		if (vectorScaleTemp > vectorScaleMax)
			vectorScaleMax=vectorScaleTemp;	//keep track of maximum required extrusion length.

		box.Init(Corners, 8);	//generate a new bounding box
		sphere.Init(box.Center,box.Extent.Length());	//generate object space bounding sphere containing box.

		CollisionMath::OverlapType result=CollisionMath::Overlap_Test(*shadowCameraFrustum,sphere);
		if (result == CollisionMath::OVERLAPPED)	//do a more accurate test
			result=CollisionMath::Overlap_Test(*shadowCameraFrustum, box);
		
		if (result != CollisionMath::OUTSIDE)
		{
			//
			// reset the silhouette data and build a new one from this light
			// source perspective
			//

			if (isSkin)
			{	//pose the mesh into scratch; the accessors read it until it is cleared below.
				if (!poseSkinMesh(geomMesh, mesh))
					return;
			}
			else
			if (m_numSilhouetteIndices[meshIndex] != 0)
			{	//this silhouette was built before and is being updated.
				//this probably means it will change again in the future.
				//make future updates faster by pre-caching face normals.
				geomMesh->buildPolygonNormals();
			}
			resetSilhouette(meshIndex);
			buildSilhouette(meshIndex, &lightPosObject);

			//
			// in a multiple shadow situation we would be allocating a volume
			// for this current shadow light, not the 0 index volume all the time
			//
			if (!m_shadowVolume[ lightIndex ][meshIndex])
				allocateShadowVolume( lightIndex,meshIndex );
			if (isSkin && !(m_shadowVolume[ lightIndex ][meshIndex]->GetFlags() & SHADOW_DYNAMIC))
			{	//a skin is animated by definition - never bother with static vertex buffers
				m_shadowVolume[ lightIndex ][meshIndex]->SetFlags(
					m_shadowVolume[ lightIndex ][meshIndex]->GetFlags() | SHADOW_DYNAMIC);
				resetShadowVolume( lightIndex,meshIndex );
				allocateShadowVolume( lightIndex,meshIndex );	//now allocates system memory for the volume
			}
			if( m_shadowVolumeVB[ lightIndex ][meshIndex] )
			{	//Updating an existing vertex buffer shadow volume.  This means we're
				//probably dealing with an animated mesh.  Update flags to reflect this fact.
				if (isMeshRotating || isLightMoving)
				{
					if (isMeshRotating)
					{	//rotating meshes will most likely need updates each frame, so stop using static vertex buffers.
						m_shadowVolume[ lightIndex ][meshIndex]->SetFlags(
							m_shadowVolume[ lightIndex ][meshIndex]->GetFlags() | SHADOW_DYNAMIC);
					}
					//release memory used to store vertices/polygons
					resetShadowVolume( lightIndex,meshIndex );	//free vertex buffers since not used for dynamic.
					//Resize the shadow volume since we'll need room to store the vertices in memory instead of VB.
					allocateShadowVolume( lightIndex,meshIndex );
				}
			}

			//
			// construct the shadow volume at this light position in the
			// passed shadow volume geometry index
			//
			if (m_shadowVolume[ lightIndex ][meshIndex]->GetFlags() & SHADOW_DYNAMIC)
				constructVolume( &lightPosObject, vectorScaleMax, lightIndex, meshIndex );
			else
				constructVolumeVB( &lightPosObject, vectorScaleMax, lightIndex, meshIndex );

			if (isSkin)
				geomMesh->setPosedData(NULL,NULL);	//scratch is shared - never leave it hooked up

			//
			// store the current light position and orientation that
			// we constructed shadow info at
			//
			m_objectXformHistory[ lightIndex ][meshIndex] = objectToWorld;
			m_lightPosHistory[lightIndex][meshIndex] = lightPosWorld;

			box.Translate(-objectCenter);	//translate box to object space.
			m_shadowVolume[ lightIndex ][meshIndex]->setBoundingBox(box);
			sphere.Center -= objectCenter;
			m_shadowVolume[ lightIndex ][meshIndex]->setBoundingSphere(sphere);
			m_shadowVolume[ lightIndex ][meshIndex]->setVisibleState(Geometry::STATE_VISIBLE);	//this volume needs rendering.
		}//end if inside view frustum
		else
		if (m_shadowVolume[ lightIndex ][meshIndex])
		{	//outside view frustum, shadow wasn't updated.
			box.Translate(-objectCenter);	//translate box to object space.
			m_shadowVolume[ lightIndex ][meshIndex]->setBoundingBox(box);
			sphere.Center -= objectCenter;
			m_shadowVolume[ lightIndex ][meshIndex]->setBoundingSphere(sphere);
			m_shadowVolume[ lightIndex ][meshIndex]->setVisibleState(Geometry::STATE_INVISIBLE);
		}
	}  // end if
	else
	{	//not reconstructing volume, so don't know if visible or not.
		if (m_shadowVolume[ lightIndex ][meshIndex])
			m_shadowVolume[ lightIndex ][meshIndex]->setVisibleState(Geometry::STATE_UNKNOWN);
	}
}

// addSilhouetteEdge ==========================================================
// It has been determined that the polygon neighbor in the "neighborIndex"
// of "visible" needs to be added to the silhouette.  We will add those two
// vertex indices to the silhouette in the order they were specified in 
// "visible" to assure that the constructed edge is in counter clockwise order
// ============================================================================
void W3DVolumetricShadow::addSilhouetteEdge(Int meshIndex, PolyNeighbor *visible, PolyNeighbor *hidden )
{
	Int i;
	Int neighborIndex = 0;
	Short visibleIndexList[ 3 ];
	Short edgeStart, edgeEnd;

	W3DShadowGeometryMesh *geomMesh=m_geometry->getMesh(meshIndex);

	// sanity
	assert( visible && hidden );

	//
	// which index in the neighbor list of "visible" refers to the 
	// polygon "hidden"
	//
	for( i = 0; i < MAX_POLYGON_NEIGHBORS; i++ )
	{

		if( visible->neighbor[ i ].neighborIndex == hidden->myIndex )
		{

			neighborIndex = i;
			break;  // exit for

		}  // end if

	}  // end for i

	// get the three vertex indices of "visible"
	geomMesh->GetPolygonIndex( visible->myIndex, visibleIndexList );

	//
	// we know that 2 of the 3 vertex indices will be present in the edge.
	// will construct the edge as follows to ensure we have counter 
	// clockwise order.  note that this assumes the vertices of the
	// polygons specified in the geometry are in counter clockwise order,
	// which they are
	//
	// 1) [ v1  Absent, v2 Present, v3 Present ] -> edge = (v2, v3)
	// 2) [ v1 Present, v2  Absent, v3 Present ] -> edge = (v3, v1)
	// 3) [ v1 Present, v2 Present, v3 Absent  ] -> edge = (v1, v2)
	//
	if( (visibleIndexList[ 0 ] != 
			 visible->neighbor[ neighborIndex ].neighborEdgeIndex[ 0 ]) &&
			(visibleIndexList[ 0 ] != 
			visible->neighbor[ neighborIndex ].neighborEdgeIndex[ 1 ]) )
	{

		// case 1 above
		edgeStart = visibleIndexList[ 1 ];
		edgeEnd = visibleIndexList[ 2 ];

	}  // end if
	else if( (visibleIndexList[ 1 ] != 
					 visible->neighbor[ neighborIndex ].neighborEdgeIndex[ 0 ]) &&
					 (visibleIndexList[ 1 ] != 
					 visible->neighbor[ neighborIndex ].neighborEdgeIndex[ 1 ]) )
	{

		// case 2 above
		edgeStart = visibleIndexList[ 2 ];
		edgeEnd = visibleIndexList[ 0 ];

	}  // end if
	else
	{

		// case 3 above
		edgeStart = visibleIndexList[ 0 ];
		edgeEnd = visibleIndexList[ 1 ];

	}  // end if

	// add to silhouette edge list
	addSilhouetteIndices(meshIndex, edgeStart, edgeEnd );

}  // end addSilhouetteEdge

// addNeighborlessEdges =======================================================
// Given a polygon neighbor information, it has been determined that this
// polygon is visible and has edges which are not connected to other 
// polygons, these edges need to be added to the silhouette.  The edge(s)
// must be added in such an order that we create silhouette edges in a
// counter clockwise order.
// ============================================================================
void W3DVolumetricShadow::addNeighborlessEdges(Int meshIndex, PolyNeighbor *us )
{
	Short vertexIndexList[ 3 ];
	Int i, j;
	Short edgeStart, edgeEnd;
	Bool addEdge;

	// sanity
	assert( us );

	W3DShadowGeometryMesh *geomMesh = m_geometry->getMesh(meshIndex);

	// get the vertex index list from the geometry
	geomMesh->GetPolygonIndex( us->myIndex, vertexIndexList );

	//
	// go through each edge, if these indices to NOT appear TOGETHER in
	// neighbor list then we must add it.
	//
	for( i = 0; i < 3; i++ )
	{

		// get the edge start and end vertex indices
		edgeStart = vertexIndexList[ i ];
		if( i == 2 )
			edgeEnd = vertexIndexList[ 0 ];  // wraps to begging of list
		else
			edgeEnd = vertexIndexList[ i + 1 ];

		// do these two vertices appear in a neighbor list of the poly?
		addEdge = TRUE;
		for( j = 0; j < MAX_POLYGON_NEIGHBORS; j++ )
		{

			if( us->neighbor[ j ].neighborIndex != NO_NEIGHBOR )
			{

				if( (us->neighbor[ j ].neighborEdgeIndex[ 0 ] == edgeStart &&
						 us->neighbor[ j ].neighborEdgeIndex[ 1 ] == edgeEnd) ||
						(us->neighbor[ j ].neighborEdgeIndex[ 1 ] == edgeStart &&
						 us->neighbor[ j ].neighborEdgeIndex[ 0 ] == edgeEnd) )
				{

					addEdge = FALSE;
					break;  // exit for j, no need to search on

				}  // end if

			}  // end if

		}  // end for j

		// add the edge if no neighbors have that edge
		if( addEdge == TRUE )
		{

			addSilhouetteIndices(meshIndex, edgeStart, edgeEnd );

		}  // end if

	}  // end for i

}  // end addNeighborlessEdges

// addSilhouetteIndices =======================================================
// Add these two indices to the silhouette data
// ============================================================================
void W3DVolumetricShadow::addSilhouetteIndices(Int meshIndex, Short edgeStart, Short edgeEnd )
{

//	DBGPRINTF(( "addSilhouetteIndices: Adding (%d,%d), the storage before the add is = (%d/%d)\n",
//							edgeStart, edgeEnd, m_numSilhouetteIndices, m_maxSilhouetteEntries ));

	// add to silhouette edge list
	assert( m_numSilhouetteIndices[meshIndex] < m_maxSilhouetteEntries[meshIndex] );
	m_silhouetteIndex[meshIndex][ m_numSilhouetteIndices[meshIndex]++ ] = edgeStart;
	assert( m_numSilhouetteIndices[meshIndex] < m_maxSilhouetteEntries[meshIndex] );
	m_silhouetteIndex[meshIndex][ m_numSilhouetteIndices[meshIndex]++ ] = edgeEnd;

}  // end if

// buildSilhouette ============================================================
// Given a light position, and our polygon neighbor information this will
// build the silhouette of the object edges from the given light position
// ============================================================================
void W3DVolumetricShadow::buildSilhouette(Int meshIndex, Vector3 *lightPosObject)
{
	PolyNeighbor *polyNeighbor;  // the poly we're looking at right now
	Vector3 lightVector;  // vector from light to polygon
	Bool visibleNeighborless;
	Int numPolys;  // number of polys in our geometry
	W3DShadowGeometryMesh *geomMesh;
	Int i, j;
	Int meshEdgeStart=0; //index to first edge contributed by specific mesh

	//
	// go through each of our shadow geometry polygon info and find out
	// which polys are visible from this light source and which ones are not
	//

	geomMesh = m_geometry->getMesh(meshIndex);

	//record where this meshes indices will begin.
	meshEdgeStart=m_numSilhouetteIndices[meshIndex];

	numPolys = geomMesh->GetNumPolygon();
	for( i = 0; i < numPolys; i++ )
	{
		Short poly[ 3 ];

		// get this polygon neighbor information
		polyNeighbor = geomMesh->GetPolyNeighbor( i );

		// take this opportunity to initialize our processing flags to zero
		polyNeighbor->status = 0;

		// get the normal for this polygon
		const Vector3& normal=geomMesh->GetPolygonNormal(i);

		// get the vertex indices at this polygon
		geomMesh->GetPolygonIndex( i, poly );

		//
		// find out "lightVector" to this polygon
		//
		// since our light source could be very close to the object and that
		// would change the shadow we are going to say that the light vector
		// is from the light position to one of the vertices in the polygon.
		// To be more correct we should use the center of the polygon but
		// this is a good approximation ... an ever broader approximation that
		// we could use would be the object center
		//
		const Vector3& vertex=geomMesh->GetVertex( poly[ 0 ] );
		lightVector= vertex - *lightPosObject;

		//
		// dot the light vector with the normal of the polygon to see if the
		// poly is visible from this location
		//
		if( Vector3::Dot_Product( lightVector, normal ) < 0.0f )
			BitSet( polyNeighbor->status, POLY_VISIBLE );

	}  // end for i

	//
	// check all our polys using our poly neighbors, where one poly neighbor
	// is not the same visible status as a neighbor that represents a
	// silhouette edge
	//
	for( i = 0; i < numPolys; i++ )
	{
		PolyNeighbor *otherNeighbor;

		// get this poly neighbor ... this is "us"
		polyNeighbor = geomMesh->GetPolyNeighbor( i );

		// initialize ourselves to not be a visible edge
		visibleNeighborless = FALSE;

		// check our 3 potential neighbors
		for( j = 0; j < MAX_POLYGON_NEIGHBORS; j++ )
		{

			// initialize this neighbor to nuttin
			otherNeighbor = NULL;

			// get our neighbor if present and cull them if processed
			if( polyNeighbor->neighbor[ j ].neighborIndex != NO_NEIGHBOR )
			{

				// get the jth polygon neighbor ... this is "them"
				otherNeighbor = 
					geomMesh->GetPolyNeighbor( 
						polyNeighbor->neighbor[ j ].neighborIndex );

				//
				// ignore neighbors that are marked as processed as those
				// onces have already detected edges if present
				//
				if( BitTest( otherNeighbor->status, POLY_PROCESSED ) )
					continue;  // for j

			}  // end if

			//
			// finally, if our own visible status is different from our 
			// neighbor visible status then that defines an edge we must
			// add to the silhouette.  Also, a visible polygon that has
			// no neighbor automatically makes a silhouette edge.  Note that
			// if we have no neighbor we just record the fact that we have
			// real model end edges to add after this inner j loop;
			//
			if( BitTest( polyNeighbor->status, POLY_VISIBLE ) )
			{

				// check for no neighbor edges
				if( otherNeighbor == NULL )
				{

					visibleNeighborless = TRUE;

				}  // end if
				else if( BitTest( otherNeighbor->status, POLY_VISIBLE ) == FALSE )
				{

					// "we" are visible and "they" are not
					addSilhouetteEdge(meshIndex, polyNeighbor, otherNeighbor );

				}  // end if

			}  // end if
			else if( otherNeighbor != NULL &&
							 BitTest( otherNeighbor->status, POLY_VISIBLE ) )
			{

				// "they" are visible and "we" are not
				addSilhouetteEdge(meshIndex, otherNeighbor, polyNeighbor );

			}  // end else

		}  // end for j

		//
		// if this polygon is visible, add any edges that are not
		// neighbors of adjacent polygons.
		//
		if( visibleNeighborless == TRUE )
		{

			addNeighborlessEdges(meshIndex, polyNeighbor );

		}  // end if

		//
		// this polyNeighbor is now considered "processed", any other
		// polygons that reference back to this one can ignore their
		// processing cause any edges were already detected
		//
		BitSet( polyNeighbor->status, POLY_PROCESSED );

	}  // end for i
	
	//record number of edge indices contrinuted by this mesh
	m_numIndicesPerMesh[meshIndex]=m_numSilhouetteIndices[meshIndex]-meshEdgeStart;
	
}  // end buildSilhouette

// constructVolume ============================================================
// Given a fresh new geometry class called "shadowVolume" to hold the actual
// shadow volume data, this method will create the shadow volume polygons
// given the information in the current silhouette of this Shadow and the
// light source position.  
//
// The light source should be in object space and the shadow volume polygon 
// data is also constructed in object space.
//
// The polygon we will create for a given edge will be that edge extruded
// out in the direction away from the light source.  This conceptual 4 sided
// polygon is however broken up into two triangles for storage.
//
// This version is designed to construct the volume inside a system memory
// buffer - to be rendered via a dynamic vertex buffer.
//
// ============================================================================
void W3DVolumetricShadow::constructVolume( Vector3 *lightPosObject,Real shadowExtrudeDistance, Int volumeIndex, Int meshIndex )
{
	Geometry *shadowVolume;
	Vector3 extrude2;  // the polypoints extruded from edge and light
	Short indexList[ 3 ];
	Int i,k;
	Int vertexCount;
	Int polygonCount;
	Int indicesPerMesh;
	W3DShadowGeometryMesh *geomMesh;

	// sanity
	if( volumeIndex < 0 || 
			volumeIndex >= MAX_SHADOW_LIGHTS ||
			lightPosObject == NULL )
	{

		assert( 0 );
		return;

	}  // end if

	// get the geometry struct we're storing the actual shadow volume data in
	shadowVolume = m_shadowVolume[ volumeIndex ][meshIndex];

	if( shadowVolume == NULL )
	{

//		DBGPRINTF(( "No volume allocated at index '%d'\n", volumeIndex ));
		assert( 0 );
		return;

	}  // end if

	// step through each of the silhouette pairs
	vertexCount = 0;
	polygonCount = 0;

	indicesPerMesh=m_numIndicesPerMesh[meshIndex];
	if (!indicesPerMesh)
		return;	//nothing to draw

	geomMesh = m_geometry->getMesh(meshIndex);

	shadowVolume->SetNumActivePolygon(0);
	shadowVolume->SetNumActiveVertex(0);

#ifdef RECORD_SHADOW_STRIP_STATS
	Int numStrips=0;	//keeps track of number of strips generated.
	Int stripLength=1;	//keeps track of segments in strip (each being 2 triangles).
	Int maxStripLength=0;	//keeps track of longest strip generated.
#endif

	Short *silhouetteIndices=m_silhouetteIndex[meshIndex];

	//Initialize first strip info
	Short stripStartIndex=silhouetteIndices[ 0 ];
	Short stripStartVertex=0;

	//Insert the first vertex and extrusion into strip.
	// get edge point
	const Vector3& ev2=  // second edge of silhouette
		geomMesh->GetVertex( silhouetteIndices[ 0 ] );

	// take one edge point and extrude away from the light
	extrude2 = ev2 - *lightPosObject;
	extrude2 *= shadowExtrudeDistance;
	extrude2 += ev2;

	shadowVolume->SetVertex( vertexCount, &ev2 );
	shadowVolume->SetVertex( vertexCount + 1, &extrude2 );

	vertexCount=2;
	Int lastEdgeVertex2Index=0;
	Int lastExtrude2Index=1;

	for( i = 0; i < indicesPerMesh; i += 2 )
	{
		Short currentEdgeEnd=silhouetteIndices[i+1];

		//look for edge connected to this one and move it next to this edge
		//in index list.  Rendering edges back-to-back improves vertex cache usage.
		for (k=i+2; k<indicesPerMesh; k+=2)
			if (silhouetteIndices[k]==currentEdgeEnd)
			{	//swap the two edges
				Int tempIndex=*(Int *)(&silhouetteIndices[i+2]);
				*(Int *)&silhouetteIndices[i+2]=*(Int *)&silhouetteIndices[k];
				*(Int *)&silhouetteIndices[k]=tempIndex;
				break;
			}

		if (k >= indicesPerMesh)
		{	//reached end of strip. Insert final edge.
			//Check if last edge wraps around to start.
			if (currentEdgeEnd == stripStartIndex)
			{	//add end of strip that wraps around to start (forming closed cylinder/shape)
				//
				// add the polygon consisting of the two edge vertices and the
				// first extruded point
				//
				indexList[ 0 ] = lastEdgeVertex2Index;  // lastedgeVertex2 index
				indexList[ 1 ] = lastExtrude2Index;  // lastextrude2 index
				indexList[ 2 ] = stripStartVertex;  // edgeVertex2 index
				shadowVolume->SetPolygonIndex( polygonCount, indexList );

				indexList[ 0 ] = stripStartVertex;  // edgeVertex2 index
				indexList[ 1 ] = lastExtrude2Index;  // extrude1 index
				indexList[ 2 ] = stripStartVertex+1;  // extrude2 index
				shadowVolume->SetPolygonIndex( polygonCount + 1, indexList );
			}
			else
			{	//add end of strip.  Finishes the last 2 polygons.
				const Vector3& ev=geomMesh->GetVertex(currentEdgeEnd);
				shadowVolume->SetVertex( vertexCount, &ev );

				//
				// add the polygon consisting of the two edge vertices and the
				// first extruded point
				//
				indexList[ 0 ] = lastEdgeVertex2Index;  // lastedgeVertex2 index
				indexList[ 1 ] = lastExtrude2Index;  // lastextrude2 index
				indexList[ 2 ] = vertexCount;  // edgeVertex2 index
				shadowVolume->SetPolygonIndex( polygonCount, indexList );

				// take the other edge point and extrude away from light
				extrude2 = ev - *lightPosObject;
				extrude2 *= shadowExtrudeDistance;
				extrude2 += ev;
				// add the one new vertex
				shadowVolume->SetVertex( vertexCount + 1, &extrude2 );

				indexList[ 0 ] = vertexCount;  // edgeVertex2 index
				indexList[ 1 ] = lastExtrude2Index;  // extrude1 index
				indexList[ 2 ] = vertexCount+1;  // extrude2 index
				shadowVolume->SetPolygonIndex( polygonCount + 1, indexList );

				lastEdgeVertex2Index=vertexCount;
				lastExtrude2Index=vertexCount+1;
				vertexCount += 2;
			}

			if ((i+2) >= indicesPerMesh)
			{	//finished with all silhouette edges
				polygonCount += 2;
#ifdef RECORD_SHADOW_STRIP_STATS
				numStrips++;
#endif
				break;	//reached end of all edges
			}
	
			//Start a new strip by adding first vertex and extrusion.
			const Vector3& ev=geomMesh->GetVertex( silhouetteIndices[ i+2 ] );
			// take one edge point and extrude away from the light
			extrude2 = ev - *lightPosObject;
			extrude2 *= shadowExtrudeDistance;
			extrude2 += ev;

			lastEdgeVertex2Index=vertexCount;
			lastExtrude2Index=vertexCount + 1;
			//record start of new strip info
			stripStartIndex=silhouetteIndices[ i+2 ];
			stripStartVertex=lastEdgeVertex2Index;

			shadowVolume->SetVertex( lastEdgeVertex2Index, &ev );
			shadowVolume->SetVertex( lastExtrude2Index, &extrude2 );
			vertexCount += 2;

			polygonCount += 2;
#ifdef RECORD_SHADOW_STRIP_STATS
			numStrips++;
			stripLength=1;
#endif
			continue;
		}
		else
		{	//continue existing strip by adding extra vertex and extrusion

			const Vector3& ev=geomMesh->GetVertex( currentEdgeEnd );
			shadowVolume->SetVertex( vertexCount, &ev );
			//
			// add the polygon consisting of the two edge vertices and the
			// first extruded point
			//
			indexList[ 0 ] = lastEdgeVertex2Index;  // lastedgeVertex2 index
			indexList[ 1 ] = lastExtrude2Index;  // lastextrude2 index
			indexList[ 2 ] = vertexCount;  // edgeVertex2 index
			shadowVolume->SetPolygonIndex( polygonCount, indexList );

			// take the other edge point and extrude away from light
			extrude2 = ev - *lightPosObject;
			extrude2 *= shadowExtrudeDistance;
			extrude2 += ev;

			// add the one new vertex
			shadowVolume->SetVertex( vertexCount + 1, &extrude2 );

			indexList[ 0 ] = vertexCount;  // edgeVertex2 index
			indexList[ 1 ] = lastExtrude2Index;  // extrude1 index
			indexList[ 2 ] = vertexCount+1;  // extrude2 index
			shadowVolume->SetPolygonIndex( polygonCount + 1, indexList );

			lastEdgeVertex2Index=vertexCount;
			lastExtrude2Index=vertexCount+1;

			vertexCount += 2;
			polygonCount += 2;
		}
#ifdef RECORD_SHADOW_STRIP_STATS
		//Continuing strip.
		stripLength++;
		maxStripLength=__max(maxStripLength,stripLength);
#endif
	}

	shadowVolume->SetNumActivePolygon(polygonCount);
	shadowVolume->SetNumActiveVertex(vertexCount);
}  // end constructVolume

// constructVolumeVB ==========================================================
// Given a fresh new geometry class called "shadowVolume" to hold the actual
// shadow volume data, this method will create the shadow volume polygons
// given the information in the current silhouette of this Shadow and the
// light source position.  
//
// The light source should be in object space and the shadow volume polygon 
// data is also constructed in object space.
//
// The polygon we will create for a given edge will be that edge extruded
// out in the direction away from the light source.  This conceptual 4 sided
// polygon is however broken up into two triangles for storage.
//
// This version is designed to construct the volume directly inside a vertex
// buffer so it's optimal for static geometry.  Since it's assumed to be called
// only once per model, we can use some more expensive computations to generate
// the volume.
//
// ============================================================================
void W3DVolumetricShadow::constructVolumeVB( Vector3 *lightPosObject,Real shadowExtrudeDistance, Int volumeIndex, Int meshIndex )
{
	Geometry *shadowVolume;
	Vector3 extrude2;  // the polypoints extruded from edge and light
	Vector3 edgeVertex2;  // second edge of silhouette
	Int i,k;
	Int vertexCount;
	Int polygonCount;
	Int indicesPerMesh;
	W3DShadowGeometryMesh *geomMesh;

	W3DBufferManager::W3DVertexBufferSlot *vbSlot;
	W3DBufferManager::W3DIndexBufferSlot *ibSlot;

	// sanity
	if( volumeIndex < 0 || 
			volumeIndex >= MAX_SHADOW_LIGHTS ||
			lightPosObject == NULL )
	{

		assert( 0 );
		return;

	}  // end if

	// get the geometry struct we're storing the actual shadow volume data in
	shadowVolume = m_shadowVolume[ volumeIndex ][meshIndex];

	if( shadowVolume == NULL )
	{

//		DBGPRINTF(( "No volume allocated at index '%d'\n", volumeIndex ));
		assert( 0 );
		return;

	}  // end if

	//*****************************************************************************************/
	//Do an initial pass through silhouette data to determine the actual vertex/polygon counts.
	//This number can't be determined any other way since it depends on degree of vertex sharing
	//in model.  We don't want to overallocate because vertex buffer space is limited.
	//This pass is also used to sort the edges so they are all connected in strip order.
	{
		#ifdef RECORD_SHADOW_STRIP_STATS
			Int numStrips=0;	//keeps track of number of strips generated.
			Int stripLength=1;	//keeps track of segments in strip (each being 2 triangles).
			Int maxStripLength=0;	//keeps track of longest strip generated.
		#endif

		// step through each of the silhouette pairs
		vertexCount = 0;
		polygonCount = 0;

		indicesPerMesh=m_numIndicesPerMesh[meshIndex];
		if (!indicesPerMesh)
			return;	//nothing to draw

		Short *silhouetteIndices=m_silhouetteIndex[meshIndex];

		//Initialize first strip info
		Short stripStartIndex=silhouetteIndices[ 0 ];
		Short stripStartVertex=0;

		vertexCount=2;
		Int lastEdgeVertex2Index=0;
		Int lastExtrude2Index=1;

		for( i = 0; i < indicesPerMesh; i += 2 )
		{
			Short currentEdgeEnd=silhouetteIndices[i+1];

			//look for edge connected to this one and move it next to this edge
			//in index list.  Rendering edges back-to-back improves vertex cache usage.
			for (k=i+2; k<indicesPerMesh; k+=2)
				if (silhouetteIndices[k]==currentEdgeEnd)
				{	//swap the two edges
					Int tempIndex=*(Int *)(&silhouetteIndices[i+2]);
					*(Int *)&silhouetteIndices[i+2]=*(Int *)&silhouetteIndices[k];
					*(Int *)&silhouetteIndices[k]=tempIndex;
					break;
				}

			if (k >= indicesPerMesh)
			{	//reached end of strip. Insert final edge.
				//Check if last edge wraps around to start.
				if (currentEdgeEnd == stripStartIndex)
				{	//add end of strip that wraps around to start (forming closed cylinder/shape)
					//
					// add the polygon consisting of the two edge vertices and the
					// first extruded point
					//
				}
				else
				{	//add end of strip.  Finishes the last 2 polygons.
					lastEdgeVertex2Index=vertexCount;
					lastExtrude2Index=vertexCount+1;
					vertexCount += 2;
				}

				if ((i+2) >= indicesPerMesh)
				{	//finished with all silhouette edges
					polygonCount += 2;
	#ifdef RECORD_SHADOW_STRIP_STATS
					numStrips++;
	#endif
					break;	//reached end of all edges
				}
		
				lastEdgeVertex2Index=vertexCount;
				lastExtrude2Index=vertexCount + 1;
				//record start of new strip info
				stripStartIndex=silhouetteIndices[ i+2 ];
				stripStartVertex=lastEdgeVertex2Index;

				vertexCount += 2;

				polygonCount += 2;
	#ifdef RECORD_SHADOW_STRIP_STATS
				numStrips++;
				stripLength=1;
	#endif
				continue;
			}
			else
			{	//continue existing strip by adding extra vertex and extrusion

				lastEdgeVertex2Index=vertexCount;
				lastExtrude2Index=vertexCount+1;

				vertexCount += 2;
				polygonCount += 2;
			}
	#ifdef RECORD_SHADOW_STRIP_STATS
			//Continuing strip.
			stripLength++;
			maxStripLength=__max(maxStripLength,stripLength);
	#endif
		}
	}	//initial pass to determine vertex/polygon counts.
	//***********************************************************************************************

	DEBUG_ASSERTCRASH(m_shadowVolumeVB[ volumeIndex ][meshIndex] == NULL,("Updating Existing Static Vertex Buffer Shadow"));
	vbSlot=m_shadowVolumeVB[ volumeIndex ][meshIndex] = TheW3DBufferManager->getSlot(W3DBufferManager::VBM_FVF_XYZ,
		vertexCount);

	DEBUG_ASSERTCRASH(vbSlot != NULL, ("Can't allocate vertex buffer slot for shadow volume"));

	DEBUG_ASSERTCRASH(vbSlot->m_size >= vertexCount,("Overflowing Shadow Vertex Buffer Slot"));

	DEBUG_ASSERTCRASH(m_shadowVolume[ volumeIndex ][meshIndex]->GetNumPolygon() == 0,("Updating Existing Static Shadow Volume"));

	DEBUG_ASSERTCRASH(m_shadowVolumeIB[ volumeIndex ][meshIndex] == NULL,("Updating Existing Static Index Buffer Shadow"));
	ibSlot=m_shadowVolumeIB[ volumeIndex ][meshIndex] = TheW3DBufferManager->getSlot(polygonCount*3);

	DEBUG_ASSERTCRASH(ibSlot != NULL, ("Can't allocate index buffer slot for shadow volume"));

	DEBUG_ASSERTCRASH(ibSlot->m_size >= (polygonCount*3),("Overflowing Shadow Index Buffer Slot"));

	if (!ibSlot || !vbSlot)
	{	//could not allocate storage to hold buffers
		if (ibSlot)
			TheW3DBufferManager->releaseSlot(ibSlot);
		if (vbSlot)
			TheW3DBufferManager->releaseSlot(vbSlot);

		m_shadowVolumeIB[ volumeIndex ][meshIndex]=NULL;
		m_shadowVolumeVB[ volumeIndex ][meshIndex]=NULL;
		return; 
	}

	geomMesh = m_geometry->getMesh(meshIndex);

	DX8VertexBufferClass::AppendLockClass lockVtxBuffer(vbSlot->m_VB->m_DX8VertexBuffer,vbSlot->m_start,vertexCount);
	VertexFormatXYZ *vb = (VertexFormatXYZ*)lockVtxBuffer.Get_Vertex_Array();

	if (vb == NULL)
		return;

	DX8IndexBufferClass::AppendLockClass lockIdxBuffer(ibSlot->m_IB->m_DX8IndexBuffer,ibSlot->m_start,polygonCount*3);
	UnsignedShort *ib = (UnsignedShort*)lockIdxBuffer.Get_Index_Array();

	if (ib == NULL)
		return;

	shadowVolume->SetNumActivePolygon(polygonCount);
	shadowVolume->SetNumActiveVertex(vertexCount);

	Short *silhouetteIndices=m_silhouetteIndex[meshIndex];

	//Initialize first strip info
	Short stripStartIndex=silhouetteIndices[ 0 ];
	Short stripStartVertex=0;

	//Insert the first vertex and extrusion into strip.
	// get edge point
	const Vector3& ev=geomMesh->GetVertex( silhouetteIndices[ 0 ] );

	// take one edge point and extrude away from the light
	extrude2 = ev - *lightPosObject;
	extrude2 *= shadowExtrudeDistance;
	extrude2 += ev;

	*vb++ = *(VertexFormatXYZ *)&ev;
	*vb++ = *(VertexFormatXYZ *)&extrude2;

	vertexCount=2;
	polygonCount=0;
	Int lastEdgeVertex2Index=0;
	Int lastExtrude2Index=1;

	for( i = 0; i < indicesPerMesh; i += 2 )
	{
		Short currentEdgeEnd=silhouetteIndices[i+1];

		//Check if another edge is connected to this one, edges were sorted in initial
		//pass so only need to check the next one.
		if (((i+2) >= indicesPerMesh) || silhouetteIndices[i+2] != currentEdgeEnd)
		{	//reached end of strip. Insert final edge.
			//Check if last edge wraps around to start.
			if (currentEdgeEnd == stripStartIndex)
			{	//add end of strip that wraps around to start (forming closed cylinder/shape)
				//
				// add the polygon consisting of the two edge vertices and the
				// first extruded point
				//
				ib[ 0 ] = lastEdgeVertex2Index;  // lastedgeVertex2 index
				ib[ 4 ] = ib[ 1 ] = lastExtrude2Index;  // lastextrude2 index
				ib[ 3 ] = ib[ 2 ] = stripStartVertex;  // edgeVertex2 index
				ib[ 5 ] = stripStartVertex+1;  // extrude2 index
				ib += 6;	//skip past 2 triangles just added.
			}
			else
			{	//add end of strip.  Finishes the last 2 polygons.
				const Vector3& ev=geomMesh->GetVertex( currentEdgeEnd );
				*vb++ = *(VertexFormatXYZ *)&ev;

				//
				// add the polygon consisting of the two edge vertices and the
				// first extruded point
				//
				ib[ 0 ] = lastEdgeVertex2Index;  // lastedgeVertex2 index
				ib[ 4 ] = ib[ 1 ] = lastExtrude2Index;  // lastextrude2 index
				ib[ 3 ] = ib[ 2 ] = vertexCount;  // edgeVertex2 index
				ib[ 5 ] = vertexCount+1;  // extrude2 index
				ib += 6;	//skip past 2 triangles just added.

				// take the other edge point and extrude away from light
				extrude2 = ev - *lightPosObject;
				extrude2 *= shadowExtrudeDistance;
				extrude2 += ev;
				// add the one new vertex
				*vb++ = *(VertexFormatXYZ *)&extrude2;

				lastEdgeVertex2Index=vertexCount;
				lastExtrude2Index=vertexCount+1;
				vertexCount += 2;
			}

			if ((i+2) >= indicesPerMesh)
			{	//finished with all silhouette edges
				polygonCount += 2;
				break;	//reached end of all edges
			}
	
			//Start a new strip by adding first vertex and extrusion.
			const Vector3& evb=geomMesh->GetVertex( silhouetteIndices[ i+2 ] );
			// take one edge point and extrude away from the light
			extrude2 = evb - *lightPosObject;
			extrude2 *= shadowExtrudeDistance;
			extrude2 += evb;

			lastEdgeVertex2Index=vertexCount;
			lastExtrude2Index=vertexCount + 1;
			//record start of new strip info
			stripStartIndex=silhouetteIndices[ i+2 ];
			stripStartVertex=lastEdgeVertex2Index;

			*vb++ = *(VertexFormatXYZ *)&evb;
			*vb++ = *(VertexFormatXYZ *)&extrude2;
			vertexCount += 2;

			polygonCount += 2;
			continue;
		}
		else
		{	//continue existing strip by adding extra vertex and extrusion

			const Vector3& ev=geomMesh->GetVertex( currentEdgeEnd );
			*vb++ = *(VertexFormatXYZ *)&ev;
			//
			// add the polygon consisting of the two edge vertices and the
			// first extruded point
			//
			ib[ 0 ] = lastEdgeVertex2Index;  // lastedgeVertex2 index
			ib[ 4 ] = ib[ 1 ] = lastExtrude2Index;  // lastextrude2 index
			ib[ 3 ] = ib[ 2 ] = vertexCount;  // edgeVertex2 index
			ib[ 5 ] = vertexCount+1;  // extrude2 index
			ib += 6;	//skip past 2 triangles just added

			// take the other edge point and extrude away from light
			extrude2 = ev - *lightPosObject;
			extrude2 *= shadowExtrudeDistance;
			extrude2 += ev;

			// add the one new vertex
			*vb++ = *(VertexFormatXYZ *)&extrude2;
			
			lastEdgeVertex2Index=vertexCount;
			lastExtrude2Index=vertexCount+1;

			vertexCount += 2;
			polygonCount += 2;
		}
	}

//	DEBUG_ASSERTLOG(polygonCount == vertexCount, ("WARNING***Shadow volume mesh not optimal: %s\n",m_geometry->Get_Name()));
}  // end constructVolume

// allocateShadowVolume =======================================================
// Allocate a space for us to construct the shadow volume in
// ============================================================================
Bool W3DVolumetricShadow::allocateShadowVolume( Int volumeIndex, Int meshIndex )
{
	Int numVertices, numPolygons;
	Geometry *shadowVolume;

	// sanity
	if( volumeIndex < 0 || volumeIndex >= MAX_SHADOW_LIGHTS )
	{

//		DBGPRINTF(( "Illegal allocate shadow volume index '%d'\n", volumeIndex ));
		assert( 0 );
		return FALSE;

	}  // end if

	if ((shadowVolume = m_shadowVolume[ volumeIndex ][meshIndex]) == 0)
	{	
		// poolify
		shadowVolume = NEW Geometry;		// create the new geometry
		// we now have one more valid geometry volume
		m_shadowVolumeCount[meshIndex]++;
	}

	if( shadowVolume == NULL )
	{

//		DBGPRINTF(( "Unable to allocate '%d' shadow volume\n", volumeIndex ));
		assert( 0 );
		// we now have one more valid geometry volume
		m_shadowVolumeCount[meshIndex]--;
		return FALSE;

	}  // end if

	// assign to list
	m_shadowVolume[ volumeIndex ][meshIndex] = shadowVolume;

	//
	// polygons are determined from the edges extruded from the light.
	// since we have a list of disjoint edge pairs we will have a 4 sided
	// polygon for each edge pair, however we will be splitting the
	// 4 sided polys into 2 triangles so it just works out that num polys
	// is the number of silhouette indices
	//
	numPolygons = m_maxSilhouetteEntries[meshIndex];

	//
	// vertices are an extrusion of the edges, and since we have disjoint
	// pairs of edge indices we will have num indices * 2 actual vertex
	// points.  it may be a good future optimization to not duplicate
	// any vertices that appear twice here, but then again the cost over
	// optimizing this routine over the rendering and transformation
	// optimization may not be worth it
	//
	numVertices = m_maxSilhouetteEntries[meshIndex] * 2;

	//Only allocate space here for dynamic shadows.  Shadows for static/non-animated
	//models will be stored in vertex buffers which are allocated once exact size
	//is known.
	if (shadowVolume->GetFlags() & SHADOW_DYNAMIC)
	{
		//for dynamic shadow casters, we need to allocate the maximum amount of vertices that could ever be required.
//		if (m_shadowVolumeVB[ volumeIndex ][meshIndex])
//			TheW3DBufferManager->releaseSlot(m_shadowVolumeVB[ volumeIndex ][meshIndex]);
//		m_shadowVolumeVB[ volumeIndex ][meshIndex] = TheW3DVertexBufferManager->getSlot(W3DVertexBufferManager::VBM_FVF_XYZ, numVertices);

		// allocate memory for the vertices and polygons
		if( shadowVolume->Create( numVertices, numPolygons ) == FALSE )
		{

	//		DBGPRINTF(( "Unable to create shadow volume\n" ));
			assert( 0 );
			delete shadowVolume;
			return FALSE;

		}  // end if
	}

	return TRUE;  // success

}  // end allocateShadowVolume

// deleteShadowVolume =========================================================
// Free all resources allocated to the shadow volume(s)
// ============================================================================
void W3DVolumetricShadow::deleteShadowVolume( Int volumeIndex )
{

	// sanity
	if( volumeIndex < 0 || volumeIndex >= MAX_SHADOW_LIGHTS )
	{

//		DBGPRINTF(( "Illegal delete shadow volume index '%d'\n", volumeIndex ));
		assert( 0 );
		return;

	}  // end if

	// delete it!
	for (Int meshIndex=0; meshIndex<MAX_SHADOW_CASTER_MESHES; meshIndex++)
	{
		if( m_shadowVolume[ volumeIndex ][meshIndex] )
		{

			delete m_shadowVolume[ volumeIndex ][meshIndex];
			m_shadowVolume[ volumeIndex ][meshIndex] = NULL;

			// we now have one less shadow volume
			m_shadowVolumeCount[meshIndex]--;

		}  // end if
	}

}  // end deleteShadowVolume

// resetShadowVolume ==========================================================
// Reset the contents of the shadow volume information.  Since we're using
// a geometry class it would be ideal if these structures had a reset
// option where their resoures were released back to a pool rather than
// delete and allocate new storage space
// ============================================================================
void W3DVolumetricShadow::resetShadowVolume( Int volumeIndex, Int meshIndex )
{
	Geometry *geometry;

	// sanity
	if( volumeIndex < 0 || volumeIndex >= MAX_SHADOW_LIGHTS )
	{

//		DBGPRINTF(( "Illegal reset shadow volume index '%d'\n", volumeIndex ));
		assert( 0 );
		return;

	}  // end if

	geometry = m_shadowVolume[ volumeIndex ][meshIndex];

	//Release buffers used to hold shadow volume geometry
	if (geometry)
	{	if (m_shadowVolumeVB[volumeIndex][meshIndex])
		{	TheW3DBufferManager->releaseSlot(m_shadowVolumeVB[volumeIndex][meshIndex]);
			m_shadowVolumeVB[volumeIndex][meshIndex]=NULL;
		}
		if (m_shadowVolumeIB[ volumeIndex ][meshIndex])
		{	TheW3DBufferManager->releaseSlot(m_shadowVolumeIB[volumeIndex][meshIndex]);
			m_shadowVolumeIB[volumeIndex][meshIndex]=NULL;
		}
		geometry->Release();
	}

}  // end resetShadowVolume

// allocateSilhouette =========================================================
// Allocate space for new silhouette storage, the number of vertices passed
// in is the total vertices in the model, a silhouette must be able to
// accomodate that as a series of disjoint edge pairs, otherwise known
// as numVertices * 2
// ============================================================================
Bool W3DVolumetricShadow::allocateSilhouette(Int meshIndex, Int numVertices )
{
	Int numEntries = numVertices * 5;	///@todo: HACK, HACK... Should be 2!

	// sanity
	assert( m_silhouetteIndex[meshIndex] == NULL && 
					m_numSilhouetteIndices[meshIndex] == 0 &&
					numEntries > 0 );

	// allocate memory
	m_silhouetteIndex[meshIndex] = NEW short[ numEntries ];
	if( m_silhouetteIndex[meshIndex] == NULL )
	{
	
//		DBGPRINTF(( "Unable to allcoate silhouette storage '%d'\n", numEntries ));
		assert( 0 );
		return FALSE;

	}  // end if
		
	// set our list to empty just to be clean
	m_numSilhouetteIndices[meshIndex] = 0;
	
	// save the size of our silhouette list
	m_maxSilhouetteEntries[meshIndex] = numEntries;

	return TRUE;  // success

}  // end allocateSilhouette

// deleteSilhouette ===========================================================
// Delete all silhouette data and memory allocated
// ============================================================================
void W3DVolumetricShadow::deleteSilhouette( Int meshIndex )
{

	if( m_silhouetteIndex[meshIndex])
		delete [] m_silhouetteIndex[meshIndex];
	m_silhouetteIndex[meshIndex] = NULL;
	m_numSilhouetteIndices[meshIndex] = 0;

}  // end deletesilhouette

// resetSilhouette ============================================================
// Resets the silhouette to empty, it does NOT free any of the memory
// allocated for silhouette data
// ============================================================================
void W3DVolumetricShadow::resetSilhouette( Int meshIndex )
{

	m_numSilhouetteIndices[meshIndex] = 0;

}  // end resetSilhouette

// renderStencilShadows =======================================================
// The stencil buffer now has our shadow information in it, take that
// info and draw a big transparent rectangle over the screen for the final
// shadow pass wherever there is data in the stencil buffer
// ============================================================================
void W3DVolumetricShadowManager::renderStencilShadows( void )
{
	LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

	if (!m_pDev)
		return;	//need device to render anything.

	struct _TRANSLITVERTEX {
	    D3DXVECTOR4 p;
		UnsignedInt color;   // diffuse color    
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

    v[0].p = D3DXVECTOR4( xpos+width, ypos+height, 0.0f, 1.0f );
    v[1].p = D3DXVECTOR4( xpos+width, 0, 0.0f, 1.0f );
    v[2].p = D3DXVECTOR4(  xpos, ypos+height, 0.0f, 1.0f );
    v[3].p = D3DXVECTOR4(  xpos,  0, 0.0f, 1.0f );
    v[0].color = TheW3DShadowManager->getShadowColor();
    v[1].color = TheW3DShadowManager->getShadowColor();
    v[2].color = TheW3DShadowManager->getShadowColor();
    v[3].color = TheW3DShadowManager->getShadowColor();

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Set_Vertex_Format(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

	// Use alpha blending to draw the transparent shadow
    DX8Wrapper::Set_DX8_Render_State( D3DRS_ALPHABLENDENABLE, TRUE );
		DX8Wrapper::Set_DX8_Render_State( D3DRS_SRCBLEND,  D3DBLEND_DESTCOLOR);
		DX8Wrapper::Set_DX8_Render_State( D3DRS_DESTBLEND, D3DBLEND_ZERO );


	// Set stencil states
    DX8Wrapper::Set_DX8_Render_State( D3DRS_ZENABLE,          TRUE );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_ALWAYS);

	// Only write where stencil val >= 1 (count indicates # of shadows that
	// overlap that pixel)
    DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILENABLE, TRUE );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILFUNC, D3DCMP_LESSEQUAL );	//reference value is less or equal to stencil
    DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILPASS, D3DSTENCILOP_KEEP );
	//Upper bits of stencil could be used for storing occluded models which are player colored.  So we mask out those
	//pixels and only use the lower bits for shadow calculations.
	DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILMASK,     ~TheW3DShadowManager->getStencilShadowMask());
    DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILREF,      0x1 );


	DX8Wrapper::Set_DX8_Render_State(D3DRS_SHADEMODE, D3DSHADE_FLAT);

	if (DX8Wrapper::_Is_Triangle_Draw_Enabled())
		DX8Wrapper::_Draw_DX8_Primitive_UP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANSLITVERTEX));

	DX8Wrapper::Set_DX8_Render_State(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	DX8Wrapper::Set_DX8_Render_State( D3DRS_ALPHABLENDENABLE, FALSE );
	// turn off the stencil buffer
	DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILENABLE, FALSE );

}  // end renderStencilShadows

// THREADING-ROADMAP.md section 0 step 2: the stencil shadow pass had no gather at all, which is
// exactly the number section 3.2 has to be argued with.  shadowVolumeUpdate/-Submit inside split
// it into the CPU half and the D3D half.
DECLARE_PERF_TIMER(stencilShadows)
DECLARE_PERF_TIMER(shadowVolumeUpdate)
DECLARE_PERF_TIMER(shadowVolumeSubmit)

/** Nothing of this frame's smoke is in the map: every system keeps its blob. */
static void forgetSmokeInSunMap( void )
{
	if (TheParticleSystemManager == NULL)
		return;
	ParticleSystemManager::ParticleSystemList &systems = TheParticleSystemManager->getAllParticleSystems();
	for (ParticleSystemManager::ParticleSystemListIt it = systems.begin(); it != systems.end(); ++it)
		if (*it)
			(*it)->setInSunMap( FALSE );
}

/** One particle on its way into the smoke map, with the system it came from and how far it is from
		what the camera looks at, which decides who goes in when there are more than the map takes. */
struct SmokeCaster
{
	Real x, y, z, radius, opticalDepth;
	Real distanceSqr;
	ParticleSystem *system;
	bool operator<( const SmokeCaster &other ) const { return distanceSqr < other.distanceSqr; }
};

/** The smoke into the sun's light: every alpha-blended billboard particle as a soft ball, its optical
		depth the one its own alpha implies (particleSunMapOpticalDepth), handed to the backend's smoke
		map.  The systems are read as the last client update left them, because the pass runs before
		the frame's particles are drawn.  Fire is additive and casts nothing; a ground-aligned system is
		a decal already.  Only what stands in the sun's box counts, and past the map's limit the
		particles nearest the camera's focus win, so a new fire on screen is never starved by old smoke
		at the edge.  Every system with a particle in the map is marked, and those lose their blob;
		with the option off, or the map refused, none is. */
static void fillSmokeMap( const Matrix3D &sunTransform, const Vector3 &focus, Real halfWidth, Real farClip )
{
	static std::vector<SmokeCaster> found;	// kept: a burning base is thousands of particles every frame
	static std::vector<Real> packed;
	static Bool reported = FALSE;
	static UnsignedInt nextReportFrame = 0;
	const size_t mostCasters = 16384;		// the backend's own SMOKE_MOST_CASTERS

	forgetSmokeInSunMap();
	found.clear();
	packed.clear();

	if (TheGlobalData->m_volumetricSmokeShadows && TheParticleSystemManager != NULL)
	{
		ParticleSystemManager::ParticleSystemList &systems = TheParticleSystemManager->getAllParticleSystems();
		for (ParticleSystemManager::ParticleSystemListIt it = systems.begin(); it != systems.end(); ++it)
		{
			ParticleSystem *sys = *it;
			if (sys == NULL || sys->getShaderType() != ParticleSystemInfo::ALPHA || !sys->shouldBillboard()
					|| sys->isUsingDrawables() || sys->isUsingStreak() || sys->isUsingSmudge())
				continue;
			// the heat haze's texture names start with SMUD, as W3DParticleSystemManager::doParticles tests
			if (*((UnsignedInt *)sys->getParticleTypeName().str()) == 0x44554D53)
				continue;

			const UnsignedInt layers = sys->getVolumeParticleDepth();
			for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
			{
				const Real opticalDepth = particleSunMapOpticalDepth( p->getAlpha(), layers );
				if (opticalDepth <= 0.0f)
					continue;
				const Coord3D *pos = p->getPosition();
				const Real radius = p->getSize() * 0.5f;		// the billboard's size is its full width
				if (radius <= 0.0f)
					continue;

				// the box the depth map covers, in the sun's own frame: it looks down its -Z
				Vector3 inSun;
				Matrix3D::Inverse_Transform_Vector( sunTransform, Vector3( pos->x, pos->y, pos->z ), &inSun );
				const Real reach = halfWidth + radius;
				if (inSun.X < -reach || inSun.X > reach || inSun.Y < -reach || inSun.Y > reach
						|| -inSun.Z < SHADOW_MAP_NEAR_CLIP - radius || -inSun.Z > farClip + radius)
					continue;

				SmokeCaster caster;
				caster.x = pos->x;
				caster.y = pos->y;
				caster.z = pos->z;
				caster.radius = radius;
				caster.opticalDepth = opticalDepth;
				const Real dx = pos->x - focus.X;
				const Real dy = pos->y - focus.Y;
				caster.distanceSqr = dx * dx + dy * dy;
				caster.system = sys;
				found.push_back( caster );
			}
		}
	}

	const size_t inBox = found.size();
	if (found.size() > mostCasters)
	{
		std::nth_element( found.begin(), found.begin() + mostCasters, found.end() );
		found.resize( mostCasters );
	}

	packed.reserve( found.size() * 5 );
	for (size_t i = 0; i < found.size(); ++i)
	{
		packed.push_back( found[ i ].x );
		packed.push_back( found[ i ].y );
		packed.push_back( found[ i ].z );
		packed.push_back( found[ i ].radius );
		packed.push_back( found[ i ].opticalDepth );
	}

	const Bool held = Direct3D11_Fill_Smoke_Map( packed.empty() ? NULL : &packed[ 0 ],
		(unsigned)found.size(), SMOKE_SHADOW_STRENGTH );
	if (!held)
		return;

	Int systemsHeld = 0;
	for (size_t i = 0; i < found.size(); ++i)
	{
		if (!found[ i ].system->isInSunMap())
		{
			found[ i ].system->setInSunMap( TRUE );
			++systemsHeld;
		}
	}

	// A run's log says whether the smoke was in the map at all: the first frame it held any, and
	// every ten seconds while it does.
	const UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	if (!found.empty() && (!reported || frame >= nextReportFrame))
	{
		nextReportFrame = frame + 10 * LOGICFRAMES_PER_SECOND;
		reported = TRUE;
		DEBUG_LOG(("SMOKEMAP: frame %u, %d casters from %d systems in the sun's map, %d more past its limit;"
			" fire light %s\n",
			frame, (Int)found.size(), systemsHeld, (Int)( inBox - found.size() ),
			TheGlobalData->m_smokeFireLighting ? "on" : "off"));
	}
}

/* The 2D scene (W3DView::draw, last) is drawn through its own camera at (0, 0, 1) with a view plane
	 of 2 by 1.5 at unit distance (W3DView.cpp, m_2DCamera), so its quads - the script fade and the
	 team dot of W3DStatusCircle - lie on z = 0 inside |x| <= 1, |y| <= 0.75.  Render2DClass draws
	 the interface's quads and text with identity world, view and projection (render2d.cpp), so a
	 pixel of those comes back out at its own NDC: |x|, |y| <= 1 and a depth from 0 to 1.  Those that
	 blend by source alpha or multiply by source colour go through the shadow program like any world
	 draw, and their pixels land at those world positions: at the corner of the map, which the sun's
	 box covers whenever the camera is near it.  The reach is the corner of that unit cube from the
	 origin, the square root of three; the filter's own reach is added by the caller. */
#define SHADOW_MAP_OVERLAY_REACH 1.75f
// ponytail: a constant pad on every caster's bounding sphere, because an HLod's sphere comes from
// its model and need not cover what a deployed state swings out (a raised Scud, the Nuke Cannon's
// barrel, a crane arm).  The ceiling is a part reaching more than this past its sphere; the upgrade
// is a sphere per condition state, or the sphere of the posed sub-objects.
#define SHADOW_MAP_CASTER_PAD 30.0f

/** Whether a water mirror will read the map this pass fills.  WaterRenderObjClass::renderMirror
		draws the reflected scene before the next frame's depth pass, through that frame's camera,
		while the map still holds this frame's casters, and the legacy frame buffer mirror draws one
		inside this frame through a reflected camera.  Neither is a frustum known here, so a map that
		can reflect at all keeps every caster in the box.  Translucent water mirrors only through its
		water areas; the grid mesh never does. */
static Bool waterMirrorReadsTheMap()
{
	if (TheWaterRenderObj == NULL)
		return FALSE;
	if (TheGlobalData->m_waterType == WaterRenderObjClass::WATER_TYPE_3_GRIDMESH)
		return FALSE;
	if (TheGlobalData->m_waterType != WaterRenderObjClass::WATER_TYPE_0_TRANSLUCENT)
		return TRUE;
	for (PolygonTrigger *area = PolygonTrigger::getFirstPolygonTrigger(); area; area = area->getNext())
	{
		if (area->isWaterArea() && area->getNumPoints() >= 3)
			return TRUE;
	}
	return FALSE;
}

/** Whether anything a pixel of this frame samples can be shadowed by a caster.  The caster is its
		bounding sphere, and what it can darken lies downwind of it: the sphere swept away from the sun
		from its own sun side down to the lowest ground on the map, widened by how far the receiving
		filter reaches across the map.  That capsule runs from upwind to downwind with this radius.
		Only the four sides of the view are tested: a draw with a depth-biased projection reads the
		map through near and far planes that are not the camera's, and the sides are the same for
		every one of them.  The overlay's quads sit at z = 0 whatever the ground does, so for them the
		capsule runs on to overlayDownwind, swept down to z = 0 when the lowest ground is above it. */
static Bool shadowReachesTheFrame( const FrustumClass &view, const Vector3 &upwind,
	const Vector3 &downwind, const Vector3 &overlayDownwind, Real radius )
{
	Bool inView = TRUE;
	for (Int side = 1; side <= 4 && inView; ++side)
	{
		const PlaneClass &plane = view.Planes[ side ];
		inView = Vector3::Dot_Product( plane.N, upwind ) - plane.D <= radius
			|| Vector3::Dot_Product( plane.N, downwind ) - plane.D <= radius;
	}
	if (inView)
		return TRUE;

	// the 2D scene's and the interface's quads, at the world's origin
	const Vector3 along = overlayDownwind - upwind;
	const Real lengthSqr = along.Length2();
	Real t = (lengthSqr > 0.0f) ? -Vector3::Dot_Product( upwind, along ) / lengthSqr : 0.0f;
	t = WWMath::Clamp( t, 0.0f, 1.0f );
	const Vector3 nearest = upwind + along * t;
	const Real reach = radius + SHADOW_MAP_OVERLAY_REACH;
	return nearest.Length2() <= reach * reach;
}

/** Render() is not only a draw.  Animatable3DObjClass::Render moves a playing animation on to the
		frame the clock says, and W3DModelDraw reads that frame back (Is_Animation_Complete,
		Peek_Animation_And_Info) to step its states and carry a frame across them.  The depth pass
		used to do that for every caster in the box every frame, and the frame is a running float sum,
		so a caster the pass leaves out still has its clock moved here exactly as Render would, which
		keeps every frame number what it was.  The pose is what the pass no longer pays for: Render
		posed the bones and the meshes on them, and here they are only marked stale, so whatever draws
		the model or reads a bone or a sub-object's transform next poses it from the same frame and
		the same transform the pass would have used. */
class CasterAnimationClock : public Animatable3DObjClass
{
public:
	static void advance( RenderObjClass *robj )
	{
		if (robj->Class_ID() != RenderObjClass::CLASSID_HLOD || !robj->Is_Not_Hidden_At_All())
			return;
		HLodClass *hlod = (HLodClass *)robj;
		if (hlod->Get_HTree() == NULL)
			return;
		float frame, multiplier;
		int frames, mode;
		if (hlod->Peek_Animation_And_Info( frame, frames, mode, multiplier ) != NULL
				&& mode != RenderObjClass::ANIM_MODE_MANUAL)
		{
			void (Animatable3DObjClass::*progress)( void ) = &CasterAnimationClock::Single_Anim_Progress;
			(hlod->*progress)();
		}
		hlod->Set_Sub_Object_Transforms_Dirty( true );
	}
};

/** Where an edge of the view crosses the level z, held to the stretch between its near and far
		planes. */
static Vector3 viewEdgeAtHeight( const Vector3 &nearPoint, const Vector3 &farPoint, Real z )
{
	if (nearPoint.Z <= z)
		return nearPoint;
	if (farPoint.Z >= z)
		return farPoint;
	return nearPoint + (farPoint - nearPoint) * ((nearPoint.Z - z) / (nearPoint.Z - farPoint.Z));
}

/** How wide the sun's box has to be to hold the ground the tactical camera sees.  Every ground point
		in view lies on an edge-bounded stretch of the view between the highest and the lowest ground on
		the map, so each edge is cut at both heights and the eight points are taken into the sun's frame
		around the look point: the box is as wide as the furthest of them, rounded up to a step.  The
		lowest level alone would miss a hill at the bottom of the screen, whose point sits much nearer
		the camera and can land outside all four low ones in the sun's frame. */
static Real shadowMapHalfWidth( const CameraClass &sceneCamera, const Matrix3D &sunAtFocus, Real lowestGround,
	Real highestGround )
{
	const Vector3 *corners = sceneCamera.Get_Frustum().Corners;
	Real needed = 0.0f;
	for (Int i = 0; i < 8; ++i)
	{
		const Vector3 ground = viewEdgeAtHeight( corners[ i & 3 ], corners[ (i & 3) + 4 ],
			(i < 4) ? lowestGround : highestGround );
		Vector3 inSun;
		Matrix3D::Inverse_Transform_Vector( sunAtFocus, ground, &inSun );
		needed = WWMath::Max( needed, WWMath::Max( WWMath::Fabs( inSun.X ), WWMath::Fabs( inSun.Y ) ) );
	}
	/* It grows at once and shrinks only a whole step late.  The look point's own ground height moves
		 the footprint in the sun's frame as the camera scrolls over hills, and a box that followed it
		 both ways would hop across a step and back, and every shadow edge on the screen with it. */
	static Real held = SHADOW_MAP_HALF_WIDTH;
	if (needed > held || needed < held - SHADOW_MAP_HALF_WIDTH_STEP)
	{
		const Real stepped = WWMath::Ceil( needed / SHADOW_MAP_HALF_WIDTH_STEP ) * SHADOW_MAP_HALF_WIDTH_STEP;
		held = WWMath::Clamp( stepped, SHADOW_MAP_HALF_WIDTH, SHADOW_MAP_WIDEST_HALF_WIDTH );
	}
	return held;
}

/** The freecam's box: the whole map.  Its middle at the lowest ground, and its eight corners at the
		lowest ground and at the highest plus aircraft headroom, taken into the sun's frame, give the half
		width and how far the box reaches toward the sun and away from it.  The sun's seat is set back
		past the nearest corner and the far plane past the furthest, so nothing on the map falls out of
		the box's depth however low the sun is. */
static void wholeMapSunBox( const Vector3 &toSun, Vector3 *focus, Real *halfWidth, Real *sunDistance, Real *farClip )
{
	Region3D extent;
	TheTerrainRenderObject->getDrawnExtent( &extent );	// the border ring is drawn, so it takes shadows too
	const Real lowest = extent.lo.z;
	const Real highest = extent.hi.z + SHADOW_MAP_WHOLE_MAP_HEADROOM;
	focus->Set( (extent.lo.x + extent.hi.x) * 0.5f, (extent.lo.y + extent.hi.y) * 0.5f, lowest );

	Matrix3D sunAtFocus;
	sunAtFocus.Look_At( *focus + toSun, *focus, 0.0f );
	Real across = 0.0f;
	Real towardSun = 0.0f;
	Real awayFromSun = 0.0f;
	for (Int corner = 0; corner < 8; ++corner)
	{
		const Vector3 at( (corner & 1) ? extent.hi.x : extent.lo.x, (corner & 2) ? extent.hi.y : extent.lo.y,
			(corner & 4) ? highest : lowest );
		Vector3 inSun;
		Matrix3D::Inverse_Transform_Vector( sunAtFocus, at, &inSun );
		across = WWMath::Max( across, WWMath::Max( WWMath::Fabs( inSun.X ), WWMath::Fabs( inSun.Y ) ) );
		const Real along = Vector3::Dot_Product( at - *focus, toSun );
		towardSun = WWMath::Max( towardSun, along );
		awayFromSun = WWMath::Max( awayFromSun, -along );
	}
	*halfWidth = WWMath::Ceil( across / SHADOW_MAP_HALF_WIDTH_STEP ) * SHADOW_MAP_HALF_WIDTH_STEP;
	*sunDistance = towardSun + SHADOW_MAP_WHOLE_MAP_DEPTH_MARGIN + SHADOW_MAP_NEAR_CLIP;
	*farClip = *sunDistance + awayFromSun + SHADOW_MAP_WHOLE_MAP_DEPTH_MARGIN;
}

/** The sun's depth pass.  The casters are the ones that cast a volume today, drawn again from the
		sun into a depth buffer nothing samples yet, so this phase can be proved on its own: with it
		off the frame is what it was, and with it on the map has the world in it and the frame is
		still what it was.  The box is square and centred on what the tactical camera is looking at,
		which is the ground the player can see; a cascade is only worth it once the box has to cover
		more than one view.  SHADOW-MAP-PLAN.md phase 1. */
void W3DVolumetricShadowManager::renderShadowMap( CameraClass &sceneCamera )
{
	theShadowMapHoldsTheFrame = FALSE;

	/* The freecam asks for a map four times as wide and makes do with the usual one if refused.  A
		 refusal is remembered until the freecam lands: asking again every frame released the usual
		 map, failed the large one and made the usual one again, a texture rebuild a frame. */
	const Bool wholeMap = TheTacticalView != NULL && TheTacticalView->isFreeCamera();
	static Bool wholeMapRefused = FALSE;
	if (!wholeMap)
		wholeMapRefused = FALSE;
	UnsignedInt texels = SHADOW_MAP_TEXELS;
	Bool begun = FALSE;
	if (TheGlobalData->m_shadowMap && m_shadowList != NULL && TheTacticalView != NULL)
	{
		if (wholeMap && !wholeMapRefused && Direct3D11_Begin_Shadow_Map( SHADOW_MAP_WHOLE_MAP_TEXELS ))
		{
			texels = SHADOW_MAP_WHOLE_MAP_TEXELS;
			begun = TRUE;
		}
		else
		{
			if (wholeMap)
				wholeMapRefused = TRUE;
			begun = Direct3D11_Begin_Shadow_Map( SHADOW_MAP_TEXELS );
		}
	}
	if (!begun)
	{
		//no Direct3D 11 device, or it refused the surface: the volumes keep the frame, no smoke is
		//read out of a map the last frame filled, and every cloud keeps its blob
		Direct3D11_Fill_Smoke_Map( NULL, 0, 0.0f );
		forgetSmokeInSunMap();
		return;
	}

#ifdef DEBUG_LOGGING
	// The scene timer includes this pass and cannot say so.
	extern Real TheShadowMapMS;
	extern Int TheShadowMapCasters;
	extern UnsignedInt TheShadowMapDraws;
	Int castersCounted = 0;
	Int64 tShadowStart;
	const unsigned shadowDrawsBefore = DX8Wrapper::Get_Draw_Calls();
	tShadowStart = Clock_Ticks();
#endif

	Vector3 toSun = TheW3DShadowManager->getLightPosWorld( 0 );
	toSun.Normalize();

	Vector3 focus;
	Real halfWidth;
	Real sunDistance = SHADOW_MAP_SUN_DISTANCE;
	Real farClip = SHADOW_MAP_FAR_CLIP;
	Matrix3D transform;
	const Real lowestReceiver = TheTerrainRenderObject->getMinHeight();
	if (wholeMap)
	{
		wholeMapSunBox( toSun, &focus, &halfWidth, &sunDistance, &farClip );
		transform.Look_At( focus + toSun * sunDistance, focus, 0.0f );
	}
	else
	{
		Coord3D look;
		TheTacticalView->getPosition( &look );
		focus.Set( look.x, look.y, TheTerrainLogic->getGroundHeight( look.x, look.y ) );
		transform.Look_At( focus + toSun * sunDistance, focus, 0.0f );
		halfWidth = shadowMapHalfWidth( sceneCamera, transform, lowestReceiver,
			TheTerrainRenderObject->getMaxHeight() );
	}

	CameraClass sun;
	sun.Set_Projection_Type( CameraClass::ORTHO );
	sun.Set_View_Plane( Vector2( -halfWidth, -halfWidth ), Vector2( halfWidth, halfWidth ) );
	sun.Set_Clip_Planes( SHADOW_MAP_NEAR_CLIP, farClip );

	/* The box has to sit on whole texels of its own map, or every scroll of the camera slides the
		 grid under the world by a fraction of a texel and every shadow edge crawls and sparkles.  The
		 look point is taken into the sun's own frame, rounded to the texel it lands in, and taken back
		 out: the box then moves in texel steps and a shadow that did not move does not shimmer. */
	const Real texelWidth = (2.0f * halfWidth) / (Real)texels;
	Vector3 focusInSun;
	Matrix3D::Inverse_Transform_Vector( transform, focus, &focusInSun );
	focusInSun.X = WWMath::Floor( focusInSun.X / texelWidth + 0.5f ) * texelWidth;
	focusInSun.Y = WWMath::Floor( focusInSun.Y / texelWidth + 0.5f ) * texelWidth;
	Vector3 snapped;
	Matrix3D::Transform_Vector( transform, focusInSun, &snapped );
	transform.Look_At( snapped + toSun * sunDistance, snapped, 0.0f );

	sun.Set_Transform( transform );

	Matrix4x4 projection;
	sun.Get_D3D_Projection_Matrix( &projection );
	DX8Wrapper::Set_Projection_Transform_With_Z_Bias( projection, SHADOW_MAP_NEAR_CLIP, farClip );
	Matrix3D view;
	transform.Get_Orthogonal_Inverse( view );
	DX8Wrapper::Set_Transform( D3DTS_VIEW, view );

	// Depth is the whole point of the pass and colour is the whole cost of it.
	DX8Wrapper::Set_DX8_Render_State( D3DRS_COLORWRITEENABLE, 0 );
	DX8Wrapper::Set_DX8_Render_State( D3DRS_ZENABLE, TRUE );
	DX8Wrapper::Set_DX8_Render_State( D3DRS_ZWRITEENABLE, TRUE );
	DX8Wrapper::Set_DX8_Render_State( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );
	DX8Wrapper::Set_DX8_Render_State( D3DRS_ALPHABLENDENABLE, FALSE );
	DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILENABLE, FALSE );
	/* Back faces only.  What the map holds is then the far side of every caster, which is behind
		 the surface that receives the light by the thickness of the object: a hull cannot shadow
		 itself, and the bias has only the map's own texel to cover rather than a whole tank. */
	DX8Wrapper::Set_DX8_Render_State( D3DRS_CULLMODE, D3DCULL_CW );

	RenderInfoClass sunInfo( sun );
	// MeshClass::Render leaves the light pieces (beams, glows) out under this hint.
	sunInfo.Push_Override_Flags( RenderInfoClass::RINFO_OVERRIDE_SHADOW_RENDERING );
	MeshClass::Shadow_Pass_Light_Meshes = 0;
#ifndef DEBUG_LOGGING
	Int castersCounted = 0;
#endif
	Int castersOutOfView = 0;

	/* The filter's limits were chosen in texels of the 900 box.  Held at the same width on the ground
		 as the box opens, a tank's edge stays as hard and a helicopter's shadow as pale from full height
		 as it does at the usual one; counted in texels they would both spread with the zoom.  The
		 freecam's larger map is the same rule: it is the width of a texel on the ground that counts. */
	const Real texelsPerBoxTexel = (2.0f * SHADOW_MAP_HALF_WIDTH / (Real)SHADOW_MAP_TEXELS) / texelWidth;
	const Real widest = texelsPerBoxTexel * ((TheGlobalData->m_shadowMapWidest > 0.0f)
		? TheGlobalData->m_shadowMapWidest : SHADOW_MAP_WIDEST_TEXELS);
	const Real narrowest = texelsPerBoxTexel * SHADOW_MAP_NARROWEST_TEXELS;

	/* How far across the map a pixel reads, which is how far beside a caster's own outline its
		 shadow can still darken one.  sun_reaching in ffshader.h: the blocker search's corner tap is
		 widest times the square root of two away, the filter's turned corner the same at its widest,
		 the normal offset at most 0.6 of a texel, and a point sampled tap reads the texel whose centre
		 is up to 0.71 away.  1.5 and 2 cover those with room.  Receivers sit no lower than the
		 lowest ground on the map: anything under it is seen through the ground, which is drawn and
		 writes depth in front of it.  A sun at the horizon sweeps forever, so then nothing is left
		 out, and nothing is while a water mirror can read this map through a camera other than this
		 one. */
	const FrustumClass &seen = sceneCamera.Get_Frustum();
	const Real filterReach = (widest * 1.5f + 2.0f) * texelWidth;
	const Bool cullToFrame = toSun.Z > 0.01f && !waterMirrorReadsTheMap();

	for (W3DVolumetricShadow *shadow = m_shadowList; shadow; shadow = shadow->m_next)
	{
		RenderObjClass *robj = shadow->getRenderObject();
		if (robj == NULL || !shadow->isRenderEnabled() || shadow->isInvisibleEnabled())
			continue;

		/* What goes into the map is what stands in the sun's box, not what the tactical camera can
			 see.  Culling by the camera meant a tank just off the left of the screen stopped casting,
			 so its shadow blinked out while the shadow of the tank beside it stayed - and scrolling
			 turned that into a row of shadows flickering along the edge of the frame. */
		const SphereClass &bound = robj->Get_Bounding_Sphere();
		Vector3 inSun;
		Matrix3D::Inverse_Transform_Vector( transform, robj->Get_Position(), &inSun );
		const Real reach = halfWidth + bound.Radius;
		if (inSun.X < -reach || inSun.X > reach || inSun.Y < -reach || inSun.Y > reach)
			continue;

		/* That tank is still in the map, because its shadow sweeps onto the screen.  What can be left
			 out is a caster whose shadow, followed downwind to the lowest ground and widened by the
			 filter, never meets the frame: no pixel drawn this frame reads a texel it is nearest the
			 sun in, so the map reads the same everywhere it is read. */
		if (cullToFrame)
		{
			const Real casterRadius = bound.Radius + SHADOW_MAP_CASTER_PAD;
			const Real radius = casterRadius + filterReach;
			const Real sweep = WWMath::Max( (bound.Center.Z - lowestReceiver + radius) / toSun.Z, 0.0f );
			const Real overlaySweep = WWMath::Max(
				(bound.Center.Z - WWMath::Min( lowestReceiver, 0.0f ) + radius) / toSun.Z, 0.0f );
			if (!shadowReachesTheFrame( seen, bound.Center + toSun * casterRadius,
					bound.Center - toSun * sweep, bound.Center - toSun * overlaySweep, radius ))
			{
				CasterAnimationClock::advance( robj );
				++castersOutOfView;
				continue;
			}
		}

		robj->Render( sunInfo );
		++castersCounted;
	}
	TheDX8MeshRenderer.Flush();
	/* A mesh the material system calls translucent goes to the sort lists rather than to the mesh
		 renderer, and a helicopter's rotor disc is one of them: without this the map holds the
		 fuselage alone, and a fuselage is too thin a thing to read as a shadow once the filter opens.
		 Draining them here is safe because the pass runs before the frame queues anything of its
		 own, so everything in those lists was put there by the loop above. */
	WW3D::Render_And_Clear_Static_Sort_Lists( sunInfo );
	/* And the rotor itself goes further still: its mesh carries the SORT flag, so the mesh renderer
		 hands it to the sorting renderer, which holds it for the end of the scene and would draw it
		 onto the screen rather than into the map.  The backend writes depth for a blended caster
		 while the pass runs and cuts it at its alpha, which keeps the blades. */
	SortingRendererClass::Flush();

	/* The bridge decks are not casters in the list above and have to be in the map all the same:
		 without them the sun reached through a deck, and a tank crossing it laid one shadow on the
		 deck and a second on the ground under the bridge.  Drawn last so the bias below reaches no
		 other caster.  A deck goes in with both faces, so the map holds its top, and a top that
		 tilts away from the sun would shadow its own far edge in stripes; the slope bias pushes it
		 back by what its tilt across a texel or two is worth, which is nothing next to the height
		 of a bridge over the ground and leaves a unit on the deck nearer the sun than the deck.
		 Nothing else in the game sets this state, so it goes back to zero rather than to the
		 wrapper's cached value, which an invalidate leaves as a sentinel. */
	const float bridgeSlopeBias = SHADOW_MAP_BRIDGE_SLOPE_BIAS;
	DX8Wrapper::Set_DX8_Render_State( D3DRS_SLOPESCALEDEPTHBIAS, *(const uint32 *)&bridgeSlopeBias );
	TheTerrainRenderObject->getBridgeBuffer()->drawBridgeShadowCasters();
	DX8Wrapper::Set_DX8_Render_State( D3DRS_SLOPESCALEDEPTHBIAS, 0 );

	DX8Wrapper::Set_DX8_Render_State( D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE
		| D3DCOLORWRITEENABLE_ALPHA );

	Direct3D11_End_Shadow_Map();

	// The smoke goes into a map of its own through the sun the casters were just drawn with.
	// past the map's limit the smoke nearest the camera wins: the freecam's box is centred on the
	// map's middle, which is not where it is looking from
	fillSmokeMap( transform, wholeMap ? sceneCamera.Get_Position() : focus, halfWidth, farClip );

	// The frame's own camera, put back: the view, the projection and the viewport all went with the
	// sun.  Without this everything drawn after the pass is drawn from the sun's seat, which is the
	// whole picture rather than a corner of it.
	sceneCamera.Apply();

	/* And its view said to the backend outright.  The sorted particles are written in this camera's
		 space and drawn with an identity view, and their pixels go back to the world through this
		 matrix; a guess at it from whatever perspective draw came last picked up the camera-relative
		 view the aligned spheres draw with.  In the layout DX8Wrapper hands the device. */
	{
		Matrix4x4 view;
		DX8Wrapper::Get_Transform( D3DTS_VIEW, view );
		const Matrix4x4 deviceView = view.Transpose();
		Direct3D11_Set_Scene_View( (const float *)&deviceView );
	}

	/* And what turns the map into a shadow.  The matrix that takes a pixel from the frame's clip
		 space into the sun's is built in the backend, out of the sun's own view and projection as it
		 held them during the pass and the frame's as it holds them now: both are already there, in
		 one convention, and a matrix assembled on this side would have to agree with a layout it
		 cannot see.  SHADOW-MAP-PLAN.md phase 2. */
	/* The two conversions the filter needs, both of them the box's own arithmetic.  A texel is this
		 many world units across, and a unit of depth is the whole of the near to far range, because
		 an orthographic projection puts depth on a straight line. */
	const Real worldPerTexel = texelWidth;
	const Real unitsPerUnitOfDepth = farClip - SHADOW_MAP_NEAR_CLIP;
	/* The bias is a share of that range, picked for the usual 3990 units.  The freecam's deeper box
		 would stretch it to tens of units and lift every shadow off its caster, so it is held at the
		 same distance in the world: six units. */
	const Real depthBias = wholeMap
		? SHADOW_MAP_DEPTH_BIAS * (SHADOW_MAP_FAR_CLIP - SHADOW_MAP_NEAR_CLIP) / unitsPerUnitOfDepth
		: SHADOW_MAP_DEPTH_BIAS;

	// -shadowtune overrules any of the four that it was given; a zero leaves the build's own.
	const Real penumbra = (TheGlobalData->m_shadowMapPenumbra > 0.0f)
		? TheGlobalData->m_shadowMapPenumbra : SHADOW_MAP_PENUMBRA_PER_UNIT;
	const Real skyFill = (TheGlobalData->m_shadowMapSkyFill > 0.0f)
		? TheGlobalData->m_shadowMapSkyFill : SHADOW_MAP_SKY_FILL;
	const Real strength = (TheGlobalData->m_shadowMapStrength > 0.0f)
		? TheGlobalData->m_shadowMapStrength : SHADOW_MAP_STRENGTH;

	Direct3D11_Set_Shadow_Parameters( depthBias, strength, widest,
		narrowest, penumbra / worldPerTexel, unitsPerUnitOfDepth, skyFill );

	/* Said last, and only on the way out: everything above can bail, and the volumes have to know
		 whether this frame's shadows are in the map or still theirs to draw.  The count of casters is
		 not part of that answer.  It was, and a frame that happened to hold none - a camera over open
		 ground - handed the frame back to the volumes for that one frame, so the whole scene's
		 shadows changed style and back again as the camera moved. */
	theShadowMapHoldsTheFrame = TRUE;

#ifdef DEBUG_LOGGING
	{
		Int64 tShadowEnd, freq;
		tShadowEnd = Clock_Ticks();
		freq = Clock_Ticks_Per_Second();
		if( freq > 0 )
			TheShadowMapMS += (Real)((double)(tShadowEnd - tShadowStart) * 1000.0 / (double)freq);
		TheShadowMapCasters += castersCounted;
		TheShadowMapDraws += DX8Wrapper::Get_Draw_Calls() - shadowDrawsBefore;
	}
#endif

	// The report costs a full stall of the pipeline, so it is one line a second rather than one a
	// frame: what it answers is whether the pass draws the world at all, and that does not change
	// thirty times a second.
	if (TheGlobalData->m_shadowMapReport && castersCounted > 0)
	{
		static UnsignedInt nextReportFrame = 0;
		const UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
		if (frame >= nextReportFrame)
		{
			nextReportFrame = frame + LOGICFRAMES_PER_SECOND;
			DEBUG_LOG(("SHADOWMAP: %d casters, %d left out as reaching nothing in view, %d light meshes left out, %s\n",
				castersCounted, castersOutOfView, MeshClass::Shadow_Pass_Light_Meshes,
				Direct3D11_Shadow_Map_Report().c_str()));
		}
	}
}

void W3DVolumetricShadowManager::renderShadows( Bool forceStencilFill )
{
	USE_PERF_TIMER(stencilShadows)
	W3DVolumetricShadow *shadow;
	Int numRenderedShadows = 0;

	/* The volumes stand down only where the sun's map has actually taken this frame.  The casters
		 stay registered either way, which is what keeps them in the map; what stops is the volumes'
		 own darkening pass.  A machine with no Direct3D 11 device fills no map and keeps the volumes,
		 so nobody ends up with no shadows at all. */
	if (TheGlobalData->m_shadowMapOnly && theShadowMapHoldsTheFrame)
		return;

 	AABoxClass bbox;
	SphereClass bsphere;
 
 	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
 	//so much tighter fitting volume than what's actually visible.  This will cull
 	//particles falling under the ground.
 
 	TheTerrainRenderObject->getMaximumVisibleBox(*shadowCameraFrustum, &bbox, TRUE);
 
 	bcX = bbox.Center.X;
 	bcY = bbox.Center.Y;
 	bcZ = bbox.Center.Z;
 	beX = bbox.Extent.X;
 	beY = bbox.Extent.Y;
 	beZ = bbox.Extent.Z;

	if (m_shadowList && TheGlobalData->m_useShadowVolumes)
	{

		LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

		if (!m_pDev)
			return;	//need device to render anything.

 		//According to Nvidia there's a D3D bug that happens if you don't start with a
 		//new dynamic VB each frame - so we force a DISCARD by overflowing the counter.
 		nShadowIndicesInBuf = 0xffff;
 		nShadowVertsInBuf = 0xffff;

		//Set W3D to some known state
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);

		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,NULL);	//turn off textures
		DX8Wrapper::Set_Texture(1,NULL);	//turn off textures
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		// turn off z writing
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	  DX8Wrapper::Set_DX8_Render_State( D3DRS_ZENABLE,          TRUE );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE , FALSE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);


		// setup the TMU to default
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SHADEMODE, D3DSHADE_FLAT);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG2);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );

		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1 );
		DX8Wrapper::Set_DX8_Texture(0,NULL);
		DX8Wrapper::Set_DX8_Texture(1,NULL);

		RenderUInt32 oldColorWriteEnable=0x12345678;

	#ifdef SV_DEBUG
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE , TRUE);
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILENABLE, FALSE );
		DX8Wrapper::Set_DX8_Render_State( D3DRS_SRCBLEND, /*D3DBLEND_DESTCOLOR*/D3DBLEND_ONE );
		DX8Wrapper::Set_DX8_Render_State( D3DRS_DESTBLEND, D3DBLEND_ZERO );
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	#else
		//disable writes to color buffer
		if (DX8Wrapper::Get_Current_Caps()->Get_DX8_Caps().PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE)
		{	DX8Wrapper::_Get_D3D_Device()->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWriteEnable);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,0);
		}
		else
		{	//device does not support disabling writes to color buffer so fake it through alpha blending
			DX8Wrapper::Set_DX8_Render_State( D3DRS_SRCBLEND, D3DBLEND_ZERO );
			DX8Wrapper::Set_DX8_Render_State( D3DRS_DESTBLEND, D3DBLEND_ONE );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE , TRUE);
		}
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILENABLE, TRUE );
	#endif
		//Any pixels with stencil already set to 128 contains a potential occluder.  If this pixels also has any of the player
		//color stencil bits also set, it means that it's an occluded player color and we need to NOT render shadows here.  We
		//do this determination by comparing the value in the combined bits against a value containing only a potential occluder.
		//If the value of just the potential occluder bit is >= than the combined bits, then we know none of the player color
		//bits were set and it's okay to render shadow.
		if (TheW3DShadowManager->getStencilShadowMask() == 0x80808080)
			DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILFUNC,     D3DCMP_NOTEQUAL );	//in this mode, MSB indicates occluded player pixels.
		else
			DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILFUNC,     D3DCMP_GREATEREQUAL );	//in this mode, multiple bits indicate occluded player pixels.
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILREF,      0x80808080 );			//isolate MSB, it's used to indicate pixels containing potential occluders.
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILMASK,     TheW3DShadowManager->getStencilShadowMask());	//isolate upper bits containing PotentialOccluderBit|PlayerColorBits
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILWRITEMASK,0xffffffff );
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP );
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILFAIL,  D3DSTENCILOP_KEEP );
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILPASS,  D3DSTENCILOP_INCR );
		
		DX8Wrapper::Set_Vertex_Format(SHADOW_DYNAMIC_VOLUME_FVF);

		DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE,D3DCULL_CW);
//		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZBIAS,1);	///@todo: See if this helps or makes things worse.
		//DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);


		lastActiveVertexBuffer=NULL;	//reset

		m_dynamicShadowVolumesToRender=NULL;	//clear list of pending dynamic shadows
		W3DVolumetricShadowRenderTask *shadowDynamicTask;

		/* THREADING-ROADMAP.md 3.2 step 1.  This used to be one loop that alternated between
			 rebuilding a caster's silhouette on the CPU and submitting the volumes it had just
			 produced to D3D.  Interleaved like that the CPU half can never be measured on its own,
			 let alone moved off this thread.  The two passes below do exactly what the one loop did;
			 nothing about the geometry or the submitted volumes changes.

			 Deferring the submit is safe because RenderVolume reads only m_shadowVolume[light][mesh],
			 which Update() finished writing, and the mesh transform, which nothing here moves. */
		{
			// CPU: silhouette and extrusion for every enabled caster.  Touches no D3D state.
			USE_PERF_TIMER(shadowVolumeUpdate)
			for( shadow = m_shadowList; shadow; shadow = shadow->m_next )
			{
				if (shadow->m_isEnabled && !shadow->m_isInvisibleEnabled)
					shadow->Update();
			}
		}

		{
			/* D3D: submit the dynamic volumes the pass above accumulated.  They do not have to wait
				 in the static queue - they all share one vertex buffer - so they go out here.

				 addDynamicShadowTask prepends, so this walk is reverse creation order.  That is the
				 same order the decrement pass further down already walks the list in, and it is
				 decided by the list alone, never by the order the CPU pass happened to run in -
				 which is what step 2 needs before it can put that pass on more than one thread. */
			USE_PERF_TIMER(shadowVolumeSubmit)
			for( shadowDynamicTask = m_dynamicShadowVolumesToRender; shadowDynamicTask;
					 shadowDynamicTask = (W3DVolumetricShadowRenderTask *)shadowDynamicTask->m_nextTask )
			{
				shadowDynamicTask->m_parentShadow->RenderVolume( shadowDynamicTask->m_meshIndex,
																												shadowDynamicTask->m_lightIndex );
				numRenderedShadows++;
			}
		}

		// Set vertex format to that used by static shadow volumes
		DX8Wrapper::Set_Vertex_Format(W3DBufferManager::getDX8Format(W3DBufferManager::VBM_FVF_XYZ));

		//Empty queue of static shadow volumes to render.
		W3DBufferManager::W3DVertexBuffer *nextVb;
		W3DVolumetricShadowRenderTask *nextTask;
		for (nextVb=TheW3DBufferManager->getNextVertexBuffer(NULL,W3DBufferManager::VBM_FVF_XYZ);nextVb != NULL; nextVb=TheW3DBufferManager->getNextVertexBuffer(nextVb,W3DBufferManager::VBM_FVF_XYZ))
		{
			nextTask=(W3DVolumetricShadowRenderTask *)nextVb->m_renderTaskList;
			while (nextTask)
			{
				nextTask->m_parentShadow->RenderVolume(nextTask->m_meshIndex,nextTask->m_lightIndex);
				nextTask=(W3DVolumetricShadowRenderTask *)nextTask->m_nextTask;
				numRenderedShadows++;
			}
		}

		// change the stencil op to decrement
		DX8Wrapper::Set_DX8_Render_State( D3DRS_STENCILPASS,  D3DSTENCILOP_DECRSAT);

		//
		// invert normals of shadow volumes so we can decrement in the
		// stencil buffer and render
		//

		DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE,D3DCULL_CCW);

		for (nextVb=TheW3DBufferManager->getNextVertexBuffer(NULL,W3DBufferManager::VBM_FVF_XYZ);nextVb != NULL; nextVb=TheW3DBufferManager->getNextVertexBuffer(nextVb,W3DBufferManager::VBM_FVF_XYZ))
		{
			nextTask=(W3DVolumetricShadowRenderTask *)nextVb->m_renderTaskList;
			while (nextTask)
			{
				nextTask->m_parentShadow->RenderVolume(nextTask->m_meshIndex,nextTask->m_lightIndex);
				nextTask=(W3DVolumetricShadowRenderTask *)nextTask->m_nextTask;
			}
		}

		DX8Wrapper::Set_Vertex_Format(SHADOW_DYNAMIC_VOLUME_FVF);
		//flush any dynamic shadow volumes
		shadowDynamicTask=m_dynamicShadowVolumesToRender;
		while (shadowDynamicTask)
		{	//dynamic shadow columes don't need to wait in queue since they
			//all use the same vertex buffer.  Flush them ASAP.
			shadowDynamicTask->m_parentShadow->RenderVolume(shadowDynamicTask->m_meshIndex,shadowDynamicTask->m_lightIndex);
			shadowDynamicTask=(W3DVolumetricShadowRenderTask *)shadowDynamicTask->m_nextTask;
		}

		//Reset all render tasks for next frame.
		for (nextVb=TheW3DBufferManager->getNextVertexBuffer(NULL,W3DBufferManager::VBM_FVF_XYZ);nextVb != NULL; nextVb=TheW3DBufferManager->getNextVertexBuffer(nextVb,W3DBufferManager::VBM_FVF_XYZ))
		{
			nextVb->m_renderTaskList=NULL;
		}

		DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE,D3DCULL_CW);
//		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZBIAS,0);	///@todo: See if this helps or makes things worse.
		//DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);


		if (oldColorWriteEnable != 0x12345678)
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,oldColorWriteEnable);

		//
		// render the big transparent square of shadows in the stencil buffer
		// to the screen
		//
///@todo: Put this check back in after water is fixed so it doesn't require shadow rendering to fix alpha.
//		if (numRenderedShadows)
			renderStencilShadows();

		DX8Wrapper::Set_DX8_Render_State(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE , FALSE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);

		DX8Wrapper::Invalidate_Cached_Render_States();
	}
	else
	if (forceStencilFill)
	{	//no shadows to render, but still need to fill stencil buffer
		//for other effects.

		//Set W3D to some known state
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		renderStencilShadows();

		DX8Wrapper::Invalidate_Cached_Render_States();
	}

}  // end RenderShadows

/** This class will manage shadow geometry for each render object.  Shadow geometry may
be the same as render geometry but doesn't need to be.  This allows lower LOD versions of
the geometry to be used in shadow calculations.  Shadow geometry also keeps extended vertex
connectivity information that's not used during rendering.
*/
class W3DShadowGeometryManager
{
public:
	W3DShadowGeometryManager(void);
	~W3DShadowGeometryManager(void);

	int			 		Load_Geom(RenderObjClass *robj, const char *name);
	W3DShadowGeometry *		Get_Geom(const char * name);
	W3DShadowGeometry *		Peek_Geom(const char * name);
	Bool					Add_Geom(W3DShadowGeometry *new_anim);
	void			 		Free_All_Geoms(void);

	void					Register_Missing( const char * name );
	Bool					Is_Missing( const char * name );
	void					Reset_Missing( void );

private:

	HashTableClass	*	GeomPtrTable;
	HashTableClass	*	MissingGeomTable;

	friend	class		W3DShadowGeometryManagerIterator;
};

/*
** An Iterator to get to all loaded W3DShadowGeometries in a W3DShadowGeometryManager
*/
class W3DShadowGeometryManagerIterator : public HashTableIteratorClass {
public:
	W3DShadowGeometryManagerIterator( W3DShadowGeometryManager & manager ) : HashTableIteratorClass( *manager.GeomPtrTable ) {}
	W3DShadowGeometry * Get_Current_Geom( void );
};

/** Used to cause a rebuild of all shadow volumes*/
void W3DVolumetricShadowManager::invalidateCachedLightPositions(void)
{

	if (!m_shadowList)
		return;	//there are no shadows to render.

	W3DVolumetricShadow *shadow;
	Vector3 vec(0,0,0);

	// step through each of our shadows and update previous light position.
	for( shadow = m_shadowList; shadow; shadow = shadow->m_next )
	{
		for(Int i = 0; i < MAX_SHADOW_LIGHTS; i++ )
		{
			for (Int meshIndex=0; meshIndex<MAX_SHADOW_CASTER_MESHES; meshIndex++)
			{
				shadow->setLightPosHistory(i,meshIndex,vec);
			}
		}
	}  // end for
}

// W3DVolumetricShadowManager =============================================================
// ============================================================================
W3DVolumetricShadowManager::W3DVolumetricShadowManager( void )
{

	m_shadowList = NULL;

	m_W3DShadowGeometryManager = NEW W3DShadowGeometryManager;

	TheW3DBufferManager = NEW W3DBufferManager;

}  // end ShadowManager

// ~W3DVolumetricShadowManager ============================================================
// ============================================================================
W3DVolumetricShadowManager::~W3DVolumetricShadowManager( void )
{
	ReleaseResources();
	W3DVolumetricShadow::releaseSkinScratch();
	delete m_W3DShadowGeometryManager;
	m_W3DShadowGeometryManager = NULL;
	delete TheW3DBufferManager;
	TheW3DBufferManager=NULL;

	//all shadows should be freed up at this point but check anyway
	assert(m_shadowList==NULL);

}  // end ~W3DVolumetricShadowManager

/** Releases all W3D/D3D assets before a reset.. */
void W3DVolumetricShadowManager::ReleaseResources(void)
{
	if (shadowIndexBufferD3D)
		shadowIndexBufferD3D->Release();
	if (shadowVertexBufferD3D)
		shadowVertexBufferD3D->Release();
	shadowIndexBufferD3D=NULL;
	shadowVertexBufferD3D=NULL;
	delete shadowIndexTwin;
	delete shadowVertexTwin;
	shadowIndexTwin=NULL;
	shadowVertexTwin=NULL;
	if (TheW3DBufferManager)
	{	TheW3DBufferManager->ReleaseResources();
		invalidateCachedLightPositions();	//vertex buffers need to be refilled.
	}
}

/** (Re)allocates all W3D/D3D assets after a reset.. */
Bool W3DVolumetricShadowManager::ReAcquireResources(void)
{
	ReleaseResources();

	LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

	DEBUG_ASSERTCRASH(m_pDev, ("Trying to ReAquireResources on W3DVolumetricShadowManager without device"));

	// Logged for the reason W3DProjectedShadowManager::ReAcquireResources gives.
	RenderResult hr = m_pDev->CreateIndexBuffer
	(
		SHADOW_INDEX_SIZE*sizeof(WORD),
		D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC,
		D3DFMT_INDEX16,
		D3DPOOL_DEFAULT,
		&shadowIndexBufferD3D,
		NULL	// pSharedHandle, D3D9's extra parameter, reserved and always null
	);
	if (Render_Failed(hr))
	{
		DEBUG_LOG(("SHADOW VOLUME BUFFERS: index buffer refused, 0x%08X, cooperative level 0x%08X\n",
			(UnsignedInt)hr, (UnsignedInt)m_pDev->TestCooperativeLevel()));
		return FALSE;
	}

	shadowIndexTwin = Direct3D11_Twin_Index_Buffer(SHADOW_INDEX_SIZE*sizeof(WORD), true);

	if (shadowVertexBufferD3D == NULL)
	{	// Create vertex buffer

		hr = m_pDev->CreateVertexBuffer
		(
			SHADOW_VERTEX_SIZE*sizeof(SHADOW_DYNAMIC_VOLUME_VERTEX),
			D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC,
			0,
			D3DPOOL_DEFAULT,
			&shadowVertexBufferD3D,
			NULL	// pSharedHandle, D3D9's extra parameter, reserved and always null
		);
		if (Render_Failed(hr))
		{
			DEBUG_LOG(("SHADOW VOLUME BUFFERS: vertex buffer refused, 0x%08X, cooperative level 0x%08X\n",
				(UnsignedInt)hr, (UnsignedInt)m_pDev->TestCooperativeLevel()));
			return FALSE;
		}

		shadowVertexTwin = Direct3D11_Twin_Vertex_Buffer(
			SHADOW_VERTEX_SIZE*sizeof(SHADOW_DYNAMIC_VOLUME_VERTEX), true);
	}

	if (TheW3DBufferManager)
		if (!TheW3DBufferManager->ReAcquireResources())
			return FALSE;

	return TRUE;
}

// Init =======================================================================
// User called initialization
// ============================================================================
Bool W3DVolumetricShadowManager::init( void )
{
	return TRUE;
}  // end Init

// Reset ======================================================================
// Reset our list of shadows to empty
// ============================================================================
void W3DVolumetricShadowManager::reset( void )
{

	assert (m_shadowList == NULL);
	m_W3DShadowGeometryManager->Free_All_Geoms();
	TheW3DBufferManager->freeAllBuffers();

}  // end Reset

// addShadow ==================================================================
// Add the shadows for this hierarchy to the shadow management for
// rendering.
// ============================================================================
W3DVolumetricShadow* W3DVolumetricShadowManager::addShadow(RenderObjClass *robj, Shadow::ShadowTypeInfo *shadowInfo, Drawable *draw)
{
	if (!DX8Wrapper::Has_Stencil() || !robj || !TheGlobalData->m_useShadowVolumes)
		return NULL;	//right now we require a stencil buffer

	W3DShadowGeometry *sg=NULL;
	if (!robj)
		return NULL;	//must have a render object in order to read shadow geometry

	const char *name=robj->Get_Name();

	if (!name)
		return NULL;

	sg=m_W3DShadowGeometryManager->Get_Geom(name);

	if (sg==NULL)
	{	//did not find a cached copy of the shadow geometry, create a new one
		m_W3DShadowGeometryManager->Load_Geom(robj,name);
		//try loading again
		sg=m_W3DShadowGeometryManager->Get_Geom(name);
		if (sg==NULL)
			return NULL;	//could not create the shadow geometry
	}

	W3DVolumetricShadow *shadow = NEW W3DVolumetricShadow;	// poolify

	// sanity
	if( shadow == NULL )
		return NULL;

	shadow->setRenderObject(robj);
	shadow->SetGeometry(sg);
 	SphereClass sphere;
 	robj->Get_Obj_Space_Bounding_Sphere(sphere);
 	shadow->setRenderObjExtent(sphere.Radius*MAX_SHADOW_LENGTH_SCALE_FACTOR);

	/* Every aircraft EA shipped asks for ShadowSizeX = 89, a sun no lower than 89 degrees, so every
		 plane and helicopter cast its shadow straight down and towed it along underneath itself at any
		 height - while the tank beside it was lit by the map's real sun and threw its shadow off to the
		 side.  An aircraft takes the same sun as everything else now, so its shadow lands where the
		 sun puts it and slides out from under it as it climbs.  The floor only lifts a sun lower than
		 AIRCRAFT_MIN_SUN_ELEVATION, so a dusk map does not throw a Comanche's shadow across half the
		 screen. */
	const Real AIRCRAFT_MIN_SUN_ELEVATION = 30.0f;
	Drawable *owner = draw;
	if (owner == NULL && robj->Get_User_Data())
		owner = ((DrawableInfo *)robj->Get_User_Data())->m_drawable;

	Real sunElevation = shadowInfo->m_sizeX;
	if (owner && owner->isKindOf(KINDOF_AIRCRAFT) && sunElevation > AIRCRAFT_MIN_SUN_ELEVATION)
		sunElevation = AIRCRAFT_MIN_SUN_ELEVATION;

	Real sunElevationAngleTan = 0;
	if (sunElevation)
	{	//need to adjust sun elevation for this model in order to limit shadow length
		sunElevationAngleTan=tan(sunElevation/180.0f*PI);
	}
	shadow->setShadowLengthScale(sunElevationAngleTan);

	if (!draw || !draw->isKindOf(KINDOF_IMMOBILE))
		shadow->setOptimalExtrusionPadding(SHADOW_EXTRUSION_BUFFER);

	// add to our shadow list through the shadow next links
	shadow->m_next = m_shadowList;
	m_shadowList = shadow;	
	return shadow;
}

/** removeShadow ===========================================================
 Removes the shadows for this hierarchy from the shadow manger.  No further
 shadows from this caster will be rendered.
 ===========================================================================
*/
void W3DVolumetricShadowManager::removeShadow(W3DVolumetricShadow *shadow)
{
	W3DVolumetricShadow *prev_shadow=NULL;
	W3DVolumetricShadow *next_shadow=NULL;

	//search for this shadow
	for( next_shadow = m_shadowList; next_shadow; prev_shadow=next_shadow, next_shadow = next_shadow->m_next )
	{
		if (next_shadow == shadow)
		{
			if (prev_shadow)
				prev_shadow->m_next=shadow->m_next;
			else
				m_shadowList=shadow->m_next;

			delete shadow;
			break;
		}
	}  // end for
}

/** removeAllShadows ===========================================================
 Removes all shadows from the shadow manger.  No further
 shadows will be rendered.
 ===========================================================================
*/
void W3DVolumetricShadowManager::removeAllShadows(void)
{
	W3DVolumetricShadow *cur_shadow=NULL;
	W3DVolumetricShadow *next_shadow=m_shadowList;
	m_shadowList = NULL;

	//search for this shadow
	for( cur_shadow = next_shadow; cur_shadow; cur_shadow = next_shadow )
	{
		next_shadow = cur_shadow->m_next;
		cur_shadow->m_next = NULL;
		delete cur_shadow;
	}  // end for
}

W3DShadowGeometryManager::W3DShadowGeometryManager(void) 
{
	// Create the hash tables
	GeomPtrTable = NEW HashTableClass( 2048 );
	MissingGeomTable = NEW HashTableClass( 2048 );
}

W3DShadowGeometryManager::~W3DShadowGeometryManager(void)
{
	Free_All_Geoms();

	delete GeomPtrTable;
	GeomPtrTable = NULL;

	delete MissingGeomTable;
	MissingGeomTable = NULL;
}

/** Release all loaded animations */
void W3DShadowGeometryManager::Free_All_Geoms(void)
{
	// Make an iterator, and release all ptrs
	W3DShadowGeometryManagerIterator it( *this );
	for( it.First(); !it.Is_Done(); it.Next() ) {
		W3DShadowGeometry *geom = it.Get_Current_Geom();
		geom->Release_Ref();
	}

	// Then clear the table
	GeomPtrTable->Reset();
}

/** Find animation in cache */
W3DShadowGeometry * W3DShadowGeometryManager::Peek_Geom(const char * name)
{
	return (W3DShadowGeometry*)GeomPtrTable->Find( name );
}

/** Get animation from cache and increment its reference count */
W3DShadowGeometry * W3DShadowGeometryManager::Get_Geom(const char * name)
{	
	W3DShadowGeometry * geom = Peek_Geom( name );
	if ( geom != NULL ) {
		geom->Add_Ref();
	}
	return geom;
}

/** Add animation to cache */
Bool W3DShadowGeometryManager::Add_Geom(W3DShadowGeometry *new_geom)
{
	WWASSERT (new_geom != NULL);

	// Increment the refcount on the new animation and add it to our table.
	new_geom->Add_Ref ();
	GeomPtrTable->Add( new_geom );

	return true;
}

/*
** An entry for a table of anims not found, so we can quickly determine their loss
*/
class MissingGeomClass : public HashableClass {

public:
	MissingGeomClass( const char * name ) : Name( name ) {}
	virtual	~MissingGeomClass( void ) {}

	virtual	const char * Get_Key( void )	{ return Name;	}

private:
	StringClass	Name;

};

/*
** Missing Geoms
**
** The idea here, allow the system to register which anims are determined to be missing
** so that if they are asked for again, we can quickly return NULL, without searching the
** disk again.
*/
void	W3DShadowGeometryManager::Register_Missing( const char * name )
{
	MissingGeomTable->Add( NEW MissingGeomClass( name ) );
}

Bool	W3DShadowGeometryManager::Is_Missing( const char * name )
{
	return ( MissingGeomTable->Find( name ) != NULL );
}

/** Create shadow geometry from a reference W3D RenderObject*/
int W3DShadowGeometryManager::Load_Geom(RenderObjClass *robj, const char *name)
{
	Bool res=FALSE;

	W3DShadowGeometry * newgeom = NEW W3DShadowGeometry;

	if (newgeom == NULL) {
		goto Error;
	}

	SET_REF_OWNER( newgeom );

	newgeom->Set_Name(name);

	switch (robj->Class_ID())
	{
		case RenderObjClass::CLASSID_HLOD:
			res=newgeom->initFromHLOD(robj);
			break;
		case RenderObjClass::CLASSID_MESH:
			res=newgeom->initFromMesh(robj);
			break;
		default:
			break;	//unknown render object type
	};

	if (res != TRUE)
	{	// load failed!
		newgeom->Release_Ref();
		//DEBUG_LOG(("****Shadow Volume Creation Failed on %s\n",name));
		goto Error;
	} else if (Peek_Geom(newgeom->Get_Name()) != NULL)
	{	// duplicate exists!
		newgeom->Release_Ref();	// Release the one we just loaded
		goto Error;
	} else
	{	Add_Geom( newgeom );
		newgeom->Release_Ref();
	}

	return 0;

Error:
	return 1;
}

/*
** Iterator converter from HashableClass to GrannyAnimClass
*/
W3DShadowGeometry * W3DShadowGeometryManagerIterator::Get_Current_Geom( void )	
{ 
	return (W3DShadowGeometry *)Get_Current(); 
}
