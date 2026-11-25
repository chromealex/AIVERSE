// Pending plant fall lifecycle at the existing deterministic forest barrier.
#pragma once
#include "PlanetArborCollision.h"
namespace aiverse::lib::worldprocess {
// Scope a pending delivery to the current fallen root and exact latest
// receipt at its historic placement. Older receipts are not reinterpreted
// as a new life of the same plant ID.
inline bool currentPlanetForestFall(const World& world,const game::Surface& surface,
        const evolve::PlanetPlantFall& fall){
    using namespace terraweave;
    const auto* roots=world.resourceConst<TerraWeaveAuthorityResource>();
    const auto* delivery=world.resourceConst<evolve::PlanetRootDeliveryResource>();
    if(!roots||!delivery||!evolve::validPlanetPlantFall(fall))return false;
    const auto p=std::find_if(roots->rooted.begin(),roots->rooted.end(),[&](const auto& plant){
        return AnchoredPlantId{plant.anchor.speciesId,plant.anchor.plantKey}==fall.event.id;
    });
    const auto b=std::find_if(delivery->bindings.begin(),delivery->bindings.end(),[&](const auto& binding){
        return binding.id==fall.event.id;
    });
    if(p==roots->rooted.end()||!p->fallen||b==delivery->bindings.end()||
       b->bodyId!=fall.bodyId||b->plantedPosition!=fall.plantedPosition||
       !evolve::planetDeadwoodAddress(surface,*b))return false;
    const auto key=evolve::planetFallKey(fall);
    bool exact=false;
    for(const auto& record:delivery->events){
        if(record.fall.event.id!=fall.event.id||record.fall.bodyId!=fall.bodyId||
           record.fall.plantedPosition!=fall.plantedPosition)continue;
        const auto other=evolve::planetFallKey(record.fall);
        if(other>key)return false;
        if(other==key&&!record.acknowledged)exact=true;
    }
    return exact;
}
// Close a fully spent pending receipt without creating a native body.
// The durable acknowledgement is a terminal source tombstone; it need not
// imply a physical object exists. Never erase it or manufacture new mass.
inline bool completeSpentPlanetFall(World& world,const game::Surface& surface,
        const evolve::PlanetPlantFall& fall,uint32_t tick){
    if(!currentPlanetForestFall(world,surface,fall))return false;
    return evolve::commitPlanetPlantFall(world,fall,tick,[&](World& staged,const auto& event){
        using namespace terraweave;
        const auto* roots=staged.resourceConst<TerraWeaveAuthorityResource>();
        const auto* bindings=staged.resourceConst<evolve::PlanetRootDeliveryResource>();
        const auto* wood=staged.resourceConst<evolve::PlanetDeadwoodResource>();
        if(!roots||!bindings||!wood||!evolve::validPlanetDeadwood(*wood,surface))return false;
        const auto p=std::find_if(roots->rooted.begin(),roots->rooted.end(),[&](const auto& plant){
            return AnchoredPlantId{plant.anchor.speciesId,plant.anchor.plantKey}==event.event.id;
        });
        const auto b=std::find_if(bindings->bindings.begin(),bindings->bindings.end(),[&](const auto& binding){
            return binding.id==event.event.id;
        });
        const auto log=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),event.event.id,
            [](const auto& r,auto id){return r.id<id;});
        if(p==roots->rooted.end()||!p->fallen||b==bindings->bindings.end()||
           b->bodyId!=event.bodyId||b->plantedPosition!=event.plantedPosition||
           log==wood->ledger.logs.end()||log->id!=event.event.id||log->remainingUnits||
           log->createdTick>tick||log->lastEvaluatedTick>tick)return false;
        if(const auto* physical=staged.resourceConst<PlanetConsequenceResource>())
            for(const auto& record:physical->bodies)if(record.key==consequenceKey(event))return false;
        const auto address=evolve::planetDeadwoodAddress(surface,*b);
        return address&&address->first==log->xCm&&address->second==log->zCm;
    });
}
} // namespace aiverse::lib::worldprocess
