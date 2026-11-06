// The device-free half of the hazard cloud layers, shared by every backend.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloudCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace HazardCloud {

void multiply(const float left[16], const float right[16], float result[16])
{
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column) {
            float sum = 0.0f;
            for (unsigned index = 0; index < 4; ++index)
                sum += left[row * 4 + index] * right[index * 4 + column];
            result[row * 4 + column] = sum;
        }
}

// Visual-only inverse for reconstructing render positions from depth. Double
// intermediates keep distant camera translations stable.
bool inverse(const float source[16], float result[16])
{
    double rows[4][8];
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 8; ++c)
            rows[r][c] = c < 4 ? source[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
    for (unsigned c = 0; c < 4; ++c) {
        unsigned pivot = c;
        for (unsigned r = c + 1; r < 4; ++r)
            if (std::fabs(rows[r][c]) > std::fabs(rows[pivot][c])) pivot = r;
        if (!std::isfinite(rows[pivot][c]) || std::fabs(rows[pivot][c]) < 1e-15) return false;
        for (unsigned k = 0; k < 8; ++k) std::swap(rows[c][k], rows[pivot][k]);
        const double scale = rows[c][c];
        for (unsigned k = 0; k < 8; ++k) rows[c][k] /= scale;
        for (unsigned r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double factor = rows[r][c];
            for (unsigned k = 0; k < 8; ++k) rows[r][k] -= factor * rows[c][k];
        }
    }
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c) result[r * 4 + c] = static_cast<float>(rows[r][c + 4]);
    return true;
}

namespace {

constexpr unsigned MinimumEmbers = 24;
constexpr unsigned MaximumEmbers = 96;
constexpr float ReferenceEmberRadius = 250.0f;

} // namespace

bool emberField(const FieldState &field)
{
    return field.palette >= 0.5f && field.palette < 1.5f && field.density > 0.00001f
        && field.radius > 0.001f && field.height > 0.002f && field.formation > 0.02f;
}

unsigned emberConstants(const FieldState &field, unsigned index, bool terrain,
                        const float viewProjection[16], const float viewport[4],
                        float timeSeconds, EmberConstants &constants)
{
    const float footprintRadius = field.radius + field.fringe;
    const float areaRatio = footprintRadius * footprintRadius
        / (ReferenceEmberRadius * ReferenceEmberRadius);
    // Match particle density to visible area: 64 at the comparison size,
    // fewer for cannon residue and at most 96 for the large missile field.
    const unsigned count = static_cast<unsigned>(std::clamp(
        std::round(64.0f * areaRatio / 8.0f) * 8.0f,
        static_cast<float>(MinimumEmbers), static_cast<float>(MaximumEmbers)));
    constants = {};
    std::copy(viewProjection, viewProjection + 16, constants.viewProjection);
    std::copy(viewport, viewport + 4, constants.viewport);
    constants.field[0] = field.ground.x;
    constants.field[1] = field.ground.y;
    constants.field[2] = field.ground.z;
    constants.field[3] = footprintRadius;
    constants.shape[0] = field.height;
    constants.shape[1] = field.density;
    constants.shape[2] = field.formation;
    constants.scene[0] = timeSeconds;
    constants.scene[1] = static_cast<float>(count);
    constants.terrain[0] = terrain ? 1.0f : 0.0f;
    constants.terrain[1] = static_cast<float>(index);
    constants.terrain[2] = footprintRadius + MaximumDisplacement;
    return count;
}

bool setEmberTerrainDomain(const float domain[4], EmberConstants &constants)
{
    if (!domain) return false;
    for (unsigned i = 0; i < 4; ++i) if (!std::isfinite(domain[i])) return false;
    const float width = domain[2] - domain[0], height = domain[3] - domain[1];
    if (!(width > 0 && height > 0) || !std::isfinite(width) || !std::isfinite(height)
        || !std::isfinite(1.f / width) || !std::isfinite(1.f / height)) return false;
    constants.terrain[1] = -1;
    constants.terrain[2] = domain[0]; constants.terrain[3] = domain[1];
    constants.scene[2] = 1.f / width; constants.scene[3] = 1.f / height;
    return true;
}

void LayerCore::fail(const char *reason)
{
    if (!failed) std::fprintf(stderr, "hazard-cloud: volumetric renderer FAIL: %s\n", reason);
    failed = true;
}

