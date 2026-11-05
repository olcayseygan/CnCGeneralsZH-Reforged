#pragma once

#include <cmath>
#include <algorithm>

// Client-only recognition and shaping. Gameplay supplies the radius and lifetime;
// an unrecognized effect remains on its existing particle path.
namespace HazardCloudPolicy {

inline bool sameName(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        unsigned char ac = static_cast<unsigned char>(*a++);
        unsigned char bc = static_cast<unsigned char>(*b++);
        if (ac >= 'A' && ac <= 'Z') ac += 'a' - 'A';
        if (bc >= 'A' && bc <= 'Z') bc += 'a' - 'A';
        if (ac != bc) return false;
    }
    return *a == *b;
}

// Short impact splashes and model-attached leaks have no gameplay hazard owner.
// Keep their identity, support and lifetime separate from the lingering fields.
inline int paletteForParticleSource(const char *name)
{
    if (sameName(name, "ToxinPuddle") || sameName(name, "ToxinPuddleContinuous")
        || sameName(name, "ToxinTankPuddle")) return 0;
    if (sameName(name, "AnthraxPuddle") || sameName(name, "AnthraxPuddleContinuous")) return 2;
    if (sameName(name, "GC_Chem_ToxinPuddle") || sameName(name, "AnthraxGammaPuddleContinuous")) return 3;
    return -1;
}

inline float particleSourceHeight(float radius, float fringe)
{
    return (std::min)(18.61f, (std::max)(3.0f, (radius + fringe) * .25f));
}

inline bool attachedParticleSource(const char *name)
{
    return sameName(name, "ToxinPuddleContinuous") || sameName(name, "AnthraxPuddleContinuous")
        || sameName(name, "AnthraxGammaPuddleContinuous") || sameName(name, "ToxinTankPuddle");
}

// Palette indices are shared with the approved preview: toxin, nuclear, beta, gamma.
inline int paletteForTemplate(const char *name, bool sharedClouds = true)
{
    static const char *const names[][3] = {
        {"PoisonFieldSmall", "PoisonFieldMedium", "PoisonFieldLarge"},
        {"RadiationFieldSmall", "RadiationFieldMedium", "RadiationFieldLarge"},
        {"AnthraxFieldSmall", "AnthraxFieldMedium", "AnthraxFieldLarge"},
        {"GC_Chem_AnthraxGammaFieldSmall", "GC_Chem_AnthraxFieldGammaMedium",
            "GC_Chem_AnthraxFieldGammaLarge"}
    };
    for (int palette = 0; palette < 4; ++palette)
        for (int size = 0; size < 3; ++size)
            if (sameName(name, names[palette][size])) return palette;
    // Contaminate emits these ground effects while the tractor is firing. The
    // lingering PoisonField/AnthraxField systems above only appear after it stops.
    if (sharedClouds) {
        if (sameName(name, "ToxinPuddleLarge")) return 0;
        if (sameName(name, "AnthraxPuddleLarge")) return 2;
        if (sameName(name, "AnthraxGammaPuddleLarge")) return 3;
    }
    return sameName(name, "NukeRadiationInitial") ? 1 : -1;
}

// The caller must additionally require CLEANUP_HAZARD and a live LifetimeUpdate.
// Owner names are object templates, not the particle or weapon names.
inline int paletteForOwner(const char *name)
{
    static const struct { const char *name; int palette; } owners[] = {
        {"PoisonFieldSmall", 0}, {"PoisonFieldMedium", 0}, {"PoisonFieldLarge", 0},
        {"RadiationFieldSmall", 1}, {"RadiationFieldMedium", 1}, {"RadiationFieldLarge", 1},
        {"NukeRadiationFieldWeapon", 1}, {"Nuke_RadiationFieldSmall", 1},
        {"PoisonFieldUpgradedSmall", 2}, {"PoisonFieldUpgradedMedium", 2},
        {"PoisonFieldUpgradedLarge", 2}, {"PoisonFieldAnthraxBomb", 2},
        {"Chem_PoisonFieldGammaSmall", 3}, {"Chem_PoisonFieldGammaMedium", 3},
        {"Chem_PoisonFieldGammaLarge", 3}, {"GC_Chem_PoisonFieldGammaSmall", 3},
        {"GC_Chem_PoisonFieldGammaMedium", 3}, {"GC_Chem_PoisonFieldGammaLarge", 3},
        {"PoisonFieldAnthraxGammaBomb", 3}
    };
    for (const auto &owner : owners)
        if (sameName(name, owner.name)) return owner.palette;
    return -1;
}

