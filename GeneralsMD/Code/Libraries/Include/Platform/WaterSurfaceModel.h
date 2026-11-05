/* Render-only water displacement. No gameplay height, collision or navigation state. */
#pragma once
#include <cmath>
#include <string>
#include <cstdint>

// kx, ky (radians/world unit), amplitude (world units), angular frequency, initial phase.
// Differing dispersion and directions ensure this cannot be represented by scrolling a static map.
#define WATER_SURFACE_MODES(X) \
 X(0.1093f, 0.0395f, 1.30f, 0.94f, 0.17f) \
 X(0.1571f,-0.1264f, 0.85f, 1.24f, 2.71f) \
 X(0.2772f, 0.1810f, 0.50f, 1.59f, 4.13f) \
 X(-0.1524f,0.4835f, 0.24f, 1.97f, 1.39f) \
 X(0.6856f, 0.2624f, 0.095f,2.37f, 5.21f) \
 X(0.7915f,-0.8025f, 0.061f,2.94f, 3.47f) \
 X(-0.5575f,1.5408f,0.038f, 3.54f, 0.83f) \
 X(1.9218f, 0.6150f, 0.026f,3.93f, 4.79f) \
 X(2.4708f,-1.3037f, 0.016f,4.62f, 2.02f) \
 X(-1.7671f,3.0640f,0.011f, 5.20f, 5.91f) \
 X(4.0647f, 1.6195f, 0.007f,5.79f, 1.12f) \
 X(3.0073f,-4.1009f, 0.005f,6.24f, 3.86f) \
 X(0.3507f,-0.3705f, 0.110f,1.98f, 5.31f) \
 X(-0.4731f,-0.2409f,0.090f,2.02f, 0.93f) \
 X(0.6419f, 0.5344f, 0.085f,2.53f, 3.81f) \
 X(-0.3787f,0.9255f, 0.064f,2.77f, 2.19f) \
 X(1.0774f,-0.3441f, 0.060f,2.94f, 4.67f) \
 X(-1.1037f,-0.8015f,0.046f,3.23f, 1.87f) \
 X(0.1158f, 1.6119f, 0.040f,3.52f, 0.43f) \
 X(1.7408f,-0.8109f, 0.032f,3.84f, 5.63f) \
 X(-1.3733f,1.8788f, 0.025f,4.22f, 3.03f) \
 X(2.8292f, 0.0917f, 0.020f,4.66f, 1.71f) \
 X(-1.2031f,-2.9508f,0.016f,4.94f, 4.37f) \
 X(2.3156f, 3.0217f, 0.013f,5.40f, 2.57f)

namespace WaterSurfaceModel {
struct Sample { float height, dx, dy; };
inline Sample Evaluate(float x, float y, float seconds) {
    Sample value = {0,0,0};
#define WATER_MODE_EVAL(kx,ky,amplitude,omega,phase) { \
    const float p = x*kx+y*ky-seconds*omega+phase; \
    const float s = std::sin(p), c = std::cos(p); \
    value.height += amplitude*(s+0.13f*2.0f*s*c); \
    const float derivative = amplitude*(c+0.26f*(c*c-s*s)); \
    value.dx += derivative*kx; value.dy += derivative*ky; }
    WATER_SURFACE_MODES(WATER_MODE_EVAL)
#undef WATER_MODE_EVAL
    return value;
}
// Displacement uses exactly the same height arithmetic without retaining the outlined
// full evaluator's slope accumulation on the release compiler.
inline float EvaluateHeight(float x, float y, float seconds) {
    float height=0;
#define WATER_MODE_HEIGHT_EVAL(kx,ky,amplitude,omega,phase) { \
    const float p = x*kx+y*ky-seconds*omega+phase; \
    const float s = std::sin(p), c = std::cos(p); \
    height += amplitude*(s+0.13f*2.0f*s*c); }
    WATER_SURFACE_MODES(WATER_MODE_HEIGHT_EVAL)
