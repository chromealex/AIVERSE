#pragma once
#include "engine/sim/Command.h"
#include "modules/rts/RtsModule.h"
#include <algorithm>
#include <span>
namespace aiverse::lib::gameplay {
// Pure command construction. The caller supplies network tick/player/sequence and submits once.
inline Command nativeCommand(uint16_t type,uint32_t tick,uint8_t player,uint32_t sequence,
                             std::span<const Entity> entities,int64_t parameter=0,Vec3 point={},Entity target=kNullEntity,bool queue=false) {
    Command c;c.type=type;c.tick=tick;c.player=player;c.seq=sequence;c.param=parameter;c.point=point;c.target=target;
    if(queue)c.flags|=kCmdQueue;
    c.units.assign(entities.begin(),entities.end());
    std::sort(c.units.begin(),c.units.end(),[](Entity a,Entity b){return a.index!=b.index?a.index<b.index:a.packed()<b.packed();});
    c.units.erase(std::unique(c.units.begin(),c.units.end()),c.units.end());return c;
}
inline Command buildCommand(uint32_t tick,uint8_t player,uint32_t sequence,std::span<const Entity> builders,int64_t type,Vec3 point,bool queue=false){return nativeCommand(game::kCmdBuild,tick,player,sequence,builders,type,point,kNullEntity,queue);}
inline Command abilityCommand(uint32_t tick,uint8_t player,uint32_t sequence,std::span<const Entity> casters,int64_t ability,Vec3 point,Entity target=kNullEntity,bool queue=false){return nativeCommand(game::kCmdAbility,tick,player,sequence,casters,ability,point,target,queue);}
inline Command formationCommand(uint32_t tick,uint8_t player,uint32_t sequence,std::span<const Entity> units,int64_t shape){return nativeCommand(game::kCmdFormation,tick,player,sequence,units,shape);}
inline Command cancelConstructionCommand(uint32_t tick,uint8_t player,uint32_t sequence,std::span<const Entity> structures){return nativeCommand(game::kCmdCancelTrain,tick,player,sequence,structures,-1);}
inline Command upgradeCommand(uint32_t tick,uint8_t player,uint32_t sequence,std::span<const Entity> structures,int64_t type){return nativeCommand(game::kCmdUpgrade,tick,player,sequence,structures,type);}
inline Command sellCommand(uint32_t tick,uint8_t player,uint32_t sequence,std::span<const Entity> structures){return nativeCommand(game::kCmdSell,tick,player,sequence,structures);}
}
