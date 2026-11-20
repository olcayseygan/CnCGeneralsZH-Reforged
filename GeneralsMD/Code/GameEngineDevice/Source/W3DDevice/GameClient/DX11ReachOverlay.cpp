// The ground a defence can hit, drawn on Direct3D 11 as a decal read back out of the scene's depth.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
//
// The pass runs from the terrain pass, when the depth holds the ground, its roads, bridges and
// tracks and nothing standing on them.  Each pixel goes back to the world through its own depth, so
// the overlay lies exactly on whatever the ground drew there: no geometry of its own, nothing to
// fight the ground's depth, no slope it can float off.  The fields are sampled bilinearly by hand
// and the edges smoothed by the pixel's own footprint (fwidth), so no zoom shows their grid.
//
// The fields of one owner are drawn in one pass as one area, the union of their distances (the
// largest), so overlaps neither darken nor draw a line through each other.
//
// The look, like the game's own radius decals: a thin wash in the owner's colour, stronger toward the
// edge, and along the end of the guns' reach a light line with a dark hairline under it and a faint
// band that leaves it every few seconds and runs inward.  Where a cliff, a building or the fog cuts
// the area there is no line; the wash fades in from the edge instead (the field's hardness).
#include "W3DDevice/GameClient/W3DReachOverlay.h"
#include "W3DDevice/GameClient/ReachOverlayShaderSources.h"

#include "Common/Debug.h"
#include "Common/MapObject.h"
#include "GameClient/ReachOverlay.h"
#include "Platform/HazardCloudCore.h"
#include "WW3D2/camera.h"
#include "WW3D2/ww3d.h"

#include "dx11backend.h"
#include "dx11device.h"
#include "dx11runtime.h"

#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// a pass's worth of fields of one owner; past it a second pass draws over the first.  The program's
// arrays below hold the same number.
enum { FieldsPerPass = ReachOverlayShaderSources::FieldsPerPass };

template < class T > void drop( T *&object )
{
	if( object )
	{
		object->Release();
		object = nullptr;
	}
}

using ReachOverlayShaderSources::Constants;
using ReachOverlayShaderSources::PixelSource;

struct FieldTexture
{
	ID3D11Texture2D *texture = nullptr;
	ID3D11ShaderResourceView *view = nullptr;
	UnsignedInt version = 0;
	Real originX = 0, originY = 0, step = 0;
	Int width = 0, height = 0;
	Bool used = false;
};

struct Shared
{
	ID3D11Device *device = nullptr;
	Bool refused = false;
	ID3D11VertexShader *fullscreen = nullptr;
	ID3D11PixelShader *pixel = nullptr;
	ID3D11BlendState *blend = nullptr;
	ID3D11RasterizerState *rasterizer = nullptr;
	ID3D11Buffer *constants = nullptr;
	std::vector< FieldTexture > fields;
};
Shared shared;

void releaseFields()
{
	for( FieldTexture &field : shared.fields )
	{
		drop( field.view );
		drop( field.texture );
	}
	shared.fields.clear();
}

void releaseShared()
{
	releaseFields();
	drop( shared.fullscreen );
	drop( shared.pixel );
	drop( shared.blend );
	drop( shared.rasterizer );
	drop( shared.constants );
	shared.device = nullptr;
	shared.refused = false;
}

