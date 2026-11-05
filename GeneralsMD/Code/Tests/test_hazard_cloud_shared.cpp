// Device-free regression checks for shared cloud snapshots and uncapped bins.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "test_harness.h"
#include "Platform/HazardCloudShared.h"
#include "Platform/HazardCloudSharedModel.h"
#include "Platform/HazardCloudCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace S = HazardCloudShared;
namespace {
// Same 129/257-field supports as the fork's model checks, supplied through the
// production snapshot API instead of depending on preview lifecycle tools.
std::vector<S::Source> fields(unsigned count, bool overlap, float radius = 30)
{
	std::vector<S::Source> sources(count);
	for (unsigned i = 0; i < count; ++i) {
		auto &source = sources[i];
		source.id = i;
		source.field = {{overlap ? -37.f : -123.f + float(i % 11) * 64,
			overlap ? 123.f : -58.f + float(i / 11) * 64, 7}, radius, 18.61f, 1, float(i % 4), 1, 0, 14};
		source.active = true;
		source.born = 0;
		source.expiry = 24;
		source.sourceAge = 8;
	}
	return sources;
}
void sameSources(const std::vector<S::Source> &a, const std::vector<S::Source> &b)
{
	CHECK(a.size() == b.size());
	for (std::size_t i = 0; i < a.size(); ++i) {
		CHECK(a[i].id == b[i].id && a[i].active == b[i].active);
		CHECK(a[i].field.density == b[i].field.density);
		CHECK(a[i].field.formation == b[i].field.formation);
		CHECK(a[i].field.release == b[i].field.release);
		CHECK(a[i].sourceAge == b[i].sourceAge && a[i].sourceTravel == b[i].sourceTravel);
		CHECK(a[i].dissipation == b[i].dissipation && a[i].seed == b[i].seed);
	}
}

void uncappedBins()
{
	S::Model model;
	auto sources = fields(257, true, 140);
	const S::Bounds bounds = {-209, -49, 135, 295, 7, 60};
	CHECK(model.setSources(sources, bounds));
	const auto grid = model.buildBins(64);
	CHECK(grid.valid());
	CHECK(grid.minX == std::floor(model.bounds().minX / 64) * 64);
	CHECK(grid.minY == std::floor(model.bounds().minY / 64) * 64);
	const unsigned x = unsigned(std::floor((-37.f - grid.minX) / grid.cellSize));
	const unsigned y = unsigned(std::floor((123.f - grid.minY) / grid.cellSize));
	const std::size_t cell = std::size_t(y) * grid.width + x;
	CHECK(grid.offsets[cell + 1] - grid.offsets[cell] == 257);
	if (grid.offsets[cell + 1] - grid.offsets[cell] != 257) return;
	for (unsigned i = 0; i < 257; ++i) CHECK(grid.indices[grid.offsets[cell] + i] == i);
	CHECK(grid.offsets.back() == grid.indices.size());
	for (std::size_t c = 0; c + 1 < grid.offsets.size(); ++c)
		CHECK(grid.offsets[c + 1] - grid.offsets[c] == 257);
	for (auto &source : sources) source.active = false;
	CHECK(model.setSources(sources, bounds));
	const auto empty = model.buildBins(64);
	CHECK(empty.valid() && empty.indices.empty() && empty.offsets.back() == 0);
}

void binCoverageMatchesSupport()
{
	S::Model model;
	auto sources = fields(129, false);
	const S::Bounds bounds = {-185, -120, 579, 708, 7, 60};
	CHECK(model.setSources(sources, bounds));
	const auto grid = model.buildBins(48);
	CHECK(grid.valid());
	for (unsigned y = 0; y < grid.height; ++y) for (unsigned x = 0; x < grid.width; ++x) {
		const std::size_t cell = std::size_t(y) * grid.width + x;
		const float lowX = grid.minX + x * grid.cellSize, lowY = grid.minY + y * grid.cellSize;
		const auto first = grid.indices.begin() + grid.offsets[cell];
		const auto last = grid.indices.begin() + grid.offsets[cell + 1];
		CHECK(std::adjacent_find(first, last) == last);
		for (const auto &source : model.sources()) {
			const float reach = source.field.radius + source.field.fringe + 18;
			const bool overlaps = source.active && source.field.ground.x + reach >= lowX
				&& source.field.ground.x - reach < lowX + grid.cellSize
				&& source.field.ground.y + reach >= lowY
				&& source.field.ground.y - reach < lowY + grid.cellSize;
			CHECK((std::find(first, last, source.id) != last) == overlaps);
		}
	}
}

void productionSnapshotsAreTransactional()
{
	auto sources = fields(129, true);
	S::Bounds bounds = {-99, 61, 25, 185, 7, 60};
	S::Model model;
	CHECK(model.setSources(sources, bounds));
	const auto captured = model.sources();
	const auto full = model.buildBins(64);
	CHECK(full.valid() && !full.indices.empty());
	sources[0].active = false;
	sources[1].terrain = false;
	sources[1].field.ground.x += 400;
	sources[1].field.ground.z = 80;
	bounds.maxX += 400; bounds.maxZ += 100;
	CHECK(model.setSources(sources,bounds));
	CHECK(!model.sources()[0].active && !model.sources()[1].terrain);
	CHECK(model.sources()[1].field.ground.z == 80);
	CHECK(model.sources()[1].field.ground.x != captured[1].field.ground.x);
	const auto moved = model.buildBins(64);
	CHECK(moved.valid() && moved.width > full.width);
	CHECK(std::find(moved.indices.begin(),moved.indices.end(),0) == moved.indices.end());
	const auto stable = model.sources();
	for (unsigned kind = 0; kind < 12; ++kind) {
		auto bad = sources; auto badBounds = bounds;
		switch (kind) {
		case 0: bad[1].id = 0; break;
		case 1: bad[1].field.ground.x = (std::numeric_limits<float>::quiet_NaN)(); break;
		case 2: bad[1].sourceTravel = (std::numeric_limits<float>::infinity)(); break;
		case 3: bad[1].field.palette = 1.5f; break;
		case 4: bad[1].field.release = 2; break;
		case 5: bad[1].dissipation = -1; break;
		case 6: bad[1].expiry = bad[1].born - 1; break;
		case 7: badBounds.maxX = badBounds.minX; break;
		case 8: badBounds.minY = (std::numeric_limits<float>::quiet_NaN)(); break;
		case 9: bad[1].field.radius = -1; break;
		case 10: bad[1].seed = (std::numeric_limits<float>::infinity)(); break;
		case 11: bad[1].field.density = 2; break;
		}
		CHECK(!model.setSources(bad,badBounds));
		sameSources(stable,model.sources());
		CHECK(model.bounds().maxX == bounds.maxX);
	}
	CHECK(model.setSources({},bounds));
	CHECK(!model.buildBins(0).valid());
	CHECK(!model.buildBins((std::numeric_limits<float>::quiet_NaN)()).valid());
	const auto empty = model.buildBins(64);
	CHECK(empty.valid() && empty.indices.empty());
}

}

