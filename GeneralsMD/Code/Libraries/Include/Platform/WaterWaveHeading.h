/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>
#include "Platform/WaterFftSpectrum.h"

namespace WaterWaveHeading {
bool Supported(); // live native Modern-water renderer
constexpr unsigned PixelTextureSlot=15,VertexTextureSlot=4;

constexpr int MaxDimension=2048;
constexpr size_t MaxCells=1024u*1024u;
constexpr float DistanceCap=480.0f, PrevailingShoreRange=60.0f, PrevailingThreshold=0.15f;
// Map water is a thin cosmetic sheet (a few units deep), so its terrain depth cannot steer waves.
// Depth is modelled instead as rising ShoreSlope per unit of distance from shore, which is fully
// deep at DistanceCap; speeds are normalised to 1 there, so open sea stays an exact plane wave.
constexpr float ShoreSlope=0.4f, MinSpeed=0.2f;
constexpr float GradientRange=8.0f, MinAmplitude=0.35f, ShelterWavelengths=2.0f;
constexpr int BlurPasses=2;
// Linux-only emulated register: rotation, packed-U step, reserved, reserved.
constexpr unsigned PrevailingRegister=191;
struct Vector { float x,y; };
// The inverse FFT's +k.x,+omega.t travels against the spectrum wind.
constexpr Vector BaseHeading={-0.7071067811865475f,-0.7071067811865475f};
struct Field {
    Vector prevailing=BaseHeading;
    float uStep=1.0f;
    std::vector<uint32_t> texels; // Interleaved A: U RG, gradient BA; B: amplitude R, validity A.
};
inline float Rotation(Vector heading) {
    return std::atan2(BaseHeading.x*heading.y-BaseHeading.y*heading.x,
                     BaseHeading.x*heading.x+BaseHeading.y*heading.y);
}
inline double ReferenceWavelength() {
    static const double wavelength=[] {
        const auto spectrum=WaterFftSpectrum::InitialSpectrum();
        const unsigned n=WaterFftSpectrum::Resolution;
        double energy=0,weighted=0;
        for(unsigned y=1;y<n;++y) for(unsigned x=1;x<n;++x) {
            const double k=std::hypot(double(int(x)-int(n/2)),double(int(y)-int(n/2)));
            if(k==0)continue;
            double power=0;for(unsigned j=0;j<4;++j)power+=double(spectrum[(y*n+x)*4+j])*spectrum[(y*n+x)*4+j];
            energy+=power;weighted+=power*WaterFftSpectrum::Sizes[0]*WaterFftSpectrum::WorldScale/k;
        }
        return weighted/energy;
    }();
    return wavelength;
}
inline uint32_t EncodeGradient(float value) {
    return uint32_t(std::lround((std::max)(0.0f,(std::min)(255.0f,128.0f+127.0f*value/GradientRange))));
}
inline Vector DecodeGradient(uint32_t a) {
    return {(float(a&255)-128)*GradientRange/127,(float(a>>24)-128)*GradientRange/127};
}
inline float DecodeU(uint32_t a,float step) {return float(((a>>16)&255)*256+((a>>8)&255))*step;}
inline bool Build(const uint8_t *wet,const uint8_t *exposure,int width,int height,int border,float spacing,
                  Field &result,const uint8_t *links=nullptr) {
    result=Field{};
    if(!wet || !exposure || width<2 || height<2 || width>MaxDimension || height>MaxDimension
        || size_t(width)*height>MaxCells || border<0 || border>(width-2)/2 || border>(height-2)/2
        || !std::isfinite(spacing) || spacing<=0) return false;
    const size_t count=size_t(width)*height;
    // Clamp before arithmetic/conversion, including finite but extreme spacings.
    const float step=(std::min)(spacing,DistanceCap), diagonal=(std::min)(step*1.41421356237f,DistanceCap);
    std::vector<float> distance(count,DistanceCap);
    for(size_t i=0;i<count;++i) if(!wet[i]) distance[i]=0;
    auto relax=[&](int i,int j,float cost){distance[i]=(std::min)(distance[i],distance[j]+cost);};
    // Borders are absent neighbours, never synthetic land.
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const int i=y*width+x;
        if(x>0)relax(i,i-1,step);
        if(y>0){relax(i,i-width,step);if(x>0)relax(i,i-width-1,diagonal);if(x+1<width)relax(i,i-width+1,diagonal);}
    }
    for(int y=height-1;y>=0;--y) for(int x=width-1;x>=0;--x) {
        const int i=y*width+x;
        if(x+1<width)relax(i,i+1,step);
        if(y+1<height){relax(i,i+width,step);if(x>0)relax(i,i+width-1,diagonal);if(x+1<width)relax(i,i+width+1,diagonal);}
    }
    std::vector<Vector> normals(count,{0,0});
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const int i=y*width+x;if(!wet[i])continue;
        const int left=(std::max)(0,x-1),right=(std::min)(width-1,x+1);
        const int bottom=(std::max)(0,y-1),top=(std::min)(height-1,y+1);
        const float gx=(distance[y*width+right]-distance[y*width+left])/float(right-left);
        const float gy=(distance[top*width+x]-distance[bottom*width+x])/float(top-bottom);
        const float length=std::hypot(gx,gy);
        if(length>0.00001f && distance[i]<PrevailingShoreRange)normals[i]={-gx/length,-gy/length};
    }
    const double infinity=std::numeric_limits<double>::infinity();
    const double lambda=ReferenceWavelength(),k0=2*WaterFftSpectrum::Pi/lambda;
    std::vector<double> slow(count,1),seed(count,infinity),planes(count,0),offsets(count,0);
    const double deep=std::sqrt(std::tanh(k0*DistanceCap*ShoreSlope));
    // Exact polygon links prevent a sub-grid dry gap joining two otherwise adjacent wet cells.
    auto linked=[&](int i,int j) {return !links || (i+1==j?(links[i]&1):j+1==i?(links[j]&1):i+width==j?(links[i]&2):(links[j]&2));};
    auto neighbours=[&](int i,auto visit) {
        const int x=i%width,y=i/width;
        if(x>0 && linked(i,i-1))visit(i-1);if(x+1<width && linked(i,i+1))visit(i+1);
        if(y>0 && linked(i,i-width))visit(i-width);if(y+1<height && linked(i,i+width))visit(i+width);
    };
    std::vector<uint8_t> seen(count,0);std::vector<int> component;size_t largest=0;
    for(size_t first=0;first<count;++first) if(wet[first] && !seen[first]) {
        component.clear();component.push_back(int(first));seen[first]=1;
        double meanX=0,meanY=0,weight=0;
        for(size_t j=0;j<component.size();++j) {
            const int i=component[j];const double w=double(exposure[i])/255;
            if(normals[i].x!=0 || normals[i].y!=0){meanX+=normals[i].x*w;meanY+=normals[i].y*w;weight+=w;}
            neighbours(i,[&](int n){if(wet[n] && !seen[n]){seen[n]=1;component.push_back(n);}});
        }
        Vector prevailing=BaseHeading;const double meanLength=std::hypot(meanX,meanY);
        if(weight>0 && meanLength>=PrevailingThreshold*weight)prevailing={float(meanX/meanLength),float(meanY/meanLength)};
        if(component.size()>largest){result.prevailing=prevailing;largest=component.size();}
        const int upX=prevailing.x>0?-1:prevailing.x<0?1:0,upY=prevailing.y>0?-1:prevailing.y<0?1:0;
        bool sourced=false;
        for(int i:component) {
            const double depth=(std::min)(distance[i],DistanceCap)*double(ShoreSlope);
            slow[i]=1/(std::max)(double(MinSpeed),std::sqrt(std::tanh(k0*depth))/deep);
            const int x=i%width,y=i/width;
            planes[i]=double(prevailing.x)*(x-border)+double(prevailing.y)*(y-border);
            if((x<=border && prevailing.x>0) || (x>=width-border-1 && prevailing.x<0)
                || (y<=border && prevailing.y>0) || (y>=height-border-1 && prevailing.y<0)) {seed[i]=planes[i];sourced=true;}
        }
        if(!sourced) {
            bool seeded=false;int upwind=component[0];
            for(int i:component) {
                if(planes[i]<planes[upwind])upwind=i;
                const int x=i%width+upX,y=i/width+upY;
                if(x<0 || x>=width || y<0 || y>=height || !wet[y*width+x]){seed[i]=planes[i];seeded=true;}
            }
            if(!seeded)seed[upwind]=planes[upwind];
        }
        double offset=infinity;for(int i:component)offset=(std::min)(offset,seed[i]);
        if(!std::isfinite(offset))offset=0;
        for(int i:component){offsets[i]=offset;if(std::isfinite(seed[i]))seed[i]-=offset;}
    }
    auto solve=[&](bool deep) {
        std::vector<double> u=seed;std::vector<uint8_t> accepted(count,0);
        using Entry=std::pair<double,int>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> heap;
        for(size_t i=0;i<count;++i)if(std::isfinite(u[i]))heap.push({u[i],int(i)});
        auto update=[&](int i) {
            if(!wet[i] || accepted[i])return;
            const int x=i%width,y=i/width;
            double a=infinity,b=infinity;
            if(x>0 && linked(i,i-1) && accepted[i-1])a=u[i-1];if(x+1<width && linked(i,i+1) && accepted[i+1])a=(std::min)(a,u[i+1]);
            if(y>0 && linked(i,i-width) && accepted[i-width])b=u[i-width];if(y+1<height && linked(i,i+width) && accepted[i+width])b=(std::min)(b,u[i+width]);
            if(a>b)std::swap(a,b);
            const double cost=deep?1:slow[i];double v=a+cost;
            if(b-a<cost)v=(a+b+std::sqrt((std::max)(0.0,2*cost*cost-(a-b)*(a-b))))*.5;
            if(v<u[i]){u[i]=v;heap.push({v,i});}
        };
        while(!heap.empty()) {
            const auto entry=heap.top();heap.pop();const int i=entry.second;
            if(accepted[i] || entry.first!=u[i])continue;
            accepted[i]=1;neighbours(i,update);
        }
        return u;
    };
    std::vector<double> u=solve(false),amplitude=solve(true),scratch(count);
    // Solve in cell units to avoid under/overflow for finite extreme input spacings.
    const double worldStep=(std::min)(double(spacing),double((std::numeric_limits<float>::max)())/(MaxCells*8.0));
    for(size_t i=0;i<count;++i)amplitude[i]=std::isfinite(amplitude[i])?
        (std::max)(double(MinAmplitude),(std::min)(1.0,1-(amplitude[i]-planes[i]+offsets[i])*worldStep/(ShelterWavelengths*lambda))):0;
    auto blur=[&](std::vector<double> &values) {
        for(int pass=0;pass<BlurPasses;++pass)for(int axis=0;axis<2;++axis) {
            scratch=values;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                const int i=y*width+x,d=axis?width:1;
                // No averaging across dry gaps; leave one-sided edges affine-exact.
                if((axis?y>0 && y+1<height:x>0 && x+1<width)
                    && wet[i] && wet[i-d] && wet[i+d] && linked(i,i-d) && linked(i,i+d) && std::isfinite(u[i-d]) && std::isfinite(u[i+d]))
                    scratch[i]=values[i]*.5+(values[i-d]+values[i+d])*.25;
            }
            values.swap(scratch);
        }
    };
    blur(u);blur(amplitude);
    double maximum=0;for(double v:u)if(std::isfinite(v))maximum=(std::max)(maximum,v*worldStep);
    result.uStep=float((std::max)(double((std::numeric_limits<float>::min)()),maximum/65535));
    // Round scale upward so the farthest value is representable too.
    result.uStep=std::nextafter(result.uStep,std::numeric_limits<float>::infinity());
    result.texels.resize(count*2,0);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const int i=y*width+x;if(!wet[i] || !std::isfinite(u[i]))continue;
        auto derivative=[&](bool axis) {
            const int d=axis?width:1;
            const bool lo=(axis?y>0:x>0) && wet[i-d] && linked(i,i-d) && std::isfinite(u[i-d]);
            const bool hi=(axis?y+1<height:x+1<width) && wet[i+d] && linked(i,i+d) && std::isfinite(u[i+d]);
            return float(lo && hi?(u[i+d]-u[i-d])*.5:hi?u[i+d]-u[i]:lo?u[i]-u[i-d]:0);
        };
        const uint32_t fixed=uint32_t((std::min)(65535.0,std::round(u[i]*worldStep/result.uStep)));
        result.texels[i*2]=((fixed>>8)<<16)|((fixed&255)<<8)|EncodeGradient(derivative(false))|(EncodeGradient(derivative(true))<<24);
        result.texels[i*2+1]=0xff000000u|(uint32_t(std::round(amplitude[i]*255))<<16);
    }
    return true;
}
}
