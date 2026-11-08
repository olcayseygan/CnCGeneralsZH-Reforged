/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

// Decision 7, phase A2: the device's resources and surfaces, Clear, and the implicit back buffer
// and depth surface.  The resources are PosixResources9's, in memory; the copies, conversions and fills
// are PosixImageOps'.  D3D9's rules are kept where the renderer can see them: a format the caps do not
// offer for a use is refused (PosixD3D9Caps.h), shared handles are not offered, the Get methods AddRef
// what they hand out and answer D3DERR_NOTFOUND for an empty slot, and setting render target 0 resets
// the viewport to it.  See PosixDevice9.h for who owns what.

#include <stdlib.h>
#include "PosixDevice9.h"
#include "PosixD3D9Caps.h"
#include "PosixImageOps.h"
#include "PosixResources9.h"
#include "SdlCreationLog.h"

#include <string.h>

namespace {

/** The creation methods' common checks: an out pointer, no shared handle (D3D9Ex's), and a format
	* the device offers for this use. */
template <class Object>
RenderResult checkCreation( Object **out, void **shared, RenderUInt32 usage, D3DRESOURCETYPE type, D3DFORMAT format )
{
	if (out == NULL)
		return D3DERR_INVALIDCALL;
	*out = NULL;
	if (shared != NULL)
		return D3DERR_INVALIDCALL;
	if (!posixFormatSupported( usage, type, format ))
		return D3DERR_INVALIDCALL;
	return D3D_OK;
}

/** Every surface here is one of PosixResources9's: this device makes them all. */
inline PosixImage &imageOf( IDirect3DSurface9 *surface )
{
	return static_cast<PosixSurface9 *>( surface )->image();
}

PosixFilter filterOf( D3DTEXTUREFILTERTYPE filter )
{
	return (filter == D3DTEXF_LINEAR) ? POSIX_FILTER_LINEAR : POSIX_FILTER_POINT;
}

/** UpdateTexture: every level `to` has, from the matching level of `from`; the same format and size. */
RenderResult copyLevel( PosixImage &to, const PosixImage &from )
{
	if (from.format() != to.format() || from.width() != to.width() || from.height() != to.height()
			|| from.depth() != to.depth())
		return D3DERR_INVALIDCALL;
	memcpy( to.bytes(), from.bytes(), from.size() );
	to.markWritten();
	return D3D_OK;
}

/** Whether a region is all of its image. */
bool coversAll( const PosixImage &image, const PosixRegion &region )
{
	return region.left == 0 && region.top == 0 && region.right == image.width() && region.bottom == image.height();
}

/* The A3d render-target seam (PosixDevice9.h): with a window, a render target's pixels are the GPU's, and
	 its CPU image is stale until downloaded.  Before the CPU reads one, or writes only part of one (the
	 version bump after the write uploads the whole image), it is brought up to date. */
RenderResult currentOnCpu( PosixDevice9 &device, IDirect3DSurface9 *surface )
{
	return device.Gpu_Owns( surface ) ? device.Gpu_Download( surface ) : D3D_OK;
}

bool hasStencil( D3DFORMAT format )
{
	return format == D3DFMT_D24S8 || format == D3DFMT_D24X4S4 || format == D3DFMT_D15S1;
}

/** One rectangle clipped to the viewport and to the surface; false if nothing of it is left. */
bool clipped( const D3DVIEWPORT9 &viewport, const PosixImage &image, const D3DRECT *rect, PosixRegion *region )
{
	long left = viewport.X, top = viewport.Y;
	long right = (long)viewport.X + viewport.Width, bottom = (long)viewport.Y + viewport.Height;
	if (rect != NULL)
	{
		if (rect->x1 > left) left = rect->x1;
		if (rect->y1 > top) top = rect->y1;
		if (rect->x2 < right) right = rect->x2;
		if (rect->y2 < bottom) bottom = rect->y2;
	}
	if (right > (long)image.width()) right = image.width();
	if (bottom > (long)image.height()) bottom = image.height();
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (left >= right || top >= bottom)
		return false;
	region->left = (unsigned int)left;
	region->top = (unsigned int)top;
	region->right = (unsigned int)right;
	region->bottom = (unsigned int)bottom;
	return true;
}

} // namespace

