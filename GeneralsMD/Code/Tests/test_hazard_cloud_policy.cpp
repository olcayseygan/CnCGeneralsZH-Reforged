#include "test_harness.h"
#include "Platform/HazardCloudPolicy.h"
#include "Platform/HazardCloud.h"
#include "Platform/HazardCloudFlow.h"
#include "Platform/HazardCloudCore.h"

#include <initializer_list>
#include <limits>
#include <type_traits>

using namespace HazardCloudPolicy;

TEST(hazard_cloud_recognizes_only_known_particle_templates)
{
    const char *const names[][3] = {
        {"PoisonFieldSmall", "PoisonFieldMedium", "PoisonFieldLarge"},
        {"RadiationFieldSmall", "RadiationFieldMedium", "RadiationFieldLarge"},
        {"AnthraxFieldSmall", "AnthraxFieldMedium", "AnthraxFieldLarge"},
        {"GC_Chem_AnthraxGammaFieldSmall", "GC_Chem_AnthraxFieldGammaMedium",
            "GC_Chem_AnthraxFieldGammaLarge"}
    };
    for (int palette = 0; palette < 4; ++palette)
        for (const char *name : names[palette]) CHECK_EQ(paletteForTemplate(name), palette);
    CHECK_EQ(paletteForTemplate("NukeRadiationInitial"), 1);
    CHECK_EQ(paletteForTemplate("poisonfieldLARGE"), 0);
    for (const char *name : {static_cast<const char *>(nullptr), "", "PoisonFieldLargeExtra", "MyRadiationFieldLarge",
            "GC_Chem_AnthraxFieldGammaSmall", "ToxinTankPuddle", "NukeMushroomStem",
            "ToxinSpray", "PoisonFieldAnthraxBomb"})
        CHECK_EQ(paletteForTemplate(name), -1);
}

TEST(hazard_cloud_recognizes_active_tractor_contamination)
{
    // The secondary weapon emits these before FireOCLAfterWeaponCooldownUpdate
    // creates the lingering PoisonField owner. All three are ground-area FX.
    CHECK_EQ(paletteForTemplate("ToxinPuddleLarge"), 0);
    CHECK_EQ(paletteForTemplate("AnthraxPuddleLarge"), 2);
    CHECK_EQ(paletteForTemplate("AnthraxGammaPuddleLarge"), 3);
    CHECK_EQ(paletteForTemplate("toxinPUDDLElarge"), 0);
    for (const char *name : {"ToxinPuddleLargeExtra", "MyAnthraxPuddleLarge",
            "AnthraxGammaPuddle", "ToxinTankPuddle", "ToxinSpray", "AnthraxSpray"})
        CHECK_EQ(paletteForTemplate(name), -1);
    // Active effects have no cleanup-hazard owner until the tractor stops.
    for (const char *name : {"ToxinPuddleLarge", "AnthraxPuddleLarge", "AnthraxGammaPuddleLarge"}) {
        CHECK_EQ(paletteForOwner(name), -1);
        CHECK_EQ(paletteForTemplate(name, false), -1);
    }
    CHECK_EQ(paletteForTemplate("PoisonFieldMedium", false), 0);
}

TEST(hazard_cloud_particle_sources_are_separate_from_gameplay_hazards)
{
    const struct { const char *name; int palette; bool attached; } sources[] = {
        {"ToxinPuddle", 0, false}, {"AnthraxPuddle", 2, false}, {"GC_Chem_ToxinPuddle", 3, false},
        {"ToxinPuddleContinuous", 0, true}, {"AnthraxPuddleContinuous", 2, true},
        {"AnthraxGammaPuddleContinuous", 3, true}, {"ToxinTankPuddle", 0, true}
    };
    for (const auto &source : sources) {
        CHECK_EQ(paletteForParticleSource(source.name), source.palette);
        CHECK_EQ(attachedParticleSource(source.name), source.attached);
        CHECK_EQ(paletteForTemplate(source.name), -1);
        CHECK_EQ(paletteForOwner(source.name), -1);
    }
    CHECK_EQ(paletteForParticleSource("anthraXPUDDLE"), 2);
    for (const char *name : {static_cast<const char *>(nullptr), "", "ToxinSpray", "ToxinTarget",
            "ToxinTankPuddleExtra", "PoisonFieldMedium", "ToxinPuddleLarge", "GLAViralOutbreakClouds"})
        CHECK_EQ(paletteForParticleSource(name), -1);
}

