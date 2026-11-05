// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "WaterPreparedMeshCache.h"
#include "WaterProjectedMeshCache.h"
#include "WaterProjectedSampleCache.h"
#include "WaterStaticSampleCache.h"
#include "WaterTerrainCulling.h"

namespace WaterRenderCaches {
// Camera matrices, patch geometry, level, mesh resolution and bow refinement are
// exact keys in the mesh caches. These other preferences invalidate the set.
struct Settings {
	int reflectionMode=0, refraction=0, reflectionDetail=1;
	unsigned requestedFftResolution=256, activeFftResolution=256;
	int width=0, height=0;
	bool sceneReflections=true;
	bool operator==(const Settings &other) const {
		return reflectionMode==other.reflectionMode && refraction==other.refraction &&
			reflectionDetail==other.reflectionDetail && requestedFftResolution==other.requestedFftResolution &&
			activeFftResolution==other.activeFftResolution && width==other.width && height==other.height &&
			sceneReflections==other.sceneReflections;
	}
};

template<class Sample> class Cache {
	Settings settings;
	bool configured=false;
public:
	WaterProjectedMeshCache::Cache topology;
	WaterPreparedMeshCache::Cache prepared;
	WaterProjectedSampleCache::Cache<Sample> samples;
	WaterStaticSampleCache::Cache staticSamples;
	WaterTerrainCulling::WetNodeCache terrain;

	// Style, terrain, polygon, map and device lifecycle boundaries call Clear.
	void Clear() {
		topology.Clear();
		prepared.Clear();
		samples.Clear();
		staticSamples.Clear();
		terrain.Clear();
		configured=false;
	}
	void BeginRender(const Settings &next) {
		if (configured && !(settings==next)) Clear();
		settings=next;
		configured=true;
		// The first view must retain static data prepared during loading.
		samples.BeginRender();
		staticSamples.BeginRender();
	}
};
}
