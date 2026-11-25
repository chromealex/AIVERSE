#pragma once
// Shared AIVERSE navigation algorithms over any deterministic directed graph.
// Genre-independent: graphs own walkability/edges, solvers own searches.
// Fixed integer costs; deterministic tie breaks by node ID.
// Can be used by caves, 3D volumes and future MapGrid graph adapters.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::nav {
struct GraphEdge { uint32_t to=0, cost=0; };
struct GraphRoute {
    std::vector<uint32_t> nodes;
    uint32_t cost=0;
    bool reached=false;
};
struct GraphFlow {
    std::vector<uint32_t> values;
    std::vector<uint32_t> next;
    static constexpr uint32_t kUnreachable=UINT32_MAX;
};
namespace graph_detail {
inline uint32_t addCost(uint32_t a,uint32_t b)noexcept {
    return b>UINT32_MAX-a?UINT32_MAX:a+b;
}
struct QueueNode {
    uint32_t priority=0,actual=0,node=0;
};
struct QueueOrder {
    bool operator()(QueueNode a,QueueNode b)const noexcept {
        if(a.priority!=b.priority)return a.priority>b.priority;
        if(a.actual!=b.actual)return a.actual>b.actual;
        return a.node>b.node;
    }
};
}
// Graph contract: size(), passable(node), neighbors(node,vector<GraphEdge>&).
// Heuristic(node,goal) must be admissible for optimal A*; use zero for Dijkstra.
template<class Graph,class Heuristic>
GraphRoute findGraphAStar(const Graph& graph,uint32_t start,uint32_t goal,
                         uint32_t maxVisits,Heuristic heuristic) {
    const size_t count=graph.size();
    if(count==0||count>1048576||!maxVisits||start>=count||goal>=count||
       !graph.passable(start)||!graph.passable(goal))return {};
    if(start==goal)return {{start},0,true};
    std::vector<uint32_t> distance(count,UINT32_MAX);
    std::vector<uint32_t> previous(count,UINT32_MAX);
    std::priority_queue<graph_detail::QueueNode,
        std::vector<graph_detail::QueueNode>,graph_detail::QueueOrder> queue;
    distance[start]=0;
    queue.push({uint32_t(std::min<uint64_t>(heuristic(start,goal),UINT32_MAX)),
                0,start});
    std::vector<GraphEdge> edges;
    uint32_t visits=0;
    while(!queue.empty()&&visits<maxVisits) {
        const auto item=queue.top();queue.pop();
        if(item.actual!=distance[item.node])continue;
        if(item.node==goal)break;
        ++visits;
        graph.neighbors(item.node,edges);
        for(const auto edge:edges) {
            if(edge.to>=count||edge.cost==0)
                throw std::invalid_argument("AIVERSE graph malformed edge");
            const uint32_t candidate=graph_detail::addCost(
                item.actual,edge.cost);
            if(candidate==UINT32_MAX||candidate>=distance[edge.to])continue;
            distance[edge.to]=candidate;
            previous[edge.to]=item.node;
            const uint32_t estimate=uint32_t(std::min<uint64_t>(
                heuristic(edge.to,goal),UINT32_MAX));
            queue.push({graph_detail::addCost(candidate,estimate),
                        candidate,edge.to});
        }
    }
    if(distance[goal]==UINT32_MAX)return {};
    GraphRoute route;route.reached=true;route.cost=distance[goal];
    for(uint32_t node=goal;node!=UINT32_MAX;node=previous[node]){
        route.nodes.push_back(node);
        if(node==start)break;
    }
    if(route.nodes.empty()||route.nodes.back()!=start)return {};
    std::reverse(route.nodes.begin(),route.nodes.end());
    return route;
}
template<class Graph>
GraphRoute findGraphDijkstra(const Graph& graph,uint32_t start,uint32_t goal,
                             uint32_t maxVisits) {
    return findGraphAStar(graph,start,goal,maxVisits,
        [](uint32_t,uint32_t)->uint32_t{return 0;});
}
// Multi-goal reverse flow field on a directed graph. To be correct for
// asymmetric/one-way links we build an explicit reverse adjacency list.
// The memory and node limits are the caller's bounded-data contract.
template<class Graph>
GraphFlow buildGraphFlow(const Graph& graph,
                         const std::vector<uint32_t>& goals,
                         uint32_t maxVisits=65536) {
    const size_t count=graph.size();
    if(!count||count>1048576||!maxVisits)
        throw std::invalid_argument("AIVERSE flow graph budget");
    std::vector<std::vector<GraphEdge>> reverse(count);
    std::vector<GraphEdge> edges;
    for(uint32_t node=0;node<count;++node) {
        if(!graph.passable(node))continue;
        graph.neighbors(node,edges);
        for(const auto edge:edges) {
            if(edge.to>=count||!edge.cost)
                throw std::invalid_argument("AIVERSE flow malformed edge");
            reverse[edge.to].push_back({node,edge.cost});
        }
    }
    GraphFlow result;
    result.values.assign(count,UINT32_MAX);
    result.next.assign(count,UINT32_MAX);
    std::priority_queue<graph_detail::QueueNode,
        std::vector<graph_detail::QueueNode>,graph_detail::QueueOrder> queue;
    for(uint32_t goal:goals){
        if(goal>=count||!graph.passable(goal))continue;
        result.values[goal]=0;
        queue.push({0,0,goal});
    }
    uint32_t visits=0;
    while(!queue.empty()&&visits<maxVisits){
        const auto item=queue.top();queue.pop();
        if(item.actual!=result.values[item.node])continue;
        ++visits;
        for(const auto edge:reverse[item.node]){
            const auto cost=graph_detail::addCost(item.actual,edge.cost);
            if(cost==UINT32_MAX)continue;
            if(cost<result.values[edge.to] ||
               (cost==result.values[edge.to]&&
                item.node<result.next[edge.to])){
                result.values[edge.to]=cost;
                result.next[edge.to]=item.node;
                queue.push({cost,cost,edge.to});
            }
        }
    }
    return result;
}
} // namespace aiverse::nav
