// Shared compositing contract for independent GI layers (CPU reference; shaders must mirror it).
// A scene MUST designate exactly one static indirect owner, never sum lightmap +
// baked probes + scalable grid. Dynamic light sources must be excluded from that
// owner's bake to prevent double counting. No gameplay/ECS state lives here.
#pragma once
#include "Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace aiverse::render {

enum class GiBaseSource : uint8_t {
    Hemisphere = 0, BakedLightmap = 1, BakedProbes = 2, ScalableProbes = 3
};
struct GiIndirectLayers {
    GiBaseSource baseSource = GiBaseSource::Hemisphere;
    float3 hemisphere{}, bakedLightmap{}, bakedProbes{}, scalableProbes{};
    float3 dynamicBounce{};   // ONLY indirect, exclude any sources already baked into selected base
    float3 screenBounce{};    // optional additional INCIDENT light, never post-tonemapped color
    float indirectOcclusion = 1.0f;
};
struct GiIndirectResult {
    GiBaseSource owner = GiBaseSource::Hemisphere;
    float3 incident{};       // apply surface diffuse albedo and BRDF later
    float3 unoccluded{};     // useful for debugger and independent specular occlusion
};
inline GiIndirectResult composeIndirectGi(const GiIndirectLayers& layers) {
    const auto finite = [](float3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    for (auto v : {layers.hemisphere, layers.bakedLightmap, layers.bakedProbes,
                   layers.scalableProbes, layers.dynamicBounce, layers.screenBounce})
        if (!finite(v) || v.x < 0 || v.y < 0 || v.z < 0)
            throw std::invalid_argument("invalid indirect GI radiance");
    if (!std::isfinite(layers.indirectOcclusion) ||
        layers.indirectOcclusion < 0 || layers.indirectOcclusion > 1)
        throw std::invalid_argument("invalid indirect GI occlusion");
    float3 base{};
    switch (layers.baseSource) {
    case GiBaseSource::Hemisphere: base = layers.hemisphere; break;
    case GiBaseSource::BakedLightmap: base = layers.bakedLightmap; break;
    case GiBaseSource::BakedProbes: base = layers.bakedProbes; break;
    case GiBaseSource::ScalableProbes: base = layers.scalableProbes; break;
    default: throw std::invalid_argument("unknown indirect GI owner");
    }
    const float3 incident = base + layers.dynamicBounce + layers.screenBounce;
    return {layers.baseSource, incident * layers.indirectOcclusion, incident};
}

} // namespace aiverse::render
