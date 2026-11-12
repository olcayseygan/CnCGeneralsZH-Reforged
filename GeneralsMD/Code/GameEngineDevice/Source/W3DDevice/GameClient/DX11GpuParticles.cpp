// Direct3D 11 backend for the GPU particle path (GpuParticles.h, GPU-PARTICLES.md in the wrapper):
// the spawn ring, the system table, the texture array and the three passes, drawn on the context
// between two of the backend's draws.  Distributed under the repository's GPL-3.0 terms; see
// LICENSE.md.
//
// A frame runs one compute pass and two draws.  The compute pass ages every record in the ring's
// live range once, in closed form, into a buffer of drawable particles (position, size, angle,
// colour, slice); doing it in the vertex shader instead would evaluate each particle six times a
// pass and twice over for the two passes.  Both draws go in from doParticles, ADDITIVE then ALPHA,
// so smoke darkens the fire under it as the sorted pool's order does, and before the CPU's sorted
// billboards land over them.  Each is one Draw of six vertices a particle with no vertex buffer,
// every particle texture in one Texture2DArray, so a frame is three calls whatever the number of
// systems or textures.
//
// The HLSL below mirrors ParticleClosedForm.h line for line: that header is the reference the tests
// hold, and a change to one is a change to both.
#include "W3DDevice/GameClient/GpuParticles.h"
#include "Common/Debug.h"
#include "Common/GameCommon.h"
#include "dx11backend.h"
#include "dx11device.h"
#include "dx11runtime.h"
#include "dx11resource.h"
#include "dx11texture.h"

#include <d3d11.h>
#include <cmath>
#include <cstring>
#include <string>

