// Genre-agnostic bridge from a World component pool to a deterministic
// gameplay policy. Native RTS, FPS, racing, strategy and hybrid projects can
// bind their own component types without depending on a genre GameModule.
//
// Policies are invoked in the lead pool's stable dense order. Each callback
// may update only its owned component and other explicitly synchronized state.
// All structural operations and ordered event emission must be deferred to
// a separate canonical commit barrier.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/sim/Simulation.h"
#include <cstddef>
#include <string>
#include <utility>

namespace aiverse::lib::mechanics {

template<class Component,class Policy>
size_t eachEcsMechanic(World& world,Policy&& policy) {
    // Prepare the writable pool exactly once so COW detaches on the caller
    // rather than implicitly inside each callback.
    auto* pool=world.prepareExistingWrite<Component>();
    if(!pool)return 0;
    const size_t n=pool->size();
    for(size_t i=0;i<n;++i)
        policy(pool->entityAt(i),pool->dataAt(i));
    return n;
}

// Installs a system into any GameModule's Simulation. The component and
// policy types are chosen by the native game, not hardcoded to RTS or FPS.
// The policy signature is (Simulation&, Entity, Component&).
template<class Component,class Policy>
void addEcsMechanicSystem(Simulation& sim,std::string name,Policy policy) {
    sim.addSystem(std::move(name),
        [policy=std::move(policy)](Simulation& current) mutable {
            eachEcsMechanic<Component>(current.world(),
                [&](Entity entity,Component& component) {
                    policy(current,entity,component);
                });
        });
}
} // namespace aiverse::lib::mechanics
