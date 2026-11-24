#pragma once
// Read-only DTO for the native authored room graph. No gameplay state or new route solver.
#include <aiverselib/systems/BuildingCirculationRuntime.h>
#include "engine/data/Json.h"
#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::studio {
namespace buildingTopologyDetail {
using namespace aiverse::lib::systems;
inline Json point(float x,float y,float z){
    Json out=Json::array();
    for(float v:{x,y,z}){
        if(!std::isfinite(v)||v<0.f||v>160.f)
            throw std::runtime_error("Invalid authored room coordinate");
        out.push(Json::number(double(v),3));
    }
    return out;
}
inline const char* kindName(BuildingConnectionKind kind){
    switch(kind){
        case BuildingConnectionKind::Passage:return "passage";
        case BuildingConnectionKind::Stair:return "stair";
        case BuildingConnectionKind::Elevator:return "elevator";
    }
    throw std::runtime_error("Invalid room connection kind");
}
struct Components {std::vector<int> labels;int count=0;};
inline Components components(const BuildingStructureGraph& graph,bool allowElevators){
    Components result;
    result.labels.assign(graph.rooms.size(),-1);
    BuildingRoutePolicy policy;
    policy.allowElevators=allowElevators;
    policy.requireAvailableLift=false; // authored topology only, no live controller claim
    for(size_t i=0;i<graph.rooms.size();++i){
        if(result.labels[i]>=0)continue;
        const int group=result.count++;
        result.labels[i]=group;
        for(size_t j=i+1;j<graph.rooms.size();++j){
            if(result.labels[j]>=0)continue;
            // Reuse the canonical native room planner; do not add another BFS.
            const auto route=planBuildingRoomRoute(graph,graph.rooms[i].id,graph.rooms[j].id,policy);
            if(route.reachable)result.labels[j]=group;
        }
    }
    return result;
}
inline std::string verticalSocket(const BuildingStructureGraph& graph,const BuildingGraphConnection& edge){
    if(edge.kind==BuildingConnectionKind::Passage)return {};
    // Same stable token emitted by the native functional graph generator for both records.
    if(edge.id.rfind("vertical/",0)!=0)throw std::runtime_error("Invalid vertical edge identity");
    const bool stair=edge.kind==BuildingConnectionKind::Stair;
    const auto socketId=std::string(stair?"stairs/":"lift/")+edge.id.substr(9);
    const auto it=std::find_if(graph.sockets.begin(),graph.sockets.end(),[&](const auto& socket){
        return socket.id==socketId&&socket.repeatIndex==edge.floor&&
               socket.kind==(stair?BuildingFeatureKind::Stairs:BuildingFeatureKind::Elevator);
    });
    if(it==graph.sockets.end())throw std::runtime_error("Missing native vertical socket");
    return it->id;
}
} // namespace buildingTopologyDetail

inline Json studioBuildingTopology(const lib::systems::BuildingStructureGraph& graph){
    using namespace buildingTopologyDetail;
    if(!graph||graph.rooms.empty()||graph.rooms.size()>128||
       graph.connections.size()>512||graph.sockets.size()>256)
        throw std::runtime_error("Invalid or oversized authored room graph");
    std::set<std::string> roomIds,edgeIds,socketIds;
    for(const auto& room:graph.rooms)
        if(room.id.empty()||!roomIds.insert(room.id).second)
            throw std::runtime_error("Duplicate or empty room ID");
    for(const auto& socket:graph.sockets)
        if(socket.id.empty()||!socketIds.insert(socket.id).second)
            throw std::runtime_error("Duplicate or empty socket ID");
    for(const auto& edge:graph.connections){
        if(edge.id.empty()||!edgeIds.insert(edge.id).second||
           edge.from==edge.to||!roomIds.count(edge.from)||!roomIds.count(edge.to))
            throw std::runtime_error("Invalid room connection identity");
        (void)kindName(edge.kind);
        if(!std::isfinite(edge.width)||!std::isfinite(edge.height)||
           edge.width<0.001f||edge.height<0.001f||edge.width>160.f||edge.height>160.f)
            throw std::runtime_error("Invalid room connection extent");
    }
    const auto all=components(graph,true),without=components(graph,false);
    Json result=Json::object(),rooms=Json::array(),edges=Json::array();
    result.set("version",Json::number(int64_t(1)));
    result.set("components",Json::number(int64_t(all.count)));
    result.set("withoutElevatorsComponents",Json::number(int64_t(without.count)));
    for(size_t i=0;i<graph.rooms.size();++i){
        const auto& r=graph.rooms[i];
        if(!(r.x0<r.x1&&r.y0<r.y1&&r.z0<r.z1))
            throw std::runtime_error("Invalid room bounds");
        Json row=Json::object();
        row.set("id",Json::string(r.id));
        row.set("floor",Json::number(int64_t(r.floor)));
        row.set("x",Json::number(int64_t(r.x)));
        row.set("z",Json::number(int64_t(r.z)));
        row.set("min",point(r.x0,r.y0,r.z0));
        row.set("max",point(r.x1,r.y1,r.z1));
        row.set("component",Json::number(int64_t(all.labels[i])));
        row.set("withoutElevatorsComponent",Json::number(int64_t(without.labels[i])));
        rooms.push(std::move(row));
    }
    for(const auto& e:graph.connections){
        Json row=Json::object();
        row.set("id",Json::string(e.id));
        row.set("from",Json::string(e.from));
        row.set("to",Json::string(e.to));
        row.set("floor",Json::number(int64_t(e.floor)));
        row.set("kind",Json::string(kindName(e.kind)));
        row.set("position",point(e.x,e.y,e.z));
        row.set("width",Json::number(double(e.width),3));
        row.set("height",Json::number(double(e.height),3));
        const auto socket=verticalSocket(graph,e);
        if(!socket.empty())row.set("socketId",Json::string(socket));
        edges.push(std::move(row));
    }
    result.set("rooms",std::move(rooms));
    result.set("connections",std::move(edges));
    return result;
}
} // namespace aiverse::studio
