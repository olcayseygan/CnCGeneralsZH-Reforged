/* Render-only rejection of water triangles proven to be entirely below terrain. */
#pragma once

#include "Platform/WaterProjectedMesh.h"
#include <deque>
#include <limits>

namespace WaterTerrainCulling
{
constexpr size_t MaxTriangleSamples = 1024;
constexpr size_t MaxMeshSamples = 4u * 1024u * 1024u;

// Shared by loading preparation and the live proof, including float displacement
// rounding. Never replace this multiplication predicate with division/floor.
inline bool ProofCeiling(double heightScale, double level, double verticalBound, double &ceiling)
{
    if (!std::isfinite(heightScale) || heightScale <= 0 || !std::isfinite(level) ||
        !std::isfinite(verticalBound) || verticalBound < 0)
        return false;
    const double top = level + verticalBound;
    if (!std::isfinite(top) || !std::isfinite(255 * heightScale))
        return false;
    const double epsilon = 8 * std::numeric_limits<float>::epsilon();
    ceiling = top + epsilon * (std::max)(1.0, (std::max)(std::abs(top), 255 * heightScale));
    return std::isfinite(ceiling);
}

// The owner supplies an immutable height snapshot and clears this with its terrain
// caches when bytes change. Equal byte cutoffs share a table across water levels.
// At most eight (2049 * 2049 * sizeof(uint32_t)) tables can be retained.
class WetNodeCache
{
    struct Entry {
        int cutoff;
        std::vector<uint32_t> prefix;
    };
public:
    static constexpr size_t MaxEntries = 8;
    class Query
    {
        friend class WetNodeCache;
        WetNodeCache *owner = nullptr;
        Entry *entry = nullptr;
        int cutoff = 0;
    public:
        bool HasWet(int firstX, int lastX, int firstY, int lastY, bool &wet);
    };
    explicit WetNodeCache(size_t maxEntries = MaxEntries)
        : maxEntries_((std::min)(maxEntries, MaxEntries)) {}

    // Consume the query before the next Prepare/Clear; eviction may replace it.
    Query Prepare(const uint8_t *heights, int width, int height, double heightScale, double ceiling)
    {
        Query query;
        if (!maxEntries_ || !heights || width < 2 || height < 2 || width > 2048 || height > 2048 ||
            !std::isfinite(heightScale) || heightScale <= 0 || !std::isfinite(ceiling))
            return query;
        if (heights != heights_ || width != width_ || height != height_) {
            Clear();
            heights_ = heights;
            width_ = width;
            height_ = height;
        }
        // Use the scalar predicate itself: division/floor can round a boundary
        // differently from the multiplication used by the original proof.
        int cutoff = -1;
        while (cutoff < 255 && double(cutoff + 1) * heightScale <= ceiling)
            ++cutoff;
        query.owner = this;
        query.cutoff = cutoff;
        if (cutoff < 0 || cutoff == 255)
            return query;
        for (Entry &entry : entries_)
            if (entry.cutoff == cutoff) {
                query.entry = &entry;
                return query;
            }
        if (entries_.size() == maxEntries_) {
            bytes_ -= entries_.front().prefix.capacity() * sizeof(uint32_t);
            entries_.pop_front();
        }
        entries_.push_back(Entry{cutoff, {}});
        query.entry = &entries_.back();
        return query;
    }
    // Build immutable map data during loading without counting a render query.
    // Trivial all-wet/all-dry cutoffs need no prefix allocation.
    bool Prewarm(const uint8_t *heights, int width, int height, double heightScale, double ceiling)
    {
        Query query = Prepare(heights, width, height, heightScale, ceiling);
        if (!query.owner)
            return false;
        if (query.entry && query.entry->prefix.empty())
            Build(*query.entry);
        return true;
    }
    void Clear()
    {
        entries_.clear();
        heights_ = nullptr;
        width_ = height_ = 0;
        bytes_ = builds_ = queryHits_ = 0;
    }
    size_t CachedBytes() const {
        return bytes_;
    }
    size_t EntryCount() const {
        return entries_.size();
    }
    size_t BuildCount() const {
        return builds_;
    }
    size_t QueryHits() const {
        return queryHits_;
    }
private:
    void Build(Entry &entry)
    {
        const size_t stride = size_t(width_) + 1;
        entry.prefix.assign(stride * (size_t(height_) + 1), 0);
        for (int y = 0; y < height_; ++y) {
            uint32_t row = 0;
            for (int x = 0; x < width_; ++x) {
                row += heights_[size_t(y) * width_ + x] <= entry.cutoff;
                entry.prefix[size_t(y + 1) * stride + x + 1] =
                    entry.prefix[size_t(y) * stride + x + 1] + row;
            }
        }
        bytes_ += entry.prefix.capacity() * sizeof(uint32_t);
        ++builds_;
    }
    const uint8_t *heights_ = nullptr;
    int width_ = 0, height_ = 0;
    size_t maxEntries_, bytes_ = 0, builds_ = 0, queryHits_ = 0;
    std::deque<Entry> entries_;
};

inline bool WetNodeCache::Query::HasWet(int firstX, int lastX, int firstY, int lastY, bool &wet)
{
    if (!owner || firstX < 0 || firstY < 0 || lastX < firstX || lastY < firstY ||
        lastX >= owner->width_ || lastY >= owner->height_)
        return false;
    ++owner->queryHits_;
    if (cutoff < 0 || cutoff == 255) {
        wet = cutoff == 255;
        return true;
    }
    if (entry->prefix.empty())
        owner->Build(*entry);
    const size_t stride = size_t(owner->width_) + 1;
    const auto &p = entry->prefix;
    const uint32_t right = p[size_t(lastY + 1) * stride + lastX + 1] - p[size_t(firstY) * stride + lastX + 1];
    const uint32_t left = p[size_t(lastY + 1) * stride + firstX] - p[size_t(firstY) * stride + firstX];
    wet = right != left;
    return true;
}

// The terrain renderer triangulates each heightmap cell. Its surface can be above
// the bilinear sample on a saddle, so require clearance over every cell corner.
// Callers can use this to disable terrain occlusion proofs from below the ground.
inline bool CameraAboveTerrain(
    const uint8_t *heights, int width, int height, int border,
    double spacing, double heightScale,
    double cameraX, double cameraY, double cameraZ)
{
    if (!heights || width < 2 || height < 2 || width > 2048 || height > 2048 ||
        border < 0 || border > (width - 2) / 2 || border > (height - 2) / 2 ||
        !std::isfinite(spacing) || spacing <= 0 ||
        !std::isfinite(heightScale) || heightScale <= 0 ||
        !std::isfinite(cameraX) || !std::isfinite(cameraY) || !std::isfinite(cameraZ))
    {
        return false;
    }

    const double x = cameraX / spacing + border;
    const double y = cameraY / spacing + border;
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < 0 || y < 0 || x > width - 1 || y > height - 1)
    {
        return false;
    }

