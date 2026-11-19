// TerraWeave GPU crown wind: first-party material ShaderGraph vertexOffset.
// Reuses Renderer3D's existing GLSL/MSL shader-injection and rigid instance U0.
// No new RHI buffers, shader backend, ECS components or per-tree asset clones.
#pragma once

#include "engine/world/TerraWeaveWind.h"
#include "engine/render/graph/ShaderGraph.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace aiverse::terraweave {

// The graph shader uses I.anim only for rigid, non-lightmapped instances
// explicitly opting into ModelDraw::vertexParameters. This Program is trusted
// engine-generated code, NOT arbitrary user-supplied shader source.
// Vertex alpha holds a 0..1 crown height weight and is ignored by the
// foliage fragment material (vertexColors=false).
inline const std::shared_ptr<const render::graph::Program>& canopyWindGraph() {
    static const auto program=[] {
        auto p=std::make_shared<render::graph::Program>();
        p->domain="material";
        p->minQuality=0; // keep the low material-quality/mobile fallback
        p->outputs["color"]="sg_input"; // unchanged fragment PBR/mask output
        p->outputs["vertexOffset"]="tw_offset";
        p->vertexGlsl=R"(
float tw_weight=clamp(sg_color.a,0.0,1.0);
tw_weight*=tw_weight;
float tw_phase=I.anim.z+sg_world.x*0.073+sg_world.z*0.051+
               sg_object.y*0.27;
float tw_wave=0.62+0.38*sin(tw_phase)+
              I.anim.w*0.25*sin(tw_phase*2.13+sg_object.x*0.43);
vec4 tw_offset=vec4(I.anim.x*tw_weight*tw_wave,0.0,
                    I.anim.y*tw_weight*tw_wave,0.0);
)";
        p->vertexMsl=R"(
float tw_weight=clamp(sg_color.a,0.0,1.0);
tw_weight*=tw_weight;
float tw_phase=I.anim.z+sg_world.x*0.073+sg_world.z*0.051+
               sg_object.y*0.27;
float tw_wave=0.62+0.38*sin(tw_phase)+
              I.anim.w*0.25*sin(tw_phase*2.13+sg_object.x*0.43);
float4 tw_offset=float4(I.anim.x*tw_weight*tw_wave,0.0,
                        I.anim.y*tw_weight*tw_wave,0.0);
)";
        return std::shared_ptr<const render::graph::Program>(std::move(p));
    }();
    return program;
}

// Far Horizon cluster wind uses the same per-draw macro vector but a
// separate program, so one batched forest may have region-varying direction,
// amplitude and phase. sg_color RGB is a stable 2.4km world-space noise
// sample (encoded once into forest-card vertices), alpha is canopy weight.
// This avoids rebuilding atlases or sending per-cluster instance uniforms.
inline const std::shared_ptr<const render::graph::Program>&
horizonSpatialWindGraph() {
    static const auto program=[] {
        auto p=std::make_shared<render::graph::Program>(*canopyWindGraph());
        p->vertexGlsl=R"(
float tw_weight=clamp(sg_color.a,0.0,1.0);
tw_weight*=tw_weight;
float tw_angle=(sg_color.r*2.0-1.0)*0.5235987756;
float tw_amp=0.75+sg_color.g*0.5;
float tw_cs=cos(tw_angle),tw_sn=sin(tw_angle);
vec2 tw_wind=vec2(
    I.anim.x*tw_cs-I.anim.y*tw_sn,
    I.anim.x*tw_sn+I.anim.y*tw_cs);
float tw_phase=I.anim.z+sg_color.b*6.28318530718+
               sg_object.y*0.27;
float tw_wave=0.62+0.38*sin(tw_phase)+
              I.anim.w*0.25*sin(tw_phase*2.13);
vec4 tw_offset=vec4(tw_wind.x*tw_weight*tw_amp*tw_wave,
                    0.0,tw_wind.y*tw_weight*tw_amp*tw_wave,0.0);
)";
        p->vertexMsl=R"(
float tw_weight=clamp(sg_color.a,0.0,1.0);
tw_weight*=tw_weight;
float tw_angle=(sg_color.r*2.0-1.0)*0.5235987756;
float tw_amp=0.75+sg_color.g*0.5;
float tw_cs=cos(tw_angle),tw_sn=sin(tw_angle);
float2 tw_wind=float2(
    I.anim.x*tw_cs-I.anim.y*tw_sn,
    I.anim.x*tw_sn+I.anim.y*tw_cs);
float tw_phase=I.anim.z+sg_color.b*6.28318530718+
               sg_object.y*0.27;
float tw_wave=0.62+0.38*sin(tw_phase)+
              I.anim.w*0.25*sin(tw_phase*2.13);
float4 tw_offset=float4(tw_wind.x*tw_weight*tw_amp*tw_wave,
                        0.0,tw_wind.y*tw_weight*tw_amp*tw_wave,0.0);
)";
        return std::shared_ptr<const render::graph::Program>(std::move(p));
    }();
    return program;
}

