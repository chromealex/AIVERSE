#pragma once
// Stage42: binds Caverns 3D navigation source to shared engine/nav graph
// solvers. All algorithms live in engine/nav, not in the Caverns world.
// Existing MapGrid 2D graph specialization stays intact. NavMesh and
// custom algorithms can be registered separately through CavePathRouter.
#include "engine/world/TerraWeaveCavernsPathRouter.h"
#include "engine/world/TerraWeaveCavernsNavigationGraph.h"
#include "engine/nav/GraphHierarchy.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveGraphSolverSettings {
    int32_t cellCm=100;
    uint32_t maxNodes=65536;
    uint32_t hpaClusterCells=8;
    uint32_t hybridThreshold=4096;
    int32_t regionMarginCells=8;
};
inline CavePathResult solveCaveGraph(
    const CavePathQuery& query,
    std::shared_ptr<const CaveNavigationDataProvider> data,
    CavePathBackend backend,CaveGraphSolverSettings settings={}) {
    CavePathResult result;result.sourceRevision=query.sourceRevision;
    if(!data||data->revision()!=query.sourceRevision||
       !query.maxVisits||!settings.cellCm||settings.cellCm>10000||
       !settings.maxNodes||settings.maxNodes>1048576||
       !settings.hpaClusterCells||settings.hpaClusterCells>64||
       settings.regionMarginCells<0||settings.regionMarginCells>128)
        return result;
    CaveNavigationDataProvider::validateProfile(query.agent);
    const int64_t pitch=settings.cellCm;
    const int64_t margin=pitch*settings.regionMarginCells;
    const auto low=[margin](int64_t a,int64_t b){
        return std::max<int64_t>(-kLimitCm,std::min(a,b)-margin);
    };
    const auto high=[margin](int64_t a,int64_t b){
        return std::min<int64_t>(kLimitCm,std::max(a,b)+margin);
    };
    const PointCm first{low(query.start.x,query.goal.x),
                        low(query.start.y,query.goal.y),
                        low(query.start.z,query.goal.z)};
    const PointCm last{high(query.start.x,query.goal.x),
                       high(query.start.y,query.goal.y),
                       high(query.start.z,query.goal.z)};
    const auto dimension=[pitch](int64_t a,int64_t b)->uint32_t {
        const uint64_t n=uint64_t((b-a)/pitch)+1;
        return uint32_t(std::min<uint64_t>(n,UINT32_MAX));
    };
    CaveGraphGrid cfg;
    cfg.origin=first;
    cfg.width=dimension(first.x,last.x);
    cfg.height=dimension(first.y,last.y);
    cfg.depth=dimension(first.z,last.z);
    cfg.cellCm=settings.cellCm;
    cfg.maxCells=settings.maxNodes;
    const uint64_t count=uint64_t(cfg.width)*cfg.height*cfg.depth;
    if(count==0||count>settings.maxNodes)return result;
    auto source=std::make_shared<CaveNavigationGridView>(data,cfg,query.agent);
    const auto project=[pitch](int64_t x,int64_t origin,uint32_t dimension){
        return int32_t(std::min<int64_t>(
            dimension-1,std::max<int64_t>(0,(x-origin)/pitch)));
    };
    const auto start=source->index({
        project(query.start.x,first.x,cfg.width),
        project(query.start.y,first.y,cfg.height),
        project(query.start.z,first.z,cfg.depth)});
    const auto goal=source->index({
        project(query.goal.x,first.x,cfg.width),
        project(query.goal.y,first.y,cfg.height),
        project(query.goal.z,first.z,cfg.depth)});
    nav::GraphRoute route;
    const auto heuristic=[&](uint32_t a,uint32_t b)->uint32_t{
        const auto x=source->coordinates(a),y=source->coordinates(b);
        const uint64_t length=uint64_t(std::abs(int64_t(x.x)-y.x))+
            uint64_t(std::abs(int64_t(x.y)-y.y))+
            uint64_t(std::abs(int64_t(x.z)-y.z));
        return uint32_t(std::min<uint64_t>(length*10,UINT32_MAX));
    };
    const auto clusterOf=[&](uint32_t node)->uint64_t {
        const auto p=source->coordinates(node);
        const uint64_t k=settings.hpaClusterCells;
        const uint64_t cx=uint64_t(p.x)/k,cy=uint64_t(p.y)/k,
                       cz=uint64_t(p.z)/k;
        const uint64_t nx=(uint64_t(cfg.width)+k-1)/k;
        const uint64_t ny=(uint64_t(cfg.height)+k-1)/k;
        return (cz*ny+cy)*nx+cx;
    };
    switch(backend){
    case CavePathBackend::AStar:
        route=nav::findGraphAStar(*source,start,goal,
                                 query.maxVisits,heuristic);
        break;
    case CavePathBackend::HPAStar:
        route=nav::findGraphHierarchical(*source,start,goal,
                                        query.maxVisits,clusterOf);
        break;
    case CavePathBackend::Hybrid:
        route=source->size()>=settings.hybridThreshold
            ? nav::findGraphHierarchical(*source,start,goal,
                                         query.maxVisits,clusterOf)
            : nav::findGraphAStar(*source,start,goal,
                                  query.maxVisits,heuristic);
        break;
    case CavePathBackend::FlowField: {
        const auto field=nav::buildGraphFlow(*source,{goal},query.maxVisits);
        if(field.values[start]==UINT32_MAX)break;
        route.nodes.push_back(start);
        uint32_t current=start;
        for(uint32_t n=0;n<query.maxVisits&&current!=goal;++n){
            const auto next=field.next[current];
            if(next==UINT32_MAX||next>=source->size()||
               field.values[next]>=field.values[current])break;
            route.nodes.push_back(next);
            current=next;
        }
        if(current==goal){route.reached=true;route.cost=field.values[start];}
        break;
    }
    case CavePathBackend::NavMesh:
    case CavePathBackend::Custom:
        return result; // use the engine native mesh/custom registered solver
    }
    if(!route.reached)return result;
    result.reached=true;
    result.totalCost=route.cost;
    result.waypoints.reserve(route.nodes.size());
    for(auto node:route.nodes)
        result.waypoints.push_back(source->centre(source->coordinates(node)));
    return result;
}
inline void registerCaveSharedGraphSolvers(
    CavePathRouter& router,CaveGraphSolverSettings settings={}) {
    for(const auto& item:std::vector<std::pair<const char*,CavePathBackend>>{
        {"astar",CavePathBackend::AStar},
        {"hpa",CavePathBackend::HPAStar},
        {"flow",CavePathBackend::FlowField},
        {"hybrid",CavePathBackend::Hybrid}
    }) {
        router.registerSolver(item.first,item.second,
            [backend=item.second,settings](
                const CavePathQuery& query,
                std::shared_ptr<const CaveNavigationDataProvider> source) {
                return solveCaveGraph(query,std::move(source),backend,settings);
            });
    }
}
} // namespace aiverse::terraweave::caverns
