// The ground a defence can hit, on the terrain.  Direct3D 11 draws it in DX11ReachOverlay.cpp from
// the distance fields themselves; this file picks the pass and holds the fixed-function one that
// -d3d9, SDL3 and a refused Direct3D 11 program fall back to.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "W3DDevice/GameClient/W3DReachOverlay.h"

#include "Common/Debug.h"
#include "Common/MapObject.h"
#include "GameClient/ReachOverlay.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/shader.h"
#include "WW3D2/texture.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/surfaceclass.h"

#include <cmath>

namespace
{

/* The fixed-function look is a texture baked from the field, drawn on the terrain's own cells.

	 The baked grid is the field's grid cut n ways, so every baked texel's cell lies inside one of the
	 field's cells and the field taken bilinearly over it is a blend of its four corners.  A texel is
	 lit only when it and its eight neighbours are all inside, so whatever the filter blends into a
	 pixel comes from a baked cell whose four corners are inside, and the field there is inside too:
	 the area shrinks by at most one baked texel and never grows. */
const Int MaxBakedSide = 512;
const Real BakedTexel = 0.75f;			// finest texel, world units: half the border line
const Real InsideMargin = 0.05f;		// world units, for the filter's own rounding
const Real CellLift = 0.01f * MAP_XY_FACTOR;	// the projected decals' lift off the ground they are laid on

struct Baked
{
	TextureClass *texture;
	UnsignedInt version;
	Real originX, originY, step;
	Int width, height;
	Int color;							// the owner's, baked in: the line is lighter than the fill
	Real texel;							// world units a baked texel spans
	Int side[ 2 ];					// the texture's size, a power of two each way
	Bool used;
};
std::vector< Baked > s_baked;

Bool sameGrid( const Baked &baked, const ReachOverlayField &field )
{
	return baked.version == field.version && baked.width == field.width && baked.height == field.height
		&& baked.originX == field.originX && baked.originY == field.originY && baked.step == field.step
		&& baked.color == ( field.color & 0xFFFFFF );
}

Real clamp01( Real value )
{
	return value < 0.0f ? 0.0f : ( value > 1.0f ? 1.0f : value );
}

// One of the field's grids (distance or hardness) taken bilinearly at grid coordinates (gx, gy), both
// inside the grid.
Real fieldAt( const ReachOverlayField &field, const std::vector< Real > &grid, Real gx, Real gy )
{
	Int i = (Int)floorf( gx ), j = (Int)floorf( gy );
	if( i > field.width - 2 ) i = field.width - 2;
	if( j > field.height - 2 ) j = field.height - 2;
	const Real fx = gx - (Real)i, fy = gy - (Real)j;
	const Real *row = &grid[ j * field.width + i ];
	const Real bottom = row[ 0 ] + ( row[ 1 ] - row[ 0 ] ) * fx;
	const Real top = row[ field.width ] + ( row[ field.width + 1 ] - row[ field.width ] ) * fx;
	return bottom + ( top - bottom ) * fy;
}

/** A baked texel at distance d inside the edge, as straight colour and alpha for a SRCALPHA blend,
	* the Direct3D 11 look without the ripple.  The lit texels start up to a texel and a half in (the
	* nine-neighbour rule), so the line is laid from there, and at least two texels wide: at the
	* border's own 1.5 units a coarse texel held half a line, which the filter smeared into the fade and
	* no edge showed.  hard is the field's hardness: 1 a range edge with its line, 0 a blocked edge that
	* fades in over the soft width. */
UnsignedInt texelAt( Real d, Real hard, Real texel, const Real owner[ 3 ] )
{
	hard = clamp01( hard );
	const Real lineEnd = 1.5f * texel + ( ReachLook::BorderWidth > 2.0f * texel ? ReachLook::BorderWidth : 2.0f * texel );
	const Real line = clamp01( ( lineEnd - d ) / texel ) * ReachLook::LineAlpha
		* ( ReachLook::SoftLine + ( 1.0f - ReachLook::SoftLine ) * hard );
	const Real shade = ( d >= lineEnd && d < lineEnd + texel ? ReachLook::ShadeAlpha * hard : 0.0f ) * ( 1.0f - line );
	Real soft = clamp01( d / ReachLook::SoftWidth );
	soft = soft * soft * ( 3.0f - 2.0f * soft );
	Real band = clamp01( 1.0f - d / ReachLook::BandWidth );
	band *= band;
	const Real cover = soft + ( 1.0f - soft ) * hard;
	const Real fill = ( ReachLook::FillAlpha + ReachLook::BandAlpha * band ) * cover * ( 1.0f - line ) * ( 1.0f - shade );
	const Real alpha = line + shade + fill;
	UnsignedInt result = (UnsignedInt)( clamp01( alpha ) * 255.0f + 0.5f ) << 24;
	for( Int c = 0; c < 3; ++c )
	{
		const Real lineColour = owner[ c ] + ( 1.0f - owner[ c ] ) * ReachLook::LineWhite;
		const Real colour = alpha > 0.0f ? ( lineColour * line + owner[ c ] * fill ) / alpha : owner[ c ];
		result |= (UnsignedInt)( clamp01( colour ) * 255.0f + 0.5f ) << ( 16 - 8 * c );
	}
	return result;
}

Int powerOfTwo( Int value )
{
	Int result = 1;
	while( result < value )
		result <<= 1;
	return result;
}

Bool bake( const ReachOverlayField &field, Baked &baked )
{
	Int cuts = (Int)ceilf( field.step / BakedTexel );
	cuts = cuts < 1 ? 1 : ( cuts > 8 ? 8 : cuts );
	while( cuts > 1 && ( ( field.width - 1 ) * cuts + 1 > MaxBakedSide || ( field.height - 1 ) * cuts + 1 > MaxBakedSide ) )
		--cuts;
	const Int across = ( field.width - 1 ) * cuts + 1, down = ( field.height - 1 ) * cuts + 1;
	if( across > MaxBakedSide * 2 || down > MaxBakedSide * 2 )
		return FALSE;	// ponytail: a field past 1024 samples a side is left out here; cut the grid if one ever comes

	std::vector< Real > d( across * down ), hard( across * down, 1.0f );
	const Bool hasHardness = field.hardness.size() >= field.distance.size();
	for( Int y = 0; y < down; ++y )
	{
		for( Int x = 0; x < across; ++x )
		{
			d[ y * across + x ] = fieldAt( field, field.distance, (Real)x / cuts, (Real)y / cuts );
			if( hasHardness )
				hard[ y * across + x ] = fieldAt( field, field.hardness, (Real)x / cuts, (Real)y / cuts );
		}
	}

	baked.texel = field.step / cuts;
	baked.side[ 0 ] = powerOfTwo( across );
	baked.side[ 1 ] = powerOfTwo( down );
	baked.texture = MSGNEW( "TextureClass" ) TextureClass( baked.side[ 0 ], baked.side[ 1 ], WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1 );
	SurfaceClass *surface = baked.texture->Get_Surface_Level();
	Int pitch = 0;
	UnsignedByte *bits = surface ? (UnsignedByte *)surface->Lock( &pitch ) : NULL;
	if( bits == NULL )
	{
		REF_PTR_RELEASE( surface );
		REF_PTR_RELEASE( baked.texture );
		return FALSE;
	}
	const Real owner[ 3 ] = { ( ( field.color >> 16 ) & 0xff ) / 255.0f, ( ( field.color >> 8 ) & 0xff ) / 255.0f,
		( field.color & 0xff ) / 255.0f };
	// what an unlit texel holds: no alpha and the line's colour, so the filter blends no other colour
	// into the edge it fades out on
	const UnsignedInt unlit = texelAt( 0.0f, 1.0f, baked.texel, owner ) & 0x00FFFFFF;
	for( Int y = 0; y < baked.side[ 1 ]; ++y )
	{
		UnsignedInt *row = (UnsignedInt *)( bits + y * pitch );
		for( Int x = 0; x < baked.side[ 0 ]; ++x )
		{
			Bool lit = x > 0 && y > 0 && x < across - 1 && y < down - 1;
			for( Int dy = -1; lit && dy <= 1; ++dy )
				for( Int dx = -1; lit && dx <= 1; ++dx )
					lit = d[ ( y + dy ) * across + x + dx ] > InsideMargin;
			row[ x ] = lit ? texelAt( d[ y * across + x ], hard[ y * across + x ], baked.texel, owner ) : unlit;
		}
	}
	surface->Unlock();
	REF_PTR_RELEASE( surface );
	baked.texture->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	baked.texture->Get_Filter().Set_V_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	return TRUE;
}

Baked *bakedFor( const ReachOverlayField &field )
{
	for( size_t i = 0; i < s_baked.size(); ++i )
		if( sameGrid( s_baked[ i ], field ) )
			return &s_baked[ i ];
	Baked baked = {};
	baked.version = field.version;
	baked.originX = field.originX;
	baked.originY = field.originY;
	baked.step = field.step;
	baked.width = field.width;
	baked.height = field.height;
	baked.color = field.color & 0xFFFFFF;
	if( !bake( field, baked ) )
		return NULL;
	s_baked.push_back( baked );
	return &s_baked.back();
}

// Prelit, textured, alpha blended over the ground and depth tested against it, no depth written.
#define SC_REACH_OVERLAY ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

/** One field on the terrain's own vertices: the heights and the diagonal of every cell as the ground
	* is drawn (queueDecal's fill), lifted as the decals are, so it lies on the ground without fighting
	* it.  Several fields of one owner overlap here as two sheets. */
void drawCells( const ReachOverlayField &field, const Baked &baked )
{
	WorldHeightMap *map = TheTerrainRenderObject->getMap();
	if( map == NULL )
		return;
	const Int border = map->getBorderSizeInline();
	const Int lastX = map->getXExtent() - 1, lastY = map->getYExtent() - 1;
	const Real maxX = field.originX + ( field.width - 1 ) * field.step;
	const Real maxY = field.originY + ( field.height - 1 ) * field.step;
	Int startX = (Int)floorf( field.originX / MAP_XY_FACTOR ) + border;
	Int endX = (Int)ceilf( maxX / MAP_XY_FACTOR ) + border;
	Int startY = (Int)floorf( field.originY / MAP_XY_FACTOR ) + border;
	Int endY = (Int)ceilf( maxY / MAP_XY_FACTOR ) + border;
	startX = startX < 0 ? 0 : ( startX > lastX ? lastX : startX );
	endX = endX < 0 ? 0 : ( endX > lastX ? lastX : endX );
	startY = startY < 0 ? 0 : ( startY > lastY ? lastY : startY );
	endY = endY < 0 ? 0 : ( endY > lastY ? lastY : endY );
	if( endX <= startX || endY <= startY )
		return;
	// on the subdivided ground its half cells and their heights (drawnHalfZ), as the decals lie
	const Int step = TheTerrainRenderObject->drawnStep();
	const Int vertsPerRow = ( 2 * ( endX - startX ) ) / step + 1;
	const Int rowsAll = ( 2 * ( endY - startY ) ) / step;

	// the colour is in the texture; the vertex carries the opacity alone
	const UnsignedInt alpha = (UnsignedInt)( clamp01( field.opacity ) * 255.0f + 0.5f );
	const UnsignedInt diffuse = ( alpha << 24 ) | 0x00FFFFFF;
	const Real uScale = 1.0f / ( baked.texel * baked.side[ 0 ] ), vScale = 1.0f / ( baked.texel * baked.side[ 1 ] );
	const Real uHalf = 0.5f / baked.side[ 0 ], vHalf = 0.5f / baked.side[ 1 ];

	// in row bands, so a band's vertices and its 16 bit indices fit the dynamic buffers
	const Int cellsPerRow = vertsPerRow - 1;
	Int rowsPerBand = 16000 / vertsPerRow - 1;
	if( rowsPerBand * cellsPerRow * 6 > 60000 )
		rowsPerBand = 60000 / ( cellsPerRow * 6 );
	if( rowsPerBand < 1 )
		return;
	for( Int bandStart = 0; bandStart < rowsAll; bandStart += rowsPerBand )
	{
		const Int bandEnd = bandStart + rowsPerBand < rowsAll ? bandStart + rowsPerBand : rowsAll;
		const Int rows = bandEnd - bandStart;
		const Int verts = ( rows + 1 ) * vertsPerRow;
		DynamicVBAccessClass vbAccess( BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, verts );
		DynamicIBAccessClass ibAccess( BUFFER_TYPE_DYNAMIC_DX8, rows * cellsPerRow * 6 );
		{
			DynamicVBAccessClass::WriteLockClass vbLock( &vbAccess );
			DynamicIBAccessClass::WriteLockClass ibLock( &ibAccess );
			VertexFormatXYZNDUV2 *vb = vbLock.Get_Formatted_Vertex_Array();
			UnsignedShort *ib = ibLock.Get_Index_Array();
			if( vb == NULL || ib == NULL )
				return;
			for( Int j = bandStart; j <= bandEnd; ++j )
			{
				const Int y2 = 2 * startY + j * step;
				const Real y = (Real)( y2 - 2 * border ) * ( 0.5f * MAP_XY_FACTOR );
				for( Int i = 0; i < vertsPerRow; ++i, ++vb )
				{
					const Int x2 = 2 * startX + i * step;
					const Real x = (Real)( x2 - 2 * border ) * ( 0.5f * MAP_XY_FACTOR );
					vb->x = x;
					vb->y = y;
					vb->z = TheTerrainRenderObject->drawnHalfZ( x2, y2 ) + CellLift;
					vb->nx = 0.0f;
					vb->ny = 0.0f;
					vb->nz = 1.0f;
					vb->diffuse = diffuse;
					vb->u1 = ( x - field.originX ) * uScale + uHalf;
					vb->v1 = ( y - field.originY ) * vScale + vHalf;
					vb->u2 = 0.0f;
					vb->v2 = 0.0f;
				}
			}
			for( Int j = 0; j < rows; ++j )
			{
				for( Int k = 0; k < cellsPerRow; ++k )
				{
					const UnsignedShort at = (UnsignedShort)( j * vertsPerRow + k );
					// the quad's cell, cut along its diagonal as the tiles cut it
					const Int cellX = ( 2 * startX + k * step ) >> 1, cellY = ( 2 * startY + ( bandStart + j ) * step ) >> 1;
					if( step == 1 ? TheTerrainRenderObject->drawnFlip( cellX, cellY ) : map->getQuickFlipState( cellX, cellY ) )
					{
						*ib++ = at + 1; *ib++ = at + vertsPerRow; *ib++ = at;
						*ib++ = at + 1; *ib++ = at + 1 + vertsPerRow; *ib++ = at + vertsPerRow;
					}
					else
					{
						*ib++ = at; *ib++ = at + 1 + vertsPerRow; *ib++ = at + vertsPerRow;
						*ib++ = at; *ib++ = at + 1; *ib++ = at + 1 + vertsPerRow;
					}
				}
			}
		}
		DX8Wrapper::Set_Index_Buffer( ibAccess, 0 );
		DX8Wrapper::Set_Vertex_Buffer( vbAccess );
		DX8Wrapper::Draw_Triangles( 0, rows * cellsPerRow * 2, 0, verts );
	}
}

void drawFixedFunction( const std::vector< const ReachOverlayField * > &fields )
{
	if( TheTerrainRenderObject == NULL || DX8Wrapper::_Get_D3D_Device() == NULL )
		return;
	static ShaderClass shader( SC_REACH_OVERLAY );
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset( VertexMaterialClass::PRELIT_DIFFUSE );
	DX8Wrapper::Set_Material( material );
	REF_PTR_RELEASE( material );
	DX8Wrapper::Set_Shader( shader );
	DX8Wrapper::Set_Transform( D3DTS_WORLD, Matrix3D( 1 ) );
	for( size_t i = 0; i < fields.size(); ++i )
	{
		Baked *baked = bakedFor( *fields[ i ] );
		if( baked == NULL )
			continue;
		baked->used = TRUE;
		DX8Wrapper::Set_Texture( 0, baked->texture );
		DX8Wrapper::Set_Texture( 1, NULL );
		drawCells( *fields[ i ], *baked );
	}
	DX8Wrapper::Set_Texture( 0, NULL );
	ShaderClass::Invalidate();
	static Bool logged = FALSE;
	if( !logged )
	{
		logged = TRUE;
		DEBUG_LOG( ( "REACHOVERLAY: fixed-function sheet drawn, %d fields\n", (Int)fields.size() ) );
	}
}

}	// namespace

