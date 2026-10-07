// Native Direct3D 9 checkpoint for the move off the translating DLL.  The D3D8 path went
// through our d3d8to9 d3d8.dll; this one calls Direct3DCreate9
// itself, with no translating DLL anywhere, and answers the questions that decide
// whether the phase-1 rename is a rename or a rewrite:
//
//   1. a device comes up with the game's own present parameters (dx8wrapper.cpp);
//   2. d3dx9_43.dll binds and its assembler still takes ps.1.1 / vs.1.1 source, which
//      is what W3DWater.cpp hands D3DXAssembleShader three times at runtime;
//   3. a vs.1.1 shader survives the D3D8-to-D3D9 round trip - disassemble, fix the two
//      rules D3D9 tightened, reassemble - which is how the shipped Trees.vso and
//      wave.vso will have to be loaded;
//   4. the D3D8 calls with no D3D9 counterpart have working replacements:
//      D3DTSS sampler states -> SetSamplerState, CopyRects -> UpdateSurface,
//      CreateImageSurface -> CreateOffscreenPlainSurface, D3DRS_ZBIAS -> D3DRS_DEPTHBIAS.
//
// "DX9 smoke OK" means pass.  Every failure prints what it was doing first.

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

#include "d3dx9runtime.h"
#include "d3dx9math.h"
#include "d3dx_golden.h"

#include <intrin.h>

static const int WINDOW_EDGE = 64;
static const int BACK_BUFFER_WIDTH = 640;
static const int BACK_BUFFER_HEIGHT = 480;
static const int RENDER_TARGET_EDGE = 256;
static const int SAMPLE_PIXEL_OFFSET = 10;
static const int BYTES_PER_PIXEL = 4;
static const BYTE CLEAR_RED = 64;
static const BYTE CLEAR_GREEN = 0;
static const BYTE CLEAR_BLUE = 128;
static const BYTE CHANNEL_TOLERANCE = 16;
static const float MAX_DEPTH = 1.0f;
static const float DEPTH_BIAS = 0.0001f;
static const DWORD MAX_ANISOTROPY = 4;

// The river shader out of W3DWater.cpp, unchanged.  It is the real thing the game
// assembles at runtime, so it is the right thing to ask the D3D9 assembler about.
static const char RIVER_PIXEL_SHADER[] =
	"ps.1.1\n"
	"tex t0\n"
	"tex t1\n"
	"tex t2\n"
	"tex t3\n"
	"mul r0.rgb, v0, t0\n"
	"mov r0.a, t0\n"
	"mul r1, t1, t2\n"
	"add r1.rgb, r1, t3\n"
	"mul r1.rgb, r1, v0.a\n"
	"+mul r0.a, r0, t3\n"
	"add r0.rgb, r0, r1\n";

// A vs.1.1 shader written the way the shipped .vso files were, and the same shader
// after the repairs D3D9 forces.  Three rules changed under D3D8, and this shader
// breaks all of them: the vertex declaration travelled beside the shader as a DWORD
// array instead of dcl statements, a register could be read before it was written,
// and an m*x* instruction could name its destination as its own first source.
static const char LEGACY_VERTEX_SHADER[] =
	"vs.1.1\n"
	"m4x4 r0, r0, c0\n"
	"mov oPos, r0\n"
	"mov oD0, v5\n";

static const char REPAIRED_VERTEX_SHADER[] =
	"vs.1.1\n"
	"dcl_position v0\n"		// built from the DWORD declaration the caller passes alongside
	"dcl_color v5\n"
	"mov r0, c0\n"			// every temp and output register written before it is read
	"mov r1, c0\n"
	"mov r1, r0\n"			// m4x4's first source moved off its destination
	"m4x4 r0, r1, c0\n"
	"mov oPos, r0\n"
	"mov oD0, v5\n";