//-------------------------------------------------------------------------------------------------
// The implicit surfaces
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::Create_Implicit_Surfaces()
{
	// A windowed back buffer of UNKNOWN format takes the display's, which here is always X8R8G8B8.
	const D3DFORMAT format = (Parameters.BackBufferFormat == D3DFMT_UNKNOWN) ? D3DFMT_X8R8G8B8 : Parameters.BackBufferFormat;
	if (!posixFormatSupported( D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, format ))
		return D3DERR_INVALIDCALL;
	PosixSurface9 *back = PosixSurface9::createStandalone( Parameters.BackBufferWidth, Parameters.BackBufferHeight, format,
		D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT, Parameters.MultiSampleType, Parameters.MultiSampleQuality );
	if (back == NULL)
		return D3DERR_OUTOFVIDEOMEMORY;
	BackBuffer = back;							// the device's own reference, from creation
	Posix_Bind( RenderTargets[0], BackBuffer );

	if (Parameters.EnableAutoDepthStencil)
	{
		if (!posixFormatSupported( D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, Parameters.AutoDepthStencilFormat ))
			return D3DERR_INVALIDCALL;
		PosixSurface9 *depth = PosixSurface9::createStandalone( Parameters.BackBufferWidth, Parameters.BackBufferHeight,
			Parameters.AutoDepthStencilFormat, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT, Parameters.MultiSampleType,
			Parameters.MultiSampleQuality );
		if (depth == NULL)
			return D3DERR_OUTOFVIDEOMEMORY;
		DepthSurface = depth;
		Posix_Bind( DepthStencil, DepthSurface );
	}
	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
// Resources
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::CreateTexture( unsigned int width, unsigned int height, unsigned int levels, RenderUInt32 usage,
	D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **texture, void **shared )
{
	const RenderResult checked = checkCreation( texture, shared, usage, D3DRTYPE_TEXTURE, format );
	if (checked != D3D_OK)
		return checked;
	if (width == 0 || height == 0)
		return D3DERR_INVALIDCALL;
	if (Sdl_Creation_Log_Asked())
	{
		// PERF1: who makes the same small texture again every frame.  ZH_GPU_CREATION_TRACE=WxH names the
		// caller of the 100th, 600th, 1100th and 1600th texture of that size.
		static unsigned int traced_width = 0, traced_height = 0, made = 0;
		static const bool tracing = getenv( "ZH_GPU_CREATION_TRACE" ) != NULL
			&& sscanf( getenv( "ZH_GPU_CREATION_TRACE" ), "%ux%u", &traced_width, &traced_height ) == 2;
		if (tracing && width == traced_width && height == traced_height && ++made <= 1600 && made % 500 == 100)
		{
			char what[96];
			snprintf( what, sizeof(what), "CreateTexture %ux%u format %u pool %u, #%u", width, height, (unsigned)format,
				(unsigned)pool, made );
			Sdl_Creation_Log_Trace( what );
		}
	}
	*texture = PosixTexture9::create( width, height, levels, usage, format, pool );
	return (*texture != NULL) ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
}

RenderResult PosixDevice9::CreateVolumeTexture( unsigned int width, unsigned int height, unsigned int depth, unsigned int levels,
	RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **texture, void **shared )
{
	const RenderResult checked = checkCreation( texture, shared, usage, D3DRTYPE_VOLUMETEXTURE, format );
	if (checked != D3D_OK)
		return checked;
	if (width == 0 || height == 0 || depth == 0)
		return D3DERR_INVALIDCALL;
	*texture = PosixVolumeTexture9::create( width, height, depth, levels, usage, format, pool );
	return (*texture != NULL) ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
}

RenderResult PosixDevice9::CreateCubeTexture( unsigned int edge, unsigned int levels, RenderUInt32 usage, D3DFORMAT format,
	D3DPOOL pool, IDirect3DCubeTexture9 **texture, void **shared )
{
	const RenderResult checked = checkCreation( texture, shared, usage, D3DRTYPE_CUBETEXTURE, format );
	if (checked != D3D_OK)
		return checked;
	if (edge == 0)
		return D3DERR_INVALIDCALL;
	*texture = PosixCubeTexture9::create( edge, levels, usage, format, pool );
	return (*texture != NULL) ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
}

RenderResult PosixDevice9::CreateVertexBuffer( unsigned int length, RenderUInt32 usage, RenderUInt32 fvf, D3DPOOL pool,
	IDirect3DVertexBuffer9 **buffer, void **shared )
{
	if (buffer == NULL)
		return D3DERR_INVALIDCALL;
	*buffer = NULL;
	if (shared != NULL || length == 0)
		return D3DERR_INVALIDCALL;
	*buffer = PosixVertexBuffer9::create( length, usage, fvf, pool );
	return (*buffer != NULL) ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
}

RenderResult PosixDevice9::CreateIndexBuffer( unsigned int length, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool,
	IDirect3DIndexBuffer9 **buffer, void **shared )
{
	if (buffer == NULL)
		return D3DERR_INVALIDCALL;
	*buffer = NULL;
	if (shared != NULL || length == 0 || (format != D3DFMT_INDEX16 && format != D3DFMT_INDEX32))
		return D3DERR_INVALIDCALL;
	*buffer = PosixIndexBuffer9::create( length, usage, format, pool );
	return (*buffer != NULL) ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
}

RenderResult PosixDevice9::CreateRenderTarget( unsigned int width, unsigned int height, D3DFORMAT format,
	D3DMULTISAMPLE_TYPE multisample, RenderUInt32 quality, int, IDirect3DSurface9 **surface, void **shared )
{
	const RenderResult checked = checkCreation( surface, shared, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, format );
	if (checked != D3D_OK)
		return checked;
	if (width == 0 || height == 0)
		return D3DERR_INVALIDCALL;
	*surface = PosixSurface9::createStandalone( width, height, format, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT, multisample, quality );
	return (*surface != NULL) ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
}

RenderResult PosixDevice9::CreateDepthStencilSurface( unsigned int width, unsigned int height, D3DFORMAT format,
	D3DMULTISAMPLE_TYPE multisample, RenderUInt32 quality, int, IDirect3DSurface9 **surface, void **shared )
{
	const RenderResult checked = checkCreation( surface, shared, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, format );
	if (checked != D3D_OK)
		return checked;
	if (width == 0 || height == 0)
		return D3DERR_INVALIDCALL;
	*surface = PosixSurface9::createStandalone( width, height, format, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT, multisample, quality );
	return (*surface != NULL) ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
}

RenderResult PosixDevice9::CreateOffscreenPlainSurface( unsigned int width, unsigned int height, D3DFORMAT format, D3DPOOL pool,
	IDirect3DSurface9 **surface, void **shared )
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	*surface = NULL;
	// An offscreen plain surface holds any format the device can store: SYSTEMMEM and SCRATCH are memory.
	if (shared != NULL || width == 0 || height == 0 || !posixFormatLayout( format, NULL ))
		return D3DERR_INVALIDCALL;
	*surface = PosixSurface9::createStandalone( width, height, format, 0, pool, D3DMULTISAMPLE_NONE, 0 );
	return (*surface != NULL) ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
}

