// PHYSIS tornado cues (2026-11-22): pure, device-free planners that turn the
// tornadoes and debris of a NaturePresentationFrame into SpectraFX emitter
// commands (dust funnel, ground skirt, flying debris) and a positional roar,
// plus the built-in effect / sound definitions. Applied by
// PhysisNatureFxHost / PhysisNatureAudioHost; floats, presentation only.
#pragma once
#include "engine/world/PhysisNaturePresentation.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace aiverse::world {

// Funnel = dust funnel, FireFunnel = flames/glow/embers/smoke of a fire whirl,
// Spray = waterspout column + base cascade + ring on the water, Ember = one
// flying ember, Scar = ground scour mark. The funnel look blends from dust to
// fire (or spray) by quantized levels: the old emitter fades while the new one
// starts, so the change is gradual.
enum class TornadoFxLayer : std::uint8_t { Funnel = 0, Debris = 1, FireFunnel = 2, Spray = 3, Ember = 4, Scar = 5 };
constexpr std::size_t kTornadoFxLayers = 6;
enum class TornadoFxOp : std::uint8_t { Start, Move, Stop };

struct TornadoFxCommand {
    TornadoFxOp op = TornadoFxOp::Start;
    TornadoFxLayer layer = TornadoFxLayer::Funnel;
    std::uint64_t key = 0;
    float x = 0, y = 0, z = 0;
    float scale = 1;
    float tint[4] = {1, 1, 1, 1};
};

struct TornadoFxPolicy {
    std::uint32_t maxFunnels = 4;
    std::uint32_t maxDebris = 96;
    std::uint32_t maxEmbers = 96;
    std::uint32_t maxScars = 160;
    std::uint32_t levels = 4;          // intensity quantization (restart on change)
    std::uint32_t fireLevels = 6;      // fireness quantization
    std::uint32_t sprayLevels = 4;
    float minFireness = 0.05f, minSpray = 0.05f, minScarDepth = 0.03f;
    float funnelScalePerCoreMeter = 1; // built-in funnel: scale 1 = 1 m core
    float minFunnelScale = 0.5f, maxFunnelScale = 60;
};

// Per render frame. `since` = seconds since the frame's tick was committed:
// positions are extrapolated with the last tick's motion so the funnel and
// debris glide instead of stepping at the simulation rate.
class TornadoFxPlanner {
public:
    explicit TornadoFxPlanner(TornadoFxPolicy p = {}) : policy_(p) {}
    std::size_t active(TornadoFxLayer l) const { return slots_[std::size_t(l)].size(); }
    std::vector<TornadoFxCommand> clear() {
        std::vector<TornadoFxCommand> out;
        for (std::size_t l = 0; l < kTornadoFxLayers; ++l) {
            for (const auto& [k, s] : slots_[l]) out.push_back(stop(TornadoFxLayer(l), k));
            slots_[l].clear();
        }
        return out;
    }

