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

#include "dx11backend.h"

#include "dx11layout.h"
#include "dx11resource.h"

#include <d3dcommon.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
#include <xmmintrin.h>
#endif
#include <cstddef>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

// d3dcompiler_47.dll ships with Windows.  Binding it by hand rather than linking it keeps a machine
// without it running the Direct3D 9 device instead of failing to start.
static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const VERTEX_PROFILE = "vs_4_0";
static const char * const PIXEL_PROFILE = "ps_4_0";

// The three stage counts are one count in three headers.  The vertex constant block is copied
// wholesale out of the backend's own texture transforms, the generated pixel shader declares one
// sampler per texture the backend binds, and a mismatch is a silent overrun rather than a build
// failure.  Both blocks are declared in the header, because the backend keeps a copy of each.
static_assert(MAXIMUM_VERTEX_STAGES == DX11_BACKEND_TEXTURE_STAGES,
	"ffvertex declares one texture matrix per stage and the backend uploads one per stage");
static_assert(MAXIMUM_COMBINER_STAGES == DX11_BACKEND_TEXTURE_STAGES,
	"ffshader declares one sampler per stage and the backend binds one per stage");

static D3DCompileFunction compiler_function()
{
	static D3DCompileFunction compiler = NULL;
	static bool attempted = false;
	if (!attempted) {
		attempted = true;
		HMODULE module = LoadLibraryA(COMPILER_MODULE);
		if (module != NULL) {
			compiler = reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
		}
	}
	return compiler;
}

// The cache file is this marker, then one record a program: the hash, the byte count, the bytes.
// The shipped file (Load_Shipped_Programs) is the same format.
static const char SHADER_CACHE_MAGIC[8] = { 'D', 'X', '1', '1', 'S', 'C', '1', '\0' };
static const unsigned SHADER_CACHE_LARGEST_PROGRAM = 1u << 20;
static const unsigned long SHADER_CACHE_SAVE_INTERVAL_MS = 10000;

// Every record of a cache file into programs, a record whole or not at all: a file cut short by a
// crash while it was written loses its last record and nothing else.  Returns how many were read.
static unsigned read_program_file(const char * path,
	std::map<unsigned long long, std::vector<unsigned char> > & programs)
{
	FILE * file = fopen(path, "rb");
	if (file == NULL) {
		return 0;
	}
	unsigned count = 0;
	char magic[sizeof(SHADER_CACHE_MAGIC)];
	if (fread(magic, sizeof(magic), 1, file) == 1
		&& memcmp(magic, SHADER_CACHE_MAGIC, sizeof(magic)) == 0) {
		unsigned long long hash = 0;
		unsigned int size = 0;
		while (fread(&hash, sizeof(hash), 1, file) == 1 && fread(&size, sizeof(size), 1, file) == 1
			&& size > 0 && size <= SHADER_CACHE_LARGEST_PROGRAM) {
			std::vector<unsigned char> bytecode(size);
			if (fread(&bytecode[0], size, 1, file) != 1) {
				break;
			}
			programs[hash].swap(bytecode);
			++count;
		}
	}
	fclose(file);
	return count;
}

// FNV-1a over the profile, the source and the compiler flags, which between them decide the
// bytecode.  The generated text is the key rather than the pipeline's state key, so a build that
// changes what a state generates compiles again instead of drawing with the previous build's
// program.  Default flags add nothing, so every key from before the flags existed still matches.
static unsigned long long program_hash(const char * profile, const std::string & source, unsigned flags)
{
	const unsigned long long FNV_OFFSET = 14695981039346656037ULL;
	const unsigned long long FNV_PRIME = 1099511628211ULL;
	const unsigned BITS_PER_BYTE = 8;

	unsigned long long hash = FNV_OFFSET;
	for (const char * cursor = profile; *cursor != '\0'; ++cursor) {
		hash = (hash ^ static_cast<unsigned char>(*cursor)) * FNV_PRIME;
	}
	for (size_t index = 0; index < source.size(); ++index) {
		hash = (hash ^ static_cast<unsigned char>(source[index])) * FNV_PRIME;
	}
	for (unsigned remaining = flags; remaining != 0; remaining >>= BITS_PER_BYTE) {
		hash = (hash ^ static_cast<unsigned char>(remaining)) * FNV_PRIME;
	}
	return hash;
}

static void set_identity(float matrix[16])
{
	memset(matrix, 0, sizeof(float) * 16);
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

// Row major throughout, the way D3D9 stores a matrix and the way the generated shaders declare one.
static void multiply(const float left[16], const float right[16], float result[16])
{
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned index = 0; index < 4; ++index) {
				sum += left[row * 4 + index] * right[index * 4 + column];
			}
			result[row * 4 + column] = sum;
		}
	}
}

// A light is handed to Direct3D 9 in world space and lit in camera space: the fixed-function
// pipeline carries its position and direction through the view matrix itself, once per light rather
// than once per vertex.  The generated shader lights in camera space too, so the same two products
// happen here.  Without them a world space light direction is dotted against a camera space normal
// and every lit model is shaded from the wrong angle - which reads as a lighting mood, not as a
// bug, and cost the whole of Alpine Assault's remaining 9.23%.
static void transform_point(const float source[4], const float matrix[16], float result[4])
{
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * matrix[column]
			+ source[1] * matrix[4 + column]
			+ source[2] * matrix[8 + column]
			+ matrix[12 + column];
	}
}

static void transform_direction(const float source[4], const float matrix[16], float result[4])
{
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * matrix[column]
			+ source[1] * matrix[4 + column]
			+ source[2] * matrix[8 + column];
	}
}

// A normal is transformed by the inverse transpose of the upper three by three, not by the matrix
// itself.  With a uniform scale the two differ only by a factor that normalize removes, but the
// engine scales models unevenly and a normal carried through the matrix itself comes out pointing
// off the surface, which is a lighting error that looks like a modelling error.
static void inverse_transpose(const float source[16], float result[16])
{
	const float a = source[0], b = source[1], c = source[2];
	const float d = source[4], e = source[5], f = source[6];
	const float g = source[8], h = source[9], i = source[10];

	const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

	set_identity(result);
	if (determinant == 0.0f) {
		return;
	}
	const float scale = 1.0f / determinant;

	// The inverse is the adjugate over the determinant; transposing it is the same as reading the
	// cofactors along the rows instead of down the columns, which is what this does.
	result[0] = (e * i - f * h) * scale;
	result[1] = (f * g - d * i) * scale;
	result[2] = (d * h - e * g) * scale;
	result[4] = (c * h - b * i) * scale;
	result[5] = (a * i - c * g) * scale;
	result[6] = (b * g - a * h) * scale;
	result[8] = (b * f - c * e) * scale;
	result[9] = (c * d - a * f) * scale;
	result[10] = (a * e - b * d) * scale;
}

DX11BackendClass::DX11BackendClass()
	: Device(NULL)
	, VertexFormat(0)
	, StreamBuffer(NULL)
	, StreamStride(0)
	, StreamOffset(0)
	, IndexBuffer(NULL)
	, IndexFormat(DXGI_FORMAT_R16_UINT)
	, MaterialPower(0.0f)
	, OffsetContext(NULL)
	, FrameConstantWrites(0)
	, FrameConstantRenames(0)
	, ConstantPath("")
	, VertexProgram(ENGINE_SHADER_NONE)
	, PixelProgram(ENGINE_SHADER_NONE)
	, UserBuffer(NULL)
	, UserBufferBytes(0)
	, PipelinesBuilt(0)
	, FrameBuildMilliseconds(0.0)
	, FrameBuildCount(0)
	, Worker(NULL)
	, CompileOffFrame(false)
	, ProgramsCompiledOffFrame(0)
	, LongestOffFrameMilliseconds(0.0)
	, DrawsWaitedForProgram(0)
	, ShaderCacheChanged(false)
	, ShippedPrograms(0)
	, LastShaderCacheSave(0)
	, TracedUserStrip(false)
	, MaskWhileTargeted(0)
	, TargetsBound(0)
	, TargetsRestored(0)
	, DrawsIntoTargets(0)
	, CurrentTarget(NULL)
	, CurrentDepth(NULL)
	, CurrentTargetResource(NULL)
	, TargetCopy(NULL)
	, TargetCopyView(NULL)
	, ShadowMapSurface(NULL)
	, ShadowMapDepth(NULL)
	, ShadowMapTexture(NULL)
	, ShadowMapSize(0)
	, ShadowMapBound(false)
	, ShadowMapSavedWidth(0)
	, ShadowMapSavedHeight(0)
	, ShadowMapSavedTarget(NULL)
	, ShadowMapSampler(NULL)
	, ShadowBias(0.0f)
	, ShadowStrength(0.0f)
	, ShadowRadius(1.0f)
	, ShadowNarrowestRadius(1.0f)
	, ShadowTexelsPerGap(0.0f)
	, ShadowUnitsPerDepth(0.0f)
	, ShadowSkyFill(0.0f)
	, ShadowReceiving(false)
	, SmokeGlow(false)
	, FireLook(false)
	, Emissive(false)
	, TeamGlow(false)
	, HemiAmbient(false)
	, AdditiveGain(1.0f)
	, GlowDraw(false)
	, SceneStandIn(NULL)
	, DrawsMade(0)
	, DrawsRefused(0)
	, RefusedNoBuffer(0)
	, RefusedNoStage(0)
	, RefusedNoLayout(0)
	, RefusedNoProgram(0)
	, RefusedNoObject(0)
	, RefusedForeignShader(0)
	, RefusedNoTexture(0)
	, ForeignPixelShader(false)
	, ForeignVertexShader(false)
{
	memset(Memos, 0, sizeof(Memos));
	LastMemo = 0;
	NextMemo = 0;
	memset(&VertexConstantRing, 0, sizeof(VertexConstantRing));
	memset(&PixelConstantRing, 0, sizeof(PixelConstantRing));
	memset(&EngineConstantRing, 0, sizeof(EngineConstantRing));
	Forget_Last_State_Objects();
	memset(&HeldVertexConstants, 0, sizeof(HeldVertexConstants));
	memset(&HeldPixelConstants, 0, sizeof(HeldPixelConstants));
	memset(RecentPixelDiscards, 0, sizeof(RecentPixelDiscards));	// no ring is at nought once made
	NextRecentPixel = 0;
	memset(HeldEngineConstants, 0, sizeof(HeldEngineConstants));
	VertexConstantsHeld = false;
	PixelConstantsHeld = false;
	EngineConstantsHeld = false;
	ConstantsChanged = true;
	VertexConstantsChanged = false;
	PixelConstantsChanged = false;
	PixelBlendChanged = false;
	HeldPixelBlendKey = 0;
	WorldChanged = false;
	TexturesChanged = true;
	PipelineChanged = true;
	StateObjectsChanged = true;
	for (unsigned sampler = 0; sampler < DX11_BACKEND_TEXTURE_STAGES; ++sampler) {
		SamplerChanged[sampler] = true;
	}
	LastResolveHeld = false;
	memset(StageStates, 0, sizeof(StageStates));
	memset(Textures, 0, sizeof(Textures));
	memset(EngineConstants, 0, sizeof(EngineConstants));
	memset(MissingTexture, 0, sizeof(MissingTexture));
	memset(Lights, 0, sizeof(Lights));
	memset(MaterialAmbient, 0, sizeof(MaterialAmbient));
	memset(MaterialDiffuse, 0, sizeof(MaterialDiffuse));
	memset(MaterialSpecular, 0, sizeof(MaterialSpecular));
	memset(MaterialEmissive, 0, sizeof(MaterialEmissive));

	set_identity(World);
	set_identity(View);
	set_identity(Projection);
	set_identity(SceneView);
	set_identity(SceneProjection);
	SceneViewKnown = false;
	SceneViewThisFrame = false;
	SmokeMapSurface = NULL;
	SmokeMapTarget = NULL;
	SmokeMapTexture = NULL;
	SmokeMapSampler = NULL;
	SmokeSplatVertexShader = NULL;
	SmokeSplatPixelShader = NULL;
	SmokeSplatLayout = NULL;
	SmokeSplatBlend = NULL;
	SmokeSplatRasterizer = NULL;
	SmokeSplatInstances = NULL;
	SmokeSplatCapacity = 0;
	SmokeMapRefused = false;
	SmokeMapFilled = false;
	SmokeStrength = 0.0f;
	memset(ShadowCasterVertexShaders, 0, sizeof(ShadowCasterVertexShaders));
	memset(ShadowCasterLayouts, 0, sizeof(ShadowCasterLayouts));
	ShadowCasterAlphaTest = NULL;
	ShadowCasterInstances = NULL;
	ShadowCasterCapacity = 0;
	ShadowCasterBones = NULL;
	ShadowCasterBonesView = NULL;
	ShadowCasterBonesCapacity = 0;
	ShadowCasterRefused = false;
	ShadowSkinSource = NULL;
	ShadowSkinCapacity = 0;
	ShadowSkinUsed = 0;
	ShadowSkinGeneration = 1;
	memset(Headlights, 0, sizeof(Headlights));
	HeadlightCount = 0;
	memset(HeadlightGain, 0, sizeof(HeadlightGain));
	memset(BlastLights, 0, sizeof(BlastLights));
	BlastLightCount = 0;
	SoftParticles = false;
	SoftParticlesAllowed = true;
	SceneDepthCopy = NULL;
	SceneDepthCopyView = NULL;
	SceneDepthStale = true;
	SceneDepthRefused = false;
	FrameDepthCopies = 0;
	DepthResolveVertexShader = NULL;
	DepthResolvePixelShader = NULL;
	DepthResolveState = NULL;
	DepthResolveRasterizer = NULL;
	DepthResolveStale = true;
	DepthResolveRefused = false;
	DrawEffect = DRAW_EFFECT_NONE;
	memset(DrawEffectNumbers, 0, sizeof(DrawEffectNumbers));
	SceneColourCopy = NULL;
	SceneColourCopyView = NULL;
	SceneColourStale = true;
	SceneColourRefused = false;
	set_identity(WorldFromClip);
	for (unsigned stage = 0; stage < DX11_BACKEND_TEXTURE_STAGES; ++stage) {
		set_identity(TextureTransforms[stage]);
	}
	ViewportWidth = 1;
	ViewportHeight = 1;
	ViewportX = 0;
	ViewportY = 0;
	Water = Create_Water();

	// The device's own defaults, so a stage nobody has written to is a stage that is off.
	for (unsigned stage = 0; stage < DX11_BACKEND_TEXTURE_STAGES; ++stage) {
		StageStates[stage][D3DTSS_COLOROP] = (stage == 0) ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		StageStates[stage][D3DTSS_ALPHAOP] = (stage == 0) ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		StageStates[stage][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		StageStates[stage][D3DTSS_COLORARG2] = D3DTA_CURRENT;
		StageStates[stage][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		StageStates[stage][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		StageStates[stage][D3DTSS_TEXCOORDINDEX] = stage;
	}
}

DX11BackendClass::~DX11BackendClass()
{
	Shutdown();
	Destroy_Water(Water);
}

bool DX11BackendClass::Initialise(DX11DeviceClass * device)
{
	Device = device;
	Forget_Bindings();

	// New buffers hold nothing the copies below know about.
	VertexConstantsHeld = false;
	PixelConstantsHeld = false;
	EngineConstantsHeld = false;

	// The rings want both halves of 11.1: binding at an offset, and a constant buffer mapped without
	// a discard.  The 11.0 runtime refuses the question, and an old driver answers no to either.
	D3D11_FEATURE_DATA_D3D11_OPTIONS options;
	memset(&options, 0, sizeof(options));
	ConstantPath = "one entry a kind, a discard every write: no 11.1 offset binding";
	if (SUCCEEDED(Device->Get_Device()->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options,
			sizeof(options)))
		&& options.ConstantBufferOffsetting && options.MapNoOverwriteOnDynamicConstantBuffer) {
		Device->Get_Context()->QueryInterface(__uuidof(ID3D11DeviceContext1),
			reinterpret_cast<void **>(&OffsetContext));
	}

	auto create_rings = [this]() {
		return Create_Constant_Ring(VertexConstantRing, sizeof(VertexConstantBlock))
			&& Create_Constant_Ring(PixelConstantRing, sizeof(PixelConstantBlock))
			&& Create_Constant_Ring(EngineConstantRing, sizeof(EngineConstants));
	};
	if (OffsetContext != NULL) {
		if (create_rings()) {
			ConstantPath = "a 4 MB ring a kind, written NO_OVERWRITE and bound at offsets";
			return true;
		}
		// A driver that says yes to offsets can still refuse a constant buffer this large, and
		// losing the Direct3D 11 frame over it would be the wrong trade: the one-entry rings below
		// are how every write went before the rings.
		Release_Constant_Ring(VertexConstantRing);
		Release_Constant_Ring(PixelConstantRing);
		Release_Constant_Ring(EngineConstantRing);
		OffsetContext->Release();
		OffsetContext = NULL;
		ConstantPath = "one entry a kind, a discard every write: the device refused a 4 MB ring";
	}
	return create_rings();
}

// A ring's size.  A heavy frame writes about three thousand blocks across the three rings, a
// kilobyte to two and a half each, so four megabytes a ring holds most of a frame and wraps about
// once a frame where every write used to discard.
static const unsigned CONSTANT_RING_BYTES = 4u << 20;
// An offset and a count are both in whole sixteen-constant steps of sixteen bytes a constant.
static const unsigned CONSTANT_BYTES = 16;
static const unsigned CONSTANT_ENTRY_ALIGNMENT = 16 * CONSTANT_BYTES;

bool DX11BackendClass::Create_Constant_Ring(ConstantRing & ring, unsigned block_bytes)
{
	ring.EntryBytes = (block_bytes + CONSTANT_ENTRY_ALIGNMENT - 1) & ~(CONSTANT_ENTRY_ALIGNMENT - 1);
	ring.Bytes = (OffsetContext != NULL)
		? (CONSTANT_RING_BYTES / ring.EntryBytes) * ring.EntryBytes : ring.EntryBytes;
	// Full, so the first write discards: the buffer has never been mapped.
	ring.Next = ring.Bytes;
	ring.Current = 0;
	++ring.Discards;

	D3D11_BUFFER_DESC description;
	memset(&description, 0, sizeof(description));
	description.ByteWidth = ring.Bytes;
	description.Usage = D3D11_USAGE_DYNAMIC;
	description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	return SUCCEEDED(Device->Get_Device()->CreateBuffer(&description, NULL, &ring.Buffer));
}

// NO_OVERWRITE is the promise that nothing a queued draw reads is written, which holds because a
// write only ever lands past every entry written since the last discard.  A failed Map leaves the
// ring where it was, so a retry neither skips a discard nor writes over an entry in flight.
bool DX11BackendClass::Write_Constants(ConstantRing & ring, const void * block, unsigned bytes)
{
	const bool wraps = ring.Next + ring.EntryBytes > ring.Bytes;
	const unsigned offset = wraps ? 0 : ring.Next;
	ID3D11DeviceContext * context = Device->Get_Context();
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(ring.Buffer, 0,
			wraps ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped))) {
		return false;
	}
	memcpy(static_cast<unsigned char *>(mapped.pData) + offset, block, bytes);
	context->Unmap(ring.Buffer, 0);
	ring.Current = offset;
	ring.Next = offset + ring.EntryBytes;
	++FrameConstantWrites;
	if (wraps) {
		++FrameConstantRenames;
		++ring.Discards;
	}
	return true;
}

void DX11BackendClass::Release_Constant_Ring(ConstantRing & ring)
{
	if (ring.Buffer != NULL) {
		ring.Buffer->Release();
	}
	const unsigned discards = ring.Discards;
	memset(&ring, 0, sizeof(ring));
	ring.Discards = discards + 1;
}

void DX11BackendClass::Release_Cached()
{
	// The memos hold copies of these pipelines and state objects, so they go first.
	for (unsigned entry = 0; entry < RESOLVE_MEMO_ENTRIES; ++entry) {
		Memos[entry].Valid = false;
	}
	LastMemo = 0;
	NextMemo = 0;
	LastResolveHeld = false;
	Resolutions.clear();
	Forget_Last_State_Objects();

	for (std::map<std::string, Pipeline>::iterator entry = Pipelines.begin();
			entry != Pipelines.end(); ++entry) {
		entry->second.VertexShader->Release();
		entry->second.PixelShader->Release();
		entry->second.Layout->Release();
	}
	Pipelines.clear();

	for (BlendStateMap::iterator entry = BlendStates.begin();
			entry != BlendStates.end(); ++entry) {
		entry->second->Release();
	}
	BlendStates.clear();

	for (DepthStencilStateMap::iterator entry
			= DepthStencilStates.begin(); entry != DepthStencilStates.end(); ++entry) {
		entry->second->Release();
	}
	DepthStencilStates.clear();

	for (RasterizerStateMap::iterator entry = RasterizerStates.begin();
			entry != RasterizerStates.end(); ++entry) {
		entry->second->Release();
	}
	RasterizerStates.clear();

	for (std::map<unsigned long long, ID3D11DepthStencilView *>::iterator entry
			= TargetDepths.begin(); entry != TargetDepths.end(); ++entry) {
		if (entry->second != NULL) {
			entry->second->Release();
		}
	}
	TargetDepths.clear();
	CurrentTarget = NULL;
	CurrentDepth = NULL;

	for (SamplerStateMap::iterator entry = SamplerStates.begin();
			entry != SamplerStates.end(); ++entry) {
		entry->second->Release();
	}
	SamplerStates.clear();
}

void DX11BackendClass::Shutdown()
{
	// Before the save, so what the worker finished goes into the file.
	Stop_Compile_Worker();
	if (ShaderCacheChanged) {
		Save_Shader_Cache();
		ShaderCacheChanged = false;
	}
	Release_Cached();
	Release_Water();

	Release_Constant_Ring(EngineConstantRing);
	Release_Constant_Ring(PixelConstantRing);
	Release_Constant_Ring(VertexConstantRing);
	if (OffsetContext != NULL) {
		OffsetContext->Release();
		OffsetContext = NULL;
	}
	if (UserBuffer != NULL) {
		UserBuffer->Release();
		UserBuffer = NULL;
		UserBufferBytes = 0;
	}
	if (TargetCopy != NULL) {
		TargetCopyView->Release();
		TargetCopy->Release();
		TargetCopyView = NULL;
		TargetCopy = NULL;
	}
	SceneStandIn = NULL;
	if (ShadowMapTexture != NULL) {
		ShadowMapTexture->Release();
		ShadowMapTexture = NULL;
	}
	if (ShadowMapDepth != NULL) {
		ShadowMapDepth->Release();
		ShadowMapDepth = NULL;
	}
	if (ShadowMapSurface != NULL) {
		ShadowMapSurface->Release();
		ShadowMapSurface = NULL;
	}
	if (ShadowMapSampler != NULL) {
		ShadowMapSampler->Release();
		ShadowMapSampler = NULL;
	}
	ShadowMapSize = 0;
	ShadowMapBound = false;
	Release_Smoke_Map();
	Release_Shadow_Casters();
	Release_Scene_Depth();
	Release_Depth_Resolve();
	Release_Scene_Colour();
	RenderStates.Set_Shadow_Caster_Pass(false);
	Note_Shadow_State_Changed();
	Device = NULL;
}

/** The sun's depth buffer.  One surface with two views of it, because a depth buffer that is also
		sampled cannot be made as a depth format: the surface is typeless and each view says how its
		bits are to be read.  Made at the size asked for and kept while that is what is asked for; the
		console's freecam asks for a larger one over the whole map, and the surface is made again at
		whichever size the frame wants.  The views are dropped with it; a draw that still has the old
		texture view bound holds its own reference until the unbind below. */