//-------------------------------------------------------------------------------------------------
// Copies
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::UpdateSurface( IDirect3DSurface9 *source, const RenderRect *source_rect, IDirect3DSurface9 *dest,
	const RenderPoint *dest_point )
{
	if (source == NULL || dest == NULL)
		return D3DERR_INVALIDCALL;
	PosixImage &from = imageOf( source ), &to = imageOf( dest );
	if (from.format() != to.format())
		return D3DERR_INVALIDCALL;		// D3D9's UpdateSurface neither converts nor scales
	PosixRegion sourceRegion;
	if (!posixRegionOf( from, source_rect, &sourceRegion ))
		return D3DERR_INVALIDCALL;
	if (dest_point != NULL && (dest_point->x < 0 || dest_point->y < 0))
		return D3DERR_INVALIDCALL;
	PosixRegion destRegion;
	destRegion.left = dest_point ? (unsigned int)dest_point->x : 0;
	destRegion.top = dest_point ? (unsigned int)dest_point->y : 0;
	destRegion.right = destRegion.left + (sourceRegion.right - sourceRegion.left);
	destRegion.bottom = destRegion.top + (sourceRegion.bottom - sourceRegion.top);
	if (destRegion.right > to.width() || destRegion.bottom > to.height())
		return D3DERR_INVALIDCALL;
	// D3D9 reads UpdateSurface's source from SYSTEMMEM, so only the destination can be the GPU's.
	if (!coversAll( to, destRegion ))
	{
		const RenderResult result = currentOnCpu( *this, dest );
		if (result != D3D_OK)
			return result;
	}
	return posixCopyImage( to, destRegion, from, sourceRegion, POSIX_FILTER_NONE );
}

