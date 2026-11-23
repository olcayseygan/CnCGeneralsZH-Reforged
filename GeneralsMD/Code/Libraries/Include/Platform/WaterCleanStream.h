/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <string>
#include <cstdio>
#include "Platform/WaterCleanCollapse.h"

// Fixed clean-ocean triangle basis. Each point's coordinate and chain rule serve
// every cascade and both physical/lighting LODs, rather than rebuilding per band.
namespace WaterCleanStream {
inline std::string Source() {
    char constants[96];std::snprintf(constants,sizeof(constants),
        "static const float fftTileCollapseAngle=%.9g;\n",double(WaterCleanCollapse::AngleRadians));
    return std::string(constants)+R"HLSL(
static const float fftCleanTileSize=300.0;
float2 fftRotate(float2 p,float2 r) {return float2(r.x*p.x-r.y*p.y,r.y*p.x+r.x*p.y);}
struct FftShoreTiles {
    float2 cell;bool upper,active,collapsed;
    float3 barycentric,normalization;
    float2 meanRotation,meanVertex,vertexX,vertexY,angleGradient;
    float3 scale;
};
float2 fftTileIndex(FftShoreTiles t,int i) {
    float2 offset=i==0?float2(0,0):i==1?float2(1,0):float2(0,1);
    return t.cell+(t.upper?1-offset:offset);
}
float2 fftTileVertex(FftShoreTiles t,int i) {
    float2 index=fftTileIndex(t,i);
    return float2(index.x+.5*index.y,.866025404*index.y)*fftCleanTileSize;
}
float4 fftTileRotation(float2 index) {
    int2 at=int2(index)-int2(CleanAtlasOrigin.xy);
    if(CleanAtlasInfo.w<.5 || any(at<0) || any(at>=int2(CleanAtlasInfo.yz)))return float4(1,0,0,0);
    float2 uv=(float2(at)+float2(.5,CleanAtlasInfo.x+.5))*CleanAtlasOrigin.zw;
    return FftCleanAtlas.SampleLevel(FftCleanAtlasSampler,uv,0);
}
float3 fftTileWeight(FftShoreTiles t,int i) {
    float b=t.barycentric[i],weight=b*b*b*t.normalization.x;
    float2 gradient=i==0?float2(-1,-.577350269):i==1?float2(1,-.577350269):float2(0,1.154700538);
    gradient*=t.upper?-1:1;gradient/=fftCleanTileSize;
    return float3(weight,(3*b*b*gradient-weight*t.normalization.yz)*t.normalization.x);
}
FftShoreTiles fftShoreTiles(float2 p) {
    FftShoreTiles t=(FftShoreTiles)0;
    if(CleanAtlasInfo.w<.5)return t;
    float2 uv=float2(p.x-p.y*.577350269,p.y*1.154700538)/fftCleanTileSize;
    t.cell=floor(uv);float2 f=frac(uv);t.upper=f.x+f.y>1;
    float3 b=t.upper?float3(f.x+f.y-1,1-f.x,1-f.y):float3(1-f.x-f.y,f.x,f.y);
    float3 bx=float3(-1,1,0)/fftCleanTileSize;
    float3 by=float3(-.577350269,-.577350269,1.154700538)/fftCleanTileSize;
    if(t.upper){bx=-bx;by=-by;}
    b=saturate(b);float3 a=b*b*b,ax=3*b*b*bx,ay=3*b*b*by;
    float inverseNorm=rsqrt(max(dot(a,a),1e-20));float3 weight=a*inverseNorm;
    t.barycentric=b;t.normalization=float3(inverseNorm,dot(weight,ax),dot(weight,ay));
    float3 angles=float3(fftTileRotation(fftTileIndex(t,0)).z,
        fftTileRotation(fftTileIndex(t,1)).z,fftTileRotation(fftTileIndex(t,2)).z);
    t.active=any(abs(angles)>1e-6);
    // Compare each absolute wrapped angle to zero. Equal large turns have
    // separate pivots and cannot collapse (the reference stern artifact).
    float3 turn=angles-6.283185307*floor((angles+3.141592654)/6.283185307);
    t.collapsed=max(abs(turn.x),max(abs(turn.y),abs(turn.z)))<fftTileCollapseAngle;
    if(t.active && t.collapsed) {
        float sum=dot(weight,float3(1,1,1));
        float first=angles.x;angles-=first;
        angles.yz-=6.283185307*floor((angles.yz+3.141592654)/6.283185307);
        float offset=dot(weight,angles)/sum;
        float3 dx=(ax-weight*t.normalization.y)*inverseNorm,dy=(ay-weight*t.normalization.z)*inverseNorm;
        float sx=dot(dx,float3(1,1,1)),sy=dot(dy,float3(1,1,1));
        t.angleGradient=float2(dot(dx,angles)-offset*sx,dot(dy,angles)-offset*sy)/sum;
        float2 v0=fftTileVertex(t,0),v1=fftTileVertex(t,1),v2=fftTileVertex(t,2);
        t.meanVertex=(weight.x*v0+weight.y*v1+weight.z*v2)/sum;
        t.vertexX=(dx.x*v0+dx.y*v1+dx.z*v2-t.meanVertex*sx)/sum;
        t.vertexY=(dy.x*v0+dy.y*v1+dy.z*v2-t.meanVertex*sy)/sum;
        sincos(first+offset,t.meanRotation.y,t.meanRotation.x);t.scale=float3(sum,sx,sy);
    }
    return t;
}
struct FftShorePoint {float2 q,rotation,qx,qy,angleGradient;float3 weight;};
FftShorePoint fftShorePoint(float2 p,FftShoreTiles t,int i) {
    FftShorePoint v=(FftShorePoint)0;
    if(t.collapsed) {
        v.rotation=t.meanRotation;float2 inverseRotation=v.rotation*float2(1,-1);
        float2 relative=fftRotate(p-t.meanVertex,inverseRotation);
        v.q=t.meanVertex+relative;float2 perpendicular=float2(-relative.y,relative.x);
        v.qx=t.vertexX+fftRotate(float2(1,0)-t.vertexX,inverseRotation)-perpendicular*t.angleGradient.x;
        v.qy=t.vertexY+fftRotate(float2(0,1)-t.vertexY,inverseRotation)-perpendicular*t.angleGradient.y;
        v.angleGradient=t.angleGradient;v.weight=t.scale;
    } else {
        v.rotation=fftTileRotation(fftTileIndex(t,i)).xy;
        float2 inverseRotation=v.rotation*float2(1,-1),vertex=fftTileVertex(t,i);
        v.q=vertex+fftRotate(p-vertex,inverseRotation);
        v.qx=fftRotate(float2(1,0),inverseRotation);v.qy=fftRotate(float2(0,1),inverseRotation);
        v.weight=fftTileWeight(t,i);
    }
    return v;
}
)HLSL";}
}
