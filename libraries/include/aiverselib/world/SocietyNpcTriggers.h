// SOCIETY -> deterministic NPC behavior triggers from actual knowledge.
// This is an event/condition contract. Project AI/dialogue/quest systems
// consume triggered actions; SOCIETY never duplicates their state machines.
#pragma once
#include "SocietyEcs.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::society {
inline constexpr std::size_t kMaxTriggers=16384;
enum class TriggerMode:std::uint8_t {
    AllTopics=0,AnyTopic=1,NoTopics=2
};
// A trigger is attached to the action/game object, references an NPC
// Person, and records its own acknowledgement in persistent ECS.
struct NpcTrigger {
    Entity person=kNullEntity;
    Mask topics=0;
    std::uint32_t actionId=0;   // project-defined route/behavior/quest action
    TriggerMode mode=TriggerMode::AllTopics;
    bool enabled=true,once=true;
    bool active=false,fired=false;
    std::uint32_t sampledTick=UINT32_MAX;
    std::uint32_t changedTick=UINT32_MAX;
    std::uint32_t transition=0;
};
inline bool valid(const NpcTrigger& t) {
    return static_cast<std::uint8_t>(t.mode)<=2 && t.actionId!=0 &&
           t.topics!=0;
}
inline void io(Archive& a,NpcTrigger& t){
    a(t.person,t.topics,t.actionId,t.mode,t.enabled,t.once,
      t.active,t.fired,t.sampledTick,t.changedTick,t.transition);
    if(!a.writing()&&!valid(t))a.fail();
}
inline bool evaluate(TriggerMode mode,Mask knowledge,Mask topics){
    switch(mode){
        case TriggerMode::AllTopics:return (knowledge&topics)==topics;
        case TriggerMode::AnyTopic:return (knowledge&topics)!=0;
        case TriggerMode::NoTopics:return (knowledge&topics)==0;
    }
    return false;
}
// Called after society.knowledge has completed the same tick. A newly
// learned topic can drive an explicit behavior transition. A one-shot
// trigger stays fired through save/load; rollback restores original flag.
inline bool updateNpcTriggers(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<NpcTrigger>()>kMaxTriggers)return false;
    std::vector<std::pair<Entity,NpcTrigger>> triggers;
    read.eachExistingConst<NpcTrigger>([&](Entity e,const NpcTrigger& t){
        if(read.alive(e))triggers.push_back({e,t});
    });
    std::sort(triggers.begin(),triggers.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    if(triggers.empty())return true;
    for(auto& [entity,t]:triggers){
        (void)entity;
        if(!valid(t))return false;
        if(t.sampledTick==tick)continue;
        const auto* p=read.tryGetConst<Person>(t.person);
        const bool available=t.enabled && p && p->enabled &&
                             p->sampledTick==tick;
        bool next=available && evaluate(t.mode,p->knowledge,t.topics);
        if(t.once&&t.fired)next=false;
        if(next&&!t.active){
            if(t.transition==UINT32_MAX)return false;
            ++t.transition;
            t.changedTick=tick;
            t.fired=true;
        }
        t.active=next;
        t.sampledTick=tick;
    }
    if(!world.prepareExistingWrite<NpcTrigger>())return false;
    for(const auto& [entity,t]:triggers)world.get<NpcTrigger>(entity)=t;
    return true;
}
inline void registerNpcTriggerPersistence(Simulation& sim){
    sim.registerPersistentComponent<NpcTrigger>("society.npc_trigger",1);
}
inline void installNpcTriggers(Simulation& sim){
    sim.addSystem("society.npc_reactions",[](Simulation& s){
        (void)updateNpcTriggers(s.world(),s.tick());
    });
}
} // namespace aiverse::society