RenderResult PosixDevice9::UpdateTexture( IDirect3DBaseTexture9 *source, IDirect3DBaseTexture9 *dest )
{
	if (source == NULL || dest == NULL || source->GetType() != dest->GetType())
		return D3DERR_INVALIDCALL;
	// D3D9: the destination may have fewer levels; they match the source's from its top.
	const unsigned int levels = dest->GetLevelCount();
	if (levels > source->GetLevelCount())
		return D3DERR_INVALIDCALL;
	for (unsigned int level = 0; level < levels; ++level)
	{
		RenderResult result = D3DERR_INVALIDCALL;
		switch (source->GetType())
		{
			case D3DRTYPE_TEXTURE:
				result = copyLevel( static_cast<PosixTexture9 *>( dest )->level( level ),
					static_cast<PosixTexture9 *>( source )->level( level ) );
				break;
			case D3DRTYPE_VOLUMETEXTURE:
				result = copyLevel( static_cast<PosixVolumeTexture9 *>( dest )->level( level ),
					static_cast<PosixVolumeTexture9 *>( source )->level( level ) );
				break;
			case D3DRTYPE_CUBETEXTURE:
				for (unsigned int face = 0; face < 6; ++face)
				{
					result = copyLevel( *static_cast<PosixCubeTexture9 *>( dest )->image( face, level ),
						*static_cast<PosixCubeTexture9 *>( source )->image( face, level ) );
					if (result != D3D_OK)
						break;
				}
				break;
			default:
				break;
		}
		if (result != D3D_OK)
			return result;
	}
	return D3D_OK;
}

RenderResult PosixDevice9::GetRenderTargetData( IDirect3DSurface9 *render_target, IDirect3DSurface9 *dest )
{
	if (render_target == NULL || dest == NULL)
		return D3DERR_INVALIDCALL;
	PosixImage &from = imageOf( render_target ), &to = imageOf( dest );
	if (from.format() != to.format() || from.width() != to.width() || from.height() != to.height())
		return D3DERR_INVALIDCALL;		// D3D9: the same size and format, a straight read-back
	const RenderResult result = currentOnCpu( *this, render_target );
	if (result != D3D_OK)
		return result;
	PosixRegion whole;
	posixRegionOf( from, NULL, &whole );
	return posixCopyImage( to, whole, from, whole, POSIX_FILTER_NONE );
}

// The front buffer is the frame last presented, which is the back buffer's contents; D3D9 hands it back
// as A8R8G8B8 in a surface the size of the display, which here is the back buffer's size.
RenderResult PosixDevice9::GetFrontBufferData( unsigned int swap_chain, IDirect3DSurface9 *dest )
{
	if (swap_chain != 0 || dest == NULL || BackBuffer == NULL)
		return D3DERR_INVALIDCALL;
	PosixImage &from = imageOf( BackBuffer ), &to = imageOf( dest );
	if (to.format() != D3DFMT_A8R8G8B8 || to.width() < from.width() || to.height() < from.height())
		return D3DERR_INVALIDCALL;
	// With a window the back buffer may be half drawn or cleared by now: the frame keeps what Present showed.
	if (Gpu_Owns( BackBuffer ))
		return Gpu_Download_Front( dest );
	PosixRegion whole;
	posixRegionOf( from, NULL, &whole );
	return posixCopyImage( to, whole, from, whole, POSIX_FILTER_NONE );
}

RenderResult PosixDevice9::StretchRect( IDirect3DSurface9 *source, const RenderRect *source_rect, IDirect3DSurface9 *dest,
	const RenderRect *dest_rect, D3DTEXTUREFILTERTYPE filter )
{
	if (source == NULL || dest == NULL)
		return D3DERR_INVALIDCALL;
	PosixImage &from = imageOf( source ), &to = imageOf( dest );
	PosixRegion sourceRegion, destRegion;
	if (!posixRegionOf( from, source_rect, &sourceRegion ) || !posixRegionOf( to, dest_rect, &destRegion ))
		return D3DERR_INVALIDCALL;
	const bool gpuSource = Gpu_Owns( source ), gpuDest = Gpu_Owns( dest );
	if (gpuSource && gpuDest)
		return Gpu_StretchRect( source, source_rect, dest, dest_rect, filter );
	RenderResult result = D3D_OK;
	if (gpuSource)
		result = Gpu_Download( source );
	if (result == D3D_OK && gpuDest && !coversAll( to, destRegion ))
		result = Gpu_Download( dest );
	if (result != D3D_OK)
		return result;
	return posixCopyImage( to, destRegion, from, sourceRegion, filterOf( filter ) );
}

//-------------------------------------------------------------------------------------------------
// Render targets and the depth surface
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::SetRenderTarget( RenderUInt32 index, IDirect3DSurface9 *surface )
{
	if (index >= RENDER_TARGET_COUNT || (index == 0 && surface == NULL))
		return D3DERR_INVALIDCALL;
	Posix_Bind( RenderTargets[index], surface );
	if (index == 0)
	{
		// D3D9: a new render target 0 resets the viewport to all of it.
		const PosixImage &image = imageOf( surface );
		Viewport.X = 0;
		Viewport.Y = 0;
		Viewport.Width = image.width();
		Viewport.Height = image.height();
		Viewport.MinZ = 0.0f;
		Viewport.MaxZ = 1.0f;
	}
	return D3D_OK;
}

