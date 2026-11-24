// Derived, exact undirected connectivity cache. No world or persistent state.
// Vertex identities/order must remain identical; caller resets on remapping.
#pragma once
#include "Graphs.h"
#include <iterator>
namespace aiverse::lib {
struct SupportGraphActivity {
    size_t rebuiltVertices=0,rebuiltEdges=0,components=0;
};
class SupportGraphCache {
    using Edge=std::pair<size_t,size_t>;
    std::vector<Edge> edges_;
    std::vector<size_t> labels_; // canonical minimum vertex per component
    bool ready_=false;
public:
    void reset(){edges_.clear();labels_.clear();ready_=false;}
    // Exact topology comparison, not a hash/revision guess. Root changes
    // alone never rebuild connectivity. Added/removed edges invalidate
    // the complete old components of their endpoints (including splits).
    std::optional<std::vector<bool>> update(size_t n,
        const std::vector<bool>& roots,std::vector<Edge> edges,
        SupportGraphActivity* activity=nullptr){
        if(roots.size()!=n)return {};
        for(auto& e:edges){
            if(e.first>=n||e.second>=n)return {};
            if(e.first>e.second)std::swap(e.first,e.second);
        }
        edges.erase(std::remove_if(edges.begin(),edges.end(),
            [](auto e){return e.first==e.second;}),edges.end());
        std::sort(edges.begin(),edges.end());
        edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
        SupportGraphActivity stats{};
        const bool full=!ready_||labels_.size()!=n;
        std::vector<size_t> labels=full?std::vector<size_t>(n,kUnreachable):labels_;
        std::vector<bool> affected(n,full);
        if(!full&&edges!=edges_){
            std::vector<Edge> changed;
            std::set_symmetric_difference(edges.begin(),edges.end(),edges_.begin(),edges_.end(),
                std::back_inserter(changed));
            std::vector<bool> dirty(n,false);
            for(auto [a,b]:changed){dirty[labels[a]]=true;dirty[labels[b]]=true;}
            for(size_t i=0;i<n;++i)affected[i]=dirty[labels[i]];
        }
        std::vector<size_t> local(n,kUnreachable),global;
        for(size_t i=0;i<n;++i)if(affected[i]){
            local[i]=global.size();global.push_back(i);
        }
        stats.rebuiltVertices=global.size();
        if(!global.empty()){
            Graph graph(global.size());
            for(auto [a,b]:edges){
                if(!affected[a]&&!affected[b])continue;
                // By construction every new edge crossing old components
                // marks both components. Reject an inconsistent cache.
                if(!affected[a]||!affected[b])return {};
                graph[local[a]].push_back(local[b]);++stats.rebuiltEdges;
            }
            const auto components=weakComponents(graph);
            if(!components)return {};
            for(size_t i=0;i<global.size();++i)labels[global[i]]=global[(*components)[i]];
        }
        std::vector<bool> hasRoot(n,false),supported(n,false);
        for(size_t i=0;i<n;++i){
            if(labels[i]>=n)return {};
            if(labels[i]==i)++stats.components;
            if(roots[i])hasRoot[labels[i]]=true;
        }
        for(size_t i=0;i<n;++i)supported[i]=hasRoot[labels[i]];
        edges_=std::move(edges);labels_=std::move(labels);ready_=true;
        if(activity)*activity=stats;
        return supported;
    }
};
} // namespace aiverse::lib
