#pragma once
// Extracted from WW3D2/dx11backend.cpp (upstream CnCGeneralsZH-Reforged, GPL-3.0-or-later).
// Shared with the Windows smoke map: additive optical-depth moments and nearest front.
#include <cmath>
#include <vector>
namespace SunSmoke {
static const unsigned Texels = 512, MaxCasters = 16384, SplatFloats = 6;
static const float SelfGain = 0.55f, SelfCurve = 4.0f;	// SMOKE_SELF_SHADOW_* in dx11backend.cpp, which says why
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


// Input: world xyz, radius, optical depth. Output: clip xyz/radius, optical depth, spread.
inline unsigned Project(const float *sun, const float *casters, unsigned count, std::vector<float> &out)
{
    out.clear();
    if (!casters || !sun) return 0;
    if (count > MaxCasters) count = MaxCasters;
    const float across = std::sqrt(sun[0] * sun[0] + sun[4] * sun[4] + sun[8] * sun[8]);
    const float deep = std::sqrt(sun[2] * sun[2] + sun[6] * sun[6] + sun[10] * sun[10]);
    out.reserve(count * SplatFloats);
    for (unsigned i = 0; i < count; ++i) {
        const float *caster = casters + i * 5;
        bool finite = true;
        for (unsigned k = 0; k < 5; ++k)
            finite = finite && std::isfinite(caster[k]);
        const float w = caster[0] * sun[3] + caster[1] * sun[7] + caster[2] * sun[11] + sun[15];
        if (!finite || !std::isfinite(w) || w <= 0 || caster[3] <= 0 || caster[4] <= 0) continue;
        const float x = (caster[0] * sun[0] + caster[1] * sun[4] + caster[2] * sun[8] + sun[12]) / w;
        const float y = (caster[0] * sun[1] + caster[1] * sun[5] + caster[2] * sun[9] + sun[13]) / w;
        const float z = (caster[0] * sun[2] + caster[1] * sun[6] + caster[2] * sun[10] + sun[14]) / w;
        const float radius = caster[3] * across;
        const float spread = 0.5f * caster[3] * deep;
        if (!std::isfinite(x + y + z + radius + spread) || x < -1 - radius || x > 1 + radius
            || y < -1 - radius || y > 1 + radius || z < -spread || z > 1 + spread) continue;
        const float splat[] = {x, y, z, radius, caster[4], spread};
        out.insert(out.end(), splat, splat + SplatFloats);
    }
    return unsigned(out.size() / SplatFloats);
}
}