bool DX11BackendClass::Begin_Shadow_Map(unsigned size)
{
	if (Device == NULL || size == 0 || ShadowMapBound) {
		return false;
	}

	if (ShadowMapSurface != NULL && ShadowMapSize != size) {
		ShadowMapTexture->Release();
		ShadowMapTexture = NULL;
		ShadowMapDepth->Release();
		ShadowMapDepth = NULL;
		ShadowMapSurface->Release();
		ShadowMapSurface = NULL;
		ShadowMapSize = 0;
	}

	if (ShadowMapSurface == NULL) {
		D3D11_TEXTURE2D_DESC description;
		memset(&description, 0, sizeof(description));
		description.Width = size;
		description.Height = size;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R24G8_TYPELESS;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(Device->Get_Device()->CreateTexture2D(&description, NULL, &ShadowMapSurface))) {
			Note_Refusal("the device refused the sun's depth buffer");
			return false;
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC depth_description;
		memset(&depth_description, 0, sizeof(depth_description));
		depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depth_description.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(Device->Get_Device()->CreateDepthStencilView(ShadowMapSurface, &depth_description,
				&ShadowMapDepth))) {
			Note_Refusal("the device refused a depth view of the sun's depth buffer");
			ShadowMapSurface->Release();
			ShadowMapSurface = NULL;
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC texture_description;
		memset(&texture_description, 0, sizeof(texture_description));
		texture_description.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		texture_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		texture_description.Texture2D.MipLevels = 1;
		if (FAILED(Device->Get_Device()->CreateShaderResourceView(ShadowMapSurface,
				&texture_description, &ShadowMapTexture))) {
			Note_Refusal("the device refused a texture view of the sun's depth buffer");
			ShadowMapDepth->Release();
			ShadowMapDepth = NULL;
			ShadowMapSurface->Release();
			ShadowMapSurface = NULL;
			return false;
		}

		ShadowMapSize = size;
	}

	// The map cannot be read and written at once, and the frame before this one left it bound to
	// whichever sampler reads it.
	ID3D11ShaderResourceView * const none[DX11_BACKEND_TEXTURE_STAGES] = { NULL };
	Device->Get_Context()->PSSetShaderResources(0, DX11_BACKEND_TEXTURE_STAGES, none);

	Device->Get_Context()->OMSetRenderTargets(0, NULL, ShadowMapDepth);
	Device->Get_Context()->ClearDepthStencilView(ShadowMapDepth,
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	ShadowMapSavedWidth = ViewportWidth;
	ShadowMapSavedHeight = ViewportHeight;
	ShadowMapSavedTarget = CurrentTarget;
	ShadowMapBound = true;
	RenderStates.Set_Shadow_Caster_Pass(true);
	Note_Shadow_State_Changed();
	Set_Viewport(0, 0, ShadowMapSize, ShadowMapSize);
	Forget_Bindings();
	return true;
}

void DX11BackendClass::End_Shadow_Map()
{
	if (!ShadowMapBound) {
		return;
	}

	ShadowMapBound = false;
	RenderStates.Set_Shadow_Caster_Pass(false);
	Note_Shadow_State_Changed();
	// What the sun was looking through, kept for the draws that will read the map.
	multiply(View, Projection, SunViewProjection);
	ShadowFromClipValid = false;
	// Back to whatever the frame was drawing into, which is a texture the post chain shows and not
	// the back buffer.
	Set_Render_Target(ShadowMapSavedTarget);
	ShadowMapSavedTarget = NULL;
	if (ShadowMapSavedWidth != 0 && ShadowMapSavedHeight != 0) {
		Set_Viewport(0, 0, ShadowMapSavedWidth, ShadowMapSavedHeight);
	}
	Forget_Bindings();
}

void DX11BackendClass::Set_Shadow_Parameters(float bias, float strength,
	float widest_radius_in_texels, float narrowest_radius_in_texels, float texels_per_unit_of_gap,
	float units_per_unit_of_depth, float sky_fill)
{
	if (ShadowMapTexture == NULL) {
		Clear_Shadow_Parameters();
		return;
	}
	ShadowBias = bias;
	ShadowStrength = strength;
	ShadowRadius = widest_radius_in_texels;
	ShadowNarrowestRadius = narrowest_radius_in_texels;
	ShadowTexelsPerGap = texels_per_unit_of_gap;
	ShadowUnitsPerDepth = units_per_unit_of_depth;
	ShadowSkyFill = sky_fill;
	ShadowReceiving = true;
	Note_Shadow_State_Changed();
}

void DX11BackendClass::Clear_Shadow_Parameters()
{
	ShadowReceiving = false;
	ShadowFromClipValid = false;
	Note_Shadow_State_Changed();
}

/* The smoke's map.  A quarter of the depth map's width is plenty: 3.5 world units a texel over the
	 1800 unit box, and nothing in a smoke cloud is that sharp.  Four 32-bit channels because the third
	 sum is a square, and in half floats a plume 200 units deep vanished into the rounding of a depth
	 near 0.5 squared.  512 square of that is 4 MB. */
static const unsigned SMOKE_MAP_TEXELS = 512;
static const unsigned SMOKE_MAP_SLOT = DX11_BACKEND_TEXTURE_STAGES + 2;		///< t6 and s6, after the depth map's
static const unsigned SMOKE_SPLAT_FLOATS = 6;		///< per caster: sun clip x, y, z, radius, optical depth, depth spread
static const unsigned SMOKE_MOST_CASTERS = 16384;
// How a smoke particle shades itself (VOLUMETRIC_SAMPLING, smoke_reaching): how dark one on its
// plume's far side goes, and the power on the share of the plume ahead of it that keeps the sun
// side lit.  The 2026-10-04 sweep: at 16 white smoke's sun side came out 6.2% under the unshaded
// plume and its far side 0.85 of the sun side, soot 2.4% and 0.87; at 8 white lost 10.8% on its sun
// side, and at 32 soot went flat.
static const float SMOKE_SELF_SHADOW_GAIN = 0.8f;
static const float SMOKE_SELF_SHADOW_CURVE = 16.0f;
// How far in front of what is behind it a particle sprite has fully faded in, in world units.  A
// tank is about thirty long; a smoke puff is ten to forty across.  At two, a sprite still met the
// ground over a pixel or two at the default zoom: a nuke's haze and the Scud's grey smoke came out as
// stacked stair-stepped terraces and every toxin puff as a hard-rimmed island, the posterized
// blastcheck frames of 2026-10-06, and at zero it is the unfaded game's cut lines outright.  Twelve
// on the smoothstep (SOFT_PARTICLE_SAMPLING) is smooth in both and keeps the toxin carpet whole; the
// islands once blamed on twelve were the straight ramp's creases, which the smoothstep took away.
// Twenty starts to thin the carpet.  It is the longest fade and not the only one: a sprite fades
// over its own half width where that is shorter, so a mine's flare a few units across, hanging
// under one unit above the ground, is not faded away entire.
static const float SOFT_PARTICLE_FADE_UNITS = 12.0f;

// Each caster is a disc facing the sun, drawn as a four corner strip whose corners come from the
// vertex number, so the only buffer is the one holding the casters.
static const char SMOKE_SPLAT_VERTEX_PROGRAM[] =
	"struct Caster\n"
	"{\n"
	"    float4 Centre : CENTRE;\n"	// the sun's clip x, y and depth, and the radius in clip units
	"    float2 Shape  : SHAPE;\n"	// optical depth through the middle, and the spread in depth
	"};\n"
	"struct Splat\n"
	"{\n"
	"    float4 Position : SV_Position;\n"
	"    float2 Corner   : TEXCOORD0;\n"
	"    float3 Moment   : TEXCOORD1;\n"
	"};\n"
	"Splat main(Caster caster, uint corner : SV_VertexID)\n"
	"{\n"
	"    Splat splat;\n"
	"    splat.Corner = float2((corner & 1u) ? 1.0 : -1.0, (corner & 2u) ? 1.0 : -1.0);\n"
	"    splat.Position = float4(caster.Centre.xy + splat.Corner * caster.Centre.w, 0.5, 1.0);\n"
	"    splat.Moment = float3(caster.Shape.x, caster.Centre.z, caster.Shape.y);\n"
	"    return splat;\n"
	"}\n";

// The three sums VOLUMETRIC_SAMPLING reads back, added up by the blend, and in alpha the depth of
// the particle nearest the sun, kept by a minimum blend: the plume's own front, which a particle
// with nothing ahead of it is measured from.  The disc is thickest in its middle and thins to
// nothing at its rim, which is the shape every smoke sprite in the game has; outside it the quad's
// corners leave the front alone.
static const char SMOKE_SPLAT_PIXEL_PROGRAM[] =
	"struct Splat\n"
	"{\n"
	"    float4 Position : SV_Position;\n"
	"    float2 Corner   : TEXCOORD0;\n"
	"    float3 Moment   : TEXCOORD1;\n"
	"};\n"
	"float4 main(Splat splat) : SV_Target\n"
	"{\n"
	"    float rim = saturate(1.0 - dot(splat.Corner, splat.Corner));\n"
	"    float thickness = splat.Moment.x * rim * rim;\n"
	"    float depth = splat.Moment.y;\n"
	"    float spread = splat.Moment.z;\n"
	"    return float4(thickness, thickness * depth, thickness * (depth * depth + spread * spread),\n"
	"        rim > 0.0 ? depth : 1.0);\n"
	"}\n";

void DX11BackendClass::Release_Smoke_Map()
{
	IUnknown ** const held[] = {
		(IUnknown **)&SmokeMapTexture, (IUnknown **)&SmokeMapTarget, (IUnknown **)&SmokeMapSurface,
		(IUnknown **)&SmokeMapSampler, (IUnknown **)&SmokeSplatVertexShader,
		(IUnknown **)&SmokeSplatPixelShader, (IUnknown **)&SmokeSplatLayout,
		(IUnknown **)&SmokeSplatBlend, (IUnknown **)&SmokeSplatRasterizer,
		(IUnknown **)&SmokeSplatInstances,
	};
	for (unsigned index = 0; index < sizeof(held) / sizeof(held[0]); ++index) {
		if (*held[index] != NULL) {
			(*held[index])->Release();
			*held[index] = NULL;
		}
	}
	SmokeSplatCapacity = 0;
	SmokeMapRefused = false;
	SmokeMapFilled = false;
}

bool DX11BackendClass::Make_Smoke_Map()
{
	if (SmokeMapTarget != NULL) {
		return true;
	}
	if (SmokeMapRefused) {
		return false;
	}

	ID3D11Device * device = Device->Get_Device();
	const UINT wanted = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET
		| D3D11_FORMAT_SUPPORT_BLENDABLE | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
	UINT support = 0;
	if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_R32G32B32A32_FLOAT, &support))
		|| (support & wanted) != wanted) {
		Note_Refusal("the device cannot blend and filter the smoke's 32-bit float map");
		SmokeMapRefused = true;
		return false;
	}

	D3D11_TEXTURE2D_DESC description;
	memset(&description, 0, sizeof(description));
	description.Width = SMOKE_MAP_TEXELS;
	description.Height = SMOKE_MAP_TEXELS;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	D3D11_SAMPLER_DESC sampler;
	memset(&sampler, 0, sizeof(sampler));
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;

	D3D11_BLEND_DESC blend;
	memset(&blend, 0, sizeof(blend));
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MIN;		// the front, nearest the sun
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	D3D11_RASTERIZER_DESC rasterizer;
	memset(&rasterizer, 0, sizeof(rasterizer));
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = FALSE;

	const D3D11_INPUT_ELEMENT_DESC elements[] = {
		{ "CENTRE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "SHAPE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	};

	std::vector<unsigned char> vertex_code;
	std::vector<unsigned char> pixel_code;
	const bool made = SUCCEEDED(device->CreateTexture2D(&description, NULL, &SmokeMapSurface))
		&& SUCCEEDED(device->CreateRenderTargetView(SmokeMapSurface, NULL, &SmokeMapTarget))
		&& SUCCEEDED(device->CreateShaderResourceView(SmokeMapSurface, NULL, &SmokeMapTexture))
		&& SUCCEEDED(device->CreateSamplerState(&sampler, &SmokeMapSampler))
		&& SUCCEEDED(device->CreateBlendState(&blend, &SmokeSplatBlend))
		&& SUCCEEDED(device->CreateRasterizerState(&rasterizer, &SmokeSplatRasterizer))
		&& Compile_Program(SMOKE_SPLAT_VERTEX_PROGRAM, "smokesplat", VERTEX_PROFILE, vertex_code)
		&& Compile_Program(SMOKE_SPLAT_PIXEL_PROGRAM, "smokesplat", PIXEL_PROFILE, pixel_code)
		&& SUCCEEDED(device->CreateVertexShader(&vertex_code[0], vertex_code.size(), NULL,
			&SmokeSplatVertexShader))
		&& SUCCEEDED(device->CreatePixelShader(&pixel_code[0], pixel_code.size(), NULL,
			&SmokeSplatPixelShader))
		&& SUCCEEDED(device->CreateInputLayout(elements, 2, &vertex_code[0], vertex_code.size(),
			&SmokeSplatLayout));
	if (!made) {
		Note_Refusal("the device would not make the smoke's map or the program that fills it");
		Release_Smoke_Map();
		SmokeMapRefused = true;
		return false;
	}
	return true;
}

bool DX11BackendClass::Fill_Smoke_Map(const float * casters, unsigned count, float strength)
{
	if (SmokeMapFilled) {
		SmokeMapFilled = false;
		ConstantsChanged = true;
	}
	if (Device == NULL || ShadowMapBound) {
		return false;
	}
	// Nothing to draw is not a reason to make the map; whether it could be made is answered the
	// first time there is.  A ground strength of nought still fills it for the smoke's own shade.
	if (casters == NULL || count == 0) {
		return !SmokeMapRefused;
	}
	if (!Make_Smoke_Map()) {
		return false;
	}
	if (count > SMOKE_MOST_CASTERS) {
		count = SMOKE_MOST_CASTERS;
	}

	/* Into the sun's clip space through the matrix the depth pass drew with, on the CPU: the sun is
		 orthographic, so a sphere's radius is the same number of clip units wherever it stands and
		 the disc needs no projection of its own.  How many clip units a world unit is comes off the
		 matrix's own columns. */
	const float * sun = SunViewProjection;
	const float across = sqrtf(sun[0] * sun[0] + sun[4] * sun[4] + sun[8] * sun[8]);
	const float deep = sqrtf(sun[2] * sun[2] + sun[6] * sun[6] + sun[10] * sun[10]);

	SmokeSplats.resize(count * SMOKE_SPLAT_FLOATS);
	unsigned splats = 0;
	for (unsigned index = 0; index < count; ++index) {
		const float * caster = casters + index * 5;
		const float w = caster[0] * sun[3] + caster[1] * sun[7] + caster[2] * sun[11] + sun[15];
		if (w <= 0.0f || caster[3] <= 0.0f || caster[4] <= 0.0f) {
			continue;
		}
		const float x = (caster[0] * sun[0] + caster[1] * sun[4] + caster[2] * sun[8] + sun[12]) / w;
		const float y = (caster[0] * sun[1] + caster[1] * sun[5] + caster[2] * sun[9] + sun[13]) / w;
		const float z = (caster[0] * sun[2] + caster[1] * sun[6] + caster[2] * sun[10] + sun[14]) / w;
		const float radius = caster[3] * across;
		// A ball's depth along the ray spreads about half its radius either side of its middle.
		const float spread = 0.5f * caster[3] * deep;
		if (x < -1.0f - radius || x > 1.0f + radius || y < -1.0f - radius || y > 1.0f + radius
			|| z < -spread || z > 1.0f + spread) {
			continue;
		}
		float * splat = &SmokeSplats[splats * SMOKE_SPLAT_FLOATS];
		splat[0] = x;
		splat[1] = y;
		splat[2] = z;
		splat[3] = radius;
		splat[4] = caster[4];
		splat[5] = spread;
		++splats;
	}
	if (splats == 0) {
		return true;
	}

	ID3D11Device * device = Device->Get_Device();
	ID3D11DeviceContext * context = Device->Get_Context();
	if (SmokeSplatCapacity < splats) {
		if (SmokeSplatInstances != NULL) {
			SmokeSplatInstances->Release();
			SmokeSplatInstances = NULL;
		}
		SmokeSplatCapacity = 0;
		unsigned capacity = 1024;
		while (capacity < splats) {
			capacity *= 2;
		}
		D3D11_BUFFER_DESC buffer;
		memset(&buffer, 0, sizeof(buffer));
		buffer.ByteWidth = capacity * SMOKE_SPLAT_FLOATS * sizeof(float);
		buffer.Usage = D3D11_USAGE_DYNAMIC;
		buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&buffer, NULL, &SmokeSplatInstances))) {
			Note_Refusal("the device refused the smoke casters' buffer");
			SmokeSplatInstances = NULL;
			return false;
		}
		SmokeSplatCapacity = capacity;
	}
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(SmokeSplatInstances, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return false;
	}
	memcpy(mapped.pData, &SmokeSplats[0], splats * SMOKE_SPLAT_FLOATS * sizeof(float));
	context->Unmap(SmokeSplatInstances, 0);

	// The map is still bound for reading from the frame before, and a view cannot be both.  What the
	// frame is drawing into comes back afterwards, the way End_Shadow_Map puts it back.
	ID3D11ShaderResourceView * const none = NULL;
	context->PSSetShaderResources(SMOKE_MAP_SLOT, 1, &none);
	ID3D11RenderTargetView * const saved_target = CurrentTarget;
	const unsigned saved_width = ViewportWidth;
	const unsigned saved_height = ViewportHeight;

	const float empty[4] = { 0.0f, 0.0f, 0.0f, 1.0f };		// no smoke, and its front at the far plane
	context->OMSetRenderTargets(1, &SmokeMapTarget, NULL);
	context->ClearRenderTargetView(SmokeMapTarget, empty);
	Set_Viewport(0, 0, SMOKE_MAP_TEXELS, SMOKE_MAP_TEXELS);

	const UINT stride = SMOKE_SPLAT_FLOATS * sizeof(float);
	const UINT offset = 0;
	context->IASetInputLayout(SmokeSplatLayout);
	context->IASetVertexBuffers(0, 1, &SmokeSplatInstances, &stride, &offset);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context->VSSetShader(SmokeSplatVertexShader, NULL, 0);
	context->PSSetShader(SmokeSplatPixelShader, NULL, 0);
	context->OMSetBlendState(SmokeSplatBlend, NULL, 0xffffffff);
	context->OMSetDepthStencilState(NULL, 0);
	context->RSSetState(SmokeSplatRasterizer);
	context->DrawInstanced(4, splats, 0, 0);

	Set_Render_Target(saved_target);
	if (saved_width != 0 && saved_height != 0) {
		Set_Viewport(0, 0, saved_width, saved_height);
	}
	Forget_Bindings();

	SmokeMapFilled = true;
	SmokeStrength = strength;
	ConstantsChanged = true;
	return true;
}

// A caster's depth and nothing else.  The position goes through the instance's product of world,
// view and projection the way the generated vertex programs take it through theirs
// (ffvertex.cpp: mul of the model position by a row major matrix), so the same vertex lands on the
// same depth.  No pixel program for an opaque caster: the pass writes no colour, and it discards
// nothing.
#define SHADOW_CASTER_INSTANCE_INPUT \
	"    float4 Row0 : WORLDVIEWPROJECTION0;\n" \
	"    float4 Row1 : WORLDVIEWPROJECTION1;\n" \
	"    float4 Row2 : WORLDVIEWPROJECTION2;\n" \
	"    float4 Row3 : WORLDVIEWPROJECTION3;\n"
#define SHADOW_CASTER_PROJECT(position) \
	"    float4x4 world_view_projection = float4x4(caster.Row0, caster.Row1, caster.Row2, caster.Row3);\n" \
	"    return mul(float4(" position ", 1.0), world_view_projection);\n"

static const char SHADOW_CASTER_VERTEX_PROGRAM[] =
	"struct Caster\n"
	"{\n"
	"    float3 Position : POSITION;\n"
	SHADOW_CASTER_INSTANCE_INPUT
	"};\n"
	"float4 main(Caster caster) : SV_Position\n"
	"{\n"
	SHADOW_CASTER_PROJECT("caster.Position")
	"}\n";

// An alpha tested caster carries stage 0's coordinates to the pixel program, untransformed, as the
// generated programs pass them for a stage without a mapper.
static const char SHADOW_CASTER_ALPHA_VERTEX_PROGRAM[] =
	"struct Caster\n"
	"{\n"
	"    float3 Position : POSITION;\n"
	"    float2 TexCoord : TEXCOORD;\n"
	SHADOW_CASTER_INSTANCE_INPUT
	"};\n"
	"struct Output\n"
	"{\n"
	"    float4 Position : SV_Position;\n"
	"    float2 TexCoord : TEXCOORD0;\n"
	"};\n"
	"Output main(Caster caster)\n"
	"{\n"
	"    Output output;\n"
	"    float4x4 world_view_projection = float4x4(caster.Row0, caster.Row1, caster.Row2, caster.Row3);\n"
	"    output.Position = mul(float4(caster.Position, 1.0), world_view_projection);\n"
	"    output.TexCoord = caster.TexCoord;\n"
	"    return output;\n"
	"}\n";

// A skin's vertex through its bone the way MeshGeometryClass::get_deformed_vertices takes it
// (Matrix3D::Transform_Vector: each row's dot with the position plus its translation), then through
// view and projection with the identity world the texture category's walk gives a skin.  The
// bones are three rows each, the pass's skins one after another.
static const char SHADOW_CASTER_SKIN_VERTEX_PROGRAM[] =
	"Buffer<float4> Bones : register(t0);\n"
	"struct Caster\n"
	"{\n"
	"    float3 Position : POSITION;\n"
	"    uint Bone : BONE;\n"
	SHADOW_CASTER_INSTANCE_INPUT
	"    uint FirstBone : FIRSTBONE;\n"
	"};\n"
	"float4 main(Caster caster) : SV_Position\n"
	"{\n"
	"    uint row = (caster.FirstBone + caster.Bone) * 3;\n"
	"    float4 x = Bones[row];\n"
	"    float4 y = Bones[row + 1];\n"
	"    float4 z = Bones[row + 2];\n"
	"    float3 p = caster.Position;\n"
	"    float3 skinned = float3(x.x * p.x + x.y * p.y + x.z * p.z + x.w,\n"
	"        y.x * p.x + y.y * p.y + y.z * p.z + y.w,\n"
	"        z.x * p.x + z.y * p.y + z.z * p.z + z.w);\n"
	SHADOW_CASTER_PROJECT("skinned")
	"}\n";

// ShaderClass::Apply's test for an opaque material (reference 0x60, greater or equal) on stage 0's
// alpha, rounded to a level first as ffshader.cpp's ALPHA_LEVEL does.  The material's diffuse
// alpha that stage 0 multiplies by is one for every caster taken (Add_Shadow_Caster).
static const char SHADOW_CASTER_ALPHA_PIXEL_PROGRAM[] =
	"Texture2D Texture0 : register(t0);\n"
	"SamplerState Sampler0 : register(s0);\n"
	"struct Input\n"
	"{\n"
	"    float4 Position : SV_Position;\n"
	"    float2 TexCoord : TEXCOORD0;\n"
	"};\n"
	"void main(Input input)\n"
	"{\n"
	"    float alpha_level = floor(Texture0.Sample(Sampler0, input.TexCoord).a * 255.0 + 0.5);\n"
	"    clip(alpha_level - 96.0);\n"
	"}\n";

#undef SHADOW_CASTER_INSTANCE_INPUT
#undef SHADOW_CASTER_PROJECT

static const unsigned SHADOW_CASTER_MATRIX_FLOATS = 16;
// The matrix, the first bone and three floats of padding.
static const unsigned SHADOW_CASTER_INSTANCE_FLOATS = 20;
static const unsigned SHADOW_CASTER_FIRST_BONE_BYTES = SHADOW_CASTER_MATRIX_FLOATS * sizeof(float);
static const unsigned SHADOW_CASTER_ROWS_A_BONE = 3;
static const unsigned SHADOW_CASTER_FIRST_INSTANCES = 1024;
static const unsigned SHADOW_CASTER_FIRST_BONE_ROWS = 4096;
static const unsigned SHADOW_SKIN_FIRST_VERTICES = 65536;

void DX11BackendClass::Release_Shadow_Casters()
{
	IUnknown ** const held[] = {
		(IUnknown **)&ShadowCasterVertexShaders[0], (IUnknown **)&ShadowCasterVertexShaders[1],
		(IUnknown **)&ShadowCasterVertexShaders[2], (IUnknown **)&ShadowCasterLayouts[0],
		(IUnknown **)&ShadowCasterLayouts[1], (IUnknown **)&ShadowCasterLayouts[2],
		(IUnknown **)&ShadowCasterAlphaTest, (IUnknown **)&ShadowCasterInstances,
		(IUnknown **)&ShadowCasterBonesView, (IUnknown **)&ShadowCasterBones,
		(IUnknown **)&ShadowSkinSource,
	};
	for (unsigned index = 0; index < sizeof(held) / sizeof(held[0]); ++index) {
		if (*held[index] != NULL) {
			(*held[index])->Release();
			*held[index] = NULL;
		}
	}
	ShadowCasterCapacity = 0;
	ShadowCasterBonesCapacity = 0;
	ShadowCasterRefused = false;
	ShadowSkinCapacity = 0;
	ShadowSkinUsed = 0;
	++ShadowSkinGeneration;
}