namespace {

// ParticleClosedForm.h's records; W3DParticleSys.cpp asserts the sizes against the structs.
const unsigned SpawnStride = 80;
const unsigned EntryStride = 240;
// The compute pass's output: float3 position, size, four half colour channels, angle, kind.
const unsigned ParticleStride = 32;
const unsigned SliceTexels = 128;
const unsigned SliceMips = 8;		// 128 down to 1
const unsigned GroupSize = 64;
// The backend's own fade length for the sorted billboards (SOFT_PARTICLE_FADE_UNITS, dx11backend.cpp).
const float SoftFadeUnits = 12.0f;
const unsigned Additive = 1;		// ParticleSystemInfo::ADDITIVE
const unsigned Alpha = 2;				// ParticleSystemInfo::ALPHA

// The records, field for field as ParticleClosedForm.h lays them out.  A StructuredBuffer packs
// its elements at four bytes, so a float3 followed by a scalar is the C struct's sixteen bytes.
#define GPU_PARTICLE_RECORDS \
	"struct Spawn\n" \
	"{\n" \
	"    float3 Position;\n" \
	"    uint Birth;\n" \
	"    float3 Velocity;\n" \
	"    float Lifetime;\n" \
	"    float Size;\n" \
	"    float SizeRate;\n" \
	"    float SizeDamping;\n" \
	"    float VelocityDamping;\n" \
	"    float Angle;\n" \
	"    float AngularRate;\n" \
	"    float AngularDamping;\n" \
	"    float ColorScale;\n" \
	"    uint SlotSeed;\n" \
	"    uint2 AlphaKeys;\n" \
	"    uint Pad;\n" \
	"};\n" \
	"struct AlphaSegment\n" \
	"{\n" \
	"    float Time;\n" \
	"    uint End;\n" \
	"};\n" \
	"struct ColorSegment\n" \
	"{\n" \
	"    float3 Rate;\n" \
	"    uint End;\n" \
	"};\n" \
	"struct SystemEntry\n" \
	"{\n" \
	"    float3 Drift;\n" \
	"    float Gravity;\n" \
	"    float3 Color;\n" \
	"    uint Slice;\n" \
	"    uint ShaderType;\n" \
	"    uint Flags;\n" \
	"    uint AlphaCount;\n" \
	"    uint ColorCount;\n" \
	"    AlphaSegment Alpha[8];\n" \
	"    ColorSegment Colour[8];\n" \
	"};\n" \
	"struct Particle\n" \
	"{\n" \
	"    float3 Position;\n" \
	"    float Size;\n" \
	"    uint2 Colour;\n"		/* red and green, blue and alpha, as halves */ \
	"    float Angle;\n" \
	"    uint Kind;\n"				/* the slice in the low 16 bits, the shader type above; 0 is dead */ \
	"};\n" \
	"\n"

// One thread a record.  The system entry is read from the buffer where it is indexed by a key, not
// copied: a copied entry is an indexable temporary of sixty registers.
static const char EVALUATE_PROGRAM[] =
	GPU_PARTICLE_RECORDS
	"cbuffer Frame : register(b0)\n"
	"{\n"
	"    uint First;\n"
	"    uint Count;\n"
	"    uint Capacity;\n"
	"    uint Now;\n"
	"    float Smooth;\n"
	"    float3 Unused;\n"
	"    float4 GroundMap;\n"		// world x and y to the height texture's coordinates: scale x, y, offset x, y
	"};\n"
	"StructuredBuffer<Spawn> Spawns : register(t0);\n"
	"StructuredBuffer<SystemEntry> Systems : register(t1);\n"
	"StructuredBuffer<float> Fades : register(t2);\n"
	"Texture2D<float> Ground : register(t3);\n"
	"SamplerState Linear : register(s0);\n"
	"RWStructuredBuffer<Particle> Particles : register(u0);\n"
	"\n"
	"static const uint ADDITIVE = 1;\n"
	"static const uint ALPHA = 2;\n"
	"static const uint ALPHA_TEST = 3;\n"
	"\n"
	"float alpha_key(Spawn spawn, uint key)\n"
	"{\n"
	"    uint word = (key < 4) ? spawn.AlphaKeys.x : spawn.AlphaKeys.y;\n"
	"    return float((word >> ((key & 3) * 8)) & 0xFF) / 255.0;\n"
	"}\n"
	"\n"
	// particleClosedFormSums
	"void damped_sums(float damping, float k, out float power, out float sum, out float nested)\n"
	"{\n"
	"    float d = (damping < 1.0e-6) ? 1.0e-6 : damping;\n"
	"    float e = 1.0 - d;\n"
	"    power = pow(abs(d), k);\n"		// d is at least 1e-6 already; abs only tells FXC so
	"    if (abs(e) * k < 0.1)\n"
	"    {\n"
	"        sum = k * (1.0 - e * (k - 1.0) / 2.0 * (1.0 - e * (k - 2.0) / 3.0 * (1.0 - e * (k - 3.0) / 4.0)));\n"
	"        nested = k * (k + 1.0) / 2.0\n"
	"            * (1.0 - e * (k - 1.0) / 3.0 * (1.0 - e * (k - 2.0) / 4.0 * (1.0 - e * (k - 3.0) / 5.0)));\n"
	"        return;\n"
	"    }\n"
	"    sum = (1.0 - power) / e;\n"
	"    nested = (k - d * sum) / e;\n"
	"}\n"
	"\n"
	// particleClosedFormAlpha
	"float closed_alpha(Spawn spawn, uint slot, float k)\n"
	"{\n"
	"    if (Systems[slot].ShaderType == ADDITIVE)\n"
	"        return alpha_key(spawn, 0);\n"
	"    uint start = 0;\n"
	"    uint key = 0;\n"
	"    uint count = Systems[slot].AlphaCount;\n"
	"    [loop] for (; key < count; ++key)\n"
	"    {\n"
	"        uint end = Systems[slot].Alpha[key].End;\n"
	"        if (k < float(end))\n"
	"        {\n"
	"            float from = alpha_key(spawn, key);\n"
	"            float rate = (alpha_key(spawn, key + 1) - from) / Systems[slot].Alpha[key].Time;\n"
	"            return saturate(from + (k - float(start)) * rate);\n"
	"        }\n"
	"        start = end;\n"
	"    }\n"
	"    return saturate(alpha_key(spawn, key));\n"
	"}\n"
	"\n"
	// particleClosedFormColor.  Two colour keys on one frame divide by nought: the CPU carries the
	// infinity into a clamp, which is what this does too, but nought over nought is a NaN that a
	// blend would spread through the bloom, so a NaN rate is no change, which is what equal keys mean.
	"float3 closed_colour(Spawn spawn, uint slot, float k)\n"
	"{\n"
	"    float scale = spawn.ColorScale;\n"
	"    float3 colour = Systems[slot].Color;\n"
	"    float done = 0.0;\n"
	"    uint count = Systems[slot].ColorCount;\n"
	"    [loop] for (uint i = 0; i < count && done < k; ++i)\n"
	"    {\n"
	"        float end = min(k, float(Systems[slot].Colour[i].End));\n"
	"        float3 rate = Systems[slot].Colour[i].Rate;\n"
	"        bool3 undefined = (asuint(rate) & 0x7FFFFFFFu) > 0x7F800000u;\n"
	"        rate = undefined ? float3(0.0, 0.0, 0.0) : rate;\n"
	"        colour = saturate(colour + (end - done) * (rate + scale));\n"
	"        done = end;\n"
	"    }\n"
	"    if (done < k)\n"
	"        colour = saturate(colour + (k - done) * scale);\n"
	"    return colour;\n"
	"}\n"
	"\n"
	// particleClosedFormFade.  An additive particle as black as nought under a falling envelope
	// scales by a negative over nought; it is invisible either way, so it is left black.
	"void closed_fade(Spawn spawn, uint shader, float k, inout float alpha, inout float3 colour)\n"
	"{\n"
	"    uint lifetime = (uint)spawn.Lifetime;\n"
	"    if (lifetime == 0u)\n"
	"        return;\n"
	"    uint frames = lifetime / 4u;\n"
	"    frames = (frames > FADE_MAXu) ? FADE_MAXu : ((frames == 0u) ? 1u : frames);\n"
	"    float left = spawn.Lifetime - k;\n"
	"    if (left > float(frames))\n"
	"        return;\n"
	"    float envelope = (left - 1.0) / float(frames);\n"
	"    if (shader == ALPHA || shader == ALPHA_TEST)\n"
	"    {\n"
	"        if (alpha > envelope)\n"
	"            alpha = envelope;\n"
	"    }\n"
	"    else if (shader == ADDITIVE)\n"
	"    {\n"
	"        float brightest = max(colour.r, max(colour.g, colour.b));\n"
	"        if (brightest > envelope && brightest > 0.0)\n"
	"            colour *= envelope / brightest;\n"
	"    }\n"
	"}\n"
	"\n"
	"[numthreads(64, 1, 1)]\n"
	"void main(uint3 thread : SV_DispatchThreadID)\n"
	"{\n"
	"    uint index = thread.x;\n"
	"    if (index >= Count)\n"
	"        return;\n"
	"    Spawn spawn = Spawns[(First + index) % Capacity];\n"
	"    uint slot = spawn.SlotSeed & 0xFFFF;\n"
	"    uint shader = Systems[slot].ShaderType;\n"
	"    float fade = Fades[slot];\n"
	"    Particle particle;\n"
	"    particle.Position = float3(0.0, 0.0, 0.0);\n"
	"    particle.Size = 0.0;\n"
	"    particle.Colour = uint2(0, 0);\n"
	"    particle.Angle = 0.0;\n"
	"    particle.Kind = 0;\n"
	// a released system's records, and a system the local player does not see
	"    if ((Systems[slot].Flags & 1) == 0 || fade <= 0.0)\n"
	"    {\n"
	"        Particles[index] = particle;\n"
	"        return;\n"
	"    }\n"
	// a newborn is updated on its birth frame, so k is one there; the picture lags a fraction of an
	// update the way the models' smooth motion does
	"    float k = max(float(Now - spawn.Birth) + Smooth, 1.0);\n"
	"\n"
	// particleClosedFormEvaluate
	"    float power, sum, nested;\n"
	"    damped_sums(spawn.VelocityDamping, k, power, sum, nested);\n"
	"    float d = (spawn.VelocityDamping < 1.0e-6) ? 1.0e-6 : spawn.VelocityDamping;\n"
	"    float travel = d * sum;\n"
	"    float3 position = spawn.Position + k * Systems[slot].Drift + spawn.Velocity * travel;\n"
	"    position.z += Systems[slot].Gravity * d * nested;\n"
	// A system that would collide with the terrain on the CPU (ParticleBounce) rests on it here: the
	// closed form cannot bounce, so a particle below the ground is put on it and stays as it falls.
	"    if ((Systems[slot].Flags & 2) != 0)\n"
	"        position.z = max(position.z, Ground.SampleLevel(Linear, position.xy * GroundMap.xy + GroundMap.zw, 0.0));\n"
	"    damped_sums(spawn.AngularDamping, k, power, sum, nested);\n"
	"    float angle = spawn.Angle + spawn.AngularRate * sum;\n"
	"    damped_sums(spawn.SizeDamping, k, power, sum, nested);\n"
	"    float size = spawn.Size + spawn.SizeRate * sum;\n"
	"\n"
	"    float alpha = closed_alpha(spawn, slot, k);\n"
	"    float3 colour = closed_colour(spawn, slot, k);\n"
	"    closed_fade(spawn, shader, k, alpha, colour);\n"
	"\n"
	"    bool invisible = false;\n"
	"    if (shader == ALPHA)\n"
	"    {\n"
	"        uint last = 0;\n"
	"        uint count = Systems[slot].AlphaCount;\n"
	"        if (count > 0)\n"
	"            last = Systems[slot].Alpha[count - 1].End;\n"
	"        invisible = k >= float(last) && alpha < 0.02;\n"
	"    }\n"
	"    else if (shader == ADDITIVE)\n"
	"    {\n"
	"        uint last = 0;\n"
	"        uint count = Systems[slot].ColorCount;\n"
	"        if (count > 0)\n"
	"            last = Systems[slot].Colour[count - 1].End;\n"
	"        if (k >= float(last))\n"
	"        {\n"
	"            invisible = (colour.r + colour.g + colour.b) <= 0.06;\n"
	"            float first = (last != 0) ? float(last) : 1.0;\n"
	"            if (!invisible && first < k)\n"
	"            {\n"
	"                float firstAlpha = alpha;\n"
	"                float3 firstColour = closed_colour(spawn, slot, first);\n"
	"                closed_fade(spawn, shader, first, firstAlpha, firstColour);\n"
	"                invisible = (firstColour.r + firstColour.g + firstColour.b) <= 0.06;\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    if (invisible || (spawn.Lifetime != 0.0 && k >= spawn.Lifetime))\n"
	"    {\n"
	"        Particles[index] = particle;\n"
	"        return;\n"
	"    }\n"
	"\n"
	// stealthFadeParticle, then the vertex colour's clamp (Convert_Color_Clamp)
	"    if (shader == ADDITIVE)\n"
	"        colour *= fade;\n"
	"    else\n"
	"        alpha *= fade;\n"
	"    colour = saturate(colour);\n"
	"    alpha = saturate(alpha);\n"
	"    particle.Position = position;\n"
	"    particle.Size = size;\n"
	"    particle.Colour = uint2(f32tof16(colour.r) | (f32tof16(colour.g) << 16),\n"
	"        f32tof16(colour.b) | (f32tof16(alpha) << 16));\n"
	"    particle.Angle = angle;\n"
	"    particle.Kind = (shader << 16) | (Systems[slot].Slice & 0xFFFF);\n"
	"    Particles[index] = particle;\n"
	"}\n";

#define GPU_PARTICLE_PASS_CONSTANTS \
	"cbuffer DrawPass : register(b0)\n" \
	"{\n" \
	"    row_major float4x4 View;\n" \
	"    row_major float4x4 Projection;\n" \
	"    float4 SoftParticleDepth;\n" \
	"    uint Count;\n" \
	"    uint ShaderType;\n" \
	"    uint NewestFirst;\n" \
	"    uint Soft;\n" \
	"    float Gain;\n" \
	"    float3 Unused;\n" \
	"};\n" \
	"struct Corner\n" \
	"{\n" \
	"    float4 Position : SV_Position;\n" \
	"    float4 Colour : COLOR0;\n" \
	"    float3 Coord : TEXCOORD0;\n" \
	"    float HalfWidth : TEXCOORD1;\n" \
	"};\n" \
	"\n"

// Six vertices a particle, two triangles over PointGroupClass's quad corners in its order (0 1 2,
// 0 2 3), turned by its orientation table: the angle cut to a byte over a turn as MSVC casts it,
// then that byte over 256.  The quad is offset in the camera's space, as Write_Billboard does.
static const char DRAW_VERTEX_PROGRAM[] =
	GPU_PARTICLE_RECORDS
	GPU_PARTICLE_PASS_CONSTANTS
	"StructuredBuffer<Particle> Particles : register(t0);\n"
	"\n"
	"Corner main(uint vertex : SV_VertexID)\n"
	"{\n"
	"    uint quad = vertex / 6;\n"
	"    uint index = (NewestFirst != 0) ? Count - 1 - quad : quad;\n"
	"    Particle particle = Particles[index];\n"
	"    Corner corner;\n"
	"    corner.Colour = float4(f16tof32(particle.Colour.x & 0xFFFF), f16tof32(particle.Colour.x >> 16),\n"
	"        f16tof32(particle.Colour.y & 0xFFFF), f16tof32(particle.Colour.y >> 16));\n"
	"    corner.Coord = float3(0.0, 0.0, 0.0);\n"
	"    corner.HalfWidth = 1.0;\n"
	// the other pass's particle, or a dead one: all six on one point, which draws nothing
	"    if ((particle.Kind >> 16) != ShaderType)\n"
	"    {\n"
	"        corner.Position = float4(0.0, 0.0, -2.0, 1.0);\n"
	"        return corner;\n"
	"    }\n"
	"    uint which = (0x320210u >> ((vertex - quad * 6) * 4)) & 0xF;\n"
	"    float2 local = float2((which >= 2) ? 0.5 : -0.5, (which == 0 || which == 3) ? 0.5 : -0.5);\n"
	"    float turn = float(uint(int(particle.Angle * (255.0 / 6.28318531))) & 255u) * (6.28318531 / 256.0);\n"
	"    float c = cos(turn);\n"
	"    float s = sin(turn);\n"
	"    float2 offset = float2(local.x * c - local.y * s, local.x * s + local.y * c) * particle.Size;\n"
	"    float4 eye = mul(float4(particle.Position, 1.0), View);\n"
	"    eye.xy += offset;\n"
	"    corner.Position = mul(eye, Projection);\n"
	"    corner.Coord = float3(local.x + 0.5, 0.5 - local.y, float(particle.Kind & 0xFFFF));\n"
	"    corner.HalfWidth = max(abs(particle.Size) * 0.5, 1.0e-3);\n"
	"    return corner;\n"
	"}\n";

// The sprite modulated by the colour, as the sprite presets' GRADIENT_MODULATE stage does, faded where
// it meets the world through the backend's own soft particle text, and the additive pass brightened
// by the Glow option's gain the way Additive_Glow's blend factor brightens the sorted ones.
static const char DRAW_PIXEL_PROGRAM[] =
	GPU_PARTICLE_PASS_CONSTANTS
	"Texture2DArray Sprites : register(t0);\n"
	"SamplerState Linear : register(s0);\n"
	SOFT_PARTICLE_SAMPLING
	"float4 main(Corner corner) : SV_Target\n"
	"{\n"
	"    float4 colour = Sprites.Sample(Linear, corner.Coord) * corner.Colour;\n"
	"    if (Soft != 0)\n"
	"    {\n"
	"        float fade = soft_particle_fade(corner.Position, corner.HalfWidth);\n"
	"        if (ShaderType == 1)\n"
	"            colour *= fade;\n"
	"        else\n"
	"            colour.a *= fade;\n"
	"    }\n"
	"    colour.rgb *= Gain;\n"
	"    return colour;\n"
	"}\n";

// A particle texture into its slice: one triangle over the 128 square, read at the level nearest that
// size, so a 256 texture is copied from its own 128 mip.
static const char BLIT_VERTEX_PROGRAM[] =
	"float4 main(uint vertex : SV_VertexID) : SV_Position\n"
	"{\n"
	"    float2 corner = float2((vertex << 1) & 2, vertex & 2);\n"
	"    return float4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);\n"
	"}\n";

static const char BLIT_PIXEL_PROGRAM[] =
	"cbuffer Blit : register(b0)\n"
	"{\n"
	"    float Level;\n"
	"    float3 Unused;\n"
	"};\n"
	"Texture2D Source : register(t0);\n"
	"SamplerState Linear : register(s0);\n"
	"float4 main(float4 position : SV_Position) : SV_Target\n"
	"{\n"
	"    return Source.SampleLevel(Linear, position.xy / 128.0, Level);\n"
	"}\n";

struct FrameBlock {
	unsigned first, count, capacity, now;
	float smooth;
	float unused[3];
	float groundMap[4];
};

struct PassBlock {
	float view[16];
	float projection[16];
	float softParticleDepth[4];
	unsigned count, shaderType, newestFirst, soft;
	float gain;
	float unused[3];
};

struct BlitBlock {
	float level;
	float unused[3];
};

template <class T> void drop(T *&object)
{
	if (object) {
		object->Release();
		object = nullptr;
	}
}

struct State {
	ID3D11Device *device = nullptr;
	unsigned capacity = 0;
	bool refused = false;
	ID3D11Buffer *spawns = nullptr;
	ID3D11ShaderResourceView *spawnView = nullptr;
	ID3D11Buffer *systems = nullptr;
	ID3D11ShaderResourceView *systemView = nullptr;
	ID3D11Buffer *fades = nullptr;
	ID3D11ShaderResourceView *fadeView = nullptr;
	ID3D11Buffer *particles = nullptr;
	ID3D11ShaderResourceView *particleView = nullptr;
	ID3D11UnorderedAccessView *particleWrite = nullptr;
	ID3D11Texture2D *sprites = nullptr;
	ID3D11ShaderResourceView *spriteView = nullptr;
	ID3D11RenderTargetView *sliceTargets[GpuParticles::TEXTURE_SLICES] = {};
	// The view each slice was drawn from, a reference held so no later view can come back at its
	// address and pass for it; null is a cleared slice.
	ID3D11ShaderResourceView *sliceSources[GpuParticles::TEXTURE_SLICES] = {};
	// Set_Slice_Texture's asks since the last Evaluate, drawn there in one go.
	ID3D11ShaderResourceView *slicePending[GpuParticles::TEXTURE_SLICES] = {};
	bool sliceAsked[GpuParticles::TEXTURE_SLICES] = {};
	unsigned slicesAsked = 0;
	ID3D11Texture2D *ground = nullptr;
	ID3D11ShaderResourceView *groundView = nullptr;
	float groundMap[4] = {};
	// What the last Evaluate aged and from what, so a pass drawn again before anything moved (a
	// paused game, a frame faster than the logic) does not age the ring again.
	bool inputsChanged = true;
	unsigned lastFirst = 0, lastCount = 0, lastNow = 0;
	float lastSmooth = -1.0f;
	ID3D11ComputeShader *evaluate = nullptr;
	ID3D11VertexShader *drawVertex = nullptr;
	ID3D11PixelShader *drawPixel = nullptr;
	ID3D11VertexShader *blitVertex = nullptr;
	ID3D11PixelShader *blitPixel = nullptr;
	ID3D11SamplerState *linear = nullptr;
	ID3D11BlendState *alphaBlend = nullptr;
	ID3D11BlendState *additiveBlend = nullptr;
	ID3D11DepthStencilState *depthRead = nullptr;
	ID3D11RasterizerState *rasterizer = nullptr;
	ID3D11Buffer *frameConstants = nullptr;
	ID3D11Buffer *passConstants = nullptr;
	ID3D11Buffer *blitConstants = nullptr;
	unsigned evaluated = 0;		// particles the last Evaluate wrote
};
State state;

void release()
{
	drop(state.spawnView);
	drop(state.spawns);
	drop(state.systemView);
	drop(state.systems);
	drop(state.fadeView);
	drop(state.fades);
	drop(state.particleWrite);
	drop(state.particleView);
	drop(state.particles);
	for (ID3D11RenderTargetView *&target : state.sliceTargets) drop(target);
	for (ID3D11ShaderResourceView *&source : state.sliceSources) drop(source);
	drop(state.groundView);
	drop(state.ground);
	drop(state.spriteView);
	drop(state.sprites);
	drop(state.evaluate);
	drop(state.drawVertex);
	drop(state.drawPixel);
	drop(state.blitVertex);
	drop(state.blitPixel);
	drop(state.linear);
	drop(state.alphaBlend);
	drop(state.additiveBlend);
	drop(state.depthRead);
	drop(state.rasterizer);
	drop(state.frameConstants);
	drop(state.passConstants);
	drop(state.blitConstants);
	state = State();
}

/* ParticleClosedForm.h's PARTICLE_CLOSED_FORM_FADE_MAX_FRAMES, eight retail frames in logic frames
	 (W3DParticleSys.cpp asserts the two agree), written into closed_fade's FADE_MAX.  At 60 ticks a second
	 the text is the 16u it always was, so the shipped shader cache still matches it. */
std::string evaluateProgram()
{
	std::string source = EVALUATE_PROGRAM;
	const std::string fadeMax = std::to_string(8 * LOGICFRAMES_PER_RETAIL_FRAME);
	for (size_t at = source.find("FADE_MAX"); at != std::string::npos; at = source.find("FADE_MAX", at))
		source.replace(at, 8, fadeMax);
	return source;
}

bool compile(DX11BackendClass *backend, ID3D11Device *device, const char *source, const char *name,
	const char *profile, void **program)
{
	std::vector<unsigned char> code;
	if (!backend->Compile_Program(source, name, profile, code)) {
		DEBUG_LOG(("GPUPARTICLES: %s did not compile: %s\n", name, backend->First_Compiler_Error()));
		return false;
	}
	switch (profile[0]) {
		case 'c': return SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, (ID3D11ComputeShader **)program));
		case 'v': return SUCCEEDED(device->CreateVertexShader(code.data(), code.size(), nullptr, (ID3D11VertexShader **)program));
		default: return SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, (ID3D11PixelShader **)program));
	}
}