    std::vector<TornadoFxCommand> plan(const NaturePresentationFrame& f, float since,
                                       double secondsPerTick) {
        std::vector<TornadoFxCommand> out;
        if (!f.valid) return clear();
        since = std::isfinite(since) ? std::clamp(since, 0.f, 0.5f) : 0.f;
        const float spt = float(std::max(1e-3, secondsPerTick));
        std::map<std::uint64_t, Slot> next[kTornadoFxLayers];
        const auto emit = [&](TornadoFxCommand c, std::uint32_t level, bool alwaysMove) {
            const auto l = std::size_t(c.layer);
            const Slot s{level, c.x, c.z};
            sync(slots_[l], s, c, out, alwaysMove);
            next[l][c.key] = s;
        };
        std::size_t n = 0;
        for (const auto& t : f.tornadoes) {
            if (n++ >= policy_.maxFunnels) break;
            TornadoFxCommand c;
            c.key = t.key;
            const float k = since / spt;
            c.x = t.x + t.moveX * k; c.y = t.y; c.z = t.z + t.moveZ * k;
            const auto lv = level(t.intensity);
            const float strength = 0.45f + 0.55f * float(lv) / float(std::max<std::uint32_t>(1, policy_.levels));
            c.scale = std::clamp(t.coreRadius * policy_.funnelScalePerCoreMeter * strength,
                                 policy_.minFunnelScale, policy_.maxFunnelScale);
            const auto fl = share(t.fireness, policy_.fireLevels, policy_.minFireness);
            const auto sl = share(t.spray, policy_.sprayLevels, policy_.minSpray);
            const float fire = float(fl) / float(std::max<std::uint32_t>(1, policy_.fireLevels));
            const float spray = float(sl) / float(std::max<std::uint32_t>(1, policy_.sprayLevels));
            // Dust funnel fades out as fire or spray take over.
            const float dust = std::clamp(1.f - 0.85f * fire - 0.8f * spray, 0.f, 1.f);
            if (dust > 0.08f) {
                TornadoFxCommand d = c;
                d.layer = TornadoFxLayer::Funnel;
                d.tint[3] = dust;
                emit(d, lv * 1000u + fl * 10u + sl, true);
            }
            if (fl > 0) {
                TornadoFxCommand d = c;
                d.layer = TornadoFxLayer::FireFunnel;
                d.tint[3] = fire;
                emit(d, lv * 100u + fl, true);
            }
            if (sl > 0) {
                TornadoFxCommand d = c;
                d.layer = TornadoFxLayer::Spray;
                d.tint[3] = spray;
                emit(d, lv * 100u + sl, true);
            }
        }
        n = 0;
        for (const auto& d : f.debris) {
            if (!d.airborne) continue;           // landed pieces stop emitting
            if (n++ >= policy_.maxDebris) break;
            TornadoFxCommand c;
            c.layer = TornadoFxLayer::Debris; c.key = d.key;
            c.x = d.x + d.vx * since; c.y = std::max(0.f, d.y + d.vy * since); c.z = d.z + d.vz * since;
            c.scale = std::clamp(d.size, 0.1f, 2.f);
            emit(c, 1, true);
        }
        n = 0;
        for (const auto& e : f.embers) {
            if (n++ >= policy_.maxEmbers) break;
            TornadoFxCommand c;
            c.layer = TornadoFxLayer::Ember; c.key = e.key;
            c.x = e.x + e.vx * since; c.y = std::max(0.f, e.y + e.vy * since); c.z = e.z + e.vz * since;
            emit(c, 1, true);
        }
        // Scars: the deepest scour blocks (stable tie-break by key).
        std::vector<const NatureScourPoint*> scars;
        for (const auto& s : f.scour) if (s.depth >= policy_.minScarDepth) scars.push_back(&s);
        std::stable_sort(scars.begin(), scars.end(), [](const auto* a, const auto* b) {
            return a->depth != b->depth ? a->depth > b->depth : a->key < b->key;
        });
        if (scars.size() > policy_.maxScars) scars.resize(policy_.maxScars);
        for (const auto* s : scars) {
            TornadoFxCommand c;
            c.layer = TornadoFxLayer::Scar; c.key = s->key;
            c.x = s->x; c.z = s->z; c.y = f.elevationAt(s->x, s->z) + 0.03f;
            c.scale = std::max(0.5f, s->size);
            const auto lv = std::uint32_t(std::clamp(int(std::ceil(s->depth / 0.12f)), 1, 5));
            c.tint[3] = 0.35f + 0.13f * float(lv);
            emit(c, lv, false);
        }
        for (std::size_t l = 0; l < kTornadoFxLayers; ++l) {
            for (const auto& [k, s] : slots_[l])
                if (!next[l].count(k)) out.push_back(stop(TornadoFxLayer(l), k));
            slots_[l] = std::move(next[l]);
        }
        return out;
    }

private:
    struct Slot { std::uint32_t level = 0; float x = 0, z = 0; };
    std::uint32_t level(float v) const {
        const auto n = std::max<std::uint32_t>(1, policy_.levels);
        return std::uint32_t(std::clamp(int(std::ceil(v * float(n))), 1, int(n)));
    }
    static std::uint32_t share(float v, std::uint32_t levels, float minimum) {
        if (!std::isfinite(v) || v < minimum) return 0;
        const auto n = std::max<std::uint32_t>(1, levels);
        return std::uint32_t(std::clamp(int(std::ceil(v * float(n))), 1, int(n)));
    }
    static TornadoFxCommand stop(TornadoFxLayer l, std::uint64_t key) {
        TornadoFxCommand c; c.op = TornadoFxOp::Stop; c.layer = l; c.key = key; return c;
    }
    static void sync(const std::map<std::uint64_t, Slot>& prev, const Slot& s, TornadoFxCommand c,
                     std::vector<TornadoFxCommand>& out, bool alwaysMove) {
        const auto it = prev.find(c.key);
        if (it == prev.end()) { c.op = TornadoFxOp::Start; out.push_back(c); return; }
        if (it->second.level != s.level) {
            out.push_back(stop(c.layer, c.key));
            c.op = TornadoFxOp::Start; out.push_back(c); return;
        }
        if (alwaysMove || std::abs(it->second.x - s.x) > 0.01f || std::abs(it->second.z - s.z) > 0.01f) {
            c.op = TornadoFxOp::Move; out.push_back(c);
        }
    }
    TornadoFxPolicy policy_;
    std::map<std::uint64_t, Slot> slots_[kTornadoFxLayers];
};
// Built-in funnel: scale 1 = 1 m core radius; the column is ~14 core radii
// tall and widens upward. Local space, so ps.move carries the live particles
// with the vortex. A project overrides it with fx/physis_tornado.json.
inline const char* physisTornadoEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "space": "local", "rate": 110, "loop": true,
      "prewarm": true, "life": [1.8, 2.6], "speed": [0, 0.1], "shape": "disc", "radius": 0.35,
      "size": [0.6, 1.0], "sizeOverLife": [[0, 0.4], [0.3, 1], [1, 2.6]], "rotation": [0, 360], "spin": [-60, 60],
      "velocityOverLife": { "y": { "curve": [[0, 4.5], [1, 6.5]] }, "orbital": [0, 330, 0],
                            "radial": { "curve": [[0, 0.02], [0.4, 0.4], [1, 2.0]] }, "space": "local" },
      "noise": { "strength": 0.25, "frequency": 0.6, "scroll": 0.8 },
      "color": [[0, "#6b5d4c00"], [0.12, "#6b5d4cb0"], [0.7, "#7d7266a0"], [1, "#8a827800"]] },
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "space": "local", "rate": 45, "loop": true,
      "prewarm": true, "life": [1.0, 1.8], "speed": [0.8, 1.8], "shape": "ring", "radius": 1.6, "radial": 1,
      "size": [1.2, 2.2], "sizeOverLife": [[0, 0.6], [1, 2.0]], "rotation": [0, 360],
      "velocityOverLife": { "y": 0.6, "orbital": [0, 220, 0], "space": "local" },
      "color": [[0, "#5a4a3800"], [0.2, "#5a4a38a0"], [1, "#6e604f00"]] },
    { "texture": "builtin:soft", "blend": "alpha", "align": "camera", "space": "local", "rate": 26, "loop": true,
      "prewarm": true, "life": [1.6, 2.8], "speed": [0, 0.3], "shape": "disc", "radius": 1.0,
      "size": [0.12, 0.3], "rotation": [0, 360], "spin": [-400, 400],
      "velocityOverLife": { "y": { "curve": [[0, 3], [1, 7]] }, "orbital": [0, 420, 0],
                            "radial": { "curve": [[0, 0.3], [1, 1.8]] }, "space": "local" },
      "color": [[0, "#241c14ff"], [0.85, "#2c2219ff"], [1, "#2c221900"]], "minQuality": "medium" }
  ]
})";
}