bool DX11BackendClass::Make_Shadow_Caster_Program()
{
	if (ShadowCasterAlphaTest != NULL) {
		return true;
	}
	if (ShadowCasterRefused) {
		return false;
	}
	const D3D11_INPUT_ELEMENT_DESC rigid[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "WORLDVIEWPROJECTION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "WORLDVIEWPROJECTION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "WORLDVIEWPROJECTION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		{ "WORLDVIEWPROJECTION", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	};
	// The coordinates come from the vertex buffer bound a second time at slot 2, its offset moved on
	// to the stage's set, so one layout serves every vertex format.
	const D3D11_INPUT_ELEMENT_DESC alpha_tested[] = {
		rigid[0], rigid[1], rigid[2], rigid[3], rigid[4],
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	const D3D11_INPUT_ELEMENT_DESC skinned[] = {
		rigid[0], rigid[1], rigid[2], rigid[3], rigid[4],
		{ "BONE", 0, DXGI_FORMAT_R32_UINT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "FIRSTBONE", 0, DXGI_FORMAT_R32_UINT, 1, SHADOW_CASTER_FIRST_BONE_BYTES, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	};
	struct Program
	{
		const char * Source;
		const char * Name;
		const D3D11_INPUT_ELEMENT_DESC * Elements;
		unsigned ElementCount;
	};
	const Program programs[3] = {
		{ SHADOW_CASTER_VERTEX_PROGRAM, "shadowcaster", rigid, sizeof(rigid) / sizeof(rigid[0]) },
		{ SHADOW_CASTER_ALPHA_VERTEX_PROGRAM, "shadowcasteralpha", alpha_tested,
			sizeof(alpha_tested) / sizeof(alpha_tested[0]) },
		{ SHADOW_CASTER_SKIN_VERTEX_PROGRAM, "shadowcasterskin", skinned, sizeof(skinned) / sizeof(skinned[0]) },
	};
	ID3D11Device * device = Device->Get_Device();
	std::vector<unsigned char> code;
	bool made = true;
	for (unsigned kind = 0; kind < 3 && made; ++kind) {
		made = Compile_Program(programs[kind].Source, programs[kind].Name, VERTEX_PROFILE, code)
			&& SUCCEEDED(device->CreateVertexShader(&code[0], code.size(), NULL, &ShadowCasterVertexShaders[kind]))
			&& SUCCEEDED(device->CreateInputLayout(programs[kind].Elements, programs[kind].ElementCount,
				&code[0], code.size(), &ShadowCasterLayouts[kind]));
	}
	made = made && Compile_Program(SHADOW_CASTER_ALPHA_PIXEL_PROGRAM, "shadowcasteralphatest", PIXEL_PROFILE, code)
		&& SUCCEEDED(device->CreatePixelShader(&code[0], code.size(), NULL, &ShadowCasterAlphaTest));
	if (!made) {
		Note_Refusal("the device would not make the instanced shadow caster programs");
		Release_Shadow_Casters();
		ShadowCasterRefused = true;
		return false;
	}
	return true;
}

bool DX11BackendClass::Upload_Shadow_Skin(const ShadowSkinVertex * vertices, unsigned count,
	unsigned & first_vertex)
{
	if (Device == NULL) {
		return false;
	}
	ID3D11Device * device = Device->Get_Device();
	if (ShadowSkinSource == NULL || ShadowSkinUsed + count > ShadowSkinCapacity) {
		unsigned capacity = (ShadowSkinCapacity != 0) ? ShadowSkinCapacity * 2 : SHADOW_SKIN_FIRST_VERTICES;
		while (capacity < count) {
			capacity *= 2;
		}
		if (ShadowSkinSource != NULL) {
			ShadowSkinSource->Release();
			ShadowSkinSource = NULL;
		}
		ShadowSkinCapacity = 0;
		ShadowSkinUsed = 0;
		++ShadowSkinGeneration;
		D3D11_BUFFER_DESC buffer;
		memset(&buffer, 0, sizeof(buffer));
		buffer.ByteWidth = capacity * sizeof(ShadowSkinVertex);
		buffer.Usage = D3D11_USAGE_DEFAULT;
		buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		if (FAILED(device->CreateBuffer(&buffer, NULL, &ShadowSkinSource))) {
			Note_Refusal("the device refused the shadow skin source");
			ShadowSkinSource = NULL;
			return false;
		}
		ShadowSkinCapacity = capacity;
	}
	D3D11_BOX box;
	box.left = ShadowSkinUsed * sizeof(ShadowSkinVertex);
	box.right = (ShadowSkinUsed + count) * sizeof(ShadowSkinVertex);
	box.top = 0;
	box.bottom = 1;
	box.front = 0;
	box.back = 1;
	Device->Get_Context()->UpdateSubresource(ShadowSkinSource, 0, &box, vertices, 0, 0);
	first_vertex = ShadowSkinUsed;
	ShadowSkinUsed += count;
	return true;
}

// The world matrix the texture category's walk puts on the device for a Matrix3D's three rows:
// Matrix4x4(Matrix3D).Transpose(), Direct3D 9's layout, the translation in the last row.
static void world_from_rows(const float rows[12], float world[16])
{
	for (unsigned row = 0; row < 3; ++row) {
		world[row * 4 + 0] = rows[0 * 4 + row];
		world[row * 4 + 1] = rows[1 * 4 + row];
		world[row * 4 + 2] = rows[2 * 4 + row];
		world[row * 4 + 3] = 0.0f;
	}
	world[12] = rows[3];
	world[13] = rows[7];
	world[14] = rows[11];
	world[15] = 1.0f;
}

#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
// multiply()'s product a row of four columns at a time: every element still starts from nought
// and adds its four products in index order, each a multiply and then an add, never a fused
// multiply-add, so the floats are multiply()'s to the bit.
static void multiply_rows(const float left[16], const float right[16], float result[16])
{
	const __m128 right0 = _mm_loadu_ps(right);
	const __m128 right1 = _mm_loadu_ps(right + 4);
	const __m128 right2 = _mm_loadu_ps(right + 8);
	const __m128 right3 = _mm_loadu_ps(right + 12);
	for (unsigned row = 0; row < 4; ++row) {
		const float * const factors = left + row * 4;
		__m128 sum = _mm_setzero_ps();
		sum = _mm_add_ps(sum, _mm_mul_ps(_mm_set1_ps(factors[0]), right0));
		sum = _mm_add_ps(sum, _mm_mul_ps(_mm_set1_ps(factors[1]), right1));
		sum = _mm_add_ps(sum, _mm_mul_ps(_mm_set1_ps(factors[2]), right2));
		sum = _mm_add_ps(sum, _mm_mul_ps(_mm_set1_ps(factors[3]), right3));
		_mm_storeu_ps(result + row * 4, sum);
	}
}
#else
static void multiply_rows(const float left[16], const float right[16], float result[16])
{
	multiply(left, right, result);
}
#endif

static const float IDENTITY_ROWS[12] ={ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

bool DX11BackendClass::Upload_Shadow_Casters(const float * const * worlds, const unsigned * first_bones,
	unsigned instance_count, const float * const * bones, unsigned bone_count)
{
	if (Device == NULL || !ShadowMapBound || !Make_Shadow_Caster_Program()) {
		return false;
	}
	if (instance_count == 0) {
		return true;
	}

	ID3D11Device * device = Device->Get_Device();
	ID3D11DeviceContext * context = Device->Get_Context();
	if (ShadowCasterCapacity < instance_count) {
		if (ShadowCasterInstances != NULL) {
			ShadowCasterInstances->Release();
			ShadowCasterInstances = NULL;
		}
		ShadowCasterCapacity = 0;
		unsigned capacity = SHADOW_CASTER_FIRST_INSTANCES;
		while (capacity < instance_count) {
			capacity *= 2;
		}
		D3D11_BUFFER_DESC buffer;
		memset(&buffer, 0, sizeof(buffer));
		buffer.ByteWidth = capacity * SHADOW_CASTER_INSTANCE_FLOATS * sizeof(float);
		buffer.Usage = D3D11_USAGE_DYNAMIC;
		buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&buffer, NULL, &ShadowCasterInstances))) {
			Note_Refusal("the device refused the shadow casters' instance buffer");
			ShadowCasterInstances = NULL;
			return false;
		}
		ShadowCasterCapacity = capacity;
	}
	const unsigned row_count = bone_count * SHADOW_CASTER_ROWS_A_BONE;
	if (ShadowCasterBonesCapacity < row_count) {
		if (ShadowCasterBonesView != NULL) {
			ShadowCasterBonesView->Release();
			ShadowCasterBonesView = NULL;
		}
		if (ShadowCasterBones != NULL) {
			ShadowCasterBones->Release();
			ShadowCasterBones = NULL;
		}
		ShadowCasterBonesCapacity = 0;
		unsigned capacity = SHADOW_CASTER_FIRST_BONE_ROWS;
		while (capacity < row_count) {
			capacity *= 2;
		}
		D3D11_BUFFER_DESC buffer;
		memset(&buffer, 0, sizeof(buffer));
		buffer.ByteWidth = capacity * 4 * sizeof(float);
		buffer.Usage = D3D11_USAGE_DYNAMIC;
		buffer.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11_SHADER_RESOURCE_VIEW_DESC view;
		memset(&view, 0, sizeof(view));
		view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		view.Buffer.FirstElement = 0;
		view.Buffer.NumElements = capacity;
		if (FAILED(device->CreateBuffer(&buffer, NULL, &ShadowCasterBones))
				|| FAILED(device->CreateShaderResourceView(ShadowCasterBones, &view, &ShadowCasterBonesView))) {
			Note_Refusal("the device refused the shadow casters' bone buffer");
			if (ShadowCasterBones != NULL) {
				ShadowCasterBones->Release();
				ShadowCasterBones = NULL;
			}
			ShadowCasterBonesView = NULL;
			return false;
		}
		ShadowCasterBonesCapacity = capacity;
	}

	/* Each instance's matrix as Upload_Vertex_Constants makes it, world times view, then times the
		 projection, written straight into the mapped buffer.  The same two products in the same order
		 are the same floats.  Every identity world (the skins, and meshes at the origin) has the same
		 product, made once. */
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(ShadowCasterInstances, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return false;
	}
	float identity[16];
	world_from_rows(IDENTITY_ROWS, identity);
	float identity_view[16];
	float identity_product[16];
	multiply(identity, View, identity_view);
	multiply(identity_view, Projection, identity_product);
	float * target = static_cast<float *>(mapped.pData);
	for (unsigned instance = 0; instance < instance_count; ++instance, target += SHADOW_CASTER_INSTANCE_FLOATS) {
		if (worlds[instance] == NULL) {
			memcpy(target, identity_product, sizeof(identity_product));
		} else {
			float world[16];
			float world_view[16];
			world_from_rows(worlds[instance], world);
			multiply_rows(world, View, world_view);
			multiply_rows(world_view, Projection, target);
		}
		// The padding too: a write-combined buffer left with gaps goes out in pieces.
		const unsigned tail[SHADOW_CASTER_INSTANCE_FLOATS - SHADOW_CASTER_MATRIX_FLOATS] ={ first_bones[instance], 0, 0, 0 };
		memcpy(target + SHADOW_CASTER_MATRIX_FLOATS, tail, sizeof(tail));
	}
	context->Unmap(ShadowCasterInstances, 0);

	if (row_count != 0) {
		if (FAILED(context->Map(ShadowCasterBones, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			return false;
		}
		float * rows = static_cast<float *>(mapped.pData);
		for (unsigned bone = 0; bone < bone_count; ++bone, rows += SHADOW_CASTER_ROWS_A_BONE * 4) {
			memcpy(rows, bones[bone], SHADOW_CASTER_ROWS_A_BONE * 4 * sizeof(float));
		}
		context->Unmap(ShadowCasterBones, 0);
	}
	return true;
}

bool DX11BackendClass::Draw_Shadow_Casters(const ShadowCasterRun * runs, unsigned run_count)
{
	if (run_count == 0) {
		return true;
	}
	ID3D11DeviceContext * context = Device->Get_Context();
	bool alpha_tested = false;
	for (unsigned index = 0; index < run_count && !alpha_tested; ++index) {
		alpha_tested = runs[index].Kind == SHADOW_CASTER_ALPHA_TESTED;
	}
	if (alpha_tested) {
		if (Textures[0] == NULL) {
			return false;
		}
		ID3D11SamplerState * const sampler = Sampler_State(0);
		context->PSSetShaderResources(0, 1, &Textures[0]);
		context->PSSetSamplers(0, 1, &sampler);
	}
	context->VSSetShaderResources(0, 1, &ShadowCasterBonesView);

	/* The cull and the depth test are each run's own, the rest of the rasterizer and depth state
		 (the bias, the clipping, the depth writes the caster pass forces) is what the pass holds, which
		 is what the mesh renderer's draws would have read.  Built through the render states so a
		 state object is the one those draws would have been given; both go back as they were. */
	const DWORD held_cull = RenderStates.Get_Stored_Render_State(D3DRS_CULLMODE);
	const DWORD held_depth = RenderStates.Get_Stored_Render_State(D3DRS_ZFUNC);
	ID3D11Buffer * bound_vertices = NULL;
	unsigned bound_stride = 0;
	unsigned bound_texture_offset = 0;
	ID3D11Buffer * bound_indices = NULL;
	bool bound_strip = false;
	bool states_known = false;
	ShadowCasterKind bound_kind = SHADOW_CASTER_RIGID;
	for (unsigned index = 0; index < run_count; ++index) {
		const ShadowCasterRun & run = runs[index];
		if (!states_known || run.CullMode != RenderStates.Get_Stored_Render_State(D3DRS_CULLMODE)
				|| run.DepthFunction != RenderStates.Get_Stored_Render_State(D3DRS_ZFUNC)) {
			Set_Render_State(D3DRS_CULLMODE, run.CullMode);
			Set_Render_State(D3DRS_ZFUNC, run.DepthFunction);
			context->RSSetState(Rasterizer_State());
			context->OMSetDepthStencilState(Depth_Stencil_State(), RenderStates.Get_Stencil_Reference());
			states_known = true;
		}
		if (index == 0 || run.Kind != bound_kind) {
			context->IASetInputLayout(ShadowCasterLayouts[run.Kind]);
			context->VSSetShader(ShadowCasterVertexShaders[run.Kind], NULL, 0);
			context->PSSetShader((run.Kind == SHADOW_CASTER_ALPHA_TESTED) ? ShadowCasterAlphaTest : NULL, NULL, 0);
			bound_kind = run.Kind;
			bound_vertices = NULL;
		}
		ID3D11Buffer * const vertices = (run.Kind == SHADOW_CASTER_SKINNED) ? ShadowSkinSource : run.Vertices;
		const unsigned stride = (run.Kind == SHADOW_CASTER_SKINNED) ? sizeof(ShadowSkinVertex) : run.Stride;
		if (vertices != bound_vertices || stride != bound_stride || run.TextureOffset != bound_texture_offset) {
			ID3D11Buffer * const buffers[3] = { vertices, ShadowCasterInstances, vertices };
			const UINT strides[3] = { stride, SHADOW_CASTER_INSTANCE_FLOATS * sizeof(float), stride };
			const UINT offsets[3] = { 0, 0, run.TextureOffset };
			context->IASetVertexBuffers(0, 3, buffers, strides, offsets);
			bound_vertices = vertices;
			bound_stride = stride;
			bound_texture_offset = run.TextureOffset;
		}
		if (index == 0 || run.Indices != bound_indices) {
			context->IASetIndexBuffer(run.Indices, DXGI_FORMAT_R16_UINT, 0);
			bound_indices = run.Indices;
		}
		if (index == 0 || run.Strip != bound_strip) {
			context->IASetPrimitiveTopology(run.Strip
				? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			bound_strip = run.Strip;
		}
		context->DrawIndexedInstanced(run.IndexCount, run.InstanceCount, run.StartIndex,
			static_cast<INT>(run.BaseVertex), run.FirstInstance);
		++DrawsMade;
	}
	Set_Render_State(D3DRS_CULLMODE, held_cull);
	Set_Render_State(D3DRS_ZFUNC, held_depth);
	ID3D11ShaderResourceView * const no_view = NULL;
	context->VSSetShaderResources(0, 1, &no_view);
	Forget_Bindings();
	return true;
}

bool DX11BackendClass::Camera_Space_Draw() const
{
	static const float IDENTITY[16] = {
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	// A perspective projection's third column ends in one; an orthographic one, the interface's and
	// the sun's own, ends in nought.
	return Projection[11] != 0.0f && memcmp(View, IDENTITY, sizeof(IDENTITY)) == 0;
}

void DX11BackendClass::Set_Scene_View(const float view[16])
{
	if (!SceneViewKnown || memcmp(SceneView, view, sizeof(SceneView)) != 0) {
		memcpy(SceneView, view, sizeof(SceneView));
		SceneViewKnown = true;
		ConstantsChanged = true;
	}
	memcpy(SceneProjection, Projection, sizeof(SceneProjection));
	SceneViewThisFrame = true;
}

void DX11BackendClass::Open_Frame()
{
	SceneViewThisFrame = false;
}

void DX11BackendClass::Set_Smoke_Glow(bool glow)
{
	if (SmokeGlow != glow) {
		SmokeGlow = glow;
		PipelineChanged = true;
	}
}

void DX11BackendClass::Set_Fire_Look(bool fire)
{
	if (FireLook != fire) {
		FireLook = fire;
		PipelineChanged = true;
	}
}

void DX11BackendClass::Set_Emissive(bool emissive)
{
	if (Emissive != emissive) {
		Emissive = emissive;
		PipelineChanged = true;
	}
}

void DX11BackendClass::Set_Team_Glow(bool glow)
{
	if (TeamGlow != glow) {
		TeamGlow = glow;
		PipelineChanged = true;
	}
}

void DX11BackendClass::Set_Hemi_Ambient(bool hemi)
{
	if (HemiAmbient != hemi) {
		HemiAmbient = hemi;
		PipelineChanged = true;
		VertexConstantsChanged = true;
	}
}

/** An opaque fixed-function draw into the scene's own target, under no other look: blended, the
		stripe would add light behind a fading or cloaked body, and a mirror, a portrait or the sun's
		map has no bloom to feed.  Like Additive_Glow it wants the half float scene (a gain off one)
		and not Classic, whose picture is -d3d9's (SoftParticlesAllowed carries that setting). */
bool DX11BackendClass::Team_Glow() const
{
	return TeamGlow && AdditiveGain != 1.0f && SoftParticlesAllowed
		&& !ShadowMapBound && CurrentTarget == NULL
		&& VertexProgram == ENGINE_SHADER_NONE && PixelProgram == ENGINE_SHADER_NONE
		&& (VertexFormat & D3DFVF_XYZRHW) == 0
		&& RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) == FALSE
		&& Draw_Effect() == DRAW_EFFECT_NONE;
}

/** Which way a fire billboard wears its look: through the colour for a blend that adds it
		unweighted, through the alpha otherwise.  Off under Classic, whose picture is -d3d9's
		(SoftParticlesAllowed carries that setting), and off for anything but a one-stage fixed-function
		draw with a second coordinate set to read, and for a multiplicative blend, which a burnt-away
		flame would darken rather than leave. */
unsigned DX11BackendClass::Fire_Look() const
{
	if (!FireLook || !SoftParticlesAllowed || ShadowMapBound
		|| VertexProgram != ENGINE_SHADER_NONE || PixelProgram != ENGINE_SHADER_NONE
		|| (VertexFormat & D3DFVF_XYZRHW) != 0
		|| (VertexFormat & D3DFVF_TEXCOUNT_MASK) < D3DFVF_TEX2
		|| StageStates[1][D3DTSS_COLOROP] != D3DTOP_DISABLE
		|| RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) == FALSE) {
		return FIRE_LOOK_NONE;
	}
	const DWORD source = RenderStates.Get_Render_State(D3DRS_SRCBLEND);
	const DWORD destination = RenderStates.Get_Render_State(D3DRS_DESTBLEND);
	if (source == D3DBLEND_DESTCOLOR || source == D3DBLEND_ZERO || destination == D3DBLEND_SRCCOLOR) {
		return FIRE_LOOK_NONE;
	}
	return (source == D3DBLEND_ONE) ? FIRE_LOOK_COLOUR : FIRE_LOOK_ALPHA;
}

void DX11BackendClass::Set_Additive_Gain(float gain)
{
	if (AdditiveGain != gain) {
		AdditiveGain = gain;
		// The factor goes with the blend state when it is bound, so the next draw binds again.
		StateObjectsChanged = true;
		Bound.Blend = NULL;
		if (TeamGlow) {
			PipelineChanged = true;		// Team_Glow reads it
		}
	}
}

/** An additive draw into the scene that opted in, the one blend the Glow option boosts: fire and
		explosion particles, laser lines, and the additive meshes the art ships (muzzle flashes, glows).
		A source alpha blend into ONE is left alone, since the blend factor would replace the alpha it
		weighs by. */
bool DX11BackendClass::Additive_Glow() const
{
	return AdditiveGain != 1.0f && GlowDraw && CurrentTarget == NULL && !ShadowMapBound
		&& (VertexFormat & D3DFVF_XYZRHW) == 0
		&& RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) != FALSE
		&& RenderStates.Get_Render_State(D3DRS_SRCBLEND) == D3DBLEND_ONE
		&& RenderStates.Get_Render_State(D3DRS_DESTBLEND) == D3DBLEND_ONE
		&& RenderStates.Get_Render_State(D3DRS_BLENDOP) == D3DBLENDOP_ADD;
}

void DX11BackendClass::Set_Draw_Effect(unsigned effect, const float parameters[4])
{
	if (effect >= DRAW_EFFECT_COUNT) {
		effect = DRAW_EFFECT_NONE;
	}
	if (DrawEffect != effect) {
		DrawEffect = effect;
		PipelineChanged = true;
		ConstantsChanged = true;
		TexturesChanged = true;		// the cloak binds the scene's copy with the stages
	}
	if (effect != DRAW_EFFECT_NONE && memcmp(DrawEffectNumbers, parameters, sizeof(DrawEffectNumbers)) != 0) {
		memcpy(DrawEffectNumbers, parameters, sizeof(DrawEffectNumbers));
		ConstantsChanged = true;
	}
	// Before any of the object's draws resolves: a copy the device refuses has to be known before a
	// cloak program is chosen, or that program reads nothing at t8 and draws the unit black.
	if (effect == DRAW_EFFECT_CLOAK && CurrentTarget == NULL && !ShadowMapBound) {
		Take_Scene_Colour();
	}
}

unsigned DX11BackendClass::Requested_Draw_Effect(float parameters[4]) const
{
	if (DrawEffect == DRAW_EFFECT_NONE) {
		memset(parameters, 0, sizeof(DrawEffectNumbers));
	} else {
		memcpy(parameters, DrawEffectNumbers, sizeof(DrawEffectNumbers));
	}
	return DrawEffect;
}

bool DX11BackendClass::Draws_Into_Scene() const
{
	return CurrentTarget == NULL && !ShadowMapBound;
}

ID3D11RenderTargetView * DX11BackendClass::Scene_Target() const
{
	if (ShadowMapBound) {
		return NULL;
	}
	if (CurrentTarget == NULL) {
		return Device->Get_Scene_View();
	}
	return CurrentDepth == Device->Get_Depth_Stencil_View() ? CurrentTarget : NULL;
}

ID3D11DepthStencilView * DX11BackendClass::Scene_Depth_Target() const
{
	return (CurrentTarget != NULL) ? CurrentDepth : Device->Get_Scene_Depth_View();
}

/** The black and white view, the motion blur and the cross fade draw the whole scene into a target
		of their own, and Draw_Effect refuses every look there; asked first, the scene keeps the heat
		vision and the disabled tint for those frames instead of drawing the object plain. */
bool DX11BackendClass::Takes_Draw_Effect(unsigned effect)
{
	if (!Draws_Into_Scene()) {
		return false;
	}
	return effect != DRAW_EFFECT_CLOAK || Make_Scene_Colour();
}

void DX11BackendClass::Resume_Scene()
{
	const unsigned width = ViewportWidth;
	const unsigned height = ViewportHeight;
	Set_Render_Target(NULL);
	Set_Viewport(0, 0, width, height);
}

/** Which look the current draw takes.  Only the scene's own world draws, and in the sun's map only
		the cloak's thinned shadow: not a mirror or a filter target, not a screen space quad and not a transcribed program.  A draw
		adding light (ONE as either factor) takes the outage alone, which puts a building's glow cards
		out with it; dressed in arcs or a rim, the card's whole quad would show.  A multiplying pass
		takes none.  The cloak needs the scene's copy, and without one the object keeps its plain
		translucency. */
unsigned DX11BackendClass::Draw_Effect() const
{
	// The cloak's shadow is the one look the sun's map takes, and the only place it is drawn.
	if (ShadowMapBound != (DrawEffect == DRAW_EFFECT_CLOAK_SHADOW)) {
		return DRAW_EFFECT_NONE;
	}
	if (DrawEffect == DRAW_EFFECT_NONE || CurrentTarget != NULL
		|| (VertexFormat & D3DFVF_XYZRHW) != 0
		|| VertexProgram != ENGINE_SHADER_NONE || PixelProgram != ENGINE_SHADER_NONE) {
		return DRAW_EFFECT_NONE;
	}
	if (RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) != FALSE) {
		const DWORD source = RenderStates.Get_Render_State(D3DRS_SRCBLEND);
		const DWORD destination = RenderStates.Get_Render_State(D3DRS_DESTBLEND);
		if (source == D3DBLEND_ZERO || source == D3DBLEND_DESTCOLOR || destination == D3DBLEND_SRCCOLOR) {
			return DRAW_EFFECT_NONE;
		}
		if (source == D3DBLEND_ONE || destination == D3DBLEND_ONE) {
			return (DrawEffect == DRAW_EFFECT_OUTAGE) ? DRAW_EFFECT_OUTAGE : DRAW_EFFECT_NONE;
		}
	}
	if (DrawEffect == DRAW_EFFECT_CLOAK && SceneColourRefused) {
		return DRAW_EFFECT_NONE;
	}
	return DrawEffect;
}

void DX11BackendClass::Release_Scene_Colour()
{
	if (SceneColourCopyView != NULL) {
		SceneColourCopyView->Release();
		SceneColourCopyView = NULL;
	}
	if (SceneColourCopy != NULL) {
		SceneColourCopy->Release();
		SceneColourCopy = NULL;
	}
	SceneColourStale = true;
}

/** The texture the cloak's copy of the scene goes into, made at the scene target's own size and
		format and made again when a resize changes either.  One sample: a multisampled scene is
		resolved into it.  Asked by the scene for each cloaked object before it decides on the look
		(Takes_Draw_Effect), so a refusal is known before the first such object goes without its heat
		vision. */
bool DX11BackendClass::Make_Scene_Colour()
{
	if (SceneColourRefused) {
		return false;
	}
	ID3D11Resource * source = NULL;
	Device->Get_Scene_View()->GetResource(&source);
	D3D11_TEXTURE2D_DESC description;
	static_cast<ID3D11Texture2D *>(source)->GetDesc(&description);
	if (SceneColourCopy != NULL) {
		D3D11_TEXTURE2D_DESC held;
		SceneColourCopy->GetDesc(&held);
		if (held.Width != description.Width || held.Height != description.Height
				|| held.Format != description.Format) {
			Release_Scene_Colour();
		}
	}
	if (SceneColourCopy == NULL) {
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		description.MiscFlags = 0;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.CPUAccessFlags = 0;
		description.SampleDesc.Count = 1;
		description.SampleDesc.Quality = 0;
		// Read the copy the way the scene is written, which also covers a typeless surface.
		D3D11_RENDER_TARGET_VIEW_DESC written;
		Device->Get_Scene_View()->GetDesc(&written);
		D3D11_SHADER_RESOURCE_VIEW_DESC view;
		memset(&view, 0, sizeof(view));
		view.Format = written.Format;
		view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		view.Texture2D.MipLevels = 1;
		if (FAILED(Device->Get_Device()->CreateTexture2D(&description, NULL, &SceneColourCopy))
			|| FAILED(Device->Get_Device()->CreateShaderResourceView(SceneColourCopy, &view,
				&SceneColourCopyView))) {
			Note_Refusal("the device refused a copy of the scene's colour for the cloak");
			Release_Scene_Colour();
			SceneColourRefused = true;
			source->Release();
			return false;
		}
	}
	source->Release();
	return true;
}

/** The scene's colour for the cloak, copied once a frame at the first cloaked draw. */
bool DX11BackendClass::Take_Scene_Colour()
{
	if (!Make_Scene_Colour()) {
		return false;
	}
	if (!SceneColourStale) {
		return true;
	}
	ID3D11Resource * source = NULL;
	Device->Get_Scene_View()->GetResource(&source);
	DX11Resource_Copy_Readable(Device->Get_Context(), SceneColourCopy, source);
	SceneColourStale = false;
	++FrameDepthCopies;		// counted with the depth copies, so DX11 FRAME COST sees it
	source->Release();
	return true;
}

bool DX11BackendClass::Smoke_Glow() const
{
	return SmokeGlow && VertexProgram == ENGINE_SHADER_NONE && PixelProgram == ENGINE_SHADER_NONE
		&& RenderStates.Get_Render_State(D3DRS_LIGHTING) == FALSE
		&& (VertexFormat & D3DFVF_NORMAL) != 0 && (VertexFormat & D3DFVF_XYZRHW) == 0;
}

void DX11BackendClass::Set_Soft_Particles(bool soft)
{
	if (SoftParticles != soft) {
		SoftParticles = soft;
		PipelineChanged = true;
		ConstantsChanged = true;
	}
}

/** Which fade a draw takes.  Only the sorted billboards, which the sorting pool marks, and only
		while the depth being tested against is the scene's own: a mirror or a filter target of another
		size has a depth buffer of its own that nothing here copies.  A multiplicative blend is left
		hard, because faded towards zero it would darken rather than vanish. */
unsigned DX11BackendClass::Soft_Particle() const
{
	if (!SoftParticles || !SoftParticlesAllowed || SceneDepthCopyView == NULL || ShadowMapBound) {
		return SOFT_PARTICLE_NONE;
	}
	if (VertexProgram != ENGINE_SHADER_NONE || PixelProgram != ENGINE_SHADER_NONE
		|| (VertexFormat & D3DFVF_XYZRHW) != 0 || !Camera_Space_Draw()) {
		return SOFT_PARTICLE_NONE;
	}
	if (CurrentTarget != NULL && CurrentDepth != Device->Get_Depth_Stencil_View()) {
		return SOFT_PARTICLE_NONE;
	}
	if (Device->Get_Depth_Texture() == NULL
		|| RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) == FALSE) {
		return SOFT_PARTICLE_NONE;
	}
	const DWORD source = RenderStates.Get_Render_State(D3DRS_SRCBLEND);
	const DWORD destination = RenderStates.Get_Render_State(D3DRS_DESTBLEND);
	if (source == D3DBLEND_DESTCOLOR || source == D3DBLEND_ZERO || destination == D3DBLEND_SRCCOLOR) {
		return SOFT_PARTICLE_NONE;
	}
	return (source == D3DBLEND_ONE) ? SOFT_PARTICLE_COLOUR : SOFT_PARTICLE_ALPHA;
}

