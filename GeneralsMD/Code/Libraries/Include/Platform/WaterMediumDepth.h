/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Bounded, renderer-only effective wave depth; no simulation or material optics inputs change.
namespace WaterMediumDepth {
constexpr int MaxDimension = 2048;
constexpr size_t MaxCells = 1024u * 1024u;
constexpr float Cap = 40.0f;
struct Field {
    int width = 0, height = 0;
    std::vector<uint8_t> shoreX,shoreY,shoreConfidence; // Optional shore direction and confidence; depth transport stays unchanged.
    std::vector<float> shoreDistance; // Optional uncoupled distance to land, capped at 3000 world units.
    std::vector<float> depth; // Quantized once to the reference RGBA8 precision before float-atlas transport.
};
inline uint8_t Encode(float depth) {
    if (!std::isfinite(depth)) return 0;
    return uint8_t(std::lround((std::max)(0.0f, (std::min)(Cap, depth)) * (255.0f / Cap)));
}
inline float Gain(float depth, float reach = 4.0f) {
    if (!std::isfinite(depth) || !std::isfinite(reach) || reach <= 0) return 0;
    const float t = (std::max)(0.0f, (std::min)(1.0f, depth / reach));
    return t * t * (3.0f - 2.0f * t);
}
// shore is the optical context's distance-to-land byte (0..100 world units).
// The normalized wet-only Gaussian excludes dry zeroes: land cannot broaden the
// shallow halo beyond raw's existing distance ramp. Borders are absent neighbours.
inline bool Build(const uint8_t *heights, const uint8_t *wet, const uint8_t *shore,
    int width, int height, float spacing, float heightScale, float level, Field &result) {
    result = Field{};
    if (!heights || !wet || !shore || width < 2 || height < 2 || width > MaxDimension
        || height > MaxDimension || size_t(width) * height > MaxCells
        || !std::isfinite(spacing) || spacing <= 0 || !std::isfinite(heightScale)
        || heightScale <= 0 || !std::isfinite(level)) return false;
    const size_t count = size_t(width) * height;
    std::vector<float> values(count, 0), scratch(count, 0);
    for (size_t i = 0; i < count; ++i) if (wet[i]) {
        const double terrainDepth = (std::max)(0.0, double(level) - double(heights[i]) * heightScale);
        values[i] = float((std::min)(0.4 * double(shore[i]) * (100.0 / 255.0), 4.0 * terrainDepth));
    }
    constexpr float weights[5] = {1, 4, 6, 4, 1};
    for (int axis = 0; axis < 2; ++axis) {
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            if (!wet[i]) continue;
            float sum = 0, total = 0;
            for (int k = -2; k <= 2; ++k) {
                const int xx = x + (axis == 0 ? k : 0), yy = y + (axis == 1 ? k : 0);
                if (xx < 0 || xx >= width || yy < 0 || yy >= height) continue;
                const size_t j = size_t(yy) * width + xx;
                if (wet[j]) { sum += weights[k + 2] * values[j]; total += weights[k + 2]; }
            }
            scratch[i] = sum / total;
        }
        values.swap(scratch);
    }
    for (float &d : values) d = Encode(d) * (Cap / 255.0f);
    result.width = width; result.height = height; result.depth.swap(values);
    return true;
}
inline float Sample(const Field &field, float gx, float gy) {
    if (field.width < 2 || field.height < 2 || field.width > MaxDimension || field.height > MaxDimension
        || size_t(field.width) * field.height > MaxCells
        || field.depth.size() != size_t(field.width) * field.height
        || !std::isfinite(gx) || !std::isfinite(gy)) return 0;
    gx = (std::max)(0.0f, (std::min)(float(field.width - 1), gx));
    gy = (std::max)(0.0f, (std::min)(float(field.height - 1), gy));
    const int x = int(gx), y = int(gy), x1 = (std::min)(x + 1, field.width - 1), y1 = (std::min)(y + 1, field.height - 1);
    const float tx = gx - x, ty = gy - y;
    const float a = field.depth[size_t(y) * field.width + x] * (1 - tx) + field.depth[size_t(y) * field.width + x1] * tx;
    const float b = field.depth[size_t(y1) * field.width + x] * (1 - tx) + field.depth[size_t(y1) * field.width + x1] * tx;
    return a * (1 - ty) + b * ty;
}
}
