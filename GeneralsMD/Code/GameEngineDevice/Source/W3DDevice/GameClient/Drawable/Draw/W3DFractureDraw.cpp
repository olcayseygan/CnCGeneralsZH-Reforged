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

// FILE: W3DFractureDraw.cpp
// See W3DFractureDraw.h.  One file's fragments are loaded once and shared (Shared): the model space
// vertices in static vertex buffers of at most 65535 vertices and 65535 indices each (Chunk), so the
// unsigned short draw arguments reach all of them, with every fragment's triangles in one index
// buffer by fragment and material.  A structure keeps its own index buffer of what still stands,
// grouped by the bone a fragment rides on and its material, rebuilt only when a hit changes it, so
// the standing structure is a handful of draws a frame.  What moves (debris, the collapse) is drawn a
// fragment at a time with its own transform, and the settled ruin is baked once into vertex buffers
// of its own and drawn like the standing structure.  Every draw goes through DX8Wrapper, which
// reaches both the Direct3D 11 frame and -d3d9.

#include "Common/ArchiveFileSystem.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Common/GameCommon.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/Drawable.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/FractureCore.h"
#include "W3DDevice/GameClient/Module/W3DFractureDraw.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DVolumetricShadow.h"
#include "Platform/SunShadow.h"
#include "WW3D2/coltest.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/hlod.h"
#include "WW3D2/matinfo.h"
#include "WW3D2/matpass.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/shader.h"
#include "WW3D2/texture.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/w3d_file.h"
#include "WW3D2/w3d_util.h"
#include "WW3D2/ww3d.h"
#include "WWMath/colmath.h"
#include "WWMath/lineseg.h"
#include "WWMath/quat.h"

#include <chrono>