TEST(hazard_cloud_short_particle_sources_form_and_expire_within_their_lifetime)
{
    // Gamma impact: 10 retail frames, one third of a second. The generic
    // three-second hazard buildup would consume its entire visible lifetime.
    const float life = 1.f / 3.f;
    CHECK(particleSourceBuildup(.05f, life, life - .05f).density > .5f);
    CHECK_EQ(particleSourceBuildup(.1f, life, life - .1f).formation, 1.f);
    CHECK(particleSourceBuildup(life - .01f, life, .01f).density < .2f);
    CHECK_EQ(particleSourceBuildup(life, life, 0).density, 0.f);
    CHECK_EQ(particleSourceBuildup(1, -1, 1).density, 0.f);
    CHECK_EQ(particleSourceBuildup(std::numeric_limits<float>::quiet_NaN(), life, life).density, 0.f);
    CHECK(particleSourceHeight(5, 10) < particleSourceHeight(55, 25));
    CHECK_EQ(particleSourceHeight(55, 25), 18.61f);
    CHECK(particleSourceHeight(1, 1) > 0);
}

TEST(hazard_cloud_matches_actual_hazard_object_names)
{
    CHECK_EQ(paletteForOwner("PoisonFieldMedium"), 0);
    CHECK_EQ(paletteForOwner("PoisonFieldUpgradedLarge"), 2);
    CHECK_EQ(paletteForOwner("PoisonFieldAnthraxBomb"), 2);
    CHECK_EQ(paletteForOwner("PoisonFieldAnthraxGammaBomb"), 3);
    CHECK_EQ(paletteForOwner("NukeRadiationFieldWeapon"), 1);
    CHECK_EQ(paletteForOwner("Nuke_RadiationFieldSmall"), 1);
    for (const char *name : {"RadiationFieldSmall", "RadiationFieldMedium", "RadiationFieldLarge"})
        CHECK_EQ(paletteForOwner(name), 1);
    for (const char *name : {"Chem_PoisonFieldGammaSmall", "Chem_PoisonFieldGammaMedium",
            "Chem_PoisonFieldGammaLarge", "GC_Chem_PoisonFieldGammaSmall",
            "GC_Chem_PoisonFieldGammaMedium", "GC_Chem_PoisonFieldGammaLarge"})
        CHECK_EQ(paletteForOwner(name), 3);
    for (const char *name : {static_cast<const char *>(nullptr), "", "NukeRadiationInitial", "AnthraxFieldLarge",
            "GC_Chem_AnthraxGammaFieldSmall", "GLAVehicleToxinTruck", "Chem_GLAScudStorm",
            "LargePoisonFieldWeapon", "PoisonFieldMediumCustom"})
        CHECK_EQ(paletteForOwner(name), -1);
}

TEST(hazard_cloud_nuclear_fades_in_across_its_full_footprint)
{
    for (float age : {0.0f, 0.1f, 1.0f, 3.0f, 6.0f, 12.0f}) {
        const Buildup nuclear = visualBuildup(1, true, age, 30.0f);
        CHECK_EQ(nuclear.formation, 1.0f);
        CHECK(nuclear.density >= 0.0f && nuclear.density <= 1.0f);
        CHECK(nuclear.release >= 0.0f && nuclear.release <= 1.0f);
    }
    CHECK_NEAR(visualBuildup(1, true, 3.0f, 30.0f).density, 0.5f, 0.00001f);
    CHECK_NEAR(visualBuildup(1, true, 3.0f, 30.0f).release, 0.675f, 0.00001f);
    CHECK_EQ(visualBuildup(1, true, 6.0f, 30.0f).density, 1.0f);
    CHECK_EQ(visualBuildup(1, true, 6.0f, 30.0f).release, 0.0f);
    CHECK_EQ(visualBuildup(1, false, 3.0f, 30.0f).density, 1.0f);
}