inline float smoothProgress(float age, float duration)
{
    const float progress = age <= 0.0f ? 0.0f : age >= duration ? 1.0f : age / duration;
    return progress * progress * (3.0f - 2.0f * progress);
}

struct Buildup {
    float formation; // Radial buildup for toxin; always full footprint for nuclear.
    float density;   // Multiply by the runtime's existing life/visibility fade.
    float release;
};

inline Buildup particleSourceBuildup(float age, float lifetime, float left)
{
    if (!std::isfinite(age) || !std::isfinite(lifetime) || !std::isfinite(left)
        || age < 0 || lifetime <= 0 || left <= 0) return {0, 0, 0};
    const float rise = (std::min)(.1f, lifetime * .25f);
    const float decay = (std::min)(.35f, lifetime * .25f);
    const float formation = smoothProgress(age, rise);
    return {formation, formation * (std::min)(1.f, left / decay), .35f};
}

inline Buildup visualBuildup(int palette, bool largeNuclear, float ageSeconds,
    float lifetimeSeconds)
{
    const bool nuclear = palette == 1;
    const Buildup hidden = {nuclear ? 1.0f : 0.0f, 0.0f, 0.0f};
    if (palette < 0 || palette > 3 || !std::isfinite(ageSeconds)
        || !std::isfinite(lifetimeSeconds) || ageSeconds < 0.0f || lifetimeSeconds <= 0.0f)
        return hidden;
    if (ageSeconds >= lifetimeSeconds) return {1.0f, 0.0f, 0.0f};

    // Small radiation fields live only a few seconds. Never let a decorative
    // buildup spend most of their actual damaging lifetime nearly invisible.
    const float nominal = nuclear && largeNuclear ? 6.0f : 3.0f;
    const float quarterLife = lifetimeSeconds * 0.25f;
    const float ramp = quarterLife < nominal ? quarterLife : nominal;
    if (!(ramp > 0.0f)) return hidden;
    const float progress = smoothProgress(ageSeconds, ramp);
    const float densityRamp = ramp < 0.6f ? ramp : 0.6f;
    return {nuclear ? 1.0f : progress,
        nuclear ? progress : smoothProgress(ageSeconds, densityRamp),
        ageSeconds < ramp ? 1.0f - 0.65f * progress : 0.0f};
}

inline float visualFringe(float halfLifePuddleSize)
{
    if (!std::isfinite(halfLifePuddleSize) || halfLifePuddleSize < 0.0f) return 0.0f;
    const float fringe = std::sqrt(2.0f) * 1.04f * halfLifePuddleSize;
    return std::isfinite(fringe) ? fringe : 0.0f;
}

inline bool validField(float x, float y, float z, float radius, float height,
    float density, float formation, float fringe, int palette)
{
    if (palette < 0 || palette > 3 || !std::isfinite(x) || !std::isfinite(y)
        || !std::isfinite(z) || !std::isfinite(radius) || !std::isfinite(height)
        || !std::isfinite(density) || !std::isfinite(formation) || !std::isfinite(fringe)
        || radius <= 0.0f || height <= 0.0f || density < 0.0f || density > 1.0f
        || formation < 0.0f || formation > 1.0f || fringe < 0.0f)
        return false;
    const float reach = radius + fringe;
    // Guard operations used by support bounds and radial distance evaluation.
    return std::isfinite(reach * reach) && std::isfinite(x - reach)
        && std::isfinite(x + reach) && std::isfinite(y - reach)
        && std::isfinite(y + reach) && std::isfinite(z + height * 2.6f);
}

} // namespace HazardCloudPolicy
