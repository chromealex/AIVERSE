// TerraWeave Stage62: native ECS-only ownership of automatically derived
// NEREID flow obstructions. No Box3D, renderer or genre dependency.
// Physics/source adapters may feed this through a structural-write barrier.
#pragma once
#include "engine/world/nereid/NereidFlowObstacles.h"
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

// Generated obstacle tag separates authoritative auto obstacles from hand
// authored NEREID debris/bridge obstructions. Stable sourceKey links a
// fallen tree/debris event to its current physics pose across rollback.
struct AutoFlowObstructionComponent {
    uint64_t sourceKey=0;
};
inline void io(Archive& a,AutoFlowObstructionComponent& c){
    a(c.sourceKey);
}
inline void registerTerraWeaveObstructionPersistence(Simulation& sim){
    sim.registerPersistentComponent<AutoFlowObstructionComponent>(
        "terraweave.auto_flow_obstruction",1);
}

struct FlowObstructionPublishResult {
    uint32_t added=0,updated=0,removed=0;
};

// Run only at the authoritative ECS STRUCTURAL barrier after Box3D step,
// before the next NEREID water sampling. Replacing derived obstructions does
// not rewrite any WaterBodyComponent/flow/geometry.
// Existing manually authored obstruction entities are NEVER touched.
inline FlowObstructionPublishResult publishNereidFlowObstructions(
        World& world,
        const std::vector<lib::nereid::WaterFlowObstructionComponent>& desired){
    using Obstacle=lib::nereid::WaterFlowObstructionComponent;
    if(desired.size()>lib::nereid::kMaxWaterFlowObstructions)
        throw std::length_error("TerraWeave water obstruction ECS budget");
    std::map<uint64_t,Obstacle> wanted;
    for(const auto& item:desired){
        if(!item.sourceKey || !item.enabled ||
           !lib::nereid::validWaterFlowObstruction(item) ||
           !world.alive(item.waterBody) ||
           !world.has<lib::nereid::WaterBodyComponent>(item.waterBody) ||
           !wanted.emplace(item.sourceKey,item).second)
            throw std::invalid_argument(
                "TerraWeave invalid/duplicated water obstruction ECS publication");
    }
    std::map<uint64_t,Entity> previous;
    world.eachExistingConst<AutoFlowObstructionComponent>(
        [&](Entity entity,const AutoFlowObstructionComponent& tag){
            const auto* obstacle=world.tryGetConst<Obstacle>(entity);
            if(!tag.sourceKey || !obstacle ||
               obstacle->sourceKey!=tag.sourceKey ||
               !previous.emplace(tag.sourceKey,entity).second)
                throw std::invalid_argument(
                    "TerraWeave corrupt auto NEREID obstruction identity");
        });
    if(previous.size()>lib::nereid::kMaxWaterFlowObstructions)
        throw std::length_error("TerraWeave retained NEREID obstruction budget");

    // Structural mutation only after every desired/source identity has been
    // validated. Existing water/forest/manual obstacles are never deleted.
    FlowObstructionPublishResult result;
    for(const auto& [key,entity]:previous){
        if(wanted.contains(key))continue;
        world.destroy(entity);
        ++result.removed;
    }
    // COW pool ownership is detached before direct in-place component writes.
    // New entities are created via World::create/add at the structural barrier.
    auto* records=&world.pool<Obstacle>();
    for(const auto& [key,item]:wanted){
        const auto known=previous.find(key);
        if(known==previous.end()){
            const Entity entity=world.create();
            world.add<AutoFlowObstructionComponent>(
                entity,AutoFlowObstructionComponent{key});
            world.add<Obstacle>(entity,item);
            ++result.added;
        }else{
            auto* mutableValue=records->tryGet(known->second.index);
            if(!mutableValue)
                throw std::logic_error(
                    "TerraWeave auto obstruction disappeared during update");
            *mutableValue=item;
            ++result.updated;
        }
    }
    return result;
}

} // namespace aiverse::terraweave