bool makeStructured(ID3D11Device *device, unsigned count, unsigned stride, bool writable, ID3D11Buffer **buffer,
	ID3D11ShaderResourceView **view, ID3D11UnorderedAccessView **write)
{
	D3D11_BUFFER_DESC description = {};
	description.ByteWidth = count * stride;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0);
	description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	description.StructureByteStride = stride;
	D3D11_SHADER_RESOURCE_VIEW_DESC read = {};
	read.Format = DXGI_FORMAT_UNKNOWN;
	read.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	read.Buffer.FirstElement = 0;
	read.Buffer.NumElements = count;
	if (FAILED(device->CreateBuffer(&description, nullptr, buffer))
		|| FAILED(device->CreateShaderResourceView(*buffer, &read, view)))
		return false;
	if (!writable) return true;
	D3D11_UNORDERED_ACCESS_VIEW_DESC written = {};
	written.Format = DXGI_FORMAT_UNKNOWN;
	written.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	written.Buffer.FirstElement = 0;
	written.Buffer.NumElements = count;
	return SUCCEEDED(device->CreateUnorderedAccessView(*buffer, &written, write));
}

bool makeConstants(ID3D11Device *device, unsigned bytes, ID3D11Buffer **buffer)
{
	D3D11_BUFFER_DESC description = {};
	description.ByteWidth = bytes;
	description.Usage = D3D11_USAGE_DYNAMIC;
	description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	return SUCCEEDED(device->CreateBuffer(&description, nullptr, buffer));
}