void DX11BackendClass::Release_Scene_Depth()
{
	if (SceneDepthCopyView != NULL) {
		SceneDepthCopyView->Release();
		SceneDepthCopyView = NULL;
	}
	if (SceneDepthCopy != NULL) {
		SceneDepthCopy->Release();
		SceneDepthCopy = NULL;
	}
	SceneDepthStale = true;
}

/** The scene's depth as the soft particles read it, taken once at the top of each sorted flush and
		before any of its draws resolves a pipeline: a sorted mesh that writes depth between two
		particle runs would otherwise cost a full copy per run, and a copy the device refuses would be
		found only after a fading program was chosen, which then read nothing and drew nothing.  The
		copy is made at the depth buffer's own size and typeless format, made again when a resize
		changes either, and refreshed only when a draw has written depth since the last one. */
void DX11BackendClass::Take_Scene_Depth()
{
	if (SceneDepthRefused || !SoftParticlesAllowed || Device == NULL) {
		return;
	}
	Resolve_Scene_Depth();
	ID3D11ShaderResourceView * const source_view = Device->Get_Depth_Texture();
	if (source_view == NULL) {
		return;
	}
	ID3D11Resource * source = NULL;
	source_view->GetResource(&source);
	if (source == NULL) {
		return;
	}

	D3D11_TEXTURE2D_DESC description;
	static_cast<ID3D11Texture2D *>(source)->GetDesc(&description);
	if (SceneDepthCopy != NULL) {
		D3D11_TEXTURE2D_DESC held;
		SceneDepthCopy->GetDesc(&held);
		if (held.Width != description.Width || held.Height != description.Height
				|| held.Format != description.Format) {
			Release_Scene_Depth();
		}
	}
	if (SceneDepthCopy == NULL) {
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		description.MiscFlags = 0;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.CPUAccessFlags = 0;
		D3D11_SHADER_RESOURCE_VIEW_DESC view;
		source_view->GetDesc(&view);
		if (FAILED(Device->Get_Device()->CreateTexture2D(&description, NULL, &SceneDepthCopy))
			|| FAILED(Device->Get_Device()->CreateShaderResourceView(SceneDepthCopy, &view,
				&SceneDepthCopyView))) {
			Note_Refusal("the device refused a copy of the scene's depth for the soft particles");
			Release_Scene_Depth();
			SceneDepthRefused = true;
			PipelineChanged = true;
			source->Release();
			return;
		}
		SceneDepthStale = true;
		PipelineChanged = true;		// Soft_Particle reads whether the copy exists
	}
	if (SceneDepthStale) {
		Device->Get_Context()->CopyResource(SceneDepthCopy, source);
		SceneDepthStale = false;
		++FrameDepthCopies;
	}
	source->Release();
}

unsigned DX11BackendClass::Take_Frame_Depth_Copies()
{
	const unsigned copies = FrameDepthCopies;
	FrameDepthCopies = 0;
	return copies;
}

// A triangle over the whole target out of the vertex number, and each pixel's first sample written
// as its depth.  The test is ALWAYS, so the triangle's own depth never decides anything.
static const char DEPTH_RESOLVE_VERTEX_PROGRAM[] =
	"float4 main(uint vertex : SV_VertexID) : SV_Position\n"
	"{\n"
	"    float2 corner = float2((vertex << 1) & 2, vertex & 2);\n"
	"    return float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
	"}\n";

static const char DEPTH_RESOLVE_PIXEL_PROGRAM[] =
	"Texture2DMS<float> SceneDepth : register(t0);\n"
	"float main(float4 position : SV_Position) : SV_Depth\n"
	"{\n"
	"    return SceneDepth.Load(int2(position.xy), 0);\n"
	"}\n";

bool DX11BackendClass::Make_Depth_Resolve()
{
	if (DepthResolvePixelShader != NULL) {
		return true;
	}
	if (DepthResolveRefused) {
		return false;
	}
	ID3D11Device * device = Device->Get_Device();

	D3D11_DEPTH_STENCIL_DESC depth;
	memset(&depth, 0, sizeof(depth));
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_ALWAYS;

	D3D11_RASTERIZER_DESC rasterizer;
	memset(&rasterizer, 0, sizeof(rasterizer));
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;

	// Shader model 5: 4.0 wants the sample count in the texture's type.
	std::vector<unsigned char> vertex_code;
	std::vector<unsigned char> pixel_code;
	const bool made = SUCCEEDED(device->CreateDepthStencilState(&depth, &DepthResolveState))
		&& SUCCEEDED(device->CreateRasterizerState(&rasterizer, &DepthResolveRasterizer))
		&& Compile_Program(DEPTH_RESOLVE_VERTEX_PROGRAM, "depthresolve", VERTEX_PROFILE, vertex_code)
		&& Compile_Program(DEPTH_RESOLVE_PIXEL_PROGRAM, "depthresolve", "ps_5_0", pixel_code)
		&& SUCCEEDED(device->CreateVertexShader(&vertex_code[0], vertex_code.size(), NULL,
			&DepthResolveVertexShader))
		&& SUCCEEDED(device->CreatePixelShader(&pixel_code[0], pixel_code.size(), NULL,
			&DepthResolvePixelShader));
	if (!made) {
		Note_Refusal("the device would not make the pass that resolves the multisampled scene's depth");
		Release_Depth_Resolve();
		DepthResolveRefused = true;
		return false;
	}
	return true;
}

void DX11BackendClass::Release_Depth_Resolve()
{
	IUnknown ** const held[] = {
		(IUnknown **)&DepthResolveVertexShader, (IUnknown **)&DepthResolvePixelShader,
		(IUnknown **)&DepthResolveState, (IUnknown **)&DepthResolveRasterizer,
	};
	for (unsigned index = 0; index < sizeof(held) / sizeof(held[0]); ++index) {
		if (*held[index] != NULL) {
			(*held[index])->Release();
			*held[index] = NULL;
		}
	}
	DepthResolveRefused = false;
	DepthResolveStale = true;
}

void DX11BackendClass::Resolve_Scene_Depth()
{
	if (!DepthResolveStale || !Draws_Into_Scene() || !Device->Scene_Is_Multisampled()
			|| !Make_Depth_Resolve()) {
		return;
	}
	DepthResolveStale = false;

	ID3D11DeviceContext * context = Device->Get_Context();
	// The draws' viewport carries Direct3D 9's half pixel (Set_Viewport); this pass wants each
	// pixel's own sample, so it sets a plain one and puts the draws' back afterwards.
	D3D11_VIEWPORT saved;
	UINT saved_count = 1;
	context->RSGetViewports(&saved_count, &saved);
	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(Device->Get_Width());
	viewport.Height = static_cast<float>(Device->Get_Height());
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	context->OMSetRenderTargets(0, NULL, Device->Get_Depth_Stencil_View());
	context->RSSetViewports(1, &viewport);
	context->RSSetState(DepthResolveRasterizer);
	context->OMSetDepthStencilState(DepthResolveState, 0);
	context->OMSetBlendState(NULL, NULL, 0xffffffff);
	context->IASetInputLayout(NULL);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(DepthResolveVertexShader, NULL, 0);
	context->PSSetShader(DepthResolvePixelShader, NULL, 0);
	ID3D11ShaderResourceView * samples = Device->Get_Multisampled_Depth_Texture();
	context->PSSetShaderResources(0, 1, &samples);
	context->Draw(3, 0);
	// Unbound before the same surface goes back on as the scene's depth.
	samples = NULL;
	context->PSSetShaderResources(0, 1, &samples);

	ID3D11RenderTargetView * target = Device->Get_Scene_View();
	context->OMSetRenderTargets(1, &target, Device->Get_Scene_Depth_View());
	if (saved_count == 1) {
		context->RSSetViewports(1, &saved);
	}
	Forget_Bindings();
	++FrameDepthCopies;
}

void DX11BackendClass::Set_Blast_Lights(const float * lights, unsigned count)
{
	if (count > BLAST_LIGHT_SLOTS) {
		count = BLAST_LIGHT_SLOTS;
	}
	if (count == BlastLightCount
		&& (count == 0 || memcmp(BlastLights, lights, sizeof(float) * BLAST_LIGHT_FLOATS * count) == 0)) {
		return;
	}
	if (count > 0) {
		memcpy(BlastLights, lights, sizeof(float) * BLAST_LIGHT_FLOATS * count);
	}
	BlastLightCount = count;
	ConstantsChanged = true;
}

bool DX11BackendClass::Views_Current_Target(unsigned stage, ID3D11ShaderResourceView * texture) const
{
	if (texture != NULL && texture == SceneStandIn) {
		return true;
	}
	if (texture == NULL || CurrentTarget == NULL) {
		return false;
	}
	// The whole scene is drawn into a target when the screen filters are on, and asking every
	// texture of every draw which resource it views went through the runtime twice a stage: 4% of
	// the fireball frame. The answer only changes with the view or the target, and a new target
	// calls Forget_Bindings, which clears these.
	if (texture != TargetCheckedViews[stage]) {
		ID3D11Resource * resource = NULL;
		texture->GetResource(&resource);
		resource->Release();
		TargetCheckedViews[stage] = texture;
		TargetCheckedIsTarget[stage] = resource == CurrentTargetResource;
	}
	return TargetCheckedIsTarget[stage];
}

bool DX11BackendClass::Samples_Current_Target() const
{
	for (unsigned stage = 0; stage < DX11_BACKEND_TEXTURE_STAGES; ++stage) {
		if (Views_Current_Target(stage, Textures[stage])) {
			return true;
		}
	}
	return false;
}

/** The inverse of a four by four, by cofactors.  Nothing else in the backend needed one: every
		other matrix it holds arrives ready to use, and this is the one journey that goes the other
		way, out of the frame's clip space and back into the world. */
static bool invert(const float in[16], float out[16])
{
	const float a00 = in[0],  a01 = in[1],  a02 = in[2],  a03 = in[3];
	const float a10 = in[4],  a11 = in[5],  a12 = in[6],  a13 = in[7];
	const float a20 = in[8],  a21 = in[9],  a22 = in[10], a23 = in[11];
	const float a30 = in[12], a31 = in[13], a32 = in[14], a33 = in[15];

	const float b00 = a00 * a11 - a01 * a10;
	const float b01 = a00 * a12 - a02 * a10;
	const float b02 = a00 * a13 - a03 * a10;
	const float b03 = a01 * a12 - a02 * a11;
	const float b04 = a01 * a13 - a03 * a11;
	const float b05 = a02 * a13 - a03 * a12;
	const float b06 = a20 * a31 - a21 * a30;
	const float b07 = a20 * a32 - a22 * a30;
	const float b08 = a20 * a33 - a23 * a30;
	const float b09 = a21 * a32 - a22 * a31;
	const float b10 = a21 * a33 - a23 * a31;
	const float b11 = a22 * a33 - a23 * a32;

	const float determinant = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
	if (determinant > -1e-12f && determinant < 1e-12f) {
		return false;
	}
	const float scale = 1.0f / determinant;

	out[0]  = ( a11 * b11 - a12 * b10 + a13 * b09) * scale;
	out[1]  = (-a01 * b11 + a02 * b10 - a03 * b09) * scale;
	out[2]  = ( a31 * b05 - a32 * b04 + a33 * b03) * scale;
	out[3]  = (-a21 * b05 + a22 * b04 - a23 * b03) * scale;
	out[4]  = (-a10 * b11 + a12 * b08 - a13 * b07) * scale;
	out[5]  = ( a00 * b11 - a02 * b08 + a03 * b07) * scale;
	out[6]  = (-a30 * b05 + a32 * b02 - a33 * b01) * scale;
	out[7]  = ( a20 * b05 - a22 * b02 + a23 * b01) * scale;
	out[8]  = ( a10 * b10 - a11 * b08 + a13 * b06) * scale;
	out[9]  = (-a00 * b10 + a01 * b08 - a03 * b06) * scale;
	out[10] = ( a30 * b04 - a31 * b02 + a33 * b00) * scale;
	out[11] = (-a20 * b04 + a21 * b02 - a23 * b00) * scale;
	out[12] = (-a10 * b09 + a11 * b07 - a12 * b06) * scale;
	out[13] = ( a00 * b09 - a01 * b07 + a02 * b06) * scale;
	out[14] = (-a30 * b03 + a31 * b01 - a32 * b00) * scale;
	out[15] = ( a20 * b03 - a21 * b01 + a22 * b00) * scale;
	return true;
}

bool DX11BackendClass::Scene_World_From_Clip(float world_from_clip[16]) const
{
	// The scene camera the shadow pass handed over at the top of the frame, not whatever the last
	// draw left: a camera space particle run leaves an identity view, the interface an orthographic
	// projection.  Every perspective draw of the scene shares the camera's projection.
	// Its projection too, not the one the last draw left: the haze goes on after the water and the
	// translucent meshes, and depth read back through any other projection lands the pixel at
	// another distance along its own ray.
	if (!SceneViewThisFrame || SceneProjection[11] == 0.0f) {
		return false;
	}
	float scene_clip[16];
	multiply(SceneView, SceneProjection, scene_clip);
	return invert(scene_clip, world_from_clip);
}

/** What is in the map, read back once.  A caster pass that drew nothing leaves every texel at the
		clear value, which no draw count tells apart from a pass that drew the whole world. */
std::string DX11BackendClass::Shadow_Map_Report()
{
	if (ShadowMapSurface == NULL) {
		return "no shadow map";
	}

	D3D11_TEXTURE2D_DESC description;
	ShadowMapSurface->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;

	ID3D11Texture2D * staging = NULL;
	if (FAILED(Device->Get_Device()->CreateTexture2D(&description, NULL, &staging))) {
		return "no staging copy of the shadow map";
	}

	Device->Get_Context()->CopyResource(staging, ShadowMapSurface);

	std::string answer = "unreadable shadow map";
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(Device->Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		const unsigned DEPTH_BITS = 24;
		const float FURTHEST = (float)((1u << DEPTH_BITS) - 1);
		unsigned drawn = 0;
		float nearest = 1.0f;
		double sum = 0.0;
		for (unsigned row = 0; row < description.Height; ++row) {
			const unsigned * line = (const unsigned *)((const unsigned char *)mapped.pData
				+ row * mapped.RowPitch);
			for (unsigned column = 0; column < description.Width; ++column) {
				const float depth = (float)(line[column] & 0x00FFFFFF) / FURTHEST;
				if (depth < 1.0f) {
					++drawn;
					sum += depth;
					if (depth < nearest) {
						nearest = depth;
					}
				}
			}
		}
		Device->Get_Context()->Unmap(staging, 0);

		char line[160];
		const unsigned texels = description.Width * description.Height;
		snprintf(line, sizeof(line),
			"%ux%u, %u texels drawn into (%.1f%%), nearest %.4f, mean %.4f",
			description.Width, description.Height, drawn, 100.0f * (float)drawn / (float)texels,
			nearest, (drawn > 0) ? sum / drawn : 1.0);
		answer = line;
	}

	staging->Release();
	return answer;
}

void DX11BackendClass::Set_Render_State(D3DRENDERSTATETYPE state, DWORD value)
{
	// The raw value, not Get_Render_State: that one answers for the caster pass while it runs.
	if (RenderStates.Get_Stored_Render_State(state) != value) {
		RenderStates.Set_Render_State(state, value);
		// Only these reach the constant blocks; see Upload_Vertex_Constants and
		// Upload_Pixel_Constants.  The blend switch is there because the caster pass's alpha
		// reference depends on it.  A blend, depth or cull change used to rebuild and compare both
		// blocks to find the same bytes.
		// The first two groups are read by a constant block and by nothing else: no description and
		// no state object is built from them, so they leave both alone.  Every object sets its own
		// ambient, and a scaled one turns NORMALIZENORMALS, which nothing here reads, on and off
		// around its draws; each of those sent the next draw down the whole Resolve.
		switch (state) {
		case D3DRS_AMBIENT:
		case D3DRS_FOGSTART:
		case D3DRS_FOGEND:
		case D3DRS_FOGDENSITY:
			VertexConstantsChanged = true;
			return;
		case D3DRS_FOGCOLOR:
		case D3DRS_TEXTUREFACTOR:
		case D3DRS_ALPHAREF:
			PixelConstantsChanged = true;
			return;
		case D3DRS_NORMALIZENORMALS:
			return;
		case D3DRS_ALPHABLENDENABLE:
		case D3DRS_SRCBLEND:		// a multiply pass takes no blast light (Upload_Pixel_Constants)
		case D3DRS_DESTBLEND:
			PixelBlendChanged = true;
			break;
		default:
			break;
		}
		PipelineChanged = true;
		StateObjectsChanged = true;
	}
}

void DX11BackendClass::Set_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state,
	DWORD value)
{
	if (stage < DX11_BACKEND_TEXTURE_STAGES && static_cast<unsigned>(state) < DX11_BACKEND_STAGE_STATES
			&& StageStates[stage][state] != value) {
		StageStates[stage][state] = value;
		PipelineChanged = true;
	}
}

void DX11BackendClass::Set_Sampler_State(unsigned sampler, D3DSAMPLERSTATETYPE state, DWORD value)
{
	if (sampler < DX11_BACKEND_TEXTURE_STAGES
			&& Samplers[sampler].Get_Sampler_State(state) != value) {
		Samplers[sampler].Set_Sampler_State(state, value);
		SamplerChanged[sampler] = true;
	}
}

