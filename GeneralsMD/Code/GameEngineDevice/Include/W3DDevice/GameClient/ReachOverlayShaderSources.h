// Exact DX11 distance-field reach shader and constant ABI, mechanically shared.
// Distributed under GPL-3.0; see LICENSE.md.
#pragma once
#include <cstddef>
namespace ReachOverlayShaderSources {
enum {FieldsPerPass=32};
// Laid out as the program's constant buffer.
struct Constants
{
	float WorldFromClip[ 16 ];
	float Viewport[ 4 ];			// pixel origin (half a pixel in, as the backend's viewport sits), size
	float Colour[ 4 ];				// the owner's colour; w unused
	float Look0[ 4 ];					// border width, band width, fill alpha, band alpha
	float Look1[ 4 ];					// line alpha, line toward white, field count, seconds
	float Look2[ 4 ];					// soft width, soft line, ripple alpha, ripple period
	float Look3[ 4 ];					// ripple span, ripple width, shade width, shade alpha
	float Grid[ FieldsPerPass ][ 4 ];		// origin, 1 / step, -distanceLimit
	float Size[ FieldsPerPass ][ 4 ];		// width - 1, height - 1, opacity
};

const char *const PixelSource = R"HLSL(
cbuffer Reach : register(b0)
{
    row_major float4x4 WorldFromClip;
    float4 Viewport;
    float4 Colour;
    float4 Look0;
    float4 Look1;
    float4 Look2;
    float4 Look3;
    float4 Grid[32];
    float4 Size[32];
};
Texture2D<float> SceneDepth : register(t0);
Texture2D<float2> Fields[32] : register(t1);	// distance, hardness

float4 main(float4 position : SV_Position) : SV_Target
{
    float depth = SceneDepth.Load(int3(position.xy, 0));
    float2 uv = (position.xy - Viewport.xy) / max(Viewport.zw, float2(1, 1));
    float4 world = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1), WorldFromClip);
    float w = abs(world.w) < 0.000001 ? 0.000001 : world.w;
    float2 at = world.xy / w;

    // the union of the fields: the largest distance with its hardness, and the strongest opacity
    // among the fields the pixel is inside
    float d = -1.0e6;
    float hard = 1;
    float inside[32];
    uint count = (uint)Look1.z;
    [unroll] for (uint i = 0; i < 32; ++i)
    {
        inside[i] = Grid[i].w;
        if (i < count)
        {
            float2 g = (at - Grid[i].xy) * Grid[i].z;
            float2 here = float2(Grid[i].w, 1);
            if (all(g >= 0) && all(g <= Size[i].xy))
            {
                float2 base = min(floor(g), Size[i].xy - 1);
                float2 f = g - base;
                int3 p = int3(base, 0);
                float2 a = Fields[i].Load(p);
                float2 b = Fields[i].Load(p + int3(1, 0, 0));
                float2 c = Fields[i].Load(p + int3(0, 1, 0));
                float2 e = Fields[i].Load(p + int3(1, 1, 0));
                here = lerp(lerp(a, b, f.x), lerp(c, e, f.x), f.y);
                inside[i] = here.x;
            }
            if (here.x > d)
            {
                d = here.x;
                hard = here.y;
            }
        }
    }

    // world units of distance a pixel spans; capped, so a cliff's or a bridge's edge in the depth
    // cannot widen the line beyond a pixel
    float pixel = clamp(fwidth(d), 0.001, 16.0);
    float opacity = 0;
    [unroll] for (uint k = 0; k < 32; ++k)
        if (k < count) opacity = max(opacity, Size[k].z * saturate(inside[k] / pixel));

    // Nothing at or under nought: every ramp below runs inward from the edge, never out of it.  A
    // range edge is a line antialiased over the pixel; a blocked one fades in over the soft width.
    hard = saturate(hard);
    float edge = saturate(d / pixel);
    float cover = lerp(smoothstep(0, Look2.x, d), edge, hard);
    float border = max(Look0.x, pixel);
    float stroke = edge * saturate((border - d) / pixel + 0.5) * Look1.x * lerp(Look2.y, 1, hard);
    float shadeEnd = border + max(Look3.z, pixel);
    float shade = saturate((d - border) / pixel + 0.5) * saturate((shadeEnd - d) / pixel + 0.5) * Look3.w * hard;

    // a faint band leaves the range line every period and runs inward, thinning as it goes
    float phase = frac(Look1.w / Look2.w);
    float r = (d - border - phase * Look3.x) / Look3.y;
    float ripple = exp(-r * r) * (1 - phase) * Look2.z * hard;

    float band = saturate(1 - d / Look0.y);
    float fill = saturate(Look0.z + Look0.w * band * band + ripple) * cover;
    float4 result = float4(Colour.rgb * fill, fill);
    result = result * (1 - shade) + float4(0, 0, 0, shade);
    result = result * (1 - stroke) + float4(lerp(Colour.rgb, float3(1, 1, 1), Look1.y) * stroke, stroke);
    if (d <= 0 || depth >= 1) discard;
    return result * opacity;
}
)HLSL";


static_assert(sizeof(Constants)==1184 && offsetof(Constants,Grid)==160 && offsetof(Constants,Size)==672,"reach uniform ABI");
}