void upload(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const void *data, size_t bytes)
{
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
	std::memcpy(mapped.pData, data, bytes);
	context->Unmap(buffer, 0);
}

bool makeAll(DX11BackendClass *backend, ID3D11Device *device, unsigned capacity)
{
	bool made = makeStructured(device, capacity, SpawnStride, false, &state.spawns, &state.spawnView, nullptr)
		&& makeStructured(device, GpuParticles::SYSTEM_SLOTS, EntryStride, false, &state.systems, &state.systemView, nullptr)
		&& makeStructured(device, GpuParticles::SYSTEM_SLOTS, sizeof(float), false, &state.fades, &state.fadeView, nullptr)
		&& makeStructured(device, capacity, ParticleStride, true, &state.particles, &state.particleView, &state.particleWrite)
		&& makeConstants(device, sizeof(FrameBlock), &state.frameConstants)
		&& makeConstants(device, sizeof(PassBlock), &state.passConstants)
		&& makeConstants(device, sizeof(BlitBlock), &state.blitConstants)
		&& compile(backend, device, evaluateProgram().c_str(), "gpuparticles evaluate", "cs_5_0", (void **)&state.evaluate)
		&& compile(backend, device, DRAW_VERTEX_PROGRAM, "gpuparticles draw", "vs_5_0", (void **)&state.drawVertex)
		&& compile(backend, device, DRAW_PIXEL_PROGRAM, "gpuparticles draw", "ps_5_0", (void **)&state.drawPixel)
		&& compile(backend, device, BLIT_VERTEX_PROGRAM, "gpuparticles slice", "vs_5_0", (void **)&state.blitVertex)
		&& compile(backend, device, BLIT_PIXEL_PROGRAM, "gpuparticles slice", "ps_5_0", (void **)&state.blitPixel);

	D3D11_TEXTURE2D_DESC sprites = {};
	sprites.Width = sprites.Height = SliceTexels;
	sprites.MipLevels = SliceMips;
	sprites.ArraySize = GpuParticles::TEXTURE_SLICES;
	sprites.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sprites.SampleDesc.Count = 1;
	sprites.Usage = D3D11_USAGE_DEFAULT;
	sprites.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	sprites.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	made = made && SUCCEEDED(device->CreateTexture2D(&sprites, nullptr, &state.sprites))
		&& SUCCEEDED(device->CreateShaderResourceView(state.sprites, nullptr, &state.spriteView));

	D3D11_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	made = made && SUCCEEDED(device->CreateSamplerState(&sampler, &state.linear));

	// The sprite presets' blends: SRCALPHA, INVSRCALPHA and ONE, ONE, the alpha channel taking the
	// same factors as Direct3D 9 gives it with no separate alpha blend.
	D3D11_BLEND_DESC blend = {};
	D3D11_RENDER_TARGET_BLEND_DESC &target = blend.RenderTarget[0];
	target.BlendEnable = TRUE;
	target.SrcBlend = target.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
	target.DestBlend = target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	made = made && SUCCEEDED(device->CreateBlendState(&blend, &state.alphaBlend));
	target.SrcBlend = target.SrcBlendAlpha = D3D11_BLEND_ONE;
	target.DestBlend = target.DestBlendAlpha = D3D11_BLEND_ONE;
	made = made && SUCCEEDED(device->CreateBlendState(&blend, &state.additiveBlend));

	// PASS_LEQUAL with no depth write, as both sprite presets
	D3D11_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	made = made && SUCCEEDED(device->CreateDepthStencilState(&depth, &state.depthRead));

	D3D11_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	made = made && SUCCEEDED(device->CreateRasterizerState(&rasterizer, &state.rasterizer));

	// A target on every slice, made here with the rest so the first frame that needs one does not.
	for (unsigned slice = 0; made && slice < GpuParticles::TEXTURE_SLICES; ++slice) {
		D3D11_RENDER_TARGET_VIEW_DESC description = {};
		description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		description.Texture2DArray.MipSlice = 0;
		description.Texture2DArray.FirstArraySlice = slice;
		description.Texture2DArray.ArraySize = 1;
		made = SUCCEEDED(device->CreateRenderTargetView(state.sprites, &description, &state.sliceTargets[slice]));
	}
	return made;
}

