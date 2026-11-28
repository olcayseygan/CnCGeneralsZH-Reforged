/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterCleanShore.h"
#include "Platform/WaterWaveHeading.h"
#include <limits>
#include <new>

// Immutable renderer-only map fields, built once per optical level and heading mode.
namespace WaterCleanPrecompute {
struct Pixel { float r,g,b,a; };
static_assert(sizeof(Pixel)==16,"RGBA32F transport");
struct Image {
    int width=0,height=0;
    std::vector<Pixel> pixels;
    uint64_t revision=0;
};
inline uint64_t NextRevision() {static uint64_t revision=0;return ++revision;}
inline bool BuildDepth(const WaterMediumDepth::Field &source,int side,Image &out) {
    if(source.width<2 || source.height<2 || side<source.width || side<source.height
        || side>WaterMediumDepth::MaxDimension || source.depth.size()!=size_t(source.width)*source.height)return false;
    Image next;next.width=next.height=side;next.pixels.resize(size_t(side)*side);
    auto value=[&](int x,int y) {
        return WaterMediumDepth::Encode(source.depth[size_t((std::min)(y,source.height-1))*source.width+(std::min)(x,source.width-1)])*(WaterMediumDepth::Cap/255.f);
    };
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        const float a=value(x,y),b=value((std::min)(x+1,side-1),y),c=value(x,(std::min)(y+1,side-1)),d=value((std::min)(x+1,side-1),(std::min)(y+1,side-1));
        next.pixels[size_t(y)*side+x]={a,b-a,c-a,d-b-c+a};
    }
    next.revision=NextRevision();out=std::move(next);return true;
}
// Heading interpolation uses the original packed gradient and excludes dry corners.
inline WaterWaveHeading::Vector SampleHeading(const std::vector<uint32_t> &words,int width,int height,int side,float gx,float gy) {
    if(width<2 || height<2 || side<width || side<height || words.size()!=size_t(width)*height*2)return {0,0};
    gx=(std::max)(0.f,(std::min)(float(side-1),gx));gy=(std::max)(0.f,(std::min)(float(side-1),gy));
    const int x=int(gx),y=int(gy);const float fx=gx-x,fy=gy-y;float sum=0,vx=0,vy=0;
    for(int j=0;j<2;++j)for(int k=0;k<2;++k) {
        const int xx=(std::min)(x+k,width-1),yy=(std::min)(y+j,height-1);
        const size_t n=(size_t(yy)*width+xx)*2;
        if((words[n+1]>>24)<128)continue;
        const float w=(k?fx:1-fx)*(j?fy:1-fy);const auto g=WaterWaveHeading::DecodeGradient(words[n]);
        vx+=g.x*w;vy+=g.y*w;sum+=w;
    }
    return sum>0?WaterWaveHeading::Vector{vx/sum,vy/sum}:WaterWaveHeading::Vector{0,0};
}
struct Rotation {
    Image image;
    int originX=0,originY=0;
};
inline bool BuildRotation(const WaterMediumDepth::Field &shore,int side,int border,
    float spacing,const WaterWaveHeading::Field &headingField,bool steering,Rotation &out) {
    if(side<2 || side>WaterMediumDepth::MaxDimension || border<0 || border>=side || shore.width<2 || shore.height<2
        || shore.width>side || shore.height>side || !std::isfinite(spacing) || spacing<=0
        || headingField.texels.size()!=size_t(shore.width)*shore.height*2)return false;
    WaterCleanShore::Settings settings;
    if(!steering)settings.follow=settings.turn=0;
    const float tile=settings.tile;
    const float lo=-float(border)*spacing,hi=float(side-1)*spacing-float(border)*spacing;
    float minX=(std::numeric_limits<float>::max)(),minY=minX,maxX=-minX,maxY=-minX;
    for(float y:{lo,hi})for(float x:{lo,hi}) {
        const float u=(x-y*.577350269f)/tile,v=y*1.154700538f/tile;
        minX=(std::min)(minX,u);minY=(std::min)(minY,v);maxX=(std::max)(maxX,u);maxY=(std::max)(maxY,v);
    }
    if(!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY)
        || minX<-1000000000.f || minY<-1000000000.f || maxX>1000000000.f || maxY>1000000000.f)return false;
    Rotation next;next.originX=int(std::floor(minX))-1;next.originY=int(std::floor(minY))-1;
    next.image.width=int(std::floor(maxX))+3-next.originX;next.image.height=int(std::floor(maxY))+3-next.originY;
    if(next.image.width<2 || next.image.height<2 || next.image.width>2048 || next.image.height>2048
        || size_t(next.image.width)*next.image.height>WaterMediumDepth::MaxCells)return false;
    next.image.pixels.resize(size_t(next.image.width)*next.image.height);
    const float baseX=WaterWaveHeading::BaseHeading.x,baseY=WaterWaveHeading::BaseHeading.y;
    for(int y=0;y<next.image.height;++y)for(int x=0;x<next.image.width;++x) {
        const float i=float(x+next.originX),j=float(y+next.originY);
        const float px=(i+.5f*j)*tile,py=.866025404f*j*tile;
        const float gx=(px+float(border)*spacing)/spacing,gy=(py+float(border)*spacing)/spacing;
        const auto g=SampleHeading(headingField.texels,shore.width,shore.height,side,gx,gy);
        const auto s=WaterCleanShore::SampleField(shore,gx,gy,baseX,baseY);
        const float magnitude=std::hypot(g.x,g.y);
        const float fieldX=magnitude>1e-5f?g.x/magnitude:baseX,fieldY=magnitude>1e-5f?g.y/magnitude:baseY;
        const float angle=WaterCleanShore::HeadingAngle(baseX,baseY,fieldX,fieldY,settings.follow)
            +WaterCleanShore::VertexAngle(fieldX,fieldY,s,settings);
        next.image.pixels[size_t(y)*next.image.width+x]={std::cos(angle),std::sin(angle),angle,1};
    }
    next.image.revision=NextRevision();out=std::move(next);return true;
}
// Immutable level-specific transport. The square depth region is followed by the
// triangular lattice's rotations. Point reads recover the depth polynomial;
// shader evaluates the same bilinear polynomial and its analytic derivatives.
struct Atlas {
    Image image;
    float info[4]={}; // depth side, rotation width/height, valid
    float origin[4]={}; // lattice origin XY, inverse atlas width/height
};
inline bool BuildAtlas(const WaterMediumDepth::Field &depth,const WaterWaveHeading::Field &heading,
    int side,int border,float spacing,bool steering,Atlas &out) {
    const size_t count=size_t(depth.width)*depth.height;
    if(depth.width<2 || depth.height<2 || depth.depth.size()!=count
        || depth.shoreDistance.size()!=count || depth.shoreX.size()!=count
        || depth.shoreY.size()!=count || depth.shoreConfidence.size()!=count
        || heading.texels.size()!=count*2)return false;
    bool wet=false;
    for(size_t i=1;i<heading.texels.size();i+=2)wet=wet || (heading.texels[i]>>24)>=128;
    if(!wet)return false;
    try {
    Image coefficients;Rotation rotation;
    if(!BuildDepth(depth,side,coefficients) || !BuildRotation(depth,side,border,spacing,heading,steering,rotation))return false;
    Atlas next;
    next.image.width=(std::max)(side,rotation.image.width);
    next.image.height=side+rotation.image.height;
    if(next.image.width>4096 || next.image.height>4096)return false;
    next.image.pixels.resize(size_t(next.image.width)*next.image.height);
    for(int y=0;y<side;++y)std::copy_n(coefficients.pixels.data()+size_t(y)*side,side,next.image.pixels.data()+size_t(y)*next.image.width);
    for(int y=0;y<rotation.image.height;++y)std::copy_n(rotation.image.pixels.data()+size_t(y)*rotation.image.width,rotation.image.width,next.image.pixels.data()+size_t(y+side)*next.image.width);
    next.info[0]=float(side);next.info[1]=float(rotation.image.width);next.info[2]=float(rotation.image.height);next.info[3]=1;
    next.origin[0]=float(rotation.originX);next.origin[1]=float(rotation.originY);
    next.origin[2]=1.f/next.image.width;next.origin[3]=1.f/next.image.height;
    next.image.revision=NextRevision();out=std::move(next);return true;
    } catch(const std::bad_alloc &) {return false;}
}
}
