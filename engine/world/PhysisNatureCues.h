// PHYSIS nature cues (2026-11-21): pure, device-free planners that turn
// NaturePresentationFrame into SpectraFX emitter commands and audio voice
// commands, plus the built-in fallback effect/sound definitions.
//
// Thin adapters apply the commands: PhysisNatureFxHost.h (fx::ParticleSystem)
// and PhysisNatureAudioHost.h (audio::AudioEngine). Splitting the decision
// logic from the device keeps it testable headless and lets other renderers
// (web, Vulkan clients, studio) reuse the same choices.
//
// Presentation-only: floats, render-frame time, never touches ECS.
#pragma once
#include "engine/world/PhysisNaturePresentation.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aiverse::world {

// ---------------------------------------------------------------- VFX plan

enum class NatureFxLayer : std::uint8_t { Flame = 0, Smoke = 1 };
enum class NatureFxOp : std::uint8_t { Start, Move, Stop };

struct NatureFxCommand {
    NatureFxOp op = NatureFxOp::Start;
    NatureFxLayer layer = NatureFxLayer::Flame;
    std::uint64_t key = 0;       // flame point key (stable)
    float x = 0, y = 0, z = 0;
    float scale = 1;             // quantized intensity -> effect scale
    float tint[4] = {1, 1, 1, 1};
};

struct NatureFxPolicy {
    std::uint32_t maxFlames = 160;
    std::uint32_t maxSmoke = 64;
    std::uint32_t levels = 4;          // intensity quantization (restart on change)
    float flameMinScale = 0.45f, flameMaxScale = 1.6f;
    float smokeMinScale = 0.7f, smokeMaxScale = 2.2f;
    float smokeEvery = 2;              // ~1 smoke column per N flame points
};

class NatureFxPlanner {
public:
    explicit NatureFxPlanner(NatureFxPolicy policy = {}) : policy_(policy) {}
    const NatureFxPolicy& policy() const { return policy_; }
    std::size_t active(NatureFxLayer layer) const {
        return layer == NatureFxLayer::Flame ? flames_.size() : smoke_.size();
    }
    // Emits Stop for every tracked instance (map change, disable, rollback).
    std::vector<NatureFxCommand> clear() {
        std::vector<NatureFxCommand> out;
        for (const auto& [k, s] : flames_) out.push_back(stop(NatureFxLayer::Flame, k));
        for (const auto& [k, s] : smoke_) out.push_back(stop(NatureFxLayer::Smoke, k));
        flames_.clear(); smoke_.clear();
        return out;
    }

