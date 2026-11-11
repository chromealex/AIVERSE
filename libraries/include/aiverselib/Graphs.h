#pragma once
#include <cstddef>
#include <optional>
#include <queue>
#include <functional>
#include <vector>
#include <limits>

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
}
