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
}