static const D3DVERTEXELEMENT9 POSITION_AND_COLOUR_DECLARATION[] =
{
	{ 0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
	D3DDECL_END()
};

template <typename Interface> class Owned
{
public:
	Owned() : Pointer(NULL) {}
	~Owned() { if (Pointer != NULL) Pointer->Release(); }
	void Take(Interface * pointer) { Pointer = pointer; }
	Interface ** operator&() { return &Pointer; }
	Interface * operator->() const { return Pointer; }
	operator Interface *() const { return Pointer; }

private:
	Owned(const Owned &);
	Owned & operator=(const Owned &);

	Interface * Pointer;
};

class BoundD3DX9Runtime
{
public:
	BoundD3DX9Runtime() : Bound(Bind_D3DX9_Runtime()) {}
	~BoundD3DX9Runtime() { if (Bound) Unbind_D3DX9_Runtime(); }
	bool Is_Bound() const { return Bound; }

private:
	BoundD3DX9Runtime(const BoundD3DX9Runtime &);
	BoundD3DX9Runtime & operator=(const BoundD3DX9Runtime &);

	bool Bound;
};

class OwnedWindow
{
public:
	OwnedWindow(const char * className, DWORD style, int width, int height)
	{
		WNDCLASSA windowClass = {0};
		windowClass.lpfnWndProc = DefWindowProcA;
		windowClass.hInstance = GetModuleHandleA(NULL);
		windowClass.lpszClassName = className;
		RegisterClassA(&windowClass);
		Handle = CreateWindowA(className, "dx9smoke", style, 0, 0, width, height,
			NULL, NULL, windowClass.hInstance, NULL);
	}
	~OwnedWindow() { if (Handle != NULL) DestroyWindow(Handle); }
	HWND Get() const { return Handle; }

private:
	OwnedWindow(const OwnedWindow &);
	OwnedWindow & operator=(const OwnedWindow &);

	HWND Handle;
};

static bool channel_is_near(BYTE actual, BYTE expected)
{
	const int difference = (int)actual - (int)expected;
	return difference <= (int)CHANNEL_TOLERANCE && difference >= -(int)CHANNEL_TOLERANCE;
}

// "-msaa N": ask for a multisampled back buffer, then build the pairing
// DX8Wrapper::_Get_Non_MultiSampled_Depth_Buffer exists for.  A multisampled back buffer
// gets a multisampled auto depth/stencil, and Direct3D requires the depth buffer to match
// the render target, so the plain render-target textures the screen filters and the water
// reflection draw into need a non-multisampled depth buffer of their own.
static int requested_multisample_level(int argument_count, char ** arguments)
{
	for (int index = 1; index + 1 < argument_count; ++index) {
		if (strcmp(arguments[index], "-msaa") == 0) {
			return atoi(arguments[index + 1]);
		}
	}
	return 0;
}

static D3DMULTISAMPLE_TYPE highest_supported_multisample(IDirect3D9 * direct3D,
	const D3DPRESENT_PARAMETERS & present, int requested)
{
	const int MAX_SAMPLES = 16;
	if (requested < 2) {
		return D3DMULTISAMPLE_NONE;
	}
	for (int samples = requested > MAX_SAMPLES ? MAX_SAMPLES : requested; samples >= 2; --samples) {
		const D3DMULTISAMPLE_TYPE type = (D3DMULTISAMPLE_TYPE)samples;
		if (FAILED(direct3D->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
				present.BackBufferFormat, TRUE, type, NULL))) continue;
		if (FAILED(direct3D->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
				present.AutoDepthStencilFormat, TRUE, type, NULL))) continue;
		return type;
	}
	return D3DMULTISAMPLE_NONE;
}

