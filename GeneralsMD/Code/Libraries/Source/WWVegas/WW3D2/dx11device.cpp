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

#include "dx11device.h"

#include <dxgi1_5.h>
#include <stdlib.h>	// getenv
#include <string.h>

// The back buffer is the same format the D3D9 device asks for, so a picture taken off one can be
// compared against the other without a conversion standing between them.
static const DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

// D24S8 is what the D3D9 device settles on, and the stencil half is not spare: the shadow volumes
// are drawn with it.
// Typeless, because the same surface is written as a depth buffer and read as a texture: the post
// chain's occlusion pass needs the frame's own depth, and a view says how the bits are read.
static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_R24G8_TYPELESS;
static const DXGI_FORMAT DEPTH_WRITE_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
static const DXGI_FORMAT DEPTH_READ_FORMAT = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

// Feature level 11_0 is the floor.  Below it there are no compute shaders and no unordered access
// views, which is most of what phase 2 is being done for, and a machine that cannot reach it can
// still run the Direct3D 9 device.
static const D3D_FEATURE_LEVEL REQUESTED_FEATURE_LEVELS[] = { D3D_FEATURE_LEVEL_11_0 };

// Three, so a flip chain always has a buffer to draw into while one is on screen and one waits for
// its turn there; with two, a frame finished early stalls on the one the compositor still holds.
static const UINT SWAP_CHAIN_BUFFER_COUNT = 3;

// How many presented frames may be queued ahead of the screen on the waitable flip chain
// (Wait_For_Frame).  Two keeps the GPU fed across a long CPU frame and adds one frame of latency at
// most.  One was measured on 2026-11-10 (Twilight Flame, 8 AIs, 164 Hz): with the vsync off it
// halved the free frame rate, 643 to 323, because the CPU then builds a frame only once the GPU is
// done with the last; with the vsync on it took a refresh off Present-to-screen in a 1280x720
// window (16.7 to 10.4 ms) at the same 164 a second, but dropped a 1920x1080 one from 164 to 128.
static const UINT MAXIMUM_FRAME_LATENCY = 2;

static void release_interface(IUnknown ** object)
{
	if (*object != NULL) {
		(*object)->Release();
		*object = NULL;
	}
}

// DXGI 1.5, Windows 10 with a driver that agrees.  Without it a flip model window still waits for
// the compositor, so there is nothing to gain over the blt model and the old swap chain stays.
static bool tearing_supported()
{
	IDXGIFactory1 * factory = NULL;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) {
		return false;
	}
	BOOL allowed = FALSE;
	IDXGIFactory5 * factory5 = NULL;
	if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5),
			reinterpret_cast<void **>(&factory5)))) {
		if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed,
				sizeof(allowed)))) {
			allowed = FALSE;
		}
		factory5->Release();
	}
	factory->Release();
	return allowed != FALSE;
}

DX11DeviceClass::DX11DeviceClass()
	: Device(NULL)
	, Context(NULL)
	, SwapChain(NULL)
	, BackBufferView(NULL)
	, SceneView(NULL)
	, DepthStencilView(NULL)
	, DepthStencilTexture(NULL)
	, DepthTexture(NULL)
	, SceneDepthSurface(NULL)
	, SceneDepthView(NULL)
	, SceneDepthTexture(NULL)
	, FeatureLevel(D3D_FEATURE_LEVEL_11_0)
	, RequestedSamples(1)
	, SampleCount(1)
	, Width(0)
	, Height(0)
	, Software(false)
	, DebugLayerRequested(false)
	, DebugLayerPresent(false)
	, Tearing(false)
	, ChainFlags(0)
	, FrameLatencyWaitable(NULL)
	, FrameWaited(false)
	, LastPresentResult(S_OK)
{
	memset(PresentTimes, 0, sizeof(PresentTimes));
}

DX11DeviceClass::~DX11DeviceClass()
{
	Release();
}

bool DX11DeviceClass::Create(HWND window, unsigned width, unsigned height)
{
	return Create_Device(true, window, width, height);
}

