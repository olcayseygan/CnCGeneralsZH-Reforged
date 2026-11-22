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
#include "Platform/SunShadow.h"
#include "WW3D2/coltest.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/matpass.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/shader.h"
#include "WW3D2/texture.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/w3d_file.h"
#include "WW3D2/w3d_util.h"
#include "WWMath/colmath.h"
#include "WWMath/lineseg.h"
#include "WWMath/quat.h"

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
const Real DROP_GAP = 1.5f;					// a piece of the ruin with more air than this under its box falls the rest of the way
const Real SUPPORT_INSET = 0.5f;			// how far two boxes have to overlap across before one holds the other up
const Real NO_REST_Z = -1e30f;
const Real TRACK_HANDOFF_SPEED = 3.0f;	// units a game second: a piece faster than this at its bake's last key carries on falling
const Real SLIDE_STOP = 1.5f;					// a piece on the ground slower than this sideways stops
const Real SLIDE_FRICTION = 4.0f;			// of its sideways speed lost a game second on the ground
const Real LONG_AGO = 1000.0f;				// game seconds: a death this far back has long finished falling
const Real GROUND_FOLLOW_HEIGHT = 20.0f;	// a baked fragment this far down to the model's foot lies on the ground under it
const Real BOUNDS_MARGIN = 150.0f;		// how far past the model debris and the fall may reach, for the culling box (the tower's diagonal falls throw 146)
const Real DUST_VOLUME = 6.0f;			// a fragment at least this big raises dust where it comes down
const Int MAX_DUST = 24;						// puffs a structure, over its life
const char *const DUST_SYSTEM = "TreeBounceDust";
const Int PILE_CELLS = 64;					// the rubble heights a plain collapse lands on, round the structure
const Real PILE_CELL = 4.0f;

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
	VertexMaterialClass *material;
	Int users;																			///< structures drawn from it now
};

std::vector<Shared *> s_shared;

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

/// The damaged models' sheet for a texture, LWFort_Stone.tga to LWFort_StoneD.tga, when there is one.
TextureClass *damagedTexture( const char *name )
{
	AsciiString damaged = name;
	const char *dot = strrchr( name, '.' );
	if( dot == NULL )
		return NULL;
	damaged.format( "%.*sD%s", (int)( dot - name ), name, dot );
	AsciiString path;
	path.format( "Art/Textures/%s", damaged.str() );
	if( !TheFileSystem->doesFileExist( path.str() ) )
		return NULL;
	return WW3DAssetManager::Get_Instance()->Get_Texture( damaged.str() );
}

Bool load( Shared &s )
{
	std::vector<unsigned char> bytes;
	if( !readFile( s.name, ".lwfrag", bytes ) || !Fracture::parse( &bytes[0], bytes.size(), s.asset ) )
	{
		DEBUG_LOG( ( "FRACTURE: Art/W3D/%s.lwfrag missing or unreadable\n", s.name.str() ) );
		return FALSE;
	}
	const Fracture::Asset &a = s.asset;
	if( readFile( s.name, ".lwcollapse", bytes ) && !Fracture::parseCollapses( &bytes[0], bytes.size(), a.fragments.size(), s.collapses ) )
		DEBUG_LOG( ( "FRACTURE: Art/W3D/%s.lwcollapse does not fit its .lwfrag, a plain fall instead\n", s.name.str() ) );

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
	}
	s.material = NEW_REF( VertexMaterialClass, () );		// the art's "default": white ambient and diffuse, lit
	s.material->Set_Lighting( true );
	DEBUG_LOG( ( "FRACTURE: %s, %d fragments in %d chunks, %d bones, collapse baked %d ways (none: a plain fall)\n", s.name.str(),
		(Int)count, (Int)s.chunks.size(), (Int)s.boneNames.size(), (Int)s.collapses.size() ) );
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

W3DModelDraw *modelDrawOf( Drawable *draw )
{
	for( DrawModule **dm = draw->getDrawModulesNonDirty(); *dm; ++dm )
	{
		ObjectDrawInterface *di = (*dm)->getObjectDrawInterface();
		if( di && ( (W3DModelDraw *)di )->getRenderObject() )
			return (W3DModelDraw *)di;
	}
	return NULL;
}

}	// namespace

