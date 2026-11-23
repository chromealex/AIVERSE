#pragma once
#include "../Grid.h"
#include <queue>
#include <tuple>
#include <limits>
#include <algorithm>
namespace aiverse::lib {
struct WatershedSeed {size_t cell;uint32_t label;};
// Seeded priority flood: minimize maximum elevation along a path, then
// hop count, then label. Seed cells remain fixed; blocked cells label zero.
inline std::optional<std::vector<uint32_t>> gridWatershed(size_t w,size_t h,std::span<const int32_t> elevation,std::span<const uint8_t> passable,std::span<const WatershedSeed> seeds,GridConnectivity mode=GridConnectivity::Four){
    if(!validGrid(w,h,elevation.size())||passable.size()!=elevation.size())return {};
    using Key=std::tuple<int64_t,size_t,uint32_t>;
    const Key inf{std::numeric_limits<int64_t>::max(),std::numeric_limits<size_t>::max(),0};
    std::vector<Key> best(elevation.size(),inf);std::vector<uint8_t> fixed(elevation.size());
    using Item=std::pair<Key,size_t>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> q;
    for(auto s:seeds){
        if(s.cell>=elevation.size()||!s.label||!passable[s.cell]||fixed[s.cell])return {};
        fixed[s.cell]=1;best[s.cell]={elevation[s.cell],0,s.label};q.emplace(best[s.cell],s.cell);
    }
    while(!q.empty()){
        auto [key,u]=q.top();q.pop();if(key!=best[u])continue;
        auto [height,hops,label]=key;
        gridNeighbors(w,h,u,mode,[&](size_t v){
            if(!passable[v]||fixed[v])return;
            Key candidate{std::max(height,int64_t(elevation[v])),hops+1,label};
            if(candidate<best[v]){best[v]=candidate;q.emplace(candidate,v);}
        });
    }
    std::vector<uint32_t> out(best.size());for(size_t i=0;i<best.size();++i)out[i]=std::get<2>(best[i]);return out;
}
}
