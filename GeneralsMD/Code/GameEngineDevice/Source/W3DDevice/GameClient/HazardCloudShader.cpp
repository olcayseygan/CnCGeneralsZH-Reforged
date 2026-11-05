// Shared bounded world-space density integrated along each camera ray, and the
// nuclear embers drawn over it. One text for every backend.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloudCore.h"

namespace HazardCloud {
namespace {

// ", spaceN" out of every register(): SDL GPU's resource sets, which FXC refuses.
std::string withSpaces(std::string source, bool registerSpaces)
{
    if (registerSpaces) return source;
    for (std::size_t at = source.find(", space"); at != std::string::npos; at = source.find(", space", at))
        source.erase(at, 8);
    return source;
}

// The text is cut into several literals: MSVC refuses one longer than 16 KB (C2026).
std::string cloudShader(int pass)
{
    return std::string(pass == 1 ? "#define CLOUD_COMPOSITE 1\n" : "#define CLOUD_COMPOSITE 0\n")
        + "#define CLOUD_HEAT_PASS " + std::to_string(pass == 4 ? 1 : 0) + "\n"
        + "#define CLOUD_CACHE_PASS " + std::to_string(pass == 2 || pass == 3 ? pass - 1 : 0) + "\n" + R"HLSL(
Texture2D<float> SceneDepth : register(t0, space2);
SamplerState DepthSampler : register(s0, space2);
Texture2D<float4> InteractionField : register(t1, space2);
SamplerState InteractionSampler : register(s1, space2);
#if CLOUD_COMPOSITE
Texture2D<float4> CloudImage : register(t2, space2);
SamplerState CloudSampler : register(s2, space2);
#endif
#if CLOUD_HEAT_PASS
Texture2D<float4> HeatBackground : register(t5, space2);
SamplerState HeatSampler : register(s5, space2);
#endif
Texture2D<float4> CacheImage0 : register(t3, space2);
SamplerState CacheSampler0 : register(s3, space2);
Texture2D<float4> CacheImage1 : register(t4, space2);
SamplerState CacheSampler1 : register(s4, space2);
Texture2D<float> TerrainImage : register(t6, space2);
SamplerState TerrainSampler : register(s6, space2);

cbuffer SdlHazardCloudConstants : register(b0, space3)
{
    row_major float4x4 WorldFromClip;
    float4 CameraPosition;
    float4 Viewport;
    float4 Scene;
    float4 Field0;
    float4 Field1;
    float4 Shape0;
    float4 Shape1;
    float4 Footprints;
    float4 Source0;
    float4 Source1;
    float4 Settings;
    float4 Interaction;
    float4 FlowSettings;
    float4 Cache0;
    float4 Cache1;
    float4 CachePass;
    float4 PaletteMain[4];
    float4 PaletteShadow[4];
    float4 Terrain0;
    float4 Terrain1;
};

float volumeHash(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float volumeNoise(float3 p)
{
    float3 cell = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(volumeHash(cell), volumeHash(cell + float3(1, 0, 0)), f.x);
    float b = lerp(volumeHash(cell + float3(0, 1, 0)), volumeHash(cell + float3(1, 1, 0)), f.x);
    float c = lerp(volumeHash(cell + float3(0, 0, 1)), volumeHash(cell + float3(1, 0, 1)), f.x);
    float d = lerp(volumeHash(cell + float3(0, 1, 1)), volumeHash(cell + float3(1, 1, 1)), f.x);
    return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}

float4 volumeInteraction(float2 world)
{
    if (FlowSettings.x < 0.5) return float4(0, 0, 0, 0);
    float2 uv = (world - Interaction.xy) * Interaction.zw;
    if (any(uv < 0.0) || any(uv > 1.0)) return float4(0, 0, 0, 0);
    uint width, height;
    InteractionField.GetDimensions(width, height);
    float2 size = float2(width, height);
    float2 texel = uv * size - 0.5;
    float2 lower = floor(texel);
    float2 weight = frac(texel);
    // RGBA32F linear filtering is not universal. The preview binds a point
    // sampler and interpolates four texel centres explicitly instead.
    float2 a = (clamp(lower, float2(0, 0), size - 1.0) + 0.5) / size;
    float2 b = (clamp(lower + 1.0, float2(0, 0), size - 1.0) + 0.5) / size;
    float4 top = lerp(InteractionField.SampleLevel(InteractionSampler, a, 0),
        InteractionField.SampleLevel(InteractionSampler, float2(b.x, a.y), 0), weight.x);
    float4 bottom = lerp(InteractionField.SampleLevel(InteractionSampler, float2(a.x, b.y), 0),
        InteractionField.SampleLevel(InteractionSampler, b, 0), weight.x);
    return lerp(top, bottom, weight.y);
}

float3 volumeWorldAt(float2 pixel, float depth)
{
    float2 uv = (pixel - Viewport.xy) / max(Viewport.zw, float2(1, 1));
    float4 world = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1), WorldFromClip);
    float w = abs(world.w) < 0.000001 ? (world.w < 0 ? -0.000001 : 0.000001) : world.w;
    return world.xyz / w;
}

// Terrain grids include both support boundaries and are packed side by side.
// Point samples with explicit bilinear interpolation also work on devices that
// cannot linearly filter R32F. Disabled fields retain the approved planar path.
float volumeGround(float2 world, float4 field, float4 shape, float fringe, float identity)
{
    float4 terrain = identity < 0.5 ? Terrain0 : Terrain1;
    if (terrain.z < 0.5) return field.z - shape.x;
    uint width, height;
    TerrainImage.GetDimensions(width, height);
    float side = float(height);
    float radius = field.w + fringe + FlowSettings.y;
    float2 uv = saturate((world - field.xy + radius) / max(radius * 2.0, 0.001));
    float2 texel = uv * (side - 1.0);
    float2 lower = floor(texel), weight = frac(texel);
    float2 upper = min(lower + 1.0, side - 1.0);
    float2 offset = float2(identity < 0.5 ? 0.0 : side, 0.0);
    float2 size = float2(width, height);
    float2 a = (lower + offset + 0.5) / size;
    float2 b = (upper + offset + 0.5) / size;
    float top = lerp(TerrainImage.SampleLevel(TerrainSampler, a, 0),
        TerrainImage.SampleLevel(TerrainSampler, float2(b.x, a.y), 0), weight.x);
    float bottom = lerp(TerrainImage.SampleLevel(TerrainSampler, float2(a.x, b.y), 0),
        TerrainImage.SampleLevel(TerrainSampler, b, 0), weight.x);
    return lerp(top, bottom, weight.y);
}

bool volumeInterval(float3 origin, float3 direction, float4 field, float4 shape, float fringe, float identity, float sceneLimit,
                    out float first, out float last)
{
    first = last = 0.0;
    if (field.w <= 0.001 || shape.x <= 0.001 || shape.y <= 0.00001) return false;
    float margin = FlowSettings.x > 0.5 ? max(FlowSettings.y, 0.0) : 0.0;
    float3 extent = float3(field.w + fringe + margin, field.w + fringe + margin, shape.x);
    float3 safeDirection = float3(
        abs(direction.x) < 0.000001 ? (direction.x < 0 ? -0.000001 : 0.000001) : direction.x,
        abs(direction.y) < 0.000001 ? (direction.y < 0 ? -0.000001 : 0.000001) : direction.y,
        abs(direction.z) < 0.000001 ? (direction.z < 0 ? -0.000001 : 0.000001) : direction.z);
    float3 low = field.xyz - extent;
    float3 high = field.xyz + extent;
    float4 terrain = identity < 0.5 ? Terrain0 : Terrain1;
    if (terrain.z > 0.5) {
        low.z = terrain.x;
        high.z = terrain.y + shape.x * 2.0;
    }
    float3 lo = (low - origin) / safeDirection;
    float3 hi = (high - origin) / safeDirection;
    float3 nearAxis = min(lo, hi);
    float3 farAxis = max(lo, hi);
    first = max(0.0, max(nearAxis.x, max(nearAxis.y, nearAxis.z)));
    last = min(sceneLimit, min(farAxis.x, min(farAxis.y, farAxis.z)));
    return last > first;
}

// Smooth, overlapping density lobes, evaluated in 3D rather than drawn as
// sprites. Jittered cells distribute billows at a stable world size; their tops
// vary independently so the gas has no single dome or flat opaque ceiling.
float volumeBillows(float2 p, float h, float erosion)
{
    float2 cell = floor(p);
    float largest = 0.0, total = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 c = cell + float2(x, y);
            float a = volumeHash(float3(c, 3.7));
            float b = volumeHash(float3(c, 19.1));
            float2 centre = c + float2(0.10 + a * 0.80, 0.10 + b * 0.80);
            float distance = length(p - centre);
            // The existing compact-support factor is exactly zero here.
            // Skip the remaining hash and lobe math without changing support.
            if (distance >= 1.09) continue;
            float k = volumeHash(float3(c, 43.4));
            float2 q = (p - centre) / (0.55 + k * 0.50);
            float z = (h - (0.18 + a * 0.26)) / (0.28 + b * 0.27);
            float body = saturate(1.0 - dot(q, q) - z * z + erosion);
            body *= sqrt(body);
            // A cell outside this 3x3 neighborhood is at least 1.10 cells
            // away. Keep eroded/expanded lobes compact before that boundary
            // so changing the neighborhood cannot reveal a density seam.
            body *= 1.0 - smoothstep(0.95, 1.09, distance);
            largest = max(largest, body);
            total += body;
        }
    }
    return largest * 0.55 + total * 0.45;
}

)HLSL" R"HLSL(
// The hazard radius controls readable coverage. The separate soft fringe matches
// the original renderer's textured puddles, which extend beyond their centres.
float volumeDensity(float3 world, float4 field, float4 shape, float4 source, float fringe, float identity)
{
    if (field.w <= 0.001 || shape.y <= 0.00001) return 0.0;
    float halfHeight = max(shape.x, 0.25);
    float ground = volumeGround(world.xy, field, float4(halfHeight, shape.yzw), fringe, identity);
    float h = (world.z - ground) / (2.0 * halfHeight);
    if (h <= 0.0 || h >= 1.0) return 0.0;
    float2 local = world.xy - field.xy;
    float radius = length(local);
    float outerRadius = field.w + fringe;
    if (radius >= outerRadius + FlowSettings.y) return 0.0;

    float heightWeight = 1.0 - 0.65 * smoothstep(0.40, 1.0, h);
    float2 displacement = volumeInteraction(world.xy).xy;
    displacement *= min(1.0, max(FlowSettings.y, 0.0) / max(length(displacement), 0.0001));
    float2 advected = local - displacement * heightWeight;
    float movingRadius = length(advected);

    // These masks are independent of procedural detail. Reject only exactly
    // zero contributions, retaining the full veil and the original billow math.
    float coverage = 1.0 - smoothstep(field.w, max(field.w + 1.0, outerRadius), radius);
    float outside = smoothstep(field.w * 0.95, max(field.w + 1.0, outerRadius), radius);
    float veil = 0.70 * (1.0 - smoothstep(0.18, 0.55, h));
    float bulkEdge = 1.0 - smoothstep(field.w * 0.78, field.w + fringe * 0.68, movingRadius);
    float front = field.w * lerp(0.12, 1.0, sqrt(saturate(shape.w))) + fringe * 0.68 * shape.w;
    float formation = 1.0 - smoothstep(max(0.0, front - 18.0), front + 12.0, movingRadius);
    float topFade = 1.0 - smoothstep(0.90, 1.0, h);
    bool hasBulk = bulkEdge > 0.0 && formation > 0.0 && topFade > 0.0;
    bool hasVeil = coverage > 0.0 && veil > 0.0;
    if (!hasBulk && !hasVeil) return 0.0;
    if (!hasBulk && outside == 0.0) {
        // Inside the footprint, the veil has no procedural modulation.
        float density = (coverage * veil) * shape.y;
        density *= smoothstep(0.0, 0.045, h);
        return density;
    }

    float time = source.w;
    // Nuclear residue fades in over the entire field; it does not transport
    // billows outward from a central emitter like a toxin release.
    bool nuclear = shape.z >= 0.5 && shape.z < 1.5;
    float expansion = nuclear ? 1.0 : exp(-min(source.y / max(field.w + fringe, 1.0) * 0.35, 0.5));

    // World-sized billows: a large bomb has more curls, never one enlarged blob.
    // Vertical coordinates retain several folds inside the infantry-height slab.
    float3 at = float3((field.xy + advected * expansion) * 0.048, h * 1.6);
    at += float3(identity * 9.7, identity * 3.1, identity * 13.2);
    at.xy += float2(-0.030, 0.018) * time + float2(0.048, -0.040) * time * h;
    at.z -= time * 0.12;
    float curlX = volumeNoise(at * 0.57 + float3(4.2, 1.7, time * 0.025)) - 0.5;
    float curlY = volumeNoise(at * 0.57 + float3(1.3, 8.1, -time * 0.020)) - 0.5;
    at += float3(curlX, curlY, (curlX - curlY) * 0.55) * 0.85;
    float broad = volumeNoise(at);
    float folds = volumeNoise(at * 2.03 + float3(7.1, 3.7, 11.3));
    float fine = volumeNoise(at * 4.09 + float3(11.7, 8.3, 2.1));
    float detail = broad * 0.56 + folds * 0.30 + fine * 0.14;

    // A shallow continuous veil makes the whole hazard legible, even between
    // billows or while they are forming. Infantry advect the curls, not this
    // coverage mask, so their wakes cannot suggest a safe hole in the hazard.
    coverage *= lerp(1.0, 0.20 + 0.80 * smoothstep(0.20, 0.80, detail), outside);

    // Volume height is independent of the horizontal footprint. Individual
    // rolling tops replace the old ellipsoid and its shrinking ground contact.
    float bulk = 0.0;
    if (hasBulk) {
        float erosion = (folds - 0.5) * 0.55 + (fine - 0.5) * 0.20 - source.z * 0.14;
        float billow = volumeBillows(at.xy, h, erosion) * 0.72
            + volumeBillows(at.xy * 0.58 + float2(17.3, 29.1), h, erosion * 0.75) * 0.46;
        bulk = billow * bulkEdge * formation * 2.30 * topFade;
    }
    float density = (coverage * veil + bulk) * shape.y;
    // Soft ground contact and a naturally broken upper surface; no actor capsule
    // cutout, pressure rim, or artificial ring is added to the density.
    density *= smoothstep(0.0, 0.045, h);
    return density;
}

// Each slice has its own texel-centred XY rectangle. Clamp to the last texel
// centre before hardware bilinear filtering, so adjacent atlas slices cannot
// bleed into one another. Interpolate Z explicitly between two slices.
float4 volumeCacheRead(float3 world, float4 field, float4 shape, float fringe, float4 grid, float identity)
{
    if (shape.x <= 0.001 || shape.y <= 0.00001 || grid.z < 2.0) return float4(0, 0, 0, 0);
    float radius = field.w + fringe + FlowSettings.y;
    float ground = volumeGround(world.xy, field, shape, fringe, identity);
    float3 uv = (world - float3(field.xy - radius, ground))
        / float3(radius * 2.0, radius * 2.0, shape.x * 2.0);
    if (any(uv < 0.0) || any(uv > 1.0)) return float4(0, 0, 0, 0);
    float3 voxel = saturate(uv) * (grid.xyz - 1.0);
    uint z0 = (uint)floor(voxel.z), z1 = min(z0 + 1, (uint)grid.z - 1);
    uint columns = (uint)grid.w, rows = ((uint)grid.z + columns - 1) / columns;
    float2 atlasSize = grid.xy * float2(columns, rows);
    float2 tile0 = float2(z0 % columns, z0 / columns) * grid.xy;
    float2 tile1 = float2(z1 % columns, z1 / columns) * grid.xy;
    float2 uv0 = (tile0 + voxel.xy + 0.5) / atlasSize;
    float2 uv1 = (tile1 + voxel.xy + 0.5) / atlasSize;
    // Bind resources globally: this HLSL compiler emits invalid SPIR-V stores
    // for resource-valued function parameters, even though HLSL accepts them.
    float4 a, b;
    if (identity < 0.5) {
        a = CacheImage0.SampleLevel(CacheSampler0, uv0, 0);
        b = CacheImage0.SampleLevel(CacheSampler0, uv1, 0);
    } else {
        a = CacheImage1.SampleLevel(CacheSampler1, uv0, 0);
        b = CacheImage1.SampleLevel(CacheSampler1, uv1, 0);
    }
    return lerp(a, b, frac(voxel.z));
}

float4 volumeCached(float3 world, float4 field, float4 shape, float fringe, float identity)
{
    if (identity < 0.5)
        return volumeCacheRead(world, field, shape, fringe, Cache0, identity);
    return volumeCacheRead(world, field, shape, fringe, Cache1, identity);
}

float volumeExtinction(float4 shape)
{
    return 0.075 * min(1.8, pow(9.3 / max(shape.x, 0.25), 0.35));
}

uint volumePalette(float palette)
{
    return (uint)clamp(floor(palette + 0.5), 0.0, 3.0);
}

float3 volumeScattered(float palette, float sunlight)
{
    uint index = volumePalette(palette);
    return PaletteShadow[index].rgb + PaletteMain[index].rgb * (0.12 + 0.88 * sunlight) * 0.84;
}

// RGB holds scattered-light coefficient, A holds extinction. Integrating the
// sum still mixes overlapping clouds in world space rather than sorting layers.
void volumeSample(float3 world, float4 field, float4 shape, float4 source, float fringe, float identity,
                  inout float optical, inout float3 light)
{
    float4 coefficients = volumeCached(world, field, shape, fringe, identity);
    optical += coefficients.a;
    light += coefficients.rgb;
}

)HLSL" R"HLSL(
#if CLOUD_CACHE_PASS
float4 volumeBuildCache(float2 pixel)
{
    float identity = CachePass.x;
    float4 field = identity < 0.5 ? Field0 : Field1;
    float4 shape = identity < 0.5 ? Shape0 : Shape1;
    float4 source = identity < 0.5 ? Source0 : Source1;
    float fringe = identity < 0.5 ? Footprints.x : Footprints.y;
    float4 grid = identity < 0.5 ? Cache0 : Cache1;
    uint2 texel = (uint2)floor(pixel), dimensions = (uint2)grid.xy;
    uint slice = texel.x / dimensions.x + (texel.y / dimensions.y) * (uint)grid.w;
    if (slice >= (uint)grid.z) return float4(0, 0, 0, 0);
    float3 voxel = float3(texel.x % dimensions.x, texel.y % dimensions.y, slice);
    float radius = field.w + fringe + FlowSettings.y;
    float3 world = float3(field.xy - radius, field.z - shape.x)
        + voxel / (grid.xyz - 1.0) * float3(radius * 2.0, radius * 2.0, shape.x * 2.0);
    float4 terrain = identity < 0.5 ? Terrain0 : Terrain1;
    if (terrain.z > 0.5)
        world.z = volumeGround(world.xy, field, shape, fringe, identity)
            + voxel.z / (grid.z - 1.0) * shape.x * 2.0;
#if CLOUD_CACHE_PASS == 1
    return float4(volumeDensity(world, field, shape, source, fringe, identity), 0, 0, 1);
#else
    float density = volumeCached(world, field, shape, fringe, identity).r;
    if (density < 0.001) return float4(0, 0, 0, 0);
    const float3 sun = float3(-0.462, -0.347, 0.816);
    float scale = max(shape.x, 0.25) / 9.3;
    float optical = volumeCached(world + sun * (2.5 * scale), field, shape, fringe, identity).r * 5.0;
    optical += volumeCached(world + sun * (8.0 * scale), field, shape, fringe, identity).r * 10.0;
    optical += volumeCached(world + sun * (16.0 * scale), field, shape, fringe, identity).r * 14.0;
    // Change optical thickness without changing the cloud's footprint, billows
    // or source history. The same multiplier softens its internal shadows.
    float extinction = volumeExtinction(shape) * PaletteShadow[volumePalette(shape.z)].w;
    float sunlight = exp(-optical * extinction * scale * 1.20);
    float coefficient = density * extinction;
    if (shape.z >= 0.5 && shape.z < 1.5) {
        // Nuclear smoke carries rolling charcoal billows above the warm ground
        // haze. Bake their optical properties here, so they cost no additional
        // camera-ray samples and share the existing hazard support and height.
        float h = saturate((world.z - volumeGround(world.xy, field, shape, fringe, identity))
            / max(shape.x * 2.0, 0.5));
        float age = source.w;
        float3 at = float3(world.xy * 0.041, h * 1.85);
        at += float3(-0.024 * age, 0.016 * age, -0.17 * age);
        at += float3(12.7, 31.4, 8.6);
        float broad = volumeNoise(at);
        float folds = volumeNoise(at * 2.07 + float3(6.1, 17.3, 4.8));
        float soot = smoothstep(0.48, 0.69, broad * 0.72 + folds * 0.28);
        // Retain the amber veil and lighter billow rims. The plume's own
        // density still determines coverage, formation front, and dissipation.
        soot *= smoothstep(0.06, 0.25, h);
        soot *= smoothstep(0.25, 1.10, density / max(shape.y, 0.00001));
        soot *= smoothstep(0.0, 1.20, age) * smoothstep(0.05, 0.55, shape.w);
        soot *= 1.0 - 0.60 * saturate(source.z);
        float sootLight = sqrt(saturate(sunlight));
        float3 charcoal = float3(0.055, 0.052, 0.047)
            + float3(0.45, 0.43, 0.39) * sootLight;
        // Offset the approved neutral charcoal with the shadow color. Avoid
        // dividing by its small default channels, which would amplify blue
        // edits into clipped, luminous billows instead of a usable tint.
        charcoal = saturate(charcoal + (PaletteShadow[1].rgb - float3(0.115, 0.085, 0.030)));
        float3 scattered = lerp(volumeScattered(shape.z, sunlight), charcoal, soot * 0.80);
        coefficient *= 1.0 + 0.08 * soot;
        return float4(scattered * coefficient * PaletteMain[1].w, coefficient);
    }
    return float4(volumeScattered(shape.z, sunlight) * coefficient * PaletteMain[volumePalette(shape.z)].w, coefficient);
#endif
}
#endif

void volumeIntegrate(float3 origin, float3 direction, float first, float last,
                     bool include0, bool include1, int steps, float jitter,
                     inout float3 radiance, inout float transmittance)
{
    if (last <= first || transmittance < 0.015) return;
    float stride = (last - first) / float(steps);
    [loop] for (int sampleIndex = 0; sampleIndex < 96; ++sampleIndex) {
        if (sampleIndex >= steps || transmittance < 0.015) break;
        float3 world = origin + direction * (first + (float(sampleIndex) + jitter) * stride);
        float optical = 0.0;
        float3 light = float3(0, 0, 0);
        if (include0) volumeSample(world, Field0, Shape0, Source0, Footprints.x, 0.0, optical, light);
        if (include1) volumeSample(world, Field1, Shape1, Source1, Footprints.y, 1.0, optical, light);
        if (optical < 0.000001) continue;
        float opacity = 1.0 - exp(-optical * stride);
        radiance += transmittance * opacity * light / optical;
        transmittance *= 1.0 - opacity;
    }
}

float4 volumeRaymarch(float2 pixel)
{
    if (Settings.z < 0.5 || Scene.z < 0.5 || Settings.x < 0.5) return float4(0, 0, 0, 0);
    // Starting on the near plane works for perspective and orthographic views,
    // including a camera inside the cloud. Depth clips the ray itself, so smoke
    // in front of an infantryman remains visible while smoke behind him does not.
    float3 origin = volumeWorldAt(pixel, 0.0);
    float3 farPoint = volumeWorldAt(pixel, 1.0);
    float3 direction = normalize(farPoint - origin);
    float sceneDepth = SceneDepth.SampleLevel(DepthSampler, pixel * Scene.xy, 0).r;
    float sceneLimit = 100000.0;
    if (sceneDepth < 0.999999) {
        float3 opaqueWorld = volumeWorldAt(pixel, sceneDepth);
        sceneLimit = max(0.0, dot(opaqueWorld - origin, direction));
    }
    float near0, far0, near1, far1;
    bool hit0 = volumeInterval(origin, direction, Field0, Shape0, Footprints.x, 0.0, sceneLimit, near0, far0);
    bool hit1 = volumeInterval(origin, direction, Field1, Shape1, Footprints.y, 1.0, sceneLimit, near1, far1);
    hit1 = hit1 && Settings.x > 1.5;
    if (!hit0 && !hit1) return float4(0, 0, 0, 0);
    if (!hit0) { near0 = 100001.0; far0 = 0.0; }
    if (!hit1) { near1 = 100001.0; far1 = 0.0; }

    float jitter = 0.35 + volumeHash(float3(floor(pixel), 17.0)) * 0.30;
    float3 radiance = float3(0, 0, 0);
    float transmittance = 1.0;
    int singleSteps = (int)clamp(Settings.w * 0.5, 32.0, 96.0);
    // A hill broadens the conservative Z interval without making the local
    // cloud thicker. Spend more samples there so the thin occupied parts do
    // not disappear between steps. Flat/deck fields keep their exact budget.
    float terrainScale = 1.0;
    if (hit0 && Terrain0.z > 0.5)
        terrainScale = max(terrainScale, 1.0 + max(0.0, Terrain0.y - Terrain0.x)
            / max(Shape0.x * 2.0, 0.5));
    if (hit1 && Terrain1.z > 0.5)
        terrainScale = max(terrainScale, 1.0 + max(0.0, Terrain1.y - Terrain1.x)
            / max(Shape1.x * 2.0, 0.5));
    if (terrainScale > 1.0)
        singleSteps = (int)min(96.0, ceil(float(singleSteps) * terrainScale));
    if (hit0 && hit1 && min(far0, far1) > max(near0, near1)) {
        float first = min(near0, near1);
        float last = max(far0, far1);
        float longest = max(far0 - near0, far1 - near1);
        // Matching slabs need the same sample spacing as a single field.
        // Increase the budget only as their combined interval grows, while
        // still summing both fields' density and light at every sample.
        int jointSteps = (int)clamp(ceil(float(singleSteps) * ((last - first) / longest)),
            float(singleSteps), 96.0);
        volumeIntegrate(origin, direction, first, last, true, true, jointSteps, jitter, radiance, transmittance);
    } else if (near0 <= near1) {
        volumeIntegrate(origin, direction, near0, far0, true, false, singleSteps, jitter, radiance, transmittance);
        volumeIntegrate(origin, direction, near1, far1, false, true, singleSteps, jitter, radiance, transmittance);
    } else {
        volumeIntegrate(origin, direction, near1, far1, false, true, singleSteps, jitter, radiance, transmittance);
        volumeIntegrate(origin, direction, near0, far0, true, false, singleSteps, jitter, radiance, transmittance);
    }
    float opacity = 1.0 - transmittance;
    // Premultiplied radiance can be interpolated without dark fringes. The
    // composite uses ONE/INVSRCALPHA; density and lighting remain unchanged.
    return float4(radiance, opacity);
}

float2 volumeFullPixel(float2 lowPixel)
{
    // Each half-resolution texel represents one exact full-resolution pixel.
    // Clamp the final sample for odd target sizes, retaining the camera's
    // original viewport and depth coordinates in both passes.
    return min(floor(lowPixel) * 2.0 + 1.5, round(1.0 / Scene.xy) - 0.5);
}

)HLSL" R"HLSL(
#if CLOUD_COMPOSITE || CLOUD_HEAT_PASS
float volumeDepthSlope(float2 pixel, float depth, float2 axis, float maximumStep)
{
    float before = depth - SceneDepth.SampleLevel(DepthSampler, (pixel - axis) * Scene.xy, 0).r;
    float after = SceneDepth.SampleLevel(DepthSampler, (pixel + axis) * Scene.xy, 0).r - depth;
    // Pick the neighbor on the same surface. At a thin silhouette both sides
    // can cross an edge: discard large jumps instead of extending that edge as
    // a plane. Device depth is affine across a rasterized surface triangle.
    float slope = abs(before) < abs(after) ? before : after;
    float distance = length(volumeWorldAt(pixel, depth + slope) - volumeWorldAt(pixel, depth));
    return distance <= maximumStep ? slope : 0.0;
}
#endif

#if CLOUD_HEAT_PASS
bool volumeHeatContains(float3 world, float4 field, float4 shape, float fringe, float identity)
{
    if (shape.z < 0.5 || shape.z >= 1.5 || shape.y <= 0.00001 || field.w <= 0.001)
        return false;
    float ground = volumeGround(world.xy, field, shape, fringe, identity);
    float2 local = world.xy - field.xy;
    float radius = field.w + fringe;
    return dot(local, local) <= radius * radius
        && world.z >= ground - 0.65 && world.z <= ground + shape.x * 5.2;
}

float2 volumeHeatOffset(float3 origin, float3 direction, float limit, bool immersed,
                        float4 field, float4 shape, float4 source, float fringe, float identity)
{
    // Palette 1 identifies nuclear heat. Green toxin, blue anthrax and purple
    // gamma have no refraction, even when their volume caches are reused.
    if (shape.z < 0.5 || shape.z >= 1.5 || shape.y <= 0.00001) return float2(0, 0);
    float4 heatField = field;
    float4 heatShape = shape;
    heatShape.x *= 2.6;
    heatField.z += heatShape.x - shape.x; // same ground, gently rising hot air
    float first, last;
    // Use one continuous warp through an immersed unit and its surroundings.
    // Stopping the wave at the unit's depth gives its silhouette a different
    // displacement from the adjacent ground, leaving a doubled edge.
    float waveLimit = immersed ? 100000.0 : limit;
    if (!volumeInterval(origin, direction, heatField, heatShape, fringe, identity, waveLimit, first, last))
        return float2(0, 0);
    float3 world = origin + direction * ((first + last) * 0.5);
    float h = saturate((world.z - volumeGround(world.xy, field, shape, fringe, identity))
        / (heatShape.x * 2.0));
    float radial = 1.0 - smoothstep(field.w * 0.45, field.w + fringe * 0.65, length(world.xy - field.xy));
    float heightFade = 1.0 - smoothstep(0.35, 1.0, h);
    float path = saturate((last - first) / max(shape.x * 2.0, 1.0));
    float strength = radial * heightFade * path * saturate(shape.y)
        * lerp(0.45, 1.0, shape.w) * (0.72 + 0.28 * source.x);
    if (strength <= 0.001) return float2(0, 0);
    // The midpoint of a ray through a flat slab has nearly constant Z. Use a
    // vertical curtain through the field centre for phase, so the visible folds
    // actually travel upward through the air instead of merely flickering.
    float horizontal = dot(direction.xy, direction.xy);
    float curtain = dot(field.xy - origin.xy, direction.xy) / max(horizontal, 0.0001);
    float2 curtainXY = origin.xy + direction.xy * curtain;
    float waveZ = horizontal > 0.0001
        ? origin.z + direction.z * curtain - volumeGround(curtainXY, field, shape, fringe, identity)
        : world.y - field.y;
    float2 local = world.xy - field.xy;
    float turbulence = volumeNoise(float3(local * 0.055, waveZ * 0.10 - source.w * 0.5)) * 2.0 - 1.0;
    // Broad lateral variation keeps immersed silhouettes from being stretched
    // by a sharply changing displacement across just a few screen pixels.
    float phase = waveZ * 0.17 - source.w * 3.4 + 0.9 * sin(dot(local, float2(0.028, 0.015)));
    float2 ripple = float2(
        0.80 * sin(phase + turbulence * 0.5) + 0.20 * sin(phase * 1.41 + local.x * 0.025 + 1.7),
        0.22 * cos(phase * 1.21 + local.y * 0.025) + 0.10 * turbulence);
    return ripple * strength;
}

bool volumeHeatDepthSafe(float2 displaced, float2 pixel, float depth, float2 slope, float tolerance,
                         uint immersed)
{
    // Validate all four bilinear colour taps against the local depth plane.
    // An immersed unit and the ground in the same hot field may refract across
    // their shared silhouette. Foreground objects outside it remain protected.
    float2 lower = floor(displaced - 0.5) + 0.5;
    [unroll] for (int y = 0; y < 2; ++y) {
        [unroll] for (int x = 0; x < 2; ++x) {
            float2 tap = clamp(lower + float2(x, y), Viewport.xy, Viewport.xy + Viewport.zw - 1.0);
            float actual = SceneDepth.SampleLevel(DepthSampler, tap * Scene.xy, 0).r;
            float expected = depth + dot(slope, tap - pixel);
            float3 actualWorld = volumeWorldAt(tap, actual);
            float difference = length(actualWorld - volumeWorldAt(tap, expected));
            if (difference > tolerance) {
                bool sharedHotAir = ((immersed & 1u) != 0
                    && volumeHeatContains(actualWorld, Field0, Shape0, Footprints.x, 0.0))
                    || ((immersed & 2u) != 0
                    && volumeHeatContains(actualWorld, Field1, Shape1, Footprints.y, 1.0));
                if (!sharedHotAir) return false;
            }
        }
    }
    return true;
}

float4 volumeHeatColour(float2 pixel, float depth, float3 opaqueWorld, float2 slope, float tolerance)
{
    if (Settings.y <= 0.0) return float4(0, 0, 0, 0);
    float3 origin = volumeWorldAt(pixel, 0.0);
    float3 direction = normalize(volumeWorldAt(pixel, 1.0) - origin);
    float limit = depth < 0.999999 ? max(0.0, dot(opaqueWorld - origin, direction)) : 100000.0;
    uint immersed = volumeHeatContains(opaqueWorld, Field0, Shape0, Footprints.x, 0.0) ? 1u : 0u;
    if (Settings.x > 1.5 && volumeHeatContains(opaqueWorld, Field1, Shape1, Footprints.y, 1.0)) immersed |= 2u;
    float2 offset = volumeHeatOffset(origin, direction, limit, (immersed & 1u) != 0,
        Field0, Shape0, Source0, Footprints.x, 0.0);
    if (Settings.x > 1.5)
        offset += volumeHeatOffset(origin, direction, limit, (immersed & 2u) != 0,
            Field1, Shape1, Source1, Footprints.y, 1.0);
    offset *= Settings.y * clamp(Viewport.w / 800.0, 0.65, 1.5);
    float magnitude = length(offset);
    offset *= min(1.0, 8.0 / max(magnitude, 0.0001));
    if (magnitude < 0.01) return float4(0, 0, 0, 0);
    // Near a silhouette, try a smaller bend rather than pull foreground pixels
    // into the haze or impose a broad undistorted border around the infantry.
    [loop] for (int attempt = 0; attempt < 3; ++attempt) {
        float2 displaced = clamp(pixel + offset, Viewport.xy, Viewport.xy + Viewport.zw - 1.0);
        if (volumeHeatDepthSafe(displaced, pixel, depth, slope, tolerance, immersed)) {
            float3 refracted = HeatBackground.SampleLevel(HeatSampler, displaced * Scene.xy, 0).rgb;
            return float4(refracted, 1.0);
        }
        offset *= 0.5;
    }
    return float4(0, 0, 0, 0);
}
#endif

)HLSL" R"HLSL(
float4 main(float4 position : SV_Position) : SV_Target
{
#if CLOUD_HEAT_PASS
    if (Settings.z < 0.5 || Scene.z < 0.5 || Settings.x < 0.5 || Settings.y <= 0.0)
        return float4(0, 0, 0, 0);
    float depth = SceneDepth.SampleLevel(DepthSampler, position.xy * Scene.xy, 0).r;
    float3 world = volumeWorldAt(position.xy, depth);
    float pixelSpan = length(volumeWorldAt(position.xy + float2(1, 0), depth) - world);
    float tolerance = max(0.65, pixelSpan * 0.75);
    float2 slope = float2(
        volumeDepthSlope(position.xy, depth, float2(1, 0), pixelSpan * 4.0),
        volumeDepthSlope(position.xy, depth, float2(0, 1), pixelSpan * 4.0));
    return volumeHeatColour(position.xy, depth, world, slope, tolerance);
#elif CLOUD_CACHE_PASS
    return volumeBuildCache(position.xy);
#elif !CLOUD_COMPOSITE
    return volumeRaymarch(volumeFullPixel(position.xy));
#else
    if (Settings.z < 0.5 || Scene.z < 0.5 || Settings.x < 0.5) return float4(0, 0, 0, 0);
    uint width, height;
    CloudImage.GetDimensions(width, height);
    float2 size = float2(width, height);
    float2 grid = (position.xy - 1.5) * 0.5;
    // An odd target has a final sample only one pixel from its predecessor.
    // Select that exact sample on the final row/column rather than blending it.
    float2 fullSize = round(1.0 / Scene.xy);
    if (position.x >= fullSize.x - 0.5) grid.x = size.x - 1.0;
    if (position.y >= fullSize.y - 0.5) grid.y = size.y - 1.0;
    float2 lower = floor(grid);
    float2 fraction = frac(grid);
    float centreDepth = SceneDepth.SampleLevel(DepthSampler, position.xy * Scene.xy, 0).r;
    float3 centreWorld = volumeWorldAt(position.xy, centreDepth);
    float pixelSpan = length(volumeWorldAt(position.xy + float2(1, 0), centreDepth) - centreWorld);
    float tolerance = max(0.65, pixelSpan * 0.75);
    float2 slope = float2(
        volumeDepthSlope(position.xy, centreDepth, float2(1, 0), pixelSpan * 4.0),
        volumeDepthSlope(position.xy, centreDepth, float2(0, 1), pixelSpan * 4.0));
    float4 result = float4(0, 0, 0, 0);
    float total = 0.0;
    [unroll] for (int y = 0; y < 2; ++y) {
        [unroll] for (int x = 0; x < 2; ++x) {
            float2 texel = clamp(lower + float2(x, y), float2(0, 0), size - 1.0);
            float2 tapPixel = volumeFullPixel(texel);
            float tapDepth = SceneDepth.SampleLevel(DepthSampler, tapPixel * Scene.xy, 0).r;
            // Compare distance along the current ray, not nonlinear device Z.
            // Never interpolate smoke clipped at an unrelated foreground body
            // into the background, or background smoke across an infantryman.
            float expectedDepth = centreDepth + dot(slope, tapPixel - position.xy);
            float difference = length(volumeWorldAt(tapPixel, tapDepth) - volumeWorldAt(tapPixel, expectedDepth));
            float weight = (x ? fraction.x : 1.0 - fraction.x) * (y ? fraction.y : 1.0 - fraction.y);
            weight *= difference <= tolerance ? 1.0 : 0.0;
            result += CloudImage.SampleLevel(CloudSampler, (texel + 0.5) / size, 0) * weight;
            total += weight;
        }
    }
    // A thin silhouette may be absent from all four low-resolution samples.
    // Integrate that pixel at full resolution rather than blur through it.
    if (total < 0.001) return volumeRaymarch(position.xy);
    return result / total;
#endif
}
)HLSL";
}

