#pragma once
// Moved from libraries/include/aiverselib/Graphs.h (2026-11-21, docs/architecture-layers.md L1); the old include still works.
#include <cstddef>
#include <optional>
#include <queue>
#include <functional>
#include <vector>
#include <limits>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <tuple>
#include <numeric>

namespace aiverse::lib {
using Graph = std::vector<std::vector<std::size_t>>;
inline constexpr std::size_t kUnreachable=std::numeric_limits<std::size_t>::max();
struct BfsResult { std::vector<std::size_t> distance,parent; };
inline bool validGraph(const Graph& edges) {
    for (const auto& row:edges) for (auto v:row) if (v>=edges.size()) return false;
    return true;
}
inline std::optional<BfsResult> bfs(const Graph& edges,std::size_t start) {
    if (start>=edges.size() || !validGraph(edges)) return std::nullopt;
    BfsResult result{std::vector<std::size_t>(edges.size(),kUnreachable),std::vector<std::size_t>(edges.size(),kUnreachable)};
    std::vector<std::size_t> queue{start}; result.distance[start]=0;
    for (std::size_t head=0;head<queue.size();++head) {
        const auto u=queue[head];
        for (auto v:edges[u]) if (result.distance[v]==kUnreachable) {
            result.distance[v]=result.distance[u]+1; result.parent[v]=u; queue.push_back(v);
        }
    }
    return result;
}
// Nullopt means malformed edges or a cycle; empty graph has an empty ordering.
inline std::optional<std::vector<std::size_t>> topological(const Graph& edges) {
    if (!validGraph(edges)) return std::nullopt;
    std::vector<std::size_t> degree(edges.size()),result;
    for (const auto& row:edges) for (auto v:row) ++degree[v];
    std::priority_queue<std::size_t,std::vector<std::size_t>,std::greater<std::size_t>> ready;
    for (std::size_t i=0;i<degree.size();++i) if (degree[i]==0) ready.push(i);
    while (!ready.empty()) {
        auto u=ready.top(); ready.pop(); result.push_back(u);
        for (auto v:edges[u]) if (--degree[v]==0) ready.push(v);
    }
    if (result.size()!=edges.size()) return std::nullopt;
    return result;
}

struct WeightedEdge {std::size_t to;std::uint64_t cost;};
using WeightedGraph=std::vector<std::vector<WeightedEdge>>;
inline constexpr auto kInfiniteCost=std::numeric_limits<std::uint64_t>::max();
struct WeightedPaths {std::vector<std::uint64_t> distance;std::vector<std::size_t> parent;};
inline std::optional<WeightedPaths> dijkstra(const WeightedGraph& g,std::size_t start){
    if(start>=g.size())return {};
    for(const auto& row:g)for(auto e:row)if(e.to>=g.size()||e.cost==kInfiniteCost)return {};
    WeightedPaths out{std::vector<std::uint64_t>(g.size(),kInfiniteCost),std::vector<std::size_t>(g.size(),kUnreachable)};
    using Item=std::pair<std::uint64_t,std::size_t>;
    std::priority_queue<Item,std::vector<Item>,std::greater<Item>> q;
    out.distance[start]=0;q.emplace(0,start);
    while(!q.empty()){
        auto [d,u]=q.top();q.pop();if(d!=out.distance[u])continue;
        for(auto e:g[u]){
            if(e.cost>=kInfiniteCost-d)continue;
            auto next=d+e.cost;
            if(next<out.distance[e.to]){out.distance[e.to]=next;out.parent[e.to]=u;q.emplace(next,e.to);}
        }
    }return out;
}
// Multi-source shortest paths with initial nonnegative costs. Equal-cost
// paths choose the smallest stable source ID, including across zero-cost
// cycles. A vertex's best label dominates every weaker label along any
// common suffix. No parent tree is promised for this owner-labelled query.
struct WeightedSeed {std::size_t vertex;std::uint64_t cost,sourceId;};
struct MultiSourcePaths {
    std::vector<std::uint64_t> distance,sourceId;
};
inline std::optional<MultiSourcePaths> multiSourceDijkstra(
        const WeightedGraph& g,const std::vector<WeightedSeed>& seeds,
        std::uint64_t maxCost=kInfiniteCost-1){
    if(maxCost==kInfiniteCost)return {};
    for(const auto& row:g)for(auto e:row)
        if(e.to>=g.size()||e.cost==kInfiniteCost)return {};
    for(auto seed:seeds)if(seed.vertex>=g.size()||seed.cost>maxCost)return {};
    MultiSourcePaths out{std::vector<std::uint64_t>(g.size(),kInfiniteCost),
                         std::vector<std::uint64_t>(g.size(),0)};
    using Item=std::tuple<std::uint64_t,std::uint64_t,std::size_t>;
    std::priority_queue<Item,std::vector<Item>,std::greater<Item>> q;
    auto offer=[&](std::size_t v,std::uint64_t cost,std::uint64_t source){
        if(cost<out.distance[v]||(cost==out.distance[v]&&source<out.sourceId[v])){
            out.distance[v]=cost;out.sourceId[v]=source;q.emplace(cost,source,v);
        }
    };
    for(auto seed:seeds)offer(seed.vertex,seed.cost,seed.sourceId);
    while(!q.empty()){
        const auto [cost,source,u]=q.top();q.pop();
        if(cost!=out.distance[u]||source!=out.sourceId[u])continue;
        for(auto e:g[u])if(e.cost<=maxCost-cost)
            offer(e.to,cost+e.cost,source);
    }
    return out;
}
inline std::optional<std::vector<std::size_t>> graphPath(const WeightedPaths& p,std::size_t start,std::size_t goal){
    if(p.distance.size()!=p.parent.size()||start>=p.parent.size()||goal>=p.parent.size()||p.distance[goal]==kInfiniteCost)return {};
    std::vector<std::size_t> out;
    for(auto u=goal;out.size()<p.parent.size();u=p.parent[u]){
        if(u>=p.parent.size())return {};
        out.push_back(u);if(u==start){std::reverse(out.begin(),out.end());return out;}
    }return {};
}
inline std::optional<std::vector<std::size_t>> dfs(const Graph& g,std::size_t start){
    if(start>=g.size()||!validGraph(g))return {};
    std::vector<std::size_t> order,stack{start};std::vector<bool> seen(g.size());
    while(!stack.empty()){
        auto u=stack.back();stack.pop_back();if(seen[u])continue;seen[u]=true;order.push_back(u);
        for(auto it=g[u].rbegin();it!=g[u].rend();++it)if(!seen[*it])stack.push_back(*it);
    }return order;
}
// Weak components for directed graphs (ignore edge direction).
inline std::optional<std::vector<std::size_t>> weakComponents(const Graph& g){
    if(!validGraph(g))return {};
    Graph undirected=g;for(std::size_t u=0;u<g.size();++u)for(auto v:g[u])undirected[v].push_back(u);
    std::vector<std::size_t> labels(g.size(),kUnreachable),queue;
    for(std::size_t root=0;root<g.size();++root){
        if(labels[root]!=kUnreachable)continue;
        queue.clear();queue.push_back(root);labels[root]=root;
        for(std::size_t i=0;i<queue.size();++i)for(auto v:undirected[queue[i]])if(labels[v]==kUnreachable){labels[v]=root;queue.push_back(v);}
    }return labels;
}