TEST(hazard_cloud_toxin_formation_and_release_preserve_palette_identity)
{
    for (int palette : {0, 2, 3}) {
        const Buildup beginning = visualBuildup(palette, false, 0.0f, 30.0f);
        const Buildup middle = visualBuildup(palette, false, 1.5f, 30.0f);
        const Buildup formed = visualBuildup(palette, false, 3.0f, 30.0f);
        CHECK_EQ(beginning.formation, 0.0f);
        CHECK_EQ(beginning.density, 0.0f);
        CHECK_EQ(beginning.release, 1.0f);
        CHECK_NEAR(middle.formation, 0.5f, 0.00001f);
        CHECK_EQ(middle.density, 1.0f);
        CHECK_EQ(formed.formation, 1.0f);
        CHECK_EQ(formed.density, 1.0f);
        CHECK_EQ(formed.release, 0.0f);
        CHECK_NEAR(visualBuildup(palette, true, 1.5f, 30.0f).formation,
            middle.formation, 0.00001f); // Large-nuclear flag must not slow toxin.
    }
}

TEST(hazard_cloud_short_lifetimes_have_a_visible_lingering_phase)
{
    for (float lifetime : {0.04f, 0.5f, 2.5f, 10.0f, 30.0f, 180.0f})
        for (int palette = 0; palette < 4; ++palette) {
            const Buildup formed = visualBuildup(palette, true, lifetime * 0.25f, lifetime);
            CHECK_EQ(formed.formation, 1.0f);
            CHECK_EQ(formed.density, 1.0f);
            CHECK_EQ(formed.release, 0.0f);
            CHECK_EQ(visualBuildup(palette, true, lifetime, lifetime).density, 0.0f);
            CHECK_EQ(visualBuildup(palette, true, lifetime + 1.0f, lifetime).density, 0.0f);
        }
    CHECK(visualBuildup(1, false, 0.3f, 2.5f).density > 0.4f);
}

TEST(hazard_cloud_invalid_buildup_is_hidden_and_finite)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int palette : {-1, 0, 1, 4})
        for (float value : {-1.0f, nan, inf}) {
            const Buildup badAge = visualBuildup(palette, true, value, 30.0f);
            const Buildup badLife = visualBuildup(palette, true, 1.0f, value);
            CHECK_EQ(badAge.density, 0.0f);
            CHECK_EQ(badLife.density, 0.0f);
            CHECK(std::isfinite(badAge.formation));
            CHECK(std::isfinite(badLife.formation));
        }
    CHECK_EQ(visualBuildup(1, true, 0.0f, 0.0f).density, 0.0f);
    CHECK_EQ(visualBuildup(-1, true, 1.0f, 30.0f).density, 0.0f);
}

TEST(hazard_cloud_fringe_covers_original_rotated_breathing_quads)
{
    CHECK_EQ(visualFringe(0.0f), 0.0f);
    CHECK_NEAR(visualFringe(42.82f), std::sqrt(2.0f) * 1.04f * 42.82f, 0.000001f);
    CHECK(visualFringe(50.0f) > 50.0f * std::sqrt(2.0f));
    CHECK_EQ(visualFringe(-1.0f), 0.0f);
    CHECK_EQ(visualFringe(std::numeric_limits<float>::infinity()), 0.0f);
    CHECK_EQ(visualFringe((std::numeric_limits<float>::max)()), 0.0f);
}

TEST(hazard_cloud_admission_rejects_invalid_values_without_changing_footprints)
{
    CHECK(validField(-100.0f, 200.0f, 30.0f, 300.0f, 18.0f, 1.0f, 1.0f, 89.0f, 2));
    CHECK(validField(0.0f, 0.0f, 0.0f, 7.5f, 18.0f, 0.0f, 0.0f, 0.0f, 3));
    const float good[] = {0.0f, 0.0f, 30.0f, 80.0f, 18.0f, 1.0f, 1.0f, 63.0f};
    for (int slot = 0; slot < 8; ++slot)
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity()}) {
            float values[8];
            for (int i = 0; i < 8; ++i) values[i] = i == slot ? invalid : good[i];
            CHECK(!validField(values[0], values[1], values[2], values[3], values[4],
                values[5], values[6], values[7], 0));
        }
    CHECK(!validField(0, 0, 0, 0, 18, 1, 1, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 0, 1, 1, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 18, -0.1f, 1, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 18, 1.1f, 1, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 18, 1, 1.1f, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 18, 1, 1, -1, 0));
    CHECK(!validField(0, 0, 0, 80, 18, 1, 1, 0, -1));
    CHECK(!validField(0, 0, 0, 80, 18, 1, 1, 0, 4));
    CHECK(!validField(0, 0, 0, 1e30f, 18, 1, 1, 0, 0));
}


