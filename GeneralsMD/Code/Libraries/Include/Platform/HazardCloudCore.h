// The device-free half of every hazard cloud backend: the field and wake state,
// the constants the shaders read, the shader text and when the caches rebake.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "Platform/HazardCloud.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace HazardCloud {

constexpr float Steps = 96.0f;
constexpr float MaximumDisplacement = 18.0f;
constexpr unsigned MaximumBodies = 32;
constexpr unsigned CacheSlices = 24;
constexpr unsigned CacheColumns = 6;
constexpr unsigned CacheRows = CacheSlices / CacheColumns;
// How often a cloud that is only aging rebakes its density and light atlases.
// ponytail: a fixed rate; a wake behind running infantry steps at it too, raise
// it if that ever reads as stutter.
constexpr float BakesPerSecond = 20.0f;

// One atlas slice's side in voxels, the same for the estimate and the device.
unsigned cacheSide(const FieldState &field);

// Matches the cloud pixel shaders' b0 exactly; all matrices keep the renderer's
// row-major convention.
struct Uniforms {
    float WorldFromClip[16];
    float CameraPosition[4];
    float Viewport[4];                 // origin including half pixel, width, height
    float Scene[4];                    // inverse target size, depth available, seconds
    float Field0[4];                   // world centre XYZ, horizontal radius
    float Field1[4];
    float Shape0[4];                   // half height, density gain, palette, formation
    float Shape1[4];
    float Footprints[4];               // field 0/1 fringe beyond hazard radius, reserved ZW
    float Source0[4];                  // release, outward travel, dissipation, source age
    float Source1[4];
    float Settings[4];                 // active fields, nuclear heat pixels, enabled, maximum steps
    float Interaction[4];              // world origin XY, inverse world width/height
    float FlowSettings[4];             // enabled, maximum displacement, reserved, reserved
    float Cache0[4];                   // voxel counts XYZ, atlas slice columns
    float Cache1[4];
    float CachePass[4];                // field index for cache construction, reserved YZW
    float PaletteMain[4][4];           // per-palette main RGB, overall brightness
    float PaletteShadow[4][4];         // per-palette shadow RGB, optical thickness multiplier
    float Terrain0[4];                 // minimum ground Z, maximum ground Z, enabled, reserved
    float Terrain1[4];
};
static_assert(sizeof(Uniforms) == 480, "hazard volume uniform layout");

// The nuclear embers' vertex constants, b0 of the ember vertex shader.
struct EmberConstants {
    float viewProjection[16];
    float viewport[4];
    float field[4];
    float shape[4];
    float scene[4];
    float terrain[4]; // enabled, field index, sampled domain radius, reserved
};
static_assert(sizeof(EmberConstants) == 144, "ember shader constant layout");

// A field that shows embers: nuclear, visible and past the first of its formation.
bool emberField(const FieldState &field);
// The constants for one field's embers and how many billboards they draw.
unsigned emberConstants(const FieldState &field, unsigned index, bool terrain,
                        const float viewProjection[16], const float viewport[4],
                        float timeSeconds, EmberConstants &constants);

// Row-major 4x4 product and inverse, in the device's layout (the inverse in doubles, so a camera
// far from the origin still comes back); the reach overlay takes them too.
void multiply(const float left[16], const float right[16], float result[16]);
bool inverse(const float source[16], float result[16]);

// HLSL text. SDL GPU wants explicit register spaces; FXC's shader model 5
// rejects them, so the Direct3D 11 backend asks for the text without.
enum class Pass { Volume, Composite, Density, Lighting, Heat };
std::string vertexShader();
std::string pixelShader(Pass pass, bool registerSpaces);
std::string emberVertexShader(bool registerSpaces);
std::string emberPixelShader(bool registerSpaces);

// Everything a backend keeps that needs no device. A backend derives from this
// and supplies initialize, render, estimatedCacheBytes and shutdown.
class LayerCore : public Layer {
public:
    void setFields(const FieldState *fields, unsigned count) override;
    void setBodies(const Body *bodies, unsigned count) override;
    void setFlowDomain(float minX, float minY, float maxX, float maxY) override;
    void setTerrain(unsigned fieldIndex, const float *heights) override;
    void setPalette(unsigned index, const Palette &palette) override;
    void setInteractionEnabled(bool enabled) override;
    bool isInteractionEnabled() const override { return interactionEnabled; }
    void setHeatEnabled(bool enabled) override { heatEnabled = enabled; }
    bool isHeatEnabled() const override { return heatEnabled; }
    void update(float timeSeconds) override;
    void reset() override;
    bool valid() const override { return initialized && !failed && flow.valid(); }
    unsigned activeBodies() const override { return flow.activeBodies(); }
    float maxDisturbance() const override { return flow.maxDisturbance(); }
    unsigned interactionUploads() const override { return flowUploads; }
    unsigned renderedFrames() const override { return renderedFrameCount; }

    // Whether the atlases have to be rebuilt for these constants, and if so the
    // constants are taken as baked. A view change never rebakes; a structural
    // change (palette, terrain, size, a new field) rebakes at once; a cloud that
    // only ages or stirs rebakes at most BakesPerSecond times a second.
    bool takeBake(const Uniforms &uniforms);
    unsigned bakes() const { return cacheBakes; }
    // Whether the wake grid changed since the device last uploaded it.
    bool flowNeedsUpload() const { return flowDirty; }

protected:
    void fail(const char *reason);
    bool fieldActive(unsigned index) const;
    bool anyFieldActive() const;
    // The cloud constants for this camera over a target of this size, and the
    // camera's world to clip matrix. False (and failed) for a singular camera.
    bool buildUniforms(const Camera &camera, unsigned targetWidth, unsigned targetHeight,
                       Uniforms &uniforms, float viewProjection[16]);
    // The scissor that holds the nuclear heat, inside the camera's viewport;
    // width zero when no field is hot.
    void heatScissor(const float viewProjection[16], const Camera &camera,
                     unsigned targetWidth, unsigned targetHeight, int rectangle[4]) const;
    // The device uploaded the wake grid; the next bake sees the new wakes.
    void flowUploaded() { flowDirty = false; ++flowUploads; }
    // Forget what was baked: new atlases, new terrain, a reset device.
    void invalidateBake() { cachedInputsValid = false; }
    void resetCounters();

    FieldState fieldStates[2] = {};
    unsigned fieldCount = 0;
    float sourceTravel[2] = {}, sourceAge[2] = {}, peakDensity[2] = {};
    float simulationTime = 0;
    bool hasSimulationTime = false;
    bool initialized = false;
    bool interactionEnabled = true;
    bool heatEnabled = true;
    bool flowDirty = true;
    bool failed = false;
    bool explicitDomain = false;
    unsigned renderedFrameCount = 0;
    unsigned flowUploads = 0;
    std::vector<Body> bodies;
    HazardCloudFlow::Flow flow;
    std::array<Palette, 4> palettes = {{defaultPalette(0), defaultPalette(1), defaultPalette(2), defaultPalette(3)}};
    // Both fields' grids side by side, TerrainGridSide * 2 wide.
    std::vector<float> terrainPixels = std::vector<float>(TerrainGridSide * TerrainGridSide * 2, 0.0f);
    bool terrainEnabled[2] = {};
    float terrainMin[2] = {}, terrainMax[2] = {};
    bool terrainDirty = false;

private:
    Uniforms cachedInputs = {};
    bool cachedInputsValid = false;
    unsigned cachedFlowUploads = 0;
    long long cachedBakeTick = 0;
    unsigned cacheBakes = 0;
};

} // namespace HazardCloud