TEST(shared_cloud_bins_retain_257_overlapping_fields) { uncappedBins(); }
TEST(shared_cloud_bins_cover_all_129_distributed_supports) { binCoverageMatchesSupport(); }
TEST(shared_cloud_snapshots_are_transactional) { productionSnapshotsAreTransactional(); }

TEST(shared_cloud_capability_declines_without_a_backend)
{
	static_assert(std::is_abstract<S::Renderer>::value, "shared renderer is a backend interface");
	static_assert(std::has_virtual_destructor<S::Renderer>::value, "owned through the interface");
	CHECK(!S::Supported());
	CHECK(S::CreateRenderer() == nullptr);
}

TEST(hazard_cloud_embers_support_shared_terrain_without_changing_legacy_constants)
{
	const HazardCloud::FieldState field = {{40, 80, 12}, 40, 18.61f, 1, 1, 1, 0, 10};
	float matrix[16] = {}, viewport[4] = {0, 0, 1280, 800};
	HazardCloud::EmberConstants constants;
	const unsigned count = HazardCloud::emberConstants(field, 1, true, matrix, viewport, 3, constants);
	CHECK_EQ(constants.terrain[1], 1.f);
	CHECK_EQ(constants.terrain[2], field.radius + field.fringe + HazardCloud::MaximumDisplacement);
	const float domain[4] = {-128, 0, 384, 256};
	CHECK(HazardCloud::setEmberTerrainDomain(domain, constants));
	CHECK_EQ(constants.terrain[0], 1.f);
	CHECK_EQ(constants.terrain[1], -1.f);
	// The shared grid's world centre must sample its centre, independently of
	// the source's position and the old per-source atlas index.
	CHECK_EQ((128.f - constants.terrain[2]) * constants.scene[2], .5f);
	CHECK_EQ((128.f - constants.terrain[3]) * constants.scene[3], .5f);
	CHECK_EQ(constants.field[2], field.ground.z);
	CHECK_EQ(constants.scene[0], 3.f);
	CHECK_EQ(constants.scene[1], float(count));
	const HazardCloud::EmberConstants valid = constants;
	for (const std::array<float, 4> invalid : {
			std::array<float, 4>{{0, 0, 0, 1}}, {{0, 0, 1, -1}},
			{{0, 0, std::numeric_limits<float>::infinity(), 1}},
			{{0, 0, std::numeric_limits<float>::denorm_min(), 1}}}) {
		CHECK(!HazardCloud::setEmberTerrainDomain(invalid.data(), constants));
		CHECK(std::equal(constants.scene, constants.scene + 4, valid.scene));
		CHECK(std::equal(constants.terrain, constants.terrain + 4, valid.terrain));
	}
	CHECK(!HazardCloud::setEmberTerrainDomain(nullptr, constants));
}