// Only whether a stage has a texture reaches a description; which one is bound is read at bind time.
void DX11BackendClass::Set_Texture(unsigned stage, ID3D11ShaderResourceView * texture)
{
	if (stage < DX11_BACKEND_TEXTURE_STAGES) {
		if ((Textures[stage] == NULL) != (texture == NULL)) {
			PipelineChanged = true;
		}
		// Sampling the current target takes a draw off the shadow receivers (Shadow_Receiving), so a
		// change into or out of that is a change of pipeline.  The old view's answer is in the cache
		// when it was drawn with; when it is not, it is taken as yes.
		else if (texture != Textures[stage] && (CurrentTarget != NULL || texture == SceneStandIn
				|| Textures[stage] == SceneStandIn)) {
			const bool was_target = (TargetCheckedViews[stage] == Textures[stage])
				? TargetCheckedIsTarget[stage] : true;
			if (was_target || Views_Current_Target(stage, texture)) {
				PipelineChanged = true;
			}
		}
		if (texture != Textures[stage]) {
			TexturesChanged = true;
		}
		Textures[stage] = texture;
	}
	else {
		Set_Water_Texture(stage, texture);
	}
}

void DX11BackendClass::Set_Headlights(const float * lights, unsigned count, const float gain[3])
{
	if (count > HEADLIGHT_SLOTS) {
		count = HEADLIGHT_SLOTS;
	}
	if (count == HeadlightCount && memcmp(HeadlightGain, gain, sizeof(HeadlightGain)) == 0
		&& (count == 0 || memcmp(Headlights, lights, sizeof(float) * 8 * count) == 0)) {
		return;
	}
	if (count > 0) {
		memcpy(Headlights, lights, sizeof(float) * 8 * count);
	}
	memcpy(HeadlightGain, gain, sizeof(HeadlightGain));
	HeadlightCount = count;
	ConstantsChanged = true;
}

void DX11BackendClass::Set_Vertex_Format(DWORD fvf)
{
	if (VertexFormat != fvf) {
		VertexFormat = fvf;
		PipelineChanged = true;
	}
}

void DX11BackendClass::Set_Pixel_Program(EngineShaderProgram program, bool bound,
	const char * file_name)
{
	// The wrapper clears both programs on every vertex buffer it applies, so only a different one
	// counts.  A foreign shader is refused before Resolve and is no input to it.
	const EngineShaderProgram resolved = bound ? program : ENGINE_SHADER_NONE;
	if (PixelProgram != resolved) {
		PipelineChanged = true;
	}
	PixelProgram = resolved;
	ForeignPixelShader = bound && program == ENGINE_SHADER_NONE;
	ForeignPixelName = ForeignPixelShader ? file_name : "";
}

void DX11BackendClass::Set_Vertex_Program(EngineShaderProgram program, bool bound,
	const char * file_name)
{
	const EngineShaderProgram resolved = bound ? program : ENGINE_SHADER_NONE;
	if (VertexProgram != resolved) {
		PipelineChanged = true;
	}
	VertexProgram = resolved;
	ForeignVertexShader = bound && program == ENGINE_SHADER_NONE;
	ForeignVertexName = ForeignVertexShader ? file_name : "";
}

// Named by whichever half has no program, and by both when neither does.
void DX11BackendClass::Note_Foreign_Refusal()
{
	std::string which = ForeignVertexShader ? ForeignVertexName : std::string();
	if (ForeignPixelShader) {
		if (!which.empty()) {
			which += " with ";
		}
		which += ForeignPixelName;
	}
	++ForeignRefusals[which];
}

unsigned DX11BackendClass::Foreign_Report_Count() const
{
	return (unsigned)ForeignRefusals.size();
}

const char * DX11BackendClass::Foreign_Report(unsigned index)
{
	std::map<std::string, unsigned long long>::const_iterator entry = ForeignRefusals.begin();
	for (unsigned step = 0; step < index && entry != ForeignRefusals.end(); ++step) {
		++entry;
	}
	if (entry == ForeignRefusals.end()) {
		return "";
	}

	char tail[64];
	snprintf(tail, sizeof(tail), " -> %llu draws", entry->second);
	ReportLine = entry->first + tail;
	return ReportLine.c_str();
}

void DX11BackendClass::Set_Vertex_Program_Constant(unsigned first_register, const float * values,
	unsigned count)
{
	if (first_register + count > ENGINE_SHADER_CONSTANTS) {
		return;
	}
	memcpy(EngineConstants[first_register], values, sizeof(float) * 4 * count);
}

void DX11BackendClass::Set_Stream_Source(ID3D11Buffer * buffer, unsigned stride, unsigned offset)
{
	StreamBuffer = buffer;
	StreamStride = stride;
	StreamOffset = offset;
}

void DX11BackendClass::Set_Indices(ID3D11Buffer * buffer, DXGI_FORMAT format)
{
	IndexBuffer = buffer;
	IndexFormat = format;
}

void DX11BackendClass::Set_Transform(D3DTRANSFORMSTATETYPE state, const float matrix[16])
{
	float * held = NULL;
	switch (state) {
	case D3DTS_WORLD:       held = World; break;
	case D3DTS_VIEW:        held = View; break;
	case D3DTS_PROJECTION:  held = Projection; break;
	default:
		if (state >= D3DTS_TEXTURE0
			&& state < D3DTS_TEXTURE0 + (int)DX11_BACKEND_TEXTURE_STAGES) {
			held = TextureTransforms[state - D3DTS_TEXTURE0];
		}
		break;
	}
	if (held != NULL && memcmp(held, matrix, sizeof(float) * 16) != 0) {
		memcpy(held, matrix, sizeof(float) * 16);
		// The pixel block reads the view and the projection; the world and the texture transforms
		// are the vertex block's alone.
		if (held == View || held == Projection) {
			ConstantsChanged = true;
		} else if (held == World) {
			WorldChanged = true;
		} else {
			VertexConstantsChanged = true;
		}
	}
}

void DX11BackendClass::Set_Material(const float ambient[4], const float diffuse[4],
	const float specular[4], const float emissive[4], float power)
{
	// The wrapper reapplies the material on every mesh whether or not it changed.
	if (memcmp(MaterialAmbient, ambient, sizeof(MaterialAmbient)) == 0
		&& memcmp(MaterialDiffuse, diffuse, sizeof(MaterialDiffuse)) == 0
		&& memcmp(MaterialSpecular, specular, sizeof(MaterialSpecular)) == 0
		&& memcmp(MaterialEmissive, emissive, sizeof(MaterialEmissive)) == 0
		&& memcmp(&MaterialPower, &power, sizeof(MaterialPower)) == 0) {
		return;
	}
	memcpy(MaterialAmbient, ambient, sizeof(MaterialAmbient));
	memcpy(MaterialDiffuse, diffuse, sizeof(MaterialDiffuse));
	memcpy(MaterialSpecular, specular, sizeof(MaterialSpecular));
	memcpy(MaterialEmissive, emissive, sizeof(MaterialEmissive));
	MaterialPower = power;
	VertexConstantsChanged = true;
}

void DX11BackendClass::Set_Light(unsigned index, DWORD type, const float position[4],
	const float direction[4], const float diffuse[4], const float specular[4],
	const float attenuation[4], const float spot[4], const float ambient[4])
{
	if (index >= MAXIMUM_VERTEX_LIGHTS) {
		return;
	}

	// Set_Light_Environment marks all four lights on every object it lights, so most of these
	// arrive unchanged.  Only whether a light is on and its type reach the pipeline description.
	Light & light = Lights[index];
	const bool pipeline_changed = !light.Enabled || light.Type != type;
	if (!pipeline_changed
		&& memcmp(light.Position, position, sizeof(light.Position)) == 0
		&& memcmp(light.Direction, direction, sizeof(light.Direction)) == 0
		&& memcmp(light.Diffuse, diffuse, sizeof(light.Diffuse)) == 0
		&& memcmp(light.Specular, specular, sizeof(light.Specular)) == 0
		&& memcmp(light.Attenuation, attenuation, sizeof(light.Attenuation)) == 0
		&& memcmp(light.Spot, spot, sizeof(light.Spot)) == 0
		&& memcmp(light.Ambient, ambient, sizeof(light.Ambient)) == 0) {
		return;
	}
	light.Enabled = true;
	light.Type = type;
	memcpy(light.Position, position, sizeof(light.Position));
	memcpy(light.Direction, direction, sizeof(light.Direction));
	memcpy(light.Diffuse, diffuse, sizeof(light.Diffuse));
	memcpy(light.Specular, specular, sizeof(light.Specular));
	memcpy(light.Attenuation, attenuation, sizeof(light.Attenuation));
	memcpy(light.Spot, spot, sizeof(light.Spot));
	memcpy(light.Ambient, ambient, sizeof(light.Ambient));
	// The lights are the vertex block's alone; the pixel block's lamps and blasts are its own.
	VertexConstantsChanged = true;
	if (pipeline_changed) {
		PipelineChanged = true;
	}
}

void DX11BackendClass::Disable_Light(unsigned index)
{
	if (index < MAXIMUM_VERTEX_LIGHTS && Lights[index].Enabled) {
		Lights[index].Enabled = false;
		VertexConstantsChanged = true;
		PipelineChanged = true;
	}
}

void DX11BackendClass::Set_Texture_Missing(unsigned stage, bool missing)
{
	if (stage < DX11_BACKEND_TEXTURE_STAGES) {
		MissingTexture[stage] = missing;
	}
}

// A stage only matters while its colour operation is on: the engine leaves textures bound at stages
// it has switched off, and refusing for one of those would refuse most of the frame.
bool DX11BackendClass::Any_Missing_Texture() const
{
	for (unsigned stage = 0; stage < DX11_BACKEND_TEXTURE_STAGES; ++stage) {
		if (StageStates[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			break;
		}
		if (MissingTexture[stage]) {
			return true;
		}
	}
	return false;
}

void DX11BackendClass::Set_Dump_Directory(const char * directory)
{
	DumpDirectory = directory == NULL ? "" : directory;

	// fopen will not make the directory, and a dump that writes nothing looks exactly like a run
	// that built no pipelines.  An existing one comes back as an error and is ignored.
	if (!DumpDirectory.empty()) {
		CreateDirectoryA(DumpDirectory.c_str(), NULL);
	}
}

void DX11BackendClass::Set_Shader_Cache_Path(const char * path)
{
	ShaderCachePath = (path == NULL) ? "" : path;
	if (ShaderCachePath.empty()) {
		return;
	}
	read_program_file(ShaderCachePath.c_str(), CompiledPrograms);
}

void DX11BackendClass::Load_Shipped_Programs(const char * path)
{
	if (path != NULL && path[0] != '\0') {
		ShippedPrograms = read_program_file(path, CompiledPrograms);
	}
}

void DX11BackendClass::Save_Shader_Cache_If_Due()
{
	// Once a frame, so a program whose effect was gone before it landed still reaches the file.
	Take_Off_Frame_Programs();
	if (!ShaderCacheChanged) {
		return;
	}
	const unsigned long now = GetTickCount();
	if (LastShaderCacheSave != 0 && now - LastShaderCacheSave < SHADER_CACHE_SAVE_INTERVAL_MS) {
		return;
	}
	Save_Shader_Cache();
	ShaderCacheChanged = false;
	LastShaderCacheSave = now;
}

void DX11BackendClass::Save_Shader_Cache() const
{
	if (ShaderCachePath.empty()) {
		return;
	}

	// Into a file beside it, then over it: a run killed while writing leaves the previous cache whole.
	const std::string writing = ShaderCachePath + ".writing";
	FILE * file = fopen(writing.c_str(), "wb");
	if (file == NULL) {
		return;
	}

	fwrite(SHADER_CACHE_MAGIC, sizeof(SHADER_CACHE_MAGIC), 1, file);
	for (std::map<unsigned long long, std::vector<unsigned char> >::const_iterator entry
			= CompiledPrograms.begin(); entry != CompiledPrograms.end(); ++entry) {
		const unsigned int size = static_cast<unsigned int>(entry->second.size());
		fwrite(&entry->first, sizeof(entry->first), 1, file);
		fwrite(&size, sizeof(size), 1, file);
		fwrite(&entry->second[0], size, 1, file);
	}
	const bool written = ferror(file) == 0;
	fclose(file);
	if (!written || !MoveFileExA(writing.c_str(), ShaderCachePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileA(writing.c_str());
	}
}

bool DX11BackendClass::Compile_Program(const std::string & source, const char * name,
	const char * profile, std::vector<unsigned char> & bytecode, unsigned flags)
{
	const unsigned long long hash = program_hash(profile, source, flags);
	std::map<unsigned long long, std::vector<unsigned char> >::const_iterator known
		= CompiledPrograms.find(hash);
	if (known != CompiledPrograms.end()) {
		bytecode = known->second;
		return true;
	}

	D3DCompileFunction compiler = compiler_function();
	if (compiler == NULL) {
		return false;
	}

	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	if (FAILED(compiler(source.c_str(), source.size(), name, NULL, NULL, ENTRY_POINT, profile, flags, 0,
			&code, &errors))) {
		Record_Compiler_Error(name, errors);
		return false;
	}
	if (errors != NULL) {
		errors->Release();
	}

	const unsigned char * bytes = static_cast<const unsigned char *>(code->GetBufferPointer());
	bytecode.assign(bytes, bytes + code->GetBufferSize());
	code->Release();

	CompiledPrograms[hash] = bytecode;
	ShaderCacheChanged = true;
	return true;
}

// Jobs waiting at once.  A frame meets a handful of new programs at most; past this a program
// compiles on the spot, as before.
static const size_t COMPILE_QUEUE_LIMIT = 64;

// Two threads, because one left the last of a match's first eight new programs waiting 0.9 s.
static const unsigned COMPILE_THREADS = 2;

// Threads that run D3DCompile, which d3dcompiler_47 allows from any thread.  They never see the
// device, the context or the backend's maps: jobs go in and bytecode comes out under one lock, and
// the render thread moves the bytecode into CompiledPrograms.
struct DX11BackendClass::CompileWorker
{
	struct Job
	{
		unsigned long long Hash;
		std::string Source;
		std::string Name;
		std::string Profile;
		unsigned Flags;
		double Queued;
	};
	struct Done
	{
		unsigned long long Hash;
		bool Compiled;
		std::vector<unsigned char> Bytecode;
		double Milliseconds;
	};

	explicit CompileWorker(D3DCompileFunction compiler)
		: Compiler(compiler), Stopping(false)
	{
		for (unsigned index = 0; index < COMPILE_THREADS; ++index) {
			Threads[index] = std::thread(&CompileWorker::Run, this);
		}
	}

	void Run()
	{
		for (;;) {
			Job job;
			{
				std::unique_lock<std::mutex> hold(Lock);
				Wake.wait(hold, [this] { return Stopping || !Jobs.empty(); });
				if (Stopping) {
					return;
				}
				job = std::move(Jobs.front());
				Jobs.pop_front();
			}
			Done done;
			done.Hash = job.Hash;
			ID3DBlob * code = NULL;
			ID3DBlob * errors = NULL;
			done.Compiled = SUCCEEDED(Compiler(job.Source.c_str(), job.Source.size(), job.Name.c_str(), NULL,
				NULL, ENTRY_POINT, job.Profile.c_str(), job.Flags, 0, &code, &errors));
			if (done.Compiled) {
				const unsigned char * bytes = static_cast<const unsigned char *>(code->GetBufferPointer());
				done.Bytecode.assign(bytes, bytes + code->GetBufferSize());
				code->Release();
			}
			if (errors != NULL) {
				errors->Release();
			}
			done.Milliseconds = DX11Resource_Milliseconds_Now() - job.Queued;
			std::lock_guard<std::mutex> hold(Lock);
			Finished.push_back(std::move(done));
		}
	}

	D3DCompileFunction Compiler;
	std::mutex Lock;
	std::condition_variable Wake;
	std::deque<Job> Jobs;
	std::vector<Done> Finished;
	std::set<unsigned long long> Queued;	// queued, running or finished and not yet taken
	bool Stopping;
	std::thread Threads[COMPILE_THREADS];
};

bool DX11BackendClass::Compiles_Off_Frame() const
{
	if (!CompileOffFrame || VertexProgram != ENGINE_SHADER_NONE || PixelProgram != ENGINE_SHADER_NONE
		|| CurrentTarget != NULL || ShadowMapBound || (VertexFormat & D3DFVF_XYZRHW) != 0) {
		return false;
	}
	// A perspective projection has nought in its corner; Render2D's identity and an orthographic
	// overlay have one, and those are the interface.  Camera-space particles keep the perspective.
	if (Projection[15] != 0.0f) {
		return false;
	}
	if (RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) != FALSE) {
		const DWORD source = RenderStates.Get_Render_State(D3DRS_SRCBLEND);
		const DWORD destination = RenderStates.Get_Render_State(D3DRS_DESTBLEND);
		if (source == D3DBLEND_DESTCOLOR || source == D3DBLEND_ZERO || destination == D3DBLEND_SRCCOLOR) {
			return false;
		}
	}
	return true;
}

bool DX11BackendClass::Queue_Off_Frame(unsigned long long hash, const std::string & source,
	const char * name, const char * profile, unsigned flags)
{
	if (Worker == NULL) {
		D3DCompileFunction compiler = compiler_function();
		if (compiler == NULL) {
			return false;
		}
		Worker = new CompileWorker(compiler);
	}
	std::lock_guard<std::mutex> hold(Worker->Lock);
	if (Worker->Queued.count(hash) != 0) {
		return true;
	}
	if (Worker->Jobs.size() >= COMPILE_QUEUE_LIMIT) {
		return false;
	}
	CompileWorker::Job job = { hash, source, name, profile, flags, DX11Resource_Milliseconds_Now() };
	Worker->Jobs.push_back(std::move(job));
	Worker->Queued.insert(hash);
	Worker->Wake.notify_one();
	return true;
}

void DX11BackendClass::Take_Off_Frame_Programs()
{
	if (Worker == NULL) {
		return;
	}
	std::vector<CompileWorker::Done> finished;
	{
		std::lock_guard<std::mutex> hold(Worker->Lock);
		finished.swap(Worker->Finished);
		for (size_t index = 0; index < finished.size(); ++index) {
			Worker->Queued.erase(finished[index].Hash);
		}
	}
	for (size_t index = 0; index < finished.size(); ++index) {
		CompileWorker::Done & done = finished[index];
		if (!done.Compiled) {
			FailedOffFrame[done.Hash] = true;
			continue;
		}
		CompiledPrograms[done.Hash].swap(done.Bytecode);
		ShaderCacheChanged = true;
		++ProgramsCompiledOffFrame;
		if (done.Milliseconds > LongestOffFrameMilliseconds) {
			LongestOffFrameMilliseconds = done.Milliseconds;
		}
	}
}

void DX11BackendClass::Stop_Compile_Worker()
{
	if (Worker == NULL) {
		return;
	}
	{
		std::lock_guard<std::mutex> hold(Worker->Lock);
		Worker->Stopping = true;
	}
	Worker->Wake.notify_all();
	// At most the compile each thread is in the middle of; the jobs behind them are dropped and
	// queued again by the next draw that wants them.
	for (unsigned index = 0; index < COMPILE_THREADS; ++index) {
		Worker->Threads[index].join();
	}
	Take_Off_Frame_Programs();
	delete Worker;
	Worker = NULL;
	PendingPipelines.clear();
}

bool DX11BackendClass::Compile_Pixel_Program(const std::string & source, const char * name,
	std::vector<unsigned char> & bytecode)
{
	return Compile_Program(source, name, PIXEL_PROFILE, bytecode);
}

// The key has characters a file name cannot carry, so it becomes the file's first line and the
// name is a number.  Both programs of one pipeline go in one file, in the order they run.
void DX11BackendClass::Dump_Program(const std::string & key, const std::string & vertex_hlsl,
	const std::string & pixel_hlsl)
{
	if (DumpDirectory.empty()) {
		return;
	}

	char path[512];
	snprintf(path, sizeof(path), "%s/pipeline%03u.hlsl", DumpDirectory.c_str(), PipelinesBuilt);
	FILE * file = fopen(path, "wb");
	if (file == NULL) {
		return;
	}

	fprintf(file, "// state: %s\n\n// ---- vertex ----\n%s\n// ---- pixel ----\n%s\n",
		key.c_str(), vertex_hlsl.c_str(), pixel_hlsl.c_str());
	fclose(file);
}

void DX11BackendClass::Begin_Scene()
{
	SceneDepthStale = true;
	SceneColourStale = true;
	Water_Scene_Changed(NULL);

	// A scene can begin with a render target already set: the water's reflection and the shadow
	// projector both set theirs and then call WW3D::Begin_Render, and forcing the back buffer here
	// sent the reflection scene, and its clear, over the picture that was already drawn.
	if (CurrentTarget != NULL) {
		return;
	}

	ID3D11RenderTargetView * target = Device->Get_Scene_View();
	if (target == NULL) {
		return;
	}

	Device->Get_Context()->OMSetRenderTargets(1, &target, Device->Get_Scene_Depth_View());
	Forget_Bindings();
	Set_Viewport(0, 0, Device->Get_Width(), Device->Get_Height());
}

ID3D11DepthStencilView * DX11BackendClass::Depth_For(unsigned width, unsigned height)
{
	// A target the size of the back buffer shares the back buffer's depth, because that is what
	// the Direct3D 9 device does: the screen filters redirect the scene into a texture and hand
	// SetRenderTarget the depth surface the frame's Begin_Render already cleared.  Made separately
	// here, nothing ever cleared it, every triangle in the scene failed the depth test, and the
	// texture the composite sampled held its clear colour and nothing else - a black world under a
	// live command bar, which is what the bloom looked like for two sessions.
	if (width == Device->Get_Width() && height == Device->Get_Height()) {
		return Device->Get_Depth_Stencil_View();
	}

	const unsigned long long key = (static_cast<unsigned long long>(width) << 32) | height;
	std::map<unsigned long long, ID3D11DepthStencilView *>::const_iterator existing
		= TargetDepths.find(key);
	if (existing != TargetDepths.end()) {
		return existing->second;
	}

	ID3D11Texture2D * texture = NULL;
	ID3D11ShaderResourceView * unused = NULL;
	if (!DX11Resource_Create_Texture(Device->Get_Device(), width, height, 1, D3DFMT_D24S8,
			D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL, &texture, &unused)) {
		Note_Refusal("the device refused a depth buffer for a render target");
		TargetDepths[key] = NULL;
		return NULL;
	}

	ID3D11DepthStencilView * view = NULL;
	if (FAILED(Device->Get_Device()->CreateDepthStencilView(texture, NULL, &view))) {
		Note_Refusal("the device refused a depth view for a render target");
		view = NULL;
	}
	else {
		// A new depth texture holds whatever the driver left in it, which reads as everything being
		// nearer than anything about to be drawn.  Nothing else clears this one: the engine's own
		// clear goes to whichever surface Direct3D 9 has bound, and this surface has no D3D9 twin.
		Device->Get_Context()->ClearDepthStencilView(view,
			D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}
	texture->Release();

	TargetDepths[key] = view;
	return view;
}

// The last few target changes rather than the first few: the first are the loading screen's, and
// the question is which target the scene itself is drawn into.
void DX11BackendClass::Trace_Target(const char * what, unsigned width, unsigned height)
{
	char line[80];
	if (width == 0) {
		snprintf(line, sizeof(line), "%s after %llu draws", what, DrawsMade);
	}
	else {
		snprintf(line, sizeof(line), "%s %ux%u after %llu draws", what, width, height, DrawsMade);
	}
	TargetTrace.push_back(line);
	if (TargetTrace.size() > TARGET_TRACE_LIMIT) {
		TargetTrace.erase(TargetTrace.begin());
	}
}

void DX11BackendClass::Set_Render_Target(ID3D11RenderTargetView * target)
{
	if (target == NULL) {
		++TargetsRestored;
		Trace_Target("back buffer", 0, 0);
		CurrentTarget = NULL;
		CurrentDepth = NULL;
		ID3D11RenderTargetView * back_buffer = Device->Get_Scene_View();
		if (back_buffer != NULL) {
			Device->Get_Context()->OMSetRenderTargets(1, &back_buffer,
				Device->Get_Scene_Depth_View());
			Forget_Bindings();
			Set_Viewport(0, 0, Device->Get_Width(), Device->Get_Height());
		}
		return;
	}

	ID3D11Resource * resource = NULL;
	target->GetResource(&resource);
	if (resource == NULL) {
		return;
	}

	unsigned width = 0;
	unsigned height = 0;
	ID3D11Texture2D * texture = NULL;
	if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&texture))) {
		D3D11_TEXTURE2D_DESC description;
		texture->GetDesc(&description);
		width = description.Width;
		height = description.Height;
		texture->Release();
	}
	resource->Release();

	if (width == 0 || height == 0) {
		return;
	}

	++TargetsBound;
	Trace_Target("texture", width, height);
	CurrentTarget = target;
	CurrentTargetResource = resource;
	CurrentDepth = Depth_For(width, height);
	Device->Get_Context()->OMSetRenderTargets(1, &CurrentTarget, CurrentDepth);
	Forget_Bindings();
	Set_Viewport(0, 0, width, height);
}

