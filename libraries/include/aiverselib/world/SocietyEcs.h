// SOCIETY: persistent ECS knowledge, trusted one-hop communication and rumors.
// Does not duplicate dialogue, quests, social AI or relationships owned by games.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::society {
using Mask=std::uint64_t;
inline constexpr std::size_t kMaxPeople=8192,kMaxContacts=32768,kMaxMessages=8192;
struct Person {
    Mask knowledge=0;
    std::uint16_t credibility=1000; // 0..1000
    std::uint32_t acceptance=250000; // trust * credibility threshold, 0..1e6
    bool enabled=true;
    std::uint32_t sampledTick=UINT32_MAX;
};
struct Contact {
    Entity source=kNullEntity,target=kNullEntity;
    std::uint16_t trust=1000; // 0..1000; directed relationship
    bool enabled=true;
};
struct Message {
    Entity source=kNullEntity;
    Mask topics=0;
    bool enabled=true;
};
inline bool valid(const Person& p){
    return p.credibility<=1000&&p.acceptance<=1000000;
}
inline bool valid(const Contact& c){return c.trust<=1000;}
inline void io(Archive& a,Person& p){
    a(p.knowledge,p.credibility,p.acceptance,p.enabled,p.sampledTick);
    if(!a.writing()&&!valid(p))a.fail();
}
inline void io(Archive& a,Contact& c){
    a(c.source,c.target,c.trust,c.enabled);
    if(!a.writing()&&!valid(c))a.fail();
}
inline void io(Archive& a,Message& m){a(m.source,m.topics,m.enabled);}
// One-hop per tick, snapshot-isolated: Alice->Bob->Carol needs two ticks.
// Message sources authorize topics, trusted directed contacts propagate
// all currently known bits; recipients filter via acceptance threshold.
// No identity assumptions, private profile inference or content semantics.
inline bool advance(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Person>()>kMaxPeople||
       read.countExisting<Contact>()>kMaxContacts||
       read.countExisting<Message>()>kMaxMessages)return false;
    std::vector<std::pair<Entity,Person>> people;
    read.eachExistingConst<Person>([&](Entity e,const Person& p){
        if(read.alive(e))people.push_back({e,p});
    });
    std::sort(people.begin(),people.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    if(people.empty())return true;
    std::map<std::uint64_t,std::size_t> ids;
    bool all=true,any=false;
    for(std::size_t i=0;i<people.size();++i){
        const auto& p=people[i].second;
        if(!valid(p))return false;
        ids.emplace(people[i].first.packed(),i);
        all &= p.sampledTick==tick;
        any |= p.sampledTick==tick;
    }
    if(all)return true;
    if(any)return false;
    std::vector<Mask> published(people.size(),0),next(people.size(),0);
    for(std::size_t i=0;i<people.size();++i)
        published[i]=next[i]=people[i].second.knowledge;
    std::vector<std::pair<Entity,Message>> messages;
    read.eachExistingConst<Message>([&](Entity e,const Message& m){
        if(read.alive(e)&&m.enabled)messages.push_back({e,m});
    });
    std::sort(messages.begin(),messages.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    for(const auto& [entity,msg]:messages){
        (void)entity;
        const auto it=ids.find(msg.source.packed());
        if(it==ids.end()||!people[it->second].second.enabled)continue;
        published[it->second] |= msg.topics;
        next[it->second] |= msg.topics;
    }
    std::vector<std::pair<Entity,Contact>> contacts;
    read.eachExistingConst<Contact>([&](Entity e,const Contact& c){
        if(read.alive(e)&&c.enabled)contacts.push_back({e,c});
    });
    std::sort(contacts.begin(),contacts.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    for(const auto& [entity,contact]:contacts){
        (void)entity;
        if(!valid(contact))return false;
        const auto a=ids.find(contact.source.packed());
        const auto b=ids.find(contact.target.packed());
        if(a==ids.end()||b==ids.end()||a==b)continue;
        const auto& source=people[a->second].second;
        const auto& target=people[b->second].second;
        if(!source.enabled||!target.enabled)continue;
        // 1000*1000 fits unsigned 32; trust is the *owner's* authored
        // relationship, not a derived persistent social graph elsewhere.
        if(std::uint32_t(contact.trust)*source.credibility<target.acceptance)continue;
        next[b->second] |= published[a->second];
    }
    if(!world.prepareExistingWrite<Person>())return false;
    for(std::size_t i=0;i<people.size();++i){
        auto& p=people[i].second;
        if(p.enabled)p.knowledge=next[i];
        p.sampledTick=tick;
        world.get<Person>(people[i].first)=p;
    }
    return true;
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Person>("society.person",1);
    sim.registerPersistentComponent<Contact>("society.contact",1);
    sim.registerPersistentComponent<Message>("society.message",1);
}
inline void install(Simulation& sim,std::string name="society.knowledge"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){(void)advance(s.world(),s.tick());});
}
} // namespace aiverse::society
