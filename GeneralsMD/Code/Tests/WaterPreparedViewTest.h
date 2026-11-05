#pragma once

// Included after the prepared-cache fixtures in test_water_grid.cpp.

TEST(water_prepared_view_matches_copy_pipeline_without_output_copy)
{
    using namespace WaterProjectedMesh;
    const Mesh input = preparedCacheInput();
    const auto terrain = preparedCacheTerrain();
    WaterPreparedMeshCache::Cache copied, borrowed;
    const Vertex *computedVertices = nullptr;
    int computes = 0;
    bool reused = true;
    auto compute = [&](Mesh &mesh) {
        ++computes;
        preparedCachePipeline(mesh, terrain, 5, true, true);
        computedVertices = mesh.vertices.data();
    };
    Mesh expected = input;
    copied.Process(expected, 5, true, true, [&](Mesh &mesh) {
        preparedCachePipeline(mesh, terrain, 5, true, true);
    });
    Mesh caller = input;
    const auto first = borrowed.ProcessView(caller, 5, true, true, compute, &reused);
    CHECK(!reused && computes == 1 && projectedCacheSameMesh(first.Get(), expected));
    CHECK(first->vertices.data() == computedVertices && caller.vertices.empty());
    const Mesh *address = &first.Get();
    // Returned data belongs to the cache, including on the miss. Editing the
    // separate caller mesh cannot alter it, and each later hit borrows it again.
    caller = input;
    caller.vertices[0].world.x = -100;
    CHECK(projectedCacheSameMesh(first.Get(), expected));
    for (int frame = 0; frame < 3; ++frame) {
        caller = input;
        const auto hit = borrowed.ProcessView(caller, 5, true, true, compute, &reused);
        CHECK(reused && computes == 1 && &hit.Get() == address && projectedCacheSameMesh(hit.Get(), expected));
        CHECK(projectedCacheSameMesh(caller, input));
    }
}

TEST(water_prepared_view_bypasses_preserve_computed_output_and_prior_entry)
{
    using namespace WaterProjectedMesh;
    const Mesh input = preparedCacheInput();
    WaterPreparedMeshCache::Cache cache;
    auto identity = [](Mesh &) {};
    bool reused = true;
    Mesh initial = input;
    cache.ProcessView(initial, 5, false, false, identity);
    const size_t retained = cache.CachedBytes();
    for (int invalid = 0; invalid < 5; ++invalid) {
        Mesh caller = input;
        ++caller.cells;
        float level = 5;
        if (invalid == 0)
            caller.indices[0] = 65535;
        if (invalid == 1)
            caller.vertices[0].world.x = NAN;
        if (invalid == 2)
            level = INFINITY;
        const auto result = cache.ProcessView(caller, level, false, false, [&](Mesh &mesh) {
            if (invalid == 3)
                mesh.collars[0].count = -1;
            if (invalid == 4)
                mesh.vertices.resize(65537);
        }, &reused);
        CHECK(!reused && &result.Get() == &caller && cache.CachedBytes() == retained && cache.EntryCount() == 1);
        Mesh probe = input;
        const auto hit = cache.ProcessView(probe, 5, false, false, identity, &reused);
        CHECK(reused && projectedCacheSameMesh(hit.Get(), input));
    }
    for (bool bytesDisabled :{false, true}) {
        WaterPreparedMeshCache::Cache disabled(bytesDisabled ? 0 : 32u * 1024u * 1024u, bytesDisabled ? 8 : 0);
        Mesh caller = input;
        const auto result = disabled.ProcessView(caller, 5, false, false, identity, &reused);
        CHECK(!reused && &result.Get() == &caller && projectedCacheSameMesh(result.Get(),
            input) && disabled.CachedBytes() == 0);
    }
}

