/* Footprint filtering of the existing FFT fields. Geometry continues to use level zero. */
#pragma once
#include "Platform/WaterFftSpectrum.h"
#include <array>
#include <cstdio>

namespace WaterFftFiltering {
constexpr unsigned Levels=WaterFftSpectrum::Phases+1;
using VarianceTable=std::array<std::array<double,WaterFftSpectrum::Cascades>,Levels>;

// A mip is a box average of 2^level base texels. Integrate the energy removed from the
// two height-slope channels, using this exact deterministic spectrum, not an unrelated
// roughness constant. Independent forward/backward modes have additive expected power.
inline VarianceTable UnresolvedSlopeVariance(const std::vector<float>& initial, WaterFftSpectrum::Configuration configuration=WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution)) {
    using namespace WaterFftSpectrum;
    const unsigned Resolution=configuration.resolution;
    VarianceTable result={};
    if(initial.size()!=Resolution*Resolution*Cascades*4) return result;
    for(unsigned c=0;c<Cascades;++c) for(unsigned y=0;y<Resolution;++y) for(unsigned x=0;x<Resolution;++x) {
        const float *h=&initial[((c*Resolution+y)*Resolution+x)*4];
        const double power=double(h[0])*h[0]+double(h[1])*h[1]+double(h[2])*h[2]+double(h[3])*h[3];
        if(power==0) continue;
        const double ax=Pi*(int(x)-int(Resolution/2))/Resolution,ay=Pi*(int(y)-int(Resolution/2))/Resolution;
        const double kx=2*ax*Resolution/Sizes[c],ky=2*ay*Resolution/Sizes[c];
        const double energy=power*(kx*kx+ky*ky);
        for(unsigned l=1;l<configuration.levels;++l) {
            const unsigned width=1u<<l;
            const double fx=ax==0?1:std::sin(width*ax)/(width*std::sin(ax));
            const double fy=ay==0?1:std::sin(width*ay)/(width*std::sin(ay));
            result[l][c]+=energy*(std::max)(0.0,1-fx*fx*fy*fy);
        }
    }
    return result;
}
inline const VarianceTable &SpectrumVariance(WaterFftSpectrum::Configuration configuration=WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution)) {
    if(configuration.resolution==128) {
        static const VarianceTable low=UnresolvedSlopeVariance(WaterFftSpectrum::InitialSpectrum(WaterFftSpectrum::ForResolution(128)),WaterFftSpectrum::ForResolution(128));
        return low;
    }
    static const VarianceTable high=UnresolvedSlopeVariance(WaterFftSpectrum::InitialSpectrum());
    return high;
}
inline std::string Source(WaterFftSpectrum::Configuration configuration=WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution)) {
    std::string source="static const float3 fftLostSlopeVariance["+std::to_string(configuration.levels)+"]={\n";
    const auto &table=SpectrumVariance(configuration);
    for(unsigned l=0;l<configuration.levels;++l) {
        char row[192];std::snprintf(row,sizeof(row),"float3(%.9g,%.9g,%.9g)%s\n",table[l][0],table[l][1],table[l][2],l+1<configuration.levels?",":"");
        source+=row;
    }
    source+=R"HLSL(};
float3 fftShadingLod(float footprint) {
    float3 texels=max(footprint,0.0)*256.0/(float3(450.0,103.0,13.0)*fftWorldScale);
    return clamp(log2(max(texels,float3(1,1,1))),0.0,8.0);
}
float fftShadingVariance(float3 lod) {
    float result=0;
    for(int c=0;c<3;++c) {
        float level=clamp(lod[c],0.0,8.0);int low=min((int)level,7);
        result+=lerp(fftLostSlopeVariance[low][c],fftLostSlopeVariance[low+1][c],level-low);
    }
    return result;
}
)HLSL";
    auto replace=[&](const std::string &from,const std::string &to) {
        for(size_t at=source.find(from);at!=std::string::npos;at=source.find(from,at+to.size())) source.replace(at,from.size(),to);
    };
    replace("*256.0/","*"+std::to_string(configuration.resolution)+".0/");
    replace(",0.0,8.0)",",0.0,"+std::to_string(configuration.phases)+".0)");
    replace("level,7)","level,"+std::to_string(configuration.phases-1)+")");
    return source;
}
}