#undef WATER_MODE_HEIGHT_EVAL
    return height;
}
// Open water has its own spectrum. Height energy is shared by several crossing swells rather
// than concentrated into the old river's first two directions. A slowly evolving coordinate
// warp bends the crests, and a bounded packet envelope breaks their uniform strength.
#define WATER_OCEAN_MODES(X) \
 X( 0.1074f, 0.0423f,1.200f,1.02f,0.31f) \
 X( 0.1383f,-0.1092f,1.000f,1.25f,2.41f) \
 X(-0.0729f, 0.2148f,0.900f,1.52f,4.33f) \
 X( 0.2481f, 0.1554f,0.720f,1.75f,1.20f) \
 X(-0.2952f, 0.1176f,0.600f,1.91f,5.17f) \
 X( 0.1689f,-0.3501f,0.480f,2.15f,3.62f) \
 X( 0.4443f, 0.0519f,0.360f,2.37f,0.83f) \
 X(-0.3387f,-0.3822f,0.260f,2.49f,4.71f) \
 X( 0.0891f, 0.6243f,0.180f,2.74f,2.07f) \
 X( 0.6642f,-0.3291f,0.120f,3.00f,5.91f) \
 X(-0.6801f, 0.5724f,0.085f,3.24f,1.79f) \
 X( 0.8943f, 0.3048f,0.060f,3.51f,3.11f) \
 X( 0.7300f, 0.3600f,0.110f,3.32f,4.61f) \
 X( 0.9300f,-0.6400f,0.084f,3.74f,0.73f) \
 X(-1.2400f, 0.3100f,0.062f,4.08f,2.97f) \
 X(-0.8200f,-1.3500f,0.050f,4.47f,5.13f) \
 X( 1.6800f, 0.5200f,0.036f,4.72f,1.87f) \
 X( 1.4500f,-1.8000f,0.026f,5.19f,3.43f) \
 X(-2.6300f, 0.8800f,0.020f,5.64f,0.29f) \
 X( 0.6200f, 3.3400f,0.016f,6.02f,4.07f)

struct OceanSurface { float height,dx,dy,laplacian; };
// The height-only evaluator retains the full model's warp and envelope arithmetic.
struct OceanWarp { float q0,q1,s0,s1,px,py; };
inline OceanWarp OceanWarpAt(float x,float y,float seconds) {
    const float q0=x*0.0241f+y*0.0157f-seconds*0.23f+0.4f;
    const float q1=x*-0.0173f+y*0.0299f-seconds*0.19f+2.1f;
    const float s0=std::sin(q0),s1=std::sin(q1);
    return {q0,q1,s0,s1,x+5.0f*s0,y+4.0f*s1};
}
inline float OceanEnvelope(float s0,float s1) { return 0.86f+0.14f*s0*s1; }
template<bool Curvature,bool Slopes=true,bool SwellOnly=false> inline OceanSurface EvaluateOceanInternal(float x,float y,float seconds) {
    const float q0=x*0.0241f+y*0.0157f-seconds*0.23f+0.4f;
    const float q1=x*-0.0173f+y*0.0299f-seconds*0.19f+2.1f;
    const float s0=std::sin(q0),c0=std::cos(q0),s1=std::sin(q1),c1=std::cos(q1);
    const float px=x+5.0f*s0,py=y+4.0f*s1;
    const float xx=1.0f+5.0f*c0*0.0241f,xy=5.0f*c0*0.0157f;
    const float yx=4.0f*c1*-0.0173f,yy=1.0f+4.0f*c1*0.0299f;
    const float q0Length2=0.0241f*0.0241f+0.0157f*0.0157f;
    const float q1Length2=0.0173f*0.0173f+0.0299f*0.0299f;
    const float qDot=0.0241f*-0.0173f+0.0157f*0.0299f;
    const float lapX=-5.0f*s0*q0Length2,lapY=-4.0f*s1*q1Length2;
    OceanSurface value={0,0,0,0};
#define WATER_OCEAN_EVAL(kx,ky,amplitude,omega,phase) if (!SwellOnly || kx*kx+ky*ky<0.18f) { \
    const float p=px*kx+py*ky-seconds*omega+phase; \
    const float s=std::sin(p),c=std::cos(p); \
    value.height+=amplitude*(s+0.16f*s*c); \
    if (Slopes || Curvature) { \
        const float derivative=amplitude*(c+0.16f*(c*c-s*s)); \
        const float phaseX=kx*xx+ky*yx,phaseY=kx*xy+ky*yy; \
        value.dx+=derivative*phaseX; value.dy+=derivative*phaseY; \
        if (Curvature) value.laplacian+=amplitude*(-s-0.64f*s*c)*(phaseX*phaseX+phaseY*phaseY)+derivative*(kx*lapX+ky*lapY); } }
    WATER_OCEAN_MODES(WATER_OCEAN_EVAL)
