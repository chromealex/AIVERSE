// Canonical owner-scoped advisory Entity refresh queue. No global scheduler,
// authority, wall clock or persistence schema. EVOLVE and other consumers
// share the exact same selection implementation and WorkBudget primitive.
#pragma once
#include "engine/sim/Simulation.h"
#include "engine/world/SimulationLod.h"
#include "aiverselib/runtime/WorkBudget.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace aiverse::lib::worldprocess {
struct EntityWorkDecision {
    Entity entity=kNullEntity;
    world::SimulationLodDecision lod{};
};
struct EntityWorkPlan {
    std::vector<EntityWorkDecision> entities;
    world::SimulationLodDiagnostics diagnostics{};
};
struct PendingEntityWork {
    Entity entity=kNullEntity;
    uint32_t firstDueTick=0;
    bool urgent=false;
};
template<class Pending> struct BasicEntityBudgetState {
    std::optional<uint32_t> lastTick;
    uint64_t lastServedId=0;
    std::vector<Pending> pending;
    uint64_t lastUrgentId=0;
    bool preferUrgent=true;
};
template<class Pending> struct BasicEntityBudgetBatch {
    BasicEntityBudgetState<Pending> next;
    std::vector<Pending> selected;
    size_t queued=0,removed=0,remaining=0;
    size_t woken=0,ignoredWake=0,urgentSelected=0,urgentRemaining=0;
};
using EntityBudgetState=BasicEntityBudgetState<PendingEntityWork>;
using EntityBudgetBatch=BasicEntityBudgetBatch<PendingEntityWork>;
struct EntityBudgetOptions {
    size_t maxVisitsPerTick=0;
    std::span<const Entity> wake{};
};
namespace work_detail {
// Key adapter allows the original EVOLVE region-named records to retain
// source compatibility without copying the algorithm or maintaining a mirror.
// Caller owns state; publish next only together with successful outputs.
template<class Pending,class Items,class Key>
inline std::optional<BasicEntityBudgetBatch<Pending>> prepare(
        const Items& items,uint32_t tick,
        const BasicEntityBudgetState<Pending>& state,EntityBudgetOptions options,
        size_t maxItems,Key key){
    using Batch=BasicEntityBudgetBatch<Pending>;
    const auto maxVisitsPerTick=options.maxVisitsPerTick;
    namespace rt=lib::runtime;
    if(items.size()>maxItems||state.pending.size()>maxItems||options.wake.size()>maxItems||
       (state.lastTick&&tick<=*state.lastTick)||
       (!state.lastTick&&(!state.pending.empty()||state.lastServedId||
                         state.lastUrgentId||!state.preferUrgent)))
        return std::nullopt;
    uint64_t previous=0;
    bool first=true;
    for(const auto& item:items){
        const auto id=key(item).packed();
        if(!key(item).valid()||(!first&&id<=previous))return std::nullopt;
        first=false;previous=id;
    }
    first=true;
    for(const auto& item:state.pending){
        const auto id=key(item).packed();
        if(!key(item).valid()||(!first&&id<=previous)||
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
    Batch out;
    out.next.lastTick=tick;
    out.next.lastServedId=state.lastServedId;
    out.next.lastUrgentId=state.lastUrgentId;
    out.next.preferUrgent=state.preferUrgent;
    auto& pending=out.next.pending;
    pending.reserve(items.size());
    // Merge sorted inputs. Drop removed/disabled entities, preserve the
    // oldest due tick and any pending wake across non-due phases.
    size_t old=0,w=0;
    for(const auto& item:items){
        const auto id=key(item).packed();
        while(old<state.pending.size()&&key(state.pending[old]).packed()<id){
            ++old;++out.removed;
        }
        while(w<wake.size()&&wake[w]<id){++w;++out.ignoredWake;}
        const bool forced=w<wake.size()&&wake[w]==id;
        if(forced){++w;++out.woken;}
        if(old<state.pending.size()&&key(state.pending[old])==key(item)){
            auto retained=state.pending[old++];
            retained.urgent=retained.urgent||forced;
            pending.push_back(retained);
        }else if(item.lod.due||forced){
            pending.push_back({key(item),tick,forced});++out.queued;
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
        (item.urgent?urgent:normal).push_back({key(item).packed(),1,0});
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
                [&key](const Pending& item,uint64_t idKey){return key(item).packed()<idKey;});
            if(i==pending.end()||key(*i).packed()!=id)return false;
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
} // namespace work_detail
inline std::optional<EntityBudgetBatch> prepareEntityBudget(
        const EntityWorkPlan& plan,const EntityBudgetState& state,
        EntityBudgetOptions options,size_t maxItems){
    return work_detail::prepare(plan.entities,plan.diagnostics.tick,state,options,
        maxItems,[](const auto& item){return item.entity;});
}
} // namespace aiverse::lib::worldprocess
