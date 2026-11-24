// A shared ECS mechanic: projectile flight and impact staging for every genre.
// The same components and system can be attached to RTS units, FPS weapons,
// turrets on vehicles, drones, spells or user-defined native game objects.
//
// Flight movement/collision policy is independent from who launched the shot.
// Damage, explosions, audio, particle effects, score and presentation are
// deliberately handled by separate systems consuming canonical impact events.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/mechanics/EcsMechanicSystems.h"
#include "engine/mechanics/EcsSweepQuerySystems.h"
#include "engine/mechanics/Weapons.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::mechanics {

struct ProjectileFlightComponent {
    ProjectileConfig config{};
    ProjectileState state{};
    Entity owner=kNullEntity;
    uint32_t shotId=0; // stable semantic ID for replay and cross-system ordering
};

struct ProjectileImpactEvent {
    Entity projectile=kNullEntity;
    Entity owner=kNullEntity;
    uint32_t shotId=0;
    ProjectileImpact contact{};
};

// Lightweight deterministic bounce policy for timed grenades, rolling
// mines, dropped objects and other physical projectiles that have their
// OWN expiration/fuse system. Does not apply impact damage or despawn.
// The divisor form is deliberate: it preserves fixed-point /2_fx rounding
// of the pre-ECS FPS grenade solver, unlike multiplying by a half constant.
struct BounceMotionConfig {
    Fixed gravity=10_fx;
    Fixed bounceDivisor=2_fx;
    Fixed impactFractionInset=Fixed::fromRatio(1,100);
    Vec3 halfExtent{Fixed::fromRatio(1,10),
                    Fixed::fromRatio(1,10),
                    Fixed::fromRatio(1,10)};
};

inline void stepBouncingProjectile(Vec3& position,Vec3& velocity,
                                   const world::SpatialWorld& collision,
                                   Fixed dt,const BounceMotionConfig& config={}) {
    velocity.y-=config.gravity*dt;
    const Vec3 delta=velocity*dt;
    const auto hit=collision.cast(position,delta,config.halfExtent);
    if(hit.hit) {
        position+=delta*fxMax(Fixed{},hit.fraction-config.impactFractionInset);
        velocity=(velocity-hit.normal*velocity.dot(hit.normal)*2_fx)
                 /config.bounceDivisor;
    } else position+=delta;
}

// Shared ballistic policy: bullets, shells, launched grenades and any object
// with the same gravity/radius/bounce parameters use the SAME flight solver.
// Other policies may implement steering/homing and still use this ECS system.
struct BallisticFlightPolicy {
    ProjectileStep operator()(ProjectileFlightComponent& projectile,
                              const world::SpatialWorld& collision,Fixed dt) const {
        return stepProjectile(projectile.config,projectile.state,collision,dt);
    }
};

// Provider: const world::SpatialWorld&(Simulation&) to support arbitrary
// world/map/collision owners without GameCore/actor::ActorHost coupling.
// ImpactSink: void(Simulation&, const ProjectileImpactEvent&), called only
// AFTER all projectile updates in canonical dense order.
// FlightPolicy: ProjectileStep(ProjectileFlightComponent&,SpatialWorld&,Fixed)
// (default BallisticFlightPolicy).
//
// This system owns projectile motion and despawning, but not damage logic.
// In particular, do not allocate a new projectile from the impact callback
// while iterating or use thread-dependent RNG. Damage/explosions must be
// handled in a separate ordered impact/commit stage.
template<class CollisionProvider,class ImpactSink,
         class FlightPolicy=BallisticFlightPolicy>
void addProjectileFlightSystem(Simulation& sim,std::string systemName,
                               CollisionProvider collisionSource,
                               ImpactSink onImpact,
                               FlightPolicy flightPolicy={}) {
    sim.addSystem(std::move(systemName),
        [collisionSource=std::move(collisionSource),
         onImpact=std::move(onImpact),
         flightPolicy=std::move(flightPolicy)](Simulation& current) mutable {
            const world::SpatialWorld& collision=collisionSource(current);
            std::vector<ProjectileImpactEvent> hits;
            std::vector<Entity> expired;
            eachEcsMechanic<ProjectileFlightComponent>(current.world(),
                [&](Entity entity,ProjectileFlightComponent& projectile) {
                    if(!projectile.state.alive) {
                        expired.push_back(entity);
                        return;
                    }
                    const auto result=flightPolicy(projectile,collision,current.dt());
                    if(!result.accepted)return; // invalid input: no gameplay side effects
                    if(result.impact)
                        hits.push_back({entity,projectile.owner,projectile.shotId,
                                        *result.impact});
                    if(!projectile.state.alive)expired.push_back(entity);
                });
            // Canonical order = ECS dense order of the ProjectileFlight pool.
            for(const auto& hit:hits)onImpact(current,hit);
            for(Entity entity:expired)current.world().destroyLater(entity);
        });
}

} // namespace aiverse::lib::mechanics
