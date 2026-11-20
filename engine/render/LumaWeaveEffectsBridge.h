// Generic stable effect-light publisher for SpectraFX, ECS gameplay and
// editor previews. No dependency on any genre or the SpectraFX internal graph
// implementation: the VFX host passes an immutable per-frame light snapshot.
// Entity/effect instance ID and emitter slot form an unambiguous 64-bit domain.
// Expired effects are naturally removed by LumaWeaveRegistry::publish.
// This provides DIRECT light source data only; separate GI bounce is opt-in.
#pragma once
#include "LumaWeaveAreaLights.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct LumaEffectLight {
    uint32_t instanceId=0;      // stable SpectraFX cue / entity instance
    uint16_t emitterSlot=0;     // stable emitter within the same effect graph
    LumaLightKind kind=LumaLightKind::Point;
    float3 position{};
    float3 direction{0,-1,0};
    float3 color{1,1,1};
    float radius=4,intensity=1;
    float cosOuter=.6f,cosInner=.85f;
    float importanceBias=1;
    bool castsShadows=true;
    bool contributesToGi=false; // bounced GI must be independently enabled
};

struct LumaEffectFrame {
    LumaPublishResult published{};
    size_t effectLights=0,areaChildren=0,authoredLights=0;
};

class LumaWeaveEffectBridge {
public:
    static constexpr uint64_t kEffectNamespace=1ull<<62;

    static uint64_t stableKey(uint32_t instance,uint16_t slot) {
        if(!instance)throw std::invalid_argument("effect ID must be stable and nonzero");
        return kEffectNamespace | (uint64_t(instance)<<16) | uint64_t(slot);
    }

    static LumaLight convert(const LumaEffectLight& effect) {
        LumaLight l;
        l.key=stableKey(effect.instanceId,effect.emitterSlot);
        l.kind=effect.kind;
        l.position=effect.position;
        l.direction=effect.direction;
        l.color=effect.color;
        l.radius=effect.radius;
        l.intensity=effect.intensity;
        l.cosOuter=effect.cosOuter;
        l.cosInner=effect.cosInner;
        l.importanceBias=effect.importanceBias;
        l.castsShadows=effect.castsShadows;
        l.contributesToGi=effect.contributesToGi;
        if(!lumaValidLight(l))throw std::invalid_argument("invalid VFX light source");
        return l;
    }

    // Authors' plain lights MUST have top two key bits 00. Effect sources use
    // 01 and sampled areas use 10/11. No hash collisions and no fake IDs.
    static LumaEffectFrame publish(
            LumaWeaveRegistry& target,std::span<const LumaLight> authored,
            std::span<const LumaEffectLight> effects,
            std::span<const LumaAreaEmitter> areas={}) {
        const size_t expected=authored.size()+effects.size();
        if(expected>LumaWeaveRegistry::kMaxSources)
            throw std::length_error("effect and authored light budget exceeded");
        std::vector<LumaLight> all;
        all.reserve(expected);
        for(const auto& l:authored) {
            if(l.key>>62)
                throw std::invalid_argument("authored light ID reserved by VFX/area namespace");
            all.push_back(l);
        }
        for(const auto& e:effects)all.push_back(convert(e));
        auto combined=LumaWeaveAreaLights::combine(std::move(all),areas);
        const size_t childCount=combined.size()-expected;
        auto published=target.publish(std::move(combined));
        return {std::move(published),effects.size(),childCount,authored.size()};
    }
};

} // namespace aiverse::render
