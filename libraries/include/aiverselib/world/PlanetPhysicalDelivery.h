// Common delivery barrier; reuse FRACTURE's existing durable outbox and
// EVOLVE's plant fall outbox. The game supplies its actual durable owner.
#pragma once
#include "PlanetFractureDelivery.h"
#include "EvolvePlanetRootSupport.h"
namespace aiverse::lib::worldprocess {
struct PlanetPhysicalDeliveryBatch {
    std::vector<fracture::PlanetCollapseDelivery> collapses;
    std::vector<evolve::PlanetPlantFall> plants;
};
inline PlanetPhysicalDeliveryBatch pendingPlanetPhysicalDelivery(
        const World& world,const game::Surface& surface,std::string_view body,uint32_t tick){
    PlanetPhysicalDeliveryBatch batch;
    for(const auto& event:fracture::pending(world)){
        if(event.tick>tick)continue;
        const auto projected=fracture::projectPlanetCollapse(world,surface,event);
        if(projected&&surface.bodies[size_t(projected->frame.body)].id==body)
            batch.collapses.push_back(*projected);
    }
    for(const auto& fall:evolve::pendingPlanetPlantFalls(world))
        if(fall.bodyId==body&&fall.event.tick<=tick)batch.plants.push_back(fall);
    return batch;
}
// Process the selected body's pending events as one staged owner transaction.
// Callbacks must persist actual shape/pose and retire the original support,
// not allocate a transient Box3D body. False rejects all acknowledgements.
template<class CollapseOwner,class PlantOwner>
inline bool commitPlanetPhysicalDelivery(World& world,const game::Surface& surface,
        std::string_view body,uint32_t tick,CollapseOwner&& collapse,PlantOwner&& plant){
    if(tick==UINT32_MAX||surface.findBody(std::string(body))<0)return false;
    const auto batch=pendingPlanetPhysicalDelivery(world,surface,body,tick);
    if(batch.collapses.empty()&&batch.plants.empty())return true;
    World staged=world;
    for(const auto& event:batch.collapses)
        if(!fracture::commitPlanetCollapse(staged,surface,event.event,tick,collapse))return false;
    for(const auto& event:batch.plants)
        if(!evolve::commitPlanetPlantFall(staged,event,tick,plant))return false;
    world=std::move(staged);return true;
}
} // namespace aiverse::lib::worldprocess