#undef WATER_OCEAN_EVAL
    const float envelope=0.86f+0.14f*s0*s1;
    const float ex=0.14f*(c0*0.0241f*s1+s0*c1*-0.0173f);
    const float ey=0.14f*(c0*0.0157f*s1+s0*c1*0.0299f);
    const float lapEnvelope=0.14f*(-s0*s1*(q0Length2+q1Length2)+2.0f*c0*c1*qDot);
    const float laplacian=Curvature ? value.laplacian*envelope+2.0f*(value.dx*ex+value.dy*ey)+value.height*lapEnvelope : 0.0f;
    return {value.height*envelope,Slopes?value.dx*envelope+value.height*ex:0.0f,
        Slopes?value.dy*envelope+value.height*ey:0.0f,laplacian};
}
inline OceanSurface EvaluateOceanSurface(float x,float y,float seconds) { return EvaluateOceanInternal<true>(x,y,seconds); }
// Foam breaking and broad face lighting follow the swell curvature, rather than the much larger
// second derivatives of tiny ripples. This subset retains exactly the full model's warp/envelope.
inline OceanSurface EvaluateOceanSwell(float x,float y,float seconds) { return EvaluateOceanInternal<true,true,true>(x,y,seconds); }
inline Sample EvaluateOcean(float x,float y,float seconds) {
    // Displacement does not need curvature; the constant template branch removes its work.
    const OceanSurface value=EvaluateOceanInternal<false>(x,y,seconds);
    return {value.height,value.dx,value.dy};
}
// The fixed, unlit mesh vertex path does not consume ocean normals. A scalar return also
// avoids the release compiler's spills when returning the height-only OceanSurface struct.
// The mode accumulation, warp and envelope match the full model.
inline float EvaluateOceanHeight(float x,float y,float seconds) {
    const OceanWarp warp=OceanWarpAt(x,y,seconds);
    float height=0;
#define WATER_OCEAN_HEIGHT_EVAL(kx,ky,amplitude,omega,phase) { \
    const float p=warp.px*kx+warp.py*ky-seconds*omega+phase; \
    const float s=std::sin(p),c=std::cos(p); \
    height+=amplitude*(s+0.16f*s*c); }
    WATER_OCEAN_MODES(WATER_OCEAN_HEIGHT_EVAL)
