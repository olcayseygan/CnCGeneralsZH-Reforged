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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

/*
** The Direct3D 11 device, its swap chain and the two views that hang off it.
**
** This is the bottom of the Direct3D 11 backend and it knows nothing about the game: a
** device, a back buffer, a depth buffer, a clear and a present.  Everything above it - the state
** translation, the combiner programs -ffshader generates, the two .vso rewrites - is built on this
** and can be written against a device that already exists rather than one being designed at the
** same time.
**
** It creates on hardware and falls back to WARP, and it can be created with no window at all,
** which is what lets a test run it: a device with no swap chain has no back buffer and cannot
** present, and Create_Offscreen says so by refusing both.  ZH_DX11_DRIVER (warp or null) forces the
** driver for measuring; the NULL driver's back buffer is a texture and its present does nothing.
**
** Nothing here is bound to D3D9's rules.  A D3D11 device is not lost by an alt-tab, so there is no
** TestCooperativeLevel and no Reset_Device; a resize is DXGI's ResizeBuffers and the views are
** rebuilt around it.  A device is removed rather than lost, which is a driver reset or a removed
** adapter, and Device_Removed_Reason reports it.
*/

#ifndef DX11DEVICE_H
#define DX11DEVICE_H

#include <d3d11.h>

class DX11DeviceClass
{
public:
	DX11DeviceClass();
	~DX11DeviceClass();

	// Ask for the validation layer on the next Create.  It reports every misuse D3D11 otherwise
	// answers by drawing nothing - a vertex buffer bound at the wrong stride, an input layout that
	// does not match the shader's signature, a constant buffer too small for what the shader
	// declares - and there is no other way to be told about any of them.  It needs the graphics
	// tools feature installed; without it the create is refused and this falls back to a device
	// without it, so asking is always safe.
	void Request_Debug_Layer() { DebugLayerRequested = true; }
	bool Has_Debug_Layer() const { return DebugLayerPresent; }

	// Multisampling for the scene, asked for before Create: 0 or 1 is none, otherwise 2, 4, 8 or 16.
	// The swap chain keeps one sample, because a flip chain refuses more; the scene is drawn into
	// the post chain's multisampled target over a multisampled depth buffer, and resolved on its way
	// to the screen.  Create_Views grants the most the device offers at or under the request for the
	// scene's colour formats and its depth together, so an unsupported count degrades rather than
	// failing the device.
	void Request_Sample_Count(unsigned samples) { RequestedSamples = samples; }
	// What Create_Views granted: one when the scene is not multisampled.
	unsigned Get_Sample_Count() const { return SampleCount; }
	// One sample for the rest of the run, resizes included: the backend could not make the pass
	// that resolves the multisampled depth.
	void Drop_Multisampling();

	// A device with a window: swap chain, back buffer, depth buffer, the lot.  Width and height
	// are the back buffer's, not the window's, so a borderless window and a fullscreen mode ask
	// for the same thing.
	bool Create(HWND window, unsigned width, unsigned height);

	// A device with no window.  It draws into whatever render target is given to it and cannot
	// present.  Tests use it, and so does anything that wants a device before there is a window.
	bool Create_Offscreen();

	void Release();

	bool Is_Created() const { return Device != NULL; }
	bool Has_Swap_Chain() const { return SwapChain != NULL; }

	// True when the adapter was created by WARP rather than by a driver.  The picture is the same
	// and the speed is not, so anything measuring frame time has to know which one it got.
	bool Is_Software() const { return Software; }

	ID3D11Device * Get_Device() const { return Device; }
	ID3D11DeviceContext * Get_Context() const { return Context; }
	ID3D11RenderTargetView * Get_Back_Buffer_View() const { return BackBufferView; }
	// The single-sample depth buffer: the swap chain's, a same-size render target's, and with
	// multisampling the scene's depth resolved (DX11BackendClass::Resolve_Scene_Depth).
	ID3D11DepthStencilView * Get_Depth_Stencil_View() const { return DepthStencilView; }
	// The same surface read as a texture, for the pass that wants the frame's own depth.
	ID3D11ShaderResourceView * Get_Depth_Texture() const { return DepthTexture; }
	// The multisampled depth the scene is tested against while it goes into the post chain's
	// multisampled target, and the same surface as a texture for the resolve.  Null at one sample.
	ID3D11ShaderResourceView * Get_Multisampled_Depth_Texture() const { return SceneDepthTexture; }
	// True while the scene view is the multisampled one.
	bool Scene_Is_Multisampled() const { return SceneView != NULL && SceneDepthView != NULL; }
	// The depth that goes with Get_Scene_View.
	ID3D11DepthStencilView * Get_Scene_Depth_View() const
	{
		return Scene_Is_Multisampled() ? SceneDepthView : DepthStencilView;
	}