// One flying debris piece: a dark tumbling chunk with a short dust trail.
inline const char* physisTornadoDebrisEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:soft", "blend": "alpha", "align": "camera", "space": "local", "loop": true,
      "rate": 4, "maxParticles": 1, "life": [0.6, 0.6], "speed": [0, 0], "size": [0.9, 1.1],
      "rotation": [0, 360], "spin": [-540, 540], "color": [[0, "#2a2018ff"], [1, "#2a2018ff"]] },
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "rate": 10, "loop": true,
      "life": [0.5, 0.9], "speed": [0, 0.3], "size": [0.6, 1.0], "sizeOverLife": [[0, 0.6], [1, 1.8]],
      "color": [[0, "#6a5c4a80"], [1, "#6a5c4a00"]], "minQuality": "medium" }
  ]
})";
}

// Fire whirl: a spinning column of flame (additive, local space like the
// dust funnel), a glowing core with fire light, sparks thrown out of the
// spiral and dark smoke above. Tint alpha = fireness share.
inline const char* physisTornadoFireEffectJson() {
    // CC0 flipbooks (engine/assets/fx/physis): tall flame tongues aligned to
    // their spiral motion (twisted licks), a fireball core glow with the fire
    // light, sparks thrown out, dark smoke above (alpha). Additive parts keep
    // moderate alpha so the column does not burn out to white.
    return R"({
  "emitters": [
    { "texture": "engine/fx/physis/flame_tall_16x4.png", "frames": [16, 4], "fps": 0, "startFrame": [0, 15],
      "blend": "additive", "align": "velocity", "aspect": [1.8, 1.8], "space": "local",
      "rate": 120, "loop": true, "prewarm": true, "life": [0.9, 1.4], "speed": [0, 0.2],
      "shape": "disc", "radius": 0.45, "size": [0.42, 0.7], "sizeOverLife": [[0, 0.6], [0.3, 1], [1, 0.55]],
      "velocityOverLife": { "y": { "curve": [[0, 5], [1, 8]] }, "orbital": [0, 380, 0],
                            "radial": { "curve": [[0, 0.05], [0.5, 0.35], [1, 0.9]] }, "space": "local" },
      "noise": { "strength": 0.4, "frequency": 0.9, "scroll": 1.2 },
      "color": [[0, "#ffffff00"], [0.1, "#ffe8ccb0"], [0.45, "#ffc89098"], [0.8, "#ff9a5a60"], [1, "#ff603000"]] },
    { "texture": "engine/fx/physis/flame_16x4.png", "frames": [16, 4], "fps": 0, "startFrame": [0, 15],
      "blend": "additive", "align": "upright", "aspect": [2, 2], "pivot": [0, 0.5, 0], "space": "local",
      "rate": 26, "loop": true, "prewarm": true, "life": [0.7, 1.1], "speed": [0, 0.2],
      "shape": "ring", "radius": 1.1, "size": [0.5, 0.8],
      "velocityOverLife": { "y": 0.8, "orbital": [0, 240, 0], "space": "local" },
      "color": [[0, "#ffffff00"], [0.15, "#ffe0c0b0"], [1, "#ff703000"]] },
    { "texture": "engine/fx/physis/fireball_8x8.png", "frames": [8, 8], "fps": 0, "startFrame": [0, 30],
      "blend": "additive", "align": "camera", "space": "local", "rate": 10, "loop": true, "prewarm": true,
      "life": [0.8, 1.2], "speed": [0, 0.1], "shape": "disc", "radius": 0.3, "size": [1.4, 2.0],
      "velocityOverLife": { "y": 2.5, "orbital": [0, 200, 0], "space": "local" },
      "color": [[0, "#ff905000"], [0.2, "#ff9a5a50"], [1, "#ff502000"]],
      "light": { "color": "#ff8a3a", "radius": 14, "intensity": 2.0, "ratio": 0.2, "max": 4,
                 "useParticleColor": false, "alphaAffectsIntensity": true } },
    { "texture": "builtin:spark", "blend": "additive", "align": "velocity", "rate": 30, "loop": true,
      "life": [0.9, 1.8], "speed": [4, 9], "spread": 70, "shape": "disc", "radius": 1.2,
      "gravity": [0, 1.5, 0], "drag": 0.5, "stretch": 0.06, "size": [0.04, 0.09],
      "noise": { "strength": 1.6, "frequency": 1.3, "scroll": 1.0 },
      "color": [[0, "#fff0b0ff"], [0.5, "#ff8a2ae0"], [1, "#ff200000"]], "minQuality": "medium" },
    { "texture": "engine/fx/physis/smoke_8x8.png", "frames": [8, 8], "fps": 0, "startFrame": [0, 20],
      "blend": "alpha", "align": "camera", "space": "local", "rate": 20, "loop": true,
      "prewarm": true, "life": [2.5, 4.0], "speed": [0, 0.2], "shape": "disc", "radius": 1.2,
      "size": [2.4, 3.6], "sizeOverLife": [[0, 0.6], [1, 2.6]], "rotation": [0, 360], "spin": [-30, 30],
      "velocityOverLife": { "y": { "curve": [[0, 6], [1, 4]] }, "orbital": [0, 90, 0],
                            "radial": { "curve": [[0, 0.2], [1, 1.6]] }, "space": "local" },
      "color": [[0, "#1c181500"], [0.25, "#24201cc0"], [1, "#3a353000"]], "minQuality": "medium" }
  ]
})";
}