void DX11BackendClass::Set_Viewport(unsigned x, unsigned y, unsigned width, unsigned height)
{
	// Half a pixel to the right and down, which is the whole of the difference between where
	// Direct3D 9 draws a triangle and where Direct3D 11 draws the same one.  D3D9 tests coverage at
	// a pixel's integer coordinate and D3D10 onwards at the half-integer, so an edge that D3D9 puts
	// at screen x 32.25 covers column 32 there and column 31 here.  Every account of this is written
	// about screen space quads and texel alignment; it is the projected geometry that carries the
	// error into a whole frame, and moving the viewport is the only place that catches the generated
	// programs and the transcribed .vso shaders in one edit.
	//
	// test_pixelcentre draws that edge on both runtimes in one binary and reads back the column.
	// Take this out and it says 32 against 31.
	const float PIXEL_CENTRE_OFFSET = 0.5f;
	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = static_cast<float>(x) + PIXEL_CENTRE_OFFSET;
	viewport.TopLeftY = static_cast<float>(y) + PIXEL_CENTRE_OFFSET;
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	Device->Get_Context()->RSSetViewports(1, &viewport);

	ViewportX = x;
	ViewportY = y;
	ViewportWidth = (width == 0) ? 1 : width;
	ViewportHeight = (height == 0) ? 1 : height;
	ConstantsChanged = true;
}

void DX11BackendClass::Clear(bool colour, bool depth, const float colour_value[4])
{
	// Whatever the draws are landing in is what a clear clears; a pass that clears its own render
	// target and then draws into it would otherwise clear the picture instead.
	ID3D11RenderTargetView * target = (CurrentTarget != NULL)
		? CurrentTarget : Device->Get_Scene_View();
	ID3D11DepthStencilView * depth_view = Scene_Depth_Target();

	if (colour && target != NULL) {
		Device->Get_Context()->ClearRenderTargetView(target, colour_value);
		ID3D11Resource * cleared = CurrentTargetResource;
		if (CurrentTarget == NULL) {
			target->GetResource(&cleared);
			cleared->Release();	// the device holds the scene target for as long as it lives
		}
		Water_Scene_Changed(cleared);
	}
	if (depth && depth_view != NULL) {
		Device->Get_Context()->ClearDepthStencilView(depth_view,
			D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		if (depth_view != Device->Get_Depth_Stencil_View() && CurrentTarget == NULL) {
			// The single-sample depth too: a screen filter binds it for its own texture after this
			// clear (Depth_For), and it still holds the last frame's resolve and stencil.
			Device->Get_Context()->ClearDepthStencilView(Device->Get_Depth_Stencil_View(),
				D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		}
		SceneDepthStale = true;
		DepthResolveStale = DepthResolveStale || CurrentTarget == NULL;
	}
}

bool DX11BackendClass::Build_Combiner_Description(CombinerDescription & description) const
{
	memset(&description, 0, sizeof(description));

	// The alpha test and the fog are pipeline state under D3D9 and shader instructions here, so
	// they are read off the render states and handed to the generator as part of the program.
	description.PixelPipeline.AlphaTestEnabled =
		RenderStates.Get_Render_State(D3DRS_ALPHATESTENABLE) != FALSE;
	description.PixelPipeline.AlphaFunction = RenderStates.Get_Render_State(D3DRS_ALPHAFUNC);
	description.PixelPipeline.FogEnabled = RenderStates.Get_Render_State(D3DRS_FOGENABLE) != FALSE;
	description.PixelPipeline.AlphaToCoverage = Alpha_To_Coverage();
	description.SpecularAdd = RenderStates.Get_Render_State(D3DRS_SPECULARENABLE) != FALSE;

	description.StageCount = 0;
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		if (StageStates[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			break;
		}

		CombinerStage & target = description.Stages[stage];
		target.ColourOperation = StageStates[stage][D3DTSS_COLOROP];
		target.ColourArgument0 = StageStates[stage][D3DTSS_COLORARG0];
		target.ColourArgument1 = StageStates[stage][D3DTSS_COLORARG1];
		target.ColourArgument2 = StageStates[stage][D3DTSS_COLORARG2];
		target.AlphaOperation = StageStates[stage][D3DTSS_ALPHAOP];
		target.AlphaArgument0 = StageStates[stage][D3DTSS_ALPHAARG0];
		target.AlphaArgument1 = StageStates[stage][D3DTSS_ALPHAARG1];
		target.AlphaArgument2 = StageStates[stage][D3DTSS_ALPHAARG2];
		target.TextureCoordinateIndex = StageStates[stage][D3DTSS_TEXCOORDINDEX];
		target.TextureBound = Textures[stage] != NULL;
		description.StageCount = stage + 1;
	}
	description.ShadowReceiving = description.StageCount > 0 && Shadow_Receiving();
	description.SmokeGlow = Smoke_Glow();
	description.SoftParticle = Soft_Particle();
	description.DrawEffect = Draw_Effect();
	description.FireLook = Fire_Look();
	description.TeamGlow = Team_Glow();
	return description.StageCount > 0;
}

/** Which draws take a shadow.  The world's, and only while the sun's map holds this frame's casters:
		a screen space quad has no place in the world to look up, a draw that is adding light to the
		frame rather than painting it would come out darker rather than shadowed, and the pass that
		fills the map must not shadow itself. */
bool DX11BackendClass::Shadow_Receiving() const
{
	if (!ShadowReceiving || ShadowMapBound || ShadowMapTexture == NULL) {
		return false;
	}
	if (Emissive) {
		return false;		// a fireball's body blended over the frame: it gives light, it takes no shade
	}
	// A transcribed terrain or road program takes one too: the ground is where a shadow is seen,
	// and those programs carry the sampling whether a frame has a map or not.  The water and the
	// trees keep their own shadows for now.
	if (PixelProgram != ENGINE_SHADER_NONE && !EngineShader_Paints_Ground(PixelProgram)) {
		return false;
	}
	if (VertexProgram != ENGINE_SHADER_NONE && PixelProgram == ENGINE_SHADER_NONE) {
		return false;
	}
	if ((VertexFormat & D3DFVF_XYZRHW) != 0) {
		return false;		// already in screen space: the interface, the filters, the darkening quad
	}
	if (Samples_Current_Target()) {
		return false;		// the heat haze: a copy of a picture that took its shadows already
	}
	if (RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) != FALSE) {
		const DWORD source = RenderStates.Get_Render_State(D3DRS_SRCBLEND);
		const DWORD destination = RenderStates.Get_Render_State(D3DRS_DESTBLEND);
		if (destination == D3DBLEND_ONE || source == D3DBLEND_ONE
			|| destination == D3DBLEND_DESTCOLOR || source == D3DBLEND_DESTCOLOR
			|| destination == D3DBLEND_SRCCOLOR) {
			return false;	// additive and multiplicative passes: fire, glow, the shadows themselves
		}
		if (destination == D3DBLEND_INVSRCCOLOR) {
			return false;	// the toxin field's stain: its colour is light taken away, and shaded it ran backwards
		}
	}
	return true;
}

bool DX11BackendClass::Build_Vertex_Description(VertexPipelineDescription & description) const
{
	memset(&description, 0, sizeof(description));
	description.FVF = VertexFormat;
	description.LightingEnabled = RenderStates.Get_Render_State(D3DRS_LIGHTING) != FALSE;
	description.SpecularEnabled = RenderStates.Get_Render_State(D3DRS_SPECULARENABLE) != FALSE;
	description.LocalViewer = RenderStates.Get_Render_State(D3DRS_LOCALVIEWER) != FALSE;
	description.ColourVertexEnabled = RenderStates.Get_Render_State(D3DRS_COLORVERTEX) != FALSE;
	description.DiffuseMaterialSource = RenderStates.Get_Render_State(D3DRS_DIFFUSEMATERIALSOURCE);
	description.AmbientMaterialSource = RenderStates.Get_Render_State(D3DRS_AMBIENTMATERIALSOURCE);
	description.EmissiveMaterialSource = RenderStates.Get_Render_State(D3DRS_EMISSIVEMATERIALSOURCE);
	description.SpecularMaterialSource = RenderStates.Get_Render_State(D3DRS_SPECULARMATERIALSOURCE);

	// The lights have to be contiguous in the shader, so an enabled light after a disabled one
	// moves down.  Which register a light ends up in is the shader's business and the constants
	// are packed to match below.
	description.LightCount = 0;
	for (unsigned index = 0; index < MAXIMUM_VERTEX_LIGHTS; ++index) {
		if (Lights[index].Enabled) {
			description.Lights[description.LightCount].Type = Lights[index].Type;
			++description.LightCount;
		}
	}

	// With a pixel shader bound the colour operations mean nothing - the shader replaces the
	// combiners and reads whatever coordinate sets it likes - so the walk cannot stop at a
	// disabled one.  The water leaves stages one to three disabled and samples all four.
	const bool every_stage = PixelProgram != ENGINE_SHADER_NONE;

	description.StageCount = 0;
	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		if (!every_stage && StageStates[stage][D3DTSS_COLOROP] == D3DTOP_DISABLE) {
			break;
		}
		description.Stages[stage].TextureCoordinateIndex = StageStates[stage][D3DTSS_TEXCOORDINDEX];
		description.Stages[stage].TextureTransformFlags =
			StageStates[stage][D3DTSS_TEXTURETRANSFORMFLAGS];
		description.StageCount = stage + 1;
	}

	description.FogEnabled = RenderStates.Get_Render_State(D3DRS_FOGENABLE) != FALSE;
	description.FogVertexMode = RenderStates.Get_Render_State(D3DRS_FOGVERTEXMODE);
	description.SmokeGlow = Smoke_Glow();
	description.SoftParticle = Soft_Particle() != SOFT_PARTICLE_NONE;
	description.FireLook = Fire_Look() != FIRE_LOOK_NONE;
	// SoftParticlesAllowed carries the Classic setting, and the sun's caster pass writes depth alone.
	description.HemiAmbient = HemiAmbient && SoftParticlesAllowed && !ShadowMapBound
		&& description.LightingEnabled && (VertexFormat & D3DFVF_XYZRHW) == 0;
	return true;
}

bool DX11BackendClass::Resolve(Pipeline & pipeline)
{
	// Nothing either description is built from has been set since the last draw resolved.  Alpha to
	// coverage is read off the target as well as the states, and a target change sets nothing.
	if (!PipelineChanged && LastResolveHeld
		&& Memos[LastMemo].Combiner.PixelPipeline.AlphaToCoverage == Alpha_To_Coverage()) {
		ResolveMemo & memo = Memos[LastMemo];
		pipeline = memo.Resolved;
		++memo.Use->Draws;
		return true;
	}
	PipelineChanged = false;
	LastResolveHeld = false;

	VertexPipelineDescription vertex_description;
	CombinerDescription combiner_description;
	const bool built_combiners = Build_Combiner_Description(combiner_description);
	if (!Build_Vertex_Description(vertex_description)
		|| (!built_combiners && PixelProgram == ENGINE_SHADER_NONE)) {
		// Stage zero's colour operation is disabled, so there is no texture stage to write a
		// program from.  A transcribed pixel program does not need one: it is the combiners.  There
		// is no key yet at this point.
		++RefusedNoStage;
		return false;
	}

	// Nothing below this line runs for a draw that asks for a pipeline one of the last few draws
	// did, which is nearly all of them: the renderer walks its texture categories, so a category's
	// meshes arrive in a row, and sorted particles go back and forth between a few pipelines.  See
	// ResolveMemo in the header for what building the key costs and why memcmp is safe here.
	for (unsigned probe = 0; probe < RESOLVE_MEMO_ENTRIES; ++probe) {
		const unsigned entry = (LastMemo + probe) % RESOLVE_MEMO_ENTRIES;
		ResolveMemo & memo = Memos[entry];
		if (memo.Valid
			&& memo.Format == VertexFormat
			&& memo.VertexProgram == VertexProgram
			&& memo.PixelProgram == PixelProgram
			&& memcmp(&memo.Vertex, &vertex_description, sizeof(vertex_description)) == 0
			&& memcmp(&memo.Combiner, &combiner_description, sizeof(combiner_description)) == 0) {
			pipeline = memo.Resolved;
			++memo.Use->Draws;
			LastMemo = entry;
			LastResolveHeld = true;
			return true;
		}
	}

	ResolveKey resolve_key;
	memset(&resolve_key, 0, sizeof(resolve_key));
	resolve_key.Format = VertexFormat;
	resolve_key.VertexProgram = VertexProgram;
	resolve_key.PixelProgram = PixelProgram;
	memcpy(&resolve_key.Vertex, &vertex_description, sizeof(vertex_description));
	memcpy(&resolve_key.Combiner, &combiner_description, sizeof(combiner_description));
	ResolutionMap::iterator resolved = Resolutions.find(resolve_key);
	if (resolved != Resolutions.end()) {
		if (resolved->second.Refused) {
			Refuse(resolved->second.Key, static_cast<RefusalReason>(resolved->second.Reason));
			return false;
		}
		pipeline = resolved->second.Resolved;
		++resolved->second.Use->Draws;
		Remember_Resolution(resolved->second.Use, pipeline, vertex_description, combiner_description);
		return true;
	}

	char format[32];
	snprintf(format, sizeof(format), "|%lu", VertexFormat);

	// A transcribed program is the whole half it replaces: the fixed-function description says
	// nothing about it and two draws with the same shader and different stage state are the same
	// program, so its name replaces that description in the key rather than joining it.  The alpha
	// test and the fog stay in the pixel one, because they are still written into the program.
	const std::string vertex_key = (VertexProgram != ENGINE_SHADER_NONE)
		? std::string(EngineShader_Name(VertexProgram))
		: VertexShader_Key(vertex_description);
	const std::string pixel_key = (PixelProgram != ENGINE_SHADER_NONE)
		? EngineShader_Name(PixelProgram)
			+ CombinerShader_Pipeline_Key(combiner_description.PixelPipeline)
		: CombinerShader_Key(combiner_description);
	const std::string key = vertex_key + pixel_key + format;

	std::map<std::string, Pipeline>::const_iterator existing = Pipelines.find(key);
	if (existing != Pipelines.end()) {
		pipeline = existing->second;
		PipelineUse * const use = Record_Use(key);
		const Resolution resolution = { false, 0, pipeline, use, key };
		Resolutions[resolve_key] = resolution;
		Remember_Resolution(use, pipeline, vertex_description, combiner_description);
		return true;
	}
	std::map<std::string, unsigned>::const_iterator refused = RefusedPipelines.find(key);
	if (refused != RefusedPipelines.end()) {
		// A refusal is kept once it has been seen here, not where it is first made: the paths
		// below refuse in several places and the next draw of the same state lands here anyway.
		const Pipeline none = { NULL, NULL, NULL };
		const Resolution resolution = { true, refused->second, none, NULL, key };
		Resolutions[resolve_key] = resolution;
		Refuse(key, static_cast<RefusalReason>(refused->second));
		return false;
	}
	// The same key drawn where it may not wait (into a texture, say) compiles on the spot below.
	const bool off_frame = Compiles_Off_Frame();
	if (off_frame && !PendingPipelines.empty()) {
		std::map<std::string, std::pair<unsigned long long, unsigned long long> >::iterator pending
			= PendingPipelines.find(key);
		if (pending != PendingPipelines.end()) {
			Take_Off_Frame_Programs();
			if (!Program_Settled(pending->second.first) || !Program_Settled(pending->second.second)) {
				++DrawsWaitedForProgram;
				return false;
			}
			PendingPipelines.erase(pending);
		}
	}

	const double build_started = DX11Resource_Milliseconds_Now();
	std::string vertex_hlsl;
	std::string pixel_hlsl;
	const bool wrote_vertex = (VertexProgram != ENGINE_SHADER_NONE)
		? EngineShader_Vertex_Program(VertexProgram, vertex_hlsl)
		: VertexShader_Generate(vertex_description, VERTEX_SHADER_TARGET_D3D11, vertex_hlsl);
	if (!wrote_vertex) {
		Note_Refusal("the vertex half would not generate this description");
		Refuse(key, REFUSED_NO_PROGRAM);
		return false;
	}
	const bool wrote_pixel = (PixelProgram != ENGINE_SHADER_NONE)
		? EngineShader_Pixel_Program(PixelProgram, combiner_description.PixelPipeline, pixel_hlsl)
		: CombinerShader_Generate(combiner_description, COMBINER_SHADER_TARGET_D3D11, pixel_hlsl);
	if (!wrote_pixel) {
		Note_Refusal("the pixel half would not generate this description");
		Refuse(key, REFUSED_NO_PROGRAM);
		return false;
	}

	Dump_Program(key, vertex_hlsl, pixel_hlsl);

	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned element_count = 0;
	unsigned stride = 0;
	if (!DX11Layout_From_FVF(VertexFormat, elements, element_count, stride)) {
		Refuse(key, REFUSED_NO_LAYOUT);
		return false;
	}

	std::vector<unsigned char> vertex_code;
	std::vector<unsigned char> pixel_code;
	// The Modern water writes a dynamically indexed array, which shader model 4 refuses; every other
	// program keeps the profile and flags its cached and shipped bytecode was compiled with.
	const bool modern_water = EngineShader_Is_Modern_Water(PixelProgram);
	const char * const pixel_profile = modern_water ? "ps_5_0" : PIXEL_PROFILE;
	const unsigned pixel_flags = modern_water ? DX11_MODERN_WATER_COMPILE_FLAGS : 0;
	// A scene draw of a generated program neither cache holds waits for the worker (the header has
	// which draws do not).  A full queue compiles here, as before.
	if (off_frame) {
		const unsigned long long vertex_hash = program_hash(VERTEX_PROFILE, vertex_hlsl, 0);
		const unsigned long long pixel_hash = program_hash(pixel_profile, pixel_hlsl, pixel_flags);
		const bool vertex_missing = !Program_Settled(vertex_hash);
		const bool pixel_missing = !Program_Settled(pixel_hash);
		if ((vertex_missing || pixel_missing)
			&& (!vertex_missing || Queue_Off_Frame(vertex_hash, vertex_hlsl, "ffvertex", VERTEX_PROFILE, 0))
			&& (!pixel_missing || Queue_Off_Frame(pixel_hash, pixel_hlsl, "ffshader", pixel_profile, pixel_flags))) {
			PendingPipelines[key] = std::make_pair(vertex_hash, pixel_hash);
			++DrawsWaitedForProgram;
			return false;
		}
	}
	if (!Compile_Program(vertex_hlsl, "ffvertex", VERTEX_PROFILE, vertex_code)
		|| !Compile_Program(pixel_hlsl, "ffshader", pixel_profile, pixel_code, pixel_flags)) {
		Refuse(key, REFUSED_NO_PROGRAM);
		return false;
	}

	Pipeline built;
	built.VertexShader = NULL;
	built.PixelShader = NULL;
	built.Layout = NULL;

	ID3D11Device * device = Device->Get_Device();
	const bool created = SUCCEEDED(device->CreateVertexShader(&vertex_code[0], vertex_code.size(),
			NULL, &built.VertexShader))
		&& SUCCEEDED(device->CreatePixelShader(&pixel_code[0], pixel_code.size(), NULL,
			&built.PixelShader))
		&& SUCCEEDED(device->CreateInputLayout(elements, element_count, &vertex_code[0],
			vertex_code.size(), &built.Layout));

	if (!created) {
		if (built.Layout != NULL) {
			built.Layout->Release();
		}
		if (built.PixelShader != NULL) {
			built.PixelShader->Release();
		}
		if (built.VertexShader != NULL) {
			built.VertexShader->Release();
		}
		Note_Refusal("the device would not make the shaders or the input layout");
		Refuse(key, REFUSED_NO_OBJECT);
		return false;
	}

	Pipelines[key] = built;
	++PipelinesBuilt;
	FrameBuildMilliseconds += DX11Resource_Milliseconds_Now() - build_started;
	++FrameBuildCount;
	pipeline = built;
	PipelineUse * const use = Record_Use(key);
	const Resolution resolution = { false, 0, pipeline, use, key };
	Resolutions[resolve_key] = resolution;
	Remember_Resolution(use, pipeline, vertex_description, combiner_description);
	return true;
}

size_t DX11BackendClass::ResolveKey_Hash::operator()(const ResolveKey & key) const
{
	// FNV-1a a word at a time; the key is all four byte fields and zeroed padding.
	static_assert(sizeof(ResolveKey) % sizeof(unsigned) == 0, "the key hashes whole words");
	unsigned words[sizeof(ResolveKey) / sizeof(unsigned)];
	memcpy(words, &key, sizeof(words));
	unsigned long long hash = 14695981039346656037ULL;
	for (unsigned index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
		hash = (hash ^ words[index]) * 1099511628211ULL;
	}
	return static_cast<size_t>(hash);
}

void DX11BackendClass::Remember_Resolution(PipelineUse * use, const Pipeline & resolved,
	const VertexPipelineDescription & vertex, const CombinerDescription & combiner)
{
	ResolveMemo & memo = Memos[NextMemo];
	memo.Valid = true;
	memo.Vertex = vertex;
	memo.Combiner = combiner;
	memo.Format = VertexFormat;
	memo.VertexProgram = VertexProgram;
	memo.PixelProgram = PixelProgram;
	memo.Resolved = resolved;
	memo.Use = use;
	LastResolveHeld = true;
	LastMemo = NextMemo;
	NextMemo = (NextMemo + 1) % RESOLVE_MEMO_ENTRIES;
}

DX11BackendClass::PipelineUse * DX11BackendClass::Record_Use(const std::string & key)
{
	PipelineUse & use = PipelineUses[key];
	if (use.Draws++ != 0) {
		return &use;
	}

	use.TextureWidth = 0;
	use.TextureHeight = 0;
	if (Textures[0] == NULL) {
		return &use;
	}

	ID3D11Resource * resource = NULL;
	Textures[0]->GetResource(&resource);
	if (resource == NULL) {
		return &use;
	}

	ID3D11Texture2D * texture = NULL;
	if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&texture))) {
		D3D11_TEXTURE2D_DESC description;
		texture->GetDesc(&description);
		use.TextureWidth = description.Width;
		use.TextureHeight = description.Height;
		texture->Release();
	}
	resource->Release();
	return &use;
}

unsigned DX11BackendClass::Pipeline_Report_Count() const
{
	return (unsigned)PipelineUses.size();
}

const char * DX11BackendClass::Pipeline_Report(unsigned index)
{
	std::map<std::string, PipelineUse>::const_iterator entry = PipelineUses.begin();
	for (unsigned step = 0; step < index && entry != PipelineUses.end(); ++step) {
		++entry;
	}
	if (entry == PipelineUses.end()) {
		return "";
	}

	char tail[96];
	snprintf(tail, sizeof(tail), " -> %llu draws, stage 0 texture %ux%u",
		entry->second.Draws, entry->second.TextureWidth, entry->second.TextureHeight);
	ReportLine = entry->first + tail;
	return ReportLine.c_str();
}

void DX11BackendClass::Upload_Constants()
{
	// A transcribed program reads the engine's own register bank instead of the block below, so
	// that bank goes up only on the draws that take one.
	if (VertexProgram != ENGINE_SHADER_NONE
		&& (!EngineConstantsHeld
			|| memcmp(HeldEngineConstants, EngineConstants, sizeof(EngineConstants)) != 0)
		&& Write_Constants(EngineConstantRing, EngineConstants, sizeof(EngineConstants))) {
		memcpy(HeldEngineConstants, EngineConstants, sizeof(EngineConstants));
		EngineConstantsHeld = true;
	}

	// Each block is built only when something it is built from has been set since its buffer was
	// last written.
	const bool vertex_due = ConstantsChanged || VertexConstantsChanged || !VertexConstantsHeld;
	const bool world_due = !vertex_due && WorldChanged;
	bool pixel_due = ConstantsChanged || PixelConstantsChanged || !PixelConstantsHeld;
	// A blend change rebuilt the pixel block on every sorted run, and nearly always to the same
	// bytes: the block reads the blend only through Pixel_Blend_Key.
	if (!pixel_due && PixelBlendChanged) {
		pixel_due = Pixel_Blend_Key() != HeldPixelBlendKey;
	}
	ConstantsChanged = false;
	VertexConstantsChanged = false;
	PixelConstantsChanged = false;
	PixelBlendChanged = false;
	WorldChanged = false;
	if (vertex_due) {
		Upload_Vertex_Constants();
	} else if (world_due) {
		Upload_World_Constants();
	}
	if (pixel_due) {
		Upload_Pixel_Constants();
	}
}

