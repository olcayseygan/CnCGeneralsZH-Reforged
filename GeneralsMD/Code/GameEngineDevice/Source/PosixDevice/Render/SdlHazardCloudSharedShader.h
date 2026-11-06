// Shader programs for the shared hazard cloud volume.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include <string>
namespace HazardCloudShared {
enum class ShaderPass { Inject, Light, Volume, Composite, Heat };
std::string sharedPixelShader(ShaderPass pass);
}
