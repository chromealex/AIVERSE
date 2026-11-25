#pragma once
#include "Graphs.h"
namespace aiverse::lib {
struct FlowEdge {size_t from,to;uint64_t capacity;int64_t cost=0;};
struct FlowResult {uint64_t flow=0;int64_t cost=0;std::vector<uint64_t> edgeFlow;bool budgetExhausted=false;};
enum class FlowMode {Maximum,MinimumCost};
// Successive shortest augmenting paths. MinimumCost requires no negative
// residual cycle reachable from source; rejects such inputs explicitly.
// Costs and capacities bounded to keep portable signed arithmetic safe.
inline std::optional<FlowResult> graphFlow(size_t n,const std::vector<FlowEdge>& edges,size_t source,size_t sink,
    uint64_t limit=1000000000,FlowMode mode=FlowMode::Maximum,size_t budget=100000){
    if(n>100000||edges.size()>1000000||source>=n||sink>=n||source==sink||limit>1000000000||!budget)return {};
    struct Arc {size_t to,rev;uint64_t cap;int64_t cost;};
    std::vector<std::vector<Arc>> g(n);
    std::vector<std::pair<size_t,size_t>> refs;
    for(auto e:edges){
        if(e.from>=n||e.to>=n||e.from==e.to||e.capacity>1000000000||e.cost<-1000000||e.cost>1000000)return {};
        auto a=g[e.from].size(),b=g[e.to].size();refs.emplace_back(e.from,a);
        auto c=mode==FlowMode::Maximum?0:e.cost;
        g[e.from].push_back({e.to,b,e.capacity,c});g[e.to].push_back({e.from,a,0,-c});
    }
    FlowResult out;
    constexpr int64_t inf=std::numeric_limits<int64_t>::max();
    while(out.flow<limit){
        if(!budget--){out.budgetExhausted=true;break;}
        std::vector<size_t> parent(n,kUnreachable),arc(n);
        std::vector<int64_t> d(n,inf);d[source]=0;
        if(mode==FlowMode::Maximum){
            std::vector<size_t> q{source};
            for(size_t i=0;i<q.size()&&d[sink]==inf;++i){auto u=q[i];for(size_t j=0;j<g[u].size();++j){auto a=g[u][j];if(a.cap&&d[a.to]==inf){d[a.to]=d[u]+1;parent[a.to]=u;arc[a.to]=j;q.push_back(a.to);}}}
        }else{
            for(size_t pass=0;pass<n;++pass){
                bool changed=false;
                for(size_t u=0;u<n;++u)if(d[u]!=inf)for(size_t j=0;j<g[u].size();++j){
                    auto a=g[u][j];if(!a.cap)continue;
                    if((a.cost>0&&d[u]>inf-a.cost)||(a.cost<0&&d[u]<std::numeric_limits<int64_t>::min()-a.cost))return {};
                    if(d[u]+a.cost<d[a.to]){d[a.to]=d[u]+a.cost;parent[a.to]=u;arc[a.to]=j;changed=true;}
                }
                if(!changed)break;if(pass+1==n)return {};
            }
        }
        if(parent[sink]==kUnreachable)break;
        uint64_t amount=limit-out.flow;size_t hops=0;
        for(auto v=sink;v!=source;v=parent[v]){if(++hops>=n||parent[v]==kUnreachable)return {};amount=std::min(amount,g[parent[v]][arc[v]].cap);}
        if(mode==FlowMode::MinimumCost){
            auto a=int64_t(amount),c=d[sink];
            if(c>inf/a||c<std::numeric_limits<int64_t>::min()/a)return {};
            auto delta=a*c;
            if((delta>0&&out.cost>inf-delta)||(delta<0&&out.cost<std::numeric_limits<int64_t>::min()-delta))return {};
            out.cost+=delta;
        }
        for(auto v=sink;v!=source;v=parent[v]){auto u=parent[v];auto& a=g[u][arc[v]];a.cap-=amount;g[v][a.rev].cap+=amount;}
        out.flow+=amount;
    }
    for(size_t i=0;i<edges.size();++i){auto [u,j]=refs[i];out.edgeFlow.push_back(edges[i].capacity-g[u][j].cap);}
    return out;
}
}
