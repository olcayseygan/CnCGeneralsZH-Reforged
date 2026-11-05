// Shared-grid density, formation and dissipation reuse the approved cloud shader.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "SdlHazardCloudSharedShader.h"
#include "Platform/HazardCloudCore.h"
#include <stdexcept>

namespace HazardCloudShared {
namespace {
std::size_t marker(const std::string &text, const char *token, std::size_t from = 0)
{
	const auto at = text.find(token, from);
	if (at == std::string::npos)
		throw std::runtime_error(std::string("production cloud shader extraction marker changed: ") + token);
	return at;
}

void replaceOnce(std::string &text, const char *before, const char *after)
{
	const auto at = marker(text, before);
	if (text.find(before, at + std::char_traits<char>::length(before)) != std::string::npos)
		throw std::runtime_error(std::string("production cloud shader extraction marker is ambiguous: ") + before);
	text.replace(at, std::char_traits<char>::length(before), after);
}

std::string section(const std::string &text, const char *first, const char *last)
{
	const auto begin = marker(text, first);
	const auto end = marker(text, last, begin);
	return text.substr(begin, end - begin);
}
}

std::string sharedPixelShader(ShaderPass pass)
{
	const std::string original = HazardCloud::pixelShader(HazardCloud::Pass::Composite, true);
	std::string prefix = original.substr(0, marker(original, "// Each slice has its own"));
	const auto constantsEnd = marker(prefix, "};");
	prefix.insert(constantsEnd, R"HLSL(
	float4 SharedMin;
	float4 SharedMax;
	float4 SharedGrid;
	float4 SharedBins;
	float4 SharedData;
)HLSL");
	// A single shared support map, or the independent elevated source plane.
	const auto groundStart = marker(prefix, "float volumeGround(");
	const auto groundEnd = marker(prefix, "bool volumeInterval(", groundStart);
	prefix.replace(groundStart, groundEnd - groundStart, R"HLSL(
float volumeGround(float2 world, float4 field, float4 shape, float fringe, float identity)
{
	if (SharedData.z < 2.0 || identity < 0.5) return field.z - shape.x;
	float2 uv = saturate((world - SharedMin.xy) / (SharedMax.xy - SharedMin.xy));
	float2 p = uv * (SharedData.z - 1.0), low = floor(p), f = frac(p);
	int2 a = (int2)low, b = min(a + 1, (int)SharedData.z - 1);
	float ground = lerp(lerp(TerrainImage.Load(int3(a, 0)), TerrainImage.Load(int3(b.x, a.y, 0)), f.x),
		lerp(TerrainImage.Load(int3(a.x, b.y, 0)), TerrainImage.Load(int3(b, 0)), f.x), f.y);
	return ground;
}
)HLSL");
	// Terrain-supported sources share ground and wakes at a voxel, even when
	// their centre, age, palette and footprint differ. Keep all procedural coordinates and
	// formation masks from production, supplying only those common inputs.
	replaceOnce(prefix, "    float4 terrain = identity < 0.5 ? Terrain0 : Terrain1;",
		"    float4 terrain = float4(SharedMin.z, SharedMax.z, SharedData.z >= 2.0 && identity >= 0.5 ? 1.0 : 0.0, 0.0);");
	replaceOnce(prefix,
		"float volumeDensity(float3 world, float4 field, float4 shape, float4 source, float fringe, float identity)",
		"float sharedDensity(float3 world, float4 field, float4 shape, float4 source, float fringe, float identity, float ground, float2 displacement)");
	replaceOnce(prefix,
		"    float ground = volumeGround(world.xy, field, float4(halfHeight, shape.yzw), fringe, identity);\n", "");
	replaceOnce(prefix,
		"    float2 displacement = volumeInteraction(world.xy).xy;\n"
		"    displacement *= min(1.0, max(FlowSettings.y, 0.0) / max(length(displacement), 0.0001));\n", "");
	// Broad noise only changes the veil outside the footprint. Folds and fine
	// noise still erode the bulk, so retain their original evaluations.
	replaceOnce(prefix, "    float broad = volumeNoise(at);",
		"    float broad = 0.0;\n"
		"    [branch] if (hasVeil && outside > 0.0) broad = volumeNoise(at);");
	replaceOnce(prefix,
		"    coverage *= lerp(1.0, 0.20 + 0.80 * smoothstep(0.20, 0.80, detail), outside);",
		"    if (hasVeil && outside > 0.0)\n"
		"        coverage *= lerp(1.0, 0.20 + 0.80 * smoothstep(0.20, 0.80, detail), outside);");
	replaceOnce(prefix,
		"            float body = saturate(1.0 - dot(q, q) - z * z + erosion);\n",
		"            float body = saturate(1.0 - dot(q, q) - z * z + erosion);\n"
		"            if (body <= 0.0) continue;\n");
	std::string source = prefix + R"HLSL(
Texture2D<float4> SourceData : register(t7, space2);
SamplerState SourceSampler : register(s7, space2);
Texture2D<float4> BinData : register(t8, space2);
SamplerState BinSampler : register(s8, space2);
Texture2D<float4> IndexData : register(t9, space2);
SamplerState IndexSampler : register(s9, space2);
Texture2D<float4> SharedBackground : register(t5, space2);
SamplerState SharedBackgroundSampler : register(s5, space2);

float4 sourceAt(uint at) { return SourceData.Load(int3(at % 1024, at / 1024, 0)); }
float4 binAt(uint at) { return BinData.Load(int3(at % 1024, at / 1024, 0)); }
uint indexAt(uint at)
{
	uint texel = at / 4;
	float4 ids = IndexData.Load(int3(texel % 1024, texel / 1024, 0));
	return (uint)ids[at % 4];
}
float3 atlasWorld(float2 pixel)
{
	uint2 p = (uint2)pixel;
	uint side = (uint)SharedGrid.x, columns = (uint)SharedGrid.z;
	uint z = p.x / side + p.y / side * columns;
	float3 voxel = float3(p.x % side, p.y % side, z);
	return SharedMin.xyz + voxel / float3(side - 1, side - 1, SharedGrid.y - 1) * (SharedMax.xyz - SharedMin.xyz);
}
float4 sharedRead(float3 world, bool lit)
{
	float3 uv = (world - SharedMin.xyz) / (SharedMax.xyz - SharedMin.xyz);
	if (any(uv < 0) || any(uv > 1)) return 0;
	float3 voxel = saturate(uv) * float3(SharedGrid.xx - 1, SharedGrid.y - 1);
	uint z0 = (uint)floor(voxel.z), z1 = min(z0 + 1, (uint)SharedGrid.y - 1);
	uint columns = (uint)SharedGrid.z;
	float2 size = SharedGrid.xx * SharedGrid.zw;
	float2 a = (float2(z0 % columns, z0 / columns) * SharedGrid.x + voxel.xy + 0.5) / size;
	float2 b = (float2(z1 % columns, z1 / columns) * SharedGrid.x + voxel.xy + 0.5) / size;
	if (lit) return lerp(CacheImage1.SampleLevel(CacheSampler1, a, 0), CacheImage1.SampleLevel(CacheSampler1, b, 0), frac(voxel.z));
	return lerp(CacheImage0.SampleLevel(CacheSampler0, a, 0), CacheImage0.SampleLevel(CacheSampler0, b, 0), frac(voxel.z));
}
float4 inject(float3 world)
{
	int2 bin = (int2)floor((world.xy - SharedBins.xy) / SharedBins.z);
	if (any(bin < 0) || bin.x >= (int)SharedBins.w || bin.y >= (int)SharedData.x) return 0;
	float4 range = binAt((uint)bin.x + (uint)bin.y * (uint)SharedBins.w);
	if (range.y < 1.0) return 0;
	float sharedGround = 0.0;
	if (SharedData.z >= 2.0) sharedGround = volumeGround(world.xy, 0, 0, 0, 1);
	float2 displacement = volumeInteraction(world.xy).xy;
	displacement *= min(1.0, max(FlowSettings.y, 0.0) / max(length(displacement), 0.0001));
	float4 result = 0;
	// CSR lists are variable length: no fixed per-bin source limit or fallback.
	[loop] for (uint item = 0; item < (uint)range.y; ++item) {
		uint id = indexAt((uint)range.x + item);
		float4 field = sourceAt(id * 4), packed = sourceAt(id * 4 + 1);
		float4 history = sourceAt(id * 4 + 2), extra = sourceAt(id * 4 + 3);
		float4 shape = float4(packed.x * 0.5, packed.yzw);
		field.z += shape.x;
		float ground = SharedData.z >= 2.0 && extra.z > 0.5 ? sharedGround : field.z - max(shape.x, 0.25);
		float density = sharedDensity(world, field, shape, history, extra.x, extra.y, ground, displacement);
		if (density <= 0.00001) continue;
		uint palette = (uint)clamp(round(shape.z), 0.0, 3.0);
		float extinction = 0.075 * min(1.8, pow(9.3 / max(shape.x, 0.25), 0.35));
		extinction *= PaletteShadow[palette].w;
		float coefficient = density * extinction;
		float3 colour = PaletteShadow[palette].rgb + PaletteMain[palette].rgb * 0.84;
		if (palette == 1) {
			// Production's soot uses the unclamped source support plane.
			float sootGround = SharedData.z >= 2.0 && extra.z > 0.5 ? sharedGround : field.z - shape.x;
			float h = saturate((world.z - sootGround) / max(packed.x, 0.5));
			float age = history.w;
			float3 at = float3(world.xy * 0.041, h * 1.85) + float3(-0.024 * age, 0.016 * age, -0.17 * age) + float3(12.7,31.4,8.6);
			float broad = volumeNoise(at), folds = volumeNoise(at * 2.07 + float3(6.1,17.3,4.8));
			float soot = smoothstep(0.48,0.69,broad * 0.72 + folds * 0.28);
			soot *= smoothstep(0.06,0.25,h) * smoothstep(0.25,1.10,density / max(shape.y,0.00001));
			soot *= smoothstep(0.0,1.20,age) * smoothstep(0.05,0.55,shape.w) * (1.0 - 0.60 * saturate(history.z));
			float3 charcoal = saturate(float3(0.505,0.482,0.437) + PaletteShadow[1].rgb - float3(0.115,0.085,0.030));
			colour = lerp(colour, charcoal, soot * 0.80);
			coefficient *= 1.0 + 0.08 * soot;
		}
		result.rgb += colour * coefficient * PaletteMain[palette].w;
		result.a += coefficient;
	}
	return result;
}
float4 lightVolume(float3 world)
{
	float4 material = sharedRead(world, false);
	if (material.a < 0.00001) return 0;
	const float3 sun = float3(-0.462,-0.347,0.816);
	float optical = sharedRead(world + sun * 2.5, false).a * 5.0;
	optical += sharedRead(world + sun * 8.0, false).a * 10.0;
	optical += sharedRead(world + sun * 16.0, false).a * 14.0;
	material.rgb *= 0.12 + 0.88 * exp(-optical * 1.20);
	return material;
}
float4 volumeRaymarch(float2 pixel)
{
	float3 origin = volumeWorldAt(pixel,0), direction = normalize(volumeWorldAt(pixel,1) - origin);
	float3 safe = float3(abs(direction.x) < 1e-6 ? 1e-6 : direction.x,
		abs(direction.y) < 1e-6 ? 1e-6 : direction.y, abs(direction.z) < 1e-6 ? 1e-6 : direction.z);
	float3 a = (SharedMin.xyz - origin) / safe, b = (SharedMax.xyz - origin) / safe;
	float3 nearAxis = min(a,b), farAxis = max(a,b);
	float first = max(0.0,max(nearAxis.x,max(nearAxis.y,nearAxis.z)));
	float last = min(farAxis.x,min(farAxis.y,farAxis.z));
	float depth = SceneDepth.SampleLevel(DepthSampler,pixel * Scene.xy,0).r;
	if (depth < 0.999999) last = min(last,max(0.0,dot(volumeWorldAt(pixel,depth) - origin,direction)));
	if (last <= first) return 0;
	// Fixed integration cost, independent of source count. Sampling follows the
	// volume's occupied slab, rather than the whole scene's depth range.
	const int steps = 96;
	float stride = (last - first) / steps;
	float jitter = 0.35 + volumeHash(float3(floor(pixel),17)) * 0.30;
	float transmittance = 1; float3 radiance = 0;
	[loop] for (int sampleIndex = 0; sampleIndex < steps; ++sampleIndex) {
		if (transmittance < 0.015) break;
		float3 world = origin + direction * (first + (sampleIndex + jitter) * stride);
		float4 coefficient = sharedRead(world,true);
		if (coefficient.a < 0.000001) continue;
		float opacity = 1.0 - exp(-coefficient.a * stride);
		radiance += transmittance * opacity * coefficient.rgb / coefficient.a;
		transmittance *= 1.0 - opacity;
	}
	return float4(radiance,1.0 - transmittance);
}
)HLSL";
	source += section(original, "float2 volumeFullPixel(", "#if CLOUD_COMPOSITE || CLOUD_HEAT_PASS");
	source += section(original, "float volumeDepthSlope(", "#endif");
	const std::string hot = HazardCloud::pixelShader(HazardCloud::Pass::Heat, true);
	source += section(hot, "bool volumeHeatContains(", "bool volumeHeatDepthSafe(");
	// Keep production's edge-aware reconstruction and exact full-resolution
	// silhouette fallback, now sampling the shared spatial volume.
	const auto composite = original.rfind("#else\n    if (Settings.z");
	if (composite == std::string::npos)
		throw std::runtime_error("production cloud shader composite extraction marker changed");
	const auto compositeEnd = marker(original, "\n#endif\n}", composite);
	const std::string body = original.substr(composite + 6, compositeEnd - composite - 6);
	source += "\nfloat4 main(float4 position : SV_Position) : SV_Target\n{\n";
	if (pass == ShaderPass::Inject) source += "return inject(atlasWorld(position.xy));\n";
	else if (pass == ShaderPass::Light) source += "return lightVolume(atlasWorld(position.xy));\n";
	else if (pass == ShaderPass::Volume) source += "return volumeRaymarch(volumeFullPixel(position.xy));\n";
	else if (pass == ShaderPass::Composite) source += body;
	else source += R"HLSL(
	float depth = SceneDepth.SampleLevel(DepthSampler,position.xy * Scene.xy,0).r;
	float3 world = volumeWorldAt(position.xy,depth);
	float3 origin = volumeWorldAt(position.xy,0);
	float3 direction = normalize(volumeWorldAt(position.xy,1) - origin);
	float limit = depth < 0.999999 ? max(0.0,dot(world - origin,direction)) : 100000.0;
	float2 offset = 0;
	[loop] for (uint id = 0; id < (uint)SharedData.y; ++id) {
		float4 shapeData = sourceAt(id * 4 + 1);
		if (shapeData.z < 0.5 || shapeData.z >= 1.5 || shapeData.y < 0.00001) continue;
		float4 field = sourceAt(id * 4), history = sourceAt(id * 4 + 2), extra = sourceAt(id * 4 + 3);
		float4 shape = float4(shapeData.x * 0.5,shapeData.yzw);
		field.z += shape.x;
		bool immersed = volumeHeatContains(world,field,shape,extra.x,extra.z);
		offset += volumeHeatOffset(origin,direction,limit,immersed,field,shape,history,extra.x,extra.z);
	}
	offset *= 10.0 * clamp(Viewport.w / 800.0,0.65,1.5);
	float magnitude = length(offset);
	offset *= min(1.0,8.0 / max(magnitude,0.0001));
	if (magnitude < 0.01) return 0;
	float pixelSpan = length(volumeWorldAt(position.xy + float2(1,0),depth) - world);
	float tolerance = max(0.65,pixelSpan * 0.75);
	float2 slope = float2(volumeDepthSlope(position.xy,depth,float2(1,0),pixelSpan * 4.0),
		volumeDepthSlope(position.xy,depth,float2(0,1),pixelSpan * 4.0));
	[loop] for (int attempt = 0; attempt < 3; ++attempt) {
		float2 pixel = clamp(position.xy + offset,Viewport.xy,Viewport.xy + Viewport.zw - 1);
		float movedDepth = SceneDepth.SampleLevel(DepthSampler,pixel * Scene.xy,0).r;
		float expected = depth + dot(slope,pixel - position.xy);
		if (length(volumeWorldAt(pixel,movedDepth) - volumeWorldAt(pixel,expected)) <= tolerance)
			return float4(SharedBackground.SampleLevel(SharedBackgroundSampler,pixel * Scene.xy,0).rgb,1);
		offset *= 0.5;
	}
	return 0;
)HLSL";
	source += "}\n";
	return source;
}
}
