// Native shader bindings plus pure preparation/reference helpers. No D3D API.
#pragma once
#include "W3DDevice/GameClient/ReachOverlayShaderSources.h"
#include <array>
#include <string>
namespace SdlReachShader {
std::string Vertex();
std::string Pixel();
struct Sample {float Distance,Hardness;};
// Independent scalar CPU reference for analytic probes. PixelFootprint is the
// fwidth supplied by the test's analytic field, clamped as the shader does.
std::array<float,4> Shade(float distance,float hardness,float opacity,float pixelFootprint,
    const float colour[3],const float look0[4],const float look1[4],const float look2[4],const float look3[4]);
}