    std::vector<NatureFxCommand> plan(const NaturePresentationFrame& frame) {
        std::vector<NatureFxCommand> out;
        if (!frame.valid) return clear();
        std::map<std::uint64_t, Slot> flames, smoke;
        // Flame points come sorted by key; select the strongest within budget
        // without reordering identities (stable tie-break by key).
        std::vector<const NatureFlamePoint*> lit, smoky;
        for (const auto& p : frame.flames) {
            if (p.intensity > 0) lit.push_back(&p);
            // A deterministic subset carries smoke columns.
            const auto every = std::max<std::uint64_t>(1, std::uint64_t(policy_.smokeEvery));
            if (p.smoke > 0.05f && (p.key % every) == 0) smoky.push_back(&p);
        }
        const auto strongest = [](auto& v, std::size_t cap, auto value) {
            if (v.size() <= cap) return;
            std::stable_sort(v.begin(), v.end(), [&](auto* a, auto* b) {
                const float va = value(*a), vb = value(*b);
                return va != vb ? va > vb : a->key < b->key;
            });
            v.resize(cap);
        };
        strongest(lit, policy_.maxFlames, [](const NatureFlamePoint& p) { return p.intensity; });
        strongest(smoky, policy_.maxSmoke, [](const NatureFlamePoint& p) { return p.smoke; });
        const auto level = [&](float v) {
            const auto n = std::max<std::uint32_t>(1, policy_.levels);
            return std::uint32_t(std::clamp(int(std::ceil(v * float(n))), 1, int(n)));
        };
        const auto scaleOf = [&](std::uint32_t lv, float lo, float hi) {
            const auto n = float(std::max<std::uint32_t>(1, policy_.levels));
            return lo + (hi - lo) * (float(lv) / n);
        };
        for (const auto* p : lit) {
            Slot s{level(p->intensity), p->x, p->y, p->z};
            NatureFxCommand c;
            c.layer = NatureFxLayer::Flame; c.key = p->key;
            c.x = p->x; c.y = p->y; c.z = p->z;
            c.scale = scaleOf(s.level, policy_.flameMinScale, policy_.flameMaxScale);
            sync(flames_, s, c, out);
            flames[p->key] = s;
        }
        for (const auto* p : smoky) {
            Slot s{level(p->smoke), p->x, p->y, p->z};
            NatureFxCommand c;
            c.layer = NatureFxLayer::Smoke; c.key = p->key;
            c.x = p->x; c.y = p->y; c.z = p->z;
            c.scale = scaleOf(s.level, policy_.smokeMinScale, policy_.smokeMaxScale);
            // Dry grass: dark grey; wet fuel / smouldering: pale, steamy.
            const float pale = std::clamp(0.35f + 0.65f * p->wetness +
                                          (p->intensity <= 0 ? 0.25f : 0.f), 0.f, 1.f);
            c.tint[0] = c.tint[1] = c.tint[2] = 0.55f + 0.9f * pale;
            c.tint[3] = 1;
            sync(smoke_, s, c, out);
            smoke[p->key] = s;
        }
        for (const auto& [k, s] : flames_)
            if (!flames.count(k)) out.push_back(stop(NatureFxLayer::Flame, k));
        for (const auto& [k, s] : smoke_)
            if (!smoke.count(k)) out.push_back(stop(NatureFxLayer::Smoke, k));
        flames_ = std::move(flames);
        smoke_ = std::move(smoke);
        return out;
    }

private:
    struct Slot { std::uint32_t level = 0; float x = 0, y = 0, z = 0; };
    static NatureFxCommand stop(NatureFxLayer layer, std::uint64_t key) {
        NatureFxCommand c; c.op = NatureFxOp::Stop; c.layer = layer; c.key = key; return c;
    }
    // New key: Start. Changed intensity level: Stop + Start (old particles
    // fade naturally). Moved (ground height edit): Move. Else nothing.
    static void sync(const std::map<std::uint64_t, Slot>& prev, const Slot& s,
                     NatureFxCommand c, std::vector<NatureFxCommand>& out) {
        const auto it = prev.find(c.key);
        if (it == prev.end()) { c.op = NatureFxOp::Start; out.push_back(c); return; }
        if (it->second.level != s.level) {
            out.push_back(stop(c.layer, c.key));
            c.op = NatureFxOp::Start; out.push_back(c); return;
        }
        if (std::abs(it->second.y - s.y) > 0.05f || std::abs(it->second.x - s.x) > 0.05f ||
            std::abs(it->second.z - s.z) > 0.05f) {
            c.op = NatureFxOp::Move; out.push_back(c);
        }
    }
    NatureFxPolicy policy_;
    std::map<std::uint64_t, Slot> flames_, smoke_;
};

// Built-in fallback effects. A project overrides them by shipping
// fx/physis_flame.json / fx/physis_smoke.json (same names). Flames and smoke
// use "externalForces", so ParticleSystem::setWind (PHYSIS wind) bends them.
inline const char* physisFlameEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:smoke", "blend": "additive", "align": "camera", "rate": 16, "loop": true, "prewarm": true,
      "life": [0.45, 0.85], "speed": [1.2, 2.4], "spread": 14, "shape": "disc", "radius": 0.9, "drag": 1.1,
      "size": [0.7, 1.15], "sizeOverLife": [[0, 0.8], [0.35, 1], [1, 0.25]], "rotation": [0, 360], "spin": [-90, 90],
      "externalForces": 0.6, "noise": { "strength": 0.7, "frequency": 0.8, "scroll": 0.6 },
      "color": [[0, "#fff0b0f0"], [0.25, "#ffb040d0"], [0.6, "#ff5a1490"], [1, "#30100000"]],
      "light": { "color": "#ff9440", "radius": 7, "intensity": 1.6, "ratio": 0.12, "max": 24,
                 "useParticleColor": false, "alphaAffectsIntensity": true } },
    { "texture": "builtin:spark", "blend": "additive", "align": "velocity", "rate": 5, "loop": true,
      "life": [1.0, 2.2], "speed": [2.5, 5], "spread": 30, "shape": "disc", "radius": 0.8,
      "gravity": [0, 0.8, 0], "drag": 0.6, "stretch": 0.05, "size": [0.05, 0.1],
      "externalForces": 1, "noise": { "strength": 1.4, "frequency": 1.2, "scroll": 0.8 },
      "color": [[0, "#ffe0a0ff"], [0.6, "#ff7020c0"], [1, "#ff200000"]], "minQuality": "medium" }
  ]
})";
}

