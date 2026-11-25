#pragma once
// Shared engine HPA-style region corridor over any bounded directed graph.
// It uses the SAME engine/nav/GraphSearch.h A* implementation twice:
// one hierarchy search, then a detailed refinement in the selected corridor.
// On sparse/invalid corridors it falls back to the full native graph solver.
// No RTS or Caverns-specific pathfinder implementation.
#include "engine/nav/GraphSearch.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::nav {
struct GraphHierarchyStats {
    uint32_t clusters=0,portalEdges=0;
    bool usedHierarchy=false,fallback=false;
};
template<class Graph,class ClusterOf>
GraphRoute findGraphHierarchical(const Graph& graph,
    uint32_t start,uint32_t goal,uint32_t maxVisits,
    ClusterOf clusterOf,GraphHierarchyStats* stats=nullptr,
    uint32_t maxClusters=65536) {
    if(!maxVisits||!maxClusters||maxClusters>262144||
       start>=graph.size()||goal>=graph.size())return {};
    // Compact unique cluster IDs in input node order so a noncontiguous
    // 3D integer coordinate scheme does not allocate a huge sparse array.
    std::map<uint64_t,uint32_t> clusters;
    std::vector<uint32_t> owner(graph.size(),UINT32_MAX);
    for(uint32_t node=0;node<graph.size();++node){
        if(!graph.passable(node))continue;
        const uint64_t key=clusterOf(node);
        auto it=clusters.find(key);
        if(it==clusters.end()){
            if(clusters.size()>=maxClusters)
                return findGraphDijkstra(graph,start,goal,maxVisits);
            it=clusters.emplace(key,uint32_t(clusters.size())).first;
        }
        owner[node]=it->second;
    }
    if(owner[start]==UINT32_MAX||owner[goal]==UINT32_MAX)return {};
    if(owner[start]==owner[goal])
        return findGraphDijkstra(graph,start,goal,maxVisits);
    struct RegionGraph {
        std::vector<std::vector<GraphEdge>> adjacency;
        size_t size()const{return adjacency.size();}
        bool passable(uint32_t n)const{return n<adjacency.size();}
        void neighbors(uint32_t n,std::vector<GraphEdge>& out)const{
            out=adjacency.at(n);
        }
    };
    RegionGraph regions;
    regions.adjacency.resize(clusters.size());
    std::vector<GraphEdge> edges;
    std::map<std::pair<uint32_t,uint32_t>,uint32_t> portals;
    for(uint32_t node=0;node<graph.size();++node){
        if(owner[node]==UINT32_MAX)continue;
        graph.neighbors(node,edges);
        for(auto e:edges) {
            if(e.to>=graph.size()||!e.cost)
                throw std::invalid_argument("AIVERSE hierarchical graph edge");
            if(owner[e.to]==UINT32_MAX||owner[e.to]==owner[node])continue;
            auto key=std::pair{owner[node],owner[e.to]};
            auto [it,inserted]=portals.try_emplace(key,e.cost);
            if(!inserted)it->second=std::min(it->second,e.cost);
        }
    }
    for(const auto& [key,cost]:portals)
        regions.adjacency[key.first].push_back({key.second,cost});
    for(auto& neighbors:regions.adjacency)
        std::sort(neighbors.begin(),neighbors.end(),
            [](GraphEdge a,GraphEdge b){return a.to<b.to;});
    if(stats){stats->clusters=uint32_t(clusters.size());
              stats->portalEdges=uint32_t(portals.size());}
    auto abstract=findGraphDijkstra(regions,owner[start],owner[goal],
                                    maxVisits);
    if(!abstract.reached) {
        if(stats)stats->fallback=true;
        return findGraphDijkstra(graph,start,goal,maxVisits);
    }
    // Keep the corridor and its directly bordering clusters. This makes
    // refinement robust to narrow passages while bounding the detailed
    // graph walk. Worst-case route is always validated on original edges.
    std::vector<uint8_t> allowed(regions.size(),0);
    for(uint32_t c:abstract.nodes){
        allowed[c]=1;
        for(auto e:regions.adjacency[c])allowed[e.to]=1;
    }
    struct CorridorGraph {
        const Graph& source;
        const std::vector<uint32_t>& ownership;
        const std::vector<uint8_t>& allowed;
        size_t size()const{return source.size();}
        bool passable(uint32_t n)const{
            return n<ownership.size()&&ownership[n]!=UINT32_MAX&&
                   allowed[ownership[n]]&&source.passable(n);
        }
        void neighbors(uint32_t n,std::vector<GraphEdge>& out)const{
            source.neighbors(n,out);
            out.erase(std::remove_if(out.begin(),out.end(),
                [&](GraphEdge e){return !passable(e.to);}),out.end());
        }
    } corridor{graph,owner,allowed};
    auto answer=findGraphDijkstra(corridor,start,goal,maxVisits);
    if(answer.reached){
        if(stats)stats->usedHierarchy=true;
        return answer;
    }
    if(stats)stats->fallback=true;
    return findGraphDijkstra(graph,start,goal,maxVisits);
}
} // namespace aiverse::nav
