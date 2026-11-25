// SOCIETY bridge to existing NPC AI/dialogue/quest owners.
// The authoritative ECS World contains knowledge and trigger outcomes;
// this adapter does not create a second reputation or behavior blackboard.
#pragma once
#include "aiverselib/world/SocietyNpcTriggers.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace aiverse::examples::world_processes_native {
struct NpcKnowledgeAction {
    Entity trigger=kNullEntity,person=kNullEntity;
    std::uint32_t actionId=0,transition=0,changedTick=0;
    society::Mask topics=0;
    bool once=true;
};
class SocietyNpcHost {
public:
    using Dispatch=std::function<bool(const NpcKnowledgeAction&)>;
    void setDispatch(Dispatch dispatch){dispatch_=std::move(dispatch);}
    // Call after society.npc_reactions. Acknowledgement ensures once-only
    // action dispatch at this revision; source of truth remains ECS.
    bool reconcile(const World& world,std::uint32_t completedTick,bool force=false){
        std::vector<std::pair<Entity,society::NpcTrigger>> triggers;
        world.eachExistingConst<society::NpcTrigger>(
            [&](Entity e,const society::NpcTrigger& t){
                if(world.alive(e))triggers.push_back({e,t});
            });
        std::sort(triggers.begin(),triggers.end(),
            [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
        std::set<std::uint64_t> live;
        for(const auto& [entity,t]:triggers){
            if(!society::valid(t)||t.sampledTick!=completedTick)return false;
            const auto key=entity.packed();
            live.insert(key);
            if(!t.transition)continue;
            const auto previous=acknowledged_.find(key);
            if(!force&&previous!=acknowledged_.end()&&
               previous->second==t.transition)continue;
            // After rollback force=true may republish to the game's
            // idempotent event owner. Use (full Entity, transition) as ID.
            NpcKnowledgeAction action{
                entity,t.person,t.actionId,t.transition,t.changedTick,
                t.topics,t.once
            };
            if(dispatch_&&!dispatch_(action))return false;
            acknowledged_[key]=t.transition;
        }
        for(auto it=acknowledged_.begin();it!=acknowledged_.end();)
            it=live.contains(it->first)?std::next(it):acknowledged_.erase(it);
        return true;
    }
    void resetAfterRollback(){acknowledged_.clear();}
    std::size_t acknowledgedCount()const{return acknowledged_.size();}
private:
    Dispatch dispatch_;
    std::map<std::uint64_t,std::uint32_t> acknowledged_;
};
} // namespace aiverse::examples::world_processes_native
