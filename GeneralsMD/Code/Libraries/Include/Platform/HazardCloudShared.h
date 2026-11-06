// SDL shared-volume hazard clouds with bounded incremental snapshot publication.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include "Platform/HazardCloud.h"
#include <cstdint>
#include <memory>
#include <string>
namespace HazardCloudShared {
class Model;
struct RendererConfig {
	unsigned volumeSide = 256;
	unsigned slices = 24;
	float binCellSize = 128.0f;
	float bakesPerSecond = 20.0f;
	// Bound injection work on every display render, including between logic
	// updates. A complete material snapshot is lit and published atomically.
	unsigned bakeSlicesPerFrame = 8;
	bool heat = true;
	bool embers = true;
};
struct RendererStats {
	unsigned sources = 0, activeSources = 0, maximumBinSources = 0;
	unsigned activeBodies = 0, interactionUploads = 0;
	float maxDisturbance = 0;
	unsigned bakes = 0, frames = 0;
	// Active sources in the visible snapshot, unfinished material slices, and
	// the largest injection allowance actually consumed since initialization.
	unsigned bakedSources = 0, pendingSlices = 0, maxSlicesPerFrame = 0;
	// Current simulation time minus the visible snapshot's captured time.
	float publicationAgeSeconds = 0;
	std::uint64_t volumeBytes = 0, metadataBytes = 0;
};
class Renderer {
public:
	virtual ~Renderer() = default;
	virtual bool initialize(std::string &error) = 0;
	// False guarantees no cloud draw touched scene colour; classic fallback is safe.
	virtual bool render(const HazardCloud::Camera &camera, const Model &model, float seconds, std::string &error) = 0;
	// Optional shared terrain grid covering the model's XY bounds. Values are
	// absolute world heights, copied with each snapshot and its XY bounds.
	// Only sources with terrain=true sample this grid. nullptr restores planes.
	virtual void setTerrain(const float *heights, unsigned side) = 0;
	virtual void setHeatEnabled(bool enabled) = 0;
	virtual void setBodies(const HazardCloud::Body *bodies, unsigned count) = 0;
	virtual void reset() = 0;
	virtual void shutdown() = 0;
	virtual const RendererStats &stats() const = 0;
};
// Query the current backend, including before a device exists.
bool Supported();
std::unique_ptr<Renderer> CreateRenderer();
}