Bool makeShared( DX11BackendClass *backend, ID3D11Device *device )
{
	if( shared.device == device )
		return !shared.refused;
	releaseShared();
	shared.device = device;
	std::vector< unsigned char > code;
	// the hazard clouds' full screen triangle, the same text, so the same program in the cache
	Bool made = backend->Compile_Program( HazardCloud::vertexShader(), "reach overlay fullscreen", "vs_5_0", code )
		&& SUCCEEDED( device->CreateVertexShader( code.data(), code.size(), nullptr, &shared.fullscreen ) );
	made = made && backend->Compile_Program( PixelSource, "reach overlay", "ps_5_0", code )
		&& SUCCEEDED( device->CreatePixelShader( code.data(), code.size(), nullptr, &shared.pixel ) );
	if( !made )
		DEBUG_LOG( ( "REACHOVERLAY: the Direct3D 11 program did not compile: %s\n", backend->First_Compiler_Error() ) );

	// premultiplied over the scene, its alpha left alone
	D3D11_BLEND_DESC blend = {};
	D3D11_RENDER_TARGET_BLEND_DESC &target = blend.RenderTarget[ 0 ];
	target.BlendEnable = TRUE;
	target.SrcBlend = D3D11_BLEND_ONE;
	target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	target.SrcBlendAlpha = D3D11_BLEND_ZERO;
	target.DestBlendAlpha = D3D11_BLEND_ONE;
	target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
	made = made && SUCCEEDED( device->CreateBlendState( &blend, &shared.blend ) );

	D3D11_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	rasterizer.ScissorEnable = TRUE;
	made = made && SUCCEEDED( device->CreateRasterizerState( &rasterizer, &shared.rasterizer ) );

	D3D11_BUFFER_DESC buffer = {};
	buffer.Usage = D3D11_USAGE_DYNAMIC;
	buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buffer.ByteWidth = sizeof( Constants );
	made = made && SUCCEEDED( device->CreateBuffer( &buffer, nullptr, &shared.constants ) );
	if( !made )
	{
		DEBUG_LOG( ( "REACHOVERLAY: Direct3D 11 refused the pass; the fixed-function overlay draws instead\n" ) );
		releaseShared();
		shared.device = device;
		shared.refused = true;
	}
	return made;
}

// The field's distances as a texture, kept while the field's version and grid hold.
ID3D11ShaderResourceView *viewFor( const ReachOverlayField &field )
{
	for( FieldTexture &held : shared.fields )
	{
		if( held.version == field.version && held.width == field.width && held.height == field.height
			&& held.originX == field.originX && held.originY == field.originY && held.step == field.step )
		{
			held.used = true;
			return held.view;
		}
	}
	FieldTexture made;
	D3D11_TEXTURE2D_DESC description = {};
	description.Width = (UINT)field.width;
	description.Height = (UINT)field.height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R32G32_FLOAT;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	const size_t samples = (size_t)field.width * field.height;
	std::vector< float > texels( samples * 2 );
	for( size_t k = 0; k < samples; ++k )
	{
		texels[ 2 * k ] = field.distance[ k ];
		texels[ 2 * k + 1 ] = k < field.hardness.size() ? field.hardness[ k ] : 1.0f;
	}
	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = texels.data();
	data.SysMemPitch = (UINT)( field.width * 2 * sizeof( float ) );
	if( FAILED( shared.device->CreateTexture2D( &description, &data, &made.texture ) )
		|| FAILED( shared.device->CreateShaderResourceView( made.texture, nullptr, &made.view ) ) )
	{
		drop( made.view );
		drop( made.texture );
		return nullptr;
	}
	made.version = field.version;
	made.originX = field.originX;
	made.originY = field.originY;
	made.step = field.step;
	made.width = field.width;
	made.height = field.height;
	made.used = true;
	shared.fields.push_back( made );
	return made.view;
}

// The camera as the scene is drawn from it: CameraClass::Apply's matrices and viewport.
struct SceneCamera
{
	float viewProjection[ 16 ];
	float worldFromClip[ 16 ];
	float x, y, width, height, minZ, maxZ;
};