// This target deliberately links the no-backend implementation on every
// platform. It keeps the public API and device-free transport independently
// usable when a game build has no volumetric renderer (Windows today).
static_assert(std::is_abstract<HazardCloud::Layer>::value, "Layer must remain a backend interface");
static_assert(std::has_virtual_destructor<HazardCloud::Layer>::value, "Layer is owned through the interface");
static_assert(std::is_same<HazardCloud::Body, HazardCloudFlow::Body>::value, "Flow and renderer share body geometry");

TEST(hazard_cloud_no_backend_reports_unavailable_and_creates_no_layer)
{
    CHECK(!HazardCloud::Supported());
    CHECK(HazardCloud::CreateLayer() == nullptr);
}

TEST(hazard_cloud_neutral_palette_and_cache_policy_need_no_device)
{
    using HazardCloud::Layer;
    CHECK_EQ(HazardCloud::TerrainGridSide, 129u);
    CHECK_EQ(&HazardCloud::defaultPalette(4), &HazardCloud::defaultPalette(0));
    for (unsigned index = 0; index < 4; ++index) {
        const HazardCloud::Palette &palette = HazardCloud::defaultPalette(index);
        CHECK(std::isfinite(palette.main.r) && std::isfinite(palette.shadow.r));
        CHECK(palette.brightness > 0.0f && palette.brightness <= 2.0f);
        CHECK(palette.transparency >= 0.0f && palette.transparency <= 1.0f);
    }
    HazardCloud::FieldState fields[2] = {
        {{0, 0, 0}, 40, 18, 1, 0, 1, 0, 0},
        {{80, 0, 0}, 80, 18, 1, 1, 1, 0, 0}
    };
    const std::uint64_t shared = Layer::estimateCacheBytes(nullptr, 0);
    const std::uint64_t first = Layer::estimateCacheBytes(fields, 1);
    const std::uint64_t second = Layer::estimateCacheBytes(fields + 1, 1);
    CHECK(first > shared);
    CHECK(second > first);
    CHECK_EQ(Layer::estimateCacheBytes(fields, 2), first + second - shared);
    const std::uint64_t rejected = (std::numeric_limits<std::uint64_t>::max)();
    CHECK_EQ(Layer::estimateCacheBytes(nullptr, 1), rejected);
    CHECK_EQ(Layer::estimateCacheBytes(fields, 3), rejected);
    fields[0].radius = std::numeric_limits<float>::quiet_NaN();
    CHECK_EQ(Layer::estimateCacheBytes(fields, 1), rejected);
}

TEST(hazard_cloud_neutral_flow_advances_and_resets_without_a_renderer)
{
    HazardCloudFlow::Flow flow;
    CHECK(!flow.valid());
    flow.setDomain(-80, -40, 80, 40);
    CHECK(flow.valid());
    const HazardCloud::Body body = {0, 0, 0, 30, 0, 3, 15};
    for (unsigned step = 0; step < 30; ++step)
        flow.step(1.0f / 60.0f, &body, 1);
    CHECK(flow.valid());
    CHECK_EQ(flow.activeBodies(), 1u);
    CHECK(flow.maxDisturbance() > 0.0f && flow.maxDisturbance() <= 1.0f);
    const float disturbance = flow.maxDisturbance();
    flow.step(0.0f, nullptr, 0);
    CHECK_EQ(flow.maxDisturbance(), disturbance);
    flow.reset();
    CHECK(flow.valid());
    CHECK_EQ(flow.domainMinX(), -80.0f);
    CHECK_EQ(flow.domainMaxY(), 40.0f);
    CHECK_EQ(flow.activeBodies(), 0u);
    CHECK_EQ(flow.maxDisturbance(), 0.0f);
    for (unsigned slot = 0; slot < flow.width() * flow.height() * 4; ++slot)
        if (flow.data()[slot] != 0.0f) {
            CHECK(false);
            break;
        }
}

