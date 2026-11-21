// RTS-owned conversion of authored placement / portrait inputs to generic
// presentation descriptions. No dependency on legacy RtsView or its GPU state.
// The host maps these descriptions into a View model packet or a specialized
// model/stage adapter until legacy material rendering is fully migrated.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::game::view_adapter {

struct GhostPresentation {
    uint16_t type=0;
    uint8_t owner=1;
    render::float3 position{};
    render::float4 tint{1,1,1,1};
};
inline GhostPresentation makeGhostPresentation(
        uint16_t type,render::float3 position,bool allowed,double seconds) {
    const float pulse=.4f+.08f*float(std::sin(seconds*5.0));
    return {type,1,position,
        allowed?render::float4{.45f,1.f,.55f,pulse}
               :render::float4{1.f,.35f,.3f,pulse}};
}

struct ModelPreviewPresentation {
    uint16_t type=0;
    uint8_t owner=0;
    render::float3 facing{1,0,0};
    uint64_t sourceKey=0;
};
inline ModelPreviewPresentation makeModelPreviewPresentation(
        uint16_t type,uint8_t owner,float yaw) {
    return {type,owner,{std::cos(yaw),0,-std::sin(yaw)},
        (uint64_t{1}<<62)|(uint64_t(owner)<<16)|uint64_t(type)};
}

// Keep the model preview's previous max-frame-step and initial zero-dt
// semantics independent from RtsView animation state ownership.
inline float previewFrameDt(double now,double previous) {
    return previous<0 ? 0.f : float(std::clamp(now-previous,0.0,0.1));
}

} // namespace aiverse::game::view_adapter
