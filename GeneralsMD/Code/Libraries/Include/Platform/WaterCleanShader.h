/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterCleanStream.h"
#include "Platform/WaterFftSpectrum.h"
#include <cstdio>
namespace WaterCleanShader {
inline std::string CommonSource(bool vertex) {
    return std::string(vertex?
        "Texture2D<float4> FftCleanAtlas : register(t4);\nSamplerState FftCleanAtlasSampler : register(s4);\n":
        "Texture2D<float4> FftCleanAtlas : register(t15);\nSamplerState FftCleanAtlasSampler : register(s15);\n")+R"HLSL(
float4 fftMediumDepthField(float2 p) {
    if(CleanAtlasInfo.w<.5 || CleanAtlasInfo.x<2 || TerrainParameters.w<.5)return float4(40,0,0,0);
    float side=CleanAtlasInfo.x,scale=side/max(TerrainParameters.y,1.0);
    float2 raw=(p+TerrainParameters.xx)*scale,cell=clamp(raw,0.0,side-1);
    float2 lo=min(floor(cell),side-2),f=cell-lo;
    // Coefficients of the exact clamped bilinear cell, prepared once at map load.
    float4 c=FftCleanAtlas.SampleLevel(FftCleanAtlasSampler,(lo+.5)*CleanAtlasOrigin.zw,0);
    float2 inside=step(0.0,raw)*step(raw,side-1);
    return float4(c.x+c.y*f.x+c.z*f.y+c.w*f.x*f.y,
        (c.y+c.w*f.y)*scale*inside.x,(c.z+c.w*f.x)*scale*inside.y,1);
}
float3 fftMediumGain(float4 depth,float reach) {
    float t=saturate(depth.x/reach);
    return float3(t*t*(3-2*t),depth.yz*(6*t*(1-t)/reach));
}
void fftMediumScale(inout float3 d,inout float3 dx,inout float3 dy,float3 gain) {
    dx=dx*gain.x+d*gain.y;dy=dy*gain.x+d*gain.z;d*=gain.x;
}
)HLSL"+WaterCleanStream::Source();
}
inline std::string VertexSource() {return CommonSource(true)+R"HLSL(
float3 fftCleanVertexSample(float2 q,int band) {
    if(band==0)return FftDisplacement0.SampleLevel(FftSampler0,q/(450.0*fftWorldScale),0).xzy;
    if(band==1)return FftDisplacement1.SampleLevel(FftSampler1,q/(103.0*fftWorldScale),0).xzy;
    return FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xzy;
}
float3 fftRawDisplacement(float2 p) {
    float4 depth=fftMediumDepthField(p);FftShoreTiles tiles=fftShoreTiles(p);
    float3 a=0,b=0,c=0;
    if(!tiles.active) {
        a=fftCleanVertexSample(p,0);b=fftCleanVertexSample(p,1);c=fftCleanVertexSample(p,2);
    } else {
        [unroll]for(int i=0;i<(tiles.collapsed?1:3);++i) {
            FftShorePoint v=fftShorePoint(p,tiles,i);
            float3 da=fftCleanVertexSample(v.q,0),db=fftCleanVertexSample(v.q,1),dc=fftCleanVertexSample(v.q,2);
            da.xy=fftRotate(da.xy,v.rotation);db.xy=fftRotate(db.xy,v.rotation);dc.xy=fftRotate(dc.xy,v.rotation);
            a+=v.weight.x*da;b+=v.weight.x*db;c+=v.weight.x*dc;
        }
    }
    return (a*float3(-1.83,-1.83,1)*fftMediumGain(depth,24).x
        +b*float3(-1.92,-1.92,1)*fftMediumGain(depth,12).x
        +c*float3(-1.92,-1.92,1)*fftMediumGain(depth,4).x)*fftWorldScale;
}
)HLSL";}
inline std::string PixelSource(WaterFftSpectrum::Configuration configuration) {
    const auto rms=WaterFftSpectrum::SpectrumHeightRms(WaterFftSpectrum::InitialSpectrum(configuration,true));
    char constants[256];std::snprintf(constants,sizeof(constants),
        "static const float3 fftCleanRms=float3(%.9g,%.9g,%.9g);\n",rms[0],rms[1],rms[2]);
    return CommonSource(false)+constants+R"HLSL(
void fftCleanFinish(inout FftSurface value,float4 depth,float3 lod) {
    value.mediumDepth=depth;
    float3 limit=fftLimitDerivative(value.displacement);
    float2 x=value.derivativeX.xy*limit.xy,y=value.derivativeY.xy*limit.xy;
    value.incidentCompression=(1+x.x)*(1+y.y)-x.y*y.x;
    value.contactIncident=float3(fftLimit(value.displacement).z,float2(value.derivativeX.z,value.derivativeY.z)*limit.z);
    float3 gain=float3(fftMediumGain(depth,24).x,fftMediumGain(depth,12).x,fftMediumGain(depth,4).x);
    value.unresolvedSlopeVariance=fftShadingVariance(float3(lod.x,0,0))*gain.x*gain.x
        +fftShadingVariance(float3(0,lod.y,0))*gain.y*gain.y+fftShadingVariance(float3(0,0,lod.z))*gain.z*gain.z;
}
void fftCleanMaps(float2 q,int band,float lod,out float4 a,out float4 derivatives) {
    if(band==0){a=Texture8.SampleLevel(Sampler8,q/(450.0*fftWorldScale),lod);derivatives=Texture9.SampleLevel(Sampler9,q/(450.0*fftWorldScale),lod);}
    else if(band==1){a=Texture10.SampleLevel(Sampler10,q/(103.0*fftWorldScale),lod);derivatives=Texture11.SampleLevel(Sampler11,q/(103.0*fftWorldScale),lod);}
    else {a=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod);derivatives=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod);}
}
void fftCleanAdd(inout FftSurface value,FftShorePoint v,float4 a,float4 derivatives,float horizontal,float3 gain) {
    float3 d=a.xzy*float3(horizontal,horizontal,1)*fftWorldScale;
    float3 x=float3(derivatives.z,a.w,derivatives.x)*float3(horizontal,horizontal,1);
    float3 y=float3(a.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);
    float3 dx=x*v.qx.x+y*v.qx.y,dy=x*v.qy.x+y*v.qy.y;
    float2 turn=float2(-d.y,d.x);dx.xy+=turn*v.angleGradient.x;dy.xy+=turn*v.angleGradient.y;
    d.xy=fftRotate(d.xy,v.rotation);dx.xy=fftRotate(dx.xy,v.rotation);dy.xy=fftRotate(dy.xy,v.rotation);
    dx=v.weight.x*dx+v.weight.y*d;dy=v.weight.x*dy+v.weight.z*d;d*=v.weight.x;
    fftMediumScale(d,dx,dy,gain);
    value.displacement+=d;value.derivativeX+=dx;value.derivativeY+=dy;
}
void fftCleanWalk(inout FftSurface physical,inout FftSurface shading,float2 p,float4 depth,float3 lod,FftShoreTiles tiles,bool filtered) {
    int count=tiles.active?(tiles.collapsed?1:3):1;
    [unroll]for(int i=0;i<count;++i) {
        FftShorePoint v=(FftShorePoint)0;
        if(tiles.active)v=fftShorePoint(p,tiles,i);
        else {v.q=p;v.rotation=float2(1,0);v.qx=float2(1,0);v.qy=float2(0,1);v.weight=float3(1,0,0);}
        [unroll]for(int band=0;band<3;++band) {
            float horizontal=band==0?-1.83:-1.92;
            float3 gain=fftMediumGain(depth,band==0?24.0:band==1?12.0:4.0);
            float4 a,derivatives;
            if(!filtered) {
                fftCleanMaps(v.q,band,0,a,derivatives);
                FftSurface contribution=(FftSurface)0;fftCleanAdd(contribution,v,a,derivatives,horizontal,gain);
                physical.displacement+=contribution.displacement;physical.derivativeX+=contribution.derivativeX;physical.derivativeY+=contribution.derivativeY;
                if(lod[band]<=0) {shading.displacement+=contribution.displacement;shading.derivativeX+=contribution.derivativeX;shading.derivativeY+=contribution.derivativeY;}
            } else if(lod[band]>0) {
                fftCleanMaps(v.q,band,lod[band],a,derivatives);fftCleanAdd(shading,v,a,derivatives,horizontal,gain);
            }
        }
    }
}
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    physical=(FftSurface)0;shading=(FftSurface)0;
    float4 depth=fftMediumDepthField(p);FftShoreTiles tiles=fftShoreTiles(p);
    // Separate lighting walk keeps point and band intermediates out of live
    // registers across the physical samples. Resolved bands share mip zero.
    fftCleanWalk(physical,shading,p,depth,lod,tiles,false);
    if(any(lod>0))fftCleanWalk(physical,shading,p,depth,lod,tiles,true);
    fftCleanFinish(physical,depth,0);fftCleanFinish(shading,depth,lod);
}
FftSurface fftRawSurface(float2 p,float3 lod) {
    FftSurface physical,shading;fftRawAndShadingSurface(p,lod,physical,shading);return shading;
}
FftSurface fftRawSurface(float2 p){return fftRawSurface(p,float3(0,0,0));}
)HLSL";
}
}