bool LayerCore::fieldActive(unsigned index) const
{
    const FieldState &field = fieldStates[index];
    return index < fieldCount && field.radius > 0.001f && field.height > 0.002f && field.density > 0.00001f;
}

bool LayerCore::anyFieldActive() const
{
    for (unsigned i = 0; i < fieldCount; ++i)
        if (fieldActive(i)) return true;
    return false;
}

void LayerCore::setFields(const FieldState *values, unsigned count)
{
    if (count > 2 || (count && !values)) {
        fail("invalid cloud lifecycle field list");
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        const FieldState &field = values[i];
        if (!std::isfinite(field.ground.x) || !std::isfinite(field.ground.y)
            || !std::isfinite(field.ground.z) || !std::isfinite(field.radius)
            || !std::isfinite(field.height) || !std::isfinite(field.density)
            || !std::isfinite(field.palette) || !std::isfinite(field.formation)
            || !std::isfinite(field.release) || !std::isfinite(field.fringe)
            || field.fringe < 0 || field.radius < 0 || field.height < 0
            || field.density < 0 || field.palette < 0 || field.palette > 3
            || field.formation < 0 || field.formation > 1 || field.release < 0 || field.release > 1) {
            fail("invalid cloud lifecycle field state");
            return;
        }
    }
    if (!explicitDomain && count) {
        float minX = values[0].ground.x, maxX = minX;
        float minY = values[0].ground.y, maxY = minY;
        for (unsigned i = 0; i < count; ++i) {
            const float radius = (std::max)(1.0f, values[i].radius + values[i].fringe + MaximumDisplacement);
            minX = (std::min)(minX, values[i].ground.x - radius);
            maxX = (std::max)(maxX, values[i].ground.x + radius);
            minY = (std::min)(minY, values[i].ground.y - radius);
            maxY = (std::max)(maxY, values[i].ground.y + radius);
        }
        if (flow.domainMinX() != minX || flow.domainMaxX() != maxX
            || flow.domainMinY() != minY || flow.domainMaxY() != maxY) {
            flow.setDomain(minX, minY, maxX, maxY);
            flowDirty = true;
        }
    }
    fieldCount = count;
    for (unsigned i = 0; i < count; ++i) {
        fieldStates[i] = values[i];
        peakDensity[i] = (std::max)(peakDensity[i], values[i].density);
    }
}

void LayerCore::reset()
{
    flow.reset();
    flowDirty = true;
    for (unsigned i = 0; i < 2; ++i) sourceTravel[i] = sourceAge[i] = peakDensity[i] = 0;
}

void LayerCore::setBodies(const Body *values, unsigned count)
{
    if (count > MaximumBodies || (count && !values)) {
        fail("invalid infantry interaction body list");
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        const Body &body = values[i];
        if (!std::isfinite(body.x) || !std::isfinite(body.y) || !std::isfinite(body.z)
            || !std::isfinite(body.vx) || !std::isfinite(body.vy)
            || !std::isfinite(body.radius) || !std::isfinite(body.height)
            || body.radius <= 0 || body.height <= 0) {
            fail("invalid infantry interaction body");
            return;
        }
    }
    if (count) bodies.assign(values, values + count);
    else bodies.clear();
}

void LayerCore::setFlowDomain(float minX, float minY, float maxX, float maxY)
{
    flow.setDomain(minX, minY, maxX, maxY);
    explicitDomain = true;
    flowDirty = true;
    if (!flow.valid()) fail("invalid interaction simulation domain");
}

void LayerCore::setTerrain(unsigned index, const float *heights)
{
    if (index >= 2) { fail("invalid terrain field index"); return; }
    if (!heights) {
        terrainEnabled[index] = false;
        terrainMin[index] = terrainMax[index] = 0;
        return;
    }
    float minimum = heights[0], maximum = heights[0];
    for (unsigned i = 0; i < TerrainGridSide * TerrainGridSide; ++i) {
        if (!std::isfinite(heights[i]) || std::abs(heights[i]) > 100000.0f) {
            fail("invalid terrain height");
            return;
        }
        minimum = (std::min)(minimum, heights[i]);
        maximum = (std::max)(maximum, heights[i]);
    }
    bool changed = !terrainEnabled[index];
    for (unsigned y = 0; y < TerrainGridSide; ++y) {
        float *destination = terrainPixels.data() + y * TerrainGridSide * 2 + index * TerrainGridSide;
        const float *source = heights + y * TerrainGridSide;
        changed = changed || std::memcmp(destination, source, TerrainGridSide * sizeof(float)) != 0;
        std::copy(source, source + TerrainGridSide, destination);
    }
    terrainEnabled[index] = true;
    terrainMin[index] = minimum;
    terrainMax[index] = maximum;
    terrainDirty = terrainDirty || changed;
}

