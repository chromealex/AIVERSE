#pragma once
// PRISMA volume/profile resolver. No RHI, no game genre, no ECS dependency.
// Coordinates use the rendering world's float3 (never simulation Fixed).
#include "PrismaData.h"
#include "Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class PrismaVolumeShape { Global, Sphere, Box };

// ECS/native projects may store these values in their own components.
// No genre-specific entity, actor or camera class is required.
struct PrismaVolumeEffectMask {
    PrismaType type = PrismaType::ColorGrade;
    std::vector<uint8_t> numeric; // flattened catalog params, 1 = explicitly authored
    bool enabled = false, minQuality = false, text = false, graph = false;
};

struct PrismaVolume {
    std::string id;            // optional stable name for map overrides
    bool overrideEnabled = false; // partial profiles must not reset camera HDR/enable
    bool overrideHdr = false;
    PrismaVolumeShape shape = PrismaVolumeShape::Global;
    float3 center{};
    float3 halfExtents{1, 1, 1};   // axis-aligned local extent for Box
    float radius = 1;             // Sphere radius, metres/world units
    float blendDistance = 0;      // outside-border falloff in world units
    float weight = 1;             // user weight, clamped [0,1]
    int priority = 0;             // ascending order, later higher priority wins
    uint32_t layerMask = 0xffffffffu;
    bool enabled = true;
    PrismaSettings profile;
    std::vector<PrismaVolumeEffectMask> effectMasks; // empty => native C++ full-profile behavior
};

// Deterministic: priority-ascending, insertion order for equal priorities.
// A volume influences a camera INSIDE its shape and fades linearly OUTSIDE
// its boundary over blendDistance. No temporal state; replay-safe for fixed input.
class PrismaVolumeStack {
public:
    std::vector<PrismaVolume> volumes;

