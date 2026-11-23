// ECHO: zone-portal acoustic gameplay propagation. Opt-in, integer-authoritative ECS.
// This is NOT an audio renderer: sound playback is a presentation consumer.
#pragma once
#include "engine/sim/Simulation.h"
#include "libraries/include/aiverselib/world/WorldProcesses.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::echo {
using Intensity=std::int64_t;
inline constexpr Intensity kMaxIntensity=1000000;
inline constexpr std::size_t kMaxZones=4096;
inline constexpr std::size_t kMaxPortals=16384;
inline constexpr std::size_t kMaxEmitters=256;

// A zone is a room/region node. No global mutable zone graph or hidden genre state.
struct AcousticZone {bool enabled=true;};
struct AcousticPortal {
    Entity from=kNullEntity,to=kNullEntity;
    Intensity loss=0; // attenuation in integer intensity units
    bool bidirectional=true,enabled=true;
};
struct AcousticEmitter {
    Entity zone=kNullEntity;
    Intensity intensity=0; // 1..kMaxIntensity
    bool enabled=true;
};
struct AcousticReceiver {
    Entity zone=kNullEntity;
    Intensity threshold=1; // 0..kMaxIntensity
    bool enabled=true;
};
struct AcousticHearing {
    Entity source=kNullEntity;       // generation-safe source entity
    Intensity intensity=-1;         // -1 when nothing qualifies
    std::uint32_t sampledTick=0;
    bool audible=false,valid=false; // valid means receiver's zone is active
};

inline void io(Archive& a,AcousticZone& v){a(v.enabled);}
inline void io(Archive& a,AcousticPortal& v){
    a(v.from,v.to,v.loss,v.bidirectional,v.enabled);
    if(!a.writing() && (v.loss<0||v.loss>kMaxIntensity))a.fail();
}
inline void io(Archive& a,AcousticEmitter& v){
    a(v.zone,v.intensity,v.enabled);
    if(!a.writing() && (v.intensity<0||v.intensity>kMaxIntensity))a.fail();
}
inline void io(Archive& a,AcousticReceiver& v){
    a(v.zone,v.threshold,v.enabled);
    if(!a.writing() && (v.threshold<0||v.threshold>kMaxIntensity))a.fail();
}
inline void io(Archive& a,AcousticHearing& v){
    a(v.source,v.intensity,v.sampledTick,v.audible,v.valid);
    if(!a.writing() && (v.intensity< -1||v.intensity>kMaxIntensity))a.fail();
}

// Read: Zone, Portal, Emitter, Receiver. Write: Hearing only.
// Stable entity-ID ordering makes results independent of ECS dense insertion order.
// Invalid or destroyed Entity references are ignored, never rebound by index.
// Returns false on excessive graph size; outputs remain cleared for this tick.
inline bool propagate(World& world,std::uint32_t tick) {
    auto* outputs=world.prepareExistingWrite<AcousticHearing>();
    if(!outputs)return true;
    for(std::size_t i=0;i<outputs->size();++i){
        outputs->dataAt(i)=AcousticHearing{};
        outputs->dataAt(i).sampledTick=tick;
    }
    const World& read=world;
    if(read.countExisting<AcousticZone>()>kMaxZones ||
       read.countExisting<AcousticPortal>()>kMaxPortals ||
       read.countExisting<AcousticEmitter>()>kMaxEmitters)return false;

    std::vector<Entity> zones;
    read.eachExistingConst<AcousticZone>([&](Entity e,const AcousticZone& z){
        if(read.alive(e)&&z.enabled)zones.push_back(e);
    });
    std::sort(zones.begin(),zones.end(),
              [](Entity a,Entity b){return a.packed()<b.packed();});
    std::map<std::uint64_t,std::size_t> indices;
    for(std::size_t i=0;i<zones.size();++i)indices.emplace(zones[i].packed(),i);

    struct DirectedPortal {Entity entity;AcousticPortal data;};
    std::vector<DirectedPortal> portals;
    read.eachExistingConst<AcousticPortal>([&](Entity e,const AcousticPortal& p){
        if(read.alive(e)&&p.enabled&&p.loss>=0&&p.loss<=kMaxIntensity)
            portals.push_back({e,p});
    });
    std::sort(portals.begin(),portals.end(),[](const auto& a,const auto& b){
        return a.entity.packed()<b.entity.packed();
    });
    std::vector<lib::worldprocess::AcousticEdge> edges;
    edges.reserve(portals.size()*2);
    for(const auto& p:portals){
        const auto from=indices.find(p.data.from.packed());
        const auto to=indices.find(p.data.to.packed());
        if(from==indices.end()||to==indices.end())continue;
        edges.push_back({from->second,to->second,p.data.loss});
        if(p.data.bidirectional)
            edges.push_back({to->second,from->second,p.data.loss});
    }

    struct SourceLevels {Entity entity;std::vector<Intensity> levels;};
    std::vector<std::pair<Entity,AcousticEmitter>> emitters;
    read.eachExistingConst<AcousticEmitter>([&](Entity e,const AcousticEmitter& em){
        if(read.alive(e)&&em.enabled&&em.intensity>0&&em.intensity<=kMaxIntensity
           &&indices.contains(em.zone.packed()))emitters.push_back({e,em});
    });
    std::sort(emitters.begin(),emitters.end(),[](const auto& a,const auto& b){
        return a.first.packed()<b.first.packed();
    });
    std::vector<SourceLevels> levels;
    levels.reserve(emitters.size());
    for(const auto& [entity,em]:emitters){
        levels.push_back({entity,lib::worldprocess::audibility(
            zones.size(),indices.at(em.zone.packed()),em.intensity,edges)});
    }

    for(std::size_t i=0;i<outputs->size();++i){
        const Entity receiverEntity=outputs->entityAt(i);
        if(!read.alive(receiverEntity))continue;
        auto* hearing=&outputs->dataAt(i);
        const auto* receiver=read.tryGetConst<AcousticReceiver>(receiverEntity);
        if(!receiver||!receiver->enabled||receiver->threshold<0||
           receiver->threshold>kMaxIntensity)continue;
        const auto zone=indices.find(receiver->zone.packed());
        if(zone==indices.end())continue;
        hearing->valid=true;
        for(const auto& src:levels){
            const Intensity value=src.levels[zone->second];
            if(value<=0||value<receiver->threshold)continue;
            if(!hearing->audible||value>hearing->intensity||
               (value==hearing->intensity&&src.entity.packed()<hearing->source.packed())){
                hearing->audible=true;
                hearing->source=src.entity;
                hearing->intensity=value;
            }
        }
    }
    return true;
}

// Register only when opted in; old projects retain their save manifest.
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<AcousticZone>("echo.zone",1);
    sim.registerPersistentComponent<AcousticPortal>("echo.portal",1);
    sim.registerPersistentComponent<AcousticEmitter>("echo.emitter",1);
    sim.registerPersistentComponent<AcousticReceiver>("echo.receiver",1);
    sim.registerPersistentComponent<AcousticHearing>("echo.hearing",1);
}
// Call during native GameModule::setup after any authoritative sound emitters.
// Never install twice in the same module.
inline void install(Simulation& sim,std::string name="echo.propagation"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){
        (void)propagate(s.world(),s.tick());
    });
}
} // namespace aiverse::echo
