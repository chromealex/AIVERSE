#pragma once
#include <cstddef>
#include "../Graphs.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace aiverse::lib::strategy {
// Abilities are permanent bits. Doors are directed; a return path must be explicit.
struct ZoneDef {uint32_t grants=0;bool checkpoint=false,final=false;};
struct ZoneDoor {uint32_t from=0,to=0,required=0;};
struct ZoneGraph {std::vector<ZoneDef> zones;std::vector<ZoneDoor> doors;};
struct ZoneState {
    uint32_t zone=0,checkpoint=0,abilities=0;
    std::vector<uint32_t> visited;
    uint64_t lastCommand=0;
    bool operator==(const ZoneState&)const=default;
};
inline bool valid(const ZoneGraph& g) {
    if(g.zones.empty()||g.zones.size()>4096||g.doors.size()>16384)return false;
    std::vector<std::pair<uint32_t,uint32_t>> pairs;
    for(const auto& d:g.doors){if(d.from>=g.zones.size()||d.to>=g.zones.size()||d.from==d.to)return false;pairs.emplace_back(d.from,d.to);}
    std::sort(pairs.begin(),pairs.end());
    return std::adjacent_find(pairs.begin(),pairs.end())==pairs.end();
}
inline bool valid(const ZoneState& s) {
    return s.zone<4096&&s.checkpoint<4096&&!s.visited.empty()&&s.visited.size()<=4096&&
        std::is_sorted(s.visited.begin(),s.visited.end())&&s.visited.back()<4096&&
        std::adjacent_find(s.visited.begin(),s.visited.end())==s.visited.end()&&
        std::binary_search(s.visited.begin(),s.visited.end(),s.zone)&&
        std::binary_search(s.visited.begin(),s.visited.end(),s.checkpoint);
}
inline bool compatible(const ZoneGraph& g,const ZoneState& s) {
    if(!valid(g)||!valid(s)||s.visited.back()>=g.zones.size()||!g.zones[s.checkpoint].checkpoint)return false;
    uint32_t grants=0;for(auto id:s.visited)grants|=g.zones[id].grants;
    return (s.abilities&grants)==grants;
}
namespace zone_detail {
inline void enterZone(const ZoneGraph& g,ZoneState& s,uint32_t zone) {
    s.zone=zone;s.abilities|=g.zones[zone].grants;
    auto at=std::lower_bound(s.visited.begin(),s.visited.end(),zone);
    if(at==s.visited.end()||*at!=zone)s.visited.insert(at,zone);
    if(g.zones[zone].checkpoint)s.checkpoint=zone;
}
}
inline std::optional<ZoneState> beginZones(const ZoneGraph& g,uint32_t start,uint32_t abilities=0) {
    if(!valid(g)||start>=g.zones.size()||!g.zones[start].checkpoint)return {};
    ZoneState s;s.checkpoint=start;s.abilities=abilities;zone_detail::enterZone(g,s,start);return s;
}
inline bool moveZone(const ZoneGraph& g,ZoneState& s,uint32_t target,uint64_t sequence) {
    if(!compatible(g,s)||sequence<=s.lastCommand||target>=g.zones.size()||g.zones[s.zone].final)return false;
    auto door=std::find_if(g.doors.begin(),g.doors.end(),[&](const auto& d){return d.from==s.zone&&d.to==target&&(s.abilities&d.required)==d.required;});
    if(door==g.doors.end())return false;
    zone_detail::enterZone(g,s,target);s.lastCommand=sequence;return true;
}
inline bool respawnZone(const ZoneGraph& g,ZoneState& s,uint64_t sequence) {
    if(!compatible(g,s)||sequence<=s.lastCommand||g.zones[s.zone].final)return false;
    s.zone=s.checkpoint;s.lastCommand=sequence;return true;
}
enum class ZoneProofStatus {Safe,Softlock,NoFinal,Invalid,BudgetExceeded};
struct ZoneWitness {uint32_t zone=0,abilities=0,checkpoint=0;bool operator==(const ZoneWitness&)const=default;};
struct ZoneProof {ZoneProofStatus status=ZoneProofStatus::Invalid;std::vector<ZoneWitness> path;size_t explored=0;};
// Exhaustively explores (zone, permanent abilities, checkpoint), not just zone IDs.
// Respawn is optional: enabling it proves safety only when that command is always available.
inline ZoneProof proveZones(const ZoneGraph& g,const ZoneState& start,bool respawn=false,size_t budget=100000) {
    ZoneProof out;if(!compatible(g,start)||!budget||budget>100000)return out;
    using Key=std::tuple<uint32_t,uint32_t,uint32_t>;
    auto key=[](const ZoneWitness& s){return Key{s.zone,s.abilities,s.checkpoint};};
    std::vector<ZoneWitness> states{{start.zone,start.abilities,start.checkpoint}};
    std::vector<size_t> parent{0};Graph reverse(1);std::map<Key,size_t> ids{{key(states[0]),0}};
    std::vector<std::vector<ZoneDoor>> outgoing(g.zones.size());
    for(const auto& door:g.doors)outgoing[door.from].push_back(door);
    size_t cursor=0;
    for(;cursor<states.size();++cursor){
        auto current=states[cursor];if(g.zones[current.zone].final)continue;
        std::vector<uint32_t> targets;
        for(const auto& d:outgoing[current.zone])if((current.abilities&d.required)==d.required)targets.push_back(d.to);
        if(respawn&&current.zone!=current.checkpoint)targets.push_back(current.checkpoint);
        std::sort(targets.begin(),targets.end());targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
        for(auto target:targets){
            ZoneWitness next{target,current.abilities|g.zones[target].grants,g.zones[target].checkpoint?target:current.checkpoint};
            auto found=ids.find(key(next));size_t id;
            if(found==ids.end()){
                if(states.size()==budget){out.status=ZoneProofStatus::BudgetExceeded;out.explored=states.size();return out;}
                id=states.size();ids.emplace(key(next),id);states.push_back(next);parent.push_back(cursor);reverse.emplace_back();
            }else id=found->second;
            reverse[id].push_back(cursor);
        }
    }
    // Reuse catalog BFS with a virtual root connected to every reached final state.
    reverse.emplace_back();for(size_t i=0;i<states.size();++i)if(g.zones[states[i].zone].final)reverse.back().push_back(i);
    out.explored=states.size();if(reverse.back().empty()){out.status=ZoneProofStatus::NoFinal;return out;}
    auto reaches=bfs(reverse,states.size());
    for(size_t i=0;i<states.size();++i)if(reaches->distance[i]==kUnreachable){
        out.status=ZoneProofStatus::Softlock;
        for(size_t at=i;;at=parent[at]){out.path.push_back(states[at]);if(!at)break;}
        std::reverse(out.path.begin(),out.path.end());return out;
    }
    out.status=ZoneProofStatus::Safe;return out;
}
}