// Every slice cleared and the mips made once, at the Prepare that made them (a map's load), so no
// layer is ever undefined and the driver's first GenerateMips is not paid mid-match.
void clearSlices(ID3D11DeviceContext *context)
{
	const float clear[4] = {};
	for (ID3D11RenderTargetView *target : state.sliceTargets) context->ClearRenderTargetView(target, clear);
	context->GenerateMips(state.spriteView);
}

// The asked slices drawn from their textures, then every mip of the array again.  Binds on the
// context behind the backend's back, so the caller forgets the backend's bindings afterwards.
void drawSlices(ID3D11DeviceContext *context)
{
	ID3D11RenderTargetView *savedTarget = nullptr;
	ID3D11DepthStencilView *savedDepth = nullptr;
	context->OMGetRenderTargets(1, &savedTarget, &savedDepth);
	D3D11_VIEWPORT savedViewport;
	UINT viewportCount = 1;
	context->RSGetViewports(&viewportCount, &savedViewport);

	const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, float(SliceTexels), float(SliceTexels), 0.0f, 1.0f };
	const float clear[4] = {};
	context->RSSetViewports(1, &viewport);
	context->RSSetState(state.rasterizer);
	context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	context->OMSetDepthStencilState(nullptr, 0);
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(state.blitVertex, nullptr, 0);
	context->PSSetShader(state.blitPixel, nullptr, 0);
	context->PSSetSamplers(0, 1, &state.linear);
	context->PSSetConstantBuffers(0, 1, &state.blitConstants);

	ID3D11ShaderResourceView *const none = nullptr;
	for (unsigned slice = 0; slice < GpuParticles::TEXTURE_SLICES; ++slice) {
		if (!state.sliceAsked[slice]) continue;
		state.sliceAsked[slice] = false;
		ID3D11ShaderResourceView *source = state.slicePending[slice];
		drop(state.sliceSources[slice]);
		state.sliceSources[slice] = source;
		if (source) source->AddRef();
		// The texture is read only through its own two dimensional view; anything else leaves the
		// slice clear, which draws nothing in either blend.
		D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
		if (source) source->GetDesc(&view);
		context->PSSetShaderResources(0, 1, &none);
		context->OMSetRenderTargets(1, &state.sliceTargets[slice], nullptr);
		context->ClearRenderTargetView(state.sliceTargets[slice], clear);
		if (!source || view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) continue;

		ID3D11Resource *resource = nullptr;
		source->GetResource(&resource);
		D3D11_TEXTURE2D_DESC texture;
		static_cast<ID3D11Texture2D *>(resource)->GetDesc(&texture);
		resource->Release();
		// SampleLevel counts from the view's most detailed mip, which a reduced texture setting moves
		const unsigned levels = view.Texture2D.MipLevels ? view.Texture2D.MipLevels : 1;
		const unsigned width = texture.Width >> view.Texture2D.MostDetailedMip;
		float level = std::log2(float(width ? width : 1) / float(SliceTexels));
		level = level < 0.0f ? 0.0f : (level > float(levels - 1) ? float(levels - 1) : level);
		const BlitBlock block = { level, { 0.0f, 0.0f, 0.0f } };
		upload(context, state.blitConstants, &block, sizeof(block));
		context->PSSetShaderResources(0, 1, &source);
		context->Draw(3, 0);
	}
	context->PSSetShaderResources(0, 1, &none);
	context->OMSetRenderTargets(1, &savedTarget, savedDepth);
	context->RSSetViewports(viewportCount, &savedViewport);
	if (savedTarget) savedTarget->Release();
	if (savedDepth) savedDepth->Release();
	context->GenerateMips(state.spriteView);
	state.slicesAsked = 0;
}

} // namespace

