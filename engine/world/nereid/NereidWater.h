// AIVERSE NEREID -- shared deterministic water body definitions and sampling.
// The legacy RTS waterNetwork/baked map cells stay authoritative for old maps;
// this capability is opt-in and has no genre-module or renderer dependency.
#pragma once
#include "engine/core/Archive.h"
#include <cstdint>

namespace aiverse::lib::nereid {

enum class WaterKind : uint8_t { Ocean=0, Lake=1, River=2, Pool=3, Puddle=4 };
enum class WaterFootprint : uint8_t { Infinite=0, Box=1, Disc=2 };

// Y is up; X/Z are horizontal. All units use AIVERSE Fixed metres.
// Body data is authoritative ECS state, not a GPU displacement field.
struct WaterBodyComponent {
    WaterKind kind=WaterKind::Lake;
    WaterFootprint footprint=WaterFootprint::Box;
    Vec3 center{};
    Fixed surfaceY=1_fx;
    Fixed bottomY{};
    Vec2 halfExtents{1_fx,1_fx};  // X/Z when footprint==Box
    Fixed radius=1_fx;           // horizontal when footprint==Disc
    Vec3 flow{};                 // metres/second, deterministic baseline
    int32_t priority=0;          // larger overrides smaller at overlaps
    bool enabled=true;
};

// Position is supplied by a movement/AI/physics system, never by the renderer.
struct WaterProbeComponent { Vec3 position{}; };

// Sampling returns the highest-priority covering surface even if the point is
// above it. "submerged" is true only between the configured floor and surface.
struct WaterSample {
    Entity body=kNullEntity;
    WaterKind kind=WaterKind::Lake;
    Fixed surfaceY{};
    Fixed bottomY{};
    Fixed depth{};
    Vec3 flow{};
    int32_t priority=0;
    bool found=false;
    bool submerged=false;
};
struct WaterContactComponent {
    WaterSample sample{};
    uint32_t sampledTick=0;
};

inline bool validWaterBody(const WaterBodyComponent& b) {
    if(!b.enabled || b.surfaceY<=b.bottomY)return false;
    switch(b.footprint) {
        case WaterFootprint::Infinite:return true;
        case WaterFootprint::Box:return b.halfExtents.x>Fixed{} && b.halfExtents.y>Fixed{};
        case WaterFootprint::Disc:return b.radius>Fixed{};
        default:return false;
    }
}
inline bool coversXZ(const WaterBodyComponent& b,Vec3 p) {
    if(!validWaterBody(b))return false;
    switch(b.footprint) {
        case WaterFootprint::Infinite:return true;
        case WaterFootprint::Box:
            return fxAbs(p.x-b.center.x)<=b.halfExtents.x &&
                   fxAbs(p.z-b.center.z)<=b.halfExtents.y;
        case WaterFootprint::Disc: {
            const Vec2 delta{p.x-b.center.x,p.z-b.center.z};
            return delta.lengthSq()<=b.radius*b.radius;
        }
        default:return false;
    }
}

// Explicit field-wise serialization, stable across machines and struct padding.
// Register these schemas before Simulation::setup seals persistence registration.
inline void io(Archive& a,WaterBodyComponent& b) {
    a(b.kind,b.footprint,b.center,b.surfaceY,b.bottomY,b.halfExtents.x,
      b.halfExtents.y,b.radius,b.flow,b.priority,b.enabled);
}
inline void io(Archive& a,WaterProbeComponent& p) {a(p.position);}
inline void io(Archive& a,WaterSample& s) {
    a(s.body,s.kind,s.surfaceY,s.bottomY,s.depth,s.flow,s.priority,s.found,s.submerged);
}
inline void io(Archive& a,WaterContactComponent& c) {a(c.sample,c.sampledTick);}

} // namespace aiverse::lib::nereid
