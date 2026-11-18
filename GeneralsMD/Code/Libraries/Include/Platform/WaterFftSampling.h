// Native sampling of the three-cascade Phillips ocean described by codeagent/webgl-ocean.
// The reference uses Y-up; the game uses Z-up. The field stays entirely on the GPU.
#pragma once
#include <string>
#include <cstdio>
#include <cstring>
#include "Platform/WaterWaveHeading.h"
#include "Platform/WaterCleanShader.h"
#include "Platform/WaterFftGeometry.h"
#include "Platform/WaterVesselWaves.h"
#include "Platform/WaterFftFiltering.h"
#include "Platform/WaterBowLift.h"
#include "Platform/WaterWaveCageShader.h"
#include "Platform/ModernWaterMaterial.h"

namespace WaterFftSampling {
// The reference's close water-level camera uses metre-sized waves. Generals' RTS camera
// requires a larger render scale; scale wavelength and displacement together, not normals.
constexpr float WorldScale=WaterFftSpectrum::WorldScale;
constexpr float TimeScale=0.4f;
// Both stages consume the same immutable draw snapshot. Slot zero remains the generated
// fixed vertex block; the vessel field is water-only slot one on the vertex stage.
inline std::string UniformSource(const char *name,unsigned slot,bool visual_hull=false,bool sdl=false) {
    std::string source=std::string("cbuffer ")+name+" : register(b"+std::to_string(slot)+R"HLSL()
{
    float4 TextureFactor;
    float4 FogColour;
    float4 AlphaReference;
    row_major float4x4 WorldFromClip;
    float4 CameraPosition;
    float4 SunDirection;
    float4 SunColour;
    float4 Viewport;
    float4 ReflectionArea;
    float4 WaterParameters;
    float4 TerrainParameters;
    float4 SceneParameters;
    float4 OpticalParameters;
    float4 HullParameters;
    float4 HullData[80];
    float4 FftParameters;
    float4 FoamHistoryArea;
    float4 FoamHistoryParameters;
    float4 ContactHullData[80];
    float4 FoamFillLights[4];
)HLSL";
#define MODERN_WATER_HLSL_UNIFORM(name) "    float4 " #name ";\n"
    source+=MODERN_WATER_MATERIAL_UNIFORMS(MODERN_WATER_HLSL_UNIFORM);
#undef MODERN_WATER_HLSL_UNIFORM
    if(sdl) source+="    row_major float4x4 ClipFromWorld;\n";
    if(visual_hull) source+="    float4 VisualHullMeta[8];\n";
    source+="};\n";
    if(visual_hull) source+=R"HLSL(
cbuffer WaterVisualBlock0 : register(b1) { float4 VisualHullBlock0[256]; };
cbuffer WaterVisualBlock1 : register(b2) { float4 VisualHullBlock1[256]; };
cbuffer WaterVisualBlock2 : register(b3) { float4 VisualHullBlock2[192]; };
)HLSL";
    return source;
}
inline std::string CommonSource()
{
    char limits[256];
    std::snprintf(limits,sizeof(limits),
        "static const float fftVerticalBound=%.9g;\nstatic const float fftHorizontalBound=%.9g;\nstatic const float fftWorldScale=%.9g;\n",
        double(WaterFftGeometry::MaxVerticalDisplacement),double(WaterFftGeometry::MaxHorizontalDisplacement)/1.4142135623730951,double(WorldScale));
    return std::string(limits)+R"HLSL(
// Smooth limits make culling/floor bounds independent of the random spectrum's extrema.
// Each XY component is bounded by the radial limit / sqrt(2), preserving the hull-clearance
// proof even for diagonal crests. Keep the derivative in the same model as displacement.
float3 fftLimit(float3 d) {
    float3 bound=float3(fftHorizontalBound,fftHorizontalBound,fftVerticalBound);
    return bound*tanh(d/bound);
}
float3 fftLimitDerivative(float3 d) {
    float3 q=tanh(d/float3(fftHorizontalBound,fftHorizontalBound,fftVerticalBound));
    return 1.0-q*q;
}
// Shared physical coordinate map: generated bow pressure changes only Z, so all
// incident/reflected XY can be resolved first without an iterative height solve.
float2 fftHorizontalPosition(float2 p,float3 raw,float scale) {
    return p+fftLimit(raw).xy*scale;
}
struct FftHorizontalRegistration { float2 position,derivativeX,derivativeY; };
FftHorizontalRegistration fftHorizontalRegistration(float2 p,float3 raw,float3 rawX,float3 rawY,float3 scale) {
    FftHorizontalRegistration map;
    map.position=fftHorizontalPosition(p,raw,scale.x);
    float2 limited=fftLimit(raw).xy,derivative=fftLimitDerivative(raw).xy;
    map.derivativeX=float2(1,0)+rawX.xy*derivative*scale.x+limited*scale.y;
    map.derivativeY=float2(0,1)+rawY.xy*derivative*scale.x+limited*scale.z;
    return map;
}
)HLSL";
}
constexpr unsigned SwellComponents=4;
constexpr double SwellWavelengths[SwellComponents]={287,431,683,1091};
constexpr double SwellOffsets[SwellComponents]={0.37,2.13,4.71,1.19};
constexpr double SwellNoiseSize=400, SwellNoiseAmount=0.30;
constexpr double SwellGravity=9.81, SwellHorizontal=-1.2;
constexpr double SwellHashScale=0.1031, SwellHashOffset=33.33;
// Both stages consume the same immutable draw snapshot. Slot zero remains the generated
// fixed vertex block; the vessel field is water-only slot one on the vertex stage.
// Opt-in Linux field; default/shared shader text remains unchanged.
inline std::string HeadingSource(bool vertex,unsigned resolution=WaterFftSpectrum::Resolution,bool clean=false) {
    char constants[2048];
    const double noiseVariance=(26.0/35)*(26.0/35)/12;
    std::snprintf(constants,sizeof(constants),
        "static const float fftGradientRange=%.9g;\nstatic const float fftSwellHeight=%.9g;\n"
        "static const float fftSwellTime=%.9g;\nstatic const float fftSwellTexel=%.9g;\n"
        "static const float4 fftSwellWavelengths=float4(%.9g,%.9g,%.9g,%.9g);\n"
        "static const float4 fftSwellOffsets=float4(%.9g,%.9g,%.9g,%.9g);\n"
        "static const float fftSwellNoiseSize=%.9g,fftSwellNoiseAmount=%.9g,fftSwellNoiseNormalization=%.9g;\n"
        "static const float fftSwellGravityScale=%.9g,fftSwellHorizontal=%.9g;\n"
        "static const float fftSwellHashScale=%.9g,fftSwellHashOffset=%.9g;\n",
        double(WaterWaveHeading::GradientRange),WaterFftSpectrum::HeightRms[0]*std::sqrt(2.0/SwellComponents),
        double(TimeScale),double(WorldScale*WaterFftSpectrum::Sizes[0]/resolution),
        SwellWavelengths[0],SwellWavelengths[1],SwellWavelengths[2],SwellWavelengths[3],
        SwellOffsets[0],SwellOffsets[1],SwellOffsets[2],SwellOffsets[3],
        SwellNoiseSize,SwellNoiseAmount,std::sqrt(1+SwellNoiseAmount*SwellNoiseAmount*noiseVariance),
        SwellGravity*WorldScale,SwellHorizontal,SwellHashScale,SwellHashOffset);
    char base[128];
    std::snprintf(base,sizeof(base),"static const float2 fftBaseHeading=float2(%.9g,%.9g);\n",
        double(WaterWaveHeading::BaseHeading.x),double(WaterWaveHeading::BaseHeading.y));
    std::string source=std::string(vertex?R"HLSL(
Texture2D FftHeadingTexture : register(t4);
SamplerState FftHeadingSampler : register(s4);
)HLSL":R"HLSL(
Texture2D FftHeadingTexture : register(t15);
SamplerState FftHeadingSampler : register(s15);
)HLSL")+constants+base+R"HLSL(
float2 fftRotate(float2 p,float2 cs) {return float2(cs.x*p.x-cs.y*p.y,cs.y*p.x+cs.x*p.y);}
float3 fftRotateVector(float3 v,float2 cs) {return float3(fftRotate(v.xy,cs),v.z);}
struct FftSwell {float3 displacement,derivativeX,derivativeY,shadingDisplacement,shadingDerivativeX,shadingDerivativeY;float variance;
    float2 rotation0,rotation1,weights;};
float4 fftSwellField(float2 p) {
    uint width,height;FftHeadingTexture.GetDimensions(width,height);
    float2 cell=clamp((p+TerrainParameters.xx)*(width/2)/max(TerrainParameters.y,1.0),0.0,float2(width/2-1,height-1));
    int2 lo=int2(floor(cell)),hi=min(lo+1,int2(width/2-1,height-1));
    float2 f=frac(cell),texel=1.0/float2(width,height);float4 value=0;float weight=0;
    // The sampler is point-filtered and these are texel centres, so each read is one exact packed
    // texel. Sampling, not Load, keeps the image paired with its sampler for ShaderCross.
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 at=int2(x?hi.x:lo.x,y?hi.y:lo.y);
        float2 centre=(float2(at.x*2,at.y)+0.5)*texel;
        float4 a=round(FftHeadingTexture.SampleLevel(FftHeadingSampler,centre,0)*255.0);
        float4 b=FftHeadingTexture.SampleLevel(FftHeadingSampler,centre+float2(texel.x,0),0);
        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y)*step(0.5,b.a);
        value+=w*float4((a.r*256+a.g)*FoamFillLights[3].w,(a.ba-128.0)*fftGradientRange/127.0,b.r);
        weight+=w;
    }
    return weight>0?value/weight:0;
}
// Differentiable value noise along crests. Return value and its world gradient.
float fftSwellHash(float2 p) {
    float3 q=frac(float3(p.x,p.y,p.x)*fftSwellHashScale);
    q+=dot(q,q.yzx+fftSwellHashOffset);return frac((q.x+q.y)*q.z);
}
float3 fftSwellNoise(float2 p) {
    float2 q=p/fftSwellNoiseSize,i=floor(q),f=frac(q),u=f*f*(3-2*f),du=6*f*(1-f)/fftSwellNoiseSize;
    float a=fftSwellHash(i),b=fftSwellHash(i+float2(1,0)),c=fftSwellHash(i+float2(0,1)),d=fftSwellHash(i+1);
    return float3(lerp(lerp(a,b,u.x),lerp(c,d,u.x),u.y),
        lerp(b-a,d-c,u.y)*du.x,lerp(c-a,d-b,u.x)*du.y);
}
FftSwell fftSwell(float2 p,float lod) {
    float4 field=fftSwellField(p);float2 g=field.yz,n=g/max(length(g),0.00001);
    FftSwell value=(FftSwell)0;
    // The FFT cascades, the waves actually seen moving at game scale, follow the refracted
    // direction in 45-degree sectors. The field turns gradually and never flips across a channel,
    // so the pairwise sector cross-fade stays spread out. Unreached water keeps the prevailing wind.
    if(dot(g,g)<1e-10) {
        sincos(FftParameters.w,value.rotation0.y,value.rotation0.x);
        value.rotation1=value.rotation0;value.weights=float2(1,0);
    } else {
        float phi=atan2(fftBaseHeading.x*n.y-fftBaseHeading.y*n.x,dot(fftBaseHeading,n));
        float sector=phi/0.7853981633974483,k=floor(sector),t=frac(sector),u=saturate((t-0.25)/0.5);
        sincos(k*0.7853981633974483,value.rotation0.y,value.rotation0.x);
        sincos((k+1.0)*0.7853981633974483,value.rotation1.y,value.rotation1.x);
        // Explicit endpoints make the zero-weight branch exact (cos(pi/2) need not be zero).
        value.weights=u<=0.0?float2(1,0):u>=1.0?float2(0,1):float2(cos(u*1.5707963267948966),sin(u*1.5707963267948966));
    }
    // Four non-harmonic wavelengths inside the broad band; equal powers sum to HeightRms[0]^2.
    float3 noise=fftSwellNoise(p);
    // Mean-square normalization: smooth bilinear value noise has variance (26/35)^2/12.
    float gain=(1+fftSwellNoiseAmount*(noise.x-0.5))/fftSwellNoiseNormalization;
    float2 gainGradient=fftSwellNoiseAmount*noise.yz/fftSwellNoiseNormalization;
    [unroll] for(int i=0;i<4;++i) {
        float k=6.283185307179586/fftSwellWavelengths[i],sn,cs;
        sincos(k*field.x-sqrt(fftSwellGravityScale*k)*WaterParameters.x*fftSwellTime+fftSwellOffsets[i],sn,cs);
        float a=fftSwellHeight*field.w;
        float3 d=a*gain*float3(fftSwellHorizontal*n*sn,cs);
        float3 dx=a*(gain*float3(fftSwellHorizontal*n*cs,-sn)*k*g.x+gainGradient.x*float3(fftSwellHorizontal*n*sn,cs));
        float3 dy=a*(gain*float3(fftSwellHorizontal*n*cs,-sn)*k*g.y+gainGradient.y*float3(fftSwellHorizontal*n*sn,cs));
        // Gaussian with the box mip's second moment; level zero remains exact.
        float filter=exp(-k*k*dot(g,g)*max(0.0,exp2(2*lod)-1)*fftSwellTexel*fftSwellTexel/24.0);
        value.displacement+=d;value.derivativeX+=dx;value.derivativeY+=dy;
        value.shadingDisplacement+=filter*d;value.shadingDerivativeX+=filter*dx;value.shadingDerivativeY+=filter*dy;
        value.variance+=0.5*a*a*gain*gain*k*k*dot(g,g)*(1-filter*filter);
    }
    return value;
}
FftSwell fftSwell(float2 p) {return fftSwell(p,0.0);}
FftSwell fftFilteredSwell(FftSwell value) {
    value.displacement=value.shadingDisplacement;value.derivativeX=value.shadingDerivativeX;value.derivativeY=value.shadingDerivativeY;
    return value;
}
)HLSL";
    if(clean) {
        for(const auto &r:{std::pair<const char*,const char*>{"width/2","width/4"},{"at.x*2","at.x*4"}})
            for(size_t at=source.find(r.first);at!=std::string::npos;at=source.find(r.first,at+std::strlen(r.second)))source.replace(at,std::strlen(r.first),r.second);
    }
    return source;
}
inline std::string HeadingVertexSource() {
    return R"HLSL(
// The texture-array rewrite expects each cascade sampled once, at q.
float3 fftChopDisplacement(float2 p,float2 r) {
    float2 q=fftRotate(p,r*float2(1,-1));
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,q/(103.0*fftWorldScale),0).xyz;
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xyz;
    return fftRotateVector((b.xzy+c.xzy)*float3(-1.5,-1.5,1)*fftWorldScale,r);
}
float3 fftRawDisplacement(float2 p) {
    [branch] if(FftParameters.z<=0.5) return fftUnrotatedDisplacement(p);
    FftSwell swell=fftSwell(p);float3 result=swell.displacement;
    [branch] if(swell.weights.x>0.0) result+=swell.weights.x*fftChopDisplacement(p,swell.rotation0);
    [branch] if(swell.weights.y>0.0) result+=swell.weights.y*fftChopDisplacement(p,swell.rotation1);
    return result;
}
)HLSL";
}
inline std::string HeadingPixelSource() {
    return R"HLSL(
// Both FFT cascades at one sector rotation, accumulated with its cross-fade weight. The medium
// cascade also feeds ambient breaking and contact wash, kept apart in `medium`.
void fftAccumulateChop(inout FftSurface medium,inout FftSurface fine,float2 p,float3 lod,float2 r,float w) {
    float2 q=fftRotate(p,r*float2(1,-1));
    float4 b=Texture10.SampleLevel(Sampler10,q/(103.0*fftWorldScale),lod.y);
    float4 bd=Texture11.SampleLevel(Sampler11,q/(103.0*fftWorldScale),lod.y);
    float4 c=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod.z);
    float4 cd=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod.z);
    float3 x=float3(bd.z,b.w,bd.x)*float3(-1.5,-1.5,1),y=float3(b.w,bd.w,bd.y)*float3(-1.5,-1.5,1);
    medium.displacement+=w*fftRotateVector(b.xzy*float3(-1.5,-1.5,1)*fftWorldScale,r);
    // Chain rule for q=R(-a)p, then R(a) on displacement XY.
    medium.derivativeX+=w*fftRotateVector(r.x*x-r.y*y,r);
    medium.derivativeY+=w*fftRotateVector(r.y*x+r.x*y,r);
    medium.contactIncident.yz+=w*fftRotate(bd.xy,r);
    x=float3(cd.z,c.w,cd.x)*float3(-1.5,-1.5,1);y=float3(c.w,cd.w,cd.y)*float3(-1.5,-1.5,1);
    fine.displacement+=w*fftRotateVector(c.xzy*float3(-1.5,-1.5,1)*fftWorldScale,r);
    fine.derivativeX+=w*fftRotateVector(r.x*x-r.y*y,r);
    fine.derivativeY+=w*fftRotateVector(r.y*x+r.x*y,r);
}
FftSurface fftSwellSurface(float2 p,float3 lod,FftSwell swell) {
    FftSurface medium=(FftSurface)0,fine=(FftSurface)0;
    [branch] if(swell.weights.x>0.0) fftAccumulateChop(medium,fine,p,lod,swell.rotation0,swell.weights.x);
    [branch] if(swell.weights.y>0.0) fftAccumulateChop(medium,fine,p,lod,swell.rotation1,swell.weights.y);
    // Broad swell and medium chop carry ambient breaking and contact wash, as cascades 0 and 1 did.
    float2 breaking=medium.displacement.xy+swell.displacement.xy;
    float2 breakingX=medium.derivativeX.xy+swell.derivativeX.xy,breakingY=medium.derivativeY.xy+swell.derivativeY.xy;
    float2 limit=fftLimitDerivative(float3(breaking,0)).xy;
    breakingX*=limit;breakingY*=limit;
    FftSurface value=(FftSurface)0;
    value.incidentCompression=(1+breakingX.x)*(1+breakingY.y)-breakingX.y*breakingY.x;
    float height=fftVerticalBound*tanh((medium.displacement.z+swell.displacement.z)/fftVerticalBound);
    float2 slope=medium.contactIncident.yz+float2(swell.derivativeX.z,swell.derivativeY.z);
    value.contactIncident=float3(height,slope*(1-(height/fftVerticalBound)*(height/fftVerticalBound)));
    value.displacement=medium.displacement+fine.displacement+swell.displacement;
    value.derivativeX=medium.derivativeX+fine.derivativeX+swell.derivativeX;
    value.derivativeY=medium.derivativeY+fine.derivativeY+swell.derivativeY;
    value.unresolvedSlopeVariance=swell.variance;
    return value;
}
FftSurface fftRawSurface(float2 p,float3 lod) {
    [branch] if(FftParameters.z<=0.5) return fftUnrotatedSurface(p,lod);
    return fftSwellSurface(p,lod,fftFilteredSwell(fftSwell(p,lod.x)));
}
FftSurface fftRawSurface(float2 p) {return fftRawSurface(p,float3(0,0,0));}
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    [branch] if(FftParameters.z<=0.5) {fftUnrotatedAndShadingSurface(p,lod,physical,shading);return;}
    FftSwell swell=fftSwell(p,lod.x);
    physical=fftSwellSurface(p,float3(0,0,0),swell);shading=physical;
    [branch] if(any(lod>0.0)) shading=fftSwellSurface(p,lod,fftFilteredSwell(swell));
}
)HLSL";
}
inline std::string VertexSource(bool waveHeading=false,bool clean=false)
{
    std::string source=std::string(R"HLSL(
Texture2D FftDisplacement0 : register(t0);
SamplerState FftSampler0 : register(s0);
Texture2D FftDisplacement1 : register(t1);
SamplerState FftSampler1 : register(s1);
Texture2D FftDisplacement2 : register(t2);
SamplerState FftSampler2 : register(s2);
)HLSL")+CommonSource()+WaterBowLift::Source()+WaterVesselWaves::Source()+R"HLSL(
float3 fftRawDisplacement(float2 p) {
    float3 a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xyz;
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,p/(103.0*fftWorldScale),0).xyz;
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,p/(13.0*fftWorldScale),0).xyz;
    return (a.xzy*float3(-1.2,-1.2,1)+b.xzy*float3(-1.5,-1.5,1)
        +c.xzy*float3(-1.5,-1.5,1))*fftWorldScale;
}
float3 fftVertexBowDisplacement(float2 p,float level,float horizontalScale,out float bowHeight) {
    bowHeight=0;
    float3 displacement=fftRawDisplacement(p);
    {
        int count=clamp((int)HullParameters.x,0,8);
        float3 oldBow[8];
        for(int h=0;h<count;++h) {
            VesselReflection reflected=vesselReflection(p,level,h);
            if(reflected.weight>0.0)
                displacement+=vesselReflectedValue(fftRawDisplacement(reflected.samplePosition),reflected);
            displacement.z+=vesselGeneratedWave(p,level,WaterParameters.x,h,oldBow[h]).x;
        }
        {
            float2 position=fftHorizontalPosition(p,displacement,horizontalScale);
            for(int h=0;h<count;++h)
                {float currentBow=vesselGeneratedBow(position,level,h).x;
                 displacement.z+=currentBow-oldBow[h].x;bowHeight+=currentBow;}
        }
    }
    return fftLimit(displacement);
}
float3 fftVertexDisplacement(float2 p,float level,float horizontalScale) {
    float ignored;return fftVertexBowDisplacement(p,level,horizontalScale,ignored);
}
float3 fftVertexDisplacement(float2 p,float level) {return fftVertexDisplacement(p,level,0.0);}
float3 fftVertexDisplacement(float2 p) {return fftVertexDisplacement(p,WaterParameters.w);}
)HLSL";
    if(clean) {
        const std::string entry="float3 fftRawDisplacement(float2 p)";
        source.replace(source.find(entry),entry.size(),"float3 fftUnrotatedDisplacement(float2 p)");
        const size_t end=source.find("\n}",source.find("float3 fftUnrotatedDisplacement"))+3;
        source.insert(end,HeadingSource(true,WaterFftSpectrum::Resolution,true)+WaterCleanShader::VertexSource()+"float3 fftRawDisplacement(float2 p){return fftCleanDisplacement(p,fftMediumDepthField(p));}\n");
    } else if(waveHeading) {
        const std::string entry="float3 fftRawDisplacement(float2 p)";
        source.replace(source.find(entry),entry.size(),"float3 fftUnrotatedDisplacement(float2 p)");
        const size_t end=source.find("\n}",source.find("float3 fftUnrotatedDisplacement"))+3;
        source.insert(end,HeadingSource(true)+HeadingVertexSource());
    }
    return source;
}
// Add a water-only wrapper to the generated fixed vertex program. It retains the engine's
// original projection, shroud coordinate generation and fog after moving the actual vertex.
inline bool WrapVertex(std::string &hlsl,bool sdl=false,bool waveHeading=false,bool clean=false)
{
    const std::string entry="Output main(Input input)";
    const size_t at=hlsl.find(entry);
    if(at==std::string::npos || hlsl.find("float3 Normal")==std::string::npos
        || hlsl.find("float2 TexCoord1")==std::string::npos) return false;
    hlsl.replace(at,entry.size(),"Output waterFixedVertex(Input input)");
    hlsl+=UniformSource("WaterVertexParameters",1,false,sdl);
    hlsl+=VertexSource(waveHeading,clean);
    hlsl+=WaterWaveCageShader::Source(true);
    hlsl+=R"HLSL(
Output main(Input input)
{
    float2 parameter=input.Position.xy;
    float weight=saturate(input.Normal.x), freeDamping=saturate(input.Normal.y);
    float offshore=saturate(input.Normal.z);
    // Carry the same vertical phases through the shallows, while retaining the
    // coastal restriction on horizontal chop and its terrain/obstacle clearance.
    float horizontalWeight=weight*offshore;
    float originalHeight=input.Position.z-input.TexCoord1.y;
    if(weight>0.0) {
        float damping=vesselApplyHullDamping(parameter,input.TexCoord1.y,
            float3(freeDamping,0,0),float3(offshore,0,0)).x;
        float bowHeight;
        float3 displacement=fftVertexBowDisplacement(parameter,input.TexCoord1.y,horizontalWeight*damping,bowHeight);
        float lifted=fftBowLift(float3(bowHeight,0,0),float3(freeDamping,0,0),float3(damping,0,0)).x;
        WaveCage cage=waveCage(parameter,input.TexCoord1.y);
        float cageAmount=cage.support;
        float3 cageDisplacement=0;
        if(cageAmount>0.0) {
            float cageObstacle=vesselApplyHullDamping(parameter,input.TexCoord1.y,
                float3(saturate(input.Diffuse.r),0,0),float3(1,0,0)).x;
            if(cageObstacle>0.0)
                cageDisplacement=waveCageFloor(parameter,input.TexCoord1.y,waveCageMap(cage,displacement)*cageObstacle);
        }
        input.Position.xy+=lerp(displacement.xy*(horizontalWeight*damping),cageDisplacement.xy*weight,cageAmount);
        input.Position.z=input.TexCoord1.y+lerp(originalHeight,displacement.z*damping+lifted,weight);
        input.Position.z=lerp(input.Position.z,input.TexCoord1.y+lerp(originalHeight,cageDisplacement.z,weight),cageAmount);
    }
    input.Normal=float3(0,0,1);
    Output output=waterFixedVertex(input);
    // TC3 is the existing shroud; TC2 and specular are unused by this water material.
    output.TexCoord2=parameter;
    output.Specular=float4(weight,freeDamping,originalHeight,offshore);
    return output;
}
)HLSL";
    return true;
}
inline std::string PixelSource(WaterFftSpectrum::Configuration configuration=WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution),bool waveHeading=false,bool clean=false)
{
    std::string source=CommonSource()+WaterBowLift::Source()+WaterVesselWaves::Source()+WaterFftFiltering::Source(configuration,clean)+R"HLSL(
struct FftSurface {
    float3 bow;
    float3 displacement;
    float3 derivativeX;
    float3 derivativeY;
    float incidentCompression;
    float3 contactIncident;
    float vesselCrest;
    float3 shadingDisplacement;
    float3 shadingDerivativeX;
    float3 shadingDerivativeY;
    float unresolvedSlopeVariance;
};
struct FftMapSamples { float4 a,ad,b,bd,c,cd; };
FftMapSamples fftMapSamples(float2 p,float3 lod) {
    FftMapSamples samples;
    samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod[0]);
    samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod[0]);
    samples.b=Texture10.SampleLevel(Sampler10,p/(103.0*fftWorldScale),lod[1]);
    samples.bd=Texture11.SampleLevel(Sampler11,p/(103.0*fftWorldScale),lod[1]);
    samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod[2]);
    samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod[2]);
    return samples;
}
FftSurface fftDecodeSamples(FftMapSamples samples) {
    float4 a=samples.a,ad=samples.ad,b=samples.b,bd=samples.bd,c=samples.c,cd=samples.cd;
    // Keep the same decode operations across paired and independent shader contexts.
    // Reassociation/FMA contraction must not make physical geometry depend on footprint.
    precise float3 d=(a.xzy*float3(-1.2,-1.2,1)+b.xzy*float3(-1.5,-1.5,1)
        +c.xzy*float3(-1.5,-1.5,1))*fftWorldScale;
    // Scaling wavelength and height together leaves dimensionless derivatives unchanged.
    precise float3 x=float3(ad.z,a.w,ad.x)*float3(-1.2,-1.2,1)
        +float3(bd.z,b.w,bd.x)*float3(-1.5,-1.5,1)+float3(cd.z,c.w,cd.x)*float3(-1.5,-1.5,1);
    precise float3 y=float3(a.w,ad.w,ad.y)*float3(-1.2,-1.2,1)
        +float3(b.w,bd.w,bd.y)*float3(-1.5,-1.5,1)+float3(c.w,cd.w,cd.y)*float3(-1.5,-1.5,1);
    FftSurface value=(FftSurface)0;
    value.displacement=d;
    value.derivativeX=x;
    value.derivativeY=y;
    // Ambient breaking follows the broad and medium incident surface. Fine ripples
    // still displace geometry and contribute their full normals, but their short-scale
    // compression must not turn the entire surface into whitecaps. Limit this selected
    // surface independently: a limiter evaluated on all three cascades would let the
    // fine displacement modulate the breaking signal even with its derivatives removed.
    precise float2 breakingDisplacement=(a.xz*-1.2+b.xz*-1.5)*fftWorldScale;
    precise float2 breakingX=float2(ad.z,a.w)*-1.2+float2(bd.z,b.w)*-1.5;
    precise float2 breakingY=float2(a.w,ad.w)*-1.2+float2(b.w,bd.w)*-1.5;
    float2 breakingLimit=fftLimitDerivative(float3(breakingDisplacement,0)).xy;
    breakingX*=breakingLimit;
    breakingY*=breakingLimit;
    value.incidentCompression=(1+breakingX.x)*(1+breakingY.y)-breakingX.y*breakingY.x;
    // Contact wash responds to the incident swell/wind waves. Reflected or generated
    // vessel fields and fine ripples must not regenerate stationary propulsion foam.
    // Reuse these physical samples; no extra texture fetch or vertex displacement.
    float contactHeight=fftVerticalBound*tanh((a.y+b.y)*fftWorldScale/fftVerticalBound);
    float contactDerivative=1.0-(contactHeight/fftVerticalBound)*(contactHeight/fftVerticalBound);
    value.contactIncident=float3(contactHeight,(ad.xy+bd.xy)*contactDerivative);
    return value;
}
FftSurface fftRawSurface(float2 p,float3 lod) {return fftDecodeSamples(fftMapSamples(p,lod));}
FftSurface fftRawSurface(float2 p) {return fftRawSurface(p,float3(0,0,0));}
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    FftMapSamples samples=fftMapSamples(p,float3(0,0,0));
    physical=fftDecodeSamples(samples);
    shading=physical;
    if(any(lod>0.0)) {
        // Preserve each already resolved cascade's exact mip-zero samples. Only the
        // unresolved pairs need another lookup; decoding and accumulation order stay identical.
        [branch] if(lod.x>0.0) {
            samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod.x);
            samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod.x);
        }
        [branch] if(lod.y>0.0) {
            samples.b=Texture10.SampleLevel(Sampler10,p/(103.0*fftWorldScale),lod.y);
            samples.bd=Texture11.SampleLevel(Sampler11,p/(103.0*fftWorldScale),lod.y);
        }
        [branch] if(lod.z>0.0) {
            samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod.z);
            samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod.z);
        }
        shading=fftDecodeSamples(samples);
    }
}
FftSurface fftSurface(float2 p,float level,float footprint,float3 horizontalScale) {
    float3 shadingLod=fftShadingLod(footprint);
    bool filtered=any(shadingLod>0.0);
    FftSurface value,shading;
    fftRawAndShadingSurface(p,shadingLod,value,shading);
    float reflectionVariance=1.0;
    // Mirroring a field around a hull changes its parameterization; a compressed mirror
    // Jacobian alone is not a breaking wave. Preserve incident whitecaps and add only the
    // restrained bow/wake crest signal below, while using full derivatives for the normals.
    // fftDecodeSamples already retained the broad/medium incident compression before
    // any hull reflection or generated vessel packet changes this full surface.
    {
        int count=clamp((int)HullParameters.x,0,8);
        float3 oldBow[8];float oldGenerated[8];
        for(int h=0;h<count;++h) {
            VesselReflection reflected=vesselReflection(p,level,h);
            if(reflected.weight>0.0) {
                FftSurface incident,shadingIncident;
                fftRawAndShadingSurface(reflected.samplePosition,shadingLod,incident,shadingIncident);
                if(filtered) {
                    shading.displacement+=vesselReflectedValue(shadingIncident.displacement,reflected);
                    shading.derivativeX+=vesselReflectedDerivative(shadingIncident.displacement,shadingIncident.derivativeX,
                        shadingIncident.derivativeY,float2(1,0),reflected);
                    shading.derivativeY+=vesselReflectedDerivative(shadingIncident.displacement,shadingIncident.derivativeX,
                        shadingIncident.derivativeY,float2(0,1),reflected);
                    reflectionVariance+=reflected.weight*reflected.weight;
                }
                value.displacement+=vesselReflectedValue(incident.displacement,reflected);
                value.derivativeX+=vesselReflectedDerivative(incident.displacement,incident.derivativeX,
                    incident.derivativeY,float2(1,0),reflected);
                value.derivativeY+=vesselReflectedDerivative(incident.displacement,incident.derivativeX,
                    incident.derivativeY,float2(0,1),reflected);
            }
            float3 generated=vesselGeneratedWave(p,level,WaterParameters.x,h,oldBow[h]);
            oldGenerated[h]=generated.x;
            if(filtered) {
                shading.displacement.z+=generated.x;
                shading.derivativeX.z+=generated.y;
                shading.derivativeY.z+=generated.z;
            }
            value.displacement.z+=generated.x;
            value.derivativeX.z+=generated.y;
            value.derivativeY.z+=generated.z;
            value.vesselCrest+=max(generated.x,0.0)*0.32;
        }
        {
            // The physical mip-zero map is shared even when reflected lighting is filtered.
            // Retaining the original loop above preserves stern/history accumulation and
            // permits an exact zero-chop difference, without another FFT texture sample.
            FftHorizontalRegistration map=fftHorizontalRegistration(p,value.displacement,
                value.derivativeX,value.derivativeY,horizontalScale);
            for(int h=0;h<count;++h) {
                float3 bow=vesselGeneratedBow(map.position,level,h);
                bow.yz=float2(dot(bow.yz,map.derivativeX),dot(bow.yz,map.derivativeY));
                value.bow+=bow;
                float3 correction=bow-oldBow[h];
                if(filtered) {
                    shading.displacement.z+=correction.x;
                    shading.derivativeX.z+=correction.y;
                    shading.derivativeY.z+=correction.z;
                }
                value.displacement.z+=correction.x;
                value.derivativeX.z+=correction.y;
                value.derivativeY.z+=correction.z;
                value.vesselCrest+=(max(oldGenerated[h]+correction.x,0.0)-max(oldGenerated[h],0.0))*0.32;
            }
        }
    }
    float3 derivative=fftLimitDerivative(value.displacement);
    value.displacement=fftLimit(value.displacement);
    value.derivativeX*=derivative;
    value.derivativeY*=derivative;
    if(filtered) {
        float3 shadingLimit=fftLimitDerivative(shading.displacement);
        value.shadingDisplacement=fftLimit(shading.displacement);
        value.shadingDerivativeX=shading.derivativeX*shadingLimit;
        value.shadingDerivativeY=shading.derivativeY*shadingLimit;
        // Expected missing slope power, with local limiter attenuation. Reflected copies use
        // an independent-energy approximation; this does not claim a full covariance solver.
        value.unresolvedSlopeVariance=fftShadingVariance(shadingLod)*reflectionVariance*shadingLimit.z*shadingLimit.z;
    } else {
        value.shadingDisplacement=value.displacement;
        value.shadingDerivativeX=value.derivativeX;
        value.shadingDerivativeY=value.derivativeY;
    }
    value.vesselCrest=saturate(value.vesselCrest);
    return value;
}
FftSurface fftSurface(float2 p,float level,float footprint) {return fftSurface(p,level,footprint,float3(0,0,0));}
FftSurface fftSurface(float2 p,float level) {return fftSurface(p,level,0.0);}
FftSurface fftSurface(float2 p) {return fftSurface(p,WaterParameters.w);}
float2 fftParameterGradient(float value,float2 px,float2 py) {
    float det=px.x*py.y-px.y*py.x;
    float inverse=abs(det)>0.000001?1.0/det:0.0;
    return float2(ddx(value)*py.y-ddy(value)*px.y,ddy(value)*px.x-ddx(value)*py.x)*inverse;
}
)HLSL";
    if(clean) {
        const size_t begin=source.find("FftSurface fftRawSurface(float2 p,float3 lod)");
        const size_t end=source.find("FftSurface fftSurface(float2 p,float level,float footprint",begin);
        source.replace(begin,end-begin,HeadingSource(false,configuration.resolution,true)+WaterCleanShader::PixelSource(configuration)+R"HLSL(
FftSurface fftRawSurface(float2 p,float3 lod){return fftCleanSurface(p,fftMediumDepthField(p),lod);}
FftSurface fftRawSurface(float2 p){return fftRawSurface(p,float3(0,0,0));}
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading){fftCleanAndShadingSurface(p,fftMediumDepthField(p),lod,physical,shading);}
)HLSL");
        const std::string from="value.unresolvedSlopeVariance=fftShadingVariance(shadingLod)*";
        source.replace(source.find(from),from.size(),"value.unresolvedSlopeVariance=shading.unresolvedSlopeVariance*");
    } else if(waveHeading) {
        // Only rename the position entry points, never fftMapSamples / fftDecodeSamples.
        const size_t begin=source.find("FftSurface fftRawSurface(float2 p,float3 lod)");
        const size_t end=source.find("FftSurface fftSurface(float2 p,float level,float footprint",begin);
        std::string raw=source.substr(begin,end-begin);
        for(const auto &names:{std::pair<const char*,const char*>{"fftRawSurface","fftUnrotatedSurface"},
            {"fftRawAndShadingSurface","fftUnrotatedAndShadingSurface"}})
            for(size_t at=raw.find(names.first);at!=std::string::npos;at=raw.find(names.first,at+std::strlen(names.second)))
                raw.replace(at,std::strlen(names.first),names.second);
        source.replace(begin,end-begin,raw+HeadingSource(false,configuration.resolution)+HeadingPixelSource());
        const std::string variance="fftShadingVariance(shadingLod)*";
        source.replace(source.find(variance),variance.size(),
            "(fftShadingVariance(float3(0,shadingLod.yz))+shading.unresolvedSlopeVariance)*");
    }
    return source;
}
} // namespace WaterFftSampling