int main(int argument_count, char ** arguments)
{
	const int requested_samples = requested_multisample_level(argument_count, arguments);

	Owned<IDirect3D9> direct3D;
	direct3D.Take(Direct3DCreate9(D3D_SDK_VERSION));
	if (direct3D == NULL) {
		printf("FAIL: Direct3DCreate9 returned NULL\n");
		return 1;
	}

	D3DADAPTER_IDENTIFIER9 adapter;
	if (SUCCEEDED(direct3D->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &adapter))) {
		printf("adapter: %s\n", adapter.Description);
	}

	OwnedWindow window("dx9smoke", WS_OVERLAPPEDWINDOW, WINDOW_EDGE, WINDOW_EDGE);
	if (window.Get() == NULL) {
		printf("FAIL: CreateWindow\n");
		return 1;
	}

	D3DDISPLAYMODE displayMode;
	if (FAILED(direct3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode))) {
		printf("FAIL: GetAdapterDisplayMode\n");
		return 1;
	}

	// dx8wrapper.cpp's own choices: windowed, discard, auto depth, mixed vertex processing,
	// plus the lockable back buffer that lets the clear be read back.
	D3DPRESENT_PARAMETERS present = {0};
	present.BackBufferWidth = BACK_BUFFER_WIDTH;
	present.BackBufferHeight = BACK_BUFFER_HEIGHT;
	present.Windowed = TRUE;
	present.SwapEffect = D3DSWAPEFFECT_DISCARD;
	present.BackBufferFormat = displayMode.Format;
	present.hDeviceWindow = window.Get();
	present.EnableAutoDepthStencil = TRUE;
	present.AutoDepthStencilFormat = D3DFMT_D16;
	present.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

	const D3DMULTISAMPLE_TYPE multisample =
		highest_supported_multisample(direct3D, present, requested_samples);
	if (requested_samples >= 2) {
		printf("msaa: %dx requested, %dx supported\n", requested_samples, (int)multisample);
		if (multisample == D3DMULTISAMPLE_NONE) {
			printf("FAIL: no multisample level up to %dx is supported\n", requested_samples);
			return 1;
		}
		present.MultiSampleType = multisample;
		present.SwapEffect = D3DSWAPEFFECT_DISCARD;	// the only swap effect D3D allows with multisampling
		present.Flags = 0;							// and a multisampled back buffer cannot be lockable
	}

	Owned<IDirect3DDevice9> device;
	HRESULT created = direct3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.Get(),
		D3DCREATE_MIXED_VERTEXPROCESSING, &present, &device);
	if (FAILED(created) || device == NULL) {
		printf("FAIL: CreateDevice hr=0x%08lx\n", (unsigned long)created);
		return 1;
	}
	printf("device: native Direct3D 9, no translating dll\n");

	if (FAILED(device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
			D3DCOLOR_XRGB(CLEAR_RED, CLEAR_GREEN, CLEAR_BLUE), MAX_DEPTH, 0))) {
		printf("FAIL: Clear\n");
		return 1;
	}

	// Read the cleared back buffer back, so a runtime that created a device and painted
	// nothing does not pass.  A multisampled back buffer cannot be locked, so that run
	// proves itself with the render-to-texture readback below instead.
	if (multisample == D3DMULTISAMPLE_NONE) {
		Owned<IDirect3DSurface9> backBuffer;
		if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) {
			printf("FAIL: GetBackBuffer\n");
			return 1;
		}
		D3DLOCKED_RECT locked;
		if (FAILED(backBuffer->LockRect(&locked, NULL, D3DLOCK_READONLY))) {
			printf("FAIL: the back buffer is not lockable\n");
			return 1;
		}
		const BYTE * pixel = (const BYTE *)locked.pBits
			+ SAMPLE_PIXEL_OFFSET * locked.Pitch + SAMPLE_PIXEL_OFFSET * BYTES_PER_PIXEL;
		const bool holdsClearColour = channel_is_near(pixel[0], CLEAR_BLUE)
			&& channel_is_near(pixel[1], CLEAR_GREEN)
			&& channel_is_near(pixel[2], CLEAR_RED);
		printf("back buffer pixel (b,g,r) = (%u,%u,%u)\n", pixel[0], pixel[1], pixel[2]);
		backBuffer->UnlockRect();
		if (!holdsClearColour) {
			printf("FAIL: the back buffer does not hold the clear colour (%u,%u,%u)\n",
				CLEAR_RED, CLEAR_GREEN, CLEAR_BLUE);
			return 1;
		}
	}

	// The D3D8 calls that have no D3D9 counterpart, each in its replacement form.
	if (FAILED(device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR))
		|| FAILED(device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR))
		|| FAILED(device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR))
		|| FAILED(device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP))
		|| FAILED(device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, MAX_ANISOTROPY))) {
		printf("FAIL: the D3DTSS filter and address states did not move to SetSamplerState\n");
		return 1;
	}
	const float depthBias = DEPTH_BIAS;
	uint32_t depthBiasBits;
	memcpy(&depthBiasBits, &depthBias, sizeof(depthBiasBits));
	if (FAILED(device->SetRenderState(D3DRS_DEPTHBIAS, depthBiasBits))) {
		printf("FAIL: D3DRS_ZBIAS has no working D3DRS_DEPTHBIAS replacement\n");
		return 1;
	}
	{
		Owned<IDirect3DTexture9> renderTexture;
		Owned<IDirect3DSurface9> renderSurface;
		Owned<IDirect3DSurface9> readback;
		if (FAILED(device->CreateTexture(RENDER_TARGET_EDGE, RENDER_TARGET_EDGE, 1,
				D3DUSAGE_RENDERTARGET, present.BackBufferFormat, D3DPOOL_DEFAULT, &renderTexture, NULL))
			|| FAILED(renderTexture->GetSurfaceLevel(0, &renderSurface))
			|| FAILED(device->CreateOffscreenPlainSurface(RENDER_TARGET_EDGE, RENDER_TARGET_EDGE,
				present.BackBufferFormat, D3DPOOL_SYSTEMMEM, &readback, NULL))) {
			printf("FAIL: CreateImageSurface has no working CreateOffscreenPlainSurface replacement\n");
			return 1;
		}
		// CopyRects is gone; GetRenderTargetData is the render-target-to-system-memory half of it.
		if (FAILED(device->GetRenderTargetData(renderSurface, readback))) {
			printf("FAIL: CopyRects has no working GetRenderTargetData replacement\n");
			return 1;
		}
	}
	printf("replacements: sampler states, depth bias, offscreen surface, render-target readback\n");

	// The pairing DX8Wrapper::_Get_Non_MultiSampled_Depth_Buffer exists for: a plain render
	// target texture with a depth buffer of its own, while the back buffer is multisampled.
	// Cleared green and read back, so a target that was bound but never drawn into fails.
	if (multisample != D3DMULTISAMPLE_NONE) {
		Owned<IDirect3DTexture9> renderTexture;
		Owned<IDirect3DSurface9> renderSurface;
		Owned<IDirect3DSurface9> renderDepth;
		Owned<IDirect3DSurface9> backBuffer;
		Owned<IDirect3DSurface9> autoDepth;
		Owned<IDirect3DSurface9> readback;
		if (FAILED(device->CreateTexture(RENDER_TARGET_EDGE, RENDER_TARGET_EDGE, 1,
				D3DUSAGE_RENDERTARGET, present.BackBufferFormat, D3DPOOL_DEFAULT, &renderTexture, NULL))
			|| FAILED(renderTexture->GetSurfaceLevel(0, &renderSurface))
			|| FAILED(device->CreateDepthStencilSurface(RENDER_TARGET_EDGE, RENDER_TARGET_EDGE,
				present.AutoDepthStencilFormat, D3DMULTISAMPLE_NONE, 0, TRUE, &renderDepth, NULL))
			|| FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))
			|| FAILED(device->GetDepthStencilSurface(&autoDepth))
			|| FAILED(device->CreateOffscreenPlainSurface(RENDER_TARGET_EDGE, RENDER_TARGET_EDGE,
				present.BackBufferFormat, D3DPOOL_SYSTEMMEM, &readback, NULL))) {
			printf("FAIL: could not build the render-to-texture surfaces\n");
			return 1;
		}

		if (FAILED(device->SetRenderTarget(0, renderSurface))
			|| FAILED(device->SetDepthStencilSurface(renderDepth))
			|| FAILED(device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
				D3DCOLOR_XRGB(0, 255, 0), MAX_DEPTH, 0))
			|| FAILED(device->GetRenderTargetData(renderSurface, readback))
			|| FAILED(device->SetRenderTarget(0, backBuffer))
			|| FAILED(device->SetDepthStencilSurface(autoDepth))) {
			printf("FAIL: render to a plain target with its own depth buffer\n");
			return 1;
		}

		D3DLOCKED_RECT locked;
		if (FAILED(readback->LockRect(&locked, NULL, D3DLOCK_READONLY))) {
			printf("FAIL: the render texture readback is not lockable\n");
			return 1;
		}
		const BYTE * pixel = (const BYTE *)locked.pBits
			+ SAMPLE_PIXEL_OFFSET * locked.Pitch + SAMPLE_PIXEL_OFFSET * BYTES_PER_PIXEL;
		const bool holdsGreen = channel_is_near(pixel[1], 255) && channel_is_near(pixel[0], 0)
			&& channel_is_near(pixel[2], 0);
		printf("render texture pixel (b,g,r) = (%u,%u,%u)\n", pixel[0], pixel[1], pixel[2]);
		readback->UnlockRect();
		if (!holdsGreen) {
			printf("FAIL: the render texture does not hold the clear colour (0,255,0)\n");
			return 1;
		}
	}

	// d3dx9runtime.cpp is what the renderer will bind, so the test binds the same thing
	// rather than a copy of it: a missing entry point fails here before it fails in a match.
	BoundD3DX9Runtime d3dx9;
	// Without the DLL the bind lands on the port's own D3DX and still succeeds, so ask what it bound.
	const bool boundDLL = d3dx9.Is_Bound() && strcmp(D3DX9_Runtime_Name(), "d3dx9_43.dll") == 0;
