// GRANULAR: deterministic bounded cellular solid settling and deposition.
// Owns quantized grains only. TerraWeave owns terrain; Box3D owns rigid bodies.
#pragma once
#include "engine/sim/Simulation.h"
#include "WorldProcesses.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::granular {
using I=std::int64_t;
inline constexpr std::size_t kMaxFields=16,kMaxSources=4096;
inline constexpr std::uint16_t kMaxDimension=128;
inline constexpr I kMaxCellMass=1000000,kMaxSourceRate=100000;
struct Field {
    std::uint16_t width=1,height=1;
    I capacity=1000;
    std::vector<I> mass{0};
    std::vector<std::uint8_t> solid{0}; // 1=blocking boundary
    bool enabled=true;
    std::uint32_t sampledTick=UINT32_MAX;
};
struct Source {
    Entity field=kNullEntity;
    std::uint16_t x=0,y=0;
    I delta=0; // +deposition, -excavation; owning game decides the source
    bool enabled=true;
};
namespace settle_detail {
using RowMask=std::array<std::uint64_t,2>;
using Occupancy=std::array<RowMask,kMaxDimension>;
static_assert(kMaxDimension<=128);
inline void set(RowMask& row,std::size_t x){
    row[x/64]|=std::uint64_t(1)<<(x%64);
}
inline bool empty(const RowMask& row){return !(row[0]|row[1]);}
// Include vertical and both diagonal destinations, even across the two
// 64-bit words. Source occupancy masks out padding beyond the grid width.
inline RowMask candidates(const RowMask& occupied,const RowMask& free){
    return {
        occupied[0]&(free[0]|(free[0]<<1)|(free[0]>>1)|(free[1]<<63)),
        occupied[1]&(free[1]|(free[1]<<1)|(free[1]>>1)|(free[0]>>63))
    };
}
inline bool validateField(const Field& f,Occupancy* occupied=nullptr){
    if(f.width==0||f.height==0||
       f.width>kMaxDimension||f.height>kMaxDimension||
       f.capacity<1||f.capacity>kMaxCellMass)return false;
    const auto size=std::size_t(f.width)*f.height;
    if(f.mass.size()!=size||f.solid.size()!=size)return false;
    for(std::size_t y=0;y<f.height;++y)
        for(std::size_t x=0;x<f.width;++x){
            const auto i=y*f.width+x;
            if(f.mass[i]<0||f.mass[i]>f.capacity||
               f.solid[i]>1||(f.solid[i]&&f.mass[i]!=0))return false;
            if(occupied&&f.mass[i])set((*occupied)[y],x);
        }
    return true;
}
} // namespace settle_detail
inline bool valid(const Field& f){return settle_detail::validateField(f);}
inline bool valid(const Source& s){
    return s.delta>=-kMaxSourceRate&&s.delta<=kMaxSourceRate;
}
inline void io(Archive& a,Field& f){
    a(f.width,f.height,f.capacity,f.mass,f.solid,f.enabled,f.sampledTick);
    if(!a.writing()&&!valid(f))a.fail();
}
inline void io(Archive& a,Source& s){
    a(s.field,s.x,s.y,s.delta,s.enabled);
    if(!a.writing()&&!valid(s))a.fail();
}
// Derived activity for this invocation only; never serialized or used as a
// reason to skip future ticks. Quiet fields still receive their tick stamp.
struct SettlementActivity {
    std::size_t sourceRows=0,activeRows=0,skippedRows=0;
    std::size_t candidateCells=0,transfers=0;
    I movedMass=0;
};
// Moves mass down at most one row per tick, preserving the original
// bottom-to-top and left-to-right order, including parity-based diagonals.
inline bool settle(Field& f,std::uint32_t tick,SettlementActivity* activity){
    settle_detail::Occupancy occupied{};
    if(!settle_detail::validateField(f,&occupied))return false;
    SettlementActivity stats{};
    const auto finish=[&](){
        f.sampledTick=tick;
        if(activity)*activity=stats;
        return true;
    };
    if(!f.enabled||f.height<2)return finish();
    const std::size_t w=f.width,h=f.height;
    auto& m=f.mass;
    const auto& solid=f.solid;
    stats.sourceRows=h-1;
    for(std::size_t y=h-1;y>0;--y){
        // The source row has not yet received grains from above. Its
        // start-of-tick occupancy is exact. The destination row may have
        // just moved downward, so derive free cells from its live state.
        if(settle_detail::empty(occupied[y-1])){++stats.skippedRows;continue;}
        settle_detail::RowMask free{};
        const auto base=y*w;
        for(std::size_t x=0;x<w;++x)
            if(!solid[base+x]&&m[base+x]<f.capacity)settle_detail::set(free,x);
        auto mask=settle_detail::candidates(occupied[y-1],free);
        if(settle_detail::empty(mask)){++stats.skippedRows;continue;}
        ++stats.activeRows;
        // Capacity can only decrease during this row's transfers. A
        // candidate may become blocked by an earlier source, but an
        // omitted source cannot become active until a later tick.
        for(std::size_t word=0;word<mask.size();++word){
            auto bits=mask[word];
            while(bits){
                const std::size_t x=word*64+std::countr_zero(bits);
                bits&=bits-1;
                const auto src=(y-1)*w+x;
                ++stats.candidateCells;
                const auto transfer=[&](std::size_t xx){
                    const auto dst=base+xx;
                    if(solid[dst])return;
                    const I amount=std::min(m[src],f.capacity-m[dst]);
                    if(!amount)return;
                    m[src]-=amount;m[dst]+=amount;
                    ++stats.transfers;stats.movedMass+=amount;
                };
                transfer(x);
                if(m[src]){
                    const bool left=((tick+std::uint32_t(y)+std::uint32_t(x))&1)==0;
                    if(left){
                        if(x>0)transfer(x-1);
                        if(x+1<w)transfer(x+1);
                    }else{
                        if(x+1<w)transfer(x+1);
                        if(x>0)transfer(x-1);
                    }
                }
            }
        }
    }
    return finish();
}
inline bool settle(Field& f,std::uint32_t tick){return settle(f,tick,nullptr);}
// A custom field settler receives staged data and a const authoritative
// World; it must not publish external side effects or mutate ECS.
template<class Settler>
inline bool advanceWithSettler(World& world,std::uint32_t tick,const Settler& settleField){
    const World& read=world;
    if(read.countExisting<Field>()>kMaxFields||
       read.countExisting<Source>()>kMaxSources)return false;
    std::vector<std::pair<Entity,Field>> fields;
    read.eachExistingConst<Field>([&](Entity e,const Field& f){
        if(read.alive(e))fields.push_back({e,f});
    });
    std::sort(fields.begin(),fields.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    if(fields.empty())return true;
    std::map<std::uint64_t,std::size_t> ids;
    bool all=true,any=false;
    for(std::size_t i=0;i<fields.size();++i){
        if(!valid(fields[i].second))return false;
        ids.emplace(fields[i].first.packed(),i);
        all &= fields[i].second.sampledTick==tick;
        any |= fields[i].second.sampledTick==tick;
    }
    if(all)return true;
    if(any)return false;
    std::vector<std::pair<Entity,Source>> sources;
    read.eachExistingConst<Source>([&](Entity e,const Source& s){
        if(read.alive(e)&&s.enabled)sources.push_back({e,s});
    });
    std::sort(sources.begin(),sources.end(),
              [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    for(const auto& [entity,s]:sources){
        (void)entity;
        if(!valid(s))return false;
        const auto it=ids.find(s.field.packed());
        if(it==ids.end())continue; // stale generation never rebinds
        auto& field=fields[it->second].second;
        if(!field.enabled||s.x>=field.width||s.y>=field.height)return false;
        const std::size_t offset=std::size_t(s.y)*field.width+s.x;
        if(field.solid[offset])return false;
        const I amount=field.mass[offset];
        if(s.delta<0 && amount< -s.delta)return false;
        if(s.delta>0 && amount>field.capacity-s.delta)return false;
        field.mass[offset]=amount+s.delta;
    }
    for(auto& [entity,field]:fields){
        if(!settleField(read,entity,field,tick))return false;
    }
    if(!world.prepareExistingWrite<Field>())return false;
    for(const auto& [entity,field]:fields)world.get<Field>(entity)=field;
    return true;
}
inline bool advance(World& world,std::uint32_t tick){
    return advanceWithSettler(world,tick,
        [](const World&,Entity,Field& field,std::uint32_t currentTick){
            return settle(field,currentTick);
        });
}
inline void registerPersistence(Simulation& sim){
    sim.registerPersistentComponent<Field>("granular.field",1);
    sim.registerPersistentComponent<Source>("granular.source",1);
}
inline void install(Simulation& sim,std::string name="granular.settle"){
    registerPersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){(void)advance(s.world(),s.tick());});
}
} // namespace aiverse::granular
