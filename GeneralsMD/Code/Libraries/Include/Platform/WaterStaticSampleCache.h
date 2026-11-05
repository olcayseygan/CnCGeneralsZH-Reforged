/* Exact static field reuse for consecutive water draws. Animated inputs stay outside this cache. */
#pragma once

#include "WaterProjectedMesh.h"
#include "WaterSurfaceModel.h"

namespace WaterStaticSampleCache {
// Preserve WaterTerrainDepth's clamp(lo, value, hi) argument order and
// WorldHeightMapData's flattened-index bounds, including samples beyond an edge.
// The caller owns a width*height immutable draw snapshot.
inline float TerrainDepth(const uint8_t *heights,int width,int height,int border,
    float spacing,float heightScale,float x,float y,float level) {
    if (!heights || width<2 || height<2) return 0.0f;
    const float gx=x/spacing+border,gy=y/spacing+border;
    const float mx=0.0f<gx?gx:0.0f,my=0.0f<gy?gy:0.0f;
    const int ix=int(mx),iy=int(my);const float fx=mx-ix,fy=my-iy;
    const auto at=[&](int column,int row) {
        const int64_t index=int64_t(row)*width+column;
        return index>=0 && index<int64_t(width)*height?heights[size_t(index)]:uint8_t(0);
    };
    const float a=at(ix,iy)*(1-fx)+at(ix+1,iy)*fx;
    const float b=at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx;
    return level-(a*(1-fy)+b*fy)*heightScale;
}

struct Sample {
    WaterSurfaceModel::Sample offshore;
    float exposure;
    float terrainDepth;
    float terrainDamping;
};

// A stable projected mesh visits the same coordinates in the same order. A sequence
// cache avoids a hash lookup per vertex; changed order/coordinates are ordinary misses.
// The owner must Clear when terrain, optical polygons, or field interpretation changes.
class Cache {
    struct Entry { WaterProjectedMesh::WorldKey key; Sample value; };
    std::vector<Entry> entries;
    size_t cursor = 0;
    const size_t limit;
public:
    explicit Cache(size_t maximum = WaterSurfaceModel::VertexBudget) : limit(maximum) {}
    void BeginRender() { cursor = 0; }
    void Clear() { entries.clear(); cursor = 0; }
    size_t Size() const { return entries.size(); }

    template<class Compute>
    Sample Get(float x, float y, float level, const Compute &compute, bool *reused = nullptr) {
        if (reused) *reused = false;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(level) || cursor >= limit)
            return compute();
        const auto key = WaterProjectedMesh::SampleKey(x, y, level);
        const size_t index = cursor++;
        if (index < entries.size() && entries[index].key == key) {
            if (reused) *reused = true;
            return entries[index].value;
        }
        const Sample value = compute();
        if (index < entries.size()) entries[index] = {key, value};
        else entries.push_back({key, value});
        return value;
    }
};
}
