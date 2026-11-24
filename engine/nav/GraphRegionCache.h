#pragma once
// AIVERSE: reusable bounded 3D/2D graph-region cache for any game project.
// Persistent derived graph adjacency, incremental changed-node refresh,
// incremental HPA portal refresh and goal-keyed reverse FlowField cache.
// Algorithms remain shared in GraphSearch.h; no cave-specific duplicate.
#include "engine/nav/GraphSearch.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
namespace aiverse::nav {
class GraphRegionCache {
    struct RegionGraph {
        std::vector<std::vector<GraphEdge>> edges;
        size_t size()const{return edges.size();}
        bool passable(uint32_t n)const{return n<edges.size();}
        void neighbors(uint32_t n,std::vector<GraphEdge>& out)const {
            out=edges.at(n);
        }
    };
public:
    struct Stats {
        uint64_t changedNodes=0,portalRebuilds=0,flowBuilds=0,flowHits=0;
        size_t nodes=0,regions=0,flows=0;
    };
    explicit GraphRegionCache(size_t maxNodes=65536,size_t maxFlows=8)
        :maxNodes_(maxNodes),maxFlows_(maxFlows){
        if(!maxNodes_||maxNodes_>1048576||!maxFlows_||maxFlows_>64)
            throw std::invalid_argument("AIVERSE graph region cache capacity");
    }
    template<class Source,class RegionForNode>
    void build(const Source& source,RegionForNode group){
        if(!source.size()||source.size()>maxNodes_)
            throw std::length_error("AIVERSE graph region size");
        std::vector<uint32_t> assigned(source.size());
        std::map<uint64_t,uint32_t> ids;
        for(uint32_t i=0;i<source.size();++i){
            const uint64_t key=group(i);
            auto [it,inserted]=ids.try_emplace(key,uint32_t(ids.size()));
            assigned[i]=it->second;
        }
        std::vector<std::vector<uint32_t>> regionNodes(ids.size());
        std::vector<uint8_t> pass(source.size());
        std::vector<std::vector<GraphEdge>> edges(source.size());
        for(uint32_t i=0;i<source.size();++i){
            regionNodes[assigned[i]].push_back(i);
            pass[i]=source.passable(i);
            if(pass[i])source.neighbors(i,edges[i]);
            validateEdges(edges[i],source.size());
        }
        owner_=std::move(assigned);
        regionNodes_=std::move(regionNodes);
        passable_=std::move(pass);
        adjacency_=std::move(edges);
        portal_.edges.resize(regionNodes_.size());
        for(uint32_t c=0;c<regionNodes_.size();++c)
            refreshPortal(c);
        flows_.clear();serial_=0;
        stats_={};
        stats_.nodes=adjacency_.size();
        stats_.regions=regionNodes_.size();
    }
    size_t size()const noexcept{return adjacency_.size();}
    bool passable(uint32_t n)const {
        return n<passable_.size()&&passable_[n]!=0;
    }
    void neighbors(uint32_t n,std::vector<GraphEdge>& out)const{
        out=adjacency_.at(n);
    }
    // changedNodes MUST include incoming-edge sources (typically a 1-cell
    // halo around modified SDF cells). Caller supplies deterministic IDs.
    // Strong exception guarantee for per-node validation: validate all new
    // edges before replacing any previously published graph state.
    template<class Source>
    void refresh(const Source& source,std::vector<uint32_t> changedNodes){
        if(source.size()!=size())
            throw std::invalid_argument("AIVERSE graph topology size changed");
        std::sort(changedNodes.begin(),changedNodes.end());
        changedNodes.erase(std::unique(changedNodes.begin(),
                                      changedNodes.end()),changedNodes.end());
        std::vector<std::pair<uint32_t,std::vector<GraphEdge>>> prepared;
        std::vector<uint8_t> statuses;
        std::set<uint32_t> affected;
        for(uint32_t id:changedNodes) {
            if(id>=size())throw std::out_of_range("AIVERSE graph dirty node");
            const bool ok=source.passable(id);
            std::vector<GraphEdge> next;
            if(ok)source.neighbors(id,next);
            validateEdges(next,size());
            prepared.push_back({id,std::move(next)});
            statuses.push_back(ok);
            affected.insert(owner_[id]);
        }
        for(size_t i=0;i<prepared.size();++i){
            const auto id=prepared[i].first;
            adjacency_[id]=std::move(prepared[i].second);
            passable_[id]=statuses[i];
        }
        for(uint32_t region:affected)refreshPortal(region);
        stats_.changedNodes+=changedNodes.size();
        flows_.clear();
    }
    GraphRoute astar(uint32_t start,uint32_t goal,uint32_t maxVisits)const {
        return findGraphDijkstra(*this,start,goal,maxVisits);
    }
    GraphRoute hpa(uint32_t start,uint32_t goal,uint32_t maxVisits)const {
        if(start>=size()||goal>=size()||
           !passable(start)||!passable(goal))return {};
        if(owner_[start]==owner_[goal])
            return astar(start,goal,maxVisits);
        const auto abstract=findGraphDijkstra(
            portal_,owner_[start],owner_[goal],maxVisits);
        if(!abstract.reached)return astar(start,goal,maxVisits);
        std::vector<uint8_t> allowed(portal_.size());
        for(uint32_t c:abstract.nodes){
            allowed[c]=1;
            for(auto edge:portal_.edges[c])allowed[edge.to]=1;
        }
        struct Corridor {
            const GraphRegionCache& graph;
            const std::vector<uint8_t>& enabled;
            size_t size()const{return graph.size();}
            bool passable(uint32_t n)const{
                return n<graph.size()&&graph.passable(n)&&
                       enabled[graph.owner_[n]];
            }
            void neighbors(uint32_t n,std::vector<GraphEdge>& out)const{
                graph.neighbors(n,out);
                out.erase(std::remove_if(out.begin(),out.end(),
                    [&](GraphEdge e){return !passable(e.to);}),out.end());
            }
        } corridor{*this,allowed};
        auto result=findGraphDijkstra(corridor,start,goal,maxVisits);
        return result.reached?result:astar(start,goal,maxVisits);
    }
    GraphRoute flow(uint32_t start,uint32_t goal,uint32_t maxVisits){
        if(start>=size()||goal>=size()||
           !passable(start)||!passable(goal))return {};
        auto it=flows_.find(goal);
        if(it==flows_.end()){
            if(flows_.size()>=maxFlows_){
                auto victim=flows_.begin();
                for(auto f=flows_.begin();f!=flows_.end();++f)
                    if(f->second.lastUse<victim->second.lastUse)
                        victim=f;
                flows_.erase(victim);
            }
            auto computed=buildGraphFlow(*this,{goal},maxVisits);
            it=flows_.emplace(goal,CachedFlow{std::move(computed),
                                              ++serial_}).first;
            ++stats_.flowBuilds;
        }else{
            it->second.lastUse=++serial_;
            ++stats_.flowHits;
        }
        const auto& field=it->second.field;
        if(field.values[start]==UINT32_MAX)return {};
        GraphRoute result;
        result.nodes.push_back(start);
        result.cost=field.values[start];
        uint32_t node=start;
        for(uint32_t steps=0;steps<maxVisits&&node!=goal;++steps){
            const uint32_t next=field.next[node];
            if(next>=size()||field.values[next]>=field.values[node])
                return {};
            result.nodes.push_back(next);node=next;
        }
        result.reached=node==goal;
        if(!result.reached)result.nodes.clear();
        return result;
    }
    void invalidateFlows(){flows_.clear();}
    Stats stats()const noexcept{
        Stats value=stats_;value.flows=flows_.size();return value;
    }
private:
    struct CachedFlow{GraphFlow field;uint64_t lastUse=0;};
    static void validateEdges(const std::vector<GraphEdge>& edges,size_t n){
        for(const auto& edge:edges)
            if(edge.to>=n||!edge.cost)
                throw std::invalid_argument("AIVERSE cached graph edge");
    }
    void refreshPortal(uint32_t region){
        std::map<uint32_t,uint32_t> targets;
        for(uint32_t node:regionNodes_[region]){
            if(!passable(node))continue;
            for(const auto edge:adjacency_[node]){
                const auto to=owner_[edge.to];
                if(to==region||!passable(edge.to))continue;
                const auto [it,inserted]=targets.try_emplace(to,edge.cost);
                if(!inserted)it->second=std::min(it->second,edge.cost);
            }
        }
        auto& output=portal_.edges[region];
        output.clear();
        for(const auto& [target,cost]:targets)
            output.push_back({target,cost});
        ++stats_.portalRebuilds;
    }
    size_t maxNodes_,maxFlows_;
    std::vector<uint32_t> owner_;
    std::vector<std::vector<uint32_t>> regionNodes_;
    std::vector<uint8_t> passable_;
    std::vector<std::vector<GraphEdge>> adjacency_;
    RegionGraph portal_;
    std::map<uint32_t,CachedFlow> flows_;
    uint64_t serial_=0;
    Stats stats_{};
};
} // namespace aiverse::nav
