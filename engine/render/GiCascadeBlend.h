// CPU reference and shader contract for blending up to three nested GI clipmaps.
// Each level contributes *indirect-only* linear RGB. The compositor preserves
// energy: a coarse level fills the fraction the fine level cannot provide,
// and unrepresented space contributes zero rather than inventing illumination.
// It deliberately does not include direct sun, point lights or baked GI.
#pragma once
#include "Math3D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace aiverse::render {

struct GiCascadeBlendLevel {
    float3 origin{};
    float spacing = 1;
    std::array<int,3> dims{1,1,1};
    float3 radiance{};
    float validFraction = 1;  // effective fraction of interpolated resident probes
    float fadeCells = 1.5f;   // border transition measured in probe cells
};

struct GiCascadeBlendResult {
    float3 indirect{};
    std::array<float,3> levelWeights{};
    float uncovered = 1;
};

inline float giCascadeCoverage(const GiCascadeBlendLevel& level,float3 worldPos) {
    const auto finite=[](float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    };
    if(!finite(level.origin)||!finite(worldPos)||
       !std::isfinite(level.spacing)||level.spacing<=0 ||
       !std::isfinite(level.fadeCells)||level.fadeCells<=0 ||
       level.dims[0]<1||level.dims[1]<1||level.dims[2]<1)
        throw std::invalid_argument("invalid GI cascade coverage input");
    const float3 p=(worldPos-level.origin)*(1.0f/level.spacing);
    const float values[3]={p.x,p.y,p.z};
    float border=1e30f;
    for(int axis=0;axis<3;++axis) {
        const float distance=std::min(values[axis],float(level.dims[axis])-values[axis]);
        if(distance<=0)return 0.f;
        border=std::min(border,distance);
    }
    const float t=std::clamp(border/level.fadeCells,0.f,1.f);
    return t*t*(3.f-2.f*t);
}

inline GiCascadeBlendResult composeCascadeGi(
    const std::array<GiCascadeBlendLevel,3>& levels,
    size_t count,float3 receiverPosition) {
    if(count==0 || count>levels.size())
        throw std::invalid_argument("GI cascade count must be 1..3");
    GiCascadeBlendResult result;
    float remaining=1.f;
    for(size_t i=0;i<count;++i) {
        const auto& level=levels[i];
        if(!std::isfinite(level.validFraction) ||
           level.validFraction<0 || level.validFraction>1 ||
           !std::isfinite(level.radiance.x) || !std::isfinite(level.radiance.y) ||
           !std::isfinite(level.radiance.z) ||
           level.radiance.x<0 || level.radiance.y<0 || level.radiance.z<0)
            throw std::invalid_argument("invalid GI cascade radiance or validity");
        const float coverage=giCascadeCoverage(level,receiverPosition);
        const float weight=remaining*coverage*level.validFraction;
        result.levelWeights[i]=weight;
        result.indirect += level.radiance*weight;
        remaining=std::max(0.f,remaining-weight);
    }
    result.uncovered=remaining;
    return result;
}

} // namespace aiverse::render