bool DX11DeviceClass::Create_Offscreen()
{
	return Create_Device(false, NULL, 0, 0);
}

bool DX11DeviceClass::Create_Device(bool with_swap_chain, HWND window, unsigned width,
	unsigned height)
{
	DXGI_SWAP_CHAIN_DESC swap_chain;
	ZeroMemory(&swap_chain, sizeof(swap_chain));
	swap_chain.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
	swap_chain.BufferDesc.Width = width;
	swap_chain.BufferDesc.Height = height;
	swap_chain.BufferDesc.Format = BACK_BUFFER_FORMAT;
	swap_chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain.OutputWindow = window;
	swap_chain.SampleDesc.Count = 1;
	swap_chain.SampleDesc.Quality = 0;

	// The game asks for a windowed device and resizes the window itself for a borderless one, so
	// DXGI is never asked to take the display mode.  A real fullscreen mode is SetFullscreenState
	// afterwards, which is a decision for whoever owns the window and not for this.
	swap_chain.Windowed = TRUE;
	swap_chain.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	// The blt model above is composed by the desktop once a refresh whatever the sync interval, so
	// with vsync off the game still ran at exactly the monitor's rate, fullscreen included, since
	// fullscreen is this window over a mode change.  The flip model with tearing allowed is the one
	// a window may present past the refresh with.  It is tried first where DXGI allows tearing, and
	// a create that refuses it falls back to the blt model, which is what this always was.
	DXGI_SWAP_CHAIN_DESC flip_chain = swap_chain;
	flip_chain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	flip_chain.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	// The same flip chain with its queue held by a wait at the top of the frame (Wait_For_Frame)
	// rather than inside Present.  Windows 8.1 and later; a create that refuses it keeps the flip
	// chain without the wait, because the blt model would take the tearing with it.
	DXGI_SWAP_CHAIN_DESC waitable_chain = flip_chain;
	waitable_chain.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	const DXGI_SWAP_CHAIN_DESC * chain_choices[] = { &waitable_chain, &flip_chain, &swap_chain };
	const unsigned chain_choice_count = sizeof(chain_choices)/sizeof(chain_choices[0]);
	const unsigned chain_start = (with_swap_chain && tearing_supported()) ? 0u : chain_choice_count - 1;

	// Hardware first, WARP second.  WARP draws the same picture on a machine with no D3D11 driver,
	// which is what makes the device creatable on a build agent as well as on this one.
	D3D_DRIVER_TYPE driver_types[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
	unsigned driver_type_count = sizeof(driver_types)/sizeof(driver_types[0]);

	// ZH_DX11_DRIVER is for measuring, not playing.  "warp" takes WARP even where there is hardware.
	// "null" takes the NULL driver, which accepts every call and draws nothing, so a run times the
	// game's own submission work with no rasterizer in it: it has no swap chain, so Create_Views
	// gives it a texture for a back buffer and Present does nothing.  Unset, or anything else, the
	// order above stands.  The Direct3D 9 side's counterpart is ZH_D3D9_DEVTYPE (dx8wrapper.cpp).
	const char * requested = getenv("ZH_DX11_DRIVER");
	if (requested != NULL && _stricmp(requested, "warp") == 0) {
		driver_types[0] = D3D_DRIVER_TYPE_WARP;
		driver_type_count = 1;
	} else if (requested != NULL && _stricmp(requested, "null") == 0) {
		driver_types[0] = D3D_DRIVER_TYPE_NULL;
		driver_type_count = 1;
	}

	// With the layer asked for, try it first and fall back to the same driver type without it.  A
	// machine without the graphics tools feature refuses the create outright, and a refusal there
	// must not be a refusal to make a device at all.
	const UINT flag_choices[] = { D3D11_CREATE_DEVICE_DEBUG, 0 };
	const unsigned flag_start = DebugLayerRequested ? 0u : 1u;

	const unsigned flag_choice_count = sizeof(flag_choices)/sizeof(flag_choices[0]);

	// Every Direct3D 11 call this process makes is on the main thread: the texture loader thread
	// only fills Direct3D 9 surfaces the main thread locked, the job pool's skin, billboard and
	// particle work writes plain memory between a lock and an unlock taken outside it, the video
	// writer thread only writes the pipe, and the hardware cursor thread only touches the Direct3D 9
	// device.  So the device is spared its own locking.  The Direct3D 9 device is made without
	// D3DCREATE_MULTITHREADED for the same reason.
	const UINT thread_flags = D3D11_CREATE_DEVICE_SINGLETHREADED;

	for (unsigned index = 0; index < driver_type_count; ++index) {
		for (unsigned flag_index = flag_start; flag_index < flag_choice_count; ++flag_index) {
			for (unsigned chain_index = chain_start; chain_index < chain_choice_count; ++chain_index) {
				const UINT flags = flag_choices[flag_index] | thread_flags;
				// The NULL driver has no swap chain to make (ZH_DX11_DRIVER=null, above).
				const bool null_driver = (driver_types[index] == D3D_DRIVER_TYPE_NULL);
				const HRESULT result = Create_Device_Guarded(with_swap_chain && !null_driver,
					driver_types[index], flags, chain_choices[chain_index]);
				if (FAILED(result)) {
					continue;
				}

				Software = (driver_types[index] == D3D_DRIVER_TYPE_WARP);
				DebugLayerPresent = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
				Tearing = !null_driver && (chain_choices[chain_index] != &swap_chain);
				ChainFlags = chain_choices[chain_index]->Flags;
				Width = width;
				Height = height;
				if (!with_swap_chain) {
					return true;
				}
				// Asked for on the interface the flag belongs to; with the waitable flag the
				// device's own SetMaximumFrameLatency is not read.  A wrapper dxgi.dll may hand back
				// a chain without this interface, and then the frame simply does not wait.
				IDXGISwapChain2 * waitable = NULL;
				if (SwapChain != NULL && chain_choices[chain_index] == &waitable_chain
						&& SUCCEEDED(SwapChain->QueryInterface(__uuidof(IDXGISwapChain2),
							reinterpret_cast<void **>(&waitable)))) {
					waitable->SetMaximumFrameLatency(MAXIMUM_FRAME_LATENCY);
					FrameLatencyWaitable = waitable->GetFrameLatencyWaitableObject();
					waitable->Release();
				}
				// DXGI answers Alt+Enter on its own by taking the chain to exclusive fullscreen,
				// behind the game's back.  A flip chain presenting with tearing is refused there
				// and the picture freezes, and the game already has its own fullscreen.
				IDXGIFactory1 * factory = NULL;
				if (SwapChain != NULL && SUCCEEDED(SwapChain->GetParent(__uuidof(IDXGIFactory1),
						reinterpret_cast<void **>(&factory)))) {
					factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
					factory->Release();
				}
				if (Create_Views()) {
					return true;
				}
				Release();
				return false;
			}
		}
	}

	Release();
	return false;
}

// An overlay or a wrapper dxgi.dll that faults inside the create used to take the game down during
// initialisation.  A fault here is a refusal instead, and the Direct3D 9 device presents.
HRESULT DX11DeviceClass::Create_Device_Guarded(bool with_swap_chain, D3D_DRIVER_TYPE driver_type,
	UINT flags, const DXGI_SWAP_CHAIN_DESC * swap_chain)
{
	const UINT feature_level_count = sizeof(REQUESTED_FEATURE_LEVELS)/sizeof(D3D_FEATURE_LEVEL);
	__try {
		if (with_swap_chain) {
			return D3D11CreateDeviceAndSwapChain(NULL, driver_type, NULL, flags,
				REQUESTED_FEATURE_LEVELS, feature_level_count, D3D11_SDK_VERSION, swap_chain,
				&SwapChain, &Device, &FeatureLevel, &Context);
		}
		return D3D11CreateDevice(NULL, driver_type, NULL, flags, REQUESTED_FEATURE_LEVELS,
			feature_level_count, D3D11_SDK_VERSION, &Device, &FeatureLevel, &Context);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		// Whatever the create wrote before it faulted is not an object that is safe to Release.
		SwapChain = NULL;
		Device = NULL;
		Context = NULL;
		return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
	}
}

bool DX11DeviceClass::Create_Views()
{
	ID3D11Texture2D * back_buffer = NULL;
	if (SwapChain == NULL) {
		// ZH_DX11_DRIVER=null: no swap chain, so the back buffer is a texture of the size and format
		// a swap chain's would have.  The view below holds the only reference that outlives this.
		D3D11_TEXTURE2D_DESC buffer;
		ZeroMemory(&buffer, sizeof(buffer));
		buffer.Width = Width;
		buffer.Height = Height;
		buffer.MipLevels = 1;
		buffer.ArraySize = 1;
		buffer.Format = BACK_BUFFER_FORMAT;
		buffer.SampleDesc.Count = 1;
		buffer.Usage = D3D11_USAGE_DEFAULT;
		buffer.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(Device->CreateTexture2D(&buffer, NULL, &back_buffer))) {
			return false;
		}
	} else if (FAILED(SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void **>(&back_buffer)))) {
		return false;
	}

	const HRESULT view_result = Device->CreateRenderTargetView(back_buffer, NULL, &BackBufferView);
	back_buffer->Release();
	if (FAILED(view_result)) {
		return false;
	}

	D3D11_TEXTURE2D_DESC depth;
	ZeroMemory(&depth, sizeof(depth));
	depth.Width = Width;
	depth.Height = Height;
	depth.MipLevels = 1;
	depth.ArraySize = 1;
	depth.Format = DEPTH_STENCIL_FORMAT;
	depth.SampleDesc.Count = 1;
	depth.Usage = D3D11_USAGE_DEFAULT;
	depth.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(Device->CreateTexture2D(&depth, NULL, &DepthStencilTexture))) {
		return false;
	}
	D3D11_DEPTH_STENCIL_VIEW_DESC depth_view;
	ZeroMemory(&depth_view, sizeof(depth_view));
	depth_view.Format = DEPTH_WRITE_FORMAT;
	depth_view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	if (FAILED(Device->CreateDepthStencilView(DepthStencilTexture, &depth_view, &DepthStencilView))) {
		return false;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC depth_read;
	ZeroMemory(&depth_read, sizeof(depth_read));
	depth_read.Format = DEPTH_READ_FORMAT;
	depth_read.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	depth_read.Texture2D.MipLevels = 1;
	if (FAILED(Device->CreateShaderResourceView(DepthStencilTexture, &depth_read, &DepthTexture))) {
		return false;
	}

	// The scene's own depth at the scene's sample count, beside the single-sample one above, which
	// with multisampling holds the scene's depth resolved for every pass that reads it.  A device
	// that refuses it draws the scene at one sample.
	SampleCount = Granted_Sample_Count();
	if (SampleCount > 1) {
		depth.SampleDesc.Count = SampleCount;
		depth_view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
		depth_read.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
		if (FAILED(Device->CreateTexture2D(&depth, NULL, &SceneDepthSurface))
			|| FAILED(Device->CreateDepthStencilView(SceneDepthSurface, &depth_view, &SceneDepthView))
			|| FAILED(Device->CreateShaderResourceView(SceneDepthSurface, &depth_read, &SceneDepthTexture))) {
			release_interface(reinterpret_cast<IUnknown **>(&SceneDepthTexture));
			release_interface(reinterpret_cast<IUnknown **>(&SceneDepthView));
			release_interface(reinterpret_cast<IUnknown **>(&SceneDepthSurface));
			SampleCount = 1;
		}
	}

	Context->OMSetRenderTargets(1, &BackBufferView, DepthStencilView);

	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(Width);
	viewport.Height = static_cast<float>(Height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	Context->RSSetViewports(1, &viewport);
	return true;
}

void DX11DeviceClass::Release_Views()
{
	if (Context != NULL) {
		// ResizeBuffers refuses while anything still references a back buffer, and the context's
		// own output merger binding counts as a reference.
		ID3D11RenderTargetView * no_target = NULL;
		Context->OMSetRenderTargets(1, &no_target, NULL);
	}
	// Whoever lent it a scene view sized the texture behind it to the buffers that are about to go,
	// so the loan ends here and the next frame asks for a new one.
	SceneView = NULL;
	release_interface(reinterpret_cast<IUnknown **>(&SceneDepthTexture));
	release_interface(reinterpret_cast<IUnknown **>(&SceneDepthView));
	release_interface(reinterpret_cast<IUnknown **>(&SceneDepthSurface));
	release_interface(reinterpret_cast<IUnknown **>(&DepthTexture));
	release_interface(reinterpret_cast<IUnknown **>(&DepthStencilView));
	release_interface(reinterpret_cast<IUnknown **>(&DepthStencilTexture));
	release_interface(reinterpret_cast<IUnknown **>(&BackBufferView));
}

void DX11DeviceClass::Release()
{
	Release_Views();
	if (FrameLatencyWaitable != NULL) {
		CloseHandle(FrameLatencyWaitable);
		FrameLatencyWaitable = NULL;
	}
	release_interface(reinterpret_cast<IUnknown **>(&SwapChain));
	release_interface(reinterpret_cast<IUnknown **>(&Context));
	release_interface(reinterpret_cast<IUnknown **>(&Device));
	Width = 0;
	Height = 0;
	Software = false;
	Tearing = false;
	ChainFlags = 0;
	SampleCount = 1;
	FrameWaited = false;
	LastPresentResult = S_OK;
	memset(PresentTimes, 0, sizeof(PresentTimes));
}

void DX11DeviceClass::Drop_Multisampling()
{
	RequestedSamples = 1;
	SampleCount = 1;
	release_interface(reinterpret_cast<IUnknown **>(&SceneDepthTexture));
	release_interface(reinterpret_cast<IUnknown **>(&SceneDepthView));
	release_interface(reinterpret_cast<IUnknown **>(&SceneDepthSurface));
}

// The most samples at or under the request that the scene's two colour formats (the post chain's
// eight bit and half float targets) and its depth all take.  Every feature level 11 device takes 4
// and 8 for those, so 16 is the one that degrades in practice.
unsigned DX11DeviceClass::Granted_Sample_Count() const
{
	const DXGI_FORMAT formats[] = { BACK_BUFFER_FORMAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DEPTH_WRITE_FORMAT };
	for (unsigned samples = 16; samples > 1; samples /= 2) {
		bool taken = samples <= RequestedSamples;
		for (unsigned index = 0; taken && index < sizeof(formats) / sizeof(formats[0]); ++index) {
			UINT levels = 0;
			taken = SUCCEEDED(Device->CheckMultisampleQualityLevels(formats[index], samples, &levels))
				&& levels > 0;
		}
		if (taken) {
			return samples;
		}
	}
	return 1;
}

bool DX11DeviceClass::Resize(unsigned width, unsigned height)
{
	Release_Views();
	// With no swap chain (ZH_DX11_DRIVER=null) the views alone are the buffers, made again below.
	// ResizeBuffers has to be handed the flags the chain was made with, or it refuses: the waitable
	// flag in particular can be neither added nor taken away here.
	if (SwapChain != NULL && FAILED(SwapChain->ResizeBuffers(SWAP_CHAIN_BUFFER_COUNT, width, height,
			BACK_BUFFER_FORMAT, ChainFlags))) {
		return false;
	}
	Width = width;
	Height = height;
	return Create_Views();
}

void DX11DeviceClass::Clear(float red, float green, float blue, float alpha, bool clear_depth)
{
	const float colour[4] = { red, green, blue, alpha };
	Context->ClearRenderTargetView(BackBufferView, colour);
	if (clear_depth) {
		Context->ClearDepthStencilView(DepthStencilView,
			D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
}

bool DX11DeviceClass::Present(unsigned present_interval)
{
	// ZH_DX11_DRIVER=null has a back buffer and nowhere to show it; an offscreen device has neither.
	if (SwapChain == NULL) {
		return BackBufferView != NULL;
	}
	FrameWaited = false;
	LARGE_INTEGER presented;
	QueryPerformanceCounter(&presented);
	if (!Tearing) {
		LastPresentResult = SwapChain->Present(present_interval, 0);
		Note_Present(presented.QuadPart);
		return SUCCEEDED(LastPresentResult);
	}

	// A flip model Present unbinds the back buffer from the output merger.  The backend keeps its
	// own record of what is bound and sends only the difference, so whatever was bound goes back
	// on, and the next frame's first draw lands where the backend thinks it does.
	ID3D11RenderTargetView * targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { NULL };
	ID3D11DepthStencilView * depth = NULL;
	Context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth);

	// Tearing is only allowed with a sync interval of 0; vsync on is an ordinary flip.
	const HRESULT result = SwapChain->Present(present_interval,
		present_interval == 0 ? DXGI_PRESENT_ALLOW_TEARING : 0);

	Context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depth);
	for (unsigned index = 0; index < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++index) {
		if (targets[index] != NULL) {
			targets[index]->Release();
		}
	}
	if (depth != NULL) {
		depth->Release();
	}
	LastPresentResult = result;
	Note_Present(presented.QuadPart);
	return SUCCEEDED(result);
}

// When each present was asked for, by DXGI's own count of it, for Queue_Statistics.
void DX11DeviceClass::Note_Present(LONGLONG presented)
{
	UINT count = 0;
	if (LastPresentResult == S_OK && SUCCEEDED(SwapChain->GetLastPresentCount(&count))) {
		PresentTimes[count % PRESENT_TIME_SLOTS] = presented;
	}
}

bool DX11DeviceClass::Wait_For_Frame(unsigned timeout_milliseconds)
{
	// Only a present that returned S_OK queued a frame.  An occluded one (a minimised or hidden
	// window) or a failed one (a removed device, an invalid call) queued nothing, so nothing will
	// leave the queue to signal the wait, and every frame would sit out the whole limit.  The first
	// frame finds the S_OK the device starts with and waits, which keeps waits and presents paired.
	// Once a present: the top of the pass waits before the input is read, and the scene's own
	// Begin_Scene asks again and must not take a second count for the same frame.
	if (FrameLatencyWaitable == NULL || LastPresentResult != S_OK || FrameWaited) {
		return true;
	}
	FrameWaited = true;
	return WaitForSingleObject(FrameLatencyWaitable, timeout_milliseconds) == WAIT_OBJECT_0;
}

bool DX11DeviceClass::Queue_Statistics(unsigned & queued, double & present_to_screen_ms)
{
	DXGI_FRAME_STATISTICS stats;
	UINT last = 0;
	// After a present that queued nothing the slot it would have stamped holds an old time.
	if (SwapChain == NULL || LastPresentResult != S_OK || FAILED(SwapChain->GetFrameStatistics(&stats))
			|| FAILED(SwapChain->GetLastPresentCount(&last))
			|| stats.PresentCount == 0 || last < stats.PresentCount
			|| last - stats.PresentCount >= PRESENT_TIME_SLOTS) {
		return false;
	}
	const LONGLONG presented = PresentTimes[stats.PresentCount % PRESENT_TIME_SLOTS];
	if (presented == 0 || stats.SyncQPCTime.QuadPart < presented) {
		return false;
	}
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	queued = last - stats.PresentCount;
	present_to_screen_ms = (double)(stats.SyncQPCTime.QuadPart - presented) * 1000.0
		/ (double)frequency.QuadPart;
	return true;
}

HRESULT DX11DeviceClass::Device_Removed_Reason() const
{
	return Device->GetDeviceRemovedReason();
}
