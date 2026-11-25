// Optional PHYSIS -> FRACTURE adapter, not a second environmental damage solver.
// Call after physis.nature_stress, before fracture.support.
// PHYSIS owns and persists environmental stress / its NatureFractureOutbox.
#pragma once
#include "FractureEcs.h"
#include "engine/physis/PhysisNatureStress.h"
#include <cstddef>
#include <string>
#include <utility>

namespace aiverse::fracture {
// An already fractured PHYSIS source marks its corresponding structural member
// broken. The source of stress remains PHYSIS ECS and the structural state is
// updated only on the existing Member entity. Never reset PHYSIS stress here.
inline std::size_t applyPhysisFractures(World& world){
    auto* members=world.prepareExistingWrite<Member>();
    if(!members)return 0;
    const World& read=world;
    std::size_t count=0;
    for(std::size_t i=0;i<members->size();++i){
        const Entity e=members->entityAt(i);
        if(!read.alive(e))continue;
        const auto* stress=read.tryGetConst<physis::NatureStressOutputComponent>(e);
        if(stress && stress->valid && stress->fractured && !members->dataAt(i).broken){
            members->dataAt(i).broken=true;
            ++count;
        }
    }
    return count;
}
inline void addPhysisFractureBridgeSystem(Simulation& sim,
                                         std::string name="fracture.physis_bridge"){
    sim.addSystem(std::move(name),[](Simulation& s){
        (void)applyPhysisFractures(s.world());
    });
}
// IMPORTANT: A member that already publishes a PHYSIS nature fracture
// should NOT attach a second Collapse outbox for the SAME physical fall.
// PHYSIS's outbox carries that primary event; attach Collapse on downstream
// unsupported members whose detached bodies need a distinct authoritative
// host reaction. An external sink must reconcile its durable ledger for all
// physical effects and acknowledge both event types only after committing.
} // namespace aiverse::fracture
