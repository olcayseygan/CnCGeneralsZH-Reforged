#pragma once
#include "WaterShoreHistoryReference.h"
#include "Platform/WaterShoreHistory.h"

namespace WaterShorePreparedTest {
namespace Old = WaterShoreHistoryFrozenReference;
namespace New = WaterShoreHistory;

struct Source {
    std::vector<double> times;
    std::vector<uint32_t> indices;
    template <class Cell> void operator()(double time, const Cell *cells, size_t count, double *rates) {
        times.push_back(time);
        for (size_t i = 0; i < count; ++i) {
            indices.push_back(cells[i].sourceIndex);
            rates[i] = (1 + cells[i].sourceIndex % 7) * (.4 + .1 * std::sin(time * 1.7 + cells[i].x * .2
                + cells[i].y * .4));
        }
    }
};

inline std::vector<New::Cell> Cells() {
    std::vector<New::Cell> out;
    for (double level :{8., 11.})
        for (int y = -2; y <= 2; ++y)
            for (int x = -2; x <= 2; ++x)
                if (x || y)
                    out.push_back({x, y, level, uint32_t(out.size()), uint8_t((x == 1 ? 0 : 1) | (y == 1 ? 0 : 2))});
    return out;
}

inline std::vector<Old::Cell> OldCells(const std::vector<New::Cell> &cells) {
    std::vector<Old::Cell> out;
    for (const auto &c : cells)
        out.push_back({c.x, c.y, c.level, c.sourceIndex, c.connections});
    return out;
}

template <class A, class B> inline void States(const A &a, const B &b) {
    CHECK(a.size() == b.size());
    if (a.size() != b.size())
        return;
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(projectedCacheSameDouble(a[i].density, b[i].density));
        CHECK(projectedCacheSameDouble(a[i].ageMoment, b[i].ageMoment));
    }
}
struct Fixture {
    Old::History old;
    New::History normal, prepared;
    Source oldSource, normalSource, preparedSource;
    void Check() {
        CHECK(projectedCacheSameDouble(old.Time(), normal.Time()));
        CHECK(projectedCacheSameDouble(old.Time(), prepared.Time()));
        CHECK(projectedCacheSameDouble(old.CommittedTime(), normal.CommittedTime()));
        CHECK(projectedCacheSameDouble(old.CommittedTime(), prepared.CommittedTime()));
        States(old.Committed(), normal.Committed());
        States(old.Committed(), prepared.Committed());
        States(old.Preview(oldSource), normal.Preview(normalSource));
        States(old.Preview(oldSource), prepared.Preview(preparedSource));
        CHECK(oldSource.times == normalSource.times && oldSource.times == preparedSource.times);
        CHECK(oldSource.indices == normalSource.indices && oldSource.indices == preparedSource.indices);
        const auto &a = old.Domain();
        const auto &b = normal.Domain();
        const auto &c = prepared.Domain();
        CHECK(a.Cells().size() == b.Cells().size() && a.Cells().size() == c.Cells().size());
        if (a.Cells().size() == c.Cells().size())
            for (size_t i = 0; i < a.Cells().size(); ++i) {
                CHECK(a.Cells()[i].x == c.Cells()[i].x && a.Cells()[i].y == c.Cells()[i].y &&
                    projectedCacheSameDouble(a.Cells()[i].level, c.Cells()[i].level) &&
                    a.Cells()[i].sourceIndex == c.Cells()[i].sourceIndex && a.Cells()[i].connections == c.Cells()[i].connections);
            }
        CHECK(a.Edges().size() == b.Edges().size() && a.Edges().size() == c.Edges().size());
        if (a.Edges().size() == c.Edges().size())
            for (size_t i = 0; i < a.Edges().size(); ++i)
                CHECK(a.Edges()[i].a == c.Edges()[i].a && a.Edges()[i].b == c.Edges()[i].b);
        CHECK(a.RasterTiles().size() == b.RasterTiles().size() && a.RasterTiles().size() == c.RasterTiles().size());
        if (a.RasterTiles().size() == c.RasterTiles().size())
            for (size_t i = 0; i < a.RasterTiles().size(); ++i) {
                const auto &x = a.RasterTiles()[i];
                const auto &y = c.RasterTiles()[i];
                CHECK(x.x == y.x && x.y == y.y && projectedCacheSameDouble(x.level, y.level) && x.nodes == y.nodes);
            }
    }
    void Install(const std::vector<New::Cell> &cells, uint64_t revision, double time,
        New::Parameters settings = New::Parameters(),
        New::Topology *prebuilt = nullptr) {
        const New::Grid grid{-13, 17, 5};
        const Old::Grid oldGrid{grid.originX, grid.originY, grid.spacing};
        const Old::Parameters oldSettings{settings.lifetime, settings.diffusivity, settings.maximumRate};
        New::Topology topology;
        New::BuildResult built;
        if (prebuilt) {
            topology = std::move(*prebuilt);
            built = New::BuildResult::Ready;
        }
        else
            built = topology.Build(grid, cells); // No history timestamp or source sample is touched here.
        const auto expected = old.Install(oldGrid, OldCells(cells), revision, time, oldSettings);
        const auto ordinary = normal.Install(grid, cells, revision, time, settings);
        const auto result = built == New::BuildResult::Ready ? prepared.InstallPrepared(std::move(topology),
            revision, time, settings) : built;
        CHECK(unsigned(expected) == unsigned(ordinary) && unsigned(expected) == unsigned(result));
        if (built == New::BuildResult::Ready &&result == New::BuildResult::Invalid)
            CHECK(topology.Cells().size() == cells.size()); // Invalid activation must not consume its candidate.
        CHECK(old.ValidFor(revision) == normal.ValidFor(revision)
            && old.ValidFor(revision) == prepared.ValidFor(revision));
        Check();
    }
    void Advance(double time) {
        const auto a = old.Advance(time, oldSource);
        const auto b = normal.Advance(time, normalSource);
        const auto c = prepared.Advance(time, preparedSource);
        CHECK(unsigned(a) == unsigned(b) && unsigned(a) == unsigned(c));
        Check();
    }
    void Clear() {
        old.Clear();
        normal.Clear();
        prepared.Clear();
        Check();
    }
};