// Waterspout: a pale column of water spray, a spray cascade at the base and
// expanding rings on the water surface. Tint alpha = spray share.
inline const char* physisTornadoSprayEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "space": "local", "rate": 100, "loop": true,
      "prewarm": true, "life": [1.6, 2.4], "speed": [0, 0.1], "shape": "disc", "radius": 0.3,
      "size": [0.5, 0.9], "sizeOverLife": [[0, 0.4], [0.3, 1], [1, 2.0]], "rotation": [0, 360], "spin": [-60, 60],
      "velocityOverLife": { "y": { "curve": [[0, 5], [1, 7]] }, "orbital": [0, 340, 0],
                            "radial": { "curve": [[0, 0.02], [0.4, 0.3], [1, 1.4]] }, "space": "local" },
      "noise": { "strength": 0.2, "frequency": 0.6, "scroll": 0.8 },
      "color": [[0, "#dfe9f000"], [0.12, "#dfe9f0b8"], [0.7, "#c9d6dfa0"], [1, "#b8c6d000"]] },
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "space": "local", "rate": 70, "loop": true,
      "prewarm": true, "life": [0.8, 1.4], "speed": [2.5, 4.5], "shape": "ring", "radius": 1.4, "radial": 1,
      "size": [0.9, 1.6], "sizeOverLife": [[0, 0.5], [1, 2.2]], "rotation": [0, 360], "gravity": [0, -2.5, 0],
      "velocityOverLife": { "y": { "curve": [[0, 2.5], [1, -1]] }, "orbital": [0, 160, 0], "space": "local" },
      "color": [[0, "#eef5fa00"], [0.15, "#eef5fad0"], [1, "#e0ebf200"]] },
    { "texture": "builtin:ring", "blend": "alpha", "align": "ground", "rate": 1.6, "loop": true, "prewarm": true,
      "life": [2.2, 2.6], "speed": [0, 0], "size": [2.0, 2.4], "sizeOverLife": [[0, 1], [1, 4.5]],
      "color": [[0, "#ffffff00"], [0.15, "#f2f8ffa0"], [1, "#f2f8ff00"]] }
  ]
})";
}

