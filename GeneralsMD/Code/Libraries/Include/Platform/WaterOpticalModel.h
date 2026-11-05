/* Water light transport shared by generated HLSL and its numerical regression checks. */
#pragma once
#include <algorithm>
#include <cmath>

// Keep the existing shallow-water spectrum and its standing-water path scale in one definition.
#define WATER_OPTICAL_CONSTANTS(X) X(0.065f, 0.026f, 0.019f, 2.4f, 0.25f, 0.01f, 80.0f, 1.0e20f)
#define WATER_OPTICAL_CLARITY_CONSTANTS(X) X(2.5f, 5.5f, 0.5f, 1.5f)
#define WATER_OPTICAL_RAY_CONSTANTS(X) X(0.2f, 0.08f, 2.0f)

// Clear/coastal and river retain their current source; ocean retains the current FFT endpoint.
// These source-function radiances are linear RGB. River extinction remains independently defined.
#define WATER_OPTICAL_SOURCE_PRESETS(X) X(0.002f,0.028f,0.041f, 0.002f,0.028f,0.041f, 0.002f,0.035f,0.075f, 0.08f,0.40f,1.80f)

namespace WaterOpticalModel {
struct Rgb { float r,g,b; };
#define WATER_OPTICAL_CPP_CONSTANTS(red,green,blue,standing,minDepth,minTransmission,maxExponent,maxFinite) \
    constexpr Rgb BaseExtinction={red,green,blue}; \
    constexpr float StandingPathScale=standing,MinimumReferenceDepth=minDepth; \
    constexpr float MinimumReferenceTransmission=minTransmission,MaximumExponent=maxExponent,MaximumFiniteInput=maxFinite;
WATER_OPTICAL_CONSTANTS(WATER_OPTICAL_CPP_CONSTANTS)
#undef WATER_OPTICAL_CPP_CONSTANTS
#define WATER_OPTICAL_RAY_CPP_CONSTANTS(grazing,margin,boundScale) \
    constexpr float RayMinimumViewZ=grazing,RaySurfaceMargin=margin,RayHeightBoundScale=boundScale;
WATER_OPTICAL_RAY_CONSTANTS(WATER_OPTICAL_RAY_CPP_CONSTANTS)
#undef WATER_OPTICAL_RAY_CPP_CONSTANTS

#define WATER_OPTICAL_CLARITY_CPP_CONSTANTS(shallow,deep,reliefStart,reliefFull) \
    constexpr float ClarityShallowDepth=shallow,ClarityMaximumDepth=deep; \
    constexpr float ClarityReliefStart=reliefStart,ClarityReliefFull=reliefFull;
WATER_OPTICAL_CLARITY_CONSTANTS(WATER_OPTICAL_CLARITY_CPP_CONSTANTS)
#undef WATER_OPTICAL_CLARITY_CPP_CONSTANTS

// Comparisons also define a deterministic NaN/Infinity policy in both languages: NaN and negative
// path/weights become zero, positive infinity saturates, and invalid reference depth uses 0.25.
inline float Unit(float value) { return value>=0.0f?(std::min)(value,1.0f):0.0f; }
inline float Path(float value) { return value>=0.0f?(std::min)(value,MaximumFiniteInput):0.0f; }
inline float ReferenceDepth(float value) {
    return value>=MinimumReferenceDepth?(std::min)(value,MaximumFiniteInput):MinimumReferenceDepth;
}
// Only terrain depths classify a shallow shelf. A ship hull or submerged object
// can shorten the optical ray without turning a broad offshore basin into clear water.
// The caller supplies the deepest nearby wet terrain at the same water level.
// Eligibility scales only authored-opacity relaxation; natural extinction stays intact.
inline float ClarityEligibility(float terrainDepth,float surroundingDepth,float offshore) {
    if (!(std::abs(terrainDepth)<=MaximumFiniteInput) || !(std::abs(surroundingDepth)<=MaximumFiniteInput)
        || !(std::abs(offshore)<=MaximumFiniteInput)) return 0.0f;
    const float depth=Unit(((std::max)(terrainDepth,0.0f)-ClarityShallowDepth)/(ClarityMaximumDepth-ClarityShallowDepth));
    const float relief=Unit(((std::max)(surroundingDepth-terrainDepth,0.0f)-ClarityReliefStart)
        /(ClarityReliefFull-ClarityReliefStart));
    const float depthWeight=1.0f-depth*depth*(3.0f-2.0f*depth);
    const float shelfWeight=relief*relief*(3.0f-2.0f*relief);
    return depthWeight*(1.0f+Unit(offshore)*(shelfWeight-1.0f));
}
// Positive movement is toward the camera along its normalized view ray. A finer optical surface
// may correct raster interpolation, but must never pass the actual scene hit or the camera. The
// original raster surface is retained for clear water, below-surface views and invalid directions.
inline float RayShift(float heightDelta,float viewZ,float offshoreFactor,float displacementBound,
    float sceneRayDistance,float cameraDistance) {
    if (!(offshoreFactor>0.0f) || !(viewZ>0.0f) || !(viewZ<=MaximumFiniteInput)
        || !(std::abs(heightDelta)<=MaximumFiniteInput)) return 0.0f;
    const float bound=RayHeightBoundScale*Path(displacementBound);
    const float shift=Unit(offshoreFactor)*(std::max)(-bound,(std::min)(heightDelta,bound))
        /(std::max)(viewZ,RayMinimumViewZ);
    const float sceneLimit=(std::max)(Path(sceneRayDistance)-RaySurfaceMargin,0.0f);
    const float cameraLimit=(std::max)(Path(cameraDistance)-RaySurfaceMargin,0.0f);
    return (std::max)(-sceneLimit,(std::min)(shift,cameraLimit));
}
inline float OffshoreExtinction(float transparentDepth,float minOpacity) {
    const float remaining=(std::max)(1.0f-Unit(minOpacity),MinimumReferenceTransmission);
    return -std::log(remaining)/ReferenceDepth(transparentDepth);
}
inline Rgb Transmission(float path,float riverBlend,float offshoreFactor,float referenceDepth,float opacity,float clarity=0.0f) {
    path=Path(path);
    const float materialPath=path*(StandingPathScale+(1.0f-StandingPathScale)*Unit(riverBlend));
    // Authored opacity describes extinction per physical world distance. Applying the legacy
    // standing-water multiplier to this term a second time would obscure shallow seas too much.
    const float authored=path*Unit(offshoreFactor)*(1.0f-Unit(clarity))*OffshoreExtinction(referenceDepth,opacity);
    return {std::exp(-(std::min)(materialPath*BaseExtinction.r+authored,MaximumExponent)),
        std::exp(-(std::min)(materialPath*BaseExtinction.g+authored,MaximumExponent)),
        std::exp(-(std::min)(materialPath*BaseExtinction.b+authored,MaximumExponent))};
}
// Source-function radiances in linear RGB; these are not extinction coefficients.
#define WATER_OPTICAL_SOURCE_CPP(clearR,clearG,clearB,riverR,riverG,riverB,oceanR,oceanG,oceanB,ambient,faceMin,faceRange) \
    constexpr Rgb ClearSource={clearR,clearG,clearB},RiverSource={riverR,riverG,riverB},OceanSource={oceanR,oceanG,oceanB}; \
    constexpr float SourceAmbient=ambient,SourceFaceMinimum=faceMin,SourceFaceRange=faceRange;
WATER_OPTICAL_SOURCE_PRESETS(WATER_OPTICAL_SOURCE_CPP)
#undef WATER_OPTICAL_SOURCE_CPP
inline float SourceOffshore(float riverBlend,float effectiveOffshore) {
    // The caller has already excluded the river share from offshore. Cap malformed overlap;
    // multiplying by (1-river) here would apply that gate twice at continuous transitions.
    return (std::min)(Unit(effectiveOffshore),1.0f-Unit(riverBlend));
}
inline Rgb MaterialSource(float riverBlend,float effectiveOffshore) {
    const float river=Unit(riverBlend),offshore=SourceOffshore(river,effectiveOffshore);
    // Convex context weights are clear=(1-river-offshore), river, and offshore. Nearshore
    // connected water blends from clear to ocean; enclosed pools retain the clear endpoint.
    return {ClearSource.r+(RiverSource.r-ClearSource.r)*river+(OceanSource.r-ClearSource.r)*offshore,
        ClearSource.g+(RiverSource.g-ClearSource.g)*river+(OceanSource.g-ClearSource.g)*offshore,
        ClearSource.b+(RiverSource.b-ClearSource.b)*river+(OceanSource.b-ClearSource.b)*offshore};
}
inline Rgb ScatteringSource(float riverBlend,float effectiveOffshore,float daylight,float faceLight) {
    const Rgb material=MaterialSource(riverBlend,effectiveOffshore);
    const float light=SourceAmbient+(1.0f-SourceAmbient)*Unit(daylight);
    const float face=1.0f+SourceOffshore(riverBlend,effectiveOffshore)
        *(SourceFaceMinimum+SourceFaceRange*Unit(faceLight)-1.0f);
    return {material.r*light*face,material.g*light*face,material.b*light*face};
}
inline Rgb Body(Rgb scene,Rgb transmission,Rgb source,float caustic,float sceneAvailable) {
    const Rgb integrated={scene.r*transmission.r*caustic+source.r*(1.0f-transmission.r),
        scene.g*transmission.g*caustic+source.g*(1.0f-transmission.g),
        scene.b*transmission.b*caustic+source.b*(1.0f-transmission.b)};
    const float available=Unit(sceneAvailable);
    return {source.r+(integrated.r-source.r)*available,source.g+(integrated.g-source.g)*available,
        source.b+(integrated.b-source.b)*available};
}

inline const char *GetHlslSource() {
#define WATER_OPTICAL_HLSL(red,green,blue,standing,minDepth,minTransmission,maxExponent,maxFinite) \
    "static const float WaterOpticalMaximumFiniteInput=" #maxFinite ";\n" \
    "float WaterOpticalUnit(float v) { return v>=0.0?min(v,1.0):0.0; }\n" \
    "float WaterOpticalPath(float v) { return v>=0.0?min(v," #maxFinite "):0.0; }\n" \
    "float WaterOpticalOffshoreExtinction(float depth,float opacity) {\n" \
    " float reference=depth>=" #minDepth "?min(depth," #maxFinite "):" #minDepth ";\n" \
    " return -log(max(1.0-WaterOpticalUnit(opacity)," #minTransmission "))/reference; }\n" \
    "float3 WaterOpticalTransmission(float path,float riverBlend,float offshoreFactor,float referenceDepth,float opacity) {\n" \
    " path=WaterOpticalPath(path);\n" \
    " float materialPath=path*(" #standing "+(1.0-" #standing ")*WaterOpticalUnit(riverBlend));\n" \
    " float authored=path*WaterOpticalUnit(offshoreFactor)*WaterOpticalOffshoreExtinction(referenceDepth,opacity);\n" \
    " float3 exponent=materialPath*float3(" #red "," #green "," #blue ")+authored;\n" \
    " return exp(-min(exponent,float3(" #maxExponent "," #maxExponent "," #maxExponent "))); }\n"
#define WATER_OPTICAL_RAY_HLSL(grazing,margin,boundScale) \
    "float WaterOpticalRayShift(float heightDelta,float viewZ,float offshoreFactor,float displacementBound,float sceneRayDistance,float cameraDistance) {\n" \
    " if (!(offshoreFactor>0.0) || !(viewZ>0.0) || !(viewZ<=WaterOpticalMaximumFiniteInput) || !(abs(heightDelta)<=WaterOpticalMaximumFiniteInput)) return 0.0;\n" \
    " float bound=" #boundScale "*WaterOpticalPath(displacementBound);\n" \
    " float shift=WaterOpticalUnit(offshoreFactor)*clamp(heightDelta,-bound,bound)/max(viewZ," #grazing ");\n" \
    " float sceneLimit=max(WaterOpticalPath(sceneRayDistance)-" #margin ",0.0);\n" \
    " float cameraLimit=max(WaterOpticalPath(cameraDistance)-" #margin ",0.0);\n" \
    " return clamp(shift,-sceneLimit,cameraLimit); }\n"
#define WATER_OPTICAL_SOURCE_HLSL(clearR,clearG,clearB,riverR,riverG,riverB,oceanR,oceanG,oceanB,ambient,faceMin,faceRange) \
    "float WaterOpticalSourceOffshore(float riverBlend,float effectiveOffshore) {\n" \
    " return min(WaterOpticalUnit(effectiveOffshore),1.0-WaterOpticalUnit(riverBlend)); }\n" \
    "float3 WaterOpticalMaterialSource(float riverBlend,float effectiveOffshore) {\n" \
    " float river=WaterOpticalUnit(riverBlend),offshore=WaterOpticalSourceOffshore(river,effectiveOffshore);\n" \
    " float3 clearSource=float3(" #clearR "," #clearG "," #clearB ");\n" \
    " float3 riverSource=float3(" #riverR "," #riverG "," #riverB ");\n" \
    " float3 oceanSource=float3(" #oceanR "," #oceanG "," #oceanB ");\n" \
    " return clearSource+(riverSource-clearSource)*river+(oceanSource-clearSource)*offshore; }\n" \
    "float3 WaterOpticalScatteringSource(float riverBlend,float effectiveOffshore,float daylight,float faceLight) {\n" \
    " float illumination=" #ambient "+(1.0-" #ambient ")*WaterOpticalUnit(daylight);\n" \
    " float face=1.0+WaterOpticalSourceOffshore(riverBlend,effectiveOffshore)*(" #faceMin "+" #faceRange "*WaterOpticalUnit(faceLight)-1.0);\n" \
    " return WaterOpticalMaterialSource(riverBlend,effectiveOffshore)*illumination*face; }\n" \
    "float3 WaterOpticalBody(float3 scene,float3 transmission,float3 source,float caustic,float sceneAvailable) {\n" \
    " return lerp(source,scene*transmission*caustic+source*(1.0-transmission),WaterOpticalUnit(sceneAvailable)); }\n"

    return WATER_OPTICAL_CONSTANTS(WATER_OPTICAL_HLSL) WATER_OPTICAL_RAY_CONSTANTS(WATER_OPTICAL_RAY_HLSL)
        WATER_OPTICAL_SOURCE_PRESETS(WATER_OPTICAL_SOURCE_HLSL);
#undef WATER_OPTICAL_SOURCE_HLSL
#undef WATER_OPTICAL_RAY_HLSL
#undef WATER_OPTICAL_HLSL
}
inline const char *GetClarityHlslSource() {
#define WATER_OPTICAL_CLARITY_HLSL(shallow,deep,reliefStart,reliefFull) \
    "static const float WaterOpticalClarityMaximumDepth=" #deep ";\n" \
    "float WaterOpticalClarityEligibility(float terrainDepth,float surroundingDepth,float offshore) {\n" \
    " if (!(abs(terrainDepth)<=WaterOpticalMaximumFiniteInput) || !(abs(surroundingDepth)<=WaterOpticalMaximumFiniteInput) || !(abs(offshore)<=WaterOpticalMaximumFiniteInput)) return 0.0;\n" \
    " float depth=WaterOpticalUnit((max(terrainDepth,0.0)-" #shallow ")/(" #deep "-" #shallow "));\n" \
    " float relief=WaterOpticalUnit((max(surroundingDepth-terrainDepth,0.0)-" #reliefStart ")/(" #reliefFull "-" #reliefStart "));\n" \
    " float depthWeight=1.0-depth*depth*(3.0-2.0*depth),shelfWeight=relief*relief*(3.0-2.0*relief);\n" \
    " return depthWeight*(1.0+WaterOpticalUnit(offshore)*(shelfWeight-1.0)); }\n"
#define WATER_OPTICAL_CLARITY_TRANSMISSION(red,green,blue,standing,minDepth,minTransmission,maxExponent,maxFinite) \
    "float3 WaterClarityTransmission(float path,float riverBlend,float offshoreFactor,float referenceDepth,float opacity,float clarity) {\n" \
    " path=WaterOpticalPath(path); float materialPath=path*(" #standing "+(1.0-" #standing ")*WaterOpticalUnit(riverBlend));\n" \
    " float authored=path*WaterOpticalUnit(offshoreFactor)*(1.0-WaterOpticalUnit(clarity))*WaterOpticalOffshoreExtinction(referenceDepth,opacity);\n" \
    " return exp(-min(materialPath*float3(" #red "," #green "," #blue ")+authored,float3(" #maxExponent "," #maxExponent "," #maxExponent "))); }\n"
    return WATER_OPTICAL_CLARITY_CONSTANTS(WATER_OPTICAL_CLARITY_HLSL)
        WATER_OPTICAL_CONSTANTS(WATER_OPTICAL_CLARITY_TRANSMISSION);
#undef WATER_OPTICAL_CLARITY_TRANSMISSION
#undef WATER_OPTICAL_CLARITY_HLSL
}

}
