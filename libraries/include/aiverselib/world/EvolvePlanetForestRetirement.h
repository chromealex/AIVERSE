// Retirement of spent planetary forest bodies. The existing deadwood ledger
// owns mass; the existing consequence owner owns physical records.
#pragma once
#include "EvolvePlanetDeadwood.h"
#include "PlanetConsequenceOwner.h"

namespace aiverse::lib::worldprocess {
struct PlanetForestRetirement {
    std::vector<ConsequenceKey> retired;
};
// Call at the authoritative world barrier, before publishing the next
// native physics frame. A missing ledger entry never means zero mass.
// Stable plant IDs are not reused after a delivered fall.
inline std::optional<PlanetForestRetirement> retireSpentPlanetForestBodies(
        World& world,const game::Surface& surface,uint32_t tick){
    using namespace terraweave;
    const auto* source=world.resourceConst<PlanetConsequenceResource>();
    PlanetForestRetirement result;
    if(tick==UINT32_MAX)return {};
    if(!source||source->bodies.empty())return result;
    const auto* wood=world.resourceConst<evolve::PlanetDeadwoodResource>();
    const auto* roots=world.resourceConst<TerraWeaveAuthorityResource>();
    const auto* delivery=world.resourceConst<evolve::PlanetRootDeliveryResource>();
    if(!wood||!roots||!delivery||!surface.tab||
       source->bodies.size()>kMaxConsequences||
       roots->rooted.size()!=delivery->bindings.size()||
       roots->rooted.size()>evolve::kMaxPlanetRootBindings||
       delivery->events.size()>evolve::kMaxPlanetPlantFallEvents||
       !evolve::validPlanetDeadwood(*wood,surface))return {};
    try{
        std::map<AnchoredPlantId,const AnchoredPlant*> plants;
        for(const auto& plant:roots->rooted){
            const AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
            if(!id.speciesId||!id.key||!plants.emplace(id,&plant).second)return {};
        }
        std::map<AnchoredPlantId,const evolve::PlanetRootBinding*> bindings;
        for(const auto& binding:delivery->bindings)
            if(!plants.contains(binding.id)||!bindings.emplace(binding.id,&binding).second)return {};
        std::map<ConsequenceKey,const evolve::PlanetPlantFallRecord*> receipts;
        for(const auto& receipt:delivery->events){
            if(!evolve::validPlanetPlantFall(receipt.fall)||
               !receipts.emplace(consequenceKey(receipt.fall),&receipt).second)return {};
        }
        auto next=*source;
        std::vector<ConsequenceBody> retained;
        retained.reserve(next.bodies.size());
        for(const auto& record:next.bodies){
            bool remove=false;
            if(record.key.kind==ConsequenceKind::Plant&&record.key.tick<=tick&&
               (record.lastMotionTick==UINT32_MAX||record.lastMotionTick<=tick)){
                const AnchoredPlantId id{record.key.species,record.key.source};
                const auto plant=plants.find(id);
                const auto binding=bindings.find(id);
                const auto receipt=receipts.find(record.key);
                const auto log=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),id,
                    [](const auto& entry,auto key){return entry.id<key;});
                if(plant!=plants.end()&&plant->second->fallen&&binding!=bindings.end()&&
                   receipt!=receipts.end()&&receipt->second->acknowledged&&
                   log!=wood->ledger.logs.end()&&log->id==id&&log->remainingUnits==0&&
                   log->lastEvaluatedTick<=tick&&log->createdTick<=tick&&
                   record.body==binding->second->bodyId&&record.body==receipt->second->fall.bodyId&&
                   binding->second->plantedPosition==receipt->second->fall.plantedPosition){
                    const auto address=evolve::planetDeadwoodAddress(surface,*binding->second);
                    if(!address||!validConsequence(record))return {};
                    remove=address->first==log->xCm&&address->second==log->zCm;
                }
            }
            if(remove)result.retired.push_back(record.key);
            else retained.push_back(record);
        }
        if(result.retired.empty())return result;
        next.bodies=std::move(retained);
        world.setResource<PlanetConsequenceResource>(std::move(next));
        // Do not erase the acknowledged outbox, deadwood tombstone, roots,
        // nutrient accounting, or any structure consequence.
        return result;
    }catch(const std::exception&){return {};}
}
} // namespace aiverse::lib::worldprocess
