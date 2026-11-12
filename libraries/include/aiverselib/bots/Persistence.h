#pragma once
#include "Fps.h"
#include "../runtime/NativeBytes.h"
#include <string>
namespace aiverse::lib::bots {
inline void io(Archive& a,FpsState& s){a(s.slot,s.target,s.life,s.targetLife,s.lastTick,s.lastSensor,s.acquired,s.lastSeen,s.sequence,s.initialized,s.lastKnown,s.patrol,s.decision);}
inline std::string saveFps(FpsState state){
    if(!valid(state))return {};
    std::string bytes;
    Archive a(&bytes);
    uint32_t version=1;
    a(version,state);
    return bytes;
}
inline bool loadFps(const std::string& bytes,FpsState& state){
    if(bytes.size()>256)return false;
    Archive a(bytes);
    uint32_t version=0;
    FpsState copy;
    a(version,copy);
    if(!a.ok()||a.remaining()||version!=1||!valid(copy)||saveFps(copy)!=bytes)return false;
    state=copy;
    return true;
}
using runtime::storeBytes;
using runtime::loadBytes;
}