// Exact remote-cluster wind values, not just the macro + procedural jitter.
// RGB8 = signed X/Z velocity and unsigned gust from the last published
// immutable field. Alpha remains the 0/255 root-to-crown height weight.
// A single first-party program is shared by ALL distant forest cards.
inline const std::shared_ptr<const render::graph::Program>&
horizonExactFieldWindGraph() {
    static const auto program=[] {
        auto p=std::make_shared<render::graph::Program>(*canopyWindGraph());
        p->vertexGlsl=R"(
float tw_weight=clamp(sg_color.a,0.0,1.0);
tw_weight*=tw_weight;
vec2 tw_wind=clamp(
    (sg_color.rg*255.0-vec2(128.0))/127.0,-1.0,1.0);
float tw_phase=I.anim.z+
               sg_world.x*0.007+sg_world.z*0.011+
               sg_object.y*0.27;
float tw_wave=0.62+0.38*sin(tw_phase)+
              sg_color.b*0.25*sin(tw_phase*2.13);
vec4 tw_offset=vec4(
    tw_wind.x*I.anim.x*tw_weight*tw_wave,
    0.0,tw_wind.y*I.anim.x*tw_weight*tw_wave,0.0);
)";
        p->vertexMsl=R"(
float tw_weight=clamp(sg_color.a,0.0,1.0);
tw_weight*=tw_weight;
float2 tw_wind=clamp(
    (sg_color.rg*255.0-float2(128.0))/127.0,-1.0,1.0);
float tw_phase=I.anim.z+
               sg_world.x*0.007+sg_world.z*0.011+
               sg_object.y*0.27;
float tw_wave=0.62+0.38*sin(tw_phase)+
              sg_color.b*0.25*sin(tw_phase*2.13);
float4 tw_offset=float4(
    tw_wind.x*I.anim.x*tw_weight*tw_wave,
    0.0,tw_wind.y*I.anim.x*tw_weight*tw_wave,0.0);
)";
        return std::shared_ptr<const render::graph::Program>(std::move(p));
    }();
    return program;
}

// The exact field carries local X/Z and gust in the uploaded RGB8 colours;
// U0 needs only a bounded PHYSIS-neutral strength policy + simulation phase.
inline render::float4 horizonExactFieldVertexParameters(
        const ForestWindState& policy,uint64_t tick) {
    const float amplitude=0.35f*0.30f*
        (float(policy.maxLeanMilliDegrees)/3000.0f)*
        (float(policy.branchFlexPermille)/1000.0f);
    constexpr float tau=6.2831853071795864769f;
    const float phase=tau*float(tick%policy.cycleTicks)/
                       float(policy.cycleTicks);
    return {amplitude,0.0f,phase,0.0f};
}

// Stable, SMOOTH spatial metadata. All four card corners of one cluster
// receive the same RGB, with only A varying from 0 (base) to 255 (crown).
// Using world-centimetre noise rather than snapped camera region indices
// prevents direction or gust snapping on camera-region rebases.
inline uint32_t horizonSpatialWindVertexColor(
        int64_t worldXcm,int64_t worldZcm,uint64_t terrainSeed,
        bool canopyTop) {
    constexpr int32_t kVariationPeriodCm=240000; // 2.4km
    const auto channel=[&](uint64_t salt)->uint32_t {
        const int64_t noise=detail::noise(
            worldXcm,worldZcm,kVariationPeriodCm,terrainSeed^salt);
        const int64_t encoded=(noise+65535)*255/131070;
        return static_cast<uint32_t>(std::clamp<int64_t>(encoded,0,255));
    };
    const uint32_t direction=channel(0x444952454354494fULL);
    const uint32_t amplitude=channel(0x414d504c49545544ULL);
    const uint32_t phase=channel(0x5048415345484c4fULL);
    return (canopyTop?0xff000000u:0u)|
           (phase<<16)|(amplitude<<8)|direction;
}

// Existing rigid-instance I.anim = {local wind X, local wind Z, phase,
// secondary gust}. Caller supplies PHYSIS-neutral integer wind and tick.
// Convert world wind into the object's local yaw once, before GPU instancing.
// 0.35m maximum local displacement at maximum validated input.
inline render::float4 canopyWindVertexParameters(
        const ForestWindState& wind,uint64_t tick,
        float yawRadians=0.0f,float strength=1.0f) {
    if(!wind.xCmPerSec && !wind.zCmPerSec)return {};
    const float amplitude=0.35f*
        (float(wind.maxLeanMilliDegrees)/3000.0f)*
        (float(wind.branchFlexPermille)/1000.0f)*
        std::clamp(strength,0.0f,1.0f);
    const float x=amplitude*float(wind.xCmPerSec)/5000.0f;
    const float z=amplitude*float(wind.zCmPerSec)/5000.0f;
    const float c=std::cos(yawRadians),s=std::sin(yawRadians);
    constexpr float tau=6.2831853071795864769f;
    const float phase=tau*float(tick%wind.cycleTicks)/
                            float(wind.cycleTicks);
    return {c*x-s*z,s*x+c*z,phase,
            float(wind.gustPermille)/1000.0f};
}

// Conservative 0.5m local-space bound covers the maximum 0.35m xz
// component multiplied by (0.62+0.38+0.25) and diagonal displacement.
inline constexpr float kCanopyGpuWindMaxOffsetMetres=0.65f;

} // namespace aiverse::terraweave
