// IMPRINT: persistent world-cell traces, scent and residue; no VFX/terrain copy.
// Producers send quantized contact/deposit data into existing ECS cells.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::imprint {
using I=std::int64_t;
inline constexpr I kMaxLevel=1000000,kMaxRate=100000;
inline constexpr std::size_t kMaxCells=8192,kMaxLinks=32768,kMaxSources=8192;
struct Cell {
    I footprint=0,scent=0,residue=0;
    I footprintFade=10,scentFade=10,residueFade=1;
    bool enabled=true;
    std::uint32_t sampledTick=UINT32_MAX;
};
struct Link {
    Entity a=kNullEntity,b=kNullEntity;
    I transfer=0; // maximum concentration-gradient exchange per tick
    bool enabled=true;
};
struct Deposit {
    Entity cell=kNullEntity;
    I footprint=0,scent=0,residue=0;
    bool enabled=true;
};
inline bool inLevel(I v){return v>=0&&v<=kMaxLevel;}
inline bool inRate(I v){return v>=0&&v<=kMaxRate;}
inline bool valid(const Cell& c){
    return inLevel(c.footprint)&&inLevel(c.scent)&&inLevel(c.residue)&&
           inRate(c.footprintFade)&&inRate(c.scentFade)&&inRate(c.residueFade);
}
inline bool valid(const Link& l){return inRate(l.transfer);}
inline bool valid(const Deposit& d){
    return inRate(d.footprint)&&inRate(d.scent)&&inRate(d.residue);
}
inline void io(Archive& a,Cell& v){
    a(v.footprint,v.scent,v.residue,v.footprintFade,v.scentFade,
      v.residueFade,v.enabled,v.sampledTick);
    if(!a.writing()&&!valid(v))a.fail();
}
inline void io(Archive& a,Link& v){
    a(v.a,v.b,v.transfer,v.enabled);
    if(!a.writing()&&!valid(v))a.fail();
}
inline void io(Archive& a,Deposit& v){
    a(v.cell,v.footprint,v.scent,v.residue,v.enabled);
    if(!a.writing()&&!valid(v))a.fail();
}
// Deterministic stage->commit update. Stale generation links are ignored.
// Footprints mostly decay, scent diffuses through authored adjacency;
// the same rules apply to all three independent, nonnegative quantities.
inline bool advance(World& world,std::uint32_t tick){
    const World& read=world;
    if(read.countExisting<Cell>()>kMaxCells||
       read.countExisting<Link>()>kMaxLinks||
       read.countExisting<Deposit>()>kMaxSources)return false;
    std::vector<std::pair<Entity,Cell>> cells;
    read.eachExistingConst<Cell>([&](Entity e,const Cell& c){
        if(read.alive(e))cells.push_back({e,c});
    });
    std::sort(cells.begin(),cells.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    if(cells.empty())return true;
    std::map<std::uint64_t,std::size_t> ids;
    bool all=true,any=false;
    for(std::size_t i=0;i<cells.size();++i){
        const auto& c=cells[i].second;
        if(!valid(c))return false;
        ids.emplace(cells[i].first.packed(),i);
        all &= c.sampledTick==tick;
        any |= c.sampledTick==tick;
    }
    if(all)return true;
    if(any)return false;
    std::vector<std::pair<Entity,Link>> links;
    read.eachExistingConst<Link>([&](Entity e,const Link& l){
        if(read.alive(e)&&l.enabled)links.push_back({e,l});
    });
    std::sort(links.begin(),links.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    std::vector<std::pair<Entity,Deposit>> sources;
    read.eachExistingConst<Deposit>([&](Entity e,const Deposit& d){
        if(read.alive(e)&&d.enabled)sources.push_back({e,d});
    });
    std::sort(sources.begin(),sources.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    for(auto& [entity,c]:cells){
        (void)entity;
        if(!c.enabled)continue;
        if(!lib::worldprocess::erode(c.footprint,c.footprintFade)||
           !lib::worldprocess::erode(c.scent,c.scentFade)||
           !lib::worldprocess::erode(c.residue,c.residueFade))return false;
    }
    for(const auto& [entity,l]:links){
        (void)entity;
        if(!valid(l))return false;
        const auto a=ids.find(l.a.packed()),b=ids.find(l.b.packed());
        if(a==ids.end()||b==ids.end()||a==b)continue;
        auto& left=cells[a->second].second;
        auto& right=cells[b->second].second;
        if(!left.enabled||!right.enabled)continue;
        I* va[3]={&left.footprint,&left.scent,&left.residue};
        I* vb[3]={&right.footprint,&right.scent,&right.residue};
        for(int kind=0;kind<3;++kind){
            I& x=*va[kind],&y=*vb[kind];
            // Inputs are <= 1e6. Bounded exchange conserves each species.
            if(x>y){
                const I move=std::min({(x-y)/2,l.transfer,x,kMaxLevel-y});
                x-=move;y+=move;
            }else if(y>x){
                const I move=std::min({(y-x)/2,l.transfer,y,kMaxLevel-x});
                y-=move;x+=move;
            }
        }
    }
    for(const auto& [entity,d]:sources){
        (void)entity;
        if(!valid(d))return false;
        const auto it=ids.find(d.cell.packed());
        if(it==ids.end()||!cells[it->second].second.enabled)continue;
        auto& v=cells[it->second].second;
        v.footprint=std::min(kMaxLevel,v.footprint+d.footprint);
        v.scent=std::min(kMaxLevel,v.scent+d.scent);
        v.residue=std::min(kMaxLevel,v.residue+d.residue);
    }
    if(!world.prepareExistingWrite<Cell>())return false;
    for(auto& [entity,c]:cells){
        c.sampledTick=tick;
        world.get<Cell>(entity)=c;
    }
    return true;
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Cell>("imprint.cell",1);
    sim.registerPersistentComponent<Link>("imprint.link",1);
    sim.registerPersistentComponent<Deposit>("imprint.deposit",1);
}
inline void install(Simulation& sim,std::string name="imprint.diffusion"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){(void)advance(s.world(),s.tick());});
}
} // namespace aiverse::imprint