#undef WATER_OCEAN_HEIGHT_EVAL
    return height*OceanEnvelope(warp.s0,warp.s1);
}
inline Sample BlendSamples(const Sample &river,const Sample &ocean,const Sample &offshore) {
    const float blend=offshore.height<0?0:(offshore.height>1?1:offshore.height);
    const float delta=ocean.height-river.height;
    return {river.height+delta*blend,
        river.dx+(ocean.dx-river.dx)*blend+delta*offshore.dx,
        river.dy+(ocean.dy-river.dy)*blend+delta*offshore.dy};
}
inline Sample EvaluateBlended(float x,float y,float seconds,const Sample &offshore) {
    if (offshore.height<=0 && offshore.dx==0 && offshore.dy==0) return Evaluate(x,y,seconds);
    if (offshore.height>=1 && offshore.dx==0 && offshore.dy==0) return EvaluateOcean(x,y,seconds);
    return BlendSamples(Evaluate(x,y,seconds),EvaluateOcean(x,y,seconds),offshore);
}
inline float EvaluateBlendedHeight(float x,float y,float seconds,const Sample &offshore) {
    if (offshore.height<=0 && offshore.dx==0 && offshore.dy==0) return EvaluateHeight(x,y,seconds);
    if (offshore.height>=1 && offshore.dx==0 && offshore.dy==0) return EvaluateOceanHeight(x,y,seconds);
    const float river=EvaluateHeight(x,y,seconds),ocean=EvaluateOceanHeight(x,y,seconds);
    const float blend=offshore.height<0?0:(offshore.height>1?1:offshore.height);
    return river+(ocean-river)*blend;
}
// The GPU context texture repeats the last real row/column into its padding. Its texel-centre UV
// maps to exactly these terrain-grid coordinates; derivatives here are per world unit.
inline Sample SampleOffshore(const std::uint8_t *field,int width,int height,float gx,float gy,float spacing) {
    if (!field || width<2 || height<2 || spacing<=0) return {0,0,0};
    const bool clampX=gx<0 || gx>width-1,clampY=gy<0 || gy>height-1;
    gx=gx<0?0:(gx>width-1?float(width-1):gx); gy=gy<0?0:(gy>height-1?float(height-1):gy);
    const int ix=int(gx)<width-1?int(gx):width-2,iy=int(gy)<height-1?int(gy):height-2;
    const float fx=gx-ix,fy=gy-iy;
    const float a=field[iy*width+ix]/255.0f,b=field[iy*width+ix+1]/255.0f;
    const float c=field[(iy+1)*width+ix]/255.0f,d=field[(iy+1)*width+ix+1]/255.0f;
    return {(a+(b-a)*fx)*(1-fy)+(c+(d-c)*fx)*fy,
        clampX?0:((b-a)*(1-fy)+(d-c)*fy)/spacing,
        clampY?0:((c-a)*(1-fx)+(d-b)*fx)/spacing};
}
inline const char *OceanShaderSource() {
#define WATER_OCEAN_HLSL(kx,ky,amplitude,omega,phase) \
    "{ float p=dot(warped,float2(" #kx "," #ky "))-seconds*" #omega "+" #phase ";" \
    "float s=sin(p),c=cos(p);float4 mode;mode.x=" #amplitude "*(s+0.16*s*c);" \
    "float derivative=" #amplitude "*(c+0.16*(c*c-s*s));" \
    "float2 phaseSlope=" #kx "*wx+" #ky "*wy;mode.yz=derivative*phaseSlope;" \
    "mode.w=" #amplitude "*(-s-0.64*s*c)*dot(phaseSlope,phaseSlope)+derivative*dot(float2(" #kx "," #ky "),warpLaplacian);" \
    "value+=mode;if(" #kx "*" #kx "+" #ky "*" #ky "<0.18) swell+=mode; }\n"
    return "float4 waterOceanSurface(float2 pxy,float seconds,out float4 swell) {\n"
        "float q0=dot(pxy,float2(0.0241,0.0157))-seconds*0.23+0.4;\n"
        "float q1=dot(pxy,float2(-0.0173,0.0299))-seconds*0.19+2.1;\n"
        "float s0=sin(q0),c0=cos(q0),s1=sin(q1),c1=cos(q1);\n"
        "float2 warped=pxy+float2(5.0*s0,4.0*s1);\n"
        "float2 wx=float2(1.0,0.0)+5.0*c0*float2(0.0241,0.0157);\n"
        "float2 wy=float2(0.0,1.0)+4.0*c1*float2(-0.0173,0.0299);float4 value=0;swell=0;\n"
        "float q0Length2=dot(float2(0.0241,0.0157),float2(0.0241,0.0157));\n"
        "float q1Length2=dot(float2(-0.0173,0.0299),float2(-0.0173,0.0299));\n"
        "float qDot=dot(float2(0.0241,0.0157),float2(-0.0173,0.0299));\n"
        "float2 warpLaplacian=float2(-5.0*s0*q0Length2,-4.0*s1*q1Length2);\n"
        WATER_OCEAN_MODES(WATER_OCEAN_HLSL)
        "float envelope=0.86+0.14*s0*s1;\n"
        "float2 envelopeSlope=0.14*(c0*s1*float2(0.0241,0.0157)+s0*c1*float2(-0.0173,0.0299));\n"
        "float lapEnvelope=0.14*(-s0*s1*(q0Length2+q1Length2)+2.0*c0*c1*qDot);\n"
        "float laplacian=value.w*envelope+2.0*dot(value.yz,envelopeSlope)+value.x*lapEnvelope;\n"
        "swell=float4(swell.x*envelope,swell.yz*envelope+swell.x*envelopeSlope,"
        "swell.w*envelope+2.0*dot(swell.yz,envelopeSlope)+swell.x*lapEnvelope);\n"
        "return float4(value.x*envelope,value.yz*envelope+value.x*envelopeSlope,laplacian); }\n"
        "float4 waterOceanSurface(float2 pxy,float seconds) { float4 swell;return waterOceanSurface(pxy,seconds,swell); }\n"
        "float3 waterOceanWave(float2 pxy,float seconds) { return waterOceanSurface(pxy,seconds).xyz; }\n";
#undef WATER_OCEAN_HLSL
}
inline const char *ShaderSource() {
#define WATER_MODE_HLSL(kx,ky,amplitude,omega,phase) \
    "{ float p=dot(pxy,float2(" #kx "," #ky "))-seconds*" #omega "+" #phase ";" \
    "float s=sin(p), c=cos(p); value.x+=" #amplitude "*(s+0.26*s*c);" \
    "float2 slope=" #amplitude "*(c+0.26*(c*c-s*s))*float2(" #kx "," #ky ");value.yz+=slope;" \
    "if(" #kx "*" #kx "+" #ky "*" #ky ">=1.0) rippleSlope+=slope; }\n"
    return "float3 waterWave(float2 pxy,float seconds,out float2 rippleSlope) { float3 value=0;rippleSlope=0;\n"
        WATER_SURFACE_MODES(WATER_MODE_HLSL)
        "return value; }\n"
        "float3 waterWave(float2 pxy,float seconds) { float2 rippleSlope;return waterWave(pxy,seconds,rippleSlope); }\n";
#undef WATER_MODE_HLSL
}
inline void AppendHLSL(std::string &source) { source += ShaderSource(); source += OceanShaderSource(); }
inline float AdvanceTime(float seconds, float elapsed, bool paused, bool scriptFrozen, bool debugFrozen) {
    if (paused || scriptFrozen || debugFrozen) return seconds;
    return seconds + (elapsed < 0 ? 0 : (elapsed > 0.1f ? 0.1f : elapsed));
}
inline float ShoreDamping(float depth) {
    float t = depth/9.0f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t*t*(3-2*t);
}
inline float BoxDamping(float x,float y,float major,float minor) {
    const float qx=std::abs(x)-major,qy=std::abs(y)-minor;
    const float outsideX=qx>0?qx:0,outsideY=qy>0?qy:0;
    const float inside=qx>qy?qx:qy;
    return ShoreDamping(std::sqrt(outsideX*outsideX+outsideY*outsideY)+(inside<0?inside:0));
}
inline bool BeyondObstacleFade(float dx,float dy,float radius) {
    // A one-unit conservative margin avoids changing results at the floating-point boundary
    // of the nine-unit fade. The bounding circle also encloses every oriented box footprint.
    const float reach=radius+10.0f;
    return dx*dx+dy*dy>reach*reach;
}
#define WATER_MODE_BOUND(kx,ky,amplitude,omega,phase) + amplitude * 1.13f
constexpr float LegacyMaxDisplacement = 0.0f WATER_SURFACE_MODES(WATER_MODE_BOUND);
#undef WATER_MODE_BOUND
#define WATER_OCEAN_BOUND(kx,ky,amplitude,omega,phase) + amplitude * 1.08f
constexpr float OceanMaxDisplacement = 0.0f WATER_OCEAN_MODES(WATER_OCEAN_BOUND);
#undef WATER_OCEAN_BOUND
constexpr float MaxDisplacement = OceanMaxDisplacement>LegacyMaxDisplacement?OceanMaxDisplacement:LegacyMaxDisplacement;
inline std::uint32_t EncodeObstacleAttenuation(std::uint32_t diffuse,float attenuation) {
    const float bounded=attenuation<0?0:(attenuation>1?1:attenuation);
    return (diffuse&0xff00ffffu)|(std::uint32_t(bounded*255.0f+0.5f)<<16);
}
inline float TerrainDamping(float depth,float offshore) {
    const float river=ShoreDamping(depth);
    if (offshore<=0) return river;
    const float blend=offshore>1?1:offshore;
    float ocean=(depth-0.15f)/OceanMaxDisplacement;
    ocean=ocean<0?0:(ocean>1?1:ocean);
    return river+(ocean-river)*blend;
}
constexpr int MeshCells = 128;
constexpr int MeshVertices = (MeshCells+1)*(MeshCells+1);
constexpr int MeshTriangles = MeshCells*MeshCells*2;
// This is a whole-map bound, before frustum rejection; each draw is still safely 16-bit indexed.
// It permits 64 cells on multi-segment rivers and 128 on broad oceans, so near-camera silhouettes
// resolve the dominant waves instead of relying on their shading to hide a sparse mesh.
constexpr int VertexBudget = 262144;
inline int IndexBatchRows(int cells) { return 65535/(cells*6); }
inline int CellsForPatches(int patches) {
    if (patches<=0) return MeshCells;
    int cells=MeshCells;
    while (cells>1 && static_cast<long long>(patches)*(cells+1)*(cells+1)>VertexBudget) cells/=2;
    return cells;
}
// All patches use one camera-height LOD, so both sides of a shared edge change together.
// Between two resolutions, the fine mesh morphs onto the coarse triangles before changing
// topology. Full detail is retained below 120 world units above the highest authored water.
struct MeshLOD { int cells; float morph; };
constexpr float FullDetailHeight = 120.0f;
constexpr float LODHeightRatio = 1.5f;
constexpr int MinimumLODCells = 8;
inline MeshLOD LODForCameraHeight(int baseCells, float height) {
    MeshLOD lod={baseCells,0.0f};
    const int minimum=baseCells<MinimumLODCells?baseCells:MinimumLODCells;
    while (height>=LODHeightRatio*FullDetailHeight && lod.cells>minimum) {
        height/=LODHeightRatio;
        lod.cells/=2;
    }
    if (height>FullDetailHeight && lod.cells>minimum) {
        const float t=(height-FullDetailHeight)/(FullDetailHeight*(LODHeightRatio-1.0f));
        lod.morph=t*t*(3.0f-2.0f*t);
    }
    return lod;
}
// A map-wide open-water extent makes detail independent of view-frustum crossings. Keep the
// existing river/pool LOD exactly on maps without a large ocean; the same budget and morph apply.
inline MeshLOD LODForView(int baseCells,float height,float oceanExtent) {
    float t=(oceanExtent-600.0f)/800.0f;
    t=t<0?0:(t>1?1:t);
    const float detailScale=1.0f+3.0f*t*t*(3.0f-2.0f*t);
    return LODForCameraHeight(baseCells,height/detailScale);
}
// Every fine vertex is a coarse vertex or the midpoint of a coarse edge/diagonal. The same
// a-to-d diagonal is used by the renderer's triangle indices. Endpoints are always even/even,
// allowing an in-place morph pass without overwriting samples that another vertex still needs.
struct CoarsePair { int first,second; };
inline CoarsePair CoarsePairForVertex(int i,int j,int cells) {
    const int side=cells+1,self=j*side+i;
    if (cells<2) return {self,self};
    const int ox=i&1,oy=j&1;
    return {(j-oy)*side+i-ox,(j+oy)*side+i+ox};
}
inline float MorphCoordinate(float fine,float a,float b,float morph) {
    if (morph<=0.0f) return fine;
    const float coarse=(a+b)*0.5f;
    if (morph>=1.0f) return coarse;
    return fine+(coarse-fine)*morph;
}
inline std::uint32_t MorphColour(std::uint32_t fine,std::uint32_t a,std::uint32_t b,float morph) {
    if (morph<=0.0f) return fine;
    std::uint32_t result=0;
    for (int shift=0;shift<32;shift+=8) {
        const float value=MorphCoordinate(float((fine>>shift)&255),float((a>>shift)&255),float((b>>shift)&255),morph);
        result|=std::uint32_t(value+0.5f)<<shift;
    }
    return result;
}
struct Point { float x,y,z; };
inline Point Lerp(const Point &a, const Point &b, float t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
// A camera-dependent density warp moves continuously and keeps exact edge endpoints. Every patch
// computes a shared edge in canonical endpoint order, so even reversed edges yield identical bytes.
inline Point EdgePoint(Point a, Point b, float t, const Point &camera) {
    if (a.x>b.x || (a.x==b.x && (a.y>b.y || (a.y==b.y && a.z>b.z)))) {
        const Point swap=a; a=b; b=swap; t=1-t;
    }
    if (t<=0) return a;
    if (t>=1) return b;
    const float dx=b.x-a.x,dy=b.y-a.y,len2=dx*dx+dy*dy;
    if (len2<0.0001f) return Lerp(a,b,t);
    float focus=((camera.x-a.x)*dx+(camera.y-a.y)*dy)/len2;
    focus=focus<0?0:(focus>1?1:focus);
    const Point close=Lerp(a,b,focus);
    const float ex=camera.x-close.x,ey=camera.y-close.y,ez=camera.z-close.z;
    float radius=0.25f*std::sqrt((ex*ex+ey*ey+ez*ez)/len2);
    radius=radius<0.025f?0.025f:(radius>0.65f?0.65f:radius);
    const float lo=std::asinh(-focus/radius),hi=std::asinh((1-focus)/radius);
    const float concentrated=focus+radius*std::sinh(lo+(hi-lo)*t);
    return Lerp(a,b,0.35f*t+0.65f*concentrated);
}
// Coons blending preserves all four common boundary curves, including the degenerate edge of a
// triangle. A modest warp preserves a broad uniform component, limiting stretched triangles.
inline Point PatchPoint(const Point corners[4], const Point edges[4], float u, float v) {
    if (v==0) return edges[0];
    if (u==1) return edges[1];
    if (v==1) return edges[2];
    if (u==0) return edges[3];
    const Point a=Lerp(edges[0],edges[2],v),b=Lerp(edges[3],edges[1],u);
    const Point linear=Lerp(Lerp(corners[0],corners[1],u),Lerp(corners[3],corners[2],u),v);
    return {a.x+b.x-linear.x,a.y+b.y-linear.y,a.z+b.z-linear.z};
}
}