Bool sceneCamera( CameraClass &camera, SceneCamera &result )
{
	Matrix4x4 projection;
	camera.Get_D3D_Projection_Matrix( &projection );
	Matrix3D view;
	camera.Get_View_Matrix( &view );
	const Matrix4x4 d3dProjection = projection.Transpose();
	const Matrix4x4 d3dView = Matrix4x4( view ).Transpose();
	float viewRows[ 16 ], projectionRows[ 16 ];
	memcpy( viewRows, &d3dView, sizeof( viewRows ) );
	memcpy( projectionRows, &d3dProjection, sizeof( projectionRows ) );
	HazardCloud::multiply( viewRows, projectionRows, result.viewProjection );
	if( !HazardCloud::inverse( result.viewProjection, result.worldFromClip ) )
		return false;

	int width, height, bits;
	bool windowed;
	WW3D::Get_Render_Target_Resolution( width, height, bits, windowed );
	const ViewportClass &viewport = camera.Get_Viewport();
	result.x = (float)(unsigned)( viewport.Min.X * (float)width );
	result.y = (float)(unsigned)( viewport.Min.Y * (float)height );
	result.width = (float)(unsigned)( ( viewport.Max.X - viewport.Min.X ) * (float)width );
	result.height = (float)(unsigned)( ( viewport.Max.Y - viewport.Min.Y ) * (float)height );
	camera.Get_Depth_Range( &result.minZ, &result.maxZ );
	const float range = result.maxZ - result.minZ;
	if( result.width < 1 || result.height < 1 || range <= 1.0e-8f )
		return false;
	// the depth read back is the viewport's, minZ to maxZ, not the projection's nought to one
	for( unsigned column = 0; column < 4; ++column )
	{
		const float row = result.worldFromClip[ 8 + column ] / range;
		result.worldFromClip[ 12 + column ] -= result.minZ * row;
		result.worldFromClip[ 8 + column ] = row;
	}
	return true;
}

/** The pixels a group of fields can cover: their rectangles from below the lowest ground to above
	* the highest bridge, through the camera.  The whole viewport when a corner is behind it. */
D3D11_RECT coverage( const SceneCamera &camera, const ReachOverlayField *const *fields, unsigned count,
	unsigned targetWidth, unsigned targetHeight )
{
	const LONG left = (LONG)camera.x, top = (LONG)camera.y;
	const LONG right = (LONG)( std::min )( camera.x + camera.width, (float)targetWidth );
	const LONG bottom = (LONG)( std::min )( camera.y + camera.height, (float)targetHeight );
	const D3D11_RECT all = { left, top, right, bottom };
	const float lowZ = -20.0f, highZ = 255.0f * MAP_HEIGHT_SCALE + 80.0f;
	float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
	for( unsigned f = 0; f < count; ++f )
	{
		const ReachOverlayField &field = *fields[ f ];
		const float xs[ 2 ] = { field.originX, field.originX + ( field.width - 1 ) * field.step };
		const float ys[ 2 ] = { field.originY, field.originY + ( field.height - 1 ) * field.step };
		const float zs[ 2 ] = { lowZ, highZ };
		for( unsigned corner = 0; corner < 8; ++corner )
		{
			const float point[ 4 ] = { xs[ corner & 1 ], ys[ ( corner >> 1 ) & 1 ], zs[ corner >> 2 ], 1.0f };
			float clip[ 4 ] = {};
			for( unsigned column = 0; column < 4; ++column )
				for( unsigned row = 0; row < 4; ++row )
					clip[ column ] += point[ row ] * camera.viewProjection[ row * 4 + column ];
			if( clip[ 3 ] <= 0.001f )
				return all;
			const float px = camera.x + ( clip[ 0 ] / clip[ 3 ] * 0.5f + 0.5f ) * camera.width;
			const float py = camera.y + ( 0.5f - clip[ 1 ] / clip[ 3 ] * 0.5f ) * camera.height;
			minX = ( std::min )( minX, px );
			maxX = ( std::max )( maxX, px );
			minY = ( std::min )( minY, py );
			maxY = ( std::max )( maxY, py );
		}
	}
	D3D11_RECT rect;
	rect.left = ( std::max )( left, (LONG)floorf( minX ) - 1 );
	rect.top = ( std::max )( top, (LONG)floorf( minY ) - 1 );
	rect.right = ( std::min )( right, (LONG)ceilf( maxX ) + 2 );
	rect.bottom = ( std::min )( bottom, (LONG)ceilf( maxY ) + 2 );
	return rect;
}

Bool supported( DX11DeviceClass *device )
{
	return Direct3D11_Present_Is_Enabled() && Direct3D11_Backend() && device && device->Get_Device()
		&& device->Get_Depth_Texture() && device->Get_Device()->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

}	// namespace

