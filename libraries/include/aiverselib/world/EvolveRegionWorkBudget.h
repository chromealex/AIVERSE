// EVOLVE compatibility adapter to the canonical advisory Entity queue.
#pragma once
#include "EvolveSimulationLod.h"
#include "WorldProcessEntityBudget.h"
#include <utility>

namespace aiverse::evolve {
struct PendingRegionWork {
    Entity region=kNullEntity;
    uint32_t firstDueTick=0;
    bool urgent=false;
};
using RegionBudgetState=lib::worldprocess::BasicEntityBudgetState<PendingRegionWork>;
using RegionBudgetBatch=lib::worldprocess::BasicEntityBudgetBatch<PendingRegionWork>;
using RegionBudgetOptions=lib::worldprocess::EntityBudgetOptions;
inline std::optional<RegionBudgetBatch> prepareRegionBudget(
        const RegionWorkPlan& plan,const RegionBudgetState& state,
        RegionBudgetOptions options){
    return lib::worldprocess::work_detail::prepare(plan.regions,
        plan.diagnostics.tick,state,options,kMaxRegions,
        [](const auto& item){return item.region;});
}
inline std::optional<RegionBudgetBatch> prepareRegionBudget(
        const RegionWorkPlan& plan,const RegionBudgetState& state,
        size_t maxVisitsPerTick){
    return prepareRegionBudget(plan,state,RegionBudgetOptions{maxVisitsPerTick,{}});
}
// Concrete optional consumer: bounded read-only ecological reporting.
// Consumers can publish these snapshots with their sample tick and retain
// older reports for deferred regions. Never feed stale reports back into
// the canonical pressure integrator or TerraWeave climate callback.
struct RegionReportSample {
    Entity region=kNullEntity;
    uint32_t requestedTick=0,sampleTick=0;
    Region value{};
};
struct RegionReportBatch {
    RegionBudgetBatch scheduling;
    std::vector<RegionReportSample> samples;
};
template<class Critical>
inline std::optional<RegionReportBatch> prepareRegionReports(
        const World& ecs,const world::SimulationLodConfig& config,
        std::span<const world::Cell> observers,int32_t cellSizeCm,
        uint32_t tick,const RegionBudgetState& state,RegionBudgetOptions options,
        Critical critical){
    if(ecs.countExisting<Region>()>kMaxRegions)return std::nullopt;
    const auto plan=planRegionWork(ecs,config,observers,cellSizeCm,tick,critical);
    auto scheduling=prepareRegionBudget(plan,state,options);
    if(!scheduling)return std::nullopt;
    RegionReportBatch out{std::move(*scheduling),{}};
    out.samples.reserve(out.scheduling.selected.size());
    for(const auto& item:out.scheduling.selected){
        const auto* r=ecs.tryGetConst<Region>(item.region);
        if(!ecs.alive(item.region)||!r||!r->enabled||!valid(*r))
            return std::nullopt;
        out.samples.push_back({item.region,item.firstDueTick,tick,*r});
    }
    return out;
}
template<class Critical>
inline std::optional<RegionReportBatch> prepareRegionReports(
        const World& ecs,const world::SimulationLodConfig& config,
        std::span<const world::Cell> observers,int32_t cellSizeCm,
        uint32_t tick,const RegionBudgetState& state,size_t maxVisitsPerTick,
        Critical critical){
    return prepareRegionReports(ecs,config,observers,cellSizeCm,tick,state,
        RegionBudgetOptions{maxVisitsPerTick,{}},critical);
}
inline std::optional<RegionReportBatch> prepareRegionReports(
        const World& ecs,const world::SimulationLodConfig& config,
        std::span<const world::Cell> observers,int32_t cellSizeCm,
        uint32_t tick,const RegionBudgetState& state,RegionBudgetOptions options){
    return prepareRegionReports(ecs,config,observers,cellSizeCm,tick,state,options,
        [](Entity){return false;});
}
inline std::optional<RegionReportBatch> prepareRegionReports(
        const World& ecs,const world::SimulationLodConfig& config,
        std::span<const world::Cell> observers,int32_t cellSizeCm,
        uint32_t tick,const RegionBudgetState& state,size_t maxVisitsPerTick){
    return prepareRegionReports(ecs,config,observers,cellSizeCm,tick,state,
        maxVisitsPerTick,[](Entity){return false;});
}
} // namespace aiverse::evolve