void LayerCore::setPalette(unsigned index, const Palette &palette)
{
    const float values[] = { palette.main.r, palette.main.g, palette.main.b,
        palette.shadow.r, palette.shadow.g, palette.shadow.b, palette.brightness, palette.transparency };
    if (index >= 4) { fail("invalid palette index"); return; }
    for (unsigned i = 0; i < 8; ++i)
        if (!std::isfinite(values[i]) || values[i] < 0 || values[i] > (i == 6 ? 2 : 1)) {
            fail("invalid palette value");
            return;
        }
    palettes[index] = palette;
}

void LayerCore::setInteractionEnabled(bool value)
{
    if (interactionEnabled == value) return;
    interactionEnabled = value;
    flow.reset();
    flowDirty = true;
}

void LayerCore::update(float timeSeconds)
{
    if (!std::isfinite(timeSeconds) || timeSeconds < 0) {
        fail("invalid simulation time");
        return;
    }
    const float elapsed = hasSimulationTime ? timeSeconds - simulationTime : 0.0f;
    simulationTime = timeSeconds;
    hasSimulationTime = true;
    if (elapsed < 0) {
        reset();
    } else if (elapsed > 0 && !failed) {
        for (unsigned i = 0; i < fieldCount; ++i) {
            sourceAge[i] += elapsed;
            sourceTravel[i] += elapsed * fieldStates[i].release * 22.0f;
        }
        // Appearance only controls drawing. Keep the same wake history while
        // the original effects are shown, so toggling back is an in-place A/B.
        // A grid with nothing stirring it and nothing left to heal stays as it
        // is, so a quiet cloud uploads nothing and rebakes only as it ages.
        bool stirring = false;
        for (const Body &body : bodies)
            stirring = stirring || body.vx * body.vx + body.vy * body.vy > 0.02f * 0.02f;
        if (interactionEnabled && (stirring || flow.maxDisturbance() > 0.0f)) {
            flow.step(elapsed, bodies.empty() ? nullptr : bodies.data(),
                static_cast<unsigned>(bodies.size()));
            if (!flow.valid()) fail("interaction simulation produced invalid state");
            // Healed to under a fiftieth of a world unit: put it at rest.
            if (!stirring && flow.maxDisturbance() < 0.001f) flow.reset();
            flowDirty = true;
        }
    }
}

