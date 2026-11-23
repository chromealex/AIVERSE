#pragma once
// TerraWeave Caverns Stage41: deterministic 3D graph adapter for existing
// A*, HPA*, flow-field and hybrid algorithm owners (or custom callbacks).
// This does NOT embed/copy any search algorithm; the solver controls
// open/closed sets, tie-breaking and goal heuristics.
#include "engine/world/TerraWeaveCavernsNavigationData.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveGraphCell {
    int32_t x=0,y=0,z=0;
    constexpr bool operator==(const CaveGraphCell&)const=default;
};
struct CaveGraphGrid {
    PointCm origin{};
    uint32_t width=1,height=1,depth=1;
    int32_t cellCm=100;
    uint32_t maxCells=65536;
};
struct CaveGraphEdge {
    uint32_t to=0;
    uint32_t cost=0;  // integer cost, 0 forbidden
};
struct CaveGraphPortal {
    CaveGraphCell first{},second{};
    uint32_t firstNode=0,secondNode=0;
    uint32_t cost=0;
};
class CaveNavigationGridView {
public:
    explicit CaveNavigationGridView(
        std::shared_ptr<const CaveNavigationDataProvider> source,
        CaveGraphGrid grid,CaveAgentProfile agent={})
        :source_(std::move(source)),grid_(grid),agent_(agent) {
        if(!source_||!grid_.width||!grid_.height||!grid_.depth||
           grid_.cellCm<10||grid_.cellCm>10000||
           !grid_.maxCells||grid_.maxCells>1048576)
            throw std::invalid_argument("Caverns navigation grid");
        CaveNavigationDataProvider::validateProfile(agent_);
        const uint64_t count=uint64_t(grid_.width)*grid_.height*grid_.depth;
        if(count>grid_.maxCells)
            throw std::length_error("Caverns navigation grid node budget");
        const PointCm end{
            grid_.origin.x+int64_t(grid_.width)*grid_.cellCm,
            grid_.origin.y+int64_t(grid_.height)*grid_.cellCm,
            grid_.origin.z+int64_t(grid_.depth)*grid_.cellCm};
        validatePoint(grid_.origin);
        validatePoint(end);
    }
    uint64_t sourceRevision()const noexcept{return source_->revision();}
    size_t size()const noexcept{
        return size_t(grid_.width)*grid_.height*grid_.depth;
    }
    uint32_t index(CaveGraphCell c)const {
        if(!inGrid(c))throw std::out_of_range("Caverns graph node index");
        return uint32_t((uint64_t(c.z)*grid_.height+uint32_t(c.y))*
                          grid_.width+uint32_t(c.x));
    }
    CaveGraphCell coordinates(uint32_t id)const {
        if(id>=size())throw std::out_of_range("Caverns graph node");
        const auto x=id%grid_.width;
        id/=grid_.width;
        const auto y=id%grid_.height;
        return {int32_t(x),int32_t(y),int32_t(id/grid_.height)};
    }
    PointCm centre(CaveGraphCell c)const {
        if(!inGrid(c))throw std::out_of_range("Caverns grid coordinates");
        const int64_t half=grid_.cellCm/2;
        return {grid_.origin.x+int64_t(c.x)*grid_.cellCm+half,
                grid_.origin.y+int64_t(c.y)*grid_.cellCm+half,
                grid_.origin.z+int64_t(c.z)*grid_.cellCm+half};
    }
    CaveNavigationCell sample(uint32_t node)const {
        return source_->sample(centre(coordinates(node)),agent_);
    }
    bool passable(uint32_t node)const{
        return sample(node).traversable;
    }
    // Deterministic directed neighbors in -X,+X,-Y,+Y,-Z,+Z order.
    // Existing grid solvers can use this as their stepOk/edge callback.
    // Flow fields reverse legal directed links when integrating costs.
    void neighbors(uint32_t node,std::vector<CaveGraphEdge>& output)const{
        output.clear();
        const auto from=coordinates(node);
        const auto start=source_->sample(centre(from),agent_);
        if(!start.traversable)return;
        constexpr std::array<CaveGraphCell,6> directions{{
            {-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}
        }};
        for(const auto d:directions){
            const CaveGraphCell target{from.x+d.x,from.y+d.y,from.z+d.z};
            if(!inGrid(target))continue;
            const auto end=source_->sample(centre(target),agent_);
            if(!end.traversable ||
               !source_->canTraverse(centre(from),centre(target),agent_))
                continue;
            // Integer edge costs are safe for Dijkstra, A* and flows.
            // Multiplying by a destination cost preserves directed semantics.
            const uint64_t cost=uint64_t(10)*end.cost;
            output.push_back({index(target),
                uint32_t(std::min<uint64_t>(cost,UINT32_MAX))});
        }
    }
    // A* / HPA* region entrances and flow-field cluster borders consume the
    // same directed edges, including tunnels crossing +/-Y cell layers.
    std::vector<CaveGraphPortal> portals(uint32_t clusterWidth=16)const {
        if(!clusterWidth||clusterWidth>64)
            throw std::invalid_argument("Caverns cluster dimension");
        std::vector<CaveGraphPortal> result;
        std::vector<CaveGraphEdge> edges;
        for(uint32_t node=0;node<size();++node){
            const auto from=coordinates(node);
            const auto cluster=[clusterWidth](CaveGraphCell c){
                return CaveGraphCell{c.x/int32_t(clusterWidth),
                                     c.y/int32_t(clusterWidth),
                                     c.z/int32_t(clusterWidth)};
            };
            neighbors(node,edges);
            for(const auto edge:edges){
                const auto to=coordinates(edge.to);
                if(cluster(from)==cluster(to))continue;
                result.push_back({from,to,node,edge.to,edge.cost});
            }
        }
        return result;
    }
    const CaveGraphGrid& config()const noexcept{return grid_;}
    const CaveAgentProfile& profile()const noexcept{return agent_;}
    std::shared_ptr<const CaveNavigationDataProvider> source()const{
        return source_;
    }
private:
    bool inGrid(CaveGraphCell c)const noexcept{
        return c.x>=0&&c.y>=0&&c.z>=0&&
               uint32_t(c.x)<grid_.width&&
               uint32_t(c.y)<grid_.height&&
               uint32_t(c.z)<grid_.depth;
    }
    std::shared_ptr<const CaveNavigationDataProvider> source_;
    CaveGraphGrid grid_{};
    CaveAgentProfile agent_{};
};
} // namespace aiverse::terraweave::caverns