void W3DReachOverlay_Release( void )
{
	for( size_t i = 0; i < s_baked.size(); ++i )
		REF_PTR_RELEASE( s_baked[ i ].texture );
	s_baked.clear();
#ifdef _WIN32
	DX11ReachOverlay_Release();
#else
	SdlReachOverlay_Release(); // refusal retains the native owner until readers can submit
#endif
}

void W3DReachOverlay_Draw( CameraClass &camera )
{
	static std::vector< const ReachOverlayField * > fields;
	fields.clear();
	ReachOverlay_getFields( fields );
	// a field with no cell to blend cannot be drawn by either pass
	for( size_t i = fields.size(); i-- > 0; )
	{
		const ReachOverlayField *field = fields[ i ];
		if( field->width < 2 || field->height < 2 || field->step <= 0.0f || field->opacity <= 0.0f
			|| field->distance.size() < (size_t)field->width * field->height )
			fields.erase( fields.begin() + i );
	}

	for( size_t i = 0; i < s_baked.size(); ++i )
		s_baked[ i ].used = FALSE;
	// Direct3D 11 is asked with no fields too, so it lets go of its textures
	Bool drawn = fields.empty();
#ifdef _WIN32
	drawn = DX11ReachOverlay_Draw( camera, fields ) || drawn;
#else
	drawn = SdlReachOverlay_Draw( camera, fields ) || drawn;
#endif
	if( !drawn )
		drawFixedFunction( fields );
	// a texture no field drew this frame goes; a field shown again is baked again, which is cheap
	for( size_t i = s_baked.size(); i-- > 0; )
	{
		if( !s_baked[ i ].used )
		{
			REF_PTR_RELEASE( s_baked[ i ].texture );
			s_baked.erase( s_baked.begin() + i );
		}
	}
}
