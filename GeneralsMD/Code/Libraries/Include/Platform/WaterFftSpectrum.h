#pragma once

// Source-generated Phillips ocean, using the complex-pair packing of the fft-2d reference.
// All coordinates here are the reference's horizontal x/z plane, before the renderer's Z-up mapping.
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace WaterFftSpectrum {
constexpr unsigned Resolution=256, Phases=8, Cascades=3, Maps=6;
struct Configuration { unsigned resolution, phases, levels; };
// Only validated tiers are accepted; unrecognised values preserve the shared default.
constexpr Configuration ForResolution(unsigned resolution) {
    return resolution==128 ? Configuration{128,7,8} : Configuration{256,8,9};
}

constexpr float Sizes[Cascades]={450.0f,103.0f,13.0f};
// Sampling and spectrum construction share one reference-to-world conversion.
constexpr float WorldScale=20.0f;
// World-space energy budgets for the actual seeded bands, before sampling and limiting.
constexpr double HeightRms[Cascades]={1.50,0.75,0.18};
// Phillips length controls spectral shape independently of propagation direction.
// The medium/fine lengths retain the previous (2^2+2^2)/9.81 value.
constexpr double WindLengths[Cascades]={4.0,8.0/9.81,8.0/9.81};
constexpr double DirectionPowers[Cascades]={2.0,1.0,0.01};
constexpr double WindDirectionX=1.0,WindDirectionY=1.0;
constexpr double Pi=3.14159265358979323846;

inline double BandRamp(double low,double high,double wavelength) {
    const double t=(std::max)(0.0,(std::min)(1.0,(wavelength-low)/(high-low)));
    return t*t*(3.0-2.0*t);
}
// Radial POWER, not amplitude. Wavelength is in world units.
// The crossovers are 24..40 and 128..256 world units. Long swell
// fades out over 1024..1792, before the former hard 2000-unit cutoff. The powers
// sum to one below that upper fade; multiplying h0 by sqrt(power) applies a power
// fade before each band is normalized. Even radial weights preserve conjugate pairs.
inline double BandPower(unsigned cascade,double wavelength) {
    if(cascade>=Cascades || !(wavelength>0) || !std::isfinite(wavelength)) return 0.0;
    const double longer=BandRamp(128.0,256.0,wavelength);
    const double shorter=1.0-BandRamp(24.0,40.0,wavelength);
    if(cascade==0) return longer*(1.0-BandRamp(1024.0,1792.0,wavelength));
    if(cascade==1) return 1.0-longer-shorter;
    return shorter;
}

// Mulberry32's explicit unsigned arithmetic is reproducible, independent of gameplay randomness.
inline float Random(uint32_t &state) {
    uint32_t t=(state+=0x6d2b79f5u);
    t=(t^(t>>15))*(t|1u);t^=t+(t^(t>>7))*(t|61u);
    return float(double(t^(t>>14))/4294967296.0);
}
inline std::vector<float> InitialSpectrum(Configuration configuration=ForResolution(Resolution)) {
    const unsigned n=configuration.resolution,texels=n*n;
    std::vector<float> noise(texels*2),result(texels*Cascades*4);
    uint32_t seed=0;
    for(float &v:noise) v=Random(seed);
    const double direction2=WindDirectionX*WindDirectionX+WindDirectionY*WindDirectionY;
    double energy[Cascades]={};
    // A centred even-sized grid has no distinct +Nyquist partner. Omit both
    // Nyquist axes so odd derivatives and the packed real fields stay Hermitian.
    for(unsigned y=1;y<n;++y) for(unsigned x=1;x<n;++x) {
        const unsigned at=y*n+x,opposite=((n-y)%n)*n+(n-x)%n;
        const double theta=2*Pi*noise[at*2],thetaOther=2*Pi*noise[opposite*2];
        // A random zero must not create log(0), even for a future seed/configuration.
        const double radius=std::sqrt(-2*std::log((std::max)(double(noise[at*2+1]),1.0/4294967296.0)));
        const double radiusOther=std::sqrt(-2*std::log((std::max)(double(noise[opposite*2+1]),1.0/4294967296.0)));
        for(unsigned c=0;c<Cascades;++c) {
            const double kx=2*Pi*(int(x)-int(n/2))/Sizes[c],ky=2*Pi*(int(y)-int(n/2))/Sizes[c];
            const double k2=kx*kx+ky*ky;
            if(k2==0) continue; // zero DC, without evaluating a singular Phillips density
            const double power=BandPower(c,2*Pi*WorldScale/std::sqrt(k2));
            if(power==0) continue;
            const double length=WindLengths[c];
            const double phillips=power*std::exp(-1/(k2*length*length))/(k2*k2);
            const double alignment=(kx*WindDirectionX+ky*WindDirectionY)/std::sqrt(k2*direction2);
            const double positive=std::sqrt(phillips*std::pow((std::max)(0.0,alignment),DirectionPowers[c]));
            const double negative=std::sqrt(phillips*std::pow((std::max)(0.0,-alignment),DirectionPowers[c]));
            float *h=&result[(c*texels+at)*4];
            h[0]=float(radius*std::cos(theta)*positive);h[1]=float(radius*std::sin(theta)*positive);
            h[2]=float(radiusOther*std::cos(thetaOther)*negative);h[3]=float(-radiusOther*std::sin(thetaOther)*negative);
            for(unsigned j=0;j<4;++j) energy[c]+=double(h[j])*h[j];
        }
    }
    // At each k only one propagation half-plane is populated, so Parseval height
    // variance is exactly this sum of packed coefficient powers at every time.
    // One positive factor per cascade preserves phase and conjugate pairing.
    // This also normalizes angular power on the actual finite seeded lattice:
    // no Strengths, Phillips prefactor or extra angular gain survives this step.
    for(unsigned c=0;c<Cascades;++c) {
        const double gain=energy[c]>0?HeightRms[c]/(WorldScale*std::sqrt(energy[c])):0;
        for(unsigned i=0;i<texels*4;++i) {
            float &value=result[c*texels*4+i];value=float(double(value)*gain);
        }
    }
    return result;
}
inline std::vector<float> Butterfly(Configuration configuration=ForResolution(Resolution)) {
    const unsigned Resolution=configuration.resolution,Phases=configuration.phases;
    std::vector<float> result(Resolution*Phases*4);
    auto reverse=[Phases](unsigned v) { unsigned r=0;for(unsigned j=0;j<Phases;++j) { r=(r<<1)|(v&1);v>>=1; }return r; };
    for(unsigned phase=0;phase<Phases;++phase) for(unsigned i=0;i<Resolution;++i) {
        const unsigned span=1u<<phase,base=i%(span*2)<span?i:i-span;
        const double angle=2*Pi*(i*(Resolution>>(phase+1)))/Resolution;
        float *p=&result[(i*Phases+phase)*4];
        p[0]=float(std::cos(angle));p[1]=float(std::sin(angle));
        p[2]=float(phase?base:reverse(base));p[3]=float(phase?base+span:reverse(base+span));
    }
    return result;
}
inline const char *VertexHlsl() {
    return "float4 main(uint id : SV_VertexID) : SV_Position { return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1); }\n";
}
inline std::string Resources(unsigned count) {
    std::string source;
    for(unsigned i=0;i<count;++i) source+="Texture2D<float4> T"+std::to_string(i)+" : register(t"+std::to_string(i)+", space2);\nSamplerState S"+std::to_string(i)+" : register(s"+std::to_string(i)+", space2);\n";
    source+="cbuffer FftParameters : register(b0, space3) { float4 Parameters; float4 Sizes; };\n";
    source+="struct Result {\n";
    for(unsigned i=0;i<Maps;++i) source+="float4 v"+std::to_string(i)+" : SV_Target"+std::to_string(i)+";\n";
    source+="};\n";
    return source;
}
inline std::string SpectrumHlsl() {
    std::string source=Resources(3);
    source+=R"HLSL(
float2 cmul(float2 a,float2 b) { return float2(a.x*b.x-a.y*b.y,a.x*b.y+a.y*b.x); }
void spectrum(float4 initial,int2 pixel,float size,out float4 a,out float4 b) {
    float2 k=6.283185307179586*(float2(pixel)-Parameters.xx*0.5)/size;
    float lengthK=length(k);
    a=0;b=0;
    if(lengthK<=0.000001) return;
    float phase=sqrt(9.81*lengthK)*Parameters.y;
    float2 e=float2(cos(phase),sin(phase));
    float2 h=cmul(initial.xy,e)+cmul(initial.zw,e*float2(1,-1));
    float2 sx=0,sz=0,dx=0,dz=0,dxx=0,dxz=0,dzz=0;
    // The centered Nyquist row/column cannot carry odd derivatives of a real periodic field.
    if(pixel.x!=0) { sx=cmul(float2(0,k.x),h);dx=cmul(float2(0,-k.x/lengthK),h);dxx=h*k.x*k.x/lengthK; }
    if(pixel.y!=0) { sz=cmul(float2(0,k.y),h);dz=cmul(float2(0,-k.y/lengthK),h);dzz=h*k.y*k.y/lengthK;
        if(pixel.x!=0) dxz=h*k.x*k.y/lengthK; }
    a=float4(dx+cmul(float2(0,1),h),dz+cmul(float2(0,1),dxz));
    b=float4(sx+cmul(float2(0,1),sz),dxx+cmul(float2(0,1),dzz));
}
Result main(float4 position:SV_Position) {
    int2 pixel=int2(position.xy);float2 uv=(float2(pixel)+0.5)/Parameters.x;Result result;
    spectrum(T0.SampleLevel(S0,uv,0),pixel,Sizes.x,result.v0,result.v1);
    spectrum(T1.SampleLevel(S1,uv,0),pixel,Sizes.y,result.v2,result.v3);
    spectrum(T2.SampleLevel(S2,uv,0),pixel,Sizes.z,result.v4,result.v5);
    return result;
}
)HLSL";
    return source;
}
inline std::string ButterflyHlsl(Configuration configuration = ForResolution(Resolution)) {
    std::string source=Resources(7);
    source+=R"HLSL(
float4 butterfly(float4 a,float4 b,float2 w) {
    return 0.5*(a+float4(b.x*w.x-b.y*w.y,b.x*w.y+b.y*w.x,b.z*w.x-b.w*w.y,b.z*w.y+b.w*w.x));
}
Result main(float4 position:SV_Position) {
    int2 pixel=int2(position.xy);bool vertical=Parameters.w>0.5;
    float4 entry=T6.SampleLevel(S6,float2((Parameters.z+0.5)/8.0,((vertical?pixel.y:pixel.x)+0.5)/Parameters.x),0);
    float2 a=(vertical?float2(pixel.x,entry.z):float2(entry.z,pixel.y))+0.5;
    float2 b=(vertical?float2(pixel.x,entry.w):float2(entry.w,pixel.y))+0.5;
    a/=Parameters.x;b/=Parameters.x;Result result;
)HLSL";
    for(unsigned i=0;i<Maps;++i) {const std::string s=std::to_string(i);source+="result.v"+s+"=butterfly(T"+s+".SampleLevel(S"+s+",a,0),T"+s+".SampleLevel(S"+s+",b,0),entry.xy);\n";}
    const std::string divisor="/8.0,";
    const size_t at=source.find(divisor);
    if(at!=std::string::npos) source.replace(at,divisor.size(),"/"+std::to_string(configuration.phases)+".0,");
    return source+"return result; }\n";
}
inline std::string PostHlsl() {
    std::string source=Resources(6);
    source+="Result main(float4 position:SV_Position) { int2 pixel=int2(position.xy);float sign=((pixel.x+pixel.y)&1)?-1.0:1.0;float scale=sign*Parameters.x*Parameters.x;float2 uv=(float2(pixel)+0.5)/Parameters.x;Result result;\n";
    for(unsigned i=0;i<Maps;++i) {const std::string s=std::to_string(i);source+="result.v"+s+"=T"+s+".SampleLevel(S"+s+",uv,0)*scale;\n";}
    return source+"return result; }\n";
}
}
