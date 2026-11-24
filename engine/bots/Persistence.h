#pragma once
#include "engine/bots/Fps.h"
#include "engine/core/NativeBytes.h"
#include <string>
namespace aiverse::lib::bots {
inline void io(Archive& a,ActorState& s){a(s.slot,s.target,s.life,s.targetLife,s.lastTick,s.lastSensor,s.acquired,s.lastSeen,s.sequence,s.initialized,s.lastKnown,s.patrol,s.decision);}
inline std::string saveFps(ActorState state){
    if(!valid(state))return {};
    std::string bytes;
    Archive a(&bytes);
    uint32_t version=1;
    a(version,state);
    return bytes;
}
inline bool loadFps(const std::string& bytes,ActorState& state){
    if(bytes.size()>256)return false;
    Archive a(bytes);
    uint32_t version=0;
    ActorState copy;
    a(version,copy);
    if(!a.ok()||a.remaining()||version!=1||!valid(copy)||saveFps(copy)!=bytes)return false;
    state=copy;
    return true;
}
using runtime::storeBytes;
using runtime::loadBytes;
}
