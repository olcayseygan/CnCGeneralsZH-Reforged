#pragma once
#include "Platform/WaterShoreSource.h"
#include "WaterShoreSourceReference.h"
#include <cstring>
#include <limits>
namespace WaterShoreSourcePerfTest {
struct Fixture {
    int width = 65, height = 49;
    std::vector<uint8_t> heights;
    std::vector<WaterOpticalContext::Polygon> polygons;
    WaterOpticalContext::Field field;
    Fixture() {
        heights.resize(size_t(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                heights[size_t(y) * width + x] = uint8_t((std::max)(0, 20 - std::abs(x - width / 2)));
        WaterOpticalContext::Polygon polygon;
        polygon.points = {{0, 0, 10}, {640, 0, 10}, {640, 480, 10}, {0, 480, 10}};
        polygons.push_back(polygon);
        CHECK(WaterOpticalContext::Build(heights.data(), width, height, 0, 10, 1, 10, polygons, field));
    }
};

template <class T> bool SameBits(T a, T b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

inline void CheckRate() {
    uint32_t random = 0x78563412;
    auto next = [&]() {
        random = random * 1664525u + 1013904223u;
        return float(random >> 8) / 16777215.0f;
    };
    for (unsigned i = 0; i < 10000; ++i) {
        WaterShoreFoam::Context c = {next() * 18 - 1, next() * 14 - 2, next() * 2 - 1, next() * 2 - 1,
            next() * 2 - .1f, next() * 1.8f - .4f, next() * 1.4f - .2f};
        WaterShoreFoam::Wave w = {next() * 7 - 3, next() * 4 - 2, next() * 4 - 2, next() * 8 - 4};
        if (i % 31 == 0)
            c.depth = WaterShoreFoam::MinDepth;
        if (i % 37 == 0)
            c.nx = c.ny = 0;
        if (i % 41 == 0)
            w.dt = 0;
        if (i % 43 == 0)
            w.dx = w.dy = 0;
        CHECK(SameBits(WaterShoreFoam::Rate(c, w),
            WaterShoreSource::PreparedRate(WaterShoreSource::PrepareRate(c), w)));
    }
    for (float invalid :{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()}) {
        for (unsigned field = 0; field < 7; ++field) {
            WaterShoreFoam::Context c = {1, 2, -.8f, .1f, .5f, .7f, .8f};
            float *at[] = {&c.depth, &c.shore, &c.nx, &c.ny, &c.slope, &c.exposure, &c.support};
            *at[field] = invalid;
            const WaterShoreFoam::Wave w = {1, .2f, .3f, -.7f};
            CHECK(SameBits(WaterShoreFoam::Rate(c, w),
                WaterShoreSource::PreparedRate(WaterShoreSource::PrepareRate(c), w)));
        }
    }
}

inline void CheckBatch() {
    Fixture fixture;
    WaterShoreSource::Batch actual;
    WaterShoreSourceReference::Batch original;
    CHECK(actual.Build(fixture.heights.data(), fixture.width, fixture.height, 0, 10, 1, fixture.polygons,
        {{10, &fixture.field}}) == WaterShoreHistory::BuildResult::Ready);
    CHECK(original.Build(fixture.heights.data(), fixture.width, fixture.height, 0, 10, 1, fixture.polygons,
        {{10, &fixture.field}}) == WaterShoreHistory::BuildResult::Ready);
    CHECK(!actual.Cells().empty() && actual.Cells().size() == original.Cells().size());
    std::vector<double> a(actual.Cells().size()), b(a.size());
    unsigned active = 0, zeroFront = 0, liveFront = 0;
    for (const auto &node : actual.Contexts())
        for (const auto &q : node.quadrature)
            if (q.coast.support > 0) {
                if (q.frontWeights.value[0] == 0 && q.frontWeights.value[1] == 0 && q.frontWeights.value[2] == 0)
                    ++zeroFront;
                else
                    ++liveFront;
            }
    CHECK(zeroFront > 0 && liveFront > 0);
    for (double time :{0.0, .05, .1, .4, 1.7, 6.3, 17.0, 60.0, 600.0, 3600.0, 100000.0, -3.0}) {
        actual(time, actual.Cells().data(), a.size(), a.data());
        original(time, original.Cells().data(), b.size(), b.data());
        CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
        for (double v : a)
            active += v > 0;
    }
    CHECK(active > 0);
    // Fill receives topology-sorted/reordered cells, not necessarily source order.
    auto reordered = actual.Cells();
    std::reverse(reordered.begin(), reordered.end());
    actual(1.3, reordered.data(), a.size(), a.data());
    original(1.3, reordered.data(), b.size(), b.data());
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
    // Preserve malformed source-slot rejection without accessing a context.
    auto bad = reordered;
    bad[0].sourceIndex = uint32_t(actual.Contexts().size());
    actual(1.3, bad.data(), a.size(), a.data());
    original(1.3, bad.data(), b.size(), b.data());
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
}

inline void CheckCoverage() {
    for (double x :{-10241.3, -1.0, 0.0, .5, 13.0, 10240.0})
        for (double y :{-991.7, 0.0, 7.0, 333.75}) {
            const double breakup = WaterShoreSource::SpatialBreakup(x, y);
            const float stored = float(breakup);
            CHECK(double(stored) == breakup);
            for (double density :{0.0, 1e-20, .1, 1.0, 10.0})
                for (double age :{0.0, .4, 1.7, 3.0, 8.0, 14.0, 15.0}) {
                    const WaterShoreHistory::State state = {density, density * age};
                    const double original = WaterShoreSourceReference::Coverage(state, x, y, 0);
                    CHECK(SameBits(original, WaterShoreSource::Coverage(state, x, y, 123)));
                    CHECK(SameBits(original, WaterShoreSource::CoverageFromBreakup(state, double(stored))));
                }
        }
    for (auto invalid :{WaterShoreHistory::State{-1, 0}, WaterShoreHistory::State{NAN, 0},
        WaterShoreHistory::State{1, NAN}, WaterShoreHistory::State{INFINITY, 0}})
        CHECK(WaterShoreSource::CoverageFromBreakup(invalid, .5) == 0);
}
}
TEST(water_shore_source_prepared_rate_matches_original_float_bits) {
    WaterShoreSourcePerfTest::CheckRate();
}
TEST(water_shore_source_precomputed_coast_matches_frozen_batch_bits) {
    WaterShoreSourcePerfTest::CheckBatch();
}
TEST(water_shore_source_spatial_breakup_preserves_coverage_double_bits) {
    WaterShoreSourcePerfTest::CheckCoverage();
}
