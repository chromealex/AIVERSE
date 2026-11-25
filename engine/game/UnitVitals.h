#pragma once
#include "GameCore.h"

namespace aiverse::game {
// Shared domain setters used by scripts and trusted local tooling.
// Gameplay death processing remains owned by the next simulation tick.
inline bool setUnitHp(GameCore& core,Simulation& sim,Entity e,Fixed value){
    const auto* u=sim.world().tryGetConst<Unit>(e);
    if(!u||size_t(u->type)>=core.rules().types.size())return false;
    const auto maximum=core.stat(sim,e,Stat::MaxHp);
    sim.world().get<Unit>(e).hp=fxClamp(value,Fixed{},maximum);
    return true;
}
inline bool setUnitEnergy(GameCore& core,Simulation& sim,Entity e,Fixed value){
    const auto* u=sim.world().tryGetConst<Unit>(e);
    if(!u||size_t(u->type)>=core.rules().types.size())return false;
    const auto maximum=core.rules().types[u->type].maxEnergy;
    sim.world().get<Unit>(e).energy=fxClamp(value,Fixed{},maximum.raw>0?maximum:value);
    return true;
}
}