TEST(water_prepared_view_accounts_retained_capacity_and_normalizes_only_if_needed)
{
    using namespace WaterProjectedMesh;
    const Mesh input = preparedCacheInput();
    const size_t budget = preparedCacheBytes(input) * 2;
    WaterPreparedMeshCache::Cache cache(budget);
    Mesh caller = input;
    caller.vertices.reserve(1000);
    const Vertex *large = caller.vertices.data();
    bool reused = true;
    const auto result = cache.ProcessView(caller, 5, false, false, [](Mesh &) {}, &reused);
    CHECK(!reused && projectedCacheSameMesh(result.Get(), input) && result->vertices.data() != large);
    CHECK(cache.EntryCount() == 1 && cache.CachedBytes() <= budget);
    caller = input;
    const auto hit = cache.ProcessView(caller, 5, false, false, [](Mesh &) {}, &reused);
    CHECK(reused && projectedCacheSameMesh(hit.Get(), input));

    // Spare capacities that do fit are retained and included in the byte count.
    WaterPreparedMeshCache::Cache roomy;
    caller = input;
    caller.vertices.reserve(1000);
    large = caller.vertices.data();
    const size_t actualOutput = caller.vertices.capacity() * sizeof(Vertex)
        + caller.indices.capacity() * sizeof(uint16_t)
        + caller.transitions.capacity() * sizeof(CentreTransition)
        + caller.collars.capacity() * sizeof(BoundaryCollar);
    const auto moved = roomy.ProcessView(caller, 5, false, false, [](Mesh &) {});
    CHECK(moved->vertices.data() == large && roomy.CachedBytes() == preparedCacheBytes(input) + actualOutput);
}

TEST(water_prepared_view_preserves_fifo_and_interoperates_with_copy_api)
{
    using namespace WaterProjectedMesh;
    const Mesh input = preparedCacheInput();
    WaterPreparedMeshCache::Cache copied(32u * 1024u * 1024u, 2), borrowed(32u * 1024u * 1024u, 2);
    auto compute = [](Mesh &mesh) {
        mesh.vertices[0].world.x += mesh.cells;
    };
    for (int key :{0, 1, 0, 2, 1, 2, 0}) {
        Mesh expected = input, caller = input;
        expected.cells += key;
        caller.cells += key;
        bool a = false, b = false;
        copied.Process(expected, 5, false, false, compute, &a);
        const auto view = borrowed.ProcessView(caller, 5, false, false, compute, &b);
        CHECK(a == b && projectedCacheSameMesh(view.Get(), expected) && borrowed.EntryCount() == copied.EntryCount());
        // Legacy Process must still produce an independently editable output.
        Mesh own = input;
        own.cells += key;
        borrowed.Process(own, 5, false, false, compute, &b);
        CHECK(b && projectedCacheSameMesh(own, expected));
        own.vertices[0].world.x = -100;
        caller = input;
        caller.cells += key;
        const auto again = borrowed.ProcessView(caller, 5, false, false, compute, &b);
        CHECK(b && projectedCacheSameMesh(again.Get(), expected));
    }
    borrowed.Clear();
    CHECK(borrowed.EntryCount() == 0 && borrowed.CachedBytes() == 0);
    Mesh caller = input;
    bool reused = true;
    borrowed.ProcessView(caller, 5, false, false, compute, &reused);
    CHECK(!reused);
}

TEST(water_prepared_view_pins_output_across_clear_and_fifo_eviction)
{
    using namespace WaterProjectedMesh;
    const Mesh input = preparedCacheInput();
    WaterPreparedMeshCache::Cache cache(32u * 1024u * 1024u, 1);
    auto compute = [](Mesh &mesh) {
        mesh.vertices[0].world.x += mesh.cells;
    };
    Mesh expected = input;
    compute(expected);
    Mesh caller = input;
    const auto pinned = cache.ProcessView(caller, 5, false, false, compute);
    const Vertex *address = pinned->vertices.data();
    cache.Clear();
    CHECK(cache.CachedBytes() == 0 && cache.EntryCount() == 0);
    CHECK(pinned->vertices.data() == address && projectedCacheSameMesh(pinned.Get(), expected));
    for (int key :{1, 2, 3}) {
        caller = input;
        caller.cells += key;
        const auto current = cache.ProcessView(caller, 5, false, false, compute);
        CHECK(cache.EntryCount() == 1 && pinned->vertices.data() == address);
        CHECK(projectedCacheSameMesh(pinned.Get(), expected));
    }
    // A copied View pins the same immutable buffers without a deep mesh copy.
    const auto second = pinned;
    CHECK(&second.Get() == &pinned.Get());
    cache.Clear();
    CHECK(projectedCacheSameMesh(second.Get(), expected));
}
