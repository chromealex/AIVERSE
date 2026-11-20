// Native SpectraFX/ParticleSystem -> LumaWeave direct light bridge.
// ParticleSystem::collectLightsStable preserves the EXISTING particle light
// curve/color/quality logic and returns its monotonic per-system particleUid.
// A host-provided systemNamespace disambiguates separate particle systems.
// Source identities are never derived from vector order or camera culling.
// No implicit legacy LightSystem submission: caller chooses exactly ONE owner.
#pragma once
#include "LumaWeaveEffectsBridge.h"
#include "engine/fx/ParticleSystem.h"
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

class LumaWeaveParticleBridge {
public:
    static constexpr uint32_t kMaxSystemNamespace=(1u<<29)-1u;
    static constexpr uint64_t kParticleDomain=1ull<<61;
    static uint64_t stableParticleKey(uint32_t systemNamespace,uint32_t particleUid) {
        if(!systemNamespace || systemNamespace>kMaxSystemNamespace || !particleUid)
            throw std::invalid_argument("invalid SpectraFX system namespace or particle UID");
        // Top bits 01 mark VFX; bit 61 marks the particle subdomain.
        // Generic effect instance keys (instanceId<<16 | emitterSlot) use
        // only low bits 0..47 and can NEVER collide with this subdomain.
        // 29 bits for a live ParticleSystem namespace + all 32 UID bits.
        return LumaWeaveEffectBridge::kEffectNamespace | kParticleDomain |
               (uint64_t(systemNamespace)<<32) | particleUid;
    }

    static LumaLight convert(uint32_t systemNamespace,
                             const fx::FxIdentifiedPointLight& source,
                             bool contributesToGi=false) {
        LumaLight result;
        result.key=stableParticleKey(systemNamespace,source.particleUid);
        result.kind=LumaLightKind::Point;
        result.position=source.light.pos;
        result.radius=source.light.radius;
        result.color=source.light.color;
        result.intensity=source.light.intensity;
        result.importanceBias=1.f;
        result.castsShadows=true;
        result.contributesToGi=contributesToGi;
        if(!lumaValidLight(result))
            throw std::invalid_argument("invalid SpectraFX source for LumaWeave");
        return result;
    }

    // 'authored' are ordinary scene lights (top two ID bits=00).
    // Effects/area sources may be merged separately using
    // LumaWeaveEffectsBridge::publish when composing a larger game snapshot.
    static LumaPublishResult publish(
            LumaWeaveRegistry& registry,uint32_t systemNamespace,
            std::span<const fx::FxIdentifiedPointLight> particles,
            std::span<const LumaLight> authored={}) {
        if(particles.size()+authored.size()>LumaWeaveRegistry::kMaxSources)
            throw std::length_error("SpectraFX + authored source cap exceeds 4096");
        std::vector<LumaLight> next;
        next.reserve(particles.size()+authored.size());
        for(const auto& source:authored) {
            if(source.key>>62)
                throw std::invalid_argument("authored ID reserved for FX or area light");
            next.push_back(source);
        }
        for(const auto& source:particles)
            next.push_back(convert(systemNamespace,source));
        return registry.publish(std::move(next));
    }

    static LumaPublishResult collectAndPublish(
            LumaWeaveRegistry& registry,const fx::ParticleSystem& particles,
            uint32_t systemNamespace,int maxFxLights=256,
            std::span<const LumaLight> authored={}) {
        if(maxFxLights<0||maxFxLights>1024)
            throw std::invalid_argument("invalid SpectraFX light budget");
        std::vector<fx::FxIdentifiedPointLight> collected;
        if(maxFxLights)particles.collectLightsStable(collected,maxFxLights);
        return publish(registry,systemNamespace,collected,authored);
    }
};

} // namespace aiverse::render