inline void Check() {
    for (double start :{-3.271, 0., 12345.678}) {
        Fixture f;
        auto cells = Cells();
        // Static construction precedes the first nonzero live timestamp. It must
        // neither install history nor sample any temporal source during loading.
        New::Topology pending;
        CHECK(pending.Build({-13, 17, 5}, cells) == New::BuildResult::Ready);
        CHECK(!f.prepared.ValidFor(7) && f.prepared.Time() == 0 && f.prepared.Committed().empty());
        CHECK(f.preparedSource.times.empty());
        f.Install(cells, 7, start, New::Parameters(), &pending);
        f.Advance(start);
        for (double delta :{.037, .1, .117, .267, .8, 4.3, 4.3, 4.1, 4.16, 4.6})
            f.Advance(start + delta);
        std::reverse(cells.begin(), cells.end());
        for (auto &cell : cells)
            cell.sourceIndex += 101;
        // Same geometry retains both moments AND the original epoch, even when
        // the caller supplies a different timestamp and reordered source indices.
        f.Install(cells, 7, start + 50);
        f.Advance(start + 4.7);
        f.Install(cells, 8, start + 6.123);
        f.Advance(start + 6.17);
        New::Parameters changed;
        changed.lifetime = 2.4;
        changed.diffusivity = .5;
        changed.maximumRate = 7;
        f.Install(cells, 8, start + 7.29, changed);
        f.Advance(start + 7.347);
        f.Install(cells, 9, NAN); // Failure leaves the previous history untouched.
        f.Install(cells, 9, INFINITY);
        New::Parameters invalid;
        invalid.lifetime = 0;
        f.Install(cells, 9, start + 8, invalid);
        invalid = New::Parameters();
        invalid.diffusivity = 1e9;
        f.Install(cells, 9, start + 8, invalid);
        auto duplicate = cells;
        duplicate.push_back(cells.front());
        f.Install(duplicate, 9, start + 8);
        auto excessive = cells;
        excessive.resize(New::MaxCells + 1);
        f.Install(excessive, 9, start + 8);
        f.Clear();
        CHECK(!f.prepared.ValidFor(8));
        // Invalidation discards the prior live phase; rebuilding the same map
        // still starts at its next ORIGINAL live timestamp, never loading time.
        f.Install(cells, 10, start + 10.731);
        f.Advance(start + 10.768);
        f.Install({}, 11, start + 12.123);
        f.Advance(start + 12.16); // Successfully built empty topology.
    }
}
}