void DX11ReachOverlay_Release( void )
{
	releaseShared();
}

Bool DX11ReachOverlay_Draw( CameraClass &camera, const std::vector< const ReachOverlayField * > &fields )
{
	DX11DeviceClass *device = Direct3D11_Device();
	DX11BackendClass *backend = Direct3D11_Backend();
	if( !supported( device ) )
	{
		if( shared.device )
			releaseShared();
		return false;
	}
	// made on the first terrain frame of a device (shell map or match), so the first defence shown
	// does not pay a compile; the cache makes it cheap after the first run
	const Bool made = makeShared( backend, device->Get_Device() );
	if( fields.empty() )
	{
		releaseFields();
		return true;
	}
	if( !made )
		return false;
	// a mirror or the sun's map has no scene target; the fixed-function sheet goes there
	ID3D11RenderTargetView *scene = backend->Scene_Target();
	if( !scene )
		return false;
	SceneCamera view;
	if( !sceneCamera( camera, view ) )
		return true;	// nothing on the screen to draw on

	ID3D11DepthStencilView *sceneDepth = backend->Scene_Depth_Target();
	backend->Resolve_Scene_Depth();
	ID3D11ShaderResourceView *depth = device->Get_Depth_Texture();
	ID3D11DeviceContext *context = device->Get_Context();
	ID3D11Resource *sceneResource = nullptr;
	scene->GetResource( &sceneResource );
	D3D11_TEXTURE2D_DESC sceneDescription;
	static_cast< ID3D11Texture2D * >( sceneResource )->GetDesc( &sceneDescription );
	sceneResource->Release();	// the backend's own view keeps it

	for( FieldTexture &held : shared.fields )
		held.used = false;

	// the fields of each owner together, in runs of up to a pass's worth
	std::vector< const ReachOverlayField * > order( fields );
	std::stable_sort( order.begin(), order.end(),
		[]( const ReachOverlayField *a, const ReachOverlayField *b ) { return ( a->color & 0xFFFFFF ) < ( b->color & 0xFFFFFF ); } );

	// Everything below binds on the context behind the backend's back: the viewport it set is put
	// back, the targets with their depth, and it forgets the rest.
	D3D11_VIEWPORT savedViewport;
	UINT viewportCount = 1;
	context->RSGetViewports( &viewportCount, &savedViewport );
	const D3D11_VIEWPORT sceneViewport = { view.x, view.y, view.width, view.height, view.minZ, view.maxZ };
	context->RSSetViewports( 1, &sceneViewport );
	context->OMSetRenderTargets( 1, &scene, nullptr );
	context->OMSetBlendState( shared.blend, nullptr, 0xffffffff );
	context->OMSetDepthStencilState( nullptr, 0 );
	context->RSSetState( shared.rasterizer );
	context->IASetInputLayout( nullptr );
	context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	context->VSSetShader( shared.fullscreen, nullptr, 0 );
	context->PSSetShader( shared.pixel, nullptr, 0 );
	context->PSSetConstantBuffers( 0, 1, &shared.constants );

	Constants constants = {};
	memcpy( constants.WorldFromClip, view.worldFromClip, sizeof( constants.WorldFromClip ) );
	constants.Viewport[ 0 ] = view.x + 0.5f;
	constants.Viewport[ 1 ] = view.y + 0.5f;
	constants.Viewport[ 2 ] = view.width;
	constants.Viewport[ 3 ] = view.height;
	constants.Look0[ 0 ] = ReachLook::BorderWidth;
	constants.Look0[ 1 ] = ReachLook::BandWidth;
	constants.Look0[ 2 ] = ReachLook::FillAlpha;
	constants.Look0[ 3 ] = ReachLook::BandAlpha;
	constants.Look1[ 0 ] = ReachLook::LineAlpha;
	constants.Look1[ 1 ] = ReachLook::LineWhite;
	// the render clock, wrapped so a float keeps its milliseconds; the ripple's period divides it
	constants.Look1[ 3 ] = (float)( WW3D::Get_Sync_Time() % 600000u ) * 0.001f;
	constants.Look2[ 0 ] = ReachLook::SoftWidth;
	constants.Look2[ 1 ] = ReachLook::SoftLine;
	constants.Look2[ 2 ] = ReachLook::RippleAlpha;
	constants.Look2[ 3 ] = ReachLook::RipplePeriod;
	constants.Look3[ 0 ] = ReachLook::RippleSpan;
	constants.Look3[ 1 ] = ReachLook::RippleWidth;
	constants.Look3[ 2 ] = ReachLook::ShadeWidth;
	constants.Look3[ 3 ] = ReachLook::ShadeAlpha;

	for( size_t start = 0; start < order.size(); )
	{
		const Int colour = order[ start ]->color & 0xFFFFFF;
		const ReachOverlayField *group[ FieldsPerPass ];
		ID3D11ShaderResourceView *views[ 1 + FieldsPerPass ] = { depth };
		unsigned count = 0;
		size_t next = start;
		for( ; next < order.size() && count < FieldsPerPass && ( order[ next ]->color & 0xFFFFFF ) == colour; ++next )
		{
			ID3D11ShaderResourceView *field = viewFor( *order[ next ] );
			if( !field )
				continue;
			const ReachOverlayField &source = *order[ next ];
			constants.Grid[ count ][ 0 ] = source.originX;
			constants.Grid[ count ][ 1 ] = source.originY;
			constants.Grid[ count ][ 2 ] = 1.0f / source.step;
			constants.Grid[ count ][ 3 ] = -( std::max )( source.distanceLimit, 1.0f );
			constants.Size[ count ][ 0 ] = (float)( source.width - 1 );
			constants.Size[ count ][ 1 ] = (float)( source.height - 1 );
			constants.Size[ count ][ 2 ] = ( std::min )( ( std::max )( source.opacity, 0.0f ), 1.0f );
			views[ 1 + count ] = field;
			group[ count++ ] = &source;
		}
		start = next;
		if( count == 0 )
			continue;
		constants.Colour[ 0 ] = ( ( colour >> 16 ) & 0xff ) / 255.0f;
		constants.Colour[ 1 ] = ( ( colour >> 8 ) & 0xff ) / 255.0f;
		constants.Colour[ 2 ] = ( colour & 0xff ) / 255.0f;
		constants.Look1[ 2 ] = (float)count;
		const D3D11_RECT rect = coverage( view, group, count, sceneDescription.Width, sceneDescription.Height );
		if( rect.right <= rect.left || rect.bottom <= rect.top )
			continue;
		D3D11_MAPPED_SUBRESOURCE mapped;
		if( FAILED( context->Map( shared.constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
			continue;
		memcpy( mapped.pData, &constants, sizeof( constants ) );
		context->Unmap( shared.constants, 0 );
		context->RSSetScissorRects( 1, &rect );
		context->PSSetShaderResources( 0, 1 + FieldsPerPass, views );
		context->Draw( 3, 0 );
	}

	ID3D11ShaderResourceView *const none[ 1 + FieldsPerPass ] = {};
	context->PSSetShaderResources( 0, 1 + FieldsPerPass, none );
	context->OMSetRenderTargets( 1, &scene, sceneDepth );
	context->RSSetViewports( viewportCount, &savedViewport );
	backend->Forget_Bindings();
	static Bool logged = false;
	if( !logged )
	{
		logged = true;
		DEBUG_LOG( ( "REACHOVERLAY: Direct3D 11 pass drawn, %d fields\n", (Int)fields.size() ) );
	}

	// a texture no field drew this frame goes; a field shown again is uploaded again, which is cheap
	for( size_t i = shared.fields.size(); i-- > 0; )
	{
		if( !shared.fields[ i ].used )
		{
			drop( shared.fields[ i ].view );
			drop( shared.fields[ i ].texture );
			shared.fields.erase( shared.fields.begin() + i );
		}
	}
	return true;
}