namespace {

const Real GRAVITY = 80.0f;					// units a game second squared, the look test's: a 15 unit soldier is 1.8 m
const Real HOLE_SCALE = 0.4f;				// of a blast's radius, as the piece damage reach had it
const Real MIN_HOLE = 3.0f;					// a direct hit, or a small blast
const Real MAX_HOLE = 12.0f;
const Real DAMAGE_SHARE = 0.9f;			// of the structure's volume a hit may have taken by the share of health it has lost
const Real STEP = 1.0f / 60.0f;			// game seconds a physics step
const Int MAX_STEPS = 120;					// a picture's worth at most
const Real DEBRIS_LIES = 6.0f;			// game seconds a knocked out fragment lies before it goes into the ground
const Real DEBRIS_SINKS = 1.5f;
const Real FALL_SECONDS = 8.0f;			// a plain collapse is over by then, settled or not; a baked one that long past its track
const Real COLLAPSE_DELAY = 0.5f;		// game seconds the structure stands after the death, so the death flash's glare has passed
const Real PLAIN_FALL_DELAY = 0.1f;	// a plain fall's: at 0.5 the GLA Command Center read late, the object already gone
const Real PLAIN_FALL_SCATTER = 1.5f;	// units a game second at most a plain fall's piece goes sideways at random
const Real LEVEL_REACH = 12.0f;				// units from under where it stood a plain fall's piece may land at least
const Real LEVEL_REACH_PER_HEIGHT = 1.0f;	// and further by this of its height: a tower's top lands across the yard
const Real LEVEL_REACH_MAX = 60.0f;
const Real LEVEL_DISTANCE_COST = 0.04f;	// units of rubble height a unit further is worth when a piece picks where it lands
// of a landed piece's volume the rubble under the next one rises by: the cut fills the building solid
// where it had rooms. At a half the Command Center's pieces lie 95% under 19.5 (its main walls stand 29)
const Real PILE_SHARE = 0.5f;
const Real FOOTPRINT_INSET = 4.0f;		// a plain fall's pieces land this far inside the model's box
const Real PLAIN_FALL_DROP = 4.0f;		// and at most downward, so it is moving at the first frame
const Real SINKER_LIES = 0.3f;				// game seconds a FRAGMENT_SINKS piece lies once down before it goes into the ground
const Real REPAIR_MARGIN = 0.05f;			// of health over its level when a fragment went before it comes back
const Real REPAIR_WHOLE = 0.001f;			// of health lost at most: full health, every fragment back
const Int REPAIR_PUFFS = 6;						// dust puffs at most for the fragments one repair step brings back
const Real DROP_GAP = 1.5f;					// a piece of the ruin with more air than this under its box falls the rest of the way
const Real SUPPORT_INSET = 0.5f;			// how far two boxes have to overlap across before one holds the other up
const Real NO_REST_Z = -1e30f;
const Real TRACK_HANDOFF_SPEED = 3.0f;	// units a game second: a piece faster than this at its bake's last key carries on falling
const Real SLIDE_STOP = 1.5f;					// a piece on the ground slower than this sideways stops
const Real SLIDE_FRICTION = 4.0f;			// of its sideways speed lost a game second on the ground
const Real LONG_AGO = 1000.0f;				// game seconds: a death this far back has long finished falling
const Real ORPHAN_RUIN_LIES = 12.0f;	// game seconds the ruin of a structure whose object went at its death lies
const Real ORPHAN_RUIN_SINKS = 4.0f;	// and then sinks out of sight
const Real GROUND_FOLLOW_HEIGHT = 20.0f;	// a baked fragment this far down to the model's foot lies on the ground under it
const Real BOUNDS_MARGIN = 150.0f;		// how far past the model debris and the fall may reach, for the culling box (the tower's diagonal falls throw 146)
const Real DUST_VOLUME = 6.0f;			// a fragment at least this big raises dust where it comes down
const Int MAX_DUST = 24;						// puffs a structure, over its life
const char *const DUST_SYSTEM = "TreeBounceDust";
const Int PILE_CELLS = 64;					// the rubble heights a plain collapse lands on, round the structure
const Real PILE_CELL = 4.0f;
const Real VOLUME_UNDER_GROUND = 2.0f;	// a stencil volume ends this far under the map's lowest ground, so no end is in view
const Real VOLUME_REACH = 3000.0f;		// and never reaches further than this, however low the sun
const Real VOLUME_LOWEST_SUN = 0.05f;	// a sun lower than this (the light's way, straight down 1) takes VOLUME_REACH
const Int VOLUME_EDGES_A_DRAW = 16384;	// the shadow pass's streaming buffers: 65536 corners, 98304 indices
const Real VOLUME_MOVING_STEP = 1.0f / 20.0f;	// game seconds between two builds of what moves: a logic frame at 1.5x

// matrix3d.h keeps its operators behind ALLOW_TEMPORARIES
Vector3 operator*( const Matrix3D &matrix, const Vector3 &point )
{
	Vector3 result;
	Matrix3D::Transform_Vector( matrix, point, &result );
	return result;
}

Matrix3D operator*( const Matrix3D &left, const Matrix3D &right )
{
	Matrix3D result;
	Matrix3D::Multiply( left, right, &result );
	return result;
}

/// One static vertex buffer's worth of fragments: at most 65535 vertices and 65535 indices.
struct Chunk
{
	DX8VertexBufferClass *vertices;
	DX8IndexBufferClass *all;				///< every fragment's triangles, by fragment, each by material
	std::vector<unsigned short> indices;	///< the same, for building the standing index buffers
	unsigned vertexCount;
};

/// A run of one fragment's triangles of one material, in its chunk's indices.
struct Range
{
	unsigned short material;
	unsigned start, count;
};

/** One edge of a fragment for its stencil volume: its ends as the left triangle winds them (fragment
	vertices, welded by position) and the triangle on each side, right -1 on an open edge. */
struct VolumeEdge
{
	unsigned short a, b;
	Int left, right;
};

/// One fracture file, shared by every structure drawn from it.
struct Shared
{
	AsciiString name;
	Bool ok;
	Fracture::Asset asset;
	std::vector<Fracture::Collapse> collapses;		///< the ways down it was baked with, none for a plain fall
	std::vector<Chunk> chunks;
	std::vector<unsigned> chunkOf, baseVertex;			///< per fragment: its chunk and its first vertex there
	std::vector< std::vector<Range> > ranges;				///< per fragment
	std::vector<unsigned short> slotOf;							///< per fragment: the bone it rides on
	std::vector<AsciiString> boneNames;							///< per slot
	std::vector<Matrix3D> boneInverse;							///< per slot: the bone's built pose, inverted
	std::vector<TextureClass *> textures, damaged;	///< per material; damaged is the D sheet the damaged models wear
	std::vector<ShaderClass> shaders;
	std::vector<Fracture::MeshLook> cutLook;				///< the cut model's meshes and their textures, sub-object order
	std::vector<Int> lookMesh, lookSlot;						///< per material: where its texture sits in cutLook, -1 nowhere (an inner face)
	VertexMaterialClass *material;
	Int users;																			///< structures drawn from it now
	std::vector<VolumeEdge> edges;									///< the stencil volumes', made at the first need
	std::vector<unsigned> firstEdge;								///< per fragment and one past the last
	std::vector<Vector3> faceNormal;								///< per triangle, model space, as W3DShadowGeometry crosses them
};

/** The edges the stencil volumes' silhouettes are found on, every fragment's once.  A fragment's
	vertices are split where its normals or its UVs are, so they are welded by position first; an edge
	two triangles share the opposite way round joins them, any other edge is open. */
void makeVolumeEdges( Shared &s )
{
	if( !s.firstEdge.empty() )
		return;
	const Fracture::Asset &a = s.asset;
	s.faceNormal.resize( a.triangles.size() );
	struct Side { unsigned lo, hi; Int triangle; unsigned short a, b; };
	std::vector<unsigned short> weld;
	std::vector<unsigned> order;
	std::vector<Side> sides;
	for( size_t i = 0; i < a.fragments.size(); ++i )
	{
		s.firstEdge.push_back( (unsigned)s.edges.size() );
		const Fracture::Fragment &f = a.fragments[i];
		const Fracture::Vertex *v = &a.vertices[f.firstVertex];
		order.resize( f.vertices );
		for( unsigned k = 0; k < f.vertices; ++k )
			order[k] = k;
		struct ByPosition
		{
			const Fracture::Vertex *v;
			bool operator()( unsigned l, unsigned r ) const
			{
				for( Int c = 0; c < 3; ++c )
					if( v[l].pos[c] != v[r].pos[c] )
						return v[l].pos[c] < v[r].pos[c];
				return l < r;
			}
		};
		const ByPosition byPosition = { v };
		std::sort( order.begin(), order.end(), byPosition );
		weld.resize( f.vertices );
		for( unsigned k = 0; k < f.vertices; ++k )
			weld[order[k]] = ( k > 0 && memcmp( v[order[k]].pos, v[order[k - 1]].pos, sizeof( v[0].pos ) ) == 0 )
				? weld[order[k - 1]] : (unsigned short)order[k];
		sides.clear();
		for( unsigned t = 0; t < f.triangles; ++t )
		{
			const Fracture::Triangle &tri = a.triangles[f.firstTriangle + t];
			s.faceNormal[f.firstTriangle + t].Set( 0.0f, 0.0f, 0.0f );
			// alpha tested (railings, nets): no slab of shadow, as initFromHLOD leaves a translucent mesh
			// out; the triangles beside it see an open edge.  MATERIAL_DECAL is opaque, only two-sided
			// (shaderFor), and casts as the opaque two-sided meshes initFromHLOD keeps
			if( tri.material < a.materials.size() && ( a.materials[tri.material].flags & Fracture::MATERIAL_ALPHA_TEST ) )
				continue;
			const Vector3 p0( v[tri.index[0]].pos[0], v[tri.index[0]].pos[1], v[tri.index[0]].pos[2] );
			const Vector3 p1( v[tri.index[1]].pos[0], v[tri.index[1]].pos[1], v[tri.index[1]].pos[2] );
			const Vector3 p2( v[tri.index[2]].pos[0], v[tri.index[2]].pos[1], v[tri.index[2]].pos[2] );
			Vector3 n;
			Vector3::Cross_Product( p1 - p0, p2 - p0, &n );
			if( n.Length2() > 0.0f )
				n.Normalize();
			s.faceNormal[f.firstTriangle + t] = n;
			for( Int k = 0; k < 3; ++k )
			{
				const unsigned short from = weld[tri.index[k]], to = weld[tri.index[( k + 1 ) % 3]];
				if( from == to )
					continue;
				const Side side = { std::min( from, to ), std::max( from, to ), (Int)( f.firstTriangle + t ), from, to };
				sides.push_back( side );
			}
		}
		struct ByEdge
		{
			bool operator()( const Side &l, const Side &r ) const { return l.lo != r.lo ? l.lo < r.lo : l.hi != r.hi ? l.hi < r.hi : l.triangle < r.triangle; }
		};
		std::sort( sides.begin(), sides.end(), ByEdge() );
		for( size_t k = 0; k < sides.size(); )
		{
			size_t end = k + 1;
			while( end < sides.size() && sides[end].lo == sides[k].lo && sides[end].hi == sides[k].hi )
				++end;
			if( end - k == 2 && sides[k].a == sides[k + 1].b )
			{
				const VolumeEdge e = { sides[k].a, sides[k].b, sides[k].triangle, sides[k + 1].triangle };
				s.edges.push_back( e );
			}
			else
			{
				for( size_t o = k; o < end; ++o )
				{
					const VolumeEdge e = { sides[o].a, sides[o].b, sides[o].triangle, -1 };
					s.edges.push_back( e );
				}
			}
			k = end;
		}
	}
	s.firstEdge.push_back( (unsigned)s.edges.size() );
	DEBUG_LOG( ( "FRACTURE VOLUME EDGES %s: %d edges over %d fragments\n", s.name.str(), (Int)s.edges.size(), (Int)a.fragments.size() ) );
}

std::vector<Shared *> s_shared;

/// The FRACTURE COST line's counter: CPU time in the steps, the draws and the shadow casts.
const UnsignedInt COST_REPORT_MS = 1000;
double s_costUs = 0.0;
UnsignedInt s_costReportStart = 0;
Int s_volumeEdges = 0;								///< silhouette edges the stencil volumes built pictures held, since the last line

struct CostTimer
{
	std::chrono::steady_clock::time_point started;
	CostTimer() : started( std::chrono::steady_clock::now() ) { }
	~CostTimer() { s_costUs += std::chrono::duration<double, std::micro>( std::chrono::steady_clock::now() - started ).count(); }
};

/// Every buffer and texture of the files no structure draws from any more.
void freeUnusedShared( void )
{
	size_t kept = 0;
	for( size_t i = 0; i < s_shared.size(); ++i )
	{
		Shared *s = s_shared[i];
		if( !s->ok || s->users > 0 )
		{
			s_shared[kept++] = s;
			continue;
		}
		for( size_t c = 0; c < s->chunks.size(); ++c )
		{
			REF_PTR_RELEASE( s->chunks[c].vertices );
			REF_PTR_RELEASE( s->chunks[c].all );
		}
		for( size_t m = 0; m < s->textures.size(); ++m )
		{
			REF_PTR_RELEASE( s->textures[m] );
			REF_PTR_RELEASE( s->damaged[m] );
		}
		REF_PTR_RELEASE( s->material );
		delete s;
	}
	s_shared.resize( kept );
}

/// Art/W3D/<name><extension> whole, from a loose file or an archive.
Bool readFile( const AsciiString &name, const char *extension, std::vector<unsigned char> &out )
{
	AsciiString path;
	path.format( "Art/W3D/%s%s", name.str(), extension );
	File *file = TheFileSystem->openFile( path.str(), File::READ | File::BINARY );
	if( file == NULL )
		return FALSE;
	const Int size = file->size();
	char *data = file->readEntireAndClose();
	if( data == NULL || size <= 0 )
	{
		delete [] data;
		return FALSE;
	}
	out.assign( (unsigned char *)data, (unsigned char *)data + size );
	delete [] data;
	return TRUE;
}

/// The names of a W3D file's meshes in file order, container.mesh: what a fragment's part counts.
void meshNames( const std::vector<unsigned char> &w3d, std::vector<AsciiString> &out )
{
	size_t at = 0;
	while( at + 8 <= w3d.size() )
	{
		unsigned type, size;
		memcpy( &type, &w3d[at], 4 );
		memcpy( &size, &w3d[at + 4], 4 );
		size &= 0x7FFFFFFF;
		const size_t body = at + 8, end = body + size;
		if( end > w3d.size() )
			break;
		if( type == W3D_CHUNK_MESH )
		{
			for( size_t sub = body; sub + 8 <= end; )
			{
				unsigned subType, subSize;
				memcpy( &subType, &w3d[sub], 4 );
				memcpy( &subSize, &w3d[sub + 4], 4 );
				subSize &= 0x7FFFFFFF;
				if( subType == W3D_CHUNK_MESH_HEADER3 && sub + 8 + sizeof( W3dMeshHeader3Struct ) <= end )
				{
					W3dMeshHeader3Struct header;
					memcpy( &header, &w3d[sub + 8], sizeof( header ) );
					char mesh[W3D_NAME_LEN + 1], container[W3D_NAME_LEN + 1];
					memcpy( mesh, header.MeshName, W3D_NAME_LEN );
					memcpy( container, header.ContainerName, W3D_NAME_LEN );
					mesh[W3D_NAME_LEN] = container[W3D_NAME_LEN] = 0;
					AsciiString full;
					full.format( "%s.%s", container, mesh );
					out.push_back( full );
					break;
				}
				sub += 8 + subSize;
			}
		}
		at = end;
	}
}

ShaderClass shaderFor( const Fracture::Material &m )
{
	W3dShaderStruct w3d;
	memset( &w3d, 0, sizeof( w3d ) );
	w3d.DepthCompare = W3DSHADER_DEPTHCOMPARE_PASS_LEQUAL;		// as LWTOWER.W3D and LWFORT.W3D have it
	w3d.DepthMask = W3DSHADER_DEPTHMASK_WRITE_ENABLE;
	w3d.DestBlend = W3DSHADER_DESTBLENDFUNC_ZERO;
	w3d.PriGradient = W3DSHADER_PRIGRADIENT_MODULATE;
	w3d.SrcBlend = W3DSHADER_SRCBLENDFUNC_ONE;
	w3d.Texturing = W3DSHADER_TEXTURING_ENABLE;
	w3d.AlphaTest = ( m.flags & Fracture::MATERIAL_ALPHA_TEST ) ? W3DSHADER_ALPHATEST_ENABLE : W3DSHADER_ALPHATEST_DISABLE;
	ShaderClass shader;
	W3dUtilityClass::Convert_Shader( w3d, &shader );
	shader.Enable_Fog( "W3DFractureDraw" );		// as the mesh loader does every shader
	shader.Set_Cull_Mode( ( m.flags & Fracture::MATERIAL_DECAL ) ? ShaderClass::CULL_MODE_DISABLE : ShaderClass::CULL_MODE_ENABLE );
	return shader;
}

/** The damaged models' sheet for a texture when there is one: retail's ABBtCmdHQ.tga to ABBtCmdHQ_D.tga,
	or the lane art's LWFort_Stone.tga to LWFort_StoneD.tga.  Only for a material the live model's own
	look does not reach (takeLook). */
TextureClass *damagedTexture( const char *name )
{
	std::string names[2];
	if( !Fracture::damagedNames( name, names ) )
		return NULL;
	for( Int k = 0; k < 2; ++k )
	{
		// the asset manager serves a .tga name from a .dds beside it
		const size_t dot = names[k].rfind( '.' );
		const std::string dds = names[k].substr( 0, dot ) + ".dds";
		AsciiString path, ddsPath;
		path.format( "Art/Textures/%s", names[k].c_str() );
		ddsPath.format( "Art/Textures/%s", dds.c_str() );
		if( TheFileSystem->doesFileExist( path.str() ) || TheFileSystem->doesFileExist( ddsPath.str() ) )
			return WW3DAssetManager::Get_Instance()->Get_Texture( names[k].c_str() );
	}
	return NULL;
}

/** A model's meshes as the fragments' look follows them, in sub-object order (every level of an HLOD),
	and in textures, when asked, each texture they wear, peeked: good while the model lives. */
void modelLook( RenderObjClass *model, std::vector<Fracture::MeshLook> &out, std::vector< std::vector<TextureClass *> > *textures )
{
	out.clear();
	if( textures )
		textures->clear();
	for( Int i = 0; model && i < model->Get_Num_Sub_Objects(); ++i )
	{
		RenderObjClass *sub = model->Get_Sub_Object( i );
		out.push_back( Fracture::MeshLook() );
		if( textures )
			textures->push_back( std::vector<TextureClass *>() );
		if( sub == NULL )
			continue;
		out.back().mesh = Fracture::meshKey( sub->Get_Name() );
		MaterialInfoClass *info = sub->Get_Material_Info();
		for( Int k = 0; info && k < info->Texture_Count(); ++k )
		{
			TextureClass *texture = info->Peek_Texture( k );
			out.back().textures.push_back( Fracture::textureKey( texture ? texture->Get_Texture_Name().Peek_Buffer() : "" ) );
			if( textures )
				textures->back().push_back( texture );
		}
		REF_PTR_RELEASE( info );
		REF_PTR_RELEASE( sub );
	}
}

/** The fragments cut from the model the file system serves.  A Reforged batch's rebuilt model (Detailed
	Models on) has its own set, <name>_Detailed.lwfrag, so its first hit does not swap it for retail's
	pieces; without one, or with the retail model, <name>.lwfrag. */
AsciiString fractureFile( const AsciiString &name )
{
	AsciiString model, detailed, path;
	model.format( "Art\\W3D\\%s.w3d", name.str() );
	detailed.format( "%s_Detailed", name.str() );
	path.format( "Art/W3D/%s.lwfrag", detailed.str() );
	const Bool rebuilt = TheArchiveFileSystem != NULL
		&& TheArchiveFileSystem->getArchiveFilenameForFile( model ) != TheArchiveFileSystem->getRetailArchiveFilenameForFile( model );
	return rebuilt && TheFileSystem->doesFileExist( path.str() ) ? detailed : name;
}

Bool load( Shared &s )
{
	std::vector<unsigned char> bytes;
	const AsciiString file = fractureFile( s.name );
	if( !readFile( file, ".lwfrag", bytes ) || !Fracture::parse( &bytes[0], bytes.size(), s.asset ) )
	{
		DEBUG_LOG( ( "FRACTURE: Art/W3D/%s.lwfrag missing or unreadable\n", file.str() ) );
		return FALSE;
	}
	const Fracture::Asset &a = s.asset;
	if( readFile( file, ".lwcollapse", bytes ) && !Fracture::parseCollapses( &bytes[0], bytes.size(), a.fragments.size(), s.collapses ) )
		DEBUG_LOG( ( "FRACTURE: Art/W3D/%s.lwcollapse does not fit its .lwfrag, a plain fall instead\n", file.str() ) );

	// the bone each fragment's mesh hangs on, by the cut model's mesh order, and that bone's built pose
	std::vector<AsciiString> parts;
	if( readFile( s.name, ".w3d", bytes ) )
		meshNames( bytes, parts );
	RenderObjClass *model = W3DDisplay::m_assetManager->Create_Render_Obj( s.name.str(), 1.0f, 0 );
	if( model == NULL )
		return FALSE;
	model->Set_Transform( Matrix3D( true ) );
	std::vector<Int> slotOfPart( parts.size(), 0 );
	s.boneNames.push_back( AsciiString::TheEmptyString );		// slot 0, the model's own root
	s.boneInverse.push_back( Matrix3D( true ) );
	for( size_t p = 0; p < parts.size(); ++p )
	{
		RenderObjClass *sub = model->Get_Sub_Object_By_Name( parts[p].str() );
		if( sub == NULL )
			continue;
		const Int bone = model->Get_Sub_Object_Bone_Index( sub );
		sub->Release_Ref();
		const char *boneName = bone > 0 ? model->Get_Bone_Name( bone ) : NULL;
		if( boneName == NULL )
			continue;
		size_t slot = 0;
		while( slot < s.boneNames.size() && s.boneNames[slot].compareNoCase( boneName ) != 0 )
			++slot;
		if( slot == s.boneNames.size() )
		{
			Matrix3D inverse;
			model->Get_Bone_Transform( bone ).Get_Orthogonal_Inverse( inverse );
			s.boneNames.push_back( AsciiString( boneName ) );
			s.boneInverse.push_back( inverse );
		}
		slotOfPart[p] = (Int)slot;
	}
	modelLook( model, s.cutLook, NULL );
	REF_PTR_RELEASE( model );

	// chunks of whole fragments, the triangles of each by material
	const size_t count = a.fragments.size();
	s.chunkOf.resize( count );
	s.baseVertex.resize( count );
	s.ranges.resize( count );
	s.slotOf.resize( count );
	for( size_t i = 0; i < count; ++i )
	{
		const Fracture::Fragment &f = a.fragments[i];
		s.slotOf[i] = (unsigned short)( f.part < slotOfPart.size() ? slotOfPart[f.part] : 0 );
		if( s.chunks.empty() || s.chunks.back().vertexCount + f.vertices > 65535
			|| s.chunks.back().indices.size() + 3 * f.triangles > 65535 )
		{
			Chunk chunk;
			chunk.vertices = NULL;
			chunk.all = NULL;
			chunk.vertexCount = 0;
			s.chunks.push_back( chunk );
		}
		Chunk &chunk = s.chunks.back();
		s.chunkOf[i] = (unsigned)( s.chunks.size() - 1 );
		s.baseVertex[i] = chunk.vertexCount;
		for( unsigned short m = 0; m < a.materials.size(); ++m )
		{
			Range range = { m, (unsigned)chunk.indices.size(), 0 };
			for( unsigned t = 0; t < f.triangles; ++t )
			{
				const Fracture::Triangle &tri = a.triangles[f.firstTriangle + t];
				if( tri.material != m )
					continue;
				for( int k = 0; k < 3; ++k )
					chunk.indices.push_back( (unsigned short)( chunk.vertexCount + tri.index[k] ) );
				range.count += 3;
			}
			if( range.count )
				s.ranges[i].push_back( range );
		}
		chunk.vertexCount += f.vertices;
	}
	for( size_t c = 0; c < s.chunks.size(); ++c )
	{
		Chunk &chunk = s.chunks[c];
		chunk.vertices = NEW_REF( DX8VertexBufferClass, ( DX8_FVF_XYZNUV1, (unsigned short)chunk.vertexCount ) );
		chunk.all = NEW_REF( DX8IndexBufferClass, ( (unsigned short)chunk.indices.size() ) );
		{
			DX8VertexBufferClass::WriteLockClass lock( chunk.vertices );
			VertexFormatXYZNUV1 *v = (VertexFormatXYZNUV1 *)lock.Get_Vertex_Array();
			for( size_t i = 0; v && i < count; ++i )
			{
				if( s.chunkOf[i] != c )
					continue;
				const Fracture::Fragment &f = a.fragments[i];
				for( unsigned k = 0; k < f.vertices; ++k )
				{
					const Fracture::Vertex &src = a.vertices[f.firstVertex + k];
					VertexFormatXYZNUV1 &dst = v[s.baseVertex[i] + k];
					dst.x = src.pos[0]; dst.y = src.pos[1]; dst.z = src.pos[2];
					dst.nx = src.normal[0]; dst.ny = src.normal[1]; dst.nz = src.normal[2];
					dst.u1 = src.uv[0];
					dst.v1 = 1.0f - src.uv[1];		// stored bottom-up, as the W3D loader flips it
				}
			}
		}
		{
			DX8IndexBufferClass::WriteLockClass lock( chunk.all );
			unsigned short *ib = lock.Get_Index_Array();
			if( ib )
				memcpy( ib, &chunk.indices[0], chunk.indices.size() * sizeof( unsigned short ) );
		}
	}

	for( size_t m = 0; m < a.materials.size(); ++m )
	{
		s.textures.push_back( WW3DAssetManager::Get_Instance()->Get_Texture( a.materials[m].texture ) );
		s.damaged.push_back( ( a.materials[m].flags & Fracture::MATERIAL_INNER ) ? NULL : damagedTexture( a.materials[m].texture ) );
		s.shaders.push_back( shaderFor( a.materials[m] ) );
		unsigned mesh = 0, slot = 0;
		const Bool worn = !( a.materials[m].flags & Fracture::MATERIAL_INNER )
			&& Fracture::findTexture( s.cutLook, Fracture::textureKey( a.materials[m].texture ), mesh, slot );
		s.lookMesh.push_back( worn ? (Int)mesh : -1 );
		s.lookSlot.push_back( worn ? (Int)slot : -1 );
	}
	s.material = NEW_REF( VertexMaterialClass, () );		// the art's "default": white ambient and diffuse, lit
	s.material->Set_Lighting( true );
	DEBUG_LOG( ( "FRACTURE: %s from %s.lwfrag, %d fragments in %d chunks, %d bones, collapse baked %d ways (none: a plain fall)\n", s.name.str(),
		file.str(), (Int)count, (Int)s.chunks.size(), (Int)s.boneNames.size(), (Int)s.collapses.size() ) );
	return TRUE;
}

Shared *sharedFor( const AsciiString &name )
{
	for( size_t i = 0; i < s_shared.size(); ++i )
		if( s_shared[i]->name.compareNoCase( name ) == 0 )
			return s_shared[i]->ok ? s_shared[i] : NULL;
	Shared *s = NEW Shared;
	s->name = name;
	s->material = NULL;
	s->users = 0;
	s->ok = load( *s );
	s_shared.push_back( s );
	return s->ok ? s : NULL;
}

/// Game seconds, between ticks too: a pause holds everything still.
Real gameSeconds( void )
{
	return ( (Real)TheGameLogic->getFrame() + WWMath::Clamp( GameEngine_logicTickFraction(), 0.0f, 1.0f ) ) / LOGICFRAMES_PER_GAME_SECOND;
}

Vector3 randomAxis( void )
{
	Vector3 axis( GameClientRandomValueReal( -1.0f, 1.0f ), GameClientRandomValueReal( -1.0f, 1.0f ), GameClientRandomValueReal( -1.0f, 1.0f ) );
	if( axis.Length2() < 1e-4f )
		axis.Set( 0.0f, 0.0f, 1.0f );
	axis.Normalize();
	return axis;
}

/** Whether the stencil volumes give this structure a shadow whole: a volume its template asks for, or
	one Shadows For Props hands an immobile thing with none (fillShadowInfoFromTemplate in W3DModelDraw).
	Classic has that option off, and its tower and fortress cast nothing standing or fallen. */
Bool castsVolumeWhole( const Drawable *draw )
{
	const ThingTemplate *tmplate = draw ? draw->getTemplate() : NULL;
	if( tmplate == NULL || !TheGlobalData->m_useShadowVolumes )
		return FALSE;
	if( tmplate->getShadowType() == SHADOW_VOLUME )
		return TRUE;
	return tmplate->getShadowType() == SHADOW_NONE && TheGlobalData->m_shadowsForProps && tmplate->isKindOf( KINDOF_IMMOBILE )
		&& !tmplate->isKindOf( KINDOF_PROJECTILE );
}

/** Placed and not started, going up, left half built or being sold.  The model then is NONE (the draw
	after it shows the scaffold) or clipped by the construction share, and the fragments are the whole
	building: no fragments are made then, and no fall is staged. */
Bool underConstruction( const Drawable *draw )
{
	const ModelConditionFlags &flags = draw->getModelConditionFlags();
	return flags.test( MODELCONDITION_AWAITING_CONSTRUCTION ) || flags.test( MODELCONDITION_PARTIALLY_CONSTRUCTED )
		|| flags.test( MODELCONDITION_ACTIVELY_BEING_CONSTRUCTED ) || flags.test( MODELCONDITION_SOLD );
}

/** The W3DModelDraw the fragments stand in for, picked once when they are made and kept for good (a
	door or attack state's other model then stays this draw's): of the draws with a model now, the one
	drawing the cut model itself, else the one sharing the most meshes with it, the first in module
	order of equals; with none sharing a mesh (a general's copy renamed throughout), the first with a
	model.  Module order alone is no rule: ReplaceModule removes and appends, so the Scud Storm's main
	draw comes after the one that draws its missile, and the scaffold's draw comes after the main one. */
W3DModelDraw *pickModelDraw( Drawable *draw, const Shared &s )
{
	std::vector<W3DModelDraw *> draws;
	std::vector<int> shared;
	std::vector<Fracture::MeshLook> look;
	for( DrawModule **dm = draw->getDrawModulesNonDirty(); *dm; ++dm )
	{
		ObjectDrawInterface *di = (*dm)->getObjectDrawInterface();
		if( di == NULL )
			continue;
		W3DModelDraw *modelDraw = (W3DModelDraw *)di;
		RenderObjClass *model = modelDraw->getRenderObject();
		Int score = -1;
		if( model )
		{
			modelLook( model, look, NULL );
			score = (Int)Fracture::sharedMeshes( s.cutLook, look );
			if( model->Get_Name() && Fracture::modelKey( model->Get_Name() ) == Fracture::modelKey( s.name.str() ) )
				score += (Int)s.cutLook.size() + 1;
		}
		draws.push_back( modelDraw );
		shared.push_back( score );
	}
	const int pick = Fracture::pickDraw( shared );
	return pick < 0 ? NULL : draws[pick];
}

}	// namespace