    const int x0 = int(x);
    const int y0 = int(y);
    const int x1 = (std::min)(x0 + 1, width - 1);
    const int y1 = (std::min)(y0 + 1, height - 1);
    const uint8_t maximum = (std::max)(
        (std::max)(heights[size_t(y0) * width + x0], heights[size_t(y0) * width + x1]),
        (std::max)(heights[size_t(y1) * width + x0], heights[size_t(y1) * width + x1]));
    const double ceiling = maximum * heightScale;
    const double epsilon = 8 * std::numeric_limits<float>::epsilon() *
        (std::max)(1.0, (std::max)(std::abs(ceiling), std::abs(cameraZ)));

    return std::isfinite(ceiling) && cameraZ > ceiling + epsilon;
}

inline bool ValidMesh(const WaterProjectedMesh::Mesh &mesh)
{
    if (mesh.vertices.size() > 65536 || mesh.indices.size() % 3)
    {
        return false;
    }

    for (const auto &vertex : mesh.vertices)
    {
        if (!std::isfinite(vertex.world.x) || !std::isfinite(vertex.world.y))
        {
            return false;
        }
    }

    for (uint16_t index : mesh.indices)
    {
        if (index >= mesh.vertices.size())
        {
            return false;
        }
    }

    return true;
}

// All references in collars/transitions are world coordinates or authored edge IDs,
// not mesh vertex indices. Retain those tables and each surviving vertex verbatim.
inline void Install(WaterProjectedMesh::Mesh &mesh, std::vector<uint16_t> &indices)
{
    std::vector<int> remap(mesh.vertices.size(), -1);
    for (uint16_t index : indices)
    {
        remap[index] = 0;
    }

    size_t retained = 0;
    for (size_t i = 0; i < remap.size(); ++i)
    {
        if (remap[i] == 0)
        {
            remap[i] = int(retained);
            // Stable old-index order means the destination never passes unread input.
            if (retained != i)
                mesh.vertices[retained] = mesh.vertices[i];
            ++retained;
        }
    }

    for (uint16_t &index : indices)
    {
        index = uint16_t(remap[index]);
    }

    mesh.vertices.resize(retained);
    mesh.indices.swap(indices);
}