inline const char* physisSmokeEffectJson() {
    return R"({
  "emitters": [
    { "texture": "builtin:smoke", "blend": "alpha", "align": "camera", "rate": 3.5, "loop": true, "prewarm": true,
      "life": [4.5, 7.5], "speed": [1.4, 2.4], "spread": 10, "shape": "disc", "radius": 1.2,
      "gravity": [0, 0.35, 0], "drag": 0.35, "size": [1.4, 2.2], "sizeOverLife": [[0, 0.5], [1, 3.6]],
      "rotation": [0, 360], "spin": [-15, 15], "externalForces": 1,
      "noise": { "strength": 0.5, "frequency": 0.25, "scroll": 0.2 },
      "color": [[0, "#2c2926a0"], [0.4, "#4a4540" ], [1, "#6e6a6400"]] }
  ]
})";
}

// --------------------------------------------------------------- audio plan

enum class NatureAudioOp : std::uint8_t { Start, Update, Stop };
enum class NatureSound : std::uint8_t { FireLoop = 0, WindLoop = 1 };

struct NatureAudioCommand {
    NatureAudioOp op = NatureAudioOp::Start;
    NatureSound sound = NatureSound::FireLoop;
    std::uint64_t key = 0;     // cluster key; wind uses key 1
    bool positional = true;
    float x = 0, y = 0, z = 0;
    float volume = 0, pitch = 1;
};

struct NatureAudioPolicy {
    std::uint32_t maxFireVoices = 4;
    float fireAudibleMeters = 140;   // clusters beyond are not voiced
    float hysteresis = 1.25f;        // a voiced cluster keeps its slot unless
                                     // a rival is this much stronger
    float windMinSpeed = 2, windFullSpeed = 18;
    float volumeSlewPerSecond = 1.5f;
};

class NatureAudioPlanner {
public:
    explicit NatureAudioPlanner(NatureAudioPolicy p = {}) : policy_(p) {}
    std::size_t fireVoices() const { return fire_.size(); }
    bool windVoice() const { return windOn_; }

    std::vector<NatureAudioCommand> clear() {
        std::vector<NatureAudioCommand> out;
        for (const auto& [k, v] : fire_) out.push_back(stopCmd(NatureSound::FireLoop, k));
        if (windOn_) out.push_back(stopCmd(NatureSound::WindLoop, 1));
        fire_.clear(); windOn_ = false; windVolume_ = 0;
        return out;
    }