//-------------------------------------------------------------------------------------------------
/** The fragments of one structure in the scene.  It carries the drawable's info, so the scene lights
	it, fogs it and picks it as it does the model it stands in for. */
//-------------------------------------------------------------------------------------------------
class FractureRenderObjClass : public RenderObjClass
{
public:

	FractureRenderObjClass( Shared *shared, Drawable *drawable, W3DModelDraw *modelDraw, const std::vector<AsciiString> &gunBones );
	~FractureRenderObjClass();

	virtual RenderObjClass *Clone( void ) const { return NULL; }		///< never copied: a ghost copies the model
	virtual void Render( RenderInfoClass &rinfo );
	virtual bool Cast_Ray( RayCollisionTestClass &raytest );
	virtual void Get_Obj_Space_Bounding_Sphere( SphereClass &sphere ) const;
	virtual void Get_Obj_Space_Bounding_Box( AABoxClass &box ) const;

	void follow( void );										///< the picked W3DModelDraw's model as it stands this frame
	void standDown( void );									///< sold after a hit: the model back, shown and casting, before this goes
	void hit( const Coord3D &pos, Real radius );
	void heardFrom( const Coord3D &source ) { m_hasSource = TRUE; m_source.Set( source.x, source.y, source.z ); }	///< who fired the last hit
	void collapse( Bool over );									///< over: it fell before a load, straight to the ruin
	void tick( void );													///< every frame, seen or not: what moves, the ruin, dust
	void beforeRender( void );									///< outside the draw: the look and the index buffers
	Bool isCollapsing( void ) const { return m_collapsing; }
	void renderCaster( RenderInfoClass &sunInfo );
	void renderVolumes( Bool build );						///< the stencil volumes; build: what changed since the last picture
	void holdModelVolume( void );								///< the whole model's stencil volume off while the fragments cast theirs
	/// What a save keeps: the fragments standing, or at the death those that stood, and the way it fell.
	const std::vector<unsigned char> &standingSet( void ) const { return m_collapsing ? m_present : m_standing; }
	Int collapseWay( void ) const { return m_collapse ? m_way : -1; }
	/// And the knocked out fragments in the order they went, each with the share of health lost then.
	const std::vector<unsigned> &knockedFragments( void ) const { return m_knocked; }
	const std::vector<Real> &knockedAt( void ) const { return m_knockedAt; }
	/// A load's, before anything moves; knocked and knockedAt as saved (empty from a version 2 save).
	void restore( const std::vector<unsigned char> &standing, Int way, const std::vector<unsigned> &knocked,
		const std::vector<Real> &knockedAt );
	/// Health back (a Worker's repair, a heal): the fragments come back, the last knocked out first.
	void repair( void );
	/** Its drawable is going while it falls (DestroyDie, the GLA Command Center): it lets go of it and
		finishes the fall on its own, nothing to pick, then its ruin lies a while and sinks away. */
	void orphan( void );
	Bool orphanTick( void );										///< FALSE once it is gone, or a new game began

private:

	struct Moving
	{
		unsigned fragment;
		Vector3 pos;						///< its centroid, world
		Matrix3x3 rot;					///< its turn, world
		Vector3 velocity, spin;	///< spin: axis times radians a second
		Real start;							///< game seconds it lets go
		Real restedAt;					///< -1 while it moves
		Bool track;							///< follows the baked collapse
		Bool fromHit;						///< knocked out: lies a while and goes
		Bool sinks;							///< FRAGMENT_SINKS in a plain fall: lies SINKER_LIES, as fromHit otherwise
		Bool landed;
		Vector3 offset;					///< a gun's turn at the death, folded into the bake
		Matrix3x3 turn;
		Real restZ;							///< an aimed plain fall's piece or a ruin piece dropped onto what is under it: its centroid's height there; NO_REST_Z otherwise
	};

	struct Group
	{
		unsigned chunk, slot, start, count;
		unsigned short material;
	};

	void computeSlots( void );
	void takeLook( void );
	Matrix3D fragmentWorld( unsigned fragment ) const { return m_slotWorld[m_shared->slotOf[fragment]]; }
	Matrix3D movingWorld( const Moving &m ) const;
	Bool castRayAt( RayCollisionTestClass &raytest, unsigned fragment, const Matrix3D &world ) const;
	Moving still( unsigned fragment, const Matrix3D &world ) const;		///< a fragment as it is, not moving yet
	void launch( unsigned fragment, const Vector3 &from );
	Int dropUnsupported( Real now, Bool drop );
	void step( Real now );
	void stepTrack( Moving &m, Real now );
	void stepFall( Moving &m, Real dt, Real now );
	Real floorAt( const Vector3 &p ) const;
	void pileOn( std::vector<Real> &pile, unsigned fragment, const Vector3 &p, const Matrix3x3 &rot, Bool stands ) const;
	void aimPlainFall( size_t first );
	void rebuild( void );
	void bakeRuin( void );
	void drawGeometry( Bool materials );
	void applyMaterial( unsigned short m );
	Bool castsShadow( void );
	void addVolume( std::vector<Vector3> &out, unsigned fragment, const Matrix3D &world, const Vector3 &toward, Real floorZ );

	Shared *m_shared;
	const Fracture::Collapse *m_collapse;		///< the way down it takes, chosen at the death; NULL: a plain fall
	Int m_way;															///< its index, or a load's before the death is replayed; -1 none
	Vector3 m_source;
	Bool m_hasSource;
	Drawable *m_drawable;
	W3DModelDraw *m_modelDraw;						///< the drawable's draw it stands in for (pickModelDraw), NULL once orphaned
	RenderObjClass *m_model;
	std::vector<TextureClass *> m_look;		///< per material: the texture the live model wears there, NULL for the file's own (takeLook)
	std::vector<Int> m_modelBones;				///< per slot, in m_model
	std::vector<Matrix3D> m_slotWorld;
	Matrix3D m_root;
	std::vector<unsigned char> m_standing, m_kept, m_present;
	std::vector<unsigned> m_knocked;			///< knocked out by hits, in order; a repair takes them back from the end
	std::vector<Real> m_knockedAt;				///< per knocked: the share of health lost when it went
	std::vector<DX8IndexBufferClass *> m_standingIndices;		///< per chunk
	std::vector<DX8VertexBufferClass *> m_ruinVertices;			///< per chunk, once baked
	std::vector<Matrix3D> m_ruinPose;												///< per fragment, once baked: where it lies, model space
	std::vector<Group> m_groups;
	std::vector<Moving> m_moving;
	std::vector<Vector3> m_dust;
	std::vector<Real> m_pile;
	Vector3 m_pileOrigin;
	Real m_now, m_deathTime, m_removedVolume;
	Int m_dustLeft;
	Bool m_dirty, m_collapsing, m_dropped, m_settled, m_ruin, m_damagedLook;
	VertexMaterialClass *m_houseMaterial;		///< the colour the model's house meshes wear, for MATERIAL_HOUSE
	Matrix3D m_ruinRoot;										///< an orphan's root when its ruin settled
	Real m_ruinAt;													///< an orphan's: game seconds its ruin settled, -1 before
	UnsignedInt m_shownVersion;							///< moves at each rebuild of what is shown
	/// The stencil volumes, world space, four corners a silhouette edge: one a bone (the ruin: one), then what moves
	std::vector< std::vector<Vector3> > m_volumes;
	std::vector< std::vector<Real> > m_volumeKeys;	///< what each was built from: the sun, the shown set, the root, its bone
	Real m_movingVolumeAt;									///< game seconds the moving volume was built
	Bool m_castsVolume;											///< the model it stands in for had a stencil volume (W3DModelDraw's rule)
};

//-------------------------------------------------------------------------------------------------
FractureRenderObjClass::FractureRenderObjClass( Shared *shared, Drawable *drawable, W3DModelDraw *modelDraw, const std::vector<AsciiString> &gunBones ) :
	m_shared( shared ), m_collapse( NULL ), m_way( -1 ), m_source( 0, 0, 0 ), m_hasSource( FALSE ), m_drawable( drawable ), m_modelDraw( modelDraw ),
	m_model( NULL ), m_look( shared->asset.materials.size(), (TextureClass *)NULL ), m_root( true ), m_pileOrigin( 0, 0, 0 ),
	m_now( -1.0f ), m_deathTime( 0.0f ), m_removedVolume( 0.0f ), m_dustLeft( MAX_DUST ), m_dirty( TRUE ),
	m_collapsing( FALSE ), m_dropped( FALSE ), m_settled( FALSE ), m_ruin( FALSE ), m_damagedLook( FALSE ),
	m_houseMaterial( NULL ), m_ruinRoot( true ), m_ruinAt( -1.0f ), m_shownVersion( 0 ), m_movingVolumeAt( 0.0f ), m_castsVolume( castsVolumeWhole( drawable ) )
{
	++shared->users;
	const size_t count = shared->asset.fragments.size();
	m_standing.assign( count, 1 );
	m_kept.assign( count, 0 );
	for( size_t i = 0; i < count; ++i )
		for( size_t g = 0; g < gunBones.size(); ++g )
			if( shared->boneNames[shared->slotOf[i]].compareNoCase( gunBones[g] ) == 0 )
				m_kept[i] = 1;
	m_slotWorld.assign( shared->boneNames.size(), Matrix3D( true ) );
	m_modelBones.assign( shared->boneNames.size(), 0 );
	for( size_t c = 0; c < shared->chunks.size(); ++c )
		m_standingIndices.push_back( NEW_REF( DX8IndexBufferClass, ( (unsigned short)shared->chunks[c].indices.size() ) ) );
	Set_User_Data( drawable->getDrawableInfo() );
}

