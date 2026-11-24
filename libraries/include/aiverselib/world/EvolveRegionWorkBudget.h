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
    bool urgent=false; // wake retained until serviced
};
// One state per independent optional workload. Snapshot with owner state
// when decisions affect gameplay; editor-only owners can discard on rollback.
// Never use an array index as a persistent identity.
struct RegionBudgetState {
    std::optional<uint32_t> lastTick;
    uint64_t lastServedId=0;
    std::vector<PendingRegionWork> pending; // sorted unique packed identities
    uint64_t lastUrgentId=0;
    bool preferUrgent=true; // alternating service when both lanes have work
};
struct RegionBudgetBatch {
    RegionBudgetState next;
    std::vector<PendingRegionWork> selected;
    size_t queued=0,removed=0,remaining=0;
    size_t woken=0,ignoredWake=0,urgentSelected=0,urgentRemaining=0;
};
struct RegionBudgetOptions {
    size_t maxVisitsPerTick=0;
    std::span<const Entity> wake{}; // explicit owner requests, copied during prepare
};
// Pure prepare: no callbacks or owner-state mutations. Commit next together
// with the staged outputs only after every selected operation succeeds.
// Each due request coalesces into one pending refresh; never use this queue
// for mandatory events, elapsed integration, or work that must run once per
// original due tick. Strictly increasing tick, one batch per owner per tick.
inline std::optional<RegionBudgetBatch> prepareRegionBudget(
        const RegionWorkPlan& plan,const RegionBudgetState& state,
        RegionBudgetOptions options){
    const auto maxVisitsPerTick=options.maxVisitsPerTick;
    namespace rt=lib::runtime;
    const auto tick=plan.diagnostics.tick;
    if(plan.regions.size()>kMaxRegions||state.pending.size()>kMaxRegions||options.wake.size()>kMaxRegions||
       (state.lastTick&&tick<=*state.lastTick)||
       (!state.lastTick&&(!state.pending.empty()||state.lastServedId||
                         state.lastUrgentId||!state.preferUrgent)))
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
    std::vector<uint64_t> wake;
    wake.reserve(options.wake.size());
    for(auto e:options.wake){
        if(!e.valid())return std::nullopt;
        wake.push_back(e.packed());
    }
    std::sort(wake.begin(),wake.end());
    wake.erase(std::unique(wake.begin(),wake.end()),wake.end());
    RegionBudgetBatch out;
    out.next.lastTick=tick;
    out.next.lastServedId=state.lastServedId;
    out.next.lastUrgentId=state.lastUrgentId;
    out.next.preferUrgent=state.preferUrgent;
    auto& pending=out.next.pending;
    pending.reserve(plan.regions.size());
    // Merge sorted inputs. Drop removed/disabled regions, preserve the
    // oldest due tick and any pending wake across non-due phases.
    size_t old=0,w=0;
    for(const auto& item:plan.regions){
        const auto id=item.region.packed();
        while(old<state.pending.size()&&state.pending[old].region.packed()<id){
            ++old;++out.removed;
        }
        while(w<wake.size()&&wake[w]<id){++w;++out.ignoredWake;}
        const bool forced=w<wake.size()&&wake[w]==id;
        if(forced){++w;++out.woken;}
        if(old<state.pending.size()&&state.pending[old].region==item.region){
            auto retained=state.pending[old++];
            retained.urgent=retained.urgent||forced;
            pending.push_back(retained);
        }else if(item.lod.due||forced){
            pending.push_back({item.region,tick,forced});++out.queued;
        }
    }
    out.removed+=state.pending.size()-old;
    out.ignoredWake+=wake.size()-w;
    for(const auto& item:pending)if(item.urgent)++out.urgentRemaining;
    if(pending.empty()||!maxVisitsPerTick){out.remaining=pending.size();return out;}
    // Both lanes use the existing WorkBudget primitive. Reserve alternating
    // slots when both have work, including a one-visit budget across ticks.
    // A continuing stream of wakes cannot monopolize ordinary pending work.
    std::vector<rt::WorkItem> urgent,normal;
    urgent.reserve(out.urgentRemaining);
    normal.reserve(pending.size()-out.urgentRemaining);
    for(const auto& item:pending)
        (item.urgent?urgent:normal).push_back({item.region.packed(),1,0});
    const size_t limit=std::min(maxVisitsPerTick,pending.size());
    size_t uc=0,nc=0;
    for(size_t i=0;i<limit;++i){
        const bool u=uc<urgent.size(),n=nc<normal.size();
        if(u&&n){
            if(out.next.preferUrgent)++uc;else ++nc;
            out.next.preferUrgent=!out.next.preferUrgent;
        }else if(u)++uc;else if(n)++nc;
    }
    out.selected.reserve(limit);
    std::vector<uint8_t> served(pending.size(),0);
    auto service=[&](std::vector<rt::WorkItem>& lane,size_t count,uint64_t& lastId){
        if(!count)return true;
        const auto next=std::upper_bound(lane.begin(),lane.end(),lastId,
            [](uint64_t id,const rt::WorkItem& item){return id<item.id;});
        rt::WorkCursor cursor{next==lane.end()?0:size_t(next-lane.begin())};
        const auto result=rt::runBudget(lane,cursor,count,[&](uint64_t id,uint64_t){
            const auto i=std::lower_bound(pending.begin(),pending.end(),id,
                [](const PendingRegionWork& item,uint64_t key){return item.region.packed()<key;});
            if(i==pending.end()||i->region.packed()!=id)return false;
            const auto index=size_t(i-pending.begin());
            if(served[index])return false;
            served[index]=1;
            out.selected.push_back(*i);
            lastId=id;
            return true;
        });
        return result.valid&&!result.blocked&&result.operations==count;
    };
    // Urgent outputs are listed first within a batch. Quotas, not output
    // order, enforce fairness. Ordinary cursor is never moved by a wake.
    if(!service(urgent,uc,out.next.lastUrgentId)||
       !service(normal,nc,out.next.lastServedId))return std::nullopt;
    out.urgentSelected=uc;
    out.urgentRemaining-=uc;
    size_t write=0;
    for(size_t i=0;i<pending.size();++i)
        if(!served[i])pending[write++]=pending[i];
    pending.resize(write);
    out.remaining=pending.size();
    return out;
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
