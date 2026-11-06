// Device-free source snapshots and spatial bins for shared hazard clouds.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloudSharedModel.h"
#include "Platform/HazardCloudPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace HazardCloudShared {
namespace { constexpr float Displacement = 18; }

bool Model::setSources(const std::vector<Source> &sources, const Bounds &bounds)
{
	const float coordinates[] = {bounds.minX, bounds.minY, bounds.minZ,
		bounds.maxX, bounds.maxY, bounds.maxZ};
	for (float value : coordinates) if (!std::isfinite(value)) return false;
	if (!(bounds.maxX > bounds.minX && bounds.maxY > bounds.minY && bounds.maxZ > bounds.minZ)
		|| !std::isfinite(bounds.maxX - bounds.minX) || !std::isfinite(bounds.maxY - bounds.minY)
		|| !std::isfinite(bounds.maxZ - bounds.minZ)
		|| sources.size() > (std::numeric_limits<std::uint32_t>::max)()) return false;
	for (std::size_t i = 0; i < sources.size(); ++i) {
		const auto &source = sources[i];
		const auto &field = source.field;
		const float finite[] = {field.palette, field.release, source.sourceAge, source.sourceTravel,
			source.dissipation, source.seed, source.born, source.expiry};
		for (float value : finite) if (!std::isfinite(value)) return false;
		if (source.id != i || field.palette < 0 || field.palette > 3
			|| field.palette != std::floor(field.palette)
			|| !HazardCloudPolicy::validField(field.ground.x, field.ground.y, field.ground.z,
				field.radius, field.height, field.density, field.formation, field.fringe, int(field.palette))
			|| field.release < 0 || field.release > 1 || source.sourceAge < 0 || source.sourceTravel < 0
			|| source.dissipation < 0 || source.dissipation > 1 || source.expiry < source.born)
			return false;
	}
	values = sources;
	domain = bounds;
	return true;
}

Grid Model::buildBins(float cellSize) const
{
	Grid grid;
	if (!std::isfinite(cellSize) || cellSize <= 0
		|| domain.maxX <= domain.minX || domain.maxY <= domain.minY) return grid;
	grid.minX = float(std::floor(double(domain.minX) / cellSize) * cellSize);
	grid.minY = float(std::floor(double(domain.minY) / cellSize) * cellSize);
	const double columns = std::floor((double(domain.maxX) - grid.minX) / cellSize) + 1;
	const double rows = std::floor((double(domain.maxY) - grid.minY) / cellSize) + 1;
	const auto maxIndex = (std::numeric_limits<std::uint32_t>::max)();
	if (!std::isfinite(grid.minX) || !std::isfinite(grid.minY) || columns < 1 || rows < 1
		|| columns * rows >= maxIndex) return {};
	grid.cellSize = cellSize;
	grid.width = static_cast<unsigned>(columns);
	grid.height = static_cast<unsigned>(rows);
	const std::size_t cells = std::size_t(grid.width) * grid.height;
	grid.offsets.assign(cells + 1, 0);
	const auto visit = [&](const Source &source, auto emit) {
		const float reach = source.field.radius + source.field.fringe + Displacement;
		const auto cell = [cellSize](double point, float origin, unsigned count) {
			return static_cast<unsigned>(std::clamp(std::floor((point - origin) / cellSize), 0.0, double(count - 1)));
		};
		const unsigned loX = cell(double(source.field.ground.x) - reach, grid.minX, grid.width);
		const unsigned hiX = cell(double(source.field.ground.x) + reach, grid.minX, grid.width);
		const unsigned loY = cell(double(source.field.ground.y) - reach, grid.minY, grid.height);
		const unsigned hiY = cell(double(source.field.ground.y) + reach, grid.minY, grid.height);
		for (unsigned y = loY; y <= hiY; ++y)
			for (unsigned x = loX; x <= hiX; ++x) emit(std::size_t(y) * grid.width + x);
	};
	std::uint64_t total = 0;
	for (const Source &source : values) if (source.active) {
		visit(source, [&](std::size_t cell) { ++grid.offsets[cell + 1]; ++total; });
		if (total > maxIndex) return {};
	}
	for (std::size_t cell = 1; cell <= cells; ++cell) grid.offsets[cell] += grid.offsets[cell - 1];
	grid.indices.resize(static_cast<std::size_t>(total));
	std::vector<std::uint32_t> cursor(grid.offsets.begin(), grid.offsets.end() - 1);
	for (const Source &source : values) if (source.active)
		visit(source, [&](std::size_t cell) { grid.indices[cursor[cell]++] = source.id; });
	return grid;
}

} // namespace HazardCloudShared
