// Authored Modern water materials. These constants do not participate in Classic rendering.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include <string>

// Keep the two native float4 declarations identical in C++ and generated HLSL.
#define MODERN_WATER_MATERIAL_UNIFORMS(X) X(MaterialColor) X(MaterialParameters)

namespace ModernWaterMaterial {
enum : unsigned { RegisterColor=94, RegisterParameters=95, RegisterOptics=190, UniformRegisters=2 };
// c94: display-space RGB, brightness. c95: tint amount, pollution amount, bubbles, reserved.
// A zero parameter vector preserves the existing clear-water material exactly.
inline const char *OpticalSource() {
    return R"HLSL(
float3 modernWaterMaterialSource(float3 original,float4 colour,float tint,float illumination,float faceLight) {
    if(!(tint>0.0)) return original;
    float3 display=saturate(colour.rgb),linearColour=pow(display,2.2);
    float maximum=max(max(display.r,display.g),display.b);
    float minimum=min(min(display.r,display.g),display.b);
    float chroma=(maximum-minimum)/max(maximum,0.00001);
    // Neutral authored tints attenuate the original water, preserving white as identity.
    // Chromatic colours introduce their own suspended material instead of multiplying blue by red.
    float3 neutral=original*pow(maximum,2.2);
    float3 authored=linearColour*(0.08+0.92*saturate(illumination))*(0.55+1.10*saturate(faceLight));
    return lerp(original,lerp(neutral,authored,chroma)*saturate(colour.w),saturate(tint));
}
float3 modernWaterMaterialHue(float3 colour) {
    colour=saturate(colour);
    return colour/max(max(max(colour.r,colour.g),colour.b),0.00001);
}
float3 modernWaterMaterialTransmission(float3 original,float3 colour,float pollution,float path,float patch) {
    if(!(pollution>0.0)) return original;
    // Preferential extinction follows the selected material; red and blue pollutants need no code changes.
    float3 extinction=0.004+0.042*(1.0-modernWaterMaterialHue(colour));
    float density=saturate(pollution)*lerp(0.78,1.0,saturate(patch));
    return original*exp(-extinction*clamp(path,0.0,80.0)*density);
}
float3 modernWaterBubbleFilm(float3 colour) {
    return 0.05+0.25*modernWaterMaterialHue(colour);
}
float modernWaterBubbleWeight(float pollution,float enabled,float depth) {
    return saturate(pollution)*saturate(enabled)*smoothstep(0.1,1.0,depth);
}
)HLSL";
}

// Uses the Modern shader's deterministic oceanHash; no gameplay random state or simulation objects.
inline const char *BubbleSource() {
    return R"HLSL(
struct ModernWaterBubbles { float2 slope; float cap; float rim; float ripple; };
ModernWaterBubbles modernWaterBubbles(float2 p,float time,float footprint) {
    ModernWaterBubbles result=(ModernWaterBubbles)0;
    // Every emitter's full footprint remains inside its cell, including the expanding pop ring.
    const float spacing=18.0;
    float2 cell=floor(p/spacing);
    float seed=oceanHash(cell+float2(13.1,7.7));
    float period=4.8+seed*4.2;
    float clock=time/period+oceanHash(cell+43.7);
    float cycle=floor(clock);
    float age=frac(clock)*period;
    float emit=step(0.28,oceanHash(cell+float2(cycle*7.1,cycle*3.4)));
    float2 centre=(cell+0.28+0.44*float2(oceanHash(cell+1.5),oceanHash(cell+71.3)))*spacing;
    centre+=float2(sin(time*0.34+seed*23.0),cos(time*0.28+seed*15.0))*0.35;
    float2 delta=p-centre;
    float distance=length(delta);
    float2 axis=delta/max(distance,0.001);
    float radius=(0.38+seed*0.68)*smoothstep(0.0,0.9,age);
    float live=smoothstep(0.0,0.24,age)*(1.0-smoothstep(1.24,1.42,age))*emit;
    float aa=max(footprint*0.65,0.08);
    float resolved=1.0-smoothstep(1.0,3.5,footprint);
    float cap=(1.0-smoothstep(max(0.0,radius-aa),radius+aa,distance))*live*resolved;
    result.slope=axis*(distance/max(radius,0.08))*cap*0.55;
    result.cap=cap;
    result.rim=exp(-pow((distance-radius)/max(aa,0.10),2.0))*live*resolved;
    float sincePop=age-1.42;
    float ringRadius=radius+max(0.0,sincePop)*2.3;
    float ringLife=smoothstep(0.0,0.12,sincePop)*(1.0-smoothstep(0.35,1.20,sincePop))*emit;
    float width=max(0.19,aa);
    float ring=exp(-pow((distance-ringRadius)/width,2.0))*ringLife*resolved;
    result.slope+=axis*ring*(distance-ringRadius)/width*0.16;
    result.ripple=ring;
    return result;
}
)HLSL";
}
}
