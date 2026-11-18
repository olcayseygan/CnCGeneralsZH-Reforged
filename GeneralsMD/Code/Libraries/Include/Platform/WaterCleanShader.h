/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterCleanShore.h"
#include "Platform/WaterFftSpectrum.h"
#include <cstdio>
namespace WaterCleanShader {
inline std::string DepthSource(){return R"HLSL(
float4 fftMediumDepthField(float2 p) {
    if(TerrainParameters.w<0.5 || FftParameters.z<=0.5)return float4(40,0,0,0);
    uint width,height;FftHeadingTexture.GetDimensions(width,height);
    const int stride=4;
    int2 size=int2(width/stride,height);if(any(size<2))return float4(40,0,0,0);
    float scale=(width/stride)/max(TerrainParameters.y,1.0);
    float2 raw=(p+TerrainParameters.xx)*scale,cell=clamp(raw,0.0,float2(size-1));
    int2 lo=min(int2(floor(cell)),size-2);float2 f=cell-float2(lo);
    float4 depths=0;float valid=1;
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 at=lo+int2(x,y);
        float2 uv=(float2(at.x*stride+2,at.y)+0.5)/float2(width,height);
        float4 packed=FftHeadingTexture.SampleLevel(FftHeadingSampler,uv,0);
        depths[y*2+x]=packed.g*40.0;valid=min(valid,packed.a);
    }
    float2 inside=step(0.0,raw)*step(raw,float2(size-1));
    return float4(lerp(lerp(depths.x,depths.y,f.x),lerp(depths.z,depths.w,f.x),f.y),
        lerp(depths.y-depths.x,depths.w-depths.z,f.y)*scale*inside.x,
        lerp(depths.z-depths.x,depths.w-depths.y,f.x)*scale*inside.y,valid);
}
float3 fftMediumGain(float4 depth,float reach) {
    float t=saturate(depth.x/reach);
    return float3(t*t*(3-2*t),depth.yz*(6*t*(1-t)/reach));
}
void fftMediumScale(inout float3 d,inout float3 dx,inout float3 dy,float3 gain) {
 dx=dx*gain.x+d*gain.y;dy=dy*gain.x+d*gain.z;d*=gain.x;
}
)HLSL";}
inline std::string VertexSource(){return DepthSource()+R"HLSL(
// D changes amplitude, never coordinates or dispersion.
float3 fftCleanBaseDisplacement(float2 p,float4 depth) {
    float3 a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xyz;
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,p/(103.0*fftWorldScale),0).xyz;
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,p/(13.0*fftWorldScale),0).xyz;
    return (a.xzy*float3(-1.83,-1.83,1)*fftMediumGain(depth,24.0).x
        +b.xzy*float3(-1.92,-1.92,1)*fftMediumGain(depth,12.0).x
        +c.xzy*float3(-1.92,-1.92,1)*fftMediumGain(depth,4.0).x)*fftWorldScale;
}
)HLSL"+WaterCleanShore::Source()+R"HLSL(
float3 fftShoreVertexBand(float2 p,FftShoreTiles tiles,int band) {
    float3 result=0;
    [unroll]for(int i=0;i<3;++i) {
        float2 rotation=tiles.rotation[i],inverseRotation=rotation*float2(1,-1);float2 q=tiles.vertex[i]+fftRotate(p-tiles.vertex[i],inverseRotation);
        float3 a=0;
        if(band==0)a=FftDisplacement0.SampleLevel(FftSampler0,q/(450.0*fftWorldScale),0).xyz;
        else if(band==1)a=FftDisplacement1.SampleLevel(FftSampler1,q/(103.0*fftWorldScale),0).xyz;
        else a=FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xyz;
        float3 d=a.xzy;d.xy=fftRotate(d.xy,rotation);result+=tiles.weight[i]*d;
    }
    return result;
}
float3 fftCleanPreparedDisplacement(float2 p,float4 depth,FftShoreTiles tiles) {
    // Exact identity prevents coherent near-zero tiles from changing amplitude/read count.
    if(!tiles.active)return fftCleanBaseDisplacement(p,depth);
    float3 a=0;
    if(fftShoreLong)a=fftShoreVertexBand(p,tiles,0);
    else a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xzy;
    float3 b=fftShoreVertexBand(p,tiles,1);
    float3 c=0;
    if(fftShoreFine)c=fftShoreVertexBand(p,tiles,2);
    else c=FftDisplacement2.SampleLevel(FftSampler2,p/(13.0*fftWorldScale),0).xzy;
    return (a*float3(-1.83,-1.83,1)*fftMediumGain(depth,24.0).x+b*float3(-1.92,-1.92,1)*fftMediumGain(depth,12.0).x+c*float3(-1.92,-1.92,1)*fftMediumGain(depth,4.0).x)*fftWorldScale;
}
float3 fftCleanDisplacement(float2 p,float4 depth) {return fftCleanPreparedDisplacement(p,depth,fftShoreTiles(p));}
)HLSL";}
inline std::string PixelSource(WaterFftSpectrum::Configuration configuration){
 const auto rms=WaterFftSpectrum::SpectrumHeightRms(WaterFftSpectrum::InitialSpectrum(configuration,true));char constants[256];
 std::snprintf(constants,sizeof(constants),"static const float3 fftCleanRms=float3(%.9g,%.9g,%.9g);\n",rms[0],rms[1],rms[2]);
 return DepthSource()+std::string(constants)+R"HLSL(
// retain the D-gradient product rule for all displacement derivatives.
void fftCleanBand(inout FftSurface value,float4 map,float4 derivatives,float horizontal,float3 gain) {
    float3 d=map.xzy*float3(horizontal,horizontal,1)*fftWorldScale;
    float3 x=float3(derivatives.z,map.w,derivatives.x)*float3(horizontal,horizontal,1);
    float3 y=float3(map.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);
    fftMediumScale(d,x,y,gain);
    value.displacement+=d;value.derivativeX+=x;value.derivativeY+=y;
}
FftSurface fftCleanDecode(FftMapSamples samples,float4 depth,float3 lod) {
    FftSurface value=(FftSurface)0;
    float3 longGain=fftMediumGain(depth,24.0),mediumGain=fftMediumGain(depth,12.0),fineGain=fftMediumGain(depth,4.0);
    fftCleanBand(value,samples.a,samples.ad,-1.83,longGain);
    fftCleanBand(value,samples.b,samples.bd,-1.92,mediumGain);
    fftCleanBand(value,samples.c,samples.cd,-1.92,fineGain);
    float3 limit=fftLimitDerivative(value.displacement);
    float2 x=value.derivativeX.xy*limit.xy,y=value.derivativeY.xy*limit.xy;
    // True horizontal-displacement determinant. With spatial amplitude gradients the
    // mixed derivatives need not be equal, so use their product rather than a square.
    value.incidentCompression=(1+x.x)*(1+y.y)-x.y*y.x;
    value.contactIncident=float3(fftLimit(value.displacement).z,
        float2(value.derivativeX.z,value.derivativeY.z)*limit.z);

    // CPU spectrum integration gives total-minus-mip-resolved slope power per band.
    // Amplitude gains multiply variance quadratically. Existing outer limiter/hull
    // factors are applied once by fftSurface and the material.
    value.unresolvedSlopeVariance=fftShadingVariance(float3(lod.x,0,0))*longGain.x*longGain.x
        +fftShadingVariance(float3(0,lod.y,0))*mediumGain.x*mediumGain.x
        +fftShadingVariance(float3(0,0,lod.z))*fineGain.x*fineGain.x;
    return value;
}
FftSurface fftCleanBaseSurface(float2 p,float4 depth,float3 lod) {
    return fftCleanDecode(fftMapSamples(p,lod),depth,lod);
}
void fftCleanBaseAndShadingSurface(float2 p,float4 depth,float3 lod,out FftSurface physical,out FftSurface shading) {
    FftMapSamples samples=fftMapSamples(p,float3(0,0,0));
    physical=fftCleanDecode(samples,depth,float3(0,0,0));shading=physical;
    if(any(lod>0.0)) {
        if(lod.x>0) {
            samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod.x);
            samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod.x);
        }
        if(lod.y>0) {
            samples.b=Texture10.SampleLevel(Sampler10,p/(103.0*fftWorldScale),lod.y);
            samples.bd=Texture11.SampleLevel(Sampler11,p/(103.0*fftWorldScale),lod.y);
        }
        if(lod.z>0) {
            samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod.z);
            samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod.z);
        }
        shading=fftCleanDecode(samples,depth,lod);
    }
}
)HLSL"+WaterCleanShore::Source()+R"HLSL(
void fftShorePixelBand(inout FftSurface value,float2 p,FftShoreTiles tiles,int band,float lod,float horizontal,float3 gain) {
    float3 displacement=0,dx=0,dy=0;
    [unroll]for(int i=0;i<3;++i) {
        float2 rotation=tiles.rotation[i],inverseRotation=rotation*float2(1,-1);float2 q=tiles.vertex[i]+fftRotate(p-tiles.vertex[i],inverseRotation);
        float4 a,derivatives;
        if(band==0) {a=Texture8.SampleLevel(Sampler8,q/(450.0*fftWorldScale),lod);derivatives=Texture9.SampleLevel(Sampler9,q/(450.0*fftWorldScale),lod);}
        else if(band==1) {a=Texture10.SampleLevel(Sampler10,q/(103.0*fftWorldScale),lod);derivatives=Texture11.SampleLevel(Sampler11,q/(103.0*fftWorldScale),lod);}
        else {a=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod);derivatives=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod);}
        float3 d=a.xzy*float3(horizontal,horizontal,1)*fftWorldScale;
        float3 x=float3(derivatives.z,a.w,derivatives.x)*float3(horizontal,horizontal,1);
        float3 y=float3(a.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);
        // q=vertex+R(-angle)(p-vertex), output horizontal vector=R(angle)d.
        float2 qx=fftRotate(float2(1,0),inverseRotation),qy=fftRotate(float2(0,1),inverseRotation);
        float3 xx=x*qx.x+y*qx.y,yy=x*qy.x+y*qy.y;
        d.xy=fftRotate(d.xy,rotation);xx.xy=fftRotate(xx.xy,rotation);yy.xy=fftRotate(yy.xy,rotation);
        displacement+=tiles.weight[i]*d;
        dx+=tiles.weight[i]*xx+tiles.dx[i]*d;dy+=tiles.weight[i]*yy+tiles.dy[i]*d;
    }
    fftMediumScale(displacement,dx,dy,gain);
    value.displacement+=displacement;value.derivativeX+=dx;value.derivativeY+=dy;
}
FftSurface fftShoreSurface(float2 p,float4 depth,float3 lod,FftShoreTiles tiles) {
    FftMapSamples samples=(FftMapSamples)0;
    if(!fftShoreLong) {samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod.x);samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod.x);}
    if(!fftShoreFine) {samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod.z);samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod.z);}
    FftSurface value=fftCleanDecode(samples,depth,lod);
    if(fftShoreLong)fftShorePixelBand(value,p,tiles,0,lod.x,-1.83,fftMediumGain(depth,24.0));
    fftShorePixelBand(value,p,tiles,1,lod.y,-1.92,fftMediumGain(depth,12.0));
    if(fftShoreFine)fftShorePixelBand(value,p,tiles,2,lod.z,-1.92,fftMediumGain(depth,4.0));
    float3 limit=fftLimitDerivative(value.displacement);
    float2 x=value.derivativeX.xy*limit.xy,y=value.derivativeY.xy*limit.xy;
    value.incidentCompression=(1+x.x)*(1+y.y)-x.y*y.x;
    value.contactIncident=float3(fftLimit(value.displacement).z,float2(value.derivativeX.z,value.derivativeY.z)*limit.z);
    return value;
}
FftSurface fftCleanSurface(float2 p,float4 depth,float3 lod) {
    FftShoreTiles tiles=fftShoreTiles(p);if(!tiles.active)return fftCleanBaseSurface(p,depth,lod);
    return fftShoreSurface(p,depth,lod,tiles);
}
void fftShoreFinish(inout FftSurface value) {
    float3 limit=fftLimitDerivative(value.displacement);
    float2 x=value.derivativeX.xy*limit.xy,y=value.derivativeY.xy*limit.xy;
    value.incidentCompression=(1+x.x)*(1+y.y)-x.y*y.x;
    value.contactIncident=float3(fftLimit(value.displacement).z,float2(value.derivativeX.z,value.derivativeY.z)*limit.z);
}
void fftShorePairedBand(inout FftSurface physical,inout FftSurface shading,float2 p,FftShoreTiles tiles,int band,float lod,float horizontal,float3 gain) {
    FftSurface physicalBand=(FftSurface)0;
    fftShorePixelBand(physicalBand,p,tiles,band,0,horizontal,gain);
    FftSurface shadingBand=physicalBand;
    if(lod>0) {
        shadingBand=(FftSurface)0;
        fftShorePixelBand(shadingBand,p,tiles,band,lod,horizontal,gain);
    }
    physical.displacement+=physicalBand.displacement;physical.derivativeX+=physicalBand.derivativeX;physical.derivativeY+=physicalBand.derivativeY;
    shading.displacement+=shadingBand.displacement;shading.derivativeX+=shadingBand.derivativeX;shading.derivativeY+=shadingBand.derivativeY;
}
void fftCleanPreparedAndShadingSurface(float2 p,float4 depth,float3 lod,FftShoreTiles tiles,out FftSurface physical,out FftSurface shading) {
    if(!tiles.active) {fftCleanBaseAndShadingSurface(p,depth,lod,physical,shading);return;}
    FftMapSamples samples=(FftMapSamples)0;
    if(!fftShoreLong) {samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),0);samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),0);}
    if(!fftShoreFine) {samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),0);samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),0);}
    physical=fftCleanDecode(samples,depth,0);shading=physical;
    if(any(lod>0)) {
        if(!fftShoreLong && lod.x>0) {samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod.x);samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod.x);}
        if(!fftShoreFine && lod.z>0) {samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod.z);samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod.z);}
        shading=fftCleanDecode(samples,depth,lod);
    }
    // Keep each band's mip-zero physical contribution; only its unresolved shading pair is read again.
    if(fftShoreLong)fftShorePairedBand(physical,shading,p,tiles,0,lod.x,-1.83,fftMediumGain(depth,24.0));
    fftShorePairedBand(physical,shading,p,tiles,1,lod.y,-1.92,fftMediumGain(depth,12.0));
    if(fftShoreFine)fftShorePairedBand(physical,shading,p,tiles,2,lod.z,-1.92,fftMediumGain(depth,4.0));
    fftShoreFinish(physical);fftShoreFinish(shading);
}
void fftCleanAndShadingSurface(float2 p,float4 depth,float3 lod,out FftSurface physical,out FftSurface shading) {
    fftCleanPreparedAndShadingSurface(p,depth,lod,fftShoreTiles(p),physical,shading);
}

)HLSL";
}
}
