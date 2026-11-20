#pragma once
#include "Platform/WaterWaveHeading.h"
#include <limits>

namespace WaterWaveHeadingTest {
using namespace WaterWaveHeading;
inline float Dot(Vector a,Vector b){return a.x*b.x+a.y*b.y;}
inline float Alignment(Vector a,Vector b){return Dot(a,b)/std::hypot(a.x,a.y)/std::hypot(b.x,b.y);}
inline void Check() {
    const int w=161,h=161;const size_t count=size_t(w)*h;
    std::vector<uint8_t> wet(count,255),exposure(count,0);Field field,again;
    auto build=[&](Field &f){return Build(wet.data(),exposure.data(),w,h,0,10,f);};
    auto gradient=[&](int x,int y){return DecodeGradient(field.texels[2*(y*w+x)]);};
    auto u=[&](int x,int y){return DecodeU(field.texels[2*(y*w+x)],field.uStep);};
    auto amplitude=[&](int x,int y){return float((field.texels[2*(y*w+x)+1]>>16)&255)/255;};
    auto valid=[&](){for(size_t i=0;i<count;++i)CHECK((field.texels[2*i+1]>>24)==(wet[i]?255u:0u));};
    CHECK(ReferenceWavelength()>200 && ReferenceWavelength()<1500);
    CHECK(build(field));CHECK(field.texels.size()==2*count);valid();
    CHECK(field.prevailing.x==BaseHeading.x && field.prevailing.y==BaseHeading.y);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const Vector g=gradient(x,y);
        CHECK(Alignment(g,BaseHeading)>.999f);
        CHECK(amplitude(x,y)==1);
        if(x+1<w)CHECK(u(x,y)>u(x+1,y));
        if(y+1<h)CHECK(u(x,y)>u(x,y+1));
        const double exact=-(BaseHeading.x*(w-1-x)+BaseHeading.y*(h-1-y))*10;
        CHECK(std::abs(u(x,y)-exact)<field.uStep*.51+0.02);
    }
    // Oblique incidence on a west beach: shallows near shore turn and shorten the waves.
    // Suppress exposure to keep the oblique wind.
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)wet[y*w+x]=x>10?255:0;
    CHECK(build(field));valid();
    CHECK(gradient(12,80).x< -2 && std::abs(gradient(12,80).y)<.8);
    CHECK(std::hypot(gradient(12,80).x,gradient(12,80).y)>2);
    CHECK(Alignment(gradient(120,80),BaseHeading)>.999f);
    // Exposure still selects the old onshore prevailing direction.
    std::fill(exposure.begin(),exposure.end(),255);CHECK(build(again));
    CHECK(again.prevailing.x<-.999f && std::abs(again.prevailing.y)<.001f);
    std::fill(exposure.begin(),exposure.end(),0);
    // A diagonal channel travels along P and wraps onto both banks, never flipping between them.
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)wet[y*w+x]=std::abs(x-y)<45?255:0;
    CHECK(build(field));valid();
    CHECK(Alignment(gradient(80,80),BaseHeading)>.999f);
    CHECK(gradient(58,100).x<gradient(58,100).y-.4);
    CHECK(gradient(100,58).y<gradient(100,58).x-.4);
    // Inflow-edge cells hold the incoming plane wave, so the few beside a shallow bank there may kink.
    for(int y=0;y<h-3;++y)for(int x=0;x<w-3;++x)if(wet[y*w+x]) {
        if(x+1<w && wet[y*w+x+1])CHECK(Alignment(gradient(x,y),gradient(x+1,y))>std::cos(20*WaterFftSpectrum::Pi/180));
        if(y+1<h && wet[(y+1)*w+x])CHECK(Alignment(gradient(x,y),gradient(x,y+1))>std::cos(20*WaterFftSpectrum::Pi/180));
    }
    std::fill(wet.begin(),wet.end(),255);
    for(int y=65;y<=95;++y)for(int x=65;x<=95;++x)wet[y*w+x]=0;
    CHECK(build(field));valid();
    CHECK(amplitude(50,50)<.98 && amplitude(110,110)==1);
    // No boundary water: the upwind shores seed the entire enclosed lake.
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)wet[y*w+x]=x>10 && x<150 && y>10 && y<150?255:0;
    CHECK(build(field));valid();CHECK(build(again));
    CHECK(field.texels==again.texels && field.uStep==again.uStep);
    CHECK(field.prevailing.x==again.prevailing.x && field.prevailing.y==again.prevailing.y);
    for(size_t i=0;i<count;++i) {
        const Vector g=DecodeGradient(field.texels[2*i]);
        CHECK(std::isfinite(g.x) && std::isfinite(g.y) && std::isfinite(DecodeU(field.texels[2*i],field.uStep)));
        CHECK((field.texels[2*i+1]&0x0000ffffu)==0);
    }
    // The independent packed words survive byte encoding, including gradient alpha.
    for(unsigned fixed:{0u,1u,255u,256u,32768u,65535u}) {
        const uint32_t a=((fixed>>8)<<16)|((fixed&255)<<8)|EncodeGradient(-3.25f)|(EncodeGradient(4.5f)<<24);
        CHECK(std::abs(DecodeU(a,field.uStep)-fixed*field.uStep)<=field.uStep*.001f);
        const Vector g=DecodeGradient(a);
        CHECK(std::abs(g.x+3.25f)<=GradientRange/254 && std::abs(g.y-4.5f)<=GradientRange/254);
    }
    std::fill(wet.begin(),wet.end(),0);CHECK(build(field));
    for(uint32_t word:field.texels)CHECK(word==0);
    auto rejects=[&](const uint8_t *water,const uint8_t *ex,int width,int height,int border,float step) {
        CHECK(!Build(water,ex,width,height,border,step,field));CHECK(field.texels.empty());
    };
    const auto water=wet.data(),ex=exposure.data();
    rejects(nullptr,ex,w,h,0,10);rejects(water,nullptr,w,h,0,10);
    rejects(water,ex,1,h,0,10);rejects(water,ex,w,1,0,10);
    rejects(water,ex,MaxDimension+1,h,0,10);rejects(water,ex,w,MaxDimension+1,0,10);
    rejects(water,ex,1025,1024,0,10);
    rejects(water,ex,w,h,-1,10);rejects(water,ex,w,h,80,10);
    const float inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
    for(float step:{0.0f,-1.0f,inf,nan})rejects(water,ex,w,h,0,step);
    const uint8_t smallWet[4]={0,255,255,255};
    for(float step:{(std::numeric_limits<float>::min)(),(std::numeric_limits<float>::max)()}) {
        CHECK(Build(smallWet,smallWet,2,2,0,step,field));
        CHECK(std::isfinite(field.uStep) && field.uStep>0);
        for(size_t i=0;i<4;++i)CHECK(std::isfinite(DecodeU(field.texels[2*i],field.uStep)));
    }
    // Playable sources are inside a dry apron, independent of allocation dimensions.
    const uint8_t insetWet[16]={0,0,0,0,0,255,255,0,0,255,255,0,0,0,0,0};
    CHECK(Build(insetWet,insetWet,4,4,1,10,field));
    for(size_t i=0;i<16;++i)CHECK((field.texels[2*i+1]>>24)==(insetWet[i]?255u:0u));
}
}
