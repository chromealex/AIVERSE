// Stage 7AX — explicit single-pixel GPU colour vs triangle-ID coverage
// contract. The *caller* owns same-frame screen and RHI ID readbacks;
// this pure C++20 function cannot infer GPU/depth parity without pixels.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace aiverse::vivid {

struct SculptGpuColorPixelResult {
    enum class Verdict : std::uint8_t {
        Covered, ClearBackground, Mismatch, Inconclusive
    };
    Verdict verdict=Verdict::Inconclusive;
    int distanceFromClear=0;
    bool triangleIdVisible=false;
};

// The ID target is linear RGBA8 with ID 0 as clear. Screen RGBA8 may have
// colour-space differences, so do NOT compare its channels with ID bytes.
// Only assert whether a probe pixel is visibly different from the known
// presentation clear colour. This is a *coverage* oracle, not a numerical
// RGB/depth parity oracle. Positive probes must use a strong flat-shaded
// actor colour, separated from the clear colour by the specified margin.
// A nonzero ID but clear-colour screen is necessarily a mismatch; a zero
// ID with nonclear screen may be an unrelated occluder/overlay and remains
// inconclusive until that occluder is matched from Renderer3D.
inline SculptGpuColorPixelResult compareSculptGpuColorIdCoverage(
        const std::array<std::uint8_t,4>& screenRgba,
        const std::array<std::uint8_t,4>& triangleIdRgba,
        const std::array<std::uint8_t,3>& sceneClearRgb,
        int clearDistanceThreshold=36) noexcept {
    SculptGpuColorPixelResult result;
    if(clearDistanceThreshold<0 || clearDistanceThreshold>765)return result;
    result.distanceFromClear=
        std::abs(int(screenRgba[0])-int(sceneClearRgb[0]))+
        std::abs(int(screenRgba[1])-int(sceneClearRgb[1]))+
        std::abs(int(screenRgba[2])-int(sceneClearRgb[2]));
    result.triangleIdVisible=triangleIdRgba[0]!=0 ||
                            triangleIdRgba[1]!=0 ||
                            triangleIdRgba[2]!=0 ||
                            triangleIdRgba[3]!=0;
    const bool colourCovered=result.distanceFromClear>clearDistanceThreshold;
    if(result.triangleIdVisible)
        result.verdict=colourCovered?
            SculptGpuColorPixelResult::Verdict::Covered:
            SculptGpuColorPixelResult::Verdict::Mismatch;
    else
        result.verdict=colourCovered?
            SculptGpuColorPixelResult::Verdict::Inconclusive:
            SculptGpuColorPixelResult::Verdict::ClearBackground;
    return result;
}

// Both samples MUST come from actual GPU Depth32Float attachments in
// the same render frame and at the same top-left pixel. CPU-projected
// depth is not admissible. A clear-depth sample (1.0) cannot prove a
// foreground parity hit, even when both attachments are untouched.
struct SculptGpuDepthComparison {
    bool samplesValid=false;
    bool matching=false;
    float absoluteDifference=0.f;
};
inline SculptGpuDepthComparison compareSculptGpuDepthPixels(
        float sceneDepth01,float idDepth01,
        float tolerance=0.003f) noexcept {
    SculptGpuDepthComparison result;
    if(!std::isfinite(sceneDepth01) || !std::isfinite(idDepth01) ||
       !std::isfinite(tolerance) || tolerance<0.f || tolerance>1.f ||
       sceneDepth01<0.f || sceneDepth01>=1.f ||
       idDepth01<0.f || idDepth01>=1.f)return result;
    result.samplesValid=true;
    result.absoluteDifference=std::fabs(sceneDepth01-idDepth01);
    result.matching=result.absoluteDifference<=tolerance;
    return result;
}

} // namespace aiverse::vivid
