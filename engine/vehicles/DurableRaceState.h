#pragma once
#include "VehicleRace.h"
#include "engine/mechanics/VehiclePersistence.h"
#include "engine/world/PersistentEntityReferences.h"
#include <set>

namespace aiverse::vehicles {
// Resource codec for the shared race adapter. Racer IDs within the mechanics
// session are ordinal IDs; only RaceState::racers stores runtime Entity handles.
inline bool ioDurableRaceState(Archive& a,RaceState& s,const World& w){
    world::PersistentEntityReferences refs;
    if(const auto* ledger=w.resourceConst<world::RegionEntityLedger>()){
        if(!refs.rebuild(w,*ledger))return false;
    }else if(!refs.rebuildTagged(w))return false;
    a(s.started,s.winnerTeam);
    if(!a.ok()||(a.writing()&&(s.racers.size()>lm::kRaceMaxRacers||
        s.grid.size()>lm::kRaceMaxRacers)))return false;
    uint32_t count=a.count(s.racers.size(),8);
    if(!a.ok()||count>lm::kRaceMaxRacers)return false;
    if(!a.writing())s.racers.resize(count);
    std::set<uint64_t> seen;
    for(auto& car:s.racers){
        if(!world::ioPersistentEntity(a,w,refs,car)||!car.valid()||
            !w.has<VehicleDrive>(car)||!seen.insert(car.packed()).second)return false;
    }
    count=a.count(s.grid.size(),32);
    if(!a.ok()||count>lm::kRaceMaxRacers)return false;
    if(!a.writing())s.grid.resize(count);
    for(auto& slot:s.grid)a(slot);
    lm::io(a,s.session);
    if(!a.ok()||!lm::valid(s.session)||s.grid.size()!=s.racers.size())return false;
    if(s.started&&s.racers.size()!=s.session.race.racers.size())return false;
    if(!s.started&&(!s.racers.empty()||!s.session.race.racers.empty()))return false;
    for(size_t i=0;i<s.racers.size();++i)
        if(!s.session.race.racers.contains(int64_t(i+1)))return false;
    return true;
}
}
