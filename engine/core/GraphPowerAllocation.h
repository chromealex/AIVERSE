// Exact component-local finite-budget routing over directed graphs.
// Derived cache only: inputs are compared, not inferred from tick/revision.
#pragma once
#include "GraphSupportCache.h"
#include <map>

namespace aiverse::lib {
struct PowerVertex {
    int64_t supply=0,demand=0;
    bool enabled=true;
    bool operator==(const PowerVertex&)const=default;
};
struct PowerAllocationActivity {
    size_t components=0,recomputedComponents=0,recomputedVertices=0;
    size_t topologyVertices=0,generatorTraversals=0;
};
class GraphPowerAllocation {
    struct Component {
        std::vector<size_t> members;
        std::vector<PowerVertex> vertices;
        Graph directed;
        std::vector<bool> powered;
    };
    SupportGraphCache connectivity_;
    std::map<size_t,Component> previous_;
    size_t count_=0;
public:
    void reset(){connectivity_.reset();previous_.clear();count_=0;}
    // Caller must reset when vertex identity/order changes. Adjacency input
    // order has no semantic significance; normalize to ascending vertex ID.
    std::optional<std::vector<bool>> update(const std::vector<PowerVertex>& vertices,
            Graph directed,PowerAllocationActivity* activity=nullptr){
        const size_t n=vertices.size();
        if(directed.size()!=n||!validGraph(directed))return {};
        for(const auto& v:vertices)if(v.supply<0||v.demand<0)return {};
        if(n!=count_)reset();
        std::vector<std::pair<size_t,size_t>> edges;
        for(size_t u=0;u<n;++u){
            auto& row=directed[u];
            row.erase(std::remove_if(row.begin(),row.end(),[&](size_t v){
                return u==v||!vertices[u].enabled||!vertices[v].enabled;
            }),row.end());
            std::sort(row.begin(),row.end());
            row.erase(std::unique(row.begin(),row.end()),row.end());
            for(auto v:row)edges.emplace_back(u,v);
        }
        SupportGraphActivity topology{};
        if(!connectivity_.update(n,std::vector<bool>(n,false),std::move(edges),&topology))return {};
        const auto& labels=connectivity_.labels();
        PowerAllocationActivity stats{};
        stats.topologyVertices=topology.rebuiltVertices;
        std::map<size_t,Component> next;
        std::vector<size_t> local(n);
        for(size_t i=0;i<n;++i){
            auto& component=next[labels[i]];
            local[i]=component.members.size();
            component.members.push_back(i);
            component.vertices.push_back(vertices[i]);
        }
        stats.components=next.size();
        std::vector<bool> powered(n,false);
        for(auto& [label,component]:next){
            const size_t size=component.members.size();
            component.directed.resize(size);
            for(size_t i=0;i<size;++i)
                for(auto v:directed[component.members[i]])
                    component.directed[i].push_back(local[v]);
            const auto old=previous_.find(label);
            if(old!=previous_.end()&&old->second.members==component.members&&
               old->second.vertices==component.vertices&&old->second.directed==component.directed){
                component.powered=old->second.powered;
            }else{
                ++stats.recomputedComponents;stats.recomputedVertices+=size;
                component.powered.assign(size,false);
                // Same generator-ID and sorted BFS order as the global
                // reference. Distinct weak components cannot share power.
                std::vector<size_t> queue,visited(size,0);
                queue.reserve(size);
                size_t epoch=0;
                for(size_t root=0;root<size;++root){
                    const auto& v=component.vertices[root];
                    if(!v.enabled||v.supply<=0)continue;
                    ++stats.generatorTraversals;++epoch;
                    int64_t budget=v.supply;
                    queue.clear();queue.push_back(root);visited[root]=epoch;
                    for(size_t head=0;head<queue.size();++head){
                        const auto u=queue[head];
                        const auto& node=component.vertices[u];
                        if(!node.enabled)continue;
                        if(!component.powered[u]&&node.demand<=budget&&budget>0){
                            budget-=node.demand;component.powered[u]=true;
                        }
                        for(auto v:component.directed[u])if(visited[v]!=epoch){
                            visited[v]=epoch;queue.push_back(v);
                        }
                    }
                }
            }
            for(size_t i=0;i<size;++i)powered[component.members[i]]=component.powered[i];
        }
        previous_=std::move(next);count_=n;
        if(activity)*activity=stats;
        return powered;
    }
};
} // namespace aiverse::lib
