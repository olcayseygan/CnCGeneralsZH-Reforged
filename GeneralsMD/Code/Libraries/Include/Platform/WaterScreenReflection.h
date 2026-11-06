// Screen-space and planar reflections for SDL Modern water. Render-only.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

namespace WaterScreenReflection {

bool Supported(); // Queries the current backend; false before a GPU device exists.
enum Mode { Mirror, ScreenSpace, Hybrid, SkyOnly };
constexpr unsigned LegacyMirrorSize = 512;
constexpr unsigned HybridMirrorSize = 1024;
inline unsigned MirrorSize(Mode mode, int detail = 1) {
	const unsigned high = mode == Hybrid ? HybridMirrorSize : LegacyMirrorSize;
	return detail == 0 ? high / 2 : high;
}
inline Mode Resolve(int setting, bool enabled = true)
{
	if (!enabled) return SkyOnly;
	switch (setting) {
		case 0: return Mirror;
		case 1: return ScreenSpace;
		default: return Hybrid;
	}
}
inline bool Traces(Mode mode) { return mode == ScreenSpace || mode == Hybrid; }
inline bool UsesMirror(Mode mode) { return mode == Mirror || mode == Hybrid; }

// Needs the water shader's WorldFromClip, ClipFromWorld, Viewport, CameraPosition, SceneParameters and MaterialParameters, the
// scene colour (Texture5) and depth (Texture6) snapshots, and waterScenePixel/scenePosition.
inline const char *ShaderSource() { return R"HLSL(
float2 waterSsrPixel(float4 clip)
{
    float2 ndc=clip.xy/clip.w;
    return Viewport.xy+(ndc*float2(0.5,-0.5)+0.5)*Viewport.zw;
}
float waterSsrDepth(float2 pixel)
{
    // A depth discontinuity must not become a phantom intermediate surface.
    return Texture6.Load(int3(int2(floor(waterScenePixel(pixel))),0)).r;
}
struct WaterSsrSample
{
    float3 colour;     // display-space scene colour at the hit
    float confidence;  // 0 for a miss
    float travel;      // world ray distance / WaterSsrDistance
    float refused;     // 1 when no trace was attempted
    float distance;    // world ray distance; 0 for a miss
    float3 position;   // accepted scene surface, for reflection correspondence
};
static const int WaterSsrSteps=32;
static const int WaterSsrRefinements=7; // Stops earlier once the bracket is subpixel.
static const float WaterSsrDistance=700.0;  // world units; a map cell is 10
// How far behind a depth sample a ray may pass and still hit it: a few world units, plus two
// pixels of the ray's own depth travel. A fixed allowance as wide as a building also accepts
// every ray passing behind a thin lintel or pillar, which fills the open water between them.
static const float WaterSsrThickness=3.0;
// The share of the wave normal the trace follows. The mirror only nudges its lookup by the
// normal; a fully rippled ray scatters a reflection into speckle over a long trace.
static const float WaterSsrNormalWeight=0.3;
// A downward reflected ray meets the water itself, and the snapshot holds no water. Rays leaving
// the screen, or passing behind everything, fall back to the caller's reflection.
WaterSsrSample waterScreenReflection(float3 world, float3 view, float3 normal, float roughness, float2 pixel)
{
    float3 ray=reflect(-view, normalize(lerp(float3(0,0,1), normal, WaterSsrNormalWeight)));
    WaterSsrSample result;
    result.colour=float3(0,0,0); result.confidence=0.0; result.travel=1.0; result.refused=1.0;
    result.distance=0.0; result.position=world;
    [branch] if (!(MaterialParameters.w>0.5 && MaterialParameters.w<1.5 && SceneParameters.x>0.5 && SceneParameters.w>0.5) || ray.z<0.02)
        return result;
    // Uploaded once per draw, including the same viewport depth mapping as WorldFromClip.
    float4 c0=mul(float4(world,1),ClipFromWorld);
    float4 c1=mul(float4(world+ray*WaterSsrDistance,1),ClipFromWorld);
    [branch] if (!(c0.w>0.0)) return result;
    // Keep the segment in front of the camera.
    float nearW=c0.w*0.05;
    if (c1.w<nearW) c1=lerp(c0,c1,(c0.w-nearW)/max(c0.w-c1.w,1e-5));
    float2 p0=waterSsrPixel(c0), p1=waterSsrPixel(c1);
    float z0=c0.z/c0.w, z1=c1.z/c1.w;
    // Device depth is affine in screen space along a projected line, so it interpolates with the pixel.
    float2 lo=waterScenePixel(float2(-1e9,-1e9)), hi=waterScenePixel(float2(1e9,1e9));
    float2 d=p1-p0;
    float2 exitT=float2(d.x>0.0?(hi.x-p0.x)/d.x:(d.x<0.0?(lo.x-p0.x)/d.x:1.0),
                        d.y>0.0?(hi.y-p0.y)/d.y:(d.y<0.0?(lo.y-p0.y)/d.y:1.0));
    float span=saturate(min(exitT.x,exitT.y));
    p1=lerp(p0,p1,span); z1=lerp(z0,z1,span);
    float rayPixels=length(p1-p0);
    [branch] if (rayPixels<2.0) return result;
    result.refused=0.0;
    // The water origin passed the opaque depth test, so the first segment starts in front.
    bool previousFront=true;
    float previous=0.0, found=-1.0, hitQuality=0.0;
    // Keep a fixed screen-space phase: deterministic zero-phase stepping produced coherent
    // missing bands on thin geometry without a history resolve. This retains the existing
    // spatial stipple limitation, but never changes the phase with frame number.
    float jitter=frac(52.9829189*frac(dot(pixel,float2(0.06711056,0.00583715))));
    [loop] for (int i=1; i<=WaterSsrSteps && found<0.0; ++i) {
        // Denser near contact. Include the segment endpoint as the final sample.
        float t=i==WaterSsrSteps?1.0:pow((float(i)-1.0+jitter)/float(WaterSsrSteps),1.6);
        float2 at=waterScenePixel(floor(lerp(p0,p1,t))+0.5);
        float rayDepth=lerp(z0,z1,t);
        if (rayDepth<0.0 || rayDepth>1.0) break;
        float sceneDepth=waterSsrDepth(at);
        bool behindNow=sceneDepth<0.999999 && rayDepth>sceneDepth;
        [branch] if (behindNow) {
            float a=previous, b=t;
            // Only refine a real front-to-behind bracket. After a rejected occluder, later
            // behind samples still need a slab test: they can meet a different valid surface.
            if (previousFront) {
                [loop] for (int r=0; r<WaterSsrRefinements && (b-a)*rayPixels>0.5; ++r) {
                    float m=(a+b)*0.5;
                    float md=waterSsrDepth(lerp(p0,p1,m));
                    if (md<0.999999 && lerp(z0,z1,m)>md) b=m; else a=m;
                }
            }
            float2 hp=waterScenePixel(floor(lerp(p0,p1,b))+0.5);
            float hd=waterSsrDepth(hp);
            float3 sceneHit=scenePosition(hp,hd);
            float3 rayHit=scenePosition(hp,lerp(z0,z1,b));
            float behind=length(rayHit-CameraPosition.xyz)-length(sceneHit-CameraPosition.xyz);
            float before=max(b-1.0/rayPixels,0.0);
            // Use the same pixel for both reconstructions: only depth travel widens the slab.
            float3 rayBefore=scenePosition(hp,lerp(z0,z1,before));
            float pixelTravel=abs(length(rayHit-CameraPosition.xyz)-length(rayBefore-CameraPosition.xyz));
            float tolerance=min(WaterSsrThickness+2.0*pixelTravel,6.0);
            // Under-water scene (the bed the refraction sees) cannot be met by an upward ray.
            if (hd<0.999999 && behind>=0.0 && behind<=tolerance && sceneHit.z>world.z-1.0) {
                found=b;
                result.position=sceneHit;
                hitQuality=1.0-smoothstep(0.8*tolerance,tolerance,behind);
            }
        }
        previous=t;
        previousFront=!behindNow;
    }
    [branch] if (found<0.0) return result;
    float2 hit=lerp(p0,p1,found);
    float2 radius=float2(1.0,1.0)+saturate(roughness)*6.0;
    float3 colour=Texture5.SampleLevel(Sampler5,waterScenePixel(hit)*SceneParameters.yz,0).rgb*0.4;
    colour+=Texture5.SampleLevel(Sampler5,waterScenePixel(hit+float2(radius.x,0))*SceneParameters.yz,0).rgb*0.15;
    colour+=Texture5.SampleLevel(Sampler5,waterScenePixel(hit-float2(radius.x,0))*SceneParameters.yz,0).rgb*0.15;
    colour+=Texture5.SampleLevel(Sampler5,waterScenePixel(hit+float2(0,radius.y))*SceneParameters.yz,0).rgb*0.15;
    colour+=Texture5.SampleLevel(Sampler5,waterScenePixel(hit-float2(0,radius.y))*SceneParameters.yz,0).rgb*0.15;
    float2 border=min(hit-lo,hi-hit);
    float filterSupport=max(radius.x,radius.y)+1.0;
    result.colour=colour;
    // Projected t is not a world-distance fraction, especially after clipping the ray.
    float3 hitWorld=scenePosition(hit,lerp(z0,z1,found));
    result.distance=length(hitWorld-world);
    result.travel=saturate(result.distance/WaterSsrDistance);
    result.confidence=hitQuality*smoothstep(filterSupport,filterSupport+12.0,min(border.x,border.y))
        *(1.0-smoothstep(0.75,1.0,result.travel));
    return result;
}
)HLSL"; }

}
