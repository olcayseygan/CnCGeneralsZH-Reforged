// Device-free hazard cloud source snapshots and uncapped spatial bins.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "Platform/HazardCloud.h"
#include <cstdint>
#include <vector>

namespace HazardCloudShared {

struct Source {
	std::uint32_t id = 0; // Stable vector index, including before birth and after expiry.
	HazardCloud::FieldState field = {};
	float sourceAge = 0, sourceTravel = 0, dissipation = 0, seed = 0;
	float born = 0, expiry = 0; // Absolute source lifetime in seconds.
	bool active = false;
	bool terrain = true; // Sample the shared ground grid; false keeps this source on its plane.
};

struct Bounds {
	float minX = 0, minY = 0, maxX = 0, maxY = 0, minZ = 0, maxZ = 0;
};

// Conservative XY support bins. Cell (x,y) owns indices[offsets[y*width+x]..
// offsets[y*width+x+1]); offsets has width*height+1 entries. No per-cell cap.
// The origin is anchored to integer multiples of cellSize in world space.
struct Grid {
	float minX = 0, minY = 0, cellSize = 0;
	unsigned width = 0, height = 0;
	std::vector<std::uint32_t> offsets, indices;
	bool valid() const { return width && height && offsets.size() == std::size_t(width) * height + 1; }
};

class Model {
public:
	// Copies a production snapshot transactionally. IDs must equal their vector
	// indices; invalid source data or a collapsed/nonfinite domain leaves state intact.
	bool setSources(const std::vector<Source> &sources, const Bounds &bounds);
	const std::vector<Source> &sources() const { return values; }
	const Bounds &bounds() const { return domain; }
	// Invalid cell size or unrepresentable 32-bit GPU indices return an empty grid.
	Grid buildBins(float cellSize) const;

private:
	std::vector<Source> values;
	Bounds domain;
};

} // namespace HazardCloudShared