namespace {

// The device-free core with no device under it: what a backend would bake and upload.
class CoreProbe final : public HazardCloud::LayerCore {
public:
    bool initialize() override { initialized = true; return true; }
    bool render(const HazardCloud::Camera &) override { return false; }
    std::uint64_t estimatedCacheBytes() const override { return 0; }
    void shutdown() override {}
    using LayerCore::flowUploaded;
    bool bakesFrom(float cameraX)
    {
        // A perspective camera standing back from the field, moved sideways by cameraX.
        HazardCloud::Camera camera = {
            {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, cameraX, 0, 400, 1},
            {1, 0, 0, 0, 0, 1.7f, 0, 0, 0, 0, 1.001f, 1, 0, 0, -1.001f, 0},
            0, 0, 1280, 720, 0, 1 };
        HazardCloud::Uniforms uniforms;
        float viewProjection[16];
        CHECK(buildUniforms(camera, 1280, 720, uniforms, viewProjection));
        return takeBake(uniforms);
    }
};

} // namespace

TEST(hazard_cloud_atlases_rebake_on_a_change_and_at_most_twenty_times_a_second)
{
    CoreProbe layer;
    layer.initialize();
    const HazardCloud::FieldState field = {{0, 0, 0}, 40, 18.61f, 1, 0, 1, 0, 10};
    layer.setFields(&field, 1);
    layer.update(1.0f);
    CHECK(layer.bakesFrom(0));
    CHECK(!layer.bakesFrom(0));
    CHECK(!layer.bakesFrom(50));      // the camera never reaches the atlases
    layer.update(1.01f);              // aging inside one bake period
    CHECK(!layer.bakesFrom(0));
    layer.update(1.06f);              // and into the next
    CHECK(layer.bakesFrom(0));
    HazardCloud::Palette palette = HazardCloud::defaultPalette(0);
    palette.brightness = 1.0f;
    layer.setPalette(0, palette);     // a structural change shows on the next frame
    CHECK(layer.bakesFrom(0));
    HazardCloud::FieldState wider = field;
    wider.radius = 60;
    layer.setFields(&wider, 1);
    CHECK(layer.bakesFrom(0));
    CHECK_EQ(layer.bakes(), 4u);
}

TEST(hazard_cloud_quiet_wake_grid_uploads_nothing)
{
    CoreProbe layer;
    layer.initialize();
    const HazardCloud::FieldState field = {{0, 0, 0}, 40, 18.61f, 1, 0, 1, 0, 10};
    layer.setFields(&field, 1);
    CHECK(layer.flowNeedsUpload());
    layer.flowUploaded();
    float seconds = 1.0f;
    for (unsigned frame = 0; frame < 30; ++frame) layer.update(seconds += 1.0f / 60.0f);
    CHECK(!layer.flowNeedsUpload());  // no infantry, nothing to heal
    const HazardCloud::Body walker = {0, 0, 0, 30, 0, 3, 15};
    layer.setBodies(&walker, 1);
    for (unsigned frame = 0; frame < 10; ++frame) layer.update(seconds += 1.0f / 60.0f);
    CHECK(layer.flowNeedsUpload());
    CHECK(layer.maxDisturbance() > 0.0f);
    layer.flowUploaded();
    layer.setBodies(nullptr, 0);
    for (unsigned frame = 0; frame < 60 * 20 && layer.maxDisturbance() > 0.0f; ++frame)
        layer.update(seconds += 1.0f / 60.0f);
    CHECK_EQ(layer.maxDisturbance(), 0.0f);  // healed, then put at rest
    layer.flowUploaded();
    layer.update(seconds += 1.0f / 60.0f);
    CHECK(!layer.flowNeedsUpload());
    CHECK(layer.valid());
}

TEST(hazard_cloud_shader_text_drops_register_spaces_only_for_direct3d_11)
{
    using HazardCloud::Pass;
    for (Pass pass : {Pass::Volume, Pass::Composite, Pass::Density, Pass::Lighting, Pass::Heat}) {
        CHECK(HazardCloud::pixelShader(pass, true).find(", space2)") != std::string::npos);
        CHECK(HazardCloud::pixelShader(pass, false).find(", space") == std::string::npos);
    }
    CHECK(HazardCloud::emberVertexShader(false).find(", space") == std::string::npos);
    CHECK(HazardCloud::emberPixelShader(false).find("register(b0)") != std::string::npos);
}
