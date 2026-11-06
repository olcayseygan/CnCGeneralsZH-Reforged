// Entry points for builds without a hazard cloud renderer backend: the tests
// that link no device. The game links SDL GPU's or Direct3D 11's instead.
// The options menu asks the Modern water the same question, answered here the same way.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloud.h"
#include "Platform/HazardCloudShared.h"
#include "Platform/ModernWaterBuild.h"
#include "Platform/WaterScreenReflection.h"

namespace HazardCloud {

bool Supported() { return false; }
std::unique_ptr<Layer> CreateLayer() { return nullptr; }
void ReleaseDevice() {}

} // namespace HazardCloud

namespace ModernWater {

bool Supported() { return false; }

} // namespace ModernWater

namespace HazardCloudShared {

bool Supported() { return false; }
std::unique_ptr<Renderer> CreateRenderer() { return nullptr; }

} // namespace HazardCloudShared

namespace WaterScreenReflection {
bool Supported() { return false; }
}