//-------------------------------------------------------------------------------------------------
/** The fragments of one structure in the scene.  It carries the drawable's info, so the scene lights
	it, fogs it and picks it as it does the model it stands in for. */
//-------------------------------------------------------------------------------------------------
class FractureRenderObjClass : public RenderObjClass
{
public:

	FractureRenderObjClass( Shared *shared, Drawable *drawable, const std::vector<AsciiString> &gunBones );
	~FractureRenderObjClass();

	virtual RenderObjClass *Clone( void ) const { return NULL; }		///< never copied: a ghost copies the model
	virtual void Render( RenderInfoClass &rinfo );
	virtual bool Cast_Ray( RayCollisionTestClass &raytest );
	virtual void Get_Obj_Space_Bounding_Sphere( SphereClass &sphere ) const;
	virtual void Get_Obj_Space_Bounding_Box( AABoxClass &box ) const;

	void follow( RenderObjClass *model );		///< the W3DModelDraw's model as it stands this frame
	void hit( const Coord3D &pos, Real radius );
	void heardFrom( const Coord3D &source ) { m_hasSource = TRUE; m_source.Set( source.x, source.y, source.z ); }	///< who fired the last hit
	void collapse( Bool over );									///< over: it fell before a load, straight to the ruin
	void tick( void );													///< every frame, seen or not: what moves, the ruin, dust
	void beforeRender( void );									///< outside the draw: the look and the index buffers
	Bool isCollapsing( void ) const { return m_collapsing; }
	void renderCaster( RenderInfoClass &sunInfo );
	/// What a save keeps: the fragments standing, or at the death those that stood, and the way it fell.
	const std::vector<unsigned char> &standingSet( void ) const { return m_collapsing ? m_present : m_standing; }
	Int collapseWay( void ) const { return m_collapse ? m_way : -1; }
	void restore( const std::vector<unsigned char> &standing, Int way );		///< a load's, before anything moves

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
		Bool landed;
		Vector3 offset;					///< a gun's turn at the death, folded into the bake
		Matrix3x3 turn;
		Real restZ;							///< a ruin piece dropped onto what is under it: its centroid's height there; NO_REST_Z otherwise
	};

	struct Group
	{
		unsigned chunk, slot, start, count;
		unsigned short material;
	};

	void computeSlots( void );
	Matrix3D fragmentWorld( unsigned fragment ) const { return m_slotWorld[m_shared->slotOf[fragment]]; }
	Matrix3D movingWorld( const Moving &m ) const;
	Bool castRayAt( RayCollisionTestClass &raytest, unsigned fragment, const Matrix3D &world ) const;
	Moving still( unsigned fragment, const Matrix3D &world ) const;		///< a fragment as it is, not moving yet
	void launch( unsigned fragment, const Vector3 &from );
	Bool dropUnsupported( Real now );
	void step( Real now );
	void stepTrack( Moving &m, Real now );
	void stepFall( Moving &m, Real dt, Real now );
	Real floorAt( const Vector3 &p ) const;
	void pileOn( unsigned fragment, const Vector3 &p );
	void rebuild( void );
	void bakeRuin( void );
	void drawGeometry( Bool materials );
	void applyMaterial( unsigned short m );

	Shared *m_shared;
	const Fracture::Collapse *m_collapse;		///< the way down it takes, chosen at the death; NULL: a plain fall
	Int m_way;															///< its index, or a load's before the death is replayed; -1 none
	Vector3 m_source;
	Bool m_hasSource;
	Drawable *m_drawable;
	RenderObjClass *m_model;
	std::vector<Int> m_modelBones;				///< per slot, in m_model
	std::vector<Matrix3D> m_slotWorld;
	Matrix3D m_root;
	std::vector<unsigned char> m_standing, m_kept, m_present;
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
};

