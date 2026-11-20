// Optional SDL Modern-water transmission. Independent of authored material rules.
#pragma once
namespace WaterUnderwaterClarity {
constexpr unsigned Register = 190;
constexpr float Strength = 0.9f;
bool Supported(); // live renderer capability; false on Windows and null/headless devices
}
