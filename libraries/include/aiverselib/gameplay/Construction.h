#pragma once
#include "Diagnostics.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace aiverse::lib::gameplay {
struct Cell { std::int64_t x=0,z=0; };
struct Placement { std::int64_t x=0,z=0,width=1,height=1,rotation=0,owner=0,maxSlope=0; bool requireTerritory=false; };
struct CellInfo { bool passable=true; std::int64_t owner=0,slope=0,blocker=0; };
struct Cost { std::int64_t resource=0,amount=0; };
template<class Inspect>
Decision checkPlacement(const Placement& p,Inspect inspect) {
    if(p.width<1||p.height<1||p.width>256||p.height>256||p.rotation<0||p.rotation>3||p.maxSlope<0) return {Reason::Invalid};
    auto w=p.rotation%2?p.height:p.width, h=p.rotation%2?p.width:p.height;
    if(p.x>std::numeric_limits<std::int64_t>::max()-w||p.z>std::numeric_limits<std::int64_t>::max()-h) return {Reason::Invalid};
    for(std::int64_t z=0;z<h;++z) for(std::int64_t x=0;x<w;++x) {
        auto c=inspect(Cell{p.x+x,p.z+z});
        if(!c.passable) return {Reason::Blocked,c.blocker};
        if(p.requireTerritory&&c.owner!=p.owner) return {Reason::Territory,c.owner,p.owner,c.owner};
        if(c.slope<0||c.slope>p.maxSlope) return {Reason::Slope,0,c.slope,p.maxSlope};
    }
    return {};
}
template<class Available>
Decision checkCosts(std::span<const Cost> costs,Available available) {
    std::vector<Cost> sorted(costs.begin(),costs.end());std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a.resource<b.resource;});
    for(std::size_t i=0;i<sorted.size();) {
        const auto id=sorted[i].resource;std::int64_t sum=0;
        while(i<sorted.size()&&sorted[i].resource==id) { auto amount=sorted[i++].amount;if(amount<0||amount>std::numeric_limits<std::int64_t>::max()-sum)return {Reason::Invalid,id};sum+=amount; }
        auto have=available(id);if(have<sum)return {Reason::Resources,id,sum,have};
    }
    return {};
}
inline std::int64_t refund(std::int64_t paid,std::int64_t perMille) {
    if(paid<0||perMille<0||perMille>1000)return 0;
    return (paid/1000)*perMille+(paid%1000)*perMille/1000;
}
}
