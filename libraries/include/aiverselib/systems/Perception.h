#pragma once
#include <cstdint>
#include <map>
#include <optional>
namespace aiverse::lib::systems {
struct Observation {std::int64_t entity=0,x=0,y=0,z=0,threat=0;std::uint64_t seenAt=0;};
struct MemoryConfig {std::size_t capacity=128;std::uint64_t lifetime=200;};
struct PerceptionMemory {std::map<std::int64_t,Observation> known;};
// Caller provides only observations allowed by gameplay visibility, never omniscient world queries.
inline bool observe(PerceptionMemory& memory,MemoryConfig config,Observation value){
    if(value.entity<=0||value.threat<0||!config.capacity||memory.known.size()>config.capacity)return false;
    auto found=memory.known.find(value.entity);
    if(found!=memory.known.end()){if(value.seenAt<found->second.seenAt)return false;found->second=value;return true;}
    if(memory.known.size()==config.capacity){
        auto oldest=memory.known.begin();for(auto it=memory.known.begin();it!=memory.known.end();++it)
            if(it->second.seenAt<oldest->second.seenAt)oldest=it;
        if(value.seenAt<oldest->second.seenAt||(value.seenAt==oldest->second.seenAt&&value.entity<oldest->second.entity))return false;
        memory.known.erase(oldest);
    }
    memory.known.emplace(value.entity,value);return true;
}
inline bool expire(PerceptionMemory& memory,MemoryConfig config,std::uint64_t now){
    for(const auto& [id,o]:memory.known){(void)id;if(o.seenAt>now)return false;}
    for(auto it=memory.known.begin();it!=memory.known.end();){if(now-it->second.seenAt>=config.lifetime)it=memory.known.erase(it);else ++it;}return true;
}
inline std::optional<Observation> highestThreat(const PerceptionMemory& memory,std::uint64_t now,MemoryConfig config){
    std::optional<Observation> best;
    for(const auto& [id,o]:memory.known){(void)id;if(o.seenAt>now||now-o.seenAt>=config.lifetime)continue;
        if(!best||o.threat>best->threat||(o.threat==best->threat&&o.entity<best->entity))best=o;}
    return best;
}
}
