#pragma once
#include "Platform/WaterProjectedSampleCache.h"
#include <array>
#include <cstring>

namespace WaterProjectedSampleCacheTest {
using Sample = std::array<float, 7>;
inline bool Same(const Sample &a, const Sample &b) {
    return std::memcmp(a.data(), b.data(), sizeof(a)) == 0;
}

inline Sample Evaluate(float x, float y, float level, float time, float hull, float terrain) {
    return {{std::sin(x + time) + hull, terrain - level, hull * y, 1 - hull,
        x * y + terrain, time * .125f, level - terrain}};
}

inline void CheckDynamic() {
    WaterProjectedSampleCache::Cache<Sample> cache;
    std::array<const Sample *, 23> nodes = {{}};
    int computed = 0;
    for (int frame = 0; frame < 80; ++frame) {
        cache.BeginRender();
        for (int visit = 0; visit < 4; ++visit)
            for (int n = 0; n < 23; ++n) {
                const int i = (n * 7 + visit) % 23;
                const float x = float(i) * .5f, y = float(i % 5), level = float(i % 3);
                const auto key = WaterProjectedMesh::SampleKey(x, y, level);
                const Sample reference = Evaluate(x, y, level, frame * .125f, frame * .01f, float(frame % 7));
                const Sample *found = cache.Find(key);
                if (visit == 0)
                    CHECK(!found);
                else
                    CHECK(found);
                if (!found) {
                    ++computed;
                    cache.Store(key, reference);
                    found = cache.Find(key);
                }
                CHECK(found && Same(*found, reference));
                // The stable camera reuses the node, but its payload is new each render.
                if (frame)
                    CHECK(found == nodes[i]);
                nodes[i] = found;
            }
        CHECK(cache.Size() == 23 && cache.LiveSize() == 23 && computed == (frame + 1) * 23);
    }
    cache.Clear();
    CHECK(cache.Size() == 0 && cache.LiveSize() == 0);
    CHECK(!cache.Find(WaterProjectedMesh::SampleKey(0, 0, 0)));
}

inline void CheckOracle() {
    WaterProjectedSampleCache::Cache<Sample> cache(17);
    std::unordered_map<WaterProjectedMesh::WorldKey, Sample, WaterProjectedMesh::WorldHash> original;
    for (int frame = 0; frame < 60; ++frame) {
        original.clear();
        cache.BeginRender();
        for (int request = 0; request < 100; ++request) {
            const int n = (request * 7 + frame * 3) % 29;
            const float x = float(n), y = float(n % 4), level = float(n % 3);
            const auto key = WaterProjectedMesh::SampleKey(x, y, level);
            const bool share = request % 5 != 0;
            auto old = share ? original.find(key) : original.end();
            const bool oldHit = old != original.end();
            const Sample *found = share ? cache.Find(key) : nullptr;
            Sample expected, actual;
            auto evaluate = [&]() {
                auto s = Evaluate(x, y, level, frame * .2f, frame * .01f, frame % 5);
                return s;
            };
            if (oldHit)
                expected = old->second;
            else {
                expected = evaluate();
                if (share && original.size() < 17)
                    original.emplace(key, expected);
            }
            if (found)
                actual = *found;
            else {
                actual = evaluate();
                if (share)
                    cache.Store(key, actual);
            }
            CHECK((found != nullptr) == oldHit);
            CHECK(Same(actual, expected));
            CHECK(cache.Size() <= 17 && cache.LiveSize() <= 17);
        }
    }
    // A later request at the same XYZ must receive the complete sample.
    cache.Clear();
    cache.BeginRender();
    const auto key = WaterProjectedMesh::SampleKey(1, 2, 3);
    CHECK(!cache.Find(key));
    const auto complete = Evaluate(1, 2, 3, 4, .2f, 7);
    cache.Store(key, complete);
    CHECK(cache.Find(key) && Same(*cache.Find(key), complete));
    Sample replacement = complete;
    replacement[0] = -100;
    cache.Store(key, replacement);
    CHECK(Same(*cache.Find(key), complete));
}

inline void CheckBoundsAndWrap() {
    const Sample value = {{1, 2, 3, 4, 5, 6, 7}};
    WaterProjectedSampleCache::Cache<Sample> full(3);
    for (int frame = 0; frame < 3; ++frame) {
        full.BeginRender();
        for (int i = 0; i < 3; ++i) {
            const auto key = WaterProjectedMesh::SampleKey(float(frame * 3 + i), 0, 0);
            CHECK(!full.Find(key));
            full.Store(key, value);
            CHECK(full.Find(key) && Same(*full.Find(key), value));
        }
        CHECK(full.Size() == 3 && full.LiveSize() == 3);
        const auto denied = WaterProjectedMesh::SampleKey(float(frame * 3 + 3), 0, 0);
        full.Store(denied, value);
        CHECK(!full.Find(denied));
        for (int i = 0; i < 3; ++i) {
            const auto found = full.Find(WaterProjectedMesh::SampleKey(float(frame * 3 + i), 0, 0));
            CHECK(found && Same(*found, value));
        }
    }
    WaterProjectedSampleCache::Cache<Sample> cache(10000);
    cache.BeginRender();
    for (int i = 0; i < 9000; ++i)
        cache.Store(WaterProjectedMesh::SampleKey(float(i), 0, 0), value);
    cache.BeginRender();
    CHECK(cache.Size() == 9000 && cache.LiveSize() == 0);
    cache.Store(WaterProjectedMesh::SampleKey(0, 0, 0), value);
    cache.BeginRender();
    CHECK(cache.Size() == 0); // Large old view versus one live point.
    WaterProjectedSampleCache::Cache<Sample> disabled(0);
    disabled.BeginRender();
    disabled.Store(WaterProjectedMesh::SampleKey(0, 0, 0), value);
    CHECK(disabled.Size() == 0 && !disabled.Find(WaterProjectedMesh::SampleKey(0, 0, 0)));
    WaterProjectedSampleCache::Cache<Sample, uint8_t> wrapping(10);
    const auto anchor = WaterProjectedMesh::SampleKey(1, 2, 3), current = WaterProjectedMesh::SampleKey(4, 5, 6);
    wrapping.BeginRender();
    wrapping.Store(anchor, value);
    for (int frame = 2; frame <= 256; ++frame) {
        wrapping.BeginRender();
        CHECK(!wrapping.Find(anchor));
        CHECK(!wrapping.Find(current));
        wrapping.Store(current, value);
        CHECK(wrapping.Find(current));
    }
    CHECK(wrapping.Size() == 1); // Wrap discarded the old epoch-one anchor.
}
}

TEST(water_projected_sample_cache_refreshes_dynamic_inputs_once_per_render_and_reuses_nodes) {
    WaterProjectedSampleCacheTest::CheckDynamic();
}
TEST(water_projected_sample_cache_matches_per_render_map_and_keeps_partial_samples_unpublished) {
    WaterProjectedSampleCacheTest::CheckOracle();
}
TEST(water_projected_sample_cache_bounds_camera_history_invalidates_and_handles_epoch_wrap) {
    WaterProjectedSampleCacheTest::CheckBoundsAndWrap();
}
