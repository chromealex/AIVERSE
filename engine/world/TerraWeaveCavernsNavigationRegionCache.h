#pragma once
// Caverns Stage44: reusable live regional graph view feeding shared nav cache.
// One region may handle thousands of queries without re-sampling SDF edges;
// an authored or environment dirty event refreshes affected cells and one
// neighbor ring, followed by only affected HPA portal groups.
#include "engine/world/TerraWeaveCavernsNavigationInvalidation.h"
#include "engine/world/TerraWeaveCavernsPathRouter.h"
#include "engine/nav/GraphRegionCache.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
class CaveNavigationRegionCache {
public:
    CaveNavigationRegionCache(
        std::shared_ptr<const CaveNavigationDataProvider> provider,
        CaveGraphGrid config,CaveAgentProfile agent={},
        uint32_t clusterSize=8,size_t maxFlows=8)
        :grid_(config),agent_(agent),clusterSize_(clusterSize),
         graphCache_(config.maxCells,maxFlows) {
        if(!provider||!clusterSize||clusterSize>64)
            throw std::invalid_argument("Caverns cached region source");
        reset(std::move(provider));
    }
    void reset(std::shared_ptr<const CaveNavigationDataProvider> next) {
        auto view=std::make_shared<const CaveNavigationGridView>(
            next,grid_,agent_);
        sourceForCluster_=view;
        graphCache_.build(*view,[this](uint32_t n){
            return clusterOf(n);
        });
        source_=std::move(view);
    }
    // A changed immutable source may alter both edge endpoints. A one-cell
    // halo expands the half-open authored bounds to include incoming edges.
    void refresh(const CaveNavigationChange& change,
                 std::shared_ptr<const CaveNavigationDataProvider> next){
        if(!next)throw std::invalid_argument("Caverns missing refreshed region");
        if(!source_||change.fullRebuild) {
            reset(std::move(next));return;
        }
        const auto view=std::make_shared<const CaveNavigationGridView>(
            next,grid_,agent_);
        const auto range=caveGraphDirtyRange(change.bounds,grid_,1);
        if(!range) {
            source_=std::move(view);
            return;
        }
        std::vector<uint32_t> dirty;
        const uint64_t count=uint64_t(range->max.x-range->min.x+1)*
            uint64_t(range->max.y-range->min.y+1)*
            uint64_t(range->max.z-range->min.z+1);
        if(count>graphCache_.size())throw std::length_error(
            "Caverns dirty region exceeds cache size");
        dirty.reserve(size_t(count));
        for(int32_t z=range->min.z;z<=range->max.z;++z)
        for(int32_t y=range->min.y;y<=range->max.y;++y)
        for(int32_t x=range->min.x;x<=range->max.x;++x)
            dirty.push_back(view->index({x,y,z}));
        graphCache_.refresh(*view,std::move(dirty));
        source_=std::move(view);
    }
    CavePathResult find(CavePathQuery query,CavePathBackend kind,
                        uint32_t hybridThreshold=4096) {
        CavePathResult result;
        result.sourceRevision=query.sourceRevision;
        result.environmentRevision=source_->source()->environmentRevision();
        if(query.sourceRevision!=source_->sourceRevision()||
           query.agent.traversal!=agent_.traversal||
           query.agent.radiusCm!=agent_.radiusCm||
           query.agent.heightCm!=agent_.heightCm||
           query.agent.maxStepCm!=agent_.maxStepCm||
           query.agent.sampleCm!=agent_.sampleCm||
           query.agent.groundProbeCm!=agent_.groundProbeCm||
           query.agent.areaMask!=agent_.areaMask||
           !query.maxVisits)
            return result;
        const auto cell=[this](PointCm p)->CaveGraphCell{
            const auto project=[this](int64_t v,int64_t origin,uint32_t size){
                (void)size;
                return int32_t(floorDiv(v-origin,grid_.cellCm));
            };
            return {project(p.x,grid_.origin.x,grid_.width),
                    project(p.y,grid_.origin.y,grid_.height),
                    project(p.z,grid_.origin.z,grid_.depth)};
        };
        const CaveGraphCell a=cell(query.start),b=cell(query.goal);
        if(a.x<0||a.y<0||a.z<0||b.x<0||b.y<0||b.z<0||
           uint32_t(a.x)>=grid_.width||uint32_t(a.y)>=grid_.height||
           uint32_t(a.z)>=grid_.depth||uint32_t(b.x)>=grid_.width||
           uint32_t(b.y)>=grid_.height||uint32_t(b.z)>=grid_.depth)
            return result;
        const auto start=source_->index(a),goal=source_->index(b);
        nav::GraphRoute route;
        switch(kind) {
        case CavePathBackend::AStar:
            route=graphCache_.astar(start,goal,query.maxVisits);break;
        case CavePathBackend::HPAStar:
            route=graphCache_.hpa(start,goal,query.maxVisits);break;
        case CavePathBackend::FlowField:
            route=graphCache_.flow(start,goal,query.maxVisits);break;
        case CavePathBackend::Hybrid:
            route=graphCache_.size()>=hybridThreshold
                ?graphCache_.hpa(start,goal,query.maxVisits)
                :graphCache_.astar(start,goal,query.maxVisits);
            break;
        default:return result;
        }
        if(!route.reached)return result;
        result.reached=true;result.totalCost=route.cost;
        for(uint32_t n:route.nodes)
            result.waypoints.push_back(
                source_->centre(source_->coordinates(n)));
        return result;
    }
    nav::GraphRegionCache::Stats stats()const noexcept{
        return graphCache_.stats();
    }
    uint64_t revision()const noexcept{return source_->sourceRevision();}
private:
    uint64_t clusterOf(uint32_t node)const{
        const auto p=sourceForCluster_->coordinates(node);
        const uint64_t k=clusterSize_;
        const uint64_t nx=(uint64_t(grid_.width)+k-1)/k;
        const uint64_t ny=(uint64_t(grid_.height)+k-1)/k;
        return ((uint64_t(p.z)/k)*ny+uint64_t(p.y)/k)*nx+
               uint64_t(p.x)/k;
    }
    CaveGraphGrid grid_;
    CaveAgentProfile agent_;
    uint32_t clusterSize_;
    nav::GraphRegionCache graphCache_;
    std::shared_ptr<const CaveNavigationGridView> source_;
    // Assigned during reset before GraphRegionCache::build evaluates group().
    std::shared_ptr<const CaveNavigationGridView> sourceForCluster_;
};
} // namespace aiverse::terraweave::caverns
