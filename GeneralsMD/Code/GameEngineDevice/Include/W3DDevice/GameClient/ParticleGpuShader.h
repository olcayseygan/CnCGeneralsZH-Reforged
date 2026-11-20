// Exact DX11 closed-form particle programs, with explicit native SDL bindings.
// Distributed under GPL-3.0; see LICENSE.md.
#pragma once
#include <string>
namespace ParticleGpuShader {
enum class Program { Evaluate, DrawVertex, DrawPixel, BlitVertex, BlitPixel };
const char *Original(Program program);
std::string Sdl(Program program);
}
