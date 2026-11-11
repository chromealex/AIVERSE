#pragma once
#include "Diagnostics.h"
#include "Construction.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::gameplay {
struct Member { std::int64_t id=0,role=0,leaderPriority=0;bool alive=true; };
inline std::optional<std::int64_t> selectLeader(std::span<const Member> members) {
    const Member* best=nullptr;for(const auto& m:members)if(m.alive&&(!best||m.leaderPriority>best->leaderPriority||(m.leaderPriority==best->leaderPriority&&m.id<best->id)))best=&m;
    return best?std::optional(best->id):std::nullopt;
}
enum class Formation { Line,Box,Wedge,Loose };
struct AssignedSlot { std::int64_t entity=0;Cell offset; };
inline std::optional<std::vector<AssignedSlot>> formationSlots(std::span<const Member> members,Formation shape,std::int64_t spacing,std::int64_t columns=4) {
    if(spacing<1||spacing>1000000||columns<1||columns>65536||members.size()>65536)return std::nullopt;
    std::vector<Member> alive;for(auto m:members)if(m.alive)alive.push_back(m);
    std::sort(alive.begin(),alive.end(),[](auto a,auto b){return a.role!=b.role?a.role<b.role:a.id<b.id;});
    std::vector<std::int64_t> ids;for(auto m:alive)ids.push_back(m.id);std::sort(ids.begin(),ids.end());
    if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return std::nullopt;
    std::vector<AssignedSlot> out;
    for(std::size_t i=0;i<alive.size();++i){auto n=static_cast<std::int64_t>(i);Cell p;
        switch(shape){
        case Formation::Line:p={n*spacing,0};break;
        case Formation::Box:p={(n%columns)*spacing,(n/columns)*spacing};break;
        case Formation::Wedge:p={(n%2?1:-1)*((n+1)/2)*spacing,((n+1)/2)*spacing};break;
        case Formation::Loose:p={(n%columns)*spacing*2+(n/columns%2)*spacing,(n/columns)*spacing*2};break;
        default:return std::nullopt;
        }out.push_back({alive[i].id,p});
    }return out;
}
}