	// Where the scene goes, which is not always the swap chain.  The post-process chain hands its
	// own texture's view over here at the top of every frame and the backend binds whatever it
	// finds, so nothing between the two knows the frame is being drawn somewhere else first.  NULL
	// puts it back on the swap chain, which is what an absent or a failed chain leaves behind.
	//
	// The view is borrowed, not owned: whoever handed it over releases it, and hands it over again
	// on the next frame if it is still there to hand over.
	void Set_Scene_View(ID3D11RenderTargetView * view) { SceneView = view; }
	ID3D11RenderTargetView * Get_Scene_View() const
	{
		return (SceneView != NULL) ? SceneView : BackBufferView;
	}

	unsigned Get_Width() const { return Width; }
	unsigned Get_Height() const { return Height; }

	// Rebuilds the swap chain's buffers and the two views around them.  DXGI refuses to resize
	// while a view still holds a back buffer, so both views go first.
	bool Resize(unsigned width, unsigned height);

	void Clear(float red, float green, float blue, float alpha, bool clear_depth);

	// present_interval is DXGI's sync interval: 0 for no wait, 1 for the next vertical blank.
	bool Present(unsigned present_interval);

	// True when the swap chain is the flip model with tearing allowed, which is the only kind the
	// compositor lets a window present faster than the monitor refreshes.
	bool Can_Tear() const { return Tearing; }

	// Wait, before a frame is built, until the waitable flip chain has room for it in its queue
	// (MAXIMUM_FRAME_LATENCY in the .cpp).  Once a presented frame and no more, or the count the wait
	// keeps drifts.  Returns at once on a chain without the wait (the blt model, an older Windows) and
	// after an occluded present; false only when the limit ran out first.
	bool Wait_For_Frame(unsigned timeout_milliseconds);
	bool Has_Frame_Wait() const { return FrameLatencyWaitable != NULL; }

	// DXGI's frame statistics: presents asked for and not yet on the screen, and how long the newest
	// one on it took from Present to the refresh that showed it.  False while DXGI has none to give.
	// Read with the vsync on: a tearing present is not tied to a refresh, and the statistics only
	// move once one, so there the count is presents since the last refresh rather than a queue.
	bool Queue_Statistics(unsigned & queued, double & present_to_screen_ms);

	// What the last Present returned, for the one log line a refused present is worth.
	HRESULT Last_Present_Result() const { return LastPresentResult; }

	// D3DERR_DEVICELOST's replacement.  S_OK while the device is usable; anything else is a driver
	// reset or a removed adapter and the device has to be built again from scratch.
	HRESULT Device_Removed_Reason() const;

private:
	DX11DeviceClass(const DX11DeviceClass &);
	DX11DeviceClass & operator=(const DX11DeviceClass &);

	bool Create_Device(bool with_swap_chain, HWND window, unsigned width, unsigned height);
	HRESULT Create_Device_Guarded(bool with_swap_chain, D3D_DRIVER_TYPE driver_type, UINT flags,
		const DXGI_SWAP_CHAIN_DESC * swap_chain);
	bool Create_Views();
	void Release_Views();
	unsigned Granted_Sample_Count() const;

	ID3D11Device * Device;
	ID3D11DeviceContext * Context;
	IDXGISwapChain * SwapChain;
	ID3D11RenderTargetView * BackBufferView;
	ID3D11RenderTargetView * SceneView;
	ID3D11DepthStencilView * DepthStencilView;
	ID3D11Texture2D * DepthStencilTexture;
	ID3D11ShaderResourceView * DepthTexture;
	ID3D11Texture2D * SceneDepthSurface;
	ID3D11DepthStencilView * SceneDepthView;
	ID3D11ShaderResourceView * SceneDepthTexture;

	D3D_FEATURE_LEVEL FeatureLevel;
	unsigned RequestedSamples;
	unsigned SampleCount;
	unsigned Width;
	unsigned Height;
	bool Software;
	bool DebugLayerRequested;
	bool DebugLayerPresent;
	bool Tearing;
	UINT ChainFlags;					// what the chain was made with, which ResizeBuffers must repeat
	HANDLE FrameLatencyWaitable;		// the waitable chain's handle, closed by Release
	bool FrameWaited;					// Wait_For_Frame took this frame's count since the last present
	HRESULT LastPresentResult;
	enum { PRESENT_TIME_SLOTS = 16 };
	LONGLONG PresentTimes[PRESENT_TIME_SLOTS];	// QPC at each present, by DXGI's present count

	void Note_Present(LONGLONG presented);
};

#endif // DX11DEVICE_H