bool LayerCore::buildUniforms(const Camera &camera, unsigned targetWidth, unsigned targetHeight,
                              Uniforms &uniforms, float viewProjection[16])
{
    if (!camera.width || !camera.height || !targetWidth || !targetHeight) {
        fail("the scene camera has an empty viewport");
        return false;
    }
    uniforms = {};
    float inverseView[16];
    multiply(camera.view, camera.projection, viewProjection);
    if (!inverse(viewProjection, uniforms.WorldFromClip) || !inverse(camera.view, inverseView)) {
        fail("scene camera has a singular transform");
        return false;
    }
    const float depthRange = camera.maxZ - camera.minZ;
    if (depthRange <= 1.0e-8f) {
        fail("scene camera has an empty depth range");
        return false;
    }
    for (unsigned column = 0; column < 4; ++column) {
        const float row = uniforms.WorldFromClip[8 + column] / depthRange;
        uniforms.WorldFromClip[12 + column] -= camera.minZ * row;
        uniforms.WorldFromClip[8 + column] = row;
    }
    for (unsigned i = 0; i < 3; ++i) uniforms.CameraPosition[i] = inverseView[12 + i];
    uniforms.CameraPosition[3] = 1;
    uniforms.Viewport[0] = static_cast<float>(camera.x) + 0.5f;
    uniforms.Viewport[1] = static_cast<float>(camera.y) + 0.5f;
    uniforms.Viewport[2] = static_cast<float>(camera.width);
    uniforms.Viewport[3] = static_cast<float>(camera.height);
    uniforms.Scene[0] = 1.0f / targetWidth;
    uniforms.Scene[1] = 1.0f / targetHeight;
    uniforms.Scene[2] = 1;
    uniforms.Scene[3] = simulationTime;
    float *fields[] = { uniforms.Field0, uniforms.Field1 };
    float *shapes[] = { uniforms.Shape0, uniforms.Shape1 };
    float *sources[] = { uniforms.Source0, uniforms.Source1 };
    bool hotField = false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        const FieldState &field = fieldStates[i];
        hotField = hotField || (field.palette >= 0.5f && field.palette < 1.5f && fieldActive(i));
        fields[i][0] = field.ground.x;
        fields[i][1] = field.ground.y;
        fields[i][2] = field.ground.z + field.height * 0.5f;
        fields[i][3] = field.radius;
        shapes[i][0] = field.height * 0.5f;
        shapes[i][1] = field.density;
        shapes[i][2] = field.palette;
        shapes[i][3] = field.formation;
        uniforms.Footprints[i] = field.fringe;
        sources[i][0] = field.release;
        sources[i][1] = sourceTravel[i];
        sources[i][2] = peakDensity[i] > 0.0001f
            ? (std::max)(0.0f, 1.0f - field.density / peakDensity[i]) : 0.0f;
        sources[i][3] = sourceAge[i];
    }
    float *terrain[] = { uniforms.Terrain0, uniforms.Terrain1 };
    for (unsigned i = 0; i < 2; ++i) {
        terrain[i][0] = terrainMin[i];
        terrain[i][1] = terrainMax[i];
        terrain[i][2] = terrainEnabled[i] ? 1.0f : 0.0f;
    }
    uniforms.Settings[0] = static_cast<float>(fieldCount);
    for (unsigned i = 0; i < 4; ++i) {
        const Palette &palette = palettes[i];
        uniforms.PaletteMain[i][0] = palette.main.r;
        uniforms.PaletteMain[i][1] = palette.main.g;
        uniforms.PaletteMain[i][2] = palette.main.b;
        uniforms.PaletteMain[i][3] = palette.brightness;
        uniforms.PaletteShadow[i][0] = palette.shadow.r;
        uniforms.PaletteShadow[i][1] = palette.shadow.g;
        uniforms.PaletteShadow[i][2] = palette.shadow.b;
        uniforms.PaletteShadow[i][3] = 1.0f - palette.transparency;
    }
    // Visible hot-air refraction only for the warm nuclear palette.
    uniforms.Settings[1] = heatEnabled && hotField ? 10.0f : 0.0f;
    uniforms.Settings[2] = 1;
    uniforms.Settings[3] = Steps;
    uniforms.Interaction[0] = flow.domainMinX();
    uniforms.Interaction[1] = flow.domainMinY();
    uniforms.Interaction[2] = 1.0f / (flow.domainMaxX() - flow.domainMinX());
    uniforms.Interaction[3] = 1.0f / (flow.domainMaxY() - flow.domainMinY());
    uniforms.FlowSettings[0] = interactionEnabled ? 1.0f : 0.0f;
    uniforms.FlowSettings[1] = MaximumDisplacement;
    return true;
}

bool LayerCore::takeBake(const Uniforms &uniforms)
{
    // Nothing the camera does reaches the atlases: orbiting, zooming, resizing
    // or drawing a paused frame reuses the same world-space volume.
    Uniforms inputs = uniforms;
    std::fill(inputs.WorldFromClip, inputs.WorldFromClip + 16, 0.0f);
    std::fill(inputs.CameraPosition, inputs.CameraPosition + 4, 0.0f);
    std::fill(inputs.Viewport, inputs.Viewport + 4, 0.0f);
    std::fill(inputs.Scene, inputs.Scene + 4, 0.0f);
    inputs.Settings[1] = inputs.Settings[3] = 0;
    std::fill(inputs.CachePass, inputs.CachePass + 4, 0.0f);
    const long long tick = static_cast<long long>(std::floor(simulationTime * BakesPerSecond));
    bool bake = !cachedInputsValid;
    if (!bake && (cachedFlowUploads != flowUploads
            || std::memcmp(&inputs, &cachedInputs, sizeof(inputs)) != 0)) {
        // What a cloud does on its own as it ages: its density, its formation
        // front and its source history. Anything else that differs is a change
        // the next frame has to show.
        Uniforms structure = inputs, cachedStructure = cachedInputs;
        for (Uniforms *u : { &structure, &cachedStructure }) {
            u->Shape0[1] = u->Shape0[3] = u->Shape1[1] = u->Shape1[3] = 0;
            std::fill(u->Source0, u->Source0 + 4, 0.0f);
            std::fill(u->Source1, u->Source1 + 4, 0.0f);
        }
        bake = std::memcmp(&structure, &cachedStructure, sizeof(structure)) != 0
            || tick != cachedBakeTick;
    }
    if (!bake) return false;
    cachedInputs = inputs;
    cachedFlowUploads = flowUploads;
    cachedBakeTick = tick;
    cachedInputsValid = true;
    ++cacheBakes;
    return true;
}

