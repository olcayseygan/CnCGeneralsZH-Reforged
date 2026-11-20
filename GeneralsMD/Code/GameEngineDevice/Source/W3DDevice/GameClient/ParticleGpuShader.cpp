#include "W3DDevice/GameClient/ParticleGpuShader.h"
#include "ffshader.h"
#include "Common/GameCommon.h"
#include <cstring>
namespace {
#include "W3DDevice/GameClient/ParticleGpuShaderSources.h"
void replace(std::string &text,const char *from,const char *to) {
    size_t at=0;while((at=text.find(from,at))!=std::string::npos) {text.replace(at,strlen(from),to);at+=strlen(to);}
}
}
namespace ParticleGpuShader {
const char *Original(Program program) {
    switch(program) {
    case Program::Evaluate:return EVALUATE_PROGRAM;
    case Program::DrawVertex:return DRAW_VERTEX_PROGRAM;
    case Program::DrawPixel:return DRAW_PIXEL_PROGRAM;
    case Program::BlitVertex:return BLIT_VERTEX_PROGRAM;
    case Program::BlitPixel:return BLIT_PIXEL_PROGRAM;
    }
    return "";
}
std::string Sdl(Program program) {
    std::string text=Original(program);
    const std::string fadeMax=std::to_string(8 * LOGICFRAMES_PER_RETAIL_FRAME);
    replace(text,"FADE_MAX",fadeMax.c_str());
    // The CPU birth record's alpha words start at68. Scalar HLSL declarations
    // express that ABI explicitly without relying on relaxed uint2 alignment.
    // DX11 packs float+float3 padding in one register. Scalar padding preserves
    // those bytes while satisfying strict Vulkan1.0 std140 member alignment.
    replace(text,"float3 Unused;","float Unused0; float Unused1; float Unused2;");
    replace(text,"uint2 AlphaKeys;","uint AlphaKeys0; uint AlphaKeys1;");
    replace(text,"spawn.AlphaKeys.x","spawn.AlphaKeys0");replace(text,"spawn.AlphaKeys.y","spawn.AlphaKeys1");
    if(program==Program::Evaluate) {
        replace(text,"cbuffer Frame : register(b0)","cbuffer Frame : register(b0, space2)");
        replace(text,"StructuredBuffer<Spawn> Spawns : register(t0)","StructuredBuffer<Spawn> Spawns : register(t1, space0)");
        replace(text,"StructuredBuffer<SystemEntry> Systems : register(t1)","StructuredBuffer<SystemEntry> Systems : register(t2, space0)");
        replace(text,"StructuredBuffer<float> Fades : register(t2)","StructuredBuffer<float> Fades : register(t3, space0)");
        replace(text,"Texture2D<float> Ground : register(t3)","Texture2D<float> Ground : register(t0, space0)");
        replace(text,"SamplerState Linear : register(s0)","SamplerState Linear : register(s0, space0)");
        replace(text,"RWStructuredBuffer<Particle> Particles : register(u0)","RWStructuredBuffer<Particle> Particles : register(u0, space1)");
    } else if(program==Program::DrawVertex || program==Program::DrawPixel) {
        replace(text,"cbuffer DrawPass : register(b0)",program==Program::DrawVertex ? "cbuffer DrawPass : register(b0, space1)" : "cbuffer DrawPass : register(b0, space3)");
        replace(text,"float4 Colour : COLOR0;","[[vk::location(0)]] float4 Colour : COLOR0;");
        replace(text,"float3 Coord : TEXCOORD0;","[[vk::location(2)]] float3 Coord : TEXCOORD0;");
        replace(text,"float HalfWidth : TEXCOORD1;","[[vk::location(3)]] float HalfWidth : TEXCOORD1;");
        replace(text,"StructuredBuffer<Particle> Particles : register(t0)","StructuredBuffer<Particle> Particles : register(t0, space0)");
        replace(text,"Texture2DArray Sprites : register(t0)","Texture2DArray Sprites : register(t0, space2)");
        replace(text,"SamplerState Linear : register(s0)","SamplerState Linear : register(s0, space2)");
        replace(text,"Texture2D SceneDepth : register(t7)","Texture2D SceneDepth : register(t1, space2)");
    } else if(program==Program::BlitPixel) {
        replace(text,"cbuffer Blit : register(b0)","cbuffer Blit : register(b0, space3)");
        replace(text,"Texture2D Source : register(t0)","Texture2D Source : register(t0, space2)");
        replace(text,"SamplerState Linear : register(s0)","SamplerState Linear : register(s0, space2)");
    }
    return text;
}
}
