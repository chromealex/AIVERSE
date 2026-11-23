#pragma once
#include "Graphs.h"
namespace aiverse::lib {
struct SignedEdge {size_t from,to;int64_t cost;};
inline constexpr int64_t kSignedInfinity=std::numeric_limits<int64_t>::max();
struct SignedPaths {std::vector<int64_t> distance;std::vector<size_t> parent;bool negativeCycle=false;};
inline bool graphAdd(int64_t a,int64_t b,int64_t& out){
    if((b>0&&a>kSignedInfinity-b)||(b<0&&a<std::numeric_limits<int64_t>::min()-b))return false;
    out=a+b;return out!=kSignedInfinity;
}
inline std::optional<SignedPaths> bellmanFord(size_t n,const std::vector<SignedEdge>& edges,size_t start){
    if(start>=n)return {};
    for(auto e:edges)if(e.from>=n||e.to>=n||e.cost==kSignedInfinity)return {};
    SignedPaths out{std::vector<int64_t>(n,kSignedInfinity),std::vector<size_t>(n,kUnreachable)};
    out.distance[start]=0;
    for(size_t pass=0;pass<n;++pass){
        bool changed=false;
        for(auto e:edges){if(out.distance[e.from]==kSignedInfinity)continue;int64_t d;
            if(!graphAdd(out.distance[e.from],e.cost,d))return {};
            if(d<out.distance[e.to]){out.distance[e.to]=d;out.parent[e.to]=e.from;changed=true;}
        }
        if(!changed)break;if(pass+1==n)out.negativeCycle=true;
    }return out;
}
struct AllPairsPaths {size_t size=0;std::vector<int64_t> distance;bool negativeCycle=false;};
inline std::optional<AllPairsPaths> floydWarshall(size_t n,const std::vector<SignedEdge>& edges){
    if(n>1024)return {};
    AllPairsPaths out{n,std::vector<int64_t>(n*n,kSignedInfinity)};
    auto& d=out.distance;for(size_t i=0;i<n;++i)d[i*n+i]=0;
    for(auto e:edges){if(e.from>=n||e.to>=n||e.cost==kSignedInfinity)return {};auto& v=d[e.from*n+e.to];v=std::min(v,e.cost);}
    for(size_t k=0;k<n;++k)for(size_t i=0;i<n;++i){if(d[i*n+k]==kSignedInfinity)continue;
        for(size_t j=0;j<n;++j){if(d[k*n+j]==kSignedInfinity)continue;int64_t v;if(!graphAdd(d[i*n+k],d[k*n+j],v))return {};d[i*n+j]=std::min(d[i*n+j],v);}
    }
    for(size_t i=0;i<n;++i)if(d[i*n+i]<0)out.negativeCycle=true;
    return out;
}
struct GraphCuts {std::vector<size_t> bridges,articulation;};
// Undirected multigraph: bridge IDs refer to input edges, parallel edges supported.
inline std::optional<GraphCuts> graphCuts(size_t n,const std::vector<std::pair<size_t,size_t>>& edges){
    using Link=std::pair<size_t,size_t>;
    std::vector<std::vector<Link>> g(n);
    for(size_t i=0;i<edges.size();++i){auto [u,v]=edges[i];if(u>=n||v>=n)return {};g[u].emplace_back(v,i);g[v].emplace_back(u,i);}
    std::vector<size_t> tin(n,kUnreachable),low(n),parent(n,kUnreachable),parentEdge(n,kUnreachable),children(n),cursor(n);
    std::vector<bool> cut(n);size_t timer=0;GraphCuts out;
    for(size_t root=0;root<n;++root){
        if(tin[root]!=kUnreachable)continue;
        std::vector<size_t> stack{root};tin[root]=low[root]=timer++;
        while(!stack.empty()){
            auto u=stack.back();
            if(cursor[u]<g[u].size()){
                auto [v,e]=g[u][cursor[u]++];if(e==parentEdge[u])continue;
                if(tin[v]!=kUnreachable){low[u]=std::min(low[u],tin[v]);continue;}
                parent[v]=u;parentEdge[v]=e;++children[u];tin[v]=low[v]=timer++;stack.push_back(v);
            }else{
                stack.pop_back();auto p=parent[u];
                if(p==kUnreachable){cut[u]=children[u]>1;continue;}
                low[p]=std::min(low[p],low[u]);
                if(low[u]>tin[p])out.bridges.push_back(parentEdge[u]);
                if(parent[p]!=kUnreachable&&low[u]>=tin[p])cut[p]=true;
            }
        }
    }
    std::sort(out.bridges.begin(),out.bridges.end());for(size_t i=0;i<n;++i)if(cut[i])out.articulation.push_back(i);return out;
}
}
