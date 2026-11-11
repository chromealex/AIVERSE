#pragma once
#include "modules/rts/RtsNative.h"
#include "Reservations.h"
#include "Sequence.h"
#include "Loot.h"
#include "Perception.h"
#include <limits>
namespace aiverse::lib::systems {
inline const std::vector<std::int64_t>* record(const game::NativeState& state,const std::string& key){
    auto it=state.arrays.find(key);return it!=state.arrays.end()&&!it->second.empty()&&it->second[0]==1?&it->second:nullptr;
}
inline bool storeReservations(game::NativeState& state,const std::string& key,const ReservationBook& book){
    if(key.empty())return false;std::vector<std::int64_t> out{1};
    for(const auto& [id,r]:book.tickets){if(id<=0||r.owner<=0||!validAmounts(r.amounts)||r.amounts.size()>INT64_MAX)return false;
        out.insert(out.end(),{id,r.owner,static_cast<std::int64_t>(r.amounts.size())});for(auto [item,n]:r.amounts)out.insert(out.end(),{item,n});}
    state.arrays[key]=std::move(out);return true;
}
inline std::optional<ReservationBook> loadReservations(const game::NativeState& state,const std::string& key){
    auto* data=record(state,key);if(!data)return {};ReservationBook out;std::size_t i=1;
    while(i<data->size()){
        if(data->size()-i<3)return {};auto id=(*data)[i++],owner=(*data)[i++],count=(*data)[i++];
        if(id<=0||owner<=0||count<=0||static_cast<std::uint64_t>(count)>(data->size()-i)/2||out.tickets.count(id))return {};
        Reservation r;r.owner=owner;for(std::int64_t j=0;j<count;++j){auto item=(*data)[i++],n=(*data)[i++];if(item<=0||n<=0||!r.amounts.emplace(item,n).second)return {};}
        out.tickets.emplace(id,std::move(r));
    }
    return out;
}
inline bool storeSequence(game::NativeState& state,const std::string& key,const SequenceState& sequence){
    if(key.empty()||sequence.stage>INT64_MAX||sequence.started>INT64_MAX||sequence.lastTick>INT64_MAX||(sequence.phase<SequencePhase::Ready||sequence.phase>SequencePhase::Failed))return false;
    state.arrays[key]={1,static_cast<std::int64_t>(sequence.stage),static_cast<std::int64_t>(sequence.started),static_cast<std::int64_t>(sequence.lastTick),sequence.hasTick?1:0,static_cast<std::int64_t>(sequence.phase)};return true;
}
inline std::optional<SequenceState> loadSequence(const game::NativeState& state,const std::string& key){
    auto* d=record(state,key);if(!d||d->size()!=6)return {};
    for(auto n:*d)if(n<0)return {};
    if((*d)[4]>1||(*d)[5]>static_cast<std::int64_t>(SequencePhase::Failed)||static_cast<std::uint64_t>((*d)[1])>std::numeric_limits<std::size_t>::max())return {};
    return SequenceState{static_cast<std::size_t>((*d)[1]),static_cast<std::uint64_t>((*d)[2]),static_cast<std::uint64_t>((*d)[3]),(*d)[4]!=0,static_cast<SequencePhase>((*d)[5])};
}
inline bool storeMemory(game::NativeState& state,const std::string& key,const PerceptionMemory& memory){
    if(key.empty())return false;std::vector<std::int64_t> out{1};
    for(const auto& [id,o]:memory.known){if(id<=0||id!=o.entity||o.threat<0||o.seenAt>INT64_MAX)return false;out.insert(out.end(),{id,o.x,o.y,o.z,o.threat,static_cast<std::int64_t>(o.seenAt)});}
    state.arrays[key]=std::move(out);return true;
}
inline std::optional<PerceptionMemory> loadMemory(const game::NativeState& state,const std::string& key,std::size_t capacity){
    auto* d=record(state,key);if(!d||(d->size()-1)%6||(d->size()-1)/6>capacity)return {};
    PerceptionMemory out;for(std::size_t i=1;i<d->size();i+=6){
        Observation o{(*d)[i],(*d)[i+1],(*d)[i+2],(*d)[i+3],(*d)[i+4],static_cast<std::uint64_t>((*d)[i+5])};
        if(o.entity<=0||o.threat<0||(*d)[i+5]<0||!out.known.emplace(o.entity,o).second)return {};
    }
    return out;
}
inline bool storeLoot(game::NativeState& state,const std::string& key,LootState loot){if(key.empty()||loot.misses<0)return false;state.arrays[key]={1,loot.misses};return true;}
inline std::optional<LootState> loadLoot(const game::NativeState& state,const std::string& key){auto* d=record(state,key);if(!d||d->size()!=2||(*d)[1]<0)return {};return LootState{(*d)[1]};}
}