namespace GpuParticles {

bool Supported()
{
	DX11DeviceClass *device = Direct3D11_Device();
	return Direct3D11_Present_Is_Enabled() && Direct3D11_Backend() && device && device->Get_Device()
		&& device->Get_Device()->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

bool Prepare(unsigned capacity, bool *fresh)
{
	ID3D11Device *device = Direct3D11_Device()->Get_Device();
	*fresh = false;
	if (state.device == device && state.capacity == capacity) return !state.refused;
	const double started = DX11Resource_Milliseconds_Now();
	release();
	state.device = device;
	state.capacity = capacity;
	*fresh = true;
	if (!makeAll(Direct3D11_Backend(), device, capacity)) {
		DEBUG_LOG(("GPUPARTICLES: the Direct3D 11 device refused a ring of %u; every particle stays on the CPU\n", capacity));
		release();
		state.device = device;
		state.capacity = capacity;
		state.refused = true;
		return false;
	}
	clearSlices(Direct3D11_Device()->Get_Context());
	DEBUG_LOG(("GPUPARTICLES: ring of %u records (%u MB with the aged copy) made in %.1f ms\n", capacity,
		unsigned((unsigned long long)capacity * (SpawnStride + ParticleStride) >> 20),
		DX11Resource_Milliseconds_Now() - started));
	return true;
}

void Write_Spawns(unsigned first, const ParticleGpuSpawn *spawns, unsigned count)
{
	const D3D11_BOX box = { first * SpawnStride, 0, 0, (first + count) * SpawnStride, 1, 1 };
	Direct3D11_Device()->Get_Context()->UpdateSubresource(state.spawns, 0, &box, spawns, 0, 0);
	state.inputsChanged = true;
}

void Write_Systems(unsigned first, const ParticleGpuSystemEntry *entries, unsigned count)
{
	const D3D11_BOX box = { first * EntryStride, 0, 0, (first + count) * EntryStride, 1, 1 };
	Direct3D11_Device()->Get_Context()->UpdateSubresource(state.systems, 0, &box, entries, 0, 0);
	state.inputsChanged = true;
}

void Write_Fades(const float *fades, unsigned count)
{
	const D3D11_BOX box = { 0, 0, 0, count * unsigned(sizeof(float)), 1, 1 };
	Direct3D11_Device()->Get_Context()->UpdateSubresource(state.fades, 0, &box, fades, 0, 0);
	state.inputsChanged = true;
}

void Set_Ground(const float *heights, unsigned width, unsigned height, const float mapping[4])
{
	drop(state.groundView);
	drop(state.ground);
	D3D11_TEXTURE2D_DESC description = {};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R32_FLOAT;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	const D3D11_SUBRESOURCE_DATA data = { heights, width * unsigned(sizeof(float)), 0 };
	ID3D11Device *device = Direct3D11_Device()->Get_Device();
	// no ground leaves the clamp on zero, the height of a map with none drawn
	if (FAILED(device->CreateTexture2D(&description, &data, &state.ground))
		|| FAILED(device->CreateShaderResourceView(state.ground, nullptr, &state.groundView)))
		DEBUG_LOG(("GPUPARTICLES: the Direct3D 11 device refused a %ux%u ground\n", width, height));
	std::memcpy(state.groundMap, mapping, sizeof(state.groundMap));
	state.inputsChanged = true;
}

void Set_Slice_Texture(unsigned slice, IDirect3DBaseTexture9 *texture)
{
	DX11DeviceClass *device = Direct3D11_Device();
	ID3D11ShaderResourceView *view = texture
		? DX11Texture_Mirror(device->Get_Device(), device->Get_Context(), texture) : nullptr;
	// The held reference keeps the drawn view alive, so a match on it is that same view and not a
	// later one made at a freed address.
	if (view == state.sliceSources[slice]) return;
	state.slicePending[slice] = view;
	if (!state.sliceAsked[slice]) {
		state.sliceAsked[slice] = true;
		++state.slicesAsked;
	}
}

void Evaluate(unsigned first, unsigned count, unsigned now, float smooth)
{
	ID3D11DeviceContext *context = Direct3D11_Device()->Get_Context();
	if (state.slicesAsked) {
		drawSlices(context);
		Direct3D11_Backend()->Forget_Bindings();
	}
	state.evaluated = count;
	if (count == 0) return;
	const bool same = !state.inputsChanged && first == state.lastFirst && count == state.lastCount
		&& now == state.lastNow && smooth == state.lastSmooth;
	if (same) return;
	state.inputsChanged = false;
	state.lastFirst = first;
	state.lastCount = count;
	state.lastNow = now;
	state.lastSmooth = smooth;

	FrameBlock block = { first, count, state.capacity, now, smooth, { 0.0f, 0.0f, 0.0f }, {} };
	std::memcpy(block.groundMap, state.groundMap, sizeof(block.groundMap));
	upload(context, state.frameConstants, &block, sizeof(block));
	ID3D11ShaderResourceView *const views[4] = { state.spawnView, state.systemView, state.fadeView, state.groundView };
	context->CSSetShader(state.evaluate, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &state.frameConstants);
	context->CSSetShaderResources(0, 4, views);
	context->CSSetSamplers(0, 1, &state.linear);
	context->CSSetUnorderedAccessViews(0, 1, &state.particleWrite, nullptr);
	context->Dispatch((count + GroupSize - 1) / GroupSize, 1, 1);

	// the backend runs no compute, so nothing of its own is bound here to forget
	ID3D11ShaderResourceView *const noViews[4] = {};
	ID3D11UnorderedAccessView *const noWrite = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &noWrite, nullptr);
	context->CSSetShaderResources(0, 4, noViews);
	context->CSSetShader(nullptr, nullptr, 0);
}

void Draw(unsigned shaderType, const Camera &camera)
{
	DX11BackendClass *backend = Direct3D11_Backend();
	// the sun's depth map takes no particle, GPU or sorted
	if (state.evaluated == 0 || backend->Shadow_Map_Bound()) return;
	DX11DeviceClass *device = Direct3D11_Device();
	ID3D11DeviceContext *context = device->Get_Context();
	// A mirror draws into a target and depth of its own: the particles go there as they are, with no
	// fade, since the depth copy is the scene's and not the mirror's.
	ID3D11RenderTargetView *target = backend->Scene_Target();
	ID3D11DepthStencilView *targetDepth = backend->Scene_Depth_Target();
	ID3D11RenderTargetView *heldTarget = nullptr;
	ID3D11DepthStencilView *heldDepth = nullptr;
	ID3D11ShaderResourceView *depth = nullptr;
	if (target) {
		backend->Take_Scene_Depth();
		depth = backend->Soft_Particles_Allowed() ? backend->Scene_Depth_Copy() : nullptr;
	} else {
		context->OMGetRenderTargets(1, &heldTarget, &heldDepth);
		target = heldTarget;
		targetDepth = heldDepth;
	}
	if (!target) {
		if (heldDepth) heldDepth->Release();
		return;
	}

	PassBlock block;
	std::memcpy(block.view, camera.view, sizeof(block.view));
	std::memcpy(block.projection, camera.projection, sizeof(block.projection));
	// SOFT_PARTICLE_SAMPLING's distance from a depth, out of the projection the world was drawn with
	block.softParticleDepth[0] = camera.projection[11] * camera.projection[14];
	block.softParticleDepth[1] = camera.projection[11];
	block.softParticleDepth[2] = camera.projection[10];
	block.softParticleDepth[3] = 1.0f / SoftFadeUnits;
	block.count = state.evaluated;
	block.shaderType = shaderType;
	block.newestFirst = shaderType == Alpha ? 1u : 0u;
	block.soft = depth ? 1u : 0u;
	block.gain = (shaderType == Additive && backend->Draws_Into_Scene()) ? backend->Additive_Gain() : 1.0f;
	block.unused[0] = block.unused[1] = block.unused[2] = 0.0f;
	upload(context, state.passConstants, &block, sizeof(block));

	context->OMSetRenderTargets(1, &target, targetDepth);
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(state.drawVertex, nullptr, 0);
	context->VSSetConstantBuffers(0, 1, &state.passConstants);
	context->VSSetShaderResources(0, 1, &state.particleView);
	context->PSSetShader(state.drawPixel, nullptr, 0);
	context->PSSetConstantBuffers(0, 1, &state.passConstants);
	context->PSSetShaderResources(0, 1, &state.spriteView);
	context->PSSetShaderResources(7, 1, &depth);
	context->PSSetSamplers(0, 1, &state.linear);
	context->RSSetState(state.rasterizer);
	context->OMSetDepthStencilState(state.depthRead, 0);
	context->OMSetBlendState(shaderType == Additive ? state.additiveBlend : state.alphaBlend, nullptr, 0xffffffff);
	context->Draw(state.evaluated * 6, 0);

	ID3D11ShaderResourceView *const none = nullptr;
	context->VSSetShaderResources(0, 1, &none);
	context->PSSetShaderResources(0, 1, &none);
	context->PSSetShaderResources(7, 1, &none);
	if (heldTarget) heldTarget->Release();
	if (heldDepth) heldDepth->Release();
	backend->Forget_Bindings();
}

void ReleaseDevice() { release(); }

} // namespace GpuParticles
