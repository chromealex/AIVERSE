#pragma once
#include "../Graphs.h"
#include "engine/world/SpatialWorld.h"
#include <algorithm>
#include <optional>
namespace aiverse::lib::bots {
struct Waypoint {Vec3 point{};bool cover=false;std::vector<size_t> links;};
using Waypoints=std::vector<Waypoint>;
inline bool valid(const Waypoints& nodes) {
    if(nodes.size()>512)return false;
    size_t edgeCount=0;
    for(const auto& n:nodes){
        edgeCount+=n.links.size();if(edgeCount>4096)return false;
        if(n.point.x< -100000_fx||n.point.x>100000_fx||n.point.y< -100000_fx||n.point.y>100000_fx||n.point.z< -100000_fx||n.point.z>100000_fx)return false;
        for(auto link:n.links)if(link>=nodes.size())return false;
    }return true;
}
// Conservative standing-body sweep, with a skin above floors. Flat authored routes only.
inline bool clearPath(const world::SpatialWorld& world,Vec3 from,Vec3 to,Fixed radius,Fixed height) {
    const auto skin=Fixed::fromRatio(1,100);auto half=Vec3{radius,height/2-skin,radius};
    return !world.cast(from+Vec3{{},height/2,{}},to-from,half).hit;
}
inline std::optional<Vec3> waypointStep(const world::SpatialWorld& world,const Waypoints& nodes,Vec3 from,Vec3 goal,Fixed radius,Fixed height,size_t budget=2048) {
    auto clear=[&](Vec3 a,Vec3 b){if(!budget)return false;--budget;return clearPath(world,a,b,radius,height);};
    if(clear(from,goal))return goal;
    if(nodes.empty()||!valid(nodes))return {};
    // Virtual start/end nodes reuse deterministic BFS; links are checked against current collision.
    Graph graph(nodes.size()+2);const auto start=nodes.size(),end=start+1;
    for(size_t i=0;i<nodes.size();++i){
        if(!budget)return {};
        if(clear(from,nodes[i].point))graph[start].push_back(i);
        if(clear(nodes[i].point,goal))graph[i].push_back(end);
        auto links=nodes[i].links;std::sort(links.begin(),links.end());
        for(auto link:links)if(clear(nodes[i].point,nodes[link].point))graph[i].push_back(link);
    }
    auto result=bfs(graph,start);if(!result||result->distance[end]==kUnreachable)return {};
    auto next=end;while(result->parent[next]!=start)next=result->parent[next];
    return next==end?std::optional<Vec3>(goal):std::optional<Vec3>(nodes[next].point);
}
}
