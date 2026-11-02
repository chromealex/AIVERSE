// Per-entity presentation overrides set by view scripts (view.entity_fx, docs/scripting.md § Entity scale and
// opacity): scale, opacity, colour multiply, team colour mix and glow, ground shadow opacity / size. Presentation only:
// the simulation never reads them. The store lives with the view scripts (ViewScripts::entityFx) and is shared with
// RtsView through RtsScene::entityFx, like the paint layers. Values change at once or ease over a time in seconds of
// frame time (ViewScripts::update advances them before the frame's handlers run).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace rts::game {

// The drawn values. Defaults = no change.
struct EntityFxValues {
    enum : int { kScale = 0, kOpacity = 3, kTint = 4, kTeamMix = 7, kTeamGlow = 11, kShadow = 12, kShadowScale = 13, kLift = 16, kCorpseAge = 17, kCount = 18 };
    float v[kCount] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, -1};   // corpse age -1: the view's own clock
    float scale(int axis) const { return v[kScale + axis]; }
    float opacity() const { return v[kOpacity]; }
    bool identity() const { static const EntityFxValues d; for (int i = 0; i < kCount; ++i) if (v[i] != d.v[i]) return false; return true; }
};

struct EntityFxEntry {
    EntityFxValues from, to;       // easing from -> to over `time` seconds (time 0: `to` at once)
    float time = 0, age = 0;
    uint8_t ease = 0;              // 0 linear, 1 smoothstep
    bool shadowScaleOwn = false;   // false: the ground shadow follows `scale`
    float goneFor = -1;            // seconds since the entity left the simulation (-1: alive); dropped after kKeepDead
    static constexpr float kKeepDead = 10.0f;   // a corpse's death / fade can still use the values
    EntityFxValues current() const {
        if (time <= 0 || age >= time) return to;
        float t = std::clamp(age / time, 0.0f, 1.0f);
        if (ease == 1) t = t * t * (3 - 2 * t);
        EntityFxValues o;
        for (int i = 0; i < EntityFxValues::kCount; ++i) o.v[i] = from.v[i] + (to.v[i] - from.v[i]) * t;
        return o;
    }
};

struct EntityFxStore {
    std::unordered_map<uint64_t, EntityFxEntry> entries;   // packed entity id -> entry
    // The values to draw for an entity (with the shadow scale resolved); false = none (draw unchanged).
    bool lookup(uint64_t entity, EntityFxValues& out) const {
        auto it = entries.find(entity);
        if (it == entries.end()) return false;
        out = it->second.current();
        if (!it->second.shadowScaleOwn)
            for (int a = 0; a < 3; ++a) out.v[EntityFxValues::kShadowScale + a] = out.v[EntityFxValues::kScale + a];
        return true;
    }
    void advance(float dt) { for (auto& [k, e] : entries) e.age += dt; }
};

} // namespace rts::game