    static float influence(const PrismaVolume& v, float3 camera,
                           uint32_t cameraMask = 0xffffffffu) {
        if (!v.enabled || !(v.layerMask & cameraMask) ||
            !std::isfinite(v.weight) || v.weight <= 0 ||
            !std::isfinite(v.blendDistance) || !std::isfinite(v.radius) ||
            !std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z) ||
            !std::isfinite(v.center.x) || !std::isfinite(v.center.y) || !std::isfinite(v.center.z))
            return 0;
        const float weight = std::clamp(v.weight, 0.f, 1.f);
        if (v.shape == PrismaVolumeShape::Global) return weight;
        float distance = 0;
        if (v.shape == PrismaVolumeShape::Sphere) {
            distance = std::max(0.f, length(camera - v.center) - std::max(0.f, v.radius));
        } else {
            const float3 p = camera - v.center;
            const float3 e = v.halfExtents;
            if (!std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z)) return 0;
            const float3 o{std::max(0.f, std::abs(p.x) - std::max(0.f, e.x)),
                           std::max(0.f, std::abs(p.y) - std::max(0.f, e.y)),
                           std::max(0.f, std::abs(p.z) - std::max(0.f, e.z))};
            distance = length(o);
        }
        if (v.blendDistance <= 0) return distance <= 0 ? weight : 0;
        return weight * std::clamp(1.f - distance / v.blendDistance, 0.f, 1.f);
    }

    PrismaSettings resolve(const PrismaSettings& base, float3 camera,
                           uint32_t cameraMask = 0xffffffffu) const {
        PrismaSettings out = base;
        std::vector<const PrismaVolume*> sorted;
        sorted.reserve(volumes.size());
        for (const auto& v : volumes) sorted.push_back(&v);
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
            return a->priority < b->priority;
        });

        for (const PrismaVolume* v : sorted) {
            const float t = influence(*v, camera, cameraMask);
            if (t <= 0) continue;
            // HDR, chain enabled, LUT paths, graphs and boolean settings are discrete:
            // choose the incoming value at the halfway point.
            if (t >= .5f) {
                if (v->overrideEnabled) out.enabled = v->profile.enabled;
                if (v->overrideHdr) out.hdr = v->profile.hdr;
            }
            for (const PrismaEffect& incoming : v->profile.effects) {
                const auto maskIt = std::find_if(v->effectMasks.begin(), v->effectMasks.end(),
                    [&](const PrismaVolumeEffectMask& m){ return m.type == incoming.type; });
                const PrismaVolumeEffectMask* authored =
                    maskIt == v->effectMasks.end() ? nullptr : &*maskIt;
                auto it = std::find_if(out.effects.begin(), out.effects.end(),
                                       [&](const PrismaEffect& e){ return e.type == incoming.type; });
                if (!incoming.enabled) {
                    if (it != out.effects.end() && t >= .5f &&
                        (!authored || authored->enabled)) it->enabled = false;
                    continue;
                }
                bool newEffect = it == out.effects.end();
                std::vector<uint8_t> syntheticFade;
                if (newEffect) {
                    // Newly introduced effects that expose a strength can fade in from zero.
                    // Other effects require a discrete activation; see docs/prisma.md.
                    bool supportsFade = incoming.type == PrismaType::ColorGrade ||
                                        incoming.type == PrismaType::TiltShift;
                    for (const auto& def : prismaType(incoming.type).params)
                        supportsFade |= !std::strcmp(def.name, "intensity") ||
                                        !std::strcmp(def.name, "amount");
                    if (!supportsFade) {
                        // No neutral blend parameter: a discrete effect is either active or absent.
                        if (t >= .5f) out.effects.push_back(incoming);
                        continue;
                    }
                    PrismaEffect neutral = defaultPrismaEffect(incoming.type);
                    syntheticFade.resize(neutral.values.size(), 0);
                    size_t at = 0;
                    for (const auto& def : prismaType(incoming.type).params) {
                        const bool fade = !std::strcmp(def.name, "intensity") ||
                                          !std::strcmp(def.name, "amount") ||
                            (incoming.type == PrismaType::TiltShift && !std::strcmp(def.name, "blur"));
                        const size_t count = def.kind == PrismaParamKind::Color ? 3u :
                                             def.kind == PrismaParamKind::Text ? 0u : 1u;
                        for (size_t k=0;k<count && at+k<neutral.values.size();++k)
                            if (fade) { neutral.values[at+k] = 0; syntheticFade[at+k] = 1; }
                        at += count;
                    }
                    out.effects.push_back(std::move(neutral));
                    it = out.effects.end() - 1;
                }
                if (incoming.type == PrismaType::ShaderGraph) {
                    if (t >= .5f) {
                        if (!authored || authored->graph) it->shaderGraph = incoming.shaderGraph;
                        if (!authored || authored->enabled) it->enabled = incoming.enabled;
                        if (!authored || authored->minQuality) it->minQuality = incoming.minQuality;
                    }
                    continue;
                }
                const size_t n = std::min(it->values.size(), incoming.values.size());
                for (size_t j = 0; j < n; ++j) {
                    const bool explicitValue = !authored ||
                        (j < authored->numeric.size() && authored->numeric[j] != 0);
                    const bool initialFade = newEffect && j < syntheticFade.size() && syntheticFade[j];
                    if (explicitValue || initialFade)
                        it->values[j] += (incoming.values[j] - it->values[j]) * t;
                }
                if (t >= .5f) {
                    if (!authored || authored->enabled) it->enabled = true;
                    if (!authored || authored->minQuality) it->minQuality = incoming.minQuality;
                    if (!authored || authored->text) it->text = incoming.text;
                }
            }
        }
        std::stable_sort(out.effects.begin(), out.effects.end(), [](const auto& a, const auto& b){
            return int(a.type) < int(b.type);
        });
        // Native volume components are mutable C++ data. A nonfinite
        // authored/interpolated float must not reach any GPU uniform,
        // even if the profile bypassed the JSON parser entirely.
        sanitizePrismaSettings(out);
        return out;
    }
};

} // namespace aiverse::render
