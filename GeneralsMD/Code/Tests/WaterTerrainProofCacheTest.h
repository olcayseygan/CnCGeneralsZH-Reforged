#pragma once

#include "WaterTerrainCullingReference.h"
#include "Platform/WaterTerrainCulling.h"
#include "Platform/WaterWaveCage.h"
#include "Platform/WaterWaveCageMesh.h"
#include <limits>

// Compare active semantic fields, never padding or allocation capacity. The frozen
// implementation also supplies the original copying Install/CompactVertices path.
namespace WaterTerrainProofCacheTest {
using WaterProjectedMesh::Mesh;
using WaterProjectedMesh::Point;

inline Mesh Triangle(Point a = {1.25, 1.25}, Point b = {6.25, 1.25}, Point c = {1.25, 6.25}) {
    Mesh mesh;
    for (Point p :{a, b, c}) {
        WaterProjectedMesh::Vertex v;
        v.world = p;
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2};
    mesh.cells = 37;
    return mesh;
}

inline void Compare(const Mesh &input, const uint8_t *heights, int width, int height, int border,
    double spacing, double scale, double level, double vertical, double horizontal,
    WaterTerrainCulling::WetNodeCache *cache, bool clip = false) {
    Mesh reference = input, actual = input, scalar = input;
    const bool expected = WaterTerrainCullingFrozenReference::CullDryTriangles(
        reference, heights, width, height, border, spacing, scale, level, vertical, horizontal);
    const bool result = WaterTerrainCulling::CullDryTriangles(
        actual, heights, width, height, border, spacing, scale, level, vertical, horizontal, cache);
    const bool scalarResult = WaterTerrainCulling::CullDryTriangles(
        scalar, heights, width, height, border, spacing, scale, level, vertical, horizontal);
    CHECK(result == expected && scalarResult == expected);
    CHECK(projectedCacheSameMesh(reference, actual));
    CHECK(projectedCacheSameMesh(reference, scalar));
    if (!expected) {
        CHECK(projectedCacheSameMesh(input, actual));
        return;
    }
    if (clip) {
        const auto depth = [&](Point p) {
            return level - WaterWaveCage::Height(heights, width, height, border, spacing, scale, float(p.x), float(p.y));
        };
        const auto supported = [](Point p) {
            return p.x + p.y > 1;
        };
        const bool oldClip = WaterWaveCageMesh::ClipRestWaterline(reference, depth, supported);
        const bool newClip = WaterWaveCageMesh::ClipRestWaterline(actual, depth, supported);
        CHECK(oldClip == newClip);
    }
    const bool oldCompact = WaterTerrainCullingFrozenReference::CompactVertices(reference);
    const bool newCompact = WaterTerrainCulling::CompactVertices(actual);
    CHECK(oldCompact == newCompact && projectedCacheSameMesh(reference, actual));
}

inline uint32_t Random(uint32_t &seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

inline void ExtremesAndThresholds() {
    WaterTerrainCulling::WetNodeCache cache;
    Mesh input = Triangle();
    std::vector<uint8_t> heights(81, 30);
    for (int wet = -1; wet < 81; ++wet) {
        std::fill(heights.begin(), heights.end(), 30);
        if (wet >= 0)
            heights[wet] = 0; // A one-node pond anywhere, including dry row boundaries.
        cache.Clear();
        Compare(input, heights.data(), 9, 9, 0, 1, 1, 10, 0, 0, &cache, true);
        Compare(input, heights.data(), 9, 9, 0, 1, 1, 10, 0, 0, &cache, true);
    }
    for (uint8_t h :{uint8_t(0), uint8_t(10), uint8_t(11), uint8_t(255)}) {
        heights.assign(81, h);
        cache.Clear();
        for (double scale :{.25, 1.0, 3.0})
            for (double level :{-0.0, 0.0, 10.0,
                std::nextafter(10.0, -INFINITY), std::nextafter(10.0, INFINITY), 255.0}) {
                Compare(input, heights.data(), 9, 9, 0, 1, scale, level, 0, 0, &cache, true);
                Compare(input, heights.data(), 9, 9, 0, 1, scale, level, .5, 1, &cache, true);
            }
    }
    heights.assign(81, 30);
    cache.Clear();
    for (Mesh edge :{Triangle({-1, 1}, {1, 1}, {1, 2}), Triangle({7.9, 7.9}, {8, 7.9}, {7.9, 8}),
        Triangle({0, 0}, {0, 0}, {0, 0}), Triangle({-0.0, 0}, {.1, 0}, {0, .1})})
        Compare(edge, heights.data(), 9, 9, 0, 1, 1, 10, 0, 0, &cache, true);
    Compare(input, heights.data(), 9, 9, 2, 2, 2, 10, 0, 0, &cache, true);
}

inline void MovingProjectedPipeline() {
    const int width = 64, height = 48;
    uint32_t seed = 0x57a31c9u;
    WaterTerrainCulling::WetNodeCache cache;
    for (int terrain = 0; terrain < 4; ++terrain) {
        std::vector<uint8_t> heights(width * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const uint32_t noise = Random(seed);
                heights[y * width + x] = terrain == 0 ? uint8_t(noise >> 24) : terrain == 1 ? uint8_t(x < 30 ? 0 : 40) :
                    terrain == 2 ? uint8_t((x / 7 + y / 9) % 2 ? 0 : 40) : uint8_t(40);
            }
        if (terrain == 3)
            heights[23 * width + 31] = 0;
        cache.Clear();
        for (int frame = 0; frame < 24; ++frame) {
            const double extent = 220 + frame * 3.5, cx = 245 + frame * 2.25, cy = 210 - frame * .75;
            WaterProjectedMesh::Projection projection = {{2 / extent, 0, -cx * 2 / extent,
                0, 2 / extent, -cy * 2 / extent, frame * .000001, -frame * .0000005, 1}, {0}, {0, 0, 1}, .1, 10, .12};
            CHECK(projection.Prepare());
            WaterProjectedMesh::Point patch[4] = {{0, 0}, {630, 0}, {630, 470}, {0, 470}};
            if (frame % 3 == 1)
                std::reverse(patch, patch + 4);
            std::vector<WaterProjectedMesh::BoundaryKnot> knots;
            for (int edge = 0; edge < 4; ++edge)
                for (int k = 0; k <= 16; ++k) {
                    Point a = patch[edge], b = patch[(edge + 1) % 4];
                    if (a.x > b.x || (a.x == b.x && a.y > b.y))
                        std::swap(a, b);
                    const double t = k / 16.;
                    knots.push_back({{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}, edge, k, t});
                }
            Mesh mesh;
            CHECK(WaterProjectedMesh::Build(patch, projection, {41, 27}, mesh, 60000, knots));
            WaterProjectedMesh::Vertex unused;
            unused.world = {700, 700};
            mesh.vertices.push_back(unused);
            Compare(mesh, heights.data(), width, height, 0, 10, 1, 15, 4.01, 3.01, &cache, true);
        }
    }
}

inline void BudgetAndIndexLimits() {
    WaterTerrainCulling::WetNodeCache cache;
    std::vector<uint8_t> heights(65 * 65, 30);
    Mesh repeated = Triangle({1.25, 1.25}, {30.25, 1.25}, {1.25, 30.25});
    repeated.indices.clear();
    for (int i = 0; i < 5000; ++i)
        repeated.indices.insert(repeated.indices.end(), {0, 1, 2});
    // Each legal rectangle charges 31*31 samples, even though SAT is O(1).
    // Later identical dry triangles must remain after the mesh budget is exhausted.
    Compare(repeated, heights.data(), 65, 65, 0, 1, 1, 10, 0, 0, &cache);
    Mesh expected = repeated;
    CHECK(WaterTerrainCullingFrozenReference::CullDryTriangles(expected, heights.data(), 65, 65, 0, 1, 1, 10,
        0, 0));
    CHECK(!expected.indices.empty() && expected.indices.size() < repeated.indices.size());
    Mesh wetBudget = repeated;
    // 4,364 charges of 961 leave 500 samples. Larger requests then bypass
    // without charging, so consume the remainder with a 20*25 first-wet query.
    const size_t rectangleCharge = 31u * 31u, remainderCharge = 20u * 25u;
    CHECK(WaterTerrainCullingFrozenReference::MaxMeshSamples % rectangleCharge == remainderCharge);
    for (Point p :{Point{1.25, 1.25}, Point{19.25, 1.25}, Point{1.25, 24.25},
        Point{40.25, 40.25}, Point{45.25, 40.25}, Point{40.25, 45.25}}) {
        WaterProjectedMesh::Vertex v;
        v.world = p;
        wetBudget.vertices.push_back(v);
    }
    wetBudget.indices.insert(wetBudget.indices.end(), {3, 4, 5});
    wetBudget.indices.insert(wetBudget.indices.end(), {6, 7, 8});
    heights[1 * 65 + 1] = 0;
    cache.Clear();
    // Even immediate first-node wet exits consume their complete logical sample
    // charge. The final dry triangle must therefore remain beyond the budget.
    Compare(wetBudget, heights.data(), 65, 65, 0, 1, 1, 10, 0, 0, &cache);
    Mesh charged = wetBudget;
    CHECK(WaterTerrainCullingFrozenReference::CullDryTriangles(charged, heights.data(), 65, 65, 0, 1, 1, 10,
        0, 0));
    CHECK(charged.indices.size() == wetBudget.indices.size());
    Compare(Triangle({1, 1}, {63, 1}, {1, 63}), heights.data(), 65, 65, 0, 1, 1, 10, 0, 0, &cache);

    Mesh high = Triangle();
    high.vertices.resize(65536, high.vertices[0]);
    high.vertices[65533].world = {2, 2};
    high.vertices[65534].world = {3, 2};
    high.vertices[65535].world = {2, 3};
    high.indices = {65535, 65533, 65534};
    heights.assign(65 * 65, 0);
    cache.Clear();
    Compare(high, heights.data(), 65, 65, 0, 1, 1, 10, 0, 0, &cache);
    high.vertices.push_back(high.vertices[0]);
    Compare(high, heights.data(), 65, 65, 0, 1, 1, 10, 0, 0, &cache);
}

inline void InvalidAndCompaction() {
    WaterTerrainCulling::WetNodeCache cache;
    std::vector<uint8_t> heights(81, 30);
    const Mesh base = Triangle();
    for (int failure = 0; failure < 21; ++failure) {
        Mesh mesh = base;
        const uint8_t *data = heights.data();
        int width = 9, height = 9, border = 0;
        double spacing = 1, scale = 1, level = 10, vertical = 0, horizontal = 0;
        switch (failure) {
        case 0 : data = nullptr;
            break;
        case 1 : width = 1;
            break;
        case 2 : height = 2049;
            break;
        case 3 : border = -1;
            break;
        case 4 : border = 5;
            break;
        case 5 : spacing = 0;
            break;
        case 6 : spacing = -0.0;
            break;
        case 7 : spacing = INFINITY;
            break;
        case 8 : scale = 0;
            break;
        case 9 : scale = NAN;
            break;
        case 10 : level = NAN;
            break;
        case 11 : level = INFINITY;
            break;
        case 12 : vertical = -1;
            break;
        case 13 : horizontal = -1;
            break;
        case 14 : horizontal = INFINITY;
            break;
        case 15 : mesh.indices.push_back(0);
            break;
        case 16 : mesh.indices[2] = 65535;
            break;
        case 17 : mesh.vertices[1].world.x = NAN;
            break;
        case 18 : mesh.vertices[2].world.y = INFINITY;
            break;
        case 19 : scale = std::numeric_limits<double>::max();
            break;
        case 20 : level = std::numeric_limits<double>::max();
            vertical = level;
            break;
        }
        Compare(mesh, data, width, height, border, spacing, scale, level, vertical, horizontal, &cache);
    }
    Mesh input = base;
    for (int i = 0; i < 12; ++i) {
        WaterProjectedMesh::Vertex v;
        v.world = {double(i), double(i + 1)};
        v.boundary = i % 5 - 1;
        v.edgeParameter = i * .03125;
        v.knot = i;
        v.lattice = 53 - i;
        input.vertices.push_back(v);
    }
    input.indices = {14, 3, 8, 8, 3, 5, 5, 14, 8};
    input.transitions.resize(1);
    input.transitions[0] = {};
    input.transitions[0].analytic = .75;
    input.collars.resize(1);
    input.collars[0].count = 1;
    input.collars[0].fade = .5;
    input.collars[0].edge[0] = 2;
    input.collars[0].parameter[0] = .3;
    input.collars[0].weight[0] = .5;
    input.vertices[8].collar = 0;
    input.vertices[14].transition = 0;
    for (int variant = 0; variant < 4; ++variant) {
        Mesh old = input, actual = input;
        if (variant == 1) {
            old.indices.clear();
            actual.indices.clear();
        }
        if (variant == 2) {
            old.indices[2] = 65535;
            actual.indices[2] = 65535;
        }
        if (variant == 3) {
            old.vertices[0].world.x = NAN;
            actual.vertices[0].world.x = NAN;
        }
        const bool a = WaterTerrainCullingFrozenReference::CompactVertices(old);
        const bool b = WaterTerrainCulling::CompactVertices(actual);
        CHECK(a == b && projectedCacheSameMesh(old, actual));
    }
}

inline void LifetimeAndCapacity() {
    std::vector<uint8_t> heights(81, 30);
    const Mesh mesh = Triangle();
    WaterTerrainCulling::WetNodeCache cache(1);
    Compare(mesh, heights.data(), 9, 9, 0, 1, 1, 10, 0, 0, &cache);
    const size_t builds = cache.BuildCount();
    CHECK(builds > 0 && cache.EntryCount() == 1 && cache.CachedBytes() > 0);
    Compare(mesh, heights.data(), 9, 9, 0, 1, 1, 10.1, 0, 0, &cache);
    CHECK(cache.BuildCount() == builds && cache.EntryCount() == 1); // Same encoded wet/dry cutoff.
    Compare(mesh, heights.data(), 9, 9, 0, 1, 1, 20, 0, 0, &cache);
    CHECK(cache.EntryCount() == 1 && cache.BuildCount() > builds);
    Compare(mesh, heights.data(), 9, 9, 0, 1, 1, 10, 0, 0, &cache);
    CHECK(cache.EntryCount() == 1);
    heights[3 * 9 + 3] = 0;
    cache.Clear();
    CHECK(cache.EntryCount() == 0 && cache.CachedBytes() == 0);
    Compare(mesh, heights.data(), 9, 9, 0, 1, 1, 10, 0, 0, &cache, true);
    std::vector<uint8_t> other(81, 30);
    Compare(mesh, other.data(), 9, 9, 0, 1, 1, 10, 0, 0, &cache);
    WaterTerrainCulling::WetNodeCache disabled(0);
    Compare(mesh, other.data(), 9, 9, 0, 1, 1, 10, 0, 0, &disabled, true);
    CHECK(disabled.EntryCount() == 0 && disabled.CachedBytes() == 0 && disabled.BuildCount() == 0);

    WaterTerrainCulling::WetNodeCache warmed;
    double ceiling = 0;
    CHECK(WaterTerrainCulling::ProofCeiling(1, 10, .5, ceiling));
    const double expectedTop = 10 + .5;
    const double expectedCeiling = expectedTop + 8 * std::numeric_limits<float>::epsilon() *
        (std::max)(1.0, (std::max)(std::abs(expectedTop), 255.0));
    CHECK(projectedCacheSameDouble(ceiling, expectedCeiling));
    CHECK(warmed.Prewarm(other.data(), 9, 9, 1, ceiling));
    CHECK(warmed.BuildCount() == 1 && warmed.EntryCount() == 1 && warmed.CachedBytes() > 0
        && warmed.QueryHits() == 0);
    CHECK(warmed.Prewarm(other.data(), 9, 9, 1, ceiling) && warmed.BuildCount() == 1 && warmed.QueryHits() == 0);
    Compare(mesh, other.data(), 9, 9, 0, 1, 1, 10, .5, 0, &warmed, true);
    CHECK(warmed.BuildCount() == 1); // First gameplay proof consumes the loading-built table.
    CHECK(!disabled.Prewarm(other.data(), 9, 9, 1, ceiling));
    CHECK(!warmed.Prewarm(nullptr, 9, 9, 1, ceiling) && warmed.BuildCount() == 1);
    WaterTerrainCulling::WetNodeCache trivial;
    CHECK(trivial.Prewarm(other.data(), 9, 9, 1, -1) && trivial.Prewarm(other.data(), 9, 9, 1, 255));
    CHECK(trivial.BuildCount() == 0 && trivial.EntryCount() == 0 && trivial.CachedBytes() == 0
        && trivial.QueryHits() == 0);
    CHECK(!WaterTerrainCulling::ProofCeiling(0, 10, 0, ceiling));
    CHECK(!WaterTerrainCulling::ProofCeiling(1, INFINITY, 0, ceiling));
    CHECK(!WaterTerrainCulling::ProofCeiling(1, 10, -1, ceiling));
    CHECK(!WaterTerrainCulling::ProofCeiling(std::numeric_limits<double>::max(), 10, 0, ceiling));
}

inline void RectangleQueries() {
    const int width = 13, height = 11;
    uint32_t seed = 0x9d125ad3u;
    std::vector<uint8_t> heights(width * height);
    for (auto &h : heights)
        h = uint8_t(Random(seed) >> 24);
    heights[0] = 0;
    heights[width - 1] = 255;
    heights[(height - 1) * width] = 127;
    heights.back() = 128;
    WaterTerrainCulling::WetNodeCache cache(3);
    for (double scale :{.25, 1.0, 2.75})
        for (int cutoff = -1; cutoff <= 256; ++cutoff) {
            const double equal = cutoff * scale;
            for (double ceiling :{std::nextafter(equal, -INFINITY), equal, std::nextafter(equal, INFINITY)}) {
                auto query = cache.Prepare(heights.data(), width, height, scale, ceiling);
                for (int sample = 0; sample < 12; ++sample) {
                    int x0 = int(Random(seed) % width), x1 = int(Random(seed) % width);
                    int y0 = int(Random(seed) % height), y1 = int(Random(seed) % height);
                    if (x0 > x1)
                        std::swap(x0, x1);
                    if (y0 > y1)
                        std::swap(y0, y1);
                    if (sample < 4) {
                        y0 = y1 = sample % 2 ? height - 1 : 0;
                        x0 = x1 = sample / 2 ? width - 1 : 0;
                    }
                    bool expected = false;
                    for (int y = y0; y <= y1; ++y)
                        for (int x = x0; x <= x1; ++x)
                            expected = expected || heights[y * width + x] * scale <= ceiling;
                    bool wet = !expected;
                    CHECK(query.HasWet(x0, x1, y0, y1, wet));
                    CHECK(wet == expected);
                }
            }
            CHECK(cache.EntryCount() <= 3);
        }
    CHECK(cache.QueryHits() > 0 && cache.BuildCount() > 0);
    auto query = cache.Prepare(heights.data(), width, height, 1, 10);
    const size_t hits = cache.QueryHits(), builds = cache.BuildCount();
    for (std::array<int, 4> bounds :{std::array<int, 4>{{-1, 2, 0, 1}}, {{0, width, 0, 1}},
        {{0, 1, -1, 1}}, {{0, 1, 0, height}}, {{2, 1, 0, 1}}, {{0, 1, 2, 1}}}) {
        bool wet = true;
        CHECK(!query.HasWet(bounds[0], bounds[1], bounds[2], bounds[3], wet));
        CHECK(wet);
    }
    CHECK(cache.QueryHits() == hits && cache.BuildCount() == builds);
    for (int invalid = 0; invalid < 7; ++invalid) {
        const uint8_t *data = heights.data();
        int w = width, h = height;
        double scale = 1, ceiling = 10;
        switch (invalid) {
        case 0 : data = nullptr;
            break;
        case 1 : w = 1;
            break;
        case 2 : h = 2049;
            break;
        case 3 : scale = -0.0;
            break;
        case 4 : scale = INFINITY;
            break;
        case 5 : ceiling = NAN;
            break;
        case 6 : ceiling = INFINITY;
            break;
        }
        auto invalidQuery = cache.Prepare(data, w, h, scale, ceiling);
        bool wet = false;
        CHECK(!invalidQuery.HasWet(0, 1, 0, 1, wet));
        CHECK(!wet);
    }
}
}