// One ember: a glowing spark with a short light.
inline const char* physisTornadoEmberEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:soft", "blend": "additive", "align": "camera", "space": "local", "loop": true,
      "rate": 8, "maxParticles": 1, "ringBuffer": true, "life": [0.4, 0.4], "speed": [0, 0], "size": [0.22, 0.3],
      "color": [[0, "#ffd080ff"], [0.5, "#ff8a30ff"], [1, "#ffd080ff"]],
      "light": { "color": "#ff9040", "radius": 2.5, "intensity": 0.8, "ratio": 1, "max": 12,
                 "useParticleColor": false, "alphaAffectsIntensity": true } },
    { "texture": "builtin:spark", "blend": "additive", "align": "velocity", "rate": 14, "loop": true,
      "life": [0.2, 0.4], "speed": [0, 0.4], "size": [0.04, 0.07], "stretch": 0.05,
      "color": [[0, "#ffc070ff"], [1, "#ff400000"]], "minQuality": "medium" }
  ]
})";
}

// Scour scar: one flat dark-soil mark lying on the ground (scale = block
// size in metres). Tint alpha grows with the depth.
inline const char* physisTornadoScarEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:soft", "blend": "alpha", "align": "ground", "loop": true, "prewarm": true,
      "rate": 0.1, "maxParticles": 1, "ringBuffer": true, "life": [60, 60], "speed": [0, 0],
      "size": [2.4, 2.4],
      "color": [[0, "#3b2b1cff"], [1, "#3b2b1cff"]] }
  ]
})";
}

