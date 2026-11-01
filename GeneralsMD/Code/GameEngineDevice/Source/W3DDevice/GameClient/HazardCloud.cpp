// Backend-neutral hazard cloud palette and allocation policy.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloudCore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace HazardCloud {
namespace {

// Toxin, nuclear, beta, gamma. The last number is the transparency: 0.67, 0.77 and 0.74 hid the
// units and the ground inside a chemical field, so the three share one value now. A uniform, so no
// program changes.
constexpr float ToxinTransparency = 0.86f;

constexpr std::array<Palette, 4> approved = {{
    {{0.769999981f, 0.939999998f, 0.550000012f}, {0.125490203f, 0.31764707f, 0.0549019612f}, 0.699999988f, ToxinTransparency},
    {{0.749019623f, 0.670588255f, 0.469999999f}, {0.298039228f, 0.184313729f, 0.0705882385f}, 1.29999995f, 0.0f},
    {{0.660000026f, 0.75999999f, 1.0f}, {0.0649999976f, 0.100000001f, 0.209999993f}, 1.52999997f, ToxinTransparency},
    {{0.839999974f, 0.629999995f, 1.0f}, {0.192156866f, 0.0470588244f, 0.235294119f}, 1.32000005f, ToxinTransparency},
}};

} // namespace

// Include both domain boundaries, keep at most two world units between grid
// nodes, and round the dimension up to a small allocation granularity.
unsigned cacheSide(const FieldState &field)
{
    const float extent = 2.0f * (field.radius + field.fringe + MaximumDisplacement);
    const unsigned samples = static_cast<unsigned>(std::clamp(std::ceil(extent / 2.0f) + 1.0f, 32.0f, 512.0f));
    return (samples + 15u) & ~15u;
}

const Palette &defaultPalette(unsigned index) { return approved[index < 4 ? index : 0]; }

std::uint64_t Layer::estimateCacheBytes(const FieldState *fields, unsigned count)
{
    if (count > 2 || (count && !fields)) return (std::numeric_limits<std::uint64_t>::max)();
    std::uint64_t result = (192u * 96u * 4u + TerrainGridSide * TerrainGridSide * 2u) * sizeof(float);
    for (unsigned i = 0; i < count; ++i) {
        if (!std::isfinite(fields[i].radius) || !std::isfinite(fields[i].fringe)
            || fields[i].radius < 0 || fields[i].fringe < 0)
            return (std::numeric_limits<std::uint64_t>::max)();
        if (fields[i].radius <= 0.001f || fields[i].height <= 0.002f || fields[i].density <= 0.00001f) continue;
        const std::uint64_t side = cacheSide(fields[i]);
        result += side * side * 24u * 10u; // R16F density + RGBA16F light.
    }
    return result;
}

} // namespace HazardCloud
