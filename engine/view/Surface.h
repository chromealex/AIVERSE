// AIVERSE View — generic surface-height composition and normal sampling.
// Genres provide their own base terrain and overlay functions; this
// library knows no map rules, road classes, physics or gameplay entities.
#pragma once
#include "engine/render/Math3D.h"
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace aiverse::view {
struct SurfaceSample {
    float height=0.f;
    render::float3 normal{0,1,0};
};
// Compose any base surface and an optional height modifier without
// allocating/copying callbacks. Suitable for per-unit, per-vertex work.
// A modifier receives (x,z,baseHeight) and returns final view height.
template<class BaseFn,class LayerFn>
float composeSurfaceHeight(float x,float z,BaseFn&& base,LayerFn&& layer) {
    return std::invoke(std::forward<LayerFn>(layer),x,z,
        std::invoke(std::forward<BaseFn>(base),x,z));
}
template<class HeightFn>
std::optional<SurfaceSample> sampleSurface(float x,float z,
                                           float normalStep,
                                           HeightFn&& heightAt) {
    if(!std::isfinite(x)||!std::isfinite(z)||
       !std::isfinite(normalStep)||normalStep<=0.f)
        return std::nullopt;
    // Use a central finite-difference normal with the same height
    // policy as the host's model/vehicle/object placement.
    auto&& fn=heightAt;
    const float center=std::invoke(fn,x,z);
    const float left=std::invoke(fn,x-normalStep,z);
    const float right=std::invoke(fn,x+normalStep,z);
    const float near=std::invoke(fn,x,z-normalStep);
    const float far=std::invoke(fn,x,z+normalStep);
    if(!std::isfinite(center)||!std::isfinite(left)||
       !std::isfinite(right)||!std::isfinite(near)||
       !std::isfinite(far))return std::nullopt;
    const render::float3 gradient{
        left-right,2.f*normalStep,near-far};
    return SurfaceSample{center,render::normalize(gradient)};
}
} // namespace aiverse::view
