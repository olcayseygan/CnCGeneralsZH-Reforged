#pragma once

#include "WaterWaveCageMeshReference.h"
#include "Platform/WaterWaveCageMesh.h"
#include <limits>
#include <utility>

namespace WaterWaveCageCopyTest {
using WaterProjectedMesh::Mesh;
using WaterProjectedMesh::Point;

inline Mesh Grid(int columns = 5, int rows = 4) {
    Mesh mesh;
    mesh.cells = columns * rows;
    for (int y = 0; y <= rows; ++y)
        for (int x = 0; x <= columns; ++x) {
            WaterProjectedMesh::Vertex v;
            v.world = {double(x), double(y)};
            v.edgeParameter = (x + y) * .03125;
            v.knot = x - y;
            v.lattice = y * (columns + 1) + x;
            mesh.vertices.push_back(v);
        }
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x) {
            const uint16_t a = uint16_t(y * (columns + 1) + x), b = uint16_t(a + 1);
            const uint16_t c = uint16_t(a + columns + 2), d = uint16_t(a + columns + 1);
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    WaterProjectedMesh::CentreTransition transition = {};
    transition.source[0] = {1, 2};
    transition.source[1] = {3, 4};
    transition.source[2] = {5, 6};
    transition.weight[0] = .25;
    transition.weight[1] = .5;
    transition.weight[2] = .25;
    transition.analytic = .75;
    mesh.transitions.push_back(transition);
    WaterProjectedMesh::BoundaryCollar collar;
    collar.count = 2;
    collar.fade = .625;
    collar.edge[0] = 1;
    collar.parameter[0] = .25;
    collar.weight[0] = .75;
    collar.edge[1] = 3;
    collar.parameter[1] = .5;
    collar.weight[1] = .25;
    mesh.collars.push_back(collar);
    return mesh;
}

template <class Depth, class Support>
inline bool Compare(const Mesh &input, const Depth &depth, const Support &support, size_t budget = 65535) {
    Mesh reference = input, actual = input;
    std::vector<Point> oldDepth, newDepth, oldSupport, newSupport;
    const bool expected = WaterWaveCageMeshFrozenReference::ClipRestWaterline(reference,
        [&](Point p) {
        oldDepth.push_back(p);
        return depth(p);
    },
        [&](Point p) {
        oldSupport.push_back(p);
        return support(p);
    }, budget);
    const bool result = WaterWaveCageMesh::ClipRestWaterline(actual,
        [&](Point p) {
        newDepth.push_back(p);
        return depth(p);
    },
        [&](Point p) {
        newSupport.push_back(p);
        return support(p);
    }, budget);
    CHECK(result == expected && projectedCacheSameMesh(reference, actual));
    if (!expected)
        CHECK(projectedCacheSameMesh(input, actual));
    // Preserve callback order and every bisection coordinate, including early failure.
    for (const auto pair :{std::make_pair(&oldDepth, &newDepth), std::make_pair(&oldSupport, &newSupport)}) {
        CHECK(pair.first->size() == pair.second->size());
        if (pair.first->size() != pair.second->size())
            continue;
        for (size_t i = 0; i < pair.first->size(); ++i) {
            CHECK(projectedCacheSameDouble((*pair.first)[i].x, (*pair.second)[i].x));
            CHECK(projectedCacheSameDouble((*pair.first)[i].y, (*pair.second)[i].y));
        }
    }
    return result;
}

inline void NoopsAndChains() {
	// Equal output alone does not prove copy elision. Wet no-ops must retain
	// both buffers, and clipping must append into available vertex capacity.
	Mesh stable=Grid();
	const auto *vertices=stable.vertices.data();
	const auto *indices=stable.indices.data();
	CHECK(WaterWaveCageMesh::ClipRestWaterline(stable,[](Point) { return 1.; },[](Point) { return true; }));
	CHECK(stable.vertices.data()==vertices && stable.indices.data()==indices);
	Mesh clipped=Grid();clipped.vertices.reserve(1000);
	vertices=clipped.vertices.data();
	CHECK(WaterWaveCageMesh::ClipRestWaterline(clipped,[](Point p) { return p.x-.37; },[](Point) { return true; }));
	CHECK(clipped.vertices.data()==vertices);
    const auto supported = [](Point) {
        return true;
    };
    Mesh mesh = Grid();
    CHECK(Compare(mesh, [](Point) {
        return 1.;
    }, supported));
    CHECK(Compare(mesh, [](Point) {
        return -0.;
    }, supported));
    CHECK(Compare(mesh, [](Point) {
        return -1.;
    }, supported));
    CHECK(Compare(mesh, [](Point p) {
        return p.x - 2.;
    }, [](Point) {
        return false;
    }));
    // Crossing propagation disables both owners and their connected chain.
    for (int protection = 0; protection < 3; ++protection) {
        Mesh chain = Grid(1, 1);
        if (protection == 0)
            chain.vertices[2].boundary = 2;
        if (protection == 1)
            chain.vertices[2].collar = 0;
        if (protection == 2)
            chain.vertices[2].transition = 0;
        CHECK(Compare(chain, [](Point p) {
            return p.x - .5;
        }, supported));
    }
    mesh.indices.clear();
    CHECK(Compare(mesh, [](Point) {
        return -1.;
    }, supported));
    Mesh empty;
    CHECK(Compare(empty, [](Point) {
        return 1.;
    }, supported));
}

inline void CrossingsAndBudgets() {
    const auto supported = [](Point) {
        return true;
    };
    for (int frame = 0; frame < 24; ++frame) {
        Mesh mesh = Grid(9, 7);
        if (frame % 3 == 1)
            for (size_t t = 0; t < mesh.indices.size(); t += 3)
                std::swap(mesh.indices[t], mesh.indices[t + 2]);
        if (frame % 4 == 0) {
            mesh.vertices[0].boundary = 0;
            mesh.vertices[19].collar = 0;
            mesh.vertices[58].transition = 0;
        }
        const auto depth = [&](Point p) {
            return p.x * p.x * .075 - p.y * .21 - 1.7 + frame * .037;
        };
        CHECK(Compare(mesh, depth, [&](Point p) {
            return frame % 2 == 0 || p.x + p.y > 2.;
        }));
    }
    const Mesh mesh = Grid(1, 1);
    for (size_t budget = 0; budget <= 9; ++budget)
        Compare(mesh, [](Point p) {
            return p.x - .37;
        }, supported, budget);
    CHECK(Compare(Grid(2, 1), [](Point p) {
        return p.x - 1.;
    }, supported)); // Exact zero endpoint.
    Mesh high = mesh;
    high.vertices.resize(65532, mesh.vertices[0]);
    CHECK(Compare(high, [](Point p) {
        return p.x - .37;
    }, supported));
    high.vertices.push_back(mesh.vertices[0]);
    CHECK(!Compare(high, [](Point p) {
        return p.x - .37;
    }, supported)); // Partial split rolls back.
    high.vertices.resize(65536, mesh.vertices[0]);
    CHECK(!Compare(high, [](Point) {
        return 1.;
    }, supported));
}

inline void InvalidRollback() {
    const auto supported = [](Point) {
        return true;
    };
    const Mesh base = Grid(1, 1);
    for (int failure = 0; failure < 6; ++failure) {
        Mesh mesh = base;
        if (failure == 0)
            mesh.indices.push_back(0);
        if (failure == 1)
            mesh.indices[1] = 65535;
        if (failure == 2)
            mesh.indices[1] = mesh.indices[0];
        if (failure == 3)
            mesh.vertices[0].world.x = std::numeric_limits<double>::quiet_NaN();
        if (failure == 4)
            mesh.vertices[0].world.y = std::numeric_limits<double>::infinity();
        if (failure == 5)
            mesh.indices.insert(mesh.indices.end(), {0, 1, 2});
        // Constant wet depth must still reject invalid edges before the no-op return.
        const bool result = Compare(mesh, [](Point) {
            return 1.;
        }, supported);
        CHECK(result == (failure == 5));
    }
    Mesh nonmanifold = base;
    nonmanifold.indices.insert(nonmanifold.indices.end(), {0, 1, 3});
    CHECK(!Compare(nonmanifold, [](Point p) {
        return p.x - .5;
    }, supported));
    CHECK(!Compare(base, [](Point) {
        return std::numeric_limits<double>::quiet_NaN();
    }, supported));
    CHECK(!Compare(base, [](Point p) {
        return p.x == .5 ? std::numeric_limits<double>::infinity() : p.x - .5;
    }, supported));
    CHECK(!Compare(base, [](Point) {
        return 1.;
    }, supported, 65536));
}
}