// What the pixel block takes from the blend, and from the vertex format and the programs, which raise
// no flag of their own: the look Draw_Effect picks, the multiply test that keeps the blast lights off
// a pass multiplying what is under it, whether the program reads the scene's depth, and the alpha
// reference, which the sun's caster pass answers by the blend switch.  The same key is the same block.
unsigned DX11BackendClass::Pixel_Blend_Key() const
{
	return Draw_Effect() | (Multiplies_Below() ? 0x100u : 0u)
		| (EngineShader_Reads_Scene_Depth(PixelProgram) ? 0x200u : 0u)
		| ((RenderStates.Get_Render_State(D3DRS_ALPHAREF) & 0xffu) << 10);
}

bool DX11BackendClass::Multiplies_Below() const
{
	return RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) != FALSE
		&& (RenderStates.Get_Render_State(D3DRS_SRCBLEND) == D3DBLEND_ZERO
			|| RenderStates.Get_Render_State(D3DRS_DESTBLEND) == D3DBLEND_SRCCOLOR);
}

void DX11BackendClass::Upload_Vertex_Constants()
{
	VertexConstantBlock vertex_block;
	memset(&vertex_block, 0, sizeof(vertex_block));

	float world_view[16];
	multiply(World, View, world_view);
	multiply(world_view, Projection, vertex_block.WorldViewProjection);
	memcpy(vertex_block.WorldView, world_view, sizeof(world_view));
	inverse_transpose(world_view, vertex_block.NormalTransform);
	vertex_block.ViewportInverse[0] = 1.0f / static_cast<float>(ViewportWidth);
	vertex_block.ViewportInverse[1] = 1.0f / static_cast<float>(ViewportHeight);

	memcpy(vertex_block.TextureMatrix, TextureTransforms, sizeof(vertex_block.TextureMatrix));

	memcpy(vertex_block.MaterialAmbient, MaterialAmbient, sizeof(MaterialAmbient));
	memcpy(vertex_block.MaterialDiffuse, MaterialDiffuse, sizeof(MaterialDiffuse));
	memcpy(vertex_block.MaterialSpecular, MaterialSpecular, sizeof(MaterialSpecular));
	memcpy(vertex_block.MaterialEmissive, MaterialEmissive, sizeof(MaterialEmissive));
	vertex_block.MaterialPower[0] = MaterialPower;
	if (HemiAmbient) {
		// The world's up through the view, for the HemiAmbient programs.  Without the option these
		// stay nought, so the block's bytes are what they always were.
		memcpy(&vertex_block.MaterialPower[1], &View[8], sizeof(float) * 3);
	}

	const DWORD ambient = RenderStates.Get_Render_State(D3DRS_AMBIENT);
	vertex_block.GlobalAmbient[0] = static_cast<float>((ambient >> 16) & 0xff) / 255.0f;
	vertex_block.GlobalAmbient[1] = static_cast<float>((ambient >> 8) & 0xff) / 255.0f;
	vertex_block.GlobalAmbient[2] = static_cast<float>(ambient & 0xff) / 255.0f;
	vertex_block.GlobalAmbient[3] = static_cast<float>((ambient >> 24) & 0xff) / 255.0f;

	// The fog start, end and density, in the order the generated shader reads them.  D3D9 carries
	// all three as floats inside a DWORD render state.
	const DWORD fog_start = RenderStates.Get_Render_State(D3DRS_FOGSTART);
	const DWORD fog_end = RenderStates.Get_Render_State(D3DRS_FOGEND);
	const DWORD fog_density = RenderStates.Get_Render_State(D3DRS_FOGDENSITY);
	memcpy(&vertex_block.FogParameters[0], &fog_start, sizeof(float));
	memcpy(&vertex_block.FogParameters[1], &fog_end, sizeof(float));
	memcpy(&vertex_block.FogParameters[2], &fog_density, sizeof(float));

	// Packed the way the shader declares them, and only the enabled ones, in order: the shader was
	// generated from the same walk.
	unsigned slot = 0;
	for (unsigned index = 0; index < MAXIMUM_VERTEX_LIGHTS; ++index) {
		if (!Lights[index].Enabled) {
			continue;
		}
		transform_point(Lights[index].Position, View, vertex_block.LightFields[slot][0]);
		transform_direction(Lights[index].Direction, View, vertex_block.LightFields[slot][1]);
		memcpy(vertex_block.LightFields[slot][2], Lights[index].Diffuse, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][3], Lights[index].Specular, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][4], Lights[index].Attenuation, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][5], Lights[index].Spot, sizeof(float) * 4);
		memcpy(vertex_block.LightFields[slot][6], Lights[index].Ambient, sizeof(float) * 4);
		++slot;
	}

	// See HeldVertexConstants in the header: the buffer is only written through when the bytes
	// going into it differ from the bytes already there.
	// A failed Map leaves the buffer unknown, which also keeps the next draw off the early return.
	if (!VertexConstantsHeld
			|| memcmp(&HeldVertexConstants, &vertex_block, sizeof(vertex_block)) != 0) {
		VertexConstantsHeld = false;
		if (Write_Constants(VertexConstantRing, &vertex_block, sizeof(vertex_block))) {
			HeldVertexConstants = vertex_block;
			VertexConstantsHeld = true;
		}
	}
}

// Everything but the world came from the same inputs as the held block (only WorldChanged is up),
// so the held block with its three world products made again is what Upload_Vertex_Constants would
// build, byte for byte, without its light transforms, colour conversions and kilobyte compare.
void DX11BackendClass::Upload_World_Constants()
{
	float world[3][16];
	multiply(World, View, world[1]);
	multiply(world[1], Projection, world[0]);
	inverse_transpose(world[1], world[2]);
	// Set back to the matrices it holds, the block would be the one already in the buffer.
	if (memcmp(HeldVertexConstants.WorldViewProjection, world, sizeof(world)) == 0) {
		return;
	}
	static_assert(offsetof(VertexConstantBlock, WorldView) == sizeof(float) * 16
		&& offsetof(VertexConstantBlock, NormalTransform) == sizeof(float) * 32,
		"the three world products lead the block");
	memcpy(HeldVertexConstants.WorldViewProjection, world, sizeof(world));
	VertexConstantsHeld = Write_Constants(VertexConstantRing, &HeldVertexConstants,
		sizeof(HeldVertexConstants));
}

void DX11BackendClass::Upload_Pixel_Constants()
{
	// The held block is this one once the compare or the write below is done, or it is not held.
	HeldPixelBlendKey = Pixel_Blend_Key();
	PixelConstantBlock pixel_block;
	memset(&pixel_block, 0, sizeof(pixel_block));
	RenderStates.Get_Texture_Factor(pixel_block.TextureFactor);

	const DWORD fog_colour = RenderStates.Get_Render_State(D3DRS_FOGCOLOR);
	pixel_block.FogColour[0] = static_cast<float>((fog_colour >> 16) & 0xff) / 255.0f;
	pixel_block.FogColour[1] = static_cast<float>((fog_colour >> 8) & 0xff) / 255.0f;
	pixel_block.FogColour[2] = static_cast<float>(fog_colour & 0xff) / 255.0f;
	pixel_block.FogColour[3] = static_cast<float>((fog_colour >> 24) & 0xff) / 255.0f;

	// D3DRS_ALPHAREF is an eight bit level and it goes up as one, not as a fraction: the generated
	// clip rounds the pixel's alpha to a level first, because that is the comparison Direct3D 9 made
	// and comparing fractions instead moves every alpha tested edge by up to half a level.
	pixel_block.AlphaReference[0] =
		static_cast<float>(RenderStates.Get_Render_State(D3DRS_ALPHAREF) & 0xff);

	// A camera space draw's pixels go back to the world through the scene camera's view, not through
	// the identity it was drawn with; see Set_Scene_View in the header.
	const bool camera_space = Camera_Space_Draw();
	const float * const world_to_camera = (camera_space && SceneViewKnown) ? SceneView : View;

	// A look takes its pixel back to the world the same way, with or without a sun's map, and so
	// does a program that reads the scene's depth (the foam water).
	const unsigned effect = Draw_Effect();
	const bool reads_depth = EngineShader_Reads_Scene_Depth(PixelProgram);
	if (ShadowReceiving || effect != DRAW_EFFECT_NONE || reads_depth) {
		if (!ShadowFromClipValid
			|| memcmp(ShadowFromClipView, world_to_camera, sizeof(View)) != 0
			|| memcmp(ShadowFromClipProjection, Projection, sizeof(Projection)) != 0) {
			float scene_clip[16];
			float clip_to_world[16];
			multiply(world_to_camera, Projection, scene_clip);
			if (invert(scene_clip, clip_to_world)) {
				multiply(clip_to_world, SunViewProjection, ShadowFromClip);
				memcpy(WorldFromClip, clip_to_world, sizeof(WorldFromClip));
				memcpy(ShadowFromClipView, world_to_camera, sizeof(View));
				memcpy(ShadowFromClipProjection, Projection, sizeof(Projection));
				ShadowFromClipValid = true;
			}
		}
	}
	if (effect != DRAW_EFFECT_NONE || reads_depth) {
		pixel_block.ShadowViewport[0] = (ViewportWidth > 0)
			? 1.0f / static_cast<float>(ViewportWidth) : 0.0f;
		pixel_block.ShadowViewport[1] = (ViewportHeight > 0)
			? 1.0f / static_cast<float>(ViewportHeight) : 0.0f;
		memcpy(pixel_block.WorldFromClip, WorldFromClip, sizeof(pixel_block.WorldFromClip));
		memcpy(pixel_block.DrawEffectParameters, DrawEffectNumbers, sizeof(DrawEffectNumbers));
	}

	if (ShadowReceiving) {
		pixel_block.VolumeParameters[0] = SmokeMapFilled ? SmokeStrength : 0.0f;
		pixel_block.VolumeParameters[1] = SmokeMapFilled ? SMOKE_SELF_SHADOW_GAIN : 0.0f;
		pixel_block.VolumeParameters[2] = camera_space ? 1.0f : 0.0f;
		pixel_block.VolumeParameters[3] = SMOKE_SELF_SHADOW_CURVE;
		memcpy(pixel_block.ShadowFromClip, ShadowFromClip, sizeof(pixel_block.ShadowFromClip));
		pixel_block.ShadowParameters[0] = (ShadowMapSize > 0)
			? 1.0f / static_cast<float>(ShadowMapSize) : 0.0f;
		pixel_block.ShadowParameters[1] = ShadowBias;
		pixel_block.ShadowParameters[2] = ShadowStrength;
		pixel_block.ShadowParameters[3] = ShadowRadius;
		pixel_block.ShadowViewport[0] = (ViewportWidth > 0)
			? 1.0f / static_cast<float>(ViewportWidth) : 0.0f;
		pixel_block.ShadowViewport[1] = (ViewportHeight > 0)
			? 1.0f / static_cast<float>(ViewportHeight) : 0.0f;
		pixel_block.ShadowSoftness[0] = ShadowNarrowestRadius;
		pixel_block.ShadowSoftness[1] = ShadowTexelsPerGap;
		pixel_block.ShadowSoftness[2] = ShadowUnitsPerDepth;
		pixel_block.ShadowSoftness[3] = ShadowSkyFill;
		memcpy(pixel_block.WorldFromClip, WorldFromClip, sizeof(pixel_block.WorldFromClip));
		pixel_block.HeadlightParameters[0] = static_cast<float>(HeadlightCount);
		memcpy(&pixel_block.HeadlightParameters[1], HeadlightGain, sizeof(HeadlightGain));
		for (unsigned slot = 0; slot < HeadlightCount; ++slot) {
			memcpy(pixel_block.HeadlightPosition[slot], &Headlights[slot][0], sizeof(float) * 4);
			memcpy(pixel_block.HeadlightDirection[slot], &Headlights[slot][4], sizeof(float) * 4);
		}
		// None for a particle: the smoke takes these lights on the CPU (BLAST_LIGHT_SAMPLING).  None
		// for a pass that multiplies what is under it (ZERO, SRCCOLOR): the base pass took the light
		// already, and gained again the surface came out lit twice.
		const unsigned blasts = (camera_space || Multiplies_Below()) ? 0 : BlastLightCount;
		pixel_block.BlastLightParameters[0] = static_cast<float>(blasts);
		for (unsigned slot = 0; slot < blasts; ++slot) {
			memcpy(pixel_block.BlastLightPosition[slot], &BlastLights[slot][0], sizeof(float) * 4);
			memcpy(pixel_block.BlastLightDiffuse[slot], &BlastLights[slot][4], sizeof(float) * 4);
			memcpy(pixel_block.BlastLightAmbient[slot], &BlastLights[slot][8], sizeof(float) * 4);
		}
	}

	// The particles' projection, the one the world behind them was drawn with: the distance from a
	// depth is SoftParticleDepth.x / (depth * .y - .z) (SOFT_PARTICLE_SAMPLING).  Written only around
	// the sorted billboards, so every other draw keeps the bytes it had.
	if (SoftParticles && camera_space) {
		pixel_block.SoftParticleDepth[0] = Projection[11] * Projection[14];
		pixel_block.SoftParticleDepth[1] = Projection[11];
		pixel_block.SoftParticleDepth[2] = Projection[10];
		pixel_block.SoftParticleDepth[3] = 1.0f / SOFT_PARTICLE_FADE_UNITS;
	}

	if (!PixelConstantsHeld
			|| memcmp(&HeldPixelConstants, &pixel_block, sizeof(pixel_block)) != 0) {
		if (Rebind_Recent_Pixel_Constants(pixel_block)) {
			return;
		}
		PixelConstantsHeld = false;
		if (Write_Constants(PixelConstantRing, &pixel_block, sizeof(pixel_block))) {
			HeldPixelConstants = pixel_block;
			PixelConstantsHeld = true;
			RecentPixelConstants[NextRecentPixel] = pixel_block;
			RecentPixelOffset[NextRecentPixel] = PixelConstantRing.Current;
			RecentPixelDiscards[NextRecentPixel] = PixelConstantRing.Discards;
			NextRecentPixel = (NextRecentPixel + 1) % RECENT_PIXEL_BLOCKS;
		}
	}
}

// A block one of the last few writes left in the ring, in the buffer as it is now, bound again where
// it lies: the draws read the same bytes from an older entry.  The tail first, where a billboard
// run's block and a streak's differ, so a block that is not there costs a short compare.
bool DX11BackendClass::Rebind_Recent_Pixel_Constants(const PixelConstantBlock & block)
{
	const size_t tail = offsetof(PixelConstantBlock, SoftParticleDepth);
	const unsigned char * const wanted = reinterpret_cast<const unsigned char *>(&block);
	for (unsigned index = 0; index < RECENT_PIXEL_BLOCKS; ++index) {
		const unsigned char * const recent =
			reinterpret_cast<const unsigned char *>(&RecentPixelConstants[index]);
		if (RecentPixelDiscards[index] == PixelConstantRing.Discards
			&& memcmp(recent + tail, wanted + tail, sizeof(block) - tail) == 0
			&& memcmp(recent, wanted, tail) == 0) {
			PixelConstantRing.Current = RecentPixelOffset[index];
			HeldPixelConstants = block;
			PixelConstantsHeld = true;
			return true;
		}
	}
	return false;
}

void DX11BackendClass::Forget_Last_State_Objects()
{
	memset(&LastBlendDescription, 0, sizeof(LastBlendDescription));
	memset(&LastDepthStencilDescription, 0, sizeof(LastDepthStencilDescription));
	memset(&LastRasterizerDescription, 0, sizeof(LastRasterizerDescription));
	memset(LastSamplerDescriptions, 0, sizeof(LastSamplerDescriptions));
	LastBlendState = NULL;
	LastBlendGlow = false;
	LastBlendCoverage = false;
	LastDepthStencilState = NULL;
	LastRasterizerState = NULL;
	memset(LastSamplerStates, 0, sizeof(LastSamplerStates));
	// A released state object's address can come back as a new one, so what is bound goes too.
	Forget_Bindings();
}

bool DX11BackendClass::Alpha_To_Coverage() const
{
	const unsigned function = RenderStates.Get_Render_State(D3DRS_ALPHAFUNC);
	return Device->Scene_Is_Multisampled() && Draws_Into_Scene() && SoftParticlesAllowed
		&& RenderStates.Get_Render_State(D3DRS_ALPHATESTENABLE) != FALSE
		&& RenderStates.Get_Render_State(D3DRS_ALPHABLENDENABLE) == FALSE
		&& (function == D3DCMP_GREATER || function == D3DCMP_GREATEREQUAL);
}

ID3D11BlendState * DX11BackendClass::Blend_State(bool glow, bool coverage)
{
	D3D11_BLEND_DESC description;
	RenderStates.Build_Blend_Description(description);
	description.AlphaToCoverageEnable = coverage ? TRUE : FALSE;
	if (glow) {
		// The source term becomes the blend factor, which Bind_State_Objects passes as the gain.
		// A float target takes a factor past one unclamped; that is the whole of the boost.
		description.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
		description.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_BLEND_FACTOR;
	}
	if (LastBlendState != NULL
		&& memcmp(&LastBlendDescription, &description, sizeof(description)) == 0) {
		return LastBlendState;
	}

	BlendStateMap::const_iterator existing = BlendStates.find(description);
	if (existing != BlendStates.end()) {
		LastBlendDescription = description;
		LastBlendState = existing->second;
		return existing->second;
	}

	ID3D11BlendState * state = NULL;
	if (FAILED(Device->Get_Device()->CreateBlendState(&description, &state))) {
		// A null blend state is not a refused draw: the draw goes ahead with the default state,
		// which blends nothing, so a pass that was multiplying itself into the frame buffer paints
		// over it instead.  That is invisible in every count, hence the line.
		Note_Refusal("the device refused a blend state");
		return NULL;
	}
	BlendStates[description] = state;
	LastBlendDescription = description;
	LastBlendState = state;
	return state;
}

ID3D11DepthStencilState * DX11BackendClass::Depth_Stencil_State()
{
	D3D11_DEPTH_STENCIL_DESC description;
	RenderStates.Build_Depth_Stencil_Description(description);
	if (LastDepthStencilState != NULL
		&& memcmp(&LastDepthStencilDescription, &description, sizeof(description)) == 0) {
		return LastDepthStencilState;
	}

	DepthStencilStateMap::const_iterator existing = DepthStencilStates.find(description);
	if (existing != DepthStencilStates.end()) {
		LastDepthStencilDescription = description;
		LastDepthStencilState = existing->second;
		return existing->second;
	}

	ID3D11DepthStencilState * state = NULL;
	if (FAILED(Device->Get_Device()->CreateDepthStencilState(&description, &state))) {
		Note_Refusal("the device refused a depth stencil state");
		return NULL;
	}
	DepthStencilStates[description] = state;
	LastDepthStencilDescription = description;
	LastDepthStencilState = state;
	return state;
}

ID3D11RasterizerState * DX11BackendClass::Rasterizer_State()
{
	D3D11_RASTERIZER_DESC description;
	RenderStates.Build_Rasterizer_Description(description);
	if (LastRasterizerState != NULL
		&& memcmp(&LastRasterizerDescription, &description, sizeof(description)) == 0) {
		return LastRasterizerState;
	}

	RasterizerStateMap::const_iterator existing = RasterizerStates.find(description);
	if (existing != RasterizerStates.end()) {
		LastRasterizerDescription = description;
		LastRasterizerState = existing->second;
		return existing->second;
	}

	ID3D11RasterizerState * state = NULL;
	if (FAILED(Device->Get_Device()->CreateRasterizerState(&description, &state))) {
		Note_Refusal("the device refused a rasterizer state");
		return NULL;
	}
	RasterizerStates[description] = state;
	LastRasterizerDescription = description;
	LastRasterizerState = state;
	return state;
}

ID3D11SamplerState * DX11BackendClass::Sampler_State(unsigned sampler)
{
	// Nothing set on this stage since its object was last looked up.  Forget_Last_State_Objects
	// nulls the object, which sends the next draw down the whole path.
	if (!SamplerChanged[sampler] && LastSamplerStates[sampler] != NULL) {
		return LastSamplerStates[sampler];
	}
	SamplerChanged[sampler] = false;

	D3D11_SAMPLER_DESC description;
	Samplers[sampler].Build_Sampler_Description(description);
	if (LastSamplerStates[sampler] != NULL
		&& memcmp(&LastSamplerDescriptions[sampler], &description, sizeof(description)) == 0) {
		return LastSamplerStates[sampler];
	}

	SamplerStateMap::const_iterator existing = SamplerStates.find(description);
	if (existing != SamplerStates.end()) {
		LastSamplerDescriptions[sampler] = description;
		LastSamplerStates[sampler] = existing->second;
		return existing->second;
	}

	ID3D11SamplerState * state = NULL;
	if (FAILED(Device->Get_Device()->CreateSamplerState(&description, &state))) {
		Note_Refusal("the device refused a sampler state");
		SamplerChanged[sampler] = true;
		return NULL;
	}
	SamplerStates[description] = state;
	LastSamplerDescriptions[sampler] = description;
	LastSamplerStates[sampler] = state;
	return state;
}

const DX11BackendClass::ConstantRing & DX11BackendClass::Vertex_Constants() const
{
	return (VertexProgram != ENGINE_SHADER_NONE) ? EngineConstantRing : VertexConstantRing;
}

void DX11BackendClass::Forget_Bindings()
{
	memset(&Bound, 0, sizeof(Bound));
	memset(TargetCheckedViews, 0, sizeof(TargetCheckedViews));
	memset(TargetCheckedIsTarget, 0, sizeof(TargetCheckedIsTarget));
	// Whether a draw samples its own target is part of its pipeline (Shadow_Receiving), and every
	// target change comes through here.
	PipelineChanged = true;
}

