// Advisory region work only. The canonical EVOLVE pressure integrator and
// TerraWeave forest lifecycle still run according to their owning schedules.
#pragma once
#include "EvolveArea.h"
#include "engine/world/SimulationLod.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace aiverse::evolve {
struct RegionWorkDecision {
    Entity region=kNullEntity;
    world::SimulationLodDecision lod{};
};
struct RegionWorkPlan {
    std::vector<RegionWorkDecision> regions;
    world::SimulationLodDiagnostics diagnostics{};
};
// Floor division, including negative world coordinates. ECS extents are
// bounded to +/-kAreaCoordinateCm so the result fits a signed cell index.
inline int32_t lodCell(I cm,int32_t cellSizeCm) noexcept {
    const I q=cm/cellSizeCm;
    return int32_t(q-(cm%cellSizeCm<0?1:0));
}
// EVOLVE is planar: choose the nearest point of the rectangle in XZ cell
// space, not its center. Project away observer height. Half-open max bounds
// become inclusive integer centimetres before quantization.
// Observer cells must use the same caller-selected cellSizeCm.
inline world::SimulationLodDecision regionWorkDecision(
        const world::SimulationLodConfig& config,const Extent& extent,
        std::span<const world::Cell> observers,int32_t cellSizeCm,
        uint32_t tick,bool critical=false) noexcept {
    if(cellSizeCm<=0||observers.empty()||!extent.region.valid()||
       extent.minXcm>=extent.maxXcm||extent.minZcm>=extent.maxZcm||
       extent.minXcm< -kAreaCoordinateCm||extent.minZcm< -kAreaCoordinateCm||
       extent.maxXcm>kAreaCoordinateCm||extent.maxZcm>kAreaCoordinateCm)
        return {};
    const auto minX=lodCell(extent.minXcm,cellSizeCm);
    const auto maxX=lodCell(extent.maxXcm-1,cellSizeCm);
    const auto minZ=lodCell(extent.minZcm,cellSizeCm);
    const auto maxZ=lodCell(extent.maxZcm-1,cellSizeCm);
    uint64_t nearest=std::numeric_limits<uint64_t>::max();
    world::Cell point{},focus{};
    for(auto observer:observers){
        observer.y=0;
        const world::Cell candidate{std::clamp(observer.x,minX,maxX),0,
                                    std::clamp(observer.z,minZ,maxZ)};
        const auto d=world::cellDistance(candidate,observer);
        if(d<nearest){nearest=d;point=candidate;focus=observer;}
        if(!nearest)break;
    }
    return world::simulationLodDecision(config,point,
        std::span<const world::Cell>(&focus,1),extent.region.packed(),tick,critical);
}
// A read-only, stable-ID ordered plan. Regions without valid Area are kept
// hot rather than silently lost. Critical predicate is owner-provided and
// must read authoritative state only. No observer cache or extra clock.
template<class Critical>
inline RegionWorkPlan planRegionWork(const World& ecs,
        const world::SimulationLodConfig& config,
        std::span<const world::Cell> observers,int32_t cellSizeCm,
        uint32_t tick,Critical critical){
    RegionWorkPlan plan;
    plan.diagnostics.tick=tick;
    const auto areas=extents(ecs);
    ecs.eachExistingConst<Region>([&](Entity e,const Region& region){
        if(!ecs.alive(e)||!region.enabled||!valid(region))return;
        auto it=std::lower_bound(areas.begin(),areas.end(),e.packed(),
            [](const Extent& x,uint64_t id){return x.region.packed()<id;});
        world::SimulationLodDecision lod{};
        if(it!=areas.end()&&it->region==e)
            lod=regionWorkDecision(config,*it,observers,cellSizeCm,tick,critical(e));
        plan.regions.push_back({e,lod});
        auto& d=plan.diagnostics;
        switch(lod.tier){
            case world::SimulationLodTier::Hot:++d.hot;break;
            case world::SimulationLodTier::Warm:++d.warm;break;
            case world::SimulationLodTier::Cold:++d.cold;break;
        }
        // executed means due/planned, not proof a callback completed.
        if(lod.due)++d.executed;else ++d.deferred;
    });
    std::sort(plan.regions.begin(),plan.regions.end(),[](const auto& a,const auto& b){
        return a.region.packed()<b.region.packed();
    });
    return plan;
}
inline RegionWorkPlan planRegionWork(const World& ecs,
        const world::SimulationLodConfig& config,
        std::span<const world::Cell> observers,int32_t cellSizeCm,uint32_t tick){
    return planRegionWork(ecs,config,observers,cellSizeCm,tick,[](Entity){return false;});
}
} // namespace aiverse::evolve