 // A* with admissible heuristic (zero at goal); reopen improved vertices.
template<class Heuristic>
inline std::optional<WeightedPaths> aStar(const WeightedGraph& g,size_t start,size_t goal,Heuristic heuristic){
    if(start>=g.size()||goal>=g.size()||heuristic(goal)!=0)return {};
    for(const auto& row:g)for(auto e:row)if(e.to>=g.size()||e.cost==kInfiniteCost)return {};
    WeightedPaths out{std::vector<uint64_t>(g.size(),kInfiniteCost),std::vector<size_t>(g.size(),kUnreachable)};
    using Item=std::tuple<uint64_t,uint64_t,size_t>;
    std::priority_queue<Item,std::vector<Item>,std::greater<Item>> q;
    auto push=[&](size_t u,uint64_t d){uint64_t h=heuristic(u);q.emplace(h>kInfiniteCost-d?kInfiniteCost:d+h,d,u);};
    out.distance[start]=0;push(start,0);
    while(!q.empty()){
        auto [priority,d,u]=q.top();q.pop();(void)priority;
        if(d!=out.distance[u])continue;
        if(u==goal)return out;
        for(auto e:g[u])if(e.cost<kInfiniteCost-d&&d+e.cost<out.distance[e.to]){
            out.distance[e.to]=d+e.cost;out.parent[e.to]=u;push(e.to,d+e.cost);
        }
    }return out;
}
// Iterative Kosaraju, canonical component label = minimum vertex.
inline std::optional<std::vector<size_t>> stronglyConnected(const Graph& g){
    if(!validGraph(g))return {};
    Graph reverse(g.size());for(size_t u=0;u<g.size();++u)for(auto v:g[u])reverse[v].push_back(u);
    std::vector<bool> seen(g.size());std::vector<size_t> finish;
    for(size_t root=0;root<g.size();++root){
        if(seen[root])continue;
        std::vector<std::pair<size_t,size_t>> stack{{root,0}};seen[root]=true;
        while(!stack.empty()){
            auto& [u,i]=stack.back();
            if(i==g[u].size()){finish.push_back(u);stack.pop_back();continue;}
            auto v=g[u][i++];if(!seen[v]){seen[v]=true;stack.emplace_back(v,0);}
        }
    }
    std::vector<size_t> labels(g.size(),kUnreachable);
    for(auto it=finish.rbegin();it!=finish.rend();++it){
        auto root=*it;if(labels[root]!=kUnreachable)continue;
        std::vector<size_t> group{root};labels[root]=root;
        for(size_t i=0;i<group.size();++i)for(auto v:reverse[group[i]])if(labels[v]==kUnreachable){labels[v]=root;group.push_back(v);}
        auto label=*std::min_element(group.begin(),group.end());for(auto v:group)labels[v]=label;
    }return labels;
}
struct UndirectedEdge {size_t a,b;uint64_t cost;};
struct SpanningForest {std::vector<UndirectedEdge> edges;uint64_t cost=0;size_t components=0;};
inline std::optional<SpanningForest> minimumSpanningForest(size_t n,std::vector<UndirectedEdge> edges){
    for(auto& e:edges){if(e.a>=n||e.b>=n)return {};if(e.a>e.b)std::swap(e.a,e.b);}
    std::sort(edges.begin(),edges.end(),[](auto a,auto b){return std::tie(a.cost,a.a,a.b)<std::tie(b.cost,b.a,b.b);});
    std::vector<size_t> parent(n),rank(n);std::iota(parent.begin(),parent.end(),0);
    auto root=[&](size_t u){while(parent[u]!=u){parent[u]=parent[parent[u]];u=parent[u];}return u;};
    SpanningForest out;out.components=n;
    for(auto e:edges){auto a=root(e.a),b=root(e.b);if(a==b)continue;
        if(e.cost>kInfiniteCost-out.cost)return {};
        if(rank[a]<rank[b])std::swap(a,b);parent[b]=a;if(rank[a]==rank[b])++rank[a];
        out.edges.push_back(e);out.cost+=e.cost;--out.components;
    }return out;
}
}