// ------------------------------------------------------------------ audio

enum class TornadoAudioOp : std::uint8_t { Start, Update, Stop };
enum class TornadoSound : std::uint8_t { Roar = 0, FireRoar = 1 };
struct TornadoAudioCommand {
    TornadoAudioOp op = TornadoAudioOp::Start;
    TornadoSound sound = TornadoSound::Roar;
    std::uint64_t key = 0;
    float x = 0, y = 0, z = 0, volume = 0, pitch = 1;
};
struct TornadoAudioPolicy {
    std::uint32_t maxVoices = 2;
    float audibleMeters = 260;
    float volumeSlewPerSecond = 1.2f;
};

class TornadoAudioPlanner {
public:
    explicit TornadoAudioPlanner(TornadoAudioPolicy p = {}) : policy_(p) {}
    std::size_t voices() const { return voices_.size() + fire_.size(); }
    std::size_t fireVoices() const { return fire_.size(); }
    float fireVolume(std::uint64_t key) const { const auto it = fire_.find(key); return it == fire_.end() ? 0.f : it->second; }
    std::vector<TornadoAudioCommand> clear() {
        std::vector<TornadoAudioCommand> out;
        for (const auto& [k, v] : voices_) { TornadoAudioCommand c; c.op = TornadoAudioOp::Stop; c.key = k; out.push_back(c); }
        for (const auto& [k, v] : fire_) {
            TornadoAudioCommand c; c.op = TornadoAudioOp::Stop; c.sound = TornadoSound::FireRoar; c.key = k; out.push_back(c);
        }
        voices_.clear(); fire_.clear();
        return out;
    }
    std::vector<TornadoAudioCommand> plan(const NaturePresentationFrame& f, float lx, float ly, float lz,
                                          float dt) {
        std::vector<TornadoAudioCommand> out;
        if (!f.valid) return clear();
        dt = std::isfinite(dt) ? std::clamp(dt, 0.f, 0.5f) : 0.f;
        struct Cand { const NatureTornado* t; float d; };
        std::vector<Cand> cands;
        for (const auto& t : f.tornadoes) {
            const float dx = t.x - lx, dy = t.y - ly, dz = t.z - lz;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d <= policy_.audibleMeters + t.outerRadius) cands.push_back({&t, d});
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return a.d != b.d ? a.d < b.d : a.t->key < b.t->key;
        });
        if (cands.size() > policy_.maxVoices) cands.resize(policy_.maxVoices);
        std::map<std::uint64_t, float> next;
        const float slew = policy_.volumeSlewPerSecond * dt;
        for (const auto& c : cands) {
            const auto it = voices_.find(c.t->key);
            const bool fresh = it == voices_.end();
            const float target = std::clamp(0.35f + 0.65f * c.t->intensity, 0.f, 1.f);
            const float vol = fresh ? std::min(target, 0.2f)
                                    : it->second + std::clamp(target - it->second, -slew, slew);
            TornadoAudioCommand cmd;
            cmd.op = fresh ? TornadoAudioOp::Start : TornadoAudioOp::Update;
            cmd.key = c.t->key;
            cmd.x = c.t->x; cmd.y = c.t->y + c.t->coreRadius * 2; cmd.z = c.t->z;
            cmd.volume = vol;
            cmd.pitch = 0.75f + 0.35f * std::clamp(c.t->tangentialWind / 60.f, 0.f, 1.f);
            out.push_back(cmd);
            next[c.t->key] = vol;
        }
        for (const auto& [k, v] : voices_)
            if (!next.count(k)) { TornadoAudioCommand c; c.op = TornadoAudioOp::Stop; c.key = k; out.push_back(c); }
        voices_ = std::move(next);
        // Roaring flames of a fire whirl: a second voice, volume by fireness.
        std::map<std::uint64_t, float> fire;
        for (const auto& c : cands) {
            if (!(c.t->fireness >= 0.05f)) continue;
            const auto it = fire_.find(c.t->key);
            const bool fresh = it == fire_.end();
            const float target = std::clamp(0.1f + 0.9f * c.t->fireness, 0.f, 1.f) *
                                 std::clamp(0.4f + 0.6f * c.t->intensity, 0.f, 1.f);
            const float vol = fresh ? std::min(target, 0.15f)
                                    : it->second + std::clamp(target - it->second, -slew, slew);
            TornadoAudioCommand cmd;
            cmd.op = fresh ? TornadoAudioOp::Start : TornadoAudioOp::Update;
            cmd.sound = TornadoSound::FireRoar;
            cmd.key = c.t->key;
            cmd.x = c.t->x; cmd.y = c.t->y + c.t->coreRadius; cmd.z = c.t->z;
            cmd.volume = vol;
            cmd.pitch = 0.85f + 0.3f * std::clamp(c.t->fireness, 0.f, 1.f);
            out.push_back(cmd);
            fire[c.t->key] = vol;
        }
        for (const auto& [k, v] : fire_)
            if (!fire.count(k)) {
                TornadoAudioCommand c; c.op = TornadoAudioOp::Stop; c.sound = TornadoSound::FireRoar; c.key = k;
                out.push_back(c);
            }
        fire_ = std::move(fire);
        return out;
    }