#if defined(_M_ARM64)
	// There is no d3dx9_43.dll for ARM64, so the rest of this - the DLL's assembler, its math against the
	// signatures, its CPU dispatch - has nothing to run against.  The device checks above have run.
	if (!boundDLL) {
		printf("SKIP: no d3dx9_43.dll exists for ARM64; the D3DX half of this check needs it\n");
		return 77;
	}
#endif
	if (!boundDLL) {
		printf("FAIL: d3dx9_43.dll did not bind; all seventeen entry points are phase 1 dependencies\n");
		return 1;
	}
	printf("d3dx9: all seventeen entry points bound out of d3dx9_43.dll\n");

	// The math entry points are declared by hand in d3dx9math.h, so their calling
	// convention is a guess until something calls one.  A stdcall/cdecl mismatch
	// unbalances the stack rather than returning a wrong number, so this runs the
	// bezier basis matrix out of BezierSegment.cpp through the transform that
	// DumbProjectileBehavior's shell arc depends on, and checks the answer.
	{
		const D3DXMATRIX bezierBasis(
			-1.0f,  3.0f, -3.0f, 1.0f,
			 3.0f, -6.0f,  3.0f, 0.0f,
			-3.0f,  3.0f,  0.0f, 0.0f,
			 1.0f,  0.0f,  0.0f, 0.0f);
		const D3DXVECTOR4 unitTime(1.0f, 1.0f, 1.0f, 1.0f);
		D3DXVECTOR4 transformed(0.0f, 0.0f, 0.0f, 0.0f);
		D3DXVec4TransformFromDLL(&transformed, &unitTime, &bezierBasis);

		// Each output component is the sum of one column, and the columns of this
		// matrix sum to 0, 0, 0 and 1.
		const bool transformIsRight = transformed.x == 0.0f && transformed.y == 0.0f
			&& transformed.z == 0.0f && transformed.w == 1.0f;
		const FLOAT dot = D3DXVec4Dot(&unitTime, &unitTime);
		printf("d3dx9 math: basis transform = (%g,%g,%g,%g), unit dot = %g\n",
			transformed.x, transformed.y, transformed.z, transformed.w, dot);
		if (!transformIsRight || dot != 4.0f) {
			printf("FAIL: the hand-declared D3DX math signatures do not match the DLL\n");
			return 1;
		}
	}

	// The Windows capture B17 left for E1.  d3dx9_43.dll picks its D3DXVec4Transform body by CPU:
	// GenuineIntel sums pairwise, every other vendor and the scalar fallback sum left to right, and
	// the two disagree on lane x of the Bezier basis.  That was read out of the DLL's dispatch code
	// on a Mac, and each body's arithmetic was run there under Rosetta.  What a Mac cannot see is
	// which body a real Windows process lands on.  This prints every golden row as this machine's
	// DLL computes it, and which body that was, so the reading can be checked against a real run.
	// It fails only if the result matches neither body; a vendor/body mismatch is reported, not
	// failed, because HKLM\Software\Microsoft\Direct3D DisableD3DXPSGP=1 legitimately causes one.
	// The game no longer calls the DLL for this: D3DXVec4Transform is d3dxportable.h on every
	// platform, and it matches the left-to-right body.  This block calls D3DXVec4TransformFromDLL
	// on purpose.  Pointed at the game's own function it would compare d3dxportable.h with itself
	// and could no longer see the dispatch.
	{
		char vendor[13];
#if defined(_M_ARM64)
		// ARM64 has no CPUID and no d3dx9_43.dll: Microsoft shipped the DLL for x86 and x64 only,
		// so an ARM64 build fails at the bind above and never gets here.  This only has to compile.
		strcpy(vendor, "(arm64)");
#else
		int registers[4];
		__cpuid(registers, 0);
		memcpy(vendor + 0, &registers[1], 4);
		memcpy(vendor + 4, &registers[3], 4);
		memcpy(vendor + 8, &registers[2], 4);
		vendor[12] = '\0';
#endif

		D3DXMATRIX basis;
		memcpy(&basis, D3DX_GOLDEN_BASIS, sizeof(basis));
		unsigned int leftToRightRows = 0;
		unsigned int intelRows = 0;
		for (unsigned int row = 0; row < D3DX_GOLDEN_ROW_COUNT; ++row) {
			D3DXVECTOR4 in;
			memcpy(&in, D3DX_GOLDEN_ROWS[row].in, sizeof(in));
			D3DXVECTOR4 out;
			D3DXVec4TransformFromDLL(&out, &in, &basis);
			unsigned int bits[4];
			memcpy(bits, &out, sizeof(bits));
			printf("d3dx9 capture: row %2u -> %08x %08x %08x %08x\n", row, bits[0], bits[1], bits[2],
				bits[3]);
			const unsigned int * want = D3DX_GOLDEN_ROWS[row].out;
			const bool yzw = bits[1] == want[1] && bits[2] == want[2] && bits[3] == want[3];
			if (yzw && bits[0] == want[0]) {
				++leftToRightRows;
			}
			if (yzw && bits[0] == D3DX_GOLDEN_ROWS[row].intel_x) {
				++intelRows;
			}
		}
		const bool leftToRight = leftToRightRows == D3DX_GOLDEN_ROW_COUNT;
		const bool intel = intelRows == D3DX_GOLDEN_ROW_COUNT;
		printf("d3dx9 capture: CPU vendor %s; D3DXVec4Transform ran the %s body\n", vendor,
			leftToRight ? "left-to-right (scalar or non-Intel)" : intel ? "GenuineIntel pairwise" : "UNRECOGNISED");
		if (!leftToRight && !intel) {
			printf("FAIL: D3DXVec4Transform matches neither body recorded in d3dx_golden.h\n");
			return 1;
		}
		if (intel != (strcmp(vendor, "GenuineIntel") == 0)) {
			printf("NOTE: the body is not the one d3dxportable.h predicts for this vendor - "
				"check DisableD3DXPSGP, and tell B17/E1\n");
		}
	}

	// The water pixel shader, assembled and created exactly as W3DWater.cpp does it.
	{
		Owned<ID3DXBuffer> compiled;
		Owned<ID3DXBuffer> errors;
		HRESULT assembled = D3DXAssembleShader(RIVER_PIXEL_SHADER, (UINT)strlen(RIVER_PIXEL_SHADER),
			NULL, NULL, 0, &compiled, &errors);
		if (FAILED(assembled) || compiled == NULL) {
			printf("FAIL: the river ps.1.1 shader did not assemble: %s\n",
				errors != NULL ? (const char *)errors->GetBufferPointer() : "no error text");
			return 1;
		}
		Owned<IDirect3DPixelShader9> pixelShader;
		HRESULT loaded = device->CreatePixelShader((const DWORD *)compiled->GetBufferPointer(), &pixelShader);
		if (FAILED(loaded)) {
			printf("FAIL: CreatePixelShader on the river shader hr=0x%08lx\n", (unsigned long)loaded);
			return 1;
		}
		printf("ps.1.1: the river shader assembles and loads on a native D3D9 device\n");
	}

	// The .vso route.  A shipped vertex shader arrives as D3D8 bytecode, so WW3D2 will have
	// to disassemble it, repair the assembly text and reassemble it, which is what
	// d3d8to9_device.cpp:1400 onward does.  Both halves are checked here: that the legacy
	// form really is refused, so none of that work is defensive, and that the repaired form
	// loads.  The disassembler is checked on the way through, since the repair has nothing
	// to read without it.
	{
		Owned<ID3DXBuffer> rejected;
		Owned<ID3DXBuffer> errors;
		const HRESULT legacyAssembled = D3DXAssembleShader(LEGACY_VERTEX_SHADER,
			(UINT)strlen(LEGACY_VERTEX_SHADER), NULL, NULL, 0, &rejected, &errors);
		if (SUCCEEDED(legacyAssembled)) {
			printf("FAIL: the D3D9 assembler took a vs.1.1 shader with no dcl statements, an\n"
			       "      uninitialised register read and an m4x4 aliasing its own destination.\n"
			       "      One of those rules is not enforced, so the repair list is wrong.\n");
			return 1;
		}
		printf("vs.1.1: the legacy form is refused, as it must be\n");

		Owned<ID3DXBuffer> compiled;
		Owned<ID3DXBuffer> repairErrors;
		const HRESULT repairedAssembled = D3DXAssembleShader(REPAIRED_VERTEX_SHADER,
			(UINT)strlen(REPAIRED_VERTEX_SHADER), NULL, NULL, 0, &compiled, &repairErrors);
		if (FAILED(repairedAssembled) || compiled == NULL) {
			printf("FAIL: the repaired vs.1.1 shader did not assemble: %s\n",
				repairErrors != NULL ? (const char *)repairErrors->GetBufferPointer() : "no error text");
			return 1;
		}

		Owned<IDirect3DVertexShader9> vertexShader;
		const HRESULT loaded = device->CreateVertexShader(
			(const DWORD *)compiled->GetBufferPointer(), &vertexShader);
		if (FAILED(loaded)) {
			printf("FAIL: CreateVertexShader on the repaired shader hr=0x%08lx\n", (unsigned long)loaded);
			return 1;
		}

		Owned<ID3DXBuffer> disassembly;
		if (FAILED(D3DXDisassembleShader((const DWORD *)compiled->GetBufferPointer(), FALSE, NULL, &disassembly))
			|| disassembly == NULL) {
			printf("FAIL: D3DXDisassembleShader, which the repair pass reads its input from\n");
			return 1;
		}

		Owned<IDirect3DVertexDeclaration9> declaration;
		if (FAILED(device->CreateVertexDeclaration(POSITION_AND_COLOUR_DECLARATION, &declaration))) {
			printf("FAIL: CreateVertexDeclaration, which replaces the D3D8 declaration array\n");
			return 1;
		}
		printf("vs.1.1: the repaired form assembles, loads and declares\n");
	}

	if (FAILED(device->Present(NULL, NULL, NULL, NULL))) {
		printf("FAIL: Present\n");
		return 1;
	}

	printf("DX9 smoke OK\n");
	return 0;
}
