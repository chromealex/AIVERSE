// Ordered, genre-independent impact dispatch over ECS entity identities.
// A motion system (kinematic, ballistic or hitscan) decides WHEN a hit occurs;
// the game's damage policy decides HOW armor, shields and effects are applied.
// No physics world or genre-specific Unit/Actor representation is required.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <utility>

namespace aiverse::lib::mechanics {

// Transient impact data: do not persist or introduce a new gameplay owner.
// Callers retain their source projectile/target ECS components as authority.
struct OrderedProjectileImpact {
    Entity projectile=kNullEntity;
    Entity source=kNullEntity;
    Entity target=kNullEntity;
    Vec3 point{};
    Fixed damage{}, splashRadius{};
};

// DirectHit: void(Entity) -- called exactly once, including for missing targets.
// AfterDirect: void(Entity) -- queued hit effects; run after direct damage.
// QueryArea: void(Vec3,Fixed,visitor(Entity)) -- deterministic candidate order.
// CanSplash: bool(Entity) -- native rules (faction, unit type, distance etc.).
// SplashHit: void(Entity) -- applies splash immediately for this candidate.
//
// Deliberately resolves direct damage, on-hit staging, then area candidates
// for EACH projectile in the caller's ECS traversal order. Do not batch all
// direct hits before all area hits: doing so changes legacy RTS callbacks,
// status/shield absorption, and same-tick damage ordering.
template<class DirectHit,class AfterDirect,class QueryArea,
         class CanSplash,class SplashHit>
inline void resolveOrderedProjectileImpact(
        const OrderedProjectileImpact& impact,
        DirectHit&& directHit,
        AfterDirect&& afterDirect,
        QueryArea&& queryArea,
        CanSplash&& canSplash,
        SplashHit&& splashHit) {
    directHit(impact.target);
    afterDirect(impact.target);
    if(impact.splashRadius<=Fixed{})return;
    queryArea(impact.point,impact.splashRadius,[&](Entity candidate) {
        // Exactly one direct target; never double-apply it through area damage.
        if(candidate==impact.target || !canSplash(candidate))return;
        splashHit(candidate);
    });
}

} // namespace aiverse::lib::mechanics
