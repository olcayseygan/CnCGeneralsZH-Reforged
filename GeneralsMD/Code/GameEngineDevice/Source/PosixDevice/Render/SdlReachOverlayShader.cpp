#include "SdlReachOverlayShader.h"
#include "Platform/HazardCloudCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace {
void replace(std::string &text,const char *from,const char *to) {
    size_t at=0;while((at=text.find(from,at))!=std::string::npos) {text.replace(at,strlen(from),to);at+=strlen(to);}
}
float clamp(float value) {return std::clamp(value,0.0f,1.0f);}
float smooth(float start,float end,float value) {float t=clamp((value-start)/(end-start));return t*t*(3-2*t);}
}
namespace SdlReachShader {
std::string Vertex() {return HazardCloud::vertexShader();}
std::string Pixel() {
    std::string source=ReachOverlayShaderSources::PixelSource;
    replace(source,"register(b0)","register(b0,space3)");
    replace(source,"SceneDepth : register(t0)","SceneDepth : register(t0,space2)");
    // Each original field keeps its own Size and Load coordinates. Padding in
    // smaller layers is never sampled by the original manual bilinear bounds.
    replace(source,"Texture2D<float2> Fields[32] : register(t1)","Texture2DArray<float2> Fields : register(t1,space2)");
    replace(source,"Fields[i].Load(p)","Fields.Load(int4(p.xy,i,0))");
    replace(source,"Fields[i].Load(p + int3(1, 0, 0))","Fields.Load(int4(p.xy+int2(1,0),i,0))");
    replace(source,"Fields[i].Load(p + int3(0, 1, 0))","Fields.Load(int4(p.xy+int2(0,1),i,0))");
    replace(source,"Fields[i].Load(p + int3(1, 1, 0))","Fields.Load(int4(p.xy+int2(1,1),i,0))");
    return source;
}
std::array<float,4> Shade(float d,float hard,float opacity,float footprint,const float colour[3],
    const float l0[4],const float l1[4],const float l2[4],const float l3[4]) {
    if(d<=0) return {0,0,0,0};
    float pixel=std::clamp(footprint,.001f,16.0f);hard=clamp(hard);
    float edge=clamp(d/pixel),cover=smooth(0,l2[0],d)*(1-hard)+edge*hard,border=(std::max)(l0[0],pixel);
    float stroke=edge*clamp((border-d)/pixel+.5f)*l1[0]*(l2[1]+(1-l2[1])*hard);
    float shadeEnd=border+(std::max)(l3[2],pixel),shade=clamp((d-border)/pixel+.5f)*clamp((shadeEnd-d)/pixel+.5f)*l3[3]*hard;
    float phase=l1[3]/l2[3];phase-=std::floor(phase);float r=(d-border-phase*l3[0])/l3[1];
    float ripple=std::exp(-r*r)*(1-phase)*l2[2]*hard,band=clamp(1-d/l0[1]);
    float fill=clamp(l0[2]+l0[3]*band*band+ripple)*cover;
    std::array<float,4> result={colour[0]*fill,colour[1]*fill,colour[2]*fill,fill};
    for(unsigned c=0;c<4;++c) result[c]=result[c]*(1-shade)+(c==3?shade:0);
    for(unsigned c=0;c<4;++c) result[c]=result[c]*(1-stroke)+(c==3?stroke:(colour[c]+(1-colour[c])*l1[1])*stroke);
    for(float &c:result) c*=opacity;return result;
}
}
