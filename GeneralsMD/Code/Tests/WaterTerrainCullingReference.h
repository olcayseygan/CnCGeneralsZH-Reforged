// Frozen HEAD 3c1f8285 terrain culling oracle; only namespace renamed.
/* Render-only rejection of water triangles proven to be entirely below terrain. */
#pragma once

#include "Platform/WaterProjectedMesh.h"
#include <limits>

namespace WaterTerrainCullingFrozenReference
{
constexpr size_t MaxTriangleSamples = 1024;
constexpr size_t MaxMeshSamples = 4u * 1024u * 1024u;

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

    std::vector<WaterProjectedMesh::Vertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (size_t i = 0; i < remap.size(); ++i)
    {
        if (remap[i] == 0)
        {
            remap[i] = int(vertices.size());
            vertices.push_back(mesh.vertices[i]);
        }
    }

    for (uint16_t &index : indices)
    {
        index = uint16_t(remap[index]);
    }

    mesh.vertices.swap(vertices);
    mesh.indices.swap(indices);
}

inline bool CompactVertices(WaterProjectedMesh::Mesh &mesh)
{
    if (!ValidMesh(mesh))
    {
        return false;
    }

    std::vector<uint16_t> indices = mesh.indices;
    Install(mesh, indices);
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
    double level, double verticalBound, double horizontalBound)
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

    const double top = level + verticalBound;
    if (!std::isfinite(top) || !std::isfinite(255 * heightScale))
    {
        return false;
    }

    // Vertices and shader displacement ultimately use float; include their rounding
    // before converting bounds to grid cells and comparing terrain/water elevations.
    const double epsilon = 8 * std::numeric_limits<float>::epsilon();
    const double ceiling = top + epsilon *
        (std::max)(1.0, (std::max)(std::abs(top), 255 * heightScale));
    if (!std::isfinite(ceiling))
    {
        return false;
    }

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

                for (int y = firstY; y <= lastY && dry; ++y)
                {
                    for (int x = firstX; x <= lastX; ++x)
                    {
                        if (heights[size_t(y) * width + x] * heightScale <= ceiling)
                        {
                            dry = false;
                            break;
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
