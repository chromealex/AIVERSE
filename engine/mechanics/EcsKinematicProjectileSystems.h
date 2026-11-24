// Deterministic non-physical projectile motion for any game genre.
// Kinematic projectiles do not query collision or integrate rigid-body forces.
// The host resolves target lifetime and applies damage in a later system.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/mechanics/EcsMechanicSystems.h"
#include "engine/core/Fixed.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::mechanics {

enum class KinematicMotionMode : uint8_t {
    TimedEndpoint, // travel completes after a fixed number of simulation ticks
    Pursuit        // move by a fixed distance each tick toward the current aim
};

struct KinematicProjectileComponent {
    Vec3 origin{}, position{}, destination{};
    Entity owner=kNullEntity;
    Entity target=kNullEntity;
    uint32_t shotId=0;
    uint32_t elapsed=0, duration=1; // also the maximum lifetime for Pursuit
    Fixed distancePerTick{};        // only used by Pursuit, not by TimedEndpoint
    KinematicMotionMode mode=KinematicMotionMode::TimedEndpoint;
    bool alive=true, arrived=false;
};

struct KinematicArrivalEvent {
    Entity projectile=kNullEntity, owner=kNullEntity, target=kNullEntity;
    uint32_t shotId=0;
    Vec3 position{};
};

// Pure non-physical pursuit step shared by real RTS Projectile/Transform ECS
// and by the generic kinematic component. This intentionally preserves the
// engine moveTowards rounding and arrival semantics, including a moving aim.
inline bool advanceKinematicPursuit(Vec3& position,Vec3 destination,
                                   Fixed distancePerTick) {
    return moveTowards(position,destination,distancePerTick);
}

// No physics, raycasts or RNG. The caller resolves moving targets before each
// Pursuit step. TimedEndpoint preserves its original fixed-tick interpolation.
// The arrived bit distinguishes a hit from a projectile that merely timed out.
inline bool stepKinematicProjectile(KinematicProjectileComponent& shot) {
    if(!shot.alive || shot.duration==0 || shot.duration>100000 ||
       shot.elapsed>shot.duration ||
       (shot.mode==KinematicMotionMode::Pursuit &&
        shot.distancePerTick<=Fixed{}))return false;
    shot.arrived=false;
    if(shot.elapsed==shot.duration){shot.alive=false;return true;}
    ++shot.elapsed;
    if(shot.mode==KinematicMotionMode::Pursuit) {
        shot.arrived=advanceKinematicPursuit(shot.position,shot.destination,
                                            shot.distancePerTick);
    } else {
        const Fixed fraction=Fixed::fromRatio(shot.elapsed,shot.duration);
        shot.position=shot.origin+(shot.destination-shot.origin)*fraction;
        shot.arrived=shot.elapsed==shot.duration;
    }
    if(shot.arrived || shot.elapsed==shot.duration)shot.alive=false;
    return true;
}

// Games can substitute a deterministic target resolver that returns an updated
// destination every tick. A dead target may keep its last known destination.
struct KeepKinematicDestination {
    Vec3 operator()(Simulation&,const KinematicProjectileComponent& shot) const {
        return shot.destination;
    }
};

// ArrivalSink: void(Simulation&,const KinematicArrivalEvent&).
// Events are emitted in stable Entity-index order after the ECS traversal;
// swap-removal of older shots must not reorder same-tick damage callbacks.
// Despawn is deferred. The sink decides if a stale target takes damage.
// Do not register this system and the physical flight system for the same
// projectile entity.
template<class ArrivalSink,class DestinationResolver=KeepKinematicDestination>
void addKinematicProjectileSystem(Simulation& sim,std::string name,
                                  ArrivalSink onArrival,
                                  DestinationResolver resolveDestination={}) {
    sim.addSystem(std::move(name),
        [onArrival=std::move(onArrival),
         resolveDestination=std::move(resolveDestination)](Simulation& current) mutable {
            std::vector<KinematicArrivalEvent> arrivals;
            std::vector<Entity> expired;
            World& world=current.world();
            eachEcsMechanic<KinematicProjectileComponent>(world,
                [&](Entity entity,KinematicProjectileComponent& shot) {
                    if(!world.alive(entity))return;
                    if(!shot.alive){expired.push_back(entity);return;}
                    if(shot.mode==KinematicMotionMode::Pursuit)
                        shot.destination=resolveDestination(current,shot);
                    if(!stepKinematicProjectile(shot))return;
                    if(!shot.alive) {
                        if(shot.arrived)
                            arrivals.push_back({entity,shot.owner,shot.target,
                                                shot.shotId,shot.position});
                        expired.push_back(entity);
                    }
                });
            // Pool swap-removals can change dense iteration order as older
            // projectiles expire. Preserve canonical shot/Entity ordering for
            // simultaneous arrivals regardless of earlier lifecycle changes.
            std::sort(arrivals.begin(),arrivals.end(),
                [](const KinematicArrivalEvent& a,const KinematicArrivalEvent& b) {
                    return a.projectile.index<b.projectile.index;
                });
            for(const auto& event:arrivals)onArrival(current,event);
            for(Entity entity:expired)world.destroyLater(entity);
        });
}
} // namespace aiverse::lib::mechanics