void DX11BackendClass::Bind_State_Objects()
{
	ID3D11DeviceContext * context = Device->Get_Context();
	const bool known = Bound.Known;

	// The memos stand for what the render states would build while none of them has changed; a
	// lookup that came back null keeps the flag up, so the next draw asks again as it always did.
	ID3D11BlendState * blend = LastBlendState;
	ID3D11DepthStencilState * depth_stencil = LastDepthStencilState;
	ID3D11RasterizerState * rasterizer = LastRasterizerState;
	// Whether the glow applies hangs on the target and the vertex format as well as the render
	// states, and neither of those raises StateObjectsChanged, so it is asked every draw.
	const bool glow = Additive_Glow();
	const bool coverage = Alpha_To_Coverage();
	if (StateObjectsChanged || glow != LastBlendGlow || coverage != LastBlendCoverage || blend == NULL
			|| depth_stencil == NULL || rasterizer == NULL) {
		blend = Blend_State(glow, coverage);
		LastBlendGlow = glow;
		LastBlendCoverage = coverage;
		depth_stencil = Depth_Stencil_State();
		rasterizer = Rasterizer_State();
		StateObjectsChanged = blend == NULL || depth_stencil == NULL || rasterizer == NULL;
	}

	if (!known || blend != Bound.Blend) {
		const float gain[4] = { AdditiveGain, AdditiveGain, AdditiveGain, 1.0f };
		context->OMSetBlendState(blend, glow ? gain : NULL, 0xffffffff);
		Bound.Blend = blend;
	}
	const UINT stencil_reference = RenderStates.Get_Stencil_Reference();
	if (!known || depth_stencil != Bound.DepthStencil || stencil_reference != Bound.StencilReference) {
		context->OMSetDepthStencilState(depth_stencil, stencil_reference);
		Bound.DepthStencil = depth_stencil;
		Bound.StencilReference = stencil_reference;
	}
	if (!known || rasterizer != Bound.Rasterizer) {
		context->RSSetState(rasterizer);
		Bound.Rasterizer = rasterizer;
	}

	// The stages are what the last draw bound unless a texture or a sampler state was set since, or
	// that draw read a copy of the target, which has to be taken again for this one.
	bool stages_changed = !known || TexturesChanged;
	for (unsigned sampler = 0; sampler < DX11_BACKEND_TEXTURE_STAGES && !stages_changed; ++sampler) {
		stages_changed = SamplerChanged[sampler];
	}
	if (stages_changed) {
		TexturesChanged = false;
		ID3D11SamplerState * samplers[DX11_BACKEND_TEXTURE_STAGES];
		ID3D11ShaderResourceView * textures[DX11_BACKEND_TEXTURE_STAGES];
		for (unsigned sampler = 0; sampler < DX11_BACKEND_TEXTURE_STAGES; ++sampler) {
			samplers[sampler] = Sampler_State(sampler);
			textures[sampler] = Readable_Texture(sampler, Textures[sampler]);
			if (textures[sampler] != Textures[sampler]) {
				TexturesChanged = true;
			}
		}
		if (!known || memcmp(samplers, Bound.Samplers, sizeof(samplers)) != 0) {
			context->PSSetSamplers(0, DX11_BACKEND_TEXTURE_STAGES, samplers);
			memcpy(Bound.Samplers, samplers, sizeof(samplers));
		}
		if (!known || memcmp(textures, Bound.Textures, sizeof(textures)) != 0) {
			context->PSSetShaderResources(0, DX11_BACKEND_TEXTURE_STAGES, textures);
			memcpy(Bound.Textures, textures, sizeof(textures));
		}
	}

	// The sun's map at t5 with a sampler of its own at s5, clamped so a pixel past the edge of the
	// box reads the edge rather than wrapping the far side of the map over it.
	if (ShadowReceiving && ShadowMapTexture != NULL) {
		if (ShadowMapSampler == NULL) {
			D3D11_SAMPLER_DESC description;
			memset(&description, 0, sizeof(description));
			description.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			description.ComparisonFunc = D3D11_COMPARISON_NEVER;
			description.MaxLOD = D3D11_FLOAT32_MAX;
			Device->Get_Device()->CreateSamplerState(&description, &ShadowMapSampler);
		}
		// The smoke's map beside it at t6 and s6, or nothing on a frame without smoke, which the
		// program never reads: its strength is zero then.
		// Both were bound again on every receiving draw, which is most of the frame.
		ID3D11ShaderResourceView * const maps[2] = { ShadowMapTexture, Smoke_Map() };
		if (!known || maps[0] != Bound.Maps[0] || maps[1] != Bound.Maps[1]) {
			context->PSSetShaderResources(DX11_BACKEND_TEXTURE_STAGES + 1, 2, maps);
			Bound.Maps[0] = maps[0];
			Bound.Maps[1] = maps[1];
		}
		if (ShadowMapSampler != NULL) {
			ID3D11SamplerState * const samplers_of_maps[2] = { ShadowMapSampler, SmokeMapSampler };
			if (!known || samplers_of_maps[0] != Bound.MapSamplers[0]
					|| samplers_of_maps[1] != Bound.MapSamplers[1]) {
				context->PSSetSamplers(DX11_BACKEND_TEXTURE_STAGES + 1,
					(SmokeMapSampler != NULL) ? 2 : 1, samplers_of_maps);
				Bound.MapSamplers[0] = samplers_of_maps[0];
				Bound.MapSamplers[1] = samplers_of_maps[1];
			}
		}
	}

	// The scene's depth at t7 for a soft particle, read with Load, so it wants no sampler.  The copy
	// was taken at the top of the sorted flush (Take_Scene_Depth) and is refreshed in place.
	if (SoftParticles && Soft_Particle() != SOFT_PARTICLE_NONE) {
		ID3D11ShaderResourceView * const depth = SceneDepthCopyView;
		if (!known || depth != Bound.SceneDepth) {
			context->PSSetShaderResources(DX11_BACKEND_TEXTURE_STAGES + 3, 1, &depth);
			Bound.SceneDepth = depth;
		}
	}
	// The foam water reads the same copy, which W3DWater takes before it draws.  Into another
	// target's depth it reads none, and a null view reads zero, which the program takes as deep.
	if (EngineShader_Reads_Scene_Depth(PixelProgram)) {
		ID3D11ShaderResourceView * const depth =
			(CurrentTarget == NULL || CurrentDepth == Device->Get_Depth_Stencil_View())
			? SceneDepthCopyView : NULL;
		if (!known || depth != Bound.SceneDepth) {
			context->PSSetShaderResources(DX11_BACKEND_TEXTURE_STAGES + 3, 1, &depth);
			Bound.SceneDepth = depth;
		}
	}

	// The scene's colour at t8 for the cloak, read with Load.  Set_Draw_Effect took the copy.
	if (Draw_Effect() == DRAW_EFFECT_CLOAK) {
		ID3D11ShaderResourceView * const colour = SceneColourCopyView;
		if (!known || colour != Bound.SceneColour) {
			context->PSSetShaderResources(DX11_BACKEND_TEXTURE_STAGES + 4, 1, &colour);
			Bound.SceneColour = colour;
		}
	}
}

void DX11BackendClass::Bind_Pipeline(const Pipeline & pipeline, ID3D11Buffer * vertices, UINT stride,
	UINT offset, ID3D11Buffer * indices, DXGI_FORMAT index_format, D3D11_PRIMITIVE_TOPOLOGY topology)
{
	ID3D11DeviceContext * context = Device->Get_Context();
	const bool known = Bound.Known;

	if (!known || pipeline.Layout != Bound.Layout) {
		context->IASetInputLayout(pipeline.Layout);
		Bound.Layout = pipeline.Layout;
	}
	if (!known || vertices != Bound.VertexBuffer || stride != Bound.VertexStride
			|| offset != Bound.VertexOffset) {
		context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
		Bound.VertexBuffer = vertices;
		Bound.VertexStride = stride;
		Bound.VertexOffset = offset;
	}
	// A draw with no index buffer leaves the last one bound, as it always did.
	if (indices != NULL && (!known || indices != Bound.IndexBuffer || index_format != Bound.IndexFormat)) {
		context->IASetIndexBuffer(indices, index_format, 0);
		Bound.IndexBuffer = indices;
		Bound.IndexFormat = index_format;
	}
	if (!known || topology != Bound.Topology) {
		context->IASetPrimitiveTopology(topology);
		Bound.Topology = topology;
	}
	if (!known || pipeline.VertexShader != Bound.VertexShader) {
		context->VSSetShader(pipeline.VertexShader, NULL, 0);
		Bound.VertexShader = pipeline.VertexShader;
	}
	// Each block at the entry its last write went to, which a draw that wrote nothing still reads.
	// The window is the entry: a program declares at most the block, which the entry holds.
	const ConstantRing & vertex_constants = Vertex_Constants();
	if (!known || vertex_constants.Buffer != Bound.VertexConstants
			|| vertex_constants.Current != Bound.VertexConstantsOffset) {
		if (OffsetContext != NULL) {
			const UINT first = vertex_constants.Current / CONSTANT_BYTES;
			const UINT count = vertex_constants.EntryBytes / CONSTANT_BYTES;
			OffsetContext->VSSetConstantBuffers1(0, 1, &vertex_constants.Buffer, &first, &count);
		} else {
			context->VSSetConstantBuffers(0, 1, &vertex_constants.Buffer);
		}
		Bound.VertexConstants = vertex_constants.Buffer;
		Bound.VertexConstantsOffset = vertex_constants.Current;
	}
	if (!known || pipeline.PixelShader != Bound.PixelShader) {
		context->PSSetShader(pipeline.PixelShader, NULL, 0);
		Bound.PixelShader = pipeline.PixelShader;
	}
	if (!known || PixelConstantRing.Buffer != Bound.PixelConstants
			|| PixelConstantRing.Current != Bound.PixelConstantsOffset) {
		if (OffsetContext != NULL) {
			const UINT first = PixelConstantRing.Current / CONSTANT_BYTES;
			const UINT count = PixelConstantRing.EntryBytes / CONSTANT_BYTES;
			OffsetContext->PSSetConstantBuffers1(0, 1, &PixelConstantRing.Buffer, &first, &count);
		} else {
			context->PSSetConstantBuffers(0, 1, &PixelConstantRing.Buffer);
		}
		Bound.PixelConstants = PixelConstantRing.Buffer;
		Bound.PixelConstantsOffset = PixelConstantRing.Current;
	}
	Bound.Known = true;
}

ID3D11ShaderResourceView * DX11BackendClass::Readable_Texture(unsigned stage,
	ID3D11ShaderResourceView * texture)
{
	if (!Views_Current_Target(stage, texture)) {
		return texture;
	}
	ID3D11Resource * resource = CurrentTargetResource;
	if (texture == SceneStandIn) {
		Device->Get_Scene_View()->GetResource(&resource);
		resource->Release();	// the device holds the scene target for as long as it lives
	}

	// Set_Render_Target keeps only a target that is a two dimensional texture.
	D3D11_TEXTURE2D_DESC description;
	static_cast<ID3D11Texture2D *>(resource)->GetDesc(&description);
	if (TargetCopy != NULL) {
		D3D11_TEXTURE2D_DESC held;
		TargetCopy->GetDesc(&held);
		if (held.Width != description.Width || held.Height != description.Height
				|| held.Format != description.Format) {
			TargetCopyView->Release();
			TargetCopy->Release();
			TargetCopyView = NULL;
			TargetCopy = NULL;
		}
	}

	if (TargetCopy == NULL) {
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		description.MiscFlags = 0;
		// The multisampled scene is resolved into it.
		description.SampleDesc.Count = 1;
		description.SampleDesc.Quality = 0;
		if (FAILED(Device->Get_Device()->CreateTexture2D(&description, NULL, &TargetCopy))) {
			Note_Refusal("the device refused a copy of the render target a draw samples");
			TargetCopy = NULL;
			return texture;
		}
		if (FAILED(Device->Get_Device()->CreateShaderResourceView(TargetCopy, NULL, &TargetCopyView))) {
			Note_Refusal("the device refused a view of the render target's copy");
			TargetCopy->Release();
			TargetCopy = NULL;
			return texture;
		}
	}

	DX11Resource_Copy_Readable(Device->Get_Context(), TargetCopy, resource);
	return TargetCopyView;
}

bool DX11BackendClass::Draw_Indexed_Triangles(unsigned index_count, unsigned start_index,
	unsigned base_vertex)
{
	return Draw_Indexed(index_count, start_index, base_vertex,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

bool DX11BackendClass::Draw_Indexed_Strip(unsigned index_count, unsigned start_index,
	unsigned base_vertex)
{
	return Draw_Indexed(index_count, start_index, base_vertex,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
}

bool DX11BackendClass::Draw_Indexed(unsigned index_count, unsigned start_index,
	unsigned base_vertex, D3D11_PRIMITIVE_TOPOLOGY topology)
{
	Pipeline pipeline;
	if (StreamBuffer == NULL || IndexBuffer == NULL) {
		++RefusedNoBuffer;
		++DrawsRefused;
		return false;
	}
	if (ForeignPixelShader || ForeignVertexShader) {
		Note_Foreign_Refusal();
		++RefusedForeignShader;
		++DrawsRefused;
		return false;
	}
	if (Any_Missing_Texture()) {
		++RefusedNoTexture;
		++DrawsRefused;
		return false;
	}
	if (!Resolve(pipeline)) {
		++DrawsRefused;
		return false;
	}
	// The sun's map has no water in it, and a water program there only writes its depth.
	const bool water = EngineShader_Is_Modern_Water(PixelProgram) && !ShadowMapBound;
	if (water && !Prepare_Water()) {
		++DrawsRefused;
		return false;
	}

	ID3D11DeviceContext * context = Device->Get_Context();
	Upload_Constants();
	Bind_State_Objects();

	Bind_Pipeline(pipeline, StreamBuffer, StreamStride, StreamOffset, IndexBuffer, IndexFormat, topology);
	if (water) {
		Bind_Water();
	}
	context->DrawIndexed(index_count, start_index, base_vertex);
	if (water) {
		Unbind_Water();
	}

	++DrawsMade;
	// The soft particles' copy of the depth is behind whatever this wrote into the scene's depth
	// (Take_Scene_Depth); a mirror or a filter target's own depth is not what they read.
	if (RenderStates.Get_Render_State(D3DRS_ZWRITEENABLE) != FALSE
		&& (CurrentTarget == NULL || CurrentDepth == Device->Get_Depth_Stencil_View())) {
		SceneDepthStale = true;
		DepthResolveStale = DepthResolveStale || CurrentTarget == NULL;
	}
	if (CurrentTarget != NULL) {
		++DrawsIntoTargets;
		MaskWhileTargeted |= RenderStates.Get_Render_State(D3DRS_COLORWRITEENABLE);
	}
	return true;
}

bool DX11BackendClass::Draw_Triangles(unsigned vertex_count, unsigned start_vertex)
{
	Pipeline pipeline;
	if (StreamBuffer == NULL) {
		++RefusedNoBuffer;
		++DrawsRefused;
		return false;
	}
	if (ForeignPixelShader || ForeignVertexShader) {
		Note_Foreign_Refusal();
		++RefusedForeignShader;
		++DrawsRefused;
		return false;
	}
	if (Any_Missing_Texture()) {
		++RefusedNoTexture;
		++DrawsRefused;
		return false;
	}
	if (!Resolve(pipeline)) {
		++DrawsRefused;
		return false;
	}
	const bool water = EngineShader_Is_Modern_Water(PixelProgram) && !ShadowMapBound;
	if (water && !Prepare_Water()) {
		++DrawsRefused;
		return false;
	}

	ID3D11DeviceContext * context = Device->Get_Context();
	Upload_Constants();
	Bind_State_Objects();

	Bind_Pipeline(pipeline, StreamBuffer, StreamStride, StreamOffset, NULL, DXGI_FORMAT_UNKNOWN,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	if (water) {
		Bind_Water();
	}
	context->Draw(vertex_count, start_vertex);
	if (water) {
		Unbind_Water();
	}

	++DrawsMade;
	// The soft particles' copy of the depth is behind whatever this wrote into the scene's depth
	// (Take_Scene_Depth); a mirror or a filter target's own depth is not what they read.
	if (RenderStates.Get_Render_State(D3DRS_ZWRITEENABLE) != FALSE
		&& (CurrentTarget == NULL || CurrentDepth == Device->Get_Depth_Stencil_View())) {
		SceneDepthStale = true;
		DepthResolveStale = DepthResolveStale || CurrentTarget == NULL;
	}
	if (CurrentTarget != NULL) {
		++DrawsIntoTargets;
		MaskWhileTargeted |= RenderStates.Get_Render_State(D3DRS_COLORWRITEENABLE);
	}
	return true;
}

// What is actually in the texture bound at a stage, read back through a staging copy.  A draw that
// samples a texture nobody filled looks exactly like a draw that never happened, and no count tells
// them apart.  This costs a full stall and is only ever called once.
std::string DX11BackendClass::Texture_Average(unsigned stage)
{
	if (stage >= DX11_BACKEND_TEXTURE_STAGES || Textures[stage] == NULL) {
		return "no texture";
	}

	ID3D11Resource * resource = NULL;
	Textures[stage]->GetResource(&resource);
	if (resource == NULL) {
		return "no resource";
	}

	ID3D11Texture2D * texture = NULL;
	if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&texture))) {
		resource->Release();
		return "not a 2D texture";
	}

	D3D11_TEXTURE2D_DESC description;
	texture->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;
	description.MipLevels = 1;

	ID3D11Texture2D * staging = NULL;
	if (FAILED(Device->Get_Device()->CreateTexture2D(&description, NULL, &staging))) {
		texture->Release();
		resource->Release();
		return "no staging copy";
	}

	Device->Get_Context()->CopySubresourceRegion(staging, 0, 0, 0, 0, resource, 0, NULL);

	std::string answer = "unreadable";
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(Device->Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		double blue = 0.0, green = 0.0, red = 0.0, alpha = 0.0;
		const unsigned pixels = description.Width * description.Height;
		for (unsigned row = 0; row < description.Height; ++row) {
			const unsigned char * line = (const unsigned char *)mapped.pData + row * mapped.RowPitch;
			for (unsigned column = 0; column < description.Width; ++column) {
				blue  += line[column * 4 + 0];
				green += line[column * 4 + 1];
				red   += line[column * 4 + 2];
				alpha += line[column * 4 + 3];
			}
		}
		Device->Get_Context()->Unmap(staging, 0);

		char line[128];
		snprintf(line, sizeof(line), "%ux%u average rgba %.0f,%.0f,%.0f,%.0f",
			description.Width, description.Height, red / pixels, green / pixels, blue / pixels,
			alpha / pixels);
		answer = line;
	}

	staging->Release();
	texture->Release();
	resource->Release();
	return answer;
}

// The screen-space quads the engine draws with DrawPrimitiveUP - the filter that puts the rendered
// scene back on the screen, the smudges, the shadow volume's darkening pass - come as four
// vertices in a strip and no buffer at all.  D3D11 has no equivalent, so they are copied into a
// dynamic buffer the backend owns and drawn as a list, which is also what turns the strip's
// alternating winding into triangles.
bool DX11BackendClass::Draw_User_Strip(const void * vertices, unsigned primitive_count,
	unsigned stride)
{
	if (vertices == NULL || primitive_count == 0 || stride == 0) {
		return false;
	}

	Pipeline pipeline;
	if (ForeignPixelShader || ForeignVertexShader) {
		Note_Foreign_Refusal();
		++RefusedForeignShader;
		++DrawsRefused;
		return false;
	}
	if (Any_Missing_Texture()) {
		++RefusedNoTexture;
		++DrawsRefused;
		return false;
	}

	// Not the first quad of the run: that one is the loading screen's, drawn before anything has
	// been rendered into the texture it samples, and reading it back proves nothing.
	const unsigned long long DRAWS_BEFORE_TRACING = 50000;
	const DWORD SCREEN_QUAD_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	if (!TracedUserStrip && DrawsMade > DRAWS_BEFORE_TRACING && Textures[0] != NULL
		&& VertexFormat == SCREEN_QUAD_FVF) {
		TracedUserStrip = true;
		const float * corner = static_cast<const float *>(vertices);
		char line[192];
		snprintf(line, sizeof(line),
			"quad %.0f,%.0f fvf %lu viewport %ux%u, colour write %lu, into targets %lu, stage 0 %s",
			corner[0], corner[1], VertexFormat, ViewportWidth, ViewportHeight,
			(unsigned long)RenderStates.Get_Render_State(D3DRS_COLORWRITEENABLE),
			(unsigned long)MaskWhileTargeted, Texture_Average(0).c_str());
		Diagnostic = line;
	}

	const unsigned list_vertex_count = primitive_count * 3;
	const unsigned byte_count = list_vertex_count * stride;
	if (!Reserve_User_Buffer(byte_count)) {
		++RefusedNoBuffer;
		++DrawsRefused;
		return false;
	}

	ID3D11DeviceContext * context = Device->Get_Context();
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(UserBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		++RefusedNoBuffer;
		++DrawsRefused;
		return false;
	}

	const unsigned char * source = static_cast<const unsigned char *>(vertices);
	unsigned char * destination = static_cast<unsigned char *>(mapped.pData);
	for (unsigned primitive = 0; primitive < primitive_count; ++primitive) {
		// A strip alternates winding: the odd triangle takes its first two vertices the other way
		// round, which is what keeps every triangle facing the same way.
		const unsigned order[3] = {
			(primitive % 2 == 0) ? primitive : primitive + 1,
			(primitive % 2 == 0) ? primitive + 1 : primitive,
			primitive + 2 };
		for (unsigned corner = 0; corner < 3; ++corner) {
			memcpy(destination + (primitive * 3 + corner) * stride,
				source + order[corner] * stride, stride);
		}
	}
	context->Unmap(UserBuffer, 0);

	// The pipeline is resolved after the copy so a refusal does not leave a mapped buffer behind.
	if (!Resolve(pipeline)) {
		++DrawsRefused;
		return false;
	}

	Upload_Constants();
	Bind_State_Objects();

	Bind_Pipeline(pipeline, UserBuffer, stride, 0, NULL, DXGI_FORMAT_UNKNOWN,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->Draw(list_vertex_count, 0);

	++DrawsMade;
	// The soft particles' copy of the depth is behind whatever this wrote into the scene's depth
	// (Take_Scene_Depth); a mirror or a filter target's own depth is not what they read.
	if (RenderStates.Get_Render_State(D3DRS_ZWRITEENABLE) != FALSE
		&& (CurrentTarget == NULL || CurrentDepth == Device->Get_Depth_Stencil_View())) {
		SceneDepthStale = true;
		DepthResolveStale = DepthResolveStale || CurrentTarget == NULL;
	}
	if (CurrentTarget != NULL) {
		++DrawsIntoTargets;
		MaskWhileTargeted |= RenderStates.Get_Render_State(D3DRS_COLORWRITEENABLE);
	}
	return true;
}

bool DX11BackendClass::Reserve_User_Buffer(unsigned byte_count)
{
	if (UserBuffer != NULL && UserBufferBytes >= byte_count) {
		return true;
	}

	if (UserBuffer != NULL) {
		UserBuffer->Release();
		UserBuffer = NULL;
		UserBufferBytes = 0;
	}

	if (!DX11Resource_Create_Vertex_Buffer(Device->Get_Device(), byte_count, D3DPOOL_DEFAULT,
			D3DUSAGE_DYNAMIC, NULL, &UserBuffer)) {
		Note_Refusal("the device refused the buffer the screen quads are copied into");
		return false;
	}
	UserBufferBytes = byte_count;
	return true;
}

// The first thing the compiler complained about, kept whole.  A generated program that will not
// compile is a defect in the generator, and the message names the line of it.
void DX11BackendClass::Record_Compiler_Error(const char * which, ID3DBlob * errors)
{
	if (!CompilerError.empty()) {
		return;
	}
	CompilerError = which;
	CompilerError += ": ";
	if (errors != NULL) {
		CompilerError.append((const char *)errors->GetBufferPointer(), errors->GetBufferSize());
		errors->Release();
	}
	else {
		CompilerError += "no message";
	}
}

unsigned DX11BackendClass::Refused_Description_Count() const
{
	return (unsigned)RefusedPipelines.size();
}

const char * DX11BackendClass::Refused_Description(unsigned index) const
{
	std::map<std::string, unsigned>::const_iterator entry = RefusedPipelines.begin();
	for (unsigned step = 0; step < index && entry != RefusedPipelines.end(); ++step) {
		++entry;
	}
	return entry == RefusedPipelines.end() ? "" : entry->first.c_str();
}

// The first refusal that was not the compiler's doing, in the same field the compiler's message
// uses: a generator that declines a description and a device that declines the program it produced
// are different defects and the count alone tells them apart from nothing.
void DX11BackendClass::Note_Refusal(const char * reason)
{
	if (CompilerError.empty()) {
		CompilerError = reason;
	}
}

void DX11BackendClass::Refuse(const std::string & key, RefusalReason reason)
{
	RefusedPipelines[key] = reason;
	switch (reason) {
	case REFUSED_NO_STAGE:   ++RefusedNoStage; break;
	case REFUSED_NO_LAYOUT:  ++RefusedNoLayout; break;
	case REFUSED_NO_PROGRAM: ++RefusedNoProgram; break;
	case REFUSED_NO_OBJECT:  ++RefusedNoObject; break;
	}
}

void DX11BackendClass::Refusals(unsigned long long & no_buffer, unsigned long long & no_stage,
	unsigned long long & no_layout, unsigned long long & no_program,
	unsigned long long & no_object, unsigned long long & foreign_shader,
	unsigned long long & no_texture) const
{
	no_buffer = RefusedNoBuffer;
	no_stage = RefusedNoStage;
	no_layout = RefusedNoLayout;
	no_program = RefusedNoProgram;
	no_object = RefusedNoObject;
	foreign_shader = RefusedForeignShader;
	no_texture = RefusedNoTexture;
}

void DX11BackendClass::Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
	unsigned long long & draws_refused) const
{
	pipelines_built = PipelinesBuilt;
	draws_made = DrawsMade;
	draws_refused = DrawsRefused;
}

void DX11BackendClass::Take_Frame_Build_Cost(double & milliseconds, unsigned & pipelines)
{
	milliseconds = FrameBuildMilliseconds;
	pipelines = FrameBuildCount;
	FrameBuildMilliseconds = 0.0;
	FrameBuildCount = 0;
}

void DX11BackendClass::Take_Frame_Constant_Writes(unsigned & writes, unsigned & renames)
{
	writes = FrameConstantWrites;
	renames = FrameConstantRenames;
	FrameConstantWrites = 0;
	FrameConstantRenames = 0;
}