void LayerCore::heatScissor(const float viewProjection[16], const Camera &camera,
                            unsigned targetWidth, unsigned targetHeight, int rectangle[4]) const
{
    const float left = static_cast<float>((std::min)(camera.x, targetWidth));
    const float top = static_cast<float>((std::min)(camera.y, targetHeight));
    const float right = left + (std::min)(camera.width, targetWidth - static_cast<unsigned>(left));
    const float bottom = top + (std::min)(camera.height, targetHeight - static_cast<unsigned>(top));
    rectangle[0] = static_cast<int>(left);
    rectangle[1] = static_cast<int>(top);
    rectangle[2] = static_cast<int>(right - left);
    rectangle[3] = static_cast<int>(bottom - top);
    float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
    bool found = false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        const FieldState &field = fieldStates[i];
        if (field.palette < 0.5f || field.palette >= 1.5f || !fieldActive(i)) continue;
        found = true;
        const float radius = field.radius + field.fringe + MaximumDisplacement;
        for (unsigned corner = 0; corner < 8; ++corner) {
            const float world[4] = {
                field.ground.x + (corner & 1 ? radius : -radius),
                field.ground.y + (corner & 2 ? radius : -radius),
                (terrainEnabled[i] ? (corner & 4 ? terrainMax[i] : terrainMin[i]) : field.ground.z)
                    + (corner & 4 ? field.height * 2.6f : 0.0f), 1.0f
            };
            float clip[4] = {};
            for (unsigned column = 0; column < 4; ++column)
                for (unsigned row = 0; row < 4; ++row)
                    clip[column] += world[row] * viewProjection[row * 4 + column];
            // A box crossing the near plane cannot be bounded by projecting
            // only its corners. Keep the full viewport for that unusual view.
            if (!std::isfinite(clip[0]) || !std::isfinite(clip[1])
                || !std::isfinite(clip[2]) || !std::isfinite(clip[3])
                || clip[3] <= 0.00001f || clip[2] <= 0.00001f) return;
            const float x = camera.x + 0.5f + (clip[0] / clip[3] + 1.0f) * camera.width * 0.5f;
            const float y = camera.y + 0.5f + (1.0f - clip[1] / clip[3]) * camera.height * 0.5f;
            minX = (std::min)(minX, x); maxX = (std::max)(maxX, x);
            minY = (std::min)(minY, y); maxY = (std::max)(maxY, y);
        }
    }
    if (!found) { rectangle[2] = rectangle[3] = 0; return; }
    // Two pixels of conservative rasterization slack keep the projected
    // envelope intact at viewport edges and odd framebuffer dimensions.
    const float boundedLeft = std::clamp(std::floor(minX) - 2.0f, left, right);
    const float boundedTop = std::clamp(std::floor(minY) - 2.0f, top, bottom);
    const float boundedRight = std::clamp(std::ceil(maxX) + 2.0f, left, right);
    const float boundedBottom = std::clamp(std::ceil(maxY) + 2.0f, top, bottom);
    rectangle[0] = static_cast<int>(boundedLeft);
    rectangle[1] = static_cast<int>(boundedTop);
    rectangle[2] = static_cast<int>((std::max)(0.0f, boundedRight - boundedLeft));
    rectangle[3] = static_cast<int>((std::max)(0.0f, boundedBottom - boundedTop));
}

void LayerCore::resetCounters()
{
    cachedInputsValid = false;
    cachedInputs = {};
    cachedFlowUploads = cacheBakes = 0;
    cachedBakeTick = 0;
    renderedFrameCount = flowUploads = 0;
    flowDirty = true;
    hasSimulationTime = false;
    terrainDirty = terrainEnabled[0] || terrainEnabled[1];
    bodies.clear();
    reset();
}

} // namespace HazardCloud