//-------------------------------------------------------------------------------------------------
FractureRenderObjClass::FractureRenderObjClass( Shared *shared, Drawable *drawable, const std::vector<AsciiString> &gunBones ) :
	m_shared( shared ), m_collapse( NULL ), m_way( -1 ), m_source( 0, 0, 0 ), m_hasSource( FALSE ), m_drawable( drawable ), m_model( NULL ), m_root( true ), m_pileOrigin( 0, 0, 0 ),
	m_now( -1.0f ), m_deathTime( 0.0f ), m_removedVolume( 0.0f ), m_dustLeft( MAX_DUST ), m_dirty( TRUE ),
	m_collapsing( FALSE ), m_dropped( FALSE ), m_settled( FALSE ), m_ruin( FALSE ), m_damagedLook( FALSE )
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
	for( size_t c = 0; c < m_standingIndices.size(); ++c )
		REF_PTR_RELEASE( m_standingIndices[c] );
	for( size_t c = 0; c < m_ruinVertices.size(); ++c )
		REF_PTR_RELEASE( m_ruinVertices[c] );
	--m_shared->users;
	freeUnusedShared();
}

//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::restore( const std::vector<unsigned char> &standing, Int way )
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
	m_dirty = TRUE;
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
	hidden, which nothing else sets on a whole model, so the drawable's own hiding stays its own. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::follow( RenderObjClass *model )
{
	if( model != m_model )
	{
		REF_PTR_SET( m_model, model );
		for( size_t s = 1; s < m_modelBones.size(); ++s )
			m_modelBones[s] = model ? model->Get_Bone_Index( m_shared->boneNames[s].str() ) : 0;
	}
	if( m_model == NULL )
		return;
	m_model->Set_Animation_Hidden( true );
	m_root = m_model->Get_Transform();
	Set_Transform( m_root );
	Set_Collision_Type( m_model->Get_Collision_Type() );
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
		launch( out[i], from );
	}
	if( !out.empty() )
		m_dirty = TRUE;
	DEBUG_LOG( ( "FRACTURE HIT %s frame %d at %.0f %.0f %.0f radius %.0f hole %.1f: %d out, %.0f%% of the volume gone, health %.0f%%\n",
		m_shared->name.str(), (Int)TheGameLogic->getFrame(), local.X, local.Y, local.Z, radius, hole, (Int)out.size(),
		100.0f * m_removedVolume / a.volume, 100.0f * ( 1.0f - lost ) ) );
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
	m_deathTime = m_now + COLLAPSE_DELAY;
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
				pileOn( (unsigned)i, m.pos );
				continue;
			}
			m.track = FALSE;
			Vector3 out = m.pos - m_root.Get_Translation();
			out.Z = 0.0f;
			m.velocity = out * 0.6f + Vector3( GameClientRandomValueReal( -4.0f, 4.0f ), GameClientRandomValueReal( -4.0f, 4.0f ),
				GameClientRandomValueReal( 0.0f, 6.0f ) );
			m.spin = randomAxis() * GameClientRandomValueReal( 0.5f, 3.0f );
			m.start = m_deathTime + GameClientRandomValueReal( 0.0f, 0.3f ) + 0.25f * ( f.centroid[2] / top );
		}
		m_standing[i] = 0;
		m_moving.push_back( m );
	}
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
/// What came to rest raises the rubble under its footprint by its own volume spread over it.
void FractureRenderObjClass::pileOn( unsigned fragment, const Vector3 &p )
{
	if( m_pile.empty() )
		return;
	const Fracture::Fragment &f = m_shared->asset.fragments[fragment];
	const Real halfX = 0.5f * ( f.boxMax[0] - f.boxMin[0] ), halfY = 0.5f * ( f.boxMax[1] - f.boxMin[1] );
	const Real area = std::max( 4.0f * halfX * halfY, PILE_CELL * PILE_CELL );
	const Real rise = std::min( f.volume / area, f.boxMax[2] - f.boxMin[2] );
	const Int x0 = (Int)floorf( ( p.X - halfX - m_pileOrigin.X ) / PILE_CELL ), x1 = (Int)floorf( ( p.X + halfX - m_pileOrigin.X ) / PILE_CELL );
	const Int y0 = (Int)floorf( ( p.Y - halfY - m_pileOrigin.Y ) / PILE_CELL ), y1 = (Int)floorf( ( p.Y + halfY - m_pileOrigin.Y ) / PILE_CELL );
	for( Int y = std::max( y0, 0 ); y <= y1 && y < PILE_CELLS; ++y )
		for( Int x = std::max( x0, 0 ); x <= x1 && x < PILE_CELLS; ++x )
			m_pile[y * PILE_CELLS + x] += rise;
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
		// a ruin piece dropping onto what lies under it: straight down, no bounce
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
	Real lowest = 1e30f;
	for( Int corner = 0; corner < 8; ++corner )
	{
		const Vector3 c( ( corner & 1 ? f.boxMax[0] : f.boxMin[0] ) - f.centroid[0], ( corner & 2 ? f.boxMax[1] : f.boxMin[1] ) - f.centroid[1],
			( corner & 4 ? f.boxMax[2] : f.boxMin[2] ) - f.centroid[2] );
		lowest = std::min( lowest, ( m.rot * c ).Z );
	}
	const Real floor = floorAt( m.pos );
	if( m.pos.Z + lowest > floor )
		return;
	m.pos.Z = floor - lowest;
	if( !m.landed )
	{
		m.landed = TRUE;
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
		pileOn( m.fragment, m.pos );
}

//-------------------------------------------------------------------------------------------------
/** The baked fall is over.  It was baked from the whole structure, so a piece of it that came to rest
	on a fragment a hit had knocked out before the death, or a stump that stood on one, hangs in the
	air: such a piece falls the rest of the way, onto the ground or onto what came down under it
	(Fracture::dropGaps).  TRUE when something drops. */
//-------------------------------------------------------------------------------------------------
Bool FractureRenderObjClass::dropUnsupported( Real now )
{
	if( TheTerrainRenderObject == NULL )
		return FALSE;
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
	DEBUG_LOG( ( "FRACTURE DROP %s frame %d: %d of %d pieces had nothing under them\n", m_shared->name.str(),
		(Int)TheGameLogic->getFrame(), count, (Int)boxes.size() ) );
	return count > 0;
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
		if( m.fromHit && m.restedAt >= 0.0f && now - m.restedAt > DEBRIS_LIES )
		{
			const Fracture::Fragment &f = m_shared->asset.fragments[m.fragment];
			const Real sunk = ( now - m.restedAt - DEBRIS_LIES ) / DEBRIS_SINKS;
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
		if( m_collapse && !m_dropped )
		{
			// every track rests at its last key or hands off to a fall that lands: all at rest is the end
			if( settled )
			{
				m_dropped = TRUE;
				settled = !dropUnsupported( now );
			}
		}
		m_settled = settled || now - m_deathTime > track + FALL_SECONDS;
	}
}

//-------------------------------------------------------------------------------------------------
/** The standing index buffers: what still stands, by the bone it rides on and by material, or once
	baked the whole ruin by material. */
//-------------------------------------------------------------------------------------------------
void FractureRenderObjClass::rebuild( void )
{
	m_dirty = FALSE;
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
	size_t kept = 0;
	for( size_t k = 0; k < m_moving.size(); ++k )
	{
		Moving &m = m_moving[k];
		if( m.fromHit )
		{
			m_moving[kept++] = m;
			continue;
		}
		if( m.restZ != NO_REST_Z )
			m.pos.Z = m.restZ;		// the time ran out on a drop: where it was going
		local[m.fragment] = rootInverse * movingWorld( m );
	}
	m_moving.resize( kept );
	m_ruinPose = local;
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
	if( m_moving.empty() && ( !m_collapsing || m_ruin ) )
		return;		// nothing moves
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
void FractureRenderObjClass::applyMaterial( unsigned short m )
{
	TextureClass *damaged = m_damagedLook ? m_shared->damaged[m] : NULL;
	DX8Wrapper::Set_Texture( 0, damaged ? damaged : m_shared->textures[m] );
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
void FractureRenderObjClass::renderCaster( RenderInfoClass &sunInfo )
{
	if( m_drawable == NULL || m_drawable->isDrawableEffectivelyHidden() || !Is_In_Scene() )
		return;
	// a building in the fog keeps its shadow under its ghost, by the model's own rule
	const W3DModelDraw *modelDraw = modelDrawOf( m_drawable );
	if( modelDraw && modelDraw->isShadowHiddenByShroud() )
		return;
	step( gameSeconds() );		// off the screen the debris still falls, and so does its shadow
	if( m_dirty )
		rebuild();
	computeSlots();
	DX8Wrapper::Set_Material( m_shared->material );
	DX8Wrapper::Set_Texture( 1, NULL );
	drawGeometry( TRUE );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// W3DFractureDraw ////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace {
std::vector<W3DFractureDraw *> s_modules;
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
		m_fracture->Remove();
		REF_PTR_RELEASE( m_fracture );
	}
}

//-------------------------------------------------------------------------------------------------
/// The model the drawable's W3DModelDraw shows this frame, which the fragments stand in for.
static RenderObjClass *modelOf( Drawable *draw )
{
	W3DModelDraw *modelDraw = modelDrawOf( draw );
	return modelDraw ? modelDraw->getRenderObject() : NULL;
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
		m_fracture->restore( m_loadedStanding, m_loadedWay );
		if( rubble )
			m_fracture->collapse( m_loaded );
	}
	m_loadedStanding.clear();
	m_loadedWay = -1;
	if( m_fracture == NULL )
		return;
	m_fracture->follow( modelOf( draw ) );
	if( rubble )
		m_fracture->collapse( FALSE );
	if( m_fracture->isCollapsing() && !SunShadow::OwnsSceneShadows( DX8Wrapper::_Get_D3D_Device() ) )
	{
		// the stencil volumes (-d3d9, Classic) have no caster for the fragments: a holed structure keeps
		// the intact model's shadow, a fallen one has none rather than the standing tower's
		for( DrawModule **dm = draw->getDrawModulesNonDirty(); *dm; ++dm )
			if( (*dm)->getObjectDrawInterface() )
				(*dm)->setShadowsEnabled( FALSE );
	}
	m_fracture->beforeRender();
}

//-------------------------------------------------------------------------------------------------
/// The fragments in the model's place, from its pose now.  FALSE with nothing drawn or no art.
Bool W3DFractureDraw::makeFracture( void )
{
	if( m_fracture )
		return TRUE;
	if( TheGlobalData->m_headless || W3DDisplay::m_3DScene == NULL || W3DDisplay::m_assetManager == NULL )
		return FALSE;
	Shared *shared = sharedFor( getW3DFractureDrawModuleData()->m_fractureFile );
	RenderObjClass *model = modelOf( getDrawable() );
	if( shared == NULL || model == NULL )
		return FALSE;
	m_fracture = NEW_REF( FractureRenderObjClass, ( shared, getDrawable(), getW3DFractureDrawModuleData()->m_gunBones ) );
	m_fracture->follow( model );
	W3DDisplay::m_3DScene->Add_Render_Object( m_fracture );
	s_modules.push_back( this );
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
		m_fracture->restore( m_loadedStanding, m_loadedWay );		// a hit before the first picture after a load
		m_loadedStanding.clear();
		m_loadedWay = -1;
	}
	if( source )
		m_fracture->heardFrom( *source );
	if( radius >= 0.0f )
		m_fracture->hit( pos, radius );
	// the killing hit starts the fall here, in view or not, so an off-screen death is not kept for the
	// moment the camera comes back
	const Object *obj = getDrawable()->getObject();
	if( obj && obj->isEffectivelyDead() )
		m_fracture->collapse( FALSE );
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::updateAll( void )
{
	for( size_t i = 0; i < s_modules.size(); ++i )
		if( s_modules[i]->m_fracture )
			s_modules[i]->m_fracture->tick();
}

//-------------------------------------------------------------------------------------------------
void W3DFractureDraw::renderShadowCasters( RenderInfoClass &sunInfo )
{
	for( size_t i = 0; i < s_modules.size(); ++i )
		if( s_modules[i]->m_fracture )
			s_modules[i]->m_fracture->renderCaster( sunInfo );
}

//-------------------------------------------------------------------------------------------------
Bool W3DFractureDraw::stagesCollapse( const Drawable *draw )
{
	static const NameKeyType key = NAMEKEY( "W3DFractureDraw" );
	for( const DrawModule **dm = draw->getDrawModules(); *dm; ++dm )
		if( (*dm)->getModuleNameKey() == key )
			return sharedFor( ( (const W3DFractureDraw *)*dm )->getW3DFractureDrawModuleData()->m_fractureFile ) != NULL;
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
	*    reads it and the CRC never sees it. */
// ------------------------------------------------------------------------------------------------
void W3DFractureDraw::xfer( Xfer *xfer )
{
	const Int MOST_FRAGMENTS = 65536;
	XferVersion currentVersion = 2;
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
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_loadedStanding.swap( standing );
		m_loadedWay = way;
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