// Sparse nuclear embers. No assets, gameplay particles or damage are involved.
const char *emberVertexSource = R"HLSL(
Texture2D<float> TerrainHeight : register(t0, space0);
SamplerState TerrainSampler : register(s0, space0);
cbuffer EmberConstants : register(b0, space1)
{
    row_major float4x4 ViewProjection;
    float4 Viewport;
    float4 Field;
    float4 Shape;
    float4 Scene;
    float4 Terrain;
};
struct EmberVertex {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float2 glow : TEXCOORD1;
};
float emberHash(float seed)
{
    float3 p = frac(float3(seed, seed + 1.0, seed + 2.0) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
float emberGround(float2 world)
{
    if (Terrain.x < 0.5) return Field.z;
    uint width, height;
    TerrainHeight.GetDimensions(width, height);
    float side = float(height);
    bool sharedGrid = Terrain.y < 0.0;
    float2 uv = sharedGrid ? saturate((world - Terrain.zw) * Scene.zw)
        : saturate((world - Field.xy) / max(Terrain.z * 2.0, 0.001) + 0.5);
    float2 texel = uv * (side - 1.0);
    float2 lower = floor(texel), upper = min(lower + 1.0, side - 1.0);
    float2 weight = frac(texel);
    float gridOffset = sharedGrid ? 0.0 : Terrain.y * side;
    float2 a = (lower + float2(gridOffset, 0) + 0.5) / float2(width, height);
    float2 b = (upper + float2(gridOffset, 0) + 0.5) / float2(width, height);
    float low = lerp(TerrainHeight.SampleLevel(TerrainSampler, a, 0),
        TerrainHeight.SampleLevel(TerrainSampler, float2(b.x, a.y), 0), weight.x);
    float high = lerp(TerrainHeight.SampleLevel(TerrainSampler, float2(a.x, b.y), 0),
        TerrainHeight.SampleLevel(TerrainSampler, b, 0), weight.x);
    return lerp(low, high, weight.y);
}
EmberVertex main(uint vertex : SV_VertexID)
{
    uint particle = vertex / 6;
    uint corner = vertex % 6;
    float2 local;
    if (corner == 0 || corner == 3) local = float2(-1, -1);
    else if (corner == 1) local = float2(1, -1);
    else if (corner == 2 || corner == 4) local = float2(1, 1);
    else local = float2(-1, 1);

    float seed = float(particle) * 13.61 + Field.x * 0.21 + Field.y * 0.37;
    float random = emberHash(seed + 7.0);
    float lifetime = 3.4 + emberHash(seed + 2.0) * 2.8;
    float age = frac(Scene.x / lifetime + emberHash(seed + 19.0));
    // One anchor per equal-area radial stratum, separated by the golden angle.
    // Slight jitter breaks the spiral's regularity without random clustering.
    // Field.w includes the cloud's visual fringe. Its whole footprint ignites
    // together; formation controls opacity rather than moving sparks outward.
    float rotation = emberHash(Field.x * 0.21 + Field.y * 0.37 + 3.0) * 6.2831853;
    float angle = float(particle) * 2.39996323 + rotation
        + (emberHash(seed + 11.0) - 0.5) * 0.12;
    float stratum = (float(particle) + 0.35 + emberHash(seed + 1.0) * 0.30) / max(Scene.y, 1.0);
    float radius = sqrt(stratum) * Field.w;
    float2 origin = Field.xy + float2(cos(angle), sin(angle)) * radius;
    float2 drift = float2(sin(age * 5.0 + seed), cos(age * 4.0 + seed * 0.7));
    drift *= (2.0 + random * 5.0) * age;
    drift += float2(3.0, -1.0) * age * age;
    float height = Shape.x * (0.36 + age * (1.8 + random * 0.6));
    float3 world = float3(origin + drift, emberGround(origin + drift) + height);
    float4 clip = mul(float4(world, 1), ViewProjection);
    float4 upClip = mul(float4(world + float3(0, 0, 1), 1), ViewProjection);
    float2 up = (upClip.xy / max(upClip.w, 0.0001) - clip.xy / max(clip.w, 0.0001))
        * Viewport.zw * float2(0.5, -0.5);
    up = dot(up, up) > 0.00001 ? normalize(up) : float2(0, -1);
    float2 across = float2(-up.y, up.x);
    // The bright core is roughly 1-2 pixels across at the default view, with
    // a softer orange halo and a short tail pointing down its rising path.
    float zoom = clamp(sqrt(max(Viewport.z * Viewport.w, 1.0) / 1024000.0), 0.8, 1.7);
    float2 extent = float2(2.3 + random * 1.1, 3.0 + random * 2.5) * zoom;
    float2 pixelOffset = across * local.x * extent.x + up * local.y * extent.y;
    clip.xy += pixelOffset * float2(2, -2) / max(Viewport.zw, float2(1, 1)) * clip.w;
    float fade = smoothstep(0.0, 0.14, age) * (1.0 - smoothstep(0.58, 1.0, age));
    fade *= saturate(Shape.y * 1.6) * smoothstep(0.02, 0.28, Shape.z);
    fade *= lerp(0.58, 1.0, random) * lerp(0.55, 1.0, smoothstep(0.5, 1.1, height / Shape.x));
    // Keep the outer annulus legible. Only the last few percent of outward
    // drift die away, so the soft cloud fringe does not become a hard spark ring.
    float edgeRadius = length(world.xy - Field.xy) / max(Field.w, 0.001);
    fade *= (1.0 - 0.40 * smoothstep(0.88, 1.0, edgeRadius))
        * (1.0 - smoothstep(1.0, 1.055, edgeRadius));
    EmberVertex output;
    output.position = clip;
    output.local = local;
    output.glow = float2(fade, random);
    return output;
}
)HLSL";

const char *emberPixelSource = R"HLSL(
Texture2D<float> SceneDepth : register(t0, space2);
SamplerState DepthSampler : register(s0, space2);
cbuffer EmberPixelConstants : register(b0, space3) { float4 Scene; };
struct EmberVertex {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float2 glow : TEXCOORD1;
};
float4 main(EmberVertex input) : SV_Target
{
    float depth = SceneDepth.SampleLevel(DepthSampler, input.position.xy * Scene.xy, 0);
    // Use the same captured opaque depth as the volume: terrain and infantry
    // hide sparks behind them, while this pass never writes the scene depth.
    clip(depth - input.position.z + 0.0000005);
    float2 corePoint = (input.local - float2(0, 0.30)) * float2(1, 1.6);
    float squared = dot(corePoint, corePoint);
    float core = exp2(-squared * 12.0);
    float halo = exp2(-squared * 3.0);
    float tail = (1.0 - smoothstep(0.10, 0.34, abs(input.local.x)))
        * smoothstep(-1.0, -0.30, input.local.y)
        * (1.0 - smoothstep(-0.18, 0.30, input.local.y));
    float edge = (1.0 - smoothstep(0.80, 1.0, abs(input.local.x)))
        * (1.0 - smoothstep(0.85, 1.0, abs(input.local.y)));
    float3 warm = lerp(float3(1.0, 0.21, 0.025), float3(1.0, 0.43, 0.08), input.glow.y);
    float3 light = float3(1.0, 0.80, 0.43) * core * 0.95 + warm * (halo * 0.30 + tail * 0.17);
    return float4(light * input.glow.x * edge, 0);
}
)HLSL";

} // namespace

std::string vertexShader()
{
    return R"HLSL(
float4 main(uint vertex : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
)HLSL";
}

std::string pixelShader(Pass pass, bool registerSpaces)
{
    const int number = pass == Pass::Composite ? 1 : pass == Pass::Density ? 2
        : pass == Pass::Lighting ? 3 : pass == Pass::Heat ? 4 : 0;
    return withSpaces(cloudShader(number), registerSpaces);
}

std::string emberVertexShader(bool registerSpaces) { return withSpaces(emberVertexSource, registerSpaces); }
std::string emberPixelShader(bool registerSpaces) { return withSpaces(emberPixelSource, registerSpaces); }

} // namespace HazardCloud