RenderResult PosixDevice9::GetRenderTarget( RenderUInt32 index, IDirect3DSurface9 **surface )
{
	if (surface == NULL || index >= RENDER_TARGET_COUNT)
		return D3DERR_INVALIDCALL;
	*surface = NULL;
	if (RenderTargets[index] == NULL)
		return D3DERR_NOTFOUND;
	return Posix_Hand_Out( RenderTargets[index], surface );
}

RenderResult PosixDevice9::SetDepthStencilSurface( IDirect3DSurface9 *surface )
{
	Posix_Bind( DepthStencil, surface );
	return D3D_OK;
}

RenderResult PosixDevice9::GetDepthStencilSurface( IDirect3DSurface9 **surface )
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	*surface = NULL;
	if (DepthStencil == NULL)
		return D3DERR_NOTFOUND;
	return Posix_Hand_Out( DepthStencil, surface );
}

RenderResult PosixDevice9::GetBackBuffer( unsigned int swap_chain, unsigned int index, D3DBACKBUFFER_TYPE,
	IDirect3DSurface9 **surface )
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	*surface = NULL;
	if (swap_chain != 0 || index != 0 || BackBuffer == NULL)
		return D3DERR_INVALIDCALL;
	return Posix_Hand_Out( BackBuffer, surface );
}

//-------------------------------------------------------------------------------------------------
// Clear: the render target's colour, the depth surface's depth and stencil, within the viewport and the
// rectangles given, all for real, so what GetRenderTargetData reads back is what Windows would.
//-------------------------------------------------------------------------------------------------

RenderResult PosixDevice9::Clear( RenderUInt32 count, const D3DRECT *rects, RenderUInt32 flags, D3DCOLOR color, float z,
	RenderUInt32 stencil )
{
	if ((count != 0 && rects == NULL) || (count == 0 && rects != NULL))
		return D3DERR_INVALIDCALL;
	const bool depthAsked = (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) != 0;
	if (depthAsked && DepthStencil == NULL)
		return D3DERR_INVALIDCALL;		// D3D9: no depth surface to clear
	if ((flags & D3DCLEAR_STENCIL) && !hasStencil( imageOf( DepthStencil ).format() ))
		return D3DERR_INVALIDCALL;
	if ((flags & D3DCLEAR_TARGET) && RenderTargets[0] == NULL)
		return D3DERR_INVALIDCALL;
	// With a window, a render target's pixels are the GPU's (the A3d render-target seam): the colour is
	// cleared there when render target 0 is the GPU's, and the depth and stencil when the depth surface is,
	// each on its own.  Whatever is left is a CPU image, filled here.
	RenderUInt32 cpuFlags = flags;
	if (Get_Gpu() != NULL)
	{
		RenderUInt32 gpuFlags = 0;
		if ((flags & D3DCLEAR_TARGET) && Gpu_Owns( RenderTargets[0] ))
			gpuFlags |= D3DCLEAR_TARGET;
		if (depthAsked && Gpu_Owns( DepthStencil ))
			gpuFlags |= flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
		if (gpuFlags != 0)
		{
			const RenderResult result = Gpu_Clear( count, rects, gpuFlags, color, z, stencil );
			if (result != D3D_OK)
				return result;
		}
		cpuFlags = flags & ~gpuFlags;
	}
	const bool cpuDepth = (cpuFlags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) != 0;
	if ((cpuFlags & D3DCLEAR_TARGET) == 0 && !cpuDepth)
		return D3D_OK;

	const PosixColor colour = posixColorFromD3DColor( color );
	const unsigned int passes = (count == 0) ? 1 : count;
	for (unsigned int index = 0; index < passes; ++index)
	{
		const D3DRECT *rect = (count == 0) ? NULL : &rects[index];
		PosixRegion region;
		if (cpuFlags & D3DCLEAR_TARGET)
		{
			PosixImage &target = imageOf( RenderTargets[0] );
			if (clipped( Viewport, target, rect, &region ))
			{
				const RenderResult result = posixFillImage( target, region, colour );
				if (result != D3D_OK)
					return result;
			}
		}
		if (cpuDepth)
		{
			PosixImage &depth = imageOf( DepthStencil );
			if (clipped( Viewport, depth, rect, &region ))
			{
				const RenderResult result = posixFillDepth( depth, region, cpuFlags, z, stencil );
				if (result != D3D_OK)
					return result;
			}
		}
	}
	return D3D_OK;
}