inline bool CompactVertices(WaterProjectedMesh::Mesh &mesh)
{
    if (!ValidMesh(mesh))
    {
        return false;
    }

    Install(mesh, mesh.indices);
    return true;
}

// A bilinear terrain cell and either triangulation of that cell never fall below
// their lowest corner. Inspect ALL corners of ALL cells overlapped by the displaced
// triangle's bounds; sampling only the triangle endpoints can hide an interior pond.
// The supplied displacement bounds must include every enabled surface effect.
// Unknown/outside terrain and queries exceeding the work budget retain the triangle.
// Invalid inputs leave the entire mesh unchanged. No partial geometry is removed.
inline bool CullDryTriangles(
    WaterProjectedMesh::Mesh &mesh, const uint8_t *heights,
    int width, int height, int border, double spacing, double heightScale,
    double level, double verticalBound, double horizontalBound, WetNodeCache *cache = nullptr)
{
    if (!heights || width < 2 || height < 2 || width > 2048 || height > 2048 ||
        border < 0 || border > (width - 2) / 2 || border > (height - 2) / 2 ||
        !std::isfinite(spacing) || spacing <= 0 ||
        !std::isfinite(heightScale) || heightScale <= 0 || !std::isfinite(level) ||
        !std::isfinite(verticalBound) || verticalBound < 0 ||
        !std::isfinite(horizontalBound) || horizontalBound < 0 || !ValidMesh(mesh))
    {
        return false;
    }

    // Vertices and shader displacement ultimately use float; include their rounding
    // before converting bounds to grid cells and comparing terrain/water elevations.
    const double epsilon = 8 * std::numeric_limits<float>::epsilon();
    double ceiling;
    if (!ProofCeiling(heightScale, level, verticalBound, ceiling))
    {
        return false;
    }

    auto proof = cache ? cache->Prepare(heights, width, height, heightScale, ceiling) : WetNodeCache::Query();

    std::vector<uint16_t> indices;
    indices.reserve(mesh.indices.size());
    size_t work = 0;

    for (size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3)
    {
        const auto a = mesh.vertices[mesh.indices[triangle]].world;
        const auto b = mesh.vertices[mesh.indices[triangle + 1]].world;
        const auto c = mesh.vertices[mesh.indices[triangle + 2]].world;
        double minX = (std::min)(a.x, (std::min)(b.x, c.x));
        double maxX = (std::max)(a.x, (std::max)(b.x, c.x));
        double minY = (std::min)(a.y, (std::min)(b.y, c.y));
        double maxY = (std::max)(a.y, (std::max)(b.y, c.y));
        const double magnitude = (std::max)(1.0, (std::max)(
            (std::max)(std::abs(minX), std::abs(maxX)),
            (std::max)(std::abs(minY), std::abs(maxY))));
        const double margin = horizontalBound + epsilon * (magnitude + horizontalBound);

        minX = (minX - margin) / spacing + border;
        maxX = (maxX + margin) / spacing + border;
        minY = (minY - margin) / spacing + border;
        maxY = (maxY + margin) / spacing + border;

        bool dry = false;
        if (std::isfinite(minX) && std::isfinite(maxX) &&
            std::isfinite(minY) && std::isfinite(maxY) &&
            minX >= 0 && minY >= 0 && maxX <= width - 1 && maxY <= height - 1)
        {
            const int firstX = int(std::floor(minX));
            const int lastX = int(std::ceil(maxX));
            const int firstY = int(std::floor(minY));
            const int lastY = int(std::ceil(maxY));
            const size_t samples = size_t(lastX - firstX + 1) * size_t(lastY - firstY + 1);

            if (samples <= MaxTriangleSamples && samples <= MaxMeshSamples - work)
            {
                work += samples;
                dry = true;
                // Most ocean triangles encounter wet terrain immediately. Keep
                // that scalar early exit before lazily building/querying a table.
                bool wet = false;
                if (cache && heights[size_t(firstY) * width + firstX] * heightScale <= ceiling)
                    dry = false;
                else if (cache && proof.HasWet(firstX, lastX, firstY, lastY, wet))
                    dry = !wet;
                else {
                    for (int y = firstY; y <= lastY && dry; ++y) {
                        for (int x = firstX; x <= lastX; ++x) {
                            if (heights[size_t(y) * width + x] * heightScale <= ceiling) {
                                dry = false;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (!dry)
        {
            indices.insert(
                indices.end(), mesh.indices.begin() + triangle, mesh.indices.begin() + triangle + 3);
        }
    }

    Install(mesh, indices);
    return true;
}
}