    // listener: scene metres. dt: render seconds since previous call.
    std::vector<NatureAudioCommand> plan(const NaturePresentationFrame& frame,
                                         float lx, float ly, float lz, float dt) {
        std::vector<NatureAudioCommand> out;
        if (!frame.valid) return clear();
        dt = std::isfinite(dt) ? std::clamp(dt, 0.f, 0.5f) : 0.f;
        const float slew = policy_.volumeSlewPerSecond * dt;
        // --- fire: score = intensity * area weight / distance falloff
        struct Cand { const NatureFireCluster* c; float score, target; };
        std::vector<Cand> cands;
        for (const auto& c : frame.clusters) {
            const float dx = c.x - lx, dy = c.y - ly, dz = c.z - lz;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > policy_.fireAudibleMeters) continue;
            const float size = std::clamp(std::sqrt(c.burningArea) / 12.f, 0.25f, 1.f);
            const float target = std::clamp(c.intensity * size, 0.f, 1.f);
            const float near = 1.f - d / policy_.fireAudibleMeters;
            float score = target * (0.2f + 0.8f * near);
            if (fire_.count(c.key)) score *= policy_.hysteresis;
            cands.push_back({&c, score, target});
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return a.score != b.score ? a.score > b.score : a.c->key < b.c->key;
        });
        if (cands.size() > policy_.maxFireVoices) cands.resize(policy_.maxFireVoices);
        std::map<std::uint64_t, Voice> next;
        for (const auto& cand : cands) {
            const auto& c = *cand.c;
            auto it = fire_.find(c.key);
            Voice v = it != fire_.end() ? it->second : Voice{};
            const bool fresh = it == fire_.end();
            v.target = cand.target;
            v.volume = fresh ? std::min(cand.target, 0.15f)
                             : v.volume + std::clamp(v.target - v.volume, -slew, slew);
            NatureAudioCommand cmd;
            cmd.op = fresh ? NatureAudioOp::Start : NatureAudioOp::Update;
            cmd.sound = NatureSound::FireLoop; cmd.key = c.key;
            cmd.x = c.x; cmd.y = c.y; cmd.z = c.z;
            cmd.volume = v.volume;
            cmd.pitch = 0.9f + 0.25f * c.intensity;
            out.push_back(cmd);
            next[c.key] = v;
        }
        for (const auto& [k, v] : fire_)
            if (!next.count(k)) out.push_back(stopCmd(NatureSound::FireLoop, k));
        fire_ = std::move(next);

        // --- wind ambience at the listener (non-positional bed)
        const auto here = frame.sample(lx, lz);
        const float speed = here.valid ? std::sqrt(here.windX * here.windX + here.windZ * here.windZ)
                                       : std::sqrt(frame.meanWindX * frame.meanWindX +
                                                   frame.meanWindZ * frame.meanWindZ);
        const float span = std::max(0.1f, policy_.windFullSpeed - policy_.windMinSpeed);
        float t = std::clamp((speed - policy_.windMinSpeed) / span, 0.f, 1.f);
        t = t * t * (3 - 2 * t);
        windVolume_ += std::clamp(t * 0.8f - windVolume_, -slew, slew);
        if (windVolume_ > 0.01f || t > 0) {
            NatureAudioCommand w;
            w.op = windOn_ ? NatureAudioOp::Update : NatureAudioOp::Start;
            w.sound = NatureSound::WindLoop; w.key = 1; w.positional = false;
            w.volume = windVolume_;
            w.pitch = 0.8f + 0.5f * t;
            out.push_back(w);
            windOn_ = true;
        } else if (windOn_) {
            out.push_back(stopCmd(NatureSound::WindLoop, 1));
            windOn_ = false;
        }
        return out;
    }

private:
    struct Voice { float volume = 0, target = 0; };
    static NatureAudioCommand stopCmd(NatureSound s, std::uint64_t key) {
        NatureAudioCommand c; c.op = NatureAudioOp::Stop; c.sound = s; c.key = key; return c;
    }
    NatureAudioPolicy policy_;
    std::map<std::uint64_t, Voice> fire_;
    bool windOn_ = false;
    float windVolume_ = 0;
};

inline const char* natureSoundName(NatureSound s) {
    return s == NatureSound::FireLoop ? "physis_fire_loop" : "physis_wind_loop";
}

// Adds synthesized fallback loops to an audio bank (audio.json) unless the
// project already defines sounds with these names (real recordings win).
// Returns the number of sounds added.
inline int addPhysisNatureSounds(Json& bank) {
    if (!bank.isObject()) bank = Json::object();
    Json& sounds = bank.member("sounds");
    int added = 0;
    if (!sounds.has("physis_fire_loop")) {
        sounds.set("physis_fire_loop", Json::parse(R"({
          "synth": [
            { "wave": "noise", "freq": 220, "duration": 2.4, "attack": 0.0, "release": 0.0, "volume": 0.55, "lowpass": 520 },
            { "wave": "noise", "freq": 220, "duration": 2.4, "attack": 0.0, "release": 0.0, "volume": 0.22, "lowpass": 3200 },
            { "wave": "triangle", "freq": [62, 48], "duration": 2.4, "attack": 0.0, "release": 0.0, "volume": 0.12, "vibrato": [3.1, 0.2] }
          ],
          "loop": true, "bus": "ambient", "volume": 0.9, "maxInstances": 6, "steal": "quietest",
          "minDistance": 6, "maxDistance": 140, "priority": 2
        })"));
        ++added;
    }
    if (!sounds.has("physis_wind_loop")) {
        sounds.set("physis_wind_loop", Json::parse(R"({
          "synth": [
            { "wave": "noise", "freq": 220, "duration": 3.0, "attack": 0.0, "release": 0.0, "volume": 0.6, "lowpass": 380 },
            { "wave": "noise", "freq": 220, "duration": 3.0, "attack": 0.0, "release": 0.0, "volume": 0.18, "lowpass": 1400, "vibrato": [0.3, 0.4] }
          ],
          "loop": true, "bus": "ambient", "spatial": false, "volume": 0.7, "maxInstances": 1, "priority": 1
        })"));
        ++added;
    }
    return added;
}

} // namespace aiverse::world
