// Owner-scoped advisory region queue. Uses the existing deterministic
// WorkBudget round-robin; no new ECS scheduler or global mutable state.
#pragma once
#include "EvolveSimulationLod.h"
#include "aiverselib/runtime/WorkBudget.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::evolve {
struct PendingRegionWork {
    Entity region=kNullEntity;
    uint32_t firstDueTick=0;
};
// One state per independent optional workload. Snapshot with owner state
// when decisions affect gameplay; editor-only owners can discard on rollback.
// Never use an array index as a persistent identity.
struct RegionBudgetState {
    std::optional<uint32_t> lastTick;
    uint64_t lastServedId=0;
    std::vector<PendingRegionWork> pending; // sorted unique packed identities
};
struct RegionBudgetBatch {
    RegionBudgetState next;
    std::vector<PendingRegionWork> selected;
    size_t queued=0,removed=0,remaining=0;
};
// Pure prepare: no callbacks or owner-state mutations. Commit next together
// with the staged outputs only after every selected operation succeeds.
// Each due request coalesces into one pending refresh; never use this queue
// for mandatory events, elapsed integration, or work that must run once per
// original due tick. Strictly increasing tick, one batch per owner per tick.
inline std::optional<RegionBudgetBatch> prepareRegionBudget(
        const RegionWorkPlan& plan,const RegionBudgetState& state,
        size_t maxVisitsPerTick){
    namespace rt=lib::runtime;
    const auto tick=plan.diagnostics.tick;
    if(plan.regions.size()>kMaxRegions||state.pending.size()>kMaxRegions||
       (state.lastTick&&tick<=*state.lastTick)||
       (!state.lastTick&&(!state.pending.empty()||state.lastServedId)))
        return std::nullopt;
    uint64_t previous=0;
    bool first=true;
    for(const auto& item:plan.regions){
        const auto id=item.region.packed();
        if(!item.region.valid()||(!first&&id<=previous))return std::nullopt;
        first=false;previous=id;
    }
    first=true;
    for(const auto& item:state.pending){
        const auto id=item.region.packed();
        if(!item.region.valid()||(!first&&id<=previous)||
           !state.lastTick||item.firstDueTick>*state.lastTick)return std::nullopt;
        first=false;previous=id;
    }
    RegionBudgetBatch out;
    out.next.lastTick=tick;
    out.next.lastServedId=state.lastServedId;
    auto& pending=out.next.pending;
    pending.reserve(plan.regions.size());
    // Merge sorted inputs. Drop removed/disabled regions, retain old due
    // timestamps even when the current LOD phase is not due.
    size_t old=0;
    for(const auto& item:plan.regions){
        const auto id=item.region.packed();
        while(old<state.pending.size()&&state.pending[old].region.packed()<id){
            ++old;++out.removed;
        }
        if(old<state.pending.size()&&state.pending[old].region==item.region){
            pending.push_back(state.pending[old++]);
        }else if(item.lod.due){
            pending.push_back({item.region,tick});++out.queued;
        }
    }
    out.removed+=state.pending.size()-old;
    if(pending.empty()||!maxVisitsPerTick){out.remaining=pending.size();return out;}
    std::vector<rt::WorkItem> work;
    work.reserve(pending.size());
    for(const auto& item:pending)work.push_back({item.region.packed(),1,0});
    const auto next=std::upper_bound(work.begin(),work.end(),state.lastServedId,
        [](uint64_t id,const rt::WorkItem& item){return id<item.id;});
    rt::WorkCursor cursor{next==work.end()?0:size_t(next-work.begin())};
    out.selected.reserve(std::min(maxVisitsPerTick,pending.size()));
    const auto result=rt::runBudget(work,cursor,std::min(maxVisitsPerTick,pending.size()),
        [&](uint64_t id,uint64_t){
            const auto i=std::lower_bound(pending.begin(),pending.end(),id,
                [](const PendingRegionWork& item,uint64_t key){return item.region.packed()<key;});
            if(i==pending.end()||i->region.packed()!=id)return false;
            out.selected.push_back(*i);
            out.next.lastServedId=id;
            return true;
        });
    if(!result.valid||result.blocked)return std::nullopt;
    size_t write=0;
    for(size_t i=0;i<work.size();++i)
        if(!work[i].completed)pending[write++]=pending[i];
    pending.resize(write);
    out.remaining=pending.size();
    return out;
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
        uint32_t tick,const RegionBudgetState& state,size_t maxVisitsPerTick,
        Critical critical){
    if(ecs.countExisting<Region>()>kMaxRegions)return std::nullopt;
    const auto plan=planRegionWork(ecs,config,observers,cellSizeCm,tick,critical);
    auto scheduling=prepareRegionBudget(plan,state,maxVisitsPerTick);
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
inline std::optional<RegionReportBatch> prepareRegionReports(
        const World& ecs,const world::SimulationLodConfig& config,
        std::span<const world::Cell> observers,int32_t cellSizeCm,
        uint32_t tick,const RegionBudgetState& state,size_t maxVisitsPerTick){
    return prepareRegionReports(ecs,config,observers,cellSizeCm,tick,state,
        maxVisitsPerTick,[](Entity){return false;});
}
} // namespace aiverse::evolve