//-------------------------------------------------------------------------------------------------
FractureRenderObjClass::~FractureRenderObjClass()
{
	REF_PTR_RELEASE( m_model );
	REF_PTR_RELEASE( m_houseMaterial );
	for( size_t m = 0; m < m_look.size(); ++m )
		REF_PTR_RELEASE( m_look[m] );
	for( size_t c = 0; c < m_standingIndices.size(); ++c )
		REF_PTR_RELEASE( m_standingIndices[c] );
	for( size_t c = 0; c < m_ruinVertices.size(); ++c )
		REF_PTR_RELEASE( m_ruinVertices[c] );
	--m_shared->users;
	freeUnusedShared();
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::restore( const std::vector<unsigned char> &standing, Int way, const std::vector<unsigned> &knocked,
	const std::vector<Real> &knockedAt )
{
	const Fracture::Asset &a = m_shared->asset;
	if( m_collapsing || standing.size() != a.fragments.size() )
		return;		// a save from another cut of the art: whole, as version 1 loaded
	m_way = way;
	m_removedVolume = 0.0f;
	for( size_t i = 0; i < standing.size(); ++i )
	{
		m_standing[i] = standing[i] || m_kept[i] ? 1 : 0;
		if( !m_standing[i] )
			m_removedVolume += a.fragments[i].volume;
	}
	m_knocked.clear();
	m_knockedAt.clear();
	for( size_t k = 0; k < knocked.size() && k < knockedAt.size(); ++k )
	{
		if( knocked[k] < m_standing.size() && !m_standing[knocked[k]] )
		{
			m_knocked.push_back( knocked[k] );
			m_knockedAt.push_back( knockedAt[k] );
		}
	}
	m_dirty = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The share standing follows the share of health on the way up as it did on the way down: a fragment
	comes back once the health is REPAIR_MARGIN over what it was when the fragment went, the last one
	out first, and every one at full health, so a repaired structure stands whole.  The margin keeps
	the slow heal of a GLA building under fire from shutting each hole the frame after it opened.
	Each comes back in place, its debris gone, with a puff. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::repair( void )
{
	if( m_collapsing || m_drawable == NULL || m_knocked.empty() )
		return;
	const Object *obj = m_drawable->getObject();
	const BodyModuleInterface *body = obj ? obj->getBodyModule() : NULL;
	if( body == NULL || obj->isEffectivelyDead() || body->getMaxHealth() <= 0.0f )
		return;
	const Real lost = WWMath::Clamp( 1.0f - body->getHealth() / body->getMaxHealth(), 0.0f, 1.0f );
	const Fracture::Asset &a = m_shared->asset;
	const Bool whole = lost <= REPAIR_WHOLE;
	Int back = 0;
	computeSlots();
	while( !m_knocked.empty() && ( whole || m_knockedAt.back() > lost + REPAIR_MARGIN ) )
	{
		const unsigned fragment = m_knocked.back();
		m_knocked.pop_back();
		m_knockedAt.pop_back();
		m_standing[fragment] = 1;
		m_removedVolume = std::max( 0.0f, m_removedVolume - a.fragments[fragment].volume );
		size_t kept = 0;
		for( size_t k = 0; k < m_moving.size(); ++k )
			if( !( m_moving[k].fromHit && m_moving[k].fragment == fragment ) )
				m_moving[kept++] = m_moving[k];
		m_moving.resize( kept );
		if( back < REPAIR_PUFFS )
		{
			const Fracture::Fragment &f = a.fragments[fragment];
			m_dust.push_back( fragmentWorld( fragment ) * Vector3( f.centroid[0], f.centroid[1], f.centroid[2] ) );
		}
		++back;
	}
	if( back == 0 )
		return;
	m_dirty = TRUE;
	DEBUG_LOG( ( "FRACTURE REPAIR %s frame %d: %d back, %d still out, %.0f%% of the volume gone, health %.0f%%\n", m_shared->name.str(),
		(Int)TheGameLogic->getFrame(), back, (Int)m_knocked.size(), 100.0f * m_removedVolume / a.volume, 100.0f * ( 1.0f - lost ) ) );
}

//-------------------------------------------------------------------------------------------------
Matrix3D FractureRenderObjClass::movingWorld( const Moving &m ) const
{
	const Fracture::Fragment &f = m_shared->asset.fragments[m.fragment];
	return Matrix3D( m.rot, m.pos - m.rot * Vector3( f.centroid[0], f.centroid[1], f.centroid[2] ) );
}

//-------------------------------------------------------------------------------------------------
FractureRenderObjClass::Moving FractureRenderObjClass::still( unsigned fragment, const Matrix3D &world ) const
{
	const Fracture::Fragment &f = m_shared->asset.fragments[fragment];
	Moving m;
	m.fragment = fragment;
	m.pos = world * Vector3( f.centroid[0], f.centroid[1], f.centroid[2] );
	m.rot = Matrix3x3( world );
	m.velocity.Set( 0, 0, 0 );
	m.spin.Set( 0, 0, 0 );
	m.start = m_now;
	m.restedAt = -1.0f;
	m.track = FALSE;
	m.fromHit = FALSE;
	m.sinks = FALSE;
	m.landed = FALSE;
	m.offset.Set( 0, 0, 0 );
	m.turn.Make_Identity();
	m.restZ = NO_REST_Z;
	return m;
}

//-------------------------------------------------------------------------------------------------
Bool FractureRenderObjClass::castRayAt( RayCollisionTestClass &raytest, unsigned fragment, const Matrix3D &world ) const
{
	const Fracture::Fragment &f = m_shared->asset.fragments[fragment];
	Matrix3D inverse;
	world.Get_Orthogonal_Inverse( inverse );
	const LineSegClass ray( raytest.Ray, inverse );		// a rigid move keeps the fraction along the ray
	const Vector3 low( f.boxMin[0], f.boxMin[1], f.boxMin[2] ), high( f.boxMax[0], f.boxMax[1], f.boxMax[2] );
	const AABoxClass box( ( low + high ) * 0.5f, ( high - low ) * 0.5f );
	return CollisionMath::Collide( ray, box, raytest.Result );
}

//-------------------------------------------------------------------------------------------------
/** The pick: the boxes of the fragments as they are now, standing, falling or lying in the ruin, so a
	hole is not there to click on and the ruin is.  The debris a hit threw is not the structure's. */
//-------------------------------------------------------------------------------------------------
bool FractureRenderObjClass::Cast_Ray( RayCollisionTestClass &raytest )
{
	if( m_model == NULL || ( Get_Collision_Type() & raytest.CollisionType ) == 0 || raytest.Result->StartBad )
		return false;		// as MeshClass::Cast_Ray: the rubble model's type is none, so the ruin hides nobody
	computeSlots();
	Bool hit = FALSE;
	const std::vector<unsigned char> &shown = m_ruin ? m_present : m_standing;
	for( size_t i = 0; i < shown.size(); ++i )
		if( shown[i] && castRayAt( raytest, (unsigned)i, m_ruin ? m_root * m_ruinPose[i] : fragmentWorld( (unsigned)i ) ) )
			hit = TRUE;
	for( size_t k = 0; k < m_moving.size(); ++k )
		if( !m_moving[k].fromHit && castRayAt( raytest, m_moving[k].fragment, movingWorld( m_moving[k] ) ) )
			hit = TRUE;
	return hit != FALSE;
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::Get_Obj_Space_Bounding_Box( AABoxClass &box ) const
{
	const Fracture::Asset &a = m_shared->asset;
	const Vector3 low( a.boxMin[0] - BOUNDS_MARGIN, a.boxMin[1] - BOUNDS_MARGIN, a.boxMin[2] - 10.0f );
	const Vector3 high( a.boxMax[0] + BOUNDS_MARGIN, a.boxMax[1] + BOUNDS_MARGIN, a.boxMax[2] + 10.0f );
	box.Init( ( low + high ) * 0.5f, ( high - low ) * 0.5f );
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::Get_Obj_Space_Bounding_Sphere( SphereClass &sphere ) const
{
	AABoxClass box;
	Get_Obj_Space_Bounding_Box( box );
	sphere.Init( box.Center, box.Extent.Length() );
}

//-------------------------------------------------------------------------------------------------
/** Hide the model it stands in for and take its place: its root and its bones this frame.  Animation
	hidden, which nothing else sets on a whole model, so the drawable's own hiding stays its own.  Only
	the picked draw's model: one that went to NONE leaves the fragments where it last stood, and the
	draw after it (a scaffold) is never taken for it. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::follow( void )
{
	RenderObjClass *model = m_modelDraw ? m_modelDraw->getRenderObject() : NULL;
	if( model != m_model )
	{
		REF_PTR_SET( m_model, model );
		takeLook();
		for( size_t s = 1; s < m_modelBones.size(); ++s )
			m_modelBones[s] = model ? model->Get_Bone_Index( m_shared->boneNames[s].str() ) : 0;
		// the side's colour as the model wears it (the asset manager tints a HOUSECOLOR mesh's vertex
		// material, W3DAssetManager::Recolor_Vertex_Material): a new model is a new colour or skin
		for( Int i = 0; model && i < model->Get_Num_Sub_Objects(); ++i )
		{
			RenderObjClass *sub = model->Get_Sub_Object( i );
			const char *name = sub ? strchr( sub->Get_Name(), '.' ) : NULL;
			MaterialInfoClass *info = name && strncasecmp( name + 1, "HOUSECOLOR", 10 ) == 0 ? sub->Get_Material_Info() : NULL;
			if( info && info->Vertex_Material_Count() > 0 )
			{
				if( m_houseMaterial == NULL )
				{
					m_houseMaterial = NEW_REF( VertexMaterialClass, () );
					m_houseMaterial->Set_Lighting( true );
				}
				Vector3 colour;
				info->Peek_Vertex_Material( 0 )->Get_Diffuse( &colour );
				m_houseMaterial->Set_Diffuse( colour );
				info->Peek_Vertex_Material( 0 )->Get_Ambient( &colour );
				m_houseMaterial->Set_Ambient( colour );
				i = model->Get_Num_Sub_Objects();
			}
			REF_PTR_RELEASE( info );
			REF_PTR_RELEASE( sub );
		}
	}
	if( m_model == NULL )
		return;
	m_model->Set_Animation_Hidden( true );
	m_root = m_model->Get_Transform();
	Set_Transform( m_root );
	Set_Collision_Type( m_model->Get_Collision_Type() );
}

//-------------------------------------------------------------------------------------------------
/** A structure sold after a hit goes down by its construction share, which the fragments cannot show:
	its model comes back whole for the sale, its own shadow with it (holdModelVolume turned it off). */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::standDown( void )
{
	if( m_model )
		m_model->Set_Animation_Hidden( false );
	if( m_modelDraw && m_drawable )
		m_modelDraw->setShadowsEnabled( m_drawable->getShadowsEnabled() );
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::computeSlots( void )
{
	m_slotWorld[0] = m_root;
	for( size_t s = 1; s < m_slotWorld.size(); ++s )
	{
		if( m_model && m_modelBones[s] > 0 )
			Matrix3D::Multiply( m_model->Get_Bone_Transform( m_modelBones[s] ), m_shared->boneInverse[s], &m_slotWorld[s] );
		else
			m_slotWorld[s] = m_root;		// a model without the bone (the rubble): where it was built
	}
}

//-------------------------------------------------------------------------------------------------
/** The look of the model the fragments stand in for, taken at each model swap: a fragment's part is a
	mesh of the cut model, and its damaged, night, snow and garrisoned copies and a general's own keep
	those meshes, so each material takes the texture the live model's counterpart mesh wears in the
	same place (Fracture::counterpart).  Team colour and a skin come along, since the live model's
	textures carry them.  Only a material the live model does not reach keeps the file's texture and the
	damaged sheet rule.  A model gone to NONE keeps the last. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::takeLook( void )
{
	if( m_model == NULL )
		return;
	for( size_t m = 0; m < m_look.size(); ++m )
		REF_PTR_RELEASE( m_look[m] );
	std::vector<Fracture::MeshLook> look;
	std::vector< std::vector<TextureClass *> > worn;
	modelLook( m_model, look, &worn );
	const Fracture::Asset &a = m_shared->asset;
	Int taken = 0, reached = 0;
	for( size_t m = 0; m < m_look.size(); ++m )
	{
		if( m_shared->lookMesh[m] < 0 )
			continue;
		// a counterpart listing other textures than the cut mesh did is another mesh under the name: no slot of it is taken
		const int mesh = Fracture::counterpart( m_shared->cutLook, (unsigned)m_shared->lookMesh[m], look );
		if( mesh < 0 || look[mesh].textures.size() != m_shared->cutLook[m_shared->lookMesh[m]].textures.size()
			|| worn[mesh][m_shared->lookSlot[m]] == NULL )
			continue;
		TextureClass *texture = worn[mesh][m_shared->lookSlot[m]];
		if( texture->Get_Asset_Type() != TextureBaseClass::TEX_REGULAR )
			continue;		// an environment or cube map in that slot: another mesh under the name
		// kept even when it is the file's own sheet, so a damaged state the live model shows clean stays clean
		REF_PTR_SET( m_look[m], texture );
		++reached;
		if( look[mesh].textures[m_shared->lookSlot[m]] != Fracture::textureKey( a.materials[m].texture ) )
			++taken;
	}
	DEBUG_LOG( ( "FRACTURE LOOK %s from %s: %d of %d materials reached, %d of them on another sheet than the file's\n", m_shared->name.str(),
		m_model->Get_Name(), reached, (Int)m_look.size(), taken ) );
}

//-------------------------------------------------------------------------------------------------
/** A knocked out fragment goes off as debris: out from the structure's middle and away from the blast,
	turning as it goes. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::launch( unsigned fragment, const Vector3 &from )
{
	Moving m = still( fragment, fragmentWorld( fragment ) );
	m.fromHit = TRUE;
	Vector3 out = m.pos - m_root.Get_Translation();
	out.Z = 0.0f;
	if( out.Length2() < 1e-4f )
		out.Set( 1.0f, 0.0f, 0.0f );
	out.Normalize();
	Vector3 away = m.pos - from;
	if( away.Length2() > 1e-4f )
		away.Normalize();
	m.velocity = out * GameClientRandomValueReal( 12.0f, 35.0f ) + away * 8.0f;
	m.velocity.Z += GameClientRandomValueReal( 8.0f, 25.0f );
	m.spin = randomAxis() * GameClientRandomValueReal( 2.0f, 7.0f );
	m_moving.push_back( m );
}

//-------------------------------------------------------------------------------------------------
/** The logic's hit, after the tick: the fragments where it landed go, sized by the blast, as long as
	what is left still stands and the share of the structure gone keeps to the share of health lost. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::hit( const Coord3D &pos, Real radius )
{
	if( m_collapsing || m_drawable == NULL )
		return;
	const Object *obj = m_drawable->getObject();
	const BodyModuleInterface *body = obj ? obj->getBodyModule() : NULL;
	if( body == NULL || body->getMaxHealth() <= 0.0f )
		return;
	if( m_now < 0.0f )
		m_now = gameSeconds();
	computeSlots();

	const Fracture::Asset &a = m_shared->asset;
	const Matrix3D *transform = m_drawable->getTransformMatrix();
	Matrix3D inverse;
	transform->Get_Orthogonal_Inverse( inverse );
	Vector3 local = inverse * Vector3( pos.x, pos.y, pos.z );
	if( radius <= 0.0f )
	{
		// a shot at the object lands at its middle height on the side facing the shooter: spread them
		// over the face, as the tracers that hit it spread
		const Real height = a.boxMax[2] - a.boxMin[2];
		local.Z += GameClientRandomValueReal( -0.25f, 0.25f ) * height;
		Vector3 side( -local.Y, local.X, 0.0f );
		if( side.Length2() > 1e-4f )
		{
			side.Normalize();
			local += side * GameClientRandomValueReal( -0.3f, 0.3f ) * ( a.boxMax[0] - a.boxMin[0] ) * 0.5f;
		}
	}
	const Real hole = WWMath::Clamp( radius * HOLE_SCALE, MIN_HOLE, MAX_HOLE );
	const Real lost = 1.0f - body->getHealth() / body->getMaxHealth();
	const Real budget = a.volume * DAMAGE_SHARE * WWMath::Clamp( lost, 0.0f, 1.0f ) - m_removedVolume;
	if( budget <= 0.0f )
		return;

	// a point in the air beside a lattice (the logic's box is wider than the tower's legs) walks in
	// toward the middle until it reaches something
	std::vector<unsigned> out;
	const Vector3 middle( 0.0f, 0.0f, local.Z );
	for( Int k = 0; k <= 6 && out.empty(); ++k )
	{
		const Vector3 at = local + ( middle - local ) * ( k / 6.0f );
		const float point[3] = { at.X, at.Y, at.Z };
		Fracture::knockOut( a, m_standing, m_kept, point, hole, budget, out );
	}
	const Vector3 from = *transform * local;
	for( size_t i = 0; i < out.size(); ++i )
	{
		m_removedVolume += a.fragments[out[i]].volume;
		m_knocked.push_back( out[i] );
		m_knockedAt.push_back( lost );
		launch( out[i], from );
	}
	if( !out.empty() )
		m_dirty = TRUE;
	Int drawn = 0;
	for( size_t i = 0; i < m_standing.size(); ++i )
		if( m_standing[i] )
			drawn += (Int)a.fragments[i].triangles;
	for( size_t k = 0; k < m_moving.size(); ++k )
		drawn += (Int)a.fragments[m_moving[k].fragment].triangles;
	DEBUG_LOG( ( "FRACTURE HIT %s frame %d at %.0f %.0f %.0f radius %.0f hole %.1f: %d out, %.0f%% of the volume gone, health %.0f%%, %d triangles drawn\n",
		m_shared->name.str(), (Int)TheGameLogic->getFrame(), local.X, local.Y, local.Z, radius, hole, (Int)out.size(),
		100.0f * m_removedVolume / a.volume, 100.0f * ( 1.0f - lost ), drawn ) );
}

//-------------------------------------------------------------------------------------------------
/** Zero health: everything still standing comes down, by the baked collapse when there is one,
	otherwise by a plain fall onto what stays (the fragments on the ground). */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::collapse( Bool over )
{
	if( m_collapsing )
		return;
	m_collapsing = TRUE;
	m_now = gameSeconds();
	m_deathTime = m_now + ( m_shared->collapses.empty() ? PLAIN_FALL_DELAY : COLLAPSE_DELAY );
	if( over )
	{
		// a load of a ruin: the fall is past its end at the first step, and raises no dust
		m_deathTime = m_now - LONG_AGO;
		m_dustLeft = 0;
	}
	computeSlots();
	if( !m_shared->collapses.empty() )
	{
		// it falls away from whoever fired last; not known, the first way baked; loaded, the way it fell
		size_t way = 0;
		if( m_way >= 0 && m_way < (Int)m_shared->collapses.size() )
			way = (size_t)m_way;
		else if( m_hasSource )
		{
			Vector3 away = m_root.Get_Translation() - m_source;
			away.Z = 0.0f;
			if( away.Length2() > 1.0f )
			{
				Vector3 model;
				Matrix3D::Inverse_Rotate_Vector( m_root, away, &model );
				way = Fracture::nearestCollapse( m_shared->collapses, RAD_TO_DEGF( atan2f( model.Y, model.X ) ) );
			}
		}
		m_collapse = &m_shared->collapses[way];
		m_way = (Int)way;
	}
	const Fracture::Asset &a = m_shared->asset;
	m_present = m_standing;
	m_pile.assign( PILE_CELLS * PILE_CELLS, 0.0f );
	m_pileOrigin = m_root.Get_Translation() - Vector3( PILE_CELLS * PILE_CELL * 0.5f, PILE_CELLS * PILE_CELL * 0.5f, 0.0f );
	Matrix3D rootInverse;
	m_root.Get_Orthogonal_Inverse( rootInverse );
	const Real top = a.boxMax[2] > 1.0f ? a.boxMax[2] : 1.0f;
	const size_t first = m_moving.size();
	for( size_t i = 0; i < a.fragments.size(); ++i )
	{
		if( !m_standing[i] )
			continue;
		const Fracture::Fragment &f = a.fragments[i];
		const Vector3 centroid( f.centroid[0], f.centroid[1], f.centroid[2] );
		const Matrix3D world = fragmentWorld( (unsigned)i );
		Moving m = still( (unsigned)i, world );
		if( m_collapse )
		{
			if( m_collapse->tracks[i].count == 0 )
				continue;		// a stump of the ruin
			// a fragment on a turned bone (the gun) starts from where it is, not where it was built
			const Matrix3D delta = rootInverse * world;
			m.turn = Matrix3x3( delta );
			m.offset = delta * centroid - centroid;
			m.track = TRUE;
		}
		else
		{
			if( f.grounded )
			{
				// a stump: what falls lands on it
				pileOn( m_pile, (unsigned)i, m.pos, m.rot, TRUE );
				continue;
			}
			// down onto the stumps; aimPlainFall says where, once every piece is known
			m.track = FALSE;
			m.velocity.Set( 0.0f, 0.0f, -GameClientRandomValueReal( 0.0f, PLAIN_FALL_DROP ) );
			m.spin = randomAxis() * GameClientRandomValueReal( 0.2f, 1.0f );
			m.start = m_deathTime + GameClientRandomValueReal( 0.0f, 0.1f ) + 0.1f * ( f.centroid[2] / top );
			if( f.flags & Fracture::FRAGMENT_SINKS )
			{
				// straight down with the rest, and into the ground once there, never lying on the ruin
				m.fromHit = TRUE;
				m.sinks = TRUE;
				m.spin.Set( 0, 0, 0 );
				m_present[i] = 0;
			}
		}
		m_standing[i] = 0;
		m_moving.push_back( m );
	}
	if( m_collapse == NULL )
		aimPlainFall( first );
	m_dirty = TRUE;
	DEBUG_LOG( ( "FRACTURE COLLAPSE %s frame %d: %d fall (%s %.0f, killer %s), %d stay\n", m_shared->name.str(), (Int)TheGameLogic->getFrame(),
		(Int)m_moving.size(), m_collapse ? "baked" : "plain fall", m_collapse ? m_collapse->angle : 0.0f, m_hasSource ? "known" : "unknown",
		(Int)std::count( m_standing.begin(), m_standing.end(), 1 ) ) );
}

//-------------------------------------------------------------------------------------------------
Real FractureRenderObjClass::floorAt( const Vector3 &p ) const
{
	Real floor = TheTerrainRenderObject ? TheTerrainRenderObject->getDrawHeight( p.X, p.Y ) : 0.0f;
	if( !m_pile.empty() )
	{
		const Int x = (Int)floorf( ( p.X - m_pileOrigin.X ) / PILE_CELL ), y = (Int)floorf( ( p.Y - m_pileOrigin.Y ) / PILE_CELL );
		if( x >= 0 && y >= 0 && x < PILE_CELLS && y < PILE_CELLS )
			floor += m_pile[y * PILE_CELLS + x];
	}
	return floor;
}

//-------------------------------------------------------------------------------------------------
/// How far a fragment turned by rot reaches below its centroid.
static Real lowestBelow( const Fracture::Fragment &f, const Matrix3x3 &rot )
{
	Real lowest = 1e30f;
	for( Int corner = 0; corner < 8; ++corner )
	{
		const Vector3 c( ( corner & 1 ? f.boxMax[0] : f.boxMin[0] ) - f.centroid[0], ( corner & 2 ? f.boxMax[1] : f.boxMin[1] ) - f.centroid[1],
			( corner & 4 ? f.boxMax[2] : f.boxMin[2] ) - f.centroid[2] );
		lowest = std::min( lowest, ( rot * c ).Z );
	}
	return -lowest;
}

/** What came to rest raises the rubble under its footprint, turned by rot: a stump that stands to its
	top, a piece that landed by PILE_SHARE of its volume spread over the cells it covers. */
void FractureRenderObjClass::pileOn( std::vector<Real> &pile, unsigned fragment, const Vector3 &p, const Matrix3x3 &rot, Bool stands ) const
{
	if( pile.empty() )
		return;
	const Fracture::Fragment &f = m_shared->asset.fragments[fragment];
	const Real half[3] = { 0.5f * ( f.boxMax[0] - f.boxMin[0] ), 0.5f * ( f.boxMax[1] - f.boxMin[1] ), 0.5f * ( f.boxMax[2] - f.boxMin[2] ) };
	Real reach[3];
	for( Int axis = 0; axis < 3; ++axis )
		reach[axis] = fabsf( rot[axis][0] ) * half[0] + fabsf( rot[axis][1] ) * half[1] + fabsf( rot[axis][2] ) * half[2];
	const Int x0 = (Int)floorf( ( p.X - reach[0] - m_pileOrigin.X ) / PILE_CELL ), x1 = (Int)floorf( ( p.X + reach[0] - m_pileOrigin.X ) / PILE_CELL );
	const Int y0 = (Int)floorf( ( p.Y - reach[1] - m_pileOrigin.Y ) / PILE_CELL ), y1 = (Int)floorf( ( p.Y + reach[1] - m_pileOrigin.Y ) / PILE_CELL );
	const Real cells = (Real)( ( x1 - x0 + 1 ) * ( y1 - y0 + 1 ) );
	const Real rise = std::min( PILE_SHARE * f.volume / ( cells * PILE_CELL * PILE_CELL ), 2.0f * reach[2] );
	for( Int y = std::max( y0, 0 ); y <= y1 && y < PILE_CELLS; ++y )
		for( Int x = std::max( x0, 0 ); x <= x1 && x < PILE_CELLS; ++x )
		{
			Real &cell = pile[y * PILE_CELLS + x];
			cell = stands ? std::max( cell, f.boxMax[2] ) : cell + rise;		// a stump has not moved: its top over the model's foot
		}
}

//-------------------------------------------------------------------------------------------------
/** Where the pieces of a plain fall from first on land: lowest first, each on the lowest rubble
	within its reach inside the model's box, so the heap comes out level and low, and turning on the
	way down to lie on its broad side.  Let fall straight down, a tower's pieces landed on the pile of
	the ones under them and stood it up again as a gutted shell. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::aimPlainFall( size_t first )
{
	const Fracture::Asset &a = m_shared->asset;
	std::vector<Real> planned( m_pile );		// the stumps are on it already
	Matrix3D rootInverse;
	m_root.Get_Orthogonal_Inverse( rootInverse );
	const Real foot = m_root.Get_Translation().Z;
	// the cells inside the model's box, once: per cell per piece it cost four falls 70 ms in one frame
	std::vector<unsigned char> inside( PILE_CELLS * PILE_CELLS, 0 );
	for( Int y = 0; y < PILE_CELLS; ++y )
		for( Int x = 0; x < PILE_CELLS; ++x )
		{
			const Vector3 model = rootInverse * Vector3( m_pileOrigin.X + ( x + 0.5f ) * PILE_CELL, m_pileOrigin.Y + ( y + 0.5f ) * PILE_CELL, foot );
			inside[y * PILE_CELLS + x] = model.X >= a.boxMin[0] + FOOTPRINT_INSET && model.X <= a.boxMax[0] - FOOTPRINT_INSET
				&& model.Y >= a.boxMin[1] + FOOTPRINT_INSET && model.Y <= a.boxMax[1] - FOOTPRINT_INSET;
		}
	std::vector<std::pair<Real, size_t> > order;
	for( size_t k = first; k < m_moving.size(); ++k )
		if( !m_moving[k].sinks )
			order.push_back( std::make_pair( m_moving[k].pos.Z, k ) );
	std::sort( order.begin(), order.end() );
	for( size_t o = 0; o < order.size(); ++o )
	{
		Moving &m = m_moving[order[o].second];
		const Fracture::Fragment &f = a.fragments[m.fragment];
		// its thinnest side up: a wall's shard stood on edge heaped as high as it was wide
		Int thin = 0;
		for( Int axis = 1; axis < 3; ++axis )
			if( f.boxMax[axis] - f.boxMin[axis] < f.boxMax[thin] - f.boxMin[thin] )
				thin = axis;
		Vector3 up( m.rot[0][thin], m.rot[1][thin], m.rot[2][thin] );
		if( up.Z < 0.0f )
			up = -up;
		Vector3 axis;
		Vector3::Cross_Product( up, Vector3( 0.0f, 0.0f, 1.0f ), &axis );
		const Real sine = axis.Length();
		const Real angle = sine > 1e-4f ? atan2f( sine, up.Z ) : 0.0f;
		const Matrix3x3 lying = sine > 1e-4f ? Matrix3x3( axis / sine, angle ) * m.rot : m.rot;
		const Real below = lowestBelow( f, lying );
		const Real reach = std::min( LEVEL_REACH + LEVEL_REACH_PER_HEIGHT * ( m.pos.Z - foot ), LEVEL_REACH_MAX );
		const Int span = (Int)ceilf( reach / PILE_CELL );
		const Int cx = (Int)floorf( ( m.pos.X - m_pileOrigin.X ) / PILE_CELL ), cy = (Int)floorf( ( m.pos.Y - m_pileOrigin.Y ) / PILE_CELL );
		Int best = -1;
		Real bestCost = 1e30f;
		Vector3 target( m.pos );
		for( Int y = std::max( cy - span, 0 ); y <= cy + span && y < PILE_CELLS; ++y )
			for( Int x = std::max( cx - span, 0 ); x <= cx + span && x < PILE_CELLS; ++x )
			{
				if( !inside[y * PILE_CELLS + x] || foot + planned[y * PILE_CELLS + x] + below > m.pos.Z )
					continue;		// outside, or it would have to go up: the wall just over a stump goes off it, not onto it
				const Vector3 centre( m_pileOrigin.X + ( x + 0.5f ) * PILE_CELL, m_pileOrigin.Y + ( y + 0.5f ) * PILE_CELL, m.pos.Z );
				const Real distance = sqrtf( ( centre.X - m.pos.X ) * ( centre.X - m.pos.X ) + ( centre.Y - m.pos.Y ) * ( centre.Y - m.pos.Y ) );
				if( distance > reach )
					continue;
				const Real cost = planned[y * PILE_CELLS + x] + LEVEL_DISTANCE_COST * distance;
				if( cost < bestCost )
				{
					bestCost = cost;
					best = y * PILE_CELLS + x;
					target = centre;
				}
			}
		if( best < 0 )
			continue;		// nowhere inside the box within reach and under it: down onto what is there
		const Real ground = TheTerrainRenderObject ? TheTerrainRenderObject->getDrawHeight( target.X, target.Y ) : foot;
		const Real land = std::min( ground + planned[best] + below, m.pos.Z );
		const Real drop = m.pos.Z - land, down = -m.velocity.Z;
		const Real seconds = std::max( ( sqrtf( down * down + 2.0f * GRAVITY * drop ) - down ) / GRAVITY, 0.2f );
		m.velocity.X = ( target.X - m.pos.X ) / seconds + GameClientRandomValueReal( -PLAIN_FALL_SCATTER, PLAIN_FALL_SCATTER );
		m.velocity.Y = ( target.Y - m.pos.Y ) / seconds + GameClientRandomValueReal( -PLAIN_FALL_SCATTER, PLAIN_FALL_SCATTER );
		m.spin = sine > 1e-4f ? axis * ( angle / ( sine * seconds ) ) : Vector3( 0.0f, 0.0f, 0.0f );
		m.restZ = land;		// it comes to rest on the planned heap: the rubble under it at the time may differ
		pileOn( planned, m.fragment, target, lying, FALSE );
	}
	m_pile = planned;		// what falls unaimed lands on the heap as it will be
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::stepTrack( Moving &m, Real now )
{
	const Fracture::Fragment &f = m_shared->asset.fragments[m.fragment];
	const Real frame = ( now - m_deathTime ) * (Real)m_collapse->fps;
	float pos[3], rot[4];
	if( !Fracture::trackPose( *m_collapse, m.fragment, frame, pos, rot ) )
		return;
	const Real fade = 1.0f - WWMath::Clamp( ( now - m_deathTime ) / 0.4f, 0.0f, 1.0f );
	const Matrix3x3 turn( Quaternion( rot[0], rot[1], rot[2], rot[3] ) );
	m.rot = Matrix3x3( m_root ) * turn * m.turn;
	m.pos = m_root * ( Vector3( pos[0], pos[1], pos[2] ) + m.offset * fade );
	// the bake fell on flat ground at the structure's foot: what comes down near the ground lies on
	// the ground as it is here, so a slope neither buries the ruin nor leaves it floating
	if( TheTerrainRenderObject )
	{
		const Real nearGround = WWMath::Clamp( 1.0f - pos[2] / GROUND_FOLLOW_HEIGHT, 0.0f, 1.0f );
		m.pos.Z += ( TheTerrainRenderObject->getDrawHeight( m.pos.X, m.pos.Y ) - m_root.Get_Translation().Z ) * nearGround;
	}
	const Fracture::Track &t = m_collapse->tracks[m.fragment];
	if( m.restedAt < 0.0f && frame >= (Real)( t.start + t.count - 1 ) )
	{
		const Real bake = (Real)m_collapse->frames / (Real)m_collapse->fps;
		if( t.count >= 2 && now - m_deathTime < bake + FALL_SECONDS )
		{
			// the bake ran out with this piece still going: it carries on at its last speed under
			// gravity and slides to a stop on the ground, rather than freezing in the air or mid-slide
			const Fracture::Key &last = m_collapse->keys[t.firstKey + t.count - 1], &before = m_collapse->keys[t.firstKey + t.count - 2];
			const Vector3 speed( ( last.pos[0] - before.pos[0] ) * m_collapse->fps, ( last.pos[1] - before.pos[1] ) * m_collapse->fps,
				( last.pos[2] - before.pos[2] ) * m_collapse->fps );
			if( speed.Length() > TRACK_HANDOFF_SPEED )
			{
				Matrix3D::Rotate_Vector( m_root, speed, &m.velocity );
				m.spin.Set( 0, 0, 0 );
				m.track = FALSE;
				m.start = now;
				return;
			}
		}
		m.restedAt = now;
		if( ( ( t.flags & Fracture::COLLAPSE_KICKED ) || f.volume >= DUST_VOLUME ) && m_dustLeft > 0 )
		{
			--m_dustLeft;
			m_dust.push_back( m.pos );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::stepFall( Moving &m, Real dt, Real now )
{
	if( m.restedAt >= 0.0f || now < m.start )
		return;
	const Fracture::Fragment &f = m_shared->asset.fragments[m.fragment];
	m.velocity.Z -= GRAVITY * dt;
	m.pos += m.velocity * dt;
	const Real rate = m.spin.Length();
	if( rate > 1e-4f )
	{
		m.rot = Matrix3x3( m.spin / rate, rate * dt ) * m.rot;
		m.rot.Re_Orthogonalize();
	}
	if( m.restZ != NO_REST_Z )
	{
		// a piece whose rest height is known (aimed, or a ruin piece dropping): it stops there, no bounce
		if( m.pos.Z > m.restZ )
			return;
		m.pos.Z = m.restZ;
		m.velocity.Set( 0, 0, 0 );
		m.restedAt = now;
		if( f.volume >= DUST_VOLUME && m_dustLeft > 0 )
		{
			--m_dustLeft;
			m_dust.push_back( m.pos );
		}
		return;
	}
	// its lowest box corner against the ground and the rubble under it
	const Real below = lowestBelow( f, m.rot );
	const Real floor = floorAt( m.pos );
	if( m.pos.Z - below > floor )
		return;
	m.pos.Z = floor + below;
	if( !m.landed )
	{
		m.landed = TRUE;
		if( !m.fromHit && m_collapsing && m_collapse == NULL )
		{
			// a plain fall's piece was aimed at where it lands and turned to lie on its broad side: it stays
			m.velocity.X = m.velocity.Y = 0.0f;
			m.spin.Set( 0, 0, 0 );
		}
		if( f.volume >= DUST_VOLUME && m_dustLeft > 0 )
		{
			--m_dustLeft;
			m_dust.push_back( m.pos );
		}
	}
	if( m.velocity.Z < -12.0f )
	{
		m.velocity.Z *= -0.25f;
		m.velocity.X *= 0.5f;
		m.velocity.Y *= 0.5f;
		m.spin *= 0.5f;
		return;
	}
	const Real slide = sqrtf( m.velocity.X * m.velocity.X + m.velocity.Y * m.velocity.Y );
	if( slide > SLIDE_STOP )
	{
		// on the ground and still going sideways: friction brings it to a stop
		const Real keep = WWMath::Max( 0.0f, 1.0f - SLIDE_FRICTION * dt );
		m.velocity.Set( m.velocity.X * keep, m.velocity.Y * keep, 0.0f );
		m.spin *= keep;
		return;
	}
	m.velocity.Set( 0, 0, 0 );
	m.spin.Set( 0, 0, 0 );
	m.restedAt = now;
	if( !m.fromHit )
		pileOn( m_pile, m.fragment, m.pos, m.rot, FALSE );
}

//-------------------------------------------------------------------------------------------------
/** The fall is over.  A baked one was baked from the whole structure, so a piece of it that came to
	rest on a fragment a hit had knocked out before the death, or a stump that stood on one, hangs in
	the air; a plain fall's piece can come to rest on rubble heights that a box under it does not reach.
	Such a piece falls the rest of the way, onto the ground or onto what came down under it
	(Fracture::dropGaps).  Returns how many have nothing under them; drop: and lets them fall. */
//-------------------------------------------------------------------------------------------------
Int FractureRenderObjClass::dropUnsupported( Real now, Bool drop )
{
	if( TheTerrainRenderObject == NULL )
		return 0;
	const Fracture::Asset &a = m_shared->asset;
	std::vector<Int> movingOf;					// per piece: in m_moving, or -1 for a stump
	std::vector<unsigned> fragmentOf;
	std::vector<Fracture::Box> boxes;
	std::vector<float> ground;
	computeSlots();
	for( size_t i = 0; i < a.fragments.size() + m_moving.size(); ++i )
	{
		Int moving = -1;
		unsigned fragment = (unsigned)i;
		if( i >= a.fragments.size() )
		{
			moving = (Int)( i - a.fragments.size() );
			fragment = m_moving[moving].fragment;
			if( m_moving[moving].fromHit )
				continue;
		}
		else if( !m_standing[i] )
			continue;
		const Matrix3D world = moving < 0 ? fragmentWorld( fragment ) : movingWorld( m_moving[moving] );
		const Fracture::Fragment &f = a.fragments[fragment];
		Vector3 low( 1e30f, 1e30f, 1e30f ), high( -1e30f, -1e30f, -1e30f );
		for( Int corner = 0; corner < 8; ++corner )
		{
			const Vector3 c = world * Vector3( corner & 1 ? f.boxMax[0] : f.boxMin[0], corner & 2 ? f.boxMax[1] : f.boxMin[1],
				corner & 4 ? f.boxMax[2] : f.boxMin[2] );
			low.Update_Min( c );
			high.Update_Max( c );
		}
		const Fracture::Box box = { { low.X, low.Y, low.Z }, { high.X, high.Y, high.Z } };
		Real under = -1e30f;
		for( Int corner = 0; corner < 4; ++corner )
			under = std::max( under, TheTerrainRenderObject->getDrawHeight( corner & 1 ? high.X : low.X, corner & 2 ? high.Y : low.Y ) );
		movingOf.push_back( moving );
		fragmentOf.push_back( fragment );
		boxes.push_back( box );
		ground.push_back( under );
	}
	std::vector<float> gaps;
	Fracture::dropGaps( boxes, ground, DROP_GAP, SUPPORT_INSET, gaps );
	Int count = 0;
	for( size_t p = 0; p < gaps.size(); ++p )
	{
		if( gaps[p] <= 0.0f )
			continue;
		++count;
		if( !drop )
			continue;
		if( movingOf[p] < 0 )
		{
			m_standing[fragmentOf[p]] = 0;
			m_moving.push_back( still( fragmentOf[p], fragmentWorld( fragmentOf[p] ) ) );
			m_dirty = TRUE;
		}
		Moving &m = movingOf[p] < 0 ? m_moving.back() : m_moving[movingOf[p]];
		m.track = FALSE;
		m.velocity.Set( 0, 0, 0 );
		m.spin.Set( 0, 0, 0 );
		m.start = now;
		m.restedAt = -1.0f;
		m.restZ = m.pos.Z - gaps[p];
	}
	if( drop )
		DEBUG_LOG( ( "FRACTURE DROP %s frame %d: %d of %d pieces had nothing under them\n", m_shared->name.str(),
			(Int)TheGameLogic->getFrame(), count, (Int)boxes.size() ) );
	return count;
}

//-------------------------------------------------------------------------------------------------
/** Moves what moves up to now in fixed steps, once a picture: the debris, the fall, the bake. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::step( Real now )
{
	if( m_now < 0.0f || now < m_now )
		m_now = now;
	if( now - m_now > MAX_STEPS * STEP )
		m_now = now - MAX_STEPS * STEP;		// a long look away: the gap before the last two seconds is dropped
	const Real elapsed = now - m_now;
	const Int steps = (Int)( elapsed / STEP );
	for( Int s = 0; s <= steps; ++s )
	{
		const Real at = s < steps ? m_now + STEP : now;
		const Real dt = at - m_now;
		for( size_t i = 0; i < m_moving.size(); ++i )
		{
			Moving &m = m_moving[i];
			if( m.track )
				stepTrack( m, at );
			else
				stepFall( m, dt, at );
		}
		m_now = at;
	}
	// knocked out debris lies a while, then goes into the ground and is gone
	size_t kept = 0;
	for( size_t i = 0; i < m_moving.size(); ++i )
	{
		Moving &m = m_moving[i];
		const Real lies = m.sinks ? SINKER_LIES : DEBRIS_LIES;
		if( m.fromHit && m.restedAt >= 0.0f && now - m.restedAt > lies )
		{
			const Fracture::Fragment &f = m_shared->asset.fragments[m.fragment];
			const Real sunk = ( now - m.restedAt - lies ) / DEBRIS_SINKS;
			if( sunk >= 1.0f )
				continue;
			m.pos.Z -= ( f.boxMax[2] - f.boxMin[2] + 1.0f ) * elapsed / DEBRIS_SINKS;
		}
		m_moving[kept++] = m;
	}
	m_moving.resize( kept );
	if( m_collapsing && !m_settled )
	{
		Bool settled = TRUE;
		for( size_t i = 0; i < m_moving.size() && settled; ++i )
			settled = m_moving[i].fromHit || m_moving[i].restedAt >= 0.0f;
		const Real track = m_collapse ? (Real)m_collapse->frames / (Real)m_collapse->fps : 0.0f;
		const Bool late = now - m_deathTime > track + FALL_SECONDS;
		if( !m_dropped && ( settled || late ) )
		{
			// every piece at rest (a track at its last key or handed off to a fall that landed), or the
			// time is up: what has nothing under it drops, and the ruin waits for it unless time is up
			m_dropped = TRUE;
			if( dropUnsupported( now, TRUE ) > 0 )
				settled = FALSE;
		}
		m_settled = settled || late;
	}
}

//-------------------------------------------------------------------------------------------------
/** The standing index buffers: what still stands, by the bone it rides on and by material, or once
	baked the whole ruin by material. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::rebuild( void )
{
	m_dirty = FALSE;
	++m_shownVersion;
	m_groups.clear();
	const std::vector<unsigned char> &shown = m_ruin ? m_present : m_standing;
	const size_t slots = m_ruin ? 1 : m_shared->boneNames.size();
	const size_t materials = m_shared->asset.materials.size();
	for( size_t c = 0; c < m_shared->chunks.size(); ++c )
	{
		const Chunk &chunk = m_shared->chunks[c];
		DX8IndexBufferClass::WriteLockClass lock( m_standingIndices[c] );
		unsigned short *ib = lock.Get_Index_Array();
		if( ib == NULL )
			continue;
		unsigned at = 0;
		for( size_t slot = 0; slot < slots; ++slot )
		{
			for( unsigned short m = 0; m < materials; ++m )
			{
				const unsigned start = at;
				for( size_t i = 0; i < shown.size(); ++i )
				{
					if( !shown[i] || m_shared->chunkOf[i] != c || ( !m_ruin && m_shared->slotOf[i] != slot ) )
						continue;
					const std::vector<Range> &ranges = m_shared->ranges[i];
					for( size_t r = 0; r < ranges.size(); ++r )
						if( ranges[r].material == m )
						{
							memcpy( ib + at, &chunk.indices[ranges[r].start], ranges[r].count * sizeof( unsigned short ) );
							at += ranges[r].count;
						}
				}
				if( at > start )
				{
					Group g = { (unsigned)c, (unsigned)slot, start, at - start, m };
					m_groups.push_back( g );
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The settled collapse, once: every fragment left is written where it came to rest, in the model's
	space, so the ruin draws like the standing structure did. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::bakeRuin( void )
{
	const Fracture::Asset &a = m_shared->asset;
	std::vector<Matrix3D> local( a.fragments.size(), Matrix3D( true ) );
	Matrix3D rootInverse;
	m_root.Get_Orthogonal_Inverse( rootInverse );
	computeSlots();
	for( size_t i = 0; i < a.fragments.size(); ++i )
		if( m_standing[i] )
			local[i] = rootInverse * fragmentWorld( (unsigned)i );
	for( size_t k = 0; k < m_moving.size(); ++k )
		if( !m_moving[k].fromHit && m_moving[k].restZ != NO_REST_Z )
			m_moving[k].pos.Z = m_moving[k].restZ;		// the time ran out on its way down: where it was going
	const Int unsupported = dropUnsupported( m_now, FALSE );
	size_t kept = 0;
	for( size_t k = 0; k < m_moving.size(); ++k )
	{
		Moving &m = m_moving[k];
		if( m.fromHit )
		{
			m_moving[kept++] = m;
			continue;
		}
		local[m.fragment] = rootInverse * movingWorld( m );
	}
	m_moving.resize( kept );
	m_ruinPose = local;
	// how far the ruin spread, centroids out from the middle, against the standing structure's
	// and how high it lies: the top of each piece's box over the model's foot
	std::vector<Real> lies, tops;
	Real stood = 0.0f;
	for( size_t i = 0; i < a.fragments.size(); ++i )
	{
		if( !m_present[i] )
			continue;
		const Fracture::Fragment &f = a.fragments[i];
		const Vector3 c = local[i] * Vector3( f.centroid[0], f.centroid[1], f.centroid[2] );
		lies.push_back( sqrtf( c.X * c.X + c.Y * c.Y ) );
		stood = std::max( stood, sqrtf( f.centroid[0] * f.centroid[0] + f.centroid[1] * f.centroid[1] ) );
		Real top = -1e30f;
		for( Int corner = 0; corner < 8; ++corner )
			top = std::max( top, ( local[i] * Vector3( corner & 1 ? f.boxMax[0] : f.boxMin[0], corner & 2 ? f.boxMax[1] : f.boxMin[1],
				corner & 4 ? f.boxMax[2] : f.boxMin[2] ) ).Z );
		tops.push_back( top );
	}
	std::sort( lies.begin(), lies.end() );
	std::sort( tops.begin(), tops.end() );
	if( !lies.empty() )
		DEBUG_LOG( ( "FRACTURE RUIN %s frame %d: %d pieces lie within %.1f of the middle, 95%% within %.1f; standing, within %.1f; "
			"heap top %.1f, half under %.1f, 95%% under %.1f, the structure %.1f tall; %d with nothing under them\n",
			m_shared->name.str(), (Int)TheGameLogic->getFrame(), (Int)lies.size(), lies.back(), lies[( lies.size() * 95 ) / 100], stood,
			tops.back(), tops[tops.size() / 2], tops[( tops.size() * 95 ) / 100], a.boxMax[2], unsupported ) );
	for( size_t c = 0; c < m_shared->chunks.size(); ++c )
	{
		DX8VertexBufferClass *vb = NEW_REF( DX8VertexBufferClass, ( DX8_FVF_XYZNUV1, (unsigned short)m_shared->chunks[c].vertexCount ) );
		{
			DX8VertexBufferClass::WriteLockClass lock( vb );
			VertexFormatXYZNUV1 *v = (VertexFormatXYZNUV1 *)lock.Get_Vertex_Array();
			for( size_t i = 0; v && i < a.fragments.size(); ++i )
			{
				if( m_shared->chunkOf[i] != c )
					continue;
				const Fracture::Fragment &f = a.fragments[i];
				for( unsigned k = 0; k < f.vertices; ++k )
				{
					const Fracture::Vertex &src = a.vertices[f.firstVertex + k];
					const Vector3 p = local[i] * Vector3( src.pos[0], src.pos[1], src.pos[2] );
					Vector3 n;
					Matrix3D::Rotate_Vector( local[i], Vector3( src.normal[0], src.normal[1], src.normal[2] ), &n );
					VertexFormatXYZNUV1 &dst = v[m_shared->baseVertex[i] + k];
					dst.x = p.X; dst.y = p.Y; dst.z = p.Z;
					dst.nx = n.X; dst.ny = n.Y; dst.nz = n.Z;
					dst.u1 = src.uv[0];
					dst.v1 = 1.0f - src.uv[1];
				}
			}
		}
		m_ruinVertices.push_back( vb );
	}
	m_ruin = TRUE;
	m_dirty = TRUE;
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::beforeRender( void )
{
	if( m_drawable )
	{
		const ModelConditionFlags &flags = m_drawable->getModelConditionFlags();
		m_damagedLook = flags.test( MODELCONDITION_DAMAGED ) || flags.test( MODELCONDITION_REALLY_DAMAGED ) || flags.test( MODELCONDITION_RUBBLE );
	}
	if( m_dirty )
		rebuild();
}

//-------------------------------------------------------------------------------------------------
/** Every frame, on the screen or off it, so a fall nobody watched lands at its own time and its dust
	rises then, not when the camera comes back. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::tick( void )
{
	if( m_moving.empty() && m_dust.empty() && ( !m_collapsing || m_ruin ) )
		return;		// nothing moves, and no repair's dust to raise
	step( gameSeconds() );
	if( m_settled && !m_ruin )
		bakeRuin();
	if( !m_dust.empty() && TheParticleSystemManager )
	{
		const ParticleSystemTemplate *dust = TheParticleSystemManager->findTemplate( DUST_SYSTEM );
		for( size_t i = 0; dust && i < m_dust.size(); ++i )
		{
			ParticleSystem *sys = TheParticleSystemManager->createParticleSystem( dust );
			if( sys )
			{
				const Coord3D at = { m_dust[i].X, m_dust[i].Y, floorAt( m_dust[i] ) };
				sys->setPosition( &at );
			}
		}
	}
	m_dust.clear();
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::orphan( void )
{
	m_drawable = NULL;
	m_modelDraw = NULL;				// the drawable's module, going with it
	Set_User_Data( NULL );		// the drawable's info goes with it: the scene draws this as plain scenery
	Set_Collision_Type( 0 );	// and nothing picks it
}

//-------------------------------------------------------------------------------------------------
Bool FractureRenderObjClass::orphanTick( void )
{
	const Real now = gameSeconds();
	if( !Is_In_Scene() || now < m_deathTime - COLLAPSE_DELAY - 1.0f )
		return FALSE;		// the map was reset under it, or its clock is a new game's
	tick();
	if( !m_ruin )
		return TRUE;
	if( m_ruinAt < 0.0f )
	{
		m_ruinAt = now;
		m_ruinRoot = m_root;
	}
	const Real sunk = ( now - m_ruinAt - ORPHAN_RUIN_LIES ) / ORPHAN_RUIN_SINKS;
	if( sunk >= 1.0f )
	{
		DEBUG_LOG( ( "FRACTURE RUIN GONE %s frame %d: settled at %.1f game seconds, sunk away\n", m_shared->name.str(),
			(Int)TheGameLogic->getFrame(), m_ruinAt ) );
		return FALSE;
	}
	if( sunk > 0.0f )
	{
		// into the ground as retail debris goes, deep enough for the highest heap
		m_root = m_ruinRoot;
		m_root.Adjust_Z_Translation( -sunk * ( m_shared->asset.boxMax[2] * 0.5f + 5.0f ) );
		Set_Transform( m_root );
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::applyMaterial( unsigned short m )
{
	TextureClass *texture = m_look[m];
	if( texture == NULL )
		texture = m_damagedLook && m_shared->damaged[m] ? m_shared->damaged[m] : m_shared->textures[m];
	const Bool house = m_houseMaterial && ( m_shared->asset.materials[m].flags & Fracture::MATERIAL_HOUSE );
	DX8Wrapper::Set_Material( house ? m_houseMaterial : m_shared->material );
	DX8Wrapper::Set_Texture( 0, texture );
	DX8Wrapper::Set_Shader( m_shared->shaders[m] );
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::drawGeometry( Bool materials )
{
	unsigned lastChunk = 0xFFFFFFFF;
	for( size_t g = 0; g < m_groups.size(); ++g )
	{
		const Group &group = m_groups[g];
		const Chunk &chunk = m_shared->chunks[group.chunk];
		if( group.chunk != lastChunk )
		{
			lastChunk = group.chunk;
			DX8Wrapper::Set_Vertex_Buffer( m_ruin ? m_ruinVertices[group.chunk] : chunk.vertices );
			DX8Wrapper::Set_Index_Buffer( m_standingIndices[group.chunk], 0 );
		}
		DX8Wrapper::Set_Transform( D3DTS_WORLD, m_ruin ? m_root : m_slotWorld[group.slot] );
		if( materials )
			applyMaterial( group.material );
		DX8Wrapper::Draw_Triangles( (unsigned short)group.start, (unsigned short)( group.count / 3 ), 0, (unsigned short)chunk.vertexCount );
	}
	// what moves, a fragment at a time
	const Fracture::Asset &a = m_shared->asset;
	lastChunk = 0xFFFFFFFF;
	for( size_t k = 0; k < m_moving.size(); ++k )
	{
		const Moving &m = m_moving[k];
		const unsigned c = m_shared->chunkOf[m.fragment];
		const Chunk &chunk = m_shared->chunks[c];
		if( c != lastChunk )
		{
			lastChunk = c;
			DX8Wrapper::Set_Vertex_Buffer( chunk.vertices );
			DX8Wrapper::Set_Index_Buffer( chunk.all, 0 );
		}
		const Fracture::Fragment &f = a.fragments[m.fragment];
		const Vector3 centroid( f.centroid[0], f.centroid[1], f.centroid[2] );
		DX8Wrapper::Set_Transform( D3DTS_WORLD, Matrix3D( m.rot, m.pos - m.rot * centroid ) );
		const std::vector<Range> &ranges = m_shared->ranges[m.fragment];
		for( size_t r = 0; r < ranges.size(); ++r )
		{
			if( materials )
				applyMaterial( ranges[r].material );
			DX8Wrapper::Draw_Triangles( (unsigned short)ranges[r].start, (unsigned short)( ranges[r].count / 3 ),
				(unsigned short)m_shared->baseVertex[m.fragment], (unsigned short)f.vertices );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::Render( RenderInfoClass &rinfo )
{
	// the scene leaves out a drawable the shroud fully hides (its ghost draws the model as last seen)
	// and shades a fogged one with its shroud pass, so the fragments are shown exactly as the model was
	const CostTimer timer;
	step( gameSeconds() );		// between ticks, so the fall is as smooth as the frame
	if( m_dirty )
		rebuild();		// a hit after the last draw module pass: the model is hidden already
	computeSlots();
	if( rinfo.light_environment )
		DX8Wrapper::Set_Light_Environment( rinfo.light_environment );
	DX8Wrapper::Set_Material( m_shared->material );
	DX8Wrapper::Set_Texture( 1, NULL );
	if( ( rinfo.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_ADDITIONAL_PASSES_ONLY ) == 0 )
	{
		// the EMP's, the outage's and the reveal's look on the Direct3D 11 frame, as a mesh takes it
		static const float NO_EFFECT[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		SunShadow::SetCasterEffect( DX8Wrapper::_Get_D3D_Device(), rinfo.drawEffect, rinfo.drawEffectParameters );
		drawGeometry( TRUE );
		SunShadow::SetCasterEffect( DX8Wrapper::_Get_D3D_Device(), 0, NO_EFFECT );
	}
	// the shroud's and the heat vision's passes, over the same triangles, with the drawable's opacity and
	// glow on them as a mesh takes them (MeshClass::Render)
	for( Int i = 0; i < rinfo.Additional_Pass_Count(); ++i )
	{
		MaterialPassClass *pass = rinfo.Peek_Additional_Pass( i );
		VertexMaterialClass *material = pass->Peek_Material();
		const Bool opacity = material && rinfo.materialPassAlphaOverride != 1.0f;
		const Bool emissive = material && rinfo.materialPassEmissiveOverride != 1.0f;
		const float oldOpacity = material ? material->Get_Opacity() : 1.0f;
		Vector3 oldEmissive( 0, 0, 0 );
		if( material )
			material->Get_Emissive( &oldEmissive );
		if( opacity )
			material->Set_Opacity( rinfo.materialPassAlphaOverride );
		if( emissive )
			material->Set_Emissive( rinfo.materialPassEmissiveOverride * oldEmissive );
		pass->Install_Materials();
		drawGeometry( FALSE );
		pass->UnInstall_Materials();
		if( opacity )
			material->Set_Opacity( oldOpacity );
		if( emissive )
			material->Set_Emissive( oldEmissive );
	}
}

//-------------------------------------------------------------------------------------------------
Bool FractureRenderObjClass::castsShadow( void )
{
	if( !Is_In_Scene() || ( m_drawable && m_drawable->isDrawableEffectivelyHidden() ) )
		return FALSE;		// an orphan (no drawable) casts while it lies
	// a building in the fog keeps its shadow under its ghost, by the model's own rule
	return m_modelDraw == NULL || !m_modelDraw->isShadowHiddenByShroud();
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::renderCaster( RenderInfoClass &sunInfo )
{
	if( !castsShadow() )
		return;
	const CostTimer timer;
	step( gameSeconds() );		// off the screen the debris still falls, and so does its shadow
	if( m_dirty )
		rebuild();
	computeSlots();
	DX8Wrapper::Set_Material( m_shared->material );
	DX8Wrapper::Set_Texture( 1, NULL );
	drawGeometry( TRUE );
}

//-------------------------------------------------------------------------------------------------
/** One fragment's silhouette from the sun, by W3DShadowGeometry's rules: a triangle faces the sun when
	its normal points against the light's way, an edge between a lit and an unlit triangle, or an open
	edge of a lit one, is wound as the lit triangle winds it and pushed away from the sun until it is
	under the lowest ground of the map.  Four corners an edge, drawn as constructVolume's two triangles. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::addVolume( std::vector<Vector3> &out, unsigned fragment, const Matrix3D &world, const Vector3 &toward, Real floorZ )
{
	const Fracture::Asset &a = m_shared->asset;
	const Fracture::Fragment &f = a.fragments[fragment];
	const Fracture::Vertex *v = &a.vertices[f.firstVertex];
	Vector3 local;
	Matrix3D::Inverse_Rotate_Vector( world, toward, &local );
	static std::vector<unsigned char> lit;
	lit.resize( f.triangles );
	for( unsigned t = 0; t < f.triangles; ++t )
		lit[t] = Vector3::Dot_Product( m_shared->faceNormal[f.firstTriangle + t], local ) < 0.0f ? 1 : 0;
	const Real reach = toward.Z < -VOLUME_LOWEST_SUN ? 1.0f / -toward.Z : VOLUME_REACH;
	for( unsigned e = m_shared->firstEdge[fragment]; e < m_shared->firstEdge[fragment + 1]; ++e )
	{
		const VolumeEdge &edge = m_shared->edges[e];
		const Bool left = lit[edge.left - f.firstTriangle] != 0;
		const Bool right = edge.right >= 0 && lit[edge.right - f.firstTriangle] != 0;
		unsigned short from, to;
		if( left && !right )
		{
			from = edge.a;
			to = edge.b;
		}
		else if( right && !left )
		{
			from = edge.b;
			to = edge.a;
		}
		else
			continue;
		const Vector3 p0 = world * Vector3( v[from].pos[0], v[from].pos[1], v[from].pos[2] );
		const Vector3 p1 = world * Vector3( v[to].pos[0], v[to].pos[1], v[to].pos[2] );
		out.push_back( p0 );
		out.push_back( p1 );
		out.push_back( p0 + toward * WWMath::Clamp( ( p0.Z - floorZ ) * reach, 0.0f, VOLUME_REACH ) );
		out.push_back( p1 + toward * WWMath::Clamp( ( p1.Z - floorZ ) * reach, 0.0f, VOLUME_REACH ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** Where the fragments cast stencil volumes, the whole model's own volume, standing on under its hidden
	model, goes the moment they stand in: at the first hit, seen or not, and again at the top of every
	stencil pass before any caster queues its volume (holdModelVolumes).
	Only the volume: the patch under the footprint stays until the fall.  The options can change, so the
	answer is asked again each time; an orphan keeps its last. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::holdModelVolume( void )
{
	if( m_drawable == NULL )
		return;
	m_castsVolume = castsVolumeWhole( m_drawable );
	if( !m_castsVolume || ( TheGlobalData->m_shadowMapOnly && SunShadow::OwnsSceneShadows( DX8Wrapper::_Get_D3D_Device() ) ) )
		return;		// the sun's map alone: the hidden model is in no shadow pass
	if( m_modelDraw )
		m_modelDraw->holdShadowOff();
}

//-------------------------------------------------------------------------------------------------
/** The stencil volumes (-d3d9, Classic), the casters the sun's map has in renderCaster: whatever is
	drawn, standing, falling or lying, at the pose it is drawn in.  Built again only when that pose, the
	sun or the set shown moved, and every picture while anything falls. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::renderVolumes( Bool build )
{
	if( build )
	{
		const CostTimer timer;
		if( !m_castsVolume || !castsShadow() || TheW3DShadowManager == NULL || TheTerrainRenderObject == NULL )
		{
			m_volumes.clear();
			m_volumeKeys.clear();
			return;
		}
		const Real now = gameSeconds();
		step( now );
		if( m_dirty )
			rebuild();
		computeSlots();
		makeVolumeEdges( *m_shared );
		const Vector3 light = TheW3DShadowManager->getLightPosWorld( 0 );
		Vector3 toward = m_root.Get_Translation() - light;
		if( toward.Length2() < 1e-6f )
			toward.Set( 0.0f, 0.0f, -1.0f );
		toward.Normalize();
		const Real floorZ = TheTerrainRenderObject->getMinHeight() - VOLUME_UNDER_GROUND;
		// what stands, one volume a bone (the ruin: one), each built again only when its key moved, so a
		// turning gun rebuilds its own fragments and no others
		const size_t slots = m_ruin ? 1 : m_slotWorld.size();
		m_volumes.resize( slots + 1 );
		m_volumeKeys.resize( slots + 1 );
		const std::vector<unsigned char> &shown = m_ruin ? m_present : m_standing;
		static std::vector<Real> key;
		for( size_t s = 0; s <= slots; ++s )
		{
			key.clear();
			key.push_back( light.X );
			key.push_back( light.Y );
			key.push_back( light.Z );
			key.push_back( (Real)m_shownVersion );
			key.push_back( m_ruin ? 1.0f : 0.0f );
			key.insert( key.end(), (const Real *)&m_root, (const Real *)&m_root + 12 );
			if( s == slots )
			{
				// what moves, at most once a logic frame's worth of game time: a shadow a frame behind its
				// piece is not seen, a rebuild of every falling piece every picture is
				if( m_moving.empty() )
				{
					m_volumes[s].clear();
					m_volumeKeys[s].clear();
					continue;
				}
				if( m_volumeKeys[s] == key && fabs( now - m_movingVolumeAt ) < VOLUME_MOVING_STEP )
					continue;
				m_movingVolumeAt = now;
				m_volumeKeys[s] = key;
				m_volumes[s].clear();
				for( size_t k = 0; k < m_moving.size(); ++k )
					addVolume( m_volumes[s], m_moving[k].fragment, movingWorld( m_moving[k] ), toward, floorZ );
				continue;
			}
			if( !m_ruin )
				key.insert( key.end(), (const Real *)&m_slotWorld[s], (const Real *)&m_slotWorld[s] + 12 );
			if( key == m_volumeKeys[s] )
				continue;
			m_volumeKeys[s] = key;
			m_volumes[s].clear();
			for( size_t i = 0; i < shown.size(); ++i )
				if( shown[i] && ( m_ruin || m_shared->slotOf[i] == s ) )
					addVolume( m_volumes[s], (unsigned)i, m_ruin ? m_root * m_ruinPose[i] : fragmentWorld( (unsigned)i ), toward, floorZ );
		}
		for( size_t s = 0; s < m_volumes.size(); ++s )
			s_volumeEdges += (Int)( m_volumes[s].size() / 4 );
	}
	if( TheW3DVolumetricShadowManager == NULL )
		return;
	static std::vector<short> indices;
	if( indices.empty() )
	{
		for( Int q = 0; q < VOLUME_EDGES_A_DRAW; ++q )
		{
			const short k = (short)( q * 4 );
			const short quad[6] = { k, (short)( k + 2 ), (short)( k + 1 ), (short)( k + 1 ), (short)( k + 2 ), (short)( k + 3 ) };
			indices.insert( indices.end(), quad, quad + 6 );
		}
	}
	for( size_t s = 0; s < m_volumes.size(); ++s )
	{
		const std::vector<Vector3> &volume = m_volumes[s];
		const size_t edges = volume.size() / 4;
		for( size_t at = 0; at < edges; at += VOLUME_EDGES_A_DRAW )
		{
			const Int count = (Int)std::min( edges - at, (size_t)VOLUME_EDGES_A_DRAW );
			TheW3DVolumetricShadowManager->renderWorldVolume( &volume[at * 4], count * 4, &indices[0], count * 6 );
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// W3DFractureDraw ////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace {
std::vector<W3DFractureDraw *> s_modules;
std::vector<FractureRenderObjClass *> s_orphans;		///< falls whose drawable went at the death, one reference each
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDrawModuleData::buildFieldParse( MultiIniFieldParse &p )
{
	ModuleData::buildFieldParse( p );
	static const FieldParse dataFieldParse[] =
	{
		{ "FractureFile", INI::parseAsciiString, NULL, offsetof( W3DFractureDrawModuleData, m_fractureFile ) },
		{ "GunBones", INI::parseAsciiStringVector, NULL, offsetof( W3DFractureDrawModuleData, m_gunBones ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

//-------------------------------------------------------------------------------------------------
W3DFractureDraw::W3DFractureDraw( Thing *thing, const ModuleData *moduleData ) : DrawModule( thing, moduleData ),
	m_fracture( NULL ), m_loadedWay( -1 ), m_loaded( FALSE )
{
	if( !TheGlobalData->m_headless )		// nothing is drawn: no hit needs to reach it
		getDrawable()->setTakesImpacts();
}

//-------------------------------------------------------------------------------------------------
/** A drawable made in a tick builds its modules on the logic thread, so the list of the fractured
	ones is written only where the fragments are made and freed, on the main thread (a drawable that
	dies in a tick is deleted at the flush). */
//-------------------------------------------------------------------------------------------------
W3DFractureDraw::~W3DFractureDraw( void )
{
	if( m_fracture )
	{
		s_modules.erase( std::remove( s_modules.begin(), s_modules.end(), this ), s_modules.end() );
		if( m_fracture->isCollapsing() && TheGameLogic && !TheGameLogic->isClearingGameData() )
		{
			// a structure whose object goes at its death (DestroyDie) takes its drawable with it in
			// the same flush: the fall it started keeps the reference and finishes alone
			m_fracture->orphan();
			s_orphans.push_back( m_fracture );
			m_fracture = NULL;
			return;
		}
		m_fracture->Remove();
		REF_PTR_RELEASE( m_fracture );
	}
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::doDrawModule( const Matrix3D *transformMtx )
{
	Drawable *draw = getDrawable();
	const Bool rubble = draw->getModelConditionFlags().test( MODELCONDITION_RUBBLE );
	if( m_fracture == NULL && ( rubble || !m_loadedStanding.empty() ) && makeFracture() )
	{
		// a load's holes, or dead without a hit that reached the client (a map that starts with it in
		// ruins, a kill with no blast): a fall the client saw before a load is over, any other plays
		m_fracture->restore( m_loadedStanding, m_loadedWay, m_loadedKnocked, m_loadedKnockedAt );
		if( rubble )
			m_fracture->collapse( m_loaded );
	}
	m_loadedStanding.clear();
	m_loadedWay = -1;
	m_loadedKnocked.clear();
	m_loadedKnockedAt.clear();
	if( m_fracture == NULL )
		return;
	if( !m_fracture->isCollapsing() && underConstruction( draw ) )
	{
		// sold after a hit: the model takes the sale down as it would have, holes and debris gone
		m_fracture->standDown();
		s_modules.erase( std::remove( s_modules.begin(), s_modules.end(), this ), s_modules.end() );
		m_fracture->Remove();
		REF_PTR_RELEASE( m_fracture );
		return;
	}
	m_fracture->follow();
	if( rubble )
		m_fracture->collapse( FALSE );
	if( m_fracture->isCollapsing() && !SunShadow::OwnsSceneShadows( DX8Wrapper::_Get_D3D_Device() ) )
	{
		// fallen, under the stencil volumes (-d3d9, Classic): no standing model's shadow and no patch
		// under a footprint nothing stands on
		for( DrawModule **dm = draw->getDrawModulesNonDirty(); *dm; ++dm )
			if( (*dm)->getObjectDrawInterface() )
				(*dm)->setShadowsEnabled( FALSE );
	}
	m_fracture->holdModelVolume();
	m_fracture->beforeRender();
}

//-------------------------------------------------------------------------------------------------
/** The fragments in the model's place, from its pose now.  FALSE with nothing drawn, no art, or the
	building not finished (underConstruction).  The draw they stand in for is picked here, once. */
Bool W3DFractureDraw::makeFracture( void )
{
	if( m_fracture )
		return TRUE;
	if( TheGlobalData->m_headless || W3DDisplay::m_3DScene == NULL || W3DDisplay::m_assetManager == NULL || underConstruction( getDrawable() ) )
		return FALSE;
	Shared *shared = sharedFor( getW3DFractureDrawModuleData()->m_fractureFile );
	W3DModelDraw *modelDraw = shared ? pickModelDraw( getDrawable(), *shared ) : NULL;
	RenderObjClass *model = modelDraw ? modelDraw->getRenderObject() : NULL;
	if( model == NULL )
		return FALSE;
	m_fracture = NEW_REF( FractureRenderObjClass, ( shared, getDrawable(), modelDraw, getW3DFractureDrawModuleData()->m_gunBones ) );
	m_fracture->follow();
	// what the swap trades: the fragments have one level, the model may have been drawing a coarser one
	const Bool levels = model->Class_ID() == RenderObjClass::CLASSID_HLOD;
	DEBUG_LOG( ( "FRACTURE TAKES OVER %s frame %d: the model drew level %d of %d, %d triangles; the fragments draw %d\n",
		shared->name.str(), (Int)TheGameLogic->getFrame(), levels ? ( (HLodClass *)model )->Get_LOD_Level() + 1 : 1,
		levels ? ( (HLodClass *)model )->Get_LOD_Count() : 1, model->Get_Num_Polys(), (Int)shared->asset.triangles.size() ) );
	W3DDisplay::m_3DScene->Add_Render_Object( m_fracture );
	s_modules.push_back( this );
	m_fracture->holdModelVolume();		// an off-screen first hit included
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::setFullyObscuredByShroud( Bool fullyObscured )
{
	// the fragments ask the model's draw at each picture: it keeps the rule of whose shadow stays
}

//-------------------------------------------------------------------------------------------------
/** The first hit makes the fragments and takes the model's place in the scene; a radius below zero
	makes them without a hole (the death). */
//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::reactToImpact( const Coord3D &pos, Real radius, const Coord3D *source )
{
	if( !makeFracture() )
		return;
	if( !m_loadedStanding.empty() )
	{
		m_fracture->restore( m_loadedStanding, m_loadedWay, m_loadedKnocked, m_loadedKnockedAt );		// a hit before the first picture after a load
		m_loadedStanding.clear();
		m_loadedWay = -1;
		m_loadedKnocked.clear();
		m_loadedKnockedAt.clear();
	}
	if( source )
		m_fracture->heardFrom( *source );
	if( radius >= 0.0f )
		m_fracture->hit( pos, radius );
	// the killing hit starts the fall here, in view or not, so an off-screen death is not kept for the
	// moment the camera comes back.  No object: it was destroyed in the tick that hit it (DestroyDie),
	// the drawable goes right after this work and the fall carries on without it (orphan)
	const Object *obj = getDrawable()->getObject();
	if( obj == NULL || obj->isEffectivelyDead() )
		m_fracture->collapse( FALSE );
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::updateAll( void )
{
	{
		const CostTimer timer;
		for( size_t i = 0; i < s_modules.size(); ++i )
			if( s_modules[i]->m_fracture )
				s_modules[i]->m_fracture->tick();
		size_t kept = 0;
		for( size_t i = 0; i < s_orphans.size(); ++i )
		{
			FractureRenderObjClass *orphan = s_orphans[i];
			if( orphan->orphanTick() )
			{
				s_orphans[kept++] = orphan;
				continue;
			}
			orphan->Remove();
			REF_PTR_RELEASE( orphan );
		}
		s_orphans.resize( kept );
	}
	// once a second while any structure shows its fragments: what they cost the CPU
	const UnsignedInt now = WW3D::Get_Sync_Time();
	if( now - s_costReportStart < COST_REPORT_MS )
		return;
	Int falling = (Int)s_orphans.size();
	for( size_t i = 0; i < s_modules.size(); ++i )
		if( s_modules[i]->m_fracture && s_modules[i]->m_fracture->isCollapsing() )
			++falling;
	if( !s_modules.empty() || !s_orphans.empty() )
		DEBUG_LOG( ( "FRACTURE COST frame %d: %d structures in fragments, %d of them fallen, %.2f ms of CPU in %u ms, %d stencil volume edges\n",
			(Int)TheGameLogic->getFrame(), (Int)( s_modules.size() + s_orphans.size() ), falling, s_costUs / 1000.0, now - s_costReportStart,
			s_volumeEdges ) );
	s_costUs = 0.0;
	s_volumeEdges = 0;
	s_costReportStart = now;
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::takeReadsForRender( void )
{
	for( size_t i = 0; i < s_modules.size(); ++i )
		if( s_modules[i]->m_fracture )
			s_modules[i]->m_fracture->repair();
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::renderShadowCasters( RenderInfoClass &sunInfo )
{
	for( size_t i = 0; i < s_modules.size(); ++i )
		if( s_modules[i]->m_fracture )
			s_modules[i]->m_fracture->renderCaster( sunInfo );
	for( size_t i = 0; i < s_orphans.size(); ++i )
		s_orphans[i]->renderCaster( sunInfo );
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::holdModelVolumes( void )
{
	for( size_t i = 0; i < s_modules.size(); ++i )
		if( s_modules[i]->m_fracture )
			s_modules[i]->m_fracture->holdModelVolume();
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::renderStencilVolumes( Bool build )
{
	for( size_t i = 0; i < s_modules.size(); ++i )
		if( s_modules[i]->m_fracture )
			s_modules[i]->m_fracture->renderVolumes( build );
	for( size_t i = 0; i < s_orphans.size(); ++i )
		s_orphans[i]->renderVolumes( build );
}

//-------------------------------------------------------------------------------------------------
Bool W3DFractureDraw::stagesCollapse( const Drawable *draw )
{
	static const NameKeyType key = NAMEKEY( "W3DFractureDraw" );
	for( const DrawModule **dm = draw->getDrawModules(); *dm; ++dm )
	{
		if( (*dm)->getModuleNameKey() != key )
			continue;
		const W3DFractureDraw *fracture = (const W3DFractureDraw *)*dm;
		if( fracture->m_fracture )
			return TRUE;
		// unfinished, no fragments will be made for it: the Collapse option takes it down as before
		return !underConstruction( draw ) && sharedFor( fracture->getW3DFractureDrawModuleData()->m_fractureFile ) != NULL;
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
void W3DFractureDraw::crc( Xfer *xfer )
{
	DrawModule::crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version.  The holes were not saved: a loaded game showed the structure whole.
	* 2: The holes, one byte a fragment, 1 standing (none before the first hit), then the baked way it
	*    fell (-1 standing).  The client's look only, written by the client's draw module: no logic
	*    reads it and the CRC never sees it.
	* 3: The fragments the hits knocked out, in order, each with the share of health lost as it went, so
	*    a repair after the load brings them back the way it would have before it. */
// ------------------------------------------------------------------------------------------------
void W3DFractureDraw::xfer( Xfer *xfer )
{
	const Int MOST_FRAGMENTS = 65536;
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	DrawModule::xfer( xfer );
	if( version < 2 )
		return;
	std::vector<unsigned char> standing = m_loadedStanding;		// loaded and not drawn since
	Int way = m_loadedWay;
	if( xfer->getXferMode() != XFER_LOAD && m_fracture )
	{
		standing = m_fracture->standingSet();
		way = m_fracture->collapseWay();
	}
	Int count = (Int)standing.size();
	xfer->xferInt( &count );
	if( count < 0 || count > MOST_FRAGMENTS )
	{
		DEBUG_CRASH( ( "W3DFractureDraw::xfer - %d fragments, expected 0 to %d", count, MOST_FRAGMENTS ) );
		throw XFER_INVALID_PARAMETERS;
	}
	standing.resize( count );
	if( count > 0 )
		xfer->xferUser( &standing[0], count );
	xfer->xferInt( &way );
	std::vector<unsigned> knocked = m_loadedKnocked;
	std::vector<Real> knockedAt = m_loadedKnockedAt;
	if( xfer->getXferMode() != XFER_LOAD && m_fracture )
	{
		knocked = m_fracture->knockedFragments();
		knockedAt = m_fracture->knockedAt();
	}
	if( version >= 3 )
	{
		Int knockedCount = (Int)knocked.size();
		xfer->xferInt( &knockedCount );
		if( knockedCount < 0 || knockedCount > count )
		{
			DEBUG_CRASH( ( "W3DFractureDraw::xfer - %d knocked out, expected 0 to %d", knockedCount, count ) );
			throw XFER_INVALID_PARAMETERS;
		}
		knocked.resize( knockedCount );
		knockedAt.resize( knockedCount );
		for( Int k = 0; k < knockedCount; ++k )
		{
			xfer->xferUnsignedInt( &knocked[k] );
			xfer->xferReal( &knockedAt[k] );
		}
	}
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_loadedStanding.swap( standing );
		m_loadedWay = way;
		m_loadedKnocked.swap( knocked );
		m_loadedKnockedAt.swap( knockedAt );
	}
}

// ------------------------------------------------------------------------------------------------
void W3DFractureDraw::loadPostProcess( void )
{
	DrawModule::loadPostProcess();
	// dead in the save: its fall was over, or would have been, before the load
	const Object *obj = getDrawable()->getObject();
	m_loaded = obj && obj->isEffectivelyDead();
}
