// Planetary CROWD reservation planner. Movement, pathing, grounding and
// avoidance remain with the existing authoritative host, never this planner.
#pragma once
#include "CrowdEcs.h"
#include "WorldProcessSurfaceFrame.h"
#include "engine/sim/CommonComponents.h"
#include <optional>

namespace aiverse::crowd {
struct SurfaceSlotIntent {
    Entity agent=kNullEntity,slot=kNullEntity;
    Vec3 target{};
    int body=-1;
};
struct SurfaceSlotPlan {
    std::vector<std::pair<Entity,Agent>> agents;
    std::vector<SurfaceSlotIntent> intents;
};
// Eligibility is owner-specific, pure, and read-only. It may reject a
// non-mobile actor, off-map/transit or unsupported movement layer. Slot
// eligibility can check canonical navigation passability/clearance.
template<class Eligible,class AcceptSlot>
inline std::optional<SurfaceSlotPlan> planSurfaceSlots(const World& read,
        const game::Surface& surface,uint32_t tick,
        const Eligible& eligible,const AcceptSlot& acceptSlot){
    if(read.countExisting<Agent>()>kMaxAgents||
       read.countExisting<Slot>()>kMaxSlots||
       read.countExisting<Preference>()>kMaxPreferences)return {};
    namespace wp=lib::worldprocess;
    SurfaceSlotPlan plan;
    read.eachExistingConst<Agent>([&](Entity e,const Agent& a){
        if(read.alive(e))plan.agents.push_back({e,a});
    });
    const auto byId=[](const auto& a,const auto& b){return a.first.packed()<b.first.packed();};
    std::sort(plan.agents.begin(),plan.agents.end(),byId);
    bool all=true,any=false;
    for(const auto& [e,a]:plan.agents){
        (void)e;if(!valid(a))return {};
        all&=a.sampledTick==tick;any|=a.sampledTick==tick;
    }
    if(all){plan.agents.clear();return plan;}
    if(any)return {};
    struct PositionSlot{Entity e;wp::SurfaceFrame frame;};
    std::vector<std::pair<Entity,Slot>> slots;
    read.eachExistingConst<Slot>([&](Entity e,const Slot& s){
        if(read.alive(e))slots.push_back({e,s});
    });
    std::sort(slots.begin(),slots.end(),byId);
    std::vector<PositionSlot> available;
    std::map<uint64_t,size_t> slotIds;
    for(const auto& [e,s]:slots){
        if(!valid(s))return {};
        const auto* tr=read.tryGetConst<Transform>(e);
        if(!s.enabled||!tr)continue;
        const auto frame=wp::surfaceFrame(&surface,tr->pos);
        if(!frame)continue;
        slotIds.emplace(e.packed(),available.size());
        available.push_back({e,*frame});
    }
    std::map<uint64_t,std::vector<size_t>> preferred;
    std::vector<std::pair<Entity,Preference>> prefs;
    read.eachExistingConst<Preference>([&](Entity e,const Preference& p){
        if(read.alive(e)&&p.enabled&&read.alive(p.agent))prefs.push_back({e,p});
    });
    std::sort(prefs.begin(),prefs.end(),byId);
    for(const auto& [e,p]:prefs){
        (void)e;
        auto& list=preferred[p.agent.packed()];
        for(Entity slot:p.options){
            const auto it=slotIds.find(slot.packed());
            if(it!=slotIds.end()&&std::find(list.begin(),list.end(),it->second)==list.end())
                list.push_back(it->second);
        }
    }
    std::vector<std::vector<size_t>> priorities(plan.agents.size());
    for(size_t i=0;i<plan.agents.size();++i){
        const auto& [e,a]=plan.agents[i];
        const auto* tr=read.tryGetConst<Transform>(e);
        if(!a.enabled||!tr||!eligible(e))continue;
        const auto frame=wp::surfaceFrame(&surface,tr->pos);
        if(!frame)continue;
        if(const auto it=preferred.find(e.packed());it!=preferred.end())
            for(size_t j:it->second){
                const auto& slot=available[j];
                if(wp::sameSurfaceBody(*frame,slot.frame)&&acceptSlot(e,slot.frame))
                    priorities[i].push_back(j);
            }
    }
    const auto assignment=lib::worldprocess::assignSlots(priorities,available.size());
    for(size_t i=0;i<plan.agents.size();++i){
        auto& [e,a]=plan.agents[i];
        a.sampledTick=tick;
        a.allocated=kNullEntity;
        if(assignment[i]<0)continue;
        const auto& slot=available[size_t(assignment[i])];
        a.allocated=slot.e;
        plan.intents.push_back({e,slot.e,slot.frame.origin,slot.frame.body});
    }
    return plan;
}
inline bool commitSurfaceSlots(World& world,const SurfaceSlotPlan& plan){
    if(plan.agents.empty())return true;
    const World& read=world;
    for(const auto& [e,a]:plan.agents)
        if(!read.alive(e)||!read.has<Agent>(e)||!valid(a))return false;
    if(!world.prepareExistingWrite<Agent>())return false;
    for(const auto& [e,a]:plan.agents)world.get<Agent>(e)=a;
    return true;
}
} // namespace aiverse::crowd
