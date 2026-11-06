// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

// Render-side SDL water preferences. Updated at the water render boundary, except
// activeFftResolution, which a water FFT draw publishes before choosing its pixel program.
// Shared shader callers retain the original defaults.
namespace WaterPerformanceSettings {
enum Refraction { Full = 0, Simple = 1, Off = 2 };
struct Configuration {
    bool sceneReflections = true;
    unsigned requestedFftResolution = 256;
    unsigned activeFftResolution = 256; // the successfully allocated ocean, including fallback
    Refraction refraction = Full;
    bool sceneColourRequired = true; // SceneColourRequired() for the effective reflection mode
    int reflectionDetail = 1; // scene reflection detail; 0 Low, 1 High
};
inline Configuration &Applied() {
    static Configuration configuration;
    return configuration;
}
// Water samples scene colour for refraction and for screen-space reflection (c95.w=1).
// Planar reflections read their own capture.
inline bool SceneColourRequired(Refraction refraction, bool screenSpaceReflection) {
    return refraction != Off || screenSpaceReflection;
}
inline void Apply(bool reflections, int waveDetail, int refraction, int reflectionDetail = 1) {
    Configuration &configuration = Applied();
    configuration.sceneReflections = reflections;
    configuration.requestedFftResolution = waveDetail == 0 ? 128 : 256;
    configuration.refraction = refraction == Simple ? Simple : refraction == Off ? Off : Full;
    configuration.reflectionDetail = reflectionDetail == 0 ? 0 : 1;
}
}