private:
    TornadoAudioPolicy policy_;
    std::map<std::uint64_t, float> voices_, fire_;
};

inline const char* tornadoSoundName() { return "physis_tornado_loop"; }
inline const char* tornadoFireSoundName() { return "physis_firestorm_loop"; }

// Adds the synthesized roars unless the project ships "physis_tornado_loop"
// / "physis_firestorm_loop". Returns how many were added.
inline int addPhysisTornadoSounds(Json& bank) {
    if (!bank.isObject()) bank = Json::object();
    Json& sounds = bank.member("sounds");
    int added = 0;
    if (!sounds.has(tornadoFireSoundName())) {
        // Low roaring flame: rumbling noise, crackle band, slow flutter.
        sounds.set(tornadoFireSoundName(), Json::parse(R"({
      "synth": [
        { "wave": "noise", "freq": 220, "duration": 2.8, "attack": 0.0, "release": 0.0, "volume": 0.8, "lowpass": 520, "vibrato": [3.1, 0.35] },
        { "wave": "noise", "freq": 220, "duration": 2.8, "attack": 0.0, "release": 0.0, "volume": 0.28, "lowpass": 2600, "vibrato": [9.0, 0.6] },
        { "wave": "triangle", "freq": [55, 47], "duration": 2.8, "attack": 0.0, "release": 0.0, "volume": 0.18, "vibrato": [2.3, 0.3] }
      ],
      "loop": true, "bus": "ambient", "volume": 1.0, "maxInstances": 2, "steal": "quietest",
      "minDistance": 10, "maxDistance": 240, "priority": 3
    })"));
        ++added;
    }
    if (sounds.has(tornadoSoundName())) return added;
    sounds.set(tornadoSoundName(), Json::parse(R"({
      "synth": [
        { "wave": "noise", "freq": 220, "duration": 3.2, "attack": 0.0, "release": 0.0, "volume": 0.75, "lowpass": 240 },
        { "wave": "noise", "freq": 220, "duration": 3.2, "attack": 0.0, "release": 0.0, "volume": 0.3, "lowpass": 900, "vibrato": [0.45, 0.5] },
        { "wave": "triangle", "freq": [38, 31], "duration": 3.2, "attack": 0.0, "release": 0.0, "volume": 0.22, "vibrato": [1.7, 0.25] }
      ],
      "loop": true, "bus": "ambient", "volume": 1.0, "maxInstances": 2, "steal": "quietest",
      "minDistance": 12, "maxDistance": 280, "priority": 3
    })"));
    return added + 1;
}

} // namespace aiverse::world
