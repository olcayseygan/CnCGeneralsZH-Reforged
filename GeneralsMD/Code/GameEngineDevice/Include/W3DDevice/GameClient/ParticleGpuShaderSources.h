// Exact upstream DX11 particle shaders shared with native SDL.
// Distributed under GPL-3.0; see LICENSE.md.
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
