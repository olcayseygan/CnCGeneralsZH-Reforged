// Modern water is compiled for SDL GPU and Direct3D 11. Runtime capability queries
// decide which features the current device supports.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
#define ZH_MODERN_WATER 1
#else
#define ZH_MODERN_WATER 0
#endif

namespace ModernWater {

// Whether this run's device draws the Modern water: the SDL GPU device, or Direct3D 11 presenting.
// W3DWater.cpp answers for the game; the tests that link no device take HazardCloudUnsupported.cpp's no.
bool Supported();

} // namespace ModernWater
